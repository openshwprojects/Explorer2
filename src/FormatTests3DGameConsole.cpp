// FormatTests3DGameConsole.cpp — Stage 15 self-tests for Game Engine, Console, Simulation & Voxel 3D readers.
#include "SelfTestCommon.h"
#include "Model3DViewer.h"
#include "PathUtils.h"
#include "TestBytes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

static inline void PushU16LE(std::vector<uint8_t>& b, uint16_t v) { TestBytes::AppendU16LE(b, v); }
static inline void PushI16LE(std::vector<uint8_t>& b, int16_t v)  { TestBytes::AppendU16LE(b, static_cast<uint16_t>(v)); }
static inline void PushU32LE(std::vector<uint8_t>& b, uint32_t v) { TestBytes::AppendU32LE(b, v); }
static inline void PushI32LE(std::vector<uint8_t>& b, int32_t v)  { TestBytes::AppendI32LE(b, v); }
static inline void PushF32LE(std::vector<uint8_t>& b, float v)    { TestBytes::AppendF32LE(b, v); }
static inline void PushU16BE(std::vector<uint8_t>& b, uint16_t v) { TestBytes::AppendU16BE(b, v); }
static inline void PushU32BE(std::vector<uint8_t>& b, uint32_t v) { TestBytes::AppendU32BE(b, v); }
static inline void PushF32BE(std::vector<uint8_t>& b, float v)    { TestBytes::AppendF32BE(b, v); }

static inline void PushFixedAscii(std::vector<uint8_t>& b, const char* s, size_t fixedLen) {
    size_t slen = s ? std::strlen(s) : 0u;
    size_t copyLen = std::min(slen, fixedLen);
    if (copyLen > 0u) {
        b.insert(b.end(), reinterpret_cast<const uint8_t*>(s), reinterpret_cast<const uint8_t*>(s) + copyLen);
    }
    if (fixedLen > copyLen) {
        b.insert(b.end(), fixedLen - copyLen, 0u);
    }
}

// Wraps a small (<64 KiB) raw buffer in an RFC 1950 zlib stream using an uncompressed stored deflate block.
static std::vector<uint8_t> WrapStoredZlibStream(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out;
    out.push_back(0x78u);
    out.push_back(0x01u); // CMF=0x78, FLG=0x01 (30721 % 31 == 0)
    out.push_back(0x01u); // BFINAL=1, BTYPE=00 (stored block)
    const uint16_t len = static_cast<uint16_t>(raw.size());
    const uint16_t nlen = static_cast<uint16_t>(~len);
    PushU16LE(out, len);
    PushU16LE(out, nlen);
    out.insert(out.end(), raw.begin(), raw.end());
    uint32_t s1 = 1u, s2 = 0u;
    for (uint8_t c : raw) {
        s1 = (s1 + c) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    PushU32BE(out, (s2 << 16u) | s1);
    return out;
}

static inline void PushNbtStr(std::vector<uint8_t>& b, const char* s) {
    const uint16_t len = static_cast<uint16_t>(s ? std::strlen(s) : 0u);
    PushU16BE(b, len);
    if (len > 0u) {
        b.insert(b.end(), reinterpret_cast<const uint8_t*>(s), reinterpret_cast<const uint8_t*>(s) + len);
    }
}

static inline void PushNbtTagShort(std::vector<uint8_t>& b, const char* name, int16_t val) {
    b.push_back(2u); // TAG_Short
    PushNbtStr(b, name);
    PushU16BE(b, static_cast<uint16_t>(val));
}

static inline void PushNbtTagInt(std::vector<uint8_t>& b, const char* name, int32_t val) {
    b.push_back(3u); // TAG_Int
    PushNbtStr(b, name);
    PushU32BE(b, static_cast<uint32_t>(val));
}

static inline void PushNbtTagByteArray(std::vector<uint8_t>& b, const char* name, const std::vector<uint8_t>& arr) {
    b.push_back(7u); // TAG_Byte_Array
    PushNbtStr(b, name);
    PushU32BE(b, static_cast<uint32_t>(arr.size()));
    b.insert(b.end(), arr.begin(), arr.end());
}

static void AssertMeshRejected(TestCounter& tc, const wchar_t* testName, const std::wstring& badPath) {
    LoadedMesh3D badMesh;
    bool ok = Model3DViewer::InspectMeshFile(badPath, badMesh);
    tc.Record(!ok && !badMesh.loadedOk && badMesh.tris.empty(), testName,
              ok ? L"unexpectedly loaded corrupt input" : L"rejected cleanly");
}

} // namespace

