#pragma once
// AcclaimSkelCommon.h — Shared DRY Acclaim Skeleton (.asf) & Motion Capture (.amc) kinematics
// Used by AcclaimAsf.h and AcclaimAmc.h.

namespace Formats::AcclaimSkelCommon {

struct AsfBone {
    std::string name;
    int parentIdx = -1;
    Vec3f dir = { 0.0f, 1.0f, 0.0f };
    float length = 0.0f;
    float axisAng[3] = { 0.0f, 0.0f, 0.0f };
    std::string axisOrder = "XYZ";
    std::vector<std::string> dofs;
    bool hasAmcPose = false;
    std::vector<float> amcVals;
};

struct AsfSkeleton {
    bool angleRadians = false;
    float lengthScale = 1.0f;
    Vec3f rootPos = { 0.0f, 0.0f, 0.0f };
    Vec3f rootRot = { 0.0f, 0.0f, 0.0f };
    std::string rootAxisOrder = "XYZ";
    std::vector<std::string> rootOrder = { "TX", "TY", "TZ", "RX", "RY", "RZ" };
    std::vector<AsfBone> bones;
    std::unordered_map<std::string, size_t> boneMap;
};

inline void Mat3Identity(float m[9]) {
    for (int i = 0; i < 9; ++i) m[i] = (i % 4 == 0) ? 1.0f : 0.0f;
}

inline void Mat3Mul(const float a[9], const float b[9], float r[9]) {
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            r[row * 3 + col] = a[row * 3 + 0] * b[0 * 3 + col] +
                               a[row * 3 + 1] * b[1 * 3 + col] +
                               a[row * 3 + 2] * b[2 * 3 + col];
        }
    }
}

inline void Mat3Transpose(const float a[9], float t[9]) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            t[r * 3 + c] = a[c * 3 + r];
}

inline void Mat3RotAxis(char ax, float rad, float m[9]) {
    Mat3Identity(m);
    const float c = std::cos(rad), s = std::sin(rad);
    if (ax == 'X' || ax == 'x') {
        m[4] = c; m[5] = -s; m[7] = s; m[8] = c;
    } else if (ax == 'Y' || ax == 'y') {
        m[0] = c; m[2] = s; m[6] = -s; m[8] = c;
    } else {
        m[0] = c; m[1] = -s; m[3] = s; m[4] = c;
    }
}

inline void BuildAcclaimEuler3(float rx, float ry, float rz, std::string_view order, float outM[9]) {
    auto ang = [&](char c) -> float {
        if (c == 'X' || c == 'x') return rx;
        if (c == 'Y' || c == 'y') return ry;
        return rz;
    };
    const char c0 = (order.size() >= 1) ? order[0] : 'X';
    const char c1 = (order.size() >= 2) ? order[1] : 'Y';
    const char c2 = (order.size() >= 3) ? order[2] : 'Z';
    float r0[9], r1[9], r2[9], r21[9];
    Mat3RotAxis(c0, ang(c0), r0);
    Mat3RotAxis(c1, ang(c1), r1);
    Mat3RotAxis(c2, ang(c2), r2);
    Mat3Mul(r2, r1, r21);
    Mat3Mul(r21, r0, outM);
}

inline Vec3f Mat3Transform(const float m[9], const Vec3f& v) {
    return {
        m[0] * v.x + m[1] * v.y + m[2] * v.z,
        m[3] * v.x + m[4] * v.y + m[5] * v.z,
        m[6] * v.x + m[7] * v.y + m[8] * v.z
    };
}

