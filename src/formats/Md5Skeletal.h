#pragma once
// Md5Skeletal.h — id Tech 4 (Doom 3 / Quake 4 / Prey) Skeletal Mesh (.md5mesh) & Animation (.md5anim)

namespace Formats {

namespace Md5Detail {

struct Quat4f {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = -1.0f;
};

inline Quat4f BuildMd5Quat(float qx, float qy, float qz) {
    float t = 1.0f - (qx * qx + qy * qy + qz * qz);
    float qw = (t <= 0.0f) ? 0.0f : -std::sqrt(t);
    return { qx, qy, qz, qw };
}

inline Quat4f QuatNormalize(const Quat4f& q) {
    float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len > 1e-7f) {
        float inv = 1.0f / len;
        return { q.x * inv, q.y * inv, q.z * inv, q.w * inv };
    }
    return { 0.0f, 0.0f, 0.0f, -1.0f };
}

inline Quat4f QuatMul(const Quat4f& a, const Quat4f& b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}

inline Vec3f QuatRotate(const Quat4f& q, const Vec3f& v) {
    // Standard Hamilton rotation q * (0, v) * conj(q)
    float tx = 2.0f * (q.y * v.z - q.z * v.y);
    float ty = 2.0f * (q.z * v.x - q.x * v.z);
    float tz = 2.0f * (q.x * v.y - q.y * v.x);
    return {
        v.x + q.w * tx + (q.y * tz - q.z * ty),
        v.y + q.w * ty + (q.z * tx - q.x * tz),
        v.z + q.w * tz + (q.x * ty - q.y * tx)
    };
}

// Convert id Tech Z-up right-handed coords to Explorer2 Y-up viewport
inline Vec3f IdTechToViewport(const Vec3f& p) {
    return { p.x, p.z, -p.y };
}

struct Md5Joint {
    std::string name;
    int parent = -1;
    Vec3f pos = {};
    Quat4f orient = {};
};

struct Md5Lexer {
    std::string_view src;
    size_t pos = 0;

    void SkipWsAndComments() {
        while (pos < src.size()) {
            char c = src[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos;
                continue;
            }
            if (c == '/' && pos + 1 < src.size() && src[pos + 1] == '/') {
                pos += 2;
                while (pos < src.size() && src[pos] != '\n') ++pos;
                continue;
            }
            if (c == '/' && pos + 1 < src.size() && src[pos + 1] == '*') {
                pos += 2;
                while (pos + 1 < src.size() && !(src[pos] == '*' && src[pos + 1] == '/')) ++pos;
                if (pos + 1 < src.size()) pos += 2;
                else pos = src.size();
                continue;
            }
            break;
        }
    }

    bool Next(std::string_view& outTok, bool* wasQuoted = nullptr) {
        if (wasQuoted) *wasQuoted = false;
        SkipWsAndComments();
        if (pos >= src.size()) return false;
        char c = src[pos];
        if (c == '"') {
            if (wasQuoted) *wasQuoted = true;
            size_t start = ++pos;
            while (pos < src.size() && src[pos] != '"') ++pos;
            outTok = src.substr(start, pos - start);
            if (pos < src.size() && src[pos] == '"') ++pos;
            return true;
        }
        if (c == '{' || c == '}' || c == '(' || c == ')') {
            outTok = src.substr(pos, 1);
            ++pos;
            return true;
        }
        size_t start = pos;
        while (pos < src.size()) {
            char ch = src[pos];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
                ch == '{' || ch == '}' || ch == '(' || ch == ')' || ch == '"') {
                break;
            }
            if (ch == '/' && pos + 1 < src.size() && (src[pos + 1] == '/' || src[pos + 1] == '*')) {
                break;
            }
            ++pos;
        }
        outTok = src.substr(start, pos - start);
        return !outTok.empty();
    }

    bool Expect(std::string_view lit) {
        std::string_view tok;
        return Next(tok) && tok == lit;
    }

    bool NextInt(int& outVal) {
        std::string_view tok;
        if (!Next(tok) || tok.empty()) return false;
        char buf[64];
        if (tok.size() >= sizeof(buf)) return false;
        std::memcpy(buf, tok.data(), tok.size());
        buf[tok.size()] = '\0';
        char* endPtr = nullptr;
        long v = std::strtol(buf, &endPtr, 10);
        if (endPtr != buf + tok.size()) return false;
        outVal = static_cast<int>(v);
        return true;
    }

