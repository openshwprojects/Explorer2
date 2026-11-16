#pragma once
// FormatTestsDocs_Batch70.h — Stage 15.docs verification for the 70-format roadmap

    const std::wstring s70DocDir = TestDataDir() + L"\\samples70";
    const std::wstring docWorkDir = ScratchTestDir() + L"\\fmt_doc_b70";
    CreateDirectoryW(docWorkDir.c_str(), nullptr);
    std::vector<std::wstring> createdDocFiles;

    auto writeDocBytes = [&](const wchar_t* name, const std::vector<uint8_t>& bytes) -> std::wstring {
        std::wstring p = docWorkDir + L"\\" + name;
        PathUtils::WriteFileBytes(p, bytes.data(), bytes.size());
        createdDocFiles.push_back(p);
        return p;
    };
    auto assertDocRejected70 = [&](const wchar_t* testName, const std::wstring& path) {
        ParsedOfficeDocument badDoc;
        bool ok = SpreadsheetDocViewer::InspectOfficeFile(path, badDoc);
        tc.Record(!ok && !badDoc.valid && badDoc.sheets.empty() && badDoc.paragraphs.empty(), testName,
                  ok ? L"unexpectedly accepted corrupt document" : L"rejected cleanly");
    };
    auto hashDocContent70 = [](const ParsedOfficeDocument& d) -> uint64_t {
        uint64_t h = 14695981039346656037ULL;
        auto mixW = [&](const std::wstring& s) {
            for (wchar_t c : s) { h ^= static_cast<uint64_t>(c); h *= 1099511628211ULL; }
        };
        mixW(d.formatTitle);
        for (const auto& sh : d.sheets) {
            mixW(sh.name);
            for (const auto& r : sh.rows) for (const auto& cell : r) mixW(cell);
        }
        for (const auto& p : d.paragraphs) mixW(p.text);
        return h;
    };

    // ========================================================================
    // #64. Valve Source Material KeyValues Inspector (.vmt)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".vmt"),
                  L"Stage15.Docs.ValveVmt.ExtensionRegistered");

        ParsedOfficeDocument vmtDoc;
        bool okVmt = SpreadsheetDocViewer::InspectOfficeFile(s70DocDir + L"\\brickwall001a_shader.vmt", vmtDoc);
        bool hasBaseTex = false, hasProxy = false;
        if (okVmt && vmtDoc.valid && !vmtDoc.sheets.empty()) {
            for (const auto& row : vmtDoc.sheets[0].rows) {
                if (row.size() >= 3) {
                    if (row[1] == L"$basetexture" && row[2] == L"brick/brickwall001a") hasBaseTex = true;
                    if (row[0].find(L"Proxies/AnimatedTexture") != std::wstring::npos && row[1] == L"animatedTextureFrameRate" && row[2] == L"12") hasProxy = true;
                }
            }
        }
        tc.Record(okVmt && vmtDoc.valid && vmtDoc.formatTitle.find(L"LightmappedGeneric") != std::wstring::npos &&
                  hasBaseTex && hasProxy,
                  L"Stage15.Docs.ValveVmt.SampleShaderAndProxies");

        // Second distinct VMT -> distinct content hash + negative check on malformed VMT
        const char* vmt2Str = "\"VertexLitGeneric\"\n{\n    \"$basetexture\" \"models/alyx/alyx_sheet\"\n}\n";
        std::vector<uint8_t> vmt2Bytes(reinterpret_cast<const uint8_t*>(vmt2Str), reinterpret_cast<const uint8_t*>(vmt2Str) + std::strlen(vmt2Str));
        ParsedOfficeDocument vmt2Doc;
        bool okVmt2 = SpreadsheetDocViewer::InspectOfficeFile(writeDocBytes(L"alyx.vmt", vmt2Bytes), vmt2Doc);
        tc.Record(okVmt2 && vmt2Doc.valid && hashDocContent70(vmtDoc) != hashDocContent70(vmt2Doc),
                  L"Stage15.Docs.ValveVmt.DistinctMaterialHashes");

        const char* badVmtStr = "\"LightmappedGeneric\" { \"$basetexture\" "; // Unclosed brace & missing value
        std::vector<uint8_t> badVmtBytes(reinterpret_cast<const uint8_t*>(badVmtStr), reinterpret_cast<const uint8_t*>(badVmtStr) + std::strlen(badVmtStr));
        assertDocRejected70(L"Stage15.Docs.ValveVmt.RejectTruncatedVmt", writeDocBytes(L"bad.vmt", badVmtBytes));
    }

    // ========================================================================
    // #31. Apache Parquet v2 (ZSTD/LZ4_RAW/Delta/BSS) & Arrow/Feather v2 (LZ4_FRAME/ZSTD/Dict/>32MB)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".parquet") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".feather") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".arrow") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".ipc"),
                  L"Stage15.Docs.ParquetArrow31.ExtensionsRegistered");

        ParsedOfficeDocument pqZstd, pqLz4, feaLz4, ipcZstd;
        bool okPqZ  = SpreadsheetDocViewer::InspectOfficeFile(s70DocDir + L"\\parquet_v2_zstd_delta.parquet", pqZstd);
        bool okPqL  = SpreadsheetDocViewer::InspectOfficeFile(s70DocDir + L"\\parquet_lz4_raw_bss.parquet", pqLz4);
        bool okFeaL = SpreadsheetDocViewer::InspectOfficeFile(s70DocDir + L"\\feather_v2_lz4_dict.feather", feaLz4);
        bool okIpcZ = SpreadsheetDocViewer::InspectOfficeFile(s70DocDir + L"\\arrow_stream_zstd.ipc", ipcZstd);

        auto diagDoc = [](bool ok, const ParsedOfficeDocument& d) -> std::wstring {
            std::wstring s = L"ok=" + std::to_wstring(ok) + L" sh=" + std::to_wstring(d.sheets.size());
            if (d.sheets.size() >= 2 && d.sheets[1].rows.size() >= 2) {
                s += L" rows=" + std::to_wstring(d.sheets[1].rows.size()) + L" r1=[";
                for (size_t i = 0; i < d.sheets[1].rows[1].size(); ++i) {
                    if (i) s += L",";
                    s += d.sheets[1].rows[1][i];
                }
                s += L"]";
            }
            return s;
        };

        // Parquet v2 ZSTD + DELTA_BINARY_PACKED + DELTA_LENGTH_BYTE_ARRAY + RLE_DICTIONARY
        bool pqZCellsOk = okPqZ && pqZstd.valid && pqZstd.sheets.size() >= 2 &&
                          pqZstd.sheets[1].rows.size() == 6 && pqZstd.sheets[1].rows[1].size() >= 3 &&
                          pqZstd.sheets[1].rows[1][0] == L"1001" &&
                          pqZstd.sheets[1].rows[1][1] == L"WARSAW" &&
                          pqZstd.sheets[1].rows[1][2] == L"PL-MAZ" &&
                          pqZstd.sheets[1].rows[5][0] == L"1017" &&
                          pqZstd.sheets[1].rows[5][1] == L"WROCLAW" &&
                          pqZstd.sheets[1].rows[3][2] == L"PL-POM";
        tc.Record(pqZCellsOk, L"Stage15.Docs.ParquetArrow31.ParquetV2ZstdDeltaAndDict", diagDoc(okPqZ, pqZstd));

        // Parquet v2 LZ4_RAW + BYTE_STREAM_SPLIT (DOUBLE) + DELTA_BYTE_ARRAY (prefix strings)
        bool pqLCellsOk = okPqL && pqLz4.valid && pqLz4.sheets.size() >= 2 &&
                          pqLz4.sheets[1].rows.size() == 6 && pqLz4.sheets[1].rows[1].size() >= 2 &&
                          pqLz4.sheets[1].rows[1][0] == L"21.5" &&
                          pqLz4.sheets[1].rows[1][1] == L"node_alpha" &&
                          pqLz4.sheets[1].rows[5][0] == L"22.125" &&
                          pqLz4.sheets[1].rows[5][1] == L"node_epsilon" &&
                          hashDocContent70(pqZstd) != hashDocContent70(pqLz4);
        tc.Record(pqLCellsOk, L"Stage15.Docs.ParquetArrow31.ParquetV2Lz4RawByteStreamSplitAndPrefixDelta", diagDoc(okPqL, pqLz4));

        // Feather v2 (ARROW1) with BodyCompression LZ4_FRAME + DictionaryBatch categorical strings
        bool feaCellsOk = okFeaL && feaLz4.valid && feaLz4.sheets.size() >= 2 &&
                          feaLz4.sheets[1].rows.size() == 5 && feaLz4.sheets[1].rows[1].size() >= 3 &&
                          feaLz4.sheets[1].rows[1][0] == L"101" &&
                          feaLz4.sheets[1].rows[1][1] == L"Tokyo" &&
                          feaLz4.sheets[1].rows[2][1] == L"Toronto" &&
                          feaLz4.sheets[1].rows[3][1] == L"Berlin";
        tc.Record(feaCellsOk, L"Stage15.Docs.ParquetArrow31.FeatherV2Lz4BodyCompressionAndDict", diagDoc(okFeaL, feaLz4));

        // Arrow IPC Stream with BodyCompression ZSTD
        bool ipcCellsOk = okIpcZ && ipcZstd.valid && ipcZstd.sheets.size() >= 2 &&
                          ipcZstd.sheets[1].rows.size() == 4 && ipcZstd.sheets[1].rows[1].size() >= 3 &&
                          ipcZstd.sheets[1].rows[1][0] == L"7" &&
                          ipcZstd.sheets[1].rows[1][1] == L"Oslo" &&
                          ipcZstd.sheets[1].rows[3][0] == L"9" &&
                          ipcZstd.sheets[1].rows[3][1] == L"Riga" &&
                          hashDocContent70(feaLz4) != hashDocContent70(ipcZstd);
        tc.Record(ipcCellsOk, L"Stage15.Docs.ParquetArrow31.ArrowStreamZstdBodyCompression", diagDoc(okIpcZ, ipcZstd));

        // >32 MB (34 MB) random-access Arrow IPC & Parquet file test (verifying trailing footer seek past 32 MB!)
        {
            std::vector<uint8_t> baseFea, basePq;
            PathUtils::ReadFileBytes(s70DocDir + L"\\feather_v2_lz4_dict.feather", 1024 * 1024, baseFea);
            PathUtils::ReadFileBytes(s70DocDir + L"\\parquet_v2_zstd_delta.parquet", 1024 * 1024, basePq);
            bool largeArrowOk = false, largePqOk = false;
            if (baseFea.size() > 32 && basePq.size() > 32) {
                uint32_t fLen = *reinterpret_cast<const uint32_t*>(baseFea.data() + baseFea.size() - 10);
                size_t fStart = baseFea.size() - 10 - fLen;
                std::wstring bigArrowPath = docWorkDir + L"\\huge_34mb.arrow";
                createdDocFiles.push_back(bigArrowPath);
                HANDLE hf = CreateFileW(bigArrowPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
                if (hf != INVALID_HANDLE_VALUE) {
                    DWORD tmp = 0;
                    DeviceIoControl(hf, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &tmp, nullptr);
                    WriteFile(hf, baseFea.data(), static_cast<DWORD>(fStart), &tmp, nullptr);
                    LARGE_INTEGER seekPos{};
                    seekPos.QuadPart = 34LL * 1024LL * 1024LL;
                    SetFilePointerEx(hf, seekPos, nullptr, FILE_BEGIN);
                    WriteFile(hf, baseFea.data() + fStart, static_cast<DWORD>(baseFea.size() - fStart), &tmp, nullptr);
                    CloseHandle(hf);
                    ParsedOfficeDocument bigFeaDoc;
                    largeArrowOk = SpreadsheetDocViewer::InspectOfficeFile(bigArrowPath, bigFeaDoc) &&
                                   bigFeaDoc.valid && bigFeaDoc.sheets.size() >= 2 &&
                                   bigFeaDoc.sheets[1].rows[2][1] == L"Toronto";
                }

                uint32_t pqFLen = *reinterpret_cast<const uint32_t*>(basePq.data() + basePq.size() - 8);
                size_t pqFStart = basePq.size() - 8 - pqFLen;
                std::wstring bigPqPath = docWorkDir + L"\\huge_34mb.parquet";
                createdDocFiles.push_back(bigPqPath);
                HANDLE hp = CreateFileW(bigPqPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
                if (hp != INVALID_HANDLE_VALUE) {
                    DWORD tmp = 0;
                    DeviceIoControl(hp, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &tmp, nullptr);
                    WriteFile(hp, basePq.data(), static_cast<DWORD>(pqFStart), &tmp, nullptr);
                    LARGE_INTEGER seekPos{};
                    seekPos.QuadPart = 34LL * 1024LL * 1024LL;
                    SetFilePointerEx(hp, seekPos, nullptr, FILE_BEGIN);
                    WriteFile(hp, basePq.data() + pqFStart, static_cast<DWORD>(basePq.size() - pqFStart), &tmp, nullptr);
                    CloseHandle(hp);
                    ParsedOfficeDocument bigPqDoc;
                    largePqOk = SpreadsheetDocViewer::InspectOfficeFile(bigPqPath, bigPqDoc) &&
                                bigPqDoc.valid && bigPqDoc.sheets.size() >= 2 &&
                                bigPqDoc.sheets[1].rows[5][1] == L"WROCLAW";
                }
            }
            tc.Record(largeArrowOk && largePqOk, L"Stage15.Docs.ParquetArrow31.Over32MbRandomAccessParquetAndArrow");
        }

        // Negative checks: corrupt PAR1 payload & corrupt ARROW1 LZ4 payload
        assertDocRejected70(L"Stage15.Docs.ParquetArrow31.RejectCorruptParquetPayload",
                            writeDocBytes(L"bad.parquet", { 'P','A','R','1', 0xFF, 0x00, 0xAA, 0x55, 0x04, 0x00, 0x00, 0x00, 'P','A','R','1' }));
        assertDocRejected70(L"Stage15.Docs.ParquetArrow31.RejectTruncatedArrow",
                            writeDocBytes(L"trunc.arrow", { 'A','R','R','O','W','1', 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0x99, 0x00, 0x00, 0x00 }));
    }

    // ========================================================================
    // #32. HDF5 / NetCDF-4 Chunked B-Trees + Zlib/Shuffle (.h5/.hdf5/.nc) & MATLAB struct/cell/sparse (.mat)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".h5") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".hdf5") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".nc") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".mat"),
                  L"Stage15.Docs.Hdf5Mat32.ExtensionsRegistered");

        ParsedOfficeDocument h5Doc, ncDoc, mat5Doc, mat73Doc;
        bool okH5    = SpreadsheetDocViewer::InspectOfficeFile(s70DocDir + L"\\hdf5_chunked_shuffle_zlib.h5", h5Doc);
        bool okNc    = SpreadsheetDocViewer::InspectOfficeFile(s70DocDir + L"\\netcdf4_climate_track.nc", ncDoc);
        bool okMat5  = SpreadsheetDocViewer::InspectOfficeFile(s70DocDir + L"\\matlab_v5_struct_cell_sparse.mat", mat5Doc);
        bool okMat73 = SpreadsheetDocViewer::InspectOfficeFile(s70DocDir + L"\\matlab_v73_hdf5_matrix.mat", mat73Doc);

        // HDF5 4x4 chunked (2 chunks of 2x4) with Shuffle + Zlib Deflate via v1 B-Tree:
        // Sheet 1 = "sensor_grid", row 1 = [0]: 10.5, 11.5, 12.5, 13.5; row 3 = [2]: 20.5, 21.5, 22.5, 23.5
        bool h5ChunkOk = okH5 && h5Doc.valid && h5Doc.sheets.size() >= 2 &&
                         h5Doc.sheets[1].rows.size() == 5 && h5Doc.sheets[1].rows[1].size() >= 5 &&
                         h5Doc.sheets[1].rows[1][1] == L"10.5" &&
                         h5Doc.sheets[1].rows[2][4] == L"17.5" &&
                         h5Doc.sheets[1].rows[3][1] == L"20.5" &&
                         h5Doc.sheets[1].rows[4][4] == L"27.5";
        tc.Record(h5ChunkOk, L"Stage15.Docs.Hdf5Mat32.Hdf5ChunkedBTreeShuffleAndDeflate");

        // NetCDF-4 (.nc) with HDF5 v4 Single-Chunk Deflate (/temp_k = 288.15, 291.65, 285.4) + GPS track (/lat, /lon)
        bool ncFoundTemp = false;
        for (const auto& sh : ncDoc.sheets) {
            if (sh.name == L"temp_k" && sh.rows.size() == 4 && sh.rows[1].size() >= 2 && sh.rows[1][1] == L"288.15") {
                ncFoundTemp = true;
            }
        }
        tc.Record(okNc && ncDoc.valid && ncDoc.hasGeoTrack && ncDoc.geoPoints.size() == 3 && ncFoundTemp &&
                  hashDocContent70(h5Doc) != hashDocContent70(ncDoc),
                  L"Stage15.Docs.Hdf5Mat32.NetCdf4SingleChunkZlibAndGeoTrack");

        // MATLAB Level 5 (Zlib miCOMPRESSED struct `cfg`, cell `labels`, sparse `adj`) + MATLAB v7.3 HDF5 userblock
        bool hasStructVal = false, hasCellVal = false, hasSparseVal = false;
        for (const auto& sh : mat5Doc.sheets) {
            if (sh.name == L"cfg" && sh.rows.size() >= 3 && sh.rows[1].size() >= 3 &&
                sh.rows[1][1] == L"ResNet50" && sh.rows[2][1] == L"ViT_Base") {
                hasStructVal = true;
            } else if (sh.name == L"labels" && sh.rows.size() >= 3 && sh.rows[1].size() >= 3 &&
                       sh.rows[1][1] == L"cat" && sh.rows[2][2] == L"202") {
                hasCellVal = true;
            } else if (sh.name == L"adj" && sh.rows.size() == 4 && sh.rows[2].size() >= 3 &&
                       sh.rows[2][0] == L"3" && sh.rows[2][1] == L"2" && sh.rows[2][2] == L"-1.25") {
                hasSparseVal = true;
            }
        }
        tc.Record(okMat5 && mat5Doc.valid && hasStructVal && hasCellVal && hasSparseVal &&
                  okMat73 && mat73Doc.valid && mat73Doc.sheets.size() >= 2 &&
                  hashDocContent70(mat5Doc) != hashDocContent70(mat73Doc),
                  L"Stage15.Docs.Hdf5Mat32.MatlabV5StructCellSparseAndV73Hdf5");

        // Negative & disambiguation checks: CNC G-code .nc rejected in SpreadsheetDocViewer, corrupt HDF5 & MAT rejected
        const char* gcodeStr = "G21 G90\nG0 X0 Y0 Z5\nG1 X10 Y10 Z-1 F300\nM30\n";
        std::vector<uint8_t> gcodeBytes(reinterpret_cast<const uint8_t*>(gcodeStr), reinterpret_cast<const uint8_t*>(gcodeStr) + std::strlen(gcodeStr));
        assertDocRejected70(L"Stage15.Docs.Hdf5Mat32.RejectGCodeNcInOfficeViewer", writeDocBytes(L"mill_pocket.nc", gcodeBytes));
        assertDocRejected70(L"Stage15.Docs.Hdf5Mat32.RejectTruncatedHdf5Superblock",
                            writeDocBytes(L"bad.h5", { 0x89,'H','D','F',0x0D,0x0A,0x1A,0x0A, 2, 8, 8, 0, 0,0,0,0,0,0,0,0 }));
        std::vector<uint8_t> badMat(136, 0);
        badMat[124] = 0x00; badMat[125] = 0x01; badMat[126] = 'I'; badMat[127] = 'M';
        badMat[128] = 15; // miCOMPRESSED with invalid zlib bytes
        badMat[132] = 4;
        assertDocRejected70(L"Stage15.Docs.Hdf5Mat32.RejectCorruptZlibMat", writeDocBytes(L"bad.mat", badMat));
    }

    for (const auto& f : createdDocFiles) DeleteFileW(f.c_str());
    RemoveDirectoryW(docWorkDir.c_str());
