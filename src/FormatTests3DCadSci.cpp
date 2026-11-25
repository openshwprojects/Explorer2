// FormatTests3DCadSci.cpp — Stage 15 self-tests for CAD, CAE/FEM/CFD, DCC, Scientific, Raytracer & GIS 3D readers.
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
static inline void PushU32LE(std::vector<uint8_t>& b, uint32_t v) { TestBytes::AppendU32LE(b, v); }
static inline void PushF32LE(std::vector<uint8_t>& b, float v)    { TestBytes::AppendF32LE(b, v); }
static inline void PushU32BE(std::vector<uint8_t>& b, uint32_t v) { TestBytes::AppendU32BE(b, v); }
static inline void PushF32BE(std::vector<uint8_t>& b, float v)    { TestBytes::AppendF32BE(b, v); }

static uint32_t ComputeCrc32Bytes(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1u) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
    }
    return ~crc;
}

static std::vector<uint8_t> BuildStoredSingleFileZip(const char* innerName, const uint8_t* payload, size_t payLen) {
    const uint16_t nameLen = static_cast<uint16_t>(std::strlen(innerName));
    const uint32_t crc = ComputeCrc32Bytes(payload, payLen);
    const uint32_t sz32 = static_cast<uint32_t>(payLen);

    std::vector<uint8_t> zip;
    // Local file header
    PushU32LE(zip, 0x04034B50u);
    PushU16LE(zip, 20u);
    PushU16LE(zip, 0u);
    PushU16LE(zip, 0u); // method = 0 (stored)
    PushU16LE(zip, 0u);
    PushU16LE(zip, 0u);
    PushU32LE(zip, crc);
    PushU32LE(zip, sz32);
    PushU32LE(zip, sz32);
    PushU16LE(zip, nameLen);
    PushU16LE(zip, 0u);
    TestBytes::AppendBytes(zip, innerName, nameLen);
    if (payLen > 0) TestBytes::AppendBytes(zip, payload, payLen);

    const uint32_t cdOff = static_cast<uint32_t>(zip.size());
    // Central directory header
    PushU32LE(zip, 0x02014B50u);
    PushU16LE(zip, 20u);
    PushU16LE(zip, 20u);
    PushU16LE(zip, 0u);
    PushU16LE(zip, 0u);
    PushU16LE(zip, 0u);
    PushU16LE(zip, 0u);
    PushU32LE(zip, crc);
    PushU32LE(zip, sz32);
    PushU32LE(zip, sz32);
    PushU16LE(zip, nameLen);
    PushU16LE(zip, 0u);
    PushU16LE(zip, 0u);
    PushU16LE(zip, 0u);
    PushU16LE(zip, 0u);
    PushU32LE(zip, 0u);
    PushU32LE(zip, 0u);
    TestBytes::AppendBytes(zip, innerName, nameLen);

    const uint32_t cdSize = static_cast<uint32_t>(zip.size()) - cdOff;
    // End of central directory
    PushU32LE(zip, 0x06054B50u);
    PushU16LE(zip, 0u);
    PushU16LE(zip, 0u);
    PushU16LE(zip, 1u);
    PushU16LE(zip, 1u);
    PushU32LE(zip, cdSize);
    PushU32LE(zip, cdOff);
    PushU16LE(zip, 0u);
    return zip;
}

static void AssertMeshRejected(TestCounter& tc, const wchar_t* testName, const std::wstring& badPath) {
    LoadedMesh3D badMesh;
    bool ok = Model3DViewer::InspectMeshFile(badPath, badMesh);
    tc.Record(!ok && !badMesh.loadedOk && badMesh.tris.empty(), testName,
              ok ? L"unexpectedly loaded corrupt input" : L"rejected cleanly");
}

} // namespace