    bool NextFloat(float& outVal) {
        std::string_view tok;
        if (!Next(tok) || tok.empty()) return false;
        char buf[64];
        if (tok.size() >= sizeof(buf)) return false;
        std::memcpy(buf, tok.data(), tok.size());
        buf[tok.size()] = '\0';
        char* endPtr = nullptr;
        float v = std::strtof(buf, &endPtr);
        if (endPtr != buf + tok.size() || !std::isfinite(v)) return false;
        outVal = v;
        return true;
    }

    bool NextVec3Paren(Vec3f& v) {
        return Expect("(") && NextFloat(v.x) && NextFloat(v.y) && NextFloat(v.z) && Expect(")");
    }

    bool SkipBalancedBraces() {
        int depth = 1;
        std::string_view tok;
        bool quoted = false;
        while (depth > 0 && Next(tok, &quoted)) {
            if (!quoted) {
                if (tok == "{") ++depth;
                else if (tok == "}") --depth;
            }
        }
        return depth == 0;
    }
};

inline void AppendBoneOctahedron(LoadedMesh3D& mesh, const Vec3f& a, const Vec3f& b, float fallbackRadius) {
    Vec3f d = { b.x - a.x, b.y - a.y, b.z - a.z };
    float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len < 1e-5f) {
        float r = std::max(0.05f, fallbackRadius);
        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({ a.x,     a.y + r, a.z     });
        mesh.vertices.push_back({ a.x,     a.y - r, a.z     });
        mesh.vertices.push_back({ a.x + r, a.y,     a.z     });
        mesh.vertices.push_back({ a.x - r, a.y,     a.z     });
        mesh.vertices.push_back({ a.x,     a.y,     a.z + r });
        mesh.vertices.push_back({ a.x,     a.y,     a.z - r });
        mesh.tris.push_back({ base + 0, base + 4, base + 2, {} });
        mesh.tris.push_back({ base + 0, base + 2, base + 5, {} });
        mesh.tris.push_back({ base + 0, base + 5, base + 3, {} });
        mesh.tris.push_back({ base + 0, base + 3, base + 4, {} });
        mesh.tris.push_back({ base + 1, base + 2, base + 4, {} });
        mesh.tris.push_back({ base + 1, base + 5, base + 2, {} });
        mesh.tris.push_back({ base + 1, base + 3, base + 5, {} });
        mesh.tris.push_back({ base + 1, base + 4, base + 3, {} });
        return;
    }

    Vec3f dir = { d.x / len, d.y / len, d.z / len };
    Vec3f ref = (std::fabs(dir.y) < 0.9f) ? Vec3f{ 0.0f, 1.0f, 0.0f } : Vec3f{ 1.0f, 0.0f, 0.0f };
    Vec3f u = {
        dir.y * ref.z - dir.z * ref.y,
        dir.z * ref.x - dir.x * ref.z,
        dir.x * ref.y - dir.y * ref.x
    };
    float uLen = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
    if (uLen > 1e-6f) { u.x /= uLen; u.y /= uLen; u.z /= uLen; }
    Vec3f v = {
        dir.y * u.z - dir.z * u.y,
        dir.z * u.x - dir.x * u.z,
        dir.x * u.y - dir.y * u.x
    };

    float r = std::max(fallbackRadius * 0.35f, len * 0.11f);
    Vec3f waist = { a.x + d.x * 0.25f, a.y + d.y * 0.25f, a.z + d.z * 0.25f };
    uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(a);
    mesh.vertices.push_back(b);
    mesh.vertices.push_back({ waist.x + u.x * r, waist.y + u.y * r, waist.z + u.z * r });
    mesh.vertices.push_back({ waist.x - u.x * r, waist.y - u.y * r, waist.z - u.z * r });
    mesh.vertices.push_back({ waist.x + v.x * r, waist.y + v.y * r, waist.z + v.z * r });
    mesh.vertices.push_back({ waist.x - v.x * r, waist.y - v.y * r, waist.z - v.z * r });
    mesh.tris.push_back({ base + 0, base + 4, base + 2, {} });
    mesh.tris.push_back({ base + 0, base + 2, base + 5, {} });
    mesh.tris.push_back({ base + 0, base + 5, base + 3, {} });
    mesh.tris.push_back({ base + 0, base + 3, base + 4, {} });
    mesh.tris.push_back({ base + 1, base + 2, base + 4, {} });
    mesh.tris.push_back({ base + 1, base + 5, base + 2, {} });
    mesh.tris.push_back({ base + 1, base + 3, base + 5, {} });
    mesh.tris.push_back({ base + 1, base + 4, base + 3, {} });
}

