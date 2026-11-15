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

// Builds a binary ACIS (.sab) file with "ACIS BinaryFile" header and 0x13 TAG_POSITION records.
static std::vector<uint8_t> BuildSyntheticSab(const std::vector<Vec3f>& positions) {
    ByteWriter w;
    w.Bytes("ACIS BinaryFile", 15);
    w.I32LE(700); // version
    w.I32LE(static_cast<int32_t>(positions.size()));
    w.I32LE(static_cast<int32_t>(positions.size()));
    w.I32LE(0);   // flags
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
        w.U8(0x13); // TAG_POSITION (3 doubles)
        w.F64LE(p.x); w.F64LE(p.y); w.F64LE(p.z);
        w.U8(0x11); // '#' terminator
    }
    return w.buf;
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
        auto sabA = BuildSyntheticSab({ {0,0,0}, {10,0,0}, {5,8,0} });
        auto sabB = BuildSyntheticSab({ {0,0,0}, {2,0,0}, {2,2,0}, {0,2,0}, {1,1,9} });
        LoadedMesh3D mSabA, mSabB;
        bool okSabA = Model3DViewer::InspectMeshFile(writeBytes(L"tri_wire.sab", sabA), mSabA);
        bool okSabB = Model3DViewer::InspectMeshFile(writeBytes(L"tall_wire.sab", sabB), mSabB);
        tc.Record(okSabA && okSabB && HashMeshGeometry(mSabA) != HashMeshGeometry(mSabB),
                  L"Stage15.3D.SAB.BinaryAcisDistinct");

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
    }

    for (const auto& f : createdFiles) {
        DeleteFileW(f.c_str());
    }
    RemoveDirectoryW(workDir.c_str());
}

} // namespace Explorer2::SelfTestSuite
