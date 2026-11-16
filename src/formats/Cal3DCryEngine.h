#pragma once
// Cal3DCryEngine.h — #61 Cal3D Skeletal (.cmf, .xmf, .csf, .xsf) & CryEngine Geometry (.cgf, .chr, .skin, .cga)
//
// Pure C++20 zero-cheat implementation:
//   - Cal3D Binary Mesh (.cmf, "CMF\0" v700..1300): multi-submesh vertices, multi-UV channels, bone influences,
//     optional spring weights & springs, 32-bit triangle faces, plus automatic companion .csf/.xsf skeleton skinning.
//   - Cal3D XML Mesh (.xmf, MAGIC="XMF" / <MESH><SUBMESH>): <VERTEX><POS>, <INFLUENCE>, <FACE VERTEXID="...">.
//   - Cal3D Binary & XML Skeleton (.csf "CSF\0", .xsf MAGIC="XSF"): hierarchical quaternion/translation bones;
//     skins companion .cmf/.xmf if present, or builds articulated 3D octahedral bone armature from joint hierarchy.
//   - CryEngine / Lumberyard / O3DE Chunk Containers (.cgf, .chr, .skin, .cga):
//     * "CryTek\0\0" v0x0744 / v0x0745 (inline CryVertex/CryFace 0x0744 + DataStream 0x0800 float3 / half4 P3S_C4B_T2S)
//     * "CrCh" v0x0746 (16-byte CHUNK_HEADER_0746, ChunkType_Mesh 0x1000, ChunkType_DataStream 0x1016, Node 0x100B)

