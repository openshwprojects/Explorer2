#pragma once
// BioWareAuroraMdl.h — BioWare Aurora (NWN) & Odyssey (KotOR 1/2) 3D Models (.mdl, .mdx).
#include "../common/BioWareCommon.h"

namespace Formats {
namespace BioWare3DDetail {

struct BwMat4 {
    float m[4][4] = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };

    static BwMat4 FromPosQuatWXYZ(float tx, float ty, float tz, float qw, float qx, float qy, float qz) {
        BwMat4 r{};
        float len2 = qw * qw + qx * qx + qy * qy + qz * qz;
        if (len2 > 1e-8f) {
            float inv = 1.0f / std::sqrt(len2);
            qw *= inv; qx *= inv; qy *= inv; qz *= inv;
            float xx = qx * qx, yy = qy * qy, zz = qz * qz;
            float xy = qx * qy, xz = qx * qz, yz = qy * qz;
            float wx = qw * qx, wy = qw * qy, wz = qw * qz;
            r.m[0][0] = 1.0f - 2.0f * (yy + zz);
            r.m[0][1] = 2.0f * (xy - wz);
            r.m[0][2] = 2.0f * (xz + wy);
            r.m[1][0] = 2.0f * (xy + wz);
            r.m[1][1] = 1.0f - 2.0f * (xx + zz);
            r.m[1][2] = 2.0f * (yz - wx);
            r.m[2][0] = 2.0f * (xz - wy);
            r.m[2][1] = 2.0f * (yz + wx);
            r.m[2][2] = 1.0f - 2.0f * (xx + yy);
        }
        r.m[0][3] = tx;
        r.m[1][3] = ty;
        r.m[2][3] = tz;
        return r;
    }

    static BwMat4 FromPosAxisAngle(float tx, float ty, float tz, float ax, float ay, float az, float angleRad) {
        float alen = std::sqrt(ax * ax + ay * ay + az * az);
        if (alen < 1e-6f || std::fabs(angleRad) < 1e-6f) {
            return FromPosQuatWXYZ(tx, ty, tz, 1.0f, 0.0f, 0.0f, 0.0f);
        }
        ax /= alen; ay /= alen; az /= alen;
        float half = 0.5f * angleRad;
        float s = std::sin(half);
        return FromPosQuatWXYZ(tx, ty, tz, std::cos(half), ax * s, ay * s, az * s);
    }

    static BwMat4 Mul(const BwMat4& a, const BwMat4& b) {
        BwMat4 c{};
        for (int r = 0; r < 4; ++r) {
            for (int col = 0; col < 4; ++col) {
                c.m[r][col] = a.m[r][0] * b.m[0][col] +
                              a.m[r][1] * b.m[1][col] +
                              a.m[r][2] * b.m[2][col] +
                              a.m[r][3] * b.m[3][col];
            }
        }
        return c;
    }