void RunFormatTests3DGameConsole(TestCounter& tc) {
    const std::wstring workDir = ScratchTestDir() + L"\\fmt3d_game_console";
    CreateDirectoryW(workDir.c_str(), nullptr);
    const std::wstring td3dDir = TestDataDir() + L"\\3d";

    std::vector<std::wstring> createdFiles;
    auto writeBytes = [&](const wchar_t* relName, const std::vector<uint8_t>& bytes) -> std::wstring {
        std::wstring p = workDir + L"\\" + relName;
        PathUtils::WriteFileBytes(p, bytes.data(), bytes.size());
        createdFiles.push_back(p);
        return p;
    };
    auto writeText = [&](const wchar_t* relName, const char* s) -> std::wstring {
        std::wstring p = workDir + L"\\" + relName;
        PathUtils::WriteFileBytes(p, s, std::strlen(s));
        createdFiles.push_back(p);
        return p;
    };
    auto assertMeshRejected = [&](const wchar_t* testName, const std::wstring& path) {
        AssertMeshRejected(tc, testName, path);
    };

    // ========================================================================
    // Ritual Entertainment UberTools Skeletal Base Mesh (.skb, "SKL " v3/v4)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".skb"),
                  L"Stage15.3D.UberToolsSKB_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\fakk2_broadsword.skb", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 14 && m1.rawTriangleCount == 18 &&
                  m1.bboxSize.y > 30.0f && m1.bboxSize.x > 6.0f,
                  L"Stage15.3D.UberToolsSKB_MultiSurfaceV3SampleParsed");

        // Second distinct variant: SKB v4 (140-byte header, 80-byte embedded-basepose bones, 100-byte surface header)
        std::vector<uint8_t> skb4;
        skb4.insert(skb4.end(), { 'S', 'K', 'L', ' ' });
        PushI32LE(skb4, 4); // version 4 (Elite Force II)
        PushFixedAscii(skb4, "models/ef2/tricorder.skb", 64);
        const int32_t ofsBones4 = 140;
        const int32_t ofsSurf4  = 140 + 2 * 80; // 300
        const int32_t surfLen4  = 100 + 48 + 4 * 44;
        const int32_t ofsEnd4   = ofsSurf4 + surfLen4;
        PushI32LE(skb4, 1);         // numSurfaces
        PushI32LE(skb4, 2);         // numBones
        PushI32LE(skb4, ofsBones4); // ofsBones
        PushI32LE(skb4, ofsSurf4);  // ofsSurfaces
        PushI32LE(skb4, ofsEnd4);   // ofsEnd
        for (int i = 0; i < 10; ++i) PushI32LE(skb4, 0); // lodIndex[10]
        PushI32LE(skb4, 0); PushI32LE(skb4, 0);          // numBoxes, ofsBoxes

        auto pushBoneV4 = [&](int32_t parent, const char* bname, float tz) {
            PushI32LE(skb4, parent);
            PushI32LE(skb4, 0);
            PushFixedAscii(skb4, bname, 32);
            PushF32LE(skb4, 0.0f); PushF32LE(skb4, 0.0f); PushF32LE(skb4, 0.0f); PushF32LE(skb4, 1.0f);
            PushF32LE(skb4, 0.0f); PushF32LE(skb4, 0.0f); PushF32LE(skb4, tz);
            PushF32LE(skb4, 0.0f); PushF32LE(skb4, 0.0f); PushF32LE(skb4, 0.0f);
        };
        pushBoneV4(-1, "root_grip", 0.0f);
        pushBoneV4(0,  "lid_hinge", 10.0f);

        skb4.insert(skb4.end(), { 'S', 'K', 'L', ' ' });
        PushFixedAscii(skb4, "tricorder_casing", 64);
        PushI32LE(skb4, 4);        // numTriangles
        PushI32LE(skb4, 4);        // numVerts
        PushI32LE(skb4, 0);        // staticSurfProcessed
        PushI32LE(skb4, 100);      // ofsTriangles
        PushI32LE(skb4, 148);      // ofsVerts
        PushI32LE(skb4, 0);        // ofsCollapse
        PushI32LE(skb4, surfLen4); // ofsEnd
        PushI32LE(skb4, 0);        // ofsCollapseIndex (v4)

        const int32_t tetTris[4][3] = { {0, 1, 2}, {0, 1, 3}, {1, 2, 3}, {2, 0, 3} };
        for (const auto& t : tetTris) {
            PushI32LE(skb4, t[0]); PushI32LE(skb4, t[1]); PushI32LE(skb4, t[2]);
        }
        const float tetPts[4][4] = {
            { 0.0f, -4.0f, -3.0f, 0.0f },
            { 0.0f,  4.0f, -3.0f, 0.0f },
            { 0.0f,  0.0f,  4.0f, 0.0f },
            { 1.0f,  0.0f,  0.0f, 5.0f }, // bone 1 (world z = 10 + 5 = 15)
        };
        for (int v = 0; v < 4; ++v) {
            PushF32LE(skb4, 0.0f); PushF32LE(skb4, 0.0f); PushF32LE(skb4, 1.0f);
            PushF32LE(skb4, 0.5f); PushF32LE(skb4, 0.5f);
            PushI32LE(skb4, 1);
            PushI32LE(skb4, static_cast<int32_t>(tetPts[v][0]));
            PushF32LE(skb4, 1.0f);
            PushF32LE(skb4, tetPts[v][1]); PushF32LE(skb4, tetPts[v][2]); PushF32LE(skb4, tetPts[v][3]);
        }

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"tricorder_v4.skb", skb4), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 4 && m2.rawTriangleCount == 4 &&
                  std::fabs(m2.bboxSize.y - 15.0f) < 0.05f &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.UberToolsSKB_V4EmbeddedBaseposeAndDistinctHash");

        std::vector<uint8_t> badMagic = skb4;
        badMagic[0] = 'X';
        assertMeshRejected(L"Stage15.3D.UberToolsSKB_RejectBadMagic", writeBytes(L"bad_magic.skb", badMagic));

        std::vector<uint8_t> cycBone = skb4;
        cycBone[ofsBones4] = 1; cycBone[ofsBones4 + 1] = 0; cycBone[ofsBones4 + 2] = 0; cycBone[ofsBones4 + 3] = 0;
        assertMeshRejected(L"Stage15.3D.UberToolsSKB_RejectCyclicBoneHierarchy", writeBytes(L"cycle.skb", cycBone));

        std::vector<uint8_t> truncSkb(skb4.begin(), skb4.begin() + 200);
        assertMeshRejected(L"Stage15.3D.UberToolsSKB_RejectTruncatedSurface", writeBytes(L"trunc.skb", truncSkb));
    }

    // ========================================================================
    // Ritual Entertainment UberTools Skeletal Animation (.ska, "SKAN" v3/v4)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".ska"),
                  L"Stage15.3D.UberToolsSKA_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\fakk2_hero_idle.ska", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawTriangleCount == 68 && m1.animFrameCount == 2 &&
                  m1.bboxSize.y > 58.0f && m1.bboxSize.x > 20.0f,
                  L"Stage15.3D.UberToolsSKA_QuantizedV3SkeletonParsed");

        std::vector<uint8_t> ska4;
        ska4.insert(ska4.end(), { 'S', 'K', 'A', 'N' });
        PushI32LE(ska4, 4);
        PushFixedAscii(ska4, "models/ef2/turret_sweep.ska", 64);
        PushI32LE(ska4, 0);
        PushI32LE(ska4, 1);
        PushI32LE(ska4, 3);
        PushF32LE(ska4, 0.2f); PushF32LE(ska4, 0.2f);
        PushF32LE(ska4, 0.0f); PushF32LE(ska4, 0.0f); PushF32LE(ska4, 0.0f);
        const int32_t ofsBonesSka4  = 116;
        const int32_t ofsFramesSka4 = 116 + 3 * 40;
        const int32_t ofsEndSka4    = ofsFramesSka4 + 40 + 3 * 28;
        PushI32LE(ska4, ofsBonesSka4);
        PushI32LE(ska4, ofsFramesSka4);
        PushI32LE(ska4, ofsEndSka4);
        for (int b = 0; b < 3; ++b) {
            PushI32LE(ska4, b - 1);
            PushI32LE(ska4, 0);
            PushFixedAscii(ska4, b == 0 ? "base" : (b == 1 ? "yaw" : "barrel"), 32);
        }
        for (int k = 0; k < 10; ++k) PushF32LE(ska4, 0.0f);
        const float bOff[3][3] = { {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 8.0f}, {18.0f, 0.0f, 2.0f} };
        for (int b = 0; b < 3; ++b) {
            PushF32LE(ska4, 0.0f); PushF32LE(ska4, 0.0f); PushF32LE(ska4, 0.0f); PushF32LE(ska4, 1.0f);
            PushF32LE(ska4, bOff[b][0]); PushF32LE(ska4, bOff[b][1]); PushF32LE(ska4, bOff[b][2]);
        }

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"turret_v4.ska", ska4), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount == (3 * 8 + 2 * 4) &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.UberToolsSKA_FloatV4PoseAndDistinctHash");

        std::vector<uint8_t> badSka = ska4;
        badSka[3] = 'X';
        assertMeshRejected(L"Stage15.3D.UberToolsSKA_RejectBadMagic", writeBytes(L"bad_magic.ska", badSka));

        std::vector<uint8_t> truncSka(ska4.begin(), ska4.begin() + ofsFramesSka4 + 24);
        assertMeshRejected(L"Stage15.3D.UberToolsSKA_RejectTruncatedFrame", writeBytes(L"trunc.ska", truncSka));

        std::vector<uint8_t> selfParSka = ska4;
        selfParSka[ofsBonesSka4] = 0; selfParSka[ofsBonesSka4 + 1] = 0; selfParSka[ofsBonesSka4 + 2] = 0; selfParSka[ofsBonesSka4 + 3] = 0;
        assertMeshRejected(L"Stage15.3D.UberToolsSKA_RejectSelfParentCycle", writeBytes(L"self_par.ska", selfParSka));
    }

    // ========================================================================
    // Medal of Honor: Allied Assault Skeletal Mesh (.skd, "SKMD" v5/v6)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".skd"),
                  L"Stage15.3D.MohaaSKD_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\mohaa_garand_rifle.skd", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 13 && m1.rawTriangleCount == 18 &&
                  std::fabs(m1.bboxSize.x - 48.5f) < 0.1f,
                  L"Stage15.3D.MohaaSKD_V5NamedParentHierarchyAndMorphsParsed");

        std::vector<uint8_t> skd6;
        skd6.insert(skd6.end(), { 'S', 'K', 'M', 'D' });
        PushI32LE(skd6, 6);
        PushFixedAscii(skd6, "models/ammo/crate_v6.skd", 64);
        const int32_t ofsBones6 = 152;
        const int32_t boneRecLen6 = 96;
        const int32_t ofsSurf6  = ofsBones6 + boneRecLen6;
        const int32_t surfLen6  = 100 + 2 * 12 + 4 * (28 + 20);
        const int32_t ofsEnd6   = ofsSurf6 + surfLen6;
        PushI32LE(skd6, 1); PushI32LE(skd6, 1);
        PushI32LE(skd6, ofsBones6); PushI32LE(skd6, ofsSurf6); PushI32LE(skd6, ofsEnd6);
        for (int i = 0; i < 14; ++i) PushI32LE(skd6, 0);
        PushF32LE(skd6, 2.0f); // v6 header scale = 2.0f

        PushFixedAscii(skd6, "origin", 32);
        PushFixedAscii(skd6, "worldbone", 32);
        PushI32LE(skd6, 1); PushI32LE(skd6, 84); PushI32LE(skd6, 96); PushI32LE(skd6, 96); PushI32LE(skd6, 96);
        PushF32LE(skd6, 0.0f); PushF32LE(skd6, 0.0f); PushF32LE(skd6, 0.0f);

        skd6.insert(skd6.end(), { 'S', 'K', 'M', 'D' });
        PushFixedAscii(skd6, "crate_lid", 64);
        PushI32LE(skd6, 2); PushI32LE(skd6, 4); PushI32LE(skd6, 0);
        PushI32LE(skd6, 100); PushI32LE(skd6, 124); PushI32LE(skd6, 0); PushI32LE(skd6, surfLen6); PushI32LE(skd6, 0);
        PushI32LE(skd6, 0); PushI32LE(skd6, 1); PushI32LE(skd6, 2);
        PushI32LE(skd6, 0); PushI32LE(skd6, 2); PushI32LE(skd6, 3);
        const float qv[4][3] = { {-5, -3, 0}, {5, -3, 0}, {5, 3, 0}, {-5, 3, 0} };
        for (int v = 0; v < 4; ++v) {
            PushF32LE(skd6, 0.0f); PushF32LE(skd6, 0.0f); PushF32LE(skd6, 1.0f);
            PushF32LE(skd6, 0.0f); PushF32LE(skd6, 0.0f);
            PushI32LE(skd6, 1); PushI32LE(skd6, 0);
            PushI32LE(skd6, 0); PushF32LE(skd6, 1.0f);
            PushF32LE(skd6, qv[v][0]); PushF32LE(skd6, qv[v][1]); PushF32LE(skd6, qv[v][2]);
        }

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"crate_v6.skd", skd6), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 4 && m2.rawTriangleCount == 2 &&
                  std::fabs(m2.bboxSize.x - 20.0f) < 0.05f &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.MohaaSKD_V6ScaleFactorAndDistinctHash");

        std::vector<uint8_t> badPar = skd6;
        std::memcpy(badPar.data() + ofsBones6 + 32, "missing_parent\0", 15);
        assertMeshRejected(L"Stage15.3D.MohaaSKD_RejectMissingParentBone", writeBytes(L"bad_parent.skd", badPar));

        std::vector<uint8_t> badVer = skd6;
        badVer[4] = 99;
        assertMeshRejected(L"Stage15.3D.MohaaSKD_RejectUnsupportedVersion", writeBytes(L"bad_ver.skd", badVer));
    }

    // ========================================================================
    // Medal of Honor: Allied Assault Skeletal Animation (.skc, "SKAN" v13/v14)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".skc"),
                  L"Stage15.3D.MohaaSKC_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\mohaa_rifle_aim.skc", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawTriangleCount == 56 && m1.animFrameCount == 2 &&
                  m1.bboxSize.y > 22.0f && m1.bboxSize.x > 19.0f,
                  L"Stage15.3D.MohaaSKC_V13PosRotChannelsParsed");

        std::vector<uint8_t> skc14;
        skc14.insert(skc14.end(), { 'S', 'K', 'A', 'N' });
        PushI32LE(skc14, 14);
        PushI32LE(skc14, 0);
        PushI32LE(skc14, 256);
        PushF32LE(skc14, 0.05f);
        PushF32LE(skc14, 0.0f); PushF32LE(skc14, 0.0f); PushF32LE(skc14, 0.0f); PushF32LE(skc14, 0.0f);
        const int32_t ofsDir14 = 48 + 48;
        const int32_t ofsC0 = ofsDir14 + 8;
        const int32_t ofsC1 = ofsC0 + 20;
        const int32_t ofsNames14 = ofsC1 + 20;
        PushI32LE(skc14, 2);
        PushI32LE(skc14, ofsNames14);
        PushI32LE(skc14, 1);
        for (int k = 0; k < 11; ++k) PushF32LE(skc14, 0.0f);
        PushI32LE(skc14, 0); // triggers v14 stream directory
        PushI32LE(skc14, ofsC0);
        PushI32LE(skc14, ofsC1);
        PushI16LE(skc14, 1); PushI16LE(skc14, 0);
        PushF32LE(skc14, 0.0f); PushF32LE(skc14, 0.0f); PushF32LE(skc14, 0.0f); PushF32LE(skc14, 0.0f);
        PushI16LE(skc14, 1); PushI16LE(skc14, 0);
        PushF32LE(skc14, 0.0f); PushF32LE(skc14, 0.0f); PushF32LE(skc14, 28.0f); PushF32LE(skc14, 0.0f);
        PushFixedAscii(skc14, "turret pos", 32);
        PushFixedAscii(skc14, "turret_cannon pos", 32);

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"turret_v14.skc", skc14), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount == (2 * 8 + 1 * 4) &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.MohaaSKC_V14StreamDirAndDistinctHash");

        std::vector<uint8_t> wrongVer = skc14;
        wrongVer[4] = 3;
        assertMeshRejected(L"Stage15.3D.MohaaSKC_RejectV3Version", writeBytes(L"v3.skc", wrongVer));

        std::vector<uint8_t> badNameOfs = skc14;
        badNameOfs[40] = 0xFF; badNameOfs[41] = 0xFF;
        assertMeshRejected(L"Stage15.3D.MohaaSKC_RejectOutOfBoundsNames", writeBytes(L"bad_names.skc", badNameOfs));
    }

    // ========================================================================
    // Raven Software Ghoul 2 Skeletal Animation (.gla, "2LGA" v6)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".gla"),
                  L"Stage15.3D.RavenGLA_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\jedi_humanoid.gla", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawTriangleCount == 56 && m1.animFrameCount == 2 &&
                  m1.bboxSize.y > 59.0f && m1.bboxSize.x > 25.0f,
                  L"Stage15.3D.RavenGLA_CompBonePool24BitIndexParsed");

        std::vector<uint8_t> gla2;
        gla2.insert(gla2.end(), { '2', 'L', 'G', 'A' });
        PushI32LE(gla2, 6);
        PushFixedAscii(gla2, "models/players/gonk/gonk.gla", 64);
        PushF32LE(gla2, 2.0f);
        const int32_t ofsSkel2   = 100;
        const int32_t skelBytes2 = 8 + 172 * 2;
        const int32_t ofsFrames2 = ofsSkel2 + skelBytes2;
        const int32_t ofsPool2   = ofsFrames2 + 6;
        const int32_t ofsEnd2    = ofsPool2 + 28;
        PushI32LE(gla2, 1); PushI32LE(gla2, ofsFrames2);
        PushI32LE(gla2, 2); PushI32LE(gla2, ofsPool2);
        PushI32LE(gla2, ofsSkel2); PushI32LE(gla2, ofsEnd2);
        PushI32LE(gla2, 8); PushI32LE(gla2, 8 + 172);
        for (int b = 0; b < 2; ++b) {
            PushFixedAscii(gla2, b == 0 ? "chassis" : "antenna", 64);
            PushU32LE(gla2, 0);
            PushI32LE(gla2, b == 0 ? -1 : 0);
            for (int m = 0; m < 24; ++m) PushF32LE(gla2, (m % 5 == 0) ? 1.0f : 0.0f);
            PushI32LE(gla2, 0);
        }
        gla2.insert(gla2.end(), { 0, 0, 0,  1, 0, 0 });
        PushU16LE(gla2, 49149); PushU16LE(gla2, 32766); PushU16LE(gla2, 32766); PushU16LE(gla2, 32766);
        PushU16LE(gla2, 32768); PushU16LE(gla2, 32768); PushU16LE(gla2, 32768);
        PushU16LE(gla2, 49149); PushU16LE(gla2, 32766); PushU16LE(gla2, 32766); PushU16LE(gla2, 32766);
        PushU16LE(gla2, 32768); PushU16LE(gla2, 32768); PushU16LE(gla2, 33408);

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"gonk.gla", gla2), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount == (2 * 8 + 1 * 4) &&
                  m2.bboxSize.y > 20.0f &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.RavenGLA_ScaledPoolAndDistinctHash");

        std::vector<uint8_t> badPoolIdx = gla2;
        badPoolIdx[ofsFrames2 + 3] = 99;
        assertMeshRejected(L"Stage15.3D.RavenGLA_RejectOutOfRangePoolIndex", writeBytes(L"oob_pool.gla", badPoolIdx));

        std::vector<uint8_t> badMagicGla = gla2;
        badMagicGla[0] = 'X';
        assertMeshRejected(L"Stage15.3D.RavenGLA_RejectBadMagic", writeBytes(L"bad_magic.gla", badMagicGla));
    }

    // ========================================================================
    // Return to Castle Wolfenstein / WolfET Compressed Model (.mdc, "IDPC" v2)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".mdc"),
                  L"Stage15.3D.RtcwMDC_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\rtcw_helmet.mdc", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 17 && m1.rawTriangleCount == 24 &&
                  std::fabs(m1.bboxSize.y - 14.0f) < 0.05f,
                  L"Stage15.3D.RtcwMDC_BaseAndCompressedDeltaParsed");

        std::vector<uint8_t> mdc2;
        mdc2.insert(mdc2.end(), { 'I', 'D', 'P', 'C' });
        PushI32LE(mdc2, 2);
        PushFixedAscii(mdc2, "models/multiplayer/ammopack/ammopack.mdc", 64);
        const int32_t ofsFr2 = 112;
        const int32_t ofsSf2 = 112 + 56;
        const int32_t sfLen2 = 124 + 24 + 32 + 2;
        PushI32LE(mdc2, 0); PushI32LE(mdc2, 1); PushI32LE(mdc2, 0); PushI32LE(mdc2, 1); PushI32LE(mdc2, 0);
        PushI32LE(mdc2, ofsFr2); PushI32LE(mdc2, ofsFr2); PushI32LE(mdc2, ofsFr2);
        PushI32LE(mdc2, ofsSf2); PushI32LE(mdc2, ofsSf2 + sfLen2);
        mdc2.insert(mdc2.end(), 56u, 0u);
        PushI32LE(mdc2, 7);
        PushFixedAscii(mdc2, "ammo_lid", 64);
        PushI32LE(mdc2, 0); PushI32LE(mdc2, 0); PushI32LE(mdc2, 1); PushI32LE(mdc2, 0);
        PushI32LE(mdc2, 4); PushI32LE(mdc2, 2);
        PushI32LE(mdc2, 124); PushI32LE(mdc2, 124); PushI32LE(mdc2, 124);
        PushI32LE(mdc2, 148); PushI32LE(mdc2, 0); PushI32LE(mdc2, 180); PushI32LE(mdc2, 0);
        PushI32LE(mdc2, sfLen2);
        PushI32LE(mdc2, 0); PushI32LE(mdc2, 1); PushI32LE(mdc2, 2);
        PushI32LE(mdc2, 0); PushI32LE(mdc2, 2); PushI32LE(mdc2, 3);
        const int16_t pts64[4][3] = { {-256, -192, 0}, {256, -192, 0}, {256, 192, 0}, {-256, 192, 0} };
        for (int v = 0; v < 4; ++v) {
            PushI16LE(mdc2, pts64[v][0]); PushI16LE(mdc2, pts64[v][1]); PushI16LE(mdc2, pts64[v][2]);
            PushU16LE(mdc2, 0);
        }
        PushI16LE(mdc2, 0);

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"ammo.mdc", mdc2), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 4 && m2.rawTriangleCount == 2 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.RtcwMDC_PureBaseFrameAndDistinctHash");

        std::vector<uint8_t> badBase = mdc2;
        badBase[ofsSf2 + 180] = 5;
        assertMeshRejected(L"Stage15.3D.RtcwMDC_RejectOutOfRangeBaseFrame", writeBytes(L"bad_base.mdc", badBase));

        std::vector<uint8_t> truncMdc(mdc2.begin(), mdc2.begin() + 140);
        assertMeshRejected(L"Stage15.3D.RtcwMDC_RejectTruncated", writeBytes(L"trunc.mdc", truncMdc));
    }

    // ========================================================================
    // Raven Software Heretic II Flexible Model (.fm)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".fm"),
                  L"Stage15.3D.Heretic2FM_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\heretic2_mana_crystal.fm", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 6 && m1.rawTriangleCount == 8 &&
                  std::fabs(m1.bboxSize.y - 32.0f) < 0.25f && std::fabs(m1.bboxSize.x - 10.0f) < 0.25f,
                  L"Stage15.3D.Heretic2FM_ByteFramesAndSectionsParsed");

        auto pushFmSec = [&](std::vector<uint8_t>& dst, const char* nm, int32_t ver, const std::vector<uint8_t>& pay) {
            PushFixedAscii(dst, nm, 32);
            PushI32LE(dst, ver);
            PushI32LE(dst, static_cast<int32_t>(pay.size()));
            dst.insert(dst.end(), pay.begin(), pay.end());
        };
        std::vector<uint8_t> fm2, hp2, tp2, fp2;
        for (int32_t v : { 64, 64, 72, 0, 4, 0, 4, 0, 1, 0 }) PushI32LE(hp2, v);
        const int16_t triData[] = {
            0,1,2, 0,0,0,  0,1,3, 0,0,0,
            1,2,3, 0,0,0,  2,0,3, 0,0,0
        };
        for (int16_t idx : triData) {
            PushI16LE(tp2, idx);
        }
        PushF32LE(fp2, 0.01f); PushF32LE(fp2, 0.01f); PushF32LE(fp2, 0.01f);
        PushF32LE(fp2, 0.0f);  PushF32LE(fp2, 0.0f);  PushF32LE(fp2, 0.0f);
        PushFixedAscii(fp2, "short0", 16);
        const uint16_t sp4[4][3] = { {0, 0, 0}, {1000, 0, 0}, {500, 900, 0}, {500, 450, 1200} };
        for (int v = 0; v < 4; ++v) {
            PushU16LE(fp2, sp4[v][0]); PushU16LE(fp2, sp4[v][1]); PushU16LE(fp2, sp4[v][2]); PushU16LE(fp2, 0);
        }
        pushFmSec(fm2, "header", 2, hp2);
        pushFmSec(fm2, "tris", 1, tp2);
        pushFmSec(fm2, "short frames", 1, fp2);

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"short_tet.fm", fm2), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 4 && m2.rawTriangleCount == 4 &&
                  std::fabs(m2.bboxSize.y - 12.0f) < 0.05f &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.Heretic2FM_ShortFrames16BitAndDistinctHash");

        std::vector<uint8_t> badFm = fm2;
        badFm[0] = 'X';
        assertMeshRejected(L"Stage15.3D.Heretic2FM_RejectNonHeaderStart", writeBytes(L"bad_hdr.fm", badFm));

        std::vector<uint8_t> oobTriFm = fm2;
        oobTriFm[40 + 40 + 40] = 99;
        assertMeshRejected(L"Stage15.3D.Heretic2FM_RejectOutOfRangeTriIndex", writeBytes(L"oob_tri.fm", oobTriFm));
    }

    // ========================================================================
    // Valve Source Engine VPhysics Collision Mesh (.phy, "VPHY" + "IVPS")
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".phy"),
                  L"Stage15.3D.ValvePHY_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\hl2_supply_crate.phy", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 12 && m1.rawTriangleCount == 16 &&
                  std::fabs(m1.bboxSize.y - (0.8f * 39.3700787f)) < 0.2f,
                  L"Stage15.3D.ValvePHY_MultiSolidIvpsConvexParsed");

        std::vector<uint8_t> phy2;
        PushI32LE(phy2, 16); PushI32LE(phy2, 0); PushI32LE(phy2, 1); PushI32LE(phy2, 1234);
        PushI32LE(phy2, 208);
        phy2.insert(phy2.end(), 44u, 0u);
        phy2.insert(phy2.end(), { 'I', 'V', 'P', 'S' });
        PushI32LE(phy2, 16 + 5 * 16);
        PushI32LE(phy2, 0);
        PushU32LE(phy2, 3u | (6u << 8));
        PushI16LE(phy2, 5); PushI16LE(phy2, 0);
        const uint32_t phyTris[5][4] = {
            { 0u, 0u, 1u, 2u },
            { 1u, 0u, 1u, 3u },
            { 2u, 1u, 2u, 3u },
            { 3u, 2u, 0u, 3u },
            { 0x80000004u, 0u, 1u, 2u }, // virtual triangle -> must be skipped
        };
        for (const auto& t : phyTris) {
            PushU32LE(phy2, t[0]); PushU32LE(phy2, t[1]); PushU32LE(phy2, t[2]); PushU32LE(phy2, t[3]);
        }
        const float phyPts[4][3] = { {0,0,0}, {1.0f,0,0}, {0,-1.0f,0}, {0,0,1.0f} };
        for (const auto& p : phyPts) {
            PushF32LE(phy2, p[0]); PushF32LE(phy2, p[1]); PushF32LE(phy2, p[2]); PushF32LE(phy2, 0.0f);
        }

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"legacy_tet.phy", phy2), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 4 && m2.rawTriangleCount == 4 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.ValvePHY_LegacyHeaderVirtualTriFilteredAndDistinctHash");

        std::vector<uint8_t> noIvps = phy2;
        noIvps[20 + 44] = 'X';
        assertMeshRejected(L"Stage15.3D.ValvePHY_RejectMissingIvps", writeBytes(L"no_ivps.phy", noIvps));

        std::vector<uint8_t> oobEdge = phy2;
        oobEdge[20 + 48 + 16 + 4] = 99;
        assertMeshRejected(L"Stage15.3D.ValvePHY_RejectOutOfRangeEdgeVertex", writeBytes(L"oob_edge.phy", oobEdge));
    }

    // ========================================================================
    // Valve Source Engine Optimized Strip/Index Mesh (.vtx / .dx90.vtx)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".vtx"),
                  L"Stage15.3D.ValveVTX_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\hl2_turret_pod.dx90.vtx", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 8 && m1.rawTriangleCount == 12 &&
                  std::fabs(m1.bboxSize.y - 24.0f) < 0.05f,
                  L"Stage15.3D.ValveVTX_Dx90StripGroupPairedWithVvd");

        std::vector<uint8_t> vvdB;
        vvdB.insert(vvdB.end(), { 'I', 'D', 'S', 'V' });
        PushI32LE(vvdB, 4); PushI32LE(vvdB, 0x11223344); PushI32LE(vvdB, 1);
        PushI32LE(vvdB, 4);
        for (int i = 1; i < 8; ++i) PushI32LE(vvdB, 0);
        PushI32LE(vvdB, 0); PushI32LE(vvdB, 0); PushI32LE(vvdB, 64); PushI32LE(vvdB, 0);
        const float qPts[4][3] = { {-6, 0, 0}, {6, 0, 0}, {6, 0, 9}, {-6, 0, 9} };
        for (int i = 0; i < 4; ++i) {
            PushF32LE(vvdB, 1.0f); PushF32LE(vvdB, 0.0f); PushF32LE(vvdB, 0.0f);
            vvdB.insert(vvdB.end(), { 0, 0, 0, 1 });
            PushF32LE(vvdB, qPts[i][0]); PushF32LE(vvdB, qPts[i][1]); PushF32LE(vvdB, qPts[i][2]);
            PushF32LE(vvdB, 0.0f); PushF32LE(vvdB, -1.0f); PushF32LE(vvdB, 0.0f);
            PushF32LE(vvdB, 0.0f); PushF32LE(vvdB, 0.0f);
        }
        writeBytes(L"panel_ep2.vvd", vvdB);

        std::vector<uint8_t> vtxB;
        PushI32LE(vtxB, 7); PushI32LE(vtxB, 16); PushU16LE(vtxB, 3); PushU16LE(vtxB, 3);
        PushI32LE(vtxB, 3); PushI32LE(vtxB, 0x11223344); PushI32LE(vtxB, 1); PushI32LE(vtxB, 0);
        PushI32LE(vtxB, 1); PushI32LE(vtxB, 36);
        PushI32LE(vtxB, 1); PushI32LE(vtxB, 8);
        PushI32LE(vtxB, 1); PushI32LE(vtxB, 8);
        PushI32LE(vtxB, 1); PushI32LE(vtxB, 12); PushF32LE(vtxB, 0.0f);
        PushI32LE(vtxB, 1); PushI32LE(vtxB, 9); vtxB.push_back(0);
        PushI32LE(vtxB, 4); PushI32LE(vtxB, 33); PushI32LE(vtxB, 6); PushI32LE(vtxB, 69);
        PushI32LE(vtxB, 0); PushI32LE(vtxB, 0); vtxB.push_back(0);
        PushI32LE(vtxB, 0); PushI32LE(vtxB, 0);
        for (uint16_t vi = 0; vi < 4; ++vi) {
            vtxB.insert(vtxB.end(), { 0, 0, 0, 1 }); PushU16LE(vtxB, vi); vtxB.insert(vtxB.end(), { 0, 0, 0 });
        }
        for (uint16_t idx : { uint16_t(0), uint16_t(1), uint16_t(2), uint16_t(0), uint16_t(2), uint16_t(3) }) {
            PushU16LE(vtxB, idx);
        }
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"panel_ep2.sw.vtx", vtxB), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 4 && m2.rawTriangleCount == 2 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.ValveVTX_Ep2Stride33SwVtxAndDistinctHash");

        std::vector<uint8_t> badCrc = vtxB;
        badCrc[16] ^= 0xFF;
        assertMeshRejected(L"Stage15.3D.ValveVTX_RejectChecksumMismatch", writeBytes(L"panel_ep2.dx80.vtx", badCrc));
        assertMeshRejected(L"Stage15.3D.ValveVTX_RejectMissingCompanionVvd", writeBytes(L"orphan_only.dx90.vtx", vtxB));
    }

    // ========================================================================
    // Universal Model 3D (.m3d, "3DMO" uncompressed & zlib-compressed)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".m3d"),
                  L"Stage15.3D.Model3DM3D_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\raylib_starfighter.m3d", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 6 && m1.rawTriangleCount == 8 &&
                  std::fabs(m1.bboxSize.x - 18.0f) < 0.05f && std::fabs(m1.bboxSize.z - 26.0f) < 0.05f,
                  L"Stage15.3D.Model3DM3D_ZlibCompressedStarfighterParsed");

        std::vector<uint8_t> m3dRaw;
        m3dRaw.insert(m3dRaw.end(), { '3', 'D', 'M', 'O', 0, 0, 0, 0 });
        m3dRaw.insert(m3dRaw.end(), { 'H', 'E', 'A', 'D' });
        PushU32LE(m3dRaw, 16u);
        PushF32LE(m3dRaw, 10.0f);
        PushU32LE(m3dRaw, 1u | (1u << 2));
        m3dRaw.insert(m3dRaw.end(), { 'V', 'R', 'T', 'S' });
        PushU32LE(m3dRaw, 40u);
        const int16_t qv16[4][4] = { {-32767, 0, 0, 32767}, {32767, 0, 0, 32767}, {32767, 32767, 0, 32767}, {-32767, 32767, 0, 32767} };
        for (const auto& q : qv16) {
            PushI16LE(m3dRaw, q[0]); PushI16LE(m3dRaw, q[1]); PushI16LE(m3dRaw, q[2]); PushI16LE(m3dRaw, q[3]);
        }
        m3dRaw.insert(m3dRaw.end(), { 'M', 'E', 'S', 'H' });
        PushU32LE(m3dRaw, 17u);
        m3dRaw.push_back(0x40u);
        PushU16LE(m3dRaw, 0u); PushU16LE(m3dRaw, 1u); PushU16LE(m3dRaw, 2u); PushU16LE(m3dRaw, 3u);
        m3dRaw.insert(m3dRaw.end(), { 'O', 'M', 'D', '3' });
        PushU32LE(m3dRaw, 8u);
        const uint32_t totSz = static_cast<uint32_t>(m3dRaw.size());
        std::memcpy(m3dRaw.data() + 4u, &totSz, 4u);

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"quad_i16.m3d", m3dRaw), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 4 && m2.rawTriangleCount == 2 &&
                  std::fabs(m2.bboxSize.x - 20.0f) < 0.05f &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.Model3DM3D_UncompressedInt16QuadAndDistinctHash");

        std::vector<uint8_t> oobM3d = m3dRaw;
        oobM3d[8 + 16 + 40 + 8 + 1] = 99;
        assertMeshRejected(L"Stage15.3D.Model3DM3D_RejectOutOfRangeIndex", writeBytes(L"oob.m3d", oobM3d));

        std::vector<uint8_t> badZlib = { '3','D','M','O', 24,0,0,0, 0x78,0x9C, 0xFF,0xFF,0xFF,0xFF,0,0,0,0,0,0,0,0,0,0 };
        assertMeshRejected(L"Stage15.3D.Model3DM3D_RejectCorruptZlib", writeBytes(L"bad_z.m3d", badZlib));
    }

    // ========================================================================
    // Impulse Imagine / TurboSilver Amiga IFF 3D Object (.tddd / .iob)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".tddd") &&
                  Model3DViewer::Is3DModelExtension(L".iob"),
                  L"Stage15.3D.ImagineTDDD_ExtensionsRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\amiga_pyramid.tddd", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 5 && m1.rawTriangleCount == 6 &&
                  std::fabs(m1.bboxSize.x - 12.0f) < 0.05f && std::fabs(m1.bboxSize.y - 14.0f) < 0.05f,
                  L"Stage15.3D.ImagineTDDD_FixedPointEdgeFacesParsed");

        auto pushBeChunk = [&](std::vector<uint8_t>& dst, const char tag[4], const std::vector<uint8_t>& pay) {
            dst.insert(dst.end(), { uint8_t(tag[0]), uint8_t(tag[1]), uint8_t(tag[2]), uint8_t(tag[3]) });
            PushU32BE(dst, static_cast<uint32_t>(pay.size()));
            dst.insert(dst.end(), pay.begin(), pay.end());
            if (pay.size() & 1u) dst.push_back(0u);
        };
        std::vector<uint8_t> pnts2, edge2, face2, axis2, descPay, objPay, iobBlob;
        PushU16BE(pnts2, 3u);
        for (const auto& pt : std::array<std::array<int32_t, 3>, 3>{{ {0,0,0}, {5*65536,0,0}, {0,0,4*65536} }}) {
            PushU32BE(pnts2, uint32_t(pt[0])); PushU32BE(pnts2, uint32_t(pt[1])); PushU32BE(pnts2, uint32_t(pt[2]));
        }
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) PushU32BE(axis2, (r == c) ? uint32_t(2 * 65536) : 0u);
        PushU16BE(edge2, 3u);
        PushU16BE(edge2, 0u); PushU16BE(edge2, 1u);
        PushU16BE(edge2, 1u); PushU16BE(edge2, 2u);
        PushU16BE(edge2, 2u); PushU16BE(edge2, 0u);
        PushU16BE(face2, 1u);
        PushU16BE(face2, 0u); PushU16BE(face2, 1u); PushU16BE(face2, 2u);
        pushBeChunk(descPay, "AXIS", axis2);
        pushBeChunk(descPay, "PNTS", pnts2);
        pushBeChunk(descPay, "EDGE", edge2);
        pushBeChunk(descPay, "FACE", face2);
        pushBeChunk(objPay, "DESC", descPay);
        std::vector<uint8_t> formBody = { 'T', 'D', 'D', 'D' };
        pushBeChunk(formBody, "OBJ ", objPay);
        iobBlob.insert(iobBlob.end(), { 'F', 'O', 'R', 'M' });
        PushU32BE(iobBlob, static_cast<uint32_t>(formBody.size()));
        iobBlob.insert(iobBlob.end(), formBody.begin(), formBody.end());

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"scaled.iob", iobBlob), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 3 && m2.rawTriangleCount == 1 &&
                  std::fabs(m2.bboxSize.x - 10.0f) < 0.05f &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.ImagineTDDD_IobAxisTransformAndDistinctHash");

        std::vector<uint8_t> ilbmForm = iobBlob;
        std::memcpy(ilbmForm.data() + 8, "ILBM", 4);
        assertMeshRejected(L"Stage15.3D.ImagineTDDD_RejectNonTdddForm", writeBytes(L"ilbm.tddd", ilbmForm));

        std::vector<uint8_t> badFaceEdge = iobBlob;
        badFaceEdge[badFaceEdge.size() - 2] = 99;
        assertMeshRejected(L"Stage15.3D.ImagineTDDD_RejectOutOfRangeFaceEdge", writeBytes(L"bad_edge.tddd", badFaceEdge));
    }

    // ========================================================================
    // Celestia 3D Astronomy Mesh (.cmod, "#celmodel__ascii" & "#celmodel_binary")
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".cmod"),
                  L"Stage15.3D.CelestiaCMOD_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\celestia_asteroid.cmod", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 10 && m1.rawTriangleCount == 16 &&
                  std::fabs(m1.bboxSize.x - 24.0f) < 0.05f && std::fabs(m1.bboxSize.y - 18.5f) < 0.05f,
                  L"Stage15.3D.CelestiaCMOD_BinaryAsteroidParsed");

        const char* asciiCmod =
            "#celmodel__ascii\n"
            "# Solar wing panel\n"
            "material\n"
            "  diffuse 0.1 0.2 0.8\n"
            "end_material\n"
            "mesh\n"
            "  vertexdesc\n"
            "    position f3\n"
            "    texcoord0 f2\n"
            "  end_vertexdesc\n"
            "  vertices 4\n"
            "    -8.0 0.0 -3.0  0.0 0.0\n"
            "     8.0 0.0 -3.0  1.0 0.0\n"
            "    -8.0 0.0  3.0  0.0 1.0\n"
            "     8.0 0.0  3.0  1.0 1.0\n"
            "  tristrip 0 4\n"
            "    0 1 2 3\n"
            "end_mesh\n";
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeText(L"solar_panel.cmod", asciiCmod), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 4 && m2.rawTriangleCount == 2 &&
                  std::fabs(m2.bboxSize.x - 16.0f) < 0.05f &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.CelestiaCMOD_AsciiTristripAndDistinctHash");

        assertMeshRejected(L"Stage15.3D.CelestiaCMOD_RejectBadHeader",
                           writeText(L"bad.cmod", "#not_celestia_fmt\nmesh\nend_mesh\n"));
        assertMeshRejected(L"Stage15.3D.CelestiaCMOD_RejectOutOfRangeIndex",
                           writeText(L"oob.cmod", "#celmodel__ascii\nmesh\nvertexdesc\nposition f3\nend_vertexdesc\n"
                                                  "vertices 3\n0 0 0  1 0 0  0 1 0\ntrilist 0 3\n0 1 99\nend_mesh\n"));
    }

    // ========================================================================
    // Softimage dotXSI 3.0 / 3.6 / 5.0 Modding Scene & Mesh (.xsi)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".xsi"),
                  L"Stage15.3D.SoftimageXSI_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\battlefront_hovertank.xsi", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 12 && m1.rawTriangleCount == 16 &&
                  std::fabs(m1.bboxSize.y - 6.5f) < 0.05f && std::fabs(m1.bboxSize.z - 18.0f) < 0.05f,
                  L"Stage15.3D.SoftimageXSI_HierarchicalModelPolysAndTrisParsed");

        const char* xsi50 =
            "xsi 0500txt 0032\n"
            "SI_Model root {\n"
            "  XSI_Mesh wing {\n"
            "    XSI_Shape shp {\n"
            "      XSI_SubComponentAttributeList \"pos\" \"POSITION\" \"FLOAT\" 3 {\n"
            "        3,\n"
            "        0.0, 0.0, 0.0,\n"
            "        14.0, 0.0, 0.0,\n"
            "        0.0, 9.0, 0.0,\n"
            "      }\n"
            "    }\n"
            "    XSI_TriangleList tl {\n"
            "      1, \"POSITION\", \"mat0\", 0, 1, 2,\n"
            "    }\n"
            "  }\n"
            "}\n";
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeText(L"v5_wing.xsi", xsi50), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 3 && m2.rawTriangleCount == 1 &&
                  std::fabs(m2.bboxSize.x - 14.0f) < 0.05f &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.SoftimageXSI_V50MeshAndDistinctHash");

        assertMeshRejected(L"Stage15.3D.SoftimageXSI_RejectDirectXXofBanner",
                           writeText(L"xof.xsi", "xof 0303txt 0032\nMesh { 3; 0;0;0;, 1;0;0;, 0;1;0;; 1; 3;0,1,2;; }\n"));
        assertMeshRejected(L"Stage15.3D.SoftimageXSI_RejectUnclosedBrace",
                           writeText(L"unclosed.xsi", "xsi 0360txt 0032\nSI_Model m {\n  SI_Mesh a {\n"));
    }

    // ========================================================================
    // Outrage Descent 3 Object Format (.oof, "PSPO" v1800..v2300)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".oof"),
                  L"Stage15.3D.Descent3OOF_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\descent3_pyro_gl.oof", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 9 && m1.rawTriangleCount == 10 &&
                  std::fabs(m1.bboxSize.x - 15.0f) < 0.05f,
                  L"Stage15.3D.Descent3OOF_V2300SubmodelHierarchyParsed");

        std::vector<uint8_t> oof2;
        oof2.insert(oof2.end(), { 'P', 'S', 'P', 'O' });
        PushI32LE(oof2, 1800);
        oof2.insert(oof2.end(), { 'O', 'H', 'D', 'R' });
        PushI32LE(oof2, 28);
        PushI32LE(oof2, 1);
        for (int i = 0; i < 6; ++i) PushF32LE(oof2, 0.0f);
        std::vector<uint8_t> sbPay;
        PushI32LE(sbPay, 0); PushI32LE(sbPay, -1);
        for (int i = 0; i < 13; ++i) PushF32LE(sbPay, 0.0f);
        sbPay.insert(sbPay.end(), { 'm', 'i', 'n', 'e', 0, 0 });
        PushI32LE(sbPay, 0); PushI32LE(sbPay, 0); PushI32LE(sbPay, 0);
        PushI32LE(sbPay, 3);
        for (const auto& p : std::array<std::array<float, 3>, 3>{{ {0,0,0}, {9,0,0}, {0,7,0} }}) {
            PushF32LE(sbPay, p[0]); PushF32LE(sbPay, p[1]); PushF32LE(sbPay, p[2]);
        }
        for (int i = 0; i < 9; ++i) PushF32LE(sbPay, 0.0f);
        PushI32LE(sbPay, 1);
        PushF32LE(sbPay, 0.0f); PushF32LE(sbPay, 0.0f); PushF32LE(sbPay, 1.0f);
        PushI32LE(sbPay, 3); PushI32LE(sbPay, 0);
        for (int k = 0; k < 3; ++k) { PushI32LE(sbPay, k); PushF32LE(sbPay, 0.0f); PushF32LE(sbPay, 0.0f); }
        oof2.insert(oof2.end(), { 'S', 'O', 'B', 'J' });
        PushI32LE(oof2, static_cast<int32_t>(sbPay.size()));
        oof2.insert(oof2.end(), sbPay.begin(), sbPay.end());

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"mine_v1800.oof", oof2), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 3 && m2.rawTriangleCount == 1 &&
                  std::fabs(m2.bboxSize.x - 9.0f) < 0.05f &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.Descent3OOF_V1800LegacyAndDistinctHash");

        std::vector<uint8_t> pofMagic = oof2;
        std::memcpy(pofMagic.data(), "PSPO", 4); pofMagic[3] = 'F';
        assertMeshRejected(L"Stage15.3D.Descent3OOF_RejectBadMagic", writeBytes(L"bad.oof", pofMagic));

        std::vector<uint8_t> oobOof = oof2;
        oobOof[oobOof.size() - 12] = 99;
        assertMeshRejected(L"Stage15.3D.Descent3OOF_RejectOutOfRangeFaceVert", writeBytes(L"oob.oof", oobOof));
    }

    // ========================================================================
    // Bohemia Interactive OFP / ArmA / DayZ "MLOD" P3D Model (.p3d)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".p3d"),
                  L"Stage15.3D.BohemiaP3D_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\arma_watchtower.p3d", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 9 && m1.rawTriangleCount == 12 &&
                  std::fabs(m1.bboxSize.y - 6.2f) < 0.05f && std::fabs(m1.bboxSize.x - 6.0f) < 0.05f,
                  L"Stage15.3D.BohemiaP3D_P3dmV28QuadsAndTrisParsed");

        std::vector<uint8_t> sp3x;
        sp3x.insert(sp3x.end(), { 'M', 'L', 'O', 'D' });
        PushU32LE(sp3x, 257u); PushU32LE(sp3x, 1u);
        sp3x.insert(sp3x.end(), { 'S', 'P', '3', 'X' });
        PushU32LE(sp3x, 28u); PushU32LE(sp3x, 4u); PushU32LE(sp3x, 0u); PushU32LE(sp3x, 1u); PushU32LE(sp3x, 0u);
        for (const auto& pt : std::array<std::array<float, 3>, 4>{{ {-4,0,0}, {4,0,0}, {4,3,0}, {-4,3,0} }}) {
            PushF32LE(sp3x, pt[0]); PushF32LE(sp3x, pt[1]); PushF32LE(sp3x, pt[2]); PushU32LE(sp3x, 0u);
        }
        PushFixedAscii(sp3x, "data\\sign.pac", 32);
        PushU32LE(sp3x, 4u);
        for (int k = 0; k < 4; ++k) {
            PushI32LE(sp3x, k); PushI32LE(sp3x, 0); PushF32LE(sp3x, 0.0f); PushF32LE(sp3x, 0.0f);
        }
        PushU32LE(sp3x, 0u);

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"ofp_sign.p3d", sp3x), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 4 && m2.rawTriangleCount == 2 &&
                  std::fabs(m2.bboxSize.x - 8.0f) < 0.05f &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.BohemiaP3D_OfpSp3xAndDistinctHash");

        std::vector<uint8_t> odol = sp3x;
        std::memcpy(odol.data(), "ODOL", 4);
        assertMeshRejected(L"Stage15.3D.BohemiaP3D_DeclineBinarizedOdol", writeBytes(L"bin.p3d", odol));

        std::vector<uint8_t> oobP3d = sp3x;
        oobP3d[12 + 24 + 64 + 36] = 99;
        assertMeshRejected(L"Stage15.3D.BohemiaP3D_RejectOutOfRangePointIndex", writeBytes(L"oob.p3d", oobP3d));
    }

    // ========================================================================
    // DarkPlaces Quake Skeletal Model (.dpm, "DARKPLACESMODEL\0" big-endian)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".dpm"),
                  L"Stage15.3D.DarkPlacesDPM_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\nexuiz_plasma.dpm", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 6 && m1.rawTriangleCount == 8 &&
                  std::fabs(m1.bboxSize.x - 32.0f) < 0.05f,
                  L"Stage15.3D.DarkPlacesDPM_BigEndianPreWeightedFkParsed");

        std::vector<uint8_t> dpm2;
        PushFixedAscii(dpm2, "DARKPLACESMODEL", 16);
        const uint32_t ofsFrDpm    = 80u;
        const uint32_t ofsMshDpm   = 80u + 68u;
        const uint32_t ofsVertsDpm = ofsMshDpm + 56u;
        const uint32_t ofsIdxDpm   = ofsVertsDpm + 3u * 36u;
        const uint32_t totSzDpm    = ofsIdxDpm + 12u;
        PushU32BE(dpm2, 2u); PushU32BE(dpm2, totSzDpm);
        for (int i = 0; i < 8; ++i) PushF32BE(dpm2, 0.0f);
        PushU32BE(dpm2, 0u); PushU32BE(dpm2, 1u); PushU32BE(dpm2, 1u);
        PushU32BE(dpm2, 80u); PushU32BE(dpm2, ofsMshDpm); PushU32BE(dpm2, ofsFrDpm);
        PushFixedAscii(dpm2, "static", 32);
        for (int i = 0; i < 8; ++i) PushF32BE(dpm2, 0.0f);
        PushU32BE(dpm2, 80u);
        PushFixedAscii(dpm2, "textures/plate", 32);
        PushU32BE(dpm2, 3u); PushU32BE(dpm2, 1u);
        PushU32BE(dpm2, ofsVertsDpm); PushU32BE(dpm2, ofsIdxDpm); PushU32BE(dpm2, ofsIdxDpm); PushU32BE(dpm2, 0u);
        for (const auto& pt : std::array<std::array<float, 3>, 3>{{ {0,0,0}, {11,0,0}, {0,0,9} }}) {
            PushU32BE(dpm2, 1u);
            PushF32BE(dpm2, pt[0]); PushF32BE(dpm2, pt[1]); PushF32BE(dpm2, pt[2]); PushF32BE(dpm2, 1.0f);
            PushF32BE(dpm2, 0.0f); PushF32BE(dpm2, -1.0f); PushF32BE(dpm2, 0.0f); PushU32BE(dpm2, 0u);
        }
        PushU32BE(dpm2, 0u); PushU32BE(dpm2, 1u); PushU32BE(dpm2, 2u);

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"plate.dpm", dpm2), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 3 && m2.rawTriangleCount == 1 &&
                  std::fabs(m2.bboxSize.x - 11.0f) < 0.05f &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.DarkPlacesDPM_RigidPropAndDistinctHash");

        std::vector<uint8_t> badDpm = dpm2;
        badDpm[0] = 'X';
        assertMeshRejected(L"Stage15.3D.DarkPlacesDPM_RejectBadMagic", writeBytes(L"bad.dpm", badDpm));

        std::vector<uint8_t> oobDpm = dpm2;
        oobDpm[oobDpm.size() - 1] = 99;
        assertMeshRejected(L"Stage15.3D.DarkPlacesDPM_RejectOutOfRangeIndex", writeBytes(L"oob.dpm", oobDpm));
    }

    // ========================================================================
    // Inter-Quake Export (.iqe)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".iqe"), L"Stage15.3D.IQE_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\arena_bot.iqe", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 5 && m1.rawTriangleCount == 6 &&
                  m1.formatName.find(L".iqe") != std::wstring::npos,
                  L"Stage15.3D.IQE_ArenaBotSampleMesh");

        std::wstring pSkel = writeText(L"pose_only.iqe",
            "# Inter-Quake Export\n"
            "joint \"pelvis\" -1\n\tpq 0 0 1 0 0 0 1\n"
            "joint \"spine\" 0\n\tpq 0 0 0.8 0 0 0 1\n"
            "joint \"head\" 1\n\tpq 0 0.2 0.6 0 0 0 1\n");
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(pSkel, m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount >= 24 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.IQE_SkeletalPoseAndDistinctHash");

        assertMeshRejected(L"Stage15.3D.IQE_RejectBadMagic",
            writeText(L"bad_magic.iqe", "mesh \"cube\"\nvp 0 0 0\nvp 1 0 0\nvp 0 1 0\nfm 0 1 2\n"));
        assertMeshRejected(L"Stage15.3D.IQE_RejectOutOfRangeFaceIndex",
            writeText(L"bad_idx.iqe", "# Inter-Quake Export\nmesh \"m\"\nvp 0 0 0\nvp 1 0 0\nvp 0 1 0\nfm 0 1 9\n"));
    }

    // ========================================================================
    // Valve Source Vertex Animation (.vta)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".vta"), L"Stage15.3D.ValveVTA_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\hl2_face_flex.vta", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawTriangleCount >= 6 &&
                  m1.formatName.find(L".vta") != std::wstring::npos,
                  L"Stage15.3D.ValveVTA_FaceFlexSample");

        std::wstring pBrowFlex = writeText(L"brow_raise.vta",
            "version 1\nnodes\n0 \"head\" -1\nend\nskeleton\ntime 0\n0 0 0 0 0 0 0\nend\n"
            "vertexanimation\ntime 0\n0 -4 0 0 0 0 1\n1 4 0 0 0 0 1\n2 0 5 2 0 0 1\n"
            "time 1\n2 0 6.5 3.5 0 0 1\nend\n");
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(pBrowFlex, m2);
        tc.Record(ok2 && m2.loadedOk && HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.ValveVTA_VariantAndDistinctHash");

        assertMeshRejected(L"Stage15.3D.ValveVTA_RejectUnclosedSection",
            writeText(L"unclosed.vta", "version 1\nnodes\n0 \"h\" -1\nend\nvertexanimation\ntime 0\n0 0 0 0 0 0 1\n1 1 0 0 0 0 1\n2 0 1 0 0 0 1\n"));
    }

    // ========================================================================
    // Nintendo JSystem J3D Model (.bmd / .bdl)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".bmd") &&
                  Model3DViewer::Is3DModelExtension(L".bdl"),
                  L"Stage15.3D.NintendoJ3D_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\windwaker_crate.bmd", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 8 && m1.rawTriangleCount == 12,
                  L"Stage15.3D.NintendoJ3D_BmdCubeSample");

        std::vector<uint8_t> bdl;
        {
            std::vector<uint8_t> vtx1(0x40, 0u);
            std::memcpy(vtx1.data(), "VTX1", 4);
            PushU32BE(vtx1, 9u); PushU32BE(vtx1, 1u); PushU32BE(vtx1, 3u);
            vtx1.push_back(4u); vtx1.push_back(0); vtx1.push_back(0); vtx1.push_back(0);
            PushU32BE(vtx1, 0xFFu); PushU32BE(vtx1, 0u); PushU32BE(vtx1, 0u); PushU32BE(vtx1, 0u);
            const uint32_t posOff = static_cast<uint32_t>(vtx1.size());
            const int16_t tet[4][3] = { {0, 0, 0}, {320, 0, 0}, {0, 480, 0}, {80, 120, 640} };
            for (const auto& p : tet) {
                PushU16BE(vtx1, static_cast<uint16_t>(p[0]));
                PushU16BE(vtx1, static_cast<uint16_t>(p[1]));
                PushU16BE(vtx1, static_cast<uint16_t>(p[2]));
            }
            while (vtx1.size() % 32u) vtx1.push_back(0u);
            uint32_t vLen = static_cast<uint32_t>(vtx1.size());
            vtx1[4] = (vLen >> 24) & 0xFF; vtx1[5] = (vLen >> 16) & 0xFF; vtx1[6] = (vLen >> 8) & 0xFF; vtx1[7] = vLen & 0xFF;
            vtx1[11] = 0x40;
            vtx1[12] = (posOff >> 24) & 0xFF; vtx1[13] = (posOff >> 16) & 0xFF; vtx1[14] = (posOff >> 8) & 0xFF; vtx1[15] = posOff & 0xFF;

            std::vector<uint8_t> shp1(0x2Cu, 0u);
            std::memcpy(shp1.data(), "SHP1", 4);
            const uint32_t shInitOff = static_cast<uint32_t>(shp1.size());
            shp1.insert(shp1.end(), 0x28u, 0u);
            shp1[shInitOff + 3u] = 1u;
            const uint32_t vdOff = static_cast<uint32_t>(shp1.size());
            PushU32BE(shp1, 9u); PushU32BE(shp1, 2u);
            PushU32BE(shp1, 0xFFu); PushU32BE(shp1, 0u);
            const uint32_t pktOff = static_cast<uint32_t>(shp1.size());
            PushU32BE(shp1, 32u); PushU32BE(shp1, 0u);
            while (shp1.size() % 32u) shp1.push_back(0u);
            const uint32_t dlBase = static_cast<uint32_t>(shp1.size());
            std::vector<uint8_t> dl = { 0x90u, 0x00u, 12u,  0,2,1,  0,1,3,  1,2,3,  2,0,3 };
            while (dl.size() < 32u) dl.push_back(0u);
            shp1.insert(shp1.end(), dl.begin(), dl.end());
            auto wr32 = [&](std::vector<uint8_t>& v, size_t o, uint32_t val) {
                v[o] = (val >> 24) & 0xFF; v[o+1] = (val >> 16) & 0xFF; v[o+2] = (val >> 8) & 0xFF; v[o+3] = val & 0xFF;
            };
            wr32(shp1, 0x04u, static_cast<uint32_t>(shp1.size()));
            shp1[0x09u] = 1u;
            wr32(shp1, 0x0Cu, shInitOff);
            wr32(shp1, 0x14u, vdOff);
            wr32(shp1, 0x20u, dlBase);
            wr32(shp1, 0x28u, pktOff);

            bdl.assign(0x20u, 0u);
            std::memcpy(bdl.data(), "J3D2bdl4", 8);
            uint32_t tot = static_cast<uint32_t>(0x20u + vtx1.size() + shp1.size());
            wr32(bdl, 8u, tot);
            wr32(bdl, 12u, 2u);
            bdl.insert(bdl.end(), vtx1.begin(), vtx1.end());
            bdl.insert(bdl.end(), shp1.begin(), shp1.end());
        }
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"tetra.bdl", bdl), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 4 && m2.rawTriangleCount == 4 &&
                  m2.formatName.find(L".bdl") != std::wstring::npos &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.NintendoJ3D_BdlFixedPointAndDistinctHash");

        bdl[8] = 0xFFu;
        assertMeshRejected(L"Stage15.3D.NintendoJ3D_RejectTruncatedFileSize", writeBytes(L"bad_sz.bdl", bdl));
    }

    // ========================================================================
    // Nintendo Wii NW4R Model (.mdl0)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".mdl0"), L"Stage15.3D.Nw4rMDL0_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\kart_itembox.mdl0", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 5 && m1.rawTriangleCount == 6 &&
                  m1.formatName.find(L".mdl0") != std::wstring::npos,
                  L"Stage15.3D.Nw4rMDL0_KartItemboxSample");

        std::vector<uint8_t> raw;
        PathUtils::ReadFileBytes(td3dDir + L"\\kart_itembox.mdl0", 1u << 20, raw);
        if (raw.size() > 0x9Cu) {
            uint32_t zTall = 0x41000000u; // 8.0f BE
            for (size_t k = 0; k + 4 <= raw.size(); ++k) {
                if (raw[k] == 0x40 && raw[k+1] == 0x19 && raw[k+2] == 0x99 && raw[k+3] == 0x9A) {
                    raw[k] = (zTall >> 24) & 0xFF; raw[k+1] = (zTall >> 16) & 0xFF;
                    raw[k+2] = (zTall >> 8) & 0xFF; raw[k+3] = zTall & 0xFF;
                    break;
                }
            }
        }
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"tall_spire.mdl0", raw), m2);
        tc.Record(ok2 && m2.loadedOk && HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.Nw4rMDL0_VariantAndDistinctHash");

        raw[8] = 0; raw[9] = 0; raw[10] = 0; raw[11] = 99;
        assertMeshRejected(L"Stage15.3D.Nw4rMDL0_RejectUnsupportedVersion", writeBytes(L"bad_ver.mdl0", raw));
    }

    // ========================================================================
    // Nintendo DS NITRO Model (.nsbmd)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".nsbmd"), L"Stage15.3D.NitroNSBMD_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\ds_trophy.nsbmd", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 12 && m1.rawTriangleCount == 4 &&
                  m1.formatName.find(L".nsbmd") != std::wstring::npos,
                  L"Stage15.3D.NitroNSBMD_TrophySample");

        std::vector<uint8_t> raw;
        PathUtils::ReadFileBytes(td3dDir + L"\\ds_trophy.nsbmd", 1u << 20, raw);
        for (size_t i = 0; i + 2 <= raw.size(); ++i) {
            if (raw[i] == 0x00u && raw[i + 1] == 0x18u) {
                raw[i + 1] = 0x30u;
            }
        }
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"tall_trophy.nsbmd", raw), m2);
        tc.Record(ok2 && m2.loadedOk && HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.NitroNSBMD_VariantAndDistinctHash");

        raw[4] = 0xFEu; raw[5] = 0xFFu;
        assertMeshRejected(L"Stage15.3D.NitroNSBMD_RejectInvalidBom", writeBytes(L"bad_bom.nsbmd", raw));
    }

    // ========================================================================
    // Warzone 2100 PIE (.pie)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".pie"), L"Stage15.3D.WarzonePIE_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\viper_droid.pie", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 6 && m1.rawTriangleCount == 8,
                  L"Stage15.3D.WarzonePIE_ViperDroidSample");

        std::wstring pPieBunker = writeText(L"bunker_pie2.pie",
            "PIE 2\nTYPE 200\nLEVELS 1\nLEVEL 1\nPOINTS 4\n0 0 0\n40 0 0\n20 0 35\n20 50 15\n"
            "POLYGONS 4\n200 3 0 2 1\n200 3 0 1 3\n200 3 1 2 3\n200 3 2 0 3\n");
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(pPieBunker, m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount == 4 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.WarzonePIE_Pie2AndDistinctHash");

        assertMeshRejected(L"Stage15.3D.WarzonePIE_RejectTruncatedPoints",
            writeText(L"trunc.pie", "PIE 3\nLEVELS 1\nLEVEL 1\nPOINTS 5\n0 0 0\n1 0 0\n"));
    }

    // ========================================================================
    // Spring RTS Unit Model (.s3o)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".s3o"), L"Stage15.3D.SpringS3O_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\arm_flash_tank.s3o", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 8 && m1.rawTriangleCount == 6 &&
                  m1.formatName.find(L".s3o") != std::wstring::npos,
                  L"Stage15.3D.SpringS3O_TankHierarchySample");

        std::vector<uint8_t> raw;
        PathUtils::ReadFileBytes(td3dDir + L"\\arm_flash_tank.s3o", 1u << 20, raw);
        if (raw.size() >= 156u) {
            float yHigh = 25.0f;
            std::memcpy(raw.data() + 104u + 0x2Cu, &yHigh, 4u);
        }
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"tall_turret.s3o", raw), m2);
        tc.Record(ok2 && m2.loadedOk && HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.SpringS3O_ChildOffsetAndDistinctHash");

        if (raw.size() >= 160u) {
            uint32_t selfOff = 52u;
            std::memcpy(raw.data() + 156u, &selfOff, 4u);
        }
        assertMeshRejected(L"Stage15.3D.SpringS3O_RejectPieceCycleBomb", writeBytes(L"cycle.s3o", raw));
    }

    // ========================================================================
    // LucasArts Sith Engine Model (.3do)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".3do"), L"Stage15.3D.LucasSith3DO_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\stormtrooper_crate.3do", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 5 && m1.rawTriangleCount == 6,
                  L"Stage15.3D.LucasSith3DO_CrateSample");

        std::wstring pSabre = writeText(L"sabre_hilt.3do",
            "SECTION: HEADER\n3DO 2.1\nSECTION: GEOMETRYDEF\nGEOSET 0\nMESHES 1\nMESH 0\n"
            "VERTICES 4\n0: 0 0 0 0\n1: 1 0 0 0\n2: 0 2 0 0\n3: 0 0 4 0\n"
            "FACES 4\n0: 0 0 4 3 1 0 3  0,0 2,0 1,0\n1: 0 0 4 3 1 0 3  0,0 1,0 3,0\n"
            "2: 0 0 4 3 1 0 3  1,0 2,0 3,0\n3: 0 0 4 3 1 0 3  2,0 0,0 3,0\n");
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(pSabre, m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount == 4 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.LucasSith3DO_VariantAndDistinctHash");

        assertMeshRejected(L"Stage15.3D.LucasSith3DO_RejectMissingHeader",
            writeText(L"bad.3do", "SECTION: GEOMETRYDEF\nMESH 0\nVERTICES 3\n0: 0 0 0 0\n"));
    }

    // ========================================================================
    // UnrealEd CSG Brush Map (.t3d)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".t3d"), L"Stage15.3D.UnrealT3D_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\ut99_room_brush.t3d", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 12 && m1.rawTriangleCount == 6 &&
                  m1.formatName.find(L".t3d") != std::wstring::npos,
                  L"Stage15.3D.UnrealT3D_RoomBrushSample");

        std::wstring pWedge = writeText(L"wedge.t3d",
            "Begin Map\nBegin PolyList\nBegin Polygon\n"
            "Vertex +0.0,+0.0,+0.0\nVertex +256.0,+0.0,+0.0\nVertex +0.0,+128.0,+64.0\n"
            "End Polygon\nEnd PolyList\nEnd Map\n");
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(pWedge, m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount == 1 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.UnrealT3D_WedgeAndDistinctHash");

        assertMeshRejected(L"Stage15.3D.UnrealT3D_RejectMismatchedBeginEnd",
            writeText(L"mismatch.t3d", "Begin Map\nBegin PolyList\nBegin Polygon\nVertex 0,0,0\nVertex 1,0,0\nVertex 0,1,0\nEnd PolyList\nEnd Map\n"));
    }

    // ========================================================================
    // Open Game Engine Exchange (.ogex)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".ogex"), L"Stage15.3D.OpenGEX_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\mech_turret.ogex", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 5 && m1.rawTriangleCount == 6 &&
                  m1.formatName.find(L".ogex") != std::wstring::npos,
                  L"Stage15.3D.OpenGEX_MechTurretSample");

        std::wstring pQuad = writeText(L"quad_panel.ogex",
            "GeometryObject $g { Mesh (primitive = \"quads\") {\n"
            "  VertexArray (attrib = \"position\") { float[3] { {0,0,0}, {4,0,0}, {4,3,0}, {0,3,1} } }\n"
            "  IndexArray { uint16[4] { {0, 1, 2, 3} } }\n"
            "} }\n");
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(pQuad, m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount == 2 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.OpenGEX_QuadsAndDistinctHash");

        assertMeshRejected(L"Stage15.3D.OpenGEX_RejectUnbalancedBraces",
            writeText(L"bad_brace.ogex", "GeometryObject $g { Mesh { VertexArray (attrib = \"position\") { float[3] { {0,0,0} } } "));
    }

    // ========================================================================
    // Blizzard StarCraft II Model (.m3)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".m3"), L"Stage15.3D.BlizzardM3_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\terran_beacon.m3", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 5 && m1.rawTriangleCount == 6 &&
                  m1.formatName.find(L".m3") != std::wstring::npos,
                  L"Stage15.3D.BlizzardM3_TerranBeaconSample");

        std::vector<uint8_t> raw;
        PathUtils::ReadFileBytes(td3dDir + L"\\terran_beacon.m3", 1u << 20, raw);
        if (raw.size() >= 284u) {
            float tallZ = 9.5f;
            std::memcpy(raw.data() + 272u + 8u, &tallZ, 4u);
        }
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"tall_pylon.m3", raw), m2);
        tc.Record(ok2 && m2.loadedOk && HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.BlizzardM3_VariantAndDistinctHash");

        raw[4] = 0xFFu; raw[5] = 0xFFu;
        assertMeshRejected(L"Stage15.3D.BlizzardM3_RejectOobRefTable", writeBytes(L"bad_ref.m3", raw));
    }

    // ========================================================================
    // RAD Game Tools Granny 3D Model (.gr2 — v6/v7 Pointer-Fixup & LZ Sections)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".gr2"), L"Stage15.3D.RadGrannyGR2_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\knight_shield.gr2", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 7 && m1.rawTriangleCount == 10 &&
                  std::fabs(m1.bboxSize.x - 2.0f) < 0.05f && std::fabs(m1.bboxSize.y - 2.9f) < 0.05f &&
                  m1.formatName == L"RAD Granny 3D Model (.gr2 v7)",
                  L"Stage15.3D.RadGrannyGR2_KnightShieldLzAndFixupsSample",
                  L"verts=" + std::to_wstring(m1.rawVertexCount) + L" tris=" + std::to_wstring(m1.rawTriangleCount));

        // Second distinct variant: single-section v6 32-bit GR2 with RootObject=Mesh, GrannyDataTypeDefinition reflection & Indices16
        std::vector<uint8_t> sec0;
        struct FixRec { uint32_t fromOff; uint32_t toSec; uint32_t toOff; };
        std::vector<FixRec> fixups;

        auto align4 = [&]() { while (sec0.size() & 3u) sec0.push_back(0u); };
        auto allocStr = [&](const char* s) -> uint32_t {
            uint32_t off = static_cast<uint32_t>(sec0.size());
            size_t len = std::strlen(s);
            sec0.insert(sec0.end(), reinterpret_cast<const uint8_t*>(s), reinterpret_cast<const uint8_t*>(s) + len + 1u);
            align4();
            return off;
        };
        auto emitMember32 = [&](int32_t mType, uint32_t nameStrOff, bool hasRef, uint32_t refOff, int32_t arrWidth) {
            uint32_t base = static_cast<uint32_t>(sec0.size());
            PushI32LE(sec0, mType);
            PushU32LE(sec0, 0u); // name ptr
            PushU32LE(sec0, 0u); // refType ptr
            PushI32LE(sec0, arrWidth);
            for (int k = 0; k < 4; ++k) PushU32LE(sec0, 0u);
            if (mType != 0) {
                fixups.push_back({ base + 4u, 0u, nameStrOff });
                if (hasRef) fixups.push_back({ base + 8u, 0u, refOff });
            }
        };

        const uint32_t sPosition   = allocStr("Position");
        const uint32_t sVertices   = allocStr("Vertices");
        const uint32_t sIndices16  = allocStr("Indices16");
        const uint32_t sPrimVtx    = allocStr("PrimaryVertexData");
        const uint32_t sPrimTopo   = allocStr("PrimaryTopology");

        const uint32_t vtxTypeOff = static_cast<uint32_t>(sec0.size());
        emitMember32(10, sPosition, false, 0u, 3); // Real32Member[3] Position
        emitMember32(0, 0u, false, 0u, 0);

        const uint32_t pvTypeOff = static_cast<uint32_t>(sec0.size());
        emitMember32(7, sVertices, false, 0u, 0); // ReferenceToVariantArrayMember Vertices
        emitMember32(0, 0u, false, 0u, 0);

        const uint32_t ptTypeOff = static_cast<uint32_t>(sec0.size());
        emitMember32(3, sIndices16, false, 0u, 0); // ReferenceToArrayMember Indices16
        emitMember32(0, 0u, false, 0u, 0);

        const uint32_t meshTypeOff = static_cast<uint32_t>(sec0.size());
        emitMember32(2, sPrimVtx,  true, pvTypeOff, 0); // ReferenceMember PrimaryVertexData
        emitMember32(2, sPrimTopo, true, ptTypeOff, 0); // ReferenceMember PrimaryTopology
        emitMember32(0, 0u, false, 0u, 0);

        const uint32_t vtxDataOff = static_cast<uint32_t>(sec0.size());
        const float gr2TetPts[4][3] = { {0,0,0}, {5,0,0}, {0,6,0}, {0,0,7} };
        for (const auto& p : gr2TetPts) {
            PushF32LE(sec0, p[0]); PushF32LE(sec0, p[1]); PushF32LE(sec0, p[2]);
        }
        const uint32_t idxDataOff = static_cast<uint32_t>(sec0.size());
        for (uint16_t idx : { uint16_t(0), uint16_t(1), uint16_t(2),
                              uint16_t(0), uint16_t(1), uint16_t(3),
                              uint16_t(1), uint16_t(2), uint16_t(3),
                              uint16_t(2), uint16_t(0), uint16_t(3) }) {
            PushU16LE(sec0, idx);
        }
        align4();

        const uint32_t pvObjOff = static_cast<uint32_t>(sec0.size());
        PushU32LE(sec0, 0u); // vTypePtr -> vtxTypeOff
        PushI32LE(sec0, 4);  // vCount = 4
        PushU32LE(sec0, 0u); // vDataPtr -> vtxDataOff
        fixups.push_back({ pvObjOff + 0u, 0u, vtxTypeOff });
        fixups.push_back({ pvObjOff + 8u, 0u, vtxDataOff });

        const uint32_t ptObjOff = static_cast<uint32_t>(sec0.size());
        PushI32LE(sec0, 12); // idxCount = 12
        PushU32LE(sec0, 0u); // idxDataPtr -> idxDataOff
        fixups.push_back({ ptObjOff + 4u, 0u, idxDataOff });

        const uint32_t meshObjOff = static_cast<uint32_t>(sec0.size());
        PushU32LE(sec0, 0u); // PrimaryVertexData -> pvObjOff
        PushU32LE(sec0, 0u); // PrimaryTopology -> ptObjOff
        fixups.push_back({ meshObjOff + 0u, 0u, pvObjOff });
        fixups.push_back({ meshObjOff + 4u, 0u, ptObjOff });

        std::vector<uint8_t> gr2V6;
        static constexpr uint8_t kMagicV6[16] = {
            0xB8, 0x67, 0xB0, 0xCA, 0xF8, 0x6D, 0xB1, 0x0F,
            0x84, 0x72, 0x8C, 0x7E, 0x5E, 0x19, 0x00, 0x1E
        };
        gr2V6.insert(gr2V6.end(), kMagicV6, kMagicV6 + 16);
        PushU32LE(gr2V6, 0x44u); // hdrTotalSize
        PushU32LE(gr2V6, 0u);    // fileFormat = 0
        PushU32LE(gr2V6, 0u); PushU32LE(gr2V6, 0u); // reserved -> 0x20

        const uint32_t secDataOff = 0x20u + 0x48u + 44u; // 0x94
        const uint32_t secDataSz  = static_cast<uint32_t>(sec0.size());
        const uint32_t fixupOff   = secDataOff + secDataSz;
        const uint32_t totalGr2Sz = fixupOff + static_cast<uint32_t>(fixups.size()) * 12u;

        // GrannyFileHeader at 0x20 (72 bytes -> ends at 0x68)
        PushU32LE(gr2V6, 6u);          // version = 6
        PushU32LE(gr2V6, totalGr2Sz);  // totalBytes
        PushU32LE(gr2V6, 0u);          // crc
        PushU32LE(gr2V6, 0x48u);       // secArrRel -> 0x68
        PushU32LE(gr2V6, 1u);          // secArrCount = 1
        PushU32LE(gr2V6, 0u);          // rootTypeSec = 0
        PushU32LE(gr2V6, meshTypeOff); // rootTypeOff
        PushU32LE(gr2V6, 0u);          // rootObjSec = 0
        PushU32LE(gr2V6, meshObjOff);  // rootObjOff
        for (int i = 0; i < 9; ++i) PushU32LE(gr2V6, 0u); // remaining 36B of GrannyFileHeader

        // Section 0 header (44 bytes at 0x68)
        PushU32LE(gr2V6, 0u);        // compression = 0
        PushU32LE(gr2V6, secDataOff);
        PushU32LE(gr2V6, secDataSz);
        PushU32LE(gr2V6, secDataSz);
        PushU32LE(gr2V6, 4u);
        PushU32LE(gr2V6, secDataSz);
        PushU32LE(gr2V6, secDataSz);
        PushU32LE(gr2V6, fixupOff);
        PushU32LE(gr2V6, static_cast<uint32_t>(fixups.size()));
        PushU32LE(gr2V6, 0u);
        PushU32LE(gr2V6, 0u);

        gr2V6.insert(gr2V6.end(), sec0.begin(), sec0.end());
        for (const auto& fx : fixups) {
            PushU32LE(gr2V6, fx.fromOff);
            PushU32LE(gr2V6, fx.toSec);
            PushU32LE(gr2V6, fx.toOff);
        }

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"single_sec_v6.gr2", gr2V6), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 4 && m2.rawTriangleCount == 4 &&
                  m2.formatName == L"RAD Granny 3D Model (.gr2 v6)" &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.RadGrannyGR2_V6Indices16ReflectionAndDistinctHash");

        // Negative checks: bad magic, Oodle1 compression=2, out-of-bounds fixup table
        std::vector<uint8_t> badMagicGr2 = gr2V6;
        badMagicGr2[0] ^= 0xFFu;
        assertMeshRejected(L"Stage15.3D.RadGrannyGR2_RejectBadMagic", writeBytes(L"bad_mag.gr2", badMagicGr2));

        std::vector<uint8_t> oodleRaw;
        PathUtils::ReadFileBytes(td3dDir + L"\\knight_shield.gr2", 1u << 20, oodleRaw);
        if (oodleRaw.size() >= 0x68u + 88u) {
            oodleRaw[0x68u + 44u] = 2u; // Oodle1 proprietary compression on Section 1
        }
        assertMeshRejected(L"Stage15.3D.RadGrannyGR2_RejectOodleCompressed", writeBytes(L"oodle.gr2", oodleRaw));

        std::vector<uint8_t> badFixGr2 = gr2V6;
        badFixGr2[0x68u + 0x1Cu] = 0xFFu; badFixGr2[0x68u + 0x1Du] = 0xFFu;
        assertMeshRejected(L"Stage15.3D.RadGrannyGR2_RejectOobFixupTable", writeBytes(L"bad_fix.gr2", badFixGr2));
    }

    // ========================================================================
    // Goxel Chunked Voxel Model (.gox)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".gox"), L"Stage15.3D.GoxelGOX_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\voxel_knight.gox", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawTriangleCount == 56 &&
                  m1.formatName.find(L".gox") != std::wstring::npos,
                  L"Stage15.3D.GoxelGOX_FaceCulledSample");

        std::vector<uint8_t> raw;
        PathUtils::ReadFileBytes(td3dDir + L"\\voxel_knight.gox", 1u << 20, raw);
        for (size_t i = 8; i + 16 <= raw.size(); ++i) {
            if (std::memcmp(raw.data() + i, "LAYR", 4) == 0) {
                raw[i + 12u] = 99u;
                break;
            }
        }
        assertMeshRejected(L"Stage15.3D.GoxelGOX_RejectInvalidBlockRef", writeBytes(L"bad_blk.gox", raw));
    }

    // ========================================================================
    // Patrick Min Binvox 3D Voxel Grid (.binvox)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".binvox"), L"Stage15.3D.Binvox_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\stanford_bunny_grid.binvox", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawTriangleCount == 36 &&
                  m1.formatName.find(L".binvox") != std::wstring::npos,
                  L"Stage15.3D.Binvox_FaceCulledGridSample");

        std::string hdr2 = "#binvox 1\ndim 2\ntranslate 0 0 0\nscale 2.0\ndata\n";
        std::vector<uint8_t> bv2(hdr2.begin(), hdr2.end());
        bv2.insert(bv2.end(), { 1u, 1u, 0u, 7u });
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"single_vox.binvox", bv2), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount == 12 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.Binvox_SingleVoxelAndDistinctHash");

        bv2.back() = 9u;
        assertMeshRejected(L"Stage15.3D.Binvox_RejectRleCountMismatch", writeBytes(L"bad_rle.binvox", bv2));
    }

    // ========================================================================
    // Laminar Research X-Plane 8+ 3D Aircraft / Scenery Object (.obj8)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".obj8"),
                  L"Stage15.3D.XPlaneObj8.ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\cessna_aileron.obj8", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 8 && m1.rawTriangleCount == 12 &&
                  std::fabs(m1.bboxSize.x - 3.0f) < 0.05f &&
                  m1.formatName == L"X-Plane 8+ 3D Object (.obj8)",
                  L"Stage15.3D.XPlaneObj8.SampleCessnaAileronOBJ8",
                  L"verts=" + std::to_wstring(m1.rawVertexCount) + L" tris=" + std::to_wstring(m1.rawTriangleCount));

        // Second variant: Apple line-ending header ("A\n800\nOBJ"), ANIM_trans rest-pose offset + secondary LOD filtered out
        const char* animFlapObj8 =
            "A\n"
            "800\n"
            "OBJ\n"
            "POINT_COUNTS 4 0 0 6\n"
            "VT  0.0  0.0  0.0   0 1 0  0 0\n"
            "VT  4.0  0.0  0.0   0 1 0  1 0\n"
            "VT  0.0  3.0  0.0   0 1 0  0 1\n"
            "VT  0.0  0.0  5.0   0 1 0  1 1\n"
            "IDX 0 1 2 0 1 3\n"
            "ATTR_lod 0 500\n"
            "ANIM_begin\n"
            "  ANIM_trans  10.0 2.0 0.0   10.0 2.0 0.0   0.0 1.0  sim/flightmodel2/wing/flap\n"
            "  TRIS 0 3\n"
            "ANIM_end\n"
            "ATTR_lod 500 4000\n"
            "TRIS 3 3\n";
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeText(L"anim_flap.obj8", animFlapObj8), m2);
        // Only primary ATTR_lod 0..500 batch (TRIS 0 3 -> 3 verts, 1 tri shifted by +10 X, +2 Y) is emitted!
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 3 && m2.rawTriangleCount == 1 &&
                  std::fabs(m2.bboxMin.x - 10.0f) < 0.05f && std::fabs(m2.bboxMax.x - 14.0f) < 0.05f &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.XPlaneObj8.AnimTransAndPrimaryLodFilter");

        assertMeshRejected(L"Stage15.3D.XPlaneObj8.RejectWavefrontObjHeader",
            writeText(L"wavefront.obj8", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"));
        assertMeshRejected(L"Stage15.3D.XPlaneObj8.RejectPointCountsMismatch",
            writeText(L"bad_cnt.obj8", "I\n800\nOBJ\nPOINT_COUNTS 9 0 0 3\nVT 0 0 0 0 1 0 0 0\nVT 1 0 0 0 1 0 0 0\nVT 0 1 0 0 1 0 0 0\nIDX 0 1 2\nTRIS 0 3\n"));
        assertMeshRejected(L"Stage15.3D.XPlaneObj8.RejectOutOfBoundsTrisIndex",
            writeText(L"oob_idx.obj8", "I\n800\nOBJ\nPOINT_COUNTS 3 0 0 3\nVT 0 0 0 0 1 0 0 0\nVT 1 0 0 0 1 0 0 0\nVT 0 1 0 0 1 0 0 0\nIDX 0 1 99\nTRIS 0 3\n"));
    }

    // ========================================================================
    // Minetest / Luanti Voxel Schematic (.mts, "MTSM" v1–v4)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".mts"),
                  L"Stage15.3D.MinetestMts.ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\watchtower.mts", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 984 && m1.rawTriangleCount == 492 &&
                  std::fabs(m1.bboxSize.x - 5.0f) < 0.05f && std::fabs(m1.bboxSize.y - 6.0f) < 0.05f &&
                  m1.formatName == L"Minetest / Luanti Voxel Schematic (.mts)",
                  L"Stage15.3D.MinetestMts.SampleWatchtowerMTS",
                  L"verts=" + std::to_wstring(m1.rawVertexCount) + L" tris=" + std::to_wstring(m1.rawTriangleCount));

        // Second variant: v4 MTSM 3x1x1 with 2 adjacent "default:stone" voxels + 1 "air" -> 10 exposed faces = 20 tris
        auto buildMts = [&](uint16_t ver, bool allAir) -> std::vector<uint8_t> {
            std::vector<uint8_t> mts = { 'M', 'T', 'S', 'M' };
            PushU16BE(mts, ver);
            PushU16BE(mts, 3u); PushU16BE(mts, 1u); PushU16BE(mts, 1u); // sx=3, sy=1, sz=1
            mts.push_back(0x7Fu); // Y-slice probability for sy=1
            PushU16BE(mts, 2u);   // 2 node names: 0="air", 1="default:stone"
            PushU16BE(mts, 3u);  mts.insert(mts.end(), { 'a', 'i', 'r' });
            PushU16BE(mts, 13u); TestBytes::AppendStr(mts, "default:stone");
            std::vector<uint8_t> rawNodes;
            uint16_t idSolid = allAir ? 0u : 1u;
            PushU16BE(rawNodes, idSolid); PushU16BE(rawNodes, idSolid); PushU16BE(rawNodes, 0u); // param0[3]
            rawNodes.insert(rawNodes.end(), { 0x7Fu, 0x7Fu, 0x7Fu }); // param1[3]
            rawNodes.insert(rawNodes.end(), { 0u, 0u, 0u });          // param2[3]
            auto zStream = WrapStoredZlibStream(rawNodes);
            mts.insert(mts.end(), zStream.begin(), zStream.end());
            return mts;
        };

        std::vector<uint8_t> mts2 = buildMts(4u, false);
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"two_stone.mts", mts2), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount == 20 &&
                  std::fabs(m2.bboxSize.x - 2.0f) < 0.05f &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.MinetestMts.TwoAdjacentVoxelsCulledAndDistinct");

        std::vector<uint8_t> badMagicMts = mts2;
        badMagicMts[0] = 'X';
        assertMeshRejected(L"Stage15.3D.MinetestMts.RejectBadMagic", writeBytes(L"bad_mag.mts", badMagicMts));

        std::vector<uint8_t> badVerMts = buildMts(99u, false);
        assertMeshRejected(L"Stage15.3D.MinetestMts.RejectUnsupportedVersion", writeBytes(L"bad_ver.mts", badVerMts));

        std::vector<uint8_t> badZlibMts = mts2;
        // Corrupt stored-block NLEN complement inside the zlib deflate stream so InflateZlib fails
        badZlibMts[mts2.size() - 4u - 12u - 1u] ^= 0xFFu;
        assertMeshRejected(L"Stage15.3D.MinetestMts.RejectCorruptZlib", writeBytes(L"bad_z.mts", badZlibMts));

        assertMeshRejected(L"Stage15.3D.MinetestMts.RejectAllAirVolume", writeBytes(L"all_air.mts", buildMts(4u, true)));
    }

    // ========================================================================
    // Minecraft Binary NBT Schematics (.schem, .schematic, .nbt)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".schem") &&
                  Model3DViewer::Is3DModelExtension(L".schematic") &&
                  Model3DViewer::Is3DModelExtension(L".nbt"),
                  L"Stage15.3D.MinecraftNbt.ExtensionsRegistered");

        LoadedMesh3D mSchem, mMcedit, mNbt;
        bool okSchem  = Model3DViewer::InspectMeshFile(td3dDir + L"\\cottage_sponge.schem", mSchem);
        bool okMcedit = Model3DViewer::InspectMeshFile(td3dDir + L"\\archway_mcedit.schematic", mMcedit);
        bool okNbt    = Model3DViewer::InspectMeshFile(td3dDir + L"\\ruined_portal.nbt", mNbt);

        tc.Record(okSchem && mSchem.loadedOk && mSchem.rawVertexCount == 656 && mSchem.rawTriangleCount == 328 &&
                  mSchem.formatName == L"Minecraft Sponge Schematic (.schem)",
                  L"Stage15.3D.MinecraftNbt.SampleCottageSpongeSCHEM",
                  L"verts=" + std::to_wstring(mSchem.rawVertexCount) + L" tris=" + std::to_wstring(mSchem.rawTriangleCount));

        tc.Record(okMcedit && mMcedit.loadedOk && mMcedit.rawVertexCount == 216 && mMcedit.rawTriangleCount == 108 &&
                  mMcedit.formatName == L"Minecraft MCEdit Schematic (.schematic)" &&
                  HashMeshGeometry(mMcedit) != HashMeshGeometry(mSchem),
                  L"Stage15.3D.MinecraftNbt.SampleArchwayMceditSCHEMATIC",
                  L"verts=" + std::to_wstring(mMcedit.rawVertexCount) + L" tris=" + std::to_wstring(mMcedit.rawTriangleCount));

        tc.Record(okNbt && mNbt.loadedOk && mNbt.rawVertexCount == 216 && mNbt.rawTriangleCount == 108 &&
                  mNbt.formatName == L"Minecraft Structure NBT (.nbt)" &&
                  HashMeshGeometry(mNbt) != HashMeshGeometry(mSchem) &&
                  HashMeshGeometry(mNbt) != HashMeshGeometry(mMcedit),
                  L"Stage15.3D.MinecraftNbt.SampleRuinedPortalNBT",
                  L"verts=" + std::to_wstring(mNbt.rawVertexCount) + L" tris=" + std::to_wstring(mNbt.rawTriangleCount));

        // Sponge Schematic v3 wrapper: root -> "Schematic" -> Width=2, Height=1, Length=1, Blocks -> { Palette, Data }
        std::vector<uint8_t> sponge3;
        sponge3.push_back(10u); PushNbtStr(sponge3, "");           // root TAG_Compound ""
        sponge3.push_back(10u); PushNbtStr(sponge3, "Schematic");  // TAG_Compound "Schematic"
        PushNbtTagShort(sponge3, "Width", 2);
        PushNbtTagShort(sponge3, "Height", 1);
        PushNbtTagShort(sponge3, "Length", 1);
        sponge3.push_back(10u); PushNbtStr(sponge3, "Blocks");     // TAG_Compound "Blocks"
        sponge3.push_back(10u); PushNbtStr(sponge3, "Palette");    // TAG_Compound "Palette"
        PushNbtTagInt(sponge3, "minecraft:air", 0);
        PushNbtTagInt(sponge3, "minecraft:stone_bricks", 1);
        sponge3.push_back(0u);                                     // end Palette
        PushNbtTagByteArray(sponge3, "Data", { 1u, 1u });          // 2 adjacent solid blocks -> 20 tris
        sponge3.push_back(0u);                                     // end Blocks
        sponge3.push_back(0u);                                     // end Schematic
        sponge3.push_back(0u);                                     // end root

        LoadedMesh3D mSponge3;
        bool okSp3 = Model3DViewer::InspectMeshFile(writeBytes(L"sponge_v3.schem", sponge3), mSponge3);
        tc.Record(okSp3 && mSponge3.loadedOk && mSponge3.rawTriangleCount == 20 &&
                  mSponge3.formatName == L"Minecraft Sponge Schematic v3 (.schem)" &&
                  HashMeshGeometry(mSponge3) != HashMeshGeometry(mSchem),
                  L"Stage15.3D.MinecraftNbt.SpongeV3WrapperAndDistinctHash");

        // Negative checks: corrupt gzip, non-Compound root, all-air legacy schematic, truncated varint BlockData
        assertMeshRejected(L"Stage15.3D.MinecraftNbt.RejectCorruptGzipSchem",
            writeBytes(L"bad_gz.schem", { 0x1Fu, 0x8Bu, 0x08u, 0u, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0u, 0u, 0u, 0u }));
        assertMeshRejected(L"Stage15.3D.MinecraftNbt.RejectNonCompoundRootNbt",
            writeBytes(L"list_root.nbt", { 9u, 0u, 4u, 't', 'e', 's', 't', 1u, 0u, 0u, 0u, 0u }));

        std::vector<uint8_t> airLegacy;
        airLegacy.push_back(10u); PushNbtStr(airLegacy, "Schematic");
        PushNbtTagShort(airLegacy, "Width", 2);
        PushNbtTagShort(airLegacy, "Height", 2);
        PushNbtTagShort(airLegacy, "Length", 2);
        PushNbtTagByteArray(airLegacy, "Blocks", { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u });
        airLegacy.push_back(0u);
        assertMeshRejected(L"Stage15.3D.MinecraftNbt.RejectAllAirSchematic",
            writeBytes(L"all_air.schematic", airLegacy));

        std::vector<uint8_t> truncVarint;
        truncVarint.push_back(10u); PushNbtStr(truncVarint, "Schematic");
        PushNbtTagShort(truncVarint, "Width", 4);
        PushNbtTagShort(truncVarint, "Height", 1);
        PushNbtTagShort(truncVarint, "Length", 1);
        truncVarint.push_back(10u); PushNbtStr(truncVarint, "Palette");
        PushNbtTagInt(truncVarint, "minecraft:stone", 0);
        truncVarint.push_back(0u);
        PushNbtTagByteArray(truncVarint, "BlockData", { 0u, 0u }); // 2 varints for 4 cells -> truncated
        truncVarint.push_back(0u);
        assertMeshRejected(L"Stage15.3D.MinecraftNbt.RejectTruncatedVarintBlockData",
            writeBytes(L"trunc_varint.schem", truncVarint));
    }

    // ========================================================================
    // Sony PlayStation 1 TMD 3D Model (.tmd, id == 0x00000041)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".tmd"),
                  L"Stage15.3D.PlayStationTmd.ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\wipeout_ship.tmd", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 6 && m1.rawTriangleCount == 7 &&
                  std::fabs(m1.bboxSize.x - 2.1875f) < 0.05f && std::fabs(m1.bboxSize.z - 3.59375f) < 0.05f &&
                  m1.formatName == L"Sony PlayStation 1 TMD Model (.tmd)",
                  L"Stage15.3D.PlayStationTmd.SampleWipeoutShipTMD",
                  L"verts=" + std::to_wstring(m1.rawVertexCount) + L" tris=" + std::to_wstring(m1.rawTriangleCount));

        // Second variant: FIXP=1 (flags=1) 0-based fixed-offset TMD with unlit PS1 Z-order quad (mode=0x28, flag=0x01)
        std::vector<uint8_t> tmdFixp;
        PushU32LE(tmdFixp, 0x00000041u); // id
        PushU32LE(tmdFixp, 1u);          // flags = 1 (FIXP)
        PushU32LE(tmdFixp, 1u);          // nobj = 1
        // 28B object descriptor at offset 12..40
        PushU32LE(tmdFixp, 40u); // vertTop = 40
        PushU32LE(tmdFixp, 4u);  // nVert = 4
        PushU32LE(tmdFixp, 0u);  // normTop = 0
        PushU32LE(tmdFixp, 0u);  // nNorm = 0
        PushU32LE(tmdFixp, 72u); // primTop = 40 + 4*8 = 72
        PushU32LE(tmdFixp, 1u);  // nPrim = 1
        PushI32LE(tmdFixp, 0);   // scale = 0 (1/256)
        const int16_t qSv[4][3] = { {-512, -256, 0}, {512, -256, 0}, {-512, 256, 0}, {512, 256, 0} };
        for (const auto& sv : qSv) {
            PushI16LE(tmdFixp, sv[0]); PushI16LE(tmdFixp, sv[1]); PushI16LE(tmdFixp, sv[2]); PushI16LE(tmdFixp, 0);
        }
        // Unlit flat untextured quad packet: header (olen=5, ilen=3, flag=0x01, mode=0x28) + 6 uint16 words
        tmdFixp.insert(tmdFixp.end(), { 5u, 3u, 0x01u, 0x28u });
        tmdFixp.insert(tmdFixp.end(), { 200u, 120u, 60u, 0x28u }); // R, G, B, mode2 (2 words)
        PushU16LE(tmdFixp, 0u); PushU16LE(tmdFixp, 1u); PushU16LE(tmdFixp, 2u); PushU16LE(tmdFixp, 3u);

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"fixp_quad.tmd", tmdFixp), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 4 && m2.rawTriangleCount == 2 &&
                  std::fabs(m2.bboxSize.x - 4.0f) < 0.05f && std::fabs(m2.bboxSize.y - 2.0f) < 0.05f &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.PlayStationTmd.FixpUnlitQuadAndDistinctHash");

        std::vector<uint8_t> badIdTmd = tmdFixp;
        badIdTmd[0] = 0x42u;
        assertMeshRejected(L"Stage15.3D.PlayStationTmd.RejectInvalidId", writeBytes(L"bad_id.tmd", badIdTmd));

        std::vector<uint8_t> badFlagsTmd = tmdFixp;
        badFlagsTmd[4] = 2u;
        assertMeshRejected(L"Stage15.3D.PlayStationTmd.RejectInvalidFlags", writeBytes(L"bad_flg.tmd", badFlagsTmd));

        std::vector<uint8_t> zeroObjTmd = tmdFixp;
        zeroObjTmd[8] = 0u;
        assertMeshRejected(L"Stage15.3D.PlayStationTmd.RejectZeroObjects", writeBytes(L"zero_obj.tmd", zeroObjTmd));

        std::vector<uint8_t> oobVertTmd = tmdFixp;
        oobVertTmd[oobVertTmd.size() - 2u] = 99u; // vi[3] = 99 >= 4
        assertMeshRejected(L"Stage15.3D.PlayStationTmd.RejectOutOfBoundsVertex", writeBytes(L"oob_v.tmd", oobVertTmd));
    }

    // ========================================================================
    // Westwood Studios / EA SAGE 3D Model (.w3d)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".w3d"),
                  L"Stage15.3D.WestwoodW3d.ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\gdi_tank.w3d", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 16 && m1.rawTriangleCount == 24 &&
                  std::fabs(m1.bboxSize.x - 4.4f) < 0.05f && std::fabs(m1.bboxSize.y - 1.7f) < 0.05f &&
                  m1.formatName == L"Westwood / EA SAGE 3D Model (.w3d)",
                  L"Stage15.3D.WestwoodW3d.SampleGdiTankW3D",
                  L"verts=" + std::to_wstring(m1.rawVertexCount) + L" tris=" + std::to_wstring(m1.rawTriangleCount));

        // Second variant: HIERARCHY (2 pivots, child z=+5) + HLOD SUB_OBJECT + MESH tetrahedron -> Explorer2 Y shifted +5..+8
        auto pushW3dChunk = [](std::vector<uint8_t>& dst, uint32_t cType, bool hasSub, const std::vector<uint8_t>& pay) {
            PushU32LE(dst, cType);
            PushU32LE(dst, static_cast<uint32_t>(pay.size()) | (hasSub ? 0x80000000u : 0u));
            dst.insert(dst.end(), pay.begin(), pay.end());
        };
        std::vector<uint8_t> pivPay;
        auto pushPivot = [&](const char* nm, int32_t parent, float tz) {
            PushFixedAscii(pivPay, nm, 16);
            PushI32LE(pivPay, parent);
            PushF32LE(pivPay, 0.0f); PushF32LE(pivPay, 0.0f); PushF32LE(pivPay, tz); // translation
            PushF32LE(pivPay, 0.0f); PushF32LE(pivPay, 0.0f); PushF32LE(pivPay, 0.0f); // euler
            PushF32LE(pivPay, 0.0f); PushF32LE(pivPay, 0.0f); PushF32LE(pivPay, 0.0f); PushF32LE(pivPay, 1.0f); // quat
        };
        pushPivot("RootTransform", -1, 0.0f);
        pushPivot("TurretMount",    0, 5.0f);
        std::vector<uint8_t> hierBody;
        pushW3dChunk(hierBody, 0x00000102u, false, pivPay); // W3D_CHUNK_PIVOTS

        std::vector<uint8_t> subObjPay;
        PushU32LE(subObjPay, 1u); // boneIdx = 1 ("TurretMount")
        PushFixedAscii(subObjPay, "SAM.TURRET", 32);
        std::vector<uint8_t> hlodBody;
        pushW3dChunk(hlodBody, 0x00000703u, false, subObjPay); // W3D_CHUNK_HLOD_SUB_OBJECT

        std::vector<uint8_t> hdr3(116u, 0u);
        hdr3[0] = 2u; hdr3[2] = 4u; // version 4.2
        std::memcpy(hdr3.data() + 8u,  "TURRET", 6u);
        std::memcpy(hdr3.data() + 24u, "SAM", 3u);
        std::vector<uint8_t> vtxPay;
        for (const auto& p : std::array<std::array<float, 3>, 4>{{ {0,0,0}, {3,0,0}, {0,3,0}, {0,0,3} }}) {
            PushF32LE(vtxPay, p[0]); PushF32LE(vtxPay, p[1]); PushF32LE(vtxPay, p[2]);
        }
        std::vector<uint8_t> triPay;
        const uint32_t w3dTet[4][3] = { {0,1,2}, {0,1,3}, {1,2,3}, {2,0,3} };
        for (const auto& t : w3dTet) {
            PushU32LE(triPay, t[0]); PushU32LE(triPay, t[1]); PushU32LE(triPay, t[2]);
            PushU32LE(triPay, 0u);
            PushF32LE(triPay, 0.0f); PushF32LE(triPay, 0.0f); PushF32LE(triPay, 1.0f); PushF32LE(triPay, 0.0f);
        }
        std::vector<uint8_t> meshBody;
        pushW3dChunk(meshBody, 0x0000001Fu, false, hdr3);
        pushW3dChunk(meshBody, 0x00000002u, false, vtxPay);
        pushW3dChunk(meshBody, 0x00000020u, false, triPay);

        std::vector<uint8_t> w3d2;
        pushW3dChunk(w3d2, 0x00000100u, true, hierBody);
        pushW3dChunk(w3d2, 0x00000700u, true, hlodBody);
        pushW3dChunk(w3d2, 0x00000000u, true, meshBody);

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"sam_turret.w3d", w3d2), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 4 && m2.rawTriangleCount == 4 &&
                  std::fabs(m2.bboxMin.y - 5.0f) < 0.05f && std::fabs(m2.bboxMax.y - 8.0f) < 0.05f &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.WestwoodW3d.HlodPivotTransformAndDistinctHash");

        std::vector<uint8_t> oobTopW3d = { 0,0,0,0, 0xFF,0xFF,0x00,0x80, 0,0,0,0, 0,0,0,0 };
        assertMeshRejected(L"Stage15.3D.WestwoodW3d.RejectOversizedTopChunk", writeBytes(L"oob_top.w3d", oobTopW3d));

        std::vector<uint8_t> noHdrMeshBody;
        pushW3dChunk(noHdrMeshBody, 0x00000002u, false, vtxPay);
        pushW3dChunk(noHdrMeshBody, 0x00000020u, false, triPay);
        std::vector<uint8_t> noHdrW3d;
        pushW3dChunk(noHdrW3d, 0x00000000u, true, noHdrMeshBody);
        assertMeshRejected(L"Stage15.3D.WestwoodW3d.RejectMeshWithoutHeader3", writeBytes(L"no_hdr.w3d", noHdrW3d));

        assertMeshRejected(L"Stage15.3D.WestwoodW3d.RejectTooSmallW3D",
            writeBytes(L"tiny.w3d", { 0u, 1u, 2u, 3u }));
    }

    // ========================================================================
    // Bungie Halo CE / Halo 2 Jointed Model Skeleton (.jms, v8197–v8210)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".jms"),
                  L"Stage15.3D.HaloJms.ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\spartan_helmet.jms", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 9 && m1.rawTriangleCount == 14 &&
                  std::fabs(m1.bboxSize.x - 2.0f) < 0.05f && std::fabs(m1.bboxSize.y - 1.6f) < 0.05f &&
                  m1.formatName == L"Halo CE Jointed Model Skeleton (.jms)",
                  L"Stage15.3D.HaloJms.SampleSpartanHelmetJMS",
                  L"verts=" + std::to_wstring(m1.rawVertexCount) + L" tris=" + std::to_wstring(m1.rawTriangleCount));

        // Second variant: Halo 2 v8210 JMS tetrahedron with explicit influence & UV counts
        const char* halo2Jms =
            "8210\n"
            "3251\n"
            "1\n"
            "b_root\n"
            "-1\n"
            "0.0 0.0 0.0 1.0\n"
            "0.0 0.0 0.0\n"
            "1\n"
            "forerunner_alloy\n"
            "<none>\n"
            "0\n"
            "0\n"
            "0\n"
            "4\n"
            "0.0 0.0 0.0\n0.0 0.0 1.0\n1\n0 1.0\n1\n0.0 0.0\n"
            "6.0 0.0 0.0\n0.0 0.0 1.0\n1\n0 1.0\n1\n1.0 0.0\n"
            "0.0 6.0 0.0\n0.0 0.0 1.0\n1\n0 1.0\n1\n0.0 1.0\n"
            "0.0 0.0 9.0\n0.0 0.0 1.0\n1\n0 1.0\n1\n0.5 0.5\n"
            "4\n"
            "0 0 1 2\n"
            "0 0 1 3\n"
            "0 1 2 3\n"
            "0 2 0 3\n";
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeText(L"halo2_spire.jms", halo2Jms), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 4 && m2.rawTriangleCount == 4 &&
                  std::fabs(m2.bboxMax.y - 9.0f) < 0.05f &&
                  m2.formatName == L"Halo 2 Jointed Model Skeleton (.jms)" &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.HaloJms.Halo2V8210AndDistinctHash");

        assertMeshRejected(L"Stage15.3D.HaloJms.RejectUnsupportedVersion",
            writeText(L"ver8000.jms", "8000\n3251\n0\n0\n0\n0\n0\n"));
        assertMeshRejected(L"Stage15.3D.HaloJms.RejectOutOfBoundsTriangleVert",
            writeText(L"oob_tri.jms",
                "8200\n3251\n1\nframe\n-1\n-1\n0 0 0 1\n0 0 0\n1\nmat\n<none>\n0\n1\ndefault\n"
                "3\n0\n0 0 0\n0 0 1\n-1\n0\n0 0 0\n0\n1 0 0\n0 0 1\n-1\n0\n1 0 0\n0\n0 1 0\n0 0 1\n-1\n0\n0 1 0\n"
                "1\n0\n0\n0 1 99\n"));
        assertMeshRejected(L"Stage15.3D.HaloJms.RejectTruncatedVertices",
            writeText(L"trunc_v.jms",
                "8200\n3251\n1\nframe\n-1\n-1\n0 0 0 1\n0 0 0\n1\nmat\n<none>\n0\n1\ndefault\n5\n0\n0 0 0\n"));
    }

    for (const auto& f : createdFiles) {
        DeleteFileW(f.c_str());
    }
    RemoveDirectoryW(workDir.c_str());
}

} // namespace Explorer2::SelfTestSuite
