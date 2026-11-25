#pragma once
// NintendoGxMeshCommon.h — Shared Nintendo GameCube / Wii GX hardware display-list primitive decoder
// Reused by JSystem J3D (.bmd / .bdl) and Wii NW4R (.mdl0) 3D model loaders.

namespace Formats::NintendoGxMesh {

struct GxVtxStreamLayout {
    size_t vertexStride = 0u;
    size_t posIdxOffset = 0u;
    bool   posIdx16Bit  = true;
    bool   hasPos       = false;
};

// Decodes a big-endian GX display list (opcodes 0x80 QUADS, 0x90 TRIANGLES, 0x98 TRISTRIP, 0xA0 TRIFAN, 0x00 NOP)
// appending indexed triangles (offset by `meshVertBase`) into `outTris`.
inline bool DecodeGxDisplayList(
    const uint8_t* dlData,
    size_t dlSize,
    const GxVtxStreamLayout& layout,
    size_t posPoolCount,
    uint32_t meshVertBase,
    std::vector<MeshTriangle>& outTris)
{
    if (!dlData || !layout.hasPos || layout.vertexStride == 0u || posPoolCount == 0u) return false;
    const size_t idxBytes = layout.posIdx16Bit ? 2u : 1u;
    if (layout.posIdxOffset + idxBytes > layout.vertexStride) return false;

    auto readPosIdx = [&](const uint8_t* vtxPtr, uint32_t& outIdx) -> bool {
        const uint32_t raw = layout.posIdx16Bit
            ? static_cast<uint32_t>(ByteIO::RdU16BE(vtxPtr + layout.posIdxOffset))
            : static_cast<uint32_t>(vtxPtr[layout.posIdxOffset]);
        if (raw >= posPoolCount) return false;
        outIdx = meshVertBase + raw;
        return true;
    };

    auto pushTri = [&](uint32_t a, uint32_t b, uint32_t c) {
        if (a != b && b != c && a != c && outTris.size() < 2000000u) {
            outTris.push_back({ a, b, c, {} });
        }
    };

    size_t pos = 0u;
    while (pos < dlSize) {
        const uint8_t cmd = dlData[pos++];
        if (cmd == 0x00u) continue; // GX_NOP alignment padding

        const uint8_t primOp = static_cast<uint8_t>(cmd & 0xF8u);
        if (primOp != 0x80u && primOp != 0x88u &&
            primOp != 0x90u && primOp != 0x98u &&
            primOp != 0xA0u && primOp != 0xA8u &&
            primOp != 0xB0u && primOp != 0xB8u) {
            // Stop cleanly on unknown/end opcode or return false if nothing decoded yet
            return false;
        }

        if (dlSize - pos < 2u) return false;
        const uint16_t vtxCount = ByteIO::RdU16BE(dlData + pos);
        pos += 2u;

        const uint64_t streamBytes = static_cast<uint64_t>(vtxCount) * layout.vertexStride;
        if (streamBytes > dlSize - pos) return false;
        const uint8_t* vStream = dlData + pos;
        pos += static_cast<size_t>(streamBytes);

        if (vtxCount == 0u) continue;

        if (primOp == 0x90u) {
            // GX_TRIANGLES
            for (uint16_t i = 0u; i + 2u < vtxCount; i = static_cast<uint16_t>(i + 3u)) {
                uint32_t i0 = 0, i1 = 0, i2 = 0;
                if (!readPosIdx(vStream + static_cast<size_t>(i + 0u) * layout.vertexStride, i0) ||
                    !readPosIdx(vStream + static_cast<size_t>(i + 1u) * layout.vertexStride, i1) ||
                    !readPosIdx(vStream + static_cast<size_t>(i + 2u) * layout.vertexStride, i2)) {
                    return false;
                }
                pushTri(i0, i1, i2);
            }
        } else if (primOp == 0x98u) {
            // GX_TRISTRIP
            if (vtxCount >= 3u) {
                uint32_t p0 = 0, p1 = 0;
                if (!readPosIdx(vStream + 0u * layout.vertexStride, p0) ||
                    !readPosIdx(vStream + 1u * layout.vertexStride, p1)) {
                    return false;
                }
                for (uint16_t i = 2u; i < vtxCount; ++i) {
                    uint32_t p2 = 0;
                    if (!readPosIdx(vStream + static_cast<size_t>(i) * layout.vertexStride, p2)) return false;
                    if ((i & 1u) == 0u) pushTri(p0, p1, p2);
                    else                pushTri(p1, p0, p2);
                    p0 = p1;
                    p1 = p2;
                }
            }
        } else if (primOp == 0xA0u) {
            // GX_TRIFAN
            if (vtxCount >= 3u) {
                uint32_t center = 0, prev = 0;
                if (!readPosIdx(vStream + 0u * layout.vertexStride, center) ||
                    !readPosIdx(vStream + 1u * layout.vertexStride, prev)) {
                    return false;
                }
                for (uint16_t i = 2u; i < vtxCount; ++i) {
                    uint32_t cur = 0;
                    if (!readPosIdx(vStream + static_cast<size_t>(i) * layout.vertexStride, cur)) return false;
                    pushTri(center, prev, cur);
                    prev = cur;
                }
            }
        } else if (primOp == 0x80u || primOp == 0x88u) {
            // GX_QUADS
            for (uint16_t i = 0u; i + 3u < vtxCount; i = static_cast<uint16_t>(i + 4u)) {
                uint32_t i0 = 0, i1 = 0, i2 = 0, i3 = 0;
                if (!readPosIdx(vStream + static_cast<size_t>(i + 0u) * layout.vertexStride, i0) ||
                    !readPosIdx(vStream + static_cast<size_t>(i + 1u) * layout.vertexStride, i1) ||
                    !readPosIdx(vStream + static_cast<size_t>(i + 2u) * layout.vertexStride, i2) ||
                    !readPosIdx(vStream + static_cast<size_t>(i + 3u) * layout.vertexStride, i3)) {
                    return false;
                }
                pushTri(i0, i1, i2);
                pushTri(i0, i2, i3);
            }
        }
    }
    return true;
}

// Decodes a big-endian GX position array (`compType`: 0=u8, 1=s8, 2=u16, 3=s16, 4=f32; `fracShift` fixed-point shift)
inline bool DecodeGxPositionArrayBE(
    const uint8_t* ptr,
    size_t byteLen,
    uint32_t compType,
    uint8_t fracShift,
    bool isXYZ,
    std::vector<Vec3f>& outPositions,
    uint8_t explicitStride = 0u)
{
    outPositions.clear();
    const size_t nComp = isXYZ ? 3u : 2u;
    size_t compBytes = 0u;
    switch (compType) {
    case 0u: case 1u: compBytes = 1u; break;
    case 2u: case 3u: compBytes = 2u; break;
    case 4u:          compBytes = 4u; break;
    default: return false;
    }
    const size_t minElemBytes = nComp * compBytes;
    const size_t stride = (explicitStride >= minElemBytes) ? static_cast<size_t>(explicitStride) : minElemBytes;
    if (stride == 0u || byteLen < minElemBytes) return false;

    const float scale = (compType == 4u) ? 1.0f : std::ldexp(1.0f, -static_cast<int>(fracShift & 31u));
    const size_t count = (byteLen - minElemBytes) / stride + 1u;
    if (count > 2000000u) return false;
    outPositions.reserve(count);

    for (size_t i = 0u; i < count; ++i) {
        const uint8_t* p = ptr + i * stride;
        float c[3] = { 0.0f, 0.0f, 0.0f };
        for (size_t k = 0u; k < nComp; ++k) {
            const uint8_t* cp = p + k * compBytes;
            if (compType == 4u) {
                uint32_t u = ByteIO::RdU32BE(cp);
                float f = 0.0f;
                std::memcpy(&f, &u, 4u);
                if (!std::isfinite(f)) return false;
                c[k] = f;
            } else if (compType == 3u) {
                c[k] = static_cast<float>(static_cast<int16_t>(ByteIO::RdU16BE(cp))) * scale;
            } else if (compType == 2u) {
                c[k] = static_cast<float>(ByteIO::RdU16BE(cp)) * scale;
            } else if (compType == 1u) {
                c[k] = static_cast<float>(static_cast<int8_t>(cp[0])) * scale;
            } else {
                c[k] = static_cast<float>(cp[0]) * scale;
            }
        }
        outPositions.push_back({ c[0], c[1], c[2] });
    }
    return !outPositions.empty();
}

} // namespace Formats::NintendoGxMesh
