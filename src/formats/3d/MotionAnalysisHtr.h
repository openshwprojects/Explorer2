#pragma once
// MotionAnalysisHtr.h — Motion Analysis HTR / GTR Skeletal Motion Capture (.htr) reader
// Parses [Header] (EulerRotationOrder, RotationUnits, GlobalAxisofGravity, BoneLengthAxis, HTRS/GTRS),
// [SegmentNames&Hierarchy], [BasePosition], and per-segment keyframe blocks, computing full 4x4 forward
// kinematics for pose 0 and emitting articulated 3D joint markers + bone struts.

namespace Formats {

namespace DetailHtrMocap {

inline void MakeIdentity4(float m[16]) {
    for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

inline void MakeRotAxis4(char axis, float rad, float m[16]) {
    MakeIdentity4(m);
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    if (axis == 'X' || axis == 'x') {
        m[5] = c;  m[6] = -s;
        m[9] = s;  m[10] = c;
    } else if (axis == 'Y' || axis == 'y') {
        m[0] = c;  m[2] = s;
        m[8] = -s; m[10] = c;
    } else {
        m[0] = c;  m[1] = -s;
        m[4] = s;  m[5] = c;
    }
}

// Builds T(tx,ty,tz) * R(order[0]) * R(order[1]) * R(order[2]) in column-vector convention (v' = M * v)
inline void BuildHtrTransform4(
    float tx, float ty, float tz,
    float rxRad, float ryRad, float rzRad,
    std::string_view eulerOrder,
    float outM[16])
{
    auto angleFor = [&](char ax) -> float {
        if (ax == 'X' || ax == 'x') return rxRad;
        if (ax == 'Y' || ax == 'y') return ryRad;
        return rzRad;
    };
    const char a0 = (eulerOrder.size() >= 1) ? eulerOrder[0] : 'Z';
    const char a1 = (eulerOrder.size() >= 2) ? eulerOrder[1] : 'Y';
    const char a2 = (eulerOrder.size() >= 3) ? eulerOrder[2] : 'X';

    float r0[16], r1[16], r2[16], r01[16];
    MakeRotAxis4(a0, angleFor(a0), r0);
    MakeRotAxis4(a1, angleFor(a1), r1);
    MakeRotAxis4(a2, angleFor(a2), r2);
    MeshBuild3D::MatMul4RowMajor(r0, r1, r01);
    MeshBuild3D::MatMul4RowMajor(r01, r2, outM);
    outM[3] = tx;
    outM[7] = ty;
    outM[11] = tz;
}

inline Vec3f TransformPoint4(const float m[16], const Vec3f& p) {
    return {
        m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3],
        m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
        m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]
    };
}

struct HtrSegment {
    std::string name;
    std::string parentName;
    int parentIdx = -1;
    bool hasChildren = false;
    float baseTx = 0.0f, baseTy = 0.0f, baseTz = 0.0f;
    float baseRx = 0.0f, baseRy = 0.0f, baseRz = 0.0f;
    float boneLen = 0.0f;
    bool hasFrame0 = false;
    float f0Tx = 0.0f, f0Ty = 0.0f, f0Tz = 0.0f;
    float f0Rx = 0.0f, f0Ry = 0.0f, f0Rz = 0.0f;
    float f0Scale = 1.0f;
    float worldMat[16] = {};
    bool worldDone = false;
};

} // namespace DetailHtrMocap

