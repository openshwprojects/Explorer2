#pragma once
// NetImmerseNif.h — NetImmerse / Gamebryo / Bethesda (.nif) block-table 3D mesh reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// ============================================================================
// #45. NetImmerse / Gamebryo / Bethesda Mesh (.nif — Full Block-Table Decoder)
// ============================================================================
inline bool ParseNetImmerseNif(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (size < 40 || (std::memcmp(data, "Gamebryo File Format", 20) != 0 &&
                     std::memcmp(data, "NetImmerse File Format", 22) != 0))
    {
        return false;
    }
    mesh.formatName = L"NetImmerse / Gamebryo Mesh (.nif)";

    size_t nl = 0;
    while (nl < size && nl < 160 && data[nl] != '\n') ++nl;
    if (nl + 9 > size) return false;
    size_t p = nl + 1;

    uint32_t ver = Detail3D200::RdU32LE(data + p); p += 4;
    if (ver < 0x04000000u || ver > 0x30000000u) return false;

    bool isBE = false;
    if (ver >= 0x14000004u) {
        if (p >= size) return false;
        isBE = (data[p++] == 0);
    }
    auto rdU16 = [&](const uint8_t* ptr) -> uint16_t { return isBE ? Detail3D200::RdU16BE(ptr) : Detail3D200::RdU16LE(ptr); };
    auto rdU32 = [&](const uint8_t* ptr) -> uint32_t { return isBE ? Detail3D200::RdU32BE(ptr) : Detail3D200::RdU32LE(ptr); };
    auto rdU64 = [&](const uint8_t* ptr) -> uint64_t { return isBE ? Detail3D200::RdU64BE(ptr) : Detail3D200::RdU64LE(ptr); };
    auto rdF32 = [&](const uint8_t* ptr) -> float    { return isBE ? Detail3D200::RdF32BE(ptr) : Detail3D200::RdF32LE(ptr); };

    uint32_t userVer = 0;
    if (ver >= 0x0A010000u) {
        if (p + 4 > size) return false;
        userVer = rdU32(data + p); p += 4;
    }
    if (p + 4 > size) return false;
    uint32_t numBlocks = rdU32(data + p); p += 4;
    if (numBlocks == 0 || numBlocks > 65536) return false;

    auto looksLikeBlockTypeTable = [&](size_t off) -> bool {
        if (off + 6 > size) return false;
        uint16_t nbt = rdU16(data + off);
        if (nbt == 0 || nbt > 4096) return false;
        uint32_t len0 = rdU32(data + off + 2);
        if (len0 == 0 || len0 > 128 || off + 6 + len0 > size) return false;
        for (uint32_t i = 0; i < len0; ++i) {
            uint8_t c = data[off + 6 + i];
            bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
            if (!ok) return false;
        }
        return true;
    };

    // Bethesda BSStreamHeader (Oblivion userVer=10/11, FO3/NV userVer=11 bsVer=34, Skyrim userVer=12 bsVer=83/100, FO4 userVer=12 bsVer=130)
    uint32_t bsVer = 0;
    bool hasBsHeader = (ver == 0x0A000102u) ||
                       ((ver == 0x14000005u || ver >= 0x14020007u) && (userVer >= 10 || (ver == 0x14000005u && userVer == 3)));
    if (hasBsHeader && !looksLikeBlockTypeTable(p)) {
        if (p + 4 > size) return false;
        bsVer = rdU32(data + p); p += 4;
        auto skipShortStr = [&](size_t& cur) -> bool {
            if (cur >= size) return false;
            uint8_t slen = data[cur++];
            if (cur + slen > size) return false;
            cur += slen;
            return true;
        };
        if (!skipShortStr(p)) return false; // author
        if (bsVer > 130) {
            if (p + 4 > size) return false;
            p += 4; // unknownInt1
        }
        if (bsVer < 132) {
            if (!skipShortStr(p)) return false; // processScript
        }
        if (!skipShortStr(p)) return false; // exportScript
        if (bsVer >= 103 && !looksLikeBlockTypeTable(p)) {
            if (!skipShortStr(p)) return false; // maxFilepath (Fallout 4 bsVer=130+)
        }
    }

    if (ver < 0x0A000100u) return false;
    if (p + 2 > size) return false;
    uint16_t numBlockTypes = rdU16(data + p); p += 2;
    if (numBlockTypes == 0 || numBlockTypes > 4096) return false;

    std::vector<std::string> blockTypes(numBlockTypes);
    for (uint16_t i = 0; i < numBlockTypes; ++i) {
        if (p + 4 > size) return false;
        uint32_t slen = rdU32(data + p); p += 4;
        if (slen > 256 || p + slen > size) return false;
        blockTypes[i].assign(reinterpret_cast<const char*>(data + p), slen);
        p += slen;
    }

    if (p + static_cast<size_t>(numBlocks) * 2 > size) return false;
    std::vector<uint16_t> blockTypeIdx(numBlocks);
    for (uint32_t b = 0; b < numBlocks; ++b) {
        blockTypeIdx[b] = static_cast<uint16_t>(rdU16(data + p) & 0x7FFFu);
        p += 2;
        if (blockTypeIdx[b] >= numBlockTypes) return false;
    }

    std::vector<uint32_t> blockSizes;
    if (ver >= 0x14020007u) {
        if (p + static_cast<size_t>(numBlocks) * 4 > size) return false;
        blockSizes.resize(numBlocks);
        for (uint32_t b = 0; b < numBlocks; ++b) {
            blockSizes[b] = rdU32(data + p);
            p += 4;
        }
    }

    if (ver >= 0x14010003u) {
        if (p + 8 > size) return false;
        uint32_t numStrings = rdU32(data + p); p += 4;
        p += 4; // maxStringLength
        if (numStrings > 200000) return false;
        for (uint32_t s = 0; s < numStrings; ++s) {
            if (p + 4 > size) return false;
            uint32_t slen = rdU32(data + p); p += 4;
            if (slen > 65536 || p + slen > size) return false;
            p += slen;
        }
    }

    if (ver >= 0x05000006u) {
        if (p + 4 > size) return false;
        uint32_t numGroups = rdU32(data + p); p += 4;
        if (numGroups > 65536 || p + static_cast<size_t>(numGroups) * 4 > size) return false;
        p += static_cast<size_t>(numGroups) * 4;
    }

    struct NifXform {
        float t[3] = { 0.0f, 0.0f, 0.0f };
        float r[9] = { 1.0f,0.0f,0.0f, 0.0f,1.0f,0.0f, 0.0f,0.0f,1.0f };
        float scale = 1.0f;
        bool valid = false;
    };
    auto applyNifXform = [](const NifXform& xf, float x, float y, float z) -> Vec3f {
        if (xf.valid) {
            float sx = x * xf.scale, sy = y * xf.scale, sz = z * xf.scale;
            float wx = xf.r[0] * sx + xf.r[1] * sy + xf.r[2] * sz + xf.t[0];
            float wy = xf.r[3] * sx + xf.r[4] * sy + xf.r[5] * sz + xf.t[1];
            float wz = xf.r[6] * sx + xf.r[7] * sy + xf.r[8] * sz + xf.t[2];
            x = wx; y = wy; z = wz;
        }
        // NIF Z-up -> Explorer2 Y-up
        return { x, z, -y };
    };

    // Parse NiAVObject header (returns cursor after NiAVObject, populates NifXform)
    auto parseNiAVObject = [&](size_t bStart, size_t bEnd, NifXform& outXf) -> size_t {
        size_t c = bStart;
        // NiObjectNET: name (4B string idx if >= 20.1.0.3, else SizedString), extraDataList, controller
        if (ver >= 0x14010003u) {
            if (c + 4 > bEnd) return 0;
            c += 4;
        } else {
            if (c + 4 > bEnd) return 0;
            uint32_t sl = rdU32(data + c); c += 4;
            if (sl > 4096 || c + sl > bEnd) return 0;
            c += sl;
        }
        if (c + 4 > bEnd) return 0;
        uint32_t nExtra = rdU32(data + c); c += 4;
        if (nExtra > 4096 || c + static_cast<size_t>(nExtra) * 4 + 4 > bEnd) return 0;
        c += static_cast<size_t>(nExtra) * 4 + 4; // extraData refs + controller ref

        // NiAVObject: flags (uint32 if userVer >= 12 && bsVer > 26, else uint16)
        size_t flagBytes = (userVer >= 12 && bsVer > 26) ? 4 : 2;
        if (c + flagBytes + 52 > bEnd) return 0;
        c += flagBytes;
        outXf.t[0] = rdF32(data + c + 0);
        outXf.t[1] = rdF32(data + c + 4);
        outXf.t[2] = rdF32(data + c + 8);
        c += 12;
        for (int i = 0; i < 9; ++i) outXf.r[i] = rdF32(data + c + i * 4);
        c += 36;
        outXf.scale = rdF32(data + c);
        if (!std::isfinite(outXf.scale) || std::fabs(outXf.scale) < 1e-6f) outXf.scale = 1.0f;
        c += 4;
        outXf.valid = true;

        // Properties array (absent when bsVer > 34)
        if (bsVer <= 34) {
            if (c + 4 > bEnd) return 0;
            uint32_t nProps = rdU32(data + c); c += 4;
            if (nProps > 4096 || c + static_cast<size_t>(nProps) * 4 > bEnd) return 0;
            c += static_cast<size_t>(nProps) * 4;
        }
        // CollisionObject ref (ver >= 10.0.1.0)
        if (ver >= 0x0A000100u) {
            if (c + 4 > bEnd) return 0;
            c += 4;
        }
        return c;
    };

    // Build exact block offset table if blockSizes present
    std::vector<size_t> blockOffsets(numBlocks, 0);
    std::vector<size_t> blockEnds(numBlocks, 0);
    if (!blockSizes.empty()) {
        size_t cur = p;
        for (uint32_t b = 0; b < numBlocks; ++b) {
            if (cur > size || blockSizes[b] > size - cur) return false;
            blockOffsets[b] = cur;
            blockEnds[b]    = cur + blockSizes[b];
            cur += blockSizes[b];
        }
    } else {
        // Pre-20.2.0.7 NIF without blockSizes: only single-block or sequential NiGeometryData supported if at p
        blockOffsets[0] = p;
        blockEnds[0]    = size;
    }

    // First pass (when blockSizes present): map NiTriShape / NiTriStrips dataRef -> NifXform
    std::unordered_map<uint32_t, NifXform> geomDataXform;
    if (!blockSizes.empty()) {
        for (uint32_t b = 0; b < numBlocks; ++b) {
            const std::string& tn = blockTypes[blockTypeIdx[b]];
            if (tn == "NiTriShape" || tn == "NiTriStrips" || tn == "BSLODTriShape") {
                NifXform xf;
                size_t afterAv = parseNiAVObject(blockOffsets[b], blockEnds[b], xf);
                if (afterAv > 0 && afterAv + 4 <= blockEnds[b]) {
                    int32_t dataRef = static_cast<int32_t>(rdU32(data + afterAv));
                    if (dataRef >= 0 && static_cast<uint32_t>(dataRef) < numBlocks) {
                        geomDataXform[static_cast<uint32_t>(dataRef)] = xf;
                    }
                }
            }
        }
    }

    // Second pass: decode NiTriShapeData, NiTriStripsData, BSTriShape, BSDynamicTriShape, BSSubIndexTriShape
    uint32_t blocksToInspect = blockSizes.empty() ? 1u : numBlocks;
    for (uint32_t b = 0; b < blocksToInspect; ++b) {
        const std::string& tn = blockTypes[blockTypeIdx[b]];
        size_t bStart = blockOffsets[b];
        size_t bEnd   = blockEnds[b];
        if (bStart >= bEnd || bEnd > size) continue;

        if (tn == "NiTriShapeData" || tn == "NiTriStripsData") {
            size_t c = bStart;
            if (ver >= 0x0A020000u) {
                if (c + 4 > bEnd) continue;
                c += 4; // groupId
            }
            if (c + 2 > bEnd) continue;
            uint16_t nVerts = rdU16(data + c); c += 2;
            if (nVerts < 3) continue;
            if (ver >= 0x0A010000u) {
                if (c + 2 > bEnd) continue;
                c += 2; // keepFlags + compressFlags
            }
            if (c + 1 > bEnd) continue;
            uint8_t hasVerts = data[c++];
            if (hasVerts == 0) continue;
            if (c + static_cast<size_t>(nVerts) * 12 > bEnd) continue;
            size_t vertsOff = c;
            c += static_cast<size_t>(nVerts) * 12;

            uint16_t dataFlags = 0;
            if (ver >= 0x0A000100u) {
                if (c + 2 > bEnd) continue;
                dataFlags = rdU16(data + c); c += 2;
            }
            if (ver == 0x14020007u && bsVer > 34) {
                if (c + 4 > bEnd) continue;
                c += 4; // Skyrim materialCRC
            }
            if (c + 1 > bEnd) continue;
            uint8_t hasNormals = data[c++];
            if (hasNormals != 0) {
                size_t normBytes = static_cast<size_t>(nVerts) * 12;
                bool hasTanBitan = (bsVer > 0) ? ((dataFlags & 0x1000u) != 0) : ((dataFlags & 0xF000u) != 0);
                if (ver >= 0x0A010000u && hasTanBitan) normBytes *= 3;
                if (c + normBytes > bEnd) continue;
                c += normBytes;
            }
            if (c + 16 > bEnd) continue;
            c += 16; // center (12B) + radius (4B)

            if (c + 1 > bEnd) continue;
            uint8_t hasColors = data[c++];
            if (hasColors != 0) {
                if (c + static_cast<size_t>(nVerts) * 16 > bEnd) continue;
                c += static_cast<size_t>(nVerts) * 16;
            }

            uint32_t numUvSets = (bsVer > 34) ? ((dataFlags & 0x0001u) ? 1u : 0u) : static_cast<uint32_t>(dataFlags & 0x003Fu);
            if (c + static_cast<size_t>(numUvSets) * nVerts * 8 > bEnd) continue;
            c += static_cast<size_t>(numUvSets) * nVerts * 8;

            if (ver >= 0x0A000100u) {
                if (c + 2 > bEnd) continue;
                c += 2; // consistencyFlags
            }
            if (ver >= 0x14000004u) {
                if (c + 4 > bEnd) continue;
                c += 4; // additionalData ref
            }

            NifXform xf;
            auto xIt = geomDataXform.find(b);
            if (xIt != geomDataXform.end()) xf = xIt->second;

            if (tn == "NiTriShapeData") {
                if (c + 7 > bEnd) continue;
                uint16_t nTris = rdU16(data + c);
                uint32_t nTriPts = rdU32(data + c + 2);
                uint8_t hasTris = data[c + 6];
                if (nTris == 0 || hasTris == 0 || nTriPts != static_cast<uint32_t>(nTris) * 3u) continue;
                size_t triOff = c + 7;
                if (triOff + static_cast<size_t>(nTris) * 6 > bEnd) continue;

                uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                for (uint16_t v = 0; v < nVerts; ++v) {
                    const uint8_t* vp = data + vertsOff + static_cast<size_t>(v) * 12;
                    mesh.vertices.push_back(applyNifXform(xf, rdF32(vp + 0), rdF32(vp + 4), rdF32(vp + 8)));
                }
                for (uint16_t t = 0; t < nTris; ++t) {
                    const uint8_t* tp = data + triOff + static_cast<size_t>(t) * 6;
                    uint16_t i0 = rdU16(tp + 0), i1 = rdU16(tp + 2), i2 = rdU16(tp + 4);
                    if (i0 < nVerts && i1 < nVerts && i2 < nVerts && i0 != i1 && i1 != i2 && i0 != i2) {
                        mesh.tris.push_back({ vBase + i0, vBase + i1, vBase + i2, {} });
                    }
                }
            } else { // NiTriStripsData
                if (c + 4 > bEnd) continue;
                uint16_t nTris = rdU16(data + c); (void)nTris;
                uint16_t nStrips = rdU16(data + c + 2);
                c += 4;
                if (nStrips == 0 || nStrips > 16384 || c + static_cast<size_t>(nStrips) * 2 > bEnd) continue;
                std::vector<uint16_t> stripLens(nStrips);
                size_t totalPts = 0;
                for (uint16_t s = 0; s < nStrips; ++s) {
                    stripLens[s] = rdU16(data + c + static_cast<size_t>(s) * 2);
                    totalPts += stripLens[s];
                }
                c += static_cast<size_t>(nStrips) * 2;
                if (ver >= 0x0A000103u) {
                    if (c + 1 > bEnd) continue;
                    if (data[c++] == 0) continue; // hasPoints == 0
                }
                if (c + totalPts * 2 > bEnd) continue;

                uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                for (uint16_t v = 0; v < nVerts; ++v) {
                    const uint8_t* vp = data + vertsOff + static_cast<size_t>(v) * 12;
                    mesh.vertices.push_back(applyNifXform(xf, rdF32(vp + 0), rdF32(vp + 4), rdF32(vp + 8)));
                }
                for (uint16_t s = 0; s < nStrips; ++s) {
                    uint16_t slen = stripLens[s];
                    for (uint16_t k = 0; k + 2 < slen; ++k) {
                        uint16_t a = rdU16(data + c + static_cast<size_t>(k) * 2);
                        uint16_t bIdx = rdU16(data + c + static_cast<size_t>(k + 1) * 2);
                        uint16_t d = rdU16(data + c + static_cast<size_t>(k + 2) * 2);
                        if (a < nVerts && bIdx < nVerts && d < nVerts && a != bIdx && bIdx != d && a != d) {
                            if (k & 1u) mesh.tris.push_back({ vBase + bIdx, vBase + a, vBase + d, {} });
                            else        mesh.tris.push_back({ vBase + a, vBase + bIdx, vBase + d, {} });
                        }
                    }
                    c += static_cast<size_t>(slen) * 2;
                }
            }
        } else if ((tn == "BSTriShape" || tn == "BSDynamicTriShape" || tn == "BSSubIndexTriShape") && !blockSizes.empty()) {
            NifXform xf;
            size_t c = parseNiAVObject(bStart, bEnd, xf);
            if (c == 0) continue;
            size_t boundBytes = (bsVer == 155) ? 40 : 16; // boundingSphere (16B) [+ boundMinMax 24B in bsVer 155]
            if (c + boundBytes + 12 + 8 > bEnd) continue;
            c += boundBytes + 12; // boundingSphere + skin(4) + shaderProperty(4) + alphaProperty(4)

            uint64_t vertexDesc = rdU64(data + c); c += 8;
            size_t stride = static_cast<size_t>(vertexDesc & 0xFFu) * 4;
            bool fullPrec = ((vertexDesc >> 50) & 1u) != 0 || bsVer == 100 && stride >= 16 && ((vertexDesc >> 48) & 0x04u) != 0;

            uint32_t nTris = 0;
            if (bsVer >= 130) {
                if (c + 10 > bEnd) continue;
                nTris = rdU32(data + c); c += 4;
            } else {
                if (c + 8 > bEnd) continue;
                nTris = rdU16(data + c); c += 2;
            }
            uint16_t nVerts = rdU16(data + c); c += 2;
            uint32_t dataSize = rdU32(data + c); c += 4;
            if (nVerts < 3 || nTris == 0 || dataSize == 0 || stride < 8) continue;

            size_t vDataOff = c;
            size_t vBytes = static_cast<size_t>(nVerts) * stride;
            size_t tBytes = static_cast<size_t>(nTris) * 6;
            if (vDataOff + vBytes + tBytes > bEnd) continue;

            uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
            size_t dynOff = vDataOff + vBytes + tBytes;
            bool usedDyn = false;
            if (tn == "BSDynamicTriShape" && dynOff + 4 <= bEnd) {
                uint32_t dynSize = rdU32(data + dynOff);
                if (dynSize == static_cast<uint32_t>(nVerts) * 16u && dynOff + 4 + dynSize <= bEnd) {
                    for (uint16_t v = 0; v < nVerts; ++v) {
                        const uint8_t* dp = data + dynOff + 4 + static_cast<size_t>(v) * 16;
                        mesh.vertices.push_back(applyNifXform(xf, rdF32(dp + 0), rdF32(dp + 4), rdF32(dp + 8)));
                    }
                    usedDyn = true;
                }
            }
            if (!usedDyn) {
                for (uint16_t v = 0; v < nVerts; ++v) {
                    const uint8_t* vp = data + vDataOff + static_cast<size_t>(v) * stride;
                    float x = 0.0f, y = 0.0f, z = 0.0f;
                    if (fullPrec && stride >= 12) {
                        x = rdF32(vp + 0); y = rdF32(vp + 4); z = rdF32(vp + 8);
                    } else {
                        x = Detail3D200::HalfToFloat(rdU16(vp + 0));
                        y = Detail3D200::HalfToFloat(rdU16(vp + 2));
                        z = Detail3D200::HalfToFloat(rdU16(vp + 4));
                    }
                    mesh.vertices.push_back(applyNifXform(xf, x, y, z));
                }
            }
            size_t triOff = vDataOff + vBytes;
            for (uint32_t t = 0; t < nTris; ++t) {
                const uint8_t* tp = data + triOff + static_cast<size_t>(t) * 6;
                uint16_t i0 = rdU16(tp + 0), i1 = rdU16(tp + 2), i2 = rdU16(tp + 4);
                if (i0 < nVerts && i1 < nVerts && i2 < nVerts && i0 != i1 && i1 != i2 && i0 != i2) {
                    mesh.tris.push_back({ vBase + i0, vBase + i1, vBase + i2, {} });
                }
            }
        }
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
