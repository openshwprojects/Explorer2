#pragma once
// FormatTests3D_Batch70.h — Stage 15.3d verification for the 70-format roadmap (Phase 1: 3D, Skeletal, CAD & Geo)
// Included inside RunFormatTests3D(TestCounter& tc) in src/FormatTests3D.cpp.

    const std::wstring s70Dir = tdDir + L"\\samples70";
    auto assertRejected70 = [&](const wchar_t* testName, const std::wstring& badPath) {
        LoadedMesh3D badMesh;
        bool ok = Model3DViewer::InspectMeshFile(badPath, badMesh);
        tc.Record(!ok && !badMesh.loadedOk && badMesh.tris.empty(), testName,
                  ok ? L"unexpectedly loaded corrupt input" : L"rejected cleanly");
    };

    // ========================================================================
    // #51. id Tech 4 Skeletal Meshes & Animations (.md5mesh, .md5anim)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".md5mesh") &&
                  Model3DViewer::Is3DModelExtension(L".md5anim"),
                  L"Stage15.3D.MD5.ExtensionsRegistered");

        LoadedMesh3D mGuard, mWalk;
        bool okMesh = Model3DViewer::InspectMeshFile(s70Dir + L"\\cyber_guardian.md5mesh", mGuard);
        bool okAnim = Model3DViewer::InspectMeshFile(s70Dir + L"\\cyber_walk.md5anim", mWalk);
        tc.Record(okMesh && mGuard.loadedOk && mGuard.rawVertexCount == 13 && mGuard.rawTriangleCount == 18,
                  L"Stage15.3D.MD5.SampleGuardianMesh",
                  L"verts=" + std::to_wstring(mGuard.rawVertexCount) + L" tris=" + std::to_wstring(mGuard.rawTriangleCount));
        tc.Record(okAnim && mWalk.loadedOk && mWalk.animFrameCount == 2 && mWalk.rawTriangleCount == 18 &&
                  HashMeshGeometry(mGuard) != HashMeshGeometry(mWalk),
                  L"Stage15.3D.MD5.SampleWalkAnimSkinsCompanionMesh",
                  L"frames=" + std::to_wstring(mWalk.animFrameCount) + L" tris=" + std::to_wstring(mWalk.rawTriangleCount));

        // Standalone .md5anim without companion .md5mesh (articulated 3D octahedral bone skeleton, two distinct poses)
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

        // Negative checks: wrong MD5Version, zero-mesh .md5mesh, truncated .md5anim frame
        assertRejected70(L"Stage15.3D.MD5.RejectWrongVersion",
            writeText(L"bad_ver.md5mesh", "MD5Version 9\nnumJoints 1\nnumMeshes 0\njoints { \"r\" -1 ( 0 0 0 ) ( 0 0 0 ) }\n"));
        assertRejected70(L"Stage15.3D.MD5.RejectZeroMeshes",
            writeText(L"no_meshes.md5mesh", "MD5Version 10\nnumJoints 1\nnumMeshes 0\njoints { \"r\" -1 ( 0 0 0 ) ( 0 0 0 ) }\n"));
        assertRejected70(L"Stage15.3D.MD5.RejectTruncatedAnimFrame",
            writeText(L"trunc.md5anim",
                "MD5Version 10\nnumFrames 1\nnumJoints 2\nframeRate 24\nnumAnimatedComponents 4\n"
                "hierarchy { \"a\" -1 0 0 \"b\" 0 15 0 }\n"
                "baseframe { ( 0 0 0 ) ( 0 0 0 ) ( 1 2 3 ) ( 0 0 0 ) }\n"
                "frame 0 { 1.0 2.0 }\n"));
    }

    // ========================================================================
    // #52. Unreal Engine ActorX Meshes & Animations (.psk, .pskx, .psa)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".psk") &&
                  Model3DViewer::Is3DModelExtension(L".pskx") &&
                  Model3DViewer::Is3DModelExtension(L".psa"),
                  L"Stage15.3D.ActorX.ExtensionsRegistered");

        LoadedMesh3D mPsk, mPskx, mPsa;
        bool okPsk  = Model3DViewer::InspectMeshFile(s70Dir + L"\\ut_trooper.psk", mPsk);
        bool okPskx = Model3DViewer::InspectMeshFile(s70Dir + L"\\ut_heavy_32bit.pskx", mPskx);
        bool okPsa  = Model3DViewer::InspectMeshFile(s70Dir + L"\\ut_sprint.psa", mPsa);

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

        // Negative checks: wrong chunk magic, ACTRHEAD with 0 faces, truncated chunk payload
        std::vector<uint8_t> badMagic(64, 0);
        std::memcpy(badMagic.data(), "NOT_ACTORX", 10);
        assertRejected70(L"Stage15.3D.ActorX.RejectBadMagic", writeBytes(L"bad_magic.psk", badMagic));

        std::vector<uint8_t> hdrOnlyPsk(32, 0);
        std::memcpy(hdrOnlyPsk.data(), "ACTRHEAD", 8);
        assertRejected70(L"Stage15.3D.ActorX.RejectHeaderOnlyPSK", writeBytes(L"hdr_only.psk", hdrOnlyPsk));

        std::vector<uint8_t> truncPsa(64, 0);
        std::memcpy(truncPsa.data(), "ANIMHEAD", 8);
        std::memcpy(truncPsa.data() + 32, "BONENAMES", 9);
        int32_t dSz = 120, dCnt = 5;
        std::memcpy(truncPsa.data() + 56, &dSz, 4);
        std::memcpy(truncPsa.data() + 60, &dCnt, 4);
        assertRejected70(L"Stage15.3D.ActorX.RejectTruncatedPSA", writeBytes(L"trunc.psa", truncPsa));
    }

    // ========================================================================
    // #53. MikuMikuDance / VTuber Models & Motion (.pmx, .pmd, .vmd)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".pmx") &&
                  Model3DViewer::Is3DModelExtension(L".pmd") &&
                  Model3DViewer::Is3DModelExtension(L".vmd"),
                  L"Stage15.3D.MMD.ExtensionsRegistered");

        LoadedMesh3D mPmx, mPmd, mVmd;
        bool okPmx = Model3DViewer::InspectMeshFile(s70Dir + L"\\vtuber_star.pmx", mPmx);
        bool okPmd = Model3DViewer::InspectMeshFile(s70Dir + L"\\classic_stage.pmd", mPmd);
        bool okVmd = Model3DViewer::InspectMeshFile(s70Dir + L"\\dance_wave.vmd", mVmd);

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

        // Negative checks: wrong magic .pmx, truncated vertex table .pmd, 0-keyframe .vmd
        std::vector<uint8_t> badPmx(64, 0);
        std::memcpy(badPmx.data(), "PMX!", 4);
        assertRejected70(L"Stage15.3D.MMD.RejectBadMagicPMX", writeBytes(L"bad.pmx", badPmx));

        std::vector<uint8_t> truncPmd(300, 0);
        std::memcpy(truncPmd.data(), "Pmd", 3);
        float v10 = 1.0f;
        std::memcpy(truncPmd.data() + 3, &v10, 4);
        uint32_t nv50 = 50;
        std::memcpy(truncPmd.data() + 283, &nv50, 4);
        assertRejected70(L"Stage15.3D.MMD.RejectTruncatedPMD", writeBytes(L"trunc.pmd", truncPmd));

        std::vector<uint8_t> emptyVmd(62, 0);
        std::memcpy(emptyVmd.data(), "Vocaloid Motion Data 0002", 25);
        assertRejected70(L"Stage15.3D.MMD.RejectEmptyVMD", writeBytes(L"empty.vmd", emptyVmd));
    }

    // ========================================================================
    // #54. Blizzard WC3 & WoW 3D Models (.mdx, .mdl, .m2 + .skin, .wmo)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".mdx") &&
                  Model3DViewer::Is3DModelExtension(L".mdl") &&
                  Model3DViewer::Is3DModelExtension(L".m2") &&
                  Model3DViewer::Is3DModelExtension(L".wmo"),
                  L"Stage15.3D.Blizzard.ExtensionsRegistered");

        LoadedMesh3D mMdx, mMdl, mM2, mWmo;
        bool okMdx = Model3DViewer::InspectMeshFile(s70Dir + L"\\wc3_footman_shield.mdx", mMdx);
        bool okMdl = Model3DViewer::InspectMeshFile(s70Dir + L"\\wc3_obelisk.mdl", mMdl);
        bool okM2  = Model3DViewer::InspectMeshFile(s70Dir + L"\\wow_crystal_spire.m2", mM2);
        bool okWmo = Model3DViewer::InspectMeshFile(s70Dir + L"\\wow_chapel_000.wmo", mWmo);

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

        // Root WMO stitching sibling _000.wmo group file
        std::vector<uint8_t> grpBytes;
        if (PathUtils::ReadFileBytes(s70Dir + L"\\wow_chapel_000.wmo", 1024 * 1024, grpBytes)) {
            writeBytes(L"wow_root_test_000.wmo", grpBytes);
            std::vector<uint8_t> rootWmo;
            auto append4 = [&](const char* s) { rootWmo.insert(rootWmo.end(), s, s + 4); };
            auto appendU32 = [&](uint32_t v) {
                const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
                rootWmo.insert(rootWmo.end(), p, p + 4);
            };
            append4("REVM"); appendU32(4); appendU32(17);
            append4("DHOM"); appendU32(64);
            appendU32(1); // nTextures
            appendU32(1); // nGroups = 1 -> loads wow_root_test_000.wmo
            rootWmo.resize(rootWmo.size() + 56, 0);
            LoadedMesh3D mRootWmo;
            bool okRoot = Model3DViewer::InspectMeshFile(writeBytes(L"wow_root_test.wmo", rootWmo), mRootWmo);
            tc.Record(okRoot && mRootWmo.loadedOk && mRootWmo.rawTriangleCount == 8,
                      L"Stage15.3D.Blizzard.RootWMOStitchesGroup000");
        }

        // Negative checks: MDLX without GEOS, M2 without skin, truncated WMO
        std::vector<uint8_t> mdlxNoGeos = { 'M','D','L','X','V','E','R','S',4,0,0,0,0x20,0x03,0,0 };
        assertRejected70(L"Stage15.3D.Blizzard.RejectMDLXWithoutGeos", writeBytes(L"nogeos.mdx", mdlxNoGeos));

        std::vector<uint8_t> orphanM2;
        if (PathUtils::ReadFileBytes(s70Dir + L"\\wow_crystal_spire.m2", 1024 * 1024, orphanM2)) {
            assertRejected70(L"Stage15.3D.Blizzard.RejectM2WithoutSkin", writeBytes(L"orphan_noskin.m2", orphanM2));
        }

        std::vector<uint8_t> badWmo = { 'R','E','V','M',4,0,0,0,17,0,0,0,'P','G','O','M',100,0,0,0,0,0 };
        assertRejected70(L"Stage15.3D.Blizzard.RejectTruncatedWMO", writeBytes(L"trunc.wmo", badWmo));
    }

    // ========================================================================
    // #55. RenderWare Binary Stream 3D Clumps & World Sectors (.dff, .rws)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".dff") &&
                  Model3DViewer::Is3DModelExtension(L".rws"),
                  L"Stage15.3D.RenderWare.ExtensionsRegistered");

        LoadedMesh3D mDff, mRws;
        bool okDff = Model3DViewer::InspectMeshFile(s70Dir + L"\\vice_speeder.dff", mDff);
        bool okRws = Model3DViewer::InspectMeshFile(s70Dir + L"\\arena_sector.rws", mRws);

        tc.Record(okDff && mDff.loadedOk && mDff.rawVertexCount == 10 && mDff.rawTriangleCount == 10,
                  L"Stage15.3D.RenderWare.SampleViceSpeederDFF",
                  L"verts=" + std::to_wstring(mDff.rawVertexCount) + L" tris=" + std::to_wstring(mDff.rawTriangleCount));
        tc.Record(okRws && mRws.loadedOk && mRws.rawVertexCount == 8 && mRws.rawTriangleCount == 4 &&
                  HashMeshGeometry(mDff) != HashMeshGeometry(mRws),
                  L"Stage15.3D.RenderWare.SampleArenaSectorRWS",
                  L"verts=" + std::to_wstring(mRws.rawVertexCount) + L" tris=" + std::to_wstring(mRws.rawTriangleCount));

        // Negative checks: invalid RW version stamp, truncated Clump payload, empty Clump with 0 geometries
        std::vector<uint8_t> badStamp(32, 0);
        badStamp[0] = 0x10; badStamp[4] = 12; // stamp = 0 (version 0 < 3.0)
        assertRejected70(L"Stage15.3D.RenderWare.RejectBadRwVersionStamp", writeBytes(L"bad_stamp.dff", badStamp));

        std::vector<uint8_t> truncDff = {
            0x10,0,0,0, 200,0,0,0, 0xFF,0xFF,0x03,0x18,
            0x01,0,0,0, 12,0,0,0,  0xFF,0xFF,0x03,0x18
        };
        assertRejected70(L"Stage15.3D.RenderWare.RejectTruncatedClump", writeBytes(L"trunc.dff", truncDff));
    }

    // ========================================================================
    // #56. id Tech 3 / RTCW / WolfET / UberTools Skeletal Models (.md4, .mds, .mdm, .mdx, .tan)
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
        bool okMd4 = Model3DViewer::InspectMeshFile(s70Dir + L"\\q3_sentinel.md4", mMd4);
        bool okMds = Model3DViewer::InspectMeshFile(s70Dir + L"\\rtcw_guard.mds", mMds);
        bool okMdm = Model3DViewer::InspectMeshFile(s70Dir + L"\\et_medic.mdm", mMdm);
        bool okMdx = Model3DViewer::InspectMeshFile(s70Dir + L"\\et_medic.mdx", mMdx);
        bool okTan = Model3DViewer::InspectMeshFile(s70Dir + L"\\fakk2_blade.tan", mTan);

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

        // Standalone WolfET .mdx (without companion .mdm in scratch dir) -> articulated 3D octahedral skeleton
        std::vector<uint8_t> soloMdxBytes;
        if (PathUtils::ReadFileBytes(s70Dir + L"\\et_medic.mdx", 1024 * 1024, soloMdxBytes)) {
            LoadedMesh3D mSoloMdx;
            bool okSolo = Model3DViewer::InspectMeshFile(writeBytes(L"solo_rig.mdx", soloMdxBytes), mSoloMdx);
            tc.Record(okSolo && mSoloMdx.loadedOk && mSoloMdx.rawTriangleCount == 24 &&
                      HashMeshGeometry(mSoloMdx) != HashMeshGeometry(mMdm),
                      L"Stage15.3D.IdTech3Skel.StandaloneWolfEtMDXSkeleton",
                      L"tris=" + std::to_wstring(mSoloMdx.rawTriangleCount));
        }

        // Negative checks: wrong version MD4, truncated MDS, corrupt TAN
        std::vector<uint8_t> badMd4(128, 0);
        std::memcpy(badMd4.data(), "IDP4", 4);
        int32_t badVer99 = 99;
        std::memcpy(badMd4.data() + 4, &badVer99, 4);
        assertRejected70(L"Stage15.3D.IdTech3Skel.RejectWrongVersionMD4", writeBytes(L"bad_ver.md4", badMd4));

        std::vector<uint8_t> truncMds(128, 0);
        std::memcpy(truncMds.data(), "MDSW", 4);
        int32_t ver4 = 4;
        std::memcpy(truncMds.data() + 4, &ver4, 4);
        assertRejected70(L"Stage15.3D.IdTech3Skel.RejectTruncatedMDS", writeBytes(L"trunc.mds", truncMds));

        std::vector<uint8_t> badTan(128, 0);
        std::memcpy(badTan.data(), "TAN ", 4);
        int32_t ver2 = 2;
        std::memcpy(badTan.data() + 4, &ver2, 4);
        assertRejected70(L"Stage15.3D.IdTech3Skel.RejectZeroSurfacesTAN", writeBytes(L"empty.tan", badTan));
    }

    // ========================================================================
    // #57. BioWare Aurora ASCII (.mdl) & Odyssey KotOR Binary (.mdl + .mdx)
    // ========================================================================
    {
        LoadedMesh3D mAuroraAscii, mKotorMdl, mKotorMdx;
        bool okAsc  = Model3DViewer::InspectMeshFile(s70Dir + L"\\nwn_watchtower.mdl", mAuroraAscii);
        bool okKmdl = Model3DViewer::InspectMeshFile(s70Dir + L"\\kotor_droid.mdl", mKotorMdl);
        bool okKmdx = Model3DViewer::InspectMeshFile(s70Dir + L"\\kotor_droid.mdx", mKotorMdx);

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

        // Negative checks: truncated Aurora ASCII verts, corrupt binary KotOR MDL, orphan raw MDX
        assertRejected70(L"Stage15.3D.BioWareAurora.RejectTruncatedAsciiMDL",
                         writeText(L"bad_nwn.mdl",
                                   "newmodel bad\nbeginmodelgeom bad\nnode trimesh m\nverts 4\n0 0 0\nendnode\nendmodelgeom bad\ndonemodel bad\n"));
        std::vector<uint8_t> badKotor(128, 0);
        uint32_t rawSz = 100;
        std::memcpy(badKotor.data() + 4, &rawSz, 4);
        std::memcpy(badKotor.data() + 20, "bad_kotor", 9);
        assertRejected70(L"Stage15.3D.BioWareAurora.RejectCorruptBinaryMDL", writeBytes(L"bad_kotor.mdl", badKotor));
        assertRejected70(L"Stage15.3D.BioWareAurora.RejectOrphanRawMDX", writeBytes(L"orphan_only.mdx", std::vector<uint8_t>(96, 0x42)));
    }

    // ========================================================================
    // #58. Godot Engine 3/4 Scenes & Meshes (.tscn, .scn, .tres, .res, .mesh)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".tscn") &&
                  Model3DViewer::Is3DModelExtension(L".scn") &&
                  Model3DViewer::Is3DModelExtension(L".tres") &&
                  Model3DViewer::Is3DModelExtension(L".res") &&
                  Model3DViewer::Is3DModelExtension(L".mesh"),
                  L"Stage15.3D.Godot.ExtensionsRegistered");

        LoadedMesh3D mTres, mTscn, mMesh, mRes, mScn;
        bool okTres = Model3DViewer::InspectMeshFile(s70Dir + L"\\godot_crystal.tres", mTres);
        bool okTscn = Model3DViewer::InspectMeshFile(s70Dir + L"\\godot_outpost.tscn", mTscn);
        bool okMesh = Model3DViewer::InspectMeshFile(s70Dir + L"\\godot_turret.mesh", mMesh);
        bool okRes  = Model3DViewer::InspectMeshFile(s70Dir + L"\\godot_compressed.res", mRes);
        bool okScn  = Model3DViewer::InspectMeshFile(s70Dir + L"\\godot_bunker.scn", mScn);

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

        // Negative checks: non-mesh AudioStream resource, Win32 .res header, corrupt RSCC block
        assertRejected70(L"Stage15.3D.Godot.RejectNonMeshTRES",
                         writeText(L"audio_only.tres", "[gd_resource type=\"AudioStreamWAV\" format=3]\n[resource]\nmix_rate = 44100\n"));
        std::vector<uint8_t> win32Res(64, 0);
        win32Res[4] = 0x20; win32Res[8] = 0xFF; win32Res[9] = 0xFF;
        assertRejected70(L"Stage15.3D.Godot.RejectWin32ResourceRES", writeBytes(L"win32.res", win32Res));
        std::vector<uint8_t> badRscc(48, 0);
        std::memcpy(badRscc.data(), "RSCC", 4);
        badRscc[4] = 1; badRscc[8] = 16; badRscc[12] = 64;
        assertRejected70(L"Stage15.3D.Godot.RejectCorruptRSCC", writeBytes(L"corrupt.res", badRscc));
    }

    // ========================================================================
    // #59. Unreal Engine 4/5 Direct 3D UStaticMesh (.uasset + .uexp, .umap)
    // ========================================================================
    {
        tc.Record(Model3DViewer::Is3DModelExtension(L".uasset") &&
                  Model3DViewer::Is3DModelExtension(L".umap"),
                  L"Stage15.3D.UnrealPkg.ExtensionsRegistered");

        LoadedMesh3D mUe5Pillar, mUe4Map;
        bool okUe5 = Model3DViewer::InspectMeshFile(s70Dir + L"\\ue5_nanite_pillar.uasset", mUe5Pillar);
        bool okMap = Model3DViewer::InspectMeshFile(s70Dir + L"\\ue4_outpost_level.umap", mUe4Map);

        tc.Record(okUe5 && mUe5Pillar.loadedOk && mUe5Pillar.rawVertexCount == 8 && mUe5Pillar.rawTriangleCount == 12,
                  L"Stage15.3D.UnrealPkg.SampleUE5StaticMeshWithUEXP",
                  L"verts=" + std::to_wstring(mUe5Pillar.rawVertexCount) + L" tris=" + std::to_wstring(mUe5Pillar.rawTriangleCount));
        tc.Record(okMap && mUe4Map.loadedOk && mUe4Map.rawVertexCount == 5 && mUe4Map.rawTriangleCount == 6 &&
                  HashMeshGeometry(mUe4Map) != HashMeshGeometry(mUe5Pillar),
                  L"Stage15.3D.UnrealPkg.SampleUE4OutpostUMAP",
                  L"verts=" + std::to_wstring(mUe4Map.rawVertexCount) + L" tris=" + std::to_wstring(mUe4Map.rawTriangleCount));

        // Negative checks: Texture2D .uasset must NOT load as a 3D mesh; corrupt package magic rejected
        assertRejected70(L"Stage15.3D.UnrealPkg.RejectTexture2DUassetAs3D", s70Dir + L"\\ue4_scifi_panel.uasset");
        std::vector<uint8_t> badUe(96, 0);
        PushU32LE(badUe, 0x9E2A83C1u);
        assertRejected70(L"Stage15.3D.UnrealPkg.RejectTruncatedUasset", writeBytes(L"bad.uasset", badUe));
    }

    // ========================================================================
    // #60. Autodesk FBX (.fbx), COLLADA (.dae), & MilkShape (.ms3d) Linear-Blend Skinning & Poses
    // ========================================================================
    {
        LoadedMesh3D mFbxSkin, mDaeSkin, mMs3dRig;
        bool okFbx  = Model3DViewer::InspectMeshFile(s70Dir + L"\\skinned_arm.fbx", mFbxSkin);
        bool okDae  = Model3DViewer::InspectMeshFile(s70Dir + L"\\skinned_cyber_limb.dae", mDaeSkin);
        bool okMs3d = Model3DViewer::InspectMeshFile(s70Dir + L"\\rigged_mech.ms3d", mMs3dRig);

        // In bind pose each 3-ring limb lies straight along +X (maxY <= 0.35, maxX == 4.0).
        // With active +90 deg elbow/knee LBS deformation at x=2, tip vertices swing up to Y=2.0 (maxY > 1.85, maxX < 2.5).
        tc.Record(okFbx && mFbxSkin.loadedOk && mFbxSkin.rawVertexCount == 12 && mFbxSkin.rawTriangleCount == 20 &&
                  mFbxSkin.bboxMax.y > 1.85f && mFbxSkin.bboxMax.x < 2.50f && mFbxSkin.animFrameCount >= 2,
                  L"Stage15.3D.Skinning60.SampleFbxSkinnedArm",
                  L"verts=" + std::to_wstring(mFbxSkin.rawVertexCount) + L" maxY=" + std::to_wstring(mFbxSkin.bboxMax.y) +
                  L" maxX=" + std::to_wstring(mFbxSkin.bboxMax.x));

        tc.Record(okDae && mDaeSkin.loadedOk && mDaeSkin.rawVertexCount == 12 && mDaeSkin.rawTriangleCount == 20 &&
                  mDaeSkin.bboxMax.y > 1.85f && mDaeSkin.bboxMax.x < 2.50f &&
                  HashMeshGeometry(mDaeSkin) != HashMeshGeometry(mFbxSkin),
                  L"Stage15.3D.Skinning60.SampleColladaSkinnedLimb",
                  L"verts=" + std::to_wstring(mDaeSkin.rawVertexCount) + L" maxY=" + std::to_wstring(mDaeSkin.bboxMax.y) +
                  L" maxX=" + std::to_wstring(mDaeSkin.bboxMax.x));

        tc.Record(okMs3d && mMs3dRig.loadedOk && mMs3dRig.rawVertexCount == 12 && mMs3dRig.rawTriangleCount == 20 &&
                  mMs3dRig.bboxMax.y > 1.85f && mMs3dRig.bboxMax.x < 2.50f && mMs3dRig.animFrameCount == 24 &&
                  HashMeshGeometry(mMs3dRig) != HashMeshGeometry(mDaeSkin),
                  L"Stage15.3D.Skinning60.SampleMs3dRiggedMech",
                  L"verts=" + std::to_wstring(mMs3dRig.rawVertexCount) + L" maxY=" + std::to_wstring(mMs3dRig.bboxMax.y) +
                  L" frames=" + std::to_wstring(mMs3dRig.animFrameCount));

        // Negative checks: corrupt/truncated FBX, empty COLLADA skin, truncated MS3D vertex table
        std::vector<uint8_t> badFbx(32, 0);
        std::memcpy(badFbx.data(), "Kaydara FBX Binary  \0\x1a\0", 23);
        assertRejected70(L"Stage15.3D.Skinning60.RejectTruncatedFBX", writeBytes(L"trunc_skin.fbx", badFbx));
        assertRejected70(L"Stage15.3D.Skinning60.RejectEmptySkinDAE",
                         writeText(L"empty_skin.dae", "<COLLADA><library_controllers><controller id=\"c\"><skin source=\"#missing\"/></controller></library_controllers></COLLADA>"));
        std::vector<uint8_t> badMs3d(20, 0);
        std::memcpy(badMs3d.data(), "MS3D000000", 10);
        badMs3d[10] = 4; badMs3d[14] = 10; // version 4, claims 10 vertices in 20-byte file
        assertRejected70(L"Stage15.3D.Skinning60.RejectTruncatedMS3D", writeBytes(L"trunc.ms3d", badMs3d));
    }

    // ========================================================================
    // #61. Cal3D Skeletal (.cmf, .xmf, .csf) & CryEngine Geometry (.cgf, .chr, .skin)
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
        bool okCmf  = Model3DViewer::InspectMeshFile(s70Dir + L"\\paladin_rig.cmf", mCmf);
        bool okCsf  = Model3DViewer::InspectMeshFile(s70Dir + L"\\cal3d_biped_armature.csf", mCsfArm);
        bool okXmf  = Model3DViewer::InspectMeshFile(s70Dir + L"\\avatar_crown.xmf", mXmf);
        bool okCgf  = Model3DViewer::InspectMeshFile(s70Dir + L"\\crysis_crate.cgf", mCgf);
        bool okChr  = Model3DViewer::InspectMeshFile(s70Dir + L"\\nomad_suit.chr", mChr);
        bool okSkin = Model3DViewer::InspectMeshFile(s70Dir + L"\\nomad_pauldron.skin", mSkin);
        bool okWSk  = Model3DViewer::InspectMeshFile(s70Dir + L"\\wow_crystal_spire00.skin", mWowSkin);

        // paladin_rig.cmf + paladin_rig.csf: Bone 1 bends +90 deg around Z at x=2 -> tip swings up to Y=2.0!
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

        // Negative checks: corrupt CMF, empty XMF, corrupt CrCh, orphan SKIN
        std::vector<uint8_t> badCmf(24, 0);
        std::memcpy(badCmf.data(), "CMF\0", 4);
        badCmf[4] = 0xE8; badCmf[5] = 0x03; badCmf[8] = 2; // v1000, claims 2 submeshes in 24B
        assertRejected70(L"Stage15.3D.Cal3DCry.RejectTruncatedCMF", writeBytes(L"bad.cmf", badCmf));
        assertRejected70(L"Stage15.3D.Cal3DCry.RejectEmptyXMF", writeText(L"empty.xmf", "<HEADER MAGIC=\"XMF\"/><MESH/>"));
        std::vector<uint8_t> badCrCh(32, 0);
        std::memcpy(badCrCh.data(), "CrCh", 4);
        badCrCh[4] = 0x46; badCrCh[5] = 0x07; badCrCh[8] = 5; badCrCh[12] = 16; // claims 5 chunks in 32B
        assertRejected70(L"Stage15.3D.Cal3DCry.RejectTruncatedCrChCGF", writeBytes(L"bad.cgf", badCrCh));
        std::vector<uint8_t> orphanSkin(48, 0);
        std::memcpy(orphanSkin.data(), "SKIN", 4);
        assertRejected70(L"Stage15.3D.Cal3DCry.RejectOrphanWowSKIN", writeBytes(L"orphan99.skin", orphanSkin));
    }

    // -------------------------------------------------------------------------
    // #68 FreeCAD (.fcstd), SpaceClaim (.scdoc), Inventor (.ipt, .iam), & Solid Edge (.par, .psm)
    // -------------------------------------------------------------------------
    {
        LoadedMesh3D mFc, mSc, mIpt, mIam, mPar, mPsm;
        bool okFc  = Model3DViewer::InspectMeshFile(s70Dir + L"\\freecad_flange.fcstd", mFc);
        bool okSc  = Model3DViewer::InspectMeshFile(s70Dir + L"\\spaceclaim_bracket.scdoc", mSc);
        bool okIpt = Model3DViewer::InspectMeshFile(s70Dir + L"\\inventor_gear.ipt", mIpt);
        bool okIam = Model3DViewer::InspectMeshFile(s70Dir + L"\\inventor_drive.iam", mIam);
        bool okPar = Model3DViewer::InspectMeshFile(s70Dir + L"\\solidedge_housing.par", mPar);
        bool okPsm = Model3DViewer::InspectMeshFile(s70Dir + L"\\solidedge_bracket.psm", mPsm);

        // freecad_flange.fcstd: FlangeBase (8v, 12t, z=0..6) + FlangeSpire (5v, 6t, placed at Pz=6 -> z=6..24) => 13v, 18t, maxZ=24!
        tc.Record(okFc && mFc.loadedOk && mFc.rawVertexCount == 13 && mFc.rawTriangleCount == 18 &&
                  mFc.bboxMax.z > 23.5f,
                  L"Stage15.3D.CadPkg.SampleFreeCadFCSTD",
                  L"verts=" + std::to_wstring(mFc.rawVertexCount) + L" maxZ=" + std::to_wstring(mFc.bboxMax.z));
        tc.Record(okSc && mSc.loadedOk && mSc.rawVertexCount == 12 && mSc.rawTriangleCount == 20 &&
                  HashMeshGeometry(mSc) != HashMeshGeometry(mFc),
                  L"Stage15.3D.CadPkg.SampleSpaceClaimSCDOC",
                  L"verts=" + std::to_wstring(mSc.rawVertexCount) + L" tris=" + std::to_wstring(mSc.rawTriangleCount));
        tc.Record(okIpt && mIpt.loadedOk && mIpt.rawVertexCount == 12 && mIpt.rawTriangleCount == 20 &&
                  HashMeshGeometry(mIpt) != HashMeshGeometry(mSc),
                  L"Stage15.3D.CadPkg.SampleInventorIPT",
                  L"verts=" + std::to_wstring(mIpt.rawVertexCount) + L" tris=" + std::to_wstring(mIpt.rawTriangleCount));
        // inventor_drive.iam: 2 placed occurrences of inventor_gear.ipt at tx=0 and tx=32 => 24v, 40t, maxX = 32 + 14 = 46!
        tc.Record(okIam && mIam.loadedOk && mIam.rawVertexCount == 24 && mIam.rawTriangleCount == 40 &&
                  mIam.bboxMax.x > 45.5f && HashMeshGeometry(mIam) != HashMeshGeometry(mIpt),
                  L"Stage15.3D.CadPkg.SampleInventorAssemblyIAM",
                  L"verts=" + std::to_wstring(mIam.rawVertexCount) + L" maxX=" + std::to_wstring(mIam.bboxMax.x));
        tc.Record(okPar && mPar.loadedOk && mPar.rawVertexCount == 8 && mPar.rawTriangleCount == 12 &&
                  HashMeshGeometry(mPar) != HashMeshGeometry(mIpt),
                  L"Stage15.3D.CadPkg.SampleSolidEdgePAR",
                  L"verts=" + std::to_wstring(mPar.rawVertexCount) + L" tris=" + std::to_wstring(mPar.rawTriangleCount));
        tc.Record(okPsm && mPsm.loadedOk && mPsm.rawVertexCount == 8 && mPsm.rawTriangleCount == 12 &&
                  HashMeshGeometry(mPsm) != HashMeshGeometry(mPar),
                  L"Stage15.3D.CadPkg.SampleSolidEdgePSM",
                  L"verts=" + std::to_wstring(mPsm.rawVertexCount) + L" tris=" + std::to_wstring(mPsm.rawTriangleCount));

        // Negative checks: non-ZIP .fcstd/.scdoc, corrupt/empty OLE2 .ipt/.par
        assertRejected70(L"Stage15.3D.CadPkg.RejectCorruptFCSTD", writeText(L"bad.fcstd", "PK\x03\x04corrupt_zip"));
        assertRejected70(L"Stage15.3D.CadPkg.RejectCorruptSCDOC", writeText(L"bad.scdoc", "PK\x03\x04not_spaceclaim"));
        std::vector<uint8_t> badOle(512, 0);
        const uint8_t oleSig[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
        std::memcpy(badOle.data(), oleSig, 8);
        assertRejected70(L"Stage15.3D.CadPkg.RejectEmptyOleIPT", writeBytes(L"bad.ipt", badOle));
        assertRejected70(L"Stage15.3D.CadPkg.RejectEmptyOlePAR", writeBytes(L"bad.par", badOle));
    }

    // -------------------------------------------------------------------------
    // #70 Modo LXOB (.lxo), Zlib 3ds Max (.max), Cinema 4D C4DC (.c4d), & ZBrush (.zpr, .ztl)
    // -------------------------------------------------------------------------
    {
        LoadedMesh3D mLxo, mMax, mC4d, mZtl, mZpr;
        bool okLxo = Model3DViewer::InspectMeshFile(s70Dir + L"\\modo_drone_pod.lxo", mLxo);
        bool okMax = Model3DViewer::InspectMeshFile(s70Dir + L"\\max_compressed_mech.max", mMax);
        bool okC4d = Model3DViewer::InspectMeshFile(s70Dir + L"\\cinema4d_monolith.c4d", mC4d);
        bool okZtl = Model3DViewer::InspectMeshFile(s70Dir + L"\\zbrush_idol.ztl", mZtl);
        bool okZpr = Model3DViewer::InspectMeshFile(s70Dir + L"\\zbrush_statue_scene.zpr", mZpr);

        tc.Record(okLxo && mLxo.loadedOk && mLxo.rawVertexCount == 9 && mLxo.rawTriangleCount == 14 &&
                  mLxo.bboxMax.y > 4.1f,
                  L"Stage15.3D.ModoZBrushMaxC4d.SampleModoLXO",
                  L"verts=" + std::to_wstring(mLxo.rawVertexCount) + L" tris=" + std::to_wstring(mLxo.rawTriangleCount));
        tc.Record(okMax && mMax.loadedOk && mMax.rawVertexCount == 8 && mMax.rawTriangleCount == 12 &&
                  HashMeshGeometry(mMax) != HashMeshGeometry(mLxo),
                  L"Stage15.3D.ModoZBrushMaxC4d.SampleZlibEditablePolyMAX",
                  L"verts=" + std::to_wstring(mMax.rawVertexCount) + L" tris=" + std::to_wstring(mMax.rawTriangleCount));
        tc.Record(okC4d && mC4d.loadedOk && mC4d.rawVertexCount == 6 && mC4d.rawTriangleCount == 8 &&
                  mC4d.bboxMax.y > 8.9f && HashMeshGeometry(mC4d) != HashMeshGeometry(mMax),
                  L"Stage15.3D.ModoZBrushMaxC4d.SampleZlibVector64C4D",
                  L"verts=" + std::to_wstring(mC4d.rawVertexCount) + L" tris=" + std::to_wstring(mC4d.rawTriangleCount));
        tc.Record(okZtl && mZtl.loadedOk && mZtl.rawVertexCount == 6 && mZtl.rawTriangleCount == 8 &&
                  HashMeshGeometry(mZtl) != HashMeshGeometry(mC4d),
                  L"Stage15.3D.ModoZBrushMaxC4d.SampleZBrushSubToolZTL",
                  L"verts=" + std::to_wstring(mZtl.rawVertexCount) + L" tris=" + std::to_wstring(mZtl.rawTriangleCount));
        tc.Record(okZpr && mZpr.loadedOk && mZpr.rawVertexCount == 14 && mZpr.rawTriangleCount == 20 &&
                  HashMeshGeometry(mZpr) != HashMeshGeometry(mZtl),
                  L"Stage15.3D.ModoZBrushMaxC4d.SampleZBrushMultiSubToolZPR",
                  L"verts=" + std::to_wstring(mZpr.rawVertexCount) + L" tris=" + std::to_wstring(mZpr.rawTriangleCount));

        // Negative checks: corrupt FORM LXOB, corrupt C4DC zlib, non-ZBrush .ztl/.zpr
        assertRejected70(L"Stage15.3D.ModoZBrushMaxC4d.RejectEmptyLXO", writeText(L"bad.lxo", "FORM\x00\x00\x00\x04LXOB"));
        assertRejected70(L"Stage15.3D.ModoZBrushMaxC4d.RejectCorruptC4D", writeText(L"bad.c4d", "C4DC\x00\x00\x52\x08\x78\x9Cnot_zlib"));
        assertRejected70(L"Stage15.3D.ModoZBrushMaxC4d.RejectNonZBrushZTL", writeText(L"bad.ztl", "NOT_A_ZBRUSH_FILE_RANDOM_BYTES_12345678"));
        assertRejected70(L"Stage15.3D.ModoZBrushMaxC4d.RejectEmptyZPR", writeText(L"bad.zpr", "ZBrush File v2024 ZPR without subtools"));
    }

    // -------------------------------------------------------------------------
    // #11 Google Draco Edgebreaker + rANS (.drc) & glTF KHR_draco_mesh_compression / EXT_meshopt_compression
    // -------------------------------------------------------------------------
    {
        LoadedMesh3D mDrc, mGltfDraco, mGltfMeshopt;
        bool okDrc = Model3DViewer::InspectMeshFile(s70Dir + L"\\draco_edgebreaker_gem.drc", mDrc);
        bool okGD  = Model3DViewer::InspectMeshFile(s70Dir + L"\\gltf_draco_spaceship.glb", mGltfDraco);
        bool okGM  = Model3DViewer::InspectMeshFile(s70Dir + L"\\gltf_meshopt_turret.glb", mGltfMeshopt);

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

        // Genuine Draco v2.2 Valence Edgebreaker (travType=2) + Constrained Multi-Parallelogram + NORMAL & TEXCOORD_0 seams
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

        // Genuine Draco v2.3 KD-Tree Point Cloud (POINT_CLOUD_KD_TREE_ENCODING, 24 spiral points -> 96 tetra verts/tris)
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

        // Genuine KHR_meshopt_compression v1: ATTRIBUTES (0xA1) + EXPONENTIAL & OCTAHEDRAL filters + INDICES (0xD1)
        const char* khrMeshoptV1Gltf = R"JSON({"asset":{"version":"2.0"},"extensionsUsed":["KHR_meshopt_compression"],"extensionsRequired":["KHR_meshopt_compression"],"buffers":[{"byteLength":144,"extensions":{"KHR_meshopt_compression":{"fallback":true}}},{"byteLength":139,"uri":"data:application/octet-stream;base64,oV5aXgCgn5+gAAAYAAECAAIABgAiAAnyACAAPwAAwL+/wAAwAAECAAQABgAAAAAAAAAAAAAAAPEASAD1AAAA8QBSAKFvAACAfgT8AP1/gf39ACIA/f0AAAAAAAAAAAAAAAAAAAAAAAAAAH9/fwDRAAQEBggECgwEDhAKEAoCEAYCDAICCA4MAAAAAA=="}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":72,"byteStride":12,"extensions":{"KHR_meshopt_compression":{"buffer":1,"byteOffset":0,"byteLength":67,"byteStride":12,"count":6,"mode":"ATTRIBUTES","filter":"EXPONENTIAL"}}},{"buffer":0,"byteOffset":72,"byteLength":24,"byteStride":4,"extensions":{"KHR_meshopt_compression":{"buffer":1,"byteOffset":67,"byteLength":43,"byteStride":4,"count":6,"mode":"ATTRIBUTES","filter":"OCTAHEDRAL"}}},{"buffer":0,"byteOffset":96,"byteLength":48,"extensions":{"KHR_meshopt_compression":{"buffer":1,"byteOffset":110,"byteLength":29,"byteStride":2,"count":24,"mode":"INDICES"}}}],"accessors":[{"bufferView":0,"componentType":5126,"count":6,"type":"VEC3"},{"bufferView":1,"componentType":5120,"normalized":true,"count":6,"type":"VEC3"},{"bufferView":2,"componentType":5123,"count":24,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2}]}]})JSON";
        LoadedMesh3D mMoV1;
        bool okMoV1 = Model3DViewer::InspectMeshFile(writeText(L"khr_mo_v1.gltf", khrMeshoptV1Gltf), mMoV1);
        tc.Record(okMoV1 && mMoV1.loadedOk && mMoV1.rawVertexCount == 6 && mMoV1.rawTriangleCount == 8 &&
                  std::fabs(mMoV1.bboxMax.y - 9.0f) < 0.01f && std::fabs(mMoV1.bboxMin.y + 7.0f) < 0.01f &&
                  std::fabs(mMoV1.bboxMax.z - 6.0f) < 0.01f,
                  L"Stage15.3D.DracoMeshopt.GltfKhrMeshoptV1FiltersAndIndices",
                  L"verts=" + std::to_wstring(mMoV1.rawVertexCount) + L" maxY=" + std::to_wstring(mMoV1.bboxMax.y));

        // Negative checks: truncated Edgebreaker .drc, corrupt KHR_draco .gltf, corrupt EXT_meshopt .gltf
        const std::vector<uint8_t> badEbDrc = { 'D','R','A','C','O', 2, 2, 1, 1, 0, 0, 0, 6, 8 };
        assertRejected70(L"Stage15.3D.DracoMeshopt.RejectTruncatedEdgebreakerDRC",
                         writeBytes(L"bad_eb.drc", badEbDrc));
        assertRejected70(L"Stage15.3D.DracoMeshopt.RejectCorruptGltfMeshopt",
                         writeText(L"bad_mo.gltf", "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"data:application/octet-stream;base64,AAAA\",\"byteLength\":3}],\"bufferViews\":[{\"buffer\":0,\"byteLength\":3,\"extensions\":{\"EXT_meshopt_compression\":{\"buffer\":0,\"byteOffset\":0,\"byteLength\":3,\"byteStride\":12,\"count\":8,\"mode\":\"ATTRIBUTES\"}}}],\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":8,\"type\":\"VEC3\"}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}]}"));
    }

    // -------------------------------------------------------------------------
    // #12 Rhinoceros OpenNURBS (.3dm) ON_Brep, ON_Extrusion, ON_NurbsSurface & ON_InstanceRef
    // -------------------------------------------------------------------------
    {
        LoadedMesh3D mCanopy, mInst;
        bool okCanopy = Model3DViewer::InspectMeshFile(s70Dir + L"\\rhino_nurbs_canopy.3dm", mCanopy);
        bool okInst   = Model3DViewer::InspectMeshFile(s70Dir + L"\\rhino_instanced_pillars.3dm", mInst);

        // rhino_nurbs_canopy.3dm: 16x16 rational NURBS dome (289v, 512t, exact rational apex at u=0.5,v=0.5 is 9.0/1.25 = 7.20) + hex extrusion (12v, 20t) => 301v, 532t!
        tc.Record(okCanopy && mCanopy.loadedOk && mCanopy.rawVertexCount == 301 && mCanopy.rawTriangleCount == 532 &&
                  std::fabs(mCanopy.bboxMax.y - 7.20f) < 0.02f,
                  L"Stage15.3D.Rhino3dm.SampleNurbsBrepAndExtrusion3DM",
                  L"verts=" + std::to_wstring(mCanopy.rawVertexCount) + L" maxY=" + std::to_wstring(mCanopy.bboxMax.y));
        // rhino_instanced_pillars.3dm: 3 placed ON_InstanceRef occurrences of 8v/12t pillar at X=-20, 0, +25 => 24v, 36t, minX=-22, maxX=27!
        tc.Record(okInst && mInst.loadedOk && mInst.rawVertexCount == 24 && mInst.rawTriangleCount == 36 &&
                  mInst.bboxMin.x < -21.5f && mInst.bboxMax.x > 26.5f &&
                  HashMeshGeometry(mInst) != HashMeshGeometry(mCanopy),
                  L"Stage15.3D.Rhino3dm.SampleBlockInstanceRef3DM",
                  L"verts=" + std::to_wstring(mInst.rawVertexCount) + L" maxX=" + std::to_wstring(mInst.bboxMax.x));

        assertRejected70(L"Stage15.3D.Rhino3dm.RejectEmpty3DM",
                         writeText(L"empty.3dm", "3D Geometry File Format       70"));
    }

    // -------------------------------------------------------------------------
    // #13 Modern AutoCAD .dwg (AC1015 R2000 & AC1032 R2018 LZ77) & 3D / Binary .dxf
    // -------------------------------------------------------------------------
    {
        LoadedMesh3D mDwg15, mDwg32, mDxfSubd, mDxfBin;
        bool okD15 = Model3DViewer::InspectMeshFile(s70Dir + L"\\autocad_r2000_bunker.dwg", mDwg15);
        bool okD32 = Model3DViewer::InspectMeshFile(s70Dir + L"\\autocad_r2018_spire.dwg", mDwg32);
        bool okSub = Model3DViewer::InspectMeshFile(s70Dir + L"\\autocad_subd_polyface.dxf", mDxfSubd);
        bool okBin = Model3DViewer::InspectMeshFile(s70Dir + L"\\autocad_binary_3d.dxf", mDxfBin);

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
                  Model3DViewer::IsLikely3DDxfFile(s70Dir + L"\\autocad_binary_3d.dxf") &&
                  HashMeshGeometry(mDxfBin) != HashMeshGeometry(mDxfSubd),
                  L"Stage15.3D.DwgDxf3D.SampleBinary3DDXF",
                  L"verts=" + std::to_wstring(mDxfBin.rawVertexCount) + L" maxY=" + std::to_wstring(mDxfBin.bboxMax.y));

        // Negative checks: corrupt AC1015/AC1032 .dwg & truncated Binary .dxf
        assertRejected70(L"Stage15.3D.DwgDxf3D.RejectCorruptAC1015DWG",
                         writeBytes(L"bad15.dwg", std::vector<uint8_t>(128, 'A')));
        std::vector<uint8_t> bad32(300, 0);
        std::memcpy(bad32.data(), "AC1032", 6);
        assertRejected70(L"Stage15.3D.DwgDxf3D.RejectCorruptAC1032DWG", writeBytes(L"bad32.dwg", bad32));
        assertRejected70(L"Stage15.3D.DwgDxf3D.RejectTruncatedBinaryDXF",
                         writeText(L"badbin.dxf", "AutoCAD Binary DXF\r\n\x1A"));
    }

    // -------------------------------------------------------------------------
    // #14 Binary Parasolid (.x_b) & NURBS Surfaces in .x_t/.x_b & ACIS .sat/.sab
    // -------------------------------------------------------------------------
    {
        LoadedMesh3D mXb, mXt, mSat, mSab;
        bool okXb  = Model3DViewer::InspectMeshFile(s70Dir + L"\\parasolid_turbine_blade.x_b", mXb);
        bool okXt  = Model3DViewer::InspectMeshFile(s70Dir + L"\\parasolid_nurbs_hood.x_t", mXt);
        bool okSat = Model3DViewer::InspectMeshFile(s70Dir + L"\\acis_nurbs_wing.sat", mSat);
        bool okSab = Model3DViewer::InspectMeshFile(s70Dir + L"\\acis_nurbs_dome.sab", mSab);

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

        // Negative checks: corrupt binary .x_b, truncated NURBS .sat
        const std::vector<uint8_t> badXb = { 'P','S',0,0, '*','*','E','N','D','_','O','F','_','H','E','A','D','E','R','*','*','\n', 0,41, 0,0,0,1, 0,2 };
        assertRejected70(L"Stage15.3D.ParasolidAcisNurbs.RejectTruncatedXB", writeBytes(L"bad.x_b", badXb));
        assertRejected70(L"Stage15.3D.ParasolidAcisNurbs.RejectTruncatedNurbsSAT",
                         writeText(L"bad_nurbs.sat", "700 0 1 0\n1 1e-6 1e-10\n-0 face $-1 $-1 $-1 $-1 $1 forward #\n-1 spline-surface $-1 forward { exactsur nurbs 2 2 3 3 } #\nEnd-of-ACIS-data\n"));
    }

    // -------------------------------------------------------------------------
    // #15 STEP AP203/214/242 (.step, .stp, .stpz) NURBS B_SPLINE_SURFACE, Spheres/Tori & Assemblies
    // -------------------------------------------------------------------------
    {
        LoadedMesh3D mAir, mAssy, mStpz;
        bool okAir  = Model3DViewer::InspectMeshFile(s70Dir + L"\\step_nurbs_airfoil.step", mAir);
        bool okAssy = Model3DViewer::InspectMeshFile(s70Dir + L"\\step_sphere_torus_assembly.stp", mAssy);
        bool okStpz = Model3DViewer::InspectMeshFile(s70Dir + L"\\step_compressed_dome.stpz", mStpz);

        // step_nurbs_airfoil.step: 16x16 rational NURBS surface (289v, 512t); at u=0.5,v=0.5 rational Z->Y > 10.0!
        tc.Record(okAir && mAir.loadedOk && mAir.rawVertexCount == 289 && mAir.rawTriangleCount == 512 &&
                  mAir.bboxMax.y > 10.0f,
                  L"Stage15.3D.StepUpgrade.SampleRationalNurbsSTEP",
                  L"verts=" + std::to_wstring(mAir.rawVertexCount) + L" maxY=" + std::to_wstring(mAir.bboxMax.y));
        // step_sphere_torus_assembly.stp: 1 torus at X=0 + 2 spheres at X=-20 and X=+25 (radius=4 => minX=-24, maxX=29)!
        tc.Record(okAssy && mAssy.loadedOk && mAssy.rawTriangleCount > 1000 &&
                  std::fabs(mAssy.bboxMin.x - (-24.0f)) < 0.1f && std::fabs(mAssy.bboxMax.x - 29.0f) < 0.1f,
                  L"Stage15.3D.StepUpgrade.SampleSphereTorusAssemblySTP",
                  L"verts=" + std::to_wstring(mAssy.rawVertexCount) + L" minX=" + std::to_wstring(mAssy.bboxMin.x) + L" maxX=" + std::to_wstring(mAssy.bboxMax.x));
        tc.Record(okStpz && mStpz.loadedOk && mStpz.rawVertexCount == 289 && mStpz.rawTriangleCount == 512 &&
                  HashMeshGeometry(mStpz) != HashMeshGeometry(mAir),
                  L"Stage15.3D.StepUpgrade.SampleCompressedDomeSTPZ",
                  L"verts=" + std::to_wstring(mStpz.rawVertexCount) + L" maxY=" + std::to_wstring(mStpz.bboxMax.y));

        // Negative checks: corrupt gzip .stpz & empty STEP
        assertRejected70(L"Stage15.3D.StepUpgrade.RejectCorruptSTPZ",
                         writeText(L"bad.stpz", "\x1F\x8B\x08\x00not_valid_gzip_stream"));
        assertRejected70(L"Stage15.3D.StepUpgrade.RejectEmptySTEP",
                         writeText(L"empty_assy.step", "ISO-10303-21;\nHEADER;\nENDSEC;\nDATA;\nENDSEC;\nEND-ISO-10303-21;\n"));
    }

    // -------------------------------------------------------------------------
    // #16 SolidWorks Parts & Assemblies (.sldprt, .sldasm)
    // -------------------------------------------------------------------------
    {
        LoadedMesh3D mPrt, mAsm;
        bool okPrt = Model3DViewer::InspectMeshFile(s70Dir + L"\\solidworks_piston.sldprt", mPrt);
        bool okAsm = Model3DViewer::InspectMeshFile(s70Dir + L"\\solidworks_vtwin.sldasm", mAsm);

        tc.Record(okPrt && mPrt.loadedOk && mPrt.rawVertexCount == 10 && mPrt.rawTriangleCount == 16 &&
                  std::fabs(mPrt.bboxMax.y - 17.5f) < 0.05f,
                  L"Stage15.3D.SolidWorks.SamplePistonSLDPRT",
                  L"verts=" + std::to_wstring(mPrt.rawVertexCount) + L" tris=" + std::to_wstring(mPrt.rawTriangleCount));
        tc.Record(okAsm && mAsm.loadedOk && mAsm.rawVertexCount == 20 && mAsm.rawTriangleCount == 32 &&
                  std::fabs(mAsm.bboxMin.x - (-23.0f)) < 0.05f && std::fabs(mAsm.bboxMax.x - 23.0f) < 0.05f &&
                  std::fabs(mAsm.bboxMax.y - 22.5f) < 0.05f && HashMeshGeometry(mAsm) != HashMeshGeometry(mPrt),
                  L"Stage15.3D.SolidWorks.SampleVTwinAssemblySLDASM",
                  L"verts=" + std::to_wstring(mAsm.rawVertexCount) + L" minX=" + std::to_wstring(mAsm.bboxMin.x) + L" maxY=" + std::to_wstring(mAsm.bboxMax.y));

        // Negative checks: truncated OLE2 header & non-SolidWorks garbage
        assertRejected70(L"Stage15.3D.SolidWorks.RejectTruncatedOLE2",
                         writeBytes(L"bad.sldprt", { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1, 0x00, 0x01 }));
        assertRejected70(L"Stage15.3D.SolidWorks.RejectGarbageSLDASM",
                         writeText(L"bad.sldasm", "NOT_A_SOLIDWORKS_ASSEMBLY_CONTAINER_FILE_0000"));
    }

    // -------------------------------------------------------------------------
    // #17 Pixar OpenUSD (.usd, .usda, .usdc, .usdz) & Alembic Ogawa (.abc) Xform Hierarchy & PointInstancer
    // -------------------------------------------------------------------------
    {
        LoadedMesh3D mUsda, mUsdc, mUsdz, mAbc;
        bool okUsda = Model3DViewer::InspectMeshFile(s70Dir + L"\\usd_forest_instancer.usda", mUsda);
        bool okUsdc = Model3DViewer::InspectMeshFile(s70Dir + L"\\usd_crate_turrets.usdc", mUsdc);
        bool okUsdz = Model3DViewer::InspectMeshFile(s70Dir + L"\\usd_multilayer_station.usdz", mUsdz);
        bool okAbc  = Model3DViewer::InspectMeshFile(s70Dir + L"\\alembic_instanced_rig.abc", mAbc);

        // usd_forest_instancer.usda: 3 PointInstancer obelisks under /World (translate Y=+5), scales [1, 2, 1] -> 15v, 18t, minX=-27, maxX=32, maxY=31!
        tc.Record(okUsda && mUsda.loadedOk && mUsda.rawVertexCount == 15 && mUsda.rawTriangleCount == 18 &&
                  std::fabs(mUsda.bboxMin.x - (-27.0f)) < 0.05f && std::fabs(mUsda.bboxMax.x - 32.0f) < 0.05f &&
                  std::fabs(mUsda.bboxMax.y - 31.0f) < 0.05f,
                  L"Stage15.3D.UsdAlembic.SamplePointInstancerUSDA",
                  L"verts=" + std::to_wstring(mUsda.rawVertexCount) + L" minX=" + std::to_wstring(mUsda.bboxMin.x) + L" maxY=" + std::to_wstring(mUsda.bboxMax.y));

        // usd_crate_turrets.usdc: /Base Xform (12, 4, 0) + /Base/Turret Mesh -> 4v, 4t, minX=12, maxX=16, maxY=10!
        tc.Record(okUsdc && mUsdc.loadedOk && mUsdc.rawVertexCount == 4 && mUsdc.rawTriangleCount == 4 &&
                  std::fabs(mUsdc.bboxMin.x - 12.0f) < 0.05f && std::fabs(mUsdc.bboxMax.y - 10.0f) < 0.05f,
                  L"Stage15.3D.UsdAlembic.SampleXformCrateUSDC",
                  L"verts=" + std::to_wstring(mUsdc.rawVertexCount) + L" minX=" + std::to_wstring(mUsdc.bboxMin.x) + L" maxY=" + std::to_wstring(mUsdc.bboxMax.y));

        // usd_multilayer_station.usdz: station.usda referencing pod.usdc at X=-20 and X=+20 (Y=+3) -> 8v, 8t, minX=-20, maxX=24, maxY=9!
        tc.Record(okUsdz && mUsdz.loadedOk && mUsdz.rawVertexCount == 8 && mUsdz.rawTriangleCount == 8 &&
                  std::fabs(mUsdz.bboxMin.x - (-20.0f)) < 0.05f && std::fabs(mUsdz.bboxMax.x - 24.0f) < 0.05f &&
                  std::fabs(mUsdz.bboxMax.y - 9.0f) < 0.05f && HashMeshGeometry(mUsdz) != HashMeshGeometry(mUsdc),
                  L"Stage15.3D.UsdAlembic.SampleMultiLayerUSDZ",
                  L"verts=" + std::to_wstring(mUsdz.rawVertexCount) + L" minX=" + std::to_wstring(mUsdz.bboxMin.x) + L" maxX=" + std::to_wstring(mUsdz.bboxMax.x));

        // alembic_instanced_rig.abc: 2 Xform parents (-20,0,0) and (20,6,0 scaleY=2) DAG-instancing 5v/6t PolyMesh -> 10v, 12t, minX=-22, maxX=22, maxY=16!
        tc.Record(okAbc && mAbc.loadedOk && mAbc.rawVertexCount == 10 && mAbc.rawTriangleCount == 12 &&
                  std::fabs(mAbc.bboxMin.x - (-22.0f)) < 0.05f && std::fabs(mAbc.bboxMax.x - 22.0f) < 0.05f &&
                  std::fabs(mAbc.bboxMax.y - 16.0f) < 0.05f,
                  L"Stage15.3D.UsdAlembic.SampleHierarchicalInstancedABC",
                  L"verts=" + std::to_wstring(mAbc.rawVertexCount) + L" minX=" + std::to_wstring(mAbc.bboxMin.x) + L" maxY=" + std::to_wstring(mAbc.bboxMax.y));

        // Negative checks: corrupt Crate TOC & corrupt Ogawa root offset
        assertRejected70(L"Stage15.3D.UsdAlembic.RejectCorruptUSDC",
                         writeBytes(L"bad_crate.usdc", { 'P','X','R','-','U','S','D','C', 0,8,0,0,0,0,0,0, 0xFF,0xFF,0xFF,0xFF }));
        assertRejected70(L"Stage15.3D.UsdAlembic.RejectCorruptABC",
                         writeBytes(L"bad_ogawa.abc", { 'O','g','a','w','a', 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0,0,0,0, 0,0,0,0, 0,0,0,0 }));
    }

    // -------------------------------------------------------------------------
    // #18 VTK XML Scientific/FEM Suite (.vtu, .vtp, .vti, .vtr, .vts, .pvd)
    // -------------------------------------------------------------------------
    {
        LoadedMesh3D mVtu, mVtp, mVti, mVtr, mVts, mPvd;
        bool okVtu = Model3DViewer::InspectMeshFile(s70Dir + L"\\fem_bracket_zlib.vtu", mVtu);
        bool okVtp = Model3DViewer::InspectMeshFile(s70Dir + L"\\aerofoil_appended_raw.vtp", mVtp);
        bool okVti = Model3DViewer::InspectMeshFile(s70Dir + L"\\plasma_core_lz4.vti", mVti);
        bool okVtr = Model3DViewer::InspectMeshFile(s70Dir + L"\\wind_tunnel.vtr", mVtr);
        bool okVts = Model3DViewer::InspectMeshFile(s70Dir + L"\\curved_duct.vts", mVts);
        bool okPvd = Model3DViewer::InspectMeshFile(s70Dir + L"\\multiblock_sim.pvd", mPvd);

        // fem_bracket_zlib.vtu: 2 adjacent Hexahedra -> shared interior quad culled -> 10 exterior quads = 20 tris!
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

        // Negative checks: corrupt zlib Base64 .vtu & empty Collection .pvd
        assertRejected70(L"Stage15.3D.VtkXml.RejectCorruptZlibVTU",
                         writeText(L"bad.vtu", "<VTKFile type=\"UnstructuredGrid\" compressor=\"vtkZLibDataCompressor\"><UnstructuredGrid><Piece><Points><DataArray type=\"Float32\" format=\"binary\">!!!!not_valid_zlib!!!!</DataArray></Points></Piece></UnstructuredGrid></VTKFile>"));
        assertRejected70(L"Stage15.3D.VtkXml.RejectEmptyPVD",
                         writeText(L"empty.pvd", "<VTKFile type=\"Collection\"><Collection></Collection></VTKFile>"));
    }

    // -------------------------------------------------------------------------
    // #19 3D Gaussian Splatting (.splat, .ksplat, .spz) & OpenVDB Sparse Volumes (.vdb)
    // -------------------------------------------------------------------------
    {
        LoadedMesh3D mSplat, mKsplat, mSpz, mVdb;
        bool okSplat  = Model3DViewer::InspectMeshFile(s70Dir + L"\\garden_bonsai.splat", mSplat);
        bool okKsplat = Model3DViewer::InspectMeshFile(s70Dir + L"\\neon_arch.ksplat", mKsplat);
        bool okSpz    = Model3DViewer::InspectMeshFile(s70Dir + L"\\scaniverse_statue.spz", mSpz);
        bool okVdb    = Model3DViewer::InspectMeshFile(s70Dir + L"\\smoke_plume.vdb", mVdb);

        // garden_bonsai.splat: 5 anisotropic Gaussians * (6v, 8t) = 30v, 40t; top canopy y=8 + 2*1.0 = 10.0!
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
        // smoke_plume.vdb: 3 L-shaped contiguous active voxels (2 interior shared quads culled -> 14 quads = 28 tris)!
        tc.Record(okVdb && mVdb.loadedOk && mVdb.rawTriangleCount == 28 &&
                  std::fabs(mVdb.bboxMax.x - 3.0f) < 0.05f && std::fabs(mVdb.bboxMax.y - 3.0f) < 0.05f,
                  L"Stage15.3D.SplatVdb.SampleSparseVolumeVDB",
                  L"verts=" + std::to_wstring(mVdb.rawVertexCount) + L" tris=" + std::to_wstring(mVdb.rawTriangleCount));

        // Negative checks: 32-byte ASCII text as .splat (must fail scale/quat validation!), bad .spz, truncated .vdb
        assertRejected70(L"Stage15.3D.SplatVdb.RejectAsciiTextSPLAT",
                         writeText(L"fake32.splat", "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345"));
        assertRejected70(L"Stage15.3D.SplatVdb.RejectCorruptSPZ",
                         writeBytes(L"bad.spz", { 0x1F, 0x8B, 0x08, 0x00, 0xDE, 0xAD, 0xBE, 0xEF }));
        assertRejected70(L"Stage15.3D.SplatVdb.RejectTruncatedVDB",
                         writeBytes(L"bad.vdb", { 0x20, 'B', 'D', 'V', 0, 0, 0, 0, 224, 0, 0, 0 }));
    }

    // -------------------------------------------------------------------------
    // #20 GeoTIFF 3D DEM, Void-Safe SRTM (.hgt), 3D .shp/.geojson, OpenCTM MG1/MG2 (.ctm),
    //     Quake/Source .bsp, & Ogre3D / INRIA Medit (.mesh)
    // -------------------------------------------------------------------------
    {
        LoadedMesh3D mHgt, mGtif, mShp, mGjson, mMg1, mMg2, mBsp, mOgre, mMedit;
        bool okHgt   = Model3DViewer::InspectMeshFile(s70Dir + L"\\srtm_void_crater.hgt", mHgt);
        bool okGtif  = Model3DViewer::InspectMeshFile(s70Dir + L"\\alpine_dem.tif", mGtif);
        bool okShp   = Model3DViewer::InspectMeshFile(s70Dir + L"\\city_tower_3d.shp", mShp);
        bool okGjson = Model3DViewer::InspectMeshFile(s70Dir + L"\\skyline_extruded.geojson", mGjson);
        bool okMg1   = Model3DViewer::InspectMeshFile(s70Dir + L"\\openctm_mg1_crystal.ctm", mMg1);
        bool okMg2   = Model3DViewer::InspectMeshFile(s70Dir + L"\\openctm_mg2_statue.ctm", mMg2);
        bool okBsp   = Model3DViewer::InspectMeshFile(s70Dir + L"\\quake3_arena_room.bsp", mBsp);
        bool okOgre  = Model3DViewer::InspectMeshFile(s70Dir + L"\\ogre_paladin_helm.mesh", mOgre);
        bool okMedit = Model3DViewer::InspectMeshFile(s70Dir + L"\\medit_fem_tetra.mesh", mMedit);

        // srtm_void_crater.hgt: 4x4 grid with 1 void (-32768) cell -> 15 vertices (NOT 16!) and 17 tris (NOT 18!)
        tc.Record(okHgt && mHgt.loadedOk && mHgt.rawVertexCount == 15 && mHgt.rawTriangleCount == 17 &&
                  mHgt.bboxMin.y >= 9.9f,
                  L"Stage15.3D.BspGeo.SampleVoidSafeSRTM",
                  L"verts=" + std::to_wstring(mHgt.rawVertexCount) + L" tris=" + std::to_wstring(mHgt.rawTriangleCount) + L" minY=" + std::to_wstring(mHgt.bboxMin.y));
        tc.Record(okGtif && mGtif.loadedOk && mGtif.rawVertexCount == 16 && mGtif.rawTriangleCount == 18 &&
                  std::fabs(mGtif.bboxMax.y - 45.0f) < 0.05f &&
                  Model3DViewer::IsLikely3DGeoOrDemFile(s70Dir + L"\\alpine_dem.tif", L".tif"),
                  L"Stage15.3D.BspGeo.SampleGeoTiff3DDemTIF",
                  L"verts=" + std::to_wstring(mGtif.rawVertexCount) + L" maxY=" + std::to_wstring(mGtif.bboxMax.y));
        tc.Record(okShp && mShp.loadedOk && mShp.rawVertexCount == 8 && mShp.rawTriangleCount == 10 &&
                  std::fabs(mShp.bboxMax.y - 35.0f) < 0.05f &&
                  Model3DViewer::IsLikely3DGeoOrDemFile(s70Dir + L"\\city_tower_3d.shp", L".shp"),
                  L"Stage15.3D.BspGeo.SamplePolygonZShapefileSHP",
                  L"verts=" + std::to_wstring(mShp.rawVertexCount) + L" maxY=" + std::to_wstring(mShp.bboxMax.y));
        tc.Record(okGjson && mGjson.loadedOk && mGjson.rawVertexCount == 8 && mGjson.rawTriangleCount == 10 &&
                  std::fabs(mGjson.bboxMax.y - 42.0f) < 0.05f &&
                  Model3DViewer::IsLikely3DGeoOrDemFile(s70Dir + L"\\skyline_extruded.geojson", L".geojson"),
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
        // quake3_arena_room.bsp: 2 floor tris + 4x4*2=32 Bezier arch tris = 34 tris; Bezier apex at u=0.5 is 9.0!
        tc.Record(okBsp && mBsp.loadedOk && mBsp.rawTriangleCount == 34 && std::fabs(mBsp.bboxMax.y - 9.0f) < 0.05f,
                  L"Stage15.3D.BspGeo.SampleQuake3BezierIBSP",
                  L"verts=" + std::to_wstring(mBsp.rawVertexCount) + L" tris=" + std::to_wstring(mBsp.rawTriangleCount) + L" maxY=" + std::to_wstring(mBsp.bboxMax.y));
        tc.Record(okOgre && mOgre.loadedOk && mOgre.rawVertexCount == 5 && mOgre.rawTriangleCount == 6 &&
                  std::fabs(mOgre.bboxMax.y - 15.0f) < 0.05f,
                  L"Stage15.3D.BspGeo.SampleOgreBinaryMESH",
                  L"verts=" + std::to_wstring(mOgre.rawVertexCount) + L" maxY=" + std::to_wstring(mOgre.bboxMax.y));
        // medit_fem_tetra.mesh: 2 adjacent Tetrahedra -> shared interior triangle culled -> 6 exterior tris!
        tc.Record(okMedit && mMedit.loadedOk && mMedit.rawVertexCount == 5 && mMedit.rawTriangleCount == 6 &&
                  std::fabs(mMedit.bboxMin.y - (-12.0f)) < 0.05f && std::fabs(mMedit.bboxMax.y - 12.0f) < 0.05f,
                  L"Stage15.3D.BspGeo.SampleMeditFemMESH",
                  L"verts=" + std::to_wstring(mMedit.rawVertexCount) + L" tris=" + std::to_wstring(mMedit.rawTriangleCount));

        // Negative checks: all-void SRTM, corrupt MG1 LZMA .ctm, corrupt IBSP .bsp
        assertRejected70(L"Stage15.3D.BspGeo.RejectAllVoidHGT",
                         writeBytes(L"void.hgt", { 0x80,0, 0x80,0, 0x80,0,  0x80,0, 0x80,0, 0x80,0,  0x80,0, 0x80,0, 0x80,0 }));
        assertRejected70(L"Stage15.3D.BspGeo.RejectCorruptMg1CTM",
                         writeBytes(L"bad_mg1.ctm", { 'O','C','T','M', 5,0,0,0, 'M','G','1',0, 4,0,0,0, 4,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 'I','N','D','X', 99,0,0,0 }));
        assertRejected70(L"Stage15.3D.BspGeo.RejectCorruptBSP",
                         writeText(L"bad.bsp", "IBSP\x2E\x00\x00\x00truncated_lumps"));
    }