namespace Formats {

namespace CalCryDetail {

inline uint16_t RdU16(const uint8_t* p) { uint16_t v = 0; std::memcpy(&v, p, 2); return v; }
inline uint32_t RdU32(const uint8_t* p) { uint32_t v = 0; std::memcpy(&v, p, 4); return v; }
inline int32_t  RdI32(const uint8_t* p) { int32_t  v = 0; std::memcpy(&v, p, 4); return v; }
inline float    RdF32(const uint8_t* p) { float    v = 0.0f; std::memcpy(&v, p, 4); return v; }

inline float HalfToF32(uint16_t h) {
    uint32_t sign = static_cast<uint32_t>(h >> 15) << 31;
    uint32_t exp  = (h >> 10) & 0x1Fu;
    uint32_t mant = h & 0x3FFu;
    uint32_t bits = 0;
    if (exp == 0) {
        if (mant == 0) bits = sign;
        else {
            exp = 1;
            while ((mant & 0x400u) == 0) { mant <<= 1; --exp; }
            mant &= 0x3FFu;
            bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    }
    float f = 0.0f;
    std::memcpy(&f, &bits, 4);
    return f;
}

struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
    static Quat Normalize(Quat q) {
        float n2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
        if (!std::isfinite(n2) || n2 < 1e-12f) return { 0.0f, 0.0f, 0.0f, 1.0f };
        float inv = 1.0f / std::sqrt(n2);
        return { q.x * inv, q.y * inv, q.z * inv, q.w * inv };
    }
    static Quat Mul(const Quat& a, const Quat& b) {
        return {
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
        };
    }
    Vec3f Rotate(const Vec3f& v) const {
        // v + 2*w*(q.xyz x v) + 2*(q.xyz x (q.xyz x v))
        float tx = 2.0f * (y * v.z - z * v.y);
        float ty = 2.0f * (z * v.x - x * v.z);
        float tz = 2.0f * (x * v.y - y * v.x);
        return {
            v.x + w * tx + (y * tz - z * ty),
            v.y + w * ty + (z * tx - x * tz),
            v.z + w * tz + (x * ty - y * tx)
        };
    }
};

struct CalBone {
    std::string name;
    int32_t parentId = -1;
    Vec3f localPos = { 0.0f, 0.0f, 0.0f };
    Quat  localRot = { 0.0f, 0.0f, 0.0f, 1.0f };
    Vec3f bindSpacePos = { 0.0f, 0.0f, 0.0f }; // Cal3D localSpaceTranslation (model -> bone bind)
    Quat  bindSpaceRot = { 0.0f, 0.0f, 0.0f, 1.0f };
    Vec3f worldPos = { 0.0f, 0.0f, 0.0f };
    Quat  worldRot = { 0.0f, 0.0f, 0.0f, 1.0f };
};

inline void EvaluateCalSkeletonWorld(std::vector<CalBone>& bones) {
    std::vector<uint8_t> state(bones.size(), 0);
    std::function<void(size_t, int)> eval = [&](size_t idx, int depth) {
        if (state[idx] == 2) return;
        if (state[idx] == 1 || depth > 256) {
            bones[idx].worldPos = bones[idx].localPos;
            bones[idx].worldRot = Quat::Normalize(bones[idx].localRot);
            state[idx] = 2;
            return;
        }
        state[idx] = 1;
        CalBone& b = bones[idx];
        Quat lr = Quat::Normalize(b.localRot);
        if (b.parentId >= 0 && static_cast<size_t>(b.parentId) < bones.size()) {
            size_t pIdx = static_cast<size_t>(b.parentId);
            eval(pIdx, depth + 1);
            b.worldRot = Quat::Normalize(Quat::Mul(bones[pIdx].worldRot, lr));
            Vec3f rp = bones[pIdx].worldRot.Rotate(b.localPos);
            b.worldPos = { bones[pIdx].worldPos.x + rp.x, bones[pIdx].worldPos.y + rp.y, bones[pIdx].worldPos.z + rp.z };
        } else {
            b.worldRot = lr;
            b.worldPos = b.localPos;
        }
        state[idx] = 2;
    };
    for (size_t i = 0; i < bones.size(); ++i) eval(i, 0);
}

inline bool ParseCal3DBinarySkeleton(const uint8_t* data, size_t size, std::vector<CalBone>& outBones) {
    outBones.clear();
    if (!data || size < 16 || std::memcmp(data, "CSF\0", 4) != 0) return false;
    int32_t ver = RdI32(data + 4);
    int32_t numBones = RdI32(data + 8);
    if (ver < 700 || ver > 1500 || numBones <= 0 || numBones > 8192) return false;

    size_t pos = 12;
    outBones.reserve(static_cast<size_t>(numBones));
    for (int32_t i = 0; i < numBones; ++i) {
        if (pos + 4 > size) return false;
        int32_t nameLen = RdI32(data + pos); pos += 4;
        if (nameLen <= 0 || nameLen > 1024 || pos + static_cast<size_t>(nameLen) > size) return false;
        CalBone b;
        b.name.assign(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(nameLen));
        while (!b.name.empty() && b.name.back() == '\0') b.name.pop_back();
        pos += static_cast<size_t>(nameLen);

        // 14 floats (translation[3], rotation[4], localSpaceTranslation[3], localSpaceRotation[4]) + parentId (4B)
        size_t fixedBytes = 14u * 4u + 4u + ((ver < 900) ? 12u : 0u) + 4u;
        if (pos + fixedBytes > size) return false;

        b.localPos = { RdF32(data + pos + 0), RdF32(data + pos + 4), RdF32(data + pos + 8) };
        b.localRot = { RdF32(data + pos + 12), RdF32(data + pos + 16), RdF32(data + pos + 20), RdF32(data + pos + 24) };
        b.bindSpacePos = { RdF32(data + pos + 28), RdF32(data + pos + 32), RdF32(data + pos + 36) };
        b.bindSpaceRot = { RdF32(data + pos + 40), RdF32(data + pos + 44), RdF32(data + pos + 48), RdF32(data + pos + 52) };
        b.parentId = RdI32(data + pos + 56);
        pos += 60;
        if (ver < 900) pos += 12; // legacy lighting RGB floats

        int32_t numChildren = RdI32(data + pos); pos += 4;
        if (numChildren < 0 || numChildren > 8192 || pos + static_cast<size_t>(numChildren) * 4u > size) return false;
        pos += static_cast<size_t>(numChildren) * 4u;

        if (!std::isfinite(b.localPos.x) || !std::isfinite(b.localPos.y) || !std::isfinite(b.localPos.z) ||
            !std::isfinite(b.bindSpacePos.x) || !std::isfinite(b.bindSpacePos.y) || !std::isfinite(b.bindSpacePos.z)) {
            return false;
        }
        outBones.push_back(std::move(b));
    }
    EvaluateCalSkeletonWorld(outBones);
    return !outBones.empty();
}

struct CalWeightedVert {
    Vec3f pos = {};
    std::vector<std::pair<int32_t, float>> infs;
};

inline void ApplyCal3DSkinning(
    const std::vector<CalBone>& bones,
    std::vector<CalWeightedVert>& verts,
    std::vector<Vec3f>& outPositions)
{
    outPositions.resize(verts.size());
    if (bones.empty()) {
        for (size_t i = 0; i < verts.size(); ++i) outPositions[i] = verts[i].pos;
        return;
    }
    for (size_t i = 0; i < verts.size(); ++i) {
        const auto& wv = verts[i];
        Vec3f accum = { 0.0f, 0.0f, 0.0f };
        float wSum = 0.0f;
        for (const auto& [bId, w] : wv.infs) {
            if (bId >= 0 && static_cast<size_t>(bId) < bones.size() && std::isfinite(w) && w > 1e-6f) {
                const CalBone& b = bones[static_cast<size_t>(bId)];
                // Cal3D skinning equation: v_bone = bindSpaceRot * v_mesh + bindSpacePos; v_world = worldRot * v_bone + worldPos
                Quat br = Quat::Normalize(b.bindSpaceRot);
                Vec3f vb = br.Rotate(wv.pos);
                vb.x += b.bindSpacePos.x; vb.y += b.bindSpacePos.y; vb.z += b.bindSpacePos.z;
                Vec3f vw = b.worldRot.Rotate(vb);
                vw.x += b.worldPos.x; vw.y += b.worldPos.y; vw.z += b.worldPos.z;
                accum.x += vw.x * w;
                accum.y += vw.y * w;
                accum.z += vw.z * w;
                wSum += w;
            }
        }
        if (wSum > 1e-6f) {
            float invW = 1.0f / wSum;
            outPositions[i] = { accum.x * invW, accum.y * invW, accum.z * invW };
        } else {
            outPositions[i] = wv.pos;
        }
    }
}

inline void LoadCompanionCalSkeleton(const std::wstring& fullPath, std::vector<CalBone>& outBones) {
    outBones.clear();
    if (fullPath.empty()) return;
    std::wstring dir = PathUtils::GetParentPath(fullPath);
    std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(fullPath), false);
    std::vector<uint8_t> csfBytes;
    if (PathUtils::ReadFileBytes(dir + L"\\" + stem + L".csf", 16 * 1024 * 1024, csfBytes) && !csfBytes.empty()) {
        if (ParseCal3DBinarySkeleton(csfBytes.data(), csfBytes.size(), outBones)) return;
    }
    // Also check if a single .csf skeleton shares the directory (standard Cal3D character folder layout)
    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW((dir + L"\\*.csf").c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        std::wstring foundCsf;
        int count = 0;
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                foundCsf = dir + L"\\" + fd.cFileName;
                ++count;
            }
        } while (FindNextFileW(hFind, &fd) && count <= 2);
        FindClose(hFind);
        if (count == 1) {
            csfBytes.clear();
            if (PathUtils::ReadFileBytes(foundCsf, 16 * 1024 * 1024, csfBytes) && !csfBytes.empty()) {
                ParseCal3DBinarySkeleton(csfBytes.data(), csfBytes.size(), outBones);
            }
        }
    }
}

