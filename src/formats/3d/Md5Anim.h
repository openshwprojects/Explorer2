#pragma once
// Md5Anim.h — id Tech 4 (Doom 3 / Quake 4 / Prey) Skeletal Animation (.md5anim) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

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
