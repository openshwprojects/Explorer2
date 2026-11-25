#pragma once
// RtcwMdc.h — Return to Castle Wolfenstein & Wolfenstein: Enemy Territory Compressed Model (.mdc, "IDPC" v2)
// Parses mdcHeader_t (112B) and multi-surface mdcSurface_t (124B), reconstructing Frame 0 from
// md3XyzNormal_t base frames + optional R_MDC_DecodeXyzCompressed 24-bit delta offsets.

namespace Formats {

inline bool ParseRtcwMdc(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    // mdcHeader_t is 112 bytes
    if (!data || size < 112) return false;
    if (std::memcmp(data, "IDPC", 4) != 0) return false;
    if (ByteIO::RdI32LE(data + 4) != 2) return false;

    const int32_t numFrames   = ByteIO::RdI32LE(data + 76);
    const int32_t numTags     = ByteIO::RdI32LE(data + 80);
    const int32_t numSurfaces = ByteIO::RdI32LE(data + 84);
    const int32_t ofsFrames   = ByteIO::RdI32LE(data + 92);
    const int32_t ofsSurfaces = ByteIO::RdI32LE(data + 104);
    const int32_t ofsEnd      = ByteIO::RdI32LE(data + 108);

    if (numFrames < 1 || numFrames > 65536 || numTags < 0 || numTags > 1024) return false;
    if (numSurfaces < 1 || numSurfaces > 1024) return false;
    if (ofsEnd < 112 || static_cast<size_t>(ofsEnd) > size) return false;
    const size_t fileEnd = static_cast<size_t>(ofsEnd);

    // md3Frame_t is 56 bytes per frame
    if (ofsFrames < 112 || static_cast<uint64_t>(ofsFrames) + static_cast<uint64_t>(numFrames) * 56ULL > fileEnd) {
        return false;
    }
    if (ofsSurfaces < 112 || static_cast<size_t>(ofsSurfaces) >= fileEnd) return false;

    size_t sPos = static_cast<size_t>(ofsSurfaces);
    for (int32_t s = 0; s < numSurfaces; ++s) {
        // mdcSurface_t is 124 bytes
        if (sPos + 124u > fileEnd) return false;
        const int32_t surfIdent = ByteIO::RdI32LE(data + sPos + 0u);
        if (surfIdent != 7 && std::memcmp(data + sPos, "IDPC", 4) != 0 && std::memcmp(data + sPos, "IDP3", 4) != 0) {
            return false;
        }

        const int32_t numCompFrames = ByteIO::RdI32LE(data + sPos + 72u);
        const int32_t numBaseFrames = ByteIO::RdI32LE(data + sPos + 76u);
        const int32_t numVerts      = ByteIO::RdI32LE(data + sPos + 84u);
        const int32_t numTris       = ByteIO::RdI32LE(data + sPos + 88u);
        const int32_t ofsTris       = ByteIO::RdI32LE(data + sPos + 92u);
        const int32_t ofsXyz        = ByteIO::RdI32LE(data + sPos + 104u);
        const int32_t ofsCompVerts  = ByteIO::RdI32LE(data + sPos + 108u);
        const int32_t ofsBaseMap    = ByteIO::RdI32LE(data + sPos + 112u);
        const int32_t ofsCompMap    = ByteIO::RdI32LE(data + sPos + 116u);
        const int32_t sRelEnd       = ByteIO::RdI32LE(data + sPos + 120u);

        if (sRelEnd < 124 || sPos + static_cast<size_t>(sRelEnd) > fileEnd) return false;
        const size_t sEnd = sPos + static_cast<size_t>(sRelEnd);
        if (numBaseFrames < 1 || numBaseFrames > 16384 || numCompFrames < 0 || numCompFrames > 65536) return false;
        if (numVerts < 0 || numVerts > 250000 || numTris < 0 || numTris > 500000) return false;
        if (numVerts == 0 || numTris == 0) {
            sPos = sEnd;
            continue;
        }
        if (ofsTris < 124 || ofsXyz < 124 || ofsBaseMap < 124) return false;
        if (sPos + static_cast<uint64_t>(ofsTris) + static_cast<uint64_t>(numTris) * 12ULL > sEnd) return false;
        if (sPos + static_cast<uint64_t>(ofsBaseMap) + static_cast<uint64_t>(numFrames) * 2ULL > sEnd) return false;

        const int16_t baseIdx = ByteIO::RdI16LE(data + sPos + static_cast<size_t>(ofsBaseMap));
        if (baseIdx < 0 || baseIdx >= numBaseFrames) return false;

        int16_t compIdx = -1;
        if (numCompFrames > 0 && ofsCompMap >= 124 &&
            sPos + static_cast<uint64_t>(ofsCompMap) + static_cast<uint64_t>(numFrames) * 2ULL <= sEnd) {
            compIdx = ByteIO::RdI16LE(data + sPos + static_cast<size_t>(ofsCompMap));
            if (compIdx >= numCompFrames || compIdx < -1) return false;
        }

        const uint64_t baseVOff = static_cast<uint64_t>(ofsXyz) +
                                  static_cast<uint64_t>(baseIdx) * static_cast<uint64_t>(numVerts) * 8ULL;
        if (sPos + baseVOff + static_cast<uint64_t>(numVerts) * 8ULL > sEnd) return false;

        uint64_t compVOff = 0;
        if (compIdx >= 0) {
            if (ofsCompVerts < 124) return false;
            compVOff = static_cast<uint64_t>(ofsCompVerts) +
                       static_cast<uint64_t>(compIdx) * static_cast<uint64_t>(numVerts) * 4ULL;
            if (sPos + compVOff + static_cast<uint64_t>(numVerts) * 4ULL > sEnd) return false;
        }

        const uint32_t outBaseV = static_cast<uint32_t>(mesh.vertices.size());
        const uint8_t* xyzPtr = data + sPos + static_cast<size_t>(baseVOff);
        const uint8_t* cmpPtr = (compIdx >= 0) ? (data + sPos + static_cast<size_t>(compVOff)) : nullptr;

        constexpr float kMd3Scale = 1.0f / 64.0f;
        constexpr float kMdcDistScale = 0.05f;
        for (int32_t v = 0; v < numVerts; ++v) {
            float px = static_cast<float>(ByteIO::RdI16LE(xyzPtr + static_cast<size_t>(v) * 8u + 0u)) * kMd3Scale;
            float py = static_cast<float>(ByteIO::RdI16LE(xyzPtr + static_cast<size_t>(v) * 8u + 2u)) * kMd3Scale;
            float pz = static_cast<float>(ByteIO::RdI16LE(xyzPtr + static_cast<size_t>(v) * 8u + 4u)) * kMd3Scale;
            if (cmpPtr) {
                const uint32_t cv = ByteIO::RdU32LE(cmpPtr + static_cast<size_t>(v) * 4u);
                px += (static_cast<float>(cv & 0xFFu) - 127.0f) * kMdcDistScale;
                py += (static_cast<float>((cv >> 8) & 0xFFu) - 127.0f) * kMdcDistScale;
                pz += (static_cast<float>((cv >> 16) & 0xFFu) - 127.0f) * kMdcDistScale;
            }
            mesh.vertices.push_back(UberToolsSkel::ZUpToViewport(px, py, pz));
        }

        const uint8_t* triPtr = data + sPos + static_cast<size_t>(ofsTris);
        for (int32_t t = 0; t < numTris; ++t) {
            const int32_t i0 = ByteIO::RdI32LE(triPtr + static_cast<size_t>(t) * 12u + 0u);
            const int32_t i1 = ByteIO::RdI32LE(triPtr + static_cast<size_t>(t) * 12u + 4u);
            const int32_t i2 = ByteIO::RdI32LE(triPtr + static_cast<size_t>(t) * 12u + 8u);
            if (i0 < 0 || i0 >= numVerts || i1 < 0 || i1 >= numVerts || i2 < 0 || i2 >= numVerts) return false;
            if (i0 == i1 || i1 == i2 || i0 == i2) continue;
            mesh.tris.push_back({
                outBaseV + static_cast<uint32_t>(i0),
                outBaseV + static_cast<uint32_t>(i2),
                outBaseV + static_cast<uint32_t>(i1),
                {}
            });
        }
        sPos = sEnd;
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"RTCW / WolfET Compressed Model (MDC v2)";
    mesh.animFrameCount = static_cast<size_t>(numFrames);
    return true;
}

} // namespace Formats
