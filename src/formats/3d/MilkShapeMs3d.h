#pragma once
// MilkShapeMs3d.h — MilkShape 3D (.ms3d) skeletal mesh reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// ============================================================================
// #42. MilkShape 3D (.ms3d — "MS3D000000" v3/v4 with Skeletal Joints, Keyframes & VertexExtra LBS)
// ============================================================================
inline bool ParseMilkShapeMs3d(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (size < 16 || std::memcmp(data, "MS3D000000", 10) != 0) return false;
    int32_t ver = static_cast<int32_t>(Detail3D200::RdU32LE(data + 10));
    if (ver < 3 || ver > 4) return false;
    mesh.formatName = L"MilkShape 3D Skeletal Mesh (.ms3d)";

    size_t pos = 14;
    if (pos + 2 > size) return false;
    uint16_t nVerts = Detail3D200::RdU16LE(data + pos); pos += 2;
    if (nVerts == 0 || pos + static_cast<size_t>(nVerts) * 15 + 2 > size) return false;

    std::vector<Vec3f> bindVerts;
    std::vector<int8_t> primaryBoneIds;
    bindVerts.reserve(nVerts);
    primaryBoneIds.reserve(nVerts);
    for (uint16_t i = 0; i < nVerts; ++i) {
        const uint8_t* vp = data + pos + static_cast<size_t>(i) * 15;
        float vx = Detail3D200::RdF32LE(vp + 1);
        float vy = Detail3D200::RdF32LE(vp + 5);
        float vz = Detail3D200::RdF32LE(vp + 9);
        if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return false;
        bindVerts.push_back({ vx, vy, vz });
        primaryBoneIds.push_back(static_cast<int8_t>(vp[13]));
    }
    pos += static_cast<size_t>(nVerts) * 15;

    uint16_t nTris = Detail3D200::RdU16LE(data + pos); pos += 2;
    if (nTris == 0 || pos + static_cast<size_t>(nTris) * 70 > size) return false;
    mesh.tris.reserve(nTris);
    for (uint16_t t = 0; t < nTris; ++t) {
        const uint8_t* tp = data + pos + static_cast<size_t>(t) * 70;
        uint16_t i0 = Detail3D200::RdU16LE(tp + 2);
        uint16_t i1 = Detail3D200::RdU16LE(tp + 4);
        uint16_t i2 = Detail3D200::RdU16LE(tp + 6);
        if (i0 < nVerts && i1 < nVerts && i2 < nVerts) {
            mesh.tris.push_back({ i0, i1, i2, {} });
        }
    }
    pos += static_cast<size_t>(nTris) * 70;
    if (mesh.tris.empty()) return false;

    // Optional Groups, Materials, Animation Header, Joints, Comments, and VertexExtra multi-bone weights
    struct MsMat3x4 {
        float m[3][4] = { {1,0,0,0}, {0,1,0,0}, {0,0,1,0} };
        static MsMat3x4 FromEulerXYZAndPos(float rx, float ry, float rz, float px, float py, float pz) {
            const float cx = std::cos(rx), sx = std::sin(rx);
            const float cy = std::cos(ry), sy = std::sin(ry);
            const float cz = std::cos(rz), sz = std::sin(rz);
            // MS3D AngleMatrix: Rz * Ry * Rx
            MsMat3x4 r;
            r.m[0][0] = cy * cz;
            r.m[1][0] = cy * sz;
            r.m[2][0] = -sy;
            r.m[0][1] = sx * sy * cz - cx * sz;
            r.m[1][1] = sx * sy * sz + cx * cz;
            r.m[2][1] = sx * cy;
            r.m[0][2] = cx * sy * cz + sx * sz;
            r.m[1][2] = cx * sy * sz - sx * cz;
            r.m[2][2] = cx * cy;
            r.m[0][3] = px;
            r.m[1][3] = py;
            r.m[2][3] = pz;
            return r;
        }
        static MsMat3x4 Mul(const MsMat3x4& a, const MsMat3x4& b) {
            MsMat3x4 r;
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
                }
                r.m[i][3] = a.m[i][0] * b.m[0][3] + a.m[i][1] * b.m[1][3] + a.m[i][2] * b.m[2][3] + a.m[i][3];
            }
            return r;
        }
        // Rigid inverse (transpose 3x3 + inverse rotated translation)
        MsMat3x4 InverseRigid() const {
            MsMat3x4 inv;
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) inv.m[i][j] = m[j][i];
            }
            inv.m[0][3] = -(inv.m[0][0] * m[0][3] + inv.m[0][1] * m[1][3] + inv.m[0][2] * m[2][3]);
            inv.m[1][3] = -(inv.m[1][0] * m[0][3] + inv.m[1][1] * m[1][3] + inv.m[1][2] * m[2][3]);
            inv.m[2][3] = -(inv.m[2][0] * m[0][3] + inv.m[2][1] * m[1][3] + inv.m[2][2] * m[2][3]);
            return inv;
        }
        Vec3f Transform(const Vec3f& v) const {
            return {
                m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3],
                m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3],
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3]
            };
        }
    };

    auto parseFixedName32 = [](const uint8_t* p) -> std::string {
        size_t len = 0;
        while (len < 32 && p[len] != 0) ++len;
        return std::string(reinterpret_cast<const char*>(p), len);
    };

    bool skeletonApplied = false;
    if (pos + 2 <= size) {
        uint16_t nGroups = Detail3D200::RdU16LE(data + pos); pos += 2;
        bool groupsOk = true;
        for (uint16_t g = 0; g < nGroups; ++g) {
            if (pos + 35 > size) { groupsOk = false; break; }
            uint16_t gTris = Detail3D200::RdU16LE(data + pos + 33);
            size_t step = 35u + static_cast<size_t>(gTris) * 2u + 1u;
            if (pos + step > size) { groupsOk = false; break; }
            pos += step;
        }
        if (groupsOk && pos + 2 <= size) {
            uint16_t nMats = Detail3D200::RdU16LE(data + pos); pos += 2;
            size_t matsBytes = static_cast<size_t>(nMats) * 361u;
            if (pos + matsBytes + 14u <= size) {
                pos += matsBytes;
                float animFps = Detail3D200::RdF32LE(data + pos + 0);
                float curTime = Detail3D200::RdF32LE(data + pos + 4);
                int32_t totalFrames = static_cast<int32_t>(Detail3D200::RdU32LE(data + pos + 8));
                pos += 12;
                if (totalFrames > 1 && totalFrames < 1000000) {
                    mesh.animFrameCount = static_cast<size_t>(totalFrames);
                }
                (void)animFps;

                uint16_t nJoints = Detail3D200::RdU16LE(data + pos); pos += 2;
                struct MsKey { float time = 0.0f; float v[3] = {}; };
                struct MsJoint {
                    std::string name, parentName;
                    int parentIdx = -1;
                    float bindRot[3] = {};
                    float bindPos[3] = {};
                    std::vector<MsKey> rotKeys;
                    std::vector<MsKey> posKeys;
                };
                std::vector<MsJoint> joints;
                bool jointsOk = (nJoints > 0 && nJoints <= 4096);
                if (jointsOk) {
                    joints.reserve(nJoints);
                    for (uint16_t j = 0; j < nJoints; ++j) {
                        if (pos + 93 > size) { jointsOk = false; break; }
                        MsJoint mj;
                        mj.name = parseFixedName32(data + pos + 1);
                        mj.parentName = parseFixedName32(data + pos + 33);
                        mj.bindRot[0] = Detail3D200::RdF32LE(data + pos + 65);
                        mj.bindRot[1] = Detail3D200::RdF32LE(data + pos + 69);
                        mj.bindRot[2] = Detail3D200::RdF32LE(data + pos + 73);
                        mj.bindPos[0] = Detail3D200::RdF32LE(data + pos + 77);
                        mj.bindPos[1] = Detail3D200::RdF32LE(data + pos + 81);
                        mj.bindPos[2] = Detail3D200::RdF32LE(data + pos + 85);
                        uint16_t nRotK = Detail3D200::RdU16LE(data + pos + 89);
                        uint16_t nPosK = Detail3D200::RdU16LE(data + pos + 91);
                        pos += 93;
                        size_t kBytes = (static_cast<size_t>(nRotK) + static_cast<size_t>(nPosK)) * 16u;
                        if (pos + kBytes > size) { jointsOk = false; break; }
                        mj.rotKeys.resize(nRotK);
                        for (uint16_t k = 0; k < nRotK; ++k) {
                            mj.rotKeys[k].time = Detail3D200::RdF32LE(data + pos + 0);
                            mj.rotKeys[k].v[0] = Detail3D200::RdF32LE(data + pos + 4);
                            mj.rotKeys[k].v[1] = Detail3D200::RdF32LE(data + pos + 8);
                            mj.rotKeys[k].v[2] = Detail3D200::RdF32LE(data + pos + 12);
                            pos += 16;
                        }
                        mj.posKeys.resize(nPosK);
                        for (uint16_t k = 0; k < nPosK; ++k) {
                            mj.posKeys[k].time = Detail3D200::RdF32LE(data + pos + 0);
                            mj.posKeys[k].v[0] = Detail3D200::RdF32LE(data + pos + 4);
                            mj.posKeys[k].v[1] = Detail3D200::RdF32LE(data + pos + 8);
                            mj.posKeys[k].v[2] = Detail3D200::RdF32LE(data + pos + 12);
                            pos += 16;
                        }
                        joints.push_back(std::move(mj));
                    }
                }

                if (jointsOk && !joints.empty()) {
                    // Optional Comments section (subVersion == 1) + VertexExtra (subVersion 1..3)
                    struct VertExtraWeights {
                        int8_t boneIds[4] = { -1, -1, -1, -1 };
                        float weights[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
                    };
                    std::vector<VertExtraWeights> vWeights(nVerts);
                    for (uint16_t i = 0; i < nVerts; ++i) {
                        vWeights[i].boneIds[0] = primaryBoneIds[i];
                        vWeights[i].weights[0] = (primaryBoneIds[i] >= 0) ? 1.0f : 0.0f;
                    }

                    if (pos + 4 <= size) {
                        int32_t commentSubVer = static_cast<int32_t>(Detail3D200::RdU32LE(data + pos));
                        if (commentSubVer == 1) {
                            size_t cPos = pos + 4;
                            bool commentsOk = true;
                            // 3 indexed comment lists (group, material, joint) + 1 model comment flag
                            for (int listIdx = 0; listIdx < 3 && commentsOk; ++listIdx) {
                                if (cPos + 4 > size) { commentsOk = false; break; }
                                uint32_t nCom = Detail3D200::RdU32LE(data + cPos); cPos += 4;
                                if (nCom > 65536) { commentsOk = false; break; }
                                for (uint32_t ci = 0; ci < nCom; ++ci) {
                                    if (cPos + 8 > size) { commentsOk = false; break; }
                                    uint32_t cLen = Detail3D200::RdU32LE(data + cPos + 4);
                                    cPos += 8;
                                    if (cLen > size - cPos) { commentsOk = false; break; }
                                    cPos += cLen;
                                }
                            }
                            if (commentsOk && cPos + 4 <= size) {
                                uint32_t hasModelCom = Detail3D200::RdU32LE(data + cPos); cPos += 4;
                                if (hasModelCom == 1) {
                                    if (cPos + 4 <= size) {
                                        uint32_t mLen = Detail3D200::RdU32LE(data + cPos); cPos += 4;
                                        if (mLen <= size - cPos) cPos += mLen;
                                        else commentsOk = false;
                                    } else commentsOk = false;
                                } else if (hasModelCom > 1) {
                                    commentsOk = false;
                                }
                            }
                            if (commentsOk) pos = cPos;
                        }
                    }

                    if (pos + 4 <= size) {
                        int32_t vExtraSubVer = static_cast<int32_t>(Detail3D200::RdU32LE(data + pos));
                        size_t recSz = (vExtraSubVer == 1) ? 6u : (vExtraSubVer == 2) ? 10u : (vExtraSubVer == 3) ? 14u : 0u;
                        if (recSz > 0 && pos + 4u + static_cast<size_t>(nVerts) * recSz <= size) {
                            pos += 4;
                            const float wScale = (vExtraSubVer == 1) ? 255.0f : 100.0f;
                            for (uint16_t i = 0; i < nVerts; ++i) {
                                const uint8_t* ep = data + pos + static_cast<size_t>(i) * recSz;
                                int8_t b1 = static_cast<int8_t>(ep[0]);
                                int8_t b2 = static_cast<int8_t>(ep[1]);
                                int8_t b3 = static_cast<int8_t>(ep[2]);
                                uint8_t w0 = ep[3], w1 = ep[4], w2 = ep[5];
                                if (w0 > 0 || w1 > 0 || w2 > 0) {
                                    float fw0 = static_cast<float>(w0) / wScale;
                                    float fw1 = static_cast<float>(w1) / wScale;
                                    float fw2 = static_cast<float>(w2) / wScale;
                                    float fw3 = std::max(0.0f, 1.0f - (fw0 + fw1 + fw2));
                                    vWeights[i].boneIds[0] = primaryBoneIds[i];
                                    vWeights[i].boneIds[1] = b1;
                                    vWeights[i].boneIds[2] = b2;
                                    vWeights[i].boneIds[3] = b3;
                                    vWeights[i].weights[0] = fw0;
                                    vWeights[i].weights[1] = (b1 >= 0) ? fw1 : 0.0f;
                                    vWeights[i].weights[2] = (b2 >= 0) ? fw2 : 0.0f;
                                    vWeights[i].weights[3] = (b3 >= 0) ? fw3 : 0.0f;
                                }
                            }
                        }
                    }

                    // Resolve parent joint indices by name
                    for (size_t j = 0; j < joints.size(); ++j) {
                        if (!joints[j].parentName.empty()) {
                            for (size_t p = 0; p < joints.size(); ++p) {
                                if (p != j && joints[p].name == joints[j].parentName) {
                                    joints[j].parentIdx = static_cast<int>(p);
                                    break;
                                }
                            }
                        }
                    }

                    auto sampleKeys = [](const std::vector<MsKey>& keys, float t, float out[3]) {
                        out[0] = out[1] = out[2] = 0.0f;
                        if (keys.empty()) return;
                        if (keys.size() == 1 || t <= keys.front().time) {
                            out[0] = keys.front().v[0]; out[1] = keys.front().v[1]; out[2] = keys.front().v[2];
                            return;
                        }
                        if (t >= keys.back().time) {
                            out[0] = keys.back().v[0]; out[1] = keys.back().v[1]; out[2] = keys.back().v[2];
                            return;
                        }
                        for (size_t k = 0; k + 1 < keys.size(); ++k) {
                            if (t >= keys[k].time && t <= keys[k + 1].time) {
                                float dt = keys[k + 1].time - keys[k].time;
                                float a = (dt > 1e-6f) ? (t - keys[k].time) / dt : 0.0f;
                                for (int c = 0; c < 3; ++c) {
                                    out[c] = keys[k].v[c] + (keys[k + 1].v[c] - keys[k].v[c]) * a;
                                }
                                return;
                            }
                        }
                    };

                    float evalTime = (std::isfinite(curTime) && curTime > 0.0f) ? curTime : 0.0f;
                    std::vector<MsMat3x4> bindGlobal(joints.size());
                    std::vector<MsMat3x4> poseGlobal(joints.size());
                    std::vector<uint8_t> state(joints.size(), 0);

                    std::function<void(size_t, int)> evalJoint = [&](size_t jIdx, int depth) {
                        if (state[jIdx] == 2) return;
                        if (state[jIdx] == 1 || depth > 256) {
                            bindGlobal[jIdx] = MsMat3x4{};
                            poseGlobal[jIdx] = MsMat3x4{};
                            state[jIdx] = 2;
                            return;
                        }
                        state[jIdx] = 1;
                        const MsJoint& j = joints[jIdx];
                        MsMat3x4 localBind = MsMat3x4::FromEulerXYZAndPos(
                            j.bindRot[0], j.bindRot[1], j.bindRot[2],
                            j.bindPos[0], j.bindPos[1], j.bindPos[2]);
                        float kfR[3] = {}, kfP[3] = {};
                        sampleKeys(j.rotKeys, evalTime, kfR);
                        sampleKeys(j.posKeys, evalTime, kfP);
                        MsMat3x4 localDelta = MsMat3x4::FromEulerXYZAndPos(kfR[0], kfR[1], kfR[2], kfP[0], kfP[1], kfP[2]);
                        MsMat3x4 localPose = MsMat3x4::Mul(localBind, localDelta);

                        if (j.parentIdx >= 0 && static_cast<size_t>(j.parentIdx) < joints.size()) {
                            size_t pIdx = static_cast<size_t>(j.parentIdx);
                            evalJoint(pIdx, depth + 1);
                            bindGlobal[jIdx] = MsMat3x4::Mul(bindGlobal[pIdx], localBind);
                            poseGlobal[jIdx] = MsMat3x4::Mul(poseGlobal[pIdx], localPose);
                        } else {
                            bindGlobal[jIdx] = localBind;
                            poseGlobal[jIdx] = localPose;
                        }
                        state[jIdx] = 2;
                    };

                    std::vector<MsMat3x4> skinMats(joints.size());
                    for (size_t j = 0; j < joints.size(); ++j) {
                        evalJoint(j, 0);
                        skinMats[j] = MsMat3x4::Mul(poseGlobal[j], bindGlobal[j].InverseRigid());
                    }

                    mesh.vertices.resize(nVerts);
                    for (uint16_t i = 0; i < nVerts; ++i) {
                        const Vec3f& bv = bindVerts[i];
                        const VertExtraWeights& vw = vWeights[i];
                        Vec3f accum = { 0.0f, 0.0f, 0.0f };
                        float wSum = 0.0f;
                        for (int slot = 0; slot < 4; ++slot) {
                            int b = vw.boneIds[slot];
                            float w = vw.weights[slot];
                            if (b >= 0 && static_cast<size_t>(b) < joints.size() && w > 1e-6f) {
                                Vec3f tp = skinMats[static_cast<size_t>(b)].Transform(bv);
                                accum.x += tp.x * w;
                                accum.y += tp.y * w;
                                accum.z += tp.z * w;
                                wSum += w;
                            }
                        }
                        if (wSum > 1e-6f) {
                            float invW = 1.0f / wSum;
                            mesh.vertices[i] = { accum.x * invW, accum.y * invW, accum.z * invW };
                        } else {
                            mesh.vertices[i] = bv;
                        }
                    }
                    skeletonApplied = true;
                }
            }
        }
    }

    if (!skeletonApplied) {
        mesh.vertices = std::move(bindVerts);
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
