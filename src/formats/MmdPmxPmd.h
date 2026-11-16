#pragma once
// MmdPmxPmd.h — MikuMikuDance / VTuber Polygon Models (.pmx 2.0/2.1, .pmd 1.0) & Vocaloid Motion Data (.vmd)

namespace Formats {

namespace MmdDetail {

inline Vec3f MmdToViewport(float x, float y, float z) {
    // MMD uses left-handed Y-up (+Z away from viewer); mirror Z for Explorer2's right-handed Y-up camera
    return { x, y, -z };
}

inline bool ReadPmxVarIndex(const uint8_t* data, size_t size, size_t& pos, uint8_t idxSize, bool isVertexUnsigned, int32_t& outIdx) {
    if (pos + idxSize > size) return false;
    if (idxSize == 1) {
        if (isVertexUnsigned) outIdx = static_cast<int32_t>(data[pos]);
        else outIdx = static_cast<int32_t>(static_cast<int8_t>(data[pos]));
        pos += 1;
        return true;
    }
    if (idxSize == 2) {
        if (isVertexUnsigned) {
            uint16_t u = 0;
            std::memcpy(&u, data + pos, 2);
            outIdx = static_cast<int32_t>(u);
        } else {
            int16_t s = 0;
            std::memcpy(&s, data + pos, 2);
            outIdx = static_cast<int32_t>(s);
        }
        pos += 2;
        return true;
    }
    if (idxSize == 4) {
        int32_t s = 0;
        std::memcpy(&s, data + pos, 4);
        outIdx = s;
        pos += 4;
        return true;
    }
    return false;
}

} // namespace MmdDetail

inline bool ParseMmdPmx(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 32) return false;
    if (std::memcmp(data, "PMX ", 4) != 0) return false;

    float ver = 0.0f;
    std::memcpy(&ver, data + 4, 4);
    if (!std::isfinite(ver) || ver < 1.99f || ver > 2.2f) return false;

    uint8_t globalsCount = data[8];
    if (globalsCount < 8 || 9 + static_cast<size_t>(globalsCount) > size) return false;
    const uint8_t* globals = data + 9;
    const uint8_t addVec4Count = globals[1];
    const uint8_t vertIdxSize  = globals[2];
    const uint8_t boneIdxSize  = globals[5];
    if (addVec4Count > 8) return false;
    if ((vertIdxSize != 1 && vertIdxSize != 2 && vertIdxSize != 4) ||
        (boneIdxSize != 1 && boneIdxSize != 2 && boneIdxSize != 4)) {
        return false;
    }

    size_t pos = 9 + static_cast<size_t>(globalsCount);
    // Skip 4 text fields: ModelNameJP, ModelNameEN, CommentJP, CommentEN
    for (int t = 0; t < 4; ++t) {
        if (pos + 4 > size) return false;
        int32_t byteLen = 0;
        std::memcpy(&byteLen, data + pos, 4);
        pos += 4;
        if (byteLen < 0 || static_cast<size_t>(byteLen) > size - pos) return false;
        pos += static_cast<size_t>(byteLen);
    }

    if (pos + 4 > size) return false;
    int32_t numVerts = 0;
    std::memcpy(&numVerts, data + pos, 4);
    pos += 4;
    if (numVerts <= 0 || numVerts > 8000000) return false;

    mesh.vertices.resize(static_cast<size_t>(numVerts));
    const size_t addVecBytes = static_cast<size_t>(addVec4Count) * 16;

    for (int32_t i = 0; i < numVerts; ++i) {
        // pos(12) + normal(12) + uv(8) + addVec4(16*N) + deformType(1)
        if (pos + 32 + addVecBytes + 1 > size) return false;
        float vx = 0.0f, vy = 0.0f, vz = 0.0f;
        std::memcpy(&vx, data + pos + 0, 4);
        std::memcpy(&vy, data + pos + 4, 4);
        std::memcpy(&vz, data + pos + 8, 4);
        if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return false;
        mesh.vertices[static_cast<size_t>(i)] = MmdDetail::MmdToViewport(vx, vy, vz);
        pos += 32 + addVecBytes;

        uint8_t deformType = data[pos++];
        size_t weightBytes = 0;
        switch (deformType) {
        case 0: // BDEF1
            weightBytes = static_cast<size_t>(boneIdxSize);
            break;
        case 1: // BDEF2
            weightBytes = static_cast<size_t>(boneIdxSize) * 2 + 4;
            break;
        case 2: // BDEF4
        case 4: // QDEF (PMX 2.1)
            weightBytes = static_cast<size_t>(boneIdxSize) * 4 + 16;
            break;
        case 3: // SDEF
            weightBytes = static_cast<size_t>(boneIdxSize) * 2 + 40;
            break;
        default:
            return false;
        }
        // Plus 4-byte edgeScale at end of vertex
        if (pos + weightBytes + 4 > size) return false;
        pos += weightBytes + 4;
    }

