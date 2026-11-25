#pragma once
// Descent3Oof.h — Outrage Entertainment Descent 3 Object Format (.oof, "PSPO" v1800..v2300) reader
// Parses OHDR + hierarchical SOBJ submodels (parent offsets, vertices, normals, alpha, and N-gon faces).

namespace Formats {

inline bool ParseDescent3Oof(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 32) return false;
    if (std::memcmp(data, "PSPO", 4) != 0) return false;

    const int32_t version = ByteIO::RdI32LE(data + 4u);
    if (version < 1800 || version > 2500) return false;

    struct SobjParsed {
        int32_t subNum = 0;
        int32_t parent = -1;
        Vec3f localOffset = {};
        std::vector<Vec3f> verts;
        std::vector<MeshTriangle> localTris;
    };
    std::vector<SobjParsed> sobjs;
    int32_t declaredSubmodels = -1;

    size_t pos = 8u;
    while (pos + 8u <= size) {
        const uint8_t* cHdr = data + pos;
        const int32_t cLen  = ByteIO::RdI32LE(cHdr + 4u);
        if (cLen < 0 || pos + 8u + static_cast<size_t>(cLen) > size) return false;

        const uint8_t* pay = cHdr + 8u;
        const size_t payLen = static_cast<size_t>(cLen);

        if (std::memcmp(cHdr, "OHDR", 4) == 0) {
            if (payLen < 28u) return false;
            declaredSubmodels = ByteIO::RdI32LE(pay + 0u);
            if (declaredSubmodels < 1 || declaredSubmodels > 512) return false;
        } else if (std::memcmp(cHdr, "SOBJ", 4) == 0) {
            const size_t fixedBeforeNames = (version > 1805) ? 72u : 60u;
            if (payLen < fixedBeforeNames + 18u) return false;

            SobjParsed sb;
            sb.subNum = ByteIO::RdI32LE(pay + 0u);
            sb.parent = ByteIO::RdI32LE(pay + 4u);
            sb.localOffset = {
                ByteIO::RdF32LE(pay + 36u),
                ByteIO::RdF32LE(pay + 40u),
                ByteIO::RdF32LE(pay + 44u)
            };
            if (!std::isfinite(sb.localOffset.x) || !std::isfinite(sb.localOffset.y) || !std::isfinite(sb.localOffset.z)) {
                return false;
            }
            if (sb.subNum < 0 || sb.subNum > 512 || sb.parent < -1 || sb.parent == sb.subNum) return false;

            size_t sp = fixedBeforeNames;
            // Read 2 null-terminated strings: submodel name and submodel props
            for (int sIdx = 0; sIdx < 2; ++sIdx) {
                const size_t start = sp;
                while (sp < payLen && pay[sp] != 0) {
                    if (sp - start > 256u) return false;
                    ++sp;
                }
                if (sp >= payLen) return false;
                ++sp; // skip '\0'
            }

            if (sp + 12u > payLen) return false;
            sp += 8u; // movement_type, movement_axis
            const int32_t nFsChunks = ByteIO::RdI32LE(pay + sp);
            sp += 4u;
            if (nFsChunks < 0 || nFsChunks > 65536 || sp + static_cast<size_t>(nFsChunks) * 4u > payLen) {
                return false;
            }
            sp += static_cast<size_t>(nFsChunks) * 4u;

            if (sp + 4u > payLen) return false;
            const int32_t nVerts = ByteIO::RdI32LE(pay + sp);
            sp += 4u;
            if (nVerts < 0 || nVerts > 250000) return false;

            const size_t perVertBytes = (version >= 2300) ? 28u : 24u; // verts(12) + norms(12) [+ alpha(4)]
            if (sp + static_cast<size_t>(nVerts) * perVertBytes + 4u > payLen) return false;

            sb.verts.resize(static_cast<size_t>(nVerts));
            for (int32_t v = 0; v < nVerts; ++v) {
                const float vx = ByteIO::RdF32LE(pay + sp + static_cast<size_t>(v) * 12u + 0u);
                const float vy = ByteIO::RdF32LE(pay + sp + static_cast<size_t>(v) * 12u + 4u);
                const float vz = ByteIO::RdF32LE(pay + sp + static_cast<size_t>(v) * 12u + 8u);
                if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return false;
                sb.verts[static_cast<size_t>(v)] = { vx, vy, vz };
            }
            sp += static_cast<size_t>(nVerts) * perVertBytes;

            const int32_t nFaces = ByteIO::RdI32LE(pay + sp);
            sp += 4u;
            if (nFaces < 0 || nFaces > 500000) return false;

            for (int32_t f = 0; f < nFaces; ++f) {
                if (sp + 20u > payLen) return false;
                sp += 12u; // face normal (3 floats)
                const int32_t fvCount  = ByteIO::RdI32LE(pay + sp + 0u);
                const int32_t textured = ByteIO::RdI32LE(pay + sp + 4u);
                sp += 8u;
                if (fvCount < 3 || fvCount > 128) return false;
                if (textured != 0) {
                    if (sp + 2u > payLen) return false;
                    sp += 2u; // int16 texnum
                }
                const size_t fvBytes = static_cast<size_t>(fvCount) * 12u + ((version >= 2100) ? 8u : 0u);
                if (sp + fvBytes > payLen) return false;

                std::vector<uint32_t> fIdx(static_cast<size_t>(fvCount));
                for (int32_t k = 0; k < fvCount; ++k) {
                    const int32_t vi = ByteIO::RdI32LE(pay + sp + static_cast<size_t>(k) * 12u);
                    if (vi < 0 || vi >= nVerts) return false;
                    fIdx[static_cast<size_t>(k)] = static_cast<uint32_t>(vi);
                }
                sp += fvBytes;

                for (int32_t k = 1; k + 1 < fvCount; ++k) {
                    const uint32_t a = fIdx[0];
                    const uint32_t b = fIdx[static_cast<size_t>(k)];
                    const uint32_t c = fIdx[static_cast<size_t>(k + 1)];
                    if (a != b && b != c && a != c) {
                        sb.localTris.push_back({ a, b, c, {} });
                    }
                }
            }
            sobjs.push_back(std::move(sb));
        }

        pos += 8u + payLen;
    }

    if (declaredSubmodels < 1 || sobjs.empty()) return false;

    // Map submodelNum -> index in sobjs for cycle-safe world offset accumulation
    std::unordered_map<int32_t, size_t> subMap;
    for (size_t i = 0; i < sobjs.size(); ++i) {
        subMap[sobjs[i].subNum] = i;
    }

    for (size_t i = 0; i < sobjs.size(); ++i) {
        Vec3f worldOff = { 0.0f, 0.0f, 0.0f };
        int32_t curSub = sobjs[i].subNum;
        int hops = 0;
        while (curSub != -1) {
            if (++hops > 64) return false; // parent cycle
            auto it = subMap.find(curSub);
            if (it == subMap.end()) return false;
            const auto& node = sobjs[it->second];
            worldOff.x += node.localOffset.x;
            worldOff.y += node.localOffset.y;
            worldOff.z += node.localOffset.z;
            curSub = node.parent;
        }

        const uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
        for (const auto& lv : sobjs[i].verts) {
            mesh.vertices.push_back({ lv.x + worldOff.x, lv.y + worldOff.y, lv.z + worldOff.z });
        }
        for (const auto& lt : sobjs[i].localTris) {
            mesh.tris.push_back({ baseV + lt.i0, baseV + lt.i1, baseV + lt.i2, {} });
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Descent 3 Outrage Object (OOF)";
    return true;
}

} // namespace Formats