inline bool ParseAsfText(std::string_view sv, AsfSkeleton& skel) {
    sv = ReaderText::StripUtf8Bom(sv);
    if (sv.size() < 24 || sv.find('\0') != std::string_view::npos) return false;

    enum class Sec { None, Units, Root, BoneData, Hierarchy, Other };
    Sec sec = Sec::None;
    bool inBoneBlock = false;
    bool inHierBlock = false;
    bool sawBoneData = false;
    bool sawHierarchy = false;
    AsfBone curBone;

    auto nextFloat = [](std::string_view& rem, float& out) -> bool {
        std::string_view t;
        return ReaderText::NextTokenSv(rem, t) && ReaderText::ParseFloatSv(t, out);
    };

    size_t pos = 0;
    std::string_view line;
    while (ReaderText::NextLineSv(sv, pos, line)) {
        const size_t hp = line.find('#');
        if (hp != std::string_view::npos) line = line.substr(0, hp);
        line = ReaderText::TrimSv(line);
        if (line.empty()) continue;

        if (line.front() == ':') {
            std::string_view rem = line.substr(1);
            std::string_view kw;
            ReaderText::NextTokenSv(rem, kw);
            inBoneBlock = false;
            inHierBlock = false;
            if (ReaderText::AsciiEqCI(kw, "units")) sec = Sec::Units;
            else if (ReaderText::AsciiEqCI(kw, "root")) sec = Sec::Root;
            else if (ReaderText::AsciiEqCI(kw, "bonedata")) { sec = Sec::BoneData; sawBoneData = true; }
            else if (ReaderText::AsciiEqCI(kw, "hierarchy")) { sec = Sec::Hierarchy; sawHierarchy = true; }
            else sec = Sec::Other;
            continue;
        }

        std::string_view rem = line;
        std::string_view tok;
        if (!ReaderText::NextTokenSv(rem, tok)) continue;

        if (sec == Sec::Units) {
            if (ReaderText::AsciiEqCI(tok, "angle")) {
                std::string_view u;
                if (ReaderText::NextTokenSv(rem, u)) skel.angleRadians = ReaderText::StartsWithCI(u, "rad");
            } else if (ReaderText::AsciiEqCI(tok, "length")) {
                float s = 1.0f;
                if (nextFloat(rem, s) && s > 1e-5f) skel.lengthScale = s;
            }
        } else if (sec == Sec::Root) {
            if (ReaderText::AsciiEqCI(tok, "position")) {
                nextFloat(rem, skel.rootPos.x);
                nextFloat(rem, skel.rootPos.y);
                nextFloat(rem, skel.rootPos.z);
            } else if (ReaderText::AsciiEqCI(tok, "orientation")) {
                nextFloat(rem, skel.rootRot.x);
                nextFloat(rem, skel.rootRot.y);
                nextFloat(rem, skel.rootRot.z);
            } else if (ReaderText::AsciiEqCI(tok, "axis")) {
                std::string_view ord;
                if (ReaderText::NextTokenSv(rem, ord) && ord.size() == 3) skel.rootAxisOrder = ReaderText::ToUpperAscii(ord);
            } else if (ReaderText::AsciiEqCI(tok, "order")) {
                skel.rootOrder.clear();
                std::string_view d;
                while (ReaderText::NextTokenSv(rem, d)) {
                    skel.rootOrder.push_back(ReaderText::ToUpperAscii(d));
                }
            }
        } else if (sec == Sec::BoneData) {
            if (ReaderText::AsciiEqCI(tok, "begin")) {
                inBoneBlock = true;
                curBone = AsfBone{};
            } else if (ReaderText::AsciiEqCI(tok, "end")) {
                if (inBoneBlock && !curBone.name.empty() && skel.bones.size() < 1024) {
                    std::string key = ReaderText::ToLowerAscii(curBone.name);
                    if (skel.boneMap.find(key) == skel.boneMap.end()) {
                        skel.boneMap.emplace(std::move(key), skel.bones.size());
                        skel.bones.push_back(std::move(curBone));
                    }
                }
                inBoneBlock = false;
            } else if (inBoneBlock) {
                if (ReaderText::AsciiEqCI(tok, "name")) {
                    std::string_view bn;
                    if (ReaderText::NextTokenSv(rem, bn)) curBone.name = std::string(bn);
                } else if (ReaderText::AsciiEqCI(tok, "direction")) {
                    nextFloat(rem, curBone.dir.x);
                    nextFloat(rem, curBone.dir.y);
                    nextFloat(rem, curBone.dir.z);
                } else if (ReaderText::AsciiEqCI(tok, "length")) {
                    nextFloat(rem, curBone.length);
                } else if (ReaderText::AsciiEqCI(tok, "axis")) {
                    nextFloat(rem, curBone.axisAng[0]);
                    nextFloat(rem, curBone.axisAng[1]);
                    nextFloat(rem, curBone.axisAng[2]);
                    std::string_view ord;
                    if (ReaderText::NextTokenSv(rem, ord) && ord.size() == 3) curBone.axisOrder = ReaderText::ToUpperAscii(ord);
                } else if (ReaderText::AsciiEqCI(tok, "dof")) {
                    curBone.dofs.clear();
                    std::string_view d;
                    while (ReaderText::NextTokenSv(rem, d)) {
                        curBone.dofs.push_back(ReaderText::ToLowerAscii(d));
                    }
                }
            }
        } else if (sec == Sec::Hierarchy) {
            if (ReaderText::AsciiEqCI(tok, "begin")) {
                inHierBlock = true;
            } else if (ReaderText::AsciiEqCI(tok, "end")) {
                inHierBlock = false;
            } else if (inHierBlock) {
                int pIdx = -1;
                if (!ReaderText::AsciiEqCI(tok, "root")) {
                    auto pit = skel.boneMap.find(ReaderText::ToLowerAscii(tok));
                    if (pit == skel.boneMap.end()) continue;
                    pIdx = static_cast<int>(pit->second);
                }
                std::string_view childName;
                while (ReaderText::NextTokenSv(rem, childName)) {
                    auto cit = skel.boneMap.find(ReaderText::ToLowerAscii(childName));
                    if (cit != skel.boneMap.end() && static_cast<int>(cit->second) != pIdx) {
                        skel.bones[cit->second].parentIdx = pIdx;
                    }
                }
            }
        }
    }

    return sawBoneData && sawHierarchy && !skel.bones.empty();
}