struct Md5SubMeshRaw {
    struct Vert {
        int startWeight = 0;
        int countWeight = 0;
    };
    struct Weight {
        int joint = 0;
        float bias = 0.0f;
        Vec3f pos = {};
    };
    struct Tri {
        int i0 = 0, i1 = 0, i2 = 0;
    };
    std::vector<Vert> verts;
    std::vector<Weight> weights;
    std::vector<Tri> tris;
};

inline bool ParseMd5MeshRaw(std::string_view sv, std::vector<Md5Joint>& outJoints, std::vector<Md5SubMeshRaw>& outMeshes) {
    Md5Lexer lex{ sv, 0 };
    std::string_view tok;
    if (!lex.Next(tok) || tok != "MD5Version") return false;
    int version = 0;
    if (!lex.NextInt(version) || version != 10) return false;

    int declaredJoints = -1;
    int declaredMeshes = -1;

    while (lex.Next(tok)) {
        if (tok == "commandline") {
            std::string_view dummy;
            if (!lex.Next(dummy)) return false;
        } else if (tok == "numJoints") {
            if (!lex.NextInt(declaredJoints) || declaredJoints <= 0 || declaredJoints > 65536) return false;
        } else if (tok == "numMeshes") {
            if (!lex.NextInt(declaredMeshes) || declaredMeshes <= 0 || declaredMeshes > 16384) return false;
        } else if (tok == "joints") {
            if (!lex.Expect("{")) return false;
            while (lex.Next(tok)) {
                if (tok == "}") break;
                Md5Joint j;
                j.name = std::string(tok);
                Vec3f qxyz = {};
                if (!lex.NextInt(j.parent) || !lex.NextVec3Paren(j.pos) || !lex.NextVec3Paren(qxyz)) {
                    return false;
                }
                if (j.parent >= static_cast<int>(outJoints.size()) || j.parent < -1) return false;
                j.orient = BuildMd5Quat(qxyz.x, qxyz.y, qxyz.z);
                outJoints.push_back(std::move(j));
                if (outJoints.size() > 65536) return false;
            }
        } else if (tok == "mesh") {
            if (!lex.Expect("{")) return false;
            Md5SubMeshRaw sm;
            bool closed = false;
            while (lex.Next(tok)) {
                if (tok == "}") { closed = true; break; }
                if (tok == "shader") {
                    std::string_view sh;
                    if (!lex.Next(sh)) return false;
                } else if (tok == "numverts") {
                    int nv = 0;
                    if (!lex.NextInt(nv) || nv < 0 || nv > 2000000) return false;
                    sm.verts.assign(static_cast<size_t>(nv), {});
                } else if (tok == "vert") {
                    int vIdx = 0, sw = 0, cw = 0;
                    float u = 0.0f, v = 0.0f;
                    if (!lex.NextInt(vIdx) || !lex.Expect("(") || !lex.NextFloat(u) || !lex.NextFloat(v) ||
                        !lex.Expect(")") || !lex.NextInt(sw) || !lex.NextInt(cw)) {
                        return false;
                    }
                    if (vIdx < 0 || static_cast<size_t>(vIdx) >= sm.verts.size() || sw < 0 || cw <= 0) return false;
                    sm.verts[static_cast<size_t>(vIdx)] = { sw, cw };
                } else if (tok == "numtris") {
                    int nt = 0;
                    if (!lex.NextInt(nt) || nt < 0 || nt > 4000000) return false;
                    sm.tris.assign(static_cast<size_t>(nt), {});
                } else if (tok == "tri") {
                    int tIdx = 0, a = 0, b = 0, c = 0;
                    if (!lex.NextInt(tIdx) || !lex.NextInt(a) || !lex.NextInt(b) || !lex.NextInt(c)) return false;
                    if (tIdx < 0 || static_cast<size_t>(tIdx) >= sm.tris.size()) return false;
                    sm.tris[static_cast<size_t>(tIdx)] = { a, b, c };
                } else if (tok == "numweights") {
                    int nw = 0;
                    if (!lex.NextInt(nw) || nw < 0 || nw > 4000000) return false;
                    sm.weights.assign(static_cast<size_t>(nw), {});
                } else if (tok == "weight") {
                    int wIdx = 0, jIdx = 0;
                    float bias = 0.0f;
                    Vec3f wp = {};
                    if (!lex.NextInt(wIdx) || !lex.NextInt(jIdx) || !lex.NextFloat(bias) || !lex.NextVec3Paren(wp)) {
                        return false;
                    }
                    if (wIdx < 0 || static_cast<size_t>(wIdx) >= sm.weights.size()) return false;
                    sm.weights[static_cast<size_t>(wIdx)] = { jIdx, bias, wp };
                }
            }
            if (!closed) return false;
            outMeshes.push_back(std::move(sm));
        }
    }

    if (outJoints.empty() || outMeshes.empty()) return false;
    if (declaredJoints > 0 && static_cast<int>(outJoints.size()) != declaredJoints) return false;
    if (declaredMeshes > 0 && static_cast<int>(outMeshes.size()) != declaredMeshes) return false;
    return true;
}