inline void AppendOctahedralBonePrism(
    const Vec3f& a,
    const Vec3f& b,
    std::vector<Vec3f>& verts,
    std::vector<MeshTriangle>& tris)
{
    Vec3f d = { b.x - a.x, b.y - a.y, b.z - a.z };
    float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (!std::isfinite(len) || len < 1e-5f) return;
    Vec3f dir = { d.x / len, d.y / len, d.z / len };
    Vec3f up = (std::fabs(dir.y) < 0.9f) ? Vec3f{ 0.0f, 1.0f, 0.0f } : Vec3f{ 1.0f, 0.0f, 0.0f };
    // u = normalize(cross(dir, up))
    Vec3f u = {
        dir.y * up.z - dir.z * up.y,
        dir.z * up.x - dir.x * up.z,
        dir.x * up.y - dir.y * up.x
    };
    float uLen = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
    if (uLen < 1e-6f) return;
    u.x /= uLen; u.y /= uLen; u.z /= uLen;
    Vec3f v = {
        dir.y * u.z - dir.z * u.y,
        dir.z * u.x - dir.x * u.z,
        dir.x * u.y - dir.y * u.x
    };
    float rad = len * 0.10f;
    Vec3f mid = { a.x + d.x * 0.25f, a.y + d.y * 0.25f, a.z + d.z * 0.25f };
    uint32_t base = static_cast<uint32_t>(verts.size());
    verts.push_back(a);   // 0 root
    verts.push_back(b);   // 1 tip
    verts.push_back({ mid.x + u.x * rad, mid.y + u.y * rad, mid.z + u.z * rad }); // 2
    verts.push_back({ mid.x + v.x * rad, mid.y + v.y * rad, mid.z + v.z * rad }); // 3
    verts.push_back({ mid.x - u.x * rad, mid.y - u.y * rad, mid.z - u.z * rad }); // 4
    verts.push_back({ mid.x - v.x * rad, mid.y - v.y * rad, mid.z - v.z * rad }); // 5
    const uint32_t faces[8][3] = {
        {0,2,3}, {0,3,4}, {0,4,5}, {0,5,2},
        {1,3,2}, {1,4,3}, {1,5,4}, {1,2,5}
    };
    for (const auto& f : faces) {
        tris.push_back({ base + f[0], base + f[1], base + f[2], {} });
    }
}

inline void ParseXmlFloatsSimple(std::string_view sv, std::vector<float>& out) {
    const char* p = sv.data();
    const char* end = p + sv.size();
    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',')) ++p;
        if (p >= end) break;
        if (*p == '+') ++p;
        float v = 0.0f;
        auto r = std::from_chars(p, end, v);
        if (r.ec != std::errc{} || r.ptr == p) {
            while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != ',') ++p;
            continue;
        }
        p = r.ptr;
        if (std::isfinite(v)) out.push_back(v);
    }
}

inline std::string_view ExtractXmlAttr(std::string_view tag, std::string_view attrName) {
    size_t pos = 0;
    while (pos < tag.size()) {
        size_t eq = tag.find('=', pos);
        if (eq == std::string_view::npos) break;
        size_t ns = eq;
        while (ns > pos && (tag[ns - 1] == ' ' || tag[ns - 1] == '\t')) --ns;
        size_t nb = ns;
        while (nb > pos && (std::isalnum(static_cast<unsigned char>(tag[nb - 1])) || tag[nb - 1] == '_')) --nb;
        std::string_view key = tag.substr(nb, ns - nb);
        size_t vs = eq + 1;
        while (vs < tag.size() && (tag[vs] == ' ' || tag[vs] == '\t')) ++vs;
        if (vs < tag.size() && (tag[vs] == '"' || tag[vs] == '\'')) {
            char q = tag[vs++];
            size_t ve = tag.find(q, vs);
            if (ve == std::string_view::npos) break;
            if (key.size() == attrName.size()) {
                bool match = true;
                for (size_t k = 0; k < key.size(); ++k) {
                    if (std::tolower(static_cast<unsigned char>(key[k])) != std::tolower(static_cast<unsigned char>(attrName[k]))) {
                        match = false; break;
                    }
                }
                if (match) return tag.substr(vs, ve - vs);
            }
            pos = ve + 1;
        } else {
            pos = vs + 1;
        }
    }
    return {};
}

} // namespace CalCryDetail