inline bool ApplyAmcMotionText(std::string_view sv, AsfSkeleton& skel, size_t& outFrames) {
    sv = ReaderText::StripUtf8Bom(sv);
    if (sv.empty() || sv.find('\0') != std::string_view::npos) return false;

    size_t frameCount = 0;
    bool inFirstFrame = false;
    size_t pos = 0;
    std::string_view line;
    while (ReaderText::NextLineSv(sv, pos, line)) {
        const size_t hp = line.find('#');
        if (hp != std::string_view::npos) line = line.substr(0, hp);
        line = ReaderText::TrimSv(line);
        if (line.empty() || line.front() == ':') continue;

        int frameNum = 0;
        if (ReaderText::ParseIntSv(line, frameNum) && frameNum >= 0) {
            ++frameCount;
            inFirstFrame = (frameCount == 1);
            continue;
        }

        if (inFirstFrame) {
            std::string_view rem = line;
            std::string_view jName;
            if (!ReaderText::NextTokenSv(rem, jName)) continue;
            std::vector<float> vals;
            std::string_view nt;
            while (ReaderText::NextTokenSv(rem, nt)) {
                float v = 0.0f;
                if (ReaderText::ParseFloatSv(nt, v)) vals.push_back(v);
            }
            if (ReaderText::AsciiEqCI(jName, "root")) {
                for (size_t k = 0; k < skel.rootOrder.size() && k < vals.size(); ++k) {
                    const std::string& ch = skel.rootOrder[k];
                    if (ch == "TX") skel.rootPos.x = vals[k];
                    else if (ch == "TY") skel.rootPos.y = vals[k];
                    else if (ch == "TZ") skel.rootPos.z = vals[k];
                    else if (ch == "RX") skel.rootRot.x = vals[k];
                    else if (ch == "RY") skel.rootRot.y = vals[k];
                    else if (ch == "RZ") skel.rootRot.z = vals[k];
                }
            } else {
                auto it = skel.boneMap.find(ReaderText::ToLowerAscii(jName));
                if (it != skel.boneMap.end()) {
                    skel.bones[it->second].hasAmcPose = true;
                    skel.bones[it->second].amcVals = std::move(vals);
                }
            }
        }
    }
    if (frameCount == 0) return false;
    outFrames = frameCount;
    return true;
}

