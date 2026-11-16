#pragma once
// FormatTestsArchives_Batch70.h — Stage 15.archives verification for the 70-format roadmap

    const std::wstring s70ArcDir = TestDataDir() + L"\\samples70";
    const std::wstring arcWorkDir = ScratchTestDir() + L"\\fmt_arc_b70";
    CreateDirectoryW(arcWorkDir.c_str(), nullptr);
    std::vector<std::wstring> createdArcFiles;

    auto writeArcBytes = [&](const wchar_t* name, const std::vector<uint8_t>& bytes) -> std::wstring {
        std::wstring p = arcWorkDir + L"\\" + name;
        PathUtils::WriteFileBytes(p, bytes.data(), bytes.size());
        createdArcFiles.push_back(p);
        return p;
    };
    auto assertArcRejected70 = [&](const wchar_t* testName, const std::wstring& path) {
        ArchiveSummaryInfo bad;
        bool ok = ArchiveExplorer::InspectArchiveFile(path, bad);
        tc.Record(!ok && !bad.valid && bad.entries.empty(), testName,
                  ok ? L"unexpectedly accepted corrupt archive" : L"rejected cleanly");
    };

    // ========================================================================
    // #57. BioWare Aurora / Odyssey Archives (.erf/.mod/.hak/.nwm, .rim, .bif+.key)
    // ========================================================================
    {
        tc.Record(ArchiveExplorer::IsArchiveExtension(L".erf") &&
                  ArchiveExplorer::IsArchiveExtension(L".mod") &&
                  ArchiveExplorer::IsArchiveExtension(L".hak") &&
                  ArchiveExplorer::IsArchiveExtension(L".nwm") &&
                  ArchiveExplorer::IsArchiveExtension(L".rim") &&
                  ArchiveExplorer::IsArchiveExtension(L".bif"),
                  L"Stage15.Archives.BioWareAurora.ExtensionsRegistered");

        ArchiveSummaryInfo erfInfo, rimInfo, bifInfo;
        bool okErf = ArchiveExplorer::InspectArchiveFile(s70ArcDir + L"\\neverwinter_quest.erf", erfInfo);
        bool okRim = ArchiveExplorer::InspectArchiveFile(s70ArcDir + L"\\dantooine_enclave.rim", rimInfo);
        bool okBif = ArchiveExplorer::InspectArchiveFile(s70ArcDir + L"\\aurora_data.bif", bifInfo);

        std::vector<uint8_t> extNss, extLyt, extIni;
        bool exErf = ArchiveExplorer::ExtractSingleEntryToMemory(s70ArcDir + L"\\neverwinter_quest.erf", L"module_init.nss", extNss);
        bool exRim = ArchiveExplorer::ExtractSingleEntryToMemory(s70ArcDir + L"\\dantooine_enclave.rim", L"m13aa.lyt", extLyt);
        bool exBif = ArchiveExplorer::ExtractSingleEntryToMemory(s70ArcDir + L"\\aurora_data.bif", L"nwnplayer.ini", extIni);

        tc.Record(okErf && erfInfo.valid && erfInfo.totalFiles == 2 && exErf && extNss.size() > 20 &&
                  std::string_view(reinterpret_cast<const char*>(extNss.data()), extNss.size()).find("Welcome to Neverwinter") != std::string_view::npos,
                  L"Stage15.Archives.BioWareAurora.SampleErfInspectAndExtract");
        tc.Record(okRim && rimInfo.valid && rimInfo.totalFiles == 2 && exRim && extLyt.size() > 20 &&
                  std::string_view(reinterpret_cast<const char*>(extLyt.data()), extLyt.size()).find("beginlayout") != std::string_view::npos,
                  L"Stage15.Archives.BioWareAurora.SampleRimInspectAndExtract");
        tc.Record(okBif && bifInfo.valid && bifInfo.totalFiles == 2 && exBif && extIni.size() > 15 &&
                  HashBgraBytes(extIni) != HashBgraBytes(extNss) &&
                  std::string_view(reinterpret_cast<const char*>(extIni.data()), extIni.size()).find("Difficulty=2") != std::string_view::npos,
                  L"Stage15.Archives.BioWareAurora.SampleBifWithKeyInspectAndExtract");

        // Negative checks: out-of-bounds ERF resource offset, zero-entry RIM, truncated BIF
        std::vector<uint8_t> badErf(180, 0);
        std::memcpy(badErf.data(), "ERF V1.0", 8);
        uint32_t cnt1 = 1, off160 = 160, off184 = 184;
        std::memcpy(badErf.data() + 16, &cnt1, 4);
        std::memcpy(badErf.data() + 24, &off160, 4);
        std::memcpy(badErf.data() + 28, &off184, 4);
        assertArcRejected70(L"Stage15.Archives.BioWareAurora.RejectTruncatedERF", writeArcBytes(L"bad.erf", badErf));

        std::vector<uint8_t> badRim(120, 0);
        std::memcpy(badRim.data(), "RIM V1.0", 8);
        assertArcRejected70(L"Stage15.Archives.BioWareAurora.RejectZeroEntryRIM", writeArcBytes(L"empty.rim", badRim));

        std::vector<uint8_t> badBif(24, 0);
        std::memcpy(badBif.data(), "BIFFV1  ", 8);
        std::memcpy(badBif.data() + 8, &cnt1, 4);
        uint32_t off20 = 20;
        std::memcpy(badBif.data() + 16, &off20, 4);
        assertArcRejected70(L"Stage15.Archives.BioWareAurora.RejectTruncatedBIF", writeArcBytes(L"bad.bif", badBif));
    }

    // ========================================================================
    // #62. Quake WAD2 & Half-Life WAD3 Texture Archives (.wad)
    // ========================================================================
    {
        ArchiveSummaryInfo wad3Arc, wad2Arc;
        bool okW3 = ArchiveExplorer::InspectArchiveFile(s70ArcDir + L"\\halflife_cstrike.wad", wad3Arc);
        bool okW2 = ArchiveExplorer::InspectArchiveFile(s70ArcDir + L"\\quake1_gfx.wad", wad2Arc);

        std::vector<uint8_t> extMiptex, extPal;
        bool exW3 = ArchiveExplorer::ExtractSingleEntryToMemory(s70ArcDir + L"\\halflife_cstrike.wad", L"BRICK_WALL.miptex", extMiptex);
        bool exW2 = ArchiveExplorer::ExtractSingleEntryToMemory(s70ArcDir + L"\\quake1_gfx.wad", L"PALETTE.pal", extPal);

        tc.Record(okW3 && wad3Arc.valid && wad3Arc.totalFiles == 2 && exW3 && extMiptex.size() == (40u + 256u + 64u + 16u + 4u + 2u + 768u) &&
                  std::memcmp(extMiptex.data(), "BRICK_WALL", 10) == 0 &&
                  okW2 && wad2Arc.valid && wad2Arc.totalFiles == 2 && exW2 && extPal.size() == 768u &&
                  extPal[1 * 3 + 0] == 250,
                  L"Stage15.Archives.RetroGameWad.SampleWad2AndWad3InspectAndExtract");

        std::vector<uint8_t> badWad3 = { 'W','A','D','3', 5,0,0,0, 200,0,0,0, 0,0,0,0 };
        assertArcRejected70(L"Stage15.Archives.RetroGameWad.RejectTruncatedWad3", writeArcBytes(L"bad.wad", badWad3));
    }

    for (const auto& f : createdArcFiles) DeleteFileW(f.c_str());
    RemoveDirectoryW(arcWorkDir.c_str());