// 1. Cal3D Binary Mesh (.cmf — "CMF\0")
inline bool ParseCal3DCmf(const uint8_t* data, size_t size, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    using namespace CalCryDetail;
    if (!data || size < 16 || std::memcmp(data, "CMF\0", 4) != 0) return false;
    int32_t ver = RdI32(data + 4);
    int32_t numSubmeshes = RdI32(data + 8);
    if (ver < 700 || ver > 1500 || numSubmeshes <= 0 || numSubmeshes > 4096) return false;

    size_t pos = 12;
    std::vector<CalWeightedVert> allVerts;
    mesh.tris.clear();

    for (int32_t sm = 0; sm < numSubmeshes; ++sm) {
        if (pos + 24 > size) return false;
        /*int32_t matId =*/ RdI32(data + pos + 0);
        int32_t nVerts   = RdI32(data + pos + 4);
        int32_t nFaces   = RdI32(data + pos + 8);
        int32_t nLod     = RdI32(data + pos + 12);
        int32_t nSprings = RdI32(data + pos + 16);
        int32_t nTexMaps = RdI32(data + pos + 20);
        pos += 24;
        (void)nLod;

        if (nVerts <= 0 || nVerts > 2000000 || nFaces <= 0 || nFaces > 4000000 ||
            nSprings < 0 || nSprings > 2000000 || nTexMaps < 0 || nTexMaps > 16) {
            return false;
        }

        uint32_t baseVert = static_cast<uint32_t>(allVerts.size());
        allVerts.reserve(allVerts.size() + static_cast<size_t>(nVerts));
        for (int32_t v = 0; v < nVerts; ++v) {
            // pos(12) + norm(12) + collapseId(4) + faceCollapseCnt(4) + texCoords(nTexMaps * 8) + numInfluences(4)
            size_t fixedV = 32u + static_cast<size_t>(nTexMaps) * 8u + 4u;
            if (pos + fixedV > size) return false;
            float px = RdF32(data + pos + 0);
            float py = RdF32(data + pos + 4);
            float pz = RdF32(data + pos + 8);
            if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz)) return false;

            size_t infPos = pos + 32u + static_cast<size_t>(nTexMaps) * 8u;
            int32_t nInf = RdI32(data + infPos);
            pos = infPos + 4u;
            if (nInf < 0 || nInf > 128 || pos + static_cast<size_t>(nInf) * 8u > size) return false;

            CalWeightedVert wv;
            wv.pos = { px, py, pz };
            wv.infs.reserve(static_cast<size_t>(nInf));
            for (int32_t k = 0; k < nInf; ++k) {
                int32_t bId = RdI32(data + pos + 0);
                float   wt  = RdF32(data + pos + 4);
                pos += 8;
                wv.infs.emplace_back(bId, wt);
            }
            if (nSprings > 0) {
                if (pos + 4 > size) return false;
                pos += 4; // float physicalProperty
            }
            allVerts.push_back(std::move(wv));
        }

        // Springs: vertexId[2] (8B) + springCoef (4B) + idleLength (4B) = 16B
        size_t springBytes = static_cast<size_t>(nSprings) * 16u;
        if (pos + springBytes > size) return false;
        pos += springBytes;

        // Faces: 3 * int32 (12B each; or 3 * uint16 if v < 800, though v700+ standard loader uses 32-bit VertexId)
        size_t faceBytes = static_cast<size_t>(nFaces) * 12u;
        if (pos + faceBytes > size) return false;
        mesh.tris.reserve(mesh.tris.size() + static_cast<size_t>(nFaces));
        for (int32_t f = 0; f < nFaces; ++f) {
            int32_t i0 = RdI32(data + pos + 0);
            int32_t i1 = RdI32(data + pos + 4);
            int32_t i2 = RdI32(data + pos + 8);
            pos += 12;
            if (i0 < 0 || i0 >= nVerts || i1 < 0 || i1 >= nVerts || i2 < 0 || i2 >= nVerts) return false;
            if (i0 != i1 && i1 != i2 && i0 != i2) {
                mesh.tris.push_back({
                    baseVert + static_cast<uint32_t>(i0),
                    baseVert + static_cast<uint32_t>(i1),
                    baseVert + static_cast<uint32_t>(i2),
                    {}
                });
            }
        }
    }

    if (allVerts.empty() || mesh.tris.empty()) return false;
    std::vector<CalBone> bones;
    LoadCompanionCalSkeleton(fullPath, bones);
    ApplyCal3DSkinning(bones, allVerts, mesh.vertices);
    mesh.formatName = L"Cal3D Binary Skeletal Mesh (.cmf)";
    return true;
}

