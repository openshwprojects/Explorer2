#pragma once
// MmdVmd.h — Vocaloid Motion Data (.vmd) 3D trajectory & skeleton reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseMmdVmd(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 54) return false;
    size_t pos = 0;
    if (std::memcmp(data, "Vocaloid Motion Data 0002", 25) == 0) {
        pos = 50; // 30-byte header + 20-byte model name
    } else if (std::memcmp(data, "Vocaloid Motion Data file", 25) == 0) {
        pos = 40; // 30-byte header + 10-byte model name
    } else {
        return false;
    }

    if (pos + 4 > size) return false;
    uint32_t boneKeys = 0;
    std::memcpy(&boneKeys, data + pos, 4);
    pos += 4;
    if (boneKeys > 2000000) return false;
    if (static_cast<uint64_t>(boneKeys) * 111ULL > size - pos) return false;

    struct TrackPt {
        uint32_t frame = 0;
        Vec3f pos = {};
        Vec3f tipOffset = {};
    };
    std::map<std::string, std::vector<TrackPt>> tracks;
    uint32_t maxFrame = 0;

    for (uint32_t i = 0; i < boneKeys; ++i) {
        const uint8_t* kp = data + pos + static_cast<size_t>(i) * 111;
        char bname[16] = {};
        std::memcpy(bname, kp, 15);
        uint32_t frameNo = 0;
        float tx = 0.0f, ty = 0.0f, tz = 0.0f;
        float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;
        std::memcpy(&frameNo, kp + 15, 4);
        std::memcpy(&tx, kp + 19, 4);
        std::memcpy(&ty, kp + 23, 4);
        std::memcpy(&tz, kp + 27, 4);
        std::memcpy(&qx, kp + 31, 4);
        std::memcpy(&qy, kp + 35, 4);
        std::memcpy(&qz, kp + 39, 4);
        std::memcpy(&qw, kp + 43, 4);
        if (!std::isfinite(tx) || !std::isfinite(ty) || !std::isfinite(tz) ||
            !std::isfinite(qx) || !std::isfinite(qy) || !std::isfinite(qz) || !std::isfinite(qw)) {
            return false;
        }
        maxFrame = std::max(maxFrame, frameNo);

        // Rotate a canonical +Y bone vector (0, 2.5, 0) by keyframe quaternion so rotation-only keyframes also produce distinct 3D poses!
        ActorXDetail::Quat4 q = ActorXDetail::QuatNorm({ qx, qy, qz, qw });
        Vec3f dir = ActorXDetail::QuatRot(q, { 0.0f, 2.5f, 0.0f });
        Vec3f vp = MmdDetail::MmdToViewport(tx, ty, tz);
        Vec3f vd = MmdDetail::MmdToViewport(dir.x, dir.y, dir.z);
        tracks[std::string(bname)].push_back({ frameNo, vp, vd });
    }
    pos += static_cast<size_t>(boneKeys) * 111;

    // Also support VMD camera-only motion files if boneKeys == 0
    if (boneKeys == 0) {
        // Skip morph frames (uint32 count * 23 bytes), then read camera frames (uint32 count * 61 bytes)
        if (pos + 4 > size) return false;
        uint32_t morphKeys = 0;
        std::memcpy(&morphKeys, data + pos, 4);
        pos += 4;
        if (static_cast<uint64_t>(morphKeys) * 23ULL + 4ULL > size - pos) return false;
        pos += static_cast<size_t>(morphKeys) * 23;

        uint32_t camKeys = 0;
        std::memcpy(&camKeys, data + pos, 4);
        pos += 4;
        if (camKeys == 0 || camKeys > 1000000 || static_cast<uint64_t>(camKeys) * 61ULL > size - pos) return false;
        std::vector<TrackPt> camPts;
        for (uint32_t i = 0; i < camKeys; ++i) {
            const uint8_t* cp = data + pos + static_cast<size_t>(i) * 61;
            uint32_t frameNo = 0;
            float dist = 0.0f, cx = 0.0f, cy = 0.0f, cz = 0.0f;
            std::memcpy(&frameNo, cp + 0, 4);
            std::memcpy(&dist, cp + 4, 4);
            std::memcpy(&cx, cp + 8, 4);
            std::memcpy(&cy, cp + 12, 4);
            std::memcpy(&cz, cp + 16, 4);
            maxFrame = std::max(maxFrame, frameNo);
            camPts.push_back({ frameNo, MmdDetail::MmdToViewport(cx, cy, cz), { 0.0f, 1.5f, std::fabs(dist) * 0.1f + 0.5f } });
        }
        tracks["__camera__"] = std::move(camPts);
    }

    if (tracks.empty()) return false;

    for (auto& kv : tracks) {
        auto& pts = kv.second;
        std::sort(pts.begin(), pts.end(), [](const TrackPt& a, const TrackPt& b) { return a.frame < b.frame; });
        // Emit oriented bone prism at first keyframe + trajectory segments between consecutive keyframes
        if (!pts.empty()) {
            Vec3f base = pts[0].pos;
            Vec3f tip = { base.x + pts[0].tipOffset.x, base.y + pts[0].tipOffset.y, base.z + pts[0].tipOffset.z };
            Md5Detail::AppendBoneOctahedron(mesh, base, tip, 0.3f);
        }
        for (size_t i = 1; i < pts.size() && mesh.tris.size() < 60000; ++i) {
            Vec3f a = pts[i - 1].pos;
            Vec3f b = pts[i].pos;
            float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
            if (dx * dx + dy * dy + dz * dz > 1e-4f) {
                Md5Detail::AppendBoneOctahedron(mesh, a, b, 0.2f);
            } else {
                Vec3f tip = { b.x + pts[i].tipOffset.x, b.y + pts[i].tipOffset.y, b.z + pts[i].tipOffset.z };
                Md5Detail::AppendBoneOctahedron(mesh, b, tip, 0.25f);
            }
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Vocaloid Motion Data (.vmd)";
    mesh.animFrameCount = static_cast<size_t>(maxFrame + 1);
    return true;
}

} // namespace Formats