    Vec3f TransformZUpToViewport(float x, float y, float z) const {
        float wx = m[0][0] * x + m[0][1] * y + m[0][2] * z + m[0][3];
        float wy = m[1][0] * x + m[1][1] * y + m[1][2] * z + m[1][3];
        float wz = m[2][0] * x + m[2][1] * y + m[2][2] * z + m[2][3];
        // BioWare Aurora / Odyssey uses Z-up right-handed coordinates -> convert to Y-up viewport
        return { wx, wz, -wy };
    }
};

inline bool IsLikelyBioWareBinaryMdl(const uint8_t* data, size_t size) {
    using namespace BioWareDetail;
    if (!data || size < 92) return false;
    if (BwU32(data) != 0u) return false;
    uint32_t rawSize = BwU32(data + 4);
    if (rawSize < 80 || rawSize > size - 12) return false;
    // Check model name is printable ASCII (non-empty) at data + 12 + 8
    const uint8_t* namePtr = data + 20;
    if (namePtr[0] <= 0x20 || namePtr[0] >= 0x7F) return false;
    for (size_t i = 0; i < 32; ++i) {
        if (namePtr[i] == 0) break;
        if (namePtr[i] < 0x20 || namePtr[i] >= 0x7F) return false;
    }
    uint32_t rootNodeOff = BwU32(data + 12 + 40);
    return rootNodeOff >= 80 && rootNodeOff + 80 <= rawSize;
}

inline bool IsLikelyBioWareAsciiMdl(std::string_view sv) {
    size_t scanLen = std::min<size_t>(sv.size(), 4096);
    std::string head(sv.substr(0, scanLen));
    for (char& c : head) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return head.find("newmodel") != std::string::npos &&
           (head.find("beginmodelgeom") != std::string::npos || head.find("node ") != std::string::npos);
}

inline bool ParseOdysseyBinaryMdlPair(
    const uint8_t* mdlData,
    size_t mdlSize,
    const uint8_t* mdxData,
    size_t mdxSize,
    LoadedMesh3D& mesh)
{
    using namespace BioWareDetail;
    if (!IsLikelyBioWareBinaryMdl(mdlData, mdlSize)) return false;

    const uint32_t declaredRaw = BwU32(mdlData + 4);
    const size_t rawSize = std::min<size_t>(declaredRaw, mdlSize - 12);
    const uint8_t* base = mdlData + 12;

    const uint32_t fp1 = BwU32(base + 0);
    // KotOR 2 TSL PC (4285200 / 0x416310) & Xbox (4285872) add 8 bytes to Trimesh header before mdxDataSize
    const bool isKotor2 = (fp1 == 4285200u || fp1 == 4285872u || fp1 == 4216880u || fp1 == 4216816u);

    const uint32_t rootNodeOff = BwU32(base + 40);
    if (rootNodeOff < 80 || rootNodeOff + 80 > rawSize) return false;

    // Animation count in Model Header (+80..+196): animArray offset(+88), count(+92) relative to base
    size_t animCount = 0;
    if (rawSize >= 100) {
        uint32_t animOff = BwU32(base + 88);
        uint32_t animCnt = BwU32(base + 92);
        if (animCnt <= 4096 && (animCnt == 0 || (animOff >= 80 && static_cast<uint64_t>(animOff) + animCnt * 4ULL <= rawSize))) {
            animCount = animCnt;
        }
    }

    std::set<uint32_t> visitedNodes;
    std::function<bool(uint32_t, const BwMat4&, int)> walkNode =
        [&](uint32_t nodeOff, const BwMat4& parentWorld, int depth) -> bool
    {
        if (depth > 96 || visitedNodes.size() > 8192) return false;
        if (!visitedNodes.insert(nodeOff).second) return true;
        if (nodeOff + 80 > rawSize) return false;

        const uint8_t* nh = base + nodeOff;
        uint16_t flags = BwU16(nh + 0);
        if ((flags & 0x0001u) == 0) return false; // Invalid node header flag

        float px = BwF32(nh + 16), py = BwF32(nh + 20), pz = BwF32(nh + 24);
        // BioWare binary node orientation quaternion is stored (w, x, y, z) at +28..+40,
        // or overridden by controller key 8 (orientation) / key 12 (position) if zeroed
        float qw = BwF32(nh + 28), qx = BwF32(nh + 32), qy = BwF32(nh + 36), qz = BwF32(nh + 40);
        if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz) ||
            !std::isfinite(qw) || !std::isfinite(qx) || !std::isfinite(qy) || !std::isfinite(qz)) {
            return false;
        }