// 2. Cal3D XML Mesh (.xmf — MAGIC="XMF" / <MESH><SUBMESH>)
inline bool ParseCal3DXmf(std::string_view sv, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    using namespace CalCryDetail;
    // Also accept binary CMF accidentally saved with .xmf extension
    if (sv.size() >= 16 && std::memcmp(sv.data(), "CMF\0", 4) == 0) {
        return ParseCal3DCmf(reinterpret_cast<const uint8_t*>(sv.data()), sv.size(), fullPath, mesh);
    }
    if (sv.find("XMF") == std::string_view::npos && sv.find("<SUBMESH") == std::string_view::npos &&
        sv.find("<submesh") == std::string_view::npos) {
        return false;
    }

    std::vector<CalWeightedVert> allVerts;
    mesh.tris.clear();

    size_t pos = 0;
    while (pos < sv.size()) {
        size_t smOpen = sv.find("<SUBMESH", pos);
        bool lowerCase = false;
        if (smOpen == std::string_view::npos) {
            smOpen = sv.find("<submesh", pos);
            lowerCase = true;
        }
        if (smOpen == std::string_view::npos) break;
        size_t smClose = sv.find(lowerCase ? "</submesh>" : "</SUBMESH>", smOpen);
        if (smClose == std::string_view::npos) return false;
        std::string_view smBody = sv.substr(smOpen, smClose - smOpen);
        pos = smClose + 10;

        uint32_t baseVert = static_cast<uint32_t>(allVerts.size());
        uint32_t smVertCnt = 0;

        // Parse <VERTEX ...>...</VERTEX>
        size_t vp = 0;
        while (vp < smBody.size()) {
            size_t vOpen = smBody.find(lowerCase ? "<vertex" : "<VERTEX", vp);
            if (vOpen == std::string_view::npos) break;
            size_t vEndTag = smBody.find('>', vOpen);
            size_t vClose = smBody.find(lowerCase ? "</vertex>" : "</VERTEX>", vOpen);
            if (vEndTag == std::string_view::npos || vClose == std::string_view::npos || vClose < vEndTag) return false;
            std::string_view vInner = smBody.substr(vEndTag + 1, vClose - (vEndTag + 1));
            vp = vClose + 9;

            size_t pOpen = vInner.find(lowerCase ? "<pos>" : "<POS>");
            size_t pClose = vInner.find(lowerCase ? "</pos>" : "</POS>");
            if (pOpen == std::string_view::npos || pClose == std::string_view::npos || pClose <= pOpen + 5) continue;
            std::vector<float> pNums;
            ParseXmlFloatsSimple(vInner.substr(pOpen + 5, pClose - (pOpen + 5)), pNums);
            if (pNums.size() < 3) continue;

            CalWeightedVert wv;
            wv.pos = { pNums[0], pNums[1], pNums[2] };

            // Parse optional <INFLUENCE ID="boneId">weight</INFLUENCE>
            size_t ip = 0;
            while (ip < vInner.size()) {
                size_t iOpen = vInner.find(lowerCase ? "<influence" : "<INFLUENCE", ip);
                if (iOpen == std::string_view::npos) break;
                size_t iGt = vInner.find('>', iOpen);
                size_t iClose = vInner.find(lowerCase ? "</influence>" : "</INFLUENCE>", iOpen);
                if (iGt == std::string_view::npos || iClose == std::string_view::npos || iClose < iGt) break;
                std::string_view idAttr = ExtractXmlAttr(vInner.substr(iOpen, iGt - iOpen), "ID");
                int32_t bId = -1;
                if (!idAttr.empty()) std::from_chars(idAttr.data(), idAttr.data() + idAttr.size(), bId);
                std::vector<float> wNums;
                ParseXmlFloatsSimple(vInner.substr(iGt + 1, iClose - (iGt + 1)), wNums);
                if (bId >= 0 && !wNums.empty()) wv.infs.emplace_back(bId, wNums[0]);
                ip = iClose + 12;
            }
            allVerts.push_back(std::move(wv));
            ++smVertCnt;
        }

        // Parse <FACE VERTEXID="i0 i1 i2" .../>
        size_t fp = 0;
        while (fp < smBody.size()) {
            size_t fOpen = smBody.find(lowerCase ? "<face" : "<FACE", fp);
            if (fOpen == std::string_view::npos) break;
            size_t fGt = smBody.find('>', fOpen);
            if (fGt == std::string_view::npos) break;
            std::string_view fTag = smBody.substr(fOpen, fGt - fOpen + 1);
            fp = fGt + 1;

            std::string_view vidTxt = ExtractXmlAttr(fTag, "VERTEXID");
            std::vector<float> fIdx;
            if (!vidTxt.empty()) {
                ParseXmlFloatsSimple(vidTxt, fIdx);
            } else {
                size_t fClose = smBody.find(lowerCase ? "</face>" : "</FACE>", fp);
                if (fClose != std::string_view::npos) {
                    ParseXmlFloatsSimple(smBody.substr(fp, fClose - fp), fIdx);
                    fp = fClose + 7;
                }
            }
            if (fIdx.size() >= 3) {
                int32_t i0 = static_cast<int32_t>(fIdx[0]);
                int32_t i1 = static_cast<int32_t>(fIdx[1]);
                int32_t i2 = static_cast<int32_t>(fIdx[2]);
                if (i0 >= 0 && static_cast<uint32_t>(i0) < smVertCnt &&
                    i1 >= 0 && static_cast<uint32_t>(i1) < smVertCnt &&
                    i2 >= 0 && static_cast<uint32_t>(i2) < smVertCnt &&
                    i0 != i1 && i1 != i2 && i0 != i2) {
                    mesh.tris.push_back({
                        baseVert + static_cast<uint32_t>(i0),
                        baseVert + static_cast<uint32_t>(i1),
                        baseVert + static_cast<uint32_t>(i2),
                        {}
                    });
                }
            }
        }
    }

    if (allVerts.empty() || mesh.tris.empty()) return false;
    std::vector<CalBone> bones;
    LoadCompanionCalSkeleton(fullPath, bones);
    ApplyCal3DSkinning(bones, allVerts, mesh.vertices);
    mesh.formatName = L"Cal3D XML Skeletal Mesh (.xmf)";
    return true;
}

