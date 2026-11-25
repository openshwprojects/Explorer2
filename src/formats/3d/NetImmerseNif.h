#pragma once
// NetImmerseNif.h — NetImmerse / Gamebryo / Bethesda (.nif) block-table & hierarchical 3D mesh reader.
// Supports Morrowind v4.0.0.2, pre-20.2.0.7 sequential multi-block NIFs (v10.x – Oblivion v20.0.0.5),
// and modern block-sized Gamebryo / Creation Engine NIFs (v20.2.0.7 – Fallout 4 / Skyrim SE),
// with hierarchical parent NiNode transform composition.
#include "../common/MeshFormatCommon.h"

namespace Formats {

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

    uint32_t ver = DetailMeshSciCad::RdU32LE(data + p); p += 4;
    if (ver < 0x04000000u || ver > 0x30000000u) return false;

    bool isBE = false;
    if (ver >= 0x14000004u) {
        if (p >= size) return false;
        isBE = (data[p++] == 0);
    }
    auto rdU16 = [&](const uint8_t* ptr) -> uint16_t { return isBE ? DetailMeshSciCad::RdU16BE(ptr) : DetailMeshSciCad::RdU16LE(ptr); };
    auto rdU32 = [&](const uint8_t* ptr) -> uint32_t { return isBE ? DetailMeshSciCad::RdU32BE(ptr) : DetailMeshSciCad::RdU32LE(ptr); };
    auto rdU64 = [&](const uint8_t* ptr) -> uint64_t { return isBE ? DetailMeshSciCad::RdU64BE(ptr) : DetailMeshSciCad::RdU64LE(ptr); };
    auto rdF32 = [&](const uint8_t* ptr) -> float    { return isBE ? DetailMeshSciCad::RdF32BE(ptr) : DetailMeshSciCad::RdF32LE(ptr); };

    const size_t boolBytes = (ver <= 0x04000002u) ? 4u : 1u;
    auto rdBool = [&](size_t& cur, size_t lim, bool& outVal) -> bool {
        if (cur + boolBytes > lim) return false;
        outVal = (boolBytes == 4u) ? (rdU32(data + cur) != 0u) : (data[cur] != 0u);
        cur += boolBytes;
        return true;
    };

    uint32_t userVer = 0;
    if (ver >= 0x0A010000u) {
        if (p + 4 > size) return false;
        userVer = rdU32(data + p); p += 4;
    }
    if (p + 4 > size) return false;
    uint32_t numBlocks = rdU32(data + p); p += 4;
    if (numBlocks == 0 || numBlocks > 65536) return false;

