#pragma once
#include "MeshBuild3D.h"
// ActorXPsk.h — Unreal Engine ActorX Skeletal/Static Meshes (.psk, .pskx) & Animations (.psa)

namespace Formats {

namespace ActorXDetail {

struct ChunkHdr {
    char id[21] = {};
    int32_t typeFlag = 0;
    int32_t dataSize = 0;
    int32_t dataCount = 0;
    size_t payloadOffset = 0;
    size_t payloadBytes = 0;
};

inline bool ReadChunkHeader(const uint8_t* data, size_t size, size_t& cursor, ChunkHdr& out) {
    if (cursor + 32 > size) return false;
    std::memcpy(out.id, data + cursor, 20);
    out.id[20] = '\0';
    std::memcpy(&out.typeFlag, data + cursor + 20, 4);
    std::memcpy(&out.dataSize, data + cursor + 24, 4);
    std::memcpy(&out.dataCount, data + cursor + 28, 4);
    if (out.dataSize < 0 || out.dataCount < 0) return false;
    uint64_t bytes64 = static_cast<uint64_t>(out.dataSize) * static_cast<uint64_t>(out.dataCount);
    if (bytes64 > size - (cursor + 32)) return false;
    out.payloadOffset = cursor + 32;
    out.payloadBytes = static_cast<size_t>(bytes64);
    cursor = out.payloadOffset + out.payloadBytes;
    return true;
}

inline bool ChunkPrefixIs(const ChunkHdr& h, const char* prefix) {
    size_t n = std::strlen(prefix);
    return std::strncmp(h.id, prefix, n) == 0;
}

struct Quat4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

inline Quat4 QuatNorm(const Quat4& q) {
    float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l > 1e-7f) {
        float inv = 1.0f / l;
        return { q.x * inv, q.y * inv, q.z * inv, q.w * inv };
    }
    return { 0.0f, 0.0f, 0.0f, 1.0f };
}

inline Quat4 QuatConj(const Quat4& q) {
    return { -q.x, -q.y, -q.z, q.w };
}

using MeshBuild3D::QuatMul;

inline Vec3f QuatRot(const Quat4& q, const Vec3f& v) { return MeshBuild3D::QuatRotate(q, v); }

// Unreal Z-up coords -> Explorer2 Y-up viewport
inline Vec3f UnrealToViewport(const Vec3f& p) {
    return { p.x, p.z, -p.y };
}

struct ActorXBone {
    std::string name;
    int32_t parent = -1;
    Vec3f localPos = {};
    Quat4 localQuat = {};
    Vec3f worldPos = {};
    Quat4 worldQuat = {};
};

inline void ComputeWorldSkeleton(std::vector<ActorXBone>& bones) {
    for (size_t i = 0; i < bones.size(); ++i) {
        auto& b = bones[i];
        // In ActorX, non-root bone quaternions store conjugated orientation relative to standard Hamilton
        Quat4 q = (i == 0) ? QuatNorm({ b.localQuat.x, b.localQuat.y, b.localQuat.z, -b.localQuat.w })
                           : QuatNorm(QuatConj(b.localQuat));
        int32_t p = (i == 0) ? -1 : b.parent;
        if (p < 0 || static_cast<size_t>(p) >= i) {
            b.worldPos = b.localPos;
            b.worldQuat = q;
        } else {
            const auto& par = bones[static_cast<size_t>(p)];
            Vec3f rp = QuatRot(par.worldQuat, b.localPos);
            b.worldPos = { par.worldPos.x + rp.x, par.worldPos.y + rp.y, par.worldPos.z + rp.z };
            b.worldQuat = QuatNorm(QuatMul(par.worldQuat, q));
        }
    }
}

struct PskRawData {
    std::vector<Vec3f> points;
    std::vector<uint32_t> wedgePointIndices;
    struct Tri { uint32_t w0, w1, w2; };
    std::vector<Tri> faces;
    std::vector<ActorXBone> refBones;
    struct Weight { float weight; uint32_t pointIdx; uint32_t boneIdx; };
    std::vector<Weight> weights;
};

inline bool ParsePskRaw(const uint8_t* data, size_t size, PskRawData& out) {
    if (!data || size < 32) return false;
    size_t cursor = 0;
    ChunkHdr first;
    if (!ReadChunkHeader(data, size, cursor, first) || !ChunkPrefixIs(first, "ACTRHEAD")) {
        return false;
    }

    while (cursor + 32 <= size) {
        ChunkHdr ch;
        if (!ReadChunkHeader(data, size, cursor, ch)) break;
        const uint8_t* pay = data + ch.payloadOffset;

        if (ChunkPrefixIs(ch, "PNTS0000") && ch.dataSize >= 12 && ch.dataCount > 0 && ch.dataCount <= 8000000) {
            out.points.resize(static_cast<size_t>(ch.dataCount));
            for (int32_t i = 0; i < ch.dataCount; ++i) {
                const uint8_t* p = pay + static_cast<size_t>(i) * static_cast<size_t>(ch.dataSize);
                Vec3f v = {};
                std::memcpy(&v.x, p + 0, 4);
                std::memcpy(&v.y, p + 4, 4);
                std::memcpy(&v.z, p + 8, 4);
                if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return false;
                out.points[static_cast<size_t>(i)] = v;
            }
        } else if (ChunkPrefixIs(ch, "VTXW0000") && ch.dataSize >= 12 && ch.dataCount > 0 && ch.dataCount <= 16000000) {
            out.wedgePointIndices.resize(static_cast<size_t>(ch.dataCount));
            const size_t nPts = out.points.size();
            for (int32_t i = 0; i < ch.dataCount; ++i) {
                const uint8_t* p = pay + static_cast<size_t>(i) * static_cast<size_t>(ch.dataSize);
                uint32_t idx32 = 0;
                uint16_t idx16 = 0;
                std::memcpy(&idx32, p, 4);
                std::memcpy(&idx16, p, 2);
                uint32_t chosen = idx32;
                if (nPts > 0 && nPts <= 65536 && idx32 >= nPts && idx16 < nPts) {
                    chosen = idx16;
                }
                out.wedgePointIndices[static_cast<size_t>(i)] = chosen;
            }
        } else if (ChunkPrefixIs(ch, "FACE0000") && ch.dataSize >= 6 && ch.dataCount > 0 && ch.dataCount <= 16000000) {
            out.faces.reserve(out.faces.size() + static_cast<size_t>(ch.dataCount));
            for (int32_t i = 0; i < ch.dataCount; ++i) {
                const uint8_t* p = pay + static_cast<size_t>(i) * static_cast<size_t>(ch.dataSize);
                uint16_t w0 = 0, w1 = 0, w2 = 0;
                std::memcpy(&w0, p + 0, 2);
                std::memcpy(&w1, p + 2, 2);
                std::memcpy(&w2, p + 4, 2);
                out.faces.push_back({ w0, w1, w2 });
            }
        } else if ((ChunkPrefixIs(ch, "FACE3200") || ChunkPrefixIs(ch, "FACE0032")) &&
                   ch.dataSize >= 12 && ch.dataCount > 0 && ch.dataCount <= 16000000) {
            out.faces.reserve(out.faces.size() + static_cast<size_t>(ch.dataCount));
            for (int32_t i = 0; i < ch.dataCount; ++i) {
                const uint8_t* p = pay + static_cast<size_t>(i) * static_cast<size_t>(ch.dataSize);
                uint32_t w0 = 0, w1 = 0, w2 = 0;
                std::memcpy(&w0, p + 0, 4);
                std::memcpy(&w1, p + 4, 4);
                std::memcpy(&w2, p + 8, 4);
                out.faces.push_back({ w0, w1, w2 });
            }
        } else if (ChunkPrefixIs(ch, "REFSKEL") && ch.dataSize >= 104 && ch.dataCount > 0 && ch.dataCount <= 65536) {
            out.refBones.resize(static_cast<size_t>(ch.dataCount));
            for (int32_t i = 0; i < ch.dataCount; ++i) {
                const uint8_t* p = pay + static_cast<size_t>(i) * static_cast<size_t>(ch.dataSize);
                char nameBuf[65] = {};
                std::memcpy(nameBuf, p, 64);
                // Trim trailing spaces used by classic ActorX exporters
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

                auto& dst = out.refBones[static_cast<size_t>(i)];
                dst.name = nameBuf;
                dst.parent = (i == 0) ? -1 : parentIdx;
                dst.localQuat = q;
                dst.localPos = pos;
            }
            ComputeWorldSkeleton(out.refBones);
        } else if ((ChunkPrefixIs(ch, "RAWWEIGHTS") || ChunkPrefixIs(ch, "RAWW0000")) &&
                   ch.dataSize >= 12 && ch.dataCount > 0 && ch.dataCount <= 32000000) {
            out.weights.reserve(static_cast<size_t>(ch.dataCount));
            for (int32_t i = 0; i < ch.dataCount; ++i) {
                const uint8_t* p = pay + static_cast<size_t>(i) * static_cast<size_t>(ch.dataSize);
                float w = 0.0f;
                int32_t ptIdx = 0, boneIdx = 0;
                std::memcpy(&w, p + 0, 4);
                std::memcpy(&ptIdx, p + 4, 4);
                std::memcpy(&boneIdx, p + 8, 4);
                if (std::isfinite(w) && w > 0.0f && ptIdx >= 0 && boneIdx >= 0) {
                    out.weights.push_back({ w, static_cast<uint32_t>(ptIdx), static_cast<uint32_t>(boneIdx) });
                }
            }
        }
    }

    return !out.points.empty() && !out.wedgePointIndices.empty() && !out.faces.empty();
}

inline bool EmitPskGeometry(const PskRawData& psk, const std::vector<Vec3f>& worldPts, LoadedMesh3D& mesh) {
    if (worldPts.size() != psk.points.size() || psk.faces.empty()) return false;
    mesh.vertices.resize(worldPts.size());
    for (size_t i = 0; i < worldPts.size(); ++i) {
        mesh.vertices[i] = UnrealToViewport(worldPts[i]);
    }
    const size_t nWedges = psk.wedgePointIndices.size();
    const size_t nPts = worldPts.size();
    mesh.tris.reserve(psk.faces.size());
    for (const auto& f : psk.faces) {
        if (f.w0 >= nWedges || f.w1 >= nWedges || f.w2 >= nWedges) return false;
        uint32_t p0 = psk.wedgePointIndices[f.w0];
        uint32_t p1 = psk.wedgePointIndices[f.w1];
        uint32_t p2 = psk.wedgePointIndices[f.w2];
        if (p0 >= nPts || p1 >= nPts || p2 >= nPts) return false;
        if (p0 == p1 || p1 == p2 || p2 == p0) continue;
        mesh.tris.push_back({ p0, p2, p1, {} });
    }
    return !mesh.tris.empty();
}

} // namespace ActorXDetail

inline bool ParseActorXPsk(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    ActorXDetail::PskRawData psk;
    if (!ActorXDetail::ParsePskRaw(data, size, psk)) return false;
    if (!ActorXDetail::EmitPskGeometry(psk, psk.points, mesh)) return false;
    mesh.formatName = (ext == L".pskx")
        ? L"Unreal ActorX Extended Mesh (.pskx)"
        : L"Unreal ActorX Skeletal Mesh (.psk)";
    mesh.animFrameCount = 1;
    return true;
}

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