inline bool ParseMotionAnalysisHtr(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 32 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    bool sawHeaderSection = false;
    bool sawHierarchySection = false;
    bool isGlobalGtrs = false;
    bool rotInRadians = false;
    bool zUpGravity = false;
    char boneAxis = 'Y';
    std::string eulerOrder = "ZYX";
    int declaredFrames = 1;

    std::vector<DetailHtrMocap::HtrSegment> segs;
    std::unordered_map<std::string, size_t> segMap;

    enum class SecKind { None, Header, Hierarchy, BasePos, MotionSeg, Other };
    SecKind curSec = SecKind::None;
    size_t activeSegIdx = 0;

    auto findOrAddSeg = [&](std::string_view rawName) -> size_t {
        std::string key = ReaderText::ToLowerAscii(rawName);
        auto it = segMap.find(key);
        if (it != segMap.end()) return it->second;
        size_t idx = segs.size();
        DetailHtrMocap::HtrSegment s;
        s.name = std::string(rawName);
        DetailHtrMocap::MakeIdentity4(s.worldMat);
        segs.push_back(std::move(s));
        segMap.emplace(std::move(key), idx);
        return idx;
    };

    size_t pos = 0;
    std::string_view line;
    while (ReaderText::NextLineSv(sv, pos, line)) {
        const size_t hashPos = line.find('#');
        if (hashPos != std::string_view::npos) line = line.substr(0, hashPos);
        line = ReaderText::TrimSv(line);
        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']' && line.size() >= 3) {
            std::string_view secName = ReaderText::TrimSv(line.substr(1, line.size() - 2));
            if (ReaderText::AsciiEqCI(secName, "Header")) {
                curSec = SecKind::Header;
                sawHeaderSection = true;
            } else if (ReaderText::AsciiEqCI(secName, "SegmentNames&Hierarchy") ||
                       ReaderText::AsciiEqCI(secName, "SegmentNamesAndHierarchy")) {
                curSec = SecKind::Hierarchy;
                sawHierarchySection = true;
            } else if (ReaderText::AsciiEqCI(secName, "BasePosition")) {
                curSec = SecKind::BasePos;
            } else if (ReaderText::AsciiEqCI(secName, "EndOfFile")) {
                break;
            } else {
                auto it = segMap.find(ReaderText::ToLowerAscii(secName));
                if (it != segMap.end()) {
                    curSec = SecKind::MotionSeg;
                    activeSegIdx = it->second;
                } else {
                    curSec = SecKind::Other;
                }
            }
            continue;
        }

        std::string_view rem = line;
        if (curSec == SecKind::Header) {
            std::string_view k;
            if (!ReaderText::NextTokenSv(rem, k)) continue;
            std::string_view v = ReaderText::TrimSv(rem);
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
                v = v.substr(1, v.size() - 2);
            }
            if (ReaderText::AsciiEqCI(k, "DataType")) {
                if (ReaderText::AsciiEqCI(v, "GTRS")) isGlobalGtrs = true;
                else if (!ReaderText::AsciiEqCI(v, "HTRS")) return false;
            } else if (ReaderText::AsciiEqCI(k, "NumFrames")) {
                int nf = 0;
                if (ReaderText::ParseIntSv(v, nf) && nf >= 1 && nf <= 1000000) declaredFrames = nf;
            } else if (ReaderText::AsciiEqCI(k, "RotationUnits")) {
                rotInRadians = ReaderText::StartsWithCI(v, "Rad");
            } else if (ReaderText::AsciiEqCI(k, "EulerRotationOrder") && v.size() == 3) {
                eulerOrder = ReaderText::ToUpperAscii(v);
            } else if (ReaderText::AsciiEqCI(k, "GlobalAxisofGravity") && !v.empty()) {
                zUpGravity = (v[0] == 'Z' || v[0] == 'z' || (v.size() >= 2 && (v[1] == 'Z' || v[1] == 'z')));
            } else if (ReaderText::AsciiEqCI(k, "BoneLengthAxis") && !v.empty()) {
                char c = v.back();
                if (c == 'X' || c == 'x' || c == 'Y' || c == 'y' || c == 'Z' || c == 'z') {
                    boneAxis = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                }
            }
        } else if (curSec == SecKind::Hierarchy) {
            std::string_view childTok, parentTok;
            if (!ReaderText::NextTokenSv(rem, childTok) || !ReaderText::NextTokenSv(rem, parentTok)) continue;
            if (segs.size() >= 1024) break;
            size_t cIdx = findOrAddSeg(childTok);
            if (!ReaderText::AsciiEqCI(parentTok, "GLOBAL")) {
                segs[cIdx].parentName = std::string(parentTok);
            }
        } else if (curSec == SecKind::BasePos) {
            std::string_view sName;
            if (!ReaderText::NextTokenSv(rem, sName)) continue;
            auto it = segMap.find(ReaderText::ToLowerAscii(sName));
            if (it == segMap.end()) continue;
            auto& s = segs[it->second];
            float vals[7] = {};
            int got = 0;
            std::string_view nt;
            for (; got < 7 && ReaderText::NextTokenSv(rem, nt); ++got) {
                if (!ReaderText::ParseFloatSv(nt, vals[got])) break;
            }
            if (got >= 6) {
                s.baseTx = vals[0]; s.baseTy = vals[1]; s.baseTz = vals[2];
                s.baseRx = vals[3]; s.baseRy = vals[4]; s.baseRz = vals[5];
                if (got >= 7) s.boneLen = std::max(0.0f, vals[6]);
            }
        } else if (curSec == SecKind::MotionSeg) {
            auto& s = segs[activeSegIdx];
            if (!s.hasFrame0) {
                std::string_view fTok;
                int frameNo = 0;
                if (!ReaderText::NextTokenSv(rem, fTok) || !ReaderText::ParseIntSv(fTok, frameNo)) continue;
                float vals[7] = { 0, 0, 0, 0, 0, 0, 1.0f };
                int got = 0;
                std::string_view nt;
                for (; got < 7 && ReaderText::NextTokenSv(rem, nt); ++got) {
                    if (!ReaderText::ParseFloatSv(nt, vals[got])) break;
                }
                if (got >= 6) {
                    s.f0Tx = vals[0]; s.f0Ty = vals[1]; s.f0Tz = vals[2];
                    s.f0Rx = vals[3]; s.f0Ry = vals[4]; s.f0Rz = vals[5];
                    if (got >= 7 && vals[6] > 1e-4f) s.f0Scale = vals[6];
                    s.hasFrame0 = true;
                }
            }
        }
    }

    if (!sawHeaderSection || !sawHierarchySection || segs.empty()) return false;

    for (size_t i = 0; i < segs.size(); ++i) {
        if (!segs[i].parentName.empty()) {
            auto it = segMap.find(ReaderText::ToLowerAscii(segs[i].parentName));
            if (it != segMap.end() && it->second != i) {
                segs[i].parentIdx = static_cast<int>(it->second);
                segs[it->second].hasChildren = true;
            }
        }
    }

    const float degToRad = rotInRadians ? 1.0f : (3.14159265358979323846f / 180.0f);

    for (size_t pass = 0; pass < segs.size(); ++pass) {
        bool anyProgress = false;
        for (size_t i = 0; i < segs.size(); ++i) {
            auto& s = segs[i];
            if (s.worldDone) continue;
            if (!isGlobalGtrs && s.parentIdx >= 0 && !segs[static_cast<size_t>(s.parentIdx)].worldDone) {
                continue;
            }
            const float tx = s.hasFrame0 ? s.f0Tx : s.baseTx;
            const float ty = s.hasFrame0 ? s.f0Ty : s.baseTy;
            const float tz = s.hasFrame0 ? s.f0Tz : s.baseTz;
            const float rx = (s.hasFrame0 ? s.f0Rx : s.baseRx) * degToRad;
            const float ry = (s.hasFrame0 ? s.f0Ry : s.baseRy) * degToRad;
            const float rz = (s.hasFrame0 ? s.f0Rz : s.baseRz) * degToRad;

            float localM[16];
            DetailHtrMocap::BuildHtrTransform4(tx, ty, tz, rx, ry, rz, eulerOrder, localM);
            if (!isGlobalGtrs && s.parentIdx >= 0) {
                MeshBuild3D::MatMul4RowMajor(segs[static_cast<size_t>(s.parentIdx)].worldMat, localM, s.worldMat);
            } else {
                std::memcpy(s.worldMat, localM, sizeof(localM));
            }
            s.worldDone = true;
            anyProgress = true;
        }
        if (!anyProgress) break;
    }

    auto toViewer = [&](Vec3f p) -> Vec3f {
        return zUpGravity ? Vec3f{ p.x, p.z, -p.y } : p;
    };

    std::vector<Vec3f> jointPos(segs.size());
    Vec3f bmin = { 1e30f, 1e30f, 1e30f }, bmax = { -1e30f, -1e30f, -1e30f };
    for (size_t i = 0; i < segs.size(); ++i) {
        if (!segs[i].worldDone) return false;
        Vec3f jp = toViewer({ segs[i].worldMat[3], segs[i].worldMat[7], segs[i].worldMat[11] });
        if (!std::isfinite(jp.x) || !std::isfinite(jp.y) || !std::isfinite(jp.z)) return false;
        jointPos[i] = jp;
        bmin.x = std::min(bmin.x, jp.x); bmin.y = std::min(bmin.y, jp.y); bmin.z = std::min(bmin.z, jp.z);
        bmax.x = std::max(bmax.x, jp.x); bmax.y = std::max(bmax.y, jp.y); bmax.z = std::max(bmax.z, jp.z);
    }

    float maxBoneLen = 0.0f;
    for (const auto& s : segs) maxBoneLen = std::max(maxBoneLen, s.boneLen * s.f0Scale);
    const float span = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z, maxBoneLen });
    if (span <= 1e-5f) return false;

    const float jointRad = span * 0.022f;
    const float strutRad = span * 0.012f;

    for (size_t i = 0; i < segs.size(); ++i) {
        MeshBuild3D::EmitOctahedron(mesh, jointPos[i], jointRad);
        if (segs[i].parentIdx >= 0) {
            MeshBuild3D::EmitEdgeStrut(mesh, jointPos[static_cast<size_t>(segs[i].parentIdx)], jointPos[i], strutRad);
        }
        if (!segs[i].hasChildren && segs[i].boneLen > 1e-4f) {
            const float len = segs[i].boneLen * segs[i].f0Scale;
            Vec3f localTip = { 0.0f, len, 0.0f };
            if (boneAxis == 'X') localTip = { len, 0.0f, 0.0f };
            else if (boneAxis == 'Z') localTip = { 0.0f, 0.0f, len };
            Vec3f tip = toViewer(DetailHtrMocap::TransformPoint4(segs[i].worldMat, localTip));
            MeshBuild3D::EmitEdgeStrut(mesh, jointPos[i], tip, strutRad * 0.85f);
            MeshBuild3D::EmitOctahedron(mesh, tip, jointRad * 0.7f);
        }
    }

    if (mesh.tris.empty()) return false;
    mesh.animFrameCount = static_cast<size_t>(declaredFrames);
    mesh.formatName = isGlobalGtrs ? L"Motion Analysis Mocap (GTR)" : L"Motion Analysis Mocap (HTR)";
    return true;
}

} // namespace Formats