inline bool SkinMd5SubMeshes(
    const std::vector<Md5Joint>& joints,
    const std::vector<Md5SubMeshRaw>& subMeshes,
    LoadedMesh3D& mesh)
{
    const int nJoints = static_cast<int>(joints.size());
    for (const auto& sm : subMeshes) {
        if (sm.verts.empty() || sm.tris.empty() || sm.weights.empty()) continue;
        const uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
        const int nVerts = static_cast<int>(sm.verts.size());
        const int nWeights = static_cast<int>(sm.weights.size());

        for (int vi = 0; vi < nVerts; ++vi) {
            const auto& v = sm.verts[static_cast<size_t>(vi)];
            if (v.countWeight <= 0 || v.startWeight < 0 || v.startWeight + v.countWeight > nWeights) {
                return false;
            }
            Vec3f skinned = { 0.0f, 0.0f, 0.0f };
            for (int k = 0; k < v.countWeight; ++k) {
                const auto& w = sm.weights[static_cast<size_t>(v.startWeight + k)];
                if (w.joint < 0 || w.joint >= nJoints) return false;
                const auto& jnt = joints[static_cast<size_t>(w.joint)];
                Vec3f rotPos = QuatRotate(jnt.orient, w.pos);
                skinned.x += (jnt.pos.x + rotPos.x) * w.bias;
                skinned.y += (jnt.pos.y + rotPos.y) * w.bias;
                skinned.z += (jnt.pos.z + rotPos.z) * w.bias;
            }
            mesh.vertices.push_back(IdTechToViewport(skinned));
        }

        for (const auto& t : sm.tris) {
            if (t.i0 < 0 || t.i0 >= nVerts || t.i1 < 0 || t.i1 >= nVerts || t.i2 < 0 || t.i2 >= nVerts) {
                return false;
            }
            // Doom 3 MD5 triangles use clockwise front-face winding in Z-up space; flip (i0, i2, i1) for CCW
            mesh.tris.push_back({
                baseV + static_cast<uint32_t>(t.i0),
                baseV + static_cast<uint32_t>(t.i2),
                baseV + static_cast<uint32_t>(t.i1),
                {}
            });
        }
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Md5Detail

inline bool ParseMd5Mesh(std::string_view sv, LoadedMesh3D& mesh) {
    std::vector<Md5Detail::Md5Joint> joints;
    std::vector<Md5Detail::Md5SubMeshRaw> subMeshes;
    if (!Md5Detail::ParseMd5MeshRaw(sv, joints, subMeshes)) return false;
    if (!Md5Detail::SkinMd5SubMeshes(joints, subMeshes, mesh)) return false;
    mesh.formatName = L"id Tech 4 MD5 Skeletal Mesh (.md5mesh)";
    mesh.animFrameCount = 1;
    return true;
}

inline bool ParseMd5Anim(std::string_view sv, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    using namespace Md5Detail;
    Md5Lexer lex{ sv, 0 };
    std::string_view tok;
    if (!lex.Next(tok) || tok != "MD5Version") return false;
    int version = 0;
    if (!lex.NextInt(version) || version != 10) return false;

    struct HierItem {
        std::string name;
        int parent = -1;
        int flags = 0;
        int startIndex = 0;
    };
    struct BaseItem {
        Vec3f pos = {};
        Vec3f qxyz = {};
    };

    int numFrames = -1;
    int numJoints = -1;
    int numAnimComps = -1;
    float frameRate = 24.0f;
    std::vector<HierItem> hierarchy;
    std::vector<BaseItem> baseframe;
    std::vector<float> firstFrameComps;
    bool gotFirstFrame = false;

    while (lex.Next(tok)) {
        if (tok == "commandline") {
            std::string_view dummy;
            if (!lex.Next(dummy)) return false;
        } else if (tok == "numFrames") {
            if (!lex.NextInt(numFrames) || numFrames <= 0 || numFrames > 100000) return false;
        } else if (tok == "numJoints") {
            if (!lex.NextInt(numJoints) || numJoints <= 0 || numJoints > 65536) return false;
        } else if (tok == "frameRate") {
            if (!lex.NextFloat(frameRate) || frameRate <= 0.0f) return false;
        } else if (tok == "numAnimatedComponents") {
            if (!lex.NextInt(numAnimComps) || numAnimComps < 0 || numAnimComps > 1000000) return false;
        } else if (tok == "hierarchy") {
            if (!lex.Expect("{")) return false;
            bool closed = false;
            while (lex.Next(tok)) {
                if (tok == "}") { closed = true; break; }
                HierItem h;
                h.name = std::string(tok);
                if (!lex.NextInt(h.parent) || !lex.NextInt(h.flags) || !lex.NextInt(h.startIndex)) return false;
                if (h.parent >= static_cast<int>(hierarchy.size()) || h.parent < -1) return false;
                hierarchy.push_back(std::move(h));
                if (hierarchy.size() > 65536) return false;
            }
            if (!closed) return false;
        } else if (tok == "bounds") {
            if (!lex.Expect("{") || !lex.SkipBalancedBraces()) return false;
        } else if (tok == "baseframe") {
            if (!lex.Expect("{")) return false;
            bool closed = false;
            while (lex.Next(tok)) {
                if (tok == "}") { closed = true; break; }
                if (tok != "(") return false;
                BaseItem b;
                if (!lex.NextFloat(b.pos.x) || !lex.NextFloat(b.pos.y) || !lex.NextFloat(b.pos.z) ||
                    !lex.Expect(")") || !lex.NextVec3Paren(b.qxyz)) {
                    return false;
                }
                baseframe.push_back(b);
                if (baseframe.size() > 65536) return false;
            }
            if (!closed) return false;
        } else if (tok == "frame") {
            int frameIdx = 0;
            if (!lex.NextInt(frameIdx) || !lex.Expect("{")) return false;
            if (!gotFirstFrame) {
                if (numAnimComps < 0) return false;
                firstFrameComps.resize(static_cast<size_t>(numAnimComps));
                for (int c = 0; c < numAnimComps; ++c) {
                    if (!lex.NextFloat(firstFrameComps[static_cast<size_t>(c)])) return false;
                }
                if (!lex.Expect("}")) return false;
                gotFirstFrame = true;
            } else {
                if (!lex.SkipBalancedBraces()) return false;
            }
        }
    }

    if (numJoints <= 0 || numFrames <= 0 || !gotFirstFrame) return false;
    if (static_cast<int>(hierarchy.size()) != numJoints || static_cast<int>(baseframe.size()) != numJoints) return false;

    std::vector<Md5Joint> posedJoints(static_cast<size_t>(numJoints));
    for (int i = 0; i < numJoints; ++i) {
        const auto& h = hierarchy[static_cast<size_t>(i)];
        Vec3f lp = baseframe[static_cast<size_t>(i)].pos;
        Vec3f lq = baseframe[static_cast<size_t>(i)].qxyz;
        int idx = h.startIndex;
        auto pullComp = [&](float& dst) -> bool {
            if (idx < 0 || static_cast<size_t>(idx) >= firstFrameComps.size()) return false;
            dst = firstFrameComps[static_cast<size_t>(idx++)];
            return true;
        };
        if ((h.flags & 1)  && !pullComp(lp.x)) return false;
        if ((h.flags & 2)  && !pullComp(lp.y)) return false;
        if ((h.flags & 4)  && !pullComp(lp.z)) return false;
        if ((h.flags & 8)  && !pullComp(lq.x)) return false;
        if ((h.flags & 16) && !pullComp(lq.y)) return false;
        if ((h.flags & 32) && !pullComp(lq.z)) return false;

        Quat4f localQuat = BuildMd5Quat(lq.x, lq.y, lq.z);
        Md5Joint& dst = posedJoints[static_cast<size_t>(i)];
        dst.name = h.name;
        dst.parent = h.parent;
        if (h.parent < 0) {
            dst.pos = lp;
            dst.orient = localQuat;
        } else {
            const Md5Joint& par = posedJoints[static_cast<size_t>(h.parent)];
            Vec3f rPos = QuatRotate(par.orient, lp);
            dst.pos = { par.pos.x + rPos.x, par.pos.y + rPos.y, par.pos.z + rPos.z };
            dst.orient = QuatNormalize(QuatMul(par.orient, localQuat));
        }
    }

    // Check if a companion .md5mesh exists in the same folder with identical joint count;
    // if found, skin the companion mesh in this animation's Frame 0 pose!
    bool skinnedCompanion = false;
    if (!fullPath.empty()) {
        std::wstring dir = PathUtils::GetParentPath(fullPath);
        if (!dir.empty()) {
            std::wstring searchPat = dir + L"\\*.md5mesh";
            WIN32_FIND_DATAW fd = {};
            HANDLE hFind = FindFirstFileW(searchPat.c_str(), &fd);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    std::wstring candPath = dir + L"\\" + fd.cFileName;
                    std::vector<uint8_t> candBytes;
                    if (PathUtils::ReadFileBytes(candPath, 32 * 1024 * 1024, candBytes) && !candBytes.empty()) {
                        std::string_view candSv(reinterpret_cast<const char*>(candBytes.data()), candBytes.size());
                        std::vector<Md5Joint> meshBindJoints;
                        std::vector<Md5SubMeshRaw> subMeshes;
                        if (ParseMd5MeshRaw(candSv, meshBindJoints, subMeshes) &&
                            meshBindJoints.size() == posedJoints.size())
                        {
                            bool rigMatches = true;
                            for (size_t j = 0; j < posedJoints.size(); ++j) {
                                if (meshBindJoints[j].parent != posedJoints[j].parent ||
                                    meshBindJoints[j].name != posedJoints[j].name) {
                                    rigMatches = false;
                                    break;
                                }
                            }
                            if (rigMatches && SkinMd5SubMeshes(posedJoints, subMeshes, mesh)) {
                                skinnedCompanion = true;
                                break;
                            }
                        }
                    }
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }
        }
    }

    if (!skinnedCompanion) {
        // Render articulated 3D octahedral bone skeleton of the posed animation frame
        float spanEst = 0.0f;
        for (int i = 0; i < numJoints; ++i) {
            int p = posedJoints[static_cast<size_t>(i)].parent;
            if (p >= 0) {
                Vec3f a = posedJoints[static_cast<size_t>(p)].pos;
                Vec3f b = posedJoints[static_cast<size_t>(i)].pos;
                float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
                spanEst = std::max(spanEst, std::sqrt(dx * dx + dy * dy + dz * dz));
            }
        }
        float fallbackR = std::max(0.25f, spanEst * 0.1f);
        int boneCount = 0;
        for (int i = 0; i < numJoints; ++i) {
            int p = posedJoints[static_cast<size_t>(i)].parent;
            if (p >= 0) {
                AppendBoneOctahedron(
                    mesh,
                    IdTechToViewport(posedJoints[static_cast<size_t>(p)].pos),
                    IdTechToViewport(posedJoints[static_cast<size_t>(i)].pos),
                    fallbackR);
                ++boneCount;
            }
        }
        if (boneCount == 0 && numJoints == 1) {
            Vec3f rp = IdTechToViewport(posedJoints[0].pos);
            AppendBoneOctahedron(mesh, rp, rp, 1.0f);
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"id Tech 4 MD5 Skeletal Animation (.md5anim)";
    mesh.animFrameCount = static_cast<size_t>(numFrames);
    return true;
}

} // namespace Formats