        // Check controllers for rest position (type 8) & orientation (type 20) if present
        uint32_t ctrlOff = BwU32(nh + 56), ctrlCnt = BwU32(nh + 60);
        uint32_t cdatOff = BwU32(nh + 68), cdatCnt = BwU32(nh + 72);
        if (ctrlCnt > 0 && ctrlCnt <= 256 && cdatCnt > 0 && cdatCnt <= 65536 &&
            static_cast<uint64_t>(ctrlOff) + static_cast<uint64_t>(ctrlCnt) * 16ULL <= rawSize &&
            static_cast<uint64_t>(cdatOff) + static_cast<uint64_t>(cdatCnt) * 4ULL <= rawSize)
        {
            for (uint32_t ci = 0; ci < ctrlCnt; ++ci) {
                const uint8_t* ce = base + ctrlOff + ci * 16;
                uint32_t cType = BwU32(ce + 0);
                uint16_t nRows = BwU16(ce + 6);
                uint16_t valIdx = BwU16(ce + 10);
                uint8_t nCols = ce[12];
                if (nRows == 0) continue;
                if (cType == 8 && nCols == 3 && static_cast<uint32_t>(valIdx) + 3u <= cdatCnt) {
                    const uint8_t* vp = base + cdatOff + static_cast<size_t>(valIdx) * 4;
                    float cx = BwF32(vp + 0), cy = BwF32(vp + 4), cz = BwF32(vp + 8);
                    if (std::isfinite(cx) && std::isfinite(cy) && std::isfinite(cz)) {
                        px = cx; py = cy; pz = cz;
                    }
                } else if (cType == 20 && nCols == 4 && static_cast<uint32_t>(valIdx) + 4u <= cdatCnt) {
                    // Controller orientation quaternion is (x, y, z, w)!
                    const uint8_t* vp = base + cdatOff + static_cast<size_t>(valIdx) * 4;
                    float cx = BwF32(vp + 0), cy = BwF32(vp + 4), cz = BwF32(vp + 8), cw = BwF32(vp + 12);
                    if (std::isfinite(cx) && std::isfinite(cy) && std::isfinite(cz) && std::isfinite(cw)) {
                        qx = cx; qy = cy; qz = cz; qw = cw;
                    }
                }
            }
        }

        BwMat4 worldM = BwMat4::Mul(parentWorld, BwMat4::FromPosQuatWXYZ(px, py, pz, qw, qx, qy, qz));

        // Mesh / Trimesh payload immediately follows the 80-byte Node Header when flag 0x0020 is set
        if ((flags & 0x0020u) != 0 && (flags & 0x0400u) == 0) {
            const size_t tmSize = isKotor2 ? 340u : 332u;
            const size_t mOff   = isKotor2 ? 260u : 252u;
            if (nodeOff + 80 + tmSize > rawSize) return false;
            const uint8_t* tm = base + nodeOff + 80;

            uint32_t facesOff   = BwU32(tm + 8);
            uint32_t facesCount = BwU32(tm + 12);
            uint32_t mdxStride  = BwU32(tm + mOff + 0);
            uint32_t mdxPosOff  = BwU32(tm + mOff + 8);
            uint16_t vertCount  = BwU16(tm + mOff + 52);
            uint8_t  hasRender  = tm[mOff + 61];
            uint32_t mdxDataOff = BwU32(tm + mOff + 72);
            uint32_t mdlVertOff = BwU32(tm + mOff + 76);

            if (vertCount > 0 && facesCount > 0 && facesCount <= 500000 && hasRender != 0) {
                if (static_cast<uint64_t>(facesOff) + static_cast<uint64_t>(facesCount) * 32ULL > rawSize) {
                    return false;
                }

                bool loadedNodeVerts = false;
                const uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());

                // 1) Prefer interleaved companion .mdx vertex stream when present and valid
                if (mdxData && mdxSize > 0 && mdxStride >= 12 && mdxStride <= 256 &&
                    mdxPosOff != 0xFFFFFFFFu && mdxPosOff + 12 <= mdxStride &&
                    static_cast<uint64_t>(mdxDataOff) + static_cast<uint64_t>(vertCount) * mdxStride <= mdxSize)
                {
                    for (uint16_t vi = 0; vi < vertCount; ++vi) {
                        const uint8_t* vp = mdxData + mdxDataOff + static_cast<size_t>(vi) * mdxStride + mdxPosOff;
                        float vx = BwF32(vp + 0), vy = BwF32(vp + 4), vz = BwF32(vp + 8);
                        if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return false;
                        mesh.vertices.push_back(worldM.TransformZUpToViewport(vx, vy, vz));
                    }
                    loadedNodeVerts = true;
                }
                // 2) Fallback to inline .mdl float3 vertex array (KotOR 1 PC / Aurora binary)
                else if (mdlVertOff >= 80 &&
                         static_cast<uint64_t>(mdlVertOff) + static_cast<uint64_t>(vertCount) * 12ULL <= rawSize)
                {
                    for (uint16_t vi = 0; vi < vertCount; ++vi) {
                        const uint8_t* vp = base + mdlVertOff + static_cast<size_t>(vi) * 12;
                        float vx = BwF32(vp + 0), vy = BwF32(vp + 4), vz = BwF32(vp + 8);
                        if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return false;
                        mesh.vertices.push_back(worldM.TransformZUpToViewport(vx, vy, vz));
                    }
                    loadedNodeVerts = true;
                }

                if (loadedNodeVerts) {
                    for (uint32_t fi = 0; fi < facesCount; ++fi) {
                        const uint8_t* fp = base + facesOff + static_cast<size_t>(fi) * 32;
                        uint16_t i0 = BwU16(fp + 26);
                        uint16_t i1 = BwU16(fp + 28);
                        uint16_t i2 = BwU16(fp + 30);
                        if (i0 >= vertCount || i1 >= vertCount || i2 >= vertCount) return false;
                        if (i0 == i1 || i1 == i2 || i2 == i0) continue;
                        mesh.tris.push_back({ baseV + i0, baseV + i1, baseV + i2, {} });
                    }
                }
            }
        }

        // Recurse into child node offset table
        uint32_t childArrOff = BwU32(nh + 44);
        uint32_t childCnt    = BwU32(nh + 48);
        if (childCnt > 4096) return false;
        if (childCnt > 0) {
            if (childArrOff < 80 || static_cast<uint64_t>(childArrOff) + static_cast<uint64_t>(childCnt) * 4ULL > rawSize) {
                return false;
            }
            for (uint32_t ci = 0; ci < childCnt; ++ci) {
                uint32_t cNodeOff = BwU32(base + childArrOff + ci * 4);
                if (!walkNode(cNodeOff, worldM, depth + 1)) return false;
            }
        }
        return true;
    };

    if (!walkNode(rootNodeOff, BwMat4{}, 0)) return false;
    if (mesh.vertices.empty() || mesh.tris.empty()) return false;

    mesh.formatName = isKotor2
        ? L"BioWare Odyssey KotOR II Binary Model (.mdl)"
        : L"BioWare Odyssey KotOR Binary Model (.mdl)";
    mesh.animFrameCount = animCount;
    return true;
}

} // namespace BioWare3DDetail

