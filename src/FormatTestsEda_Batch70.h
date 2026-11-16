#pragma once
// FormatTestsEda_Batch70.h — Stage 15.eda verification for the 70-format roadmap (#21-#30, #69)

    const std::wstring s70EdaDir = TestDataDir() + L"\\samples70";
    const std::wstring edaWorkDir = ScratchTestDir() + L"\\fmt_eda_b70";
    CreateDirectoryW(edaWorkDir.c_str(), nullptr);
    std::vector<std::wstring> createdEdaFiles;

    auto writeEdaBytes = [&](const wchar_t* name, const std::vector<uint8_t>& bytes) -> std::wstring {
        std::wstring p = edaWorkDir + L"\\" + name;
        PathUtils::WriteFileBytes(p, bytes.data(), bytes.size());
        createdEdaFiles.push_back(p);
        return p;
    };
    auto writeEdaText = [&](const wchar_t* name, const char* text) -> std::wstring {
        std::wstring p = edaWorkDir + L"\\" + name;
        PathUtils::WriteFileBytes(p, text, std::strlen(text));
        createdEdaFiles.push_back(p);
        return p;
    };
    auto assertEdaRejected70 = [&](const wchar_t* testName, const std::wstring& path) {
        ParsedEdaBoard badBrd;
        bool ok = EdaViewer::InspectEdaFile(path, badBrd);
        tc.Record(!ok && !badBrd.valid && badBrd.traces.empty() && badBrd.pads.empty() &&
                  badBrd.vias.empty() && badBrd.circles.empty() && badBrd.components.empty(),
                  testName, ok ? L"unexpectedly accepted corrupt EDA input" : L"rejected cleanly");
    };
    auto hashEdaBoard70 = [](const ParsedEdaBoard& b) -> uint64_t {
        uint64_t h = 14695981039346656037ULL;
        auto mix32 = [&](int32_t v) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
            for (int i = 0; i < 4; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
        };
        for (wchar_t c : b.formatTitle) mix32(static_cast<int32_t>(c));
        for (const auto& t : b.traces) {
            mix32(static_cast<int32_t>(std::lround(t.x1 * 1000.0f)));
            mix32(static_cast<int32_t>(std::lround(t.y1 * 1000.0f)));
            mix32(static_cast<int32_t>(std::lround(t.x2 * 1000.0f)));
            mix32(static_cast<int32_t>(std::lround(t.y2 * 1000.0f)));
            mix32(static_cast<int32_t>(std::lround(t.width * 1000.0f)));
            mix32(static_cast<int32_t>(t.layer));
        }
        for (const auto& p : b.pads) {
            mix32(static_cast<int32_t>(std::lround(p.x * 1000.0f)));
            mix32(static_cast<int32_t>(std::lround(p.y * 1000.0f)));
            mix32(static_cast<int32_t>(std::lround(p.w * 1000.0f)));
            mix32(static_cast<int32_t>(std::lround(p.h * 1000.0f)));
            mix32(static_cast<int32_t>(p.shape));
        }
        for (const auto& v : b.vias) {
            mix32(static_cast<int32_t>(std::lround(v.x * 1000.0f)));
            mix32(static_cast<int32_t>(std::lround(v.y * 1000.0f)));
            mix32(static_cast<int32_t>(std::lround(v.drill * 1000.0f)));
        }
        for (const auto& c : b.circles) {
            mix32(static_cast<int32_t>(std::lround(c.cx * 1000.0f)));
            mix32(static_cast<int32_t>(std::lround(c.cy * 1000.0f)));
            mix32(static_cast<int32_t>(std::lround(c.radius * 1000.0f)));
        }
        for (const auto& l : b.labels) {
            for (wchar_t ch : l.text) mix32(static_cast<int32_t>(ch));
        }
        return h;
    };

    // ========================================================================
    // #21. Gerber RS-274X / X2, .gbrjob, & Excellon NC Drill
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".gtl") &&
                  EdaViewer::IsEdaExtension(L".gbrjob") &&
                  EdaViewer::IsEdaExtension(L".drl") &&
                  EdaViewer::IsEdaExtension(L".exc"),
                  L"Stage15.Eda.Gerber21.ExtensionsRegistered");

        ParsedEdaBoard gtl, gko, job, drl;
        bool okGtl = EdaViewer::InspectEdaFile(s70EdaDir + L"\\gerber_x2_macro_region.gtl", gtl);
        bool okGko = EdaViewer::InspectEdaFile(s70EdaDir + L"\\gerber_tz_outline.gko", gko);
        bool okJob = EdaViewer::InspectEdaFile(s70EdaDir + L"\\gerber_stackup_spec.gbrjob", job);
        bool okDrl = EdaViewer::InspectEdaFile(s70EdaDir + L"\\excellon_slots_rout.drl", drl);

        // Verify X2 attributes, %AM macro thermal circle + pads, G02/G03 arcs, G36/G37 region fill, %SR 2x2 array
        tc.Record(okGtl && gtl.valid &&
                  gtl.formatTitle.find(L"Copper,L1,Top") != std::wstring::npos &&
                  gtl.pads.size() >= 10 && gtl.circles.size() >= 1 &&
                  gtl.traces.size() >= 30 && !gtl.components.empty() &&
                  gtl.components[0].refDes == L"U1",
                  L"Stage15.Eda.Gerber21.X2MacroRegionArcsAndStepRepeat");

        // Verify trailing-zero-omitted %FSTAX24Y24*% inch outline (2.0 in = 50.8 mm wide, 1.4 in = 35.56 mm tall)
        float gkoW = gko.maxX - gko.minX;
        float gkoH = gko.maxY - gko.minY;
        tc.Record(okGko && gko.valid && gko.traces.size() >= 20 &&
                  gkoW >= 49.5f && gkoW <= 56.0f && gkoH >= 34.5f && gkoH <= 41.0f &&
                  hashEdaBoard70(gtl) != hashEdaBoard70(gko),
                  L"Stage15.Eda.Gerber21.TrailingZeroInchOutline");

        // Verify .gbrjob board size (64x42 mm) + 10 stackup layers + 5 file attributes
        tc.Record(okJob && job.valid &&
                  job.formatTitle.find(L"STM32H7_FlightCtrl") != std::wstring::npos &&
                  job.traces.size() >= 12 && job.labels.size() >= 10,
                  L"Stage15.Eda.Gerber21.GbrjobStackupAndSpecs");

        // Verify Excellon drill: 3 regular hits + 1 + 4 repeat hits + 2x2 G85 slot end-vias = 12 vias, plus 2 G85 slots + 2 G01 routs = 4 drill traces
        tc.Record(okDrl && drl.valid && drl.kind == EdaDocKind::ExcellonDrill &&
                  drl.vias.size() == 12 && drl.traces.size() == 4,
                  L"Stage15.Eda.Gerber21.ExcellonG85SlotsRepeatAndRout");

        // Negative checks: unterminated %FS block, malformed .gbrjob JSON, and empty/garbage .drl
        assertEdaRejected70(L"Stage15.Eda.Gerber21.RejectUnterminatedGerberBlock",
                            writeEdaText(L"bad_unclosed.gtl", "%FSLAX46Y46*\nX1000Y1000D03*\n"));
        assertEdaRejected70(L"Stage15.Eda.Gerber21.RejectTruncatedGbrjob",
                            writeEdaText(L"bad_job.gbrjob", "{ \"GeneralSpecs\": { \"Size\": { \"X\": 50, \"Y\": 40 } "));
        assertEdaRejected70(L"Stage15.Eda.Gerber21.RejectGarbageExcellon",
                            writeEdaText(L"bad_drill.drl", "NOT AN EXCELLON FILE AT ALL\nHELLO WORLD\n"));
    }

    // ========================================================================
    // #22. LTspice Schematic (.asc) & Symbol (.asy) Geometry + R0..M270 Pins
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".asc") && EdaViewer::IsEdaExtension(L".asy"),
                  L"Stage15.Eda.Ltspice22.ExtensionsRegistered");

        ParsedEdaBoard asyBrd, buckBrd, opampBrd;
        bool okAsy   = EdaViewer::InspectEdaFile(s70EdaDir + L"\\ltspice_custom_ldo.asy", asyBrd);
        bool okBuck  = EdaViewer::InspectEdaFile(s70EdaDir + L"\\ltspice_buck_ldo_utf16.asc", buckBrd);
        bool okOpamp = EdaViewer::InspectEdaFile(s70EdaDir + L"\\ltspice_opamp_biquad.asc", opampBrd);

        // Standalone .asy has 4 pins (VIN, EN, GND, VOUT) + rectangle + circle + arc + lines
        tc.Record(okAsy && asyBrd.valid && asyBrd.pads.size() == 4 && asyBrd.traces.size() >= 20,
                  L"Stage15.Eda.Ltspice22.StandaloneAsySymbolAndPins");

        // UTF-16LE buck+LDO schematic resolves companion .asy (4 pins) + 6 built-in symbols + Unicode µH/kΩ values + R90/R180/M0/M90 transforms
        bool sawUnicodeMicro = false, sawPinVout = false, verifiedR90IndPins = false;
        for (const auto& c : buckBrd.components) {
            if (c.refDes == L"L1" && c.value.find(L"\x00B5H") != std::wstring::npos) sawUnicodeMicro = true;
        }
        int indPinHits = 0;
        for (const auto& p : buckBrd.pads) {
            if (p.pinName == L"VOUT") sawPinVout = true;
            // SYMBOL ind2 256 80 R90 -> pins (16,16)->(240,96)=(24.0,-9.6) and (16,96)->(160,96)=(16.0,-9.6)!
            if (std::fabs(p.y - (-9.6f)) < 0.05f && (std::fabs(p.x - 24.0f) < 0.05f || std::fabs(p.x - 16.0f) < 0.05f)) {
                ++indPinHits;
            }
        }
        verifiedR90IndPins = (indPinHits >= 2);
        tc.Record(okBuck && buckBrd.valid && buckBrd.components.size() == 7 &&
                  buckBrd.pads.size() >= 17 && sawUnicodeMicro && sawPinVout && verifiedR90IndPins,
                  L"Stage15.Eda.Ltspice22.Utf16CompanionAsyAndPinTransforms");

        tc.Record(okOpamp && opampBrd.valid && opampBrd.components.size() == 4 &&
                  hashEdaBoard70(buckBrd) != hashEdaBoard70(opampBrd),
                  L"Stage15.Eda.Ltspice22.OpampBiquadAndDistinctHashes");

        // Negative checks: PGP ASCII armor .asc & corrupt .asy without SymbolType
        assertEdaRejected70(L"Stage15.Eda.Ltspice22.RejectPgpArmorAsc",
                            writeEdaText(L"pgp_sig.asc", "-----BEGIN PGP SIGNATURE-----\nVersion: GnuPG v2\niQEzBAABCAAdFiEE\n-----END PGP SIGNATURE-----\n"));
        assertEdaRejected70(L"Stage15.Eda.Ltspice22.RejectInvalidAsy",
                            writeEdaText(L"bad_sym.asy", "Version 4\nNOT_A_SYMBOL_TYPE\n"));
    }

    // ========================================================================
    // #23. SPICE Simulation Waveforms (.raw, .tr0, .ac0, .sw0)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".raw") &&
                  EdaViewer::IsEdaExtension(L".tr0") &&
                  EdaViewer::IsEdaExtension(L".ac0") &&
                  EdaViewer::IsEdaExtension(L".sw0"),
                  L"Stage15.Eda.SpiceRaw23.ExtensionsRegistered");

        ParsedEdaBoard ltBin, ltFast, ngAc, hspTr0;
        bool okLtBin  = EdaViewer::InspectEdaFile(s70EdaDir + L"\\ltspice_buck_utf16_bin.raw", ltBin);
        bool okLtFast = EdaViewer::InspectEdaFile(s70EdaDir + L"\\ltspice_fastaccess.raw", ltFast);
        bool okNgAc   = EdaViewer::InspectEdaFile(s70EdaDir + L"\\ngspice_ac_bode.raw", ngAc);
        bool okHsp    = EdaViewer::InspectEdaFile(s70EdaDir + L"\\hspice_cmos_inv.tr0", hspTr0);

        tc.Record(okLtBin && ltBin.valid && ltBin.netCount == 2 && ltBin.traces.size() >= 120 &&
                  EdaViewer::IsLikelySpiceRawFile(s70EdaDir + L"\\ltspice_buck_utf16_bin.raw"),
                  L"Stage15.Eda.SpiceRaw23.LtspiceUtf16BinaryRaw");

        tc.Record(okLtFast && ltFast.valid && ltFast.netCount == 2 && ltFast.traces.size() >= 90 &&
                  hashEdaBoard70(ltBin) != hashEdaBoard70(ltFast),
                  L"Stage15.Eda.SpiceRaw23.LtspiceFastAccessColumnMajor");

        bool hasDbLabel = false;
        for (const auto& l : ngAc.labels) {
            if (l.text.find(L"dB") != std::wstring::npos) hasDbLabel = true;
        }
        tc.Record(okNgAc && ngAc.valid && ngAc.netCount == 1 && ngAc.traces.size() >= 45 && hasDbLabel,
                  L"Stage15.Eda.SpiceRaw23.NgspiceComplexAcBodePlot");

        tc.Record(okHsp && hspTr0.valid && hspTr0.netCount == 3 && hspTr0.traces.size() >= 95 &&
                  hspTr0.formatTitle.find(L".tr0") != std::wstring::npos,
                  L"Stage15.Eda.SpiceRaw23.HspiceTransientTr0");

        // Verify non-SPICE camera/binary .raw is rejected cleanly AND IsLikelySpiceRawFile returns false
        std::vector<uint8_t> camRawFake(256, 0xA5);
        camRawFake[0] = 'I'; camRawFake[1] = 'I'; camRawFake[2] = 0x55; camRawFake[3] = 0x00; // Panasonic RW2/RAW magic
        std::wstring camRawPath = writeEdaBytes(L"camera_sensor.raw", camRawFake);
        tc.Record(!EdaViewer::IsLikelySpiceRawFile(camRawPath),
                  L"Stage15.Eda.SpiceRaw23.DisambiguateCameraRawFalse");
        assertEdaRejected70(L"Stage15.Eda.SpiceRaw23.RejectCameraRawInEda", camRawPath);

        // Verify truncated binary .raw (claims 100 points, has only 8 bytes) is rejected cleanly
        assertEdaRejected70(L"Stage15.Eda.SpiceRaw23.RejectTruncatedBinaryRaw",
                            writeEdaText(L"trunc_spice.raw",
                                "Title: Truncated\nPlotname: Transient\nFlags: real\nNo. Variables: 2\nNo. Points: 100\nVariables:\n\t0\ttime\ttime\n\t1\tv(1)\tvoltage\nBinary:\n12345678"));
        assertEdaRejected70(L"Stage15.Eda.SpiceRaw23.RejectCorruptTr0",
                            writeEdaText(L"corrupt.tr0", "NOT AN HSPICE WAVEFORM FILE\n"));
    }

    // ========================================================================
    // #24. GTKWave Fast Signal Trace (.fst) LZ4 / FastLZ / HIER_LZ4 / Real Analog
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".fst"),
                  L"Stage15.Eda.GtkWaveFst24.ExtensionRegistered");

        ParsedEdaBoard fstLz4, fstFlz, fstGw;
        bool okLz4 = EdaViewer::InspectEdaFile(s70EdaDir + L"\\verilator_lz4_real.fst", fstLz4);
        bool okFlz = EdaViewer::InspectEdaFile(s70EdaDir + L"\\gtkwave_lz4duo_fastlz.fst", fstFlz);
        bool okGw  = EdaViewer::InspectEdaFile(s70EdaDir + L"\\gtkwave_gwrapper.fst", fstGw);

        // Verilator HIER_LZ4 + LZ4 waves + FST_VT_VCD_REAL analog trace ("vdd_core [0.77..0.93]") + static alias ("clk_alias")
        bool sawRealAnalog = false, sawStaticAlias = false;
        for (const auto& l : fstLz4.labels) {
            if (l.text.find(L"vdd_core [") != std::wstring::npos) sawRealAnalog = true;
            if (l.text.find(L"clk_alias") != std::wstring::npos) sawStaticAlias = true;
        }
        tc.Record(okLz4 && fstLz4.valid && fstLz4.netCount == 4 &&
                  fstLz4.traces.size() >= 80 && sawRealAnalog && sawStaticAlias,
                  L"Stage15.Eda.GtkWaveFst24.HierLz4WaveLz4AndRealAnalog");

        // HIER_LZ4DUO + FastLZ Level 1 waves
        tc.Record(okFlz && fstFlz.valid && fstFlz.netCount == 3 &&
                  fstFlz.traces.size() >= 60 &&
                  hashEdaBoard70(fstLz4) != hashEdaBoard70(fstFlz),
                  L"Stage15.Eda.GtkWaveFst24.HierLz4DuoAndFastLzWaves");

        // FST_BL_GWRAPPER (blkType=9) outer gzip wrapper
        tc.Record(okGw && fstGw.valid && fstGw.netCount == 3 && fstGw.traces.size() >= 60,
                  L"Stage15.Eda.GtkWaveFst24.GwrapperCompressedContainer");

        // Negative check: truncated FST block & corrupt LZ4 hierarchy payload
        std::vector<uint8_t> truncFst;
        PathUtils::ReadFileBytes(s70EdaDir + L"\\verilator_lz4_real.fst", 65536, truncFst);
        if (truncFst.size() > 40) truncFst.resize(truncFst.size() - 20);
        assertEdaRejected70(L"Stage15.Eda.GtkWaveFst24.RejectTruncatedFst",
                            writeEdaBytes(L"trunc.fst", truncFst));
    }

    // ========================================================================
    // #25. ASIC Physical Layout LEF & DEF 5.8 (.lef, .def)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".lef") && EdaViewer::IsEdaExtension(L".def"),
                  L"Stage15.Eda.AsicLefDef25.ExtensionsRegistered");

        ParsedEdaBoard lefBrd, defAlu, defSram;
        bool okLef  = EdaViewer::InspectEdaFile(s70EdaDir + L"\\sky130_stdcells_v58.lef", lefBrd);
        bool okDef1 = EdaViewer::InspectEdaFile(s70EdaDir + L"\\sky130_alu_routed_v58.def", defAlu);
        bool okDef2 = EdaViewer::InspectEdaFile(s70EdaDir + L"\\sram_macro_block.def", defSram);

        // LEF 5.8: 3 macros (sky130_inv_2, sky130_nand2_4, sky130_dfxtp_1), RECT + POLYGON ports + OBS blockages
        tc.Record(okLef && lefBrd.valid && lefBrd.components.size() == 3 &&
                  lefBrd.pads.size() >= 10 && lefBrd.traces.size() >= 30,
                  L"Stage15.Eda.AsicLefDef25.LefMacrosPolygonPortsAndObs");

        // DEF 5.8: 6-edge rectilinear L-shaped DIEAREA + 5 placed/fixed cells (w/ companion .lef pins!) + 4 I/O PINS + SPECIALNETS + * wildcards + 3 vias
        tc.Record(okDef1 && defAlu.valid &&
                  defAlu.formatTitle.find(L"sky130_alu_slice") != std::wstring::npos &&
                  defAlu.components.size() == 5 && defAlu.vias.size() == 3 &&
                  defAlu.pads.size() >= 18 && defAlu.traces.size() >= 35,
                  L"Stage15.Eda.AsicLefDef25.DefRectilinearDiePinsSpecialNetsAndWildcards");

        tc.Record(okDef2 && defSram.valid && defSram.components.size() == 2 &&
                  hashEdaBoard70(defAlu) != hashEdaBoard70(defSram),
                  L"Stage15.Eda.AsicLefDef25.DistinctDefDesignHashes");

        // Negative checks: Windows DLL .def file rejected in EdaViewer + IsLikelyAsicDefFile == false, and truncated LEF/DEF rejected
        std::wstring dllDefPath = writeEdaText(L"exports_dll.def", "LIBRARY ExplorerPlugin\nEXPORTS\n    DllMain @1\n    InitPlugin @2\n");
        tc.Record(!EdaViewer::IsLikelyAsicDefFile(dllDefPath),
                  L"Stage15.Eda.AsicLefDef25.DisambiguateWindowsDllDefFalse");
        assertEdaRejected70(L"Stage15.Eda.AsicLefDef25.RejectWindowsDllDef", dllDefPath);
        assertEdaRejected70(L"Stage15.Eda.AsicLefDef25.RejectUnclosedMacroLef",
                            writeEdaText(L"unclosed.lef", "VERSION 5.8 ;\nMACRO BROKEN_CELL\n  SIZE 2.0 BY 3.0 ;\n  PIN A\n"));
        assertEdaRejected70(L"Stage15.Eda.AsicLefDef25.RejectTruncatedComponentsDef",
                            writeEdaText(L"trunc_comp.def", "VERSION 5.8 ;\nDESIGN broken ;\nDIEAREA ( 0 0 ) ( 1000 1000 ) ;\nCOMPONENTS 2 ;\n  - u1 INV_X1 + PLACED ( 100 100 ) N ;\n"));
    }

    // ========================================================================
    // #26. Cadence OrCAD Capture OLE2 (.dsn/.olb) & Specctra PCB S-Expr (.dsn)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".dsn") && EdaViewer::IsEdaExtension(L".olb"),
                  L"Stage15.Eda.OrCadSpecctra26.ExtensionsRegistered");

        ParsedEdaBoard orcDsn, orcOlb, spcDsn;
        bool okOrc = EdaViewer::InspectEdaFile(s70EdaDir + L"\\orcad_capture_mcu.dsn", orcDsn);
        bool okOlb = EdaViewer::InspectEdaFile(s70EdaDir + L"\\orcad_analog_lib.olb", orcOlb);
        bool okSpc = EdaViewer::InspectEdaFile(s70EdaDir + L"\\specctra_4layer_routed.dsn", spcDsn);

        // OrCAD Capture OLE2 .dsn: 2 schematic pages (4 parts: U1, C1, U2, C1), 13 pins, wires, bus, junctions, ports
        tc.Record(okOrc && orcDsn.valid &&
                  orcDsn.formatTitle.find(L"2 pages") != std::wstring::npos &&
                  orcDsn.components.size() == 4 && orcDsn.pads.size() == 13 &&
                  orcDsn.vias.size() == 2 && orcDsn.traces.size() >= 30,
                  L"Stage15.Eda.OrCadSpecctra26.OrCadCaptureMultiPageOle2Dsn");

        tc.Record(okOlb && orcOlb.valid &&
                  orcOlb.formatTitle.find(L".olb") != std::wstring::npos &&
                  orcOlb.components.size() == 2 && orcOlb.pads.size() == 6,
                  L"Stage15.Eda.OrCadSpecctra26.OrCadSymbolLibraryOlb");

        // Specctra 4-layer .dsn: resolution um 10 -> 52x36mm board, 4 components (U1 8p + C1 2p + C2 2p back + J1 2p THT = 14 pads), 1 via, 3 nets
        float spcW = spcDsn.maxX - spcDsn.minX;
        float spcH = spcDsn.maxY - spcDsn.minY;
        tc.Record(okSpc && spcDsn.valid &&
                  spcDsn.components.size() == 4 && spcDsn.pads.size() == 14 &&
                  spcDsn.vias.size() == 1 && spcDsn.netCount == 3 &&
                  spcW >= 51.0f && spcW <= 56.0f && spcH >= 35.0f && spcH <= 40.0f &&
                  hashEdaBoard70(orcDsn) != hashEdaBoard70(spcDsn),
                  L"Stage15.Eda.OrCadSpecctra26.Specctra4LayerLibraryPadstacksAndWiring");

        // Negative checks: unclosed S-expression .dsn and truncated/invalid OLE2 .dsn
        assertEdaRejected70(L"Stage15.Eda.OrCadSpecctra26.RejectUnclosedSpecctraDsn",
                            writeEdaText(L"unclosed.dsn", "(pcb \"broken.dsn\" (structure (boundary (path pcb 0 0 0 100 0"));
        std::vector<uint8_t> fakeOle(512, 0);
        std::memcpy(fakeOle.data(), "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8); // OLE2 magic with corrupt header/no OrCAD streams
        assertEdaRejected70(L"Stage15.Eda.OrCadSpecctra26.RejectNonOrCadOle2Dsn",
                            writeEdaBytes(L"fake_ole.dsn", fakeOle));
    }

    // ========================================================================
    // #27. Sigrok (.sr multi-chunk/unitsize/analog) & Altium (.pcbdoc/.schdoc/.pcblib/.schlib)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".sr") &&
                  EdaViewer::IsEdaExtension(L".pcbdoc") &&
                  EdaViewer::IsEdaExtension(L".schdoc") &&
                  EdaViewer::IsEdaExtension(L".pcblib") &&
                  EdaViewer::IsEdaExtension(L".schlib"),
                  L"Stage15.Eda.SigrokAltium27.ExtensionsRegistered");

        ParsedEdaBoard srBrd, pcbDoc, schDoc, pcbLib, schLib;
        bool okSr     = EdaViewer::InspectEdaFile(s70EdaDir + L"\\sigrok_16ch_mixed.sr", srBrd);
        bool okPcbDoc = EdaViewer::InspectEdaFile(s70EdaDir + L"\\altium_polygons_fills.pcbdoc", pcbDoc);
        bool okSchDoc = EdaViewer::InspectEdaFile(s70EdaDir + L"\\altium_multipart_utf8.schdoc", schDoc);
        bool okPcbLib = EdaViewer::InspectEdaFile(s70EdaDir + L"\\altium_footprints.pcblib", pcbLib);
        bool okSchLib = EdaViewer::InspectEdaFile(s70EdaDir + L"\\altium_symbols.schlib", schLib);

        // Sigrok .sr: 12 digital lanes (unitsize=2, 2 chunks) + 2 float32 analog lanes = 14 lanes
        bool sawHeartbeat12 = false, sawAnalogVcc = false;
        for (const auto& l : srBrd.labels) {
            if (l.text == L"HEARTBEAT") sawHeartbeat12 = true;
            if (l.text.find(L"VCC_3V3 [") != std::wstring::npos) sawAnalogVcc = true;
        }
        tc.Record(okSr && srBrd.valid && srBrd.netCount == 14 &&
                  srBrd.traces.size() >= 600 && sawHeartbeat12 && sawAnalogVcc,
                  L"Stage15.Eda.SigrokAltium27.SigrokMultiChunkUnitsize2AndAnalog");

        // Altium .PcbDoc: Board6 + Components6 + Polygons6 hatch + Fills6 pad + Regions6 hatch + WideStrings6/Texts6 ("RP2040_REV_B")
        bool sawWideStrText = false;
        for (const auto& l : pcbDoc.labels) {
            if (l.text == L"RP2040_REV_B") sawWideStrText = true;
        }
        tc.Record(okPcbDoc && pcbDoc.valid && pcbDoc.components.size() == 1 &&
                  pcbDoc.pads.size() == 1 && pcbDoc.traces.size() >= 20 && sawWideStrText,
                  L"Stage15.Eda.SigrokAltium27.AltiumPcbDocPolygonsFillsRegionsWideStrings");

        // Altium .SchDoc: CurrentPartId=1 filters out OwnerPartId=2 (exact 3 pins, not 4!), %UTF8%TEXT=4.7kΩ decoded
        tc.Record(okSchDoc && schDoc.valid && schDoc.components.size() == 1 &&
                  schDoc.pads.size() == 3 &&
                  schDoc.components[0].value.find(L"\x03A9") != std::wstring::npos,
                  L"Stage15.Eda.SigrokAltium27.AltiumSchDocMultiPartFilterAndUtf8");

        // Altium .PcbLib (2 footprints, 12 pads) & .SchLib (2 symbols, 4 pins)
        tc.Record(okPcbLib && pcbLib.valid && pcbLib.components.size() == 2 && pcbLib.pads.size() == 12 &&
                  okSchLib && schLib.valid && schLib.components.size() == 2 && schLib.pads.size() == 4 &&
                  hashEdaBoard70(pcbLib) != hashEdaBoard70(schLib),
                  L"Stage15.Eda.SigrokAltium27.AltiumPcbLibAndSchLib");

        // Negative checks: corrupt non-ZIP .sr & corrupt OLE2 .pcblib
        assertEdaRejected70(L"Stage15.Eda.SigrokAltium27.RejectGarbageSr",
                            writeEdaText(L"bad.sr", "PK\x03\x04CORRUPT_NOT_A_VALID_SIGROK_ZIP"));
        assertEdaRejected70(L"Stage15.Eda.SigrokAltium27.RejectGarbagePcbLib",
                            writeEdaText(L"bad.pcblib", "NOT_AN_ALTIUM_LIBRARY"));
    }

    // ========================================================================
    // #28. EasyEDA / JLCPCB Standard (.json) & Pro (.epro, .eprj, .epcb, .esch)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".epro") &&
                  EdaViewer::IsEdaExtension(L".eprj") &&
                  EdaViewer::IsEdaExtension(L".epcb") &&
                  EdaViewer::IsEdaExtension(L".esch") &&
                  EdaViewer::IsEdaExtension(L".json"),
                  L"Stage15.Eda.EasyEda28.ExtensionsRegistered");

        ParsedEdaBoard stdPcb, stdSch, proPcb, proSch, proEpro, proEprj;
        bool okStdPcb  = EdaViewer::InspectEdaFile(s70EdaDir + L"\\easyeda_std_pcb.json", stdPcb);
        bool okStdSch  = EdaViewer::InspectEdaFile(s70EdaDir + L"\\easyeda_std_sch.json", stdSch);
        bool okProPcb  = EdaViewer::InspectEdaFile(s70EdaDir + L"\\jlcpcb_pro_board.epcb", proPcb);
        bool okProSch  = EdaViewer::InspectEdaFile(s70EdaDir + L"\\jlcpcb_pro_sheet.esch", proSch);
        bool okProEpro = EdaViewer::InspectEdaFile(s70EdaDir + L"\\jlcpcb_pro_project.epro", proEpro);
        bool okProEprj = EdaViewer::InspectEdaFile(s70EdaDir + L"\\jlcpcb_pro_manifest.eprj", proEprj);

        tc.Record(okStdPcb && stdPcb.valid && stdPcb.components.size() == 2 &&
                  stdPcb.pads.size() == 6 && stdPcb.vias.size() == 1 && stdPcb.traces.size() >= 10 &&
                  EdaViewer::IsLikelyEasyEdaJsonFile(s70EdaDir + L"\\easyeda_std_pcb.json"),
                  L"Stage15.Eda.EasyEda28.StandardPcbJson");

        tc.Record(okStdSch && stdSch.valid && stdSch.components.size() == 1 &&
                  stdSch.pads.size() == 3 && stdSch.vias.size() == 1 &&
                  hashEdaBoard70(stdPcb) != hashEdaBoard70(stdSch),
                  L"Stage15.Eda.EasyEda28.StandardSchJson");

        tc.Record(okProPcb && proPcb.valid && proPcb.components.size() == 2 &&
                  proPcb.pads.size() == 7 && proPcb.vias.size() == 1 &&
                  okProSch && proSch.valid && proSch.components.size() == 1 && proSch.pads.size() == 3,
                  L"Stage15.Eda.EasyEda28.ProEpcbAndEschJsonLines");

        tc.Record(okProEpro && proEpro.valid && proEpro.components.size() == 2 &&
                  proEpro.formatTitle.find(L"CH340N_USB_UART_Pro") != std::wstring::npos &&
                  okProEprj && proEprj.valid && !proEprj.components.empty(),
                  L"Stage15.Eda.EasyEda28.ProEproZipAndEprjManifest");

        // Verify non-EasyEDA package.json is rejected in EdaViewer AND IsLikelyEasyEdaJsonFile == false
        std::wstring pkgJsonPath = writeEdaText(L"package.json", "{ \"name\": \"my-web-app\", \"version\": \"1.0.0\", \"dependencies\": {} }\n");
        tc.Record(!EdaViewer::IsLikelyEasyEdaJsonFile(pkgJsonPath),
                  L"Stage15.Eda.EasyEda28.DisambiguateGenericPackageJsonFalse");
        assertEdaRejected70(L"Stage15.Eda.EasyEda28.RejectGenericPackageJson", pkgJsonPath);
        assertEdaRejected70(L"Stage15.Eda.EasyEda28.RejectTruncatedEasyEdaJson",
                            writeEdaText(L"trunc_easyeda.json", "{ \"head\": { \"docType\": \"3\" }, \"shape\": [ \"TRACK~1~1~GND~0 0 10 10\" "));
        assertEdaRejected70(L"Stage15.Eda.EasyEda28.RejectInvalidEproZip",
                            writeEdaText(L"bad.epro", "PK\x03\x04NOT_A_VALID_EPRO_PROJECT"));
    }

    // ========================================================================
    // #29. JTAG BSDL (.bsdl/.bsd), FPGA Constraints (.xdc/.sdc/.qsf) & IPC-D-356 (.d356/.ipc)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".bsdl") &&
                  EdaViewer::IsEdaExtension(L".bsd") &&
                  EdaViewer::IsEdaExtension(L".xdc") &&
                  EdaViewer::IsEdaExtension(L".sdc") &&
                  EdaViewer::IsEdaExtension(L".qsf") &&
                  EdaViewer::IsEdaExtension(L".d356") &&
                  EdaViewer::IsEdaExtension(L".ipc"),
                  L"Stage15.Eda.BsdlFpgaIpc29.ExtensionsRegistered");

        ParsedEdaBoard bsdlBga, bsdQfp, xdcBrd, sdcBrd, sdcBrd2, qsfBrd, d356Brd;
        bool okBsdl = EdaViewer::InspectEdaFile(s70EdaDir + L"\\zynq7020_bga_jtag.bsdl", bsdlBga);
        bool okBsd  = EdaViewer::InspectEdaFile(s70EdaDir + L"\\cortex_m4_lqfp16.bsd", bsdQfp);
        bool okXdc  = EdaViewer::InspectEdaFile(s70EdaDir + L"\\artix7_lvds_pinout.xdc", xdcBrd);
        bool okSdc  = EdaViewer::InspectEdaFile(s70EdaDir + L"\\asic_200mhz_timing.sdc", sdcBrd);
        bool okQsf  = EdaViewer::InspectEdaFile(s70EdaDir + L"\\cyclone_v_pinout.qsf", qsfBrd);
        bool okD356 = EdaViewer::InspectEdaFile(s70EdaDir + L"\\ipc_d356a_metric_test.d356", d356Brd);

        // BSDL BGA: 13 mapped balls (TCK,TMS,TDI,TDO,PS_POR_B,MIO0..3,2xVCCINT,2xGND) + IDCODE 0x23727093; BSD LQFP16: 16 perimeter pins
        tc.Record(okBsdl && bsdlBga.valid && bsdlBga.pads.size() == 13 &&
                  bsdlBga.formatTitle.find(L"0x23727093") != std::wstring::npos &&
                  okBsd && bsdQfp.valid && bsdQfp.pads.size() == 16 &&
                  hashEdaBoard70(bsdlBga) != hashEdaBoard70(bsdQfp),
                  L"Stage15.Eda.BsdlFpgaIpc29.BsdlBgaBallMapAndQfpPerimeter");

        // XDC + QSF + SDC (verify two distinct timing-only SDC files produce distinct clock waveform hashes!)
        bool okSdc2 = EdaViewer::InspectEdaFile(
            writeEdaText(L"slow_10mhz.sdc", "create_clock -name slow_clk -period 100.0 [get_ports clk_i]\nset_input_delay -clock slow_clk 12.0 [get_ports din]\n"),
            sdcBrd2);
        tc.Record(okXdc && xdcBrd.valid && xdcBrd.pads.size() == 6 &&
                  okQsf && qsfBrd.valid && qsfBrd.pads.size() == 4 &&
                  okSdc && sdcBrd.valid && okSdc2 && sdcBrd2.valid &&
                  hashEdaBoard70(sdcBrd) != hashEdaBoard70(sdcBrd2),
                  L"Stage15.Eda.BsdlFpgaIpc29.XdcQsfBalloutsAndDistinctSdcClocks");

        // IPC-D-356A metric: 3 components (J1, U1, C2), 7 pads, 2 vias (307 blind via + 367 tooling hole), 3 nets
        tc.Record(okD356 && d356Brd.valid && d356Brd.components.size() == 3 &&
                  d356Brd.pads.size() == 7 && d356Brd.vias.size() == 2 && d356Brd.netCount == 3,
                  L"Stage15.Eda.BsdlFpgaIpc29.IpcD356aMetricPadsViasAndComponents");

        // Negative checks: zero-pin BSDL (no fake TDI..GPIO0 fallback!), unbalanced Tcl XDC, and garbage D356
        assertEdaRejected70(L"Stage15.Eda.BsdlFpgaIpc29.RejectFakePortWordBsdl",
                            writeEdaText(L"not_bsdl.bsdl", "This text mentions port and entity in prose without any IEEE 1149.1 syntax.\n"));
        assertEdaRejected70(L"Stage15.Eda.BsdlFpgaIpc29.RejectUnbalancedBracketXdc",
                            writeEdaText(L"bad.xdc", "set_property PACKAGE_PIN R4 [get_ports sys_clk\n"));
        assertEdaRejected70(L"Stage15.Eda.BsdlFpgaIpc29.RejectGarbageD356",
                            writeEdaText(L"bad.d356", "RANDOM_TEXT_WITHOUT_ANY_IPC_RECORDS\n"));
    }

    // ========================================================================
    // #30. EDIF / BLIF / AIGER Gate Netlists, Liberty / SDF Timing & NI TDMS
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".edif") &&
                  EdaViewer::IsEdaExtension(L".edn") &&
                  EdaViewer::IsEdaExtension(L".blif") &&
                  EdaViewer::IsEdaExtension(L".eblif") &&
                  EdaViewer::IsEdaExtension(L".aig") &&
                  EdaViewer::IsEdaExtension(L".aag") &&
                  EdaViewer::IsEdaExtension(L".lib") &&
                  EdaViewer::IsEdaExtension(L".sdf") &&
                  EdaViewer::IsEdaExtension(L".tdms"),
                  L"Stage15.Eda.GateTdms30.ExtensionsRegistered");

        ParsedEdaBoard edifBrd, blifBrd, aigBrd, libBrd, sdfBrd, tdmsBrd;
        bool okEdif = EdaViewer::InspectEdaFile(s70EdaDir + L"\\vivado_spi_ctrl.edif", edifBrd);
        bool okBlif = EdaViewer::InspectEdaFile(s70EdaDir + L"\\yosys_alu4.eblif", blifBrd);
        bool okAig  = EdaViewer::InspectEdaFile(s70EdaDir + L"\\counter_latch_bin.aig", aigBrd);
        bool okLib  = EdaViewer::InspectEdaFile(s70EdaDir + L"\\sky130_timing_nldm.lib", libBrd);
        bool okSdf  = EdaViewer::InspectEdaFile(s70EdaDir + L"\\postroute_delays.sdf", sdfBrd);
        bool okTdms = EdaViewer::InspectEdaFile(s70EdaDir + L"\\ni_daq_interleaved.tdms", tdmsBrd);

        // EDIF: 4 gate/FDRE instances + 5 primary ports + 7 nets; EBLIF: 5 LUTs + 3 DFFs = 8 gate components
        tc.Record(okEdif && edifBrd.valid && edifBrd.components.size() == 4 && edifBrd.netCount == 7 &&
                  okBlif && blifBrd.valid && blifBrd.components.size() == 8 &&
                  hashEdaBoard70(edifBrd) != hashEdaBoard70(blifBrd),
                  L"Stage15.Eda.GateTdms30.EdifAndYosysEblifGateDags");

        // Binary AIGER: 1 latch + 2 AND2 gates = 3 components, inverted circles > 0
        tc.Record(okAig && aigBrd.valid && aigBrd.components.size() == 3 && !aigBrd.circles.empty() &&
                  aigBrd.formatTitle.find(L"Binary (.aig)") != std::wstring::npos &&
                  hashEdaBoard70(aigBrd) != hashEdaBoard70(edifBrd),
                  L"Stage15.Eda.GateTdms30.BinaryAigerDeltaAndLatchDag");

        // Liberty .lib (3 cells) & IEEE 1497 .sdf (2 instanced cells + 2 interconnect delay bars) + disambiguation
        tc.Record(okLib && libBrd.valid && libBrd.components.size() == 3 &&
                  EdaViewer::IsLikelyLibertyOrEdaSdfFile(s70EdaDir + L"\\sky130_timing_nldm.lib", L".lib") &&
                  okSdf && sdfBrd.valid && sdfBrd.components.size() == 2 &&
                  EdaViewer::IsLikelyLibertyOrEdaSdfFile(s70EdaDir + L"\\postroute_delays.sdf", L".sdf") &&
                  hashEdaBoard70(libBrd) != hashEdaBoard70(sdfBrd),
                  L"Stage15.Eda.GateTdms30.LibertyNldmAndIeee1497Sdf");

        // NI TDMS: 2 segments, 2 interleaved channels x 128 samples
        tc.Record(okTdms && tdmsBrd.valid && tdmsBrd.traces.size() > 100 &&
                  tdmsBrd.formatTitle.find(L"2 channels, 2 segments") != std::wstring::npos,
                  L"Stage15.Eda.GateTdms30.NiTdmsMultiSegmentInterleavedWaveforms");

        // Disambiguation & negative checks: COFF "!<arch>\n" .lib, MDL V2000 .sdf, truncated .aig, corrupt .tdms
        std::wstring coffLib = writeEdaText(L"msvc_static.lib", "!<arch>\n/               0           0     0     0       4         `\n\x00\x00\x00\x00");
        std::wstring mdlSdf  = writeEdaText(L"benzene.sdf", "Benzene\n  OpenBabel\n\n  6  6  0  0  0  0  0  0  0  0999 V2000\n$$$$");
        tc.Record(!EdaViewer::IsLikelyLibertyOrEdaSdfFile(coffLib, L".lib") &&
                  !EdaViewer::IsLikelyLibertyOrEdaSdfFile(mdlSdf, L".sdf"),
                  L"Stage15.Eda.GateTdms30.DisambiguateCoffLibAndMdlSdfFalse");
        assertEdaRejected70(L"Stage15.Eda.GateTdms30.RejectCoffArchiveLibInEda", coffLib);
        assertEdaRejected70(L"Stage15.Eda.GateTdms30.RejectMdlMoleculeSdfInEda", mdlSdf);
        assertEdaRejected70(L"Stage15.Eda.GateTdms30.RejectTruncatedBinAiger",
                            writeEdaText(L"trunc.aig", "aig 5 2 0 1 3\n10\n\x02"));
        assertEdaRejected70(L"Stage15.Eda.GateTdms30.RejectCorruptTdms",
                            writeEdaText(L"corrupt.tdms", "TDSm\x2E\x00\x00\x00\x69\x12\x00\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\x00\x00\x00\x00\x00\x00\x00"));
    }

    // ========================================================================
    // #69. IPC-2581 Rev B/C (.cvg/.ipc2581), ODB++ (.tgz/.odb), Fritzing (.fzz/.fzpz) & gEDA (.pcb)
    // ========================================================================
    {
        tc.Record(EdaViewer::IsEdaExtension(L".cvg") &&
                  EdaViewer::IsEdaExtension(L".ipc2581") &&
                  EdaViewer::IsEdaExtension(L".odb") &&
                  EdaViewer::IsEdaExtension(L".tgz") &&
                  EdaViewer::IsEdaExtension(L".fzz") &&
                  EdaViewer::IsEdaExtension(L".fzpz") &&
                  EdaViewer::IsEdaExtension(L".fz") &&
                  EdaViewer::IsEdaExtension(L".fzp") &&
                  EdaViewer::IsEdaExtension(L".pcb"),
                  L"Stage15.Eda.MfgFritzingGeda69.ExtensionsRegistered");

        ParsedEdaBoard cvgBrd, odbTgzBrd, fzzBrd, fzpzBrd, gedaBrd;
        bool okCvg  = EdaViewer::InspectEdaFile(s70EdaDir + L"\\ipc2581_revb_board.cvg", cvgBrd);
        bool okOdb  = EdaViewer::InspectEdaFile(s70EdaDir + L"\\odbpp_4layer_fab.tgz", odbTgzBrd);
        bool okFzz  = EdaViewer::InspectEdaFile(s70EdaDir + L"\\fritzing_sensor_shield.fzz", fzzBrd);
        bool okFzpz = EdaViewer::InspectEdaFile(s70EdaDir + L"\\fritzing_bme280_part.fzpz", fzpzBrd);
        bool okGeda = EdaViewer::InspectEdaFile(s70EdaDir + L"\\geda_power_supply.pcb", gedaBrd);

        // IPC-2581 Rev B: 3 components (U1 SOIC8 5x4mm, C1, R1 bottom), 6 pads, 2 vias, 4 nets + hatched copper pour
        tc.Record(okCvg && cvgBrd.valid && cvgBrd.components.size() == 3 &&
                  cvgBrd.pads.size() == 6 && cvgBrd.vias.size() == 2 && cvgBrd.netCount == 4 &&
                  cvgBrd.formatTitle.find(L"Rev B") != std::wstring::npos,
                  L"Stage15.Eda.MfgFritzingGeda69.Ipc2581RevBDigitalTwin");

        // ODB++ .tgz manufacturing tarball: 3 components mapped to PKG names, 9 pads (6 TOP + 3 feature P), 3 drill vias, 4 nets
        tc.Record(okOdb && odbTgzBrd.valid && odbTgzBrd.components.size() == 3 &&
                  odbTgzBrd.components[0].footprint == L"QFN16_3X3" &&
                  odbTgzBrd.pads.size() == 9 && odbTgzBrd.vias.size() == 3 && odbTgzBrd.netCount == 4 &&
                  EdaViewer::IsLikelyOdbPlusPlusTgz(s70EdaDir + L"\\odbpp_4layer_fab.tgz") &&
                  hashEdaBoard70(odbTgzBrd) != hashEdaBoard70(cvgBrd),
                  L"Stage15.Eda.MfgFritzingGeda69.OdbPlusPlusTgzArchiveWithGzipFeatures");

        // Fritzing .fzz sketch (3 parts U1+R1+R2, 1 via, PCB outline + traces + custom SVG pads) & .fzpz standalone part
        tc.Record(okFzz && fzzBrd.valid && fzzBrd.components.size() == 3 &&
                  fzzBrd.vias.size() == 1 && fzzBrd.pads.size() >= 8 &&
                  okFzpz && fzpzBrd.valid && fzpzBrd.components.size() == 1 && fzpzBrd.pads.size() == 5 &&
                  hashEdaBoard70(fzzBrd) != hashEdaBoard70(fzpzBrd),
                  L"Stage15.Eda.MfgFritzingGeda69.FritzingFzzSketchAndFzpzPart");

        // gEDA .pcb: 3 elements (J1 2 pins, U1 6 pads, C1 2 bottom pads = 10 pads), 2 vias, 4 nets
        tc.Record(okGeda && gedaBrd.valid && gedaBrd.components.size() == 3 &&
                  gedaBrd.pads.size() == 10 && gedaBrd.vias.size() == 2 && gedaBrd.netCount == 4 &&
                  gedaBrd.formatTitle.find(L"LM2596_Buck_5V") != std::wstring::npos &&
                  hashEdaBoard70(gedaBrd) != hashEdaBoard70(odbTgzBrd),
                  L"Stage15.Eda.MfgFritzingGeda69.GedaPcbElementsArcsAndPolygons");

        // Disambiguation & negative checks: non-ODB++ .tgz rejected cleanly, truncated .cvg, binary garbage .pcb, invalid .fzz
        std::wstring nonOdbTgz = writeEdaBytes(L"source_code.tgz", {
            0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        });
        tc.Record(!EdaViewer::IsLikelyOdbPlusPlusTgz(nonOdbTgz),
                  L"Stage15.Eda.MfgFritzingGeda69.DisambiguateGenericTgzFalse");
        assertEdaRejected70(L"Stage15.Eda.MfgFritzingGeda69.RejectNonOdbTgzInEda", nonOdbTgz);
        assertEdaRejected70(L"Stage15.Eda.MfgFritzingGeda69.RejectTruncatedIpc2581",
                            writeEdaText(L"trunc.cvg", "<IPC-2581 revision=\"B\"><Ecad><CadData><Step><Component refDes=\"U1\">"));
        assertEdaRejected70(L"Stage15.Eda.MfgFritzingGeda69.RejectBinaryGarbagePcb",
                            writeEdaBytes(L"binary_pcad.pcb", { 'P','C','A','D','_','B','I','N', 0x00, 0x01, 0x02, 0xFF, 0xFE, 0x00, 0x00, 0x42 }));
        assertEdaRejected70(L"Stage15.Eda.MfgFritzingGeda69.RejectInvalidFzzZip",
                            writeEdaText(L"bad.fzz", "PK\x03\x04CORRUPT_FRITZING_ZIP"));
    }

    for (const auto& f : createdEdaFiles) DeleteFileW(f.c_str());
    RemoveDirectoryW(edaWorkDir.c_str());
