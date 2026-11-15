// FormatTests3D.cpp: self-tests of the 3D model readers (Model3DViewer, formats/), stage 15 ("15.3d" runs these alone)
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

// Castagnoli CRC32C (polynomial 0x82F63B78) for ASTM E2807 .e57 1024-byte physical pages.
static uint32_t TfCalcCrc32C(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0x82F63B78u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
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
        uint32_t crc = TfCalcCrc32C(phys.data() + p * kPageSize, kUsable);
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
    std::vector<uint8_t> buf(820, 0);
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

} // namespace

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
    }

    for (const auto& f : createdFiles) {
        DeleteFileW(f.c_str());
    }
    RemoveDirectoryW(workDir.c_str());
}

} // namespace Explorer2::SelfTestSuite
