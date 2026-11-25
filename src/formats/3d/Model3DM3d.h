#pragma once
// Model3DM3d.h — Universal Model 3D (.m3d, "3DMO" uncompressed & RFC1950 zlib-compressed) reader
// Parses HEAD (scale + bit-packed stride flags), VRTS (int8/int16/float32/float64 vertex table),
// and MESH (material switches + N-gon corner opcodes with optional UV/normal/tangent indices).

namespace Formats {

inline bool ParseModel3DM3d(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 24) return false;
    if (std::memcmp(data, "3DMO", 4) != 0) return false;

    const uint32_t outerSize = ByteIO::RdU32LE(data + 4u);
    if (outerSize < 24u || outerSize > size) return false;

    std::vector<uint8_t> inflated;
    const uint8_t* body = data + 8u;
    size_t bodyLen = static_cast<size_t>(outerSize) - 8u;

    if (std::memcmp(body, "HEAD", 4) != 0) {
        if (!ZipDeflate::InflateZlib(body, bodyLen, inflated, 64u * 1024u * 1024u) || inflated.size() < 16u) {
            return false;
        }
        if (std::memcmp(inflated.data(), "HEAD", 4) != 0) return false;
        body    = inflated.data();
        bodyLen = inflated.size();
    }

    auto optIdxBytes = [](uint32_t code2) -> size_t {
        return (code2 == 3u) ? 4u : static_cast<size_t>(code2);
    };
    auto readVarUint = [](const uint8_t* p, size_t nBytes) -> uint32_t {
        if (nBytes == 1u) return p[0];
        if (nBytes == 2u) return ByteIO::RdU16LE(p);
        if (nBytes == 4u) return ByteIO::RdU32LE(p);
        return 0u;
    };

    const uint32_t headLen = ByteIO::RdU32LE(body + 4u);
    if (headLen < 16u || headLen > bodyLen) return false;

    float scale = ByteIO::RdF32LE(body + 8u);
    if (!std::isfinite(scale) || scale < 0.0f || scale > 1e6f) return false;
    if (scale <= 1e-8f) scale = 1.0f;

    const uint32_t flags = ByteIO::RdU32LE(body + 12u);
    const uint32_t vcCode = (flags >> 0) & 3u;
    const uint32_t viCode = (flags >> 2) & 3u;
    if (viCode == 3u) return false;

    const size_t coordBytes  = static_cast<size_t>(1u) << vcCode; // 1 (i8), 2 (i16), 4 (f32), 8 (f64)
    const size_t vIdxBytes   = static_cast<size_t>(1u) << viCode; // 1 (u8), 2 (u16), 4 (u32)
    const size_t strIdxBytes = optIdxBytes((flags >> 4)  & 3u);
    const size_t colBytes    = optIdxBytes((flags >> 6)  & 3u);
    const size_t texIdxBytes = optIdxBytes((flags >> 8)  & 3u);
    const size_t skIdxBytes  = optIdxBytes((flags >> 14) & 3u);

    const size_t vertRecStride = 4u * coordBytes + colBytes + skIdxBytes;

    std::vector<Vec3f> vrtsTable;
    bool sawMesh = false;

    size_t pos = static_cast<size_t>(headLen);
    while (pos + 8u <= bodyLen) {
        if (std::memcmp(body + pos, "OMD3", 4) == 0) break; // end-of-model sentinel chunk
        const uint32_t cLen = ByteIO::RdU32LE(body + pos + 4u);
        if (cLen < 8u || pos + static_cast<size_t>(cLen) > bodyLen) return false;

        const uint8_t* cPay = body + pos + 8u;
        const size_t payLen = static_cast<size_t>(cLen) - 8u;

        if (std::memcmp(body + pos, "VRTS", 4) == 0) {
            if (payLen < vertRecStride || (payLen % vertRecStride) != 0u) return false;
            const size_t count = payLen / vertRecStride;
            if (count > 2000000u) return false;
            vrtsTable.reserve(vrtsTable.size() + count);
            for (size_t i = 0; i < count; ++i) {
                const uint8_t* vp = cPay + i * vertRecStride;
                float coords[3] = {};
                for (int k = 0; k < 3; ++k) {
                    const uint8_t* cp = vp + static_cast<size_t>(k) * coordBytes;
                    if (coordBytes == 1u) {
                        coords[k] = (static_cast<float>(static_cast<int8_t>(cp[0])) / 127.0f) * scale;
                    } else if (coordBytes == 2u) {
                        coords[k] = (static_cast<float>(ByteIO::RdI16LE(cp)) / 32767.0f) * scale;
                    } else if (coordBytes == 4u) {
                        coords[k] = ByteIO::RdF32LE(cp) * scale;
                    } else {
                        coords[k] = static_cast<float>(ByteIO::RdF64LE(cp)) * scale;
                    }
                    if (!std::isfinite(coords[k])) return false;
                }
                vrtsTable.push_back({ coords[0], coords[1], coords[2] });
            }
        } else if (std::memcmp(body + pos, "MESH", 4) == 0) {
            if (vrtsTable.empty()) return false;
            sawMesh = true;
            if (mesh.vertices.empty()) {
                mesh.vertices = vrtsTable;
            }
            size_t mp = 0;
            while (mp < payLen) {
                const uint8_t op = cPay[mp++];
                if (op == 0u) {
                    // Material switch command
                    if (mp + strIdxBytes > payLen) return false;
                    mp += strIdxBytes;
                    continue;
                }
                const uint32_t nCorners = static_cast<uint32_t>(op >> 4);
                if (nCorners < 3u || nCorners > 15u) return false;
                const bool hasUv  = (op & 0x01u) != 0u;
                const bool hasNrm = (op & 0x02u) != 0u;
                const bool hasTan = (op & 0x04u) != 0u;
                const size_t cornerStride = vIdxBytes +
                                            (hasUv  ? texIdxBytes : 0u) +
                                            (hasNrm ? vIdxBytes   : 0u) +
                                            (hasTan ? vIdxBytes   : 0u);
                if (mp + static_cast<size_t>(nCorners) * cornerStride > payLen) return false;

                uint32_t polyIdx[16] = {};
                for (uint32_t c = 0; c < nCorners; ++c) {
                    const uint32_t vi = readVarUint(cPay + mp, vIdxBytes);
                    if (vi >= vrtsTable.size()) return false;
                    polyIdx[c] = vi;
                    mp += cornerStride;
                }
                for (uint32_t c = 1u; c + 1u < nCorners; ++c) {
                    if (polyIdx[0] != polyIdx[c] && polyIdx[c] != polyIdx[c + 1] && polyIdx[0] != polyIdx[c + 1]) {
                        mesh.tris.push_back({ polyIdx[0], polyIdx[c], polyIdx[c + 1], {} });
                    }
                }
            }
        }

        pos += static_cast<size_t>(cLen);
    }

    if (!sawMesh || mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = inflated.empty()
        ? L"Model 3D Binary Mesh (M3D)"
        : L"Model 3D Compressed Mesh (M3D zlib)";
    return true;
}

} // namespace Formats