inline bool EmitAcclaimSkeletonMesh(
    const AsfSkeleton& skel,
    size_t animFrames,
    const wchar_t* formatLabel,
    LoadedMesh3D& mesh)
{
    const float toRad = skel.angleRadians ? 1.0f : (3.14159265358979323846f / 180.0f);
    float rootM[9];
    BuildAcclaimEuler3(
        skel.rootRot.x * toRad, skel.rootRot.y * toRad, skel.rootRot.z * toRad, skel.rootAxisOrder, rootM);

    const size_t nb = skel.bones.size();
    std::vector<bool> done(nb, false);
    std::vector<float> worldRot(nb * 9, 0.0f);
    std::vector<Vec3f> startPos(nb), endPos(nb);

    const Vec3f rootWorld = {
        skel.rootPos.x / skel.lengthScale,
        skel.rootPos.y / skel.lengthScale,
        skel.rootPos.z / skel.lengthScale
    };

    for (size_t pass = 0; pass < nb; ++pass) {
        bool prog = false;
        for (size_t i = 0; i < nb; ++i) {
            if (done[i]) continue;
            const auto& b = skel.bones[i];
            if (b.parentIdx >= 0 && !done[static_cast<size_t>(b.parentIdx)]) continue;

            float C[9], CT[9], Rdof[9];
            BuildAcclaimEuler3(b.axisAng[0] * toRad, b.axisAng[1] * toRad, b.axisAng[2] * toRad, b.axisOrder, C);
            Mat3Transpose(C, CT);

            float rx = 0.0f, ry = 0.0f, rz = 0.0f, lenScale = 1.0f;
            if (b.hasAmcPose) {
                for (size_t k = 0; k < b.dofs.size() && k < b.amcVals.size(); ++k) {
                    if (b.dofs[k] == "rx") rx = b.amcVals[k] * toRad;
                    else if (b.dofs[k] == "ry") ry = b.amcVals[k] * toRad;
                    else if (b.dofs[k] == "rz") rz = b.amcVals[k] * toRad;
                    else if (b.dofs[k] == "l")  lenScale = b.amcVals[k];
                }
            }
            BuildAcclaimEuler3(rx, ry, rz, "XYZ", Rdof);

            float CR[9], Ljoint[9];
            Mat3Mul(C, Rdof, CR);
            Mat3Mul(CR, CT, Ljoint);

            const float* parentM = (b.parentIdx >= 0) ? &worldRot[static_cast<size_t>(b.parentIdx) * 9u] : rootM;
            Mat3Mul(parentM, Ljoint, &worldRot[i * 9u]);

            startPos[i] = (b.parentIdx >= 0) ? endPos[static_cast<size_t>(b.parentIdx)] : rootWorld;
            const float effLen = (b.length * lenScale) / skel.lengthScale;
            const Vec3f restVec = { b.dir.x * effLen, b.dir.y * effLen, b.dir.z * effLen };
            const Vec3f delta = Mat3Transform(&worldRot[i * 9u], restVec);
            endPos[i] = { startPos[i].x + delta.x, startPos[i].y + delta.y, startPos[i].z + delta.z };
            done[i] = true;
            prog = true;
        }
        if (!prog) break;
    }

    Vec3f bmin = rootWorld, bmax = rootWorld;
    for (size_t i = 0; i < nb; ++i) {
        if (!done[i]) return false;
        for (const Vec3f& p : { startPos[i], endPos[i] }) {
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
            bmin.x = std::min(bmin.x, p.x); bmin.y = std::min(bmin.y, p.y); bmin.z = std::min(bmin.z, p.z);
            bmax.x = std::max(bmax.x, p.x); bmax.y = std::max(bmax.y, p.y); bmax.z = std::max(bmax.z, p.z);
        }
    }
    const float span = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z });
    if (span <= 1e-5f) return false;

    const float jRad = span * 0.022f;
    const float sRad = span * 0.012f;
    MeshBuild3D::EmitOctahedron(mesh, rootWorld, jRad * 1.15f);
    for (size_t i = 0; i < nb; ++i) {
        MeshBuild3D::EmitEdgeStrut(mesh, startPos[i], endPos[i], sRad);
        MeshBuild3D::EmitOctahedron(mesh, endPos[i], jRad);
    }
    if (mesh.tris.empty()) return false;
    mesh.animFrameCount = animFrames;
    mesh.formatName = formatLabel;
    return true;
}

} // namespace Formats::AcclaimSkelCommon
