// FormatTests3D.cpp: self-tests of the 3D model readers (Model3DViewer, formats/), stage 15 ("15.3d" runs these alone)
#include "Crc32.h"
#include "SelfTestCommon.h"
#include "Model3DViewer.h"
#include "PathUtils.h"
#include <vector>
#include <string>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace Explorer2::SelfTestSuite {

namespace {

static uint64_t FnvMix64(uint64_t h, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (i * 8)) & 0xFFull;
        h *= 1099511628211ull;
    }
    return h;
}

static uint64_t QuantizeCoord(float f) {
    if (!std::isfinite(f)) return 0;
    int64_t q = static_cast<int64_t>(std::llround(static_cast<double>(f) * 1000.0));
    return static_cast<uint64_t>(q);
}

// Deterministic geometry fingerprint over vertex/triangle counts, unnormalized bbox size, and normalized geometry.
static uint64_t HashMeshGeometry(const LoadedMesh3D& m) {
    uint64_t h = 14695981039346656037ull;
    h = FnvMix64(h, static_cast<uint64_t>(m.rawVertexCount));
    h = FnvMix64(h, static_cast<uint64_t>(m.rawTriangleCount));
    h = FnvMix64(h, QuantizeCoord(m.bboxSize.x));
    h = FnvMix64(h, QuantizeCoord(m.bboxSize.y));
    h = FnvMix64(h, QuantizeCoord(m.bboxSize.z));
    for (const auto& v : m.vertices) {
        h = FnvMix64(h, QuantizeCoord(v.x));
        h = FnvMix64(h, QuantizeCoord(v.y));
        h = FnvMix64(h, QuantizeCoord(v.z));
    }
    for (const auto& t : m.tris) {
        h = FnvMix64(h, t.i0);
        h = FnvMix64(h, t.i1);
        h = FnvMix64(h, t.i2);
    }
    return h;
}

struct ByteWriter {
    std::vector<uint8_t> buf;

    void Pad4() {
        while ((buf.size() & 3u) != 0) buf.push_back(0);
    }
    void Bytes(const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        buf.insert(buf.end(), b, b + n);
    }
    void StrZ(const char* s) {
        Bytes(s, std::strlen(s) + 1);
    }
    void Tag4(const char* s) {
        char t[4] = {};
        for (size_t i = 0; i < 4 && s[i]; ++i) t[i] = s[i];
        Bytes(t, 4);
    }
    void U8(uint8_t v) { buf.push_back(v); }
    void U16LE(uint16_t v) {
        buf.push_back(static_cast<uint8_t>(v & 0xFFu));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    }
    void U32LE(uint32_t v) {
        for (int i = 0; i < 4; ++i) buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFFu));
    }
    void I32LE(int32_t v) { U32LE(static_cast<uint32_t>(v)); }
    void U64LE(uint64_t v) {
        for (int i = 0; i < 8; ++i) buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFFu));
    }
    void F32LE(float f) {
        uint32_t u = 0;
        std::memcpy(&u, &f, 4);
        U32LE(u);
    }
    void F64LE(double d) {
        uint64_t u = 0;
        std::memcpy(&u, &d, 8);
        U64LE(u);
    }
    void U32BE(uint32_t v) {
        for (int i = 3; i >= 0; --i) buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFFu));
    }
    void U16BE(uint16_t v) {
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
        buf.push_back(static_cast<uint8_t>(v & 0xFFu));
    }
    void I32BE(int32_t v) { U32BE(static_cast<uint32_t>(v)); }
    void F32BE(float f) {
        uint32_t u = 0;
        std::memcpy(&u, &f, 4);
        U32BE(u);
    }
    void VarintU64(uint64_t v) {
        do {
            uint8_t b = static_cast<uint8_t>(v & 0x7Fu);
            v >>= 7;
            if (v != 0) b |= 0x80u;
            buf.push_back(b);
        } while (v != 0);
    }
};

// Builds a genuine 64-bit little-endian Blender 3.0 .blend file with complete DNA1 SDNA dictionary,
// ME (Mesh), MVert/MPoly/MLoop DATA blocks, and OB (Object) world transform.
static std::vector<uint8_t> BuildSyntheticBlend30(
    const std::vector<Vec3f>& verts,
    const std::vector<std::vector<int32_t>>& polys,
    float scaleX = 1.0f, float scaleY = 1.0f, float scaleZ = 1.0f)
{
    ByteWriter dna;
    dna.Bytes("SDNANAME", 8);
    const char* fieldNames[] = {
        "*next", "*prev",
        "co[3]", "loopstart", "totloop", "v",
        "totvert", "totpoly", "*mvert", "*mpoly", "*mloop",
        "*data", "obmat[4][4]"
    };
    dna.U32LE(static_cast<uint32_t>(std::size(fieldNames)));
    for (const char* fn : fieldNames) dna.StrZ(fn);
    dna.Pad4();

    dna.Tag4("TYPE");
    const char* typeNames[] = {
        "void", "int", "float", "Link", "MVert", "MPoly", "MLoop", "Mesh", "Object"
    };
    const uint16_t typeLens[] = {
        0, 4, 4, 16, 12, 8, 4, 36, 72
    };
    dna.U32LE(static_cast<uint32_t>(std::size(typeNames)));
    for (const char* tn : typeNames) dna.StrZ(tn);
    dna.Pad4();

    dna.Tag4("TLEN");
    for (uint16_t tl : typeLens) dna.U16LE(tl);
    dna.Pad4();

    dna.Tag4("STRC");
    dna.U32LE(6); // 0=Link, 1=MVert, 2=MPoly, 3=MLoop, 4=Mesh, 5=Object
    // 0: Link { void *next, *prev; } (16B)
    dna.U16LE(3); dna.U16LE(2);
    dna.U16LE(0); dna.U16LE(0);
    dna.U16LE(0); dna.U16LE(1);
    // 1: MVert { float co[3]; } (12B)
    dna.U16LE(4); dna.U16LE(1);
    dna.U16LE(2); dna.U16LE(2);
    // 2: MPoly { int loopstart, totloop; } (8B)
    dna.U16LE(5); dna.U16LE(2);
    dna.U16LE(1); dna.U16LE(3);
    dna.U16LE(1); dna.U16LE(4);
    // 3: MLoop { int v; } (4B)
    dna.U16LE(6); dna.U16LE(1);
    dna.U16LE(1); dna.U16LE(5);
    // 4: Mesh { int totvert, totpoly, totloop; MVert *mvert; MPoly *mpoly; MLoop *mloop; } (12 + 24 = 36B)
    dna.U16LE(7); dna.U16LE(6);
    dna.U16LE(1); dna.U16LE(6);
    dna.U16LE(1); dna.U16LE(7);
    dna.U16LE(1); dna.U16LE(4);
    dna.U16LE(4); dna.U16LE(8);
    dna.U16LE(5); dna.U16LE(9);
    dna.U16LE(6); dna.U16LE(10);
    // 5: Object { void *data; float obmat[4][4]; } (8 + 64 = 72B)
    dna.U16LE(8); dna.U16LE(2);
    dna.U16LE(0); dna.U16LE(11);
    dna.U16LE(2); dna.U16LE(12);

    std::vector<int32_t> flatLoops;
    ByteWriter mpBuf, mlBuf, mvBuf;
    for (const auto& v : verts) {
        mvBuf.F32LE(v.x); mvBuf.F32LE(v.y); mvBuf.F32LE(v.z);
    }
    for (const auto& p : polys) {
        mpBuf.I32LE(static_cast<int32_t>(flatLoops.size()));
        mpBuf.I32LE(static_cast<int32_t>(p.size()));
        for (int32_t vi : p) {
            flatLoops.push_back(vi);
            mlBuf.I32LE(vi);
        }
    }

    const uint64_t addrMesh  = 0x10000ull;
    const uint64_t addrMVert = 0x20000ull;
    const uint64_t addrMPoly = 0x30000ull;
    const uint64_t addrMLoop = 0x40000ull;
    const uint64_t addrObj   = 0x50000ull;

    ByteWriter meBuf;
    meBuf.I32LE(static_cast<int32_t>(verts.size()));
    meBuf.I32LE(static_cast<int32_t>(polys.size()));
    meBuf.I32LE(static_cast<int32_t>(flatLoops.size()));
    meBuf.U64LE(addrMVert);
    meBuf.U64LE(addrMPoly);
    meBuf.U64LE(addrMLoop);

    ByteWriter obBuf;
    obBuf.U64LE(addrMesh);
    float obmat[16] = {
        scaleX, 0, 0, 0,
        0, scaleY, 0, 0,
        0, 0, scaleZ, 0,
        0, 0, 0, 1
    };
    for (float f : obmat) obBuf.F32LE(f);

    ByteWriter out;
    out.Bytes("BLENDER-v300", 12);
    auto writeBHead8 = [&](const char* code, uint64_t oldAddr, int32_t sdnaNr, int32_t count, const std::vector<uint8_t>& pay) {
        out.Tag4(code);
        out.I32LE(static_cast<int32_t>(pay.size()));
        out.U64LE(oldAddr);
        out.I32LE(sdnaNr);
        out.I32LE(count);
        if (!pay.empty()) out.Bytes(pay.data(), pay.size());
    };

    writeBHead8("ME",   addrMesh,  4, 1, meBuf.buf);
    writeBHead8("DATA", addrMVert, 1, static_cast<int32_t>(verts.size()), mvBuf.buf);
    writeBHead8("DATA", addrMPoly, 2, static_cast<int32_t>(polys.size()), mpBuf.buf);
    writeBHead8("DATA", addrMLoop, 3, static_cast<int32_t>(flatLoops.size()), mlBuf.buf);
    writeBHead8("OB",   addrObj,   5, 1, obBuf.buf);
    writeBHead8("DNA1", 0x90000ull, 0, 1, dna.buf);
    writeBHead8("ENDB", 0, 0, 0, {});
    return out.buf;
}

// Builds a Blender 4.2 SoA CustomData .blend file ("position" + ".corner_verts" + poly_offset_indices).
static std::vector<uint8_t> BuildSyntheticBlend42SoA(
    const std::vector<Vec3f>& verts,
    const std::vector<std::vector<int32_t>>& polys)
{
    ByteWriter dna;
    dna.Bytes("SDNANAME", 8);
    const char* fieldNames[] = {
        "*next", "*prev",
        "type", "name[64]", "*data",
        "*layers", "totlayer",
        "totvert", "faces_num", "corners_num", "*face_offset_indices", "vert_data", "corner_data"
    };
    dna.U32LE(static_cast<uint32_t>(std::size(fieldNames)));
    for (const char* fn : fieldNames) dna.StrZ(fn);
    dna.Pad4();

    dna.Tag4("TYPE");
    const char* typeNames[] = {
        "void", "char", "int", "float", "Link", "CustomDataLayer", "CustomData", "Mesh"
    };
    // CustomDataLayer: int type(4) + char name[64](64) + void *data(8) = 76B
    // CustomData: CustomDataLayer *layers(8) + int totlayer(4) = 12B
    // Mesh: int totvert(4) + int faces_num(4) + int corners_num(4) + int *face_offset_indices(8) + CustomData vert_data(12) + CustomData corner_data(12) = 44B
    const uint16_t typeLens[] = {
        0, 1, 4, 4, 16, 76, 12, 44
    };
    dna.U32LE(static_cast<uint32_t>(std::size(typeNames)));
    for (const char* tn : typeNames) dna.StrZ(tn);
    dna.Pad4();

    dna.Tag4("TLEN");
    for (uint16_t tl : typeLens) dna.U16LE(tl);
    dna.Pad4();

    dna.Tag4("STRC");
    dna.U32LE(4); // 0=Link, 1=CustomDataLayer, 2=CustomData, 3=Mesh
    // 0: Link
    dna.U16LE(4); dna.U16LE(2);
    dna.U16LE(0); dna.U16LE(0);
    dna.U16LE(0); dna.U16LE(1);
    // 1: CustomDataLayer
    dna.U16LE(5); dna.U16LE(3);
    dna.U16LE(2); dna.U16LE(2);
    dna.U16LE(1); dna.U16LE(3);
    dna.U16LE(0); dna.U16LE(4);
    // 2: CustomData
    dna.U16LE(6); dna.U16LE(2);
    dna.U16LE(5); dna.U16LE(5);
    dna.U16LE(2); dna.U16LE(6);
    // 3: Mesh
    dna.U16LE(7); dna.U16LE(6);
    dna.U16LE(2); dna.U16LE(7);
    dna.U16LE(2); dna.U16LE(8);
    dna.U16LE(2); dna.U16LE(9);
    dna.U16LE(2); dna.U16LE(10); // *face_offset_indices
    dna.U16LE(6); dna.U16LE(11); // vert_data
    dna.U16LE(6); dna.U16LE(12); // corner_data

    const uint64_t addrMesh        = 0x11000ull;
    const uint64_t addrVLayers     = 0x21000ull;
    const uint64_t addrPosData     = 0x22000ull;
    const uint64_t addrCLayers     = 0x31000ull;
    const uint64_t addrCornerVerts = 0x32000ull;
    const uint64_t addrFaceOffsets = 0x41000ull;

    ByteWriter posData, cornerData, faceOffData;
    for (const auto& v : verts) {
        posData.F32LE(v.x); posData.F32LE(v.y); posData.F32LE(v.z);
    }
    int32_t running = 0;
    faceOffData.I32LE(0);
    for (const auto& p : polys) {
        for (int32_t vi : p) cornerData.I32LE(vi);
        running += static_cast<int32_t>(p.size());
        faceOffData.I32LE(running);
    }

    auto makeLayer = [](int32_t lType, const char* name, uint64_t dataPtr) {
        ByteWriter lw;
        lw.I32LE(lType);
        char nm[64] = {};
        std::memcpy(nm, name, std::min<size_t>(63, std::strlen(name)));
        lw.Bytes(nm, 64);
        lw.U64LE(dataPtr);
        return lw.buf;
    };

    ByteWriter meBuf;
    meBuf.I32LE(static_cast<int32_t>(verts.size()));
    meBuf.I32LE(static_cast<int32_t>(polys.size()));
    meBuf.I32LE(running);
    meBuf.U64LE(addrFaceOffsets);
    meBuf.U64LE(addrVLayers); meBuf.I32LE(1);
    meBuf.U64LE(addrCLayers); meBuf.I32LE(1);

    ByteWriter out;
    out.Bytes("BLENDER-v402", 12);
    auto writeBHead8 = [&](const char* code, uint64_t oldAddr, int32_t sdnaNr, int32_t count, const std::vector<uint8_t>& pay) {
        out.Tag4(code);
        out.I32LE(static_cast<int32_t>(pay.size()));
        out.U64LE(oldAddr);
        out.I32LE(sdnaNr);
        out.I32LE(count);
        if (!pay.empty()) out.Bytes(pay.data(), pay.size());
    };
    writeBHead8("ME",   addrMesh,        3, 1, meBuf.buf);
    writeBHead8("DATA", addrVLayers,     1, 1, makeLayer(48, "position", addrPosData));
    writeBHead8("DATA", addrPosData,     0, static_cast<int32_t>(verts.size()), posData.buf);
    writeBHead8("DATA", addrCLayers,     1, 1, makeLayer(10, ".corner_verts", addrCornerVerts));
    writeBHead8("DATA", addrCornerVerts, 0, running, cornerData.buf);
    writeBHead8("DATA", addrFaceOffsets, 0, static_cast<int32_t>(polys.size() + 1), faceOffData.buf);
    writeBHead8("DNA1", 0x99000ull,      0, 1, dna.buf);
    writeBHead8("ENDB", 0,               0, 0, {});
    return out.buf;
}

// Builds a big-endian Maya Binary (.mb) FOR4 Maya -> FOR4 MESH -> VRTS / EDGE / FACE file.
static std::vector<uint8_t> BuildSyntheticMayaMb(
    const std::vector<Vec3f>& verts,
    const std::vector<std::pair<int32_t, int32_t>>& edges,
    const std::vector<std::vector<int32_t>>& faceEdgeLoops)
{
    auto makeSubChunk = [](const char* tag, const std::vector<uint8_t>& payload) {
        ByteWriter cw;
        cw.Tag4(tag);
        cw.U32BE(static_cast<uint32_t>(payload.size()));
        cw.Bytes(payload.data(), payload.size());
        cw.Pad4();
        return cw.buf;
    };

    ByteWriter vrtsPay, edgePay, facePay;
    for (const auto& v : verts) {
        vrtsPay.F32BE(v.x); vrtsPay.F32BE(v.y); vrtsPay.F32BE(v.z);
    }
    for (const auto& e : edges) {
        edgePay.I32BE(e.first); edgePay.I32BE(e.second); edgePay.I32BE(0);
    }
    for (const auto& loop : faceEdgeLoops) {
        facePay.I32BE(static_cast<int32_t>(loop.size()));
        for (int32_t eref : loop) facePay.I32BE(eref);
    }

    ByteWriter meshBody;
    meshBody.Tag4("MESH");
    auto cV = makeSubChunk("VRTS", vrtsPay.buf);
    auto cE = makeSubChunk("EDGE", edgePay.buf);
    auto cF = makeSubChunk("FACE", facePay.buf);
    meshBody.Bytes(cV.data(), cV.size());
    meshBody.Bytes(cE.data(), cE.size());
    meshBody.Bytes(cF.data(), cF.size());

    ByteWriter mayaBody;
    mayaBody.Tag4("Maya");
    auto grpMesh = makeSubChunk("FOR4", meshBody.buf);
    mayaBody.Bytes(grpMesh.data(), grpMesh.size());

    return makeSubChunk("FOR4", mayaBody.buf);
}

// Builds an Alembic Ogawa (.abc) archive with root group -> .geom CompoundProperty -> P, .faceIndices, .faceCounts.
static std::vector<uint8_t> BuildSyntheticOgawaAbc(
    const std::vector<Vec3f>& verts,
    const std::vector<int32_t>& faceIndices,
    const std::vector<int32_t>& faceCounts)
{
    ByteWriter w;
    const uint8_t magic[8] = { 'O','g','a','w','a', 0xFF, 0x00, 0x01 };
    w.Bytes(magic, 8);
    w.U64LE(0); // placeholder for rootGroupOffset at byte 8

    auto appendDataStream = [&](const void* payload, size_t len, bool prependDigest16) -> uint64_t {
        uint64_t off = w.buf.size();
        uint64_t totalLen = len + (prependDigest16 ? 16 : 0);
        w.U64LE(totalLen);
        if (prependDigest16) {
            uint8_t dig[16] = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 };
            w.Bytes(dig, 16);
        }
        if (len > 0) w.Bytes(payload, len);
        return off | (1ULL << 63);
    };

    auto appendArrayPropGroup = [&](const void* elemData, size_t byteLen, uint64_t elemCount) -> uint64_t {
        uint64_t dsSample = appendDataStream(elemData, byteLen, true);
        ByteWriter dimW;
        dimW.U64LE(elemCount);
        uint64_t dsDims = appendDataStream(dimW.buf.data(), dimW.buf.size(), true);

        uint64_t grpOff = w.buf.size();
        w.U64LE(2);
        w.U64LE(dsSample);
        w.U64LE(dsDims);
        return grpOff;
    };

    ByteWriter pBuf, fiBuf, fcBuf;
    for (const auto& v : verts) { pBuf.F32LE(v.x); pBuf.F32LE(v.y); pBuf.F32LE(v.z); }
    for (int32_t idx : faceIndices) fiBuf.I32LE(idx);
    for (int32_t cnt : faceCounts)  fcBuf.I32LE(cnt);

    uint64_t grpP  = appendArrayPropGroup(pBuf.buf.data(),  pBuf.buf.size(),  verts.size());
    uint64_t grpFI = appendArrayPropGroup(fiBuf.buf.data(), fiBuf.buf.size(), faceIndices.size());
    uint64_t grpFC = appendArrayPropGroup(fcBuf.buf.data(), fcBuf.buf.size(), faceCounts.size());

    // AbcCoreOgawa PropertyHeaders stream for [P, .faceIndices, .faceCounts]
    ByteWriter ph;
    auto writeArrayPropHdr = [&](const char* name) {
        // info: propType=2 (Array), szHint=0 (uint8), bit 11=0x0800 (zeroChanged -> omit first/last)
        uint32_t info = 2u | 0x0800u;
        ph.U32LE(info);
        ph.U8(1); // nextSampleIndex = 1
        uint8_t nlen = static_cast<uint8_t>(std::strlen(name));
        ph.U8(nlen);
        ph.Bytes(name, nlen);
        ph.U8(0); // metaIdx = 0
    };
    writeArrayPropHdr("P");
    writeArrayPropHdr(".faceIndices");
    writeArrayPropHdr(".faceCounts");
    uint64_t dsGeomHdr = appendDataStream(ph.buf.data(), ph.buf.size(), false);

    uint64_t geomGrpOff = w.buf.size();
    w.U64LE(4);
    w.U64LE(grpP);
    w.U64LE(grpFI);
    w.U64LE(grpFC);
    w.U64LE(dsGeomHdr);

    uint64_t rootGrpOff = w.buf.size();
    w.U64LE(1);
    w.U64LE(geomGrpOff);

    // Patch rootGroupOffset at byte 8
    for (int i = 0; i < 8; ++i) {
        w.buf[8 + i] = static_cast<uint8_t>((rootGrpOff >> (i * 8)) & 0xFFu);
    }
    return w.buf;
}

// Builds a Gamebryo 20.2.0.7 (.nif) file with exact header, blockTypes, blockSizes, and NiTriShapeData.
static std::vector<uint8_t> BuildSyntheticNif(
    const std::vector<Vec3f>& verts,
    const std::vector<std::array<uint16_t, 3>>& tris)
{
    ByteWriter blk;
    blk.I32LE(0);                                     // groupId
    blk.U16LE(static_cast<uint16_t>(verts.size()));   // numVertices
    blk.U8(0); blk.U8(0);                             // keepFlags, compressFlags
    blk.U8(1);                                        // hasVertices
    for (const auto& v : verts) {
        blk.F32LE(v.x); blk.F32LE(v.y); blk.F32LE(v.z);
    }
    blk.U16LE(0);                                     // dataFlags
    blk.U8(0);                                        // hasNormals
    blk.F32LE(0.0f); blk.F32LE(0.0f); blk.F32LE(0.0f); blk.F32LE(1.0f); // center + radius
    blk.U8(0);                                        // hasVertexColors
    blk.U16LE(0x4000u);                               // consistencyFlags (CT_STATIC)
    blk.I32LE(-1);                                    // additionalData
    blk.U16LE(static_cast<uint16_t>(tris.size()));    // numTriangles
    blk.U32LE(static_cast<uint32_t>(tris.size() * 3));// numTrianglePoints
    blk.U8(1);                                        // hasTriangles
    for (const auto& t : tris) {
        blk.U16LE(t[0]); blk.U16LE(t[1]); blk.U16LE(t[2]);
    }
    blk.U16LE(0);                                     // numMatchGroups

    ByteWriter out;
    const char* hdr = "Gamebryo File Format, Version 20.2.0.7\n";
    out.Bytes(hdr, std::strlen(hdr));
    out.U32LE(0x14020007u); // version 20.2.0.7
    out.U8(1);              // ENDIAN_LITTLE
    out.U32LE(0);           // userVersion
    out.U32LE(1);           // numBlocks = 1
    out.U16LE(1);           // numBlockTypes = 1
    const char* bt = "NiTriShapeData";
    out.U32LE(static_cast<uint32_t>(std::strlen(bt)));
    out.Bytes(bt, std::strlen(bt));
    out.U16LE(0);           // blockTypeIndex[0] = 0
    out.U32LE(static_cast<uint32_t>(blk.buf.size())); // blockSizes[0]
    out.U32LE(0);           // numStrings
    out.U32LE(0);           // maxStringLength
    out.U32LE(0);           // numGroups
    out.Bytes(blk.buf.data(), blk.buf.size());
    out.U32LE(1);           // footer numRoots
    out.I32LE(0);           // root[0] = 0
    return out.buf;
}

// Builds a Google Draco v2.2 (.drc) Sequential uncompressed mesh bitstream.
static std::vector<uint8_t> BuildSyntheticDracoMesh(
    const std::vector<Vec3f>& verts,
    const std::vector<std::array<uint8_t, 3>>& tris)
{
    ByteWriter w;
    w.Bytes("DRACO", 5);
    w.U8(2); // major
    w.U8(2); // minor
    w.U8(1); // TRIANGULAR_MESH
    w.U8(0); // MESH_SEQUENTIAL_ENCODING
    w.U16LE(0); // flags

    w.VarintU64(tris.size());
    w.VarintU64(verts.size());
    w.U8(1); // connectivityMethod = 1 (uncompressed uint8 indices when numPoints < 256)
    for (const auto& t : tris) {
        w.U8(t[0]); w.U8(t[1]); w.U8(t[2]);
    }

    w.U8(1); // numAttributesDecoders = 1
    w.VarintU64(1); // numAttributes = 1
    w.U8(0); // attType = POSITION (0)
    w.U8(9); // dataType = DT_FLOAT32 (9)
    w.U8(3); // numComponents = 3
    w.U8(0); // normalized = 0
    w.VarintU64(0); // uniqueId = 0
    w.U8(0); // SEQUENTIAL_ATTRIBUTE_ENCODER_GENERIC (0)

    for (const auto& v : verts) {
        w.F32LE(v.x); w.F32LE(v.y); w.F32LE(v.z);
    }
    return w.buf;
}

// Wraps raw bytes in Pixar TfFastCompression single-chunk literal LZ4 framing.
static std::vector<uint8_t> TfWrapLz4Literals(const std::vector<uint8_t>& raw) {
    ByteWriter w;
    w.U8(0); // nChunks = 0 -> single LZ4 block
    size_t n = raw.size();
    if (n < 15) {
        w.U8(static_cast<uint8_t>(n << 4));
    } else {
        w.U8(0xF0u);
        size_t rem = n - 15;
        while (rem >= 255) { w.U8(255); rem -= 255; }
        w.U8(static_cast<uint8_t>(rem));
    }
    if (!raw.empty()) w.Bytes(raw.data(), raw.size());
    return w.buf;
}

static std::vector<uint8_t> UsdWrapCompressedInts32(const std::vector<int32_t>& vals) {
    ByteWriter raw;
    raw.I32LE(0); // commonValue = 0
    size_t codesBytes = (vals.size() * 2 + 7) / 8;
    for (size_t i = 0; i < codesBytes; ++i) raw.U8(0xFFu); // all 2-bit codes = 3 (int32 delta)
    int32_t prev = 0;
    for (int32_t v : vals) {
        int32_t d = static_cast<int32_t>(static_cast<uint32_t>(v) - static_cast<uint32_t>(prev));
        raw.I32LE(d);
        prev = v;
    }
    return TfWrapLz4Literals(raw.buf);
}

// Builds a genuine Pixar OpenUSD Binary Crate ("PXR-USDC" v0.8.0) with TOKENS, FIELDS, FIELDSETS, PATHS, SPECS.
static std::vector<uint8_t> BuildSyntheticUsdc(
    const std::vector<Vec3f>& pts,
    const std::vector<int32_t>& faceCounts,
    const std::vector<int32_t>& faceIndices)
{
    ByteWriter w;
    w.Bytes("PXR-USDC", 8);
    uint8_t ver[8] = { 0, 8, 0, 0, 0, 0, 0, 0 };
    w.Bytes(ver, 8);
    w.U64LE(0); // TOC offset placeholder at +16
    while (w.buf.size() < 88) w.U8(0);

    // Uncompressed array payloads at byte >= 88
    uint64_t ptsOff = w.buf.size();
    w.U64LE(pts.size());
    for (const auto& p : pts) { w.F32LE(p.x); w.F32LE(p.y); w.F32LE(p.z); }

    uint64_t cntOff = w.buf.size();
    w.U64LE(faceCounts.size());
    for (int32_t c : faceCounts) w.I32LE(c);

    uint64_t idxOff = w.buf.size();
    w.U64LE(faceIndices.size());
    for (int32_t idx : faceIndices) w.I32LE(idx);

    auto makeArrayValueRep = [](uint8_t typeEnum, uint64_t offset) -> uint64_t {
        return (1ULL << 63) | (static_cast<uint64_t>(typeEnum) << 48) | (offset & ((1ULL << 48) - 1ULL));
    };
    uint64_t repPts = makeArrayValueRep(19, ptsOff); // 19 = Vec3f
    uint64_t repCnt = makeArrayValueRep(3,  cntOff); // 3  = Int
    uint64_t repIdx = makeArrayValueRep(3,  idxOff); // 3  = Int

    // TOKENS: 0="", 1="Root", 2="default", 3="points", 4="faceVertexCounts", 5="faceVertexIndices"
    ByteWriter tokRaw;
    const char* toks[] = { "", "Root", "default", "points", "faceVertexCounts", "faceVertexIndices" };
    for (const char* s : toks) tokRaw.StrZ(s);
    auto tokLz4 = TfWrapLz4Literals(tokRaw.buf);

    uint64_t secTokStart = w.buf.size();
    w.U64LE(std::size(toks));
    w.U64LE(tokRaw.buf.size());
    w.U64LE(tokLz4.size());
    w.Bytes(tokLz4.data(), tokLz4.size());
    uint64_t secTokLen = w.buf.size() - secTokStart;

    // FIELDS: 3 fields with tokenIndex=2 ("default") and ValueReps [repPts, repCnt, repIdx]
    auto fTokComp = UsdWrapCompressedInts32({ 2, 2, 2 });
    ByteWriter fRepRaw;
    fRepRaw.U64LE(repPts);
    fRepRaw.U64LE(repCnt);
    fRepRaw.U64LE(repIdx);
    auto fRepComp = TfWrapLz4Literals(fRepRaw.buf);

    uint64_t secFieldsStart = w.buf.size();
    w.U64LE(3);
    w.U64LE(fTokComp.size());
    w.Bytes(fTokComp.data(), fTokComp.size());
    w.U64LE(fRepComp.size());
    w.Bytes(fRepComp.data(), fRepComp.size());
    uint64_t secFieldsLen = w.buf.size() - secFieldsStart;

    // FIELDSETS: [0, -1, 1, -1, 2, -1]
    std::vector<int32_t> fsVec = { 0, -1, 1, -1, 2, -1 };
    auto fsComp = UsdWrapCompressedInts32(fsVec);
    uint64_t secFsStart = w.buf.size();
    w.U64LE(fsVec.size());
    w.U64LE(fsComp.size());
    w.Bytes(fsComp.data(), fsComp.size());
    uint64_t secFsLen = w.buf.size() - secFsStart;

    // PATHS: 0="/" (child->1), 1="/Root" (child->2), 2="/Root.points" (sibling->3), 3="/Root.faceVertexCounts" (sibling->4), 4="/Root.faceVertexIndices" (leaf)
    auto pIdxComp  = UsdWrapCompressedInts32({ 0, 1, 2, 3, 4 });
    auto pTokComp  = UsdWrapCompressedInts32({ 0, 1, -3, -4, -5 });
    auto pJumpComp = UsdWrapCompressedInts32({ -1, -1, 0, 0, -2 });
    uint64_t secPathsStart = w.buf.size();
    w.U64LE(5); // totalPaths
    w.U64LE(5); // numEncodedPaths
    w.U64LE(pIdxComp.size());  w.Bytes(pIdxComp.data(), pIdxComp.size());
    w.U64LE(pTokComp.size());  w.Bytes(pTokComp.data(), pTokComp.size());
    w.U64LE(pJumpComp.size()); w.Bytes(pJumpComp.data(), pJumpComp.size());
    uint64_t secPathsLen = w.buf.size() - secPathsStart;

    // SPECS: specs on property paths 2, 3, 4 pointing to fieldset offsets 0, 2, 4 (SpecTypeAttribute = 1)
    auto sPathComp = UsdWrapCompressedInts32({ 2, 3, 4 });
    auto sFsComp   = UsdWrapCompressedInts32({ 0, 2, 4 });
    auto sTypeComp = UsdWrapCompressedInts32({ 1, 1, 1 });
    uint64_t secSpecsStart = w.buf.size();
    w.U64LE(3);
    w.U64LE(sPathComp.size()); w.Bytes(sPathComp.data(), sPathComp.size());
    w.U64LE(sFsComp.size());   w.Bytes(sFsComp.data(), sFsComp.size());
    w.U64LE(sTypeComp.size()); w.Bytes(sTypeComp.data(), sTypeComp.size());
    uint64_t secSpecsLen = w.buf.size() - secSpecsStart;

    // TOC
    uint64_t tocOff = w.buf.size();
    w.U64LE(5);
    auto writeSecEntry = [&](const char* name, uint64_t st, uint64_t ln) {
        char nm[16] = {};
        std::memcpy(nm, name, std::min<size_t>(15, std::strlen(name)));
        w.Bytes(nm, 16);
        w.U64LE(st);
        w.U64LE(ln);
    };
    writeSecEntry("TOKENS",    secTokStart,    secTokLen);
    writeSecEntry("FIELDS",    secFieldsStart, secFieldsLen);
    writeSecEntry("FIELDSETS", secFsStart,     secFsLen);
    writeSecEntry("PATHS",     secPathsStart,  secPathsLen);
    writeSecEntry("SPECS",     secSpecsStart,  secSpecsLen);

    for (int i = 0; i < 8; ++i) {
        w.buf[16 + i] = static_cast<uint8_t>((tocOff >> (i * 8)) & 0xFFu);
    }
    return w.buf;
}

// Builds a genuine binary ACIS (.sab) B-Rep solid (body -> lump -> shell -> face -> loop -> coedge -> edge -> vertex -> point).
static std::vector<uint8_t> BuildSyntheticSabBRep(
    const std::vector<Vec3f>& verts,
    const std::vector<std::vector<int32_t>>& faceLoops)
{
    struct UndirEdge { int32_t u, v; };
    std::vector<UndirEdge> edges;
    auto findOrAddEdge = [&](int32_t a, int32_t b) -> int32_t {
        int32_t lo = std::min(a, b), hi = std::max(a, b);
        for (size_t i = 0; i < edges.size(); ++i) {
            if (edges[i].u == lo && edges[i].v == hi) return static_cast<int32_t>(i);
        }
        edges.push_back({ lo, hi });
        return static_cast<int32_t>(edges.size() - 1);
    };
    for (const auto& poly : faceLoops) {
        for (size_t k = 0; k < poly.size(); ++k) {
            findOrAddEdge(poly[k], poly[(k + 1) % poly.size()]);
        }
    }

    const int32_t nV = static_cast<int32_t>(verts.size());
    const int32_t nE = static_cast<int32_t>(edges.size());
    const int32_t nF = static_cast<int32_t>(faceLoops.size());
    const int32_t idxPtBase   = 3;
    const int32_t idxVtBase   = 3 + nV;
    const int32_t idxCurve    = 3 + 2 * nV;
    const int32_t idxEdgeBase = idxCurve + 1;

    struct FaceRecPlan {
        int32_t surfId;
        int32_t loopId;
        int32_t faceId;
        int32_t ceBase;
    };
    std::vector<FaceRecPlan> plans(static_cast<size_t>(nF));
    int32_t curIdx = idxEdgeBase + nE;
    for (int32_t f = 0; f < nF; ++f) {
        plans[static_cast<size_t>(f)].surfId = curIdx++;
        plans[static_cast<size_t>(f)].loopId = curIdx++;
        plans[static_cast<size_t>(f)].faceId = curIdx++;
        plans[static_cast<size_t>(f)].ceBase = curIdx;
        curIdx += static_cast<int32_t>(faceLoops[static_cast<size_t>(f)].size());
    }
    const int32_t totalRecs = curIdx;

    ByteWriter w;
    w.Bytes("ACIS BinaryFile", 15);
    w.I32LE(700);
    w.I32LE(totalRecs);
    w.I32LE(totalRecs);
    w.I32LE(0);

    auto writeStrTag = [&](const char* s) {
        w.U8(0x07);
        uint8_t n = static_cast<uint8_t>(std::strlen(s));
        w.U8(n);
        w.Bytes(s, n);
    };
    auto wIdent = [&](const char* s) {
        w.U8(0x0D);
        uint8_t n = static_cast<uint8_t>(std::strlen(s));
        w.U8(n);
        w.Bytes(s, n);
    };
    auto wSubIdent = [&](const char* s) {
        w.U8(0x0E);
        uint8_t n = static_cast<uint8_t>(std::strlen(s));
        w.U8(n);
        w.Bytes(s, n);
    };
    auto wPtr = [&](int32_t ref) { w.U8(0x0C); w.I32LE(ref); };
    auto wPos = [&](double x, double y, double z) { w.U8(0x13); w.F64LE(x); w.F64LE(y); w.F64LE(z); };
    auto wVec = [&](double x, double y, double z) { w.U8(0x14); w.F64LE(x); w.F64LE(y); w.F64LE(z); };
    auto wTerm = [&]() { w.U8(0x11); };

    writeStrTag("Explorer2Test");
    writeStrTag("ACIS 7.0");
    writeStrTag("2026");
    w.U8(0x06); w.F64LE(1.0);
    w.U8(0x06); w.F64LE(1e-6);
    w.U8(0x06); w.F64LE(1e-10);

    // 0: body, 1: lump, 2: shell
    wIdent("body");  wPtr(-1); wPtr(1); wPtr(-1); wPtr(-1); wTerm();
    wIdent("lump");  wPtr(-1); wPtr(-1); wPtr(2); wPtr(0);  wTerm();
    wIdent("shell"); wPtr(-1); wPtr(-1); wPtr(-1); wPtr(nF > 0 ? plans[0].faceId : -1); wPtr(-1); wPtr(1); wTerm();

    // points & vertices
    for (int32_t i = 0; i < nV; ++i) {
        wIdent("point"); wPtr(-1); wPos(verts[static_cast<size_t>(i)].x, verts[static_cast<size_t>(i)].y, verts[static_cast<size_t>(i)].z); wTerm();
    }
    for (int32_t i = 0; i < nV; ++i) {
        wIdent("vertex"); wPtr(-1); wPtr(-1); wPtr(idxPtBase + i); wTerm();
    }

    // shared straight-curve + topological edges
    wIdent("straight"); wSubIdent("curve"); wPtr(-1); wPos(0.0, 0.0, 0.0); wVec(1.0, 0.0, 0.0); wTerm();
    for (int32_t e = 0; e < nE; ++e) {
        wIdent("edge"); wPtr(-1);
        wPtr(idxVtBase + edges[static_cast<size_t>(e)].u);
        wPtr(idxVtBase + edges[static_cast<size_t>(e)].v);
        wPtr(-1); wPtr(idxCurve); wIdent("forward"); wTerm();
    }

    // per-face plane-surface, loop, face, and circular coedges
    for (int32_t f = 0; f < nF; ++f) {
        const auto& poly = faceLoops[static_cast<size_t>(f)];
        const auto& pl = plans[static_cast<size_t>(f)];
        Vec3f p0 = verts[static_cast<size_t>(poly[0])];
        Vec3f p1 = verts[static_cast<size_t>(poly[1])];
        Vec3f p2 = verts[static_cast<size_t>(poly[2])];
        double ux = p1.x - p0.x, uy = p1.y - p0.y, uz = p1.z - p0.z;
        double vx = p2.x - p0.x, vy = p2.y - p0.y, vz = p2.z - p0.z;
        double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        double nL = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (nL > 1e-12) { nx /= nL; ny /= nL; nz /= nL; } else { nx = 0.0; ny = 0.0; nz = 1.0; }

        wIdent("plane"); wSubIdent("surface"); wPtr(-1); wPos(p0.x, p0.y, p0.z); wVec(nx, ny, nz); wTerm();
        wIdent("loop");  wPtr(-1); wPtr(-1); wPtr(pl.ceBase); wPtr(pl.faceId); wTerm();
        wIdent("face");  wPtr(-1); wPtr(f + 1 < nF ? plans[static_cast<size_t>(f + 1)].faceId : -1);
        wPtr(pl.loopId); wPtr(2); wPtr(-1); wPtr(pl.surfId); wIdent("forward"); wIdent("single"); wTerm();

        int32_t m = static_cast<int32_t>(poly.size());
        for (int32_t k = 0; k < m; ++k) {
            int32_t u = poly[static_cast<size_t>(k)];
            int32_t v = poly[static_cast<size_t>((k + 1) % m)];
            int32_t eIdx = findOrAddEdge(u, v);
            bool fwd = (u < v);
            wIdent("coedge"); wPtr(-1);
            wPtr(pl.ceBase + ((k + 1) % m));
            wPtr(pl.ceBase + ((k + m - 1) % m));
            wPtr(-1);
            wPtr(idxEdgeBase + eIdx);
            wIdent(fwd ? "forward" : "reversed");
            wPtr(pl.loopId);
            wTerm();
        }
    }
    wIdent("End-of-ACIS-data");
    return w.buf;
}

// Builds a bare disconnected point-only .sab file (zero edges/faces) to verify zero-cheat rejection.
static std::vector<uint8_t> BuildSyntheticSabBarePoints(const std::vector<Vec3f>& positions) {
    ByteWriter w;
    w.Bytes("ACIS BinaryFile", 15);
    w.I32LE(700);
    w.I32LE(static_cast<int32_t>(positions.size()));
    w.I32LE(static_cast<int32_t>(positions.size()));
    w.I32LE(0);
    auto writeStrTag = [&](const char* s) {
        w.U8(0x07);
        uint8_t n = static_cast<uint8_t>(std::strlen(s));
        w.U8(n);
        w.Bytes(s, n);
    };
    writeStrTag("Explorer2Test");
    writeStrTag("ACIS 7.0");
    writeStrTag("2026");
    w.U8(0x06); w.F64LE(1.0);
    w.U8(0x06); w.F64LE(1e-6);
    w.U8(0x06); w.F64LE(1e-10);
    for (const auto& p : positions) {
        w.U8(0x0D); w.U8(5); w.Bytes("point", 5);
        w.U8(0x0C); w.I32LE(-1);
        w.U8(0x13);
        w.F64LE(p.x); w.F64LE(p.y); w.F64LE(p.z);
        w.U8(0x11);
    }
    return w.buf;
}


static std::vector<uint8_t> FinalizeE57LogicalToPhysicalPages(
    const std::vector<uint8_t>& cvAndPackets,
    const std::string& xmlText)
{
    const size_t kPageSize = 1024;
    const size_t kUsable   = 1020;
    auto logToPhys = [&](uint64_t logOff) -> uint64_t {
        return (logOff / kUsable) * kPageSize + (logOff % kUsable);
    };

    ByteWriter logW;
    logW.Bytes("ASTM-E57", 8);
    logW.U32LE(1); // major
    logW.U32LE(0); // minor
    logW.U64LE(0); // filePhysicalLength placeholder (+16)
    logW.U64LE(0); // xmlPhysicalOffset placeholder (+24)
    logW.U64LE(xmlText.size()); // xmlLogicalLength (+32)
    logW.U64LE(kPageSize);      // pageSize (+40)

    logW.Bytes(cvAndPackets.data(), cvAndPackets.size());
    uint64_t xmlLogOff = logW.buf.size();
    logW.Bytes(xmlText.data(), xmlText.size());

    size_t numPages = (logW.buf.size() + kUsable - 1) / kUsable;
    if (numPages == 0) numPages = 1;
    logW.buf.resize(numPages * kUsable, 0);

    uint64_t filePhysLen = static_cast<uint64_t>(numPages * kPageSize);
    uint64_t xmlPhysOff  = logToPhys(xmlLogOff);
    for (int i = 0; i < 8; ++i) {
        logW.buf[16 + i] = static_cast<uint8_t>((filePhysLen >> (i * 8)) & 0xFFu);
        logW.buf[24 + i] = static_cast<uint8_t>((xmlPhysOff  >> (i * 8)) & 0xFFu);
    }

    std::vector<uint8_t> phys(numPages * kPageSize, 0);
    for (size_t p = 0; p < numPages; ++p) {
        std::memcpy(phys.data() + p * kPageSize, logW.buf.data() + p * kUsable, kUsable);
        uint32_t crc = Checksum::Crc32C(phys.data() + p * kPageSize, kUsable);
        phys[p * kPageSize + kUsable + 0] = static_cast<uint8_t>((crc >> 24) & 0xFFu);
        phys[p * kPageSize + kUsable + 1] = static_cast<uint8_t>((crc >> 16) & 0xFFu);
        phys[p * kPageSize + kUsable + 2] = static_cast<uint8_t>((crc >>  8) & 0xFFu);
        phys[p * kPageSize + kUsable + 3] = static_cast<uint8_t>(crc & 0xFFu);
    }
    return phys;
}

static std::vector<uint8_t> BuildSyntheticE57Float32(const std::vector<Vec3f>& pts) {
    ByteWriter sx, sy, sz;
    for (const auto& p : pts) {
        sx.F32LE(p.x);
        sy.F32LE(p.y);
        sz.F32LE(p.z);
    }
    ByteWriter pkt;
    pkt.U8(1); // DataPacket
    pkt.U8(0); // flags
    pkt.U16LE(0); // lengthMinus1 placeholder
    pkt.U16LE(3); // 3 bytestreams
    pkt.U16LE(static_cast<uint16_t>(sx.buf.size()));
    pkt.U16LE(static_cast<uint16_t>(sy.buf.size()));
    pkt.U16LE(static_cast<uint16_t>(sz.buf.size()));
    pkt.Bytes(sx.buf.data(), sx.buf.size());
    pkt.Bytes(sy.buf.data(), sy.buf.size());
    pkt.Bytes(sz.buf.data(), sz.buf.size());
    pkt.Pad4();
    uint16_t lenMinus1 = static_cast<uint16_t>(pkt.buf.size() - 1);
    pkt.buf[2] = static_cast<uint8_t>(lenMinus1 & 0xFFu);
    pkt.buf[3] = static_cast<uint8_t>((lenMinus1 >> 8) & 0xFFu);

    ByteWriter cv;
    cv.U8(1); // CompressedVectorSection (sectionId == 1)
    for (int i = 0; i < 7; ++i) cv.U8(0);
    cv.U64LE(32ULL + pkt.buf.size()); // sectionLogicalLength
    cv.U64LE(80ULL);                  // dataPhysicalOffset (logical 80 == physical 80 on page 0)
    cv.U64LE(0ULL);                   // indexPhysicalOffset
    cv.Bytes(pkt.buf.data(), pkt.buf.size());

    std::string xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<e57Root type=\"Structure\" xmlns=\"http://www.astm.org/COMMIT/E57/2010-e57-v1.0\">\n"
        "  <data3D type=\"Vector\" allowHeterogeneousChildren=\"1\">\n"
        "    <vectorChild type=\"Structure\">\n"
        "      <points type=\"CompressedVector\" fileOffset=\"48\" recordCount=\"" + std::to_string(pts.size()) + "\">\n"
        "        <prototype type=\"Structure\">\n"
        "          <cartesianX type=\"Float\" precision=\"single\"/>\n"
        "          <cartesianY type=\"Float\" precision=\"single\"/>\n"
        "          <cartesianZ type=\"Float\" precision=\"single\"/>\n"
        "        </prototype>\n"
        "      </points>\n"
        "    </vectorChild>\n"
        "  </data3D>\n"
        "</e57Root>\n";
    return FinalizeE57LogicalToPhysicalPages(cv.buf, xml);
}

static std::vector<uint8_t> BuildSyntheticE57ScaledPose(
    const std::vector<Vec3f>& pts,
    const std::vector<uint8_t>& invalidFlags,
    double transX, double transY, double transZ)
{
    struct LsbBitPacker {
        std::vector<uint8_t> buf;
        uint32_t bitPos = 0;
        void Put(uint64_t val, uint32_t nBits) {
            for (uint32_t b = 0; b < nBits; ++b) {
                if ((bitPos & 7u) == 0) buf.push_back(0);
                if ((val >> b) & 1ULL) buf.back() |= static_cast<uint8_t>(1u << (bitPos & 7u));
                ++bitPos;
            }
        }
    };
    // ScaledInteger in [-10000, 10000] -> range = 20000 -> bitWidth = 15 bits, scale = 0.01
    // Integer cartesianInvalidState in [0, 2] -> range = 2 -> bitWidth = 2 bits
    LsbBitPacker bx, by, bz, bInv;
    for (size_t i = 0; i < pts.size(); ++i) {
        int64_t qx = std::clamp<int64_t>(std::llround(pts[i].x * 100.0), -10000, 10000);
        int64_t qy = std::clamp<int64_t>(std::llround(pts[i].y * 100.0), -10000, 10000);
        int64_t qz = std::clamp<int64_t>(std::llround(pts[i].z * 100.0), -10000, 10000);
        bx.Put(static_cast<uint64_t>(qx - (-10000)), 15);
        by.Put(static_cast<uint64_t>(qy - (-10000)), 15);
        bz.Put(static_cast<uint64_t>(qz - (-10000)), 15);
        bInv.Put((i < invalidFlags.size() && invalidFlags[i] != 0) ? 1ULL : 0ULL, 2);
    }

    ByteWriter pkt;
    pkt.U8(1);
    pkt.U8(0);
    pkt.U16LE(0);
    pkt.U16LE(4); // 4 bytestreams
    pkt.U16LE(static_cast<uint16_t>(bx.buf.size()));
    pkt.U16LE(static_cast<uint16_t>(by.buf.size()));
    pkt.U16LE(static_cast<uint16_t>(bz.buf.size()));
    pkt.U16LE(static_cast<uint16_t>(bInv.buf.size()));
    pkt.Bytes(bx.buf.data(), bx.buf.size());
    pkt.Bytes(by.buf.data(), by.buf.size());
    pkt.Bytes(bz.buf.data(), bz.buf.size());
    pkt.Bytes(bInv.buf.data(), bInv.buf.size());
    pkt.Pad4();
    uint16_t lenMinus1 = static_cast<uint16_t>(pkt.buf.size() - 1);
    pkt.buf[2] = static_cast<uint8_t>(lenMinus1 & 0xFFu);
    pkt.buf[3] = static_cast<uint8_t>((lenMinus1 >> 8) & 0xFFu);

    ByteWriter cv;
    cv.U8(1);
    for (int i = 0; i < 7; ++i) cv.U8(0);
    cv.U64LE(32ULL + pkt.buf.size());
    cv.U64LE(80ULL);
    cv.U64LE(0ULL);
    cv.Bytes(pkt.buf.data(), pkt.buf.size());

    char xmlBuf[2048];
    std::snprintf(xmlBuf, sizeof(xmlBuf),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<e57Root type=\"Structure\" xmlns=\"http://www.astm.org/COMMIT/E57/2010-e57-v1.0\">\n"
        "  <data3D type=\"Vector\" allowHeterogeneousChildren=\"1\">\n"
        "    <vectorChild type=\"Structure\">\n"
        "      <pose type=\"Structure\">\n"
        "        <rotation type=\"Structure\"><w type=\"Float\">1.0</w><x type=\"Float\"/></rotation>\n"
        "        <translation type=\"Structure\"><x type=\"Float\">%.2f</x><y type=\"Float\">%.2f</y><z type=\"Float\">%.2f</z></translation>\n"
        "      </pose>\n"
        "      <points type=\"CompressedVector\" fileOffset=\"48\" recordCount=\"%zu\">\n"
        "        <prototype type=\"Structure\">\n"
        "          <cartesianX type=\"ScaledInteger\" minimum=\"-10000\" maximum=\"10000\" scale=\"0.01\" offset=\"0\"/>\n"
        "          <cartesianY type=\"ScaledInteger\" minimum=\"-10000\" maximum=\"10000\" scale=\"0.01\" offset=\"0\"/>\n"
        "          <cartesianZ type=\"ScaledInteger\" minimum=\"-10000\" maximum=\"10000\" scale=\"0.01\" offset=\"0\"/>\n"
        "          <cartesianInvalidState type=\"Integer\" minimum=\"0\" maximum=\"2\"/>\n"
        "        </prototype>\n"
        "      </points>\n"
        "    </vectorChild>\n"
        "  </data3D>\n"
        "</e57Root>\n",
        transX, transY, transZ, pts.size());
    return FinalizeE57LogicalToPhysicalPages(cv.buf, std::string(xmlBuf));
}

// Builds a GoldSrc v10 StudioMDL (.mdl) with 2 hierarchical mstudiobone_t bones and per-vertex bone skinning.
static std::vector<uint8_t> BuildSyntheticGoldSrcMdl10(float bone1OffsetX) {
    std::vector<uint8_t> buf(832, 0);
    auto wrU32 = [&](size_t off, uint32_t v) { std::memcpy(buf.data() + off, &v, 4); };
    auto wrI32 = [&](size_t off, int32_t v)  { std::memcpy(buf.data() + off, &v, 4); };
    auto wrI16 = [&](size_t off, int16_t v)  { std::memcpy(buf.data() + off, &v, 2); };
    auto wrU16 = [&](size_t off, uint16_t v) { std::memcpy(buf.data() + off, &v, 2); };
    auto wrF32 = [&](size_t off, float v)    { std::memcpy(buf.data() + off, &v, 4); };

    std::memcpy(buf.data(), "IDST", 4);
    wrU32(4, 10);   // version 10 (GoldSrc)
    wrU32(72, static_cast<uint32_t>(buf.size()));
    wrU32(140, 2);  // numBones
    wrU32(144, 244);// boneIndex
    wrU32(204, 1);  // numBodyparts
    wrU32(208, 468);// bodypartIndex (244 + 2 * 112)

    // Bone 0 at 244: parent = -1, pos = (0, 0, 10)
    wrI32(244 + 32, -1);
    wrF32(244 + 64, 0.0f); wrF32(244 + 68, 0.0f); wrF32(244 + 72, 10.0f);
    // Bone 1 at 356: parent = 0, pos = (bone1OffsetX, 0, 0)
    wrI32(356 + 32, 0);
    wrF32(356 + 64, bone1OffsetX); wrF32(356 + 68, 0.0f); wrF32(356 + 72, 0.0f);

    // Bodypart at 468 (76B)
    wrU32(468 + 64, 1);   // numModels
    wrU32(468 + 72, 544); // modelIndex

    // Submodel at 544 (112B)
    wrU32(544 + 72, 1);   // numMesh
    wrU32(544 + 76, 656); // meshIndex
    wrU32(544 + 80, 5);   // numVerts
    wrU32(544 + 84, 676); // vertInfoIndex
    wrU32(544 + 88, 684); // vertIndex

    // Mesh at 656 (20B)
    wrU32(656 + 4, 744);  // triIndex

    // vertInfo[5] at 676: base 4 verts on bone 0, apex vert 4 on bone 1
    buf[676] = 0; buf[677] = 0; buf[678] = 0; buf[679] = 0; buf[680] = 1;

    // 5 local vertices at 684 (60B)
    const float vLocal[5][3] = {
        { -2.0f, -2.0f, 0.0f }, {  2.0f, -2.0f, 0.0f },
        {  2.0f,  2.0f, 0.0f }, { -2.0f,  2.0f, 0.0f },
        {  0.0f,  0.0f, 5.0f }
    };
    for (size_t i = 0; i < 5; ++i) {
        wrF32(684 + i * 12 + 0, vLocal[i][0]);
        wrF32(684 + i * 12 + 4, vLocal[i][1]);
        wrF32(684 + i * 12 + 8, vLocal[i][2]);
    }

    // triverts at 744: fan -4 (0,3,2,1 -> 2 tris) + fan -5 (4,0,1,2,3 -> 3 tris) + strip 3 (4,3,0 -> 1 tri) + 0
    size_t tp = 744;
    auto emitCmd = [&](int16_t cmd, std::initializer_list<uint16_t> idxs) {
        wrI16(tp, cmd); tp += 2;
        for (uint16_t vi : idxs) {
            wrU16(tp + 0, vi); wrU16(tp + 2, 0); wrU16(tp + 4, 0); wrU16(tp + 6, 0);
            tp += 8;
        }
    };
    emitCmd(-4, { 0, 3, 2, 1 });
    emitCmd(-6, { 4, 0, 1, 2, 3, 0 });
    wrI16(tp, 0);
    return buf;
}

// Builds matched Valve Source Engine v48 (.mdl + .vvd with fixup table + .dx90.vtx) byte buffers.
struct SourceV48Buffers {
    std::vector<uint8_t> mdl;
    std::vector<uint8_t> vvd;
    std::vector<uint8_t> vtx;
};

static SourceV48Buffers BuildSyntheticSourceV48Set(
    const std::vector<Vec3f>& verts,
    const std::vector<std::array<uint16_t, 3>>& tris,
    uint32_t mdlCheck,
    uint32_t vvdCheck,
    uint32_t vtxCheck)
{
    SourceV48Buffers out;
    const int32_t nV = static_cast<int32_t>(verts.size());
    const int32_t nI = static_cast<int32_t>(tris.size() * 3);

    // 1. .mdl (968 bytes: header + 2 bones + 1 bodypart + 1 model + 1 mesh)
    out.mdl.assign(968, 0);
    auto mdlU32 = [&](size_t off, uint32_t v) { std::memcpy(out.mdl.data() + off, &v, 4); };
    auto mdlI32 = [&](size_t off, int32_t v)  { std::memcpy(out.mdl.data() + off, &v, 4); };
    auto mdlF32 = [&](size_t off, float v)    { std::memcpy(out.mdl.data() + off, &v, 4); };
    std::memcpy(out.mdl.data(), "IDST", 4);
    mdlU32(4, 48);
    mdlU32(8, mdlCheck);
    mdlU32(76, static_cast<uint32_t>(out.mdl.size()));
    mdlU32(156, 2);   // numBones
    mdlU32(160, 256); // boneIdx
    mdlI32(232, 1);   // numBodyParts
    mdlI32(236, 688); // bodyPartOffset (256 + 2 * 216)

    // 2 mstudiobone_t (216B each at 256)
    mdlI32(256 + 4, -1); mdlF32(256 + 56, 1.0f);
    mdlI32(472 + 4,  0); mdlF32(472 + 40, 14.0f); mdlF32(472 + 56, 1.0f);

    // mstudiobodyparts_t at 688 (16B) -> mstudiomodel_t at 704 (148B) -> mstudiomesh_t at 852 (116B)
    mdlI32(688 + 4,  1);
    mdlI32(688 + 12, 16);
    mdlI32(704 + 72, 1);
    mdlI32(704 + 76, 148);
    mdlI32(704 + 80, nV);
    mdlI32(704 + 84, 0);
    mdlI32(852 + 8,  nV);
    mdlI32(852 + 12, 0);

    // 2. .vvd ("IDSV" v4 with 2-entry vertexFileFixup_t table swapping first 2 and remaining vertices)
    int32_t split = std::min<int32_t>(2, nV);
    int32_t rem   = nV - split;
    out.vvd.assign(static_cast<size_t>(88 + nV * 48), 0);
    auto vvdI32 = [&](size_t off, int32_t v)  { std::memcpy(out.vvd.data() + off, &v, 4); };
    auto vvdU32 = [&](size_t off, uint32_t v) { std::memcpy(out.vvd.data() + off, &v, 4); };
    auto vvdF32 = [&](size_t off, float v)    { std::memcpy(out.vvd.data() + off, &v, 4); };
    std::memcpy(out.vvd.data(), "IDSV", 4);
    vvdI32(4, 4);
    vvdU32(8, vvdCheck);
    vvdI32(12, 1);  // numLODs
    vvdI32(16, nV); // numLODVerts[0]
    vvdI32(48, 2);  // numFixups
    vvdI32(52, 64); // fixupTableStart
    vvdI32(56, 88); // vertexDataStart
    // Fixup 0: lod=0, src=rem, cnt=split -> logical [0..split-1]
    vvdI32(64 + 0, 0); vvdI32(64 + 4, rem); vvdI32(64 + 8, split);
    // Fixup 1: lod=0, src=0,   cnt=rem   -> logical [split..nV-1]
    vvdI32(76 + 0, 0); vvdI32(76 + 4, 0);   vvdI32(76 + 8, rem);

    auto putPhysVvdVert = [&](int32_t physIdx, Vec3f p) {
        size_t vo = 88 + static_cast<size_t>(physIdx) * 48;
        vvdF32(vo + 0, 1.0f);
        out.vvd[vo + 15] = 1;
        vvdF32(vo + 16, p.x);
        vvdF32(vo + 20, p.y);
        vvdF32(vo + 24, p.z);
        vvdF32(vo + 36, 1.0f);
    };
    for (int32_t i = 0; i < rem; ++i)   putPhysVvdVert(i, verts[static_cast<size_t>(split + i)]);
    for (int32_t i = 0; i < split; ++i) putPhysVvdVert(rem + i, verts[static_cast<size_t>(i)]);

    // 3. .dx90.vtx (OptimizedModel::FileHeader_t v7, packed 1-byte structs)
    ByteWriter vw;
    vw.I32LE(7);          // version
    vw.I32LE(16);         // vertCacheSize
    vw.U16LE(3);          // maxBonesPerStrip
    vw.U16LE(3);          // maxBonesPerTri
    vw.I32LE(3);          // maxBonesPerVert
    vw.U32LE(vtxCheck);   // checkSum (+16)
    vw.I32LE(1);          // numLODs
    vw.I32LE(0);          // materialReplacementListOffset
    vw.I32LE(1);          // numBodyParts (+28)
    vw.I32LE(36);         // bodyPartOffset (+32)

    // BodyPartHeader_t (8B at 36) -> ModelHeader_t (8B at 44) -> ModelLODHeader_t (12B at 52) -> MeshHeader_t (9B at 64)
    vw.I32LE(1); vw.I32LE(8);
    vw.I32LE(1); vw.I32LE(8);
    vw.I32LE(1); vw.I32LE(12); vw.F32LE(0.0f);
    vw.I32LE(1); vw.I32LE(9); vw.U8(0);

    // StripGroupHeader_t (25B at 73)
    int32_t vertRel  = 25;
    int32_t idxRel   = vertRel + nV * 9;
    int32_t stripRel = idxRel  + nI * 2;
    vw.I32LE(nV);       vw.I32LE(vertRel);
    vw.I32LE(nI);       vw.I32LE(idxRel);
    vw.I32LE(1);        vw.I32LE(stripRel);
    vw.U8(0);

    // Vertex_t[nV] (9B each)
    for (int32_t v = 0; v < nV; ++v) {
        vw.U8(0); vw.U8(0); vw.U8(0); // boneWeightIdx
        vw.U8(1);                     // numBones
        vw.U16LE(static_cast<uint16_t>(v)); // origMeshVertID (+4)
        vw.U8(0); vw.U8(0); vw.U8(0); // boneID
    }
    // uint16_t indices[nI]
    for (const auto& t : tris) {
        vw.U16LE(t[0]); vw.U16LE(t[1]); vw.U16LE(t[2]);
    }
    // StripHeader_t (27B)
    vw.I32LE(nI); // numIndices
    vw.I32LE(0);  // indexOffset
    vw.I32LE(nV); // numVerts
    vw.I32LE(0);  // vertOffset
    vw.U16LE(1);  // numBones
    vw.U8(0x01);  // STRIP_IS_TRILIST (+18)
    vw.I32LE(0);  // numBoneStateChanges
    vw.I32LE(0);  // boneStateChangeOffset

    out.vtx = std::move(vw.buf);
    return out;
}

// Builds an OpenCTM v5 RAW (.ctm) file.
static std::vector<uint8_t> BuildSyntheticCtmRaw(
    const std::vector<Vec3f>& verts,
    const std::vector<std::array<uint32_t, 3>>& tris)
{
    ByteWriter w;
    w.Bytes("OCTM", 4);
    w.U32LE(5); // v5
    w.Bytes("RAW\0", 4);
    w.U32LE(static_cast<uint32_t>(verts.size()));
    w.U32LE(static_cast<uint32_t>(tris.size()));
    w.U32LE(0); // uvMapCount
    w.U32LE(0); // attrMapCount
    w.U32LE(0); // flags
    w.U32LE(0); // commentLength
    w.Bytes("INDX", 4);
    for (const auto& t : tris) { w.U32LE(t[0]); w.U32LE(t[1]); w.U32LE(t[2]); }
    w.Bytes("VERT", 4);
    for (const auto& v : verts) { w.F32LE(v.x); w.F32LE(v.y); w.F32LE(v.z); }
    return w.buf;
}

// Builds a LAS 1.2 (.laz) header + point records (ptFmt < 128 = uncompressed, ptFmt >= 128 = LAZ compressed).
static std::vector<uint8_t> BuildSyntheticLasOrLaz(const std::vector<Vec3f>& pts, uint8_t ptFmt) {
    std::vector<uint8_t> hdr(227, 0);
    std::memcpy(hdr.data(), "LASF", 4);
    hdr[24] = 1; hdr[25] = 2; // v1.2
    uint16_t hdrSz = 227;     std::memcpy(hdr.data() + 94, &hdrSz, 2);
    uint32_t ptOff = 227;     std::memcpy(hdr.data() + 96, &ptOff, 4);
    hdr[104] = ptFmt;
    uint16_t recLen = 20;     std::memcpy(hdr.data() + 105, &recLen, 2);
    uint32_t nPts = static_cast<uint32_t>(pts.size());
    std::memcpy(hdr.data() + 107, &nPts, 4);
    double sc = 0.01, zero = 0.0;
    std::memcpy(hdr.data() + 131, &sc, 8);
    std::memcpy(hdr.data() + 139, &sc, 8);
    std::memcpy(hdr.data() + 147, &sc, 8);
    std::memcpy(hdr.data() + 155, &zero, 8);
    std::memcpy(hdr.data() + 163, &zero, 8);
    std::memcpy(hdr.data() + 171, &zero, 8);

    ByteWriter w;
    w.Bytes(hdr.data(), hdr.size());
    for (const auto& p : pts) {
        int32_t ix = static_cast<int32_t>(std::llround(p.x * 100.0));
        int32_t iy = static_cast<int32_t>(std::llround(p.y * 100.0));
        int32_t iz = static_cast<int32_t>(std::llround(p.z * 100.0));
        w.I32LE(ix); w.I32LE(iy); w.I32LE(iz);
        w.U64LE(0); // remaining 8 bytes of 20-byte Format 0 record
    }
    return w.buf;
}

static std::string TfBase64Encode(const uint8_t* data, size_t len) {
    static constexpr char kLut[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b0 = data[i];
        uint32_t b1 = (i + 1 < len) ? data[i + 1] : 0u;
        uint32_t b2 = (i + 2 < len) ? data[i + 2] : 0u;
        uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(kLut[(triple >> 18) & 0x3Fu]);
        out.push_back(kLut[(triple >> 12) & 0x3Fu]);
        out.push_back((i + 1 < len) ? kLut[(triple >> 6) & 0x3Fu] : '=');
        out.push_back((i + 2 < len) ? kLut[triple & 0x3Fu] : '=');
    }
    return out;
}

static uint32_t TfCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static std::vector<uint8_t> TfDeflateFixedHuffman(const uint8_t* src, size_t len) {
    std::vector<uint8_t> out;
    out.reserve(len + 16);
    uint8_t curByte = 0;
    int bitPos = 0;
    auto putBit = [&](uint32_t b) {
        if (b & 1u) curByte |= static_cast<uint8_t>(1u << bitPos);
        if (++bitPos == 8) {
            out.push_back(curByte);
            curByte = 0;
            bitPos = 0;
        }
    };
    auto putMsb = [&](uint32_t code, int nBits) {
        for (int i = nBits - 1; i >= 0; --i) {
            putBit((code >> i) & 1u);
        }
    };
    // BFINAL = 1, BTYPE = 01 (Fixed Huffman)
    putBit(1);
    putBit(1);
    putBit(0);
    for (size_t i = 0; i < len; ++i) {
        uint32_t sym = src[i];
        if (sym <= 143u) putMsb(0x30u + sym, 8);
        else             putMsb(0x190u + (sym - 144u), 9);
    }
    putMsb(0x00u, 7); // End-of-block (256)
    if (bitPos > 0) out.push_back(curByte);
    return out;
}

struct TestZipFileEntry {
    std::string path;
    std::string content;
    bool deflate = false;
};

static std::vector<uint8_t> BuildTestZipArchive(const std::vector<TestZipFileEntry>& files) {
    ByteWriter w;
    struct CdRec {
        std::string name;
        uint16_t method = 0;
        uint32_t crc = 0;
        uint32_t compSz = 0;
        uint32_t uncompSz = 0;
        uint32_t localOff = 0;
    };
    std::vector<CdRec> cds;

    for (const auto& f : files) {
        const auto* rawPtr = reinterpret_cast<const uint8_t*>(f.content.data());
        size_t rawLen = f.content.size();
        uint32_t crc = TfCrc32(rawPtr, rawLen);
        std::vector<uint8_t> payload;
        uint16_t method = 0;
        if (f.deflate) {
            method = 8;
            payload = TfDeflateFixedHuffman(rawPtr, rawLen);
        } else {
            payload.assign(rawPtr, rawPtr + rawLen);
        }

        CdRec cd;
        cd.name = f.path;
        cd.method = method;
        cd.crc = crc;
        cd.compSz = static_cast<uint32_t>(payload.size());
        cd.uncompSz = static_cast<uint32_t>(rawLen);
        cd.localOff = static_cast<uint32_t>(w.buf.size());
        cds.push_back(cd);

        w.U32LE(0x04034B50u);
        w.U16LE(20); // version needed
        w.U16LE(0);  // flags
        w.U16LE(method);
        w.U16LE(0); w.U16LE(0); // time/date
        w.U32LE(crc);
        w.U32LE(cd.compSz);
        w.U32LE(cd.uncompSz);
        w.U16LE(static_cast<uint16_t>(f.path.size()));
        w.U16LE(0); // extra len
        w.Bytes(f.path.data(), f.path.size());
        w.Bytes(payload.data(), payload.size());
    }

    uint32_t cdStart = static_cast<uint32_t>(w.buf.size());
    for (const auto& cd : cds) {
        w.U32LE(0x02014B50u);
        w.U16LE(20);
        w.U16LE(20);
        w.U16LE(0);
        w.U16LE(cd.method);
        w.U16LE(0); w.U16LE(0);
        w.U32LE(cd.crc);
        w.U32LE(cd.compSz);
        w.U32LE(cd.uncompSz);
        w.U16LE(static_cast<uint16_t>(cd.name.size()));
        w.U16LE(0); w.U16LE(0); w.U16LE(0); w.U16LE(0);
        w.U32LE(0);
        w.U32LE(cd.localOff);
        w.Bytes(cd.name.data(), cd.name.size());
    }
    uint32_t cdSize = static_cast<uint32_t>(w.buf.size()) - cdStart;

    w.U32LE(0x06054B50u);
    w.U16LE(0); w.U16LE(0);
    w.U16LE(static_cast<uint16_t>(cds.size()));
    w.U16LE(static_cast<uint16_t>(cds.size()));
    w.U32LE(cdSize);
    w.U32LE(cdStart);
    w.U16LE(0);
    return w.buf;
}

// Builds a glTF 2.0 binary (.glb / .vrm) whose BIN chunk starts with a 32-byte non-geometry PNG header
// + USHORT indices first, followed by byteStride=24 interleaved vertex records, instantiated via a 2-node hierarchy.
static std::vector<uint8_t> BuildStridedGlb20(
    const std::vector<Vec3f>& verts,
    const std::vector<std::array<uint16_t, 3>>& tris,
    float scaleX, float scaleY, float scaleZ,
    float transX, float transY, float transZ)
{
    ByteWriter bin;
    // 32 bytes of non-geometry PNG header / NaN float traps at offset 0 (defeats raw BIN float scrapers!)
    const uint8_t fakePng[32] = {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
        0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0xC0, 0x7F, 0xDE, 0xAD, 0xBE, 0xEF,
        0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
    };
    bin.Bytes(fakePng, 32);

    size_t idxByteOff = bin.buf.size();
    for (const auto& t : tris) {
        bin.U16LE(t[0]);
        bin.U16LE(t[1]);
        bin.U16LE(t[2]);
    }
    size_t idxByteLen = bin.buf.size() - idxByteOff;
    bin.Pad4();

    // Interleaved 24-byte vertex records: 12B POSITION + 12B dummy normal/UV attributes
    size_t posViewOff = bin.buf.size();
    const size_t stride = 24;
    for (const auto& v : verts) {
        bin.F32LE(v.x);
        bin.F32LE(v.y);
        bin.F32LE(v.z);
        bin.F32LE(99999.0f);
        bin.F32LE(-88888.0f);
        bin.F32LE(77777.0f);
    }
    size_t posViewLen = bin.buf.size() - posViewOff;
    bin.Pad4();

    char jsonBuf[2048];
    std::snprintf(jsonBuf, sizeof(jsonBuf),
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"scene\":0,"
        "\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":["
          "{\"translation\":[%.3f,%.3f,%.3f],\"children\":[1]},"
          "{\"scale\":[%.3f,%.3f,%.3f],\"mesh\":0}"
        "],"
        "\"buffers\":[{\"byteLength\":%zu}],"
        "\"bufferViews\":["
          "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu},"
          "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"byteStride\":%zu}"
        "],"
        "\"accessors\":["
          "{\"bufferView\":0,\"byteOffset\":0,\"componentType\":5123,\"count\":%zu,\"type\":\"SCALAR\"},"
          "{\"bufferView\":1,\"byteOffset\":0,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC3\"}"
        "],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":1},\"indices\":0,\"mode\":4}]}]"
        "}",
        transX, transY, transZ,
        scaleX, scaleY, scaleZ,
        bin.buf.size(),
        idxByteOff, idxByteLen,
        posViewOff, posViewLen, stride,
        tris.size() * 3,
        verts.size());

    std::string jsonStr(jsonBuf);
    while ((jsonStr.size() & 3u) != 0) jsonStr.push_back(' ');

    ByteWriter glb;
    uint32_t totalLen = static_cast<uint32_t>(12 + 8 + jsonStr.size() + 8 + bin.buf.size());
    glb.U32LE(0x46546C67u); // "glTF"
    glb.U32LE(2u);
    glb.U32LE(totalLen);
    glb.U32LE(static_cast<uint32_t>(jsonStr.size()));
    glb.U32LE(0x4E4F534Au); // "JSON"
    glb.Bytes(jsonStr.data(), jsonStr.size());
    glb.U32LE(static_cast<uint32_t>(bin.buf.size()));
    glb.U32LE(0x004E4942u); // "BIN\0"
    glb.Bytes(bin.buf.data(), bin.buf.size());
    return glb.buf;
}

struct IgesDocBuilder {
    std::vector<std::string> dLines;
    std::vector<std::string> pLines;

    static std::string Pad80(std::string s, char sec, int seq) {
        if (s.size() > 72) s.resize(72);
        while (s.size() < 72) s.push_back(' ');
        char tail[16];
        std::snprintf(tail, sizeof(tail), "%c%7d", sec, seq);
        s.append(tail, 8);
        return s;
    }

    int AddEntity(int entType, std::string paramBody, int xformDe = 0, int formNum = 0) {
        int deSeq = static_cast<int>(dLines.size()) + 1;
        int pStart = static_cast<int>(pLines.size()) + 1;
        if (paramBody.empty() || paramBody.back() != ';') paramBody.push_back(';');

        int lineCnt = 0;
        for (size_t pos = 0; pos < paramBody.size(); pos += 64) {
            std::string chunk = paramBody.substr(pos, std::min<size_t>(64, paramBody.size() - pos));
            while (chunk.size() < 64) chunk.push_back(' ');
            char ptrPart[16];
            std::snprintf(ptrPart, sizeof(ptrPart), "%8d", deSeq);
            chunk.append(ptrPart, 8);
            int pSeq = static_cast<int>(pLines.size()) + 1;
            pLines.push_back(Pad80(chunk, 'P', pSeq));
            ++lineCnt;
        }

        char d1[80], d2[80];
        std::snprintf(d1, sizeof(d1), "%8d%8d%8d%8d%8d%8d%8d%8d00000000",
                      entType, pStart, 0, 0, 0, 0, xformDe, 0);
        std::snprintf(d2, sizeof(d2), "%8d%8d%8d%8d%8d%8s%8s%8s%8d",
                      entType, 0, 0, lineCnt, formNum, "", "", "", 0);
        dLines.push_back(Pad80(std::string(d1, 72), 'D', deSeq));
        dLines.push_back(Pad80(std::string(d2, 72), 'D', deSeq + 1));
        return deSeq;
    }

    std::string Finish() const {
        std::string out;
        out += Pad80("Synthetic IGES 5.3 Test Model", 'S', 1) + "\n";
        out += Pad80("1H,,1H;,7HTestCAD,11Htest53.iges,9HExplorer2,9H5.3,32,38,6,308,15,,1.0;", 'G', 1) + "\n";
        for (const auto& d : dLines) out += d + "\n";
        for (const auto& p : pLines) out += p + "\n";
        char term[80];
        std::snprintf(term, sizeof(term), "S%7dG%7dD%7zuP%7zu", 1, 1, dLines.size(), pLines.size());
        out += Pad80(term, 'T', 1) + "\n";
        return out;
    }
};

// Wraps raw bytes in a standard RFC 1952 Gzip stream (0x1F 0x8B + Fixed-Huffman Deflate + CRC32 + ISIZE).
static std::vector<uint8_t> TfGzipCompress(const void* data, size_t len) {
    const auto* ptr = static_cast<const uint8_t*>(data);
    ByteWriter w;
    const uint8_t gzHdr[10] = { 0x1F, 0x8B, 0x08, 0x00, 0, 0, 0, 0, 0x00, 0x03 };
    w.Bytes(gzHdr, 10);
    auto deflated = TfDeflateFixedHuffman(ptr, len);
    w.Bytes(deflated.data(), deflated.size());
    w.U32LE(TfCrc32(ptr, len));
    w.U32LE(static_cast<uint32_t>(len));
    return w.buf;
}

// Greedy LibLZF compressor emitting both literal runs (ctrl < 32) and back-references (ctrl >= 32, incl. len >= 9).
static std::vector<uint8_t> TfLzfCompress(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out;
    out.reserve(raw.size() + 32);
    std::vector<uint8_t> lit;
    auto flushLit = [&]() {
        size_t pos = 0;
        while (pos < lit.size()) {
            size_t chunk = std::min<size_t>(32, lit.size() - pos);
            out.push_back(static_cast<uint8_t>(chunk - 1));
            out.insert(out.end(), lit.begin() + static_cast<std::ptrdiff_t>(pos),
                       lit.begin() + static_cast<std::ptrdiff_t>(pos + chunk));
            pos += chunk;
        }
        lit.clear();
    };

    size_t ip = 0;
    const size_t n = raw.size();
    while (ip < n) {
        size_t bestLen = 0;
        size_t bestOff = 0;
        size_t maxBack = std::min<size_t>(ip, 8192);
        for (size_t off = 1; off <= maxBack; ++off) {
            size_t ref = ip - off;
            size_t maxL = std::min<size_t>(264, n - ip);
            size_t ml = 0;
            while (ml < maxL && raw[ref + ml] == raw[ip + ml]) ++ml;
            if (ml >= 3 && ml > bestLen) {
                bestLen = ml;
                bestOff = off;
                if (bestLen == 264) break;
            }
        }
        if (bestLen >= 3) {
            flushLit();
            uint32_t off0 = static_cast<uint32_t>(bestOff - 1);
            uint32_t lenCode = static_cast<uint32_t>(bestLen - 2);
            if (lenCode < 7u) {
                out.push_back(static_cast<uint8_t>((lenCode << 5) | ((off0 >> 8) & 0x1Fu)));
                out.push_back(static_cast<uint8_t>(off0 & 0xFFu));
            } else {
                out.push_back(static_cast<uint8_t>((7u << 5) | ((off0 >> 8) & 0x1Fu)));
                out.push_back(static_cast<uint8_t>(lenCode - 7u));
                out.push_back(static_cast<uint8_t>(off0 & 0xFFu));
            }
            ip += bestLen;
        } else {
            lit.push_back(raw[ip++]);
        }
    }
    flushLit();
    return out;
}

// Builds a PCL v0.7 DATA binary_compressed .pcd with 16-byte PointXYZRGB stride in official SoA column-major order.
static std::vector<uint8_t> BuildPcdBinaryCompressedSoA(
    const std::vector<Vec3f>& pts,
    const std::vector<uint32_t>& rgbs,
    size_t* outCompSize = nullptr,
    size_t* outUncompSize = nullptr)
{
    const size_t n = pts.size();
    ByteWriter soa;
    // Column 0: all X float32s; Column 1: all Y float32s; Column 2: all Z float32s; Column 3: all RGB packed float32s
    for (size_t i = 0; i < n; ++i) soa.F32LE(pts[i].x);
    for (size_t i = 0; i < n; ++i) soa.F32LE(pts[i].y);
    for (size_t i = 0; i < n; ++i) soa.F32LE(pts[i].z);
    for (size_t i = 0; i < n; ++i) {
        uint32_t c = (i < rgbs.size()) ? rgbs[i] : 0x0033AAFFu;
        soa.U32LE(c);
    }

    auto lzf = TfLzfCompress(soa.buf);
    if (outCompSize) *outCompSize = lzf.size();
    if (outUncompSize) *outUncompSize = soa.buf.size();

    char hdr[512];
    std::snprintf(hdr, sizeof(hdr),
        "# .PCD v0.7 - Point Cloud Data file format\n"
        "VERSION 0.7\n"
        "FIELDS x y z rgb\n"
        "SIZE 4 4 4 4\n"
        "TYPE F F F F\n"
        "COUNT 1 1 1 1\n"
        "WIDTH %zu\n"
        "HEIGHT 1\n"
        "VIEWPOINT 0 0 0 1 0 0 0\n"
        "POINTS %zu\n"
        "DATA binary_compressed\n",
        n, n);

    ByteWriter out;
    out.Bytes(hdr, std::strlen(hdr));
    out.U32LE(static_cast<uint32_t>(lzf.size()));
    out.U32LE(static_cast<uint32_t>(soa.buf.size()));
    out.Bytes(lzf.data(), lzf.size());
    return out.buf;
}

// Builds a Gmsh MSH 4.1 Binary ($MeshFormat 4.1 1 8) mesh with sparse 64-bit node tags.
static std::vector<uint8_t> BuildGmsh41BinaryVolumetric(
    const std::vector<std::pair<uint64_t, Vec3f>>& taggedNodes,
    int32_t elmType,
    const std::vector<std::vector<uint64_t>>& elemConn)
{
    ByteWriter w;
    const char* fmtHdr = "$MeshFormat\n4.1 1 8\n";
    w.Bytes(fmtHdr, std::strlen(fmtHdr));
    w.I32LE(1); // endianness marker
    const char* fmtEnd = "\n$EndMeshFormat\n$Nodes\n";
    w.Bytes(fmtEnd, std::strlen(fmtEnd));

    uint64_t minNodeTag = taggedNodes.empty() ? 1 : taggedNodes.front().first;
    uint64_t maxNodeTag = minNodeTag;
    for (const auto& nd : taggedNodes) {
        minNodeTag = std::min(minNodeTag, nd.first);
        maxNodeTag = std::max(maxNodeTag, nd.first);
    }
    w.U64LE(1);                  // numEntityBlocks
    w.U64LE(taggedNodes.size()); // numNodes
    w.U64LE(minNodeTag);
    w.U64LE(maxNodeTag);
    w.I32LE(3); // entityDim = 3
    w.I32LE(1); // entityTag = 1
    w.I32LE(0); // parametric = 0
    w.U64LE(taggedNodes.size());
    for (const auto& nd : taggedNodes) w.U64LE(nd.first);
    for (const auto& nd : taggedNodes) {
        w.F64LE(nd.second.x);
        w.F64LE(nd.second.y);
        w.F64LE(nd.second.z);
    }
    const char* nodesEnd = "\n$EndNodes\n$Elements\n";
    w.Bytes(nodesEnd, std::strlen(nodesEnd));

    w.U64LE(1);               // numEntityBlocks
    w.U64LE(elemConn.size()); // numElements
    w.U64LE(500);             // minElementTag
    w.U64LE(500 + elemConn.size() - 1);
    w.I32LE(3);               // entityDim = 3
    w.I32LE(1);               // entityTag = 1
    w.I32LE(elmType);         // elementType (e.g. 5 = hex8, 4 = tet4)
    w.U64LE(elemConn.size());
    for (size_t e = 0; e < elemConn.size(); ++e) {
        w.U64LE(500 + e);
        for (uint64_t nt : elemConn[e]) w.U64LE(nt);
    }
    const char* elemsEnd = "\n$EndElements\n";
    w.Bytes(elemsEnd, std::strlen(elemsEnd));
    return w.buf;
}

// Builds a genuine 1024-byte USGS 7.5-Minute ASCII DEM (Type A header + Type B profiles with Fortran D floats & -32767 void).
static std::string BuildUsgs75MinDemFixture() {
    std::string typeA(1024, ' ');
    auto putField = [&](size_t off, size_t width, const char* txt) {
        size_t len = std::min(width, std::strlen(txt));
        std::memcpy(typeA.data() + off + (width - len), txt, len);
    };
    const char* title = "MOUNT RAINIER WEST, WA - USGS 7.5 MINUTE DEM TEST";
    std::memcpy(typeA.data(), title, std::strlen(title));
    putField(144, 6, "1"); // DEM level 1
    putField(150, 6, "1"); // regular elevation pattern
    putField(156, 6, "1"); // UTM planimetric system
    putField(528, 6, "2"); // meters ground unit
    putField(534, 6, "2"); // meters elevation unit
    putField(540, 6, "4"); // 4-sided quadrangle

    const char* corners[8] = {
        "0.500000000000000D+06", "0.518000000000000D+07",
        "0.500000000000000D+06", "0.518005000000000D+07",
        "0.500050000000000D+06", "0.518005000000000D+07",
        "0.500050000000000D+06", "0.518000000000000D+07"
    };
    for (int i = 0; i < 8; ++i) putField(546 + static_cast<size_t>(i) * 24, 24, corners[i]);
    putField(738, 24, "0.100000000000000D+02");
    putField(762, 24, "0.950000000000000D+02");
    putField(810, 6,  "0");
    putField(816, 12, "0.100000D+02"); // dx = 10m
    putField(828, 12, "0.100000D+02"); // dy = 10m
    putField(840, 12, "0.100000D+01"); // dz = 1m
    putField(852, 6,  "1");            // 1 row of profiles
    putField(858, 6,  "6");            // 6 profiles (columns)

    std::string out = typeA;
    for (int col = 1; col <= 6; ++col) {
        char pHdr[256];
        std::snprintf(pHdr, sizeof(pHdr),
            "     1%6d     6     1 0.5000%d0000000000D+06 0.518000000000000D+07 0.000000D+00 0.100000D+02 0.950000D+02 ",
            col, col - 1);
        out += pHdr;
        for (int row = 0; row < 6; ++row) {
            int elev = (col == 3 && row == 3) ? -32767 : (15 + col * 8 + row * 6);
            char eBuf[16];
            std::snprintf(eBuf, sizeof(eBuf), "%6d", elev);
            out += eBuf;
        }
        out += "\n";
    }
    return out;
}

// ============================================================================
// Symmetric LASzip v2 Arithmetic Encoder (POINT10 v2 + Chunk Table) for LAZ Test Fixtures
// ============================================================================
namespace LazTestEnc {

static constexpr uint32_t kAcMinLen = 0x01000000u;
static constexpr uint32_t kAcMaxLen = 0xFFFFFFFFu;
static constexpr uint32_t kBmShift  = 13u;
static constexpr uint32_t kBmMaxCnt = 1u << kBmShift;
static constexpr uint32_t kDmShift  = 15u;
static constexpr uint32_t kDmMaxCnt = 1u << kDmShift;

struct BitModel {
    uint32_t updateCycle = 4, bitsUntilUpdate = 4;
    uint32_t bit0Prob = 1u << (kBmShift - 1);
    uint32_t bit0Count = 1, bitCount = 2;
    void Init() { bit0Count = 1; bitCount = 2; bit0Prob = 1u << (kBmShift - 1); updateCycle = bitsUntilUpdate = 4; }
    void Update() {
        if ((bitCount += updateCycle) > kBmMaxCnt) {
            bitCount = (bitCount + 1u) >> 1;
            bit0Count = (bit0Count + 1u) >> 1;
            if (bit0Count == bitCount) ++bitCount;
        }
        uint32_t scale = 0x80000000u / bitCount;
        bit0Prob = (bit0Count * scale) >> (31u - kBmShift);
        updateCycle = std::min<uint32_t>(64u, (5u * updateCycle) >> 2);
        bitsUntilUpdate = updateCycle;
    }
};

struct SymModel {
    uint32_t symbols = 0, totalCount = 0, updateCycle = 0, symbolsUntilUpdate = 0;
    std::vector<uint32_t> symbolCount, distribution;
    void Init(uint32_t n) {
        symbols = n;
        symbolCount.assign(n, 1u);
        distribution.assign(n, 0u);
        totalCount = 0;
        updateCycle = n;
        Update();
        symbolsUntilUpdate = updateCycle = (n + 6u) >> 1;
    }
    void Update() {
        if ((totalCount += updateCycle) > kDmMaxCnt) {
            totalCount = 0;
            for (uint32_t i = 0; i < symbols; ++i) totalCount += (symbolCount[i] = (symbolCount[i] + 1u) >> 1);
        }
        uint32_t sum = 0;
        uint32_t scale = 0x80000000u / totalCount;
        for (uint32_t k = 0; k < symbols; ++k) {
            distribution[k] = (scale * sum) >> (31u - kDmShift);
            sum += symbolCount[k];
        }
        updateCycle = (5u * updateCycle) >> 2;
        uint32_t maxCycle = std::min<uint32_t>(kDmMaxCnt >> 1, (symbols + 6u) << 3);
        if (updateCycle > maxCycle) updateCycle = maxCycle;
        symbolsUntilUpdate = updateCycle;
    }
};

struct ArithEncoder {
    std::vector<uint8_t> buf;
    uint32_t base = 0;
    uint32_t length = kAcMaxLen;

    void Init() {
        buf.clear();
        base = 0;
        length = kAcMaxLen;
    }
    void PropagateCarry() {
        if (buf.empty()) return;
        size_t i = buf.size() - 1;
        while (i > 0 && buf[i] == 0xFFu) {
            buf[i] = 0;
            --i;
        }
        ++buf[i];
    }
    void Renorm() {
        do {
            buf.push_back(static_cast<uint8_t>(base >> 24));
            base <<= 8;
        } while ((length <<= 8) < kAcMinLen);
    }
    void EncodeBit(BitModel& m, uint32_t sym) {
        uint32_t x = m.bit0Prob * (length >> kBmShift);
        if (sym == 0u) {
            length = x;
            ++m.bit0Count;
        } else {
            uint32_t initBase = base;
            base += x;
            length -= x;
            if (initBase > base) PropagateCarry();
        }
        if (length < kAcMinLen) Renorm();
        if (--m.bitsUntilUpdate == 0u) m.Update();
    }
    void EncodeSymbol(SymModel& m, uint32_t sym) {
        uint32_t initBase = base;
        if (sym == m.symbols - 1u) {
            uint32_t x = m.distribution[sym] * (length >> kDmShift);
            base += x;
            length -= x;
        } else {
            uint32_t x = m.distribution[sym] * (length >>= kDmShift);
            base += x;
            length = m.distribution[sym + 1u] * length - x;
        }
        if (initBase > base) PropagateCarry();
        if (length < kAcMinLen) Renorm();
        ++m.symbolCount[sym];
        if (--m.symbolsUntilUpdate == 0u) m.Update();
    }
    void WriteBits(uint32_t bits, uint32_t sym) {
        if (bits == 0u) return;
        if (bits > 19u) {
            WriteBits(19u, sym & ((1u << 19) - 1u));
            WriteBits(bits - 19u, sym >> 19);
            return;
        }
        uint32_t initBase = base;
        base += sym * (length >>= bits);
        if (initBase > base) PropagateCarry();
        if (length < kAcMinLen) Renorm();
    }
    void WriteInt(uint32_t v) {
        WriteBits(16u, v & 0xFFFFu);
        WriteBits(16u, v >> 16);
    }
    void Done() {
        uint32_t initBase = base;
        bool anotherByte = true;
        if (length > 2u * kAcMinLen) {
            base += kAcMinLen;
            length = kAcMinLen >> 1;
        } else {
            base += kAcMinLen >> 1;
            length = kAcMinLen >> 9;
            anotherByte = false;
        }
        if (initBase > base) PropagateCarry();
        Renorm();
        if (anotherByte) buf.push_back(0);
        buf.push_back(0);
        buf.push_back(0);
    }
};

struct IntCompressorEnc {
    uint32_t bits = 32, contexts = 1, bitsHigh = 8, corrBits = 32, k = 0;
    std::vector<SymModel> mBits;
    BitModel mCorrector0;
    std::vector<SymModel> mCorrector;

    void Setup(uint32_t numBits, uint32_t numContexts = 1, uint32_t highBits = 8) {
        bits = numBits; contexts = numContexts; bitsHigh = highBits; corrBits = numBits; k = 0;
        mBits.resize(contexts);
        mCorrector.resize(corrBits + 1);
    }
    void InitCompressor() {
        k = 0;
        for (uint32_t c = 0; c < contexts; ++c) mBits[c].Init(corrBits + 1u);
        mCorrector0.Init();
        for (uint32_t i = 1; i <= corrBits; ++i) {
            mCorrector[i].Init((i <= bitsHigh) ? (1u << i) : (1u << bitsHigh));
        }
    }
    uint32_t GetK() const { return k; }

    static uint32_t BitWidth32(uint32_t v) {
        uint32_t w = 0;
        while (v != 0u) { ++w; v >>= 1; }
        return w;
    }

    void Compress(ArithEncoder& enc, int32_t pred, int32_t realVal, uint32_t context = 0) {
        int32_t corr = static_cast<int32_t>(static_cast<uint32_t>(realVal) - static_cast<uint32_t>(pred));
        if (corr == 0) {
            k = 0;
            enc.EncodeSymbol(mBits[context], 0u);
            enc.EncodeBit(mCorrector0, 0u);
            return;
        }
        if (corr == 1) {
            k = 0;
            enc.EncodeSymbol(mBits[context], 0u);
            enc.EncodeBit(mCorrector0, 1u);
            return;
        }
        int64_t corr64 = static_cast<int64_t>(realVal) - static_cast<int64_t>(pred);
        if (corr64 > 1 && corr64 <= 0x7FFFFFFFLL) {
            uint32_t u = static_cast<uint32_t>(corr64 - 1);
            uint32_t kw = BitWidth32(u);
            if (kw <= 31u) {
                k = kw;
                enc.EncodeSymbol(mBits[context], k);
                if (k <= bitsHigh) {
                    enc.EncodeSymbol(mCorrector[k], u);
                } else {
                    uint32_t k1 = k - bitsHigh;
                    enc.WriteBits(k1, u & ((1u << k1) - 1u));
                    enc.EncodeSymbol(mCorrector[bitsHigh], u >> k1);
                }
                return;
            }
        } else if (corr64 < 0 && corr64 >= -0x7FFFFFFFLL) {
            uint32_t u = static_cast<uint32_t>(-corr64);
            uint32_t kw = BitWidth32(u);
            if (kw <= 31u) {
                k = kw;
                uint32_t c = ((1u << k) - 1u) - u;
                enc.EncodeSymbol(mBits[context], k);
                if (k <= bitsHigh) {
                    enc.EncodeSymbol(mCorrector[k], c);
                } else {
                    uint32_t k1 = k - bitsHigh;
                    enc.WriteBits(k1, c & ((1u << k1) - 1u));
                    enc.EncodeSymbol(mCorrector[bitsHigh], c >> k1);
                }
                return;
            }
        }
        k = 32u;
        enc.EncodeSymbol(mBits[context], 32u);
        enc.WriteInt(static_cast<uint32_t>(corr));
    }
};

struct StreamingMedian5 {
    int32_t values[5] = {};
    bool high = true;
    void Init() { for (int& v : values) v = 0; high = true; }
    int32_t Get() const { return values[2]; }
    void Add(int32_t v) {
        if (high) {
            if (v < values[2]) {
                values[4] = values[3]; values[3] = values[2];
                if (v < values[0]) { values[2] = values[1]; values[1] = values[0]; values[0] = v; }
                else if (v < values[1]) { values[2] = values[1]; values[1] = v; }
                else { values[2] = v; }
            } else {
                if (v < values[3]) { values[4] = values[3]; values[3] = v; }
                else { values[4] = v; }
                high = false;
            }
        } else {
            if (values[2] < v) {
                values[0] = values[1]; values[1] = values[2];
                if (values[4] < v) { values[2] = values[3]; values[3] = values[4]; values[4] = v; }
                else if (values[3] < v) { values[2] = values[3]; values[3] = v; }
                else { values[2] = v; }
            } else {
                if (values[1] < v) { values[0] = values[1]; values[1] = v; }
                else { values[0] = v; }
                high = true;
            }
        }
    }
};

static std::vector<uint8_t> EncodeLazPoint10v2Chunk(const Vec3f* pts, size_t count) {
    ByteWriter chunk;
    auto qCoord = [](float f) -> int32_t { return static_cast<int32_t>(std::llround(static_cast<double>(f) * 100.0)); };

    // 20-byte uncompressed seed point (return_number=1, number_of_returns=1 -> byte 14 = 0x09 -> m=0, l=0, n=1!)
    int32_t curX = qCoord(pts[0].x);
    int32_t curY = qCoord(pts[0].y);
    int32_t curZ = qCoord(pts[0].z);
    chunk.I32LE(curX);
    chunk.I32LE(curY);
    chunk.I32LE(curZ);
    chunk.U16LE(0);    // intensity = 0
    chunk.U8(0x09u);   // return_number=1, number_of_returns=1
    chunk.U8(2u);      // classification = ground
    chunk.U8(0u);      // scan_angle_rank
    chunk.U8(0u);      // user_data
    chunk.U16LE(1u);   // point_source_ID
    if (count <= 1) return chunk.buf;

    SymModel mChangedValues;
    mChangedValues.Init(64);
    IntCompressorEnc icDx, icDy, icZ;
    icDx.Setup(32, 2);  icDx.InitCompressor();
    icDy.Setup(32, 22); icDy.InitCompressor();
    icZ.Setup(32, 20);  icZ.InitCompressor();
    StreamingMedian5 medX, medY;
    medX.Init(); medY.Init();
    int32_t lastHeight0 = 0;

    ArithEncoder enc;
    enc.Init();
    for (size_t i = 1; i < count; ++i) {
        int32_t nx = qCoord(pts[i].x);
        int32_t ny = qCoord(pts[i].y);
        int32_t nz = qCoord(pts[i].z);

        // changedValues == 0 (all non-XYZ attributes match seed; n=1, r=1 -> m=0, l=0)
        enc.EncodeSymbol(mChangedValues, 0u);

        int32_t dx = nx - curX;
        icDx.Compress(enc, medX.Get(), dx, 1u);
        curX = nx;
        medX.Add(dx);

        uint32_t kBitsX = icDx.GetK();
        uint32_t ctxY = 1u + ((kBitsX < 20u) ? (kBitsX & ~1u) : 20u);
        int32_t dy = ny - curY;
        icDy.Compress(enc, medY.Get(), dy, ctxY);
        curY = ny;
        medY.Add(dy);

        uint32_t kBits = (icDx.GetK() + icDy.GetK()) / 2u;
        uint32_t ctxZ = 1u + ((kBits < 18u) ? (kBits & ~1u) : 18u);
        icZ.Compress(enc, lastHeight0, nz, ctxZ);
        lastHeight0 = nz;
    }
    enc.Done();
    chunk.Bytes(enc.buf.data(), enc.buf.size());
    return chunk.buf;
}

static std::vector<uint8_t> BuildLazPoint10v2Fixture(const std::vector<Vec3f>& pts, uint32_t chunkSize) {
    // 1. Build VLR 22204 ("laszip encoded") payload: 34B header + 1 * 6B item (POINT10, 20B, v2) = 40B
    ByteWriter vlrPay;
    vlrPay.U16LE(2u);         // compressor = 2 (POINTWISE_CHUNKED)
    vlrPay.U16LE(0u);         // coder = 0 (ARITHMETIC)
    vlrPay.U8(2); vlrPay.U8(2); vlrPay.U16LE(0); // version 2.2.0
    vlrPay.U32LE(0);          // options
    vlrPay.U32LE(chunkSize);  // chunk_size
    vlrPay.U64LE(0xFFFFFFFFFFFFFFFFULL); // num_special_evlrs = -1
    vlrPay.U64LE(0xFFFFFFFFFFFFFFFFULL); // offset_special_evlrs = -1
    vlrPay.U16LE(1u);         // num_items = 1
    vlrPay.U16LE(6u);         // item.type = 6 (POINT10)
    vlrPay.U16LE(20u);        // item.size = 20
    vlrPay.U16LE(2u);         // item.version = 2

    // 54-byte LAS VLR record header
    std::vector<uint8_t> vlrHdr(54, 0);
    std::memcpy(vlrHdr.data() + 2, "laszip encoded", 14);
    uint16_t recId = 22204u;
    uint16_t recLen = static_cast<uint16_t>(vlrPay.buf.size());
    std::memcpy(vlrHdr.data() + 18, &recId, 2);
    std::memcpy(vlrHdr.data() + 20, &recLen, 2);

    const uint16_t lasHdrSz = 227;
    const uint32_t ptDataOff = static_cast<uint32_t>(lasHdrSz + vlrHdr.size() + vlrPay.buf.size());

    std::vector<uint8_t> hdr(lasHdrSz, 0);
    std::memcpy(hdr.data(), "LASF", 4);
    hdr[24] = 1; hdr[25] = 2; // LAS 1.2
    std::memcpy(hdr.data() + 94, &lasHdrSz, 2);
    std::memcpy(hdr.data() + 96, &ptDataOff, 4);
    uint32_t numVlrs = 1;
    std::memcpy(hdr.data() + 100, &numVlrs, 4);
    hdr[104] = 128u + 0u; // compressed Point Format 0 (bit 7 set!)
    uint16_t ptRecLen = 20;
    std::memcpy(hdr.data() + 105, &ptRecLen, 2);
    uint32_t nPts32 = static_cast<uint32_t>(pts.size());
    std::memcpy(hdr.data() + 107, &nPts32, 4);
    double sc = 0.01, zero = 0.0;
    std::memcpy(hdr.data() + 131, &sc, 8);
    std::memcpy(hdr.data() + 139, &sc, 8);
    std::memcpy(hdr.data() + 147, &sc, 8);
    std::memcpy(hdr.data() + 155, &zero, 8);
    std::memcpy(hdr.data() + 163, &zero, 8);
    std::memcpy(hdr.data() + 171, &zero, 8);

    std::vector<std::vector<uint8_t>> chunks;
    for (size_t off = 0; off < pts.size(); off += chunkSize) {
        size_t cnt = std::min<size_t>(chunkSize, pts.size() - off);
        chunks.push_back(EncodeLazPoint10v2Chunk(pts.data() + off, cnt));
    }

    ByteWriter out;
    out.Bytes(hdr.data(), hdr.size());
    out.Bytes(vlrHdr.data(), vlrHdr.size());
    out.Bytes(vlrPay.buf.data(), vlrPay.buf.size());

    size_t tablePtrPos = out.buf.size();
    out.U64LE(0); // placeholder for int64_t chunkTableStart
    for (const auto& ch : chunks) out.Bytes(ch.data(), ch.size());

    uint64_t chunkTableStart = out.buf.size();
    for (int b = 0; b < 8; ++b) {
        out.buf[tablePtrPos + b] = static_cast<uint8_t>((chunkTableStart >> (b * 8)) & 0xFFu);
    }

    // Chunk table: version=0, numChunks, arithmetic-coded byte lengths (fixed chunk_size -> context 1 only!)
    out.U32LE(0u);
    out.U32LE(static_cast<uint32_t>(chunks.size()));
    ArithEncoder ctEnc;
    ctEnc.Init();
    IntCompressorEnc ctIc;
    ctIc.Setup(32, 2);
    ctIc.InitCompressor();
    int32_t prevBytes = 0;
    for (const auto& ch : chunks) {
        int32_t curBytes = static_cast<int32_t>(ch.size());
        ctIc.Compress(ctEnc, prevBytes, curBytes, 1u);
        prevBytes = curBytes;
    }
    ctEnc.Done();
    out.Bytes(ctEnc.buf.data(), ctEnc.buf.size());
    return out.buf;
}

static std::vector<uint8_t> BuildLas14Fmt6Fixture(const std::vector<Vec3f>& pts) {
    const uint16_t hdrSz = 375;
    std::vector<uint8_t> hdr(hdrSz, 0);
    std::memcpy(hdr.data(), "LASF", 4);
    hdr[24] = 1; hdr[25] = 4; // LAS 1.4!
    std::memcpy(hdr.data() + 94, &hdrSz, 2);
    uint32_t ptOff = hdrSz;
    std::memcpy(hdr.data() + 96, &ptOff, 4);
    hdr[104] = 6u; // Point Format 6 (30 bytes)
    uint16_t recLen = 30;
    std::memcpy(hdr.data() + 105, &recLen, 2);
    uint32_t legacyZero = 0; // legacy 32-bit count intentionally 0 per LAS 1.4 spec!
    std::memcpy(hdr.data() + 107, &legacyZero, 4);
    double sc = 0.01, zero = 0.0;
    std::memcpy(hdr.data() + 131, &sc, 8);
    std::memcpy(hdr.data() + 139, &sc, 8);
    std::memcpy(hdr.data() + 147, &sc, 8);
    std::memcpy(hdr.data() + 155, &zero, 8);
    std::memcpy(hdr.data() + 163, &zero, 8);
    std::memcpy(hdr.data() + 171, &zero, 8);
    uint64_t extCount64 = pts.size();
    std::memcpy(hdr.data() + 247, &extCount64, 8); // LAS 1.4 64-bit Extended Number of Point Records at +247!

    ByteWriter w;
    w.Bytes(hdr.data(), hdr.size());
    for (const auto& p : pts) {
        w.I32LE(static_cast<int32_t>(std::llround(static_cast<double>(p.x) * 100.0)));
        w.I32LE(static_cast<int32_t>(std::llround(static_cast<double>(p.y) * 100.0)));
        w.I32LE(static_cast<int32_t>(std::llround(static_cast<double>(p.z) * 100.0)));
        for (int k = 0; k < 18; ++k) w.U8(0); // remaining 18 bytes of 30-byte Format 6 record
    }
    return w.buf;
}

} // namespace LazTestEnc

// ============================================================================
// RFC 8878 Zstandard Skippable Seek-Table + Compressed_Block (Huffman + FSE) Encoder for Blender .blend
// ============================================================================
static std::vector<uint8_t> PackZstdReverseBitStream(const std::vector<std::pair<uint32_t, int>>& readsInOrder) {
    // ZstdRevBitStream::ReadBits reads from just below the sentinel 1-bit down to bit 0:
    // the LAST read sits at bit 0, the FIRST read sits just below the sentinel 1-bit.
    std::vector<uint8_t> bitsLSB;
    for (size_t idx = readsInOrder.size(); idx > 0; --idx) {
        uint32_t val = readsInOrder[idx - 1].first;
        int nb = readsInOrder[idx - 1].second;
        for (int b = 0; b < nb; ++b) {
            bitsLSB.push_back(static_cast<uint8_t>((val >> b) & 1u));
        }
    }
    bitsLSB.push_back(1u); // sentinel top 1-bit
    std::vector<uint8_t> out((bitsLSB.size() + 7u) / 8u, 0);
    for (size_t i = 0; i < bitsLSB.size(); ++i) {
        if (bitsLSB[i]) out[i >> 3] |= static_cast<uint8_t>(1u << (i & 7u));
    }
    return out;
}

static std::vector<uint8_t> EncodeZstdSingleMatchCompressedBlock(
    const uint8_t* seg, size_t segLen, bool useHuffman7BitLiterals, bool isLastBlock)
{
    static const uint32_t kLLBase[36] = {
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
        16,18,20,22,24,28,32,40,48,64,128,256,512,1024,2048,4096,
        8192,16384,32768,65536
    };
    static const uint8_t kLLBits[36] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        1,1,1,1,2,2,3,3,4,6,7,8,9,10,11,12,
        13,14,15,16
    };
    static const uint32_t kMLBase[53] = {
        3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,
        19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,
        35,37,39,41,43,47,51,59,67,83,99,131,259,515,1027,2051,
        4099,8195,16387,32771,65539
    };
    static const uint8_t kMLBits[53] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        1,1,1,1,2,2,3,3,4,4,5,7,8,9,10,11,
        12,13,14,15,16
    };

    // Find the longest RLE/LZ77 match (offset = 1) inside seg so we emit a real FSE sequence!
    size_t bestPos = 1, bestLen = 0;
    for (size_t i = 1; i + 4 <= segLen; ++i) {
        size_t ml = 0;
        while (i + ml < segLen && ml < 64 && seg[i + ml] == seg[i - 1]) ++ml;
        if (ml > bestLen) { bestLen = ml; bestPos = i; }
    }
    const uint32_t litLen = static_cast<uint32_t>(bestPos);
    const uint32_t matchLen = static_cast<uint32_t>(bestLen); // >= 16 on CustomDataLayer & ENDB zero runs!
    const uint32_t actualOffset = 1u;

    std::vector<uint8_t> literals;
    literals.insert(literals.end(), seg, seg + bestPos);
    literals.insert(literals.end(), seg + bestPos + bestLen, seg + segLen);

    ByteWriter blk;
    if (useHuffman7BitLiterals) {
        // Compressed_Literals_Block (litType = 2), single stream (sizeFmt = 0), regenSize < 1024:
        // Direct Huffman tree header: h0 = 254 -> numSyms = 127, weights[0..126] = 1, implicit weight[127] = 1 -> flat 7-bit table!
        ByteWriter huffPay;
        huffPay.U8(254u);
        for (int i = 0; i < 63; ++i) huffPay.U8(0x11u);
        huffPay.U8(0x10u); // 127th nibble = 1, padding nibble = 0 (treeBytes = 65)

        std::vector<std::pair<uint32_t, int>> huffReads;
        huffReads.reserve(literals.size());
        for (uint8_t sym : literals) {
            huffReads.emplace_back(static_cast<uint32_t>(sym & 0x7Fu), 7);
        }
        auto huffStream = PackZstdReverseBitStream(huffReads);
        huffPay.Bytes(huffStream.data(), huffStream.size());

        uint32_t regenSz = static_cast<uint32_t>(literals.size());
        uint32_t compSz  = static_cast<uint32_t>(huffPay.buf.size());
        uint32_t litHdr  = 2u | (0u << 2) | (regenSz << 4) | (compSz << 14);
        blk.U8(static_cast<uint8_t>(litHdr & 0xFFu));
        blk.U8(static_cast<uint8_t>((litHdr >> 8) & 0xFFu));
        blk.U8(static_cast<uint8_t>((litHdr >> 16) & 0xFFu));
        blk.Bytes(huffPay.buf.data(), huffPay.buf.size());
    } else {
        // Raw_Literals_Block (litType = 0), sizeFmt = 1 (2-byte header up to 4095 bytes)
        uint32_t regenSz = static_cast<uint32_t>(literals.size());
        uint16_t litHdr = static_cast<uint16_t>(0u | (1u << 2) | (regenSz << 4));
        blk.U16LE(litHdr);
        blk.Bytes(literals.data(), literals.size());
    }

    // Sequences Section: numSeq = 1, RLE modes (LL=1, OF=1, ML=1 -> 0x54)
    uint8_t llCode = 0;
    for (uint8_t c = 0; c < 36; ++c) if (kLLBase[c] <= litLen) llCode = c;
    uint32_t llExtra = litLen - kLLBase[llCode];

    uint8_t mlCode = 0;
    for (uint8_t c = 0; c < 53; ++c) if (kMLBase[c] <= matchLen) mlCode = c;
    uint32_t mlExtra = matchLen - kMLBase[mlCode];

    uint32_t rawOffset = actualOffset + 3u; // 4 -> ofCode = 2, ofExtra = 0
    uint8_t ofCode = 2u;
    uint32_t ofExtra = rawOffset - (1u << ofCode);

    blk.U8(1u);    // numSeq = 1
    blk.U8(0x54u); // modes: LL=RLE(1), OF=RLE(1), ML=RLE(1)
    blk.U8(llCode);
    blk.U8(ofCode);
    blk.U8(mlCode);

    // Extra bits read in order: OF, ML, LL
    std::vector<std::pair<uint32_t, int>> seqReads;
    if (ofCode > 0) seqReads.emplace_back(ofExtra, static_cast<int>(ofCode));
    if (kMLBits[mlCode] > 0) seqReads.emplace_back(mlExtra, static_cast<int>(kMLBits[mlCode]));
    if (kLLBits[llCode] > 0) seqReads.emplace_back(llExtra, static_cast<int>(kLLBits[llCode]));
    auto seqStream = PackZstdReverseBitStream(seqReads);
    blk.Bytes(seqStream.data(), seqStream.size());

    ByteWriter out;
    uint32_t bh = (isLastBlock ? 1u : 0u) | (2u << 1) | (static_cast<uint32_t>(blk.buf.size()) << 3);
    out.U8(static_cast<uint8_t>(bh & 0xFFu));
    out.U8(static_cast<uint8_t>((bh >> 8) & 0xFFu));
    out.U8(static_cast<uint8_t>((bh >> 16) & 0xFFu));
    out.Bytes(blk.buf.data(), blk.buf.size());
    return out.buf;
}

static std::vector<uint8_t> WrapBlendInZstdSeekAndCompressedBlocks(const std::vector<uint8_t>& rawBlend) {
    // Split right at "SDNANAME" so Block 1 is 100% 7-bit ASCII/low-byte (< 128) and exercises Huffman Compressed_Literals!
    std::string_view sv(reinterpret_cast<const char*>(rawBlend.data()), rawBlend.size());
    size_t splitPos = sv.find("SDNANAME");
    if (splitPos == std::string_view::npos) splitPos = rawBlend.size() / 2;

    ByteWriter w;
    // 1. Blender Zstd Skippable Seek-Table Frame (magic 0x184D2A5E) at offset 0!
    w.U32LE(0x184D2A5Eu);
    w.U32LE(9u);
    w.U32LE(0u);
    w.U32LE(static_cast<uint32_t>(rawBlend.size()));
    w.U8(0x80u);

    // 2. RFC 8878 Zstandard Frame (magic 0xFD2FB528, fhd = 0xA0 -> Single_Segment + 4-byte Frame_Content_Size)
    w.U32LE(0xFD2FB528u);
    w.U8(0xA0u);
    w.U32LE(static_cast<uint32_t>(rawBlend.size()));

    // Block 0: Compressed_Block (bType=2, Raw_Literals + FSE Sequence compressing CustomDataLayer zero padding)
    auto b0 = EncodeZstdSingleMatchCompressedBlock(rawBlend.data(), splitPos, false, false);
    // Block 1: Compressed_Block (bType=2, Huffman Compressed_Literals + FSE Sequence compressing ENDB zero tail)
    auto b1 = EncodeZstdSingleMatchCompressedBlock(rawBlend.data() + splitPos, rawBlend.size() - splitPos, true, true);
    w.Bytes(b0.data(), b0.size());
    w.Bytes(b1.data(), b1.size());
    return w.buf;
}

} // namespace

// LightWave LWO2 object: one LAYR + PNTS + POLS(FACE) per layer, big-endian IFF chunks
struct LwoTestLayer {
    uint16_t number = 0;
    std::vector<Vec3f> pts;
    std::vector<std::vector<uint16_t>> faces;
};
static std::vector<uint8_t> BuildSyntheticLwo2(const std::vector<LwoTestLayer>& layers) {
    ByteWriter body;
    body.Tag4("LWO2");
    auto chunk = [&](const char* tag, const ByteWriter& c) {
        body.Tag4(tag);
        body.U32BE(static_cast<uint32_t>(c.buf.size()));
        body.Bytes(c.buf.data(), c.buf.size());
        if (c.buf.size() & 1u) body.U8(0);
    };
    for (const auto& L : layers) {
        ByteWriter layr;
        layr.U16BE(L.number);
        layr.U16BE(0);
        layr.F32BE(0); layr.F32BE(0); layr.F32BE(0);
        layr.Bytes("Layer\0", 6);
        chunk("LAYR", layr);
        ByteWriter pnts;
        for (const auto& p : L.pts) { pnts.F32BE(p.x); pnts.F32BE(p.y); pnts.F32BE(p.z); }
        chunk("PNTS", pnts);
        ByteWriter pols;
        pols.Tag4("FACE");
        for (const auto& f : L.faces) {
            pols.U16BE(static_cast<uint16_t>(f.size()));
            for (uint16_t idx : f) pols.U16BE(idx);
        }
        chunk("POLS", pols);
    }
    ByteWriter file;
    file.Tag4("FORM");
    file.U32BE(static_cast<uint32_t>(body.buf.size()));
    file.Bytes(body.buf.data(), body.buf.size());
    return file.buf;
}

void RunFormatTests3D(TestCounter& tc) {
    const std::wstring workDir = ScratchTestDir() + L"\\fmt3d";
    CreateDirectoryW(workDir.c_str(), nullptr);
    std::vector<std::wstring> createdFiles;

    auto writeBytes = [&](const wchar_t* name, const std::vector<uint8_t>& bytes) -> std::wstring {
        std::wstring p = workDir + L"\\" + name;
        PathUtils::WriteFileBytes(p, bytes.data(), bytes.size());
        createdFiles.push_back(p);
        return p;
    };
    auto writeText = [&](const wchar_t* name, const char* text) -> std::wstring {
        std::wstring p = workDir + L"\\" + name;
        PathUtils::WriteFileBytes(p, text, std::strlen(text));
        createdFiles.push_back(p);
        return p;
    };

    const std::wstring tdDir = TestDataDir();

    // ========================================================================
    // 0. G-code: only extruding moves form the part; relative positioning / extrusion, inches, arcs,
    //    modal motion, G10 / G28 / G92 are not moves
    // ========================================================================
    {
        const char* gcode =
            "; printer file\nG21\nG90\nM83\nG28\n"
            "G1 Z0.2 F3000\n"            // travel
            "G1 X10 Y0 E0.5\n"           // extrude 1
            "X10 Y10 E0.5\n"             // modal G1: extrude 2
            "G0 X50 Y50\n"               // travel
            "G1 X60 Y50 E-1\n"           // retract while moving: travel
            "G91\nG1 Y10 E0.5\n"        // relative: extrude 3, to (60, 60)
            "G10\nG90\nG20\n"          // firmware retract is not a move; inches from here
            "G1 X3 Y3 E0.5\n"            // extrude 4, to (76.2, 76.2) mm
            "G2 X3 Y3 I1 J0 E0.5\n"      // full clockwise circle: 37 arc segments
            "G92 X0 Y0\nG28 X\n";       // position resets: no moves
        LoadedMesh3D mG;
        bool okG = Model3DViewer::InspectMeshFile(writeText(L"part.gcode", gcode), mG) && mG.loadedOk &&
                   mG.rawTriangleCount == 41u * 4u;
        // CNC program (no E words): every feed move cuts, rapids (G0) do not
        const char* cnc = "G21 G90\nG0 X0 Y0 Z5\nG1 Z-1 F100\nG1 X20\nG1 Y20\nG0 Z5\nG0 X40\n";
        LoadedMesh3D mC;
        bool okC = Model3DViewer::InspectMeshFile(writeText(L"cut.nc", cnc), mC) && mC.loadedOk &&
                   mC.rawTriangleCount == 3u * 4u;
        tc.Record(okG && okC, L"Stage15.3D.Gcode.ExtrusionOnlyModesArcs",
                  L"printTris=" + std::to_wstring(mG.rawTriangleCount) + L" cncTris=" + std::to_wstring(mC.rawTriangleCount));
    }

    // ========================================================================
    // 0b. MagicaVoxel: a solid 3x3x3 block meshes as its 54 outer faces (108 triangles), not 27 cubes (324)
    // ========================================================================
    {
        std::vector<uint8_t> vox = { 'V', 'O', 'X', ' ', 150, 0, 0, 0 };
        auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) vox.push_back(static_cast<uint8_t>(v >> (8 * i))); };
        auto tag = [&](const char* t) { vox.insert(vox.end(), t, t + 4); };
        const uint32_t sizeChunk = 12 + 12, xyziChunk = 12 + 4 + 27 * 4;
        tag("MAIN"); u32(0); u32(sizeChunk + xyziChunk);
        tag("SIZE"); u32(12); u32(0); u32(3); u32(3); u32(3);
        tag("XYZI"); u32(4 + 27 * 4); u32(0); u32(27);
        for (uint8_t z = 0; z < 3; ++z)
            for (uint8_t y = 0; y < 3; ++y)
                for (uint8_t x = 0; x < 3; ++x) { vox.push_back(x); vox.push_back(y); vox.push_back(z); vox.push_back(1); }
        LoadedMesh3D mV;
        bool okV = Model3DViewer::InspectMeshFile(writeBytes(L"block.vox", vox), mV) && mV.loadedOk && mV.rawTriangleCount == 108u;
        std::vector<uint8_t> truncated(vox.begin(), vox.begin() + 40);
        LoadedMesh3D mT;
        bool rejT = !Model3DViewer::InspectMeshFile(writeBytes(L"cut.vox", truncated), mT) || !mT.loadedOk;
        tc.Record(okV && rejT, L"Stage15.3D.MagicaVox.ExposedFacesOnly", L"tris=" + std::to_wstring(mV.rawTriangleCount));
    }

    // ========================================================================
    // 0c. Wavefront OBJ polylines ("l") show up as struts; faces with v/vt/vn and negative indices still work
    // ========================================================================
    {
        LoadedMesh3D mL, mF;
        bool okL = Model3DViewer::InspectMeshFile(writeText(L"wire.obj", "v 0 0 0\nv 1 0 0\nv 1 1 0\nl 1 2 3\n"), mL) &&
                   mL.loadedOk && mL.rawTriangleCount == 8u;
        bool okF = Model3DViewer::InspectMeshFile(writeText(L"quad.obj",
                       "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvn 0 0 1\nf -4/1/1 -3/1/1 -2/1/1 -1/1/1\n"), mF) &&
                   mF.loadedOk && mF.rawTriangleCount == 2u;
        tc.Record(okL && okF, L"Stage15.3D.Obj.PolylinesAndFaces",
                  L"lineTris=" + std::to_wstring(mL.rawTriangleCount) + L" faceTris=" + std::to_wstring(mF.rawTriangleCount));
    }

    // ========================================================================
    // 0d. Molecules: bonds parallel to X are real struts (8 tris per atom, 4 per bond); PDB keeps the first
    //     model, skips alternate locations, adds CONECT bonds; MOL bond numbers survive a damaged atom line; V3000
    // ========================================================================
    {
        auto pdbAtom = [](const char* rec, int serial, char alt, float x, float y, float z) {
            char line[96];
            snprintf(line, sizeof(line), "%-6s%5d  CA %cALA A   1    %8.3f%8.3f%8.3f  1.00  0.00           C\n", rec, serial, alt, x, y, z);
            return std::string(line);
        };
        std::string pdb = pdbAtom("ATOM", 1, ' ', 0.0f, 0.0f, 0.0f) + pdbAtom("ATOM", 2, ' ', 1.5f, 0.0f, 0.0f) +
                          pdbAtom("ATOM", 3, 'B', 9.0f, 9.0f, 9.0f) + pdbAtom("HETATM", 4, ' ', 10.0f, 0.0f, 0.0f) +
                          "CONECT    1    4\nENDMDL\n" + pdbAtom("ATOM", 5, ' ', 50.0f, 50.0f, 50.0f);
        LoadedMesh3D mP;
        bool okP = Model3DViewer::InspectMeshFile(writeText(L"mol.pdb", pdb.c_str()), mP) && mP.loadedOk && mP.rawTriangleCount == 3u * 8u + 2u * 4u;
        const char* molV2 =
            "ethanol-ish\n  test\n\n  3  1  0  0  0  0  0  0  0  0999 V2000\n"
            "    0.0000    0.0000    0.0000 C   0  0  0  0  0  0  0  0  0  0  0  0\n"
            "damaged atom line\n"
            "    1.5000    0.0000    0.0000 O   0  0  0  0  0  0  0  0  0  0  0  0\n"
            "  1  3  1  0\nM  END\n";
        const char* molV3 =
            "v3\n  test\n\n  0  0  0     0  0            999 V3000\nM  V30 BEGIN CTAB\nM  V30 COUNTS 2 1 0 0 0\n"
            "M  V30 BEGIN ATOM\nM  V30 1 C 0 0 0 0\nM  V30 2 O 1.5 0 0 0\nM  V30 END ATOM\n"
            "M  V30 BEGIN BOND\nM  V30 1 1 1 2\nM  V30 END BOND\nM  V30 END CTAB\nM  END\n";
        LoadedMesh3D m2, m3;
        bool ok2 = Model3DViewer::InspectMeshFile(writeText(L"v2.mol", molV2), m2) && m2.loadedOk && m2.rawTriangleCount == 2u * 8u + 4u;
        bool ok3 = Model3DViewer::InspectMeshFile(writeText(L"v3.mol", molV3), m3) && m3.loadedOk && m3.rawTriangleCount == 2u * 8u + 4u;
        tc.Record(okP && ok2 && ok3, L"Stage15.3D.Molecules.PdbModelsConectMolV2V3",
                  L"pdb=" + std::to_wstring(mP.rawTriangleCount) + L" v2=" + std::to_wstring(m2.rawTriangleCount) +
                  L" v3=" + std::to_wstring(m3.rawTriangleCount));
    }

    // ========================================================================
    // 0e. Point clouds: 100 000 points are sampled over the whole file (the far end is present), not cut at 20 000
    // ========================================================================
    {
        std::string cloud;
        cloud.reserve(1500000);
        for (int i = 0; i < 100000; ++i) cloud += std::to_string(i) + " 0 0\n";
        LoadedMesh3D mX;
        bool okX = Model3DViewer::InspectMeshFile(writeText(L"scan.xyz", cloud.c_str()), mX) && mX.loadedOk &&
                   mX.rawTriangleCount == 25000u * 8u && mX.bboxMax.x > 99000.0f;
        tc.Record(okX, L"Stage15.3D.Xyz.EvenSamplingWholeCloud",
                  L"tris=" + std::to_wstring(mX.rawTriangleCount) + L" maxX=" + std::to_wstring(mX.bboxMax.x));
    }

    // ========================================================================
    // 0f. 3D GeoJSON: a U-shaped footprint is extruded to its height in metres (not degrees) and its concave roof is
    //     ear-clipped: the roof's projected area is 5/6 of its bounding rectangle (a fan from the first vertex would
    //     also cover the notch); 16 wall + 6 roof triangles
    // ========================================================================
    {
        const char* uBuilding =
            R"({"type":"FeatureCollection","features":[{"type":"Feature","properties":{"height":10},"geometry":{"type":"Polygon","coordinates":[[)"
            R"([13.4000,52.5000],[13.4003,52.5000],[13.4003,52.5002],[13.4002,52.5002],[13.4002,52.5001],[13.4001,52.5001],)"
            R"([13.4001,52.5002],[13.4000,52.5002],[13.4000,52.5000]]]}}]})";
        LoadedMesh3D mG;
        bool okG = Model3DViewer::InspectMeshFile(writeText(L"block.geojson", uBuilding), mG) && mG.loadedOk && mG.rawTriangleCount == 22u;
        double roofArea = 0.0;
        float nMinX = 1e30f, nMaxX = -1e30f, nMinZ = 1e30f, nMaxZ = -1e30f;
        for (const auto& v : mG.vertices) {
            nMinX = std::min(nMinX, v.x); nMaxX = std::max(nMaxX, v.x);
            nMinZ = std::min(nMinZ, v.z); nMaxZ = std::max(nMaxZ, v.z);
        }
        for (const auto& t : mG.tris) {
            if (t.i0 >= mG.vertices.size() || t.i1 >= mG.vertices.size() || t.i2 >= mG.vertices.size()) continue;
            const Vec3f& a = mG.vertices[t.i0];
            const Vec3f& b = mG.vertices[t.i1];
            const Vec3f& c = mG.vertices[t.i2];
            roofArea += std::fabs(static_cast<double>(b.x - a.x) * (c.z - a.z) - static_cast<double>(c.x - a.x) * (b.z - a.z)) * 0.5;
        }
        double ratio = roofArea / std::max(1e-12, static_cast<double>(nMaxX - nMinX) * (nMaxZ - nMinZ));
        float sizeX = mG.bboxMax.x - mG.bboxMin.x, sizeY = mG.bboxMax.y - mG.bboxMin.y;
        okG = okG && std::fabs(ratio - 5.0 / 6.0) < 0.01 && sizeY > 9.9f && sizeY < 10.1f && sizeX > 19.5f && sizeX < 21.0f;
        tc.Record(okG, L"Stage15.3D.GeoJson3D.ExtrudedConcaveFootprintInMetres",
                  L"tris=" + std::to_wstring(mG.rawTriangleCount) + L" areaRatio=" + std::to_wstring(ratio) +
                  L" size=" + std::to_wstring(sizeX) + L"x" + std::to_wstring(sizeY));
    }

    // ========================================================================
    // 1. Blender (.blend): testdata/blender_box.blend + 3.0 Pyramid + 4.2 SoA Prism
    // ========================================================================
    {
        LoadedMesh3D mBox;
        bool okBox = Model3DViewer::InspectMeshFile(tdDir + L"\\blender_box.blend", mBox);
        tc.Record(okBox && mBox.loadedOk && mBox.rawVertexCount == 8 && mBox.rawTriangleCount == 12,
                  L"Stage15.3D.Blend.RepoBox",
                  L"verts=" + std::to_wstring(mBox.rawVertexCount) + L" tris=" + std::to_wstring(mBox.rawTriangleCount));

        auto pyrBytes = BuildSyntheticBlend30(
            { {-1,-1,0}, {1,-1,0}, {1,1,0}, {-1,1,0}, {0,0,2.5f} },
            { {0,3,2,1}, {0,1,4}, {1,2,4}, {2,3,4}, {3,0,4} },
            1.0f, 1.0f, 1.5f);
        auto soaBytes = BuildSyntheticBlend42SoA(
            { {0,0,0}, {3,0,0}, {0,2,0}, {0,0,4}, {3,0,4}, {0,2,4} },
            { {0,2,1}, {3,4,5}, {0,1,4,3}, {1,2,5,4}, {2,0,3,5} });

        LoadedMesh3D mPyr, mSoa;
        bool okPyr = Model3DViewer::InspectMeshFile(writeBytes(L"pyramid_v300.blend", pyrBytes), mPyr);
        bool okSoa = Model3DViewer::InspectMeshFile(writeBytes(L"prism_v402_soa.blend", soaBytes), mSoa);
        uint64_t hBox = HashMeshGeometry(mBox);
        uint64_t hPyr = HashMeshGeometry(mPyr);
        uint64_t hSoa = HashMeshGeometry(mSoa);
        tc.Record(okPyr && mPyr.rawVertexCount == 5 && mPyr.rawTriangleCount == 6 &&
                  okSoa && mSoa.rawVertexCount == 6 && mSoa.rawTriangleCount == 8 &&
                  hBox != hPyr && hPyr != hSoa && hBox != hSoa,
                  L"Stage15.3D.Blend.DistinctDNA1AndSoA",
                  L"pyrTris=" + std::to_wstring(mPyr.rawTriangleCount) + L" soaTris=" + std::to_wstring(mSoa.rawTriangleCount));
    }

    // ========================================================================
    // 2. STEP ISO 10303-21 (.step): sample.step + LINE B-Rep vs CIRCLE B-Rep
    // ========================================================================
    {
        LoadedMesh3D mSample;
        bool okSample = Model3DViewer::InspectMeshFile(tdDir + L"\\sample.step", mSample);
        tc.Record(okSample && mSample.loadedOk && mSample.rawTriangleCount >= 4,
                  L"Stage15.3D.STEP.RepoSample",
                  L"tris=" + std::to_wstring(mSample.rawTriangleCount));

        const char* stepLineWedge =
            "ISO-10303-21;\nHEADER;\nENDSEC;\nDATA;\n"
            "#1=CARTESIAN_POINT('',(0.0,0.0,0.0));\n"
            "#2=CARTESIAN_POINT('',(10.0,0.0,0.0));\n"
            "#3=CARTESIAN_POINT('',(0.0,8.0,0.0));\n"
            "#4=CARTESIAN_POINT('',(0.0,0.0,6.0));\n"
            "#11=VERTEX_POINT('',#1);\n#12=VERTEX_POINT('',#2);\n"
            "#13=VERTEX_POINT('',#3);\n#14=VERTEX_POINT('',#4);\n"
            "#20=DIRECTION('',(1.0,0.0,0.0));\n#21=VECTOR('',#20,1.0);\n#22=LINE('',#1,#21);\n"
            "#31=EDGE_CURVE('',#11,#12,#22,.T.);\n"
            "#32=EDGE_CURVE('',#12,#13,#22,.T.);\n"
            "#33=EDGE_CURVE('',#13,#11,#22,.T.);\n"
            "#34=EDGE_CURVE('',#11,#14,#22,.T.);\n"
            "#35=EDGE_CURVE('',#12,#14,#22,.T.);\n"
            "#36=EDGE_CURVE('',#13,#14,#22,.T.);\n"
            "#41=ORIENTED_EDGE('',*,*,#31,.F.);\n#42=ORIENTED_EDGE('',*,*,#33,.F.);\n#43=ORIENTED_EDGE('',*,*,#32,.F.);\n"
            "#44=EDGE_LOOP('',(#41,#42,#43));\n#45=FACE_OUTER_BOUND('',#44,.T.);\n"
            "#46=PLANE('',#1);\n#47=ADVANCED_FACE('',(#45),#46,.T.);\n"
            "#51=ORIENTED_EDGE('',*,*,#31,.T.);\n#52=ORIENTED_EDGE('',*,*,#35,.T.);\n#53=ORIENTED_EDGE('',*,*,#34,.F.);\n"
            "#54=EDGE_LOOP('',(#51,#52,#53));\n#55=FACE_OUTER_BOUND('',#54,.T.);\n"
            "#56=ADVANCED_FACE('',(#55),#46,.T.);\n"
            "#61=CLOSED_SHELL('',(#47,#56));\n"
            "ENDSEC;\nEND-ISO-10303-21;\n";

        const char* stepCircleCyl =
            "ISO-10303-21;\nHEADER;\nENDSEC;\nDATA;\n"
            "#1=CARTESIAN_POINT('',(0.0,0.0,0.0));\n"
            "#2=CARTESIAN_POINT('',(0.0,0.0,20.0));\n"
            "#3=DIRECTION('',(0.0,0.0,1.0));\n"
            "#4=DIRECTION('',(1.0,0.0,0.0));\n"
            "#5=AXIS2_PLACEMENT_3D('',#1,#3,#4);\n"
            "#6=AXIS2_PLACEMENT_3D('',#2,#3,#4);\n"
            "#7=CARTESIAN_POINT('',(5.0,0.0,0.0));\n"
            "#8=CARTESIAN_POINT('',(5.0,0.0,20.0));\n"
            "#9=VERTEX_POINT('',#7);\n"
            "#10=VERTEX_POINT('',#8);\n"
            "#11=CIRCLE('',#5,5.0);\n"
            "#12=CIRCLE('',#6,5.0);\n"
            "#13=VECTOR('',#3,1.0);\n"
            "#14=LINE('',#7,#13);\n"
            "#20=EDGE_CURVE('',#9,#9,#11,.T.);\n"
            "#21=EDGE_CURVE('',#10,#10,#12,.T.);\n"
            "#22=EDGE_CURVE('',#9,#10,#14,.T.);\n"
            "#30=ORIENTED_EDGE('',*,*,#20,.F.);\n"
            "#31=EDGE_LOOP('',(#30));\n"
            "#32=FACE_OUTER_BOUND('',#31,.T.);\n"
            "#33=PLANE('',#5);\n"
            "#34=ADVANCED_FACE('',(#32),#33,.F.);\n"
            "#40=ORIENTED_EDGE('',*,*,#21,.T.);\n"
            "#41=EDGE_LOOP('',(#40));\n"
            "#42=FACE_OUTER_BOUND('',#41,.T.);\n"
            "#43=PLANE('',#6);\n"
            "#44=ADVANCED_FACE('',(#42),#43,.T.);\n"
            "#50=ORIENTED_EDGE('',*,*,#20,.T.);\n"
            "#51=ORIENTED_EDGE('',*,*,#22,.T.);\n"
            "#52=ORIENTED_EDGE('',*,*,#21,.F.);\n"
            "#53=ORIENTED_EDGE('',*,*,#22,.F.);\n"
            "#54=EDGE_LOOP('',(#50,#51,#52,#53));\n"
            "#55=FACE_OUTER_BOUND('',#54,.T.);\n"
            "#56=CYLINDRICAL_SURFACE('',#5,5.0);\n"
            "#57=ADVANCED_FACE('',(#55),#56,.T.);\n"
            "#60=CLOSED_SHELL('',(#34,#44,#57));\n"
            "ENDSEC;\nEND-ISO-10303-21;\n";

        LoadedMesh3D mStepA, mStepB;
        bool okA = Model3DViewer::InspectMeshFile(writeText(L"wedge_line.step", stepLineWedge), mStepA);
        bool okB = Model3DViewer::InspectMeshFile(writeText(L"cyl_circle.step", stepCircleCyl), mStepB);
        tc.Record(okA && okB && mStepB.rawTriangleCount > mStepA.rawTriangleCount &&
                  HashMeshGeometry(mStepA) != HashMeshGeometry(mStepB),
                  L"Stage15.3D.STEP.DistinctBRepLineVsCircle",
                  L"trisA=" + std::to_wstring(mStepA.rawTriangleCount) + L" trisB=" + std::to_wstring(mStepB.rawTriangleCount));
    }

    // ========================================================================
    // 3. OpenSCAD (.scad): parametric_bracket.scad + CSG Sphere/Cylinder vs Extrude/Polyhedron
    // ========================================================================
    {
        LoadedMesh3D mBrk;
        bool okBrk = Model3DViewer::InspectMeshFile(tdDir + L"\\parametric_bracket.scad", mBrk);
        tc.Record(okBrk && mBrk.loadedOk && mBrk.rawTriangleCount >= 20,
                  L"Stage15.3D.SCAD.RepoBracket",
                  L"tris=" + std::to_wstring(mBrk.rawTriangleCount));

        const char* scadA =
            "rad = 12;\n"
            "difference() {\n"
            "    sphere(r = rad, $fn = 18);\n"
            "    cylinder(h = rad * 3, r = 4.5, center = true, $fn = 14);\n"
            "}\n";
        const char* scadB =
            "module fin(ang) {\n"
            "    rotate([0, 0, ang]) translate([14, 0, 0])\n"
            "        linear_extrude(height = 16, twist = 45, scale = 0.5)\n"
            "            polygon(points = [[0,0],[8,0],[4,6]]);\n"
            "}\n"
            "polyhedron(points=[[0,0,0],[10,0,0],[5,9,0],[5,3,12]], faces=[[0,2,1],[0,1,3],[1,2,3],[2,0,3]]);\n"
            "for (k = [0 : 120 : 240]) fin(k);\n";

        LoadedMesh3D mScadA, mScadB;
        bool okA = Model3DViewer::InspectMeshFile(writeText(L"drilled_sphere.scad", scadA), mScadA);
        bool okB = Model3DViewer::InspectMeshFile(writeText(L"extruded_fins.scad", scadB), mScadB);
        tc.Record(okA && okB && mScadA.rawTriangleCount >= 40 && mScadB.rawTriangleCount >= 20 &&
                  HashMeshGeometry(mScadA) != HashMeshGeometry(mScadB),
                  L"Stage15.3D.SCAD.DistinctCSGAndExtrude",
                  L"trisA=" + std::to_wstring(mScadA.rawTriangleCount) + L" trisB=" + std::to_wstring(mScadB.rawTriangleCount));
    }

    // ========================================================================
    // 4. IFC BIM (.ifc): IFCEXTRUDEDAREASOLID vs IFCTRIANGULATEDFACESET
    // ========================================================================
    {
        const char* ifcExtruded =
            "ISO-10303-21;\nHEADER;\nFILE_SCHEMA(('IFC4'));\nENDSEC;\nDATA;\n"
            "#1=IFCCARTESIANPOINT((0.,0.,0.));\n"
            "#2=IFCDIRECTION((0.,0.,1.));\n"
            "#3=IFCDIRECTION((1.,0.,0.));\n"
            "#4=IFCAXIS2PLACEMENT3D(#1,#2,#3);\n"
            "#5=IFCLOCALPLACEMENT($,#4);\n"
            "#6=IFCRECTANGLEPROFILEDEF(.AREA.,'Col',$,400.,800.);\n"
            "#7=IFCEXTRUDEDAREASOLID(#6,#4,#2,3200.);\n"
            "#8=IFCSHAPEREPRESENTATION($,'Body','SweptSolid',(#7));\n"
            "#9=IFCPRODUCTDEFINITIONSHAPE($,$,(#8));\n"
            "#10=IFCCOLUMN('col1',$,'C1',$,$,#5,#9,$,$);\n"
            "ENDSEC;\nEND-ISO-10303-21;\n";

        const char* ifcTriSet =
            "ISO-10303-21;\nHEADER;\nFILE_SCHEMA(('IFC4'));\nENDSEC;\nDATA;\n"
            "#1=IFCCARTESIANPOINT((100.,50.,0.));\n"
            "#2=IFCDIRECTION((0.,0.,1.));\n"
            "#3=IFCDIRECTION((1.,0.,0.));\n"
            "#4=IFCAXIS2PLACEMENT3D(#1,#2,#3);\n"
            "#5=IFCLOCALPLACEMENT($,#4);\n"
            "#6=IFCCARTESIANPOINTLIST3D(((0.,0.,0.),(2000.,0.,0.),(1000.,1500.,0.),(1000.,500.,2500.)));\n"
            "#7=IFCTRIANGULATEDFACESET(#6,$,.T.,((1,3,2),(1,2,4),(2,3,4),(3,1,4)),$);\n"
            "#8=IFCSHAPEREPRESENTATION($,'Body','Tessellation',(#7));\n"
            "#9=IFCPRODUCTDEFINITIONSHAPE($,$,(#8));\n"
            "#10=IFCBUILDINGELEMENTPROXY('prx1',$,'RoofTet',$,$,#5,#9,$,$);\n"
            "ENDSEC;\nEND-ISO-10303-21;\n";

        LoadedMesh3D mIfcA, mIfcB;
        bool okA = Model3DViewer::InspectMeshFile(writeText(L"column_extruded.ifc", ifcExtruded), mIfcA);
        bool okB = Model3DViewer::InspectMeshFile(writeText(L"roof_triset.ifc", ifcTriSet), mIfcB);
        tc.Record(okA && okB && mIfcA.rawTriangleCount == 12 && mIfcB.rawTriangleCount == 4 &&
                  HashMeshGeometry(mIfcA) != HashMeshGeometry(mIfcB),
                  L"Stage15.3D.IFC.ExtrudedVsTriangulatedFaceSet",
                  L"trisA=" + std::to_wstring(mIfcA.rawTriangleCount) + L" trisB=" + std::to_wstring(mIfcB.rawTriangleCount));
    }

    // ========================================================================
    // 5. Pixar OpenUSD Binary Crate (.usdc): two distinct PXR-USDC meshes
    // ========================================================================
    {
        auto usdcTet = BuildSyntheticUsdc(
            { {0,0,0}, {2,0,0}, {0,2,0}, {0,0,3} },
            { 3, 3, 3, 3 },
            { 0,2,1, 0,1,3, 1,2,3, 2,0,3 });
        auto usdcPyr = BuildSyntheticUsdc(
            { {-2,0,-1}, {2,0,-1}, {2,0,1}, {-2,0,1}, {0,5,0} },
            { 4, 3, 3, 3, 3 },
            { 0,3,2,1, 0,1,4, 1,2,4, 2,3,4, 3,0,4 });

        LoadedMesh3D mUsdcA, mUsdcB;
        bool okA = Model3DViewer::InspectMeshFile(writeBytes(L"crate_tet.usdc", usdcTet), mUsdcA);
        bool okB = Model3DViewer::InspectMeshFile(writeBytes(L"crate_pyr.usdc", usdcPyr), mUsdcB);
        tc.Record(okA && okB && mUsdcA.rawVertexCount == 4 && mUsdcA.rawTriangleCount == 4 &&
                  mUsdcB.rawVertexCount == 5 && mUsdcB.rawTriangleCount == 6 &&
                  HashMeshGeometry(mUsdcA) != HashMeshGeometry(mUsdcB),
                  L"Stage15.3D.USDC.BinaryCrateLZ4Distinct",
                  L"trisA=" + std::to_wstring(mUsdcA.rawTriangleCount) + L" trisB=" + std::to_wstring(mUsdcB.rawTriangleCount));
    }

    // ========================================================================
    // 6. Autodesk Maya ASCII (.ma): negative reversed edge indices + distinct meshes
    // ========================================================================
    {
        const char* maTet =
            "//Maya ASCII 2024 scene\n"
            "createNode transform -n \"tetXf\";\n"
            "createNode mesh -n \"tetShape\" -p \"tetXf\";\n"
            "setAttr -s 4 \".vt[0:3]\" 0 0 0  4 0 0  0 4 0  0 0 5;\n"
            "setAttr -s 6 \".ed[0:5]\" 0 1 0  1 2 0  2 0 0  0 3 0  1 3 0  2 3 0;\n"
            "setAttr -s 4 \".fc[0:3]\" -type \"polyFaces\"\n"
            "    f 3 -1 -3 -2\n"
            "    f 3 0 4 -4\n"
            "    f 3 1 5 -5\n"
            "    f 3 2 3 -6;\n";

        const char* maPyr =
            "//Maya ASCII 2024 scene\n"
            "createNode transform -n \"pyrXf\";\n"
            "setAttr \".s\" -type \"double3\" 2 1 3;\n"
            "createNode mesh -n \"pyrShape\" -p \"pyrXf\";\n"
            "setAttr -s 5 \".vt[0:4]\" -1 0 -1  1 0 -1  1 0 1  -1 0 1  0 3 0;\n"
            "setAttr -s 8 \".ed[0:7]\" 0 1 0  1 2 0  2 3 0  3 0 0  0 4 0  1 4 0  2 4 0  3 4 0;\n"
            "setAttr -s 5 \".fc[0:4]\" -type \"polyFaces\"\n"
            "    f 4 -1 -4 -3 -2\n"
            "    f 3 0 5 -5\n"
            "    f 3 1 6 -6\n"
            "    f 3 2 7 -7\n"
            "    f 3 3 4 -8;\n";

        LoadedMesh3D mMaA, mMaB;
        bool okA = Model3DViewer::InspectMeshFile(writeText(L"maya_tet.ma", maTet), mMaA);
        bool okB = Model3DViewer::InspectMeshFile(writeText(L"maya_pyr.ma", maPyr), mMaB);
        tc.Record(okA && okB && mMaA.rawVertexCount == 4 && mMaA.rawTriangleCount == 4 &&
                  mMaB.rawVertexCount == 5 && mMaB.rawTriangleCount == 6 &&
                  HashMeshGeometry(mMaA) != HashMeshGeometry(mMaB),
                  L"Stage15.3D.MayaASCII.PolyFacesDistinct",
                  L"trisA=" + std::to_wstring(mMaA.rawTriangleCount) + L" trisB=" + std::to_wstring(mMaB.rawTriangleCount));
    }

    // ========================================================================
    // 7. Autodesk Maya Binary (.mb): FOR4 Maya -> MESH -> VRTS / EDGE / FACE
    // ========================================================================
    {
        auto mbTet = BuildSyntheticMayaMb(
            { {0,0,0}, {3,0,0}, {0,3,0}, {0,0,6} },
            { {0,1}, {1,2}, {2,0}, {0,3}, {1,3}, {2,3} },
            { {-1,-3,-2}, {0,4,-4}, {1,5,-5}, {2,3,-6} });

        auto mbPyr = BuildSyntheticMayaMb(
            { {-2,0,-1}, {2,0,-1}, {2,0,1}, {-2,0,1}, {0,4,0} },
            { {0,1}, {1,2}, {2,3}, {3,0}, {0,4}, {1,4}, {2,4}, {3,4} },
            { {-1,-4,-3,-2}, {0,5,-5}, {1,6,-6}, {2,7,-7}, {3,4,-8} });

        LoadedMesh3D mMbA, mMbB;
        bool okA = Model3DViewer::InspectMeshFile(writeBytes(L"maya_tet.mb", mbTet), mMbA);
        bool okB = Model3DViewer::InspectMeshFile(writeBytes(L"maya_pyr.mb", mbPyr), mMbB);
        tc.Record(okA && okB && mMbA.rawVertexCount == 4 && mMbA.rawTriangleCount == 4 &&
                  mMbB.rawVertexCount == 5 && mMbB.rawTriangleCount == 6 &&
                  HashMeshGeometry(mMbA) != HashMeshGeometry(mMbB),
                  L"Stage15.3D.MayaBinary.FOR4MeshDistinct",
                  L"trisA=" + std::to_wstring(mMbA.rawTriangleCount) + L" trisB=" + std::to_wstring(mMbB.rawTriangleCount));
    }

    // ========================================================================
    // 8. Alembic Ogawa (.abc): two distinct Ogawa PolyMesh caches
    // ========================================================================
    {
        auto abcTet = BuildSyntheticOgawaAbc(
            { {0,0,0}, {4,0,0}, {0,3,0}, {0,0,2} },
            { 0,1,2, 0,3,1, 1,3,2, 2,3,0 },
            { 3, 3, 3, 3 });

        auto abcOct = BuildSyntheticOgawaAbc(
            { {2,0,0}, {-2,0,0}, {0,5,0}, {0,-5,0}, {0,0,1}, {0,0,-1} },
            { 0,4,2, 4,1,2, 1,5,2, 5,0,2, 4,0,3, 1,4,3, 5,1,3, 0,5,3 },
            { 3, 3, 3, 3, 3, 3, 3, 3 });

        LoadedMesh3D mAbcA, mAbcB;
        bool okA = Model3DViewer::InspectMeshFile(writeBytes(L"ogawa_tet.abc", abcTet), mAbcA);
        bool okB = Model3DViewer::InspectMeshFile(writeBytes(L"ogawa_oct.abc", abcOct), mAbcB);
        tc.Record(okA && okB && mAbcA.rawVertexCount == 4 && mAbcA.rawTriangleCount == 4 &&
                  mAbcB.rawVertexCount == 6 && mAbcB.rawTriangleCount == 8 &&
                  HashMeshGeometry(mAbcA) != HashMeshGeometry(mAbcB),
                  L"Stage15.3D.Alembic.OgawaDistinct",
                  L"trisA=" + std::to_wstring(mAbcA.rawTriangleCount) + L" trisB=" + std::to_wstring(mAbcB.rawTriangleCount));
    }

    // ========================================================================
    // 9. Gamebryo / NetImmerse (.nif): two distinct NiTriShapeData files
    // ========================================================================
    {
        auto nifTet = BuildSyntheticNif(
            { {0,0,0}, {5,0,0}, {0,4,0}, {0,0,3} },
            { {{0,2,1}}, {{0,1,3}}, {{1,2,3}}, {{2,0,3}} });

        auto nifBox = BuildSyntheticNif(
            { {0,0,0}, {6,0,0}, {6,2,0}, {0,2,0}, {0,0,1.5f}, {6,0,1.5f}, {6,2,1.5f}, {0,2,1.5f} },
            { {{0,2,1}}, {{0,3,2}}, {{4,5,6}}, {{4,6,7}},
              {{0,1,5}}, {{0,5,4}}, {{2,3,7}}, {{2,7,6}},
              {{1,2,6}}, {{1,6,5}}, {{3,0,4}}, {{3,4,7}} });

        LoadedMesh3D mNifA, mNifB;
        bool okA = Model3DViewer::InspectMeshFile(writeBytes(L"shape_tet.nif", nifTet), mNifA);
        bool okB = Model3DViewer::InspectMeshFile(writeBytes(L"shape_box.nif", nifBox), mNifB);
        tc.Record(okA && okB && mNifA.rawVertexCount == 4 && mNifA.rawTriangleCount == 4 &&
                  mNifB.rawVertexCount == 8 && mNifB.rawTriangleCount == 12 &&
                  HashMeshGeometry(mNifA) != HashMeshGeometry(mNifB),
                  L"Stage15.3D.NIF.NiTriShapeDataDistinct",
                  L"trisA=" + std::to_wstring(mNifA.rawTriangleCount) + L" trisB=" + std::to_wstring(mNifB.rawTriangleCount));
    }

    // ========================================================================
    // 10. Google Draco (.drc): two distinct DRACO v2.2 Sequential meshes
    // ========================================================================
    {
        auto drcTet = BuildSyntheticDracoMesh(
            { {0,0,0}, {3,0,0}, {0,4,0}, {0,0,7} },
            { {{0,2,1}}, {{0,1,3}}, {{1,2,3}}, {{2,0,3}} });

        auto drcPyr = BuildSyntheticDracoMesh(
            { {-3,0,-1}, {3,0,-1}, {3,0,1}, {-3,0,1}, {0,2,0} },
            { {{0,3,2}}, {{0,2,1}}, {{0,1,4}}, {{1,2,4}}, {{2,3,4}}, {{3,0,4}} });

        LoadedMesh3D mDrcA, mDrcB;
        bool okA = Model3DViewer::InspectMeshFile(writeBytes(L"seq_tet.drc", drcTet), mDrcA);
        bool okB = Model3DViewer::InspectMeshFile(writeBytes(L"seq_pyr.drc", drcPyr), mDrcB);
        tc.Record(okA && okB && mDrcA.rawVertexCount == 4 && mDrcA.rawTriangleCount == 4 &&
                  mDrcB.rawVertexCount == 5 && mDrcB.rawTriangleCount == 6 &&
                  HashMeshGeometry(mDrcA) != HashMeshGeometry(mDrcB),
                  L"Stage15.3D.Draco.SequentialDistinct",
                  L"trisA=" + std::to_wstring(mDrcA.rawTriangleCount) + L" trisB=" + std::to_wstring(mDrcB.rawTriangleCount));
    }

    // ========================================================================
    // 11. Binary ACIS (.sab), OpenCTM RAW (.ctm), and Uncompressed LAS (.laz)
    // ========================================================================
    {
        auto sabA = BuildSyntheticSabBRep(
            { {0,0,0}, {10,0,0}, {0,8,0}, {0,0,6} },
            { {0,2,1}, {0,1,3}, {1,2,3}, {2,0,3} });
        auto sabB = BuildSyntheticSabBRep(
            { {0,0,0}, {4,0,0}, {4,4,0}, {0,4,0}, {2,2,9} },
            { {0,3,2,1}, {0,1,4}, {1,2,4}, {2,3,4}, {3,0,4} });
        LoadedMesh3D mSabA, mSabB;
        bool okSabA = Model3DViewer::InspectMeshFile(writeBytes(L"tet_solid.sab", sabA), mSabA);
        bool okSabB = Model3DViewer::InspectMeshFile(writeBytes(L"pyr_solid.sab", sabB), mSabB);
        tc.Record(okSabA && okSabB &&
                  mSabA.rawVertexCount == 4 && mSabA.rawTriangleCount == 4 &&
                  mSabB.rawVertexCount == 5 && mSabB.rawTriangleCount == 6 &&
                  HashMeshGeometry(mSabA) != HashMeshGeometry(mSabB),
                  L"Stage15.3D.SAB.BinaryAcisDistinct",
                  L"trisA=" + std::to_wstring(mSabA.rawTriangleCount) + L" trisB=" + std::to_wstring(mSabB.rawTriangleCount));

        auto ctmA = BuildSyntheticCtmRaw(
            { {0,0,0}, {4,0,0}, {0,3,0}, {0,0,2} },
            { {{0,2,1}}, {{0,1,3}}, {{1,2,3}}, {{2,0,3}} });
        auto ctmB = BuildSyntheticCtmRaw(
            { {-2,0,-2}, {2,0,-2}, {2,0,2}, {-2,0,2}, {0,7,0} },
            { {{0,3,2}}, {{0,2,1}}, {{0,1,4}}, {{1,2,4}}, {{2,3,4}}, {{3,0,4}} });
        LoadedMesh3D mCtmA, mCtmB;
        bool okCtmA = Model3DViewer::InspectMeshFile(writeBytes(L"tet_raw.ctm", ctmA), mCtmA);
        bool okCtmB = Model3DViewer::InspectMeshFile(writeBytes(L"pyr_raw.ctm", ctmB), mCtmB);
        tc.Record(okCtmA && okCtmB && HashMeshGeometry(mCtmA) != HashMeshGeometry(mCtmB),
                  L"Stage15.3D.CTM.RawNullDistinct");

        auto lazUncompA = BuildSyntheticLasOrLaz({ {0,0,0}, {10,0,0}, {0,10,2} }, 0);
        auto lazUncompB = BuildSyntheticLasOrLaz({ {0,0,0}, {2,0,0}, {0,2,20}, {1,1,40} }, 0);
        LoadedMesh3D mLazA, mLazB;
        bool okLazA = Model3DViewer::InspectMeshFile(writeBytes(L"pts_flat.laz", lazUncompA), mLazA);
        bool okLazB = Model3DViewer::InspectMeshFile(writeBytes(L"pts_tall.laz", lazUncompB), mLazB);
        tc.Record(okLazA && okLazB && HashMeshGeometry(mLazA) != HashMeshGeometry(mLazB),
                  L"Stage15.3D.LAZ.UncompressedPointsDistinct");
    }

    // ========================================================================
    // 13. glTF 2.0 (.glb, .gltf, .vrm): BIN offset/stride, external .bin, base64 quantized strip, sparse, nodes
    // ========================================================================
    {
        LoadedMesh3D mKhrBox;
        bool okKhrBox = Model3DViewer::InspectMeshFile(tdDir + L"\\khronos_box.glb", mKhrBox);
        tc.Record(okKhrBox && mKhrBox.loadedOk && mKhrBox.rawVertexCount == 24 && mKhrBox.rawTriangleCount == 12,
                  L"Stage15.3D.GLTF.KhronosBoxRepo",
                  L"verts=" + std::to_wstring(mKhrBox.rawVertexCount) + L" tris=" + std::to_wstring(mKhrBox.rawTriangleCount));

        auto glbA = BuildStridedGlb20(
            { {0,0,0}, {2,0,0}, {0,3,0}, {0,0,4} },
            { {{0,1,2}}, {{0,1,3}}, {{1,2,3}}, {{2,0,3}} },
            1.0f, 2.0f, 1.0f,  10.0f, -5.0f, 3.0f);
        auto glbB = BuildStridedGlb20(
            { {-1,0,-1}, {1,0,-1}, {1,0,1}, {-1,0,1}, {0,5,0} },
            { {{0,2,1}}, {{0,3,2}}, {{0,1,4}}, {{1,2,4}}, {{2,3,4}}, {{3,0,4}} },
            3.0f, 1.0f, 0.5f, -4.0f, 8.0f, 0.0f);
        LoadedMesh3D mGlbA, mGlbB;
        bool okGlbA = Model3DViewer::InspectMeshFile(writeBytes(L"png_strided_tet.glb", glbA), mGlbA);
        bool okGlbB = Model3DViewer::InspectMeshFile(writeBytes(L"png_strided_pyr.glb", glbB), mGlbB);
        tc.Record(okGlbA && okGlbB &&
                  mGlbA.rawVertexCount == 4 && mGlbA.rawTriangleCount == 4 &&
                  mGlbB.rawVertexCount == 5 && mGlbB.rawTriangleCount == 6 &&
                  HashMeshGeometry(mGlbA) != HashMeshGeometry(mGlbB),
                  L"Stage15.3D.GLTF.StridedPngPrefixSceneDistinct");

        // External sibling .bin + sparse POSITION override on vertex 3
        ByteWriter extBin;
        const Vec3f baseTet[4] = { {0,0,0}, {4,0,0}, {0,4,0}, {0,0,0.1f} };
        for (const auto& v : baseTet) { extBin.F32LE(v.x); extBin.F32LE(v.y); extBin.F32LE(v.z); }
        size_t idxOff = extBin.buf.size();
        const uint16_t tetIdx[12] = { 0,1,2, 0,1,3, 1,2,3, 2,0,3 };
        for (uint16_t idx : tetIdx) extBin.U16LE(idx);
        extBin.Pad4();
        size_t spIdxOff = extBin.buf.size();
        extBin.U16LE(3); // replace vertex 3 via sparse accessor
        extBin.Pad4();
        size_t spValOff = extBin.buf.size();
        extBin.F32LE(0.0f); extBin.F32LE(0.0f); extBin.F32LE(9.0f);
        writeBytes(L"gltf_ext_buf.bin", extBin.buf);

        char extGltfJson[2048];
        std::snprintf(extGltfJson, sizeof(extGltfJson),
            "{"
            "\"asset\":{\"version\":\"2.0\"},"
            "\"buffers\":[{\"uri\":\"gltf_ext_buf.bin\",\"byteLength\":%zu}],"
            "\"bufferViews\":["
              "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48},"
              "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":24},"
              "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":2},"
              "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":12}"
            "],"
            "\"accessors\":["
              "{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\","
               "\"sparse\":{\"count\":1,"
                 "\"indices\":{\"bufferView\":2,\"componentType\":5123},"
                 "\"values\":{\"bufferView\":3}}},"
              "{\"bufferView\":1,\"componentType\":5123,\"count\":12,\"type\":\"SCALAR\"}"
            "],"
            "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}]"
            "}",
            extBin.buf.size(), idxOff, spIdxOff, spValOff);

        // Base64 data: URI .gltf with quantized SHORT (5122, normalized=true) + TRIANGLE_STRIP (mode=5)
        ByteWriter qBin;
        const int16_t qVerts[5][4] = {
            {     0,     0,     0, 0 },
            { 32767,     0,     0, 0 },
            {     0, 16384,     0, 0 },
            { 32767, 16384,  8192, 0 },
            { 16384, 32767, 24576, 0 }
        };
        for (const auto& qv : qVerts) {
            qBin.U16LE(static_cast<uint16_t>(qv[0]));
            qBin.U16LE(static_cast<uint16_t>(qv[1]));
            qBin.U16LE(static_cast<uint16_t>(qv[2]));
            qBin.U16LE(static_cast<uint16_t>(qv[3])); // 8-byte aligned stride
        }
        std::string b64Uri = "data:application/octet-stream;base64," + TfBase64Encode(qBin.buf.data(), qBin.buf.size());
        std::string stripGltf =
            "{\"asset\":{\"version\":\"2.0\"},"
            "\"buffers\":[{\"uri\":\"" + b64Uri + "\",\"byteLength\":" + std::to_string(qBin.buf.size()) + "}],"
            "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":" + std::to_string(qBin.buf.size()) + ",\"byteStride\":8}],"
            "\"accessors\":[{\"bufferView\":0,\"componentType\":5122,\"normalized\":true,\"count\":5,\"type\":\"VEC3\"}],"
            "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"mode\":5}]}]}";

        LoadedMesh3D mExtGltf, mStripGltf;
        bool okExt = Model3DViewer::InspectMeshFile(writeText(L"ext_sparse.gltf", extGltfJson), mExtGltf);
        bool okStrip = Model3DViewer::InspectMeshFile(writeText(L"quant_strip.gltf", stripGltf.c_str()), mStripGltf);
        tc.Record(okExt && okStrip &&
                  mExtGltf.rawVertexCount == 4 && mExtGltf.rawTriangleCount == 4 &&
                  mExtGltf.bboxSize.z > 8.5f && // proves sparse substitution elevated z from 0.1 to 9.0!
                  mStripGltf.rawVertexCount == 5 && mStripGltf.rawTriangleCount == 3 &&
                  HashMeshGeometry(mExtGltf) != HashMeshGeometry(mStripGltf),
                  L"Stage15.3D.GLTF.ExternalBinSparseAndQuantizedStrip");

        // .vrm multi-node humanoid avatar distinctness
        auto vrmA = BuildStridedGlb20(
            { {-1,0,-1}, {1,0,-1}, {1,0,1}, {-1,0,1}, {0,6,0} },
            { {{0,2,1}}, {{0,3,2}}, {{0,1,4}}, {{1,2,4}}, {{2,3,4}}, {{3,0,4}} },
            0.6f, 2.5f, 0.6f, 0.0f, 1.2f, 0.0f);
        auto vrmB = BuildStridedGlb20(
            { {-2,0,0}, {2,0,0}, {0,2,0}, {0,1,3} },
            { {{0,1,2}}, {{0,1,3}}, {{1,2,3}}, {{2,0,3}} },
            2.4f, 0.8f, 1.5f, 0.0f, 0.5f, 0.0f);
        LoadedMesh3D mVrmA, mVrmB;
        bool okVrmA = Model3DViewer::InspectMeshFile(writeBytes(L"avatar_tall.vrm", vrmA), mVrmA);
        bool okVrmB = Model3DViewer::InspectMeshFile(writeBytes(L"avatar_wide.vrm", vrmB), mVrmB);
        tc.Record(okVrmA && okVrmB &&
                  mVrmA.formatName == L"VRoid / glTF Humanoid Avatar (.vrm)" &&
                  mVrmA.rawVertexCount == 5 && mVrmB.rawVertexCount == 4 &&
                  HashMeshGeometry(mVrmA) != HashMeshGeometry(mVrmB),
                  L"Stage15.3D.VRM.MultiNodeAvatarDistinct");
    }

    // ========================================================================
    // 14. COLLADA (.dae): Elf Girl repo + non-position float_array first + polylist stride + skin controller + matrix
    // ========================================================================
    {
        LoadedMesh3D mElf;
        bool okElf = Model3DViewer::InspectMeshFile(tdDir + L"\\collada_elf_girl.dae", mElf);
        tc.Record(okElf && mElf.loadedOk && mElf.rawVertexCount == 7599 && mElf.rawTriangleCount == 14208,
                  L"Stage15.3D.DAE.ElfGirlRepo",
                  L"verts=" + std::to_wstring(mElf.rawVertexCount) + L" tris=" + std::to_wstring(mElf.rawTriangleCount));

        auto buildComplexDae = [](float matScaleX, float matTransZ) -> std::string {
            char buf[4096];
            std::snprintf(buf, sizeof(buf),
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                "<COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" version=\"1.4.1\">\n"
                "  <asset><unit name=\"meter\" meter=\"1\"/><up_axis>Z_UP</up_axis></asset>\n"
                "  <library_geometries>\n"
                "    <geometry id=\"base-geom\">\n"
                "      <mesh>\n"
                "        <source id=\"first-normals\">\n"
                "          <float_array id=\"norm-arr\" count=\"6\">0 0 1 0 1 0</float_array>\n"
                "          <technique_common><accessor source=\"#norm-arr\" count=\"2\" stride=\"3\"/></technique_common>\n"
                "        </source>\n"
                "        <source id=\"second-uvs\">\n"
                "          <float_array id=\"uv-arr\" count=\"4\">0.1 0.2 0.8 0.9</float_array>\n"
                "          <technique_common><accessor source=\"#uv-arr\" count=\"2\" stride=\"2\"/></technique_common>\n"
                "        </source>\n"
                "        <source id=\"real-positions\">\n"
                "          <float_array id=\"pos-arr\" count=\"15\">0 0 0  4 0 0  4 4 0  0 4 0  2 2 5</float_array>\n"
                "          <technique_common><accessor source=\"#pos-arr\" count=\"5\" stride=\"3\"/></technique_common>\n"
                "        </source>\n"
                "        <vertices id=\"base-verts\"><input semantic=\"POSITION\" source=\"#real-positions\"/></vertices>\n"
                "        <polylist count=\"2\">\n"
                "          <input semantic=\"NORMAL\" source=\"#first-normals\" offset=\"0\"/>\n"
                "          <input semantic=\"VERTEX\" source=\"#base-verts\" offset=\"1\"/>\n"
                "          <input semantic=\"TEXCOORD\" source=\"#second-uvs\" offset=\"2\"/>\n"
                "          <vcount>4 3</vcount>\n"
                "          <p>0 0 0  0 1 1  0 2 0  0 3 1   1 0 0  1 1 1  1 4 0</p>\n"
                "        </polylist>\n"
                "      </mesh>\n"
                "    </geometry>\n"
                "    <geometry id=\"rig-geom\">\n"
                "      <mesh>\n"
                "        <source id=\"rig-pos\">\n"
                "          <float_array id=\"rig-arr\" count=\"12\">0 0 0  2 0 0  2 2 0  0 2 3</float_array>\n"
                "          <technique_common><accessor source=\"#rig-arr\" count=\"4\" stride=\"3\"/></technique_common>\n"
                "        </source>\n"
                "        <vertices id=\"rig-verts\"><input semantic=\"POSITION\" source=\"#rig-pos\"/></vertices>\n"
                "        <triangles count=\"2\">\n"
                "          <input semantic=\"VERTEX\" source=\"#rig-verts\" offset=\"0\"/>\n"
                "          <p>0 1 2  0 2 3</p>\n"
                "        </triangles>\n"
                "      </mesh>\n"
                "    </geometry>\n"
                "  </library_geometries>\n"
                "  <library_controllers>\n"
                "    <controller id=\"skin-ctrl\">\n"
                "      <skin source=\"#rig-geom\">\n"
                "        <bind_shape_matrix>1 0 0 6  0 1 0 0  0 0 1 0  0 0 0 1</bind_shape_matrix>\n"
                "      </skin>\n"
                "    </controller>\n"
                "  </library_controllers>\n"
                "  <library_visual_scenes>\n"
                "    <visual_scene id=\"MainScene\">\n"
                "      <node id=\"RootNode\">\n"
                "        <matrix>%.3f 0 0 0  0 1 0 0  0 0 1 %.3f  0 0 0 1</matrix>\n"
                "        <instance_geometry url=\"#base-geom\"/>\n"
                "        <node id=\"ChildRig\"><translate>0 0 4</translate><instance_controller url=\"#skin-ctrl\"/></node>\n"
                "      </node>\n"
                "    </visual_scene>\n"
                "  </library_visual_scenes>\n"
                "  <scene><instance_visual_scene url=\"#MainScene\"/></scene>\n"
                "</COLLADA>\n",
                matScaleX, matTransZ);
            return std::string(buf);
        };

        std::string daeA = buildComplexDae(1.0f, 0.0f);
        std::string daeB = buildComplexDae(3.5f, 12.0f);
        LoadedMesh3D mDaeA, mDaeB;
        bool okDaeA = Model3DViewer::InspectMeshFile(writeText(L"polylist_skin_a.dae", daeA.c_str()), mDaeA);
        bool okDaeB = Model3DViewer::InspectMeshFile(writeText(L"polylist_skin_b.dae", daeB.c_str()), mDaeB);
        tc.Record(okDaeA && okDaeB &&
                  mDaeA.rawVertexCount == 9 && mDaeA.rawTriangleCount == 5 &&
                  mDaeB.rawVertexCount == 9 && mDaeB.rawTriangleCount == 5 &&
                  HashMeshGeometry(mDaeA) != HashMeshGeometry(mDaeB),
                  L"Stage15.3D.DAE.MultiGeomPolylistSkinAndMatrix");
    }

    // ========================================================================
    // 15. IGES 5.3 (.iges): sample.iges + NURBS Surface (128) + NURBS Curve (126) + Transform (124)
    // ========================================================================
    {
        LoadedMesh3D mSampleIges;
        bool okSampleIges = Model3DViewer::InspectMeshFile(tdDir + L"\\sample.iges", mSampleIges);
        tc.Record(okSampleIges && mSampleIges.loadedOk && mSampleIges.rawTriangleCount == 30,
                  L"Stage15.3D.IGES.SampleUnbridgedLines",
                  L"tris=" + std::to_wstring(mSampleIges.rawTriangleCount));

        // Dome: biquadratic Rational B-Spline Surface (128) translated by Entity 124 to (+100, +200, +300).
        // If header ints (K=2, M=2, PROP=0) leaked as XYZ points, bboxSize would exceed 200 instead of ~10x8x10!
        IgesDocBuilder igesDome;
        int xfDe = igesDome.AddEntity(124, "124,1.0,0.0,0.0,100.0,0.0,1.0,0.0,200.0,0.0,0.0,1.0,300.0");
        igesDome.AddEntity(128,
            "128,2,2,2,2,0,0,0,0,0,"
            "0.0,0.0,0.0,1.0,1.0,1.0,"
            "0.0,0.0,0.0,1.0,1.0,1.0,"
            "1.0,1.0,1.0,1.0,2.5,1.0,1.0,1.0,1.0,"
            "0.0,0.0,0.0, 5.0,0.0,2.0, 10.0,0.0,0.0,"
            "0.0,5.0,2.0, 5.0,5.0,8.0, 10.0,5.0,2.0,"
            "0.0,10.0,0.0, 5.0,10.0,2.0, 10.0,10.0,0.0,"
            "0.0,1.0,0.0,1.0",
            xfDe);

        // Saddle + NURBS Curve (126) with Fortran D-exponent notation
        IgesDocBuilder igesSaddle;
        igesSaddle.AddEntity(128,
            "128,2,2,2,2,0,0,1,0,0,"
            "0.0,0.0,0.0,1.0,1.0,1.0,"
            "0.0,0.0,0.0,1.0,1.0,1.0,"
            "1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,"
            "-15.0,-5.0,4.0, 0.0,-5.0,-3.0, 15.0,-5.0,4.0,"
            "-15.0,0.0,-2.0, 0.0,0.0,0.0, 15.0,0.0,-2.0,"
            "-15.0,5.0,4.0, 0.0,5.0,-3.0, 15.0,5.0,4.0,"
            "0.0,1.0,0.0,1.0");
        igesSaddle.AddEntity(126,
            "126,2,2,0,0,0,0,"
            "0.0,0.0,0.0,1.0,1.0,1.0,"
            "1.0,1.5D+00,1.0,"
            "-15.0,0.0,4.0, 0.0,0.0,1.2D+01, 15.0,0.0,4.0,"
            "0.0,1.0");

        std::string domeTxt = igesDome.Finish();
        std::string saddleTxt = igesSaddle.Finish();
        LoadedMesh3D mDome, mSaddle;
        bool okDome = Model3DViewer::InspectMeshFile(writeText(L"nurbs_dome_128.iges", domeTxt.c_str()), mDome);
        bool okSaddle = Model3DViewer::InspectMeshFile(writeText(L"nurbs_saddle_128_126.iges", saddleTxt.c_str()), mSaddle);
        tc.Record(okDome && okSaddle &&
                  mDome.rawTriangleCount >= 400 &&
                  mDome.bboxSize.x > 8.0f && mDome.bboxSize.x < 12.0f &&
                  mDome.bboxSize.y > 3.5f && mDome.bboxSize.y < 10.0f &&
                  mDome.bboxSize.z > 8.0f && mDome.bboxSize.z < 12.0f &&
                  HashMeshGeometry(mDome) != HashMeshGeometry(mSaddle),
                  L"Stage15.3D.IGES.Nurbs128SurfaceAnd126CurveDistinct",
                  L"domeTris=" + std::to_wstring(mDome.rawTriangleCount) +
                  L" domeSpanX=" + std::to_wstring(mDome.bboxSize.x));
    }

    // ========================================================================
    // 16. 3MF (.3mf) & AMF (.amf): Sub-model components, 3x4 build transforms, constellations & PK-ZIP AMF
    // ========================================================================
    {
        const char* subPartModel =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<model unit=\"millimeter\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n"
            "  <resources>\n"
            "    <object id=\"10\" type=\"model\">\n"
            "      <mesh>\n"
            "        <vertices>\n"
            "          <vertex x=\"-1\" y=\"-1\" z=\"0\"/>\n"
            "          <vertex x=\"1\" y=\"-1\" z=\"0\"/>\n"
            "          <vertex x=\"1\" y=\"1\" z=\"0\"/>\n"
            "          <vertex x=\"-1\" y=\"1\" z=\"0\"/>\n"
            "          <vertex x=\"0\" y=\"0\" z=\"3\"/>\n"
            "        </vertices>\n"
            "        <triangles>\n"
            "          <triangle v1=\"0\" v2=\"2\" v3=\"1\"/>\n"
            "          <triangle v1=\"0\" v2=\"3\" v3=\"2\"/>\n"
            "          <triangle v1=\"0\" v2=\"1\" v3=\"4\"/>\n"
            "          <triangle v1=\"1\" v2=\"2\" v3=\"4\"/>\n"
            "          <triangle v1=\"2\" v2=\"3\" v3=\"4\"/>\n"
            "          <triangle v1=\"3\" v2=\"0\" v3=\"4\"/>\n"
            "        </triangles>\n"
            "      </mesh>\n"
            "    </object>\n"
            "  </resources>\n"
            "  <build/>\n"
            "</model>\n";

        auto makeRoot3Mf = [](const char* item2Transform) -> std::string {
            return std::string(
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<model unit=\"millimeter\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\""
                " xmlns:p=\"http://schemas.microsoft.com/3dmanufacturing/production/2015/06\">\n"
                "  <resources>\n"
                "    <object id=\"1\" type=\"model\">\n"
                "      <mesh>\n"
                "        <vertices>\n"
                "          <vertex x=\"0\" y=\"0\" z=\"0\"/>\n"
                "          <vertex x=\"2\" y=\"0\" z=\"0\"/>\n"
                "          <vertex x=\"0\" y=\"2\" z=\"0\"/>\n"
                "          <vertex x=\"0\" y=\"0\" z=\"2\"/>\n"
                "        </vertices>\n"
                "        <triangles>\n"
                "          <triangle v1=\"0\" v2=\"2\" v3=\"1\"/>\n"
                "          <triangle v1=\"0\" v2=\"1\" v3=\"3\"/>\n"
                "          <triangle v1=\"1\" v2=\"2\" v3=\"3\"/>\n"
                "          <triangle v1=\"2\" v2=\"0\" v3=\"3\"/>\n"
                "        </triangles>\n"
                "      </mesh>\n"
                "    </object>\n"
                "    <object id=\"2\" type=\"model\">\n"
                "      <components>\n"
                "        <component objectid=\"10\" p:path=\"/3D/Objects/subpart.model\" transform=\"1 0 0 0 1 0 0 0 1 0 0 2\"/>\n"
                "      </components>\n"
                "    </object>\n"
                "  </resources>\n"
                "  <build>\n"
                "    <item objectid=\"1\" transform=\"1 0 0 0 1 0 0 0 1 -5 0 0\"/>\n"
                "    <item objectid=\"2\" transform=\"") + item2Transform + "\"/>\n"
                "  </build>\n"
                "</model>\n";
        };

        auto zip3mfA = BuildTestZipArchive({
            { "3D/3dmodel.model", makeRoot3Mf("1 0 0 0 1 0 0 0 1 8 0 0"), false },
            { "3D/Objects/subpart.model", subPartModel, false }
        });
        auto zip3mfB = BuildTestZipArchive({
            { "3D/3dmodel.model", makeRoot3Mf("2 0 0 0 2 0 0 0 3 25 10 0"), true },
            { "3D/Objects/subpart.model", subPartModel, true }
        });
        LoadedMesh3D m3mfA, m3mfB;
        bool ok3mfA = Model3DViewer::InspectMeshFile(writeBytes(L"assembly_a.3mf", zip3mfA), m3mfA);
        bool ok3mfB = Model3DViewer::InspectMeshFile(writeBytes(L"assembly_b.3mf", zip3mfB), m3mfB);
        tc.Record(ok3mfA && ok3mfB &&
                  m3mfA.rawVertexCount == 9 && m3mfA.rawTriangleCount == 10 &&
                  m3mfB.rawVertexCount == 9 && m3mfB.rawTriangleCount == 10 &&
                  HashMeshGeometry(m3mfA) != HashMeshGeometry(m3mfB),
                  L"Stage15.3D.3MF.SubmodelComponentsAndBuildTransforms");

        // Multi-object AMF (uncompressed XML vs PK-ZIP Deflate compressed)
        auto makeAmfXml = [](float dx2, float rz2) -> std::string {
            char buf[4096];
            std::snprintf(buf, sizeof(buf),
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<amf unit=\"millimeter\" version=\"1.1\">\n"
                "  <object id=\"1\">\n"
                "    <mesh>\n"
                "      <vertices>\n"
                "        <vertex><coordinates><x>0</x><y>0</y><z>0</z></coordinates></vertex>\n"
                "        <vertex><coordinates><x>3</x><y>0</y><z>0</z></coordinates></vertex>\n"
                "        <vertex><coordinates><x>0</x><y>3</y><z>0</z></coordinates></vertex>\n"
                "        <vertex><coordinates><x>0</x><y>0</y><z>3</z></coordinates></vertex>\n"
                "      </vertices>\n"
                "      <volume>\n"
                "        <triangle><v1>0</v1><v2>2</v2><v3>1</v3></triangle>\n"
                "        <triangle><v1>0</v1><v2>1</v2><v3>3</v3></triangle>\n"
                "        <triangle><v1>1</v1><v2>2</v2><v3>3</v3></triangle>\n"
                "        <triangle><v1>2</v1><v2>0</v2><v3>3</v3></triangle>\n"
                "      </volume>\n"
                "    </mesh>\n"
                "  </object>\n"
                "  <object id=\"2\">\n"
                "    <mesh>\n"
                "      <vertices>\n"
                "        <vertex><coordinates><x>-1</x><y>-1</y><z>0</z></coordinates></vertex>\n"
                "        <vertex><coordinates><x>1</x><y>-1</y><z>0</z></coordinates></vertex>\n"
                "        <vertex><coordinates><x>1</x><y>1</y><z>0</z></coordinates></vertex>\n"
                "        <vertex><coordinates><x>-1</x><y>1</y><z>0</z></coordinates></vertex>\n"
                "        <vertex><coordinates><x>0</x><y>0</y><z>4</z></coordinates></vertex>\n"
                "      </vertices>\n"
                "      <volume>\n"
                "        <triangle><v1>0</v1><v2>2</v2><v3>1</v3></triangle>\n"
                "        <triangle><v1>0</v1><v2>3</v2><v3>2</v3></triangle>\n"
                "        <triangle><v1>0</v1><v2>1</v2><v3>4</v3></triangle>\n"
                "        <triangle><v1>1</v1><v2>2</v2><v3>4</v3></triangle>\n"
                "        <triangle><v1>2</v1><v2>3</v2><v3>4</v3></triangle>\n"
                "        <triangle><v1>3</v1><v2>0</v2><v3>4</v3></triangle>\n"
                "      </volume>\n"
                "    </mesh>\n"
                "  </object>\n"
                "  <constellation id=\"100\">\n"
                "    <instance objectid=\"1\" deltax=\"-4\" deltay=\"0\" deltaz=\"0\"/>\n"
                "    <instance objectid=\"2\" deltax=\"%.2f\" rz=\"%.2f\"><deltaz>2.5</deltaz></instance>\n"
                "  </constellation>\n"
                "</amf>\n",
                dx2, rz2);
            return std::string(buf);
        };

        std::string amfXmlPlain = makeAmfXml(6.0f, 0.0f);
        auto amfZipDeflated = BuildTestZipArchive({
            { "plate_build.amf", makeAmfXml(24.0f, 45.0f), true }
        });
        LoadedMesh3D mAmfPlain, mAmfZip;
        bool okAmfPlain = Model3DViewer::InspectMeshFile(writeText(L"constel_plain.amf", amfXmlPlain.c_str()), mAmfPlain);
        bool okAmfZip   = Model3DViewer::InspectMeshFile(writeBytes(L"constel_deflate.amf", amfZipDeflated), mAmfZip);
        tc.Record(okAmfPlain && okAmfZip &&
                  mAmfPlain.rawVertexCount == 9 && mAmfPlain.rawTriangleCount == 10 &&
                  mAmfZip.rawVertexCount == 9 && mAmfZip.rawTriangleCount == 10 &&
                  HashMeshGeometry(mAmfPlain) != HashMeshGeometry(mAmfZip),
                  L"Stage15.3D.AMF.ConstellationAndCompressedZip");
    }

    // ========================================================================
    // 17. Spatial ACIS (.sat) & Siemens Parasolid XT (.x_t) B-Rep Solids
    // ========================================================================
    {
        // ACIS .sat #1: Planar face with inner hole loop + body transform (scale=2, translate=(10,-5,3)) -> 8 verts, 8 tris
        const char* satDrilledXf =
            "700 0 1 0\n"
            "@13 Explorer2Test @8 ACIS 7.0 @4 2026\n"
            "1 9.9999999999999995e-007 1e-010\n"
            "-0 body $-1 $2 $-1 $1 #\n"
            "-1 transform $-1 1 0 0 0 1 0 0 0 1 10 -5 3 2 rotate_f reflect_f shear_f #\n"
            "-2 lump $-1 $-1 $3 $0 #\n"
            "-3 shell $-1 $-1 $-1 $4 $-1 $2 #\n"
            "-4 face $-1 $-1 $5 $3 $-1 $7 forward single #\n"
            "-5 loop $-1 $6 $10 $4 #\n"
            "-6 loop $-1 $-1 $14 $4 #\n"
            "-7 plane-surface $-1 0 0 0 0 0 1 1 0 0 forward_v I I I I #\n"
            "-8 straight-curve $-1 0 0 0 1 0 0 I I #\n"
            "-10 coedge $-1 $11 $13 $-1 $34 forward $5 #\n"
            "-11 coedge $-1 $12 $10 $-1 $35 forward $5 #\n"
            "-12 coedge $-1 $13 $11 $-1 $36 forward $5 #\n"
            "-13 coedge $-1 $10 $12 $-1 $37 forward $5 #\n"
            "-14 coedge $-1 $15 $17 $-1 $38 forward $6 #\n"
            "-15 coedge $-1 $16 $14 $-1 $39 forward $6 #\n"
            "-16 coedge $-1 $17 $15 $-1 $40 forward $6 #\n"
            "-17 coedge $-1 $14 $16 $-1 $41 forward $6 #\n"
            "-18 vertex $-1 $-1 $26 #\n"
            "-19 vertex $-1 $-1 $27 #\n"
            "-20 vertex $-1 $-1 $28 #\n"
            "-21 vertex $-1 $-1 $29 #\n"
            "-22 vertex $-1 $-1 $30 #\n"
            "-23 vertex $-1 $-1 $31 #\n"
            "-24 vertex $-1 $-1 $32 #\n"
            "-25 vertex $-1 $-1 $33 #\n"
            "-26 point $-1 -5 -5 0 #\n"
            "-27 point $-1 5 -5 0 #\n"
            "-28 point $-1 5 5 0 #\n"
            "-29 point $-1 -5 5 0 #\n"
            "-30 point $-1 -2 -2 0 #\n"
            "-31 point $-1 -2 2 0 #\n"
            "-32 point $-1 2 2 0 #\n"
            "-33 point $-1 2 -2 0 #\n"
            "-34 edge $-1 $18 $19 $-1 $8 forward #\n"
            "-35 edge $-1 $19 $20 $-1 $8 forward #\n"
            "-36 edge $-1 $20 $21 $-1 $8 forward #\n"
            "-37 edge $-1 $21 $18 $-1 $8 forward #\n"
            "-38 edge $-1 $22 $23 $-1 $8 forward #\n"
            "-39 edge $-1 $23 $24 $-1 $8 forward #\n"
            "-40 edge $-1 $24 $25 $-1 $8 forward #\n"
            "-41 edge $-1 $25 $22 $-1 $8 forward #\n"
            "End-of-ACIS-data\n";

        // ACIS .sat #2: Closed solid cylinder (cone-surface wall + 2 plane-surface caps + 2 ellipse-curve circles)
        const char* satCylinder =
            "700 0 1 0\n"
            "@13 Explorer2Test @8 ACIS 7.0 @4 2026\n"
            "1 9.9999999999999995e-007 1e-010\n"
            "-0 body $-1 $1 $-1 $-1 #\n"
            "-1 lump $-1 $-1 $2 $0 #\n"
            "-2 shell $-1 $-1 $-1 $3 $-1 $1 #\n"
            "-3 face $-1 $4 $6 $2 $-1 $10 reversed single #\n"
            "-4 face $-1 $5 $7 $2 $-1 $11 forward single #\n"
            "-5 face $-1 $-1 $8 $2 $-1 $12 forward single #\n"
            "-6 loop $-1 $-1 $15 $3 #\n"
            "-7 loop $-1 $-1 $16 $4 #\n"
            "-8 loop $-1 $9 $17 $5 #\n"
            "-9 loop $-1 $-1 $18 $5 #\n"
            "-10 plane-surface $-1 0 0 0 0 0 1 1 0 0 forward_v I I I I #\n"
            "-11 plane-surface $-1 0 0 12 0 0 1 1 0 0 forward_v I I I I #\n"
            "-12 cone-surface $-1 0 0 0 0 0 1 4 0 0 1 0 1 forward_v I I I I #\n"
            "-13 ellipse-curve $-1 0 0 0 0 0 1 4 0 0 1 I I #\n"
            "-14 ellipse-curve $-1 0 0 12 0 0 1 4 0 0 1 I I #\n"
            "-15 coedge $-1 $15 $15 $-1 $19 reversed $6 #\n"
            "-16 coedge $-1 $16 $16 $-1 $20 forward $7 #\n"
            "-17 coedge $-1 $17 $17 $-1 $19 forward $8 #\n"
            "-18 coedge $-1 $18 $18 $-1 $20 reversed $9 #\n"
            "-19 edge $-1 $21 $21 $15 $13 forward #\n"
            "-20 edge $-1 $22 $22 $16 $14 forward #\n"
            "-21 vertex $-1 $19 $23 #\n"
            "-22 vertex $-1 $20 $24 #\n"
            "-23 point $-1 4 0 0 #\n"
            "-24 point $-1 4 0 12 #\n"
            "End-of-ACIS-data\n";

        LoadedMesh3D mSatA, mSatB;
        bool okSatA = Model3DViewer::InspectMeshFile(writeText(L"drilled_plate_xf.sat", satDrilledXf), mSatA);
        bool okSatB = Model3DViewer::InspectMeshFile(writeText(L"solid_cyl.sat", satCylinder), mSatB);
        tc.Record(okSatA && okSatB &&
                  mSatA.rawVertexCount == 8 && mSatA.rawTriangleCount == 8 &&
                  std::fabs(mSatA.bboxSize.x - 20.0f) < 0.05f &&
                  mSatB.rawVertexCount == 48 && mSatB.rawTriangleCount == 92 &&
                  HashMeshGeometry(mSatA) != HashMeshGeometry(mSatB),
                  L"Stage15.3D.SAT.BRepHoleTransformAndCylinder",
                  L"trisA=" + std::to_wstring(mSatA.rawTriangleCount) + L" trisB=" + std::to_wstring(mSatB.rawTriangleCount));

        // Parasolid XT (.x_t) #1: 4-face B-Rep tetrahedron -> 4 verts, 4 tris
        const char* xtTetra =
            "**PART1;MC=WIN64;\n"
            "SCH_3300002_33000;\n"
            "**END_OF_HEADER**\n"
            "body 1 $2 ?;\n"
            "shell 2 $10;\n"
            "point 30 0.0 0.0 0.0;\n"
            "point 31 6.0 0.0 0.0;\n"
            "point 32 0.0 8.0 0.0;\n"
            "point 33 0.0 0.0 10.0;\n"
            "vertex 40 $30;\n"
            "vertex 41 $31;\n"
            "vertex 42 $32;\n"
            "vertex 43 $33;\n"
            "line 50 0.0 0.0 0.0 1.0 0.0 0.0;\n"
            "plane 60 0.0 0.0 0.0 0.0 0.0 -1.0;\n"
            "plane 61 0.0 0.0 0.0 0.0 -1.0 0.0;\n"
            "plane 62 0.0 0.0 0.0 -1.0 0.0 0.0;\n"
            "plane 63 6.0 0.0 0.0 0.64 0.48 0.6;\n"
            "edge 70 $40 $41 $50 +;\n"
            "edge 71 $41 $42 $50 +;\n"
            "edge 72 $42 $40 $50 +;\n"
            "edge 73 $40 $43 $50 +;\n"
            "edge 74 $41 $43 $50 +;\n"
            "edge 75 $42 $43 $50 +;\n"
            "fin 80 $81 $70 -;\n"
            "fin 81 $82 $72 -;\n"
            "fin 82 $80 $71 -;\n"
            "loop 20 $10 $80;\n"
            "face 10 $11 $20 $60 +;\n"
            "fin 83 $84 $70 +;\n"
            "fin 84 $85 $74 +;\n"
            "fin 85 $83 $73 -;\n"
            "loop 21 $11 $83;\n"
            "face 11 $12 $21 $61 +;\n"
            "fin 86 $87 $72 +;\n"
            "fin 87 $88 $73 +;\n"
            "fin 88 $86 $75 -;\n"
            "loop 22 $12 $86;\n"
            "face 12 $13 $22 $62 +;\n"
            "fin 89 $90 $71 +;\n"
            "fin 90 $91 $75 +;\n"
            "fin 91 $89 $74 -;\n"
            "loop 23 $13 $89;\n"
            "face 13 ? $23 $63 +;\n"
            "**END_OF_PART**\n";

        // Parasolid XT (.x_t) #2: Capped cylinder (cylinder surface + 2 planes + 2 circles) -> 48 verts, 92 tris
        const char* xtCylinder =
            "**PART1;MC=WIN64;\n"
            "SCH_3300002_33000;\n"
            "**END_OF_HEADER**\n"
            "body 1 $2 ?;\n"
            "shell 2 $10;\n"
            "point 30 3.0 0.0 0.0;\n"
            "point 31 3.0 0.0 9.0;\n"
            "vertex 40 $30;\n"
            "vertex 41 $31;\n"
            "circle 50 0.0 0.0 0.0 0.0 0.0 1.0 1.0 0.0 0.0 3.0;\n"
            "circle 51 0.0 0.0 9.0 0.0 0.0 1.0 1.0 0.0 0.0 3.0;\n"
            "plane 60 0.0 0.0 0.0 0.0 0.0 -1.0;\n"
            "plane 61 0.0 0.0 9.0 0.0 0.0 1.0;\n"
            "cylinder 62 0.0 0.0 0.0 0.0 0.0 1.0 3.0;\n"
            "edge 70 $40 $40 $50 +;\n"
            "edge 71 $41 $41 $51 +;\n"
            "fin 80 $80 $70 -;\n"
            "loop 20 $10 $80;\n"
            "face 10 $11 $20 $60 +;\n"
            "fin 81 $81 $71 +;\n"
            "loop 21 $11 $81;\n"
            "face 11 $12 $21 $61 +;\n"
            "fin 82 $82 $70 +;\n"
            "fin 83 $83 $71 -;\n"
            "loop 22 $12 $82 $23;\n"
            "loop 23 $12 $83 ?;\n"
            "face 12 ? $22 $62 +;\n"
            "**END_OF_PART**\n";

        LoadedMesh3D mXtA, mXtB;
        bool okXtA = Model3DViewer::InspectMeshFile(writeText(L"pk_tetra.x_t", xtTetra), mXtA);
        bool okXtB = Model3DViewer::InspectMeshFile(writeText(L"pk_cyl.x_t", xtCylinder), mXtB);
        tc.Record(okXtA && okXtB &&
                  mXtA.rawVertexCount == 4 && mXtA.rawTriangleCount == 4 &&
                  mXtB.rawVertexCount == 48 && mXtB.rawTriangleCount == 92 &&
                  HashMeshGeometry(mXtA) != HashMeshGeometry(mXtB),
                  L"Stage15.3D.ParasolidXT.PKTopologyDistinct",
                  L"trisA=" + std::to_wstring(mXtA.rawTriangleCount) + L" trisB=" + std::to_wstring(mXtB.rawTriangleCount));
    }

    // ========================================================================
    // 18. ASTM E2807 (.e57): Multi-page CRC32C Float32 & 15-bit ScaledInteger + Pose + InvalidState
    // ========================================================================
    {
        std::vector<Vec3f> pts120;
        pts120.reserve(120);
        for (int i = 0; i < 120; ++i) {
            float ang = static_cast<float>(i) * 0.25f;
            pts120.push_back({ 6.0f * std::cos(ang), 6.0f * std::sin(ang), static_cast<float>(i) * 0.15f });
        }
        auto e57Float = BuildSyntheticE57Float32(pts120);

        // 10 ScaledInteger records where 2 have cartesianInvalidState=1 -> exactly 8 valid points survive!
        std::vector<Vec3f> pts10 = {
            { -4.0f, -3.0f,  0.0f }, {  4.0f, -3.0f,  0.0f },
            {  4.0f,  3.0f,  0.0f }, { -4.0f,  3.0f,  0.0f },
            { -4.0f, -3.0f, 12.0f }, {  4.0f, -3.0f, 12.0f },
            {  4.0f,  3.0f, 12.0f }, { -4.0f,  3.0f, 12.0f },
            { 95.0f, 95.0f, 95.0f }, // invalid #1 (must be filtered out!)
            {-95.0f,-95.0f,-95.0f }  // invalid #2 (must be filtered out!)
        };
        std::vector<uint8_t> inv10 = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1 };
        auto e57Scaled = BuildSyntheticE57ScaledPose(pts10, inv10, 25.0, -10.0, 5.0);

        LoadedMesh3D mE57A, mE57B;
        bool okE57A = Model3DViewer::InspectMeshFile(writeBytes(L"scan_paged_f32.e57", e57Float), mE57A);
        bool okE57B = Model3DViewer::InspectMeshFile(writeBytes(L"scan_scaled_pose.e57", e57Scaled), mE57B);
        tc.Record(okE57A && okE57B && e57Float.size() >= 2048 &&
                  mE57A.rawVertexCount == 120 * 4 && mE57A.rawTriangleCount == 120 * 4 &&
                  mE57B.rawVertexCount == 8 * 4 && mE57B.rawTriangleCount == 8 * 4 &&
                  mE57B.bboxSize.x < 12.0f && // proves invalid +/-95 outliers were skipped!
                  HashMeshGeometry(mE57A) != HashMeshGeometry(mE57B),
                  L"Stage15.3D.E57.PagedCrc32cFloatAndScaledInt",
                  L"trisA=" + std::to_wstring(mE57A.rawTriangleCount) + L" trisB=" + std::to_wstring(mE57B.rawTriangleCount));
    }

    // ========================================================================
    // 19. Valve GoldSrc (.mdl v10) & Source Engine (.mdl v48 + .vvd + .dx90.vtx)
    // ========================================================================
    {
        auto gsCompact   = BuildSyntheticGoldSrcMdl10(0.0f);
        auto gsStretched = BuildSyntheticGoldSrcMdl10(32.0f);
        LoadedMesh3D mGsA, mGsB;
        bool okGsA = Model3DViewer::InspectMeshFile(writeBytes(L"goldsrc_compact.mdl", gsCompact), mGsA);
        bool okGsB = Model3DViewer::InspectMeshFile(writeBytes(L"goldsrc_stretched.mdl", gsStretched), mGsB);
        tc.Record(okGsA && okGsB &&
                  mGsA.rawVertexCount == 5 && mGsA.rawTriangleCount == 6 &&
                  mGsB.rawVertexCount == 5 && mGsB.rawTriangleCount == 6 &&
                  mGsA.bboxSize.x < 6.0f && mGsB.bboxSize.x > 30.0f &&
                  HashMeshGeometry(mGsA) != HashMeshGeometry(mGsB),
                  L"Stage15.3D.MDL.GoldSrcV10BoneTransform");

        auto srcSetA = BuildSyntheticSourceV48Set(
            { {0,0,0}, {6,0,0}, {0,5,0}, {0,0,8} },
            { {{0,1,2}}, {{0,3,1}}, {{1,3,2}}, {{2,3,0}} },
            0x12345678u, 0x12345678u, 0x12345678u);
        writeBytes(L"src_tet.vvd", srcSetA.vvd);
        writeBytes(L"src_tet.dx90.vtx", srcSetA.vtx);
        std::wstring srcTetMdlPath = writeBytes(L"src_tet.mdl", srcSetA.mdl);

        auto srcSetB = BuildSyntheticSourceV48Set(
            { {-3,-3,0}, {3,-3,0}, {3,3,0}, {-3,3,0}, {0,0,10} },
            { {{0,2,1}}, {{0,3,2}}, {{0,1,4}}, {{1,2,4}}, {{2,3,4}}, {{3,0,4}} },
            0x22334455u, 0x22334455u, 0x22334455u);
        writeBytes(L"src_pyr.vvd", srcSetB.vvd);
        writeBytes(L"src_pyr.dx90.vtx", srcSetB.vtx);
        std::wstring srcPyrMdlPath = writeBytes(L"src_pyr.mdl", srcSetB.mdl);

        // Standalone .mdl without .vvd/.vtx on disk -> falls back cleanly to mstudiobone_t skeleton
        std::wstring srcSkelOnlyPath = writeBytes(L"src_skel_only.mdl", srcSetA.mdl);

        LoadedMesh3D mSrcA, mSrcB, mSrcSkel;
        bool okSrcA    = Model3DViewer::InspectMeshFile(srcTetMdlPath, mSrcA);
        bool okSrcB    = Model3DViewer::InspectMeshFile(srcPyrMdlPath, mSrcB);
        bool okSrcSkel = Model3DViewer::InspectMeshFile(srcSkelOnlyPath, mSrcSkel);
        tc.Record(okSrcA && okSrcB && okSrcSkel &&
                  mSrcA.rawVertexCount == 4 && mSrcA.rawTriangleCount == 4 &&
                  mSrcB.rawVertexCount == 5 && mSrcB.rawTriangleCount == 6 &&
                  mSrcSkel.rawTriangleCount >= 16 &&
                  HashMeshGeometry(mSrcA) != HashMeshGeometry(mSrcB) &&
                  HashMeshGeometry(mSrcA) != HashMeshGeometry(mSrcSkel),
                  L"Stage15.3D.MDL.SourceV48VvdVtxAndBoneFallback",
                  L"trisA=" + std::to_wstring(mSrcA.rawTriangleCount) + L" trisB=" + std::to_wstring(mSrcB.rawTriangleCount));
    }

    // ========================================================================
    // 20. PCL Point Cloud (.pcd) & Stanford PLY (.ply): LZF SoA, Big-Endian Double, Tristrips & Point Cloud
    // ========================================================================
    {
        LoadedMesh3D mRepoPcd;
        bool okRepoPcd = Model3DViewer::InspectMeshFile(tdDir + L"\\lidar_scan.pcd", mRepoPcd);
        tc.Record(okRepoPcd && mRepoPcd.loadedOk && mRepoPcd.rawVertexCount == 24 && mRepoPcd.rawTriangleCount == 24,
                  L"Stage15.3D.PCD.RepoAscii",
                  L"verts=" + std::to_wstring(mRepoPcd.rawVertexCount));

        // Build two distinct PCL v0.7 DATA binary_compressed 16B-stride PointXYZRGB files in SoA column-major layout!
        std::vector<Vec3f> ringPts;
        std::vector<uint32_t> ringRgb;
        for (int i = 0; i < 16; ++i) {
            float a = static_cast<float>(i) * 0.3926991f;
            ringPts.push_back({ 6.0f * std::cos(a), 5.0f * std::sin(a), (i & 1) ? 0.5f : 0.0f });
            ringRgb.push_back(0x00FF8822u);
        }
        std::vector<Vec3f> helixPts;
        std::vector<uint32_t> helixRgb;
        for (int i = 0; i < 20; ++i) {
            float a = static_cast<float>(i) * 0.5f;
            helixPts.push_back({ 1.5f * std::cos(a), 1.5f * std::sin(a), static_cast<float>(i) * 1.5f });
            helixRgb.push_back(0x0022CC88u);
        }
        size_t compSzA = 0, uncompSzA = 0, compSzB = 0, uncompSzB = 0;
        auto pcdSoaA = BuildPcdBinaryCompressedSoA(ringPts, ringRgb, &compSzA, &uncompSzA);
        auto pcdSoaB = BuildPcdBinaryCompressedSoA(helixPts, helixRgb, &compSzB, &uncompSzB);

        LoadedMesh3D mPcdA, mPcdB;
        bool okPcdA = Model3DViewer::InspectMeshFile(writeBytes(L"lzf_soa_ring.pcd", pcdSoaA), mPcdA);
        bool okPcdB = Model3DViewer::InspectMeshFile(writeBytes(L"lzf_soa_helix.pcd", pcdSoaB), mPcdB);
        tc.Record(okPcdA && okPcdB &&
                  compSzA < uncompSzA && compSzB < uncompSzB &&
                  mPcdA.rawVertexCount == 16 * 4 && mPcdA.rawTriangleCount == 16 * 4 &&
                  mPcdB.rawVertexCount == 20 * 4 && mPcdB.rawTriangleCount == 20 * 4 &&
                  mPcdB.bboxSize.y > 26.0f && // proves SoA Z-column was reconstructed properly (mapped to viewer Y)!
                  HashMeshGeometry(mPcdA) != HashMeshGeometry(mPcdB),
                  L"Stage15.3D.PCD.BinaryCompressedLzfSoADistinct",
                  L"compA=" + std::to_wstring(compSzA) + L"/" + std::to_wstring(uncompSzA) +
                  L" spanYB=" + std::to_wstring(mPcdB.bboxSize.y));

        // Repo ASCII PLY + Binary Big-Endian PLY (double XYZ + float normals + uchar RGB + tristrips with -1 restart) + Point-Cloud PLY
        LoadedMesh3D mRepoPly;
        bool okRepoPly = Model3DViewer::InspectMeshFile(tdDir + L"\\octahedron_mesh.ply", mRepoPly);

        const char* bePlyHdr =
            "ply\n"
            "format binary_big_endian 1.0\n"
            "element vertex 7\n"
            "property double x\n"
            "property double y\n"
            "property double z\n"
            "property float nx\n"
            "property float ny\n"
            "property float nz\n"
            "property uchar red\n"
            "property uchar green\n"
            "property uchar blue\n"
            "element tristrips 1\n"
            "property list int int vertex_indices\n"
            "end_header\n";
        ByteWriter bePly;
        bePly.Bytes(bePlyHdr, std::strlen(bePlyHdr));
        auto wrF64BE = [&](double d) {
            uint64_t u = 0;
            std::memcpy(&u, &d, 8);
            bePly.U32BE(static_cast<uint32_t>(u >> 32));
            bePly.U32BE(static_cast<uint32_t>(u & 0xFFFFFFFFu));
        };
        const double beVerts[7][3] = {
            { 0.0, 0.0, 0.0 }, { 4.0, 0.0, 0.0 }, { 0.0, 4.0, 0.0 }, { 4.0, 4.0, 2.0 },
            { 8.0, 0.0, 0.0 }, { 12.0, 0.0, 0.0 }, { 10.0, 5.0, 3.0 }
        };
        for (int i = 0; i < 7; ++i) {
            wrF64BE(beVerts[i][0]);
            wrF64BE(beVerts[i][1]);
            wrF64BE(beVerts[i][2]);
            bePly.F32BE(0.0f); bePly.F32BE(0.0f); bePly.F32BE(1.0f);
            bePly.U8(static_cast<uint8_t>(40 + i * 25));
            bePly.U8(160u);
            bePly.U8(220u);
        }
        // Strip: [0, 1, 2, 3, -1, 4, 5, 6] -> 2 tris from first strip + 1 tri from restarted strip = 3 tris!
        const int32_t stripList[8] = { 0, 1, 2, 3, -1, 4, 5, 6 };
        bePly.I32BE(8);
        for (int32_t idx : stripList) bePly.I32BE(idx);

        // Point-cloud-only binary_little_endian PLY (0 faces, 10 vertices -> 40 splat verts & 40 splat tris)
        const char* pcPlyHdr =
            "ply\n"
            "format binary_little_endian 1.0\n"
            "element vertex 10\n"
            "property float x\n"
            "property float y\n"
            "property float z\n"
            "property uchar red\n"
            "property uchar green\n"
            "property uchar blue\n"
            "element face 0\n"
            "property list uchar int vertex_indices\n"
            "end_header\n";
        ByteWriter pcPly;
        pcPly.Bytes(pcPlyHdr, std::strlen(pcPlyHdr));
        for (int i = 0; i < 10; ++i) {
            pcPly.F32LE(static_cast<float>(i) * 1.2f);
            pcPly.F32LE(std::sin(static_cast<float>(i)) * 3.0f);
            pcPly.F32LE(static_cast<float>(i % 3) * 2.0f);
            pcPly.U8(255u); pcPly.U8(128u); pcPly.U8(64u);
        }

        LoadedMesh3D mBePly, mPcPly;
        bool okBePly = Model3DViewer::InspectMeshFile(writeBytes(L"big_endian_tristrip.ply", bePly.buf), mBePly);
        bool okPcPly = Model3DViewer::InspectMeshFile(writeBytes(L"point_cloud_only.ply", pcPly.buf), mPcPly);
        tc.Record(okRepoPly && mRepoPly.rawVertexCount == 6 && mRepoPly.rawTriangleCount == 8 &&
                  okBePly && mBePly.rawVertexCount == 7 && mBePly.rawTriangleCount == 3 &&
                  mBePly.bboxSize.x > 11.5f &&
                  okPcPly && mPcPly.rawVertexCount == 10 * 4 && mPcPly.rawTriangleCount == 10 * 4 &&
                  HashMeshGeometry(mBePly) != HashMeshGeometry(mPcPly),
                  L"Stage15.3D.PLY.BinaryBigEndianTristripsAndPointCloud",
                  L"beTris=" + std::to_wstring(mBePly.rawTriangleCount) + L" pcTris=" + std::to_wstring(mPcPly.rawTriangleCount));
    }

    // ========================================================================
    // 21. VTK Legacy 1.0–5.1 (.vtk) & Gmsh FEM 2.2 / 4.1 (.msh): Volumetric Interior Culling & Sparse Binary
    // ========================================================================
    {
        LoadedMesh3D mTorusVtk;
        bool okTorusVtk = Model3DViewer::InspectMeshFile(tdDir + L"\\torus_mesh.vtk", mTorusVtk);

        // 2 adjacent VTK_HEXAHEDRON (type 12) sharing interior quad (4,5,6,7):
        // 12 total quads - 2 shared interior faces = 10 exterior boundary quads = 20 triangles!
        const char* vtkTwoHex =
            "# vtk DataFile Version 3.0\n"
            "Two Adjacent Hexahedra\n"
            "ASCII\n"
            "DATASET UNSTRUCTURED_GRID\n"
            "POINTS 12 float\n"
            "0 0 0  2 0 0  2 2 0  0 2 0\n"
            "0 0 2  2 0 2  2 2 2  0 2 2\n"
            "0 0 4  2 0 4  2 2 4  0 2 4\n"
            "CELLS 2 18\n"
            "8 0 1 2 3 4 5 6 7\n"
            "8 4 5 6 7 8 9 10 11\n"
            "CELL_TYPES 2\n"
            "12\n12\n";

        // 2 adjacent VTK_TETRA (type 10) sharing interior tri (0,1,2): 8 - 2 = 6 exterior boundary triangles!
        const char* vtkTwoTet =
            "# vtk DataFile Version 3.0\n"
            "Two Adjacent Tetrahedra\n"
            "ASCII\n"
            "DATASET UNSTRUCTURED_GRID\n"
            "POINTS 5 float\n"
            "0 0 0  4 0 0  2 3 0  2 1 4  2 1 -4\n"
            "CELLS 2 10\n"
            "4 0 1 2 3\n"
            "4 0 2 1 4\n"
            "CELL_TYPES 2\n"
            "10\n10\n";

        // VTK 5.1 POLYDATA with OFFSETS + CONNECTIVITY (both implicit counts & explicit OFFSETS <k> <type> / CONNECTIVITY <m> <type>)
        const char* vtk51Offsets =
            "# vtk DataFile Version 5.1\n"
            "vtk output\n"
            "ASCII\n"
            "DATASET POLYDATA\n"
            "POINTS 5 float\n"
            "-2 -2 0  2 -2 0  2 2 0  -2 2 0  0 0 5\n"
            "POLYGONS 6 16\n"
            "OFFSETS vtktypeint64\n"
            "0 4 7 10 13 16\n"
            "CONNECTIVITY vtktypeint64\n"
            "0 3 2 1  0 1 4  1 2 4  2 3 4  3 0 4\n";

        const char* vtk50ExplicitOff =
            "# vtk DataFile Version 5.0\n"
            "vtk explicit offsets\n"
            "ASCII\n"
            "DATASET POLYDATA\n"
            "POINTS 4 float\n"
            "0 0 0  3 0 0  0 3 0  0 0 4\n"
            "POLYGONS 4 12\n"
            "OFFSETS 5 vtktypeint32\n"
            "0 3 6 9 12\n"
            "CONNECTIVITY 12 vtktypeint32\n"
            "0 2 1  0 1 3  1 2 3  2 0 3\n";

        // VTK STRUCTURED_POINTS 2D heightfield with POINT_DATA SCALARS
        const char* vtkStructPts =
            "# vtk DataFile Version 3.0\n"
            "Heightfield Grid\n"
            "ASCII\n"
            "DATASET STRUCTURED_POINTS\n"
            "DIMENSIONS 4 4 1\n"
            "ORIGIN 0 0 0\n"
            "SPACING 2 2 1\n"
            "POINT_DATA 16\n"
            "SCALARS elevation float 1\n"
            "LOOKUP_TABLE default\n"
            "0 1 1 0  1 5 6 1  1 6 7 1  0 1 1 0\n";

        LoadedMesh3D mHexVtk, mTetVtk, mVtk51, mVtk50, mVtkSp;
        bool okHexVtk = Model3DViewer::InspectMeshFile(writeText(L"two_hex_ug.vtk", vtkTwoHex), mHexVtk);
        bool okTetVtk = Model3DViewer::InspectMeshFile(writeText(L"two_tet_ug.vtk", vtkTwoTet), mTetVtk);
        bool okVtk51  = Model3DViewer::InspectMeshFile(writeText(L"pyr_v51_offsets.vtk", vtk51Offsets), mVtk51);
        bool okVtk50  = Model3DViewer::InspectMeshFile(writeText(L"tet_v50_expoff.vtk", vtk50ExplicitOff), mVtk50);
        bool okVtkSp  = Model3DViewer::InspectMeshFile(writeText(L"height_sp.vtk", vtkStructPts), mVtkSp);
        tc.Record(okTorusVtk && mTorusVtk.rawVertexCount == 40 && mTorusVtk.rawTriangleCount == 80 &&
                  okHexVtk && mHexVtk.rawVertexCount == 12 && mHexVtk.rawTriangleCount == 20 &&
                  okTetVtk && mTetVtk.rawVertexCount == 5 && mTetVtk.rawTriangleCount == 6 &&
                  okVtk51 && mVtk51.rawVertexCount == 5 && mVtk51.rawTriangleCount == 6 &&
                  okVtk50 && mVtk50.rawVertexCount == 4 && mVtk50.rawTriangleCount == 4 &&
                  okVtkSp && mVtkSp.rawVertexCount == 16 && mVtkSp.rawTriangleCount == 18 &&
                  mVtkSp.bboxSize.z > 6.5f &&
                  HashMeshGeometry(mHexVtk) != HashMeshGeometry(mTetVtk),
                  L"Stage15.3D.VTK.UnstructuredHexTetBoundaryAndVtk5Offsets",
                  L"hexTris=" + std::to_wstring(mHexVtk.rawTriangleCount) +
                  L" tetTris=" + std::to_wstring(mTetVtk.rawTriangleCount) +
                  L" v51Tris=" + std::to_wstring(mVtk51.rawTriangleCount));

        // Gmsh MSH: repo 2.2 sample + MSH 4.1 ASCII sparse-tag 2-tet (elmType=4 -> 6 exterior tris) + MSH 4.1 Binary sparse-tag hex (elmType=5 -> 12 tris)
        LoadedMesh3D mRepoMsh;
        bool okRepoMsh = Model3DViewer::InspectMeshFile(tdDir + L"\\sample.msh", mRepoMsh);

        const char* msh41AsciiTets =
            "$MeshFormat\n"
            "4.1 0 8\n"
            "$EndMeshFormat\n"
            "$Nodes\n"
            "1 5 100 999\n"
            "3 1 0 5\n"
            "100\n205\n310\n420\n999\n"
            "0.0 0.0 0.0\n"
            "5.0 0.0 0.0\n"
            "2.5 4.0 0.0\n"
            "2.5 1.5 6.0\n"
            "2.5 1.5 -6.0\n"
            "$EndNodes\n"
            "$Elements\n"
            "1 2 10 11\n"
            "3 1 4 2\n"
            "10 100 205 310 420\n"
            "11 100 310 205 999\n"
            "$EndElements\n";

        auto msh41BinHex = BuildGmsh41BinaryVolumetric(
            {
                { 10, {0,0,0} }, { 20, {6,0,0} }, { 30, {6,4,0} }, { 40, {0,4,0} },
                { 50, {0,0,3} }, { 60, {6,0,3} }, { 70, {6,4,3} }, { 80, {0,4,3} }
            },
            5, // Gmsh elmType 5 = 8-node 3D hexahedron
            { { 10, 20, 30, 40, 50, 60, 70, 80 } });

        LoadedMesh3D mMsh41Tet, mMsh41BinHex;
        bool okMshTet = Model3DViewer::InspectMeshFile(writeText(L"gmsh41_sparse_2tet.msh", msh41AsciiTets), mMsh41Tet);
        bool okMshHex = Model3DViewer::InspectMeshFile(writeBytes(L"gmsh41_bin_hex.msh", msh41BinHex), mMsh41BinHex);
        tc.Record(okRepoMsh && mRepoMsh.rawVertexCount == 6 && mRepoMsh.rawTriangleCount == 8 &&
                  okMshTet && mMsh41Tet.rawVertexCount == 5 && mMsh41Tet.rawTriangleCount == 6 &&
                  okMshHex && mMsh41BinHex.rawVertexCount == 8 && mMsh41BinHex.rawTriangleCount == 12 &&
                  HashMeshGeometry(mMsh41Tet) != HashMeshGeometry(mMsh41BinHex),
                  L"Stage15.3D.MSH.Gmsh41AsciiAndBinarySparseVolumetric",
                  L"tetTris=" + std::to_wstring(mMsh41Tet.rawTriangleCount) +
                  L" binHexTris=" + std::to_wstring(mMsh41BinHex.rawTriangleCount));
    }

    // ========================================================================
    // 22. 3ds Max ASCII Scene Export (.ase) & ROS Robot Kinematic Tree (.urdf)
    // ========================================================================
    {
        const char* aseMulti =
            "*3DSMAX_ASCIIEXPORT\t200\n"
            "*COMMENT \"Explorer2 Multi-Object ASE Test\"\n"
            "*MATERIAL_LIST {\n"
            "  *MATERIAL_COUNT 1\n"
            "  *MATERIAL 0 {\n"
            "    *MATERIAL_DIFFUSE 0.85 0.35 0.15\n"
            "  }\n"
            "}\n"
            "*GEOMOBJECT {\n"
            "  *NODE_NAME \"TetPart\"\n"
            "  *NODE_TM {\n"
            "    *TM_ROW0 1.0 0.0 0.0\n"
            "    *TM_ROW1 0.0 1.0 0.0\n"
            "    *TM_ROW2 0.0 0.0 1.0\n"
            "    *TM_ROW3 0.0 0.0 0.0\n"
            "  }\n"
            "  *MESH {\n"
            "    *MESH_NUMVERTEX 4\n"
            "    *MESH_NUMFACES 4\n"
            "    *MESH_VERTEX_LIST {\n"
            "      *MESH_VERTEX 0  0.0  0.0  0.0\n"
            "      *MESH_VERTEX 1  4.0  0.0  0.0\n"
            "      *MESH_VERTEX 2  0.0  4.0  0.0\n"
            "      *MESH_VERTEX 3  0.0  0.0  5.0\n"
            "    }\n"
            "    *MESH_FACE_LIST {\n"
            "      *MESH_FACE 0: A: 0 B: 2 C: 1 AB: 1 BC: 1 CA: 1 *MESH_SMOOTHING 1 *MESH_MTLID 0\n"
            "      *MESH_FACE 1: A: 0 B: 1 C: 3 AB: 1 BC: 1 CA: 1 *MESH_SMOOTHING 1 *MESH_MTLID 0\n"
            "      *MESH_FACE 2: A: 1 B: 2 C: 3 AB: 1 BC: 1 CA: 1 *MESH_SMOOTHING 1 *MESH_MTLID 0\n"
            "      *MESH_FACE 3: A: 2 B: 0 C: 3 AB: 1 BC: 1 CA: 1 *MESH_SMOOTHING 1 *MESH_MTLID 0\n"
            "    }\n"
            "  }\n"
            "  *MATERIAL_REF 0\n"
            "}\n"
            "*GEOMOBJECT {\n"
            "  *NODE_NAME \"PyrPart\"\n"
            "  *MESH {\n"
            "    *MESH_NUMVERTEX 5\n"
            "    *MESH_NUMFACES 6\n"
            "    *MESH_VERTEX_LIST {\n"
            "      *MESH_VERTEX 0  10.0 -1.0  0.0\n"
            "      *MESH_VERTEX 1  12.0 -1.0  0.0\n"
            "      *MESH_VERTEX 2  12.0  1.0  0.0\n"
            "      *MESH_VERTEX 3  10.0  1.0  0.0\n"
            "      *MESH_VERTEX 4  11.0  0.0  4.0\n"
            "    }\n"
            "    *MESH_FACE_LIST {\n"
            "      *MESH_FACE 0: A: 0 B: 3 C: 2 AB: 1 BC: 1 CA: 0\n"
            "      *MESH_FACE 1: A: 0 B: 2 C: 1 AB: 0 BC: 1 CA: 1\n"
            "      *MESH_FACE 2: A: 0 B: 1 C: 4 AB: 1 BC: 1 CA: 1\n"
            "      *MESH_FACE 3: A: 1 B: 2 C: 4 AB: 1 BC: 1 CA: 1\n"
            "      *MESH_FACE 4: A: 2 B: 3 C: 4 AB: 1 BC: 1 CA: 1\n"
            "      *MESH_FACE 5: A: 3 B: 0 C: 4 AB: 1 BC: 1 CA: 1\n"
            "    }\n"
            "  }\n"
            "  *WIREFRAME_COLOR 0.2 0.7 0.9\n"
            "}\n";

        const char* asePrism =
            "*3DSMAX_ASCIIEXPORT 200\n"
            "*GEOMOBJECT {\n"
            "  *NODE_NAME \"WedgePrism\"\n"
            "  *MESH {\n"
            "    *MESH_NUMVERTEX 6\n"
            "    *MESH_NUMFACES 8\n"
            "    *MESH_VERTEX_LIST {\n"
            "      *MESH_VERTEX 0 0.0 0.0 0.0\n"
            "      *MESH_VERTEX 1 3.0 0.0 0.0\n"
            "      *MESH_VERTEX 2 0.0 2.0 0.0\n"
            "      *MESH_VERTEX 3 0.0 0.0 9.0\n"
            "      *MESH_VERTEX 4 3.0 0.0 9.0\n"
            "      *MESH_VERTEX 5 0.0 2.0 9.0\n"
            "    }\n"
            "    *MESH_FACE_LIST {\n"
            "      *MESH_FACE 0: A: 0 B: 2 C: 1\n"
            "      *MESH_FACE 1: A: 3 B: 4 C: 5\n"
            "      *MESH_FACE 2: A: 0 B: 1 C: 4\n"
            "      *MESH_FACE 3: A: 0 B: 4 C: 3\n"
            "      *MESH_FACE 4: A: 1 B: 2 C: 5\n"
            "      *MESH_FACE 5: A: 1 B: 5 C: 4\n"
            "      *MESH_FACE 6: A: 2 B: 0 C: 3\n"
            "      *MESH_FACE 7: A: 2 B: 3 C: 5\n"
            "    }\n"
            "  }\n"
            "}\n";

        LoadedMesh3D mAseA, mAseB;
        bool okAseA = Model3DViewer::InspectMeshFile(writeText(L"multi_tet_pyr.ase", aseMulti), mAseA);
        bool okAseB = Model3DViewer::InspectMeshFile(writeText(L"wedge_prism.ase", asePrism), mAseB);
        tc.Record(okAseA && okAseB &&
                  mAseA.rawVertexCount == 9 && mAseA.rawTriangleCount == 10 &&
                  mAseA.bboxSize.x > 11.5f &&
                  mAseB.rawVertexCount == 6 && mAseB.rawTriangleCount == 8 &&
                  HashMeshGeometry(mAseA) != HashMeshGeometry(mAseB),
                  L"Stage15.3D.ASE.MultiGeomObjectDistinct",
                  L"trisA=" + std::to_wstring(mAseA.rawTriangleCount) + L" trisB=" + std::to_wstring(mAseB.rawTriangleCount));

        // ROS URDF kinematic chain test: external sibling STL end-effector + 90-deg shoulder pitch rotation!
        const char* eeTipStl =
            "solid tip\n"
            "  facet normal 0 0 -1\n    outer loop\n      vertex -0.2 -0.2 0\n      vertex 0.2 0.2 0\n      vertex 0.2 -0.2 0\n    endloop\n  endfacet\n"
            "  facet normal 0 -1 0\n    outer loop\n      vertex -0.2 -0.2 0\n      vertex 0.2 -0.2 0\n      vertex 0 0 0.8\n    endloop\n  endfacet\n"
            "  facet normal 1 0 0\n    outer loop\n      vertex 0.2 -0.2 0\n      vertex 0.2 0.2 0\n      vertex 0 0 0.8\n    endloop\n  endfacet\n"
            "  facet normal -1 1 0\n    outer loop\n      vertex 0.2 0.2 0\n      vertex -0.2 -0.2 0\n      vertex 0 0 0.8\n    endloop\n  endfacet\n"
            "endsolid tip\n";
        writeText(L"urdf_ee_tip.stl", eeTipStl);

        auto buildArmUrdf = [](const char* shoulderPitchRpy) -> std::string {
            char buf[2048];
            std::snprintf(buf, sizeof(buf),
                "<?xml version=\"1.0\"?>\n"
                "<robot name=\"test_manipulator\">\n"
                "  <material name=\"orange\"><color rgba=\"1.0 0.5 0.1 1.0\"/></material>\n"
                "  <link name=\"base_link\">\n"
                "    <visual>\n"
                "      <origin xyz=\"0 0 0.2\" rpy=\"0 0 0\"/>\n"
                "      <geometry><box size=\"0.6 0.6 0.4\"/></geometry>\n"
                "      <material name=\"orange\"/>\n"
                "    </visual>\n"
                "  </link>\n"
                "  <link name=\"upper_arm\">\n"
                "    <visual>\n"
                "      <origin xyz=\"0 0 1.0\" rpy=\"0 0 0\"/>\n"
                "      <geometry><cylinder radius=\"0.12\" length=\"2.0\"/></geometry>\n"
                "    </visual>\n"
                "    <visual>\n"
                "      <origin xyz=\"0 0 2.0\" rpy=\"0 0 0\"/>\n"
                "      <geometry><sphere radius=\"0.16\"/></geometry>\n"
                "    </visual>\n"
                "  </link>\n"
                "  <link name=\"tool_tip\">\n"
                "    <visual>\n"
                "      <origin xyz=\"0 0 0\" rpy=\"0 0 0\"/>\n"
                "      <geometry><mesh filename=\"package://my_robot/urdf_ee_tip.stl\" scale=\"1 1 1\"/></geometry>\n"
                "    </visual>\n"
                "  </link>\n"
                "  <joint name=\"shoulder_pitch\" type=\"revolute\">\n"
                "    <parent link=\"base_link\"/>\n"
                "    <child link=\"upper_arm\"/>\n"
                "    <origin xyz=\"0 0 0.4\" rpy=\"%s\"/>\n"
                "  </joint>\n"
                "  <joint name=\"wrist_fixed\" type=\"fixed\">\n"
                "    <parent link=\"upper_arm\"/>\n"
                "    <child link=\"tool_tip\"/>\n"
                "    <origin xyz=\"0 0 2.0\" rpy=\"0 0 0\"/>\n"
                "  </joint>\n"
                "</robot>\n",
                shoulderPitchRpy);
            return std::string(buf);
        };

        std::string urdfVertical = buildArmUrdf("0 0 0");
        std::string urdfBent90   = buildArmUrdf("0 1.5707963 0");
        LoadedMesh3D mUrdfV, mUrdfB;
        bool okUrdfV = Model3DViewer::InspectMeshFile(writeText(L"arm_vertical.urdf", urdfVertical.c_str()), mUrdfV);
        bool okUrdfB = Model3DViewer::InspectMeshFile(writeText(L"arm_bent90.urdf", urdfBent90.c_str()), mUrdfB);
        tc.Record(okUrdfV && okUrdfB &&
                  mUrdfV.rawTriangleCount == mUrdfB.rawTriangleCount &&
                  mUrdfV.bboxSize.x < 0.8f && mUrdfV.bboxSize.y > 2.9f &&
                  mUrdfB.bboxSize.x > 2.8f && // proves 90-deg Ry shoulder pitch rotated 2.0m arm + 0.8m STL tip into +X!
                  HashMeshGeometry(mUrdfV) != HashMeshGeometry(mUrdfB),
                  L"Stage15.3D.URDF.KinematicChainPoseAndExternalMesh",
                  L"spanXV=" + std::to_wstring(mUrdfV.bboxSize.x) + L" spanXB=" + std::to_wstring(mUrdfB.bboxSize.x));
    }

    // ========================================================================
    // 23. ESRI / USGS DEM (.dem, .ascgrid) & ASPRS LAS 1.4 / Compressed LAZ (.las, .laz)
    // ========================================================================
    {
        LoadedMesh3D mSampleDem;
        bool okSampleDem = Model3DViewer::InspectMeshFile(tdDir + L"\\sample.dem", mSampleDem);

        // 300x300 ESRI ASCII Grid (>256x256!) with NODATA_value -9999 crater and far-corner ridge at r,c >= 275
        std::string bigDem =
            "ncols 300\n"
            "nrows 300\n"
            "xllcorner 100.0\n"
            "yllcorner 200.0\n"
            "cellsize 2.0\n"
            "NODATA_value -9999\n";
        bigDem.reserve(650000);
        for (int r = 0; r < 300; ++r) {
            for (int c = 0; c < 300; ++c) {
                if (r >= 130 && r < 160 && c >= 130 && c < 160) {
                    bigDem += "-9999 ";
                } else if (r >= 275 && c >= 275) {
                    bigDem += "180.0 ";
                } else {
                    int h = 10 + ((r + c) % 15);
                    bigDem += std::to_string(h) + " ";
                }
            }
            bigDem.push_back('\n');
        }

        std::string usgsDem = BuildUsgs75MinDemFixture();
        LoadedMesh3D mBigDem, mUsgsDem;
        bool okBigDem  = Model3DViewer::InspectMeshFile(writeText(L"terrain_300x300.dem", bigDem.c_str()), mBigDem);
        bool okUsgsDem = Model3DViewer::InspectMeshFile(writeText(L"rainier_usgs.dem", usgsDem.c_str()), mUsgsDem);
        tc.Record(okSampleDem && mSampleDem.rawTriangleCount == 98 &&
                  okBigDem && mBigDem.rawTriangleCount > 50000 &&
                  mBigDem.bboxMin.y >= 9.5f &&   // proves -9999 NODATA crater cells were culled!
                  mBigDem.bboxMax.y >= 179.5f && // proves rows/cols > 256 (at 275..299) were read!
                  okUsgsDem && mUsgsDem.rawTriangleCount >= 40 &&
                  mUsgsDem.bboxMin.y >= 15.0f && // proves -32767 USGS void cell was culled!
                  HashMeshGeometry(mBigDem) != HashMeshGeometry(mUsgsDem),
                  L"Stage15.3D.DEM.Large300x300NoDataAndUsgsTypeAB",
                  L"bigTris=" + std::to_wstring(mBigDem.rawTriangleCount) +
                  L" usgsTris=" + std::to_wstring(mUsgsDem.rawTriangleCount));

        // LAS 1.4 Format 6 (legacy count=0, 64-bit count at +247) + Multi-chunk Arithmetic LAZ POINT10 v2 vs Ground-Truth LAS!
        LoadedMesh3D mRepoLas;
        bool okRepoLas = Model3DViewer::InspectMeshFile(tdDir + L"\\sample.las", mRepoLas);

        std::vector<Vec3f> las14Pts;
        for (int i = 0; i < 14; ++i) {
            las14Pts.push_back({ static_cast<float>(i) * 1.5f, static_cast<float>(i % 4) * 2.0f, static_cast<float>(i) * 0.75f });
        }
        auto las14Bytes = LazTestEnc::BuildLas14Fmt6Fixture(las14Pts);

        std::vector<Vec3f> laz24Pts;
        for (int i = 0; i < 24; ++i) {
            float fx = static_cast<float>(i * 3 - (i % 5)) * 0.25f;
            float fy = static_cast<float>((i * 7) % 19) * 0.5f - 4.0f;
            float fz = static_cast<float>((i * i) % 23) * 0.4f + 1.2f;
            laz24Pts.push_back({ fx, fy, fz });
        }
        // Encode across 3 chunks of 8 points each using real LASzip v2 POINT10 arithmetic coding!
        auto lazCompBytes = LazTestEnc::BuildLazPoint10v2Fixture(laz24Pts, 8u);
        auto lasTruthBytes = BuildSyntheticLasOrLaz(laz24Pts, 0u);

        LoadedMesh3D mLas14, mLazComp, mLasTruth;
        bool okLas14    = Model3DViewer::InspectMeshFile(writeBytes(L"v14_fmt6_ext64.las", las14Bytes), mLas14);
        bool okLazComp  = Model3DViewer::InspectMeshFile(writeBytes(L"point10_arith.laz", lazCompBytes), mLazComp);
        bool okLasTruth = Model3DViewer::InspectMeshFile(writeBytes(L"point10_truth.las", lasTruthBytes), mLasTruth);
        tc.Record(okRepoLas && mRepoLas.rawTriangleCount > 0 &&
                  okLas14 && mLas14.rawVertexCount == 14 * 4 && mLas14.rawTriangleCount == 14 * 4 &&
                  okLazComp && okLasTruth &&
                  mLazComp.rawVertexCount == 24 * 4 && mLazComp.rawTriangleCount == 24 * 4 &&
                  HashMeshGeometry(mLazComp) == HashMeshGeometry(mLasTruth),
                  L"Stage15.3D.LAS_LAZ.Las14Fmt6AndLazPoint10Arithmetic",
                  L"lazVerts=" + std::to_wstring(mLazComp.rawVertexCount));
    }

    // ========================================================================
    // 24. Blender Zstd Seek+Compressed_Block (.blend), VRML 1.0/97 Gzip (.wrl) & X3D Scene Graph (.x3d)
    // ========================================================================
    {
        auto rawSoaBlend = BuildSyntheticBlend42SoA(
            { {0,0,0}, {5,0,0}, {0,4,0}, {0,0,7}, {5,0,7}, {0,4,7} },
            { {0,2,1}, {3,4,5}, {0,1,4,3}, {1,2,5,4}, {2,0,3,5} });
        auto zstdSeekBlend = WrapBlendInZstdSeekAndCompressedBlocks(rawSoaBlend);

        LoadedMesh3D mRawBlend, mZstdBlend;
        bool okRawB  = Model3DViewer::InspectMeshFile(writeBytes(L"raw_ref_42.blend", rawSoaBlend), mRawBlend);
        bool okZstdB = Model3DViewer::InspectMeshFile(writeBytes(L"zstd_seek_huff_fse_42.blend", zstdSeekBlend), mZstdBlend);
        tc.Record(okRawB && okZstdB &&
                  mZstdBlend.rawVertexCount == 6 && mZstdBlend.rawTriangleCount == 8 &&
                  HashMeshGeometry(mRawBlend) == HashMeshGeometry(mZstdBlend),
                  L"Stage15.3D.Blend.ZstdCompressedBlockWithSkippableSeekFrame",
                  L"zstdBytes=" + std::to_wstring(zstdSeekBlend.size()) + L" rawBytes=" + std::to_wstring(rawSoaBlend.size()));

        // VRML97 gzipped DEF/USE + Transform + ElevationGrid + VRML 1.0 Separator
        LoadedMesh3D mRepoWrl;
        bool okRepoWrl = Model3DViewer::InspectMeshFile(tdDir + L"\\virtual_reality.wrl", mRepoWrl);

        const char* vrml97Scene =
            "#VRML V2.0 utf8\n"
            "Transform {\n"
            "  translation -6 0 0\n"
            "  children [\n"
            "    DEF SharedPillar Shape {\n"
            "      appearance Appearance { material Material { diffuseColor 0.2 0.7 0.9 } }\n"
            "      geometry Box { size 2 4 2 }\n"
            "    }\n"
            "  ]\n"
            "}\n"
            "Transform {\n"
            "  translation 10 0 0\n"
            "  rotation 0 0 1 1.5707963\n"
            "  children [ USE SharedPillar ]\n"
            "}\n"
            "Shape {\n"
            "  geometry ElevationGrid {\n"
            "    xDimension 3\n"
            "    zDimension 3\n"
            "    xSpacing 1.0\n"
            "    zSpacing 1.0\n"
            "    height [ 0, 0.5, 0, 0.5, 2.0, 0.5, 0, 0.5, 0 ]\n"
            "  }\n"
            "}\n";
        auto vrml97Gz = TfGzipCompress(vrml97Scene, std::strlen(vrml97Scene));

        const char* vrml10Scene =
            "#VRML V1.0 ascii\n"
            "Separator {\n"
            "  Separator {\n"
            "    Translation { translation -8 0 0 }\n"
            "    Cube { width 2 height 2 depth 2 }\n"
            "  }\n"
            "  Separator {\n"
            "    Translation { translation 8 0 0 }\n"
            "    Coordinate3 { point [ 0 0 0, 3 0 0, 0 3 0, 0 0 4 ] }\n"
            "    IndexedFaceSet { coordIndex [ 0, 2, 1, -1, 0, 1, 3, -1, 1, 2, 3, -1, 2, 0, 3, -1 ] }\n"
            "  }\n"
            "}\n";

        LoadedMesh3D mWrl97Gz, mWrl10;
        bool okWrl97Gz = Model3DViewer::InspectMeshFile(writeBytes(L"scene_def_use_gz.wrl", vrml97Gz), mWrl97Gz);
        bool okWrl10   = Model3DViewer::InspectMeshFile(writeText(L"scene_vrml10.wrl", vrml10Scene), mWrl10);
        tc.Record(okRepoWrl && mRepoWrl.rawVertexCount == 5 && mRepoWrl.rawTriangleCount == 4 &&
                  okWrl97Gz && mWrl97Gz.rawTriangleCount == 32 && mWrl97Gz.bboxSize.x > 18.0f &&
                  okWrl10 && mWrl10.rawTriangleCount == 16 && mWrl10.bboxSize.x > 19.0f &&
                  HashMeshGeometry(mWrl97Gz) != HashMeshGeometry(mWrl10),
                  L"Stage15.3D.WRL.Vrml97GzipDefUseAndVrml1Separator",
                  L"gzTris=" + std::to_wstring(mWrl97Gz.rawTriangleCount) + L" v1Tris=" + std::to_wstring(mWrl10.rawTriangleCount));

        // X3D hierarchical <Transform> + DEF/USE + IndexedTriangleSet + IndexedTriangleStripSet + ElevationGrid + Gzip
        LoadedMesh3D mRepoX3d;
        bool okRepoX3d = Model3DViewer::InspectMeshFile(tdDir + L"\\web3d_pyramid.x3d", mRepoX3d);

        auto buildX3dScene = [](float rightTx) -> std::string {
            char buf[2048];
            std::snprintf(buf, sizeof(buf),
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<X3D profile=\"Immersive\" version=\"3.3\">\n"
                "  <Scene>\n"
                "    <Transform translation=\"-5 0 0\">\n"
                "      <Shape DEF=\"PodBox\">\n"
                "        <Appearance><Material diffuseColor=\"0.9 0.4 0.2\"/></Appearance>\n"
                "        <Box size=\"2 2 2\"/>\n"
                "      </Shape>\n"
                "    </Transform>\n"
                "    <Transform translation=\"%.2f 0 0\" rotation=\"0 1 0 0.785398\">\n"
                "      <Shape USE=\"PodBox\"/>\n"
                "    </Transform>\n"
                "    <Shape>\n"
                "      <IndexedTriangleSet index=\"0 1 2 0 2 3\">\n"
                "        <Coordinate point=\"-1 0 -2, 1 0 -2, 1 2 -2, -1 2 -2\"/>\n"
                "      </IndexedTriangleSet>\n"
                "    </Shape>\n"
                "    <Shape>\n"
                "      <IndexedTriangleStripSet index=\"0 1 2 3 -1\">\n"
                "        <Coordinate point=\"-1 0 2, 1 0 2, -1 2 2, 1 2 2\"/>\n"
                "      </IndexedTriangleStripSet>\n"
                "    </Shape>\n"
                "    <Shape>\n"
                "      <ElevationGrid xDimension=\"2\" zDimension=\"2\" xSpacing=\"2\" zSpacing=\"2\" height=\"0 1 1 3\"/>\n"
                "    </Shape>\n"
                "  </Scene>\n"
                "</X3D>\n",
                rightTx);
            return std::string(buf);
        };

        std::string x3dPlain = buildX3dScene(10.0f);
        std::string x3dWide  = buildX3dScene(26.0f);
        auto x3dWideGz = TfGzipCompress(x3dWide.data(), x3dWide.size());

        LoadedMesh3D mX3dPlain, mX3dGz;
        bool okX3dPlain = Model3DViewer::InspectMeshFile(writeText(L"scene_multi.x3d", x3dPlain.c_str()), mX3dPlain);
        bool okX3dGz    = Model3DViewer::InspectMeshFile(writeBytes(L"scene_multi_gz.x3d", x3dWideGz), mX3dGz);
        tc.Record(okRepoX3d && mRepoX3d.rawVertexCount == 5 && mRepoX3d.rawTriangleCount == 4 &&
                  okX3dPlain && okX3dGz &&
                  mX3dPlain.rawTriangleCount == 30 && mX3dGz.rawTriangleCount == 30 &&
                  mX3dPlain.bboxSize.x > 16.0f && mX3dGz.bboxSize.x > 31.0f &&
                  HashMeshGeometry(mX3dPlain) != HashMeshGeometry(mX3dGz),
                  L"Stage15.3D.X3D.TransformDefUseAndGzipMultiPrimitives",
                  L"tris=" + std::to_wstring(mX3dPlain.rawTriangleCount) + L" spanXGz=" + std::to_wstring(mX3dGz.bboxSize.x));
    }

    // ========================================================================
    // 11c. LightWave Scene (.lws): objects come from the referenced .lwo files (the right layer), placed with
    //      position / heading / scale / pivot / parent; missing objects are named, never replaced by boxes
    // ========================================================================
    {
        const std::wstring content = workDir + L"\\lws_content";
        const std::wstring scenesDir = content + L"\\Scenes";
        const std::wstring objectsDir = content + L"\\Objects";
        CreateDirectoryW(content.c_str(), nullptr);
        CreateDirectoryW(scenesDir.c_str(), nullptr);
        CreateDirectoryW(objectsDir.c_str(), nullptr);
        std::vector<std::wstring> lwsFiles;
        auto put = [&](const std::wstring& p, const void* data, size_t n) {
            PathUtils::WriteFileBytes(p, data, n);
            lwsFiles.push_back(p);
            return p;
        };

        // tri.lwo: one layer, one triangle. two.lwo: layer 1 = quad near the origin, layer 2 = triangle at (5,5,5)
        auto triLwo = BuildSyntheticLwo2({ { 0, { {0,0,0}, {1,0,0}, {0,0,1} }, { {0,1,2} } } });
        auto twoLwo = BuildSyntheticLwo2({
            { 0, { {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0} }, { {0,1,2,3} } },
            { 1, { {5,5,5}, {6,5,5}, {5,6,5} }, { {0,1,2} } } });
        put(objectsDir + L"\\tri.lwo", triLwo.data(), triLwo.size());
        const std::wstring twoPath = put(objectsDir + L"\\two.lwo", twoLwo.data(), twoLwo.size());

        auto env = [](int ch, const char* val) {
            return std::string("Channel ") + std::to_string(ch) + "\n{ Envelope\n  1\n  Key " + val +
                   " 0 0 0 0 0 0 0 0\n  Behaviors 1 1\n}\n";
        };
        // LightWave 9 style IDs on the first two objects, pre-9 style on the third (missing) one.
        // tri: position (10,0,0), heading 90 deg (radians), scale 2. two (layer 2): child of tri, position (0,3,0),
        // pivot (5,5,5). The light's intensity envelope after its motion channels must not be taken as motion.
        std::string scene =
            "LWSC\n3\n\nFirstFrame 1\nLastFrame 30\n\n"
            "LoadObjectLayer 1 10000000 Objects/tri.lwo\nShowObject 6 3\nObjectMotion\nNumChannels 9\n" +
            env(0, "10") + env(1, "0") + env(2, "0") + env(3, "1.5707963267948966") + env(6, "2") + env(7, "2") + env(8, "2") +
            "PivotPosition 0 0 0\n\n"
            "LoadObjectLayer 2 10000001 Objects/two.lwo\nObjectMotion\nNumChannels 9\n" + env(1, "3") +
            "PivotPosition 5 5 5\nParentItem 10000000\n\n"
            "LoadObjectLayer 1 Objects/missing_robot.lwo\n\n"
            "AddLight\nLightName Light\nLightMotion\nNumChannels 9\n" + env(2, "0") +
            "LightIntensity (envelope)\n{ Envelope\n  1\n  Key 7 0 0 0 0 0 0 0 0\n  Behaviors 1 1\n}\n\n"
            "AddCamera\nCameraName Camera\n";
        const std::wstring scenePath = put(scenesDir + L"\\scene_v3.lws", scene.data(), scene.size());

        // LightWave 5 scene: degrees, keys as rows of 9 values; tri at (0,0,5) turned 90 deg
        std::string sceneV1 =
            "LWSC\n1\n\nFirstFrame 1\nLastFrame 30\n\n"
            "LoadObject Objects\\tri.lwo\nShowObject 6 7\nObjectMotion (unnamed)\n  9\n  1\n"
            "  0 0 5 90 0 0 1 1 1\n  0 0 0 0 0\nEndBehavior 1\n";
        const std::wstring sceneV1Path = put(scenesDir + L"\\scene_v1.lws", sceneV1.data(), sceneV1.size());

        // Nothing but objects that cannot be loaded: a missing file and a network path (refused, never opened)
        std::string sceneMissing =
            "LWSC\n3\n\nLoadObjectLayer 1 Objects/nowhere.lwo\nLoadObjectLayer 1 \\\\no-such-host\\share\\robot.lwo\n";
        const std::wstring sceneMissingPath = put(scenesDir + L"\\scene_missing.lws", sceneMissing.data(), sceneMissing.size());

        // Raw (scene-space) position of vertex i: undo the viewer's centring and scaling to [-1, 1]
        auto rawVertex = [](const LoadedMesh3D& m, size_t i) -> Vec3f {
            const float span = std::max({ m.bboxSize.x, m.bboxSize.y, m.bboxSize.z, 1e-5f });
            const float s = span * 0.5f;
            return { m.vertices[i].x * s + (m.bboxMin.x + m.bboxMax.x) * 0.5f,
                     m.vertices[i].y * s + (m.bboxMin.y + m.bboxMax.y) * 0.5f,
                     m.vertices[i].z * s + (m.bboxMin.z + m.bboxMax.z) * 0.5f };
        };
        auto expectVerts = [&](const LoadedMesh3D& m, size_t first, std::initializer_list<Vec3f> want, std::wstring& why) {
            size_t i = first;
            for (const Vec3f& w : want) {
                if (i >= m.vertices.size()) { why = L"too few vertices"; return false; }
                const Vec3f r = rawVertex(m, i);
                if (std::fabs(r.x - w.x) > 2e-3f || std::fabs(r.y - w.y) > 2e-3f || std::fabs(r.z - w.z) > 2e-3f) {
                    wchar_t b[160];
                    swprintf_s(b, L"v%zu=(%.3f,%.3f,%.3f) want (%.3f,%.3f,%.3f)", i, r.x, r.y, r.z, w.x, w.y, w.z);
                    why = b;
                    return false;
                }
                ++i;
            }
            return true;
        };

        LoadedMesh3D mScene, mV1, mMissing, mTwo;
        const bool okScene = Model3DViewer::InspectMeshFile(scenePath, mScene);
        const bool okV1 = Model3DViewer::InspectMeshFile(sceneV1Path, mV1);
        const bool okMissing = Model3DViewer::InspectMeshFile(sceneMissingPath, mMissing);
        const bool okTwo = Model3DViewer::InspectMeshFile(twoPath, mTwo);

        // tri: T(10,0,0) * Ry(90) * S(2): (0,0,0)->(10,0,0), (1,0,0)->(10,0,-2), (0,0,1)->(12,0,0)
        // two layer 2 through tri: (5,5,5)->(10,6,0), (6,5,5)->(10,6,-2), (5,6,5)->(10,8,0)
        std::wstring whyScene;
        const bool placed = okScene && expectVerts(mScene, 0, { {10,0,0}, {10,0,-2}, {12,0,0}, {10,6,0}, {10,6,-2}, {10,8,0} }, whyScene);
        // Layer 1 of two.lwo would land at y = -4 / z = +10, a leaked light-intensity key at z = 7
        const bool onlyLayer2 = okScene && mScene.bboxMin.y > -1.0f && mScene.bboxMax.z < 3.0f;
        const bool namesMissing = mScene.statusMessage.find(L"2/3") != std::wstring::npos &&
                                  mScene.statusMessage.find(L"missing_robot.lwo") != std::wstring::npos;
        tc.Record(placed && onlyLayer2 && namesMissing,
                  L"Stage15.3D.LightWaveScene.ObjectsFromLwoPlacedByMotion",
                  L"ok=" + std::to_wstring(okScene) + L" " + whyScene + L" bboxMinY=" + std::to_wstring(mScene.bboxMin.y) +
                  L" bboxMaxZ=" + std::to_wstring(mScene.bboxMax.z) + L" status=" + mScene.statusMessage);

        // LightWave 5: (0,0,0)->(0,0,5), (1,0,0)->(0,0,4), (0,0,1)->(1,0,5)
        std::wstring whyV1;
        const bool placedV1 = okV1 && expectVerts(mV1, 0, { {0,0,5}, {0,0,4}, {1,0,5} }, whyV1) && mV1.rawTriangleCount == 1;
        tc.Record(placedV1, L"Stage15.3D.LightWaveScene.Lw5MotionRowsInDegrees",
                  L"ok=" + std::to_wstring(okV1) + L" tris=" + std::to_wstring(mV1.rawTriangleCount) + L" " + whyV1);

        // No geometry at all: not shown, and the message names both objects (the network one as refused)
        const bool missingOk = !okMissing && mMissing.tris.empty() &&
                               mMissing.statusMessage.find(L"nowhere.lwo") != std::wstring::npos &&
                               mMissing.statusMessage.find(L"robot.lwo") != std::wstring::npos &&
                               mMissing.statusMessage.find(L"sieciowa") != std::wstring::npos;
        tc.Record(missingOk, L"Stage15.3D.LightWaveScene.MissingObjectsNamedNoBoxes", L"status=" + mMissing.statusMessage);

        // The .lwo by itself still shows every layer: quad (4 verts, 2 tris) + triangle (3 verts, 1 tri)
        tc.Record(okTwo && mTwo.rawVertexCount == 7 && mTwo.rawTriangleCount == 3, L"Stage15.3D.LightWaveObject.AllLayersStandalone",
                  L"verts=" + std::to_wstring(mTwo.rawVertexCount) + L" tris=" + std::to_wstring(mTwo.rawTriangleCount));

        for (const auto& f : lwsFiles) DeleteFileW(f.c_str());
        RemoveDirectoryW(objectsDir.c_str());
        RemoveDirectoryW(scenesDir.c_str());
        RemoveDirectoryW(content.c_str());
    }

    // ========================================================================
    // 12. Negative / Zero-Cheat Tests (corrupt / encrypted / header-only -> false)
    // ========================================================================
    {
        auto assertRejected = [&](const wchar_t* testName, const std::wstring& path) {
            LoadedMesh3D m;
            bool ok = Model3DViewer::InspectMeshFile(path, m);
            tc.Record(!ok && !m.loadedOk && m.tris.empty(), testName);
        };

        std::vector<uint8_t> dwgEnc(128, 0);
        std::memcpy(dwgEnc.data(), "AC1032", 6);
        assertRejected(L"Stage15.3D.Reject.EncryptedDWG", writeBytes(L"encrypted_r2018.dwg", dwgEnc));

        std::vector<uint8_t> maxStub = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1, 0, 0, 0, 0 };
        assertRejected(L"Stage15.3D.Reject.CorruptMAX", writeBytes(L"corrupt_stub.max", maxStub));

        const char* blendHeaderOnly = "BLENDER-v300ENDB\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
        std::vector<uint8_t> blendEmpty(blendHeaderOnly, blendHeaderOnly + 32);
        assertRejected(L"Stage15.3D.Reject.HeaderOnlyBlend", writeBytes(L"empty_header.blend", blendEmpty));

        const char* ifcEmpty =
            "ISO-10303-21;\nHEADER;\nFILE_SCHEMA(('IFC4'));\nENDSEC;\nDATA;\n"
            "#1=IFCPROJECT('0',$,'EmptyProject',$,$,$,$,$,$);\n"
            "ENDSEC;\nEND-ISO-10303-21;\n";
        assertRejected(L"Stage15.3D.Reject.NoFakeHouseIFC", writeText(L"empty_project.ifc", ifcEmpty));

        assertRejected(L"Stage15.3D.Reject.NoFakeFallbackSCAD",
                       writeText(L"comments_only.scad", "// No geometry\nwidth = 50;\n"));

        std::vector<uint8_t> drcEdgebreaker = { 'D','R','A','C','O', 2, 2, 1, 1, 0, 0, 0, 0, 0 };
        assertRejected(L"Stage15.3D.Reject.NoTorusKnotDraco", writeBytes(L"edgebreaker_stub.drc", drcEdgebreaker));

        const char* nifHdrOnly = "Gamebryo File Format, Version 20.2.0.7\n";
        std::vector<uint8_t> nifStub(nifHdrOnly, nifHdrOnly + std::strlen(nifHdrOnly));
        nifStub.insert(nifStub.end(), 16, 0);
        assertRejected(L"Stage15.3D.Reject.NoTorusKnotNIF", writeBytes(L"header_only.nif", nifStub));

        std::string jtStub(80, ' ');
        std::memcpy(jtStub.data(), "Version 9.5 JT", 14);
        jtStub[78] = '\r'; jtStub[79] = '\n';
        jtStub.append(16, '\0');
        assertRejected(L"Stage15.3D.Reject.NoTorusKnotJT", writeText(L"header_only.jt", jtStub.c_str()));

        auto lazCompressed = BuildSyntheticLasOrLaz({ {1,2,3}, {4,5,6} }, 128 + 1);
        assertRejected(L"Stage15.3D.Reject.CompressedLAZ", writeBytes(L"compressed_stub.laz", lazCompressed));

        std::vector<uint8_t> ctmMg1(40, 0);
        std::memcpy(ctmMg1.data(), "OCTM\x05\x00\x00\x00MG1\x00", 12);
        assertRejected(L"Stage15.3D.Reject.NoTorusKnotCTM", writeBytes(L"mg1_stub.ctm", ctmMg1));

        // JSON-less GLB (BIN chunk only -> must be rejected, never scraped as raw floats!)
        ByteWriter jsonLessGlb;
        jsonLessGlb.U32LE(0x46546C67u); // "glTF"
        jsonLessGlb.U32LE(2u);
        jsonLessGlb.U32LE(12u + 8u + 36u);
        jsonLessGlb.U32LE(36u);
        jsonLessGlb.U32LE(0x004E4942u); // "BIN\0" as chunk 0 without JSON
        const float rawTri[9] = { 0,0,0, 1,0,0, 0,1,0 };
        for (float f : rawTri) jsonLessGlb.F32LE(f);
        assertRejected(L"Stage15.3D.Reject.JsonLessGLB", writeBytes(L"no_json_chunk.glb", jsonLessGlb.buf));

        // Path-traversal .gltf URI must be rejected
        const char* traversalGltf =
            "{\"asset\":{\"version\":\"2.0\"},"
            "\"buffers\":[{\"uri\":\"../outside.bin\",\"byteLength\":36}],"
            "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36}],"
            "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}],"
            "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}]}";
        assertRejected(L"Stage15.3D.Reject.PathTraversalGLTF", writeText(L"traversal.gltf", traversalGltf));

        // Truncated/corrupt .vrm must be rejected
        ByteWriter badVrm;
        badVrm.U32LE(0x46546C67u);
        badVrm.U32LE(2u);
        badVrm.U32LE(64u);
        badVrm.U32LE(200u); // chunk length exceeds total length
        badVrm.U32LE(0x4E4F534Au);
        assertRejected(L"Stage15.3D.Reject.CorruptVRM", writeBytes(L"truncated.vrm", badVrm.buf));

        // Non-COLLADA XML containing <float_array> must be rejected
        assertRejected(L"Stage15.3D.Reject.NonColladaDAE",
                       writeText(L"fake_svg.dae", "<?xml version=\"1.0\"?><svg><float_array>0 0 0 1 0 0 0 1 0</float_array></svg>"));

        // Header-only IGES with zero geometry entities must be rejected
        IgesDocBuilder emptyIges;
        std::string emptyIgesTxt = emptyIges.Finish();
        assertRejected(L"Stage15.3D.Reject.HeaderOnlyIGES", writeText(L"header_only.iges", emptyIgesTxt.c_str()));

        // .3mf ZIP without any .model XML must be rejected
        auto bad3mfZip = BuildTestZipArchive({ { "readme.txt", "no model here", false } });
        assertRejected(L"Stage15.3D.Reject.ModelLess3MF", writeBytes(L"no_model.3mf", bad3mfZip));

        // .amf ZIP without valid <amf>/<object> geometry must be rejected
        auto badAmfZip = BuildTestZipArchive({ { "broken.amf", "<?xml version=\"1.0\"?><amf><metadata/></amf>", true } });
        assertRejected(L"Stage15.3D.Reject.EmptyZipAMF", writeBytes(L"empty_zip.amf", badAmfZip));

        // Disconnected bare point soup in .sat, .sab, and .x_t must NEVER be connected sequentially
        const char* barePtsSat =
            "700 0 1 0\n"
            "@13 Explorer2Test @8 ACIS 7.0 @4 2026\n"
            "1 9.9999999999999995e-007 1e-010\n"
            "-0 point $-1 0 0 0 #\n"
            "-1 point $-1 10 0 0 #\n"
            "-2 point $-1 5 8 0 #\n"
            "-3 point $-1 5 4 9 #\n"
            "End-of-ACIS-data\n";
        assertRejected(L"Stage15.3D.Reject.BarePointsSAT", writeText(L"bare_points_only.sat", barePtsSat));

        auto barePtsSab = BuildSyntheticSabBarePoints({ {0,0,0}, {10,0,0}, {5,8,0}, {5,4,9} });
        assertRejected(L"Stage15.3D.Reject.BarePointsSAB", writeBytes(L"bare_points_only.sab", barePtsSab));

        const char* barePtsXt =
            "**PART1;MC=WIN64;\nSCH_3300002_33000;\n**END_OF_HEADER**\n"
            "point 1 0.0 0.0 0.0;\npoint 2 5.0 0.0 0.0;\npoint 3 0.0 5.0 0.0;\n"
            "**END_OF_PART**\n";
        assertRejected(L"Stage15.3D.Reject.BarePointsXT", writeText(L"bare_points_only.x_t", barePtsXt));

        // Binary Parasolid .x_b transmit without decodable XT text schema must be rejected
        std::vector<uint8_t> binXb = { 'P','S', 0x00, 0x01, 0x02, 'S','C','H','_','B','I','N', 0x00, 0xFF, 0x13, 0x37 };
        for (float f : { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f }) {
            uint8_t fb[4]; std::memcpy(fb, &f, 4); binXb.insert(binXb.end(), fb, fb + 4);
        }
        assertRejected(L"Stage15.3D.Reject.BinaryGarbageXB", writeBytes(L"binary_blob.x_b", binXb));

        // Fake .e57 with raw float32 bytes at offset 48/64 (old byte-scraper cheat) & corrupt CRC32C page must be rejected
        ByteWriter fakeE57;
        fakeE57.Bytes("ASTM-E57", 8);
        fakeE57.U32LE(1); fakeE57.U32LE(0);
        fakeE57.U64LE(1024); fakeE57.U64LE(512); fakeE57.U64LE(128); fakeE57.U64LE(1024);
        while (fakeE57.buf.size() < 1024) fakeE57.F32LE(3.5f);
        assertRejected(L"Stage15.3D.Reject.RawFloatsFakeE57", writeBytes(L"raw_float_cheat.e57", fakeE57.buf));

        auto corruptCrcE57 = BuildSyntheticE57Float32({
            {0,0,0}, {2,0,0}, {0,2,0}, {0,0,2}, {1,1,1}, {-2,0,1}
        });
        corruptCrcE57[120] ^= 0x40u; // corrupt payload byte without updating page Castagnoli CRC32C
        assertRejected(L"Stage15.3D.Reject.CorruptCrcE57", writeBytes(L"corrupt_crc.e57", corruptCrcE57));

        // Source Engine .mdl with mismatched .vvd checksum & 0-bone header-only .mdl must be rejected
        auto badSrcSet = BuildSyntheticSourceV48Set(
            { {0,0,0}, {4,0,0}, {0,4,0}, {0,0,4} },
            { {{0,1,2}}, {{0,3,1}}, {{1,3,2}}, {{2,3,0}} },
            0x12345678u, 0xDEADBEEFu, 0x12345678u);
        writeBytes(L"bad_chk.vvd", badSrcSet.vvd);
        writeBytes(L"bad_chk.dx90.vtx", badSrcSet.vtx);
        assertRejected(L"Stage15.3D.Reject.MismatchedChecksumSourceMDL", writeBytes(L"bad_chk.mdl", badSrcSet.mdl));

        std::vector<uint8_t> hdrOnlyMdl(256, 0);
        std::memcpy(hdrOnlyMdl.data(), "IDST", 4);
        uint32_t v48 = 48, len256 = 256;
        std::memcpy(hdrOnlyMdl.data() + 4, &v48, 4);
        std::memcpy(hdrOnlyMdl.data() + 76, &len256, 4);
        assertRejected(L"Stage15.3D.Reject.HeaderOnlySourceMDL", writeBytes(L"empty_v48.mdl", hdrOnlyMdl));

        // Group 1 (.pcd & .ply): corrupt/truncated LZF stream, zero-point .pcd, and header-only binary .ply must be rejected
        auto corruptLzfPcd = BuildPcdBinaryCompressedSoA({ {0,0,0}, {1,2,3}, {4,5,6}, {7,8,9} }, {});
        if (corruptLzfPcd.size() > 10) corruptLzfPcd.resize(corruptLzfPcd.size() - 10); // truncate mid-LZF stream
        assertRejected(L"Stage15.3D.Reject.CorruptLzfPCD", writeBytes(L"corrupt_lzf.pcd", corruptLzfPcd));

        const char* zeroPtPcd =
            "VERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\n"
            "WIDTH 0\nHEIGHT 1\nPOINTS 0\nDATA ascii\n";
        assertRejected(L"Stage15.3D.Reject.ZeroPointPCD", writeText(L"zero_points.pcd", zeroPtPcd));

        const char* hdrOnlyPly =
            "ply\nformat binary_little_endian 1.0\nelement vertex 4\n"
            "property float x\nproperty float y\nproperty float z\nend_header\n";
        assertRejected(L"Stage15.3D.Reject.HeaderOnlyPLY", writeText(L"header_only_bin.ply", hdrOnlyPly));

        // Group 2 (.vtk & .msh): bare VTK POINTS without cells/polygons (old (i,i+1,i+2) cheat!) & element-less .msh must be rejected
        const char* barePtsVtk =
            "# vtk DataFile Version 3.0\nBare Points Without Cells\nASCII\nDATASET POLYDATA\n"
            "POINTS 6 float\n0 0 0  1 0 0  0 1 0  0 0 1  2 0 0  0 2 0\n";
        assertRejected(L"Stage15.3D.Reject.PointsOnlyNoCellsVTK", writeText(L"points_only_no_cells.vtk", barePtsVtk));

        const char* nodesOnlyMsh =
            "$MeshFormat\n4.1 0 8\n$EndMeshFormat\n$Nodes\n1 3 1 3\n3 1 0 3\n1\n2\n3\n0 0 0\n1 0 0\n0 1 0\n$EndNodes\n";
        assertRejected(L"Stage15.3D.Reject.NodesOnlyNoElementsMSH", writeText(L"nodes_only.msh", nodesOnlyMsh));

        // Group 3 (.ase & .urdf): headerless/empty .ase and visual-less .urdf must be rejected
        assertRejected(L"Stage15.3D.Reject.HeaderlessASE",
                       writeText(L"no_magic.ase", "*GEOMOBJECT {\n\t*MESH {\n\t\t*MESH_NUMVERTEX 3\n\t\t*MESH_NUMFACES 1\n\t}\n}\n"));
        assertRejected(L"Stage15.3D.Reject.EmptyASE",
                       writeText(L"empty_scene.ase", "*3DSMAX_ASCIIEXPORT\t200\n*COMMENT \"no geomobjects\"\n"));
        assertRejected(L"Stage15.3D.Reject.EmptyRobotURDF",
                       writeText(L"empty_robot.urdf", "<?xml version=\"1.0\"?><robot name=\"empty\"><link name=\"base_link\"/></robot>"));

        // Group 4 (.dem & .laz): random prose .dem and .laz with corrupted chunkTableStart must be rejected
        assertRejected(L"Stage15.3D.Reject.ProseTextDEM",
                       writeText(L"prose_not_dem.dem", "This is a plain text document, not an ESRI or USGS elevation model.\n1 2 3 4 5\n"));

        auto badChunkLaz = LazTestEnc::BuildLazPoint10v2Fixture({ {0,0,0}, {2,1,3}, {4,2,5}, {6,3,1} }, 4u);
        uint64_t bogusTblOff = 0x0000FFFFFFFF0000ULL; // out-of-bounds chunk table pointer at offset 227 + 54 + 40 = 321
        if (badChunkLaz.size() >= 329) std::memcpy(badChunkLaz.data() + 321, &bogusTblOff, 8);
        assertRejected(L"Stage15.3D.Reject.CorruptChunkLAZ", writeBytes(L"corrupt_chunk_tbl.laz", badChunkLaz));

        // Group 5 (.blend, .wrl, .x3d): corrupt Zstd .blend, headerless .wrl, and non-X3D XML must be rejected
        std::vector<uint8_t> corruptZstdBlend = { 0x28, 0xB5, 0x2F, 0xFD, 0x20, 0x40, 0xFF, 0xFF, 0xDE, 0xAD, 0xBE, 0xEF };
        assertRejected(L"Stage15.3D.Reject.CorruptZstdBLEND", writeBytes(L"corrupt_zstd.blend", corruptZstdBlend));
        assertRejected(L"Stage15.3D.Reject.HeaderlessWRL", writeText(L"no_vrml_hdr.wrl", "Shape { geometry Box { size 2 2 2 } }\n"));
        assertRejected(L"Stage15.3D.Reject.NonX3dXML", writeText(L"not_x3d.x3d", "<?xml version=\"1.0\"?><svg><Box size=\"2 2 2\"/></svg>"));
    }

    RunFormatTests3DBatch70(tc);

    // ========================================================================
    // 25. AutoCAD R2018 (AC1032) Genuine UMC + BOT 2-Bit Object Header & AcDbSubDMesh (.dwg)
    // ========================================================================
    {
        struct DwgTestBitWriter {
            std::vector<uint8_t> buf;
            size_t bitPos = 0;

            void WriteB(uint8_t b) {
                if ((bitPos >> 3u) >= buf.size()) buf.push_back(0u);
                if (b & 1u) buf[bitPos >> 3u] |= static_cast<uint8_t>(1u << (7u - (bitPos & 7u)));
                ++bitPos;
            }
            void WriteBB(uint8_t v) { WriteB((v >> 1u) & 1u); WriteB(v & 1u); }
            void WriteRC(uint8_t v) { for (int i = 7; i >= 0; --i) WriteB(static_cast<uint8_t>((v >> i) & 1u)); }
            void WriteRS(uint16_t v) { WriteRC(static_cast<uint8_t>(v & 0xFFu)); WriteRC(static_cast<uint8_t>((v >> 8u) & 0xFFu)); }
            void WriteRL(uint32_t v) { for (int i = 0; i < 4; ++i) WriteRC(static_cast<uint8_t>((v >> (i * 8)) & 0xFFu)); }
            void WriteRD(double d) {
                uint8_t raw[8]; std::memcpy(raw, &d, 8);
                for (int i = 0; i < 8; ++i) WriteRC(raw[i]);
            }
            void WriteBS(int16_t v) {
                if (v == 0) WriteBB(2u);
                else if (v == 256) WriteBB(3u);
                else if (v > 0 && v < 256) { WriteBB(1u); WriteRC(static_cast<uint8_t>(v)); }
                else { WriteBB(0u); WriteRS(static_cast<uint16_t>(v)); }
            }
            void WriteBL(int32_t v) {
                if (v == 0) WriteBB(2u);
                else if (v > 0 && v < 256) { WriteBB(1u); WriteRC(static_cast<uint8_t>(v)); }
                else { WriteBB(0u); WriteRL(static_cast<uint32_t>(v)); }
            }
            void WriteBD(double d) {
                if (d == 0.0) WriteBB(2u);
                else if (d == 1.0) WriteBB(1u);
                else { WriteBB(0u); WriteRD(d); }
            }
            void WriteDD(double d, double def) {
                if (d == def) WriteBB(0u);
                else { WriteBB(3u); WriteRD(d); }
            }
            void WriteUMC(uint64_t u) {
                do {
                    uint8_t b = static_cast<uint8_t>(u & 0x7Fu);
                    u >>= 7u;
                    if (u != 0u) b |= 0x80u;
                    WriteRC(b);
                } while (u != 0u);
            }
            void WriteBOT(int16_t t) {
                if (t >= 0 && t <= 255) { WriteBB(0u); WriteRC(static_cast<uint8_t>(t)); }
                else if (t >= 0x1F0 && t <= 0x1F0 + 255) { WriteBB(1u); WriteRC(static_cast<uint8_t>(t - 0x1F0)); }
                else { WriteBB(2u); WriteRS(static_cast<uint16_t>(t)); }
            }
            void WriteHandle(uint8_t code, uint8_t val) {
                WriteRC(static_cast<uint8_t>((code << 4u) | 1u));
                WriteRC(val);
            }
            void WriteTU(const char* s) {
                int16_t n = static_cast<int16_t>(std::strlen(s));
                WriteBS(n);
                for (int16_t i = 0; i < n; ++i) WriteRS(static_cast<uint8_t>(s[i]));
            }
        };

        auto writeCommonEntityR2018 = [](DwgTestBitWriter& bw, uint8_t handleVal, bool trueColor) {
            bw.WriteHandle(0u, handleVal); // self-handle (NO RL objBits before handle in R2010+!)
            bw.WriteBS(0);                 // EED = 0
            bw.WriteB(0);                  // graphicPresent = 0
            bw.WriteBB(2u);                // entmode = model space
            bw.WriteBL(0);                 // numReactors = 0
            bw.WriteB(1u);                 // is_xdic_missing = 1
            bw.WriteB(0u);                 // has_ds_data = 0 (R2013+)
            if (trueColor) {
                bw.WriteBS(static_cast<int16_t>(0x8007u)); // ENC with RGB flag!
                bw.WriteBL(0x00FF8040);
            } else {
                bw.WriteBS(256);           // ByLayer ENC
            }
            bw.WriteBD(1.0);               // ltype_scale
            bw.WriteBB(2u);                // ltype_flags
            bw.WriteBB(0u);                // plotstyle_flags
            bw.WriteBB(0u);                // material_flags
            bw.WriteRC(0u);                // shadow_flags
            bw.WriteB(0u); bw.WriteB(0u); bw.WriteB(0u); // visualstyles (R2010+)
            bw.WriteBS(0);                 // invis
            bw.WriteRC(0u);                // lineweight
        };

        // Build AcDb:Classes defining class 500 = AcDbSubDMesh ("MESH") using R2007+ String Stream TU
        std::vector<uint8_t> classesSec;
        {
            DwgTestBitWriter strStream;
            strStream.WriteTU("ObjectDBX Classes");
            strStream.WriteTU("AcDbSubDMesh");
            strStream.WriteTU("MESH");
            size_t strBits = strStream.bitPos;

            DwgTestBitWriter clsData;
            clsData.WriteRL(0u); // placeholder for totalBits at bit 0..31
            clsData.WriteBS(500); // maxClassNum
            clsData.WriteRC(0u); clsData.WriteRC(0u); clsData.WriteB(1u);
            // Class 500 body (strings live in strStream!)
            clsData.WriteBS(500);
            clsData.WriteBS(0);     // proxyFlags
            clsData.WriteB(0u);     // wasZombie
            clsData.WriteBS(0x1F2); // itemClassId = entity
            clsData.WriteBL(1);     // numInstances
            clsData.WriteBL(32);    // dwgVersion
            clsData.WriteBL(0);     // maintVersion
            clsData.WriteBL(0);
            clsData.WriteBL(0);

            // Append string stream + 16-bit strBits size + 1-bit hasStrings=1 flag
            for (size_t b = 0; b < strBits; ++b) {
                uint8_t bit = (strStream.buf[b >> 3u] >> (7u - (b & 7u))) & 1u;
                clsData.WriteB(bit);
            }
            clsData.WriteRS(static_cast<uint16_t>(strBits));
            clsData.WriteB(1u); // hasStrings = 1!

            uint32_t totalBits = static_cast<uint32_t>(clsData.bitPos);
            clsData.buf[0] = static_cast<uint8_t>(totalBits & 0xFFu);
            clsData.buf[1] = static_cast<uint8_t>((totalBits >> 8u) & 0xFFu);
            clsData.buf[2] = static_cast<uint8_t>((totalBits >> 16u) & 0xFFu);
            clsData.buf[3] = static_cast<uint8_t>((totalBits >> 24u) & 0xFFu);

            static const uint8_t kStartS[16] = {
                0x8Du,0xA1u,0xC4u,0xB8u,0xC4u,0xA9u,0xF8u,0xC5u,0xC0u,0xDCu,0xF4u,0x5Fu,0xE7u,0xCFu,0xB6u,0x8Au
            };
            static const uint8_t kEndS[16] = {
                0x72u,0x5Eu,0x3Bu,0x47u,0x3Bu,0x56u,0x07u,0x3Au,0x3Fu,0x23u,0x0Bu,0xA0u,0x18u,0x30u,0x49u,0x75u
            };
            ByteWriter cw;
            cw.Bytes(kStartS, 16);
            cw.U32LE(static_cast<uint32_t>(clsData.buf.size()));
            cw.U32LE(0u); // R2013+ highZero RL
            cw.Bytes(clsData.buf.data(), clsData.buf.size());
            cw.U16LE(0u); // CRC16
            cw.Bytes(kEndS, 16);
            classesSec = std::move(cw.buf);
        }

        // Build Record 1: 3DFACE (BOT prefix 0 -> 28) at Z=18 (maps to Y=18 in viewer)
        auto packR2018Record = [](const DwgTestBitWriter& payloadBits, uint32_t handleBitsCount) -> std::vector<uint8_t> {
            DwgTestBitWriter rec;
            rec.WriteUMC(handleBitsCount);
            for (size_t b = 0; b < payloadBits.bitPos; ++b) {
                uint8_t bit = (payloadBits.buf[b >> 3u] >> (7u - (b & 7u))) & 1u;
                rec.WriteB(bit);
            }
            while ((rec.bitPos & 7u) != 0u) rec.WriteB(0u);
            for (uint32_t hb = 0; hb < handleBitsCount; ++hb) rec.WriteB(0u);

            ByteWriter out;
            uint16_t ms = static_cast<uint16_t>(rec.buf.size());
            out.U16LE(ms);
            out.Bytes(rec.buf.data(), rec.buf.size());
            out.U16BE(0u); // CRC
            return out.buf;
        };

        DwgTestBitWriter ent1;
        ent1.WriteBOT(28); // 3DFACE via BOT 2-bit prefix 00 + RC(28)
        writeCommonEntityR2018(ent1, 10u, true);
        ent1.WriteB(1u); // hasNoFlags = 1
        ent1.WriteB(0u); // zIsZero = 0
        ent1.WriteRD(-4.0); ent1.WriteRD(-4.0); ent1.WriteRD(18.0);
        ent1.WriteDD( 4.0, -4.0); ent1.WriteDD(-4.0, -4.0); ent1.WriteDD(18.0, 18.0);
        ent1.WriteDD( 4.0,  4.0); ent1.WriteDD( 4.0, -4.0); ent1.WriteDD(18.0, 18.0);
        ent1.WriteDD(-4.0,  4.0); ent1.WriteDD( 4.0,  4.0); ent1.WriteDD(18.0, 18.0);
        auto rec1Bytes = packR2018Record(ent1, 16u);

        // Build Record 2: MESH / AcDbSubDMesh (BOT prefix 01 -> 0x1F0 + 4 = 500) 5-vertex pyramid
        DwgTestBitWriter ent2;
        ent2.WriteBOT(500); // Class 500 via BOT 2-bit prefix 01 + RC(4)
        writeCommonEntityR2018(ent2, 11u, false);
        ent2.WriteBS(2); // version = 2
        ent2.WriteB(0u); // blendCrease = 0
        ent2.WriteBL(0); // subdivLevels = 0
        ent2.WriteBL(5); // 5 vertices
        const double pyrV[5][3] = {
            {-3.0, -3.0, 0.0}, {3.0, -3.0, 0.0}, {3.0, 3.0, 0.0}, {-3.0, 3.0, 0.0}, {0.0, 0.0, 10.0}
        };
        for (const auto& pv : pyrV) { ent2.WriteBD(pv[0]); ent2.WriteBD(pv[1]); ent2.WriteBD(pv[2]); }
        const int32_t faceArr[21] = {
            4, 0, 3, 2, 1,
            3, 0, 1, 4,
            3, 1, 2, 4,
            3, 2, 3, 4,
            3, 3, 0, 4
        };
        ent2.WriteBL(21);
        for (int32_t fv : faceArr) ent2.WriteBL(fv);
        auto rec2Bytes = packR2018Record(ent2, 16u);

        std::vector<uint8_t> objectsSec = rec1Bytes;
        size_t rec2Off = objectsSec.size();
        objectsSec.insert(objectsSec.end(), rec2Bytes.begin(), rec2Bytes.end());

        // Build AcDb:Handles map: (handle=10, off=0), (deltaH=1 -> 11, deltaOff=rec2Off)
        std::vector<uint8_t> handlesSec;
        {
            ByteWriter blk;
            blk.U8(10u); // UMC hd = 10
            blk.U8(0u);  // MC ld = 0
            blk.U8(1u);  // UMC hd = 1 (-> handle 11)
            // Encode MC positive delta rec2Off
            uint64_t v = rec2Off;
            while (v >= 0x40u) {
                blk.U8(static_cast<uint8_t>(0x80u | (v & 0x7Fu)));
                v >>= 7u;
            }
            blk.U8(static_cast<uint8_t>(v & 0x3Fu));

            ByteWriter hm;
            hm.U16BE(static_cast<uint16_t>(2u + blk.buf.size()));
            hm.Bytes(blk.buf.data(), blk.buf.size());
            hm.U16BE(0u); // CRC16
            hm.U16BE(2u); // End-of-map block
            hm.U16BE(0u);
            handlesSec = std::move(hm.buf);
        }

        // Assemble complete AC1032 paged .dwg file (uncompressed pages compCode=1)
        ByteWriter dwg;
        dwg.Bytes("AC1032", 6);
        while (dwg.buf.size() < 0x100u) dwg.U8(0u);

        // Page 1 (id=1): AcDb:Classes, Page 2 (id=2): AcDb:Handles, Page 3 (id=3): AcDb:AcDbObjects, Page 4 (id=4): SectionMap, Page 5: PageMap
        auto writeDataPage = [&](const std::vector<uint8_t>& sec) -> uint32_t {
            size_t start = dwg.buf.size();
            for (int i = 0; i < 32; ++i) dwg.U8(0u); // 32B encrypted data page header
            dwg.Bytes(sec.data(), sec.size());
            while (((dwg.buf.size() - start) & 7u) != 0u) dwg.U8(0u);
            return static_cast<uint32_t>(dwg.buf.size() - start);
        };
        uint32_t p1Sz = writeDataPage(classesSec);
        uint32_t p2Sz = writeDataPage(handlesSec);
        uint32_t p3Sz = writeDataPage(objectsSec);

        // Build Section Map payload (3 sections)
        ByteWriter secMapPay;
        secMapPay.U32LE(3u); secMapPay.U32LE(2u); secMapPay.U32LE(0x7400u); secMapPay.U32LE(0u); secMapPay.U32LE(3u);
        auto appendSecDesc = [&](const char* name, size_t dataLen, int32_t pgId, uint32_t pgTotalSz) {
            secMapPay.U64LE(dataLen);
            secMapPay.U32LE(1u); // pageCnt = 1
            secMapPay.U32LE(0x7400u);
            secMapPay.U32LE(1u);
            secMapPay.U32LE(1u); // compCode = 1 (raw)
            secMapPay.U32LE(0u); secMapPay.U32LE(0u);
            char nm[64] = {};
            std::memcpy(nm, name, std::strlen(name));
            secMapPay.Bytes(nm, 64);
            secMapPay.I32LE(pgId);
            secMapPay.U32LE(pgTotalSz - 32u);
            secMapPay.U64LE(0u);
        };
        appendSecDesc("AcDb:Classes",     classesSec.size(), 1, p1Sz);
        appendSecDesc("AcDb:Handles",     handlesSec.size(), 2, p2Sz);
        appendSecDesc("AcDb:AcDbObjects", objectsSec.size(), 3, p3Sz);

        auto writeSysPage = [&](uint32_t pgType, const std::vector<uint8_t>& pay) -> uint32_t {
            size_t start = dwg.buf.size();
            dwg.U32LE(pgType);
            dwg.U32LE(static_cast<uint32_t>(pay.size()));
            dwg.U32LE(static_cast<uint32_t>(pay.size()));
            dwg.U32LE(1u); // compType = 1 (uncompressed)
            dwg.U32LE(0u); // checksum
            dwg.Bytes(pay.data(), pay.size());
            return static_cast<uint32_t>(dwg.buf.size() - start);
        };
        uint32_t p4Sz = writeSysPage(0x4163003Bu, secMapPay.buf);

        uint64_t pageMapFileOff = dwg.buf.size();
        ByteWriter pageMapPay;
        pageMapPay.I32LE(1); pageMapPay.U32LE(p1Sz);
        pageMapPay.I32LE(2); pageMapPay.U32LE(p2Sz);
        pageMapPay.I32LE(3); pageMapPay.U32LE(p3Sz);
        pageMapPay.I32LE(4); pageMapPay.U32LE(p4Sz);
        writeSysPage(0x41630E3Bu, pageMapPay.buf);

        // Encrypt 0x6C header at 0x80 with ODA LCG mask
        uint8_t plainHdr[0x6C] = {};
        std::memcpy(plainHdr, "AcFssFcAJMB", 11);
        uint32_t secMapId = 4u;
        uint64_t pmRel = pageMapFileOff - 0x100ULL;
        std::memcpy(plainHdr + 0x28, &pmRel, 8);
        std::memcpy(plainHdr + 0x30, &secMapId, 4);
        uint32_t seed = 1u;
        for (size_t i = 0; i < 0x6Cu; ++i) {
            seed = seed * 0x343FDu + 0x269EC3u;
            dwg.buf[0x80u + i] = plainHdr[i] ^ static_cast<uint8_t>(seed >> 16u);
        }

        LoadedMesh3D mDwgUmc;
        bool okDwg = Model3DViewer::InspectMeshFile(writeBytes(L"r2018_genuine_umc_bot.dwg", dwg.buf), mDwgUmc);
        tc.Record(okDwg && mDwgUmc.loadedOk && mDwgUmc.rawVertexCount == 9 && mDwgUmc.rawTriangleCount == 8 &&
                  std::fabs(mDwgUmc.bboxMax.y - 18.0f) < 0.02f,
                  L"Stage15.3D.DwgDxf3D.GenuineR2018UmcBotAndSubDMeshDWG",
                  L"verts=" + std::to_wstring(mDwgUmc.rawVertexCount) +
                  L" tris=" + std::to_wstring(mDwgUmc.rawTriangleCount) +
                  L" maxY=" + std::to_wstring(mDwgUmc.bboxMax.y));
    }

    // ========================================================================
    // 26. Siemens JT Open (.jt) ISO 14306 TriStripSetShapeLODElement + Int32CDP (Bitlength + Lag1 & Huffman)
    // ========================================================================
    {
        auto tfZlibWrap = [](const uint8_t* ptr, size_t len) -> std::vector<uint8_t> {
            ByteWriter zw;
            zw.U8(0x78u); zw.U8(0x9Cu);
            auto def = TfDeflateFixedHuffman(ptr, len);
            zw.Bytes(def.data(), def.size());
            uint32_t s1 = 1u, s2 = 0u;
            for (size_t i = 0; i < len; ++i) {
                s1 = (s1 + ptr[i]) % 65521u;
                s2 = (s2 + s1) % 65521u;
            }
            zw.U32BE((s2 << 16u) | s1);
            return zw.buf;
        };

        struct JtWordPacker {
            std::vector<uint32_t> words;
            size_t bitPos = 0;
            void PutBit(uint32_t b) {
                if ((bitPos >> 5u) >= words.size()) words.push_back(0u);
                if (b & 1u) words[bitPos >> 5u] |= (1u << (31u - (bitPos & 31u)));
                ++bitPos;
            }
            void PutU(uint32_t v, int n) {
                for (int i = n - 1; i >= 0; --i) PutBit((v >> i) & 1u);
            }
            void PutS(int32_t v, int n) {
                PutU(static_cast<uint32_t>(v), n);
            }
        };

        auto writeInt32CdpBitlengthFixed = [](ByteWriter& bw, const std::vector<int32_t>& res) {
            bw.U8(1u); // CodecType_Bitlength
            int32_t mn = res[0], mx = res[0];
            for (int32_t r : res) { mn = std::min(mn, r); mx = std::max(mx, r); }
            auto reqSignedBits = [](int32_t v) -> int {
                if (v == 0) return 0;
                for (int b = 1; b < 32; ++b) {
                    int32_t lo = -(1 << (b - 1));
                    int32_t hi = (1 << (b - 1)) - 1;
                    if (v >= lo && v <= hi) return b;
                }
                return 32;
            };
            int minB = reqSignedBits(mn);
            int maxB = reqSignedBits(mx);
            uint64_t span = static_cast<uint64_t>(static_cast<int64_t>(mx) - static_cast<int64_t>(mn));
            int fw = 0;
            while (fw < 32 && (1ULL << fw) <= span) ++fw;

            JtWordPacker wp;
            wp.PutU(0u, 1); // mode = 0 (fixed-width)
            wp.PutU(static_cast<uint32_t>(minB), 6);
            wp.PutU(static_cast<uint32_t>(maxB), 6);
            if (minB > 0) wp.PutS(mn, minB);
            if (maxB > 0) wp.PutS(mx, maxB);
            for (int32_t r : res) {
                if (fw > 0) wp.PutU(static_cast<uint32_t>(static_cast<int64_t>(r) - static_cast<int64_t>(mn)), fw);
            }
            bw.I32LE(static_cast<int32_t>(wp.bitPos));
            bw.I32LE(static_cast<int32_t>(res.size()));
            bw.I32LE(static_cast<int32_t>(wp.words.size()));
            for (uint32_t w : wp.words) bw.U32LE(w);
        };

        auto writeInt32CdpBitlengthVar = [](ByteWriter& bw, const std::vector<int32_t>& res) {
            bw.U8(1u); // CodecType_Bitlength
            JtWordPacker wp;
            wp.PutU(1u, 1);  // mode = 1 (variable-width adaptive step)
            wp.PutS(0, 8);   // iMean = 0
            wp.PutU(4u, 3);  // cBlkValBits = 4 (signed step -8..+7)
            wp.PutU(4u, 3);  // cBlkLenBits = 4 (runLen up to 15)
            // Adjust width from 0 -> +7 -> +7 -> +3 = 17 bits, then emit all residuals in one block
            wp.PutU(1u, 1);
            wp.PutS(7, 4);
            wp.PutU(1u, 1);
            wp.PutS(7, 4);
            wp.PutU(1u, 1);
            wp.PutS(3, 4);
            wp.PutU(0u, 1); // done adjusting width (width = 17)
            wp.PutU(static_cast<uint32_t>(res.size()), 4);
            for (int32_t r : res) wp.PutS(r, 17);

            bw.I32LE(static_cast<int32_t>(wp.bitPos));
            bw.I32LE(static_cast<int32_t>(res.size()));
            bw.I32LE(static_cast<int32_t>(wp.words.size()));
            for (uint32_t w : wp.words) bw.U32LE(w);
        };

        auto lag1Residuals = [](const std::vector<int32_t>& vals) -> std::vector<int32_t> {
            std::vector<int32_t> r(vals.size());
            for (size_t i = 0; i < vals.size(); ++i) r[i] = (i == 0u) ? vals[0] : (vals[i] - vals[i - 1u]);
            return r;
        };

        auto wrapJt95File = [&](const std::vector<uint8_t>& elemPayload) -> std::vector<uint8_t> {
            ByteWriter segRaw;
            segRaw.I32LE(static_cast<int32_t>(elemPayload.size()));
            // TriStripSetShapeLODElement GUID: {0x10dd10ab, 0x2ac8, 0x11d1, {0x9b,0x6b,0x00,0x80,0xc7,0xbb,0x59,0x97}}
            segRaw.U32LE(0x10dd10abu);
            segRaw.U16LE(0x2ac8u);
            segRaw.U16LE(0x11d1u);
            const uint8_t gb[8] = { 0x9bu, 0x6bu, 0x00u, 0x80u, 0xc7u, 0xbbu, 0x59u, 0x97u };
            segRaw.Bytes(gb, 8);
            segRaw.U8(4u); // objBaseType = Shape LOD Data

            ByteWriter fullElem;
            fullElem.I32LE(static_cast<int32_t>(17u + elemPayload.size()));
            fullElem.Bytes(segRaw.buf.data() + 4, 17);
            fullElem.Bytes(elemPayload.data(), elemPayload.size());

            auto zBytes = tfZlibWrap(fullElem.buf.data(), fullElem.buf.size());

            const uint8_t segGuid[16] = { 1,2,3,4, 5,6, 7,8, 9,10,11,12,13,14,15,16 };
            ByteWriter seg;
            seg.Bytes(segGuid, 16);
            seg.I32LE(7); // Shape LOD0
            uint32_t totalSegLen = static_cast<uint32_t>(24u + 9u + zBytes.size());
            seg.U32LE(totalSegLen);
            seg.U32LE(2u); // compressionFlag = 2
            seg.U32LE(static_cast<uint32_t>(zBytes.size() + 1u)); // compDataLen includes alg byte
            seg.U8(2u);    // compressionAlgorithm = 2 (zlib)
            seg.Bytes(zBytes.data(), zBytes.size());

            ByteWriter jt;
            std::string ver80(80, ' ');
            std::memcpy(ver80.data(), "Version 9.5 JT", 14);
            ver80[78] = '\r'; ver80[79] = '\n';
            jt.Bytes(ver80.data(), 80);
            jt.U8(0u); // little-endian
            jt.I32LE(0);
            uint32_t tocOff = 105u;
            jt.U32LE(tocOff);
            jt.Bytes(segGuid, 16); // LSG stub GUID -> byte 105

            uint32_t segOffset = static_cast<uint32_t>(tocOff + 4u + 28u);
            jt.U32LE(1u); // 1 TOC entry
            jt.Bytes(segGuid, 16);
            jt.U32LE(segOffset);
            jt.U32LE(totalSegLen);
            jt.U32LE(7u << 24u); // segType = 7 (Shape LOD0)
            jt.Bytes(seg.buf.data(), seg.buf.size());
            return jt.buf;
        };

        // JT File A: Quantized 16-bit VertexCoordArray + Bitlength (fixed & variable-width) + Lag1 predictor
        ByteWriter bodyA;
        bodyA.I32LE(42);   // Object ID (JT v9)
        bodyA.U16LE(1u);   // VertexShapeLODData version
        bodyA.U64LE(1ULL); // vertexBindings
        bodyA.U16LE(1u);   // TriStripSet version
        bodyA.U16LE(1u);   // VertexBasedShapeCompressedRepData version
        bodyA.U8(0u); bodyA.U8(0u); bodyA.U8(0u); // bindings = 0
        bodyA.U8(16u); bodyA.U8(0u); bodyA.U8(0u); bodyA.U8(0u); // 16 bitsPerVertex
        // 2 strips of 4 vertices each: [0, 4, 8] encoded with Lag1 residuals [0, 4, 4]
        writeInt32CdpBitlengthFixed(bodyA, { 0, 4, 4 });
        // PointQuantizerData: X in [-10, 10], Y in [0, 24], Z in [-6, 6], 16 bits each
        bodyA.F32LE(-10.0f); bodyA.F32LE(10.0f); bodyA.U8(16u);
        bodyA.F32LE(0.0f);   bodyA.F32LE(24.0f); bodyA.U8(16u);
        bodyA.F32LE(-6.0f);  bodyA.F32LE(6.0f);  bodyA.U8(16u);
        bodyA.I32LE(8); // 8 vertices
        const std::vector<int32_t> qX = { 0, 32768, 0, 32768, 32768, 65535, 32768, 65535 };
        const std::vector<int32_t> qY = { 0, 0, 32768, 32768, 32768, 32768, 65535, 65535 };
        const std::vector<int32_t> qZ = { 0, 0, 65535, 65535, 0, 0, 65535, 65535 };
        writeInt32CdpBitlengthFixed(bodyA, lag1Residuals(qX));
        writeInt32CdpBitlengthVar(bodyA,   lag1Residuals(qY)); // exercises adaptive-width mode=1!
        writeInt32CdpBitlengthFixed(bodyA, lag1Residuals(qZ));
        auto jtBytesA = wrapJt95File(bodyA.buf);

        // JT File B: Lossless Zlib-compressed float32 vertices (bitsPerVertex = 0) + Huffman Int32CDP strip offsets
        ByteWriter bodyB;
        bodyB.I32LE(99);
        bodyB.U16LE(1u);
        bodyB.U64LE(1ULL);
        bodyB.U16LE(1u);
        bodyB.U16LE(1u);
        bodyB.U8(0u); bodyB.U8(0u); bodyB.U8(0u);
        bodyB.U8(0u); bodyB.U8(0u); bodyB.U8(0u); bodyB.U8(0u); // bitsPerVertex = 0 -> lossless raw float32!
        // Huffman Int32CDP for stripOffsets [0, 5] (Stride1/Lag1 residuals = [0, 5])
        {
            bodyB.U8(2u); // CodecType_Huffman
            bodyB.U8(1u); // 1 probability context table
            // Pack byte bitstream: entryCount=2 (16b), symBits=4 (6b), occBits=4 (6b), valBits=4 (6b), nextBits=0 (6b), minVal=0 (32b)
            struct ByteBitPacker {
                std::vector<uint8_t> buf;
                size_t bitPos = 0;
                void PutU(uint32_t v, int n) {
                    for (int i = n - 1; i >= 0; --i) {
                        if ((bitPos >> 3u) >= buf.size()) buf.push_back(0u);
                        if ((v >> i) & 1u) buf[bitPos >> 3u] |= static_cast<uint8_t>(1u << (7u - (bitPos & 7u)));
                        ++bitPos;
                    }
                }
            } pbp;
            pbp.PutU(2u, 16); // 2 entries
            pbp.PutU(4u, 6);  // symBits = 4
            pbp.PutU(4u, 6);  // occBits = 4
            pbp.PutU(4u, 6);  // valBits = 4
            pbp.PutU(0u, 6);  // nextBits = 0
            pbp.PutU(0u, 32); // minVal = 0
            // Entry 0: sym=0, occ=1, valOff=0 (left / bit 1 because smaller occ=1 popped first!)
            pbp.PutU(0u, 4); pbp.PutU(1u, 4); pbp.PutU(0u, 4);
            // Entry 1: sym=1, occ=2, valOff=5 (right / bit 0)
            pbp.PutU(1u, 4); pbp.PutU(2u, 4); pbp.PutU(5u, 4);
            bodyB.Bytes(pbp.buf.data(), pbp.buf.size());
            bodyB.I32LE(0); // oobCount = 0
            // CodeText: bit 1 -> Entry 0 (0), bit 0 -> Entry 1 (5) => 2 bits: 0b10_000... = 0x80000000u
            bodyB.I32LE(2); // codeTextBits = 2
            bodyB.I32LE(2); // valueCount = 2
            bodyB.I32LE(1); // 1 word
            bodyB.U32LE(0x80000000u);
        }
        ByteWriter rawF32;
        const float strip5[5][3] = {
            {0.0f, 0.0f, 0.0f}, {6.0f, 0.0f, 0.0f}, {0.0f, 8.0f, 0.0f}, {6.0f, 8.0f, 3.0f}, {3.0f, 15.0f, 5.0f}
        };
        for (const auto& sv : strip5) { rawF32.F32LE(sv[0]); rawF32.F32LE(sv[1]); rawF32.F32LE(sv[2]); }
        auto zVtx = tfZlibWrap(rawF32.buf.data(), rawF32.buf.size());
        bodyB.I32LE(static_cast<int32_t>(rawF32.buf.size()));
        bodyB.I32LE(static_cast<int32_t>(zVtx.size()));
        bodyB.Bytes(zVtx.data(), zVtx.size());
        auto jtBytesB = wrapJt95File(bodyB.buf);

        LoadedMesh3D mJtA, mJtB;
        bool okJtA = Model3DViewer::InspectMeshFile(writeBytes(L"iso14306_quant_bitlen.jt", jtBytesA), mJtA);
        bool okJtB = Model3DViewer::InspectMeshFile(writeBytes(L"iso14306_lossless_huff.jt", jtBytesB), mJtB);
        tc.Record(okJtA && okJtB &&
                  mJtA.rawVertexCount == 8 && mJtA.rawTriangleCount == 4 &&
                  std::fabs(mJtA.bboxSize.x - 20.0f) < 0.05f && std::fabs(mJtA.bboxMax.y - 24.0f) < 0.05f &&
                  mJtB.rawVertexCount == 5 && mJtB.rawTriangleCount == 3 &&
                  std::fabs(mJtB.bboxMax.y - 15.0f) < 0.02f &&
                  HashMeshGeometry(mJtA) != HashMeshGeometry(mJtB),
                  L"Stage15.3D.JT.Iso14306TriStripSetInt32CdpBitlengthLag1AndHuffman",
                  L"vertsA=" + std::to_wstring(mJtA.rawVertexCount) + L" maxYA=" + std::to_wstring(mJtA.bboxMax.y) +
                  L" vertsB=" + std::to_wstring(mJtB.rawVertexCount));
    }

    // ========================================================================
    // 27. LDraw LEGO CAD Multi-Part Document (.mpd, .ldr): Matrix Instancing, Mirroring, Bowtie Quads & Primitives
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".ldr") &&
                  Model3DViewer::Is3DModelExtension(L".mpd"),
                  L"Stage15.3D.LDraw.ExtensionsRegistered");

        const char* mpdAssembly =
            "0 FILE space_shuttle.ldr\n"
            "0 Name: Space Shuttle Test\n"
            "0 !LDRAW_ORG Model\n"
            "0 !COLOUR CustomGold CODE 88 VALUE #FFD700 EDGE #333333\n"
            "0 BFC CERTIFY CCW\n"
            // Right wing at X=+20 (identity 3x3)
            "1 4 20 0 0 1 0 0 0 1 0 0 0 1 sub\\wing_pod.ldr\n"
            // Left wing mirrored across X at X=-20 (det = -1 < 0 -> automatic BFC winding flip!)
            "0 BFC INVERTNEXT\n"
            "1 88 -20 0 0 -1 0 0 0 1 0 0 0 1 sub/wing_pod.ldr\n"
            // Top LEGO stud primitive at Y=-12 (maps to viewer +Y = +12..+16!)
            "1 14 0 -12 0 1 0 0 0 1 0 0 0 1 stud.dat\n"
            "0 NOFILE\n"
            "0 FILE sub/wing_pod.ldr\n"
            "0 BFC CERTIFY CCW\n"
            // Triangle rising to LDraw Y = -10 (viewer +Y = +10)
            "3 16 0 0 0 10 0 0 5 -10 4\n"
            // Intentionally bowtie-ordered quad (0,0,0 -> 10,0,8 -> 10,0,0 -> 0,0,8) that must untwist cleanly into 2 tris!
            "4 16 0 0 0 10 0 8 10 0 0 0 0 8\n"
            "0 NOFILE\n";

        LoadedMesh3D mMpd;
        bool okMpd = Model3DViewer::InspectMeshFile(writeText(L"shuttle_assy.mpd", mpdAssembly), mMpd);
        // Each wing_pod = 1 tri + 2 quad tris = 3 tris (x2 wings = 6 tris) + stud.dat (16*3 = 48 tris) = 54 tris!
        // X spans from (-20 - 10) = -30 to (+20 + 10) = +30; stud top at LDraw y = -12 - 4 = -16 -> viewer maxY = +16!
        tc.Record(okMpd && mMpd.loadedOk && mMpd.rawTriangleCount == 54 &&
                  std::fabs(mMpd.bboxMin.x - (-30.0f)) < 0.05f &&
                  std::fabs(mMpd.bboxMax.x - 30.0f) < 0.05f &&
                  std::fabs(mMpd.bboxMax.y - 16.0f) < 0.05f,
                  L"Stage15.3D.LDraw.MpdSubmodelsMirrorBowtieAndPrimitives",
                  L"tris=" + std::to_wstring(mMpd.rawTriangleCount) +
                  L" minX=" + std::to_wstring(mMpd.bboxMin.x) + L" maxX=" + std::to_wstring(mMpd.bboxMax.x) +
                  L" maxY=" + std::to_wstring(mMpd.bboxMax.y));

        LoadedMesh3D mBadLdr;
        bool okBadLdr = Model3DViewer::InspectMeshFile(writeText(L"not_ldraw.ldr", "Just a plain README note without LDraw geometry.\n"), mBadLdr);
        tc.Record(!okBadLdr && !mBadLdr.loadedOk, L"Stage15.3D.LDraw.RejectNonLDrawText");
    }

    // ========================================================================
    // 28. Valve Hammer (.vmf) & Quake CSG Brush Map (.map) + MSVC Linker .map Rejection
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".vmf") &&
                  Model3DViewer::Is3DModelExtension(L".map"),
                  L"Stage15.3D.CsgMapVmf.ExtensionsRegistered");

        // Quake .map: Brush 1 = 6-plane box [0..64]x[0..64]x[0..32] (8v, 12t)
        //             Brush 2 = 5-plane wedge ramp [64..128]x[0..64]x[0..48] (6v, 8t)
        //             Brush 3 = TRIGGER volume [0..512]^3 (all TRIGGER faces -> culled!)
        const char* quakeBrushMap =
            "// Game: Quake / Valve 220 Map\n"
            "{\n"
            "\"classname\" \"worldspawn\"\n"
            "\"wad\" \"quake.wad\"\n"
            "{\n"
            "( 0 64 32 ) ( 64 64 32 ) ( 64 0 32 ) BRICK1 0 0 0 1 1\n"
            "( 0 0 0 ) ( 64 0 0 ) ( 64 64 0 ) BRICK1 0 0 0 1 1\n"
            "( 64 64 32 ) ( 64 64 0 ) ( 64 0 0 ) BRICK1 0 0 0 1 1\n"
            "( 0 0 32 ) ( 0 0 0 ) ( 0 64 0 ) BRICK1 0 0 0 1 1\n"
            "( 64 64 0 ) ( 64 64 32 ) ( 0 64 32 ) BRICK1 0 0 0 1 1\n"
            "( 0 0 0 ) ( 0 0 32 ) ( 64 0 32 ) BRICK1 0 0 0 1 1\n"
            "}\n"
            "{\n"
            "( 64 0 0 ) ( 128 0 0 ) ( 128 64 0 ) METAL2 0 0 0 1 1\n"
            "( 64 0 48 ) ( 64 0 0 ) ( 64 64 0 ) METAL2 0 0 0 1 1\n"
            "( 128 64 0 ) ( 64 64 48 ) ( 64 64 0 ) METAL2 0 0 0 1 1\n"
            "( 64 0 0 ) ( 64 0 48 ) ( 128 0 0 ) METAL2 0 0 0 1 1\n"
            "( 64 64 48 ) ( 128 64 0 ) ( 128 0 0 ) METAL2 0 0 0 1 1\n"
            "}\n"
            "{\n"
            "( 0 512 512 ) ( 512 512 512 ) ( 512 0 512 ) TRIGGER 0 0 0 1 1\n"
            "( 0 0 0 ) ( 512 0 0 ) ( 512 512 0 ) TRIGGER 0 0 0 1 1\n"
            "( 512 512 512 ) ( 512 512 0 ) ( 512 0 0 ) TRIGGER 0 0 0 1 1\n"
            "( 0 0 512 ) ( 0 0 0 ) ( 0 512 0 ) TRIGGER 0 0 0 1 1\n"
            "( 512 512 0 ) ( 512 512 512 ) ( 0 512 512 ) TRIGGER 0 0 0 1 1\n"
            "( 0 0 0 ) ( 0 0 512 ) ( 512 0 512 ) TRIGGER 0 0 0 1 1\n"
            "}\n"
            "}\n";

        std::wstring qMapPath = writeText(L"bunker_csg.map", quakeBrushMap);
        LoadedMesh3D mQMap;
        bool okQMap = Model3DViewer::InspectMeshFile(qMapPath, mQMap);
        tc.Record(okQMap && mQMap.loadedOk && MeshReaders::IsLikelyQuakeMapFile(qMapPath) &&
                  mQMap.rawVertexCount == 14 && mQMap.rawTriangleCount == 20 &&
                  std::fabs(mQMap.bboxSize.x - 128.0f) < 0.05f &&
                  std::fabs(mQMap.bboxMax.y - 48.0f) < 0.05f,
                  L"Stage15.3D.CsgMapVmf.QuakeCsgBoxWedgeAndTriggerCull",
                  L"verts=" + std::to_wstring(mQMap.rawVertexCount) +
                  L" tris=" + std::to_wstring(mQMap.rawTriangleCount) +
                  L" maxY=" + std::to_wstring(mQMap.bboxMax.y));

        // MSVC Linker .map & JS Source .map MUST be cleanly rejected!
        const char* msvcLinkerMap =
            " Explorer2\n\n"
            " Timestamp is 673b1234 (Mon Nov 18 22:00:00 2026)\n\n"
            " Preferred load address is 0000000140000000\n\n"
            " Start         Length     Name                   Class\n"
            " 0001:00000000 0024a100H .text$mn                CODE\n"
            "  Address         Publics by Value              Rva+Base               Lib:Object\n"
            " 0001:00001000       wWinMain                   0000000140002000 f   main.obj\n";
        std::wstring lnkMapPath = writeText(L"Explorer2_linker.map", msvcLinkerMap);
        std::wstring jsMapPath  = writeText(L"bundle.js.map", "{\"version\":3,\"sources\":[\"app.ts\"],\"mappings\":\"AAAA\"}");
        LoadedMesh3D mLnk, mJs;
        bool okLnk = Model3DViewer::InspectMeshFile(lnkMapPath, mLnk);
        bool okJs  = Model3DViewer::InspectMeshFile(jsMapPath, mJs);
        tc.Record(!MeshReaders::IsLikelyQuakeMapFile(lnkMapPath) && !MeshReaders::IsLikelyQuakeMapFile(jsMapPath) &&
                  !okLnk && !okJs && mLnk.tris.empty() && mJs.tris.empty(),
                  L"Stage15.3D.CsgMapVmf.RejectMsvcLinkerAndJsSourceMap");

        // Valve Hammer .vmf: Solid 1 has 1 visible top slab face + 5 TOOLS/TOOLSNODRAW clipping planes (2 tris);
        //                    Solid 2 has power=2 dispinfo terrain quad-grid rising to Z=40 (32 tris) => 34 tris!
        const char* hammerVmf =
            "versioninfo { \"editorversion\" \"400\" \"mapversion\" \"1\" }\n"
            "world\n"
            "{\n"
            "  \"id\" \"1\"\n"
            "  \"classname\" \"worldspawn\"\n"
            "  solid\n"
            "  {\n"
            "    \"id\" \"2\"\n"
            "    side { \"id\" \"1\" \"plane\" \"(0 64 16) (64 64 16) (64 0 16)\" \"material\" \"CONCRETE/CONCRETEFLOOR001A\" }\n"
            "    side { \"id\" \"2\" \"plane\" \"(0 0 0) (64 0 0) (64 64 0)\" \"material\" \"TOOLS/TOOLSNODRAW\" }\n"
            "    side { \"id\" \"3\" \"plane\" \"(64 64 16) (64 64 0) (64 0 0)\" \"material\" \"TOOLS/TOOLSNODRAW\" }\n"
            "    side { \"id\" \"4\" \"plane\" \"(0 0 16) (0 0 0) (0 64 0)\" \"material\" \"TOOLS/TOOLSNODRAW\" }\n"
            "    side { \"id\" \"5\" \"plane\" \"(64 64 0) (64 64 16) (0 64 16)\" \"material\" \"TOOLS/TOOLSNODRAW\" }\n"
            "    side { \"id\" \"6\" \"plane\" \"(0 0 0) (0 0 16) (64 0 16)\" \"material\" \"TOOLS/TOOLSNODRAW\" }\n"
            "  }\n"
            "  solid\n"
            "  {\n"
            "    \"id\" \"3\"\n"
            "    side\n"
            "    {\n"
            "      \"id\" \"10\"\n"
            "      \"plane\" \"(100 64 0) (164 64 0) (164 0 0)\"\n"
            "      \"material\" \"NATURE/DIRTFLOOR001A\"\n"
            "      dispinfo\n"
            "      {\n"
            "        \"power\" \"2\"\n"
            "        \"startposition\" \"[100 0 0]\"\n"
            "        \"elevation\" \"0\"\n"
            "        normals\n"
            "        {\n"
            "          \"row0\" \"0 0 1 0 0 1 0 0 1 0 0 1 0 0 1\"\n"
            "          \"row1\" \"0 0 1 0 0 1 0 0 1 0 0 1 0 0 1\"\n"
            "          \"row2\" \"0 0 1 0 0 1 0 0 1 0 0 1 0 0 1\"\n"
            "          \"row3\" \"0 0 1 0 0 1 0 0 1 0 0 1 0 0 1\"\n"
            "          \"row4\" \"0 0 1 0 0 1 0 0 1 0 0 1 0 0 1\"\n"
            "        }\n"
            "        distances\n"
            "        {\n"
            "          \"row0\" \"0 0 0 0 0\"\n"
            "          \"row1\" \"0 12 18 12 0\"\n"
            "          \"row2\" \"0 18 40 18 0\"\n"
            "          \"row3\" \"0 12 18 12 0\"\n"
            "          \"row4\" \"0 0 0 0 0\"\n"
            "        }\n"
            "        offsets { \"row0\" \"0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\" }\n"
            "      }\n"
            "    }\n"
            "    side { \"id\" \"11\" \"plane\" \"(100 0 -16) (164 0 -16) (164 64 -16)\" \"material\" \"TOOLS/TOOLSNODRAW\" }\n"
            "    side { \"id\" \"12\" \"plane\" \"(164 64 0) (164 64 -16) (164 0 -16)\" \"material\" \"TOOLS/TOOLSNODRAW\" }\n"
            "    side { \"id\" \"13\" \"plane\" \"(100 0 0) (100 0 -16) (100 64 -16)\" \"material\" \"TOOLS/TOOLSNODRAW\" }\n"
            "    side { \"id\" \"14\" \"plane\" \"(164 64 -16) (164 64 0) (100 64 0)\" \"material\" \"TOOLS/TOOLSNODRAW\" }\n"
            "    side { \"id\" \"15\" \"plane\" \"(100 0 -16) (100 0 0) (164 0 0)\" \"material\" \"TOOLS/TOOLSNODRAW\" }\n"
            "  }\n"
            "}\n";

        LoadedMesh3D mVmf;
        bool okVmf = Model3DViewer::InspectMeshFile(writeText(L"outpost_disp.vmf", hammerVmf), mVmf);
        tc.Record(okVmf && mVmf.loadedOk && mVmf.rawVertexCount == 29 && mVmf.rawTriangleCount == 34 &&
                  std::fabs(mVmf.bboxSize.x - 164.0f) < 0.05f &&
                  std::fabs(mVmf.bboxMax.y - 40.0f) < 0.05f,
                  L"Stage15.3D.CsgMapVmf.ValveVmfNodrawClipAndDisplacement",
                  L"verts=" + std::to_wstring(mVmf.rawVertexCount) +
                  L" tris=" + std::to_wstring(mVmf.rawTriangleCount) +
                  L" maxY=" + std::to_wstring(mVmf.bboxMax.y));
    }

    // ========================================================================
    // 29. C3D Biomechanical 3D Motion Capture (.c3d): POINT:LABELS, POINT:SCALE, Multi-Frame & Occluded Residual
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".c3d"),
                  L"Stage15.3D.C3D.ExtensionRegistered");

        // Build genuine 3-sector C3D file (Intel LE procType=84, float32 scale=-0.05, 9 markers, 4 frames)
        std::vector<uint8_t> c3d(512 * 3, 0);
        c3d[0] = 2u;    // paramBlock = sector 2
        c3d[1] = 0x50u; // C3D magic byte
        uint16_t nPts = 9u, firstFr = 1u, lastFr = 4u, dataSec = 3u;
        float hdrScale = -0.05f, hdrRate = 120.0f;
        std::memcpy(c3d.data() + 2,  &nPts, 2);
        std::memcpy(c3d.data() + 6,  &firstFr, 2);
        std::memcpy(c3d.data() + 8,  &lastFr, 2);
        std::memcpy(c3d.data() + 12, &hdrScale, 4);
        std::memcpy(c3d.data() + 16, &dataSec, 2);
        std::memcpy(c3d.data() + 20, &hdrRate, 4);

        // Sector 2 (offset 512): Parameter Section
        ByteWriter prm;
        prm.U8(1u); prm.U8(0x50u); prm.U8(1u); prm.U8(84u); // 1 param block, procType=84 (Intel LE)
        // Group -1 = "POINT"
        prm.U8(5u); prm.U8(static_cast<uint8_t>(-1));
        prm.Bytes("POINT", 5);
        prm.U16LE(3u); // nextOffset = 2 + 1 (descLen=0)
        prm.U8(0u);
        // Parameter "LABELS" in group 1: 9 labels x 8 chars ("Actor:LFHD", etc., testing prefix stripping!)
        const char* labels9[9] = {
            "S1:LFHD ", "S1:RFHD ", "S1:C7   ", "S1:CLAV ",
            "S1:LSHO ", "S1:LELB ", "S1:RSHO ", "S1:RELB ", "S1:OCCL "
        };
        prm.U8(6u); prm.U8(1u);
        prm.Bytes("LABELS", 6);
        uint16_t lblNext = static_cast<uint16_t>(2u + 1u + 1u + 2u + 9u * 8u + 1u);
        prm.U16LE(lblNext);
        prm.U8(static_cast<uint8_t>(-1)); // char type
        prm.U8(2u);                       // 2D char array [8, 9]
        prm.U8(8u); prm.U8(9u);
        for (const char* lb : labels9) prm.Bytes(lb, 8);
        prm.U8(0u); // descLen
        // End-of-parameters sentinel
        prm.U8(0u); prm.U8(0u);
        std::memcpy(c3d.data() + 512, prm.buf.data(), prm.buf.size());

        // Sector 3 (offset 1024): 4 frames of 9 float32 16-byte markers (marker 8 is occluded with residual = -1.0f at 9999!)
        struct C3dPt4 { float x, y, z, res; };
        const C3dPt4 frame0[9] = {
            { -60.0f,  80.0f, 1700.0f,  0.5f }, // LFHD
            {  60.0f,  80.0f, 1700.0f,  0.5f }, // RFHD
            {   0.0f, -60.0f, 1500.0f,  0.5f }, // C7
            {   0.0f,  60.0f, 1450.0f,  0.5f }, // CLAV
            {-180.0f,   0.0f, 1460.0f,  0.5f }, // LSHO
            {-320.0f,  20.0f, 1180.0f,  0.5f }, // LELB
            { 180.0f,   0.0f, 1460.0f,  0.5f }, // RSHO
            { 320.0f,  20.0f, 1180.0f,  0.5f }, // RELB
            {9999.0f,9999.0f, 9999.0f, -1.0f }  // OCCL (occluded -> MUST be skipped!)
        };
        ByteWriter traj;
        for (int fr = 0; fr < 4; ++fr) {
            float dz = static_cast<float>(fr) * 15.0f;
            for (int m = 0; m < 9; ++m) {
                traj.F32LE(frame0[m].x);
                traj.F32LE(frame0[m].y);
                traj.F32LE(frame0[m].z + (frame0[m].res >= 0.0f ? dz : 0.0f));
                traj.F32LE(frame0[m].res);
            }
        }
        c3d.resize(1024);
        c3d.insert(c3d.end(), traj.buf.begin(), traj.buf.end());

        LoadedMesh3D mC3d;
        bool okC3d = Model3DViewer::InspectMeshFile(writeBytes(L"vicon_upperbody.c3d", c3d), mC3d);
        // 8 valid markers * 8 octahedron tris (64) + 10 anatomical bone struts * 4 tris (40) = 104 tris; maxY ~ 1712 (< 2000, proving 9999 outlier skipped!)
        tc.Record(okC3d && mC3d.loadedOk && mC3d.animFrameCount == 4 &&
                  mC3d.rawTriangleCount == 104 &&
                  mC3d.bboxSize.x > 600.0f && mC3d.bboxSize.x < 800.0f &&
                  mC3d.bboxMax.y > 1700.0f && mC3d.bboxMax.y < 1800.0f,
                  L"Stage15.3D.C3D.BinaryMocapLabelsFramesAndOccludedFilter",
                  L"frames=" + std::to_wstring(mC3d.animFrameCount) +
                  L" tris=" + std::to_wstring(mC3d.rawTriangleCount) +
                  L" maxY=" + std::to_wstring(mC3d.bboxMax.y));

        std::vector<uint8_t> badC3d(1024, 0);
        badC3d[0] = 2u; badC3d[1] = 0x00u; // missing 0x50 C3D key
        LoadedMesh3D mBadC3d;
        bool okBadC3d = Model3DViewer::InspectMeshFile(writeBytes(L"bad_magic.c3d", badC3d), mBadC3d);
        tc.Record(!okBadC3d && !mBadC3d.loadedOk, L"Stage15.3D.C3D.RejectBadMagic");
    }

    // ========================================================================
    // 30. NRRD 3D Volumetric Dataset (.nrrd): Raw & Gzip Marching Cubes Sphere vs Torus Watertight Isosurfaces
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".nrrd"),
                  L"Stage15.3D.NRRD.ExtensionRegistered");

        // Helper: count open boundary edges & compute Euler characteristic V - E + F to prove watertight manifold!
        auto checkWatertightEuler = [](const LoadedMesh3D& m, int& outBoundaryEdges, int& outEuler) {
            std::vector<uint64_t> edges;
            edges.reserve(m.tris.size() * 3u);
            for (const auto& t : m.tris) {
                uint32_t v[3] = { t.i0, t.i1, t.i2 };
                for (int k = 0; k < 3; ++k) {
                    uint32_t a = std::min(v[k], v[(k + 1) % 3]);
                    uint32_t b = std::max(v[k], v[(k + 1) % 3]);
                    edges.push_back((static_cast<uint64_t>(a) << 32u) | static_cast<uint64_t>(b));
                }
            }
            std::sort(edges.begin(), edges.end());
            outBoundaryEdges = 0;
            int uniqueEdges = 0;
            size_t i = 0;
            while (i < edges.size()) {
                size_t j = i + 1u;
                while (j < edges.size() && edges[j] == edges[i]) ++j;
                if ((j - i) != 2u) ++outBoundaryEdges;
                ++uniqueEdges;
                i = j;
            }
            outEuler = static_cast<int>(m.vertices.size()) - uniqueEdges + static_cast<int>(m.tris.size());
        };

        // 1. Raw float32 20x20x20 Sphere with anisotropic spacings (1.0, 1.0, 2.0) -> Z span is 2x X/Y span, Euler V-E+F == 2!
        const int sN = 20;
        const char* nrrdSphereHdr =
            "NRRD0004\n"
            "# Smooth 3D Sphere Volume\n"
            "type: float\n"
            "dimension: 3\n"
            "sizes: 20 20 20\n"
            "spacings: 1.0 1.0 2.0\n"
            "endian: little\n"
            "encoding: raw\n\n";
        ByteWriter rawNrrd;
        rawNrrd.Bytes(nrrdSphereHdr, std::strlen(nrrdSphereHdr));
        for (int z = 0; z < sN; ++z) {
            for (int y = 0; y < sN; ++y) {
                for (int x = 0; x < sN; ++x) {
                    float dx = static_cast<float>(x) - 9.5f;
                    float dy = static_cast<float>(y) - 9.5f;
                    float dz = static_cast<float>(z) - 9.5f;
                    float r = std::sqrt(dx * dx + dy * dy + dz * dz);
                    float density = std::clamp(6.2f - r, 0.0f, 6.2f);
                    rawNrrd.F32LE(density);
                }
            }
        }

        // 2. Gzip uint8 26x26x18 3D Torus -> genus-1 watertight surface (Euler V-E+F == 0!)
        const int tXY = 26, tZ = 18;
        std::vector<uint8_t> torusVox(static_cast<size_t>(tXY * tXY * tZ), 0u);
        for (int z = 0; z < tZ; ++z) {
            for (int y = 0; y < tXY; ++y) {
                for (int x = 0; x < tXY; ++x) {
                    float dx = static_cast<float>(x) - 12.5f;
                    float dy = static_cast<float>(y) - 12.5f;
                    float dz = static_cast<float>(z) - 8.5f;
                    float q = std::sqrt(dx * dx + dy * dy) - 7.0f;
                    float dTube = std::sqrt(q * q + dz * dz);
                    float val = std::clamp((3.2f - dTube) / 3.2f, 0.0f, 1.0f);
                    torusVox[static_cast<size_t>((z * tXY + y) * tXY + x)] = static_cast<uint8_t>(std::lround(val * 250.0f));
                }
            }
        }
        auto torusGz = TfGzipCompress(torusVox.data(), torusVox.size());
        const char* nrrdTorusHdr =
            "NRRD0005\n"
            "type: uint8\n"
            "dimension: 3\n"
            "sizes: 26 26 18\n"
            "spacings: 1.0 1.0 1.0\n"
            "encoding: gzip\n\n";
        ByteWriter gzNrrd;
        gzNrrd.Bytes(nrrdTorusHdr, std::strlen(nrrdTorusHdr));
        gzNrrd.Bytes(torusGz.data(), torusGz.size());

        LoadedMesh3D mSphere, mTorus;
        bool okSph = Model3DViewer::InspectMeshFile(writeBytes(L"sphere_raw.nrrd", rawNrrd.buf), mSphere);
        bool okTor = Model3DViewer::InspectMeshFile(writeBytes(L"torus_gzip.nrrd", gzNrrd.buf), mTorus);

        int bndSph = -1, eulerSph = -999, bndTor = -1, eulerTor = -999;
        if (okSph) checkWatertightEuler(mSphere, bndSph, eulerSph);
        if (okTor) checkWatertightEuler(mTorus,  bndTor, eulerTor);

        tc.Record(okSph && okTor &&
                  mSphere.rawTriangleCount > 300 && mTorus.rawTriangleCount > 400 &&
                  bndSph == 0 && eulerSph == 2 &&
                  bndTor == 0 && eulerTor == 0 &&
                  std::fabs((mSphere.bboxSize.z / std::max(mSphere.bboxSize.x, 1e-3f)) - 2.0f) < 0.08f &&
                  HashMeshGeometry(mSphere) != HashMeshGeometry(mTorus),
                  L"Stage15.3D.NRRD.MarchingCubesRawSphereAndGzipTorusWatertight",
                  L"sphTris=" + std::to_wstring(mSphere.rawTriangleCount) +
                  L" sphEuler=" + std::to_wstring(eulerSph) +
                  L" torTris=" + std::to_wstring(mTorus.rawTriangleCount) +
                  L" torEuler=" + std::to_wstring(eulerTor));

        // Reject constant all-zero NRRD & truncated raw NRRD
        std::string zeroNrrd = "NRRD0004\ntype: uint8\ndimension: 3\nsizes: 4 4 4\nencoding: raw\n\n";
        zeroNrrd.append(64, '\0');
        LoadedMesh3D mZeroNrrd, mTruncNrrd;
        bool okZero  = Model3DViewer::InspectMeshFile(writeBytes(L"constant_zero.nrrd",
                           std::vector<uint8_t>(zeroNrrd.begin(), zeroNrrd.end())), mZeroNrrd);
        bool okTrunc = Model3DViewer::InspectMeshFile(writeText(L"truncated.nrrd",
                           "NRRD0004\ntype: float\ndimension: 3\nsizes: 16 16 16\nencoding: raw\n\nabcd"), mTruncNrrd);
        tc.Record(!okZero && !okTrunc && mZeroNrrd.tris.empty() && mTruncNrrd.tris.empty(),
                  L"Stage15.3D.NRRD.RejectConstantAndTruncated");
    }

    for (const auto& f : createdFiles) {
        DeleteFileW(f.c_str());
    }
    RemoveDirectoryW(workDir.c_str());
}

} // namespace Explorer2::SelfTestSuite
