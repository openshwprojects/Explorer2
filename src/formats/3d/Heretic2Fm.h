#pragma once
// Heretic2Fm.h — Raven Software Heretic II Sectioned Flexible Model (.fm) reader
// Parses 40-byte fmsectionheader_t chunks ("header", "tris", "frames" / "short frames"),
// reconstructing Frame 0 quantized vertices and indexed triangles.

namespace Formats {

inline bool ParseHeretic2Fm(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    // Must start with 40-byte section header "header" + >= 36-byte fmheader_t
    if (!data || size < 116) return false;
    if (std::memcmp(data, "header\0", 7) != 0) return false;

    auto readSecName = [](const uint8_t* p) -> std::string_view {
        size_t n = 0;
        while (n < 32u && p[n] != 0) ++n;
        return std::string_view(reinterpret_cast<const char*>(p), n);
    };

    int32_t numXyz = 0;
    int32_t numTris = 0;
    int32_t numFrames = 0;
    int32_t frameSize = 0;
    bool sawHeader = false;
    const uint8_t* trisChunk = nullptr;
    size_t trisChunkBytes = 0;
    const uint8_t* framesChunk = nullptr;
    size_t framesChunkBytes = 0;
    bool isShortFrames = false;

    size_t pos = 0;
    int chunkCount = 0;
    while (pos + 40u <= size && chunkCount < 64) {
        ++chunkCount;
        const std::string_view secName = readSecName(data + pos);
        const int32_t secVer  = ByteIO::RdI32LE(data + pos + 32u);
        const int32_t secSize = ByteIO::RdI32LE(data + pos + 36u);
        if (secName.empty() || secVer < 0 || secVer > 16 || secSize < 0) return false;
        if (pos + 40u + static_cast<size_t>(secSize) > size) return false;

        const uint8_t* payload = data + pos + 40u;
        const size_t payLen = static_cast<size_t>(secSize);

        if (secName == "header") {
            if (sawHeader || payLen < 36u || payLen > 64u) return false;
            frameSize = ByteIO::RdI32LE(payload + 8u);
            numXyz    = ByteIO::RdI32LE(payload + 16u);
            numTris   = ByteIO::RdI32LE(payload + 24u);
            numFrames = ByteIO::RdI32LE(payload + 32u);
            if (numXyz < 3 || numXyz > 131072 || numTris < 1 || numTris > 262144 || numFrames < 1 || numFrames > 32768) {
                return false;
            }
            sawHeader = true;
        } else if (secName == "tris") {
            trisChunk = payload;
            trisChunkBytes = payLen;
        } else if (secName == "frames" && !framesChunk) {
            framesChunk = payload;
            framesChunkBytes = payLen;
            isShortFrames = false;
        } else if (secName == "short frames") {
            framesChunk = payload;
            framesChunkBytes = payLen;
            isShortFrames = true;
        }

        pos += 40u + payLen;
    }

    if (!sawHeader || !trisChunk || !framesChunk) return false;
    if (trisChunkBytes < static_cast<size_t>(numTris) * 12u) return false;

    const size_t vertRecSize = isShortFrames ? 8u : 4u;
    const size_t minFrame0Bytes = 40u + static_cast<size_t>(numXyz) * vertRecSize;
    if (framesChunkBytes < minFrame0Bytes) return false;
    if (frameSize > 0 && !isShortFrames && static_cast<size_t>(frameSize) < minFrame0Bytes) return false;

    const float sx = ByteIO::RdF32LE(framesChunk + 0u);
    const float sy = ByteIO::RdF32LE(framesChunk + 4u);
    const float sz = ByteIO::RdF32LE(framesChunk + 8u);
    const float tx = ByteIO::RdF32LE(framesChunk + 12u);
    const float ty = ByteIO::RdF32LE(framesChunk + 16u);
    const float tz = ByteIO::RdF32LE(framesChunk + 20u);
    if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sz) ||
        !std::isfinite(tx) || !std::isfinite(ty) || !std::isfinite(tz)) {
        return false;
    }

    mesh.vertices.reserve(static_cast<size_t>(numXyz));
    const uint8_t* vp = framesChunk + 40u;
    for (int32_t v = 0; v < numXyz; ++v) {
        float vx = 0.0f, vy = 0.0f, vz = 0.0f;
        if (isShortFrames) {
            vx = static_cast<float>(ByteIO::RdU16LE(vp + static_cast<size_t>(v) * 8u + 0u)) * sx + tx;
            vy = static_cast<float>(ByteIO::RdU16LE(vp + static_cast<size_t>(v) * 8u + 2u)) * sy + ty;
            vz = static_cast<float>(ByteIO::RdU16LE(vp + static_cast<size_t>(v) * 8u + 4u)) * sz + tz;
        } else {
            vx = static_cast<float>(vp[static_cast<size_t>(v) * 4u + 0u]) * sx + tx;
            vy = static_cast<float>(vp[static_cast<size_t>(v) * 4u + 1u]) * sy + ty;
            vz = static_cast<float>(vp[static_cast<size_t>(v) * 4u + 2u]) * sz + tz;
        }
        mesh.vertices.push_back(UberToolsSkel::ZUpToViewport(vx, vy, vz));
    }

    for (int32_t t = 0; t < numTris; ++t) {
        const int16_t i0 = ByteIO::RdI16LE(trisChunk + static_cast<size_t>(t) * 12u + 0u);
        const int16_t i1 = ByteIO::RdI16LE(trisChunk + static_cast<size_t>(t) * 12u + 2u);
        const int16_t i2 = ByteIO::RdI16LE(trisChunk + static_cast<size_t>(t) * 12u + 4u);
        if (i0 < 0 || i0 >= numXyz || i1 < 0 || i1 >= numXyz || i2 < 0 || i2 >= numXyz) return false;
        if (i0 == i1 || i1 == i2 || i0 == i2) continue;
        mesh.tris.push_back({
            static_cast<uint32_t>(i0),
            static_cast<uint32_t>(i2),
            static_cast<uint32_t>(i1),
            {}
        });
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = isShortFrames
        ? L"Heretic II Flexible Model (FM 16-bit)"
        : L"Heretic II Flexible Model (FM)";
    mesh.animFrameCount = static_cast<size_t>(numFrames);
    return true;
}

} // namespace Formats
