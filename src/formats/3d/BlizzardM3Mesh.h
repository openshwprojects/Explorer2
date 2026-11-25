#pragma once
// BlizzardM3Mesh.h — Blizzard StarCraft II / Heroes of the Storm Binary 3D Model (.m3 — "43DM" / "MD34")
// Parses the M3 Reference Table (`MODL`, `U8__` packed vertex buffer, `DIV_` mesh division,
// `U16_` index buffer, and `REGN` submesh regions) with vertex-flag stride calculation.

namespace Formats {

namespace DetailBlizzardM3 {

struct M3RefEntry {
    char     tag[4] = {};
    uint32_t offset = 0u;
    uint32_t count  = 0u;
    uint32_t ver    = 0u;

    bool TagIs(const char* a, const char* b) const {
        return std::memcmp(tag, a, 4) == 0 || std::memcmp(tag, b, 4) == 0;
    }
};

inline size_t ComputeM3VertexStride(uint32_t vFlags) {
    // Base SC2 M3 vertex: float3 pos (12B) + u8[4] boneWeights (4B) + u8[4] boneLookup (4B) + u8[4] normal (4B) = 24B
    // Tangent 4B if (vFlags & 0x100000) or standard tangent bit; UV channels at 0x20000, 0x40000, 0x80000, 0x100000 (4B each)
    size_t stride = 24u;
    if ((vFlags & 0x00000200u) != 0u) stride += 4u; // Vertex color BGRA
    if ((vFlags & 0x00020000u) != 0u) stride += 4u; // UV0 (int16[2])
    if ((vFlags & 0x00040000u) != 0u) stride += 4u; // UV1
    if ((vFlags & 0x00080000u) != 0u) stride += 4u; // UV2
    if ((vFlags & 0x00100000u) != 0u) stride += 4u; // UV3
    stride += 4u; // 4-byte packed tangent vector in standard SC2 MD34/MD33 vertices (base+UV0 = 32B)
    return stride;
}

} // namespace DetailBlizzardM3

inline bool ParseBlizzardM3Mesh(const uint8_t* data, size_t size, LoadedMesh3D& outMesh) {
    if (!data || size < 64u || size > (128u << 20)) return false;
    const bool isMd34 = (std::memcmp(data, "43DM", 4) == 0 || std::memcmp(data, "MD34", 4) == 0);
    const bool isMd33 = (std::memcmp(data, "33DM", 4) == 0 || std::memcmp(data, "MD33", 4) == 0);
    if (!isMd34 && !isMd33) return false;

    const uint32_t refTableOff   = ByteIO::RdU32LE(data + 4u);
    const uint32_t refTableCount = ByteIO::RdU32LE(data + 8u);
    if (refTableCount < 3u || refTableCount > 65536u) return false;
    const uint64_t refBytes = static_cast<uint64_t>(refTableCount) * 16ULL;
    if (refTableOff < 20u || refTableOff > size || refBytes > size - refTableOff) return false;

    std::vector<DetailBlizzardM3::M3RefEntry> refs(refTableCount);
    for (uint32_t i = 0u; i < refTableCount; ++i) {
        const uint8_t* rp = data + refTableOff + static_cast<size_t>(i) * 16u;
        std::memcpy(refs[i].tag, rp, 4u);
        refs[i].offset = ByteIO::RdU32LE(rp + 4u);
        refs[i].count  = ByteIO::RdU32LE(rp + 8u);
        refs[i].ver    = ByteIO::RdU32LE(rp + 12u);
        if (refs[i].offset > size) return false;
    }

    // Locate vertex byte stream (`U8__`), triangle index stream (`U16_`), optional `MODL` vertexFlags, and `REGN` submeshes
    uint32_t vertexFlags = 0x00020000u; // Default 32-byte SC2 vertex (pos + weights + lookup + normal + uv0 + tangent)
    for (const auto& r : refs) {
        if (r.TagIs("LDOM", "MODL") && r.count >= 1u && size - r.offset >= 0x70u) {
            // In MD34 MODL v23..v29, vertexFlags is stored right before the vertices U8__ reference; scan for valid flag mask
            const uint32_t candFlags = ByteIO::RdU32LE(data + r.offset + 0x64u);
            if ((candFlags & 0x001E0000u) != 0u) {
                vertexFlags = candFlags;
            }
            break;
        }
    }

    const DetailBlizzardM3::M3RefEntry* vtxBufRef = nullptr;
    const DetailBlizzardM3::M3RefEntry* idxBufRef = nullptr;
    const DetailBlizzardM3::M3RefEntry* regnRef   = nullptr;

    for (const auto& r : refs) {
        if (!vtxBufRef && r.TagIs("__8U", "U8__") && r.count >= 72u && r.count <= size - r.offset) {
            vtxBufRef = &r;
        } else if (!idxBufRef && r.TagIs("_61U", "U16_") && r.count >= 3u &&
                   static_cast<uint64_t>(r.count) * 2ULL <= size - r.offset) {
            idxBufRef = &r;
        } else if (!regnRef && r.TagIs("NGER", "REGN") && r.count >= 1u) {
            regnRef = &r;
        }
    }

    if (!vtxBufRef || !idxBufRef) return false;

    size_t stride = DetailBlizzardM3::ComputeM3VertexStride(vertexFlags);
    if ((vtxBufRef->count % stride) != 0u) {
        if ((vtxBufRef->count % 32u) == 0u) stride = 32u;
        else if ((vtxBufRef->count % 36u) == 0u) stride = 36u;
        else if ((vtxBufRef->count % 28u) == 0u) stride = 28u;
        else return false;
    }

    const size_t numVerts = vtxBufRef->count / stride;
    if (numVerts < 3u || numVerts > 2000000u) return false;

    outMesh = LoadedMesh3D{};
    outMesh.vertices.reserve(numVerts);
    for (size_t vi = 0u; vi < numVerts; ++vi) {
        const uint8_t* vp = data + vtxBufRef->offset + vi * stride;
        const float x = ByteIO::RdF32LE(vp + 0u);
        const float y = ByteIO::RdF32LE(vp + 4u);
        const float z = ByteIO::RdF32LE(vp + 8u);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            outMesh = {};
            return false;
        }
        outMesh.vertices.push_back({ x, y, z });
    }