// BioWare Aurora ASCII .mdl ("newmodel <name>" ... "node trimesh/skin/danglymesh <name>" ... "donemodel")
inline bool ParseBioWareAsciiMdl(std::string_view sv, LoadedMesh3D& mesh) {
    using namespace BioWare3DDetail;
    if (!IsLikelyBioWareAsciiMdl(sv)) return false;

    struct AsciiNode {
        std::string nameLower;
        std::string parentLower;
        float px = 0.0f, py = 0.0f, pz = 0.0f;
        float ax = 0.0f, ay = 0.0f, az = 1.0f, ang = 0.0f;
        bool isRenderMesh = true;
        std::vector<Vec3f> localVerts;
        std::vector<std::array<uint32_t, 3>> faces;
    };

    std::vector<AsciiNode> nodes;
    size_t animCount = 0;
    bool inAnim = false;
    int curNodeIdx = -1;
    int pendingVerts = 0;
    int pendingFaces = 0;

    auto nextLine = [&](size_t& pos, std::string_view& outLine) -> bool {
        if (pos >= sv.size()) return false;
        size_t nl = sv.find('\n', pos);
        if (nl == std::string_view::npos) {
            outLine = sv.substr(pos);
            pos = sv.size();
        } else {
            outLine = sv.substr(pos, nl - pos);
            pos = nl + 1;
        }
        size_t hash = outLine.find('#');
        if (hash != std::string_view::npos) outLine = outLine.substr(0, hash);
        while (!outLine.empty() && (outLine.front() == ' ' || outLine.front() == '\t' || outLine.front() == '\r')) outLine.remove_prefix(1);
        while (!outLine.empty() && (outLine.back() == ' ' || outLine.back() == '\t' || outLine.back() == '\r')) outLine.remove_suffix(1);
        return true;
    };

    auto toLowerAscii = [](std::string_view tok) {
        std::string s(tok);
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };

    auto splitTokens = [](std::string_view line, std::string_view toks[12]) -> size_t {
        size_t n = 0, i = 0;
        while (i < line.size() && n < 12) {
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
            if (i >= line.size()) break;
            size_t st = i;
            while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
            toks[n++] = line.substr(st, i - st);
        }
        return n;
    };

    auto parseFloatTok = [](std::string_view t, float& outV) -> bool {
        auto res = std::from_chars(t.data(), t.data() + t.size(), outV);
        return res.ec == std::errc{} && std::isfinite(outV);
    };
    auto parseIntTok = [](std::string_view t, int32_t& outV) -> bool {
        auto res = std::from_chars(t.data(), t.data() + t.size(), outV);
        return res.ec == std::errc{};
    };

    size_t pos = 0;
    std::string_view line;
    while (nextLine(pos, line)) {
        if (line.empty()) continue;
        std::string_view toks[12];
        size_t nt = splitTokens(line, toks);
        if (nt == 0) continue;

        if (pendingVerts > 0 && curNodeIdx >= 0) {
            float vx = 0, vy = 0, vz = 0;
            if (nt < 3 || !parseFloatTok(toks[0], vx) || !parseFloatTok(toks[1], vy) || !parseFloatTok(toks[2], vz)) {
                return false;
            }
            nodes[static_cast<size_t>(curNodeIdx)].localVerts.push_back({ vx, vy, vz });
            --pendingVerts;
            continue;
        }
        if (pendingFaces > 0 && curNodeIdx >= 0) {
            int32_t i0 = -1, i1 = -1, i2 = -1;
            if (nt < 3 || !parseIntTok(toks[0], i0) || !parseIntTok(toks[1], i1) || !parseIntTok(toks[2], i2) ||
                i0 < 0 || i1 < 0 || i2 < 0) {
                return false;
            }
            nodes[static_cast<size_t>(curNodeIdx)].faces.push_back({
                static_cast<uint32_t>(i0),
                static_cast<uint32_t>(i1),
                static_cast<uint32_t>(i2)
            });
            --pendingFaces;
            continue;
        }

        std::string kw = toLowerAscii(toks[0]);
        if (kw == "newanim") {
            inAnim = true;
            ++animCount;
            curNodeIdx = -1;
        } else if (kw == "doneanim") {
            inAnim = false;
            curNodeIdx = -1;
        } else if (!inAnim && kw == "node" && nt >= 3) {
            AsciiNode nd;
            std::string ntype = toLowerAscii(toks[1]);
            nd.nameLower = toLowerAscii(toks[2]);
            nd.isRenderMesh = (ntype == "trimesh" || ntype == "skin" || ntype == "danglymesh" || ntype == "patch");
            nodes.push_back(std::move(nd));
            curNodeIdx = static_cast<int>(nodes.size() - 1);
        } else if (!inAnim && kw == "endnode") {
            if (pendingVerts != 0 || pendingFaces != 0) return false;
            curNodeIdx = -1;
        } else if (!inAnim && curNodeIdx >= 0) {
            AsciiNode& nd = nodes[static_cast<size_t>(curNodeIdx)];
            if (kw == "parent" && nt >= 2) {
                nd.parentLower = toLowerAscii(toks[1]);
                if (nd.parentLower == "null") nd.parentLower.clear();
            } else if (kw == "position" && nt >= 4) {
                parseFloatTok(toks[1], nd.px);
                parseFloatTok(toks[2], nd.py);
                parseFloatTok(toks[3], nd.pz);
            } else if (kw == "orientation" && nt >= 5) {
                parseFloatTok(toks[1], nd.ax);
                parseFloatTok(toks[2], nd.ay);
                parseFloatTok(toks[3], nd.az);
                parseFloatTok(toks[4], nd.ang);
            } else if (kw == "render" && nt >= 2) {
                int32_t rv = 1;
                if (parseIntTok(toks[1], rv)) nd.isRenderMesh = (rv != 0);
            } else if (kw == "verts" && nt >= 2) {
                int32_t nv = 0;
                if (!parseIntTok(toks[1], nv) || nv < 0 || nv > 500000) return false;
                pendingVerts = nv;
                nd.localVerts.reserve(static_cast<size_t>(nv));
            } else if (kw == "faces" && nt >= 2) {
                int32_t nf = 0;
                if (!parseIntTok(toks[1], nf) || nf < 0 || nf > 1000000) return false;
                pendingFaces = nf;
                nd.faces.reserve(static_cast<size_t>(nf));
            }
        }
    }

    if (pendingVerts != 0 || pendingFaces != 0 || nodes.empty()) return false;

    std::unordered_map<std::string, size_t> nameToIdx;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (!nodes[i].nameLower.empty()) nameToIdx[nodes[i].nameLower] = i;
    }

    std::vector<BwMat4> worldMats(nodes.size());
    std::vector<uint8_t> matState(nodes.size(), 0); // 0=unvisited, 1=visiting, 2=done
    std::function<BwMat4(size_t)> resolveWorld = [&](size_t idx) -> BwMat4 {
        if (matState[idx] == 2) return worldMats[idx];
        if (matState[idx] == 1) return BwMat4{};
        matState[idx] = 1;
        const AsciiNode& nd = nodes[idx];
        BwMat4 localM = BwMat4::FromPosAxisAngle(nd.px, nd.py, nd.pz, nd.ax, nd.ay, nd.az, nd.ang);
        if (!nd.parentLower.empty()) {
            auto it = nameToIdx.find(nd.parentLower);
            if (it != nameToIdx.end() && it->second != idx) {
                worldMats[idx] = BwMat4::Mul(resolveWorld(it->second), localM);
                matState[idx] = 2;
                return worldMats[idx];
            }
        }
        worldMats[idx] = localM;
        matState[idx] = 2;
        return worldMats[idx];
    };

    for (size_t i = 0; i < nodes.size(); ++i) {
        const AsciiNode& nd = nodes[i];
        if (!nd.isRenderMesh || nd.localVerts.empty() || nd.faces.empty()) continue;
        BwMat4 wm = resolveWorld(i);
        uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
        for (const auto& lv : nd.localVerts) {
            mesh.vertices.push_back(wm.TransformZUpToViewport(lv.x, lv.y, lv.z));
        }
        for (const auto& fc : nd.faces) {
            if (fc[0] >= nd.localVerts.size() || fc[1] >= nd.localVerts.size() || fc[2] >= nd.localVerts.size()) {
                return false;
            }
            if (fc[0] == fc[1] || fc[1] == fc[2] || fc[2] == fc[0]) continue;
            mesh.tris.push_back({ baseV + fc[0], baseV + fc[1], baseV + fc[2], {} });
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"BioWare Aurora ASCII Model (.mdl)";
    mesh.animFrameCount = animCount;
    return true;
}

// BioWare Odyssey Binary .mdl (loads companion <stem>.mdx from disk when present)
inline bool ParseBioWareBinaryMdl(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    using namespace BioWare3DDetail;
    if (!IsLikelyBioWareBinaryMdl(data, size)) return false;

    std::vector<uint8_t> mdxBytes;
    if (!fullPath.empty()) {
        std::wstring dir = PathUtils::GetParentPath(fullPath);
        std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(fullPath), false);
        PathUtils::ReadFileBytes(dir + L"\\" + stem + L".mdx", 64 * 1024 * 1024, mdxBytes);
    }
    return ParseOdysseyBinaryMdlPair(
        data, size,
        mdxBytes.empty() ? nullptr : mdxBytes.data(),
        mdxBytes.size(),
        mesh);
}

// Companion KotOR .mdx Vertex Buffer (resolves paired <stem>.mdl in the same directory)
inline bool ParseBioWarePairedMdx(
    const uint8_t* mdxData,
    size_t mdxSize,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    using namespace BioWare3DDetail;
    if (!mdxData || mdxSize < 36 || fullPath.empty()) return false;
    std::wstring dir = PathUtils::GetParentPath(fullPath);
    std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(fullPath), false);
    std::vector<uint8_t> mdlBytes;
    if (!PathUtils::ReadFileBytes(dir + L"\\" + stem + L".mdl", 64 * 1024 * 1024, mdlBytes)) {
        return false;
    }
    if (!ParseOdysseyBinaryMdlPair(mdlBytes.data(), mdlBytes.size(), mdxData, mdxSize, mesh)) {
        return false;
    }
    mesh.formatName = L"BioWare Odyssey Paired Mesh (.mdx + .mdl)";
    return true;
}

} // namespace Formats
