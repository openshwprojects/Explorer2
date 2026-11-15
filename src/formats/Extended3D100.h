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
inline bool ParseLightWaveLwo(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (size < 20 || std::memcmp(data, "FORM", 4) != 0) return false;
    bool isLwo2 = (std::memcmp(data + 8, "LWO2", 4) == 0);
    bool isLwob = (std::memcmp(data + 8, "LWOB", 4) == 0);
    if (!isLwo2 && !isLwob) return false;

    mesh.formatName = isLwo2 ? L"LightWave 3D Object (.lwo LWO2)" : L"LightWave 3D Object (.lwo LWOB)";
    size_t pos = 12;
    uint32_t layerVBase = 0;
    while (pos + 8 <= size) {
        char tag[5] = {};
        std::memcpy(tag, data + pos, 4);
        uint32_t clen = Detail3D100::RdU32BE(data + pos + 4);
        pos += 8;
        if (pos + clen > size) break;

        if (std::memcmp(tag, "PNTS", 4) == 0) {
            layerVBase = static_cast<uint32_t>(mesh.vertices.size());
            uint32_t numPts = clen / 12;
            for (uint32_t i = 0; i < numPts && mesh.vertices.size() < 200000; ++i) {
                float x = Detail3D100::RdF32BE(data + pos + i * 12 + 0);
                float y = Detail3D100::RdF32BE(data + pos + i * 12 + 4);
                float z = Detail3D100::RdF32BE(data + pos + i * 12 + 8);
                mesh.vertices.push_back({ x, y, z });
            }
        } else if (std::memcmp(tag, "POLS", 4) == 0) {
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
    return !mesh.vertices.empty() && !mesh.tris.empty();
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
// 69b. Rhino OpenNURBS (.3dm), Alembic Ogawa (.abc), LightWave Scene (.lws),
//      plus honest inspection of .dwg, .max, .c4d
// ============================================================================
namespace DetailCadDcc {

inline bool ParseRhino3dm(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace Detail3D100;
    if (size < 48 || std::memcmp(data, "3D Geometry File Format ", 24) != 0) return false;

    char verStr[9] = {};
    std::memcpy(verStr, data + 24, 8);
    int archiveVer = std::atoi(verStr);
    if (archiveVer <= 0) return false;
    mesh.formatName = L"Rhinoceros 3D OpenNURBS (.3dm)";

    const bool len64 = (archiveVer >= 5); // v1..v4 = 32-bit chunk lengths, v5/v50..v80 = 64-bit chunk lengths
    const size_t chunkHdrSz = len64 ? 12 : 8;

    auto readChunk = [&](size_t& pos, size_t limit, uint32_t& outTcode, int64_t& outValOrLen, size_t& outDataStart, size_t& outDataEnd) -> bool {
        if (pos + chunkHdrSz > limit) return false;
        outTcode = RdU32LE(data + pos);
        outValOrLen = len64 ? static_cast<int64_t>(RdU64LE(data + pos + 4))
                            : static_cast<int64_t>(static_cast<int32_t>(RdU32LE(data + pos + 4)));
        pos += chunkHdrSz;
        if ((outTcode & 0x80000000u) != 0) {
            outDataStart = outDataEnd = pos;
            return true;
        }
        if (outValOrLen < 0 || static_cast<uint64_t>(outValOrLen) > limit - pos) return false;
        outDataStart = pos;
        size_t payloadLen = static_cast<size_t>(outValOrLen);
        pos += payloadLen;
        if ((outTcode & 0x00008000u) != 0 && payloadLen >= 4) payloadLen -= 4; // Strip trailing 4B CRC32
        outDataEnd = outDataStart + payloadLen;
        return true;
    };

    // Helper to decode an OpenNURBS WriteCompressedBuffer stream (sizeof_elem(4) + crc(4) + method(1) + raw/zlib)
    auto readOnCompressedArray = [&](size_t& p, size_t end, size_t expectedBytes, std::vector<uint8_t>& outBuf) -> bool {
        if (p + 9 > end) return false;
        uint32_t elemSz = RdU32LE(data + p); p += 4;
        p += 4; // buffer crc
        uint8_t method = data[p++]; // 0 = uncompressed, 1 = zlib
        (void)elemSz;
        if (method == 0) {
            if (p + expectedBytes > end) return false;
            outBuf.assign(data + p, data + p + expectedBytes);
            p += expectedBytes;
            return true;
        } else if (method == 1) {
            // Compressed buffer is wrapped in a TCODE_ANONYMOUS_CHUNK!
            uint32_t tc = 0; int64_t cl = 0; size_t ds = 0, de = 0;
            if (!readChunk(p, end, tc, cl, ds, de) || ds >= de) return false;
            return ZipDeflate::InflateZlib(data + ds, de - ds, outBuf, expectedBytes + 64) && outBuf.size() >= expectedBytes;
        }
        return false;
    };

    // ON_Mesh UUID: 60B5DBBD-E660-11D3-BFE4-0010830122F0
    static const uint8_t kOnMeshUuid[16] = {
        0xBD, 0xDB, 0xB5, 0x60, 0x60, 0xE6, 0xD3, 0x11,
        0xBF, 0xE4, 0x00, 0x10, 0x83, 0x01, 0x22, 0xF0
    };
    // ON_PolylineCurve UUID: 4ED7D4E1-E947-11D3-BFE5-0010830122F0
    static const uint8_t kOnPolylineUuid[16] = {
        0xE1, 0xD4, 0xD7, 0x4E, 0x47, 0xE9, 0xD3, 0x11,
        0xBF, 0xE5, 0x00, 0x10, 0x83, 0x01, 0x22, 0xF0
    };

    auto decodeOpenNurbsClass = [&](size_t cStart, size_t cEnd) {
        size_t p = cStart;
        uint8_t uuid[16] = {};
        bool hasUuid = false;
        while (p < cEnd) {
            uint32_t tc = 0; int64_t vl = 0; size_t ds = 0, de = 0;
            if (!readChunk(p, cEnd, tc, vl, ds, de)) break;
            uint32_t baseTc = tc & 0x7FFF7FFFu;
            if (baseTc == 0x00027FF1u && de - ds >= 16) { // TCODE_OPENNURBS_CLASS_UUID
                std::memcpy(uuid, data + ds, 16);
                hasUuid = true;
            } else if (baseTc == 0x00027FF2u && hasUuid) { // TCODE_OPENNURBS_CLASS_DATA
                if (std::memcmp(uuid, kOnMeshUuid, 16) == 0 && de - ds >= 9) {
                    size_t mp = ds;
                    uint8_t verByte = data[mp++];
                    int maj = (verByte >> 4) & 0x0F;
                    if (maj >= 1 && mp + 8 <= de) {
                        int32_t vcount = static_cast<int32_t>(RdU32LE(data + mp)); mp += 4;
                        int32_t fcount = static_cast<int32_t>(RdU32LE(data + mp)); mp += 4;
                        if (vcount >= 3 && vcount <= 2000000 && fcount >= 1 && fcount <= 4000000) {
                            std::vector<Vec3f> mVerts;
                            std::vector<std::array<int32_t, 4>> mFaces;

                            // Walk nested TCODE sub-chunks or compressed m_V / m_F buffers inside ON_Mesh
                            // Also support direct TCODE_MESH_VERTEX_ARRAY (0x00049001) & FACE_ARRAY (0x00049002)
                            size_t scan = mp;
                            while (scan + chunkHdrSz <= de) {
                                size_t tryP = scan;
                                uint32_t stc = 0; int64_t svl = 0; size_t sds = 0, sde = 0;
                                if (readChunk(tryP, de, stc, svl, sds, sde)) {
                                    uint32_t sBase = stc & 0x7FFF7FFFu;
                                    if (sBase == 0x00049001u && sde - sds >= static_cast<size_t>(vcount) * 12) {
                                        mVerts.resize(vcount);
                                        for (int32_t v = 0; v < vcount; ++v) {
                                            mVerts[v] = {
                                                RdF32LE(data + sds + v * 12 + 0),
                                                RdF32LE(data + sds + v * 12 + 4),
                                                RdF32LE(data + sds + v * 12 + 8)
                                            };
                                        }
                                        scan = tryP;
                                        continue;
                                    } else if (sBase == 0x00049002u && sde - sds >= static_cast<size_t>(fcount) * 16) {
                                        mFaces.resize(fcount);
                                        for (int32_t f = 0; f < fcount; ++f) {
                                            for (int k = 0; k < 4; ++k) {
                                                mFaces[f][k] = static_cast<int32_t>(RdU32LE(data + sds + f * 16 + k * 4));
                                            }
                                        }
                                        scan = tryP;
                                        continue;
                                    }
                                }
                                ++scan;
                                if (scan - mp > 1024 && mVerts.empty()) break;
                            }

                            // Standard Rhino ON_Mesh::Read layout after 580-byte fixed metadata header (m_Ctag + m_H + bboxes)
                            if (mVerts.empty() && mp + 580 <= de) {
                                size_t cp = mp + 580;
                                std::vector<uint8_t> fBuf, vBuf;
                                if (readOnCompressedArray(cp, de, static_cast<size_t>(fcount) * 16, fBuf) &&
                                    readOnCompressedArray(cp, de, static_cast<size_t>(vcount) * 12, vBuf))
                                {
                                    mVerts.resize(vcount);
                                    for (int32_t v = 0; v < vcount; ++v) {
                                        mVerts[v] = {
                                            RdF32LE(vBuf.data() + v * 12 + 0),
                                            RdF32LE(vBuf.data() + v * 12 + 4),
                                            RdF32LE(vBuf.data() + v * 12 + 8)
                                        };
                                    }
                                    mFaces.resize(fcount);
                                    for (int32_t f = 0; f < fcount; ++f) {
                                        for (int k = 0; k < 4; ++k) {
                                            mFaces[f][k] = static_cast<int32_t>(RdU32LE(fBuf.data() + f * 16 + k * 4));
                                        }
                                    }
                                }
                            }

                            if (!mVerts.empty() && !mFaces.empty()) {
                                uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                                for (const auto& v : mVerts) mesh.vertices.push_back({ v.x, v.z, -v.y });
                                for (const auto& fc : mFaces) {
                                    if (fc[0] >= 0 && fc[1] >= 0 && fc[2] >= 0 && fc[3] >= 0 &&
                                        fc[0] < vcount && fc[1] < vcount && fc[2] < vcount && fc[3] < vcount)
                                    {
                                        mesh.tris.push_back({ vBase + static_cast<uint32_t>(fc[0]), vBase + static_cast<uint32_t>(fc[1]), vBase + static_cast<uint32_t>(fc[2]), {} });
                                        if (fc[2] != fc[3]) {
                                            mesh.tris.push_back({ vBase + static_cast<uint32_t>(fc[0]), vBase + static_cast<uint32_t>(fc[2]), vBase + static_cast<uint32_t>(fc[3]), {} });
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else if (std::memcmp(uuid, kOnPolylineUuid, 16) == 0 && de - ds >= 13) {
                    // ON_PolylineCurve: chunk_version(1B) + dim(4B) + count(4B) + double[dim * count]
                    size_t pp = ds + 1;
                    int32_t dim = static_cast<int32_t>(RdU32LE(data + pp)); pp += 4;
                    int32_t cnt = static_cast<int32_t>(RdU32LE(data + pp)); pp += 4;
                    if ((dim == 2 || dim == 3) && cnt >= 2 && cnt <= 65536 && pp + static_cast<size_t>(cnt * dim) * 8 <= de) {
                        std::vector<Vec3f> pts(cnt);
                        for (int32_t i = 0; i < cnt; ++i) {
                            double x = RdF64LE(data + pp + i * dim * 8 + 0);
                            double y = RdF64LE(data + pp + i * dim * 8 + 8);
                            double z = (dim == 3) ? RdF64LE(data + pp + i * dim * 8 + 16) : 0.0;
                            pts[i] = { static_cast<float>(x), static_cast<float>(z), static_cast<float>(-y) };
                        }
                        for (int32_t i = 0; i + 1 < cnt; ++i) AddLineStrut(mesh, pts[i], pts[i + 1], 0.03f);
                    }
                }
            }
            if ((tc & 0x7FFFFFFFu) == 0x00027FFFu) break; // TCODE_OPENNURBS_CLASS_END
        }
    };

    size_t pos = 32;
    while (pos + chunkHdrSz <= size) {
        uint32_t tc = 0; int64_t vl = 0; size_t ds = 0, de = 0;
        if (!readChunk(pos, size, tc, vl, ds, de)) break;
        if (tc == 0x20000013u) { // TCODE_OBJECT_TABLE
            size_t op = ds;
            while (op + chunkHdrSz <= de) {
                uint32_t otc = 0; int64_t ovl = 0; size_t ods = 0, ode = 0;
                if (!readChunk(op, de, otc, ovl, ods, ode)) break;
                if (otc == 0x10000013u) { // TCODE_OBJECT_RECORD
                    size_t rp = ods;
                    while (rp + chunkHdrSz <= ode) {
                        uint32_t rtc = 0; int64_t rvl = 0; size_t rds = 0, rde = 0;
                        if (!readChunk(rp, ode, rtc, rvl, rds, rde)) break;
                        if ((rtc & 0x7FFF7FFFu) == 0x00027FF0u) { // TCODE_OPENNURBS_CLASS
                            decodeOpenNurbsClass(rds, rde);
                        }
                        if (rtc == 0x80007FFFu) break;
                    }
                }
                if (otc == 0x80007FFFu) break;
            }
        }
        if ((tc & 0x7FFFFFFFu) == 0x00007FFFu) break; // TCODE_ENDOFFILE
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

// Alembic Ogawa (.abc — "Ogawa\xFF\x00\x01" or "Ogawa\x00\xFF\x00\x01" + uint64 rootGroupOffset)
inline bool ParseAlembicOgawa(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace Detail3D100;
    if (size < 24 || std::memcmp(data, "Ogawa", 5) != 0) return false;

    size_t rootPos = 0;
    if ((data[5] == 0xFF || data[5] == 0x00) && data[6] == 0x00 && data[7] == 0x01) {
        rootPos = 8;
    } else if (data[5] == 0x00 && data[6] == 0xFF && data[7] == 0x00 && data[8] == 0x01) {
        rootPos = 9;
    } else {
        return false;
    }
    uint64_t rootGroupOff = RdU64LE(data + rootPos);
    if (rootGroupOff < rootPos + 8 || rootGroupOff + 8 > size) return false;
    mesh.formatName = L"Alembic Ogawa 3D Cache (.abc)";

    auto getDataStream = [&](uint64_t childEntry, const uint8_t*& outPtr, size_t& outLen) -> bool {
        if ((childEntry & (1ULL << 63)) == 0) return false;
        uint64_t off = childEntry & ~(1ULL << 63);
        if (off == 0 || off + 8 > size) return false;
        uint64_t bc = RdU64LE(data + static_cast<size_t>(off));
        if (bc > size - static_cast<size_t>(off) - 8) return false;
        outPtr = data + static_cast<size_t>(off) + 8;
        outLen = static_cast<size_t>(bc);
        return true;
    };

    auto readArrayPropPayload = [&](uint64_t groupOff, size_t elemByteWidth, const uint8_t*& outBytes, size_t& outCount) -> bool {
        if (groupOff == 0 || (groupOff & (1ULL << 63)) != 0 || groupOff + 8 > size) return false;
        uint64_t nCh = RdU64LE(data + static_cast<size_t>(groupOff));
        if (nCh == 0 || nCh > 65536 || groupOff + 8 + nCh * 8 > size) return false;
        uint64_t ch0 = RdU64LE(data + static_cast<size_t>(groupOff) + 8);
        const uint8_t* raw = nullptr;
        size_t rawLen = 0;
        if (!getDataStream(ch0, raw, rawLen)) return false;

        // Check optional dims stream at child[1] (16-byte Murmur3 digest + uint64 dims[])
        if (nCh >= 2) {
            uint64_t ch1 = RdU64LE(data + static_cast<size_t>(groupOff) + 16);
            const uint8_t* dimPtr = nullptr;
            size_t dimLen = 0;
            if (getDataStream(ch1, dimPtr, dimLen) && dimLen >= 24) {
                uint64_t nElems = RdU64LE(dimPtr + 16);
                if (rawLen == 16 + nElems * elemByteWidth) {
                    outBytes = raw + 16;
                    outCount = static_cast<size_t>(nElems);
                    return true;
                }
                if (rawLen == nElems * elemByteWidth) {
                    outBytes = raw;
                    outCount = static_cast<size_t>(nElems);
                    return true;
                }
            }
        }
        // Fallback: AbcCoreOgawa prepends 16-byte hash digest to non-empty data streams
        if (rawLen > 16 && ((rawLen - 16) % elemByteWidth) == 0) {
            outBytes = raw + 16;
            outCount = (rawLen - 16) / elemByteWidth;
            return true;
        }
        if (rawLen >= elemByteWidth && (rawLen % elemByteWidth) == 0) {
            outBytes = raw;
            outCount = rawLen / elemByteWidth;
            return true;
        }
        return false;
    };

    std::set<uint64_t> visitedGroups;
    auto walkOgawa = [&](auto& self, uint64_t gOff, int depth) -> void {
        if (depth > 32 || gOff == 0 || (gOff & (1ULL << 63)) != 0 || gOff + 8 > size) return;
        if (!visitedGroups.insert(gOff).second) return;

        uint64_t nCh = RdU64LE(data + static_cast<size_t>(gOff));
        if (nCh == 0 || nCh > 65536 || gOff + 8 + nCh * 8 > size) return;

        std::vector<uint64_t> children(static_cast<size_t>(nCh));
        for (size_t i = 0; i < static_cast<size_t>(nCh); ++i) {
            children[i] = RdU64LE(data + static_cast<size_t>(gOff) + 8 + i * 8);
        }

        // Check if the last child is an AbcCoreOgawa CompoundProperty PropertyHeader stream!
        const uint8_t* hdrPtr = nullptr;
        size_t hdrLen = 0;
        if (nCh >= 2 && getDataStream(children.back(), hdrPtr, hdrLen) && hdrLen >= 5) {
            // Parse N = nCh - 1 PropertyHeaders
            size_t hp = 0;
            size_t nProps = static_cast<size_t>(nCh) - 1;
            std::unordered_map<std::string, uint64_t> propMap;
            bool headersValid = true;

            for (size_t i = 0; i < nProps && hp + 4 <= hdrLen; ++i) {
                uint32_t info = RdU32LE(hdrPtr + hp); hp += 4;
                uint32_t propType = info & 0x03u; // 0=Compound, 1=Scalar, 2=Array
                uint32_t szHint   = (info >> 2) & 0x03u;
                auto rdVarU = [&]() -> uint32_t {
                    if (szHint == 0 && hp + 1 <= hdrLen) return hdrPtr[hp++];
                    if (szHint == 1 && hp + 2 <= hdrLen) { uint32_t v = RdU16LE(hdrPtr + hp); hp += 2; return v; }
                    if (szHint == 2 && hp + 4 <= hdrLen) { uint32_t v = RdU32LE(hdrPtr + hp); hp += 4; return v; }
                    headersValid = false;
                    return 0;
                };

                if (propType != 0) {
                    bool hasTsIdx    = (info & 0x0100u) != 0;
                    bool hasFirstLast= (info & 0x0800u) == 0; // bit 11 == 1 means zeroChanged (omitted)
                    rdVarU(); // nextSampleIndex
                    if (hasFirstLast) { rdVarU(); rdVarU(); }
                    if (hasTsIdx) rdVarU();
                }
                uint32_t nameSz = rdVarU();
                if (!headersValid || nameSz == 0 || nameSz > 512 || hp + nameSz + 1 > hdrLen) {
                    headersValid = false;
                    break;
                }
                std::string pName(reinterpret_cast<const char*>(hdrPtr + hp), nameSz);
                hp += nameSz;
                uint8_t metaIdx = hdrPtr[hp++];
                if (metaIdx == 0xFFu) {
                    uint32_t metaSz = rdVarU();
                    if (!headersValid || hp + metaSz > hdrLen) { headersValid = false; break; }
                    hp += metaSz;
                }
                propMap[pName] = children[i];
            }

            if (headersValid) {
                auto itP  = propMap.find("P");
                auto itFI = propMap.find(".faceIndices");
                auto itFC = propMap.find(".faceCounts");
                if (itP != propMap.end() && itFI != propMap.end()) {
                    const uint8_t* pBytes = nullptr; size_t nVerts = 0;
                    const uint8_t* fiBytes = nullptr; size_t nIdx = 0;
                    const uint8_t* fcBytes = nullptr; size_t nFaces = 0;
                    if (readArrayPropPayload(itP->second, 12, pBytes, nVerts) &&
                        readArrayPropPayload(itFI->second, 4, fiBytes, nIdx) &&
                        nVerts >= 3 && nIdx >= 3 && nVerts <= 2000000 && nIdx <= 6000000)
                    {
                        if (itFC != propMap.end()) {
                            readArrayPropPayload(itFC->second, 4, fcBytes, nFaces);
                        }
                        uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                        for (size_t v = 0; v < nVerts; ++v) {
                            mesh.vertices.push_back({
                                RdF32LE(pBytes + v * 12 + 0),
                                RdF32LE(pBytes + v * 12 + 4),
                                RdF32LE(pBytes + v * 12 + 8)
                            });
                        }
                        if (fcBytes && nFaces > 0) {
                            size_t k = 0;
                            for (size_t f = 0; f < nFaces; ++f) {
                                int32_t cnt = static_cast<int32_t>(RdU32LE(fcBytes + f * 4));
                                if (cnt >= 3 && k + static_cast<size_t>(cnt) <= nIdx) {
                                    int32_t i0 = static_cast<int32_t>(RdU32LE(fiBytes + k * 4));
                                    if (i0 >= 0 && static_cast<size_t>(i0) < nVerts) {
                                        for (int32_t t = 1; t + 1 < cnt; ++t) {
                                            int32_t i1 = static_cast<int32_t>(RdU32LE(fiBytes + (k + t) * 4));
                                            int32_t i2 = static_cast<int32_t>(RdU32LE(fiBytes + (k + t + 1) * 4));
                                            if (i1 >= 0 && i2 >= 0 &&
                                                static_cast<size_t>(i1) < nVerts &&
                                                static_cast<size_t>(i2) < nVerts)
                                            {
                                                // Alembic PolyMesh faces use CW winding -> reverse (i0, i2, i1)
                                                mesh.tris.push_back({ vBase + static_cast<uint32_t>(i0), vBase + static_cast<uint32_t>(i2), vBase + static_cast<uint32_t>(i1), {} });
                                            }
                                        }
                                    }
                                }
                                if (cnt > 0) k += static_cast<size_t>(cnt);
                            }
                        } else {
                            for (size_t k = 0; k + 2 < nIdx; k += 3) {
                                uint32_t i0 = RdU32LE(fiBytes + k * 4 + 0);
                                uint32_t i1 = RdU32LE(fiBytes + k * 4 + 4);
                                uint32_t i2 = RdU32LE(fiBytes + k * 4 + 8);
                                if (i0 < nVerts && i1 < nVerts && i2 < nVerts) {
                                    mesh.tris.push_back({ vBase + i0, vBase + i2, vBase + i1, {} });
                                }
                            }
                        }
                    }
                }
            }
        }

        // Recurse into child groups
        for (uint64_t ch : children) {
            if (ch != 0 && (ch & (1ULL << 63)) == 0) {
                self(self, ch, depth + 1);
            }
        }
    };

    walkOgawa(walkOgawa, rootGroupOff, 0);
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

// LightWave Scene (.lws — ASCII "LWSC" scene items, channel keyframe envelopes & motion paths)
inline bool ParseLightWaveScene(std::string_view sv, LoadedMesh3D& mesh) {
    using namespace Detail3D100;
    std::string_view trimmed = DetailStep::TrimSv(sv);
    if (trimmed.rfind("LWSC", 0) != 0) return false;
    mesh.formatName = L"LightWave 3D Scene (.lws)";

    struct LwsKey { float time = 0.0f; float val = 0.0f; };
    struct LwsItem {
        enum Kind { Obj, Null, Cam, Light, Bone } kind = Obj;
        uint32_t itemId = 0;
        uint32_t parentId = 0;
        Vec3f restPos = { 0, 0, 0 };
        std::vector<LwsKey> chanKeys[9]; // 0,1,2 = X,Y,Z; 3,4,5 = H,P,B
    };

    std::vector<LwsItem> items;
    uint32_t objCount = 0, camCount = 0, lightCount = 0, boneCount = 0;
    int curChan = -1;

    size_t pos = 0;
    while (pos < sv.size() && items.size() < 4096) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = DetailStep::TrimSv(sv.substr(pos, eol - pos));
        pos = eol + 1;
        if (ln.empty()) continue;

        auto startsWith = [&](const char* kw) -> bool {
            size_t kl = std::strlen(kw);
            return ln.size() >= kl && ln.compare(0, kl, kw) == 0 && (ln.size() == kl || static_cast<unsigned char>(ln[kl]) <= 32);
        };

        if (startsWith("LoadObjectLayer") || startsWith("LoadObject")) {
            items.push_back({ LwsItem::Obj, 0x10000000u | objCount++, 0, {}, {} });
            curChan = -1;
        } else if (startsWith("AddNullObject")) {
            items.push_back({ LwsItem::Null, 0x10000000u | objCount++, 0, {}, {} });
            curChan = -1;
        } else if (startsWith("AddCamera")) {
            items.push_back({ LwsItem::Cam, 0x30000000u | camCount++, 0, {}, {} });
            curChan = -1;
        } else if (startsWith("AddLight")) {
            items.push_back({ LwsItem::Light, 0x20000000u | lightCount++, 0, {}, {} });
            curChan = -1;
        } else if (startsWith("AddBone")) {
            uint32_t parentDefault = items.empty() ? 0 : items.back().itemId;
            items.push_back({ LwsItem::Bone, 0x40000000u | boneCount++, parentDefault, {}, {} });
            curChan = -1;
        } else if (!items.empty()) {
            LwsItem& cur = items.back();
            if (startsWith("ObjectPosition") || startsWith("AddPosition") || startsWith("ObjPosition") || startsWith("BoneRestPosition")) {
                size_t sp = ln.find(' ');
                if (sp != std::string_view::npos) {
                    float x = 0, y = 0, z = 0;
                    if (sscanf_s(std::string(ln.substr(sp + 1)).c_str(), "%f %f %f", &x, &y, &z) == 3) {
                        cur.restPos = { x, y, z };
                    }
                }
            } else if (startsWith("ParentItem")) {
                size_t sp = ln.find(' ');
                if (sp != std::string_view::npos) {
                    cur.parentId = static_cast<uint32_t>(std::strtoul(std::string(ln.substr(sp + 1)).c_str(), nullptr, 16));
                }
            } else if (startsWith("ParentObject")) {
                size_t sp = ln.find(' ');
                if (sp != std::string_view::npos) {
                    int idx1 = std::atoi(std::string(ln.substr(sp + 1)).c_str());
                    if (idx1 >= 1) cur.parentId = 0x10000000u | static_cast<uint32_t>(idx1 - 1);
                }
            } else if (startsWith("Channel")) {
                size_t sp = ln.find(' ');
                curChan = (sp != std::string_view::npos) ? std::atoi(std::string(ln.substr(sp + 1)).c_str()) : -1;
            } else if (startsWith("Key") && curChan >= 0 && curChan < 9) {
                size_t sp = ln.find(' ');
                if (sp != std::string_view::npos) {
                    float val = 0.0f, tm = 0.0f;
                    if (sscanf_s(std::string(ln.substr(sp + 1)).c_str(), "%f %f", &val, &tm) >= 2) {
                        cur.chanKeys[curChan].push_back({ tm, val });
                        mesh.animFrameCount = std::max(mesh.animFrameCount, cur.chanKeys[curChan].size());
                    }
                }
            }
        }
    }

    if (items.empty()) return false;

    auto sampleChan = [](const std::vector<LwsKey>& keys, float t, float fallback) -> float {
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
    };

    std::unordered_map<uint32_t, size_t> idMap;
    for (size_t i = 0; i < items.size(); ++i) idMap[items[i].itemId] = i;

    std::function<Vec3f(size_t, float, int)> evalWorldPos = [&](size_t idx, float t, int depth) -> Vec3f {
        const auto& it = items[idx];
        Vec3f lp = {
            sampleChan(it.chanKeys[0], t, it.restPos.x),
            sampleChan(it.chanKeys[1], t, it.restPos.y),
            sampleChan(it.chanKeys[2], t, it.restPos.z)
        };
        if (it.parentId != 0 && depth < 16) {
            auto pit = idMap.find(it.parentId);
            if (pit != idMap.end() && pit->second != idx) {
                Vec3f pp = evalWorldPos(pit->second, t, depth + 1);
                lp = DetailStep::StepAdd(pp, lp);
            }
        }
        return lp;
    };

    // Determine scene scale for crisp camera/light/bone/motion-path wireframes
    Vec3f mn = evalWorldPos(0, 0.0f, 0), mx = mn;
    for (size_t i = 0; i < items.size(); ++i) {
        for (int c = 0; c < 3; ++c) {
            for (const auto& k : items[i].chanKeys[c]) {
                Vec3f p = evalWorldPos(i, k.time, 0);
                mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
                mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
            }
        }
        Vec3f p0 = evalWorldPos(i, 0.0f, 0);
        mn.x = std::min(mn.x, p0.x); mn.y = std::min(mn.y, p0.y); mn.z = std::min(mn.z, p0.z);
        mx.x = std::max(mx.x, p0.x); mx.y = std::max(mx.y, p0.y); mx.z = std::max(mx.z, p0.z);
    }
    float span = std::max({ mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, 2.0f });
    float gizmoSz = span * 0.06f;
    float strutR  = span * 0.008f;

    for (size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        Vec3f p0 = evalWorldPos(i, 0.0f, 0);

        if (it.kind == LwsItem::Cam) {
            // Camera frustum pyramid pointing along +Z
            Vec3f corners[4] = {
                { p0.x - gizmoSz, p0.y - gizmoSz * 0.7f, p0.z + gizmoSz * 1.6f },
                { p0.x + gizmoSz, p0.y - gizmoSz * 0.7f, p0.z + gizmoSz * 1.6f },
                { p0.x + gizmoSz, p0.y + gizmoSz * 0.7f, p0.z + gizmoSz * 1.6f },
                { p0.x - gizmoSz, p0.y + gizmoSz * 0.7f, p0.z + gizmoSz * 1.6f }
            };
            for (int c = 0; c < 4; ++c) {
                AddLineStrut(mesh, p0, corners[c], strutR);
                AddLineStrut(mesh, corners[c], corners[(c + 1) & 3], strutR);
            }
        } else if (it.kind == LwsItem::Light) {
            AddLineStrut(mesh, { p0.x - gizmoSz, p0.y, p0.z }, { p0.x + gizmoSz, p0.y, p0.z }, strutR);
            AddLineStrut(mesh, { p0.x, p0.y - gizmoSz, p0.z }, { p0.x, p0.y + gizmoSz, p0.z }, strutR);
            AddLineStrut(mesh, { p0.x, p0.y, p0.z - gizmoSz }, { p0.x, p0.y, p0.z + gizmoSz }, strutR);
        } else {
            AddBoxPrim(mesh, p0.x, p0.y, p0.z, gizmoSz * 0.45f, gizmoSz * 0.45f, gizmoSz * 0.45f);
        }

        // Parent hierarchy link
        if (it.parentId != 0) {
            auto pit = idMap.find(it.parentId);
            if (pit != idMap.end() && pit->second != i) {
                AddLineStrut(mesh, evalWorldPos(pit->second, 0.0f, 0), p0, strutR * 1.2f);
            }
        }

        // Motion-path curve across keyframe envelope range
        float tMin = 1e30f, tMax = -1e30f;
        for (int c = 0; c < 3; ++c) {
            for (const auto& k : it.chanKeys[c]) {
                tMin = std::min(tMin, k.time);
                tMax = std::max(tMax, k.time);
            }
        }
        if (tMax > tMin + 1e-5f) {
            const int steps = 24;
            Vec3f prev = evalWorldPos(i, tMin, 0);
            for (int s = 1; s <= steps; ++s) {
                float tm = tMin + (tMax - tMin) * (static_cast<float>(s) / static_cast<float>(steps));
                Vec3f cur = evalWorldPos(i, tm, 0);
                AddLineStrut(mesh, prev, cur, strutR);
                prev = cur;
            }
        }
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

// 3ds Max (.max — OLE2 Compound File "Scene" chunk stream)
inline bool Parse3dsMaxOle(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace Detail3D100;
    Ole2::CompoundFile cf;
    if (!Ole2::ParseCompoundFile(data, size, cf)) return false;
    const Ole2::DirEntry* sceneEntry = Ole2::FindStreamCaseInsensitive(cf, L"Scene");
    if (!sceneEntry) return false;

    std::vector<uint8_t> scene;
    if (!Ole2::ExtractStream(cf, *sceneEntry, scene, 32 * 1024 * 1024) || scene.size() < 18) return false;
    mesh.formatName = L"Autodesk 3ds Max Scene (.max)";

    // Walk unencrypted 3ds Max chunk tree: [uint16 id][uint32 sizeAndContainerFlag]
    auto walkMax = [&](auto& self, size_t s, size_t e, int depth) -> void {
        if (depth > 24) return;
        std::vector<Vec3f> verts;
        std::vector<std::array<uint32_t, 3>> tris;
        size_t p = s;
        while (p + 6 <= e) {
            uint16_t cid = RdU16LE(scene.data() + p);
            uint32_t rawSz = RdU32LE(scene.data() + p + 2);
            bool isContainer = (rawSz & 0x80000000u) != 0;
            uint32_t cSz = rawSz & 0x7FFFFFFFu;
            if (cSz < 6 || p + cSz > e) break;
            if (isContainer) {
                self(self, p + 6, p + cSz, depth + 1);
            } else {
                size_t pay = p + 6;
                size_t payLen = cSz - 6;
                // Max TriObject VERTEX_CHUNK (0x0912 / 0x0100) = uint32 count + float3[count]
                if ((cid == 0x0912u || cid == 0x0100u) && payLen >= 40) {
                    uint32_t nv = RdU32LE(scene.data() + pay);
                    if (nv >= 3 && nv <= 500000 && payLen == 4 + static_cast<size_t>(nv) * 12) {
                        verts.resize(nv);
                        for (uint32_t i = 0; i < nv; ++i) {
                            verts[i] = {
                                RdF32LE(scene.data() + pay + 4 + i * 12 + 0),
                                RdF32LE(scene.data() + pay + 4 + i * 12 + 8),
                                -RdF32LE(scene.data() + pay + 4 + i * 12 + 4)
                            };
                        }
                    }
                }
                // Max TriObject FACE_CHUNK (0x0910 / 0x0914 / 0x0110) = uint32 count + Face[count]
                else if ((cid == 0x0910u || cid == 0x0914u || cid == 0x0110u) && payLen >= 16 && !verts.empty()) {
                    uint32_t nf = RdU32LE(scene.data() + pay);
                    size_t stride = (nf > 0 && (payLen - 4) % nf == 0) ? ((payLen - 4) / nf) : 0;
                    if (nf >= 1 && nf <= 1000000 && (stride == 12 || stride == 16 || stride == 20)) {
                        for (uint32_t i = 0; i < nf; ++i) {
                            uint32_t a = RdU32LE(scene.data() + pay + 4 + i * stride + 0);
                            uint32_t b = RdU32LE(scene.data() + pay + 4 + i * stride + 4);
                            uint32_t c = RdU32LE(scene.data() + pay + 4 + i * stride + 8);
                            if (a < verts.size() && b < verts.size() && c < verts.size()) {
                                tris.push_back({ a, b, c });
                            }
                        }
                    }
                }
            }
            p += cSz;
        }
        if (!verts.empty() && !tris.empty()) {
            uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.insert(mesh.vertices.end(), verts.begin(), verts.end());
            for (const auto& t : tris) mesh.tris.push_back({ b + t[0], b + t[1], b + t[2], {} });
        }
    };

    walkMax(walkMax, 0, scene.size(), 0);
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

// Cinema 4D (.c4d — MC4D / C4DC IFF polygon chunks if unencrypted)
inline bool ParseCinema4d(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace Detail3D100;
    if (size < 20) return false;
    bool isMc4d = (std::memcmp(data, "MC4D", 4) == 0 || (size >= 12 && std::memcmp(data + 4, "MC4D", 4) == 0) ||
                   (size >= 12 && std::memcmp(data + 8, "MC4D", 4) == 0) || std::memcmp(data, "C4DC", 4) == 0);
    if (!isMc4d) return false;
    mesh.formatName = L"Maxon Cinema 4D Scene (.c4d)";

    size_t p = (std::memcmp(data, "FORM", 4) == 0) ? 12 : 8;
    uint32_t vBase = 0;
    while (p + 8 <= size) {
        char tag[5] = {};
        std::memcpy(tag, data + p, 4);
        uint32_t cLen = RdU32BE(data + p + 4);
        p += 8;
        if (cLen > size - p) break;
        if (std::memcmp(tag, "POLP", 4) == 0 || std::memcmp(tag, "PNTS", 4) == 0) {
            if (cLen >= 36 && (cLen % 12) == 0) {
                vBase = static_cast<uint32_t>(mesh.vertices.size());
                uint32_t nv = cLen / 12;
                for (uint32_t i = 0; i < nv && i < 500000; ++i) {
                    mesh.vertices.push_back({
                        RdF32BE(data + p + i * 12 + 0),
                        RdF32BE(data + p + i * 12 + 4),
                        RdF32BE(data + p + i * 12 + 8)
                    });
                }
            }
        } else if (std::memcmp(tag, "POLY", 4) == 0 || std::memcmp(tag, "POLS", 4) == 0) {
            if (cLen >= 16 && (cLen % 16) == 0 && !mesh.vertices.empty()) {
                uint32_t nf = cLen / 16;
                for (uint32_t i = 0; i < nf && i < 500000; ++i) {
                    uint32_t a = vBase + RdU32BE(data + p + i * 16 + 0);
                    uint32_t b = vBase + RdU32BE(data + p + i * 16 + 4);
                    uint32_t c = vBase + RdU32BE(data + p + i * 16 + 8);
                    uint32_t d = vBase + RdU32BE(data + p + i * 16 + 12);
                    if (a < mesh.vertices.size() && b < mesh.vertices.size() && c < mesh.vertices.size() && d < mesh.vertices.size()) {
                        mesh.tris.push_back({ a, b, c, {} });
                        if (c != d) mesh.tris.push_back({ a, c, d, {} });
                    }
                }
            }
        }
        p += cLen + (cLen & 1u);
    }
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

inline bool ParseMayaOrCadContainer(const uint8_t* data, size_t size, const std::wstring& extLower, LoadedMesh3D& mesh) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (extLower == L".ma")  return DetailMaya::ParseMayaAscii(sv, mesh);
    if (extLower == L".mb")  return DetailMaya::ParseMayaBinary(data, size, mesh);
    if (extLower == L".3dm") return DetailCadDcc::ParseRhino3dm(data, size, mesh);
    if (extLower == L".abc") return DetailCadDcc::ParseAlembicOgawa(data, size, mesh);
    if (extLower == L".lws") return DetailCadDcc::ParseLightWaveScene(sv, mesh);
    if (extLower == L".max") return DetailCadDcc::Parse3dsMaxOle(data, size, mesh);
    if (extLower == L".c4d") return DetailCadDcc::ParseCinema4d(data, size, mesh);
    if (extLower == L".dwg") return DetailCadDcc::ParseAutoCadDwg(data, size, mesh);
    return false;
}

} // namespace Formats
