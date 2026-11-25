// FormatTestsDocsAnalyticsForensics.cpp — Stage 15 self-tests for Columnar Analytics, Scientific Matrices,
// Finance Ledgers, Vector GIS, Lossless/Chiptune Audio & Digital Forensics Document Readers
#include "SelfTestCommon.h"
#include "TestBytes.h"
#include "SpreadsheetDocViewer.h"
#include "PathUtils.h"
#ifdef _WIN32
#include <winioctl.h>
#endif

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Explorer2::SelfTestSuite {

namespace {

using namespace ::Explorer2::TestBytes;

inline uint64_t HashDocContent(const ParsedOfficeDocument& d) {
    uint64_t h = 14695981039346656037ULL;
    auto mixW = [&](const std::wstring& s) {
        for (wchar_t c : s) {
            h ^= static_cast<uint64_t>(c);
            h *= 1099511628211ULL;
        }
    };
    mixW(d.formatTitle);
    for (const auto& sh : d.sheets) {
        mixW(sh.name);
        for (const auto& r : sh.rows) {
            for (const auto& cell : r) mixW(cell);
        }
    }
    for (const auto& p : d.paragraphs) mixW(p.text);
    return h;
}

inline bool DocHasSubstr(const ParsedOfficeDocument& d, std::wstring_view needle) {
    if (d.formatTitle.find(needle) != std::wstring::npos) return true;
    for (const auto& sh : d.sheets) {
        if (sh.name.find(needle) != std::wstring::npos) return true;
        for (const auto& r : sh.rows) {
            for (const auto& cell : r) {
                if (cell.find(needle) != std::wstring::npos) return true;
            }
        }
    }
    for (const auto& p : d.paragraphs) {
        if (p.text.find(needle) != std::wstring::npos) return true;
    }
    return false;
}

inline bool SheetHasSubstr(const ParsedOfficeDocument& d, std::wstring_view sheetName, std::wstring_view needle) {
    for (const auto& sh : d.sheets) {
        if (sh.name == sheetName) {
            for (const auto& r : sh.rows) {
                for (const auto& cell : r) {
                    if (cell.find(needle) != std::wstring::npos) return true;
                }
            }
        }
    }
    return false;
}

inline void AppendPbVarint(std::vector<uint8_t>& out, uint64_t v) {
    do {
        uint8_t b = static_cast<uint8_t>(v & 0x7Fu);
        v >>= 7;
        if (v != 0) b |= 0x80u;
        out.push_back(b);
    } while (v != 0);
}

inline uint64_t ZigZag32(int32_t n) {
    return static_cast<uint32_t>((static_cast<uint32_t>(n) << 1) ^ static_cast<uint32_t>(n >> 31));
}

inline void AppendPbFieldVarint(std::vector<uint8_t>& out, uint32_t fn, uint64_t v) {
    AppendPbVarint(out, (static_cast<uint64_t>(fn) << 3) | 0u);
    AppendPbVarint(out, v);
}

inline void AppendPbFieldBytes(std::vector<uint8_t>& out, uint32_t fn, const std::vector<uint8_t>& pay) {
    AppendPbVarint(out, (static_cast<uint64_t>(fn) << 3) | 2u);
    AppendPbVarint(out, pay.size());
    out.insert(out.end(), pay.begin(), pay.end());
}

inline void AppendPbFieldStr(std::vector<uint8_t>& out, uint32_t fn, std::string_view sv) {
    AppendPbVarint(out, (static_cast<uint64_t>(fn) << 3) | 2u);
    AppendPbVarint(out, sv.size());
    AppendStr(out, sv);
}

// Minimal v3 (512-byte sector) OLE2 Compound File Binary builder for Jump List tests
inline std::vector<uint8_t> BuildMinimalOle2Cfb(const std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& streams) {
    constexpr uint32_t kSecSz = 512u;
    const size_t nEntries = 1u + streams.size();
    const uint32_t dirSecs = static_cast<uint32_t>((nEntries * 128u + kSecSz - 1u) / kSecSz);

    std::vector<uint32_t> fat(128u, 0xFFFFFFFFu);
    fat[0] = 0xFFFFFFFDu; // FATSECT
    uint32_t curSec = 1u;
    const uint32_t firstDirSec = curSec;
    for (uint32_t d = 0; d < dirSecs; ++d) {
        fat[curSec] = (d + 1u < dirSecs) ? (curSec + 1u) : 0xFFFFFFFEu;
        ++curSec;
    }

    struct StreamPlacement {
        std::wstring name;
        uint32_t startSec = 0xFFFFFFFEu;
        uint32_t byteSize = 0u;
    };
    std::vector<StreamPlacement> meta;
    std::vector<uint8_t> streamBlob;
    for (const auto& [name, data] : streams) {
        if (data.empty()) {
            meta.push_back({ name, 0xFFFFFFFEu, 0u });
            continue;
        }
        const uint32_t startS = curSec;
        const uint32_t nSec = static_cast<uint32_t>((data.size() + kSecSz - 1u) / kSecSz);
        for (uint32_t s = 0; s < nSec; ++s) {
            fat[curSec] = (s + 1u < nSec) ? (curSec + 1u) : 0xFFFFFFFEu;
            ++curSec;
        }
        meta.push_back({ name, startS, static_cast<uint32_t>(data.size()) });
        streamBlob.insert(streamBlob.end(), data.begin(), data.end());
        const size_t pad = static_cast<size_t>(nSec) * kSecSz - data.size();
        if (pad > 0) streamBlob.insert(streamBlob.end(), pad, 0u);
    }

    std::vector<uint8_t> dirBuf(static_cast<size_t>(dirSecs) * kSecSz, 0u);
    auto writeDirEntry = [&](size_t idx, std::wstring_view nm, uint8_t objType,
                             uint32_t leftId, uint32_t rightId, uint32_t childId,
                             uint32_t startSec, uint32_t sz) {
        uint8_t* p = dirBuf.data() + idx * 128u;
        const size_t chars = (nm.size() < 31u) ? nm.size() : 31u;
        for (size_t i = 0; i < chars; ++i) PutU16LE(p + i * 2u, static_cast<uint16_t>(nm[i]));
        PutU16LE(p + chars * 2u, 0u);
        PutU16LE(p + 64u, static_cast<uint16_t>((chars + 1u) * 2u));
        p[66] = objType;
        p[67] = 1u; // black node
        PutU32LE(p + 68u, leftId);
        PutU32LE(p + 72u, rightId);
        PutU32LE(p + 76u, childId);
        PutU32LE(p + 116u, startSec);
        PutU32LE(p + 120u, sz);
    };

    writeDirEntry(0, L"Root Entry", 5u, 0xFFFFFFFFu, 0xFFFFFFFFu,
                  meta.empty() ? 0xFFFFFFFFu : 1u, 0xFFFFFFFEu, 0u);
    for (size_t i = 0; i < meta.size(); ++i) {
        const uint32_t rightId = (i + 1u < meta.size()) ? static_cast<uint32_t>(i + 2u) : 0xFFFFFFFFu;
        writeDirEntry(i + 1u, meta[i].name, 2u, 0xFFFFFFFFu, rightId, 0xFFFFFFFFu, meta[i].startSec, meta[i].byteSize);
    }

    std::vector<uint8_t> hdr(kSecSz, 0u);
    const uint8_t sig[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    std::memcpy(hdr.data(), sig, 8);
    PutU16LE(hdr.data() + 24u, 0x003Eu);
    PutU16LE(hdr.data() + 26u, 0x0003u);
    PutU16LE(hdr.data() + 28u, 0xFFFEu);
    PutU16LE(hdr.data() + 30u, 9u);  // 512B sector
    PutU16LE(hdr.data() + 32u, 6u);  // 64B mini-sector
    PutU32LE(hdr.data() + 44u, 1u);  // 1 FAT sector
    PutU32LE(hdr.data() + 48u, firstDirSec);
    PutU32LE(hdr.data() + 56u, 1u);  // miniStreamCutoff = 1 -> all non-empty streams use regular FAT
    PutU32LE(hdr.data() + 60u, 0xFFFFFFFEu);
    PutU32LE(hdr.data() + 68u, 0xFFFFFFFEu);
    PutU32LE(hdr.data() + 76u, 0u);  // DIFAT[0] = sector 0
    for (size_t i = 1; i < 109u; ++i) PutU32LE(hdr.data() + 76u + i * 4u, 0xFFFFFFFFu);

    std::vector<uint8_t> out = std::move(hdr);
    for (uint32_t f : fat) AppendU32LE(out, f);
    out.insert(out.end(), dirBuf.begin(), dirBuf.end());
    out.insert(out.end(), streamBlob.begin(), streamBlob.end());
    return out;
}

} // namespace

void RunFormatTestsDocsAnalyticsForensics(TestCounter& tc) {
    const std::wstring docsDir     = TestDataDir() + L"\\docs";
    const std::wstring dataDir     = TestDataDir() + L"\\data";
    const std::wstring gisDir      = TestDataDir() + L"\\gis";
    const std::wstring audioDir    = TestDataDir() + L"\\audio";
    const std::wstring securityDir = TestDataDir() + L"\\security";
    const std::wstring docWorkDir  = ScratchTestDir() + L"\\fmt_doc_analytics_forensics";
    CreateDirectoryW(docWorkDir.c_str(), nullptr);
    std::vector<std::wstring> createdDocFiles;

    auto writeDocBytes = [&](const wchar_t* name, const std::vector<uint8_t>& bytes) -> std::wstring {
        std::wstring p = docWorkDir + L"\\" + name;
        PathUtils::WriteFileBytes(p, bytes.data(), bytes.size());
        createdDocFiles.push_back(p);
        return p;
    };
    auto writeDocText = [&](const wchar_t* name, std::string_view sv) -> std::wstring {
        std::vector<uint8_t> bytes(reinterpret_cast<const uint8_t*>(sv.data()),
                                   reinterpret_cast<const uint8_t*>(sv.data()) + sv.size());
        return writeDocBytes(name, bytes);
    };
    auto assertDocRejected = [&](const wchar_t* testName, const std::wstring& path) {
        ParsedOfficeDocument badDoc;
        bool ok = SpreadsheetDocViewer::InspectOfficeFile(path, badDoc);
        tc.Record(!ok && !badDoc.valid && badDoc.sheets.empty() && badDoc.paragraphs.empty(), testName,
                  ok ? L"unexpectedly accepted corrupt document" : L"rejected cleanly");
    };

    // ========================================================================
    // A. Valve Source Material KeyValues Inspector (.vmt) & Unreal Level (.umap)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".vmt"),
                  L"Stage15.Docs.ValveVmt.ExtensionRegistered");

        ParsedOfficeDocument vmtDoc;
        bool okVmt = SpreadsheetDocViewer::InspectOfficeFile(docsDir + L"\\brickwall001a_shader.vmt", vmtDoc);
        bool hasBaseTex = false, hasProxy = false;
        if (okVmt && vmtDoc.valid && !vmtDoc.sheets.empty()) {
            for (const auto& row : vmtDoc.sheets[0].rows) {
                if (row.size() >= 3) {
                    if (row[1] == L"$basetexture" && row[2] == L"brick/brickwall001a") hasBaseTex = true;
                    if (row[0].find(L"Proxies/AnimatedTexture") != std::wstring::npos &&
                        row[1] == L"animatedTextureFrameRate" && row[2] == L"12") {
                        hasProxy = true;
                    }
                }
            }
        }
        tc.Record(okVmt && vmtDoc.valid && vmtDoc.formatTitle.find(L"LightmappedGeneric") != std::wstring::npos &&
                  hasBaseTex && hasProxy,
                  L"Stage15.Docs.ValveVmt.SampleShaderAndProxies");

        const char* altVmtStr = "\"VertexLitGeneric\"\n{\n    \"$basetexture\" \"models/alyx/alyx_sheet\"\n}\n";
        ParsedOfficeDocument altVmtDoc;
        bool okAltVmt = SpreadsheetDocViewer::InspectOfficeFile(writeDocText(L"alyx.vmt", altVmtStr), altVmtDoc);
        tc.Record(okAltVmt && altVmtDoc.valid && HashDocContent(vmtDoc) != HashDocContent(altVmtDoc),
                  L"Stage15.Docs.ValveVmt.DistinctMaterialHashes");

        assertDocRejected(L"Stage15.Docs.ValveVmt.RejectTruncatedVmt",
                          writeDocText(L"bad.vmt", "\"LightmappedGeneric\" { \"$basetexture\" "));

        // Unreal Engine Package (.umap / .uasset) real sample & corrupt check
        ParsedOfficeDocument umapDoc;
        bool okUmap = SpreadsheetDocViewer::InspectOfficeFile(docsDir + L"\\ue4_outpost_level.umap", umapDoc);
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".umap") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".uasset") &&
                  okUmap && umapDoc.valid &&
                  DocHasSubstr(umapDoc, L"PersistentLevel") &&
                  DocHasSubstr(umapDoc, L"World") &&
                  DocHasSubstr(umapDoc, L"StaticMesh"),
                  L"Stage15.Docs.UnrealPackage.OutpostLevelSample");
        assertDocRejected(L"Stage15.Docs.UnrealPackage.RejectBadMagic",
                          writeDocBytes(L"bad.umap", { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77 }));
    }

    // ========================================================================
    // B. Apache Parquet (ZSTD/LZ4_RAW/Delta/BSS) & Arrow/Feather (FEA1 + ARROW1 + >32MB)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".parquet") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".feather") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".arrow") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".ipc"),
                  L"Stage15.Docs.ParquetArrow.ExtensionsRegistered");

        ParsedOfficeDocument pqZstd, pqLz4, feaLz4, ipcZstd, feaLegacy;
        bool okPqZ  = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\parquet_v2_zstd_delta.parquet", pqZstd);
        bool okPqL  = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\parquet_lz4_raw_bss.parquet", pqLz4);
        bool okFeaL = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\feather_v2_lz4_dict.feather", feaLz4);
        bool okIpcZ = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\arrow_stream_zstd.ipc", ipcZstd);
        bool okFea1 = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\metrics_v1.feather", feaLegacy);

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

        bool pqZCellsOk = okPqZ && pqZstd.valid && pqZstd.sheets.size() >= 2 &&
                          pqZstd.sheets[1].rows.size() == 6 && pqZstd.sheets[1].rows[1].size() >= 3 &&
                          pqZstd.sheets[1].rows[1][0] == L"1001" &&
                          pqZstd.sheets[1].rows[1][1] == L"WARSAW" &&
                          pqZstd.sheets[1].rows[1][2] == L"PL-MAZ" &&
                          pqZstd.sheets[1].rows[5][0] == L"1017" &&
                          pqZstd.sheets[1].rows[5][1] == L"WROCLAW" &&
                          pqZstd.sheets[1].rows[3][2] == L"PL-POM";
        tc.Record(pqZCellsOk, L"Stage15.Docs.ParquetArrow.ParquetZstdDeltaAndDict", diagDoc(okPqZ, pqZstd));

        bool pqLCellsOk = okPqL && pqLz4.valid && pqLz4.sheets.size() >= 2 &&
                          pqLz4.sheets[1].rows.size() == 6 && pqLz4.sheets[1].rows[1].size() >= 2 &&
                          pqLz4.sheets[1].rows[1][0] == L"21.5" &&
                          pqLz4.sheets[1].rows[1][1] == L"node_alpha" &&
                          pqLz4.sheets[1].rows[5][0] == L"22.125" &&
                          pqLz4.sheets[1].rows[5][1] == L"node_epsilon" &&
                          HashDocContent(pqZstd) != HashDocContent(pqLz4);
        tc.Record(pqLCellsOk, L"Stage15.Docs.ParquetArrow.ParquetLz4RawByteStreamSplitAndPrefixDelta", diagDoc(okPqL, pqLz4));

        bool feaCellsOk = okFeaL && feaLz4.valid && feaLz4.sheets.size() >= 2 &&
                          feaLz4.sheets[1].rows.size() == 5 && feaLz4.sheets[1].rows[1].size() >= 3 &&
                          feaLz4.sheets[1].rows[1][0] == L"101" &&
                          feaLz4.sheets[1].rows[1][1] == L"Tokyo" &&
                          feaLz4.sheets[1].rows[2][1] == L"Toronto" &&
                          feaLz4.sheets[1].rows[3][1] == L"Berlin";
        tc.Record(feaCellsOk, L"Stage15.Docs.ParquetArrow.FeatherArrowLz4BodyCompressionAndDict", diagDoc(okFeaL, feaLz4));

        bool ipcCellsOk = okIpcZ && ipcZstd.valid && ipcZstd.sheets.size() >= 2 &&
                          ipcZstd.sheets[1].rows.size() == 4 && ipcZstd.sheets[1].rows[1].size() >= 3 &&
                          ipcZstd.sheets[1].rows[1][0] == L"7" &&
                          ipcZstd.sheets[1].rows[1][1] == L"Oslo" &&
                          ipcZstd.sheets[1].rows[3][0] == L"9" &&
                          ipcZstd.sheets[1].rows[3][1] == L"Riga" &&
                          HashDocContent(feaLz4) != HashDocContent(ipcZstd);
        tc.Record(ipcCellsOk, L"Stage15.Docs.ParquetArrow.ArrowStreamZstdBodyCompression", diagDoc(okIpcZ, ipcZstd));

        // Upgraded Feather FEA1 FlatBuffers CTable sample (UTF8 with null bitmap, BOOL, INT16, DATE)
        tc.Record(okFea1 && feaLegacy.valid &&
                  DocHasSubstr(feaLegacy, L"Explorer2 Feather v1 Sample") &&
                  DocHasSubstr(feaLegacy, L"node-a") &&
                  DocHasSubstr(feaLegacy, L"node-c") &&
                  DocHasSubstr(feaLegacy, L"99") &&
                  HashDocContent(feaLegacy) != HashDocContent(feaLz4),
                  L"Stage15.Docs.ParquetArrow.FeatherFea1FlatBufferColumnsAndNullBitmap");

        // >32 MB (34 MB) random-access Arrow IPC & Parquet file test (verifying trailing footer seek past 32 MB)
        {
            std::vector<uint8_t> baseFea, basePq;
            PathUtils::ReadFileBytes(dataDir + L"\\feather_v2_lz4_dict.feather", 1024 * 1024, baseFea);
            PathUtils::ReadFileBytes(dataDir + L"\\parquet_v2_zstd_delta.parquet", 1024 * 1024, basePq);
            bool largeArrowOk = false, largePqOk = false;
            if (baseFea.size() > 32 && basePq.size() > 32) {
                uint32_t fLen = 0;
                std::memcpy(&fLen, baseFea.data() + baseFea.size() - 10, 4);
                size_t fStart = baseFea.size() - 10 - fLen;
                std::wstring bigArrowPath = docWorkDir + L"\\huge_34mb.arrow";
                createdDocFiles.push_back(bigArrowPath);
                HANDLE hf = CreateFileW(bigArrowPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
                if (hf != INVALID_HANDLE_VALUE) {
                    DWORD tmp = 0;
#ifdef FSCTL_SET_SPARSE
                    DeviceIoControl(hf, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &tmp, nullptr);
#endif
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

                uint32_t pqFLen = 0;
                std::memcpy(&pqFLen, basePq.data() + basePq.size() - 8, 4);
                size_t pqFStart = basePq.size() - 8 - pqFLen;
                std::wstring bigPqPath = docWorkDir + L"\\huge_34mb.parquet";
                createdDocFiles.push_back(bigPqPath);
                HANDLE hp = CreateFileW(bigPqPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
                if (hp != INVALID_HANDLE_VALUE) {
                    DWORD tmp = 0;
#ifdef FSCTL_SET_SPARSE
                    DeviceIoControl(hp, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &tmp, nullptr);
#endif
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
            tc.Record(largeArrowOk && largePqOk, L"Stage15.Docs.ParquetArrow.Over32MbRandomAccessParquetAndArrow");
        }

        assertDocRejected(L"Stage15.Docs.ParquetArrow.RejectCorruptParquetPayload",
                          writeDocBytes(L"bad.parquet", { 'P','A','R','1', 0xFF, 0x00, 0xAA, 0x55, 0x04, 0x00, 0x00, 0x00, 'P','A','R','1' }));
        assertDocRejected(L"Stage15.Docs.ParquetArrow.RejectTruncatedArrow",
                          writeDocBytes(L"trunc.arrow", { 'A','R','R','O','W','1', 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0x99, 0x00, 0x00, 0x00 }));
        assertDocRejected(L"Stage15.Docs.ParquetArrow.RejectCorruptFea1Footer",
                          writeDocBytes(L"bad_fea1.feather", { 'F','E','A','1', 0,0,0,0, 0xFF,0xFF,0xFF,0x7F, 4,0,0,0, 'F','E','A','1' }));
    }

    // ========================================================================
    // C. HDF5 / NetCDF-4 Chunked B-Trees (.h5/.hdf5/.nc) & MATLAB (.mat)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".h5") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".hdf5") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".nc") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".mat"),
                  L"Stage15.Docs.Hdf5Matlab.ExtensionsRegistered");

        ParsedOfficeDocument h5Doc, ncDoc, mat5Doc, mat73Doc;
        bool okH5    = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\hdf5_chunked_shuffle_zlib.h5", h5Doc);
        bool okNc    = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\netcdf4_climate_track.nc", ncDoc);
        bool okMat5  = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\matlab_v5_struct_cell_sparse.mat", mat5Doc);
        bool okMat73 = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\matlab_v73_hdf5_matrix.mat", mat73Doc);

        bool h5ChunkOk = okH5 && h5Doc.valid && h5Doc.sheets.size() >= 2 &&
                         h5Doc.sheets[1].rows.size() == 5 && h5Doc.sheets[1].rows[1].size() >= 5 &&
                         h5Doc.sheets[1].rows[1][1] == L"10.5" &&
                         h5Doc.sheets[1].rows[2][4] == L"17.5" &&
                         h5Doc.sheets[1].rows[3][1] == L"20.5" &&
                         h5Doc.sheets[1].rows[4][4] == L"27.5";
        tc.Record(h5ChunkOk, L"Stage15.Docs.Hdf5Matlab.Hdf5ChunkedBTreeShuffleAndDeflate");

        bool ncFoundTemp = false;
        for (const auto& sh : ncDoc.sheets) {
            if (sh.name == L"temp_k" && sh.rows.size() == 4 && sh.rows[1].size() >= 2 && sh.rows[1][1] == L"288.15") {
                ncFoundTemp = true;
            }
        }
        tc.Record(okNc && ncDoc.valid && ncDoc.hasGeoTrack && ncDoc.geoPoints.size() == 3 && ncFoundTemp &&
                  HashDocContent(h5Doc) != HashDocContent(ncDoc),
                  L"Stage15.Docs.Hdf5Matlab.NetCdf4SingleChunkZlibAndGeoTrack");

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
                  HashDocContent(mat5Doc) != HashDocContent(mat73Doc),
                  L"Stage15.Docs.Hdf5Matlab.MatlabLevel5StructCellSparseAndHdf5Matrix");

        assertDocRejected(L"Stage15.Docs.Hdf5Matlab.RejectGCodeNcInOfficeViewer",
                          writeDocText(L"mill_pocket.nc", "G21 G90\nG0 X0 Y0 Z5\nG1 X10 Y10 Z-1 F300\nM30\n"));
        assertDocRejected(L"Stage15.Docs.Hdf5Matlab.RejectTruncatedHdf5Superblock",
                          writeDocBytes(L"bad.h5", { 0x89,'H','D','F',0x0D,0x0A,0x1A,0x0A, 2, 8, 8, 0, 0,0,0,0,0,0,0,0 }));
        std::vector<uint8_t> badMat(136, 0);
        badMat[124] = 0x00; badMat[125] = 0x01; badMat[126] = 'I'; badMat[127] = 'M';
        badMat[128] = 15; // miCOMPRESSED with invalid zlib bytes
        badMat[132] = 4;
        assertDocRejected(L"Stage15.Docs.Hdf5Matlab.RejectCorruptZlibMat", writeDocBytes(L"bad.mat", badMat));
    }

    // ========================================================================
    // D. Upgraded Statistical, LSM SSTable, BinXML EVTX & Chiptune Real Samples
    // ========================================================================
    {
        // 1. SPSS $FL3 ZSAV Zlib-compressed block sample (.zsav)
        ParsedOfficeDocument zsavDoc;
        bool okZsav = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\survey_compressed.zsav", zsavDoc);
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".zsav") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".sav") &&
                  okZsav && zsavDoc.valid &&
                  DocHasSubstr(zsavDoc, L"ZSAV Zlib Block Clinical Sample") &&
                  DocHasSubstr(zsavDoc, L"SCORE") &&
                  DocHasSubstr(zsavDoc, L"42") &&
                  DocHasSubstr(zsavDoc, L"85") &&
                  DocHasSubstr(zsavDoc, L"CTRL") &&
                  DocHasSubstr(zsavDoc, L"ACTIVE"),
                  L"Stage15.Docs.StatKeyVal.SpssZsavZlibDecompression");
        assertDocRejected(L"Stage15.Docs.StatKeyVal.RejectCorruptZsav",
                          writeDocBytes(L"bad.zsav", { '$','F','L','3', 0, 1, 2, 3 }));

        // 2. SAS7BDAT SASYZCRL RLE-compressed rows sample (.sas7bdat)
        ParsedOfficeDocument sasDoc;
        bool okSas = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\clinical_rle.sas7bdat", sasDoc);
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".sas7bdat") &&
                  okSas && sasDoc.valid &&
                  DocHasSubstr(sasDoc, L"CLINICAL_RLE") &&
                  DocHasSubstr(sasDoc, L"DOSE_MG") &&
                  DocHasSubstr(sasDoc, L"ARM_NAME") &&
                  DocHasSubstr(sasDoc, L"125.5") &&
                  DocHasSubstr(sasDoc, L"250") &&
                  DocHasSubstr(sasDoc, L"TREAT_A") &&
                  DocHasSubstr(sasDoc, L"PLACEBO"),
                  L"Stage15.Docs.StatKeyVal.Sas7bdatRleCompressedRows");
        assertDocRejected(L"Stage15.Docs.StatKeyVal.RejectCorruptSas7bdat",
                          writeDocBytes(L"bad.sas7bdat", std::vector<uint8_t>(64, 0x55)));

        // 3. Stata 118 <stata_dta> with <map> + <strls> GSO heap notes (.dta)
        ParsedOfficeDocument dtaDoc;
        bool okDta = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\panel_notes.dta", dtaDoc);
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".dta") &&
                  okDta && dtaDoc.valid &&
                  DocHasSubstr(dtaDoc, L"Panel With GSO strL Notes") &&
                  DocHasSubstr(dtaDoc, L"gdp_growth") &&
                  DocHasSubstr(dtaDoc, L"3.45") &&
                  DocHasSubstr(dtaDoc, L"Strong industrial rebound across Q1 semiconductor exports") &&
                  DocHasSubstr(dtaDoc, L"Moderating consumer demand offset by renewable capex"),
                  L"Stage15.Docs.StatKeyVal.Stata118GsoStrlHeapNotes");
        assertDocRejected(L"Stage15.Docs.StatKeyVal.RejectCorruptDta",
                          writeDocText(L"bad.dta", "<stata_dta><header><release>999</release></header>"));

        // 4. RocksDB PlainTable SSTable (.sst)
        ParsedOfficeDocument sstDoc;
        bool okSst = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\plain_kv.sst", sstDoc);
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".sst") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".ldb") &&
                  okSst && sstDoc.valid &&
                  DocHasSubstr(sstDoc, L"acct:1001") &&
                  DocHasSubstr(sstDoc, L"balance=9450.25") &&
                  DocHasSubstr(sstDoc, L"acct:1002") &&
                  DocHasSubstr(sstDoc, L"eu-central-1"),
                  L"Stage15.Docs.StatKeyVal.RocksDbPlainTableSstSample");
        assertDocRejected(L"Stage15.Docs.StatKeyVal.RejectCorruptSstMagic",
                          writeDocBytes(L"bad.sst", std::vector<uint8_t>(64, 0xAA)));

        // 5. Windows Event Log BinXML TemplateInstance + SID/FILETIME substitutions (.evtx)
        ParsedOfficeDocument evtxDoc;
        bool okEvtx = SpreadsheetDocViewer::InspectOfficeFile(securityDir + L"\\security_logon.evtx", evtxDoc);
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".evtx") &&
                  okEvtx && evtxDoc.valid &&
                  DocHasSubstr(evtxDoc, L"Microsoft-Windows-Security-Auditing") &&
                  DocHasSubstr(evtxDoc, L"4624") &&
                  DocHasSubstr(evtxDoc, L"Security") &&
                  DocHasSubstr(evtxDoc, L"DC01.corp.local") &&
                  DocHasSubstr(evtxDoc, L"alice.admin") &&
                  DocHasSubstr(evtxDoc, L"S-1-5-21-1000001-2000002-3000003-1104"),
                  L"Stage15.Docs.ForensicsEvtx.BinXmlTemplateSubstitutionsSample");
        assertDocRejected(L"Stage15.Docs.ForensicsEvtx.RejectTruncatedEvtx",
                          writeDocBytes(L"bad.evtx", { 'E','l','f','F','i','l','e',0, 1, 2, 3 }));

        // 6. PLS Streaming Playlist (.pls) + SNES/Genesis/NES/C64 Chiptune real samples
        ParsedOfficeDocument plsDoc, spcDoc, vgmDoc, vgzDoc, nsfDoc, nsfeDoc, sidDoc;
        bool okPls  = SpreadsheetDocViewer::InspectOfficeFile(audioDir + L"\\radio_streams.pls", plsDoc);
        bool okSpc  = SpreadsheetDocViewer::InspectOfficeFile(audioDir + L"\\snes_overworld.spc", spcDoc);
        bool okVgm  = SpreadsheetDocViewer::InspectOfficeFile(audioDir + L"\\genesis_stage1.vgm", vgmDoc);
        bool okVgz  = SpreadsheetDocViewer::InspectOfficeFile(audioDir + L"\\arcade_fm.vgz", vgzDoc);
        bool okNsf  = SpreadsheetDocViewer::InspectOfficeFile(audioDir + L"\\famicom_vrc6.nsf", nsfDoc);
        bool okNsfe = SpreadsheetDocViewer::InspectOfficeFile(audioDir + L"\\castlevania_suite.nsfe", nsfeDoc);
        bool okSid  = SpreadsheetDocViewer::InspectOfficeFile(audioDir + L"\\c64_dual_sid.sid", sidDoc);

        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".pls") &&
                  okPls && plsDoc.valid &&
                  DocHasSubstr(plsDoc, L"Nightdrive Synthwave FM (320k Ogg)") &&
                  DocHasSubstr(plsDoc, L"https://stream.example.org/synthwave-hi.ogg") &&
                  DocHasSubstr(plsDoc, L"Aurora Borealis Stem (Local Reference)"),
                  L"Stage15.Docs.AudioPls.RadioStreamsSample");
        assertDocRejected(L"Stage15.Docs.AudioPls.RejectNonPlaylistIni",
                          writeDocText(L"bad.pls", "[Desktop Entry]\nName=App\n"));

        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".spc") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".vgm") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".vgz") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".nsf") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".nsfe") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".sid") &&
                  okSpc && spcDoc.valid &&
                  DocHasSubstr(spcDoc, L"Overworld Theme") &&
                  DocHasSubstr(spcDoc, L"Chrono Horizon Original Soundtrack") &&
                  DocHasSubstr(spcDoc, L"32000") &&
                  okVgm && vgmDoc.valid && okVgz && vgzDoc.valid &&
                  DocHasSubstr(vgmDoc, L"Emerald Hill Zone") &&
                  DocHasSubstr(vgmDoc, L"YM2612") &&
                  DocHasSubstr(vgzDoc, L"Emerald Hill Zone") &&
                  DocHasSubstr(vgzDoc, L"VGZ Compression") &&
                  okNsf && nsfDoc.valid && okNsfe && nsfeDoc.valid &&
                  DocHasSubstr(nsfDoc, L"Akumajo VRC6 Prelude") &&
                  DocHasSubstr(nsfDoc, L"Konami VRC6") &&
                  DocHasSubstr(nsfDoc, L"Namco 163") &&
                  DocHasSubstr(nsfeDoc, L"Clocktower Suite") &&
                  DocHasSubstr(nsfeDoc, L"01. Opening Gate") &&
                  DocHasSubstr(nsfeDoc, L"03. Boss Confrontation") &&
                  okSid && sidDoc.valid &&
                  DocHasSubstr(sidDoc, L"Cybernetic Stereo 8580") &&
                  DocHasSubstr(sidDoc, L"MOS 8580") &&
                  DocHasSubstr(sidDoc, L"$D420") &&
                  DocHasSubstr(sidDoc, L"$D440"),
                  L"Stage15.Docs.Chiptunes.SpcVgmVgzNsfNsfeSidRealSamples");

        assertDocRejected(L"Stage15.Docs.Chiptunes.RejectCorruptSpc",
                          writeDocBytes(L"bad.spc", { 'S','N','E','S', 0, 1, 2 }));
        assertDocRejected(L"Stage15.Docs.Chiptunes.RejectCorruptVgz",
                          writeDocBytes(L"bad.vgz", { 0x1F, 0x8B, 0x08, 0x00, 0xDE, 0xAD, 0xBE, 0xEF }));
        assertDocRejected(L"Stage15.Docs.Chiptunes.RejectCorruptNsfe",
                          writeDocBytes(L"bad.nsfe", { 'N','S','F','E', 0xFF, 0xFF, 0x00, 0x00, 'I','N','F','O', 0,0,0,0 }));
        assertDocRejected(L"Stage15.Docs.Chiptunes.RejectCorruptSid",
                          writeDocBytes(L"bad.sid", { 'P','S','I','D', 0, 99, 0, 0x7C }));
    }

    // ========================================================================
    // 1. Amazon Ion 1.0 Binary & Text (.10n / .ion)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".10n") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".ion"),
                  L"Stage15.Docs.ApacheIon.ExtensionsRegistered");

        ParsedOfficeDocument ionBinDoc, ionTxtDoc;
        bool okBin = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\telemetry_orders.10n", ionBinDoc);
        bool okTxt = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\service_catalog.ion", ionTxtDoc);

        tc.Record(okBin && ionBinDoc.valid &&
                  DocHasSubstr(ionBinDoc, L"ORD-9001") &&
                  DocHasSubstr(ionBinDoc, L"Ada Lovelace") &&
                  DocHasSubstr(ionBinDoc, L"149.95") &&
                  DocHasSubstr(ionBinDoc, L"2026-04-15T14:30:00Z") &&
                  DocHasSubstr(ionBinDoc, L"ORD-9002") &&
                  DocHasSubstr(ionBinDoc, L"Alan Turing") &&
                  DocHasSubstr(ionBinDoc, L"24.99"),
                  L"Stage15.Docs.ApacheIon.BinarySymbolTableDecimalsAndTimestamps");

        tc.Record(okTxt && ionTxtDoc.valid &&
                  DocHasSubstr(ionTxtDoc, L"svc_gateway") &&
                  DocHasSubstr(ionTxtDoc, L"edge-eu-central-1.internal") &&
                  DocHasSubstr(ionTxtDoc, L"8443") &&
                  DocHasSubstr(ionTxtDoc, L"svc_ledger") &&
                  HashDocContent(ionBinDoc) != HashDocContent(ionTxtDoc),
                  L"Stage15.Docs.ApacheIon.TextAnnotationsAndDistinctHash");

        // Synthetic binary Ion S-expression (type 12) with negative int (-42 = 0x31 0x2A) and ASCII string
        std::vector<uint8_t> synIon = { 0xE0, 0x01, 0x00, 0xEA };
        std::vector<uint8_t> sexprPay = { 0x31, 0x2A, 0x84, 'p','i','n','g' };
        synIon.push_back(static_cast<uint8_t>((12u << 4) | sexprPay.size()));
        synIon.insert(synIon.end(), sexprPay.begin(), sexprPay.end());
        ParsedOfficeDocument synIonDoc;
        bool okSynIon = SpreadsheetDocViewer::InspectOfficeFile(writeDocBytes(L"sexpr.10n", synIon), synIonDoc);
        tc.Record(okSynIon && synIonDoc.valid &&
                  DocHasSubstr(synIonDoc, L"-42") &&
                  DocHasSubstr(synIonDoc, L"ping") &&
                  HashDocContent(synIonDoc) != HashDocContent(ionBinDoc),
                  L"Stage15.Docs.ApacheIon.BinarySExprAndNegativeInt");

        assertDocRejected(L"Stage15.Docs.ApacheIon.RejectEmptyBvmWithInvalidType",
                          writeDocBytes(L"bad_bvm.10n", { 0xE0, 0x01, 0x00, 0xEA, 0xFE }));
        assertDocRejected(L"Stage15.Docs.ApacheIon.RejectBinaryNoiseAsTextIon",
                          writeDocBytes(L"bad_txt.ion", { '$','i','o','n', 0x00, 0xFF, 0xAA }));
    }

    // ========================================================================
    // 2. Universal Binary JSON (.ubj / .ubjson)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".ubj") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".ubjson"),
                  L"Stage15.Docs.Ubjson.ExtensionsRegistered");

        ParsedOfficeDocument ubjSample;
        bool okUbj = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\sensor_matrix.ubj", ubjSample);
        tc.Record(okUbj && ubjSample.valid &&
                  DocHasSubstr(ubjSample, L"rack-north-01") &&
                  DocHasSubstr(ubjSample, L"23.75") &&
                  DocHasSubstr(ubjSample, L"3.14159265358979323846") &&
                  DocHasSubstr(ubjSample, L"rack-south-02") &&
                  DocHasSubstr(ubjSample, L"27.125"),
                  L"Stage15.Docs.Ubjson.SensorMatrixTypedFloatArrayAndHighPrecision");

        // Second distinct UBJSON (.ubjson) object with [$i#U\x03] optimized int8 array
        std::vector<uint8_t> ubjCluster = {
            '{',
            'U', 7, 'c','l','u','s','t','e','r', 'S', 'U', 7, 'e','u','-','w','e','s','t',
            'U', 6, 's','h','a','r','d','s',     '[', '$', 'i', '#', 'U', 3, 4, 8, 16,
            'U', 6, 'a','c','t','i','v','e',     'T',
            '}'
        };
        ParsedOfficeDocument ubjClusterDoc;
        bool okUbj2 = SpreadsheetDocViewer::InspectOfficeFile(writeDocBytes(L"cluster.ubjson", ubjCluster), ubjClusterDoc);
        tc.Record(okUbj2 && ubjClusterDoc.valid &&
                  DocHasSubstr(ubjClusterDoc, L"eu-west") &&
                  DocHasSubstr(ubjClusterDoc, L"16") &&
                  HashDocContent(ubjSample) != HashDocContent(ubjClusterDoc),
                  L"Stage15.Docs.Ubjson.OptimizedIntArrayAndDistinctHash");

        assertDocRejected(L"Stage15.Docs.Ubjson.RejectTypeMarkerWithoutCount",
                          writeDocBytes(L"no_count.ubj", { '[', '$', 'i', 1, 2, ']' }));
        assertDocRejected(L"Stage15.Docs.Ubjson.RejectTruncatedString",
                          writeDocBytes(L"trunc_str.ubj", { 'S', 'U', 50, 'a', 'b' }));
        assertDocRejected(L"Stage15.Docs.Ubjson.RejectInvalidRootMarker",
                          writeDocBytes(L"bad_marker.ubjson", { 0x00, 0xFF, 0xAA }));
    }

    // ========================================================================
    // 3. Mozilla Firefox LZ4 JSON (.jsonlz4 / .mozlz4 / .baklz4)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".jsonlz4") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".mozlz4") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".baklz4"),
                  L"Stage15.Docs.MozillaLz4Json.ExtensionsRegistered");

        ParsedOfficeDocument bmDoc, sessDoc;
        bool okBm   = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\firefox_bookmarks.jsonlz4", bmDoc);
        bool okSess = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\firefox_session.mozlz4", sessDoc);

        tc.Record(okBm && bmDoc.valid &&
                  SheetHasSubstr(bmDoc, L"Firefox_Bookmarks", L"MDN Web Docs") &&
                  SheetHasSubstr(bmDoc, L"Firefox_Bookmarks", L"https://developer.mozilla.org/") &&
                  SheetHasSubstr(bmDoc, L"Firefox_Bookmarks", L"RFC 9110 HTTP Semantics"),
                  L"Stage15.Docs.MozillaLz4Json.BookmarksTreeSample");

        tc.Record(okSess && sessDoc.valid &&
                  SheetHasSubstr(sessDoc, L"Firefox_Session_Tabs", L"SQLite Database File Format") &&
                  SheetHasSubstr(sessDoc, L"Firefox_Session_Tabs", L"Apache Arrow Columnar Format") &&
                  HashDocContent(bmDoc) != HashDocContent(sessDoc),
                  L"Stage15.Docs.MozillaLz4Json.SessionTabsSampleAndDistinctHash");

        // Synthetic .baklz4 Search Engines archive compressed with an LZ4 literal + LZ77 match + final literals
        const std::string searchJson = "{\"engines\":[{\"_name\":\"DuckDuckGo\",\"_shortName\":\"ddg\"}]}";
        std::vector<uint8_t> bakLz4;
        AppendBytes(bakLz4, "mozLz40\0", 8);
        AppendU32LE(bakLz4, static_cast<uint32_t>(searchJson.size()));
        // Single literal-only LZ4 sequence (valid end-of-block rule)
        AppendU8(bakLz4, 0xF0u);
        size_t remLit = searchJson.size() - 15u;
        while (remLit >= 255u) { AppendU8(bakLz4, 255u); remLit -= 255u; }
        AppendU8(bakLz4, static_cast<uint8_t>(remLit));
        AppendStr(bakLz4, searchJson);

        ParsedOfficeDocument bakDoc;
        bool okBak = SpreadsheetDocViewer::InspectOfficeFile(writeDocBytes(L"search.baklz4", bakLz4), bakDoc);
        tc.Record(okBak && bakDoc.valid &&
                  SheetHasSubstr(bakDoc, L"Firefox_Search_Engines", L"DuckDuckGo") &&
                  SheetHasSubstr(bakDoc, L"Firefox_Search_Engines", L"ddg") &&
                  HashDocContent(bakDoc) != HashDocContent(bmDoc),
                  L"Stage15.Docs.MozillaLz4Json.SearchEnginesBakLz4");

        assertDocRejected(L"Stage15.Docs.MozillaLz4Json.RejectPlainJsonWithoutMozLz4Magic",
                          writeDocText(L"plain.jsonlz4", "{\"guid\":\"root\"}"));
        std::vector<uint8_t> nonJsonMoz = { 'm','o','z','L','z','4','0',0, 6,0,0,0, 0x60, 'n','o','t','j','s','n' };
        assertDocRejected(L"Stage15.Docs.MozillaLz4Json.RejectNonJsonDecompressedPayload",
                          writeDocBytes(L"nonjson.mozlz4", nonJsonMoz));
        std::vector<uint8_t> badMatchMoz = { 'm','o','z','L','z','4','0',0, 16,0,0,0, 0x24, '{','}', 0x00, 0x00 };
        assertDocRejected(L"Stage15.Docs.MozillaLz4Json.RejectZeroOffsetLz4Match",
                          writeDocBytes(L"badmatch.baklz4", badMatchMoz));
    }

    // ========================================================================
    // 4. Quicken Interchange Format (.qif)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".qif"),
                  L"Stage15.Docs.QuickenQif.ExtensionRegistered");

        ParsedOfficeDocument qifDoc;
        bool okQif = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\checking_ledger.qif", qifDoc);
        tc.Record(okQif && qifDoc.valid &&
                  SheetHasSubstr(qifDoc, L"QIF_Accounts", L"Operating Checking") &&
                  SheetHasSubstr(qifDoc, L"QIF_Accounts", L"12500.00") &&
                  SheetHasSubstr(qifDoc, L"QIF_Transactions", L"Client Retainer Deposit") &&
                  SheetHasSubstr(qifDoc, L"QIF_Transactions", L"Cloud Infrastructure Co") &&
                  SheetHasSubstr(qifDoc, L"QIF_Splits", L"Hosting:Compute") &&
                  SheetHasSubstr(qifDoc, L"QIF_Splits", L"-240.50") &&
                  SheetHasSubstr(qifDoc, L"QIF_Splits", L"Hosting:Storage"),
                  L"Stage15.Docs.QuickenQif.CheckingAccountsTransactionsAndSplits");

        const char* invQif =
            "!Type:Invst\n"
            "D2026-04-10\n"
            "NBuy\n"
            "YGlobal Semiconductor ETF\n"
            "I125.50\n"
            "Q20\n"
            "O4.95\n"
            "T2514.95\n"
            "^\n";
        ParsedOfficeDocument invDoc;
        bool okInv = SpreadsheetDocViewer::InspectOfficeFile(writeDocText(L"brokerage.qif", invQif), invDoc);
        tc.Record(okInv && invDoc.valid &&
                  DocHasSubstr(invDoc, L"Global Semiconductor ETF") &&
                  DocHasSubstr(invDoc, L"2514.95") &&
                  HashDocContent(qifDoc) != HashDocContent(invDoc),
                  L"Stage15.Docs.QuickenQif.InvestmentBrokerageAndDistinctHash");

        assertDocRejected(L"Stage15.Docs.QuickenQif.RejectCsvWithoutBangHeader",
                          writeDocText(L"not_qif.qif", "Date,Amount,Payee\n2026-03-01,100.00,Test\n"));
        assertDocRejected(L"Stage15.Docs.QuickenQif.RejectEmptyQifSection",
                          writeDocText(L"empty.qif", "!Type:Bank\n^\n"));
    }

    // ========================================================================
    // 5. Open Financial Exchange SGML & XML (.ofx / .qfx)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".ofx") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".qfx"),
                  L"Stage15.Docs.OpenFinancialOfx.ExtensionsRegistered");

        ParsedOfficeDocument ofxDoc, qfxDoc;
        bool okOfx = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\bank_statement_v1.ofx", ofxDoc);
        bool okQfx = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\credit_card_v2.qfx", qfxDoc);

        tc.Record(okOfx && ofxDoc.valid &&
                  DocHasSubstr(ofxDoc, L"Nordic Commercial Bank") &&
                  DocHasSubstr(ofxDoc, L"DE89370400440532013000") &&
                  DocHasSubstr(ofxDoc, L"ACME Robotics GmbH") &&
                  DocHasSubstr(ofxDoc, L"4800.00") &&
                  DocHasSubstr(ofxDoc, L"Precision Lab Supplies") &&
                  DocHasSubstr(ofxDoc, L"Logic analyzer probes & cables") &&
                  DocHasSubstr(ofxDoc, L"19410.10"),
                  L"Stage15.Docs.OpenFinancialOfx.SgmlBankStatementAndEntityDecode");

        tc.Record(okQfx && qfxDoc.valid &&
                  DocHasSubstr(qfxDoc, L"Pacific Card Services") &&
                  DocHasSubstr(qfxDoc, L"4111-XXXX-XXXX-8842") &&
                  DocHasSubstr(qfxDoc, L"Transit Rail Pass") &&
                  DocHasSubstr(qfxDoc, L"-74.50") &&
                  HashDocContent(ofxDoc) != HashDocContent(qfxDoc),
                  L"Stage15.Docs.OpenFinancialOfx.XmlCreditCardQfxAndDistinctHash");

        assertDocRejected(L"Stage15.Docs.OpenFinancialOfx.RejectHtmlWithoutOfxRoot",
                          writeDocText(L"fake.ofx", "<html><body><h1>Bank Statement</h1></body></html>"));
        assertDocRejected(L"Stage15.Docs.OpenFinancialOfx.RejectBinaryNullPayload",
                          writeDocBytes(L"binary.qfx", { '<','O','F','X','>', 0x00, 0xFF, '<','/','O','F','X','>' }));
    }

    // ========================================================================
    // 6. Mapbox Vector Tile (.mvt)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".mvt"),
                  L"Stage15.Docs.MapboxMvt.ExtensionRegistered");

        ParsedOfficeDocument mvtDoc;
        bool okMvt = SpreadsheetDocViewer::InspectOfficeFile(gisDir + L"\\city_transit_tile.mvt", mvtDoc);
        tc.Record(okMvt && mvtDoc.valid && mvtDoc.hasGeoTrack && !mvtDoc.geoPoints.empty() &&
                  SheetHasSubstr(mvtDoc, L"MVT_Layers", L"buildings") &&
                  SheetHasSubstr(mvtDoc, L"MVT_Layers", L"transit_stops") &&
                  SheetHasSubstr(mvtDoc, L"MVT_Features", L"POLYGON") &&
                  SheetHasSubstr(mvtDoc, L"MVT_Features", L"Central Library") &&
                  SheetHasSubstr(mvtDoc, L"MVT_Features", L"28.5") &&
                  SheetHasSubstr(mvtDoc, L"MVT_Features", L"U2 Metro"),
                  L"Stage15.Docs.MapboxMvt.GzipLayersFeaturesAndGeoPreview");

        // Second distinct uncompressed Protobuf .mvt with LINESTRING layer "waterways"
        std::vector<uint8_t> geomCmds;
        AppendPbVarint(geomCmds, (1u << 3) | 1u); // MoveTo(1)
        AppendPbVarint(geomCmds, ZigZag32(100));
        AppendPbVarint(geomCmds, ZigZag32(200));
        AppendPbVarint(geomCmds, (2u << 3) | 2u); // LineTo(2)
        AppendPbVarint(geomCmds, ZigZag32(300));
        AppendPbVarint(geomCmds, ZigZag32(150));
        AppendPbVarint(geomCmds, ZigZag32(250));
        AppendPbVarint(geomCmds, ZigZag32(-80));

        std::vector<uint8_t> tagsPacked = { 0u, 0u };
        std::vector<uint8_t> featMsg;
        AppendPbFieldVarint(featMsg, 1, 77);
        AppendPbFieldBytes(featMsg, 2, tagsPacked);
        AppendPbFieldVarint(featMsg, 3, 2); // LINESTRING
        AppendPbFieldBytes(featMsg, 4, geomCmds);

        std::vector<uint8_t> valMsg;
        AppendPbFieldStr(valMsg, 1, "Vistula");

        std::vector<uint8_t> layerMsg;
        AppendPbFieldVarint(layerMsg, 15, 2);
        AppendPbFieldStr(layerMsg, 1, "waterways");
        AppendPbFieldBytes(layerMsg, 2, featMsg);
        AppendPbFieldStr(layerMsg, 3, "river");
        AppendPbFieldBytes(layerMsg, 4, valMsg);
        AppendPbFieldVarint(layerMsg, 5, 2048);

        std::vector<uint8_t> rawTile;
        AppendPbFieldBytes(rawTile, 3, layerMsg);

        ParsedOfficeDocument riverMvtDoc;
        bool okRiverMvt = SpreadsheetDocViewer::InspectOfficeFile(writeDocBytes(L"river_tile.mvt", rawTile), riverMvtDoc);
        tc.Record(okRiverMvt && riverMvtDoc.valid && riverMvtDoc.hasGeoTrack &&
                  SheetHasSubstr(riverMvtDoc, L"MVT_Layers", L"waterways") &&
                  SheetHasSubstr(riverMvtDoc, L"MVT_Features", L"LINESTRING") &&
                  SheetHasSubstr(riverMvtDoc, L"MVT_Features", L"Vistula") &&
                  HashDocContent(mvtDoc) != HashDocContent(riverMvtDoc),
                  L"Stage15.Docs.MapboxMvt.UncompressedLinestringAndDistinctHash");

        assertDocRejected(L"Stage15.Docs.MapboxMvt.RejectCorruptGzipMvt",
                          writeDocBytes(L"bad_gz.mvt", { 0x1F, 0x8B, 0x08, 0x00, 0xFF, 0xEE, 0xDD, 0xCC }));
        assertDocRejected(L"Stage15.Docs.MapboxMvt.RejectNonMvtProtobuf",
                          writeDocBytes(L"no_layers.mvt", { 0x08, 0x01, 0x10, 0x02 }));
    }

    // ========================================================================
    // 7. OGC Well-Known Text & Binary (.wkt / .wkb)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".wkt") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".wkb"),
                  L"Stage15.Docs.WellKnownGeom.ExtensionsRegistered");

        ParsedOfficeDocument wktDoc, wkbDoc;
        bool okWkt = SpreadsheetDocViewer::InspectOfficeFile(gisDir + L"\\alpine_route.wkt", wktDoc);
        bool okWkb = SpreadsheetDocViewer::InspectOfficeFile(gisDir + L"\\harbor_zones.wkb", wkbDoc);

        tc.Record(okWkt && wktDoc.valid && wktDoc.hasGeoTrack && wktDoc.geoPoints.size() >= 3 &&
                  DocHasSubstr(wktDoc, L"LINESTRING Z") &&
                  DocHasSubstr(wktDoc, L"POLYGON") &&
                  DocHasSubstr(wktDoc, L"MULTIPOINT") &&
                  DocHasSubstr(wktDoc, L"4326"),
                  L"Stage15.Docs.WellKnownGeom.EwktAlpineRouteAndGeoTrack");

        tc.Record(okWkb && wkbDoc.valid && wkbDoc.hasGeoTrack &&
                  DocHasSubstr(wkbDoc, L"LINESTRING") &&
                  DocHasSubstr(wkbDoc, L"POINT Z") &&
                  DocHasSubstr(wkbDoc, L"4326") &&
                  HashDocContent(wktDoc) != HashDocContent(wkbDoc),
                  L"Stage15.Docs.WellKnownGeom.EwkbHarborZonesAndDistinctHash");

        // Third variant: Hex-encoded Big-Endian WKB POINT (12.0 50.0)
        const char* hexBeWkb = "000000000140280000000000004049000000000000\n";
        ParsedOfficeDocument hexWkbDoc;
        bool okHexWkb = SpreadsheetDocViewer::InspectOfficeFile(writeDocText(L"hex_be.wkb", hexBeWkb), hexWkbDoc);
        tc.Record(okHexWkb && hexWkbDoc.valid && hexWkbDoc.hasGeoTrack &&
                  DocHasSubstr(hexWkbDoc, L"POINT") &&
                  HashDocContent(hexWkbDoc) != HashDocContent(wkbDoc),
                  L"Stage15.Docs.WellKnownGeom.HexEncodedBigEndianWkb");

        assertDocRejected(L"Stage15.Docs.WellKnownGeom.RejectUnknownWktType",
                          writeDocText(L"bad.wkt", "HEXAGON ((0 0, 1 1, 2 0))"));
        assertDocRejected(L"Stage15.Docs.WellKnownGeom.RejectInvalidWkbTypeCode",
                          writeDocBytes(L"bad.wkb", { 0x01, 0x63, 0x00, 0x00, 0x00, 0,0,0,0,0,0,0,0 }));
    }

    // ========================================================================
    // 8. Apple Core Audio Format (.caf)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".caf"),
                  L"Stage15.Docs.CoreAudioCaf.ExtensionRegistered");

        ParsedOfficeDocument cafDoc;
        bool okCaf = SpreadsheetDocViewer::InspectOfficeFile(audioDir + L"\\studio_stem.caf", cafDoc);
        tc.Record(okCaf && cafDoc.valid &&
                  DocHasSubstr(cafDoc, L"Linear PCM") &&
                  DocHasSubstr(cafDoc, L"48000") &&
                  DocHasSubstr(cafDoc, L"Aurora Borealis Stem") &&
                  DocHasSubstr(cafDoc, L"Explorer2 Synth Lab") &&
                  DocHasSubstr(cafDoc, L"CueP") &&
                  DocHasSubstr(cafDoc, L"240"),
                  L"Stage15.Docs.CoreAudioCaf.StudioStemLpcmMarkersAndPacketTable");

        // Second distinct synthetic .caf: 96 kHz 6-channel ALAC with streaming data chunk (size = -1)
        std::vector<uint8_t> alacCaf;
        AppendStr(alacCaf, "caff");
        AppendU16BE(alacCaf, 1u);
        AppendU16BE(alacCaf, 0u);
        // desc chunk (32B)
        AppendStr(alacCaf, "desc");
        AppendU64BE(alacCaf, 32ULL);
        AppendF64BE(alacCaf, 96000.0);
        AppendStr(alacCaf, "alac");
        AppendU32BE(alacCaf, 0u);
        AppendU32BE(alacCaf, 0u);
        AppendU32BE(alacCaf, 4096u);
        AppendU32BE(alacCaf, 6u);
        AppendU32BE(alacCaf, 24u);
        // info chunk
        std::vector<uint8_t> infoPay;
        AppendU32BE(infoPay, 1u);
        AppendBytes(infoPay, "album\0Spatial Master\0", 21);
        AppendStr(alacCaf, "info");
        AppendU64BE(alacCaf, infoPay.size());
        alacCaf.insert(alacCaf.end(), infoPay.begin(), infoPay.end());
        // streaming data chunk (chunkSize = -1)
        AppendStr(alacCaf, "data");
        AppendU64BE(alacCaf, 0xFFFFFFFFFFFFFFFFULL);
        AppendU32BE(alacCaf, 1u); // mEditCount = 1
        alacCaf.insert(alacCaf.end(), 16, 0x55u);

        ParsedOfficeDocument alacDoc;
        bool okAlac = SpreadsheetDocViewer::InspectOfficeFile(writeDocBytes(L"spatial_alac.caf", alacCaf), alacDoc);
        tc.Record(okAlac && alacDoc.valid &&
                  DocHasSubstr(alacDoc, L"Apple Lossless (ALAC)") &&
                  DocHasSubstr(alacDoc, L"96000") &&
                  DocHasSubstr(alacDoc, L"Spatial Master") &&
                  DocHasSubstr(alacDoc, L"open-ended length=-1") &&
                  HashDocContent(cafDoc) != HashDocContent(alacDoc),
                  L"Stage15.Docs.CoreAudioCaf.AlacSurroundStreamingAndDistinctHash");

        // Corrupt 1: caff without mandatory desc chunk
        std::vector<uint8_t> noDescCaf;
        AppendStr(noDescCaf, "caff");
        AppendU16BE(noDescCaf, 1u); AppendU16BE(noDescCaf, 0u);
        AppendStr(noDescCaf, "free"); AppendU64BE(noDescCaf, 32ULL);
        noDescCaf.insert(noDescCaf.end(), 32, 0u);
        assertDocRejected(L"Stage15.Docs.CoreAudioCaf.RejectMissingDescChunk",
                          writeDocBytes(L"nodesc.caf", noDescCaf));

        // Corrupt 2: unsupported caff file version 9
        std::vector<uint8_t> badVerCaf = alacCaf;
        badVerCaf[5] = 9u;
        assertDocRejected(L"Stage15.Docs.CoreAudioCaf.RejectUnsupportedVersion",
                          writeDocBytes(L"badver.caf", badVerCaf));
    }

    // ========================================================================
    // 9. WavPack Lossless / Hybrid / DSD Audio (.wv)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".wv"),
                  L"Stage15.Docs.WavPackWv.ExtensionRegistered");

        ParsedOfficeDocument wvDoc;
        bool okWv = SpreadsheetDocViewer::InspectOfficeFile(audioDir + L"\\master_take.wv", wvDoc);
        tc.Record(okWv && wvDoc.valid &&
                  DocHasSubstr(wvDoc, L"96000") &&
                  DocHasSubstr(wvDoc, L"24-bit") &&
                  DocHasSubstr(wvDoc, L"Lossless Studio Take") &&
                  DocHasSubstr(wvDoc, L"Explorer2 DSP") &&
                  DocHasSubstr(wvDoc, L"Reference Suite"),
                  L"Stage15.Docs.WavPackWv.MasterTakeSubBlocksMd5AndApeTag");

        // Second distinct synthetic .wv: WavPack 5 DSD64 (352800 Hz base * x8 DSD multiplier = 2822400 Hz, 1-bit DSD)
        std::vector<uint8_t> dsdSubs = {
            static_cast<uint8_t>(0x27u | 0x40u), 2u, 0x20u, 0x62u, 0x05u, 0x00u, // Custom rate 352800 Hz (0x056220)
            static_cast<uint8_t>(0x0Eu | 0x40u), 1u, 0x01u, 0x00u                // DSD shift=1 -> multiplier x8
        };
        std::vector<uint8_t> dsdWv;
        AppendStr(dsdWv, "wvpk");
        AppendU32LE(dsdWv, static_cast<uint32_t>(24u + dsdSubs.size()));
        AppendU16LE(dsdWv, 0x0410u);
        AppendU8(dsdWv, 0u); AppendU8(dsdWv, 0u);
        AppendU32LE(dsdWv, 352800u);
        AppendU32LE(dsdWv, 0u);
        AppendU32LE(dsdWv, 8192u);
        AppendU32LE(dsdWv, 0x80000000u | 0x0800u | 0x1000u); // DSD_FLAG | INITIAL_BLOCK | FINAL_BLOCK
        AppendU32LE(dsdWv, 0x11223344u);
        dsdWv.insert(dsdWv.end(), dsdSubs.begin(), dsdSubs.end());

        ParsedOfficeDocument dsdDoc;
        bool okDsd = SpreadsheetDocViewer::InspectOfficeFile(writeDocBytes(L"dsd64.wv", dsdWv), dsdDoc);
        tc.Record(okDsd && dsdDoc.valid &&
                  DocHasSubstr(dsdDoc, L"DSD") &&
                  DocHasSubstr(dsdDoc, L"2822400") &&
                  HashDocContent(wvDoc) != HashDocContent(dsdDoc),
                  L"Stage15.Docs.WavPackWv.Dsd64SubBlocksAndDistinctHash");

        std::vector<uint8_t> badVerWv = dsdWv;
        PutU16LE(badVerWv.data() + 8, 0x0300u); // unsupported pre-v4 version
        assertDocRejected(L"Stage15.Docs.WavPackWv.RejectUnsupportedVersion",
                          writeDocBytes(L"badver.wv", badVerWv));
        assertDocRejected(L"Stage15.Docs.WavPackWv.RejectTruncatedHeader",
                          writeDocBytes(L"trunc.wv", { 'w','v','p','k', 24,0,0,0, 0x10, 0x04 }));
    }

    // ========================================================================
    // 10. Apple Safari / macOS Binary Cookies (.binarycookies)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".binarycookies"),
                  L"Stage15.Docs.AppleBinaryCookies.ExtensionRegistered");

        ParsedOfficeDocument cookDoc;
        bool okCook = SpreadsheetDocViewer::InspectOfficeFile(securityDir + L"\\safari_jar.binarycookies", cookDoc);
        tc.Record(okCook && cookDoc.valid &&
                  DocHasSubstr(cookDoc, L".github.com") &&
                  DocHasSubstr(cookDoc, L"user_session") &&
                  DocHasSubstr(cookDoc, L"ghs_98AF71C02E") &&
                  DocHasSubstr(cookDoc, L"Session auth") &&
                  DocHasSubstr(cookDoc, L".developer.apple.com") &&
                  DocHasSubstr(cookDoc, L"ADCDownloadAuth") &&
                  DocHasSubstr(cookDoc, L"HttpOnly"),
                  L"Stage15.Docs.AppleBinaryCookies.SafariJarSample");

        // Second distinct synthetic .binarycookies with .corp.internal & SameSite=Strict (0x18)
        std::vector<uint8_t> recPay;
        const std::string dom = ".corp.internal", nm = "sso_ticket", pth = "/admin", val = "tkt_778899";
        const uint32_t uOff = 56u;
        const uint32_t nOff = uOff + static_cast<uint32_t>(dom.size() + 1u);
        const uint32_t pOff = nOff + static_cast<uint32_t>(nm.size() + 1u);
        const uint32_t vOff = pOff + static_cast<uint32_t>(pth.size() + 1u);
        const uint32_t recSz = vOff + static_cast<uint32_t>(val.size() + 1u);
        AppendU32LE(recPay, recSz);
        AppendU32LE(recPay, 0u);
        AppendU32LE(recPay, 0x01u | 0x04u | 0x18u); // Secure | HttpOnly | SameSite=Strict
        AppendU32LE(recPay, 0u);
        AppendU32LE(recPay, uOff);
        AppendU32LE(recPay, nOff);
        AppendU32LE(recPay, pOff);
        AppendU32LE(recPay, vOff);
        AppendU32LE(recPay, 0u); // no comment
        AppendU32LE(recPay, 0u);
        AppendF64LE(recPay, 810000000.0);
        AppendF64LE(recPay, 800000000.0);
        AppendBytes(recPay, dom.c_str(), dom.size() + 1u);
        AppendBytes(recPay, nm.c_str(), nm.size() + 1u);
        AppendBytes(recPay, pth.c_str(), pth.size() + 1u);
        AppendBytes(recPay, val.c_str(), val.size() + 1u);

        std::vector<uint8_t> page;
        AppendU32BE(page, 0x00000100u);
        AppendU32LE(page, 1u);
        AppendU32LE(page, 16u);
        AppendU32LE(page, 0u);
        page.insert(page.end(), recPay.begin(), recPay.end());

        std::vector<uint8_t> synCook;
        AppendStr(synCook, "cook");
        AppendU32BE(synCook, 1u);
        AppendU32BE(synCook, static_cast<uint32_t>(page.size()));
        synCook.insert(synCook.end(), page.begin(), page.end());
        AppendU64BE(synCook, 0ULL);

        ParsedOfficeDocument synCookDoc;
        bool okSynCook = SpreadsheetDocViewer::InspectOfficeFile(writeDocBytes(L"corp.binarycookies", synCook), synCookDoc);
        tc.Record(okSynCook && synCookDoc.valid &&
                  DocHasSubstr(synCookDoc, L".corp.internal") &&
                  DocHasSubstr(synCookDoc, L"sso_ticket") &&
                  DocHasSubstr(synCookDoc, L"SameSite=Strict") &&
                  HashDocContent(cookDoc) != HashDocContent(synCookDoc),
                  L"Stage15.Docs.AppleBinaryCookies.SameSiteStrictAndDistinctHash");

        std::vector<uint8_t> badPageCook = synCook;
        PutU32BE(badPageCook.data() + 12u, 0xDEADBEEFu); // invalid page header magic
        assertDocRejected(L"Stage15.Docs.AppleBinaryCookies.RejectInvalidPageMagic",
                          writeDocBytes(L"bad_page.binarycookies", badPageCook));
        assertDocRejected(L"Stage15.Docs.AppleBinaryCookies.RejectPageSizeOverflow",
                          writeDocBytes(L"overflow.binarycookies", { 'c','o','o','k', 0,0,0,1, 0,0x10,0,0, 0,0,1,0 }));
    }

    // ========================================================================
    // 11. Windows Forensics Jump Lists (.automaticdestinations-ms)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".automaticdestinations-ms"),
                  L"Stage15.Docs.WindowsJumpList.ExtensionRegistered");

        ParsedOfficeDocument jlDoc;
        bool okJl = SpreadsheetDocViewer::InspectOfficeFile(securityDir + L"\\recent_docs.automaticdestinations-ms", jlDoc);
        tc.Record(okJl && jlDoc.valid &&
                  DocHasSubstr(jlDoc, L"C:\\Projects\\Explorer2\\README.md") &&
                  DocHasSubstr(jlDoc, L"--preview") &&
                  DocHasSubstr(jlDoc, L"WS-ANALYST01") &&
                  DocHasSubstr(jlDoc, L"Pinned") &&
                  DocHasSubstr(jlDoc, L"D:\\Forensics\\capture_01.evtx") &&
                  DocHasSubstr(jlDoc, L"--verify"),
                  L"Stage15.Docs.WindowsJumpList.Win10DestListAndEmbeddedShllinkSample");

        // Second distinct synthetic .automaticdestinations-ms: Windows 7/8 DestList v1 (114-byte fixed entry header)
        std::vector<uint8_t> dlWin7;
        AppendU32LE(dlWin7, 1u); // version 1 (Win7/8)
        AppendU32LE(dlWin7, 1u); // 1 entry
        AppendU32LE(dlWin7, 0u); // 0 pinned
        AppendU32LE(dlWin7, 0u);
        AppendU64LE(dlWin7, 1ULL);
        AppendU64LE(dlWin7, 5ULL);

        std::vector<uint8_t> entHdr(114u, 0u);
        PutU64LE(entHdr.data() + 0u, 0xAABBCCDDEEFF0011ULL);
        std::memcpy(entHdr.data() + 72u, "WORKSTATION7", 12);
        PutU32LE(entHdr.data() + 88u, 1u); // entry ID = 1
        PutU64LE(entHdr.data() + 100u, 134180000000000000ULL);
        PutI32LE(entHdr.data() + 108u, -1); // unpinned
        const std::wstring w7Target = L"E:\\Evidence\\disk_image.dd";
        PutU16LE(entHdr.data() + 112u, static_cast<uint16_t>(w7Target.size()));
        dlWin7.insert(dlWin7.end(), entHdr.begin(), entHdr.end());
        for (wchar_t ch : w7Target) AppendU16LE(dlWin7, static_cast<uint16_t>(ch));

        std::vector<uint8_t> win7Cfb = BuildMinimalOle2Cfb({ { L"DestList", dlWin7 } });
        ParsedOfficeDocument win7JlDoc;
        bool okWin7Jl = SpreadsheetDocViewer::InspectOfficeFile(
            writeDocBytes(L"win7_forensics.automaticdestinations-ms", win7Cfb), win7JlDoc);
        tc.Record(okWin7Jl && win7JlDoc.valid &&
                  DocHasSubstr(win7JlDoc, L"114B entries") &&
                  DocHasSubstr(win7JlDoc, L"WORKSTATION7") &&
                  DocHasSubstr(win7JlDoc, L"E:\\Evidence\\disk_image.dd") &&
                  HashDocContent(jlDoc) != HashDocContent(win7JlDoc),
                  L"Stage15.Docs.WindowsJumpList.Win7DestList114ByteAndDistinctHash");

        // Corrupt 1: OLE2 CFB without DestList stream
        std::vector<uint8_t> noDestCfb = BuildMinimalOle2Cfb({ { L"Summary", { 1, 2, 3, 4 } } });
        assertDocRejected(L"Stage15.Docs.WindowsJumpList.RejectOle2WithoutDestList",
                          writeDocBytes(L"nodest.automaticdestinations-ms", noDestCfb));

        // Corrupt 2: OLE2 CFB with unsupported DestList version 99
        std::vector<uint8_t> badVerDl = dlWin7;
        PutU32LE(badVerDl.data(), 99u);
        assertDocRejected(L"Stage15.Docs.WindowsJumpList.RejectUnsupportedDestListVersion",
                          writeDocBytes(L"badver.automaticdestinations-ms",
                                        BuildMinimalOle2Cfb({ { L"DestList", badVerDl } })));
    }

    for (const auto& f : createdDocFiles) DeleteFileW(f.c_str());
    RemoveDirectoryW(docWorkDir.c_str());
}

} // namespace Explorer2::SelfTestSuite