void RunFormatTests3DCadSci(TestCounter& tc) {
    const std::wstring workDir = ScratchTestDir() + L"\\fmt3d_cad_sci";
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
    auto assertMeshRejected = [&](const wchar_t* testName, const std::wstring& badPath) {
        AssertMeshRejected(tc, testName, badPath);
    };

    const std::wstring tdDir     = TestDataDir();
    const std::wstring td3dDir   = tdDir + L"\\3d";
    const std::wstring tdCadDir  = tdDir + L"\\cad";
    const std::wstring tdGisDir  = tdDir + L"\\gis";
    const std::wstring tdDocsDir = tdDir + L"\\docs";

    // ========================================================================
    // Secondary / Alias 3D Extension Registration Verification
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".triq") &&
                  Model3DViewer::Is3DModelExtension(L".nhdr") &&
                  Model3DViewer::Is3DModelExtension(L".brp")  &&
                  Model3DViewer::Is3DModelExtension(L".wrz")  &&
                  Model3DViewer::Is3DModelExtension(L".x3dz"),
                  L"Stage15.3D.CadSci.AliasExtensionsRegistered");
    }

    // ========================================================================
    // id Tech 4 Skeletal Meshes & Animations (.md5mesh, .md5anim)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".md5mesh") &&
                  Model3DViewer::Is3DModelExtension(L".md5anim"),
                  L"Stage15.3D.MD5.ExtensionsRegistered");

        LoadedMesh3D mGuard, mWalk;
        bool okMesh = Model3DViewer::InspectMeshFile(td3dDir + L"\\cyber_guardian.md5mesh", mGuard);
        bool okAnim = Model3DViewer::InspectMeshFile(td3dDir + L"\\cyber_walk.md5anim", mWalk);
        tc.Record(okMesh && mGuard.loadedOk && mGuard.rawVertexCount == 13 && mGuard.rawTriangleCount == 18,
                  L"Stage15.3D.MD5.SampleGuardianMesh",
                  L"verts=" + std::to_wstring(mGuard.rawVertexCount) + L" tris=" + std::to_wstring(mGuard.rawTriangleCount));
        tc.Record(okAnim && mWalk.loadedOk && mWalk.animFrameCount == 2 && mWalk.rawTriangleCount == 18 &&
                  HashMeshGeometry(mGuard) != HashMeshGeometry(mWalk),
                  L"Stage15.3D.MD5.SampleWalkAnimSkinsCompanionMesh",
                  L"frames=" + std::to_wstring(mWalk.animFrameCount) + L" tris=" + std::to_wstring(mWalk.rawTriangleCount));

        const char* standaloneAnimA =
            "MD5Version 10\nnumFrames 3\nnumJoints 3\nframeRate 30\nnumAnimatedComponents 3\n"
            "hierarchy {\n\t\"root\" -1 0 0\n\t\"knee\" 0 4 0\n\t\"foot\" 1 24 1\n}\n"
            "bounds { ( -5 -5 0 ) ( 5 5 20 ) }\n"
            "baseframe {\n\t( 0 0 0 ) ( 0 0 0 )\n\t( 0 0 10 ) ( 0 0 0 )\n\t( 0 6 0 ) ( 0 0 0 )\n}\n"
            "frame 0 { 10.0 0.0 0.0 }\n";
        const char* standaloneAnimB =
            "MD5Version 10\nnumFrames 3\nnumJoints 3\nframeRate 30\nnumAnimatedComponents 3\n"
            "hierarchy {\n\t\"root\" -1 0 0\n\t\"knee\" 0 4 0\n\t\"foot\" 1 24 1\n}\n"
            "bounds { ( -5 -5 0 ) ( 5 5 20 ) }\n"
            "baseframe {\n\t( 0 0 0 ) ( 0 0 0 )\n\t( 0 0 10 ) ( 0 0 0 )\n\t( 0 6 0 ) ( 0 0 0 )\n}\n"
            "frame 0 { 18.0 0.5 0.5 }\n";
        LoadedMesh3D mSkelA, mSkelB;
        bool okSA = Model3DViewer::InspectMeshFile(writeText(L"solo_pose_a.md5anim", standaloneAnimA), mSkelA);
        bool okSB = Model3DViewer::InspectMeshFile(writeText(L"solo_pose_b.md5anim", standaloneAnimB), mSkelB);
        tc.Record(okSA && okSB && mSkelA.rawTriangleCount == 16 && mSkelB.rawTriangleCount == 16 &&
                  HashMeshGeometry(mSkelA) != HashMeshGeometry(mSkelB),
                  L"Stage15.3D.MD5.StandaloneAnimDistinctPoses",
                  L"trisA=" + std::to_wstring(mSkelA.rawTriangleCount));

        assertMeshRejected(L"Stage15.3D.MD5.RejectWrongVersion",
            writeText(L"bad_ver.md5mesh", "MD5Version 9\nnumJoints 1\nnumMeshes 0\njoints { \"r\" -1 ( 0 0 0 ) ( 0 0 0 ) }\n"));
        assertMeshRejected(L"Stage15.3D.MD5.RejectZeroMeshes",
            writeText(L"no_meshes.md5mesh", "MD5Version 10\nnumJoints 1\nnumMeshes 0\njoints { \"r\" -1 ( 0 0 0 ) ( 0 0 0 ) }\n"));
        assertMeshRejected(L"Stage15.3D.MD5.RejectTruncatedAnimFrame",
            writeText(L"trunc.md5anim",
                "MD5Version 10\nnumFrames 1\nnumJoints 2\nframeRate 24\nnumAnimatedComponents 4\n"
                "hierarchy { \"a\" -1 0 0 \"b\" 0 15 0 }\n"
                "baseframe { ( 0 0 0 ) ( 0 0 0 ) ( 1 2 3 ) ( 0 0 0 ) }\n"
                "frame 0 { 1.0 2.0 }\n"));
    }

    // ========================================================================
    // Unreal Engine ActorX Meshes & Animations (.psk, .pskx, .psa)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".psk") &&
                  Model3DViewer::Is3DModelExtension(L".pskx") &&
                  Model3DViewer::Is3DModelExtension(L".psa"),
                  L"Stage15.3D.ActorX.ExtensionsRegistered");

        LoadedMesh3D mPsk, mPskx, mPsa;
        bool okPsk  = Model3DViewer::InspectMeshFile(td3dDir + L"\\ut_trooper.psk", mPsk);
        bool okPskx = Model3DViewer::InspectMeshFile(td3dDir + L"\\ut_heavy_32bit.pskx", mPskx);
        bool okPsa  = Model3DViewer::InspectMeshFile(td3dDir + L"\\ut_sprint.psa", mPsa);

        tc.Record(okPsk && mPsk.loadedOk && mPsk.rawVertexCount == 10 && mPsk.rawTriangleCount == 14,
                  L"Stage15.3D.ActorX.SampleTrooperPSK",
                  L"verts=" + std::to_wstring(mPsk.rawVertexCount) + L" tris=" + std::to_wstring(mPsk.rawTriangleCount));
        tc.Record(okPskx && mPskx.loadedOk && mPskx.rawVertexCount == 9 && mPskx.rawTriangleCount == 8 &&
                  HashMeshGeometry(mPsk) != HashMeshGeometry(mPskx),
                  L"Stage15.3D.ActorX.SampleExtended32BitPSKX",
                  L"verts=" + std::to_wstring(mPskx.rawVertexCount) + L" tris=" + std::to_wstring(mPskx.rawTriangleCount));
        tc.Record(okPsa && mPsa.loadedOk && mPsa.animFrameCount == 2 && mPsa.rawTriangleCount == 14 &&
                  HashMeshGeometry(mPsk) != HashMeshGeometry(mPsa),
                  L"Stage15.3D.ActorX.SampleSprintPSADeformsCompanionPSK",
                  L"frames=" + std::to_wstring(mPsa.animFrameCount) + L" tris=" + std::to_wstring(mPsa.rawTriangleCount));

        std::vector<uint8_t> badMagic(64, 0);
        std::memcpy(badMagic.data(), "NOT_ACTORX", 10);
        assertMeshRejected(L"Stage15.3D.ActorX.RejectBadMagic", writeBytes(L"bad_magic.psk", badMagic));

        std::vector<uint8_t> hdrOnlyPsk(32, 0);
        std::memcpy(hdrOnlyPsk.data(), "ACTRHEAD", 8);
        assertMeshRejected(L"Stage15.3D.ActorX.RejectHeaderOnlyPSK", writeBytes(L"hdr_only.psk", hdrOnlyPsk));

        std::vector<uint8_t> truncPsa(64, 0);
        std::memcpy(truncPsa.data(), "ANIMHEAD", 8);
        std::memcpy(truncPsa.data() + 32, "BONENAMES", 9);
        int32_t dSz = 120, dCnt = 5;
        std::memcpy(truncPsa.data() + 56, &dSz, 4);
        std::memcpy(truncPsa.data() + 60, &dCnt, 4);
        assertMeshRejected(L"Stage15.3D.ActorX.RejectTruncatedPSA", writeBytes(L"trunc.psa", truncPsa));
    }

    // ========================================================================
    // MikuMikuDance / VTuber Models & Motion (.pmx, .pmd, .vmd)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".pmx") &&
                  Model3DViewer::Is3DModelExtension(L".pmd") &&
                  Model3DViewer::Is3DModelExtension(L".vmd"),
                  L"Stage15.3D.MMD.ExtensionsRegistered");

        LoadedMesh3D mPmx, mPmd, mVmd;
        bool okPmx = Model3DViewer::InspectMeshFile(td3dDir + L"\\vtuber_star.pmx", mPmx);
        bool okPmd = Model3DViewer::InspectMeshFile(td3dDir + L"\\classic_stage.pmd", mPmd);
        bool okVmd = Model3DViewer::InspectMeshFile(td3dDir + L"\\dance_wave.vmd", mVmd);

        tc.Record(okPmx && mPmx.loadedOk && mPmx.rawVertexCount == 12 && mPmx.rawTriangleCount == 20,
                  L"Stage15.3D.MMD.SampleVtuberPMX",
                  L"verts=" + std::to_wstring(mPmx.rawVertexCount) + L" tris=" + std::to_wstring(mPmx.rawTriangleCount));
        tc.Record(okPmd && mPmd.loadedOk && mPmd.rawVertexCount == 7 && mPmd.rawTriangleCount == 6 &&
                  HashMeshGeometry(mPmx) != HashMeshGeometry(mPmd),
                  L"Stage15.3D.MMD.SampleClassicPMD",
                  L"verts=" + std::to_wstring(mPmd.rawVertexCount) + L" tris=" + std::to_wstring(mPmd.rawTriangleCount));
        tc.Record(okVmd && mVmd.loadedOk && mVmd.animFrameCount == 31 && mVmd.rawTriangleCount == 32 &&
                  HashMeshGeometry(mVmd) != HashMeshGeometry(mPmx),
                  L"Stage15.3D.MMD.SampleDanceVMD",
                  L"frames=" + std::to_wstring(mVmd.animFrameCount) + L" tris=" + std::to_wstring(mVmd.rawTriangleCount));

        std::vector<uint8_t> badPmx(64, 0);
        std::memcpy(badPmx.data(), "PMX!", 4);
        assertMeshRejected(L"Stage15.3D.MMD.RejectBadMagicPMX", writeBytes(L"bad.pmx", badPmx));

        std::vector<uint8_t> truncPmd(300, 0);
        std::memcpy(truncPmd.data(), "Pmd", 3);
        float v10 = 1.0f;
        std::memcpy(truncPmd.data() + 3, &v10, 4);
        uint32_t nv50 = 50;
        std::memcpy(truncPmd.data() + 283, &nv50, 4);
        assertMeshRejected(L"Stage15.3D.MMD.RejectTruncatedPMD", writeBytes(L"trunc.pmd", truncPmd));

        std::vector<uint8_t> emptyVmd(62, 0);
        std::memcpy(emptyVmd.data(), "Vocaloid Motion Data 0002", 25);
        assertMeshRejected(L"Stage15.3D.MMD.RejectEmptyVMD", writeBytes(L"empty.vmd", emptyVmd));
    }

    // ========================================================================
    // Blizzard WC3 & WoW 3D Models (.mdx, .mdl, .m2 + .skin, .wmo)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".mdx") &&
                  Model3DViewer::Is3DModelExtension(L".mdl") &&
                  Model3DViewer::Is3DModelExtension(L".m2") &&
                  Model3DViewer::Is3DModelExtension(L".wmo"),
                  L"Stage15.3D.Blizzard.ExtensionsRegistered");

        LoadedMesh3D mMdx, mMdl, mM2, mWmo;
        bool okMdx = Model3DViewer::InspectMeshFile(td3dDir + L"\\wc3_footman_shield.mdx", mMdx);
        bool okMdl = Model3DViewer::InspectMeshFile(td3dDir + L"\\wc3_obelisk.mdl", mMdl);
        bool okM2  = Model3DViewer::InspectMeshFile(td3dDir + L"\\wow_crystal_spire.m2", mM2);
        bool okWmo = Model3DViewer::InspectMeshFile(td3dDir + L"\\wow_chapel_000.wmo", mWmo);

        tc.Record(okMdx && mMdx.loadedOk && mMdx.rawVertexCount == 7 && mMdx.rawTriangleCount == 6 && mMdx.animFrameCount == 2,
                  L"Stage15.3D.Blizzard.SampleWC3ShieldMDX",
                  L"verts=" + std::to_wstring(mMdx.rawVertexCount) + L" tris=" + std::to_wstring(mMdx.rawTriangleCount));
        tc.Record(okMdl && mMdl.loadedOk && mMdl.rawVertexCount == 6 && mMdl.rawTriangleCount == 8 &&
                  HashMeshGeometry(mMdl) != HashMeshGeometry(mMdx),
                  L"Stage15.3D.Blizzard.SampleWC3ObeliskMDL",
                  L"verts=" + std::to_wstring(mMdl.rawVertexCount) + L" tris=" + std::to_wstring(mMdl.rawTriangleCount));
        tc.Record(okM2 && mM2.loadedOk && mM2.rawVertexCount == 5 && mM2.rawTriangleCount == 6 && mM2.animFrameCount == 3 &&
                  HashMeshGeometry(mM2) != HashMeshGeometry(mMdx),
                  L"Stage15.3D.Blizzard.SampleWoWSpireM2WithSkin",
                  L"verts=" + std::to_wstring(mM2.rawVertexCount) + L" tris=" + std::to_wstring(mM2.rawTriangleCount));
        tc.Record(okWmo && mWmo.loadedOk && mWmo.rawVertexCount == 6 && mWmo.rawTriangleCount == 8 &&
                  HashMeshGeometry(mWmo) != HashMeshGeometry(mMdl),
                  L"Stage15.3D.Blizzard.SampleWoWChapelWMO",
                  L"verts=" + std::to_wstring(mWmo.rawVertexCount) + L" tris=" + std::to_wstring(mWmo.rawTriangleCount));

        std::vector<uint8_t> grpBytes;
        if (PathUtils::ReadFileBytes(td3dDir + L"\\wow_chapel_000.wmo", 1024 * 1024, grpBytes)) {
            writeBytes(L"wow_root_test_000.wmo", grpBytes);
            std::vector<uint8_t> rootWmo;
            auto append4 = [&](const char* s) { rootWmo.insert(rootWmo.end(), s, s + 4); };
            auto appendU32 = [&](uint32_t v) { PushU32LE(rootWmo, v); };
            append4("REVM"); appendU32(4); appendU32(17);
            append4("DHOM"); appendU32(64);
            appendU32(1);
            appendU32(1);
            rootWmo.resize(rootWmo.size() + 56, 0);
            LoadedMesh3D mRootWmo;
            bool okRoot = Model3DViewer::InspectMeshFile(writeBytes(L"wow_root_test.wmo", rootWmo), mRootWmo);
            tc.Record(okRoot && mRootWmo.loadedOk && mRootWmo.rawTriangleCount == 8,
                      L"Stage15.3D.Blizzard.RootWMOStitchesGroup000");
        }

        std::vector<uint8_t> mdlxNoGeos = { 'M','D','L','X','V','E','R','S',4,0,0,0,0x20,0x03,0,0 };
        assertMeshRejected(L"Stage15.3D.Blizzard.RejectMDLXWithoutGeos", writeBytes(L"nogeos.mdx", mdlxNoGeos));

        std::vector<uint8_t> orphanM2;
        if (PathUtils::ReadFileBytes(td3dDir + L"\\wow_crystal_spire.m2", 1024 * 1024, orphanM2)) {
            assertMeshRejected(L"Stage15.3D.Blizzard.RejectM2WithoutSkin", writeBytes(L"orphan_noskin.m2", orphanM2));
        }

        std::vector<uint8_t> badWmo = { 'R','E','V','M',4,0,0,0,17,0,0,0,'P','G','O','M',100,0,0,0,0,0 };
        assertMeshRejected(L"Stage15.3D.Blizzard.RejectTruncatedWMO", writeBytes(L"trunc.wmo", badWmo));
    }

    // ========================================================================
    // RenderWare Binary Stream 3D Clumps & World Sectors (.dff, .rws)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".dff") &&
                  Model3DViewer::Is3DModelExtension(L".rws"),
                  L"Stage15.3D.RenderWare.ExtensionsRegistered");

        LoadedMesh3D mDff, mRws;
        bool okDff = Model3DViewer::InspectMeshFile(td3dDir + L"\\vice_speeder.dff", mDff);
        bool okRws = Model3DViewer::InspectMeshFile(td3dDir + L"\\arena_sector.rws", mRws);

        tc.Record(okDff && mDff.loadedOk && mDff.rawVertexCount == 10 && mDff.rawTriangleCount == 10,
                  L"Stage15.3D.RenderWare.SampleViceSpeederDFF",
                  L"verts=" + std::to_wstring(mDff.rawVertexCount) + L" tris=" + std::to_wstring(mDff.rawTriangleCount));
        tc.Record(okRws && mRws.loadedOk && mRws.rawVertexCount == 8 && mRws.rawTriangleCount == 4 &&
                  HashMeshGeometry(mDff) != HashMeshGeometry(mRws),
                  L"Stage15.3D.RenderWare.SampleArenaSectorRWS",
                  L"verts=" + std::to_wstring(mRws.rawVertexCount) + L" tris=" + std::to_wstring(mRws.rawTriangleCount));

        std::vector<uint8_t> badStamp(32, 0);
        badStamp[0] = 0x10; badStamp[4] = 12;
        assertMeshRejected(L"Stage15.3D.RenderWare.RejectBadRwVersionStamp", writeBytes(L"bad_stamp.dff", badStamp));

        std::vector<uint8_t> truncDff = {
            0x10,0,0,0, 200,0,0,0, 0xFF,0xFF,0x03,0x18,
            0x01,0,0,0, 12,0,0,0,  0xFF,0xFF,0x03,0x18
        };
        assertMeshRejected(L"Stage15.3D.RenderWare.RejectTruncatedClump", writeBytes(L"trunc.dff", truncDff));
    }

    // ========================================================================
    // id Tech 3 / RTCW / WolfET / UberTools Skeletal Models (.md4, .mds, .mdm, .mdx, .tan)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".md4") &&
                  Model3DViewer::Is3DModelExtension(L".mdr") &&
                  Model3DViewer::Is3DModelExtension(L".mds") &&
                  Model3DViewer::Is3DModelExtension(L".mdm") &&
                  Model3DViewer::Is3DModelExtension(L".mdx") &&
                  Model3DViewer::Is3DModelExtension(L".tan"),
                  L"Stage15.3D.IdTech3Skel.ExtensionsRegistered");

        LoadedMesh3D mMd4, mMds, mMdm, mMdx, mTan;
        bool okMd4 = Model3DViewer::InspectMeshFile(td3dDir + L"\\q3_sentinel.md4", mMd4);
        bool okMds = Model3DViewer::InspectMeshFile(td3dDir + L"\\rtcw_guard.mds", mMds);
        bool okMdm = Model3DViewer::InspectMeshFile(td3dDir + L"\\et_medic.mdm", mMdm);
        bool okMdx = Model3DViewer::InspectMeshFile(td3dDir + L"\\et_medic.mdx", mMdx);
        bool okTan = Model3DViewer::InspectMeshFile(td3dDir + L"\\fakk2_blade.tan", mTan);

        tc.Record(okMd4 && mMd4.loadedOk && mMd4.rawVertexCount == 6 && mMd4.rawTriangleCount == 8 && mMd4.animFrameCount == 2,
                  L"Stage15.3D.IdTech3Skel.SampleQ3SentinelMD4",
                  L"verts=" + std::to_wstring(mMd4.rawVertexCount) + L" tris=" + std::to_wstring(mMd4.rawTriangleCount));
        tc.Record(okMds && mMds.loadedOk && mMds.rawVertexCount == 5 && mMds.rawTriangleCount == 6 &&
                  HashMeshGeometry(mMds) != HashMeshGeometry(mMd4),
                  L"Stage15.3D.IdTech3Skel.SampleRtcwGuardMDS",
                  L"verts=" + std::to_wstring(mMds.rawVertexCount) + L" tris=" + std::to_wstring(mMds.rawTriangleCount));
        tc.Record(okMdm && mMdm.loadedOk && mMdm.rawVertexCount == 7 && mMdm.rawTriangleCount == 10 && mMdm.animFrameCount == 3,
                  L"Stage15.3D.IdTech3Skel.SampleWolfEtMedicMDM",
                  L"verts=" + std::to_wstring(mMdm.rawVertexCount) + L" tris=" + std::to_wstring(mMdm.rawTriangleCount));
        tc.Record(okMdx && mMdx.loadedOk && mMdx.rawVertexCount == 7 && mMdx.rawTriangleCount == 10 && mMdx.animFrameCount == 3,
                  L"Stage15.3D.IdTech3Skel.SampleWolfEtMedicMDXPaired");
        tc.Record(okTan && mTan.loadedOk && mTan.rawVertexCount == 6 && mTan.rawTriangleCount == 8 &&
                  HashMeshGeometry(mTan) != HashMeshGeometry(mMd4),
                  L"Stage15.3D.IdTech3Skel.SampleFakk2BladeTAN",
                  L"verts=" + std::to_wstring(mTan.rawVertexCount) + L" tris=" + std::to_wstring(mTan.rawTriangleCount));

        std::vector<uint8_t> soloMdxBytes;
        if (PathUtils::ReadFileBytes(td3dDir + L"\\et_medic.mdx", 1024 * 1024, soloMdxBytes)) {
            LoadedMesh3D mSoloMdx;
            bool okSolo = Model3DViewer::InspectMeshFile(writeBytes(L"solo_rig.mdx", soloMdxBytes), mSoloMdx);
            tc.Record(okSolo && mSoloMdx.loadedOk && mSoloMdx.rawTriangleCount == 24 &&
                      HashMeshGeometry(mSoloMdx) != HashMeshGeometry(mMdm),
                      L"Stage15.3D.IdTech3Skel.StandaloneWolfEtMDXSkeleton",
                      L"tris=" + std::to_wstring(mSoloMdx.rawTriangleCount));
        }

        std::vector<uint8_t> badMd4(128, 0);
        std::memcpy(badMd4.data(), "IDP4", 4);
        int32_t badVer99 = 99;
        std::memcpy(badMd4.data() + 4, &badVer99, 4);
        assertMeshRejected(L"Stage15.3D.IdTech3Skel.RejectWrongVersionMD4", writeBytes(L"bad_ver.md4", badMd4));

        std::vector<uint8_t> truncMds(128, 0);
        std::memcpy(truncMds.data(), "MDSW", 4);
        int32_t ver4 = 4;
        std::memcpy(truncMds.data() + 4, &ver4, 4);
        assertMeshRejected(L"Stage15.3D.IdTech3Skel.RejectTruncatedMDS", writeBytes(L"trunc.mds", truncMds));

        std::vector<uint8_t> badTan(128, 0);
        std::memcpy(badTan.data(), "TAN ", 4);
        int32_t ver2 = 2;
        std::memcpy(badTan.data() + 4, &ver2, 4);
        assertMeshRejected(L"Stage15.3D.IdTech3Skel.RejectZeroSurfacesTAN", writeBytes(L"empty.tan", badTan));
    }

    // ========================================================================
    // BioWare Aurora ASCII (.mdl) & Odyssey KotOR Binary (.mdl + .mdx)
    // ========================================================================
    {
        LoadedMesh3D mAuroraAscii, mKotorMdl, mKotorMdx;
        bool okAsc  = Model3DViewer::InspectMeshFile(td3dDir + L"\\nwn_watchtower.mdl", mAuroraAscii);
        bool okKmdl = Model3DViewer::InspectMeshFile(td3dDir + L"\\kotor_droid.mdl", mKotorMdl);
        bool okKmdx = Model3DViewer::InspectMeshFile(td3dDir + L"\\kotor_droid.mdx", mKotorMdx);

        tc.Record(okAsc && mAuroraAscii.loadedOk && mAuroraAscii.rawVertexCount == 13 &&
                  mAuroraAscii.rawTriangleCount == 16 && mAuroraAscii.animFrameCount == 1,
                  L"Stage15.3D.BioWareAurora.SampleNwnWatchtowerAsciiMDL",
                  L"verts=" + std::to_wstring(mAuroraAscii.rawVertexCount) + L" tris=" + std::to_wstring(mAuroraAscii.rawTriangleCount));
        tc.Record(okKmdl && mKotorMdl.loadedOk && mKotorMdl.rawVertexCount == 6 &&
                  mKotorMdl.rawTriangleCount == 8 &&
                  HashMeshGeometry(mKotorMdl) != HashMeshGeometry(mAuroraAscii),
                  L"Stage15.3D.BioWareAurora.SampleKotorDroidBinaryMDL",
                  L"verts=" + std::to_wstring(mKotorMdl.rawVertexCount) + L" tris=" + std::to_wstring(mKotorMdl.rawTriangleCount));
        tc.Record(okKmdx && mKotorMdx.loadedOk && mKotorMdx.rawVertexCount == 6 && mKotorMdx.rawTriangleCount == 8,
                  L"Stage15.3D.BioWareAurora.SampleKotorDroidPairedMDX");

        assertMeshRejected(L"Stage15.3D.BioWareAurora.RejectTruncatedAsciiMDL",
            writeText(L"bad_nwn.mdl",
                "newmodel bad\nbeginmodelgeom bad\nnode trimesh m\nverts 4\n0 0 0\nendnode\nendmodelgeom bad\ndonemodel bad\n"));
        std::vector<uint8_t> badKotor(128, 0);
        uint32_t rawSz = 100;
        std::memcpy(badKotor.data() + 4, &rawSz, 4);
        std::memcpy(badKotor.data() + 20, "bad_kotor", 9);
        assertMeshRejected(L"Stage15.3D.BioWareAurora.RejectCorruptBinaryMDL", writeBytes(L"bad_kotor.mdl", badKotor));
        assertMeshRejected(L"Stage15.3D.BioWareAurora.RejectOrphanRawMDX", writeBytes(L"orphan_only.mdx", std::vector<uint8_t>(96, 0x42)));
    }

    // ========================================================================
    // Godot Engine 3/4 Scenes & Meshes (.tscn, .scn, .tres, .res, .mesh)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".tscn") &&
                  Model3DViewer::Is3DModelExtension(L".scn") &&
                  Model3DViewer::Is3DModelExtension(L".tres") &&
                  Model3DViewer::Is3DModelExtension(L".res") &&
                  Model3DViewer::Is3DModelExtension(L".mesh"),
                  L"Stage15.3D.Godot.ExtensionsRegistered");

        LoadedMesh3D mTres, mTscn, mMesh, mRes, mScn;
        bool okTres = Model3DViewer::InspectMeshFile(td3dDir + L"\\godot_crystal.tres", mTres);
        bool okTscn = Model3DViewer::InspectMeshFile(td3dDir + L"\\godot_outpost.tscn", mTscn);
        bool okMesh = Model3DViewer::InspectMeshFile(td3dDir + L"\\godot_turret.mesh", mMesh);
        bool okRes  = Model3DViewer::InspectMeshFile(td3dDir + L"\\godot_compressed.res", mRes);
        bool okScn  = Model3DViewer::InspectMeshFile(td3dDir + L"\\godot_bunker.scn", mScn);

        tc.Record(okTres && mTres.loadedOk && mTres.rawVertexCount == 6 && mTres.rawTriangleCount == 8,
                  L"Stage15.3D.Godot.SampleCrystalTRES",
                  L"verts=" + std::to_wstring(mTres.rawVertexCount) + L" tris=" + std::to_wstring(mTres.rawTriangleCount));
        tc.Record(okTscn && mTscn.loadedOk && mTscn.rawVertexCount == 19 && mTscn.rawTriangleCount == 24 &&
                  HashMeshGeometry(mTscn) != HashMeshGeometry(mTres),
                  L"Stage15.3D.Godot.SampleOutpostTSCN",
                  L"verts=" + std::to_wstring(mTscn.rawVertexCount) + L" tris=" + std::to_wstring(mTscn.rawTriangleCount));
        tc.Record(okMesh && mMesh.loadedOk && mMesh.rawVertexCount == 7 && mMesh.rawTriangleCount == 10 &&
                  HashMeshGeometry(mMesh) != HashMeshGeometry(mTres),
                  L"Stage15.3D.Godot.SampleTurretBinaryMESH",
                  L"verts=" + std::to_wstring(mMesh.rawVertexCount) + L" tris=" + std::to_wstring(mMesh.rawTriangleCount));
        tc.Record(okRes && mRes.loadedOk && mRes.rawVertexCount == 5 && mRes.rawTriangleCount == 6 &&
                  HashMeshGeometry(mRes) != HashMeshGeometry(mMesh),
                  L"Stage15.3D.Godot.SampleCompressedRSCC_RES",
                  L"verts=" + std::to_wstring(mRes.rawVertexCount) + L" tris=" + std::to_wstring(mRes.rawTriangleCount));
        tc.Record(okScn && mScn.loadedOk && mScn.rawVertexCount == 8 && mScn.rawTriangleCount == 12 &&
                  HashMeshGeometry(mScn) != HashMeshGeometry(mRes),
                  L"Stage15.3D.Godot.SampleBunkerBinarySCN",
                  L"verts=" + std::to_wstring(mScn.rawVertexCount) + L" tris=" + std::to_wstring(mScn.rawTriangleCount));

        assertMeshRejected(L"Stage15.3D.Godot.RejectNonMeshTRES",
            writeText(L"audio_only.tres", "[gd_resource type=\"AudioStreamWAV\" format=3]\n[resource]\nmix_rate = 44100\n"));
        std::vector<uint8_t> win32Res(64, 0);
        win32Res[4] = 0x20; win32Res[8] = 0xFF; win32Res[9] = 0xFF;
        assertMeshRejected(L"Stage15.3D.Godot.RejectWin32ResourceRES", writeBytes(L"win32.res", win32Res));
        std::vector<uint8_t> badRscc(48, 0);
        std::memcpy(badRscc.data(), "RSCC", 4);
        badRscc[4] = 1; badRscc[8] = 16; badRscc[12] = 64;
        assertMeshRejected(L"Stage15.3D.Godot.RejectCorruptRSCC", writeBytes(L"corrupt.res", badRscc));
    }

    // ========================================================================
    // Unreal Engine 4/5 Direct 3D UStaticMesh (.uasset + .uexp, .umap)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".uasset") &&
                  Model3DViewer::Is3DModelExtension(L".umap"),
                  L"Stage15.3D.UnrealPkg.ExtensionsRegistered");

        LoadedMesh3D mUe5Pillar, mUe4Map;
        bool okUe5 = Model3DViewer::InspectMeshFile(td3dDir + L"\\ue5_nanite_pillar.uasset", mUe5Pillar);
        bool okMap = Model3DViewer::InspectMeshFile(tdDocsDir + L"\\ue4_outpost_level.umap", mUe4Map);

        tc.Record(okUe5 && mUe5Pillar.loadedOk && mUe5Pillar.rawVertexCount == 8 && mUe5Pillar.rawTriangleCount == 12,
                  L"Stage15.3D.UnrealPkg.SampleUE5StaticMeshWithUEXP",
                  L"verts=" + std::to_wstring(mUe5Pillar.rawVertexCount) + L" tris=" + std::to_wstring(mUe5Pillar.rawTriangleCount));
        tc.Record(okMap && mUe4Map.loadedOk && mUe4Map.rawVertexCount == 5 && mUe4Map.rawTriangleCount == 6 &&
                  HashMeshGeometry(mUe4Map) != HashMeshGeometry(mUe5Pillar),
                  L"Stage15.3D.UnrealPkg.SampleUE4OutpostUMAP",
                  L"verts=" + std::to_wstring(mUe4Map.rawVertexCount) + L" tris=" + std::to_wstring(mUe4Map.rawTriangleCount));

        assertMeshRejected(L"Stage15.3D.UnrealPkg.RejectTexture2DUassetAs3D", td3dDir + L"\\ue4_scifi_panel.uasset");
        std::vector<uint8_t> badUe(96, 0);
        PushU32LE(badUe, 0x9E2A83C1u);
        assertMeshRejected(L"Stage15.3D.UnrealPkg.RejectTruncatedUasset", writeBytes(L"bad.uasset", badUe));
    }

    // ========================================================================
    // Autodesk FBX (.fbx), COLLADA (.dae), & MilkShape (.ms3d) Linear-Blend Skinning & Poses
    // ========================================================================
    {
        LoadedMesh3D mFbxSkin, mDaeSkin, mMs3dRig;
        bool okFbx  = Model3DViewer::InspectMeshFile(td3dDir + L"\\skinned_arm.fbx", mFbxSkin);
        bool okDae  = Model3DViewer::InspectMeshFile(td3dDir + L"\\skinned_cyber_limb.dae", mDaeSkin);
        bool okMs3d = Model3DViewer::InspectMeshFile(td3dDir + L"\\rigged_mech.ms3d", mMs3dRig);

        tc.Record(okFbx && mFbxSkin.loadedOk && mFbxSkin.rawVertexCount == 12 && mFbxSkin.rawTriangleCount == 20 &&
                  mFbxSkin.bboxMax.y > 1.85f && mFbxSkin.bboxMax.x < 2.50f && mFbxSkin.animFrameCount >= 2,
                  L"Stage15.3D.LbsSkinning.SampleFbxSkinnedArm",
                  L"verts=" + std::to_wstring(mFbxSkin.rawVertexCount) + L" maxY=" + std::to_wstring(mFbxSkin.bboxMax.y) +
                  L" maxX=" + std::to_wstring(mFbxSkin.bboxMax.x));

        tc.Record(okDae && mDaeSkin.loadedOk && mDaeSkin.rawVertexCount == 12 && mDaeSkin.rawTriangleCount == 20 &&
                  mDaeSkin.bboxMax.y > 1.85f && mDaeSkin.bboxMax.x < 2.50f &&
                  HashMeshGeometry(mDaeSkin) != HashMeshGeometry(mFbxSkin),
                  L"Stage15.3D.LbsSkinning.SampleColladaSkinnedLimb",
                  L"verts=" + std::to_wstring(mDaeSkin.rawVertexCount) + L" maxY=" + std::to_wstring(mDaeSkin.bboxMax.y) +
                  L" maxX=" + std::to_wstring(mDaeSkin.bboxMax.x));

        tc.Record(okMs3d && mMs3dRig.loadedOk && mMs3dRig.rawVertexCount == 12 && mMs3dRig.rawTriangleCount == 20 &&
                  mMs3dRig.bboxMax.y > 1.85f && mMs3dRig.bboxMax.x < 2.50f && mMs3dRig.animFrameCount == 24 &&
                  HashMeshGeometry(mMs3dRig) != HashMeshGeometry(mDaeSkin),
                  L"Stage15.3D.LbsSkinning.SampleMs3dRiggedMech",
                  L"verts=" + std::to_wstring(mMs3dRig.rawVertexCount) + L" maxY=" + std::to_wstring(mMs3dRig.bboxMax.y) +
                  L" frames=" + std::to_wstring(mMs3dRig.animFrameCount));

        std::vector<uint8_t> badFbx(32, 0);
        std::memcpy(badFbx.data(), "Kaydara FBX Binary  \0\x1a\0", 23);
        assertMeshRejected(L"Stage15.3D.LbsSkinning.RejectTruncatedFBX", writeBytes(L"trunc_skin.fbx", badFbx));
        assertMeshRejected(L"Stage15.3D.LbsSkinning.RejectEmptySkinDAE",
            writeText(L"empty_skin.dae", "<COLLADA><library_controllers><controller id=\"c\"><skin source=\"#missing\"/></controller></library_controllers></COLLADA>"));
        std::vector<uint8_t> badMs3d(20, 0);
        std::memcpy(badMs3d.data(), "MS3D000000", 10);
        badMs3d[10] = 4; badMs3d[14] = 10;
        assertMeshRejected(L"Stage15.3D.LbsSkinning.RejectTruncatedMS3D", writeBytes(L"trunc.ms3d", badMs3d));
    }

    // ========================================================================
    // Cal3D Skeletal (.cmf, .xmf, .csf) & CryEngine Geometry (.cgf, .chr, .skin)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".cmf") &&
                  Model3DViewer::Is3DModelExtension(L".xmf") &&
                  Model3DViewer::Is3DModelExtension(L".csf") &&
                  Model3DViewer::Is3DModelExtension(L".cgf") &&
                  Model3DViewer::Is3DModelExtension(L".chr") &&
                  Model3DViewer::Is3DModelExtension(L".skin"),
                  L"Stage15.3D.Cal3DCry.ExtensionsRegistered");

        LoadedMesh3D mCmf, mCsfArm, mXmf, mCgf, mChr, mSkin, mWowSkin;
        bool okCmf  = Model3DViewer::InspectMeshFile(td3dDir + L"\\paladin_rig.cmf", mCmf);
        bool okCsf  = Model3DViewer::InspectMeshFile(td3dDir + L"\\cal3d_biped_armature.csf", mCsfArm);
        bool okXmf  = Model3DViewer::InspectMeshFile(td3dDir + L"\\avatar_crown.xmf", mXmf);
        bool okCgf  = Model3DViewer::InspectMeshFile(td3dDir + L"\\crysis_crate.cgf", mCgf);
        bool okChr  = Model3DViewer::InspectMeshFile(td3dDir + L"\\nomad_suit.chr", mChr);
        bool okSkin = Model3DViewer::InspectMeshFile(td3dDir + L"\\nomad_pauldron.skin", mSkin);
        bool okWSk  = Model3DViewer::InspectMeshFile(td3dDir + L"\\wow_crystal_spire00.skin", mWowSkin);

        tc.Record(okCmf && mCmf.loadedOk && mCmf.rawVertexCount == 8 && mCmf.rawTriangleCount == 12 &&
                  mCmf.bboxMax.y > 1.85f && mCmf.bboxMax.x < 2.50f,
                  L"Stage15.3D.Cal3DCry.SampleCal3DSkinnedCMF",
                  L"verts=" + std::to_wstring(mCmf.rawVertexCount) + L" maxY=" + std::to_wstring(mCmf.bboxMax.y));
        tc.Record(okCsf && mCsfArm.loadedOk && mCsfArm.rawVertexCount == 12 && mCsfArm.rawTriangleCount == 16 &&
                  HashMeshGeometry(mCsfArm) != HashMeshGeometry(mCmf),
                  L"Stage15.3D.Cal3DCry.SampleCal3DArmatureCSF",
                  L"verts=" + std::to_wstring(mCsfArm.rawVertexCount) + L" tris=" + std::to_wstring(mCsfArm.rawTriangleCount));
        tc.Record(okXmf && mXmf.loadedOk && mXmf.rawVertexCount == 6 && mXmf.rawTriangleCount == 8 &&
                  HashMeshGeometry(mXmf) != HashMeshGeometry(mCmf),
                  L"Stage15.3D.Cal3DCry.SampleCal3DXmlXMF",
                  L"verts=" + std::to_wstring(mXmf.rawVertexCount) + L" tris=" + std::to_wstring(mXmf.rawTriangleCount));
        tc.Record(okCgf && mCgf.loadedOk && mCgf.rawVertexCount == 8 && mCgf.rawTriangleCount == 12 &&
                  HashMeshGeometry(mCgf) != HashMeshGeometry(mCmf),
                  L"Stage15.3D.Cal3DCry.SampleCryTek0745CGF",
                  L"verts=" + std::to_wstring(mCgf.rawVertexCount) + L" tris=" + std::to_wstring(mCgf.rawTriangleCount));
        tc.Record(okChr && mChr.loadedOk && mChr.rawVertexCount == 5 && mChr.rawTriangleCount == 6 &&
                  HashMeshGeometry(mChr) != HashMeshGeometry(mCgf),
                  L"Stage15.3D.Cal3DCry.SampleCrCh0746CHR",
                  L"verts=" + std::to_wstring(mChr.rawVertexCount) + L" tris=" + std::to_wstring(mChr.rawTriangleCount));
        tc.Record(okSkin && mSkin.loadedOk && mSkin.rawVertexCount == 6 && mSkin.rawTriangleCount == 8 &&
                  HashMeshGeometry(mSkin) != HashMeshGeometry(mChr) && okWSk && mWowSkin.loadedOk,
                  L"Stage15.3D.Cal3DCry.SampleCrCh0746SKIN_AndWoWSkinCompat",
                  L"verts=" + std::to_wstring(mSkin.rawVertexCount) + L" tris=" + std::to_wstring(mSkin.rawTriangleCount));

        std::vector<uint8_t> badCmf(24, 0);
        std::memcpy(badCmf.data(), "CMF\0", 4);
        badCmf[4] = 0xE8; badCmf[5] = 0x03; badCmf[8] = 2;
        assertMeshRejected(L"Stage15.3D.Cal3DCry.RejectTruncatedCMF", writeBytes(L"bad.cmf", badCmf));
        assertMeshRejected(L"Stage15.3D.Cal3DCry.RejectEmptyXMF", writeText(L"empty.xmf", "<HEADER MAGIC=\"XMF\"/><MESH/>"));
        std::vector<uint8_t> badCrCh(32, 0);
        std::memcpy(badCrCh.data(), "CrCh", 4);
        badCrCh[4] = 0x46; badCrCh[5] = 0x07; badCrCh[8] = 5; badCrCh[12] = 16;
        assertMeshRejected(L"Stage15.3D.Cal3DCry.RejectTruncatedCrChCGF", writeBytes(L"bad.cgf", badCrCh));
        std::vector<uint8_t> orphanSkin(48, 0);
        std::memcpy(orphanSkin.data(), "SKIN", 4);
        assertMeshRejected(L"Stage15.3D.Cal3DCry.RejectOrphanWowSKIN", writeBytes(L"orphan99.skin", orphanSkin));
    }

    // ========================================================================
    // FreeCAD (.fcstd), SpaceClaim (.scdoc), Inventor (.ipt, .iam), & Solid Edge (.par, .psm)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".fcstd") &&
                  Model3DViewer::Is3DModelExtension(L".scdoc") &&
                  Model3DViewer::Is3DModelExtension(L".ipt")   &&
                  Model3DViewer::Is3DModelExtension(L".iam")   &&
                  Model3DViewer::Is3DModelExtension(L".par")   &&
                  Model3DViewer::Is3DModelExtension(L".psm"),
                  L"Stage15.3D.CadPkg.ExtensionsRegistered");

        LoadedMesh3D mFc, mSc, mIpt, mIam, mPar, mPsm;
        bool okFc  = Model3DViewer::InspectMeshFile(tdCadDir + L"\\freecad_flange.fcstd", mFc);
        bool okSc  = Model3DViewer::InspectMeshFile(tdCadDir + L"\\heat_sink.scdoc", mSc);
        bool okIpt = Model3DViewer::InspectMeshFile(tdCadDir + L"\\valve_manifold.ipt", mIpt);
        bool okIam = Model3DViewer::InspectMeshFile(tdCadDir + L"\\inventor_drive.iam", mIam);
        bool okPar = Model3DViewer::InspectMeshFile(tdCadDir + L"\\bearing_block.par", mPar);
        bool okPsm = Model3DViewer::InspectMeshFile(tdCadDir + L"\\solidedge_bracket.psm", mPsm);

        tc.Record(okFc && mFc.loadedOk && mFc.rawVertexCount == 13 && mFc.rawTriangleCount == 18 &&
                  mFc.bboxMax.z > 23.5f,
                  L"Stage15.3D.CadPkg.SampleFreeCadFCSTD",
                  L"verts=" + std::to_wstring(mFc.rawVertexCount) + L" maxZ=" + std::to_wstring(mFc.bboxMax.z));
        tc.Record(okSc && mSc.loadedOk && mSc.rawVertexCount == 8 && mSc.rawTriangleCount == 12 &&
                  std::fabs(mSc.bboxSize.x - 6.0f) < 0.05f &&
                  mSc.formatName == L"ANSYS SpaceClaim 3D CAD Document (.scdoc)" &&
                  HashMeshGeometry(mSc) != HashMeshGeometry(mFc),
                  L"Stage15.3D.CadPkg.SampleSpaceClaimHeatSinkSCDOC",
                  L"verts=" + std::to_wstring(mSc.rawVertexCount) + L" tris=" + std::to_wstring(mSc.rawTriangleCount));
        tc.Record(okIpt && mIpt.loadedOk && mIpt.rawVertexCount == 8 && mIpt.rawTriangleCount == 12 &&
                  std::fabs(mIpt.bboxSize.x - 5.0f) < 0.05f &&
                  mIpt.formatName == L"Autodesk Inventor 3D Part (.ipt)" &&
                  HashMeshGeometry(mIpt) != HashMeshGeometry(mSc),
                  L"Stage15.3D.CadPkg.SampleInventorValveManifoldIPT",
                  L"verts=" + std::to_wstring(mIpt.rawVertexCount) + L" tris=" + std::to_wstring(mIpt.rawTriangleCount));
        tc.Record(okIam && mIam.loadedOk && mIam.rawVertexCount == 8 && mIam.rawTriangleCount == 12 &&
                  std::fabs(mIam.bboxSize.x - 8.0f) < 0.05f &&
                  mIam.formatName == L"Autodesk Inventor 3D Assembly (.iam)" &&
                  HashMeshGeometry(mIam) != HashMeshGeometry(mIpt),
                  L"Stage15.3D.CadPkg.SampleInventorAssemblyIAM",
                  L"verts=" + std::to_wstring(mIam.rawVertexCount) + L" spanX=" + std::to_wstring(mIam.bboxSize.x));
        tc.Record(okPar && mPar.loadedOk && mPar.rawVertexCount == 8 && mPar.rawTriangleCount == 12 &&
                  std::fabs(mPar.bboxSize.x - 3.6f) < 0.05f &&
                  mPar.formatName == L"Siemens Solid Edge Part (.par)" &&
                  HashMeshGeometry(mPar) != HashMeshGeometry(mIpt),
                  L"Stage15.3D.CadPkg.SampleSolidEdgeBearingBlockPAR",
                  L"verts=" + std::to_wstring(mPar.rawVertexCount) + L" tris=" + std::to_wstring(mPar.rawTriangleCount));
        tc.Record(okPsm && mPsm.loadedOk && mPsm.rawVertexCount == 8 && mPsm.rawTriangleCount == 12 &&
                  std::fabs(mPsm.bboxSize.x - 5.2f) < 0.05f &&
                  mPsm.formatName == L"Siemens Solid Edge Sheet Metal (.psm)" &&
                  HashMeshGeometry(mPsm) != HashMeshGeometry(mPar),
                  L"Stage15.3D.CadPkg.SampleSolidEdgeBracketPSM",
                  L"verts=" + std::to_wstring(mPsm.rawVertexCount) + L" tris=" + std::to_wstring(mPsm.rawTriangleCount));

        assertMeshRejected(L"Stage15.3D.CadPkg.RejectCorruptFCSTD", writeText(L"bad.fcstd", "PK\x03\x04corrupt_zip"));
        assertMeshRejected(L"Stage15.3D.CadPkg.RejectCorruptSCDOC", writeText(L"bad.scdoc", "PK\x03\x04not_spaceclaim"));
        const char* nonScXml = "<other>not spaceclaim</other>";
        auto nonScZip = BuildStoredSingleFileZip("unrelated.xml", reinterpret_cast<const uint8_t*>(nonScXml), std::strlen(nonScXml));
        assertMeshRejected(L"Stage15.3D.CadPkg.RejectNonSpaceClaimZipSCDOC", writeBytes(L"non_sc.scdoc", nonScZip));
        std::vector<uint8_t> badOle(512, 0);
        const uint8_t oleSig[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
        std::memcpy(badOle.data(), oleSig, 8);
        assertMeshRejected(L"Stage15.3D.CadPkg.RejectEmptyOleIPT", writeBytes(L"bad.ipt", badOle));
        assertMeshRejected(L"Stage15.3D.CadPkg.RejectEmptyOleIAM", writeBytes(L"bad.iam", badOle));
        assertMeshRejected(L"Stage15.3D.CadPkg.RejectEmptyOlePAR", writeBytes(L"bad.par", badOle));
        assertMeshRejected(L"Stage15.3D.CadPkg.RejectEmptyOlePSM", writeBytes(L"bad.psm", badOle));
    }

    // ========================================================================
    // Modo LXOB (.lxo), Zlib 3ds Max (.max), Cinema 4D HyperFile (.c4d), & ZBrush (.zpr, .ztl)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".lxo") &&
                  Model3DViewer::Is3DModelExtension(L".max") &&
                  Model3DViewer::Is3DModelExtension(L".c4d") &&
                  Model3DViewer::Is3DModelExtension(L".ztl") &&
                  Model3DViewer::Is3DModelExtension(L".zpr"),
                  L"Stage15.3D.ModoZBrushMaxC4d.ExtensionsRegistered");

        LoadedMesh3D mLxo, mMax, mC4d, mZtl, mZpr;
        bool okLxo = Model3DViewer::InspectMeshFile(td3dDir + L"\\modo_drone_pod.lxo", mLxo);
        bool okMax = Model3DViewer::InspectMeshFile(td3dDir + L"\\max_compressed_mech.max", mMax);
        bool okC4d = Model3DViewer::InspectMeshFile(td3dDir + L"\\cinema_hyperfile.c4d", mC4d);
        bool okZtl = Model3DViewer::InspectMeshFile(td3dDir + L"\\sculpt_bust.ztl", mZtl);
        bool okZpr = Model3DViewer::InspectMeshFile(td3dDir + L"\\creature_project.zpr", mZpr);

        tc.Record(okLxo && mLxo.loadedOk && mLxo.rawVertexCount == 9 && mLxo.rawTriangleCount == 14 &&
                  mLxo.bboxMax.y > 4.1f,
                  L"Stage15.3D.ModoZBrushMaxC4d.SampleModoLXO",
                  L"verts=" + std::to_wstring(mLxo.rawVertexCount) + L" tris=" + std::to_wstring(mLxo.rawTriangleCount));
        tc.Record(okMax && mMax.loadedOk && mMax.rawVertexCount == 8 && mMax.rawTriangleCount == 12 &&
                  HashMeshGeometry(mMax) != HashMeshGeometry(mLxo),
                  L"Stage15.3D.ModoZBrushMaxC4d.SampleZlibEditablePolyMAX",
                  L"verts=" + std::to_wstring(mMax.rawVertexCount) + L" tris=" + std::to_wstring(mMax.rawTriangleCount));
        tc.Record(okC4d && mC4d.loadedOk && mC4d.rawVertexCount == 9 && mC4d.rawTriangleCount == 14 &&
                  std::fabs(mC4d.bboxMax.y - 3.10f) < 0.05f &&
                  mC4d.formatName == L"Maxon Cinema 4D Scene (.c4d)" &&
                  HashMeshGeometry(mC4d) != HashMeshGeometry(mMax),
                  L"Stage15.3D.ModoZBrushMaxC4d.SampleCinemaHyperFileC4D",
                  L"verts=" + std::to_wstring(mC4d.rawVertexCount) + L" tris=" + std::to_wstring(mC4d.rawTriangleCount));
        tc.Record(okZtl && mZtl.loadedOk && mZtl.rawVertexCount == 8 && mZtl.rawTriangleCount == 12 &&
                  std::fabs(mZtl.bboxSize.z - 2.20f) < 0.05f &&
                  mZtl.formatName == L"Maxon ZBrush 3D SubTool (.ztl)" &&
                  HashMeshGeometry(mZtl) != HashMeshGeometry(mC4d),
                  L"Stage15.3D.ModoZBrushMaxC4d.SampleZBrushSculptBustZTL",
                  L"verts=" + std::to_wstring(mZtl.rawVertexCount) + L" tris=" + std::to_wstring(mZtl.rawTriangleCount));
        tc.Record(okZpr && mZpr.loadedOk && mZpr.rawVertexCount == 8 && mZpr.rawTriangleCount == 12 &&
                  std::fabs(mZpr.bboxSize.z - 2.64f) < 0.05f &&
                  mZpr.formatName == L"Maxon ZBrush 3D Project (.zpr)" &&
                  HashMeshGeometry(mZpr) != HashMeshGeometry(mZtl),
                  L"Stage15.3D.ModoZBrushMaxC4d.SampleZBrushCreatureProjectZPR",
                  L"verts=" + std::to_wstring(mZpr.rawVertexCount) + L" tris=" + std::to_wstring(mZpr.rawTriangleCount));

        assertMeshRejected(L"Stage15.3D.ModoZBrushMaxC4d.RejectEmptyLXO", writeText(L"bad.lxo", "FORM\x00\x00\x00\x04LXOB"));
        assertMeshRejected(L"Stage15.3D.ModoZBrushMaxC4d.RejectCorruptC4D", writeText(L"bad.c4d", "C4DC\x00\x00\x52\x08\x78\x9Cnot_zlib"));
        assertMeshRejected(L"Stage15.3D.ModoZBrushMaxC4d.RejectNonZBrushZTL", writeText(L"bad.ztl", "NOT_A_ZBRUSH_FILE_RANDOM_BYTES_12345678"));
        std::vector<uint8_t> emptyZpr(160, 0);
        std::memcpy(emptyZpr.data(), "ZBrush File v2024 ZPR without subtools", 38);
        assertMeshRejected(L"Stage15.3D.ModoZBrushMaxC4d.RejectEmptyZPR", writeBytes(L"bad.zpr", emptyZpr));
    }

    // ========================================================================
    // Google Draco Edgebreaker + rANS (.drc) & glTF KHR_draco / EXT_meshopt
    // ========================================================================
    {
        LoadedMesh3D mDrc, mGltfDraco, mGltfMeshopt;
        bool okDrc = Model3DViewer::InspectMeshFile(td3dDir + L"\\draco_edgebreaker_gem.drc", mDrc);
        bool okGD  = Model3DViewer::InspectMeshFile(td3dDir + L"\\gltf_draco_spaceship.glb", mGltfDraco);
        bool okGM  = Model3DViewer::InspectMeshFile(td3dDir + L"\\gltf_meshopt_turret.glb", mGltfMeshopt);

        tc.Record(okDrc && mDrc.loadedOk && mDrc.rawVertexCount == 6 && mDrc.rawTriangleCount == 8 &&
                  mDrc.bboxMax.y > 6.95f && mDrc.bboxMin.y < -4.95f,
                  L"Stage15.3D.DracoMeshopt.SampleEdgebreakerRansDRC",
                  L"verts=" + std::to_wstring(mDrc.rawVertexCount) + L" tris=" + std::to_wstring(mDrc.rawTriangleCount));
        tc.Record(okGD && mGltfDraco.loadedOk && mGltfDraco.rawVertexCount == 7 && mGltfDraco.rawTriangleCount == 10 &&
                  mGltfDraco.bboxMax.z > 11.9f && HashMeshGeometry(mGltfDraco) != HashMeshGeometry(mDrc),
                  L"Stage15.3D.DracoMeshopt.SampleGltfKhrDracoGLB",
                  L"verts=" + std::to_wstring(mGltfDraco.rawVertexCount) + L" tris=" + std::to_wstring(mGltfDraco.rawTriangleCount));
        tc.Record(okGM && mGltfMeshopt.loadedOk && mGltfMeshopt.rawVertexCount == 8 && mGltfMeshopt.rawTriangleCount == 12 &&
                  mGltfMeshopt.bboxMax.y > 7.95f && HashMeshGeometry(mGltfMeshopt) != HashMeshGeometry(mGltfDraco),
                  L"Stage15.3D.DracoMeshopt.SampleGltfExtMeshoptGLB",
                  L"verts=" + std::to_wstring(mGltfMeshopt.rawVertexCount) + L" tris=" + std::to_wstring(mGltfMeshopt.rawTriangleCount));

        const std::vector<uint8_t> valenceMultiAttrDrc = {
            0x44, 0x52, 0x41, 0x43, 0x4F, 0x02, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x0C, 0x14, 0x02, 0x13,
            0x00, 0x00, 0x05, 0x6F, 0xAB, 0x55, 0x55, 0x0A, 0x01, 0x01, 0x10, 0xDE, 0x04, 0xC8, 0x58, 0xE2,
            0x4F, 0xDE, 0x04, 0xC8, 0x58, 0xE2, 0x4F, 0x03, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0xFF, 0x00,
            0x00, 0x01, 0x03, 0x09, 0x02, 0x01, 0x00, 0x00, 0x01, 0x01, 0x09, 0x03, 0x00, 0x01, 0x00, 0x01,
            0x00, 0x09, 0x03, 0x00, 0x02, 0x02, 0x3D, 0x0A, 0xD7, 0x3E, 0x00, 0x00, 0x00, 0x3F, 0x29, 0x5C,
            0x0F, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x3D, 0x0A, 0x57, 0x3E, 0x00, 0x00, 0x40, 0x3F, 0xAE, 0x47,
            0x21, 0x3F, 0x00, 0x00, 0x80, 0x3E, 0xC3, 0xF5, 0x68, 0x3F, 0x00, 0x00, 0x80, 0x3E, 0x29, 0x5C,
            0x8F, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x33, 0x33, 0xB3, 0x3E, 0x00, 0x00, 0x80, 0x3E, 0x29, 0x5C,
            0x8F, 0x3D, 0x00, 0x00, 0x80, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0xE1,
            0xFA, 0x3E, 0x00, 0x00, 0x40, 0x3F, 0x33, 0x33, 0x33, 0x3F, 0x00, 0x00, 0x00, 0x3F, 0x29, 0x5C,
            0x0F, 0x3E, 0x00, 0x00, 0x00, 0x3F, 0xB8, 0x1E, 0x45, 0x3F, 0x00, 0x00, 0x40, 0x3F, 0x3D, 0x0A,
            0x57, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x96, 0x06, 0xBF, 0x40, 0xC4,
            0x59, 0xBF, 0x40, 0xC4, 0x59, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x50, 0x96, 0x06, 0xBF, 0x50, 0x96,
            0x06, 0x3F, 0x40, 0xC4, 0x59, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x40, 0xC4, 0x59, 0x3F, 0x00, 0x00,
            0x00, 0x00, 0x50, 0x96, 0x06, 0x3F, 0x50, 0x96, 0x06, 0xBF, 0x40, 0xC4, 0x59, 0xBF, 0x00, 0x00,
            0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x50, 0x96, 0x06, 0xBF, 0x40, 0xC4, 0x59, 0x3F, 0x00, 0x00,
            0x00, 0x00, 0x50, 0x96, 0x06, 0x3F, 0x40, 0xC4, 0x59, 0x3F, 0x50, 0x96, 0x06, 0x3F, 0x40, 0xC4,
            0x59, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x50, 0x96, 0x06, 0xBF, 0x40, 0xC4, 0x59, 0x3F, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x96, 0x06, 0x3F, 0x40, 0xC4, 0x59, 0xBF, 0x40, 0xC4,
            0x59, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x50, 0x96, 0x06, 0xBF, 0x50, 0x96, 0x06, 0xBF, 0x40, 0xC4,
            0x59, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x40, 0xC4, 0x59, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x50, 0x96,
            0x06, 0x3F, 0x50, 0x96, 0x06, 0x3F, 0x40, 0xC4, 0x59, 0xBF, 0x00, 0x00, 0x00, 0x80, 0x01, 0x01,
            0x01, 0x00, 0x0D, 0x27, 0x55, 0x05, 0x01, 0x30, 0xAD, 0x0A, 0x04, 0x36, 0x96, 0x4A, 0x85, 0xDC,
            0xF6, 0x22, 0x00, 0xB4, 0x0D, 0x71, 0x0E, 0x77, 0x4A, 0xDB, 0x92, 0xE7, 0x0E, 0xBD, 0x68, 0xBC,
            0x9D, 0x06, 0x40, 0x00, 0x3B, 0xEC, 0x22, 0x00, 0x02, 0x20, 0x00, 0x0E, 0x7B, 0x0A, 0x00, 0x90,
            0xA7, 0x00, 0xBC, 0x08, 0x80, 0x4E, 0x61, 0x05, 0xE0, 0x33, 0x0D, 0xDF, 0x30, 0x00, 0xBA, 0x68,
            0x3C, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x0F, 0x00, 0x00, 0xCE, 0x54, 0x9B, 0xC0, 0x56, 0x71, 0x01,
            0xC1, 0x45, 0x38, 0x35, 0xC1, 0x45, 0x38, 0xB5, 0x41, 0x0C,
        };
        LoadedMesh3D mValence;
        bool okVal = Model3DViewer::InspectMeshFile(writeBytes(L"valence_multi.drc", valenceMultiAttrDrc), mValence);
        tc.Record(okVal && mValence.loadedOk && mValence.rawVertexCount == 14 && mValence.rawTriangleCount == 20 &&
                  std::fabs(mValence.bboxMax.z - 11.3262f) < 0.02f && std::fabs(mValence.bboxMin.y + 8.0902f) < 0.02f,
                  L"Stage15.3D.DracoMeshopt.ValenceEdgebreakerMultiAttrDRC",
                  L"verts=" + std::to_wstring(mValence.rawVertexCount) + L" tris=" + std::to_wstring(mValence.rawTriangleCount));

        const std::vector<uint8_t> kdTreePointCloudDrc = {
            0x44, 0x52, 0x41, 0x43, 0x4F, 0x02, 0x03, 0x00, 0x01, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01,
            0x01, 0x00, 0x09, 0x03, 0x00, 0x00, 0x04, 0x0C, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0xDE,
            0x03, 0xCC, 0x29, 0x46, 0xE6, 0x03, 0x6A, 0x8D, 0x81, 0xC0, 0x03, 0x00, 0x8A, 0x80, 0xFF, 0x01,
            0x11, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00,
            0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01,
            0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01,
            0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00,
            0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01,
            0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x60, 0x00, 0x00, 0x00, 0x18, 0xF0, 0xA4, 0x1B,
            0x2D, 0x98, 0xB1, 0xFA, 0x07, 0xA8, 0x63, 0x48, 0x68, 0x3B, 0x96, 0xEB, 0x4A, 0x9A, 0x30, 0x7B,
            0x0D, 0x09, 0x8E, 0xDA, 0xF0, 0x28, 0x2A, 0xF7, 0xA8, 0x51, 0xBB, 0x64, 0xF2, 0xB9, 0xC4, 0xA7,
            0x55, 0xA7, 0x5A, 0x86, 0xD4, 0xFC, 0xBF, 0x03, 0x6A, 0x41, 0x5D, 0xC0, 0x26, 0xA6, 0xF6, 0xE1,
            0x94, 0x06, 0x40, 0x3A, 0x52, 0x7B, 0x4F, 0x00, 0x9E, 0xEA, 0xFA, 0x52, 0xDC, 0x4B, 0xDE, 0xD2,
            0x00, 0x57, 0x66, 0x4F, 0x4B, 0x35, 0xFF, 0x0F, 0xD3, 0x0F, 0x30, 0x73, 0xB2, 0xDB, 0xE4, 0xEA,
            0x43, 0xC5, 0x00, 0x50, 0xBA, 0x2D, 0x8A, 0x1A, 0x80, 0x2D, 0x75, 0xB3, 0x04, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x28, 0x7F, 0x82, 0x1E, 0xC1,
            0x38, 0x67, 0xFF, 0xC0, 0x00, 0x00, 0xC0, 0xC0, 0x40, 0x41, 0x9F, 0x41, 0x0C,
        };
        LoadedMesh3D mKdPc;
        bool okKdPc = Model3DViewer::InspectMeshFile(writeBytes(L"kdtree_pc.drc", kdTreePointCloudDrc), mKdPc);
        tc.Record(okKdPc && mKdPc.loadedOk && mKdPc.rawVertexCount == 96 && mKdPc.rawTriangleCount == 96 &&
                  mKdPc.bboxMin.x < -10.0f && mKdPc.bboxMax.x > 10.0f && mKdPc.bboxMax.z > 9.0f,
                  L"Stage15.3D.DracoMeshopt.KdTreePointCloudDRC",
                  L"verts=" + std::to_wstring(mKdPc.rawVertexCount) + L" tris=" + std::to_wstring(mKdPc.rawTriangleCount));

        const char* khrMeshoptFltsGltf = R"JSON({"asset":{"version":"2.0"},"extensionsUsed":["KHR_meshopt_compression"],"extensionsRequired":["KHR_meshopt_compression"],"buffers":[{"byteLength":144,"extensions":{"KHR_meshopt_compression":{"fallback":true}}},{"byteLength":139,"uri":"data:application/octet-stream;base64,oV5aXgCgn5+gAAAYAAECAAIABgAiAAnyACAAPwAAwL+/wAAwAAECAAQABgAAAAAAAAAAAAAAAPEASAD1AAAA8QBSAKFvAACAfgT8AP1/gf39ACIA/f0AAAAAAAAAAAAAAAAAAAAAAAAAAH9/fwDRAAQEBggECgwEDhAKEAoCEAYCDAICCA4MAAAAAA=="}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":72,"byteStride":12,"extensions":{"KHR_meshopt_compression":{"buffer":1,"byteOffset":0,"byteLength":67,"byteStride":12,"count":6,"mode":"ATTRIBUTES","filter":"EXPONENTIAL"}}},{"buffer":0,"byteOffset":72,"byteLength":24,"byteStride":4,"extensions":{"KHR_meshopt_compression":{"buffer":1,"byteOffset":67,"byteLength":43,"byteStride":4,"count":6,"mode":"ATTRIBUTES","filter":"OCTAHEDRAL"}}},{"buffer":0,"byteOffset":96,"byteLength":48,"extensions":{"KHR_meshopt_compression":{"buffer":1,"byteOffset":110,"byteLength":29,"byteStride":2,"count":24,"mode":"INDICES"}}}],"accessors":[{"bufferView":0,"componentType":5126,"count":6,"type":"VEC3"},{"bufferView":1,"componentType":5120,"normalized":true,"count":6,"type":"VEC3"},{"bufferView":2,"componentType":5123,"count":24,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2}]}]})JSON";
        LoadedMesh3D mMoKhr;
        bool okMoKhr = Model3DViewer::InspectMeshFile(writeText(L"khr_meshopt_filters.gltf", khrMeshoptFltsGltf), mMoKhr);
        tc.Record(okMoKhr && mMoKhr.loadedOk && mMoKhr.rawVertexCount == 6 && mMoKhr.rawTriangleCount == 8 &&
                  std::fabs(mMoKhr.bboxMax.y - 9.0f) < 0.01f && std::fabs(mMoKhr.bboxMin.y + 7.0f) < 0.01f &&
                  std::fabs(mMoKhr.bboxMax.z - 6.0f) < 0.01f,
                  L"Stage15.3D.DracoMeshopt.GltfKhrMeshoptFiltersAndIndices",
                  L"verts=" + std::to_wstring(mMoKhr.rawVertexCount) + L" maxY=" + std::to_wstring(mMoKhr.bboxMax.y));

        const std::vector<uint8_t> badEbDrc = { 'D','R','A','C','O', 2, 2, 1, 1, 0, 0, 0, 6, 8 };
        assertMeshRejected(L"Stage15.3D.DracoMeshopt.RejectTruncatedEdgebreakerDRC", writeBytes(L"bad_eb.drc", badEbDrc));
        assertMeshRejected(L"Stage15.3D.DracoMeshopt.RejectCorruptGltfMeshopt",
            writeText(L"bad_mo.gltf", "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"data:application/octet-stream;base64,AAAA\",\"byteLength\":3}],\"bufferViews\":[{\"buffer\":0,\"byteLength\":3,\"extensions\":{\"EXT_meshopt_compression\":{\"buffer\":0,\"byteOffset\":0,\"byteLength\":3,\"byteStride\":12,\"count\":8,\"mode\":\"ATTRIBUTES\"}}}],\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":8,\"type\":\"VEC3\"}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}]}"));
    }

    // ========================================================================
    // Rhinoceros OpenNURBS (.3dm) ON_Brep, ON_Extrusion, ON_NurbsSurface & ON_InstanceRef
    // ========================================================================
    {
        LoadedMesh3D mCanopy, mInst;
        bool okCanopy = Model3DViewer::InspectMeshFile(tdCadDir + L"\\rhino_nurbs_canopy.3dm", mCanopy);
        bool okInst   = Model3DViewer::InspectMeshFile(tdCadDir + L"\\rhino_instanced_pillars.3dm", mInst);

        tc.Record(okCanopy && mCanopy.loadedOk && mCanopy.rawVertexCount == 301 && mCanopy.rawTriangleCount == 532 &&
                  std::fabs(mCanopy.bboxMax.y - 7.20f) < 0.02f,
                  L"Stage15.3D.Rhino3dm.SampleNurbsBrepAndExtrusion3DM",
                  L"verts=" + std::to_wstring(mCanopy.rawVertexCount) + L" maxY=" + std::to_wstring(mCanopy.bboxMax.y));
        tc.Record(okInst && mInst.loadedOk && mInst.rawVertexCount == 24 && mInst.rawTriangleCount == 36 &&
                  mInst.bboxMin.x < -21.5f && mInst.bboxMax.x > 26.5f &&
                  HashMeshGeometry(mInst) != HashMeshGeometry(mCanopy),
                  L"Stage15.3D.Rhino3dm.SampleBlockInstanceRef3DM",
                  L"verts=" + std::to_wstring(mInst.rawVertexCount) + L" maxX=" + std::to_wstring(mInst.bboxMax.x));

        assertMeshRejected(L"Stage15.3D.Rhino3dm.RejectEmpty3DM",
            writeText(L"empty.3dm", "3D Geometry File Format       70"));
    }

    // ========================================================================
    // Modern AutoCAD .dwg (AC1015 R2000 & AC1032 R2018 LZ77) & 3D / Binary .dxf
    // ========================================================================
    {
        LoadedMesh3D mDwg15, mDwg32, mDxfSubd, mDxfBin;
        bool okD15 = Model3DViewer::InspectMeshFile(tdCadDir + L"\\autocad_r2000_bunker.dwg", mDwg15);
        bool okD32 = Model3DViewer::InspectMeshFile(tdCadDir + L"\\autocad_r2018_spire.dwg", mDwg32);
        bool okSub = Model3DViewer::InspectMeshFile(tdCadDir + L"\\autocad_subd_polyface.dxf", mDxfSubd);
        bool okBin = Model3DViewer::InspectMeshFile(tdCadDir + L"\\autocad_binary_3d.dxf", mDxfBin);

        tc.Record(okD15 && mDwg15.loadedOk && mDwg15.rawVertexCount == 20 && mDwg15.rawTriangleCount == 10 &&
                  std::fabs(mDwg15.bboxMax.y - 9.0f) < 0.02f,
                  L"Stage15.3D.DwgDxf3D.SampleAutoCadR2000AC1015DWG",
                  L"verts=" + std::to_wstring(mDwg15.rawVertexCount) + L" maxY=" + std::to_wstring(mDwg15.bboxMax.y));
        tc.Record(okD32 && mDwg32.loadedOk && mDwg32.rawVertexCount == 12 && mDwg32.rawTriangleCount == 4 &&
                  std::fabs(mDwg32.bboxMax.y - 16.0f) < 0.02f &&
                  HashMeshGeometry(mDwg32) != HashMeshGeometry(mDwg15),
                  L"Stage15.3D.DwgDxf3D.SampleAutoCadR2018AC1032Lz77DWG",
                  L"verts=" + std::to_wstring(mDwg32.rawVertexCount) + L" maxY=" + std::to_wstring(mDwg32.bboxMax.y));
        tc.Record(okSub && mDxfSubd.loadedOk && mDxfSubd.rawVertexCount == 11 && mDxfSubd.rawTriangleCount == 14 &&
                  std::fabs(mDxfSubd.bboxMax.y - 11.0f) < 0.02f,
                  L"Stage15.3D.DwgDxf3D.SampleSubDMeshAndPolyfaceDXF",
                  L"verts=" + std::to_wstring(mDxfSubd.rawVertexCount) + L" tris=" + std::to_wstring(mDxfSubd.rawTriangleCount));
        tc.Record(okBin && mDxfBin.loadedOk && mDxfBin.rawVertexCount == 8 && mDxfBin.rawTriangleCount == 4 &&
                  std::fabs(mDxfBin.bboxMax.y - 12.0f) < 0.02f &&
                  Model3DViewer::IsLikely3DDxfFile(tdCadDir + L"\\autocad_binary_3d.dxf") &&
                  HashMeshGeometry(mDxfBin) != HashMeshGeometry(mDxfSubd),
                  L"Stage15.3D.DwgDxf3D.SampleBinary3DDXF",
                  L"verts=" + std::to_wstring(mDxfBin.rawVertexCount) + L" maxY=" + std::to_wstring(mDxfBin.bboxMax.y));

        assertMeshRejected(L"Stage15.3D.DwgDxf3D.RejectCorruptAC1015DWG",
            writeBytes(L"bad15.dwg", std::vector<uint8_t>(128, 'A')));
        std::vector<uint8_t> bad32(300, 0);
        std::memcpy(bad32.data(), "AC1032", 6);
        assertMeshRejected(L"Stage15.3D.DwgDxf3D.RejectCorruptAC1032DWG", writeBytes(L"bad32.dwg", bad32));
        assertMeshRejected(L"Stage15.3D.DwgDxf3D.RejectTruncatedBinaryDXF",
            writeText(L"badbin.dxf", "AutoCAD Binary DXF\r\n\x1A"));
    }

    // ========================================================================
    // Binary Parasolid (.x_b) & NURBS Surfaces in .x_t/.x_b & ACIS .sat/.sab
    // ========================================================================
    {
        LoadedMesh3D mXb, mXt, mSat, mSab;
        bool okXb  = Model3DViewer::InspectMeshFile(tdCadDir + L"\\parasolid_turbine_blade.x_b", mXb);
        bool okXt  = Model3DViewer::InspectMeshFile(tdCadDir + L"\\parasolid_nurbs_hood.x_t", mXt);
        bool okSat = Model3DViewer::InspectMeshFile(tdCadDir + L"\\acis_nurbs_wing.sat", mSat);
        bool okSab = Model3DViewer::InspectMeshFile(tdCadDir + L"\\acis_nurbs_dome.sab", mSab);

        tc.Record(okXb && mXb.loadedOk && mXb.rawVertexCount == 289 && mXb.rawTriangleCount == 512 &&
                  mXb.bboxMax.y > 19.9f && mXb.bboxMax.z > 5.2f,
                  L"Stage15.3D.ParasolidAcisNurbs.SampleBinaryParasolidXB",
                  L"verts=" + std::to_wstring(mXb.rawVertexCount) + L" maxZ=" + std::to_wstring(mXb.bboxMax.z));
        tc.Record(okXt && mXt.loadedOk && mXt.rawVertexCount == 289 && mXt.rawTriangleCount == 512 &&
                  std::fabs(mXt.bboxMax.y - 5.75f) < 0.02f && HashMeshGeometry(mXt) != HashMeshGeometry(mXb),
                  L"Stage15.3D.ParasolidAcisNurbs.SampleParasolidNurbsXT",
                  L"verts=" + std::to_wstring(mXt.rawVertexCount) + L" maxY=" + std::to_wstring(mXt.bboxMax.y));
        tc.Record(okSat && mSat.loadedOk && mSat.rawVertexCount == 289 && mSat.rawTriangleCount == 512 &&
                  mSat.bboxMax.z > 23.9f && HashMeshGeometry(mSat) != HashMeshGeometry(mXt),
                  L"Stage15.3D.ParasolidAcisNurbs.SampleAcisNurbsSAT",
                  L"verts=" + std::to_wstring(mSat.rawVertexCount) + L" maxZ=" + std::to_wstring(mSat.bboxMax.z));
        tc.Record(okSab && mSab.loadedOk && mSab.rawVertexCount == 289 && mSab.rawTriangleCount == 512 &&
                  mSab.bboxMax.y > 7.5f && HashMeshGeometry(mSab) != HashMeshGeometry(mSat),
                  L"Stage15.3D.ParasolidAcisNurbs.SampleAcisBinaryNurbsSAB",
                  L"verts=" + std::to_wstring(mSab.rawVertexCount) + L" maxY=" + std::to_wstring(mSab.bboxMax.y));

        const std::vector<uint8_t> badXb = { 'P','S',0,0, '*','*','E','N','D','_','O','F','_','H','E','A','D','E','R','*','*','\n', 0,41, 0,0,0,1, 0,2 };
        assertMeshRejected(L"Stage15.3D.ParasolidAcisNurbs.RejectTruncatedXB", writeBytes(L"bad.x_b", badXb));
        assertMeshRejected(L"Stage15.3D.ParasolidAcisNurbs.RejectTruncatedNurbsSAT",
            writeText(L"bad_nurbs.sat", "700 0 1 0\n1 1e-6 1e-10\n-0 face $-1 $-1 $-1 $-1 $1 forward #\n-1 spline-surface $-1 forward { exactsur nurbs 2 2 3 3 } #\nEnd-of-ACIS-data\n"));
    }

    // ========================================================================
    // STEP AP203/214/242 (.step, .stp, .stpz) NURBS, Spheres/Tori & Assemblies
    // ========================================================================
    {
        LoadedMesh3D mAir, mAssy, mStpz;
        bool okAir  = Model3DViewer::InspectMeshFile(tdCadDir + L"\\step_nurbs_airfoil.step", mAir);
        bool okAssy = Model3DViewer::InspectMeshFile(tdCadDir + L"\\step_sphere_torus_assembly.stp", mAssy);
        bool okStpz = Model3DViewer::InspectMeshFile(tdCadDir + L"\\step_compressed_dome.stpz", mStpz);

        tc.Record(okAir && mAir.loadedOk && mAir.rawVertexCount == 289 && mAir.rawTriangleCount == 512 &&
                  mAir.bboxMax.y > 10.0f && std::fabs(mAir.bboxMin.x + 10.0f) < 0.01f && std::fabs(mAir.bboxMax.x - 10.0f) < 0.01f &&
                  std::fabs(mAir.bboxMin.z + 30.0f) < 0.01f,
                  L"Stage15.3D.StepUpgrade.SampleRationalNurbsSTEP",
                  L"verts=" + std::to_wstring(mAir.rawVertexCount) + L" maxY=" + std::to_wstring(mAir.bboxMax.y));
        tc.Record(okAssy && mAssy.loadedOk && mAssy.rawTriangleCount > 1000 &&
                  std::fabs(mAssy.bboxMin.x - (-24.0f)) < 0.1f && std::fabs(mAssy.bboxMax.x - 29.0f) < 0.1f,
                  L"Stage15.3D.StepUpgrade.SampleSphereTorusAssemblySTP",
                  L"verts=" + std::to_wstring(mAssy.rawVertexCount) + L" minX=" + std::to_wstring(mAssy.bboxMin.x) + L" maxX=" + std::to_wstring(mAssy.bboxMax.x));
        tc.Record(okStpz && mStpz.loadedOk && mStpz.rawVertexCount == 289 && mStpz.rawTriangleCount == 512 &&
                  HashMeshGeometry(mStpz) != HashMeshGeometry(mAir),
                  L"Stage15.3D.StepUpgrade.SampleCompressedDomeSTPZ",
                  L"verts=" + std::to_wstring(mStpz.rawVertexCount) + L" maxY=" + std::to_wstring(mStpz.bboxMax.y));

        assertMeshRejected(L"Stage15.3D.StepUpgrade.RejectCorruptSTPZ",
            writeText(L"bad.stpz", "\x1F\x8B\x08\x00not_valid_gzip_stream"));
        assertMeshRejected(L"Stage15.3D.StepUpgrade.RejectEmptySTEP",
            writeText(L"empty_assy.step", "ISO-10303-21;\nHEADER;\nENDSEC;\nDATA;\nENDSEC;\nEND-ISO-10303-21;\n"));
    }

    // ========================================================================
    // SolidWorks Parts & Assemblies (.sldprt, .sldasm)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".sldprt") &&
                  Model3DViewer::Is3DModelExtension(L".sldasm"),
                  L"Stage15.3D.SolidWorks.ExtensionsRegistered");

        LoadedMesh3D mPrt, mAsm;
        bool okPrt = Model3DViewer::InspectMeshFile(tdCadDir + L"\\gearbox_cover.sldprt", mPrt);
        bool okAsm = Model3DViewer::InspectMeshFile(tdCadDir + L"\\solidworks_vtwin.sldasm", mAsm);

        tc.Record(okPrt && mPrt.loadedOk && mPrt.rawVertexCount == 8 && mPrt.rawTriangleCount == 12 &&
                  std::fabs(mPrt.bboxSize.x - 4.0f) < 0.05f && std::fabs(mPrt.bboxMax.y - 1.8f) < 0.05f &&
                  mPrt.formatName == L"Dassault SolidWorks 3D Part (.sldprt)",
                  L"Stage15.3D.SolidWorks.SampleGearboxCoverSLDPRT",
                  L"verts=" + std::to_wstring(mPrt.rawVertexCount) + L" tris=" + std::to_wstring(mPrt.rawTriangleCount));
        tc.Record(okAsm && mAsm.loadedOk && mAsm.rawVertexCount == 8 && mAsm.rawTriangleCount == 12 &&
                  std::fabs(mAsm.bboxSize.x - 6.4f) < 0.05f && std::fabs(mAsm.bboxSize.y - 2.8f) < 0.05f &&
                  mAsm.formatName == L"Dassault SolidWorks 3D Assembly (.sldasm)" &&
                  HashMeshGeometry(mAsm) != HashMeshGeometry(mPrt),
                  L"Stage15.3D.SolidWorks.SampleVTwinAssemblySLDASM",
                  L"verts=" + std::to_wstring(mAsm.rawVertexCount) + L" spanX=" + std::to_wstring(mAsm.bboxSize.x));

        assertMeshRejected(L"Stage15.3D.SolidWorks.RejectTruncatedOLE2",
            writeBytes(L"bad.sldprt", { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1, 0x00, 0x01 }));
        assertMeshRejected(L"Stage15.3D.SolidWorks.RejectGarbageSLDASM",
            writeText(L"bad.sldasm", "NOT_A_SOLIDWORKS_ASSEMBLY_CONTAINER_FILE_0000"));
    }

    // ========================================================================
    // Pixar OpenUSD (.usd, .usda, .usdc, .usdz) & Alembic Ogawa (.abc)
    // ========================================================================
    {
        LoadedMesh3D mUsda, mUsdc, mUsdz, mAbc;
        bool okUsda = Model3DViewer::InspectMeshFile(td3dDir + L"\\usd_forest_instancer.usda", mUsda);
        bool okUsdc = Model3DViewer::InspectMeshFile(td3dDir + L"\\usd_crate_turrets.usdc", mUsdc);
        bool okUsdz = Model3DViewer::InspectMeshFile(td3dDir + L"\\usd_multilayer_station.usdz", mUsdz);
        bool okAbc  = Model3DViewer::InspectMeshFile(td3dDir + L"\\alembic_instanced_rig.abc", mAbc);

        tc.Record(okUsda && mUsda.loadedOk && mUsda.rawVertexCount == 15 && mUsda.rawTriangleCount == 18 &&
                  std::fabs(mUsda.bboxMin.x - (-27.0f)) < 0.05f && std::fabs(mUsda.bboxMax.x - 32.0f) < 0.05f &&
                  std::fabs(mUsda.bboxMax.y - 31.0f) < 0.05f,
                  L"Stage15.3D.UsdAlembic.SamplePointInstancerUSDA",
                  L"verts=" + std::to_wstring(mUsda.rawVertexCount) + L" minX=" + std::to_wstring(mUsda.bboxMin.x) + L" maxY=" + std::to_wstring(mUsda.bboxMax.y));
        tc.Record(okUsdc && mUsdc.loadedOk && mUsdc.rawVertexCount == 4 && mUsdc.rawTriangleCount == 4 &&
                  std::fabs(mUsdc.bboxMin.x - 12.0f) < 0.05f && std::fabs(mUsdc.bboxMax.y - 10.0f) < 0.05f,
                  L"Stage15.3D.UsdAlembic.SampleXformCrateUSDC",
                  L"verts=" + std::to_wstring(mUsdc.rawVertexCount) + L" minX=" + std::to_wstring(mUsdc.bboxMin.x) + L" maxY=" + std::to_wstring(mUsdc.bboxMax.y));
        tc.Record(okUsdz && mUsdz.loadedOk && mUsdz.rawVertexCount == 8 && mUsdz.rawTriangleCount == 8 &&
                  std::fabs(mUsdz.bboxMin.x - (-20.0f)) < 0.05f && std::fabs(mUsdz.bboxMax.x - 24.0f) < 0.05f &&
                  std::fabs(mUsdz.bboxMax.y - 9.0f) < 0.05f && HashMeshGeometry(mUsdz) != HashMeshGeometry(mUsdc),
                  L"Stage15.3D.UsdAlembic.SampleMultiLayerUSDZ",
                  L"verts=" + std::to_wstring(mUsdz.rawVertexCount) + L" minX=" + std::to_wstring(mUsdz.bboxMin.x) + L" maxX=" + std::to_wstring(mUsdz.bboxMax.x));
        tc.Record(okAbc && mAbc.loadedOk && mAbc.rawVertexCount == 10 && mAbc.rawTriangleCount == 12 &&
                  std::fabs(mAbc.bboxMin.x - (-22.0f)) < 0.05f && std::fabs(mAbc.bboxMax.x - 22.0f) < 0.05f &&
                  std::fabs(mAbc.bboxMax.y - 16.0f) < 0.05f,
                  L"Stage15.3D.UsdAlembic.SampleHierarchicalInstancedABC",
                  L"verts=" + std::to_wstring(mAbc.rawVertexCount) + L" minX=" + std::to_wstring(mAbc.bboxMin.x) + L" maxY=" + std::to_wstring(mAbc.bboxMax.y));

        assertMeshRejected(L"Stage15.3D.UsdAlembic.RejectCorruptUSDC",
            writeBytes(L"bad_crate.usdc", { 'P','X','R','-','U','S','D','C', 0,8,0,0,0,0,0,0, 0xFF,0xFF,0xFF,0xFF }));
        assertMeshRejected(L"Stage15.3D.UsdAlembic.RejectCorruptABC",
            writeBytes(L"bad_ogawa.abc", { 'O','g','a','w','a', 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0,0,0,0, 0,0,0,0, 0,0,0,0 }));
    }

    // ========================================================================
    // VTK XML Scientific/FEM Suite (.vtu, .vtp, .vti, .vtr, .vts, .pvd)
    // ========================================================================
    {
        LoadedMesh3D mVtu, mVtp, mVti, mVtr, mVts, mPvd;
        bool okVtu = Model3DViewer::InspectMeshFile(td3dDir + L"\\fem_bracket_zlib.vtu", mVtu);
        bool okVtp = Model3DViewer::InspectMeshFile(td3dDir + L"\\aerofoil_appended_raw.vtp", mVtp);
        bool okVti = Model3DViewer::InspectMeshFile(td3dDir + L"\\plasma_core_lz4.vti", mVti);
        bool okVtr = Model3DViewer::InspectMeshFile(td3dDir + L"\\wind_tunnel.vtr", mVtr);
        bool okVts = Model3DViewer::InspectMeshFile(td3dDir + L"\\curved_duct.vts", mVts);
        bool okPvd = Model3DViewer::InspectMeshFile(td3dDir + L"\\multiblock_sim.pvd", mPvd);

        tc.Record(okVtu && mVtu.loadedOk && mVtu.rawVertexCount == 12 && mVtu.rawTriangleCount == 20 &&
                  std::fabs(mVtu.bboxMax.x - 20.0f) < 0.05f,
                  L"Stage15.3D.VtkXml.SampleZlibFemVTU",
                  L"verts=" + std::to_wstring(mVtu.rawVertexCount) + L" tris=" + std::to_wstring(mVtu.rawTriangleCount));
        tc.Record(okVtp && mVtp.loadedOk && mVtp.rawVertexCount == 5 && mVtp.rawTriangleCount == 6 &&
                  std::fabs(mVtp.bboxMax.y - 12.0f) < 0.05f,
                  L"Stage15.3D.VtkXml.SampleAppendedRawVTP",
                  L"verts=" + std::to_wstring(mVtp.rawVertexCount) + L" tris=" + std::to_wstring(mVtp.rawTriangleCount));
        tc.Record(okVti && mVti.loadedOk && mVti.rawTriangleCount >= 12 &&
                  HashMeshGeometry(mVti) != HashMeshGeometry(mVtu),
                  L"Stage15.3D.VtkXml.SampleLz4IsoVolumeVTI",
                  L"verts=" + std::to_wstring(mVti.rawVertexCount) + L" tris=" + std::to_wstring(mVti.rawTriangleCount));
        tc.Record(okVtr && mVtr.loadedOk && mVtr.rawTriangleCount >= 24 &&
                  std::fabs(mVtr.bboxMax.x - 12.0f) < 0.05f,
                  L"Stage15.3D.VtkXml.SampleRectilinearVTR",
                  L"verts=" + std::to_wstring(mVtr.rawVertexCount) + L" maxX=" + std::to_wstring(mVtr.bboxMax.x));
        tc.Record(okVts && mVts.loadedOk && mVts.rawVertexCount == 9 && mVts.rawTriangleCount == 8 &&
                  std::fabs(mVts.bboxMax.y - 5.0f) < 0.05f,
                  L"Stage15.3D.VtkXml.SampleStructuredVTS",
                  L"verts=" + std::to_wstring(mVts.rawVertexCount) + L" maxY=" + std::to_wstring(mVts.bboxMax.y));
        tc.Record(okPvd && mPvd.loadedOk && mPvd.rawVertexCount == 17 && mPvd.rawTriangleCount == 26 &&
                  mPvd.animFrameCount == 2 && std::fabs(mPvd.bboxMin.x - (-15.0f)) < 0.05f && std::fabs(mPvd.bboxMax.x - 20.0f) < 0.05f,
                  L"Stage15.3D.VtkXml.SampleMultiBlockCollectionPVD",
                  L"verts=" + std::to_wstring(mPvd.rawVertexCount) + L" tris=" + std::to_wstring(mPvd.rawTriangleCount) + L" ts=" + std::to_wstring(mPvd.animFrameCount));

        assertMeshRejected(L"Stage15.3D.VtkXml.RejectCorruptZlibVTU",
            writeText(L"bad.vtu", "<VTKFile type=\"UnstructuredGrid\" compressor=\"vtkZLibDataCompressor\"><UnstructuredGrid><Piece><Points><DataArray type=\"Float32\" format=\"binary\">!!!!not_valid_zlib!!!!</DataArray></Points></Piece></UnstructuredGrid></VTKFile>"));
        assertMeshRejected(L"Stage15.3D.VtkXml.RejectEmptyPVD",
            writeText(L"empty.pvd", "<VTKFile type=\"Collection\"><Collection></Collection></VTKFile>"));
    }

    // ========================================================================
    // 3D Gaussian Splatting (.splat, .ksplat, .spz) & OpenVDB Sparse Volumes (.vdb)
    // ========================================================================
    {
        LoadedMesh3D mSplat, mKsplat, mSpz, mVdb;
        bool okSplat  = Model3DViewer::InspectMeshFile(td3dDir + L"\\garden_bonsai.splat", mSplat);
        bool okKsplat = Model3DViewer::InspectMeshFile(td3dDir + L"\\neon_arch.ksplat", mKsplat);
        bool okSpz    = Model3DViewer::InspectMeshFile(td3dDir + L"\\scaniverse_statue.spz", mSpz);
        bool okVdb    = Model3DViewer::InspectMeshFile(td3dDir + L"\\smoke_plume.vdb", mVdb);

        tc.Record(okSplat && mSplat.loadedOk && mSplat.rawVertexCount == 30 && mSplat.rawTriangleCount == 40 &&
                  std::fabs(mSplat.bboxMax.y - 10.0f) < 0.05f,
                  L"Stage15.3D.SplatVdb.SampleAntimatterSPLAT",
                  L"verts=" + std::to_wstring(mSplat.rawVertexCount) + L" maxY=" + std::to_wstring(mSplat.bboxMax.y));
        tc.Record(okKsplat && mKsplat.loadedOk && mKsplat.rawVertexCount == 24 && mKsplat.rawTriangleCount == 32 &&
                  std::fabs(mKsplat.bboxMax.y - 11.0f) < 0.05f && HashMeshGeometry(mKsplat) != HashMeshGeometry(mSplat),
                  L"Stage15.3D.SplatVdb.SampleKelloggKSPLAT",
                  L"verts=" + std::to_wstring(mKsplat.rawVertexCount) + L" maxY=" + std::to_wstring(mKsplat.bboxMax.y));
        tc.Record(okSpz && mSpz.loadedOk && mSpz.rawVertexCount == 18 && mSpz.rawTriangleCount == 24 &&
                  std::fabs(mSpz.bboxMax.y - 16.0f) < 0.05f,
                  L"Stage15.3D.SplatVdb.SampleNianticSPZ",
                  L"verts=" + std::to_wstring(mSpz.rawVertexCount) + L" maxY=" + std::to_wstring(mSpz.bboxMax.y));
        tc.Record(okVdb && mVdb.loadedOk && mVdb.rawTriangleCount == 28 &&
                  std::fabs(mVdb.bboxMax.x - 3.0f) < 0.05f && std::fabs(mVdb.bboxMax.y - 3.0f) < 0.05f,
                  L"Stage15.3D.SplatVdb.SampleSparseVolumeVDB",
                  L"verts=" + std::to_wstring(mVdb.rawVertexCount) + L" tris=" + std::to_wstring(mVdb.rawTriangleCount));

        assertMeshRejected(L"Stage15.3D.SplatVdb.RejectAsciiTextSPLAT",
            writeText(L"fake32.splat", "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345"));
        assertMeshRejected(L"Stage15.3D.SplatVdb.RejectCorruptSPZ",
            writeBytes(L"bad.spz", { 0x1F, 0x8B, 0x08, 0x00, 0xDE, 0xAD, 0xBE, 0xEF }));
        assertMeshRejected(L"Stage15.3D.SplatVdb.RejectTruncatedVDB",
            writeBytes(L"bad.vdb", { 0x20, 'B', 'D', 'V', 0, 0, 0, 0, 224, 0, 0, 0 }));
    }

    // ========================================================================
    // GeoTIFF 3D DEM, Void-Safe SRTM (.hgt), 3D .shp/.geojson, OpenCTM (.ctm), Quake/Source .bsp, & Ogre/Medit (.mesh)
    // ========================================================================
    {
        LoadedMesh3D mHgt, mGtif, mShp, mGjson, mMg1, mMg2, mBsp, mOgre, mMedit;
        bool okHgt   = Model3DViewer::InspectMeshFile(tdGisDir + L"\\srtm_void_crater.hgt", mHgt);
        bool okGtif  = Model3DViewer::InspectMeshFile(tdGisDir + L"\\alpine_dem.tif", mGtif);
        bool okShp   = Model3DViewer::InspectMeshFile(tdGisDir + L"\\city_tower_3d.shp", mShp);
        bool okGjson = Model3DViewer::InspectMeshFile(tdGisDir + L"\\skyline_extruded.geojson", mGjson);
        bool okMg1   = Model3DViewer::InspectMeshFile(td3dDir + L"\\openctm_mg1_crystal.ctm", mMg1);
        bool okMg2   = Model3DViewer::InspectMeshFile(td3dDir + L"\\openctm_mg2_statue.ctm", mMg2);
        bool okBsp   = Model3DViewer::InspectMeshFile(td3dDir + L"\\quake3_arena_room.bsp", mBsp);
        bool okOgre  = Model3DViewer::InspectMeshFile(td3dDir + L"\\ogre_paladin_helm.mesh", mOgre);
        bool okMedit = Model3DViewer::InspectMeshFile(td3dDir + L"\\medit_fem_tetra.mesh", mMedit);

        tc.Record(okHgt && mHgt.loadedOk && mHgt.rawVertexCount == 15 && mHgt.rawTriangleCount == 17 &&
                  mHgt.bboxMin.y >= 9.9f,
                  L"Stage15.3D.BspGeo.SampleVoidSafeSRTM",
                  L"verts=" + std::to_wstring(mHgt.rawVertexCount) + L" tris=" + std::to_wstring(mHgt.rawTriangleCount));
        tc.Record(okGtif && mGtif.loadedOk && mGtif.rawVertexCount == 16 && mGtif.rawTriangleCount == 18 &&
                  std::fabs(mGtif.bboxMax.y - 45.0f) < 0.05f &&
                  Model3DViewer::IsLikely3DGeoOrDemFile(tdGisDir + L"\\alpine_dem.tif", L".tif"),
                  L"Stage15.3D.BspGeo.SampleGeoTiff3DDemTIF",
                  L"verts=" + std::to_wstring(mGtif.rawVertexCount) + L" maxY=" + std::to_wstring(mGtif.bboxMax.y));
        tc.Record(okShp && mShp.loadedOk && mShp.rawVertexCount == 8 && mShp.rawTriangleCount == 10 &&
                  std::fabs(mShp.bboxMax.y - 35.0f) < 0.05f &&
                  Model3DViewer::IsLikely3DGeoOrDemFile(tdGisDir + L"\\city_tower_3d.shp", L".shp"),
                  L"Stage15.3D.BspGeo.SamplePolygonZShapefileSHP",
                  L"verts=" + std::to_wstring(mShp.rawVertexCount) + L" maxY=" + std::to_wstring(mShp.bboxMax.y));
        tc.Record(okGjson && mGjson.loadedOk && mGjson.rawVertexCount == 8 && mGjson.rawTriangleCount == 10 &&
                  std::fabs(mGjson.bboxMax.y - 42.0f) < 0.05f &&
                  Model3DViewer::IsLikely3DGeoOrDemFile(tdGisDir + L"\\skyline_extruded.geojson", L".geojson"),
                  L"Stage15.3D.BspGeo.SampleExtruded3DGeoJSON",
                  L"verts=" + std::to_wstring(mGjson.rawVertexCount) + L" maxY=" + std::to_wstring(mGjson.bboxMax.y));
        tc.Record(okMg1 && mMg1.loadedOk && mMg1.rawVertexCount == 4 && mMg1.rawTriangleCount == 4 &&
                  std::fabs(mMg1.bboxMax.y - 9.0f) < 0.05f,
                  L"Stage15.3D.BspGeo.SampleOpenCtmMG1",
                  L"verts=" + std::to_wstring(mMg1.rawVertexCount) + L" maxY=" + std::to_wstring(mMg1.bboxMax.y));
        tc.Record(okMg2 && mMg2.loadedOk && mMg2.rawVertexCount == 5 && mMg2.rawTriangleCount == 6 &&
                  std::fabs(mMg2.bboxMax.y - 12.0f) < 0.05f && HashMeshGeometry(mMg2) != HashMeshGeometry(mMg1),
                  L"Stage15.3D.BspGeo.SampleOpenCtmMG2",
                  L"verts=" + std::to_wstring(mMg2.rawVertexCount) + L" maxY=" + std::to_wstring(mMg2.bboxMax.y));
        tc.Record(okBsp && mBsp.loadedOk && mBsp.rawTriangleCount == 34 && std::fabs(mBsp.bboxMax.y - 9.0f) < 0.05f,
                  L"Stage15.3D.BspGeo.SampleQuake3BezierIBSP",
                  L"verts=" + std::to_wstring(mBsp.rawVertexCount) + L" tris=" + std::to_wstring(mBsp.rawTriangleCount));
        tc.Record(okOgre && mOgre.loadedOk && mOgre.rawVertexCount == 5 && mOgre.rawTriangleCount == 6 &&
                  std::fabs(mOgre.bboxMax.y - 15.0f) < 0.05f,
                  L"Stage15.3D.BspGeo.SampleOgreBinaryMESH",
                  L"verts=" + std::to_wstring(mOgre.rawVertexCount) + L" maxY=" + std::to_wstring(mOgre.bboxMax.y));
        tc.Record(okMedit && mMedit.loadedOk && mMedit.rawVertexCount == 5 && mMedit.rawTriangleCount == 6 &&
                  std::fabs(mMedit.bboxMin.y - (-12.0f)) < 0.05f && std::fabs(mMedit.bboxMax.y - 12.0f) < 0.05f,
                  L"Stage15.3D.BspGeo.SampleMeditFemMESH",
                  L"verts=" + std::to_wstring(mMedit.rawVertexCount) + L" tris=" + std::to_wstring(mMedit.rawTriangleCount));

        assertMeshRejected(L"Stage15.3D.BspGeo.RejectAllVoidHGT",
            writeBytes(L"void.hgt", { 0x80,0, 0x80,0, 0x80,0,  0x80,0, 0x80,0, 0x80,0,  0x80,0, 0x80,0, 0x80,0 }));
        assertMeshRejected(L"Stage15.3D.BspGeo.RejectCorruptMg1CTM",
            writeBytes(L"bad_mg1.ctm", { 'O','C','T','M', 5,0,0,0, 'M','G','1',0, 4,0,0,0, 4,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 'I','N','D','X', 99,0,0,0 }));
        assertMeshRejected(L"Stage15.3D.BspGeo.RejectCorruptBSP",
            writeText(L"bad.bsp", "IBSP\x2E\x00\x00\x00truncated_lumps"));
    }

    // ========================================================================
    // Valve Source Engine Vertex Data (.vvd, IDSV v4 + LOD fixups + .dx90.vtx)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".vvd"),
                  L"Stage15.3D.ValveVVD_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\combine_soldier.vvd", m1);
        // 66 LOD0 fixup vertices emitted as 66 octahedral point-cloud markers (6 verts, 8 tris each)
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 66 * 6 && m1.rawTriangleCount == 66 * 8 &&
                  m1.bboxSize.x > 30.0f && m1.bboxSize.y > 20.0f,
                  L"Stage15.3D.ValveVVD_SampleFixupLod0Parsed");

        std::vector<uint8_t> vvdPanel;
        auto pushI32 = [&](std::vector<uint8_t>& b, int32_t v) { PushU32LE(b, static_cast<uint32_t>(v)); };
        vvdPanel.insert(vvdPanel.end(), { 'I', 'D', 'S', 'V' });
        pushI32(vvdPanel, 4);
        pushI32(vvdPanel, 0x13572468);
        pushI32(vvdPanel, 1);
        pushI32(vvdPanel, 4);
        for (int i = 1; i < 8; ++i) pushI32(vvdPanel, 0);
        pushI32(vvdPanel, 0);
        pushI32(vvdPanel, 0);
        pushI32(vvdPanel, 64);
        pushI32(vvdPanel, 0);
        const float quadPts[4][3] = {
            { -5.0f, 0.0f,  0.0f },
            {  5.0f, 0.0f,  0.0f },
            {  5.0f, 0.0f, 12.0f },
            { -5.0f, 0.0f, 12.0f }
        };
        for (int i = 0; i < 4; ++i) {
            PushF32LE(vvdPanel, 1.0f); PushF32LE(vvdPanel, 0.0f); PushF32LE(vvdPanel, 0.0f);
            vvdPanel.insert(vvdPanel.end(), { 0, 0, 0, 1 });
            PushF32LE(vvdPanel, quadPts[i][0]); PushF32LE(vvdPanel, quadPts[i][1]); PushF32LE(vvdPanel, quadPts[i][2]);
            PushF32LE(vvdPanel, 0.0f); PushF32LE(vvdPanel, -1.0f); PushF32LE(vvdPanel, 0.0f);
            PushF32LE(vvdPanel, 0.0f); PushF32LE(vvdPanel, 0.0f);
        }
        std::wstring pVvdPanel = writeBytes(L"shield_panel.vvd", vvdPanel);

        std::vector<uint8_t> vtxPanel;
        pushI32(vtxPanel, 7);
        pushI32(vtxPanel, 16);
        PushU16LE(vtxPanel, 3);
        PushU16LE(vtxPanel, 3);
        pushI32(vtxPanel, 3);
        pushI32(vtxPanel, 0x13572468);
        pushI32(vtxPanel, 1);
        pushI32(vtxPanel, 0);
        pushI32(vtxPanel, 1);
        pushI32(vtxPanel, 36);
        pushI32(vtxPanel, 1); pushI32(vtxPanel, 8);
        pushI32(vtxPanel, 1); pushI32(vtxPanel, 8);
        pushI32(vtxPanel, 1); pushI32(vtxPanel, 12); PushF32LE(vtxPanel, 0.0f);
        pushI32(vtxPanel, 1); pushI32(vtxPanel, 9); vtxPanel.push_back(0);
        pushI32(vtxPanel, 4); pushI32(vtxPanel, 25); pushI32(vtxPanel, 6); pushI32(vtxPanel, 61);
        pushI32(vtxPanel, 0); pushI32(vtxPanel, 0); vtxPanel.push_back(0);
        for (uint16_t vi = 0; vi < 4; ++vi) {
            vtxPanel.insert(vtxPanel.end(), { 0, 0, 0, 1 });
            PushU16LE(vtxPanel, vi);
            vtxPanel.insert(vtxPanel.end(), { 0, 0, 0 });
        }
        for (uint16_t idx : { uint16_t(0), uint16_t(1), uint16_t(2), uint16_t(0), uint16_t(2), uint16_t(3) }) {
            PushU16LE(vtxPanel, idx);
        }
        writeBytes(L"shield_panel.dx90.vtx", vtxPanel);

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(pVvdPanel, m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 4 && m2.rawTriangleCount == 2 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.ValveVVD_CompanionVtxAndDistinctGeometry");

        std::vector<uint8_t> badMagic = vvdPanel;
        badMagic[0] = 'X';
        assertMeshRejected(L"Stage15.3D.ValveVVD_RejectBadMagic", writeBytes(L"bad_magic.vvd", badMagic));

        std::vector<uint8_t> truncVvd(vvdPanel.begin(), vvdPanel.begin() + 80);
        assertMeshRejected(L"Stage15.3D.ValveVVD_RejectTruncated", writeBytes(L"trunc.vvd", truncVvd));

        std::vector<uint8_t> badFix = vvdPanel;
        badFix[48] = 1; badFix[52] = 64; badFix[56] = 96;
        assertMeshRejected(L"Stage15.3D.ValveVVD_RejectBadFixupBounds", writeBytes(L"bad_fixup.vvd", badFix));
    }

    // ========================================================================
    // Raven Software Ghoul 2 Skeletal Mesh (.glm, "2LGM" v6)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".glm"),
                  L"Stage15.3D.RavenGLM_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\jedi_saber_hilt.glm", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 34 && m1.rawTriangleCount == 48 &&
                  m1.bboxSize.y > 28.0f && m1.bboxSize.x > 6.5f,
                  L"Stage15.3D.RavenGLM_MultiSurfaceSampleParsed");

        auto buildMiniGlm = [&](float topZ) -> std::vector<uint8_t> {
            std::vector<uint8_t> surf;
            auto pushI32 = [&](std::vector<uint8_t>& b, int32_t v) { PushU32LE(b, static_cast<uint32_t>(v)); };
            const int32_t ofsTris = 40;
            const int32_t ofsVerts = 40 + 4 * 12;
            const int32_t ofsBones = ofsVerts + 4 * 32 + 4 * 8;
            const int32_t surfEnd = ofsBones + 4;
            pushI32(surf, 3); pushI32(surf, 0); pushI32(surf, -40);
            pushI32(surf, 4); pushI32(surf, ofsVerts);
            pushI32(surf, 4); pushI32(surf, ofsTris);
            pushI32(surf, 1); pushI32(surf, ofsBones); pushI32(surf, surfEnd);
            const int tris[4][3] = { {0,1,2}, {0,1,3}, {1,2,3}, {2,0,3} };
            for (const auto& t : tris) { pushI32(surf, t[0]); pushI32(surf, t[1]); pushI32(surf, t[2]); }
            const float pts[4][3] = { {-3, -2, 0}, {3, -2, 0}, {0, 4, 0}, {0, 0, topZ} };
            for (const auto& p : pts) {
                PushF32LE(surf, 0.0f); PushF32LE(surf, 0.0f); PushF32LE(surf, 1.0f);
                PushF32LE(surf, p[0]); PushF32LE(surf, p[1]); PushF32LE(surf, p[2]);
                PushU32LE(surf, 0);
                surf.insert(surf.end(), { 255, 0, 0, 0 });
            }
            for (int i = 0; i < 4; ++i) { PushF32LE(surf, 0.0f); PushF32LE(surf, 0.0f); }
            pushI32(surf, 0);

            std::vector<uint8_t> glm;
            glm.insert(glm.end(), { '2', 'L', 'G', 'M' });
            pushI32(glm, 6);
            glm.resize(glm.size() + 128, 0);
            pushI32(glm, 0);
            pushI32(glm, 1);
            pushI32(glm, 1);
            pushI32(glm, 180);
            pushI32(glm, 1);
            pushI32(glm, 164);
            const int32_t lodLen = 8 + static_cast<int32_t>(surf.size());
            pushI32(glm, 180 + lodLen);
            glm.resize(180, 0);
            pushI32(glm, lodLen);
            pushI32(glm, 4);
            glm.insert(glm.end(), surf.begin(), surf.end());
            return glm;
        };

        std::vector<uint8_t> glmTet = buildMiniGlm(9.5f);
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"tetra.glm", glmTet), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 4 && m2.rawTriangleCount == 4 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.RavenGLM_DistinctSecondMesh");

        std::vector<uint8_t> badVer = glmTet;
        badVer[4] = 99;
        assertMeshRejected(L"Stage15.3D.RavenGLM_RejectBadVersion", writeBytes(L"bad_ver.glm", badVer));

        std::vector<uint8_t> truncGlm(glmTet.begin(), glmTet.begin() + 190);
        assertMeshRejected(L"Stage15.3D.RavenGLM_RejectTruncated", writeBytes(L"trunc.glm", truncGlm));

        std::vector<uint8_t> badIdx = glmTet;
        badIdx[228] = 0xE7; badIdx[229] = 0x03;
        assertMeshRejected(L"Stage15.3D.RavenGLM_RejectOutOfBoundsTriIndex", writeBytes(L"bad_idx.glm", badIdx));
    }

    // ========================================================================
    // Unreal Engine 1 / UT99 / Deus Ex Vertex Mesh (.3d — _d.3d & _a.3d)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".3d"),
                  L"Stage15.3D.Unreal3D_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\unreal_skith_a.3d", m1);
        // Standalone _a.3d: 36 11-11-10 vertices emitted as 36 octahedral point-cloud markers (6 verts, 8 tris each)
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 36 * 6 && m1.rawTriangleCount == 36 * 8 &&
                  m1.animFrameCount == 2 && m1.bboxSize.x > 50.0f && m1.bboxSize.y > 45.0f,
                  L"Stage15.3D.Unreal3D_SampleAniv11_11_10Parsed");

        std::vector<uint8_t> dBytes(48, 0);
        dBytes[0] = 4; dBytes[1] = 0;
        dBytes[2] = 4; dBytes[3] = 0;
        const uint16_t tetTris[4][3] = { {0,1,2}, {0,1,3}, {1,2,3}, {2,0,3} };
        for (const auto& t : tetTris) {
            PushU16LE(dBytes, t[0]); PushU16LE(dBytes, t[1]); PushU16LE(dBytes, t[2]);
            dBytes.resize(dBytes.size() + 10, 0);
        }

        std::wstring orphanD = writeBytes(L"orphan_d.3d", dBytes);
        assertMeshRejected(L"Stage15.3D.Unreal3D_RejectOrphanDataWithoutAniv", orphanD);

        std::wstring pData = writeBytes(L"dispersion_d.3d", dBytes);
        std::vector<uint8_t> aBytes;
        PushU16LE(aBytes, 3);
        PushU16LE(aBytes, 32);
        const int16_t dxPts[4][4] = {
            { -2560, -1536,     0, 0 },
            {  2560, -1536,     0, 0 },
            {     0,  3072,     0, 0 },
            {     0,     0,  4608, 0 }
        };
        for (int f = 0; f < 3; ++f) {
            for (const auto& p : dxPts) {
                PushU16LE(aBytes, static_cast<uint16_t>(p[0]));
                PushU16LE(aBytes, static_cast<uint16_t>(p[1]));
                PushU16LE(aBytes, static_cast<uint16_t>(p[2] + f * 256));
                PushU16LE(aBytes, 0);
            }
        }
        std::wstring pAniv = writeBytes(L"dispersion_a.3d", aBytes);

        LoadedMesh3D mFromD, mFromA;
        bool okD = Model3DViewer::InspectMeshFile(pData, mFromD);
        bool okA = Model3DViewer::InspectMeshFile(pAniv, mFromA);
        tc.Record(okD && okA && mFromD.loadedOk && mFromA.loadedOk &&
                  mFromD.rawVertexCount == 4 && mFromD.rawTriangleCount == 4 && mFromD.animFrameCount == 3 &&
                  HashMeshGeometry(mFromD) == HashMeshGeometry(mFromA) &&
                  HashMeshGeometry(mFromD) != HashMeshGeometry(m1),
                  L"Stage15.3D.Unreal3D_PairedDataAndDeusEx64Aniv");

        std::vector<uint8_t> truncA(aBytes.begin(), aBytes.begin() + 20);
        assertMeshRejected(L"Stage15.3D.Unreal3D_RejectTruncatedAniv", writeBytes(L"trunc_a.3d", truncA));

        std::vector<uint8_t> badPolyD = dBytes;
        badPolyD[48] = 99;
        writeBytes(L"badpoly_a.3d", aBytes);
        assertMeshRejected(L"Stage15.3D.Unreal3D_RejectOutOfRangeVertexIdx", writeBytes(L"badpoly_d.3d", badPolyD));
    }

    // ========================================================================
    // Motion Analysis HTR / GTR Skeletal Motion Capture (.htr)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".htr"),
                  L"Stage15.3D.MotionHTR_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\mocap_acrobat.htr", m1);
        tc.Record(ok1 && m1.loadedOk && m1.animFrameCount == 3 && m1.rawTriangleCount > 80 &&
                  m1.bboxSize.y > 900.0f && m1.bboxSize.x > 400.0f,
                  L"Stage15.3D.MotionHTR_SampleHierarchyAndKinematicsParsed");

        const char* gtrGlobal =
            "[Header]\n"
            "FileType \"gtr\"\n"
            "DataType \"GTRS\"\n"
            "NumSegments 3\n"
            "NumFrames 5\n"
            "RotationUnits Radians\n"
            "GlobalAxisofGravity Z\n"
            "BoneLengthAxis X\n"
            "[SegmentNames&Hierarchy]\n"
            "Base GLOBAL\n"
            "Boom Base\n"
            "Claw Boom\n"
            "[BasePosition]\n"
            "Base  0 0  0   0 0 0  25\n"
            "Boom 25 0 40   0 0 0  35\n"
            "Claw 60 5 55   0 0 0  18\n"
            "[EndOfFile]\n";
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeText(L"robot_arm.htr", gtrGlobal), m2);
        tc.Record(ok2 && m2.loadedOk && m2.animFrameCount == 5 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.MotionHTR_GlobalGtrsVariantAndDistinctHash");

        assertMeshRejected(L"Stage15.3D.MotionHTR_RejectMissingHierarchy",
            writeText(L"no_hier.htr", "[Header]\nDataType HTRS\nNumSegments 2\n"));
        assertMeshRejected(L"Stage15.3D.MotionHTR_RejectCyclicParents",
            writeText(L"cycle.htr", "[Header]\nDataType HTRS\n[SegmentNames&Hierarchy]\nA B\nB A\n"));
        assertMeshRejected(L"Stage15.3D.MotionHTR_RejectBinaryGarbage",
            writeBytes(L"bin.htr", { '[', 'H', 'e', 'a', 'd', 'e', 'r', ']', 0, 1, 2, 3, 4 }));
    }

    // ========================================================================
    // Acclaim Skeleton & Motion Capture (.asf / .amc — CMU Mocap)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".asf") && Model3DViewer::Is3DModelExtension(L".amc"),
                  L"Stage15.3D.AcclaimASF_ExtensionsRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\cmu_runner.asf", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawTriangleCount > 100 &&
                  m1.bboxSize.y > 55.0f && m1.bboxSize.x > 20.0f,
                  L"Stage15.3D.AcclaimASF_SampleSkeletonParsed");

        const char* asf07 =
            ":version 1.10\n"
            ":units\n  length 1.0\n  angle deg\n"
            ":root\n  order TX TY TZ RX RY RZ\n  axis XYZ\n  position 0 0 0\n  orientation 0 0 0\n"
            ":bonedata\n"
            "  begin\n    id 1\n    name upper\n    direction 0 1 0\n    length 12.0\n    axis 0 0 30 XYZ\n    dof rx rz\n  end\n"
            "  begin\n    id 2\n    name forearm\n    direction 1 0 0\n    length 9.0\n    axis 0 0 0 XYZ\n    dof ry\n  end\n"
            ":hierarchy\n  begin\n    root upper\n    upper forearm\n  end\n";
        const char* amc07 =
            ":FULLY-SPECIFIED\n:DEGREES\n"
            "1\nroot 2.0 10.0 -1.0 0.0 15.0 0.0\nupper 35.0 -20.0\nforearm 45.0\n"
            "2\nroot 2.5 10.2 -1.0 0.0 18.0 0.0\nupper 40.0 -22.0\nforearm 50.0\n"
            "3\nroot 3.0 10.4 -1.0 0.0 21.0 0.0\nupper 45.0 -25.0\nforearm 55.0\n"
            "4\nroot 3.5 10.6 -1.0 0.0 24.0 0.0\nupper 50.0 -28.0\nforearm 60.0\n";
        std::wstring pAsf07 = writeText(L"07.asf", asf07);
        std::wstring pAmc07 = writeText(L"07_01.amc", amc07);

        LoadedMesh3D mRest, mPosed;
        bool okRest  = Model3DViewer::InspectMeshFile(pAsf07, mRest);
        bool okPosed = Model3DViewer::InspectMeshFile(pAmc07, mPosed);
        tc.Record(okRest && okPosed && mRest.loadedOk && mPosed.loadedOk &&
                  mPosed.animFrameCount == 4 &&
                  HashMeshGeometry(mRest) != HashMeshGeometry(mPosed) &&
                  HashMeshGeometry(mPosed) != HashMeshGeometry(m1),
                  L"Stage15.3D.AcclaimASF_AmcSubjectPairingAndPoseKinematics");

        assertMeshRejected(L"Stage15.3D.AcclaimASF_RejectOrphanAmc", writeText(L"99_01.amc", amc07));
        assertMeshRejected(L"Stage15.3D.AcclaimASF_RejectWmvBinaryGuid",
            writeBytes(L"video.asf", { 0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C, 0x00, 0x00 }));
        assertMeshRejected(L"Stage15.3D.AcclaimASF_RejectMissingHierarchy",
            writeText(L"incomplete.asf", ":version 1.10\n:bonedata\nbegin\nname b1\ndirection 0 1 0\nlength 5\nend\n"));
    }

    // ========================================================================
    // TetGen Piecewise Linear Complex Surface Mesh (.smesh)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".smesh"),
                  L"Stage15.3D.TetGenSMESH_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\turbine_plc.smesh", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawTriangleCount == 36 &&
                  m1.bboxSize.x > 25.0f && m1.bboxSize.z > 20.0f,
                  L"Stage15.3D.TetGenSMESH_SampleQuadsAndTrisParsed");

        writeText(L"prism_ext.node",
                  "5 3 0 0\n"
                  "0 -4.0  0.0 -4.0\n"
                  "1  4.0  0.0 -4.0\n"
                  "2  4.0  0.0  4.0\n"
                  "3 -4.0  0.0  4.0\n"
                  "4  0.0  9.0  0.0\n");
        std::wstring pExtSmesh = writeText(L"prism_ext.smesh",
                  "0 3 0 0\n"
                  "5 0\n"
                  "4 0 3 2 1\n"
                  "3 0 1 4\n"
                  "3 1 2 4\n"
                  "3 2 3 4\n"
                  "3 3 0 4\n");
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(pExtSmesh, m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount == 6 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.TetGenSMESH_ExternalNodeCompanionAndZeroBased");

        assertMeshRejected(L"Stage15.3D.TetGenSMESH_RejectMissingCompanionNode",
            writeText(L"orphan_ext.smesh", "0 3 0 0\n1 0\n3 0 1 2\n"));
        assertMeshRejected(L"Stage15.3D.TetGenSMESH_RejectInvalidFacetNodeId",
            writeText(L"bad_nid.smesh", "3 3\n1 0 0 0\n2 1 0 0\n3 0 1 0\n1 0\n3 1 2 99\n"));
        assertMeshRejected(L"Stage15.3D.TetGenSMESH_RejectTruncatedNodes",
            writeText(L"trunc.smesh", "10 3\n1 0 0 0\n2 1 0 0\n"));
    }

    // ========================================================================
    // POV-Ray Scene Description Mesh (.pov — mesh, mesh2, #declare, CSG)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".pov"),
                  L"Stage15.3D.PovRayPOV_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\crystal_chalice.pov", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawTriangleCount == 77 &&
                  m1.bboxSize.y > 22.0f && m1.bboxSize.x > 16.0f,
                  L"Stage15.3D.PovRayPOV_SampleMeshMesh2AndCylinderParsed");

        const char* povPedestal =
            "/* POV-Ray analytic CSG scene */\n"
            "box { <-10, 0, -5>, <10, 2, 5> rotate <0, 30, 0> }\n"
            "sphere { <0, 8, 0>, 4.5 scale <1.5, 1.0, 1.0> }\n";
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeText(L"pedestal.pov", povPedestal), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount > 100 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.PovRayPOV_AnalyticBoxSphereAndDistinctHash");

        assertMeshRejected(L"Stage15.3D.PovRayPOV_RejectCameraOnlyNoGeometry",
            writeText(L"cam_only.pov", "camera { location <0,5,-10> look_at <0,0,0> }\nlight_source { <1,2,3> rgb 1 }\n"));
        assertMeshRejected(L"Stage15.3D.PovRayPOV_RejectOutOfRangeMesh2Face",
            writeText(L"bad_m2.pov", "mesh2 { vertex_vectors { 3, <0,0,0>, <1,0,0>, <0,1,0> } face_indices { 1, <0, 1, 99> } }\n"));
        assertMeshRejected(L"Stage15.3D.PovRayPOV_RejectBinaryGarbage",
            writeBytes(L"garbage.pov", { 'm', 'e', 's', 'h', 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }));
    }

    // ========================================================================
    // Pixar RenderMan Interface Bytestream (.rib — RISpec 3.2)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".rib"),
                  L"Stage15.3D.RenderManRIB_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\pixar_pavilion.rib", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawTriangleCount > 1500 &&
                  m1.bboxSize.x > 22.0f && m1.bboxSize.y > 25.0f,
                  L"Stage15.3D.RenderManRIB_SampleTransformStackAndQuadricsParsed");

        const char* ribHoled =
            "WorldBegin\n"
            "  PointsGeneralPolygons [ 2 ] [ 4 4 ]\n"
            "    [ 0 1 2 3   4 5 6 7 ]\n"
            "    \"vertex point P\" [\n"
            "      -6 -6 0   6 -6 0   6  6 0  -6  6 0\n"
            "      -2 -2 0   2 -2 0   2  2 0  -2  2 0\n"
            "    ]\n"
            "  Translate 0 0 -5\n"
            "  Cone 8.0 3.0 360\n"
            "WorldEnd\n";
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeText(L"holed_frame.rib", ribHoled), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount > 100 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.RenderManRIB_GeneralPolygonHoleAndCone");

        assertMeshRejected(L"Stage15.3D.RenderManRIB_RejectHeaderOnly",
            writeText(L"empty.rib", "##RenderMan RIB\nDisplay \"out.tif\" \"file\" \"rgb\"\nWorldBegin\nWorldEnd\n"));
        assertMeshRejected(L"Stage15.3D.RenderManRIB_RejectBadPolygonIndex",
            writeText(L"bad_idx.rib", "PointsPolygons [ 3 ] [ 0 1 99 ] \"P\" [ 0 0 0  1 0 0  0 1 0 ]\n"));
        assertMeshRejected(L"Stage15.3D.RenderManRIB_RejectBinaryGarbage",
            writeBytes(L"bin.rib", { '#', '#', 'R', 'I', 'B', 0, 0xFF, 0xFE, 1, 2, 3, 4, 5, 6 }));
    }

    // ========================================================================
    // LBNL Radiance Lighting & Daylighting Scene (.rad)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".rad"),
                  L"Stage15.3D.RadianceRAD_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\atrium_daylight.rad", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawTriangleCount > 400 &&
                  m1.bboxSize.x > 15.0f && m1.bboxSize.y > 8.0f,
                  L"Stage15.3D.RadianceRAD_SampleAliasQuadricsAndGenboxParsed");

        const char* radHex =
            "void plastic terracotta 0 0 5 0.6 0.2 0.1 0 0\n"
            "terracotta polygon hex_canopy 0 0 18\n"
            "  4 0 2   2 3.46 2  -2 3.46 2  -4 0 2  -2 -3.46 2   2 -3.46 2\n";
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeText(L"hex_roof.rad", radHex), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount == 4 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.RadianceRAD_PolygonOnlyAndDistinctHash");

        assertMeshRejected(L"Stage15.3D.RadianceRAD_RejectMaterialsOnly",
            writeText(L"mat_only.rad", "void plastic white 0 0 5 0.8 0.8 0.8 0 0\n"));
        assertMeshRejected(L"Stage15.3D.RadianceRAD_RejectTruncatedRealArgs",
            writeText(L"bad_args.rad", "void polygon tri 0 0 9  0 0 0  1 0 0\n"));
        assertMeshRejected(L"Stage15.3D.RadianceRAD_RejectBinaryGarbage",
            writeBytes(L"bin.rad", { 'v', 'o', 'i', 'd', 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 }));
    }

    // ========================================================================
    // Minddesk Qubicle Binary Voxel Model (.qb, v1.1.0.0 raw & RLE)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".qb"),
                  L"Stage15.3D.QubicleQB_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\mech_walker.qb", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawTriangleCount > 100 &&
                  std::fabs(m1.bboxSize.x - 8.0f) < 0.01f &&
                  std::fabs(m1.bboxSize.y - 11.0f) < 0.01f &&
                  std::fabs(m1.bboxSize.z - 6.0f) < 0.01f,
                  L"Stage15.3D.QubicleQB_MultiMatrixRleSampleParsed");

        std::vector<uint8_t> qbRaw;
        PushU32LE(qbRaw, 0x00000101u);
        PushU32LE(qbRaw, 0);
        PushU32LE(qbRaw, 0);
        PushU32LE(qbRaw, 0);
        PushU32LE(qbRaw, 0);
        PushU32LE(qbRaw, 1);
        qbRaw.push_back(3); qbRaw.insert(qbRaw.end(), { 'B', 'a', 'r' });
        PushU32LE(qbRaw, 2); PushU32LE(qbRaw, 1); PushU32LE(qbRaw, 1);
        PushU32LE(qbRaw, 0); PushU32LE(qbRaw, 0); PushU32LE(qbRaw, 0);
        PushU32LE(qbRaw, 0xFF112233u);
        PushU32LE(qbRaw, 0xFF445566u);
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"bar_raw.qb", qbRaw), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount == 20 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.QubicleQB_UncompressedHiddenFaceCulling");

        std::vector<uint8_t> badVer = qbRaw;
        badVer[0] = 99;
        assertMeshRejected(L"Stage15.3D.QubicleQB_RejectBadVersion", writeBytes(L"bad_ver.qb", badVer));

        std::vector<uint8_t> truncQb(qbRaw.begin(), qbRaw.end() - 4);
        assertMeshRejected(L"Stage15.3D.QubicleQB_RejectTruncatedMatrix", writeBytes(L"trunc.qb", truncQb));

        std::vector<uint8_t> rleOvf = qbRaw;
        rleOvf[12] = 1;
        rleOvf.resize(24 + 4 + 24);
        PushU32LE(rleOvf, 2u);
        PushU32LE(rleOvf, 9999u);
        PushU32LE(rleOvf, 0xFFFFFFFFu);
        assertMeshRejected(L"Stage15.3D.QubicleQB_RejectRleSliceOverflow", writeBytes(L"rle_ovf.qb", rleOvf));
    }

    // ========================================================================
    // Ken Silverman Build Engine / Slab6 Voxel Model (.kvx)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".kvx"),
                  L"Stage15.3D.BuildKVX_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\build_medkit.kvx", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawTriangleCount > 150 &&
                  std::fabs(m1.bboxSize.x - 8.0f) < 0.01f &&
                  std::fabs(m1.bboxSize.y - 6.0f) < 0.01f &&
                  std::fabs(m1.bboxSize.z - 6.0f) < 0.01f,
                  L"Stage15.3D.BuildKVX_SampleColumnSlabsParsed");

        std::vector<uint8_t> kvxPillar;
        PushU32LE(kvxPillar, 42u);
        PushU32LE(kvxPillar, 1); PushU32LE(kvxPillar, 1); PushU32LE(kvxPillar, 3);
        PushU32LE(kvxPillar, 128); PushU32LE(kvxPillar, 128); PushU32LE(kvxPillar, 384);
        PushU32LE(kvxPillar, 12); PushU32LE(kvxPillar, 18);
        PushU16LE(kvxPillar, 0);  PushU16LE(kvxPillar, 6);
        kvxPillar.insert(kvxPillar.end(), { 0, 3, 0x3F, 10, 20, 30 });
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"pillar.kvx", kvxPillar), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount == 28 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.BuildKVX_PillarExactFaceCountAndDistinctHash");

        std::vector<uint8_t> badXoff = kvxPillar;
        badXoff[28] = 99;
        assertMeshRejected(L"Stage15.3D.BuildKVX_RejectBrokenXoffsetInvariant", writeBytes(L"bad_xoff.kvx", badXoff));

        std::vector<uint8_t> truncKvx(kvxPillar.begin(), kvxPillar.begin() + 30);
        assertMeshRejected(L"Stage15.3D.BuildKVX_RejectTruncatedMip0", writeBytes(L"trunc.kvx", truncKvx));

        std::vector<uint8_t> badZ = kvxPillar;
        badZ[40] = 5;
        assertMeshRejected(L"Stage15.3D.BuildKVX_RejectSlabZOutOfBounds", writeBytes(L"bad_z.kvx", badZ));
    }

    // ========================================================================
    // Ken Silverman Voxlap / Ace of Spades Voxel Model (.kv6, "Kvxl")
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".kv6"),
                  L"Stage15.3D.VoxlapKV6_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\voxlap_turret.kv6", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawTriangleCount > 150 &&
                  std::fabs(m1.bboxSize.x - 5.0f) < 0.01f &&
                  std::fabs(m1.bboxSize.y - 5.0f) < 0.01f &&
                  std::fabs(m1.bboxSize.z - 8.0f) < 0.01f,
                  L"Stage15.3D.VoxlapKV6_SampleTurretParsed");

        std::vector<uint8_t> kv6Single;
        kv6Single.insert(kv6Single.end(), { 'K', 'v', 'x', 'l' });
        PushU32LE(kv6Single, 1); PushU32LE(kv6Single, 1); PushU32LE(kv6Single, 1);
        PushF32LE(kv6Single, 0.5f); PushF32LE(kv6Single, 0.5f); PushF32LE(kv6Single, 0.5f);
        PushU32LE(kv6Single, 1);
        kv6Single.insert(kv6Single.end(), { 255, 128, 64, 128 });
        PushU16LE(kv6Single, 0);
        kv6Single.push_back(0x3F); kv6Single.push_back(0);
        PushU32LE(kv6Single, 1);
        PushU16LE(kv6Single, 1);
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"cube.kv6", kv6Single), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount == 12 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.VoxlapKV6_SingleCubeAndDistinctHash");

        std::vector<uint8_t> badMag = kv6Single;
        badMag[0] = 'X';
        assertMeshRejected(L"Stage15.3D.VoxlapKV6_RejectBadMagic", writeBytes(L"bad_mag.kv6", badMag));

        std::vector<uint8_t> badXlen = kv6Single;
        badXlen[40] = 9;
        assertMeshRejected(L"Stage15.3D.VoxlapKV6_RejectMismatchedXlenSum", writeBytes(L"bad_xlen.kv6", badXlen));

        std::vector<uint8_t> badZpos = kv6Single;
        badZpos[36] = 5;
        assertMeshRejected(L"Stage15.3D.VoxlapKV6_RejectZposOutOfBounds", writeBytes(L"bad_zpos.kv6", badZpos));
    }

    // ========================================================================
    // Westwood Studios C&C Voxel Model (.vxl, "Voxel Animation")
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".vxl"),
                  L"Stage15.3D.WestwoodVXL_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\ra2_grizzly_tank.vxl", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawTriangleCount > 250 &&
                  m1.bboxSize.x > 13.0f && m1.bboxSize.y > 4.5f,
                  L"Stage15.3D.WestwoodVXL_MultiLimbTankSampleParsed");

        std::vector<uint8_t> vxlCube(802, 0);
        std::memcpy(vxlCube.data(), "Voxel Animation", 15);
        vxlCube[16] = 1;
        vxlCube[20] = 1;
        vxlCube[24] = 1;
        vxlCube[28] = 13;
        vxlCube.resize(802 + 28, 0);
        std::memcpy(&vxlCube[802], "CRATE", 5);
        PushU32LE(vxlCube, 0);
        PushU32LE(vxlCube, 4);
        vxlCube.insert(vxlCube.end(), { 0, 1, 80, 10, 1 });
        PushU32LE(vxlCube, 0); PushU32LE(vxlCube, 4); PushU32LE(vxlCube, 8);
        PushF32LE(vxlCube, 1.0f);
        const float id3x4[12] = { 1,0,0,0,  0,1,0,0,  0,0,1,0 };
        for (float f : id3x4) PushF32LE(vxlCube, f);
        PushF32LE(vxlCube, 0.0f); PushF32LE(vxlCube, 0.0f); PushF32LE(vxlCube, 0.0f);
        PushF32LE(vxlCube, 2.0f); PushF32LE(vxlCube, 2.0f); PushF32LE(vxlCube, 2.0f);
        vxlCube.insert(vxlCube.end(), { 1, 1, 1, 2 });

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"crate.vxl", vxlCube), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount == 12 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.WestwoodVXL_SingleCubeAndDistinctHash");

        std::vector<uint8_t> badMag = vxlCube;
        badMag[0] = 'X';
        assertMeshRejected(L"Stage15.3D.WestwoodVXL_RejectBadMagic", writeBytes(L"bad_mag.vxl", badMag));

        std::vector<uint8_t> badTailCnt = vxlCube;
        badTailCnt[24] = 2;
        assertMeshRejected(L"Stage15.3D.WestwoodVXL_RejectTailerCountMismatch", writeBytes(L"bad_tc.vxl", badTailCnt));

        std::vector<uint8_t> badDup = vxlCube;
        badDup[802 + 28 + 12] = 99;
        assertMeshRejected(L"Stage15.3D.WestwoodVXL_RejectCorruptSpanTrailer", writeBytes(L"bad_dup.vxl", badDup));
    }

    // ========================================================================
    // Caligari TrueSpace 3D Object (.cob — Binary BLH & ASCII ALH + F_HOLE)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".cob"),
                  L"Stage15.3D.TrueSpaceCOB_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\truespace_shuttle.cob", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawTriangleCount == 16 &&
                  m1.bboxSize.x > 7.5f && m1.bboxSize.z > 19.0f && m1.bboxSize.y > 9.0f,
                  L"Stage15.3D.TrueSpaceCOB_BinaryPolhGrouAndHoleParsed");

        const char* asciiCob =
            "Caligari V00.01ALH             \n"
            "PolH V0.04 Id 1 Parent 0 Size 00000250\n"
            "Name Pyramid\n"
            "Transform\n"
            "1 0 0 0\n0 1 0 0\n0 0 1 0\n0 0 0 1\n"
            "World Vertices 4\n"
            " 0.0  0.0  6.0\n"
            "-3.0 -3.0  0.0\n"
            " 3.0 -3.0  0.0\n"
            " 0.0  4.0  0.0\n"
            "Faces 4\n"
            "Face verts 3 flags 0 mat 0\n<0,0> <1,0> <2,0>\n"
            "Face verts 3 flags 0 mat 0\n<0,0> <2,0> <3,0>\n"
            "Face verts 3 flags 0 mat 0\n<0,0> <3,0> <1,0>\n"
            "Face verts 3 flags 0 mat 0\n<1,0> <3,0> <2,0>\n"
            "END  V1.00 Id 2 Parent 0 Size 00000000\n";
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeText(L"pyramid_ascii.cob", asciiCob), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount == 4 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.TrueSpaceCOB_AsciiModeAndDistinctHash");

        assertMeshRejected(L"Stage15.3D.TrueSpaceCOB_RejectBadMagic",
            writeText(L"bad_sig.cob", "NotCaligari V00.01ALH          \nEND V1.00 Id 1 Parent 0 Size 0\n"));

        std::vector<uint8_t> badChunk(60, ' ');
        std::memcpy(badChunk.data(), "Caligari V00.01BLH             \nPolH", 36);
        badChunk[48] = 0xFF; badChunk[49] = 0x0F;
        assertMeshRejected(L"Stage15.3D.TrueSpaceCOB_RejectTruncatedChunk", writeBytes(L"trunc.cob", badChunk));

        const char* badIdxCob =
            "Caligari V00.01ALH             \n"
            "PolH V0.04 Id 1 Parent 0 Size 00000100\n"
            "World Vertices 3\n0 0 0\n1 0 0\n0 1 0\n"
            "Faces 1\nFace verts 3 flags 0 mat 0\n<0,0> <1,0> <99,0>\n"
            "END  V1.00 Id 2 Parent 0 Size 00000000\n";
        assertMeshRejected(L"Stage15.3D.TrueSpaceCOB_RejectOutOfRangeVertIdx", writeText(L"bad_idx.cob", badIdxCob));
    }

    // ========================================================================
    // Neutral File Format (.nff — Eric Haines SPD & Sense8 WorldToolKit)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".nff"),
                  L"Stage15.3D.NeutralNFF_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\spd_geodesic.nff", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawTriangleCount > 350 &&
                  m1.bboxSize.x > 9.5f && m1.bboxSize.y > 11.0f,
                  L"Stage15.3D.NeutralNFF_HainesSpdSampleParsed");

        const char* wtkNff =
            "// Sense8 WorldToolKit NFF\n"
            "nff\nversion 2.0\nCrateObj\n"
            "5\n"
            "-4.0 0.0 -4.0\n 4.0 0.0 -4.0\n 4.0 0.0  4.0\n-4.0 0.0  4.0\n 0.0 7.0  0.0\n"
            "5\n"
            "4 0 3 2 1 0xccc\n"
            "3 0 1 4 0xf00\n3 1 2 4 0x0f0\n3 2 3 4 0x00f\n3 3 0 4 0xff0\n";
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeText(L"wtk_pyramid.nff", wtkNff), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount == 6 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.NeutralNFF_WorldToolKitDialectAndDistinctHash");

        assertMeshRejected(L"Stage15.3D.NeutralNFF_RejectProseText",
            writeText(L"prose.nff", "Hello world this is a plain text note not an NFF scene.\n"));
        assertMeshRejected(L"Stage15.3D.NeutralNFF_RejectTruncatedSpdPoly",
            writeText(L"trunc_spd.nff", "p 4\n0 0 0\n1 0 0\n"));
        assertMeshRejected(L"Stage15.3D.NeutralNFF_RejectBadWtkIndex",
            writeText(L"bad_wtk.nff", "nff\nversion 2.0\nObj\n3\n0 0 0\n1 0 0\n0 1 0\n1\n3 0 1 55\n"));
    }

    // ========================================================================
    // Surface Evolver Minimal-Surface Mesh (.fe)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".fe"),
                  L"Stage15.3D.SurfaceEvolverFE_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\catenoid_soapfilm.fe", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawTriangleCount == 24 &&
                  m1.bboxSize.x > 9.5f && m1.bboxSize.y > 7.5f,
                  L"Stage15.3D.SurfaceEvolverFE_CatenoidOrientedEdgesParsed");

        const char* feTetra =
            "/* Tetrahedron bubble with PARAMETERs */\n"
            "PARAMETER s = 3.5\n"
            "vertices\n"
            "1  s  s  s\n"
            "2  s -s -s\n"
            "3 -s  s -s\n"
            "4 -s -s  s\n"
            "edges\n"
            "1 1 2\n2 2 3\n3 3 1\n4 1 4\n5 2 4\n6 3 4\n"
            "faces\n"
            "1  1  2  3\n"
            "2  1  5 -4\n"
            "3  2  6 -5\n"
            "4  3  4 -6\n"
            "bodies\n1 1 2 3 4 volume 10\n";
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeText(L"tetra_bubble.fe", feTetra), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount == 4 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.SurfaceEvolverFE_ParameterEvalAndDistinctHash");

        assertMeshRejected(L"Stage15.3D.SurfaceEvolverFE_RejectVerticesOnly",
            writeText(L"v_only.fe", "vertices\n1 0 0 0\n2 1 0 0\n3 0 1 0\n"));
        assertMeshRejected(L"Stage15.3D.SurfaceEvolverFE_RejectUndefinedFaceEdge",
            writeText(L"bad_fe.fe", "vertices\n1 0 0 0\n2 1 0 0\n3 0 1 0\nedges\n1 1 2\n2 2 3\nfaces\n1 1 2 -99\n"));
        assertMeshRejected(L"Stage15.3D.SurfaceEvolverFE_RejectUndefinedEdgeVert",
            writeText(L"bad_ev.fe", "vertices\n1 0 0 0\n2 1 0 0\nedges\n1 1 99\n"));
    }

    // ========================================================================
    // Brigham Young University Movie.BYU Polygon Mesh (.byu)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".byu"),
                  L"Stage15.3D.MovieBYU_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\nasa_nacelle.byu", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawTriangleCount == 34 &&
                  m1.bboxSize.x > 19.5f && m1.bboxSize.y > 13.5f,
                  L"Stage15.3D.MovieBYU_NacelleFortran6E12AndNegTermParsed");

        const char* byuTitled =
            "BYU Title Line Tetrahedron\n"
            "1 4 4 12\n"
            "1 4\n"
            "0.0 0.0 0.0  4.0 0.0 0.0  0.0 5.0 0.0  0.0 0.0 6.0\n"
            "1 3 -2\n1 2 -4\n2 3 -4\n3 1 -4\n";
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeText(L"titled_tet.byu", byuTitled), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawTriangleCount == 4 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.MovieBYU_TitleHeaderAndDistinctHash");

        assertMeshRejected(L"Stage15.3D.MovieBYU_RejectUnterminatedPoly",
            writeText(L"no_neg.byu", "1 3 1 3\n1 1\n0 0 0  1 0 0  0 1 0\n1 2 3\n"));
        assertMeshRejected(L"Stage15.3D.MovieBYU_RejectOutOfRangeVertId",
            writeText(L"bad_vid.byu", "1 3 1 3\n1 1\n0 0 0  1 0 0  0 1 0\n1 2 -99\n"));
        assertMeshRejected(L"Stage15.3D.MovieBYU_RejectInvalidPartBounds",
            writeText(L"bad_part.byu", "1 3 1 3\n1 9\n0 0 0  1 0 0  0 1 0\n1 2 -3\n"));
    }

    // ========================================================================
    // NASA Ames Cart3D Surface Triangulation (.tri & .triq — ASCII & Fortran Binary)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".tri") &&
                  Model3DViewer::Is3DModelExtension(L".triq"),
                  L"Stage15.3D.Cart3dTRI_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\cart3d_delta_wing.tri", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 9 && m1.rawTriangleCount == 10 &&
                  m1.bboxSize.x > 16.0f && m1.bboxSize.z > 18.5f,
                  L"Stage15.3D.Cart3dTRI_AsciiDeltaWingSampleParsed");

        std::vector<uint8_t> binBe;
        PushU32BE(binBe, 8u); PushU32BE(binBe, 4u); PushU32BE(binBe, 4u); PushU32BE(binBe, 8u);
        PushU32BE(binBe, 48u);
        const float tetPts[4][3] = { {0,0,0}, {6,0,0}, {0,6,0}, {0,0,8} };
        for (const auto& p : tetPts) { PushF32BE(binBe, p[0]); PushF32BE(binBe, p[1]); PushF32BE(binBe, p[2]); }
        PushU32BE(binBe, 48u);
        PushU32BE(binBe, 48u);
        const uint32_t tetIdx[4][3] = { {1,3,2}, {1,2,4}, {2,3,4}, {3,1,4} };
        for (const auto& t : tetIdx) { PushU32BE(binBe, t[0]); PushU32BE(binBe, t[1]); PushU32BE(binBe, t[2]); }
        PushU32BE(binBe, 48u);

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"tetra_be.triq", binBe), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 4 && m2.rawTriangleCount == 4 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.Cart3dTRI_BigEndianFortranTriqAndDistinctHash");

        std::vector<uint8_t> badMarker = binBe;
        badMarker[12] = 99;
        assertMeshRejected(L"Stage15.3D.Cart3dTRI_RejectBadFortranMarker", writeBytes(L"bad_rec.tri", badMarker));
        assertMeshRejected(L"Stage15.3D.Cart3dTRI_RejectZeroIndex",
            writeText(L"zero_idx.tri", "3 1\n0 0 0\n1 0 0\n0 1 0\n0 1 2\n"));
        assertMeshRejected(L"Stage15.3D.Cart3dTRI_RejectTruncatedAscii",
            writeText(L"trunc.tri", "5 2\n0 0 0\n1 0 0\n"));
    }

    // ========================================================================
    // Irrlicht Engine XML 3D Mesh (.irrmesh)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".irrmesh"),
                  L"Stage15.3D.IrrlichtIRRMESH_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\irrlicht_temple.irrmesh", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 13 && m1.rawTriangleCount == 14 &&
                  std::fabs(m1.bboxSize.x - 16.0f) < 0.01f && std::fabs(m1.bboxSize.y - 16.0f) < 0.01f,
                  L"Stage15.3D.IrrlichtIRRMESH_MultiBufferStandardAnd2TCoordsParsed");

        const char* irrTang =
            "<mesh version=\"1.0\"><buffer>"
            "<vertices type=\"tangents\" vertexCount=\"3\">"
            "  0 0 0  0 0 -1 ffffffff 0 0  1 0 0  0 1 0\n"
            "  5 0 0  0 0 -1 ffffffff 1 0  1 0 0  0 1 0\n"
            "  0 7 0  0 0 -1 ffffffff 0 1  1 0 0  0 1 0\n"
            "</vertices>"
            "<indices indexCount=\"3\">0 1 2</indices>"
            "</buffer></mesh>";
        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeText(L"tang_tri.irrmesh", irrTang), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 3 && m2.rawTriangleCount == 1 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.IrrlichtIRRMESH_TangentsLayoutAndDistinctHash");

        assertMeshRejected(L"Stage15.3D.IrrlichtIRRMESH_RejectUnrelatedXml",
            writeText(L"other.irrmesh", "<svg><rect width=\"10\" height=\"10\"/></svg>"));
        assertMeshRejected(L"Stage15.3D.IrrlichtIRRMESH_RejectVertexCountMismatch",
            writeText(L"bad_vc.irrmesh",
                "<mesh><buffer><vertices type=\"standard\" vertexCount=\"9\">"
                "0 0 0 0 1 0 ffffffff 0 0  1 0 0 0 1 0 ffffffff 1 0  0 1 0 0 1 0 ffffffff 0 1"
                "</vertices><indices>0 1 2</indices></buffer></mesh>"));
        assertMeshRejected(L"Stage15.3D.IrrlichtIRRMESH_RejectOutOfRangeIndex",
            writeText(L"bad_ix.irrmesh",
                "<mesh><buffer><vertices type=\"standard\" vertexCount=\"3\">"
                "0 0 0 0 1 0 ffffffff 0 0  1 0 0 0 1 0 ffffffff 1 0  0 1 0 0 1 0 ffffffff 0 1"
                "</vertices><indices>0 1 99</indices></buffer></mesh>"));
    }

    // ========================================================================
    // Sega Dreamcast Ninja 3D Model (.nj — NJCM Chunk & NJBM Basic)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".nj"),
                  L"Stage15.3D.SegaNinjaNJ_ExtensionRegistered");

        LoadedMesh3D m1;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\dreamcast_hovercraft.nj", m1);
        tc.Record(ok1 && m1.loadedOk && m1.rawVertexCount == 11 && m1.rawTriangleCount == 10 &&
                  m1.bboxSize.x > 9.5f && m1.bboxSize.z > 15.5f,
                  L"Stage15.3D.SegaNinjaNJ_NjcmHierarchyAndBamsSampleParsed");

        std::vector<uint8_t> njbmBody;
        PushU32LE(njbmBody, 0x07u); PushU32LE(njbmBody, 52u);
        for (int i = 0; i < 9; ++i) PushU32LE(njbmBody, 0);
        PushU32LE(njbmBody, 0); PushU32LE(njbmBody, 0);
        PushU32LE(njbmBody, 92u); PushU32LE(njbmBody, 0u); PushU32LE(njbmBody, 4u);
        PushU32LE(njbmBody, 140u); PushU32LE(njbmBody, 0u);
        PushU16LE(njbmBody, 1u); PushU16LE(njbmBody, 0u);
        for (int i = 0; i < 4; ++i) PushF32LE(njbmBody, 0.0f);
        const float qPts[4][3] = { {-3,0,0}, {3,0,0}, {3,6,0}, {-3,6,0} };
        for (const auto& p : qPts) { PushF32LE(njbmBody, p[0]); PushF32LE(njbmBody, p[1]); PushF32LE(njbmBody, p[2]); }
        PushU16LE(njbmBody, 0x4000u); PushU16LE(njbmBody, 1u); PushU32LE(njbmBody, 164u);
        for (int i = 0; i < 4; ++i) PushU32LE(njbmBody, 0u);
        PushU16LE(njbmBody, 0); PushU16LE(njbmBody, 1); PushU16LE(njbmBody, 2); PushU16LE(njbmBody, 3);

        std::vector<uint8_t> njbmFile = { 'N', 'J', 'B', 'M' };
        PushU32LE(njbmFile, static_cast<uint32_t>(njbmBody.size()));
        njbmFile.insert(njbmFile.end(), njbmBody.begin(), njbmBody.end());

        LoadedMesh3D m2;
        bool ok2 = Model3DViewer::InspectMeshFile(writeBytes(L"basic_quad.nj", njbmFile), m2);
        tc.Record(ok2 && m2.loadedOk && m2.rawVertexCount == 4 && m2.rawTriangleCount == 2 &&
                  HashMeshGeometry(m1) != HashMeshGeometry(m2),
                  L"Stage15.3D.SegaNinjaNJ_NjbmBasicQuadAndDistinctHash");

        std::vector<uint8_t> badMag = njbmFile;
        badMag[0] = 'X';
        assertMeshRejected(L"Stage15.3D.SegaNinjaNJ_RejectBadMagic", writeBytes(L"bad_mag.nj", badMag));

        std::vector<uint8_t> truncNj(njbmFile.begin(), njbmFile.begin() + 80);
        assertMeshRejected(L"Stage15.3D.SegaNinjaNJ_RejectTruncatedChunk", writeBytes(L"trunc.nj", truncNj));

        std::vector<uint8_t> badIdx = njbmFile;
        badIdx[badIdx.size() - 2] = 99;
        assertMeshRejected(L"Stage15.3D.SegaNinjaNJ_RejectOutOfRangeIndex", writeBytes(L"bad_idx.nj", badIdx));
    }

    // ========================================================================
    // Fuzz-Hardening Regression Checks (ACIS, OpenCASCADE, Parasolid, CryEngine, BioWare)
    // ========================================================================
    {
        assertMeshRejected(L"Stage15.3D.FuzzReg_AcisSatCut1", writeText(L"cut1.sat", "7"));
        assertMeshRejected(L"Stage15.3D.FuzzReg_AcisSatCut2", writeText(L"cut2.sat", "70"));
        assertMeshRejected(L"Stage15.3D.FuzzReg_AcisSatCut3", writeText(L"cut3.sat", "700"));
        std::vector<uint8_t> satBomb = { '7', '0', 0xFF, 0xFF, 0xFF, 0x7F, '1', ' ', '0', '\n', '#', '\n' };
        assertMeshRejected(L"Stage15.3D.FuzzReg_AcisSatHeaderBomb", writeBytes(L"bomb_hdr.sat", satBomb));

        assertMeshRejected(L"Stage15.3D.FuzzReg_OpenCascadeBrepNonNumericNodes",
            writeText(L"bad_nodes.brep", "DBRep_DrawableShape\nTriangulations 4000\n200000 200000 1 0.01\nNOT_A_FLOAT\nPolygon3D 4000\n100000 1 0.01\nBAD"));

        assertMeshRejected(L"Stage15.3D.FuzzReg_ParasolidXtTruncatedCountedStr",
            writeText(L"trunc_xt.x_t", "SCH_1000\n**END_OF_HEADER**\nT999999:abc"));

        std::vector<uint8_t> cgfBomb(32, 0);
        std::memcpy(cgfBomb.data(), "CryTek", 6);
        cgfBomb[12] = 0x44; cgfBomb[13] = 0x07;
        cgfBomb[16] = 0xFF; cgfBomb[17] = 0xFF; cgfBomb[18] = 0xFF; cgfBomb[19] = 0xFF;
        assertMeshRejected(L"Stage15.3D.FuzzReg_CryEngineCgfTableOffWrap", writeBytes(L"wrap.cgf", cgfBomb));

        std::vector<uint8_t> bwBomb(128, 0);
        bwBomb[4] = 100;
        std::memcpy(bwBomb.data() + 20, "kotor_test", 10);
        bwBomb[52] = 0xC0; bwBomb[53] = 0xFF; bwBomb[54] = 0xFF; bwBomb[55] = 0xFF;
        assertMeshRejected(L"Stage15.3D.FuzzReg_BioWareMdlRootNodeOffWrap", writeBytes(L"wrap_bw.mdl", bwBomb));
    }

    // ========================================================================
    // Autodesk Design Web Format (.dwf & .dwfx — Hoops W3D / HSF 3D Streams)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".dwf") &&
                  Model3DViewer::Is3DModelExtension(L".dwfx"),
                  L"Stage15.3D.AutodeskDwf.ExtensionsRegistered");

        LoadedMesh3D mDwf, mDwfx;
        bool okDwf  = Model3DViewer::InspectMeshFile(tdCadDir + L"\\bracket_assembly.dwf", mDwf);
        bool okDwfx = Model3DViewer::InspectMeshFile(tdCadDir + L"\\flange_housing.dwfx", mDwfx);

        tc.Record(okDwf && mDwf.loadedOk && mDwf.rawVertexCount == 16 && mDwf.rawTriangleCount == 22 &&
                  std::fabs(mDwf.bboxSize.y - 3.30f) < 0.05f &&
                  mDwf.formatName == L"Autodesk Design Web Format 3D (.dwf)",
                  L"Stage15.3D.AutodeskDwf.SampleBracketAssemblyDWF",
                  L"verts=" + std::to_wstring(mDwf.rawVertexCount) + L" tris=" + std::to_wstring(mDwf.rawTriangleCount));

        tc.Record(okDwfx && mDwfx.loadedOk && mDwfx.rawVertexCount == 12 && mDwfx.rawTriangleCount == 20 &&
                  std::fabs(mDwfx.bboxSize.y - 2.50f) < 0.05f &&
                  mDwfx.formatName == L"Autodesk Design Web Format XPS 3D (.dwfx)" &&
                  HashMeshGeometry(mDwfx) != HashMeshGeometry(mDwf),
                  L"Stage15.3D.AutodeskDwf.SampleFlangeHousingDWFX",
                  L"verts=" + std::to_wstring(mDwfx.rawVertexCount) + L" tris=" + std::to_wstring(mDwfx.rawTriangleCount));

        // Direct Hoops HSF stream inside a DWF v6 "(DWF V06.01)" ZIP archive with TKE_Shell tetrahedron
        std::vector<uint8_t> hsfStream;
        TestBytes::AppendStr(hsfStream, "(W3D V00.01)");
        hsfStream.push_back('S');  // TKE_Shell
        hsfStream.push_back(0u);   // uncompressed points & faces
        hsfStream.push_back(0u);   // lod = 0
        TestBytes::AppendI32LE(hsfStream, 4); // 4 points
        const float tetPts[4][3] = { {0,0,0}, {6,0,0}, {0,6,0}, {0,0,9} };
        for (const auto& pt : tetPts) {
            PushF32LE(hsfStream, pt[0]); PushF32LE(hsfStream, pt[1]); PushF32LE(hsfStream, pt[2]);
        }
        TestBytes::AppendI32LE(hsfStream, 16); // flist length = 4 * (deg=3 + 3 indices)
        const int32_t flist[16] = { 3,0,2,1,  3,0,1,3,  3,1,2,3,  3,2,0,3 };
        for (int32_t fv : flist) TestBytes::AppendI32LE(hsfStream, fv);
        hsfStream.push_back('!');

        auto dwfZip = BuildStoredSingleFileZip("com.autodesk.dwf.eModel/part.w3d", hsfStream.data(), hsfStream.size());
        std::vector<uint8_t> dwfV6;
        TestBytes::AppendStr(dwfV6, "(DWF V06.01)");
        dwfV6.insert(dwfV6.end(), dwfZip.begin(), dwfZip.end());
        LoadedMesh3D mCustomDwf;
        bool okCust = Model3DViewer::InspectMeshFile(writeBytes(L"custom_tet.dwf", dwfV6), mCustomDwf);
        tc.Record(okCust && mCustomDwf.loadedOk && mCustomDwf.rawVertexCount == 4 && mCustomDwf.rawTriangleCount == 4 &&
                  std::fabs(mCustomDwf.bboxMax.y - 9.0f) < 0.05f &&
                  HashMeshGeometry(mCustomDwf) != HashMeshGeometry(mDwf),
                  L"Stage15.3D.AutodeskDwf.V6ZipHeaderAndShellOpcodes");

        assertMeshRejected(L"Stage15.3D.AutodeskDwf.RejectCorruptHeaderDWF",
            writeText(L"bad.dwf", "(DWF V06.01)NOT_A_VALID_PKZIP_STREAM"));
        const char* noW3dTxt = "<manifest>2D sheet only</manifest>";
        auto emptyDwfx = BuildStoredSingleFileZip("FixedDocumentSequence.fdseq",
            reinterpret_cast<const uint8_t*>(noW3dTxt), std::strlen(noW3dTxt));
        assertMeshRejected(L"Stage15.3D.AutodeskDwf.RejectNo3dStreamDWFX",
            writeBytes(L"sheet_only.dwfx", emptyDwfx));
        assertMeshRejected(L"Stage15.3D.AutodeskDwf.RejectTruncatedHsfStream",
            writeText(L"trunc_hsf.dwf", "(W3D V00.01)S\x00\x00\x08\x00\x00\x00"));
    }

    // ========================================================================
    // Common Layer Interface (.cli — ASCII & Binary Laser Powder-Bed 3D Slices)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".cli"),
                  L"Stage15.3D.AdditiveCli.ExtensionRegistered");

        LoadedMesh3D mCliAsc, mCliBin;
        bool okAsc = Model3DViewer::InspectMeshFile(tdCadDir + L"\\turbine_slice_ascii.cli", mCliAsc);
        bool okBin = Model3DViewer::InspectMeshFile(tdCadDir + L"\\impeller_slice_bin.cli", mCliBin);

        tc.Record(okAsc && mCliAsc.loadedOk && mCliAsc.rawVertexCount == 336 && mCliAsc.rawTriangleCount == 320 &&
                  std::fabs(mCliAsc.bboxSize.x - 5.0f) < 0.05f && std::fabs(mCliAsc.bboxSize.y - 2.0f) < 0.05f &&
                  mCliAsc.formatName == L"Common Layer Interface 3D Slice (.cli ASCII)",
                  L"Stage15.3D.AdditiveCli.SampleTurbineAsciiCLI",
                  L"verts=" + std::to_wstring(mCliAsc.rawVertexCount) + L" tris=" + std::to_wstring(mCliAsc.rawTriangleCount));

        tc.Record(okBin && mCliBin.loadedOk && mCliBin.rawVertexCount == 108 && mCliBin.rawTriangleCount == 96 &&
                  std::fabs(mCliBin.bboxSize.x - 3.0f) < 0.05f && std::fabs(mCliBin.bboxSize.y - 1.5f) < 0.05f &&
                  mCliBin.formatName == L"Common Layer Interface 3D Slice (.cli Binary)" &&
                  HashMeshGeometry(mCliAsc) != HashMeshGeometry(mCliBin),
                  L"Stage15.3D.AdditiveCli.SampleImpellerBinaryCLI",
                  L"verts=" + std::to_wstring(mCliBin.rawVertexCount) + L" tris=" + std::to_wstring(mCliBin.rawTriangleCount));

        assertMeshRejected(L"Stage15.3D.AdditiveCli.RejectMissingHeaderEnd",
            writeText(L"no_hdrend.cli", "$$HEADERSTART\n$$ASCII\n$$UNITS/1.0\n$$LAYER/0.1\n"));
        assertMeshRejected(L"Stage15.3D.AdditiveCli.RejectEmptyLayersCLI",
            writeText(L"empty_geom.cli", "$$HEADERSTART\n$$ASCII\n$$UNITS/1.0\n$$HEADEREND\n$$GEOMETRYSTART\n$$GEOMETRYEND\n"));
        std::vector<uint8_t> truncBinCli;
        TestBytes::AppendStr(truncBinCli, "$$HEADERSTART\n$$BINARY\n$$UNITS/0.01\n$$HEADEREND$$GEOMETRYSTART");
        PushU16LE(truncBinCli, 127u); // START LAYER LONG without float32 z or polylines
        assertMeshRejected(L"Stage15.3D.AdditiveCli.RejectTruncatedBinaryCLI",
            writeBytes(L"trunc_bin.cli", truncBinCli));
    }

    // ========================================================================
    // Abaqus / CalculiX Solver Input Deck (.inp)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".inp"),
                  L"Stage15.3D.AbaqusInp.ExtensionRegistered");

        LoadedMesh3D mInp;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\cantilever_beam.inp", mInp);
        tc.Record(ok1 && mInp.loadedOk && mInp.rawVertexCount == 13 && mInp.rawTriangleCount == 26 &&
                  mInp.bboxSize.x > 4.9f &&
                  mInp.formatName == L"Abaqus / CalculiX FEM Input Deck (.inp)",
                  L"Stage15.3D.AbaqusInp.SampleCantileverBeamINP",
                  L"verts=" + std::to_wstring(mInp.rawVertexCount) + L" tris=" + std::to_wstring(mInp.rawTriangleCount));

        // 2 adjacent C3D8 hexahedra sharing interior quad (5,6,7,8) -> 10 exterior quads = 20 boundary triangles!
        const char* twoHexInp =
            "*HEADING\n"
            "** Two adjacent C3D8 bricks\n"
            "*NODE\n"
            "1, 0.0, 0.0, 0.0\n2, 2.0, 0.0, 0.0\n3, 2.0, 2.0, 0.0\n4, 0.0, 2.0, 0.0\n"
            "5, 0.0, 0.0, 3.0\n6, 2.0, 0.0, 3.0\n7, 2.0, 2.0, 3.0\n8, 0.0, 2.0, 3.0\n"
            "9, 0.0, 0.0, 6.0\n10, 2.0, 0.0, 6.0\n11, 2.0, 2.0, 6.0\n12, 0.0, 2.0, 6.0\n"
            "*ELEMENT, TYPE=C3D8R, ELSET=EALL\n"
            "1, 1, 2, 3, 4, 5, 6, 7, 8\n"
            "2, 5, 6, 7, 8, 9, 10, 11, 12\n";
        LoadedMesh3D mHexInp;
        bool ok2 = Model3DViewer::InspectMeshFile(writeText(L"two_c3d8.inp", twoHexInp), mHexInp);
        tc.Record(ok2 && mHexInp.loadedOk && mHexInp.rawVertexCount == 12 && mHexInp.rawTriangleCount == 20 &&
                  HashMeshGeometry(mInp) != HashMeshGeometry(mHexInp),
                  L"Stage15.3D.AbaqusInp.TwoC3D8InteriorFaceCulledAndDistinct");

        assertMeshRejected(L"Stage15.3D.AbaqusInp.RejectNodesOnlyNoElements",
            writeText(L"nodes_only.inp", "*NODE\n1, 0, 0, 0\n2, 1, 0, 0\n3, 0, 1, 0\n"));
        assertMeshRejected(L"Stage15.3D.AbaqusInp.RejectUndefinedElementNodes",
            writeText(L"bad_el.inp", "*NODE\n1, 0, 0, 0\n2, 1, 0, 0\n*ELEMENT, TYPE=S3\n1, 1, 2, 99\n"));
        assertMeshRejected(L"Stage15.3D.AbaqusInp.RejectBinaryNulData",
            writeBytes(L"bin_nul.inp", { '*', 'N', 'O', 'D', 'E', '\n', 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 }));
    }

    // ========================================================================
    // ANSYS Blocked Finite Element Archive (.cdb)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".cdb"),
                  L"Stage15.3D.AnsysCdb.ExtensionRegistered");

        LoadedMesh3D mCdb;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\pressure_vessel.cdb", mCdb);
        tc.Record(ok1 && mCdb.loadedOk && mCdb.rawVertexCount == 10 && mCdb.rawTriangleCount == 16 &&
                  std::fabs(mCdb.bboxSize.x - 3.5f) < 0.05f &&
                  mCdb.formatName == L"ANSYS Finite Element Archive (.cdb)",
                  L"Stage15.3D.AnsysCdb.SamplePressureVesselCDB",
                  L"verts=" + std::to_wstring(mCdb.rawVertexCount) + L" tris=" + std::to_wstring(mCdb.rawTriangleCount));

        // APDL free-format N / EN commands with FORTRAN exponentless float ("8.0+0")
        const char* apdlTetCdb =
            "/PREP7\n"
            "N, 10, 0.0, 0.0, 0.0\n"
            "N, 20, 6.0, 0.0, 0.0\n"
            "N, 30, 0.0, 5.0, 0.0\n"
            "N, 40, 0.0, 0.0, 8.0+0\n"
            "EN, 1, 10, 20, 30, 40\n"
            "FINISH\n";
        LoadedMesh3D mApdl;
        bool ok2 = Model3DViewer::InspectMeshFile(writeText(L"apdl_tet.cdb", apdlTetCdb), mApdl);
        tc.Record(ok2 && mApdl.loadedOk && mApdl.rawVertexCount == 4 && mApdl.rawTriangleCount == 4 &&
                  HashMeshGeometry(mCdb) != HashMeshGeometry(mApdl),
                  L"Stage15.3D.AnsysCdb.ApdlFreeFormatTetAndDistinctHash");

        assertMeshRejected(L"Stage15.3D.AnsysCdb.RejectNblockWithoutElements",
            writeText(L"nblock_only.cdb", "NBLOCK,6,SOLID\n(3i8,6e16.9)\n       1       0       0 0.000000000E+00 0.000000000E+00 0.000000000E+00\n-1\nFINISH\n"));
        assertMeshRejected(L"Stage15.3D.AnsysCdb.RejectMissingNodeRef",
            writeText(L"bad_ref.cdb", "/PREP7\nN, 1, 0, 0, 0\nN, 2, 1, 0, 0\nE, 1, 2, 999\nFINISH\n"));
        assertMeshRejected(L"Stage15.3D.AnsysCdb.RejectUnrelatedText",
            writeText(L"plain.cdb", "This is an unrelated database note with zero ANSYS blocks.\n"));
    }

    // ========================================================================
    // MSC / NX Nastran Bulk Data (.nas)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".nas"),
                  L"Stage15.3D.NastranNas.ExtensionRegistered");

        LoadedMesh3D mNas;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\wing_rib.nas", mNas);
        tc.Record(ok1 && mNas.loadedOk && mNas.rawVertexCount == 10 && mNas.rawTriangleCount == 16 &&
                  std::fabs(mNas.bboxMin.x - 5.0f) < 0.05f && std::fabs(mNas.bboxMax.x - 9.0f) < 0.05f &&
                  mNas.formatName == L"MSC / NX Nastran Bulk Data (.nas)",
                  L"Stage15.3D.NastranNas.SampleWingRibNAS",
                  L"verts=" + std::to_wstring(mNas.rawVertexCount) + L" tris=" + std::to_wstring(mNas.rawTriangleCount));

        // CORD2R local frame + Nastran exponentless scientific notation ("1.2+1" = 12.0) + CTETRA
        const char* cordTetNas =
            "BEGIN BULK\n"
            "CORD2R,10,0, 20.0,0.0,0.0, 20.0,0.0,1.0, 21.0,0.0,0.0\n"
            "GRID,1,10, 0.0, 0.0, 0.0\n"
            "GRID,2,10, 1.2+1, 0.0, 0.0\n"
            "GRID,3,10, 0.0, 8.0, 0.0\n"
            "GRID,4,10, 0.0, 0.0, 9.0\n"
            "CTETRA,100,1, 1,2,3,4\n"
            "ENDDATA\n";
        LoadedMesh3D mCord;
        bool ok2 = Model3DViewer::InspectMeshFile(writeText(L"cord2r_tet.nas", cordTetNas), mCord);
        tc.Record(ok2 && mCord.loadedOk && mCord.rawVertexCount == 4 && mCord.rawTriangleCount == 4 &&
                  std::fabs(mCord.bboxMin.x - 20.0f) < 0.05f && std::fabs(mCord.bboxMax.x - 32.0f) < 0.05f &&
                  HashMeshGeometry(mNas) != HashMeshGeometry(mCord),
                  L"Stage15.3D.NastranNas.Cord2rAndExponentlessFloatTet");

        assertMeshRejected(L"Stage15.3D.NastranNas.RejectGridsOnlyNoElements",
            writeText(L"grids_only.nas", "BEGIN BULK\nGRID,1,,0.0,0.0,0.0\nGRID,2,,1.0,0.0,0.0\nGRID,3,,0.0,1.0,0.0\nENDDATA\n"));
        assertMeshRejected(L"Stage15.3D.NastranNas.RejectUndefinedGridInElement",
            writeText(L"bad_grid.nas", "BEGIN BULK\nGRID,1,,0.0,0.0,0.0\nGRID,2,,1.0,0.0,0.0\nCTRIA3,1,1,1,2,999\nENDDATA\n"));
        assertMeshRejected(L"Stage15.3D.NastranNas.RejectBinaryNulNAS",
            writeBytes(L"nul.nas", { 'G', 'R', 'I', 'D', ',', '1', 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 }));
    }

    // ========================================================================
    // ANSYS Fluent Case / CFD Mesh (.cas)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".cas"),
                  L"Stage15.3D.FluentCas.ExtensionRegistered");

        LoadedMesh3D mCas;
        bool ok1 = Model3DViewer::InspectMeshFile(td3dDir + L"\\combustor_duct.cas", mCas);
        tc.Record(ok1 && mCas.loadedOk && mCas.rawVertexCount == 12 && mCas.rawTriangleCount == 20 &&
                  std::fabs(mCas.bboxSize.x - 4.2f) < 0.05f &&
                  mCas.formatName == L"ANSYS Fluent Case / CFD Mesh (.cas)",
                  L"Stage15.3D.FluentCas.SampleCombustorDuctCAS",
                  L"verts=" + std::to_wstring(mCas.rawVertexCount) + L" tris=" + std::to_wstring(mCas.rawTriangleCount));

        // 2-tet CFD mesh in hexadecimal indexing where bcType=2 interior shared face is filtered out -> 6 boundary tris!
        const char* twoTetCas =
            "(0 \"Fluent two-tetrahedron boundary filter test\")\n"
            "(2 3)\n"
            "(10 (0 1 5 0 3))\n"
            "(10 (1 1 5 1 3)(\n"
            " 0.0 0.0 0.0\n"
            " 6.0 0.0 0.0\n"
            " 3.0 5.0 0.0\n"
            " 3.0 2.0 7.0\n"
            " 3.0 2.0 -7.0\n"
            "))\n"
            "(13 (2 1 6 3 3)(\n"
            " 1 2 4 1 0\n"
            " 2 3 4 1 0\n"
            " 3 1 4 1 0\n"
            " 1 5 2 2 0\n"
            " 2 5 3 2 0\n"
            " 3 5 1 2 0\n"
            "))\n"
            "(13 (3 7 7 2 3)(\n"
            " 1 3 2 1 2\n"
            "))\n";
        LoadedMesh3D mTwoTetCas;
        bool ok2 = Model3DViewer::InspectMeshFile(writeText(L"two_tet_cfd.cas", twoTetCas), mTwoTetCas);
        tc.Record(ok2 && mTwoTetCas.loadedOk && mTwoTetCas.rawVertexCount == 5 && mTwoTetCas.rawTriangleCount == 6 &&
                  HashMeshGeometry(mCas) != HashMeshGeometry(mTwoTetCas),
                  L"Stage15.3D.FluentCas.InteriorZoneFilteredAndDistinctHash");

        assertMeshRejected(L"Stage15.3D.FluentCas.RejectCommentOnlyCAS",
            writeText(L"comment_only.cas", "(0 \"ANSYS Fluent case without nodes or faces\")\n(2 3)\n"));
        assertMeshRejected(L"Stage15.3D.FluentCas.RejectNodesWithoutFacesCAS",
            writeText(L"nodes_only.cas", "(0 \"nodes only\")\n(2 3)\n(10 (1 1 3 1 3)(0 0 0  1 0 0  0 1 0))\n"));
        assertMeshRejected(L"Stage15.3D.FluentCas.RejectOutOfBoundsFaceNodeCAS",
            writeText(L"oob_face.cas", "(0 \"oob\")\n(2 3)\n(10 (1 1 3 1 3)(0 0 0  1 0 0  0 1 0))\n(13 (2 1 1 3 3)(1 2 ff 1 0))\n"));
    }

    for (const auto& f : createdFiles) {
        DeleteFileW(f.c_str());
    }
    RemoveDirectoryW(workDir.c_str());
}

} // namespace Explorer2::SelfTestSuite