// 3. Cal3D Binary & XML Skeleton (.csf — "CSF\0" / MAGIC="XSF")
inline bool ParseCal3DCsf(const uint8_t* data, size_t size, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    using namespace CalCryDetail;
    std::vector<CalBone> bones;
    if (!ParseCal3DBinarySkeleton(data, size, bones)) {
        // Check XML skeleton variant (<HEADER MAGIC="XSF"/> ... <BONE ...>)
        std::string_view sv(reinterpret_cast<const char*>(data), size);
        if (sv.find("XSF") == std::string_view::npos && sv.find("<SKELETON") == std::string_view::npos) {
            return false;
        }
        size_t pos = 0;
        while (pos < sv.size()) {
            size_t bOpen = sv.find("<BONE", pos);
            if (bOpen == std::string_view::npos) break;
            size_t bClose = sv.find("</BONE>", bOpen);
            if (bClose == std::string_view::npos) return false;
            std::string_view bBlock = sv.substr(bOpen, bClose - bOpen);
            pos = bClose + 7;

            CalBone b;
            size_t tOpen = bBlock.find("<TRANSLATION>"), tClose = bBlock.find("</TRANSLATION>");
            if (tOpen != std::string_view::npos && tClose != std::string_view::npos && tClose > tOpen + 13) {
                std::vector<float> v;
                ParseXmlFloatsSimple(bBlock.substr(tOpen + 13, tClose - (tOpen + 13)), v);
                if (v.size() >= 3) b.localPos = { v[0], v[1], v[2] };
            }
            size_t rOpen = bBlock.find("<ROTATION>"), rClose = bBlock.find("</ROTATION>");
            if (rOpen != std::string_view::npos && rClose != std::string_view::npos && rClose > rOpen + 10) {
                std::vector<float> v;
                ParseXmlFloatsSimple(bBlock.substr(rOpen + 10, rClose - (rOpen + 10)), v);
                if (v.size() >= 4) b.localRot = { v[0], v[1], v[2], v[3] };
            }
            size_t pOpen = bBlock.find("<PARENTID>"), pClose = bBlock.find("</PARENTID>");
            if (pOpen != std::string_view::npos && pClose != std::string_view::npos && pClose > pOpen + 10) {
                std::vector<float> v;
                ParseXmlFloatsSimple(bBlock.substr(pOpen + 10, pClose - (pOpen + 10)), v);
                if (!v.empty()) b.parentId = static_cast<int32_t>(v[0]);
            }
            bones.push_back(std::move(b));
        }
        if (bones.empty()) return false;
        EvaluateCalSkeletonWorld(bones);
    }

    // Check for same-stem companion .cmf or .xmf mesh first
    if (!fullPath.empty()) {
        std::wstring dir = PathUtils::GetParentPath(fullPath);
        std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(fullPath), false);
        std::vector<uint8_t> cmfBytes;
        if (PathUtils::ReadFileBytes(dir + L"\\" + stem + L".cmf", 64 * 1024 * 1024, cmfBytes) && !cmfBytes.empty()) {
            if (ParseCal3DCmf(cmfBytes.data(), cmfBytes.size(), fullPath, mesh)) {
                mesh.formatName = L"Cal3D Skeleton + Mesh (.csf + .cmf)";
                return true;
            }
        }
    }

    // Build articulated 3D octahedral bone armature directly from real bone hierarchy
    mesh.vertices.clear();
    mesh.tris.clear();
    for (size_t i = 0; i < bones.size(); ++i) {
        int32_t p = bones[i].parentId;
        if (p >= 0 && static_cast<size_t>(p) < bones.size()) {
            AppendOctahedralBonePrism(bones[static_cast<size_t>(p)].worldPos, bones[i].worldPos, mesh.vertices, mesh.tris);
        }
    }
    if (mesh.tris.empty() && bones.size() == 1) {
        // Single root bone with non-zero length vector along local +Y
        Vec3f tip = bones[0].worldRot.Rotate({ 0.0f, 1.0f, 0.0f });
        tip.x += bones[0].worldPos.x; tip.y += bones[0].worldPos.y; tip.z += bones[0].worldPos.z;
        AppendOctahedralBonePrism(bones[0].worldPos, tip, mesh.vertices, mesh.tris);
    }
    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Cal3D Skeletal Hierarchy (.csf)";
    return true;
}

// 4. CryEngine / Lumberyard / O3DE Geometry (.cgf, .chr, .skin, .cga — "CryTek" v0x0744/0x0745 & "CrCh" v0x0746)
inline bool IsLikelyCryEngineContainer(const uint8_t* data, size_t size) {
    if (!data || size < 16) return false;
    if (std::memcmp(data, "CryTek", 6) == 0) return true;
    if (std::memcmp(data, "CrCh", 4) == 0) {
        uint32_t ver = CalCryDetail::RdU32(data + 4);
        return (ver == 0x0746u || ver == 0x0747u);
    }
    return false;
}