    const uint8_t* idxBase = data + idxBufRef->offset;
    bool usedRegn = false;
    if (regnRef) {
        const size_t regnStride = (regnRef->ver >= 4u) ? 40u : 36u;
        if (static_cast<uint64_t>(regnRef->count) * regnStride <= size - regnRef->offset) {
            usedRegn = true;
            for (uint32_t ri = 0u; ri < regnRef->count; ++ri) {
                const uint8_t* rp = data + regnRef->offset + static_cast<size_t>(ri) * regnStride;
                const uint32_t firstVtx = ByteIO::RdU32LE(rp + 8u);
                const uint32_t nVtx     = ByteIO::RdU32LE(rp + 12u);
                const uint32_t firstIdx = ByteIO::RdU32LE(rp + 16u);
                const uint32_t nIdx     = ByteIO::RdU32LE(rp + 20u);
                if (firstVtx > numVerts || nVtx > numVerts - firstVtx ||
                    firstIdx > idxBufRef->count || nIdx > idxBufRef->count - firstIdx) {
                    outMesh = {};
                    return false;
                }
                for (uint32_t k = 0u; k + 2u < nIdx; k += 3u) {
                    const uint32_t i0 = firstVtx + ByteIO::RdU16LE(idxBase + static_cast<size_t>(firstIdx + k + 0u) * 2u);
                    const uint32_t i1 = firstVtx + ByteIO::RdU16LE(idxBase + static_cast<size_t>(firstIdx + k + 1u) * 2u);
                    const uint32_t i2 = firstVtx + ByteIO::RdU16LE(idxBase + static_cast<size_t>(firstIdx + k + 2u) * 2u);
                    if (i0 >= numVerts || i1 >= numVerts || i2 >= numVerts) { outMesh = {}; return false; }
                    if (i0 != i1 && i1 != i2 && i0 != i2) {
                        outMesh.tris.push_back({ i0, i1, i2, {} });
                    }
                }
            }
        }
    }

    if (!usedRegn) {
        for (uint32_t k = 0u; k + 2u < idxBufRef->count; k += 3u) {
            const uint32_t i0 = ByteIO::RdU16LE(idxBase + static_cast<size_t>(k + 0u) * 2u);
            const uint32_t i1 = ByteIO::RdU16LE(idxBase + static_cast<size_t>(k + 1u) * 2u);
            const uint32_t i2 = ByteIO::RdU16LE(idxBase + static_cast<size_t>(k + 2u) * 2u);
            if (i0 >= numVerts || i1 >= numVerts || i2 >= numVerts) { outMesh = {}; return false; }
            if (i0 != i1 && i1 != i2 && i0 != i2) {
                outMesh.tris.push_back({ i0, i1, i2, {} });
            }
        }
    }

    if (outMesh.tris.empty()) {
        outMesh = {};
        return false;
    }

    outMesh.formatName = isMd34
        ? L"Blizzard StarCraft II Model (.m3 MD34)"
        : L"Blizzard StarCraft II Model (.m3 MD33)";
    return !outMesh.vertices.empty() && !outMesh.tris.empty();
}

} // namespace Formats