    auto isNifIdent = [&](const uint8_t* s, uint32_t len) -> bool {
        if (len < 3 || len > 96) return false;
        uint8_t c0 = s[0];
        if (!((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z'))) return false;
        for (uint32_t i = 0; i < len; ++i) {
            uint8_t c = s[i];
            bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
            if (!ok) return false;
        }
        return true;
    };

    auto looksLikeBlockTypeTable = [&](size_t off) -> bool {
        if (off + 6 > size) return false;
        uint16_t nbt = rdU16(data + off);
        if (nbt == 0 || nbt > 4096) return false;
        uint32_t len0 = rdU32(data + off + 2);
        if (len0 == 0 || len0 > 128 || off + 6 + len0 > size) return false;
        return isNifIdent(data + off + 6, len0);
    };

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
        if (!skipShortStr(p)) return false;
        if (bsVer > 130) {
            if (p + 4 > size) return false;
            p += 4;
        }
        if (bsVer < 132) {
            if (!skipShortStr(p)) return false;
        }
        if (!skipShortStr(p)) return false;
        if (bsVer >= 103 && !looksLikeBlockTypeTable(p)) {
            if (!skipShortStr(p)) return false;
        }
    }

    const bool hasHeaderBlockTypes = (ver >= 0x0A000100u);
    std::vector<std::string> blockTypes;
    std::vector<uint16_t> blockTypeIdx;
    std::vector<uint32_t> blockSizes;

    if (hasHeaderBlockTypes) {
        if (p + 2 > size) return false;
        uint16_t numBlockTypes = rdU16(data + p); p += 2;
        if (numBlockTypes == 0 || numBlockTypes > 4096) return false;

        blockTypes.resize(numBlockTypes);
        for (uint16_t i = 0; i < numBlockTypes; ++i) {
            if (p + 4 > size) return false;
            uint32_t slen = rdU32(data + p); p += 4;
            if (slen > 256 || p + slen > size) return false;
            blockTypes[i].assign(reinterpret_cast<const char*>(data + p), slen);
            p += slen;
        }

        if (p + static_cast<size_t>(numBlocks) * 2 > size) return false;
        blockTypeIdx.resize(numBlocks);
        for (uint32_t b = 0; b < numBlocks; ++b) {
            blockTypeIdx[b] = static_cast<uint16_t>(rdU16(data + p) & 0x7FFFu);
            p += 2;
            if (blockTypeIdx[b] >= numBlockTypes) return false;
        }

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
            p += 4;
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
    } else if (ver >= 0x05000006u) {
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

    auto composeNifXform = [](const NifXform& par, const NifXform& loc) -> NifXform {
        if (!par.valid) return loc;
        if (!loc.valid) return par;
        NifXform out;
        out.valid = true;
        out.scale = par.scale * loc.scale;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                out.r[row * 3 + col] =
                    par.r[row * 3 + 0] * loc.r[0 * 3 + col] +
                    par.r[row * 3 + 1] * loc.r[1 * 3 + col] +
                    par.r[row * 3 + 2] * loc.r[2 * 3 + col];
            }
        }
        float lx = loc.t[0] * par.scale, ly = loc.t[1] * par.scale, lz = loc.t[2] * par.scale;
        out.t[0] = par.r[0] * lx + par.r[1] * ly + par.r[2] * lz + par.t[0];
        out.t[1] = par.r[3] * lx + par.r[4] * ly + par.r[5] * lz + par.t[1];
        out.t[2] = par.r[6] * lx + par.r[7] * ly + par.r[8] * lz + par.t[2];
        return out;
    };

    auto applyNifXform = [](const NifXform& xf, float x, float y, float z) -> Vec3f {
        if (xf.valid) {
            float sx = x * xf.scale, sy = y * xf.scale, sz = z * xf.scale;
            float wx = xf.r[0] * sx + xf.r[1] * sy + xf.r[2] * sz + xf.t[0];
            float wy = xf.r[3] * sx + xf.r[4] * sy + xf.r[5] * sz + xf.t[1];
            float wz = xf.r[6] * sx + xf.r[7] * sy + xf.r[8] * sz + xf.t[2];
            x = wx; y = wy; z = wz;
        }
        return { x, z, -y };
    };

    auto skipNifString = [&](size_t& c, size_t lim) -> bool {
        if (ver >= 0x14010003u) {
            if (c + 4 > lim) return false;
            c += 4;
            return true;
        }
        if (c + 4 > lim) return false;
        uint32_t sl = rdU32(data + c); c += 4;
        if (sl > 16384 || c + sl > lim) return false;
        c += sl;
        return true;
    };

    auto parseNiObjectNET = [&](size_t bStart, size_t bEnd) -> size_t {
        size_t c = bStart;
        if (!skipNifString(c, bEnd)) return 0;
        if (ver < 0x0A000100u) {
            if (c + 8 > bEnd) return 0;
            c += 8; // int32 extraDataRef + int32 controllerRef
        } else {
            if (c + 4 > bEnd) return 0;
            uint32_t nExtra = rdU32(data + c); c += 4;
            if (nExtra > 4096 || c + static_cast<size_t>(nExtra) * 4 + 4 > bEnd) return 0;
            c += static_cast<size_t>(nExtra) * 4 + 4;
        }
        return c;
    };

    auto parseNiAVObject = [&](size_t bStart, size_t bEnd, NifXform& outXf) -> size_t {
        size_t c = parseNiObjectNET(bStart, bEnd);
        if (c == 0) return 0;

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

        if (ver <= 0x04020200u) {
            if (c + 12 > bEnd) return 0;
            c += 12; // Vector3 velocity in NetImmerse <= 4.2.2.0
        }
        if (bsVer <= 34) {
            if (c + 4 > bEnd) return 0;
            uint32_t nProps = rdU32(data + c); c += 4;
            if (nProps > 4096 || c + static_cast<size_t>(nProps) * 4 > bEnd) return 0;
            c += static_cast<size_t>(nProps) * 4;
        }
        if (ver <= 0x04020200u) {
            bool hasBBox = false;
            if (!rdBool(c, bEnd, hasBBox)) return 0;
            if (hasBBox) {
                if (c + 64 > bEnd) return 0;
                c += 64; // BoundingBox (uint32 + vec3 + mat33 + vec3)
            }
        }
        if (ver >= 0x0A000100u) {
            if (c + 4 > bEnd) return 0;
            c += 4; // collisionObject ref
        }
        return c;
    };

    struct ParsedGeomData {
        std::vector<Vec3f> rawVerts;
        std::vector<std::array<uint16_t, 3>> tris;
    };

    auto parseGeomDataBlock = [&](const std::string& tn, size_t bStart, size_t bEnd, ParsedGeomData& outGd) -> size_t {
        size_t c = bStart;
        if (ver >= 0x0A020000u) {
            if (c + 4 > bEnd) return 0;
            c += 4; // groupId
        }
        if (c + 2 > bEnd) return 0;
        uint16_t nVerts = rdU16(data + c); c += 2;
        if (ver >= 0x0A010000u) {
            if (c + 2 > bEnd) return 0;
            c += 2; // keepFlags + compressFlags
        }
        bool hasVerts = false;
        if (!rdBool(c, bEnd, hasVerts)) return 0;
        size_t vertsOff = c;
        if (hasVerts) {
            if (c + static_cast<size_t>(nVerts) * 12 > bEnd) return 0;
            c += static_cast<size_t>(nVerts) * 12;
        }

        uint16_t dataFlags = 0;
        if (ver >= 0x0A000100u) {
            if (c + 2 > bEnd) return 0;
            dataFlags = rdU16(data + c); c += 2;
        }
        if (ver == 0x14020007u && bsVer > 34) {
            if (c + 4 > bEnd) return 0;
            c += 4; // materialCRC
        }
        bool hasNormals = false;
        if (!rdBool(c, bEnd, hasNormals)) return 0;
        if (hasNormals) {
            size_t normBytes = static_cast<size_t>(nVerts) * 12;
            bool hasTanBitan = (bsVer > 0) ? ((dataFlags & 0x1000u) != 0) : ((dataFlags & 0xF000u) != 0);
            if (ver >= 0x0A010000u && hasTanBitan) normBytes *= 3;
            if (c + normBytes > bEnd) return 0;
            c += normBytes;
        }
        if (c + 16 > bEnd) return 0;
        c += 16; // center + radius

        bool hasColors = false;
        if (!rdBool(c, bEnd, hasColors)) return 0;
        if (hasColors) {
            if (c + static_cast<size_t>(nVerts) * 16 > bEnd) return 0;
            c += static_cast<size_t>(nVerts) * 16;
        }

        uint32_t numUvSets = 0;
        bool hasUvs = true;
        if (ver < 0x0A000100u) {
            if (c + 2 > bEnd) return 0;
            uint16_t rawUvs = rdU16(data + c); c += 2;
            numUvSets = static_cast<uint32_t>(rawUvs & 0x003Fu);
            if (ver <= 0x04000002u) {
                if (!rdBool(c, bEnd, hasUvs)) return 0;
            }
        } else {
            numUvSets = (bsVer > 34) ? ((dataFlags & 0x0001u) ? 1u : 0u) : static_cast<uint32_t>(dataFlags & 0x003Fu);
        }
        if (hasUvs && numUvSets > 0) {
            if (c + static_cast<size_t>(numUvSets) * nVerts * 8 > bEnd) return 0;
            c += static_cast<size_t>(numUvSets) * nVerts * 8;
        }

        if (ver >= 0x0A000100u) {
            if (c + 2 > bEnd) return 0;
            c += 2; // consistencyFlags
        }
        if (ver >= 0x14000004u) {
            if (c + 4 > bEnd) return 0;
            c += 4; // additionalData ref
        }

        if (hasVerts && nVerts >= 3) {
            outGd.rawVerts.resize(nVerts);
            for (uint16_t v = 0; v < nVerts; ++v) {
                const uint8_t* vp = data + vertsOff + static_cast<size_t>(v) * 12;
                outGd.rawVerts[v] = { rdF32(vp + 0), rdF32(vp + 4), rdF32(vp + 8) };
            }
        }

        if (tn == "NiTriShapeData") {
            if (c + 6 > bEnd) return 0;
            uint16_t nTris = rdU16(data + c);
            uint32_t nTriPts = rdU32(data + c + 2);
            c += 6;
            bool hasTris = true;
            if (ver >= 0x0A000102u) {
                if (!rdBool(c, bEnd, hasTris)) return 0;
            }
            if (hasTris && nTris > 0) {
                if (nTriPts != static_cast<uint32_t>(nTris) * 3u || c + static_cast<size_t>(nTris) * 6 > bEnd) return 0;
                if (!outGd.rawVerts.empty()) {
                    outGd.tris.reserve(nTris);
                    for (uint16_t t = 0; t < nTris; ++t) {
                        const uint8_t* tp = data + c + static_cast<size_t>(t) * 6;
                        uint16_t i0 = rdU16(tp + 0), i1 = rdU16(tp + 2), i2 = rdU16(tp + 4);
                        if (i0 < nVerts && i1 < nVerts && i2 < nVerts && i0 != i1 && i1 != i2 && i0 != i2) {
                            outGd.tris.push_back({ i0, i1, i2 });
                        }
                    }
                }
                c += static_cast<size_t>(nTris) * 6;
            }
            if (c + 2 <= bEnd) {
                uint16_t nMatchGroups = rdU16(data + c); c += 2;
                for (uint16_t m = 0; m < nMatchGroups; ++m) {
                    if (c + 2 > bEnd) return 0;
                    uint16_t cnt = rdU16(data + c); c += 2;
                    if (c + static_cast<size_t>(cnt) * 2 > bEnd) return 0;
                    c += static_cast<size_t>(cnt) * 2;
                }
            }
            return c;
        } else if (tn == "NiTriStripsData") {
            if (c + 4 > bEnd) return 0;
            uint16_t nStrips = rdU16(data + c + 2);
            c += 4;
            if (nStrips > 16384 || c + static_cast<size_t>(nStrips) * 2 > bEnd) return 0;
            std::vector<uint16_t> stripLens(nStrips);
            size_t totalPts = 0;
            for (uint16_t s = 0; s < nStrips; ++s) {
                stripLens[s] = rdU16(data + c + static_cast<size_t>(s) * 2);
                totalPts += stripLens[s];
            }
            c += static_cast<size_t>(nStrips) * 2;
            bool hasPts = true;
            if (ver >= 0x0A000103u) {
                if (!rdBool(c, bEnd, hasPts)) return 0;
            }
            if (hasPts) {
                if (c + totalPts * 2 > bEnd) return 0;
                if (!outGd.rawVerts.empty()) {
                    for (uint16_t s = 0; s < nStrips; ++s) {
                        uint16_t slen = stripLens[s];
                        for (uint16_t k = 0; k + 2 < slen; ++k) {
                            uint16_t a = rdU16(data + c + static_cast<size_t>(k) * 2);
                            uint16_t bIdx = rdU16(data + c + static_cast<size_t>(k + 1) * 2);
                            uint16_t d = rdU16(data + c + static_cast<size_t>(k + 2) * 2);
                            if (a < nVerts && bIdx < nVerts && d < nVerts && a != bIdx && bIdx != d && a != d) {
                                if (k & 1u) outGd.tris.push_back({ bIdx, a, d });
                                else        outGd.tris.push_back({ a, bIdx, d });
                            }
                        }
                        c += static_cast<size_t>(slen) * 2;
                    }
                } else {
                    c += totalPts * 2;
                }
            }
            return c;
        }
        return 0;
    };

    auto isNodeTypeName = [](const std::string& tn) -> bool {
        return tn == "NiNode" || tn == "BSFadeNode" || tn == "BSLeafAnimNode" || tn == "BSTreeNode" ||
               tn == "NiBillboardNode" || tn == "NiSwitchNode" || tn == "NiLODNode" ||
               tn == "RootCollisionNode" || tn == "AvoidNode" || tn == "NiBSAnimationNode" ||
               tn == "NiBSParticleNode" || tn == "BSMultiBoundNode";
    };

    // Sequential block walker for pre-20.2.0.7 & Morrowind v4.0.0.2 NIFs where blockSizes is absent
    auto advanceSequentialBlock = [&](const std::string& tn, size_t bStart, size_t lim) -> size_t {
        if (isNodeTypeName(tn)) {
            NifXform dummy;
            size_t c = parseNiAVObject(bStart, lim, dummy);
            if (c == 0 || c + 4 > lim) return 0;
            uint32_t nChild = rdU32(data + c); c += 4;
            if (nChild > 16384 || c + static_cast<size_t>(nChild) * 4 > lim) return 0;
            c += static_cast<size_t>(nChild) * 4;
            if (bsVer < 130) {
                if (c + 4 > lim) return 0;
                uint32_t nEff = rdU32(data + c); c += 4;
                if (nEff > 4096 || c + static_cast<size_t>(nEff) * 4 > lim) return 0;
                c += static_cast<size_t>(nEff) * 4;
            }
            return c;
        }
        if (tn == "NiTriShape" || tn == "NiTriStrips") {
            NifXform dummy;
            size_t c = parseNiAVObject(bStart, lim, dummy);
            if (c == 0 || c + 8 > lim) return 0;
            c += 8; // dataRef + skinInstanceRef
            if (ver >= 0x0A000100u && ver < 0x14020007u) {
                bool hasShader = false;
                if (!rdBool(c, lim, hasShader)) return 0;
                if (hasShader) {
                    if (!skipNifString(c, lim) || c + 4 > lim) return 0;
                    c += 4;
                }
            }
            return c;
        }
        if (tn == "NiTriShapeData" || tn == "NiTriStripsData") {
            ParsedGeomData dummyGd;
            return parseGeomDataBlock(tn, bStart, lim, dummyGd);
        }
        if (tn == "NiMaterialProperty") {
            size_t c = parseNiObjectNET(bStart, lim);
            if (c == 0) return 0;
            if (ver <= 0x0A000102u) { if (c + 2 > lim) return 0; c += 2; }
            size_t colBytes = (ver == 0x14020007u && userVer >= 11 && bsVer > 21) ? 32u : 56u;
            if (c + colBytes > lim) return 0;
            return c + colBytes;
        }
        if (tn == "NiAlphaProperty") {
            size_t c = parseNiObjectNET(bStart, lim);
            return (c == 0 || c + 3 > lim) ? 0 : (c + 3);
        }
        if (tn == "NiVertexColorProperty" || tn == "NiSpecularProperty" || tn == "NiWireframeProperty" ||
            tn == "NiShadeProperty" || tn == "NiDitherProperty")
        {
            size_t c = parseNiObjectNET(bStart, lim);
            return (c == 0 || c + 2 > lim) ? 0 : (c + 2);
        }
        if (tn == "NiZBufferProperty") {
            size_t c = parseNiObjectNET(bStart, lim);
            if (c == 0 || c + 2 > lim) return 0;
            c += 2;
            if (ver >= 0x0401000Cu && ver <= 0x14000005u) {
                if (c + 4 > lim) return 0;
                c += 4;
            }
            return c;
        }
        if (tn == "NiStencilProperty") {
            size_t c = parseNiObjectNET(bStart, lim);
            if (c == 0) return 0;
            if (ver <= 0x0A000102u) { if (c + 2 > lim) return 0; c += 2; }
            if (ver <= 0x14000005u) {
                bool stEn = false;
                if (!rdBool(c, lim, stEn) || c + 28 > lim) return 0;
                return c + 28;
            }
            return (c + 10 <= lim) ? (c + 10) : 0;
        }
        if (tn == "NiTexturingProperty") {
            size_t c = parseNiObjectNET(bStart, lim);
            if (c == 0) return 0;
            if (ver <= 0x0A000102u || ver >= 0x14010002u) { if (c + 2 > lim) return 0; c += 2; }
            if (ver <= 0x14000005u) { if (c + 4 > lim) return 0; c += 4; }
            if (c + 4 > lim) return 0;
            uint32_t texCnt = rdU32(data + c); c += 4;
            if (texCnt > 32) return 0;
            for (uint32_t t = 0; t < texCnt; ++t) {
                bool hasTex = false;
                if (!rdBool(c, lim, hasTex)) return 0;
                if (!hasTex) continue;
                if (c + 4 > lim) return 0;
                c += 4; // sourceRef
                if (ver <= 0x14000005u) {
                    if (c + 12 > lim) return 0;
                    c += 12; // clampMode + filterMode + uvSet
                    if (ver < 0x0A040001u) {
                        size_t ps2Bytes = (ver <= 0x0A020000u) ? 6u : 4u;
                        if (c + ps2Bytes > lim) return 0;
                        c += ps2Bytes;
                    }
                } else {
                    if (c + 2 > lim) return 0;
                    c += 2; // flags
                }
                if (ver >= 0x0401000Cu) {
                    bool hasXf = false;
                    if (!rdBool(c, lim, hasXf)) return 0;
                    if (hasXf) {
                        if (c + 24 > lim) return 0;
                        c += 24;
                    }
                }
            }
            if (ver >= 0x0A000100u) {
                if (c + 4 > lim) return 0;
                uint32_t nSh = rdU32(data + c); c += 4;
                if (nSh > 32) return 0;
                for (uint32_t s = 0; s < nSh; ++s) {
                    bool hasSh = false;
                    if (!rdBool(c, lim, hasSh)) return 0;
                    if (hasSh) return 0;
                }
            }
            return c;
        }
        if (tn == "NiSourceTexture") {
            size_t c = parseNiObjectNET(bStart, lim);
            if (c == 0 || c + 1 > lim) return 0;
            uint8_t useExt = data[c++];
            if (useExt != 0) {
                if (!skipNifString(c, lim)) return 0;
                if (ver >= 0x0A010000u) { if (c + 4 > lim) return 0; c += 4; }
            } else {
                if (ver >= 0x0A010000u) { if (!skipNifString(c, lim)) return 0; }
                if (c + 4 > lim) return 0;
                c += 4; // pixelData ref
            }
            if (c + 12 > lim) return 0;
            c += 12; // pixelLayout + useMipmaps + alphaFormat
            bool dummyB = false;
            if (!rdBool(c, lim, dummyB)) return 0; // isStatic
            if (ver >= 0x0A01006Au && !rdBool(c, lim, dummyB)) return 0;
            if (ver >= 0x14020004u && !rdBool(c, lim, dummyB)) return 0;
            return c;
        }
        if (tn == "BSXFlags" || tn == "NiIntegerExtraData" || tn == "NiFloatExtraData") {
            size_t c = bStart;
            if (ver < 0x0A000100u) {
                if (c + 8 > lim) return 0;
                c += 8; // nextExtraData + bytesRemaining
            } else {
                if (!skipNifString(c, lim)) return 0;
            }
            return (c + 4 <= lim) ? (c + 4) : 0;
        }
        if (tn == "NiStringExtraData") {
            size_t c = bStart;
            if (ver < 0x0A000100u) {
                if (c + 8 > lim) return 0;
                c += 8; // nextExtraData + bytesRemaining
            } else {
                if (!skipNifString(c, lim)) return 0;
            }
            return skipNifString(c, lim) ? c : 0;
        }
        if (tn == "NiBinaryExtraData") {
            size_t c = bStart;
            if (ver < 0x0A000100u) {
                if (c + 8 > lim) return 0;
                c += 8;
            } else {
                if (!skipNifString(c, lim)) return 0;
            }
            if (c + 4 > lim) return 0;
            uint32_t blen = rdU32(data + c); c += 4;
            return (blen <= lim - c) ? (c + blen) : 0;
        }
        if (tn == "BSBound") {
            size_t c = bStart;
            if (!skipNifString(c, lim)) return 0;
            return (c + 24 <= lim) ? (c + 24) : 0;
        }
        return 0;
    };

    // Build blockOffsets, blockEnds, and per-block type names across all NIF versions
    std::vector<size_t> blockOffsets(numBlocks, 0);
    std::vector<size_t> blockEnds(numBlocks, 0);
    std::vector<std::string> resolvedTypeNames(numBlocks);
    uint32_t validBlocks = 0;

    if (!blockSizes.empty()) {
        size_t cur = p;
        for (uint32_t b = 0; b < numBlocks; ++b) {
            if (cur > size || blockSizes[b] > size - cur) return false;
            blockOffsets[b]      = cur;
            blockEnds[b]         = cur + blockSizes[b];
            resolvedTypeNames[b] = blockTypes[blockTypeIdx[b]];
            cur += blockSizes[b];
            validBlocks = b + 1;
        }
    } else if (hasHeaderBlockTypes) {
        size_t cur = p;
        for (uint32_t b = 0; b < numBlocks; ++b) {
            const std::string& tn = blockTypes[blockTypeIdx[b]];
            resolvedTypeNames[b] = tn;
            blockOffsets[b] = cur;
            size_t nextOff = advanceSequentialBlock(tn, cur, size);
            if (nextOff == 0 || nextOff < cur || nextOff > size) {
                // Still allow inspecting this block with remaining file window if it's geometry data
                blockEnds[b] = size;
                validBlocks = b + 1;
                break;
            }
            blockEnds[b] = nextOff;
            cur = nextOff;
            validBlocks = b + 1;
        }
    } else {
        // Morrowind v4.0.0.2: each block is prefixed by SizedString [uint32 len][typeName]
        size_t cur = p;
        for (uint32_t b = 0; b < numBlocks && cur + 4 <= size; ++b) {
            uint32_t tlen = rdU32(data + cur);
            if (!isNifIdent(data + cur + 4, tlen) || cur + 4 + tlen > size) {
                // Scan forward for next Morrowind block header if previous block had unknown layout
                bool resynced = false;
                for (size_t scan = cur + 1; scan + 8 <= size; ++scan) {
                    uint32_t sl = rdU32(data + scan);
                    if (sl >= 6 && sl <= 40 && scan + 4 + sl <= size && isNifIdent(data + scan + 4, sl) &&
                        (std::memcmp(data + scan + 4, "Ni", 2) == 0 || std::memcmp(data + scan + 4, "BS", 2) == 0 ||
                         std::memcmp(data + scan + 4, "RootCollisionNode", 17) == 0))
                    {
                        cur = scan;
                        tlen = sl;
                        resynced = true;
                        break;
                    }
                }
                if (!resynced) break;
            }
            resolvedTypeNames[b].assign(reinterpret_cast<const char*>(data + cur + 4), tlen);
            size_t bodyStart = cur + 4 + tlen;
            blockOffsets[b] = bodyStart;
            size_t nextOff = advanceSequentialBlock(resolvedTypeNames[b], bodyStart, size);
            if (nextOff > bodyStart && nextOff <= size) {
                blockEnds[b] = nextOff;
                cur = nextOff;
            } else {
                blockEnds[b] = size;
                cur = bodyStart + 1;
            }
            validBlocks = b + 1;
        }
    }

    if (validBlocks == 0) return false;

    // Pass 1: Extract local transforms & NiNode children + NiGeometry -> Data references, then compose world transforms!
    std::vector<NifXform> localXforms(validBlocks);
    std::vector<std::vector<uint32_t>> nodeChildren(validBlocks);
    std::vector<int32_t> parentBlock(validBlocks, -1);
    std::vector<std::pair<uint32_t, uint32_t>> shapeToDataRefs; // (shapeBlock, dataBlock)

    for (uint32_t b = 0; b < validBlocks; ++b) {
        const std::string& tn = resolvedTypeNames[b];
        size_t bStart = blockOffsets[b];
        size_t bEnd   = blockEnds[b];
        if (bStart >= bEnd || bEnd > size) continue;

        if (isNodeTypeName(tn)) {
            NifXform xf;
            size_t afterAv = parseNiAVObject(bStart, bEnd, xf);
            if (afterAv > 0) {
                localXforms[b] = xf;
                if (afterAv + 4 <= bEnd) {
                    uint32_t nChild = rdU32(data + afterAv);
                    if (nChild <= 16384 && afterAv + 4 + static_cast<size_t>(nChild) * 4 <= bEnd) {
                        for (uint32_t k = 0; k < nChild; ++k) {
                            int32_t cref = static_cast<int32_t>(rdU32(data + afterAv + 4 + static_cast<size_t>(k) * 4));
                            if (cref >= 0 && static_cast<uint32_t>(cref) < validBlocks && static_cast<uint32_t>(cref) != b) {
                                uint32_t cid = static_cast<uint32_t>(cref);
                                nodeChildren[b].push_back(cid);
                                if (parentBlock[cid] < 0) parentBlock[cid] = static_cast<int32_t>(b);
                            }
                        }
                    }
                }
            }
        } else if (tn == "NiTriShape" || tn == "NiTriStrips" || tn == "BSLODTriShape") {
            NifXform xf;
            size_t afterAv = parseNiAVObject(bStart, bEnd, xf);
            if (afterAv > 0) {
                localXforms[b] = xf;
                if (afterAv + 4 <= bEnd) {
                    int32_t dataRef = static_cast<int32_t>(rdU32(data + afterAv));
                    if (dataRef >= 0 && static_cast<uint32_t>(dataRef) < validBlocks) {
                        shapeToDataRefs.emplace_back(b, static_cast<uint32_t>(dataRef));
                    }
                }
            }
        } else if (tn == "BSTriShape" || tn == "BSDynamicTriShape" || tn == "BSSubIndexTriShape") {
            NifXform xf;
            if (parseNiAVObject(bStart, bEnd, xf) > 0) {
                localXforms[b] = xf;
            }
        }
    }

    std::vector<NifXform> worldXforms = localXforms;
    std::vector<uint8_t> visited(validBlocks, 0);
    auto computeWorldXf = [&](auto& self, uint32_t b, int depth) -> const NifXform& {
        if (visited[b] || depth > 64) return worldXforms[b];
        visited[b] = 1;
        if (parentBlock[b] >= 0) {
            const NifXform& pxf = self(self, static_cast<uint32_t>(parentBlock[b]), depth + 1);
            worldXforms[b] = composeNifXform(pxf, localXforms[b]);
        }
        return worldXforms[b];
    };
    for (uint32_t b = 0; b < validBlocks; ++b) {
        computeWorldXf(computeWorldXf, b, 0);
    }

    std::unordered_map<uint32_t, std::vector<NifXform>> geomDataInstances;
    for (const auto& sd : shapeToDataRefs) {
        geomDataInstances[sd.second].push_back(worldXforms[sd.first]);
    }

    // Pass 2: Decode geometry blocks and emit transformed triangles!
    for (uint32_t b = 0; b < validBlocks; ++b) {
        const std::string& tn = resolvedTypeNames[b];
        size_t bStart = blockOffsets[b];
        size_t bEnd   = blockEnds[b];
        if (bStart >= bEnd || bEnd > size) continue;

        if (tn == "NiTriShapeData" || tn == "NiTriStripsData") {
            ParsedGeomData gd;
            if (parseGeomDataBlock(tn, bStart, bEnd, gd) == 0 || gd.rawVerts.empty() || gd.tris.empty()) continue;

            auto emitWithXf = [&](const NifXform& xf) {
                uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                for (const Vec3f& rv : gd.rawVerts) {
                    mesh.vertices.push_back(applyNifXform(xf, rv.x, rv.y, rv.z));
                }
                for (const auto& t : gd.tris) {
                    mesh.tris.push_back({ vBase + t[0], vBase + t[1], vBase + t[2], {} });
                }
            };

            auto itInst = geomDataInstances.find(b);
            if (itInst != geomDataInstances.end() && !itInst->second.empty()) {
                for (const auto& xf : itInst->second) emitWithXf(xf);
            } else {
                NifXform ident;
                emitWithXf(ident);
            }
        } else if ((tn == "BSTriShape" || tn == "BSDynamicTriShape" || tn == "BSSubIndexTriShape") && !blockSizes.empty()) {
            NifXform dummyLocal;
            size_t c = parseNiAVObject(bStart, bEnd, dummyLocal);
            if (c == 0) continue;
            const NifXform& xf = worldXforms[b];
            size_t boundBytes = (bsVer == 155) ? 40 : 16;
            if (c + boundBytes + 12 + 8 > bEnd) continue;
            c += boundBytes + 12;

            uint64_t vertexDesc = rdU64(data + c); c += 8;
            size_t stride = static_cast<size_t>(vertexDesc & 0xFFu) * 4;
            bool fullPrec = ((vertexDesc >> 50) & 1u) != 0 || (bsVer == 100 && stride >= 16 && ((vertexDesc >> 48) & 0x04u) != 0);

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
                        x = DetailMeshSciCad::HalfToFloat(rdU16(vp + 0));
                        y = DetailMeshSciCad::HalfToFloat(rdU16(vp + 2));
                        z = DetailMeshSciCad::HalfToFloat(rdU16(vp + 4));
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