inline bool ParseCryEngineGeometry(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    LoadedMesh3D& mesh)
{
    using namespace CalCryDetail;
    if (!IsLikelyCryEngineContainer(data, size)) return false;

    struct CryChunk {
        uint32_t type = 0;
        uint32_t version = 0;
        uint32_t id = 0;
        uint32_t offset = 0;
        uint32_t byteSize = 0;
        bool hasInlineHeader = false;
    };

    std::vector<CryChunk> chunks;
    bool isCrCh = (std::memcmp(data, "CrCh", 4) == 0);
    uint32_t fileVer = 0;

    if (isCrCh) {
        fileVer = RdU32(data + 4);
        uint32_t numChunks = RdU32(data + 8);
        uint32_t tableOff  = RdU32(data + 12);
        if (numChunks == 0 || numChunks > 65536 || static_cast<uint64_t>(tableOff) + static_cast<uint64_t>(numChunks) * 16ULL > size) {
            return false;
        }
        chunks.reserve(numChunks);
        for (uint32_t i = 0; i < numChunks; ++i) {
            const uint8_t* cp = data + tableOff + i * 16u;
            CryChunk ch;
            ch.type     = RdU16(cp + 0);
            ch.version  = RdU16(cp + 2);
            ch.id       = RdU32(cp + 4);
            ch.byteSize = RdU32(cp + 8);
            ch.offset   = RdU32(cp + 12);
            ch.hasInlineHeader = false;
            if (ch.offset >= size || static_cast<uint64_t>(ch.offset) + ch.byteSize > size) return false;
            chunks.push_back(ch);
        }
    } else {
        if (size < 20) return false;
        fileVer = RdU32(data + 12);
        uint32_t tableOff = RdU32(data + 16);
        if ((fileVer != 0x0744u && fileVer != 0x0745u) || tableOff + 4u > size) return false;
        uint32_t numChunks = RdU32(data + tableOff);
        size_t entrySz = (fileVer == 0x0745u) ? 20u : 16u;
        if (numChunks == 0 || numChunks > 65536 ||
            static_cast<uint64_t>(tableOff) + 4ULL + static_cast<uint64_t>(numChunks) * entrySz > size) {
            return false;
        }
        chunks.reserve(numChunks);
        for (uint32_t i = 0; i < numChunks; ++i) {
            const uint8_t* cp = data + tableOff + 4u + i * entrySz;
            CryChunk ch;
            ch.type    = RdU32(cp + 0);
            ch.version = RdU32(cp + 4);
            ch.offset  = RdU32(cp + 8);
            ch.id      = RdU32(cp + 12);
            ch.byteSize = (entrySz == 20u) ? RdU32(cp + 16) : 0u;
            ch.hasInlineHeader = true;
            if (ch.offset >= size) return false;
            chunks.push_back(ch);
        }
        // Compute inferred sizes for 0x0744 chunks sorted by offset
        for (size_t i = 0; i < chunks.size(); ++i) {
            if (chunks[i].byteSize == 0) {
                uint32_t nextOff = static_cast<uint32_t>(size);
                if (tableOff > chunks[i].offset && tableOff < nextOff) nextOff = tableOff;
                for (size_t j = 0; j < chunks.size(); ++j) {
                    if (chunks[j].offset > chunks[i].offset && chunks[j].offset < nextOff) {
                        nextOff = chunks[j].offset;
                    }
                }
                chunks[i].byteSize = nextOff - chunks[i].offset;
            }
            if (static_cast<uint64_t>(chunks[i].offset) + chunks[i].byteSize > size) return false;
        }
    }

    auto isMeshChunkType = [](uint32_t t) { return t == 0xCCCC0000u || t == 0x1000u; };
    auto isDataStreamType = [](uint32_t t) { return t == 0xCCCC0016u || t == 0x1016u; };

    // Index all DataStream chunks (0x0800 / 0x0801) by chunk ID
    struct ParsedPosStream { std::vector<Vec3f> verts; };
    struct ParsedIdxStream { std::vector<uint32_t> indices; };
    std::unordered_map<uint32_t, ParsedPosStream> posStreams;
    std::unordered_map<uint32_t, ParsedIdxStream> idxStreams;
    std::vector<uint32_t> posStreamOrder;
    std::vector<uint32_t> idxStreamOrder;

    for (const CryChunk& ch : chunks) {
        if (!isDataStreamType(ch.type)) continue;
        size_t bodyOff = ch.offset + (ch.hasInlineHeader ? 16u : 0u);
        if (bodyOff + 24u > ch.offset + ch.byteSize) continue;
        const uint8_t* sp = data + bodyOff;
        /*uint32_t flags =*/ RdU32(sp + 0);
        uint32_t streamType = RdU32(sp + 4);
        uint32_t elemCount  = RdU32(sp + 8);
        uint32_t elemSize   = RdU32(sp + 12);
        // reserved[2] at +16..+24
        size_t payloadOff = bodyOff + 24u;
        uint64_t payloadBytes = static_cast<uint64_t>(elemCount) * elemSize;
        if (elemCount == 0 || elemCount > 4000000u || payloadOff + payloadBytes > ch.offset + ch.byteSize) continue;

        const uint8_t* pay = data + payloadOff;
        // CGF_STREAM_POSITIONS = 0 (float3 = 12B, or half4 = 8B), CGF_STREAM_P3S_C4B_T2S = 15 (half3/half4 + color + uv = 16B)
        if ((streamType == 0u && (elemSize == 12u || elemSize == 8u || elemSize == 16u)) ||
            (streamType == 15u && (elemSize == 16u || elemSize == 8u)))
        {
            ParsedPosStream ps;
            ps.verts.reserve(elemCount);
            bool ok = true;
            for (uint32_t i = 0; i < elemCount; ++i) {
                const uint8_t* ep = pay + static_cast<size_t>(i) * elemSize;
                float vx = 0.0f, vy = 0.0f, vz = 0.0f;
                if (streamType == 0u && elemSize >= 12u) {
                    vx = RdF32(ep + 0); vy = RdF32(ep + 4); vz = RdF32(ep + 8);
                } else {
                    vx = HalfToF32(RdU16(ep + 0));
                    vy = HalfToF32(RdU16(ep + 2));
                    vz = HalfToF32(RdU16(ep + 4));
                }
                if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) { ok = false; break; }
                // CryEngine Z-up meters -> Y-up viewport
                ps.verts.push_back({ vx, vz, -vy });
            }
            if (ok && !ps.verts.empty()) {
                posStreams[ch.id] = std::move(ps);
                posStreamOrder.push_back(ch.id);
            }
        } else if (streamType == 5u && (elemSize == 2u || elemSize == 4u)) {
            ParsedIdxStream is;
            is.indices.reserve(elemCount);
            for (uint32_t i = 0; i < elemCount; ++i) {
                uint32_t idx = (elemSize == 4u)
                    ? RdU32(pay + static_cast<size_t>(i) * 4u)
                    : static_cast<uint32_t>(RdU16(pay + static_cast<size_t>(i) * 2u));
                is.indices.push_back(idx);
            }
            idxStreams[ch.id] = std::move(is);
            idxStreamOrder.push_back(ch.id);
        }
    }

    mesh.vertices.clear();
    mesh.tris.clear();

    auto appendStreamPair = [&](const ParsedPosStream& ps, const ParsedIdxStream& is) -> bool {
        if (ps.verts.size() < 3 || is.indices.size() < 3) return false;
        uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
        size_t addedTris = 0;
        for (size_t t = 0; t + 2 < is.indices.size(); t += 3) {
            uint32_t i0 = is.indices[t + 0];
            uint32_t i1 = is.indices[t + 1];
            uint32_t i2 = is.indices[t + 2];
            if (i0 >= ps.verts.size() || i1 >= ps.verts.size() || i2 >= ps.verts.size()) return false;
            if (i0 != i1 && i1 != i2 && i0 != i2) {
                mesh.tris.push_back({ baseV + i0, baseV + i1, baseV + i2, {} });
                ++addedTris;
            }
        }
        if (addedTris == 0) return false;
        mesh.vertices.insert(mesh.vertices.end(), ps.verts.begin(), ps.verts.end());
        return true;
    };

    // Match Mesh chunks (0x0800 / 0x0801 / 0x0802 stream references, or legacy 0x0744 inline CryVertex/CryFace)
    for (const CryChunk& ch : chunks) {
        if (!isMeshChunkType(ch.type)) continue;
        size_t bodyOff = ch.offset + (ch.hasInlineHeader ? 16u : 0u);
        size_t bodyLen = (ch.offset + ch.byteSize > bodyOff) ? (ch.offset + ch.byteSize - bodyOff) : 0u;
        const uint8_t* mp = data + bodyOff;

        if (ch.version == 0x0744u && bodyLen >= 20u) {
            // Legacy Far Cry / CryEngine 1 MESH_CHUNK_DESC_0744:
            // flags(4) + nVerts(4) + nTVerts(4) + nFaces(4) + vertAnimId(4) + nVerts*24 (CryVertex) + nFaces*20 (CryFace)
            int32_t nVerts  = RdI32(mp + 4);
            int32_t nTVerts = RdI32(mp + 8);
            int32_t nFaces  = RdI32(mp + 12);
            (void)nTVerts;
            uint64_t vBytes = static_cast<uint64_t>(nVerts) * 24ULL;
            uint64_t fBytes = static_cast<uint64_t>(nFaces) * 20ULL;
            if (nVerts >= 3 && nVerts <= 2000000 && nFaces >= 1 && nFaces <= 4000000 &&
                20ULL + vBytes + fBytes <= bodyLen)
            {
                uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
                const uint8_t* vp = mp + 20;
                for (int32_t v = 0; v < nVerts; ++v) {
                    float vx = RdF32(vp + static_cast<size_t>(v) * 24u + 0);
                    float vy = RdF32(vp + static_cast<size_t>(v) * 24u + 4);
                    float vz = RdF32(vp + static_cast<size_t>(v) * 24u + 8);
                    if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return false;
                    mesh.vertices.push_back({ vx, vz, -vy });
                }
                const uint8_t* fp = vp + vBytes;
                for (int32_t f = 0; f < nFaces; ++f) {
                    int32_t i0 = RdI32(fp + static_cast<size_t>(f) * 20u + 0);
                    int32_t i1 = RdI32(fp + static_cast<size_t>(f) * 20u + 4);
                    int32_t i2 = RdI32(fp + static_cast<size_t>(f) * 20u + 8);
                    if (i0 < 0 || i0 >= nVerts || i1 < 0 || i1 >= nVerts || i2 < 0 || i2 >= nVerts) return false;
                    if (i0 != i1 && i1 != i2 && i0 != i2) {
                        mesh.tris.push_back({
                            baseV + static_cast<uint32_t>(i0),
                            baseV + static_cast<uint32_t>(i1),
                            baseV + static_cast<uint32_t>(i2),
                            {}
                        });
                    }
                }
            }
        } else if (bodyLen >= 96u) {
            // MESH_CHUNK_DESC_0800 / 0801: flags1(4), flags2(4), nVerts(4), nIndices(4), nSubsets(4), subsetsChunkId(4),
            // vertAnimId(4), streamChunkID[16] (64B starting at +28)
            uint32_t posCid  = RdU32(mp + 28 + 0 * 4);
            uint32_t p3sCid  = RdU32(mp + 28 + 15 * 4);
            uint32_t idxCid  = RdU32(mp + 28 + 5 * 4);
            uint32_t usePosCid = (posStreams.find(posCid) != posStreams.end()) ? posCid : p3sCid;
            auto itP = posStreams.find(usePosCid);
            auto itI = idxStreams.find(idxCid);
            if (itP != posStreams.end() && itI != idxStreams.end()) {
                appendStreamPair(itP->second, itI->second);
            }
        }
    }

    // Fallback for streamlined 0x0802+ mesh chunks where streamChunkID[16][8] starts at a different offset:
    // pair validated DataStream position & index chunks in chunk order
    if (mesh.tris.empty() && !posStreamOrder.empty() && !idxStreamOrder.empty()) {
        size_t pairs = std::min(posStreamOrder.size(), idxStreamOrder.size());
        for (size_t k = 0; k < pairs; ++k) {
            appendStreamPair(posStreams[posStreamOrder[k]], idxStreams[idxStreamOrder[k]]);
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    if (extLower == L".chr")       mesh.formatName = L"CryEngine Skinned Character (.chr)";
    else if (extLower == L".skin") mesh.formatName = L"CryEngine Skinned Attachment (.skin)";
    else if (extLower == L".cga")  mesh.formatName = L"CryEngine Animated Geometry (.cga)";
    else                           mesh.formatName = isCrCh ? L"CryEngine V Geometry (.cgf CrCh)" : L"CryEngine Geometry (.cgf)";
    return true;
}

} // namespace Formats
