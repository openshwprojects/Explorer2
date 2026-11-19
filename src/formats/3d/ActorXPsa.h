#pragma once
// ActorXPsa.h — Unreal Engine ActorX Skeletal Animation (.psa) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseActorXPsa(const uint8_t* data, size_t size, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    using namespace ActorXDetail;
    if (!data || size < 32) return false;
    size_t cursor = 0;
    ChunkHdr first;
    if (!ReadChunkHeader(data, size, cursor, first) || !ChunkPrefixIs(first, "ANIMHEAD")) {
        return false;
    }

    std::vector<ActorXBone> bones;
    struct SeqInfo {
        int32_t totalBones = 0;
        int32_t firstRawFrame = 0;
        int32_t numRawFrames = 0;
    };
    std::vector<SeqInfo> seqs;
    struct RawKey {
        Vec3f pos = {};
        Quat4 quat = {};
    };
    std::vector<RawKey> keys;

    while (cursor + 32 <= size) {
        ChunkHdr ch;
        if (!ReadChunkHeader(data, size, cursor, ch)) break;
        const uint8_t* pay = data + ch.payloadOffset;

        if (ChunkPrefixIs(ch, "BONENAMES") && ch.dataSize >= 104 && ch.dataCount > 0 && ch.dataCount <= 65536) {
            bones.resize(static_cast<size_t>(ch.dataCount));
            for (int32_t i = 0; i < ch.dataCount; ++i) {
                const uint8_t* p = pay + static_cast<size_t>(i) * static_cast<size_t>(ch.dataSize);
                char nameBuf[65] = {};
                std::memcpy(nameBuf, p, 64);
                size_t nLen = std::strlen(nameBuf);
                while (nLen > 0 && nameBuf[nLen - 1] == ' ') nameBuf[--nLen] = '\0';

                int32_t parentIdx = 0;
                std::memcpy(&parentIdx, p + 72, 4);
                Quat4 q = {};
                Vec3f pos = {};
                std::memcpy(&q.x, p + 76, 4);
                std::memcpy(&q.y, p + 80, 4);
                std::memcpy(&q.z, p + 84, 4);
                std::memcpy(&q.w, p + 88, 4);
                std::memcpy(&pos.x, p + 92, 4);
                std::memcpy(&pos.y, p + 96, 4);
                std::memcpy(&pos.z, p + 100, 4);

                auto& dst = bones[static_cast<size_t>(i)];
                dst.name = nameBuf;
                dst.parent = (i == 0) ? -1 : parentIdx;
                dst.localQuat = q;
                dst.localPos = pos;
            }
        } else if (ChunkPrefixIs(ch, "ANIMINFO") && ch.dataSize >= 168 && ch.dataCount > 0 && ch.dataCount <= 65536) {
            seqs.resize(static_cast<size_t>(ch.dataCount));
            for (int32_t i = 0; i < ch.dataCount; ++i) {
                const uint8_t* p = pay + static_cast<size_t>(i) * static_cast<size_t>(ch.dataSize);
                std::memcpy(&seqs[static_cast<size_t>(i)].totalBones, p + 128, 4);
                std::memcpy(&seqs[static_cast<size_t>(i)].firstRawFrame, p + 160, 4);
                std::memcpy(&seqs[static_cast<size_t>(i)].numRawFrames, p + 164, 4);
            }
        } else if (ChunkPrefixIs(ch, "ANIMKEYS") && ch.dataSize >= 28 && ch.dataCount > 0 && ch.dataCount <= 16000000) {
            keys.resize(static_cast<size_t>(ch.dataCount));
            for (int32_t i = 0; i < ch.dataCount; ++i) {
                const uint8_t* p = pay + static_cast<size_t>(i) * static_cast<size_t>(ch.dataSize);
                RawKey k;
                std::memcpy(&k.pos.x, p + 0, 4);
                std::memcpy(&k.pos.y, p + 4, 4);
                std::memcpy(&k.pos.z, p + 8, 4);
                std::memcpy(&k.quat.x, p + 12, 4);
                std::memcpy(&k.quat.y, p + 16, 4);
                std::memcpy(&k.quat.z, p + 20, 4);
                std::memcpy(&k.quat.w, p + 24, 4);
                if (!std::isfinite(k.pos.x) || !std::isfinite(k.pos.y) || !std::isfinite(k.pos.z) ||
                    !std::isfinite(k.quat.x) || !std::isfinite(k.quat.y) || !std::isfinite(k.quat.z) || !std::isfinite(k.quat.w)) {
                    return false;
                }
                keys[static_cast<size_t>(i)] = k;
            }
        }
    }

    if (bones.empty() || seqs.empty() || keys.empty()) return false;
    const size_t nBones = bones.size();
    int64_t totalFrames = 0;
    for (const auto& s : seqs) {
        if (s.numRawFrames > 0) totalFrames += s.numRawFrames;
    }
    if (totalFrames <= 0) return false;

    const auto& seq0 = seqs[0];
    if (seq0.numRawFrames <= 0 || seq0.firstRawFrame < 0) return false;
    size_t keyStart = static_cast<size_t>(seq0.firstRawFrame) * nBones;
    if (keyStart + nBones > keys.size()) return false;

    for (size_t b = 0; b < nBones; ++b) {
        bones[b].localPos = keys[keyStart + b].pos;
        bones[b].localQuat = keys[keyStart + b].quat;
    }
    ComputeWorldSkeleton(bones);

    // Optional companion .psk/.pskx linear-blend skinning if exact bone hierarchy matches
    bool skinnedCompanion = false;
    if (!fullPath.empty()) {
        std::wstring dir = PathUtils::GetParentPath(fullPath);
        if (!dir.empty()) {
            const wchar_t* patterns[2] = { L"\\*.psk", L"\\*.pskx" };
            for (const wchar_t* pat : patterns) {
                if (skinnedCompanion) break;
                WIN32_FIND_DATAW fd = {};
                HANDLE hFind = FindFirstFileW((dir + pat).c_str(), &fd);
                if (hFind == INVALID_HANDLE_VALUE) continue;
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    std::wstring candPath = dir + L"\\" + fd.cFileName;
                    std::vector<uint8_t> candBytes;
                    if (PathUtils::ReadFileBytes(candPath, 64 * 1024 * 1024, candBytes) && !candBytes.empty()) {
                        PskRawData psk;
                        if (ParsePskRaw(candBytes.data(), candBytes.size(), psk) &&
                            psk.refBones.size() == nBones && !psk.weights.empty())
                        {
                            bool match = true;
                            for (size_t b = 0; b < nBones; ++b) {
                                if (psk.refBones[b].parent != bones[b].parent ||
                                    psk.refBones[b].name != bones[b].name) {
                                    match = false;
                                    break;
                                }
                            }
                            if (match) {
                                std::vector<Vec3f> deformed(psk.points.size(), { 0.0f, 0.0f, 0.0f });
                                std::vector<float> wSum(psk.points.size(), 0.0f);
                                for (const auto& rw : psk.weights) {
                                    if (rw.pointIdx >= psk.points.size() || rw.boneIdx >= nBones) continue;
                                    const auto& bindB = psk.refBones[rw.boneIdx];
                                    const auto& animB = bones[rw.boneIdx];
                                    // Express bind-pose vertex in bindB local space, then transform by animB
                                    Vec3f rel = {
                                        psk.points[rw.pointIdx].x - bindB.worldPos.x,
                                        psk.points[rw.pointIdx].y - bindB.worldPos.y,
                                        psk.points[rw.pointIdx].z - bindB.worldPos.z
                                    };
                                    Vec3f localPt = QuatRot(QuatConj(bindB.worldQuat), rel);
                                    Vec3f animRel = QuatRot(animB.worldQuat, localPt);
                                    Vec3f animPt = {
                                        animB.worldPos.x + animRel.x,
                                        animB.worldPos.y + animRel.y,
                                        animB.worldPos.z + animRel.z
                                    };
                                    deformed[rw.pointIdx].x += animPt.x * rw.weight;
                                    deformed[rw.pointIdx].y += animPt.y * rw.weight;
                                    deformed[rw.pointIdx].z += animPt.z * rw.weight;
                                    wSum[rw.pointIdx] += rw.weight;
                                }
                                for (size_t pi = 0; pi < psk.points.size(); ++pi) {
                                    if (wSum[pi] <= 1e-6f) deformed[pi] = psk.points[pi];
                                }
                                if (EmitPskGeometry(psk, deformed, mesh)) {
                                    skinnedCompanion = true;
                                    break;
                                }
                            }
                        }
                    }
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }
        }
    }

    if (!skinnedCompanion) {
        float span = 0.0f;
        for (size_t i = 1; i < nBones; ++i) {
            int32_t p = bones[i].parent;
            if (p >= 0 && static_cast<size_t>(p) < i) {
                Vec3f a = bones[static_cast<size_t>(p)].worldPos;
                Vec3f b = bones[i].worldPos;
                float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
                span = std::max(span, std::sqrt(dx * dx + dy * dy + dz * dz));
            }
        }
        float fallbackR = std::max(0.25f, span * 0.1f);
        int bonePrisms = 0;
        for (size_t i = 1; i < nBones; ++i) {
            int32_t p = bones[i].parent;
            if (p >= 0 && static_cast<size_t>(p) < i) {
                Md5Detail::AppendBoneOctahedron(
                    mesh,
                    UnrealToViewport(bones[static_cast<size_t>(p)].worldPos),
                    UnrealToViewport(bones[i].worldPos),
                    fallbackR);
                ++bonePrisms;
            }
        }
        if (bonePrisms == 0) {
            Vec3f rp = UnrealToViewport(bones[0].worldPos);
            Md5Detail::AppendBoneOctahedron(mesh, rp, rp, 1.0f);
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Unreal ActorX Animation (.psa)";
    mesh.animFrameCount = static_cast<size_t>(totalFrames);
    return true;
}

} // namespace Formats
