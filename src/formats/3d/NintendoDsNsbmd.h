#pragma once
// NintendoDsNsbmd.h — Nintendo DS NITRO-System Binary 3D Model (.nsbmd)
// Parses `BMD0` container & `MDL0` NITRO dictionary, decoding DS hardware Geometry Engine
// packed 4-command display lists (`BEGIN_VTXS`, `VTX_16`, `VTX_10`, `VTX_XY/XZ/YZ`, `VTX_DIFF`).

namespace Formats {

namespace DetailNitroNsbmd {

inline size_t DsGpuCmdParamWords(uint8_t cmd) {
    switch (cmd) {
    case 0x00u: return 0u; // NOP
    case 0x10u: return 1u; // MTX_MODE
    case 0x11u: return 0u; // MTX_PUSH
    case 0x12u: return 1u; // MTX_POP
    case 0x13u: return 1u; // MTX_STORE
    case 0x14u: return 1u; // MTX_RESTORE
    case 0x15u: return 0u; // MTX_IDENTITY
    case 0x16u: return 16u; // MTX_LOAD_4x4
    case 0x17u: return 12u; // MTX_LOAD_4x3
    case 0x18u: return 16u; // MTX_MULT_4x4
    case 0x19u: return 12u; // MTX_MULT_4x3
    case 0x1Au: return 9u;  // MTX_MULT_3x3
    case 0x1Bu: return 3u;  // MTX_SCALE
    case 0x1Cu: return 3u;  // MTX_TRANS
    case 0x20u: return 1u;  // COLOR
    case 0x21u: return 1u;  // NORMAL
    case 0x22u: return 1u;  // TEXCOORD
    case 0x23u: return 2u;  // VTX_16
    case 0x24u: return 1u;  // VTX_10
    case 0x25u: return 1u;  // VTX_XY
    case 0x26u: return 1u;  // VTX_XZ
    case 0x27u: return 1u;  // VTX_YZ
    case 0x28u: return 1u;  // VTX_DIFF
    case 0x29u: return 1u;  // POLYGON_ATTR
    case 0x2Au: return 1u;  // TEXIMAGE_PARAM
    case 0x2Bu: return 1u;  // PLTT_BASE
    case 0x30u: return 1u;  // DIF_AMB
    case 0x31u: return 1u;  // SPE_EMI
    case 0x32u: return 1u;  // LIGHT_VECTOR
    case 0x33u: return 1u;  // LIGHT_COLOR
    case 0x34u: return 32u; // SHININESS
    case 0x40u: return 1u;  // BEGIN_VTXS
    case 0x41u: return 0u;  // END_VTXS
    case 0x70u: return 3u;  // BOX_TEST
    case 0x71u: return 2u;  // POS_TEST
    case 0x72u: return 1u;  // VEC_TEST
    default:    return SIZE_MAX;
    }
}

inline int16_t SignExtend10(uint32_t v) {
    int32_t s = static_cast<int32_t>(v & 0x3FFu);
    if ((s & 0x200) != 0) s -= 0x400;
    return static_cast<int16_t>(s);
}

// Parses a NITRO G3D Dict at `dictPtr` (within `limitBytes`), returning each entry's value data slice.
inline bool ReadNitroDictValues(
    const uint8_t* dictPtr,
    size_t limitBytes,
    size_t expectedValStride,
    std::vector<const uint8_t*>& outValPtrs)
{
    outValPtrs.clear();
    if (limitBytes < 16u) return false;
    const uint8_t  revision = dictPtr[0];
    const uint8_t  count    = dictPtr[1];
    const uint16_t dictSize = ByteIO::RdU16LE(dictPtr + 2u);
    if (revision != 0u || count == 0u || dictSize < 16u || dictSize > limitBytes) return false;

    const uint16_t refOfs = ByteIO::RdU16LE(dictPtr + 6u);
    if (refOfs < 8u || static_cast<size_t>(refOfs) + 4u > dictSize) return false;
    const uint16_t valStride = ByteIO::RdU16LE(dictPtr + refOfs + 0u);
    const uint16_t namesOfs  = ByteIO::RdU16LE(dictPtr + refOfs + 2u);
    if (valStride < expectedValStride || valStride > 256u) return false;

    const uint64_t valsStart = static_cast<uint64_t>(refOfs) + 4ULL;
    const uint64_t valsEnd   = valsStart + static_cast<uint64_t>(count) * valStride;
    if (valsEnd > dictSize || namesOfs < valsEnd) return false;

    outValPtrs.reserve(count);
    for (uint8_t i = 0u; i < count; ++i) {
        outValPtrs.push_back(dictPtr + static_cast<size_t>(valsStart) + static_cast<size_t>(i) * valStride);
    }
    return true;
}

inline bool DecodeDsDisplayList(
    const uint8_t* dl,
    size_t dlBytes,
    float posScale,
    LoadedMesh3D& mesh)
{
    if ((dlBytes & 3u) != 0u || dlBytes < 4u) return false;
    int32_t curX = 0, curY = 0, curZ = 0; // FX16 (1.3.12)
    int primMode = -1; // 0=tris, 1=quads, 2=tristrip, 3=quadstrip
    std::vector<uint32_t> batchVerts;

    auto flushPrimBatch = [&]() {
        const size_t n = batchVerts.size();
        auto addTri = [&](uint32_t a, uint32_t b, uint32_t c) {
            if (a != b && b != c && a != c && mesh.tris.size() < 2000000u) {
                mesh.tris.push_back({ a, b, c, {} });
            }
        };
        if (primMode == 0) { // TRIANGLES
            for (size_t i = 0u; i + 2u < n; i += 3u) {
                addTri(batchVerts[i], batchVerts[i + 1u], batchVerts[i + 2u]);
            }
        } else if (primMode == 1) { // QUADS
            for (size_t i = 0u; i + 3u < n; i += 4u) {
                addTri(batchVerts[i], batchVerts[i + 1u], batchVerts[i + 2u]);
                addTri(batchVerts[i], batchVerts[i + 2u], batchVerts[i + 3u]);
            }
        } else if (primMode == 2) { // TRIANGLE_STRIP
            for (size_t i = 2u; i < n; ++i) {
                if ((i & 1u) == 0u) addTri(batchVerts[i - 2u], batchVerts[i - 1u], batchVerts[i]);
                else                addTri(batchVerts[i - 1u], batchVerts[i - 2u], batchVerts[i]);
            }
        } else if (primMode == 3) { // QUAD_STRIP
            for (size_t i = 0u; i + 3u < n; i += 2u) {
                addTri(batchVerts[i], batchVerts[i + 1u], batchVerts[i + 3u]);
                addTri(batchVerts[i], batchVerts[i + 3u], batchVerts[i + 2u]);
            }
        }
        batchVerts.clear();
    };

    auto pushCurVertex = [&]() {
        if (primMode < 0 || mesh.vertices.size() >= 2000000u) return;
        const uint32_t idx = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({
            static_cast<float>(curX) * posScale,
            static_cast<float>(curY) * posScale,
            static_cast<float>(curZ) * posScale
        });
        batchVerts.push_back(idx);
    };

    size_t pos = 0u;
    while (pos + 4u <= dlBytes) {
        const uint8_t cmds[4] = { dl[pos + 0u], dl[pos + 1u], dl[pos + 2u], dl[pos + 3u] };
        pos += 4u;
        for (int ci = 0; ci < 4; ++ci) {
            const uint8_t op = cmds[ci];
            const size_t nParams = DsGpuCmdParamWords(op);
            if (nParams == SIZE_MAX) return false;
            if (nParams * 4u > dlBytes - pos) return false;
            const uint8_t* p = dl + pos;
            pos += nParams * 4u;

            if (op == 0x40u) { // BEGIN_VTXS
                flushPrimBatch();
                primMode = static_cast<int>(ByteIO::RdU32LE(p) & 3u);
            } else if (op == 0x41u) { // END_VTXS
                flushPrimBatch();
                primMode = -1;
            } else if (op == 0x23u) { // VTX_16
                const uint32_t w0 = ByteIO::RdU32LE(p + 0u);
                const uint32_t w1 = ByteIO::RdU32LE(p + 4u);
                curX = static_cast<int16_t>(w0 & 0xFFFFu);
                curY = static_cast<int16_t>((w0 >> 16u) & 0xFFFFu);
                curZ = static_cast<int16_t>(w1 & 0xFFFFu);
                pushCurVertex();
            } else if (op == 0x24u) { // VTX_10 (signed 4.6 -> shift left 6 to FX16 4.12)
                const uint32_t w0 = ByteIO::RdU32LE(p);
                curX = static_cast<int32_t>(SignExtend10(w0 >> 0u))  << 6;
                curY = static_cast<int32_t>(SignExtend10(w0 >> 10u)) << 6;
                curZ = static_cast<int32_t>(SignExtend10(w0 >> 20u)) << 6;
                pushCurVertex();
            } else if (op == 0x25u) { // VTX_XY
                const uint32_t w0 = ByteIO::RdU32LE(p);
                curX = static_cast<int16_t>(w0 & 0xFFFFu);
                curY = static_cast<int16_t>((w0 >> 16u) & 0xFFFFu);
                pushCurVertex();
            } else if (op == 0x26u) { // VTX_XZ
                const uint32_t w0 = ByteIO::RdU32LE(p);
                curX = static_cast<int16_t>(w0 & 0xFFFFu);
                curZ = static_cast<int16_t>((w0 >> 16u) & 0xFFFFu);
                pushCurVertex();
            } else if (op == 0x27u) { // VTX_YZ
                const uint32_t w0 = ByteIO::RdU32LE(p);
                curY = static_cast<int16_t>(w0 & 0xFFFFu);
                curZ = static_cast<int16_t>((w0 >> 16u) & 0xFFFFu);
                pushCurVertex();
            } else if (op == 0x28u) { // VTX_DIFF (signed 10-bit 0.9 delta in FX16 units)
                const uint32_t w0 = ByteIO::RdU32LE(p);
                curX += SignExtend10(w0 >> 0u);
                curY += SignExtend10(w0 >> 10u);
                curZ += SignExtend10(w0 >> 20u);
                pushCurVertex();
            }
        }
    }
    flushPrimBatch();
    return true;
}

} // namespace DetailNitroNsbmd

inline bool ParseNintendoDsNsbmd(const uint8_t* data, size_t size, LoadedMesh3D& outMesh) {
    if (!data || size < 48u || size > (64u << 20)) return false;
    if (std::memcmp(data, "BMD0", 4) != 0) return false;

    const uint16_t bom       = ByteIO::RdU16LE(data + 4u);
    const uint16_t version   = ByteIO::RdU16LE(data + 6u);
    const uint32_t fileSize  = ByteIO::RdU32LE(data + 8u);
    const uint16_t hdrSize   = ByteIO::RdU16LE(data + 12u);
    const uint16_t numBlocks = ByteIO::RdU16LE(data + 14u);

    if (bom != 0xFEFFu || (version != 1u && version != 2u)) return false;
    if (fileSize < 48u || fileSize > size || hdrSize < 16u || numBlocks == 0u || numBlocks > 16u) return false;
    if (static_cast<uint64_t>(hdrSize) + static_cast<uint64_t>(numBlocks) * 4ULL > fileSize) return false;

    const uint8_t* mdl0Blk = nullptr;
    size_t mdl0Len = 0u;
    for (uint16_t b = 0u; b < numBlocks; ++b) {
        const uint32_t blkOff = ByteIO::RdU32LE(data + hdrSize + static_cast<size_t>(b) * 4u);
        if (blkOff >= fileSize || fileSize - blkOff < 24u) return false;
        const uint32_t blkSz = ByteIO::RdU32LE(data + blkOff + 4u);
        if (blkSz < 24u || blkSz > fileSize - blkOff) return false;
        if (std::memcmp(data + blkOff, "MDL0", 4) == 0) {
            mdl0Blk = data + blkOff;
            mdl0Len = blkSz;
            break;
        }
    }
    if (!mdl0Blk) return false;

    std::vector<const uint8_t*> modelOffPtrs;
    if (!DetailNitroNsbmd::ReadNitroDictValues(mdl0Blk + 8u, mdl0Len - 8u, 4u, modelOffPtrs)) return false;

    outMesh = LoadedMesh3D{};
    for (const uint8_t* mp : modelOffPtrs) {
        const uint32_t mdlRel = ByteIO::RdU32LE(mp);
        if (mdlRel < 24u || mdlRel >= mdl0Len || mdl0Len - mdlRel < 0x40u) {
            outMesh = {};
            return false;
        }
        const uint8_t* mdl = mdl0Blk + mdlRel;
        const uint32_t mdlSz = ByteIO::RdU32LE(mdl + 0x00u);
        const uint32_t shpRel = ByteIO::RdU32LE(mdl + 0x0Cu);
        const int32_t  upScaleShift = ByteIO::RdI32LE(mdl + 0x20u);

        if (mdlSz < 0x40u || mdlSz > mdl0Len - mdlRel || shpRel >= mdlSz) {
            outMesh = {};
            return false;
        }
        const int clampedShift = std::clamp<int>(upScaleShift, -16, 16);
        const float posScale = std::ldexp(1.0f / 4096.0f, clampedShift);

        std::vector<const uint8_t*> polyOffPtrs;
        if (!DetailNitroNsbmd::ReadNitroDictValues(mdl + shpRel, mdlSz - shpRel, 4u, polyOffPtrs)) {
            outMesh = {};
            return false;
        }

        const uint8_t* shpBase = mdl + shpRel;
        const size_t   shpMax  = mdlSz - shpRel;
        for (const uint8_t* pp : polyOffPtrs) {
            const uint32_t polyRel = ByteIO::RdU32LE(pp);
            if (polyRel >= shpMax || shpMax - polyRel < 16u) { outMesh = {}; return false; }
            const uint8_t* polyHdr = shpBase + polyRel;
            const uint32_t dlRel   = ByteIO::RdU32LE(polyHdr + 8u);
            const uint32_t dlBytes = ByteIO::RdU32LE(polyHdr + 12u);
            const uint64_t dlAbsInShp = static_cast<uint64_t>(polyRel) + dlRel;
            if (dlBytes == 0u || dlAbsInShp > shpMax || dlBytes > shpMax - static_cast<size_t>(dlAbsInShp)) {
                outMesh = {};
                return false;
            }
            if (!DetailNitroNsbmd::DecodeDsDisplayList(
                    shpBase + static_cast<size_t>(dlAbsInShp), dlBytes, posScale, outMesh)) {
                outMesh = {};
                return false;
            }
        }
    }

    if (outMesh.vertices.empty() || outMesh.tris.empty()) {
        outMesh = {};
        return false;
    }

    outMesh.formatName = L"Nintendo DS NITRO Model (.nsbmd)";
    return !outMesh.vertices.empty() && !outMesh.tris.empty();
}

} // namespace Formats