    if (pos + 4 > size) return false;
    int32_t numIndices = 0;
    std::memcpy(&numIndices, data + pos, 4);
    pos += 4;
    if (numIndices < 3 || (numIndices % 3) != 0 || numIndices > 24000000) return false;
    if (static_cast<uint64_t>(numIndices) * static_cast<uint64_t>(vertIdxSize) > size - pos) return false;

    const int32_t nTris = numIndices / 3;
    mesh.tris.reserve(static_cast<size_t>(nTris));
    for (int32_t t = 0; t < nTris; ++t) {
        int32_t a = 0, b = 0, c = 0;
        if (!MmdDetail::ReadPmxVarIndex(data, size, pos, vertIdxSize, true, a) ||
            !MmdDetail::ReadPmxVarIndex(data, size, pos, vertIdxSize, true, b) ||
            !MmdDetail::ReadPmxVarIndex(data, size, pos, vertIdxSize, true, c)) {
            return false;
        }
        if (a < 0 || a >= numVerts || b < 0 || b >= numVerts || c < 0 || c >= numVerts) return false;
        if (a == b || b == c || c == a) continue;
        // Flip (a, c, b) for Z-mirrored right-handed CCW winding
        mesh.tris.push_back({ static_cast<uint32_t>(a), static_cast<uint32_t>(c), static_cast<uint32_t>(b), {} });
    }

    if (mesh.tris.empty()) return false;
    mesh.formatName = (ver >= 2.05f)
        ? L"MikuMikuDance PMX 2.1 Model (.pmx)"
        : L"MikuMikuDance PMX 2.0 Model (.pmx)";
    return true;
}

inline bool ParseMmdPmd(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    // Header: "Pmd" (3) + float version (4) + name[20] + comment[256] = 283 bytes + uint32 numVerts (4) = 287 bytes
    if (!data || size < 291) return false;
    if (std::memcmp(data, "Pmd", 3) != 0) return false;
    float ver = 0.0f;
    std::memcpy(&ver, data + 3, 4);
    if (!std::isfinite(ver) || ver < 0.9f || ver > 1.1f) return false;

    size_t pos = 283;
    uint32_t numVerts = 0;
    std::memcpy(&numVerts, data + pos, 4);
    pos += 4;
    if (numVerts == 0 || numVerts > 1000000) return false;
    uint64_t vertBytes = static_cast<uint64_t>(numVerts) * 38ULL;
    if (vertBytes + 4ULL > size - pos) return false;

    mesh.vertices.resize(numVerts);
    for (uint32_t i = 0; i < numVerts; ++i) {
        const uint8_t* vp = data + pos + static_cast<size_t>(i) * 38;
        float vx = 0.0f, vy = 0.0f, vz = 0.0f;
        std::memcpy(&vx, vp + 0, 4);
        std::memcpy(&vy, vp + 4, 4);
        std::memcpy(&vz, vp + 8, 4);
        if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return false;
        mesh.vertices[i] = MmdDetail::MmdToViewport(vx, vy, vz);
    }
    pos += static_cast<size_t>(vertBytes);

    uint32_t numIndices = 0;
    std::memcpy(&numIndices, data + pos, 4);
    pos += 4;
    if (numIndices < 3 || (numIndices % 3) != 0 || numIndices > 12000000) return false;
    if (static_cast<uint64_t>(numIndices) * 2ULL > size - pos) return false;

    const uint32_t nTris = numIndices / 3;
    mesh.tris.reserve(nTris);
    for (uint32_t t = 0; t < nTris; ++t) {
        uint16_t a = 0, b = 0, c = 0;
        std::memcpy(&a, data + pos + static_cast<size_t>(t) * 6 + 0, 2);
        std::memcpy(&b, data + pos + static_cast<size_t>(t) * 6 + 2, 2);
        std::memcpy(&c, data + pos + static_cast<size_t>(t) * 6 + 4, 2);
        if (a >= numVerts || b >= numVerts || c >= numVerts) return false;
        if (a == b || b == c || c == a) continue;
        mesh.tris.push_back({ a, c, b, {} });
    }

    if (mesh.tris.empty()) return false;
    mesh.formatName = L"MikuMikuDance PMD 1.0 Model (.pmd)";
    return true;
}

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
