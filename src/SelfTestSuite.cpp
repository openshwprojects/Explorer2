#include "SelfTestSuite.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "HexViewer.h"
#include "Model3DViewer.h"
#include "NotepadViewer.h"
#include "EdaViewer.h"
#include "SpreadsheetDocViewer.h"
#include "ImageFormatLoader.h"
#include "ArchiveExplorer.h"
#include "PdfViewer.h"
#include "HardwareInfo.h"
#include "LanScanner.h"
#include "BackupManager.h"
#include "FtpClient.h"
#include "FileEngine.h"
#include "TransferManager.h"
#include "ReaderGuard.h"
#include "TextLayoutCache.h"
#include "ImageCache.h"
#include "FileContentPane.h"
#include <winioctl.h>
#include <wincodec.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <sddl.h>
#include <stdexcept>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace Explorer2::SelfTestSuite {

namespace {

struct TestCounter {
    int passed = 0;
    int failed = 0;
    bool quietPass = false;
    std::wstring log;

    void Record(bool ok, const wchar_t* name, const std::wstring& detail = L"") {
        if (ok) {
            ++passed;
            if (quietPass) return;
            log += L"[PASS] ";
        } else {
            ++failed;
            log += L"[FAIL] ";
        }
        log += name;
        if (!detail.empty()) {
            log += L" — " + detail;
        }
        log += L"\r\n";
    }
};

static std::wstring GetScratchTestDir() {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"Explorer2_SelfTest";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

// Folder that holds the running executable (the repository root for a normal build).
static std::wstring GetExeDir() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    return PathUtils::GetParentPath(exePath);
}

// Tests that need a private, machine-specific input file take its path from an environment
// variable and report themselves as skipped when it is not set.
static std::wstring GetTestEnvVar(const wchar_t* name) {
    wchar_t buf[1024] = {};
    DWORD n = GetEnvironmentVariableW(name, buf, static_cast<DWORD>(std::size(buf)));
    return (n > 0 && n < std::size(buf)) ? std::wstring(buf, n) : std::wstring();
}

// The repository's testdata folder: next to the executable, or in the current directory when the
// executable was built somewhere else (build\...).
static std::wstring GetTestDataDir() {
    std::wstring dir = GetExeDir() + L"\\testdata";
    if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        wchar_t cwd[MAX_PATH] = {};
        GetCurrentDirectoryW(MAX_PATH, cwd);
        dir = std::wstring(cwd) + L"\\testdata";
    }
    return dir;
}

// Writes a minimal one-page PDF (200 x 200 pt, one filled square) with a cross-reference table, which
// Windows.Data.Pdf accepts and renders.
static bool WriteRenderablePdfForTest(const std::wstring& path) {
    const char* validPdf =
        "%PDF-1.4\n"
        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
        "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R >>\nendobj\n"
        "4 0 obj\n<< /Length 35 >>\nstream\n0.1 0.4 0.8 rg 20 20 160 160 re f\nendstream\nendobj\n"
        "xref\n0 5\n"
        "0000000000 65535 f \n"
        "0000000009 00000 n \n"
        "0000000058 00000 n \n"
        "0000000115 00000 n \n"
        "0000000202 00000 n \n"
        "trailer\n<< /Size 5 /Root 1 0 R >>\n"
        "startxref\n287\n%%EOF\n";
    return PathUtils::WriteFileBytes(path, validPdf, std::strlen(validPdf));
}

// Runs in a process of its own, started by Stage13.Test26 as `--self-test pdf-exit-child <report>`.
// Does what the program does when a PDF is selected, and returns. What is under test happens after that:
// the process then ends the way the program normally ends (wWinMain releases the PDF renderer, closes COM
// and returns), and it has to end with exit code 0.
//
// The order of events is the program's: its window is drawn with Direct2D first, which loads Windows'
// graphics libraries (DXGI, Direct3D), and only later a PDF page is rendered, which loads Windows' PDF
// library. In that order, and without PdfViewer::ReleaseRenderer, the process ends with exception 0x87A.
static int RunPdfExitChild(const std::wstring& reportPath) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    // A window that is never shown, drawn once with a Direct2D render target
    bool drew = false;
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target;
    if (hwnd && SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory.GetAddressOf())) &&
        SUCCEEDED(d2dFactory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(64, 64)), &target))) {
        target->BeginDraw();
        target->Clear(D2D1::ColorF(D2D1::ColorF::White));
        drew = SUCCEEDED(target->EndDraw());
    }

    wchar_t tmpDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmpDir);
    const std::wstring pdfPath = std::wstring(tmpDir) + L"e2_pdf_exit_child_" + std::to_wstring(GetCurrentProcessId()) + L".pdf";
    std::vector<uint8_t> bgra;
    uint32_t w = 0, h = 0, pages = 0;
    const bool rendered = WriteRenderablePdfForTest(pdfPath) &&
                          PdfViewer::RenderPdfPageToBgra(pdfPath, 0, 300, bgra, w, h, &pages) && w == 300 && h == 300;
    DeleteFileW(pdfPath.c_str());
    const bool libraryLoaded = GetModuleHandleW(L"Windows.Data.Pdf.dll") != nullptr;

    // The program lets go of its drawing resources and its window before it ends
    target.Reset();
    d2dFactory.Reset();
    if (hwnd) DestroyWindow(hwnd);

    char report[96] = {};
    const int n = sprintf_s(report, "drew=%d rendered=%d libraryLoaded=%d\r\n", drew ? 1 : 0, rendered ? 1 : 0, libraryLoaded ? 1 : 0);
    PathUtils::WriteFileBytes(reportPath, report, static_cast<size_t>(n));
    return (drew && rendered) ? 0 : 1;
}

// Stage 1: DRY PathUtils, File I/O, UTF-8/UTF-16, RFC 1951 DEFLATE & ZIP/TAR core stability (5 tests)
static void RunStage1Tests(TestCounter& tc) {
    std::wstring testDir = GetScratchTestDir();

    // Test 1.1: PathUtils normalization, extension extraction, parent & stem handling
    {
        bool ok = true;
        ok = ok && (PathUtils::GetExtension(L"C:\\dir\\Board.KiCad_PCB") == L".kicad_pcb");
        ok = ok && (PathUtils::ToUpper(L"abc_123") == L"ABC_123");
        ok = ok && (PathUtils::NormalizePathKey(L"W:/GIT/Explorer2/src/") == L"w:\\git\\explorer2\\src");
        ok = ok && (PathUtils::GetParentPath(L"C:\\Folder\\Sub\\") == L"C:\\Folder");
        ok = ok && (PathUtils::GetStem(L"archive.tar.gz", false) == L"archive.tar");
        tc.Record(ok, L"Stage1.Test1_PathUtils_DRY_Normalization");
    }

    // Test 1.2: UTF-8 <-> UTF-16 lossless round-trip with Polish diacritics & symbols
    {
        std::wstring orig = L"Zażółć gęślą jaźń — Explorer2 120.5 KB ✓";
        std::string u8 = PathUtils::WideToUtf8(orig);
        std::wstring back = PathUtils::Utf8ToWide(u8);
        bool ok = (!u8.empty() && back == orig);
        tc.Record(ok, L"Stage1.Test2_Utf8_Wide_RoundTrip", back);
    }

    // Test 1.3: Non-locking ReadFileBytes & WriteFileBytes round-trip
    {
        std::wstring fpath = testDir + L"\\stage1_io_test.bin";
        std::vector<uint8_t> payload(4096);
        for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>((i * 73 + 19) & 0xFF);
        bool wrote = PathUtils::WriteFileBytes(fpath, payload.data(), payload.size());
        std::vector<uint8_t> readBack;
        uint64_t totalSz = 0;
        bool readOk = PathUtils::ReadFileBytes(fpath, 1024 * 1024, readBack, &totalSz);
        DeleteFileW(fpath.c_str());
        bool ok = wrote && readOk && totalSz == 4096 && readBack == payload;
        tc.Record(ok, L"Stage1.Test3_SharedFileIO_NonLocking");
    }

    // Test 1.4: RFC 1951 Raw & Fixed Huffman DEFLATE decompression
    {
        // Stored DEFLATE block (BFINAL=1, BTYPE=00, LEN=5, NLEN=~5, "HELLO")
        const uint8_t storedDeflate[] = { 0x01, 0x05, 0x00, 0xFA, 0xFF, 'H', 'E', 'L', 'L', 'O' };
        std::vector<uint8_t> outStored;
        bool ok1 = ZipDeflate::InflateRaw(storedDeflate, sizeof(storedDeflate), outStored) &&
                   outStored.size() == 5 && std::memcmp(outStored.data(), "HELLO", 5) == 0;

        // Fixed Huffman DEFLATE stream for "Explorer2_Explorer2"
        // Let's also test Gzip header + DEFLATE block
        const uint8_t gzipHello[] = {
            0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0B,
            0x01, 0x05, 0x00, 0xFA, 0xFF, 'H', 'E', 'L', 'L', 'O',
            0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00
        };
        std::vector<uint8_t> outGzip;
        bool ok2 = ZipDeflate::InflateGzip(gzipHello, sizeof(gzipHello), outGzip) &&
                   outGzip.size() == 5 && std::memcmp(outGzip.data(), "HELLO", 5) == 0;
        tc.Record(ok1 && ok2, L"Stage1.Test4_RFC1951_Deflate_And_Gzip");
    }

    // Test 1.5: In-Memory & File ZIP Central Directory + Extraction verification
    {
        // Construct a valid minimal ZIP archive with 1 file "folder/test.txt" ("DATA1234")
        std::wstring zipPath = testDir + L"\\stage1_test.zip";
        const char* fn = "folder/test.txt";
        const char* content = "DATA1234";
        uint16_t fnLen = 15;
        uint32_t dataLen = 8;
        std::vector<uint8_t> zipBytes;
        auto push16 = [&](uint16_t v) { zipBytes.push_back(v & 0xFF); zipBytes.push_back((v >> 8) & 0xFF); };
        auto push32 = [&](uint32_t v) {
            zipBytes.push_back(v & 0xFF); zipBytes.push_back((v >> 8) & 0xFF);
            zipBytes.push_back((v >> 16) & 0xFF); zipBytes.push_back((v >> 24) & 0xFF);
        };
        // Local file header @ 0
        push32(0x04034B50u); push16(20); push16(0); push16(0); push16(0); push16(0);
        push32(0); push32(dataLen); push32(dataLen); push16(fnLen); push16(0);
        zipBytes.insert(zipBytes.end(), fn, fn + fnLen);
        zipBytes.insert(zipBytes.end(), content, content + dataLen);
        uint32_t cdOffset = static_cast<uint32_t>(zipBytes.size());
        // Central directory header
        push32(0x02014B50u); push16(20); push16(20); push16(0); push16(0); push16(0); push16(0);
        push32(0); push32(dataLen); push32(dataLen); push16(fnLen); push16(0); push16(0);
        push16(0); push16(0); push32(0); push32(0);
        zipBytes.insert(zipBytes.end(), fn, fn + fnLen);
        uint32_t cdSize = static_cast<uint32_t>(zipBytes.size()) - cdOffset;
        // EOCD
        push32(0x06054B50u); push16(0); push16(0); push16(1); push16(1);
        push32(cdSize); push32(cdOffset); push16(0);

        PathUtils::WriteFileBytes(zipPath, zipBytes.data(), zipBytes.size());
        std::vector<ZipDeflate::ArchiveItemMeta> entries;
        bool listed = ZipDeflate::ListZipEntriesFromFile(zipPath, entries);
        std::vector<uint8_t> extracted;
        bool extOk = ZipDeflate::ExtractZipFileByName(zipPath, L"folder/test.txt", extracted);
        DeleteFileW(zipPath.c_str());

        bool ok = listed && entries.size() == 1 && entries[0].name == L"test.txt" &&
                  extOk && extracted.size() == 8 && std::memcmp(extracted.data(), "DATA1234", 8) == 0;
        tc.Record(ok, L"Stage1.Test5_ZipCentralDir_And_Extraction");
    }
}

// Stage 2: EDA Viewer — KiCad PCB, KiCad Schematic, Eagle Board, Eagle Schematic, Gerber RS-274X + 3D Parts (5 tests)
static void RunStage2Tests(TestCounter& tc) {
    std::wstring testDir = GetScratchTestDir();

    // Test 2.1: KiCad PCB (.kicad_pcb) with footprints, SMD pads, vias, copper traces & 3D part classification
    {
        std::wstring pcbPath = testDir + L"\\test_board.kicad_pcb";
        const char* kicadPcb = R"((kicad_pcb (version 20221018)
          (net 0 "") (net 1 "VCC") (net 2 "GND")
          (gr_rect (start 0 0) (end 50 35) (layer "Edge.Cuts") (width 0.15))
          (segment (start 10 10) (end 30 10) (width 0.4) (layer "F.Cu") (net 1))
          (via (at 20 15) (size 0.8) (drill 0.4) (layers "F.Cu" "B.Cu") (net 2))
          (footprint "Package_SO:SOIC-8" (layer "F.Cu") (at 25 18 90)
            (fp_text reference "U1" (at 0 -3) (layer "F.SilkS"))
            (fp_text value "NE555" (at 0 3) (layer "F.Fab"))
            (pad "1" smd rect (at -1.9 -2.4 90) (size 0.6 1.5) (layers "F.Cu"))
            (pad "2" smd rect (at -0.63 -2.4 90) (size 0.6 1.5) (layers "F.Cu"))
          )
          (footprint "Resistor_SMD:R_0805" (layer "F.Cu") (at 12 12 0)
            (fp_text reference "R1" (at 0 -1.5) (layer "F.SilkS"))
            (fp_text value "10k" (at 0 1.5) (layer "F.Fab"))
            (pad "1" smd rect (at -0.95 0) (size 0.9 1.3) (layers "F.Cu"))
            (pad "2" smd rect (at 0.95 0) (size 0.9 1.3) (layers "F.Cu"))
          )
        ))";
        PathUtils::WriteFileBytes(pcbPath, kicadPcb, std::strlen(kicadPcb));
        ParsedEdaBoard brd;
        bool ok = EdaViewer::InspectEdaFile(pcbPath, brd) &&
                  brd.kind == EdaDocKind::KiCadPcb &&
                  brd.components.size() == 2 &&
                  brd.components[0].category == EdaPartCategory::IC_Package &&
                  brd.components[1].category == EdaPartCategory::ResistorSMD &&
                  brd.pads.size() == 4 && brd.vias.size() == 1 && brd.traces.size() >= 5;
        DeleteFileW(pcbPath.c_str());
        tc.Record(ok, L"Stage2.Test1_KiCad_PCB_And_3DParts");
    }

    // Test 2.2: KiCad Schematic (.kicad_sch) with wires, buses, and symbols
    {
        std::wstring schPath = testDir + L"\\test_schem.kicad_sch";
        const char* kicadSch = R"((kicad_sch (version 20230121)
          (wire (pts (xy 50.8 40.64) (xy 76.2 40.64)))
          (symbol (lib_id "Device:R") (at 63.5 40.64 0)
            (property "Reference" "R10" (at 63.5 38.1 0))
            (property "Value" "4.7k" (at 63.5 43.18 0))
          )
        ))";
        PathUtils::WriteFileBytes(schPath, kicadSch, std::strlen(kicadSch));
        ParsedEdaBoard sch;
        bool ok = EdaViewer::InspectEdaFile(schPath, sch) &&
                  sch.kind == EdaDocKind::KiCadSch &&
                  sch.components.size() == 1 &&
                  sch.components[0].refDes == L"R10" &&
                  sch.traces.size() >= 5;
        DeleteFileW(schPath.c_str());
        tc.Record(ok, L"Stage2.Test2_KiCad_Schematic_Parser");
    }

    // Test 2.3: Eagle XML Board (.brd) with packages, SMD/THT pads, elements, and copper wires
    {
        std::wstring brdPath = testDir + L"\\eagle_test.brd";
        const char* eagleBrd = R"(<?xml version="1.0" encoding="utf-8"?>
        <eagle version="9.6.2"><drawing><board>
          <plain><wire x1="0" y1="0" x2="40" y2="0" width="0.2" layer="20"/></plain>
          <libraries><library name="rcl"><packages>
            <package name="C0603">
              <smd name="1" x="-0.75" y="0" dx="0.8" dy="0.8" layer="1"/>
              <smd name="2" x="0.75" y="0" dx="0.8" dy="0.8" layer="1"/>
            </package>
          </packages></library></libraries>
          <elements>
            <element name="C1" library="rcl" package="C0603" value="100nF" x="15.5" y="10.2" rot="R90"/>
          </elements>
          <signals><signal name="GND">
            <wire x1="5" y1="5" x2="15.5" y2="10.2" width="0.3" layer="1"/>
            <via x="10" y="8" extent="1-16" drill="0.5" diameter="1.0"/>
          </signal></signals>
        </board></drawing></eagle>)";
        PathUtils::WriteFileBytes(brdPath, eagleBrd, std::strlen(eagleBrd));
        ParsedEdaBoard brd;
        bool ok = EdaViewer::InspectEdaFile(brdPath, brd) &&
                  brd.kind == EdaDocKind::EagleBrd &&
                  brd.components.size() == 1 &&
                  brd.components[0].refDes == L"C1" &&
                  brd.components[0].category == EdaPartCategory::CapacitorSMD &&
                  brd.pads.size() == 2 && brd.vias.size() == 1;
        DeleteFileW(brdPath.c_str());
        tc.Record(ok, L"Stage2.Test3_Eagle_XML_Board_And_3DParts");
    }

    // Test 2.4: Eagle XML Schematic (.sch) with parts, instances, and net wires
    {
        std::wstring schPath = testDir + L"\\eagle_test.sch";
        const char* eagleSch = R"(<?xml version="1.0" encoding="utf-8"?>
        <eagle version="9.6.2"><drawing><schematic>
          <parts><part name="U2" library="mcu" deviceset="ATMEGA328P" value="ATMEGA328P"/></parts>
          <sheets><sheet>
            <instances><instance part="U2" gate="G$1" x="50.8" y="38.1" rot="R0"/></instances>
            <nets><net name="VCC"><segment><wire x1="25.4" y1="38.1" x2="50.8" y2="38.1" width="0.15" layer="91"/></segment></net></nets>
          </sheet></sheets>
        </schematic></drawing></eagle>)";
        PathUtils::WriteFileBytes(schPath, eagleSch, std::strlen(eagleSch));
        ParsedEdaBoard sch;
        bool ok = EdaViewer::InspectEdaFile(schPath, sch) &&
                  sch.kind == EdaDocKind::EagleSch &&
                  sch.components.size() == 1 &&
                  sch.components[0].refDes == L"U2" &&
                  !sch.traces.empty();
        DeleteFileW(schPath.c_str());
        tc.Record(ok, L"Stage2.Test4_Eagle_XML_Schematic_Parser");
    }

    // Test 2.5: Gerber RS-274X (.gtl) & Excellon Drill (.drl) parser
    {
        std::wstring gbrPath = testDir + L"\\top_copper.gtl";
        const char* gerber = "%FSLAX24Y24*%\n%MOMM*%\n%ADD10C,0.254*%\n%ADD11C,1.500*%\nD10*\nX100000Y100000D02*\nX250000Y100000D01*\nD11*\nX250000Y100000D03*\nM02*\n";
        PathUtils::WriteFileBytes(gbrPath, gerber, std::strlen(gerber));
        ParsedEdaBoard gbr;
        bool ok1 = EdaViewer::InspectEdaFile(gbrPath, gbr) &&
                   gbr.kind == EdaDocKind::GerberLayer &&
                   gbr.traces.size() == 1 && gbr.pads.size() == 1;
        DeleteFileW(gbrPath.c_str());

        std::wstring drlPath = testDir + L"\\board.drl";
        const char* drill = "M48\nMETRIC\nT01C0.800\n%\nT01\nX10.0Y15.0\nX20.0Y25.0\nM30\n";
        PathUtils::WriteFileBytes(drlPath, drill, std::strlen(drill));
        ParsedEdaBoard drl;
        bool ok2 = EdaViewer::InspectEdaFile(drlPath, drl) &&
                   drl.kind == EdaDocKind::ExcellonDrill &&
                   drl.vias.size() == 2;
        DeleteFileW(drlPath.c_str());
        tc.Record(ok1 && ok2, L"Stage2.Test5_Gerber_RS274X_And_Excellon_Drill");
    }
}

// Stage 3: Expanded 3D Model Viewers — OBJ/STL, Quake II MD2, Quake III MD3, Blender .blend, 3DS & GLB (5 tests)
static void RunStage3Tests(TestCounter& tc) {
    std::wstring testDir = GetScratchTestDir();

    // Test 3.1: Wavefront OBJ (with quad triangulation) & Binary STL
    {
        std::wstring objPath = testDir + L"\\cube.obj";
        const char* objTxt =
            "v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\n"
            "f 1 2 3 4\n";
        PathUtils::WriteFileBytes(objPath, objTxt, std::strlen(objTxt));
        LoadedMesh3D mObj;
        bool okObj = Model3DViewer::InspectMeshFile(objPath, mObj) &&
                     mObj.rawVertexCount == 4 && mObj.rawTriangleCount == 2;
        DeleteFileW(objPath.c_str());
        tc.Record(okObj, L"Stage3.Test1_Wavefront_OBJ_QuadTriangulation");
    }

    // Test 3.2: Quake II .md2 (IDP2 v8 binary model)
    {
        std::wstring md2Path = testDir + L"\\test.md2";
        struct Md2Hdr {
            int32_t ident, version, skinwidth, skinheight, framesize;
            int32_t num_skins, num_xyz, num_st, num_tris, num_glcmds, num_frames;
            int32_t ofs_skins, ofs_st, ofs_tris, ofs_frames, ofs_glcmds, ofs_end;
        } hdr = {};
        std::memcpy(&hdr.ident, "IDP2", 4);
        hdr.version = 8;
        hdr.num_xyz = 3;
        hdr.num_tris = 1;
        hdr.num_frames = 2;
        hdr.framesize = 40 + 3 * 4;
        hdr.ofs_frames = 68;
        hdr.ofs_tris = 68 + hdr.framesize;
        hdr.ofs_end = hdr.ofs_tris + 12;

        std::vector<uint8_t> buf(hdr.ofs_end, 0);
        std::memcpy(buf.data(), &hdr, 68);
        float scale[3] = { 1.0f, 1.0f, 1.0f };
        std::memcpy(buf.data() + 68, scale, 12);
        // 3 vertices at offset 68 + 40 = 108
        uint8_t v0[4] = { 10, 10, 10, 0 };
        uint8_t v1[4] = { 50, 10, 10, 0 };
        uint8_t v2[4] = { 10, 50, 10, 0 };
        std::memcpy(buf.data() + 108, v0, 4);
        std::memcpy(buf.data() + 112, v1, 4);
        std::memcpy(buf.data() + 116, v2, 4);
        uint16_t tri[6] = { 0, 1, 2, 0, 0, 0 };
        std::memcpy(buf.data() + hdr.ofs_tris, tri, 12);

        PathUtils::WriteFileBytes(md2Path, buf.data(), buf.size());
        LoadedMesh3D mMd2;
        bool ok = Model3DViewer::InspectMeshFile(md2Path, mMd2) &&
                  mMd2.rawVertexCount == 3 && mMd2.rawTriangleCount == 1 && mMd2.animFrameCount == 2;
        DeleteFileW(md2Path.c_str());
        tc.Record(ok, L"Stage3.Test2_Quake2_MD2_BinaryParser");
    }

    // Test 3.3: Quake III .md3 (IDP3 v15 multi-surface binary model)
    {
        std::wstring md3Path = testDir + L"\\test.md3";
        std::vector<uint8_t> buf(108 + 108 + 12 + 24, 0);
        // Main header
        std::memcpy(buf.data(), "IDP3", 4);
        int32_t ver = 15, nFrames = 1, nSurf = 1, ofsSurf = 108;
        std::memcpy(buf.data() + 4, &ver, 4);
        std::memcpy(buf.data() + 72, &nFrames, 4);
        std::memcpy(buf.data() + 80, &nSurf, 4);
        std::memcpy(buf.data() + 96, &ofsSurf, 4);
        // Surface header @ 108
        uint8_t* sp = buf.data() + 108;
        std::memcpy(sp, "IDP3", 4);
        int32_t sVerts = 3, sTris = 1, ofsTris = 108, ofsXyz = 120, ofsEnd = 144;
        std::memcpy(sp + 76, &sVerts, 4);
        std::memcpy(sp + 80, &sTris, 4);
        std::memcpy(sp + 84, &ofsTris, 4);
        std::memcpy(sp + 100, &ofsXyz, 4);
        std::memcpy(sp + 104, &ofsEnd, 4);
        int32_t triIdx[3] = { 0, 1, 2 };
        std::memcpy(sp + ofsTris, triIdx, 12);
        int16_t xyz[12] = { 0,0,0,0,  640,0,0,0,  0,640,0,0 };
        std::memcpy(sp + ofsXyz, xyz, 24);

        PathUtils::WriteFileBytes(md3Path, buf.data(), buf.size());
        LoadedMesh3D mMd3;
        bool ok = Model3DViewer::InspectMeshFile(md3Path, mMd3) &&
                  mMd3.rawVertexCount == 3 && mMd3.rawTriangleCount == 1;
        DeleteFileW(md3Path.c_str());
        tc.Record(ok, L"Stage3.Test3_Quake3_MD3_SurfaceParser");
    }

    // Test 3.4: Blender .blend (BLENDER-v300 BHead ME + DATA float3 vertex block)
    {
        std::wstring blendPath = testDir + L"\\test.blend";
        std::vector<uint8_t> buf;
        const char* hdr = "BLENDER-v305";
        buf.insert(buf.end(), hdr, hdr + 12);
        auto appendBHead32 = [&](const char code[4], int32_t len, int32_t nr, const void* payload) {
            uint8_t bh[20] = {};
            std::memcpy(bh, code, 4);
            std::memcpy(bh + 4, &len, 4);
            std::memcpy(bh + 16, &nr, 4);
            buf.insert(buf.end(), bh, bh + 20);
            if (len > 0 && payload) {
                const uint8_t* p = static_cast<const uint8_t*>(payload);
                buf.insert(buf.end(), p, p + len);
            }
        };
        uint8_t dummyMe[8] = {};
        appendBHead32("ME\0\0", 8, 1, dummyMe);
        float verts[9] = { 0.0f, 0.0f, 0.0f,  2.0f, 0.0f, 0.0f,  0.0f, 2.0f, 1.0f };
        appendBHead32("DATA", sizeof(verts), 3, verts);
        appendBHead32("ENDB", 0, 0, nullptr);

        PathUtils::WriteFileBytes(blendPath, buf.data(), buf.size());
        LoadedMesh3D mBlend;
        bool ok = Model3DViewer::InspectMeshFile(blendPath, mBlend) &&
                  mBlend.rawVertexCount == 3 && mBlend.rawTriangleCount == 1;
        DeleteFileW(blendPath.c_str());
        tc.Record(ok, L"Stage3.Test4_Blender_Blend_SDNA_MeshExtractor");
    }

    // Test 3.5: Autodesk .3ds (0x4D4D chunk) & Binary glTF 2.0 (.glb)
    {
        std::wstring p3ds = testDir + L"\\test.3ds";
        std::vector<uint8_t> b3ds;
        auto pushU16 = [&](uint16_t v) { b3ds.push_back(v & 0xFF); b3ds.push_back((v >> 8) & 0xFF); };
        auto pushU32 = [&](uint32_t v) {
            b3ds.push_back(v & 0xFF); b3ds.push_back((v >> 8) & 0xFF);
            b3ds.push_back((v >> 16) & 0xFF); b3ds.push_back((v >> 24) & 0xFF);
        };
        // 0x4110 (verts: 6 + 2 + 36 = 44 bytes), 0x4120 (faces: 6 + 2 + 8 = 16 bytes)
        // 0x4100 (6 + 44 + 16 = 66 bytes), 0x4000 (6 + 2 ("A\0") + 66 = 74 bytes), 0x3D3D (6 + 74 = 80 bytes), 0x4D4D (6 + 80 = 86 bytes)
        pushU16(0x4D4D); pushU32(86);
        pushU16(0x3D3D); pushU32(80);
        pushU16(0x4000); pushU32(74); b3ds.push_back('A'); b3ds.push_back(0);
        pushU16(0x4100); pushU32(66);
        pushU16(0x4110); pushU32(44); pushU16(3);
        float v3[9] = { 0,0,0, 5,0,0, 0,5,0 };
        b3ds.insert(b3ds.end(), reinterpret_cast<uint8_t*>(v3), reinterpret_cast<uint8_t*>(v3) + 36);
        pushU16(0x4120); pushU32(16); pushU16(1);
        pushU16(0); pushU16(1); pushU16(2); pushU16(0);

        PathUtils::WriteFileBytes(p3ds, b3ds.data(), b3ds.size());
        LoadedMesh3D m3ds;
        bool ok = Model3DViewer::InspectMeshFile(p3ds, m3ds) &&
                  m3ds.rawVertexCount == 3 && m3ds.rawTriangleCount == 1;
        DeleteFileW(p3ds.c_str());
        tc.Record(ok, L"Stage3.Test5_3DS_ChunkMeshParser");
    }
}

// Stage 4: Pro Code Editor ("Notepad++ Style") — Language Detection & Multi-Language Syntax Tokenizers (5 tests)
static void RunStage4Tests(TestCounter& tc) {
    // Test 4.1: Automatic Syntax Language Detection by extension & shebang/header
    {
        bool ok = true;
        ok = ok && (NotepadViewer::DetectSyntaxLanguage(L".cpp") == SyntaxLang::Cpp);
        ok = ok && (NotepadViewer::DetectSyntaxLanguage(L".py") == SyntaxLang::Python);
        ok = ok && (NotepadViewer::DetectSyntaxLanguage(L".json") == SyntaxLang::Json);
        ok = ok && (NotepadViewer::DetectSyntaxLanguage(L".sql") == SyntaxLang::Sql);
        ok = ok && (NotepadViewer::DetectSyntaxLanguage(L"", L"#!/usr/bin/env python3") == SyntaxLang::Python);
        ok = ok && (NotepadViewer::DetectSyntaxLanguage(L"", L"<?xml version=\"1.0\"?>") == SyntaxLang::XmlHtml);
        tc.Record(ok, L"Stage4.Test1_NotepadPP_SyntaxLangDetection");
    }

    // Test 4.2: C++ Preprocessor, Keyword, Type, String, Number & Comment Tokenization
    {
        bool inBlk = false;
        std::vector<SyntaxToken> toks;
        NotepadViewer::TokenizeLine(L"#include <vector>", SyntaxLang::Cpp, inBlk, toks);
        bool ok1 = !toks.empty() && toks[0].kind == SyntaxTokenKind::Preprocessor;

        NotepadViewer::TokenizeLine(L"static uint32_t count = 0x100; // comment", SyntaxLang::Cpp, inBlk, toks);
        bool hasKw = false, hasType = false, hasNum = false, hasCmt = false;
        for (const auto& t : toks) {
            if (t.kind == SyntaxTokenKind::Keyword) hasKw = true;
            if (t.kind == SyntaxTokenKind::Type) hasType = true;
            if (t.kind == SyntaxTokenKind::Number) hasNum = true;
            if (t.kind == SyntaxTokenKind::Comment) hasCmt = true;
        }
        tc.Record(ok1 && hasKw && hasType && hasNum && hasCmt, L"Stage4.Test2_Cpp_Lexer_Tokens");
    }

    // Test 4.3: Multi-line /* ... */ Block Comment State Machine across lines
    {
        bool inBlk = false;
        std::vector<SyntaxToken> toks1, toks2;
        NotepadViewer::TokenizeLine(L"int x = 1; /* start block", SyntaxLang::Cpp, inBlk, toks1);
        bool okStart = inBlk;
        NotepadViewer::TokenizeLine(L"still in comment */ return x;", SyntaxLang::Cpp, inBlk, toks2);
        bool okEnd = !inBlk && toks2.size() >= 2 &&
                     toks2.front().kind == SyntaxTokenKind::Comment;
        tc.Record(okStart && okEnd, L"Stage4.Test3_MultilineBlockComment_StateMachine");
    }

    // Test 4.4: JSON Key vs String & Numeric Value Tokenization
    {
        bool inBlk = false;
        std::vector<SyntaxToken> toks;
        NotepadViewer::TokenizeLine(L"  \"version\": \"2.0\", \"port\": 8080", SyntaxLang::Json, inBlk, toks);
        bool hasKey = false, hasStr = false, hasNum = false;
        for (const auto& t : toks) {
            if (t.kind == SyntaxTokenKind::TagOrKey) hasKey = true;
            if (t.kind == SyntaxTokenKind::String) hasStr = true;
            if (t.kind == SyntaxTokenKind::Number) hasNum = true;
        }
        tc.Record(hasKey && hasStr && hasNum, L"Stage4.Test4_Json_KeyValue_SyntaxHighlighter");
    }

    // Test 4.5: Python, SQL, and INI/YAML Syntax Tokenization
    {
        bool inBlk = false;
        std::vector<SyntaxToken> pyToks, sqlToks, iniToks;
        NotepadViewer::TokenizeLine(L"async def fetch_data(url: str): # py", SyntaxLang::Python, inBlk, pyToks);
        NotepadViewer::TokenizeLine(L"SELECT id, name FROM users WHERE active = 1 -- sql", SyntaxLang::Sql, inBlk, sqlToks);
        NotepadViewer::TokenizeLine(L"[Database]", SyntaxLang::IniYaml, inBlk, iniToks);
        bool ok = !pyToks.empty() && pyToks.front().kind == SyntaxTokenKind::Keyword &&
                  !sqlToks.empty() && sqlToks.front().kind == SyntaxTokenKind::Keyword &&
                  !iniToks.empty() && iniToks.front().kind == SyntaxTokenKind::Preprocessor;
        tc.Record(ok, L"Stage4.Test5_Python_SQL_INI_SyntaxHighlighter");
    }
}

// ============================================================================
// STAGE 5 TESTS: Spreadsheet (.xlsx, .csv, .tsv) & Document (.docx, .rtf)
// ============================================================================
static std::vector<uint8_t> BuildMultiFileStoredZip(const std::vector<std::pair<std::string, std::string>>& entries) {
    std::vector<uint8_t> zipBytes;
    auto push16 = [&](uint16_t v) { zipBytes.push_back(v & 0xFF); zipBytes.push_back((v >> 8) & 0xFF); };
    auto push32 = [&](uint32_t v) {
        zipBytes.push_back(v & 0xFF); zipBytes.push_back((v >> 8) & 0xFF);
        zipBytes.push_back((v >> 16) & 0xFF); zipBytes.push_back((v >> 24) & 0xFF);
    };

    std::vector<uint32_t> localOffsets;
    for (const auto& kv : entries) {
        localOffsets.push_back(static_cast<uint32_t>(zipBytes.size()));
        uint16_t fnLen = static_cast<uint16_t>(kv.first.size());
        uint32_t dataLen = static_cast<uint32_t>(kv.second.size());
        push32(0x04034B50);
        push16(20); push16(0); push16(0); push16(0); push16(0);
        push32(0); push32(dataLen); push32(dataLen);
        push16(fnLen); push16(0);
        zipBytes.insert(zipBytes.end(), kv.first.begin(), kv.first.end());
        zipBytes.insert(zipBytes.end(), kv.second.begin(), kv.second.end());
    }

    uint32_t cdOffset = static_cast<uint32_t>(zipBytes.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& kv = entries[i];
        uint16_t fnLen = static_cast<uint16_t>(kv.first.size());
        uint32_t dataLen = static_cast<uint32_t>(kv.second.size());
        push32(0x02014B50);
        push16(20); push16(20); push16(0); push16(0); push16(0); push16(0);
        push32(0); push32(dataLen); push32(dataLen);
        push16(fnLen); push16(0); push16(0); push16(0); push16(0);
        push32(0); push32(localOffsets[i]);
        zipBytes.insert(zipBytes.end(), kv.first.begin(), kv.first.end());
    }
    uint32_t cdSize = static_cast<uint32_t>(zipBytes.size()) - cdOffset;

    push32(0x06054B50);
    push16(0); push16(0);
    push16(static_cast<uint16_t>(entries.size()));
    push16(static_cast<uint16_t>(entries.size()));
    push32(cdSize); push32(cdOffset); push16(0);
    return zipBytes;
}

void RunStage5Tests(TestCounter& tc) {
    wchar_t tmpDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmpDir);

    // Test 5.1: RFC 4180 CSV with Quoted Commas, Escaped Quotes, and Numeric Cells
    {
        std::wstring csvPath = std::wstring(tmpDir) + L"e2_stage5_test1.csv";
        const char* csvData =
            "ID,PartName,Price,Qty\r\n"
            "1,\"Resistor, 10k\",0.05,100\r\n"
            "2,\"IC \"\"MCU\"\" STM32\",4.50,20\r\n"
            "3,Capacitor 100nF,0.02,500\r\n";
        PathUtils::WriteFileBytes(csvPath, csvData, std::strlen(csvData));

        ParsedOfficeDocument doc;
        bool ok = SpreadsheetDocViewer::InspectOfficeFile(csvPath, doc) &&
                  doc.valid && doc.isSpreadsheet && doc.sheets.size() == 1 &&
                  doc.sheets[0].rows.size() == 4 && doc.sheets[0].maxCols == 4 &&
                  doc.sheets[0].rows[0][0] == L"ID" &&
                  doc.sheets[0].rows[1][1] == L"Resistor, 10k" &&
                  doc.sheets[0].rows[2][1] == L"IC \"MCU\" STM32";
        DeleteFileW(csvPath.c_str());
        tc.Record(ok, L"Stage5.Test1_CSV_CommaAndQuotes",
            L"rows=" + std::to_wstring(doc.sheets.empty() ? 0 : doc.sheets[0].rows.size()) +
            L", cols=" + std::to_wstring(doc.sheets.empty() ? 0 : doc.sheets[0].maxCols));
    }

    // Test 5.2: European Semicolon CSV (;) and Tab-Separated TSV (\t) Auto-Detection
    {
        std::wstring csvSemiPath = std::wstring(tmpDir) + L"e2_stage5_semi.csv";
        const char* semiData = "Kod;Nazwa;Wartosc\r\nA01;Rezystor;12,50\r\nA02;Kondensator;3,75\r\n";
        PathUtils::WriteFileBytes(csvSemiPath, semiData, std::strlen(semiData));

        std::wstring tsvPath = std::wstring(tmpDir) + L"e2_stage5_tab.tsv";
        const char* tsvData = "ColA\tColB\tColC\tColD\n10\t20\t30\t40\n";
        PathUtils::WriteFileBytes(tsvPath, tsvData, std::strlen(tsvData));

        ParsedOfficeDocument docSemi, docTsv;
        bool ok1 = SpreadsheetDocViewer::InspectOfficeFile(csvSemiPath, docSemi) &&
                   docSemi.valid && docSemi.sheets.size() == 1 &&
                   docSemi.sheets[0].rows.size() == 3 && docSemi.sheets[0].maxCols == 3 &&
                   docSemi.sheets[0].rows[0][0] == L"Kod";
        bool ok2 = SpreadsheetDocViewer::InspectOfficeFile(tsvPath, docTsv) &&
                   docTsv.valid && docTsv.sheets.size() == 1 &&
                   docTsv.sheets[0].rows.size() == 2 && docTsv.sheets[0].maxCols == 4 &&
                   docTsv.sheets[0].rows[0][0] == L"ColA";
        DeleteFileW(csvSemiPath.c_str());
        DeleteFileW(tsvPath.c_str());
        tc.Record(ok1 && ok2, L"Stage5.Test2_CSV_SemicolonAndTSV_AutoDetect");
    }

    // Test 5.3: Excel .xlsx Multi-Sheet + sharedStrings.xml + Cell Coordinate Decoder
    {
        std::string wbXml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<workbook><sheets>"
            "<sheet name=\"Sales\" sheetId=\"1\" r:id=\"rId1\"/>"
            "<sheet name=\"Inventory\" sheetId=\"2\" r:id=\"rId2\"/>"
            "</sheets></workbook>";
        std::string sstXml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<sst count=\"3\" uniqueCount=\"3\">"
            "<si><t>Product</t></si>"
            "<si><t>Revenue</t></si>"
            "<si><t>Explorer2 Pro</t></si>"
            "</sst>";
        std::string sheet1Xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<worksheet><sheetData>"
            "<row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>1</v></c></row>"
            "<row r=\"2\"><c r=\"A2\" t=\"s\"><v>2</v></c><c r=\"C2\"><v>1999.95</v></c></row>"
            "</sheetData></worksheet>";
        std::string sheet2Xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<worksheet><sheetData>"
            "<row r=\"1\"><c r=\"A1\" t=\"inlineStr\"><is><t>Stock</t></is></c><c r=\"B1\"><v>42</v></c></row>"
            "</sheetData></worksheet>";

        std::vector<uint8_t> zipBytes = BuildMultiFileStoredZip({
            { "xl/workbook.xml", wbXml },
            { "xl/sharedStrings.xml", sstXml },
            { "xl/worksheets/sheet1.xml", sheet1Xml },
            { "xl/worksheets/sheet2.xml", sheet2Xml }
        });
        std::wstring xlsxPath = std::wstring(tmpDir) + L"e2_stage5_test3.xlsx";
        PathUtils::WriteFileBytes(xlsxPath, zipBytes.data(), zipBytes.size());

        ParsedOfficeDocument doc;
        bool ok = SpreadsheetDocViewer::InspectOfficeFile(xlsxPath, doc) &&
                  doc.valid && doc.isSpreadsheet && doc.sheets.size() == 2 &&
                  doc.sheets[0].name == L"Sales" &&
                  doc.sheets[0].rows.size() == 2 && doc.sheets[0].maxCols == 3 &&
                  doc.sheets[0].rows[0][0] == L"Product" &&
                  doc.sheets[0].rows[1][0] == L"Explorer2 Pro" &&
                  doc.sheets[0].rows[1][2] == L"1999.95";
        DeleteFileW(xlsxPath.c_str());
        tc.Record(ok, L"Stage5.Test3_XLSX_SharedStrings_And_MultiSheet",
            L"sheets=" + std::to_wstring(doc.sheets.size()));
    }

    // Test 5.4: Word .docx word/document.xml Headings, Paragraphs, Tables & Word Count
    {
        std::string docXml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<w:document><w:body>"
            "<w:p><w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr><w:r><w:t>Architecture Overview</w:t></w:r></w:p>"
            "<w:p><w:r><w:t>Explorer2 includes a fast native Word document reader.</w:t></w:r></w:p>"
            "<w:tbl><w:tr>"
            "<w:tc><w:p><w:r><w:t>Module</w:t></w:r></w:p></w:tc>"
            "<w:tc><w:p><w:r><w:t>Status</w:t></w:r></w:p></w:tc>"
            "</w:tr></w:tbl>"
            "</w:body></w:document>";
        std::vector<uint8_t> zipBytes = BuildMultiFileStoredZip({
            { "word/document.xml", docXml }
        });
        std::wstring docxPath = std::wstring(tmpDir) + L"e2_stage5_test4.docx";
        PathUtils::WriteFileBytes(docxPath, zipBytes.data(), zipBytes.size());

        ParsedOfficeDocument doc;
        bool ok = SpreadsheetDocViewer::InspectOfficeFile(docxPath, doc) &&
                  doc.valid && !doc.isSpreadsheet && doc.paragraphs.size() >= 2 &&
                  doc.wordCount >= 10 &&
                  doc.paragraphs[0].headingLevel == 1 &&
                  doc.paragraphs[0].text == L"Architecture Overview";
        DeleteFileW(docxPath.c_str());
        tc.Record(ok, L"Stage5.Test4_DOCX_Paragraphs_Headings_WordCount",
            L"paras=" + std::to_wstring(doc.paragraphs.size()) + L", words=" + std::to_wstring(doc.wordCount));
    }

    // Test 5.5: Rich Text Format (.rtf) Control Word & Group Stripping
    {
        std::wstring rtfPath = std::wstring(tmpDir) + L"e2_stage5_test5.rtf";
        const char* rtfData =
            "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}"
            "\\pard\\b Welcome to Explorer2 RTF Viewer\\b0\\par "
            "Second paragraph with \\i italic\\i0  and \\'41SCII hex escape.\\par}";
        PathUtils::WriteFileBytes(rtfPath, rtfData, std::strlen(rtfData));

        ParsedOfficeDocument doc;
        bool ok = SpreadsheetDocViewer::InspectOfficeFile(rtfPath, doc) &&
                  doc.valid && !doc.isSpreadsheet && doc.paragraphs.size() == 2 &&
                  doc.wordCount >= 10;
        DeleteFileW(rtfPath.c_str());
        tc.Record(ok, L"Stage5.Test5_RTF_ControlWordStripping",
            L"paras=" + std::to_wstring(doc.paragraphs.size()) + L", words=" + std::to_wstring(doc.wordCount));
    }
}

// ============================================================================
// STAGE 6 TESTS: Advanced Raster Image Formats (.tga, .psd, .xcf, .hdr, .ppm, .pcx, .qoi)
// ============================================================================
void RunStage6Tests(TestCounter& tc) {
    // Test 6.1: Truevision TGA Uncompressed 24-bit & RLE 32-bit Decoder
    {
        // 2x2 RLE 32-bit top-to-bottom TGA: run of 4 pixels with BGRA = (10, 20, 200, 255)
        std::vector<uint8_t> tga(18, 0);
        tga[2]  = 10; // RLE True-Color
        tga[12] = 2;  // width = 2
        tga[14] = 2;  // height = 2
        tga[16] = 32; // 32 bpp
        tga[17] = 0x20; // top-to-bottom
        tga.push_back(0x83); // RLE packet: 4 pixels
        tga.push_back(10);   // B
        tga.push_back(20);   // G
        tga.push_back(200);  // R
        tga.push_back(255);  // A

        ExtendedImageInfo info;
        bool ok = ImageFormatLoader::DecodeImageMemory(tga.data(), tga.size(), L".tga", 0, info) &&
                  info.valid && info.width == 2 && info.height == 2 &&
                  info.bgraPixels.size() == 16 &&
                  info.bgraPixels[0] == 10 && info.bgraPixels[1] == 20 && info.bgraPixels[2] == 200 && info.bgraPixels[3] == 255;
        tc.Record(ok, L"Stage6.Test1_TGA_Uncompressed_And_RLE");
    }

    // Test 6.2: Adobe Photoshop PSD (8BPS) Layer Metadata + PackBits RLE Planar Composite
    {
        std::vector<uint8_t> psd;
        auto push16BE = [&](uint16_t v) { psd.push_back((v >> 8) & 0xFF); psd.push_back(v & 0xFF); };
        auto push32BE = [&](uint32_t v) {
            psd.push_back((v >> 24) & 0xFF); psd.push_back((v >> 16) & 0xFF);
            psd.push_back((v >> 8) & 0xFF);  psd.push_back(v & 0xFF);
        };
        // Header (26 bytes): "8BPS", ver=1, 6 zero bytes, channels=3, height=2, width=2, depth=8, mode=3 (RGB)
        psd.insert(psd.end(), { '8', 'B', 'P', 'S' });
        push16BE(1);
        for (int i = 0; i < 6; ++i) psd.push_back(0);
        push16BE(3);
        push32BE(2);
        push32BE(2);
        push16BE(8);
        push16BE(3);
        // Color Mode Data (0) & Image Resources (0)
        push32BE(0);
        push32BE(0);
        // Layer and Mask Info (with 1 layer named "HeroLayer")
        std::vector<uint8_t> lrec;
        auto l16 = [&](uint16_t v) { lrec.push_back((v >> 8) & 0xFF); lrec.push_back(v & 0xFF); };
        auto l32 = [&](uint32_t v) {
            lrec.push_back((v >> 24) & 0xFF); lrec.push_back((v >> 16) & 0xFF);
            lrec.push_back((v >> 8) & 0xFF);  lrec.push_back(v & 0xFF);
        };
        l16(1); // 1 layer
        l32(0); l32(0); l32(2); l32(2); // top, left, bottom, right
        l16(1); // 1 channel in layer record
        l16(0); l32(2); // channel id=0, len=2
        lrec.insert(lrec.end(), { '8', 'B', 'I', 'M', 'n', 'o', 'r', 'm', 255, 0, 0, 0 });
        // extraData: maskLen(4)=0, blendLen(4)=0, Pascal string "\x09HeroLayer" + 2 pad
        l32(20);
        l32(0); l32(0);
        lrec.push_back(9);
        const char* ln = "HeroLayer";
        lrec.insert(lrec.end(), ln, ln + 9);
        lrec.push_back(0); lrec.push_back(0);

        push32BE(static_cast<uint32_t>(lrec.size() + 4));
        push32BE(static_cast<uint32_t>(lrec.size()));
        psd.insert(psd.end(), lrec.begin(), lrec.end());

        // Composite Image Data: compression = 1 (PackBits RLE), 3 channels * 2 rows = 6 scanlines
        // Each scanline is a 2-byte PackBits run: [-1 (0xFF), value] = 2 pixels of value
        push16BE(1);
        for (int i = 0; i < 6; ++i) push16BE(2);
        // R rows (220), G rows (110), B rows (55)
        psd.insert(psd.end(), { 0xFF, 220, 0xFF, 220, 0xFF, 110, 0xFF, 110, 0xFF, 55, 0xFF, 55 });

        ExtendedImageInfo info;
        bool ok = ImageFormatLoader::DecodeImageMemory(psd.data(), psd.size(), L".psd", 0, info) &&
                  info.valid && info.width == 2 && info.height == 2 &&
                  info.layerNames.size() == 1 && info.layerNames[0] == L"HeroLayer" &&
                  info.bgraPixels[0] == 55 && info.bgraPixels[1] == 110 && info.bgraPixels[2] == 220;
        tc.Record(ok, L"Stage6.Test2_PSD_Layers_And_PackBits_Composite",
            info.layerNames.empty() ? L"" : info.layerNames[0]);
    }

    // Test 6.3: GIMP XCF v003 Canvas + Layer Hierarchy + 64x64 RLE Tile Compositor
    {
        std::vector<uint8_t> xcf;
        auto push32BE = [&](uint32_t v) {
            xcf.push_back((v >> 24) & 0xFF); xcf.push_back((v >> 16) & 0xFF);
            xcf.push_back((v >> 8) & 0xFF);  xcf.push_back(v & 0xFF);
        };
        const char* sig = "gimp xcf v003";
        xcf.insert(xcf.end(), sig, sig + 14); // 14 bytes including null
        push32BE(2); // canvasW = 2
        push32BE(2); // canvasH = 2
        push32BE(0); // baseType = RGB
        // Image properties: PROP_END (0, 0)
        push32BE(0); push32BE(0);
        // Layer pointer table: 1 layer at offset 46, followed by 0 terminator
        uint32_t layerOff = static_cast<uint32_t>(xcf.size() + 8);
        push32BE(layerOff);
        push32BE(0);

        // Layer structure @ layerOff: w=2, h=2, type=0 (RGB), name="Base\0", PROP_END, hierOff
        push32BE(2); push32BE(2); push32BE(0);
        push32BE(5); xcf.insert(xcf.end(), { 'B', 'a', 's', 'e', 0 });
        push32BE(0); push32BE(0); // PROP_END
        uint32_t hierOff = static_cast<uint32_t>(xcf.size() + 8);
        push32BE(hierOff);
        push32BE(0); // mask_offset = 0

        // Hierarchy @ hierOff: w=2, h=2, bpp=3, levelOff, 0
        push32BE(2); push32BE(2); push32BE(3);
        uint32_t levelOff = static_cast<uint32_t>(xcf.size() + 8);
        push32BE(levelOff);
        push32BE(0);

        // Level @ levelOff: w=2, h=2, tile0Off, 0
        push32BE(2); push32BE(2);
        uint32_t tileOff = static_cast<uint32_t>(xcf.size() + 8);
        push32BE(tileOff);
        push32BE(0);

        // RLE Tile @ tileOff for 4 pixels * 3 channels: each channel is [run of 4 (n=3), value]
        xcf.insert(xcf.end(), { 3, 240, 3, 128, 3, 64 });

        ExtendedImageInfo info;
        bool ok = ImageFormatLoader::DecodeImageMemory(xcf.data(), xcf.size(), L".xcf", 0, info) &&
                  info.valid && info.width == 2 && info.height == 2 &&
                  info.layerNames.size() == 1 && info.layerNames[0] == L"Base" &&
                  info.bgraPixels[0] == 64 && info.bgraPixels[1] == 128 && info.bgraPixels[2] == 240;
        tc.Record(ok, L"Stage6.Test3_GIMP_XCF_Canvas_Layer_TileRLE");
    }

    // Test 6.4: Radiance HDR (RGBE Tone-Mapped) & Netpbm PPM (P3 ASCII & P6 Binary)
    {
        std::string hdr = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
        hdr.push_back(static_cast<char>(200));
        hdr.push_back(static_cast<char>(100));
        hdr.push_back(static_cast<char>(50));
        hdr.push_back(static_cast<char>(129));
        hdr.push_back(static_cast<char>(100));
        hdr.push_back(static_cast<char>(200));
        hdr.push_back(static_cast<char>(50));
        hdr.push_back(static_cast<char>(128));

        ExtendedImageInfo hdrInfo;
        bool okHdr = ImageFormatLoader::DecodeImageMemory(
            reinterpret_cast<const uint8_t*>(hdr.data()), hdr.size(), L".hdr", 0, hdrInfo) &&
            hdrInfo.valid && hdrInfo.width == 2 && hdrInfo.height == 1 && hdrInfo.bgraPixels[2] > hdrInfo.bgraPixels[0];

        const char* ppm = "P3\n# comment\n2 1\n255\n255 0 128  0 255 64\n";
        ExtendedImageInfo ppmInfo;
        bool okPpm = ImageFormatLoader::DecodeImageMemory(
            reinterpret_cast<const uint8_t*>(ppm), std::strlen(ppm), L".ppm", 0, ppmInfo) &&
            ppmInfo.valid && ppmInfo.width == 2 && ppmInfo.height == 1 &&
            ppmInfo.bgraPixels[0] == 128 && ppmInfo.bgraPixels[1] == 0 && ppmInfo.bgraPixels[2] == 255;

        tc.Record(okHdr && okPpm, L"Stage6.Test4_Radiance_HDR_And_Netpbm_PPM");
    }

    // Test 6.5: ZSoft PCX (RLE + 256-Color VGA Palette) & Quite OK Image (.qoi)
    {
        std::vector<uint8_t> qoi = {
            'q', 'o', 'i', 'f',
            0, 0, 0, 2, // width = 2
            0, 0, 0, 1, // height = 1
            4, 0,       // channels = 4, sRGB
            0xFE, 30, 144, 255, // QOI_OP_RGB(30, 144, 255)
            0xC0,               // QOI_OP_RUN(1 more pixel)
            0, 0, 0, 0, 0, 0, 0, 1
        };
        ExtendedImageInfo qoiInfo;
        bool okQoi = ImageFormatLoader::DecodeImageMemory(qoi.data(), qoi.size(), L".qoi", 0, qoiInfo) &&
                     qoiInfo.valid && qoiInfo.width == 2 && qoiInfo.height == 1 &&
                     qoiInfo.bgraPixels[0] == 255 && qoiInfo.bgraPixels[1] == 144 && qoiInfo.bgraPixels[2] == 30 &&
                     qoiInfo.bgraPixels[4] == 255 && qoiInfo.bgraPixels[5] == 144 && qoiInfo.bgraPixels[6] == 30;

        // Minimal 2x1 8-bit 1-plane PCX with 256-color palette at end
        std::vector<uint8_t> pcx(128, 0);
        pcx[0] = 0x0A; pcx[1] = 5; pcx[2] = 1; pcx[3] = 8; // 8-bit RLE
        pcx[8] = 1; pcx[10] = 0; // xmax=1, ymax=0 -> 2x1
        pcx[65] = 1; pcx[66] = 2; // 1 plane, bytesPerLine=2
        pcx.push_back(0xC2); pcx.push_back(1); // RLE: 2 pixels of palette index 1
        pcx.push_back(0x0C); // VGA palette marker
        for (int i = 0; i < 256; ++i) {
            pcx.push_back(static_cast<uint8_t>(i == 1 ? 99 : 0));  // R
            pcx.push_back(static_cast<uint8_t>(i == 1 ? 188 : 0)); // G
            pcx.push_back(static_cast<uint8_t>(i == 1 ? 77 : 0));  // B
        }
        ExtendedImageInfo pcxInfo;
        bool okPcx = ImageFormatLoader::DecodeImageMemory(pcx.data(), pcx.size(), L".pcx", 0, pcxInfo) &&
                     pcxInfo.valid && pcxInfo.width == 2 && pcxInfo.height == 1 &&
                     pcxInfo.bgraPixels[0] == 77 && pcxInfo.bgraPixels[1] == 188 && pcxInfo.bgraPixels[2] == 99;

        tc.Record(okQoi && okPcx, L"Stage6.Test5_ZSoft_PCX_And_QOI_Decoder");
    }
}

// ============================================================================
// STAGE 7 TESTS: Archive Viewer & Virtual Sub-Explorer (.zip, .rar, .7z, .tar, .gz)
// ============================================================================
void RunStage7Tests(TestCounter& tc) {
    wchar_t tmpDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmpDir);

    // Test 7.1: ZIP Hierarchical Sub-Explorer + Virtual Folder Enumeration in FileEngine
    {
        std::vector<uint8_t> zipBytes = BuildMultiFileStoredZip({
            { "src/core/engine.cpp", "int main() { return 0; }" },
            { "src/main.cpp", "// entry point" },
            { "README.md", "# Explorer2 Archive Test" }
        });
        std::wstring zipPath = std::wstring(tmpDir) + L"e2_stage7_test1.zip";
        PathUtils::WriteFileBytes(zipPath, zipBytes.data(), zipBytes.size());

        std::wstring arcOut, innerOut;
        bool okSplit = ArchiveExplorer::SplitVirtualArchivePath(zipPath + L"\\src\\core", arcOut, innerOut) &&
                       arcOut == zipPath && innerOut == L"src/core";

        std::vector<ArchiveItemEntry> rootItems, srcItems;
        bool okRoot = ArchiveExplorer::EnumerateArchiveFolder(zipPath, L"", rootItems) &&
                      rootItems.size() == 2 && rootItems[0].isDirectory && rootItems[0].name == L"src" &&
                      !rootItems[1].isDirectory && rootItems[1].name == L"README.md";
        bool okSub = ArchiveExplorer::EnumerateArchiveFolder(zipPath, L"src", srcItems) &&
                     srcItems.size() == 2 && srcItems[0].isDirectory && srcItems[0].name == L"core" &&
                     !srcItems[1].isDirectory && srcItems[1].name == L"main.cpp";

        FileEngine fe;
        std::vector<FileEntry> feItems = fe.EnumerateDirectory(zipPath + L"\\src", true, true, Lang::PL);
        bool okFe = (feItems.size() == 2 && feItems[0].isDirectory && feItems[0].name == L"core" &&
                     !feItems[1].isDirectory && feItems[1].name == L"main.cpp");

        DeleteFileW(zipPath.c_str());
        tc.Record(okSplit && okRoot && okSub && okFe, L"Stage7.Test1_ZIP_SubExplorer_And_VirtualPath");
    }

    // Test 7.2: RAR 4.x Binary Block Header Parser & Store Extraction
    {
        std::vector<uint8_t> rar4 = { 'R', 'a', 'r', '!', 0x1A, 0x07, 0x00 };
        auto push16 = [&](uint16_t v) { rar4.push_back(v & 0xFF); rar4.push_back((v >> 8) & 0xFF); };
        auto push32 = [&](uint32_t v) {
            rar4.push_back(v & 0xFF); rar4.push_back((v >> 8) & 0xFF);
            rar4.push_back((v >> 16) & 0xFF); rar4.push_back((v >> 24) & 0xFF);
        };
        const char* fn = "docs/manual.txt";
        const char* payload = "RAR4_PAYLOAD_OK";
        uint16_t fnLen = static_cast<uint16_t>(std::strlen(fn));
        uint32_t dataLen = static_cast<uint32_t>(std::strlen(payload));
        uint16_t headSize = static_cast<uint16_t>(32 + fnLen);

        push16(0);        // HEAD_CRC
        rar4.push_back(0x74); // HEAD_TYPE = File Header
        push16(0x8000);   // HEAD_FLAGS
        push16(headSize); // HEAD_SIZE
        push32(dataLen);  // PACK_SIZE
        push32(dataLen);  // UNP_SIZE
        rar4.push_back(2);// HOST_OS (Win32)
        push32(0);        // FILE_CRC
        push32(0);        // FTIME
        rar4.push_back(29);// UNP_VER
        rar4.push_back(0x30); // METHOD = Store
        push16(fnLen);    // NAME_SIZE
        push32(0x20);     // ATTR
        rar4.insert(rar4.end(), fn, fn + fnLen);
        rar4.insert(rar4.end(), payload, payload + dataLen);

        std::wstring rar4Path = std::wstring(tmpDir) + L"e2_stage7_test2.rar";
        PathUtils::WriteFileBytes(rar4Path, rar4.data(), rar4.size());

        ArchiveSummaryInfo sum;
        std::vector<uint8_t> extracted;
        bool ok = ArchiveExplorer::InspectArchiveFile(rar4Path, sum) &&
                  sum.valid && sum.formatName == L"RAR4" && sum.entries.size() == 1 &&
                  sum.entries[0].pathInArchive == L"docs/manual.txt" &&
                  ArchiveExplorer::ExtractSingleEntryToMemory(rar4Path, L"docs/manual.txt", extracted) &&
                  extracted.size() == dataLen && std::memcmp(extracted.data(), payload, dataLen) == 0;
        DeleteFileW(rar4Path.c_str());
        tc.Record(ok, L"Stage7.Test2_RAR4_HeaderParser_And_StoreExtraction");
    }

    // Test 7.3: RAR 5.0 Vint Header Parser & Store Extraction
    {
        std::vector<uint8_t> rar5 = { 'R', 'a', 'r', '!', 0x1A, 0x07, 0x01, 0x00 };
        const char* fn = "config/settings.ini";
        const char* payload = "RAR5_VINT_OK";
        uint8_t fnLen = static_cast<uint8_t>(std::strlen(fn));
        uint8_t dataLen = static_cast<uint8_t>(std::strlen(payload));

        // Build RAR5 File Header body (vint fields):
        // HeaderType=2, HeaderFlags=0x02 (has DataSize), DataSize=dataLen,
        // FileFlags=0, UnpackedSize=dataLen, Attributes=0x20, CompInfo=0 (Store), HostOS=0, NameLen=fnLen, Name
        std::vector<uint8_t> hdrBody = {
            2, 0x02, dataLen,
            0, dataLen, 0x20, 0, 0, fnLen
        };
        hdrBody.insert(hdrBody.end(), fn, fn + fnLen);

        // CRC32 (4 bytes) + HeaderSize (1-byte vint) + hdrBody + payload
        rar5.insert(rar5.end(), { 0, 0, 0, 0 });
        rar5.push_back(static_cast<uint8_t>(hdrBody.size()));
        rar5.insert(rar5.end(), hdrBody.begin(), hdrBody.end());
        rar5.insert(rar5.end(), payload, payload + dataLen);

        std::wstring rar5Path = std::wstring(tmpDir) + L"e2_stage7_test3.rar";
        PathUtils::WriteFileBytes(rar5Path, rar5.data(), rar5.size());

        ArchiveSummaryInfo sum;
        std::vector<uint8_t> extracted;
        bool ok = ArchiveExplorer::InspectArchiveFile(rar5Path, sum) &&
                  sum.valid && sum.formatName == L"RAR5" && sum.entries.size() == 1 &&
                  sum.entries[0].pathInArchive == L"config/settings.ini" &&
                  ArchiveExplorer::ExtractSingleEntryToMemory(rar5Path, L"config/settings.ini", extracted) &&
                  extracted.size() == dataLen && std::memcmp(extracted.data(), payload, dataLen) == 0;
        DeleteFileW(rar5Path.c_str());
        tc.Record(ok, L"Stage7.Test3_RAR5_VintHeaderParser_And_StoreExtraction");
    }

    // Test 7.4: 7-Zip Signature & NextHeader UTF-16LE Table of Contents Parser
    {
        std::vector<uint8_t> sz = {
            '7', 'z', 0xBC, 0xAF, 0x27, 0x1C, // 6-byte signature
            0, 4,                             // version 0.4
            0, 0, 0, 0                        // StartHeaderCRC
        };
        auto push64 = [&](uint64_t v) {
            for (int i = 0; i < 8; ++i) sz.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
        };
        const char* rawPayload = "7ZIP_DATA_STREAM"; // 16 bytes @ offset 32
        // Build NextHeader with kHeader(0x01), kFilesInfo(0x05), kName(0x11), size, external=0, UTF-16LE "bin/tool.exe\0"
        const wchar_t* wname = L"bin/tool.exe";
        size_t wlen = wcslen(wname);
        std::vector<uint8_t> nextHdr = { 0x01, 0x05, 0x11 };
        uint8_t propSize = static_cast<uint8_t>(1 + (wlen + 1) * 2);
        nextHdr.push_back(propSize);
        nextHdr.push_back(0); // external = 0
        for (size_t i = 0; i <= wlen; ++i) {
            uint16_t ch = static_cast<uint16_t>(wname[i]);
            nextHdr.push_back(ch & 0xFF);
            nextHdr.push_back((ch >> 8) & 0xFF);
        }
        nextHdr.push_back(0x00); // kEnd

        push64(16); // NextHeaderOffset = 16 (after 16-byte payload)
        push64(nextHdr.size());
        sz.insert(sz.end(), { 0, 0, 0, 0 }); // NextHeaderCRC
        sz.insert(sz.end(), rawPayload, rawPayload + 16);
        sz.insert(sz.end(), nextHdr.begin(), nextHdr.end());

        std::wstring szPath = std::wstring(tmpDir) + L"e2_stage7_test4.7z";
        PathUtils::WriteFileBytes(szPath, sz.data(), sz.size());

        ArchiveSummaryInfo sum;
        std::vector<uint8_t> extracted;
        bool ok = ArchiveExplorer::InspectArchiveFile(szPath, sum) &&
                  sum.valid && sum.formatName == L"7-Zip" && sum.entries.size() == 1 &&
                  sum.entries[0].pathInArchive == L"bin/tool.exe" &&
                  ArchiveExplorer::ExtractSingleEntryToMemory(szPath, L"bin/tool.exe", extracted) &&
                  extracted.size() == 16 && std::memcmp(extracted.data(), rawPayload, 16) == 0;
        DeleteFileW(szPath.c_str());
        tc.Record(ok, L"Stage7.Test4_7Zip_SignatureAndUTF16Header");
    }

    // Test 7.5: POSIX TAR & GZIP (.tar, .gz) Inspection and Full Folder Extraction
    {
        // Build a valid 1024-byte POSIX TAR archive (512-byte header + 512-byte data block)
        std::vector<uint8_t> tar(1536, 0);
        const char* tname = "sub/hello.txt";
        const char* tdata = "TAR_EXTRACT_SUCCESS";
        std::memcpy(tar.data(), tname, std::strlen(tname));
        std::memcpy(tar.data() + 124, "00000000023", 11); // octal 23 = 19 bytes ("TAR_EXTRACT_SUCCESS")
        tar[156] = '0'; // regular file
        std::memcpy(tar.data() + 257, "ustar", 5);
        std::memcpy(tar.data() + 512, tdata, 19);

        std::wstring tarPath = std::wstring(tmpDir) + L"e2_stage7_test5.tar";
        PathUtils::WriteFileBytes(tarPath, tar.data(), tar.size());

        std::wstring extractDir = std::wstring(tmpDir) + L"e2_stage7_extracted";
        bool okExtract = ArchiveExplorer::ExtractEntireArchiveToFolder(tarPath, extractDir);
        std::vector<uint8_t> readBack;
        bool okRead = PathUtils::ReadFileBytes(extractDir + L"\\sub\\hello.txt", 1024, readBack) &&
                      readBack.size() == 19 && std::memcmp(readBack.data(), tdata, 19) == 0;

        DeleteFileW((extractDir + L"\\sub\\hello.txt").c_str());
        RemoveDirectoryW((extractDir + L"\\sub").c_str());
        RemoveDirectoryW(extractDir.c_str());
        DeleteFileW(tarPath.c_str());
        tc.Record(okExtract && okRead, L"Stage7.Test5_POSIX_TAR_And_FullExtraction");
    }
}

// ============================================================================
// STAGE 8 TESTS: LAN Ping Tool Auto-Refresh & Rolling RTT Telemetry
// ============================================================================
void RunStage8Tests(TestCounter& tc) {
    // Test 8.1: Rolling RTT Min / Max / Avg & 16-Sample Sliding Window
    {
        LanHost h;
        h.ip = L"192.168.1.10";
        LanScanner::UpdateRollingPingStats(h, 10);
        LanScanner::UpdateRollingPingStats(h, 20);
        LanScanner::UpdateRollingPingStats(h, 30);
        bool ok1 = (h.pingSent == 3 && h.pingLost == 0 && h.rttMs == 30 &&
                    h.minRttMs == 10 && h.maxRttMs == 30 && h.avgRttMs == 20 &&
                    h.recentRtts.size() == 3);
        for (int i = 0; i < 20; ++i) {
            LanScanner::UpdateRollingPingStats(h, 40);
        }
        bool ok2 = (h.recentRtts.size() == 16 && h.avgRttMs == 40 && h.minRttMs == 10 && h.maxRttMs == 40);
        tc.Record(ok1 && ok2, L"Stage8.Test1_RollingPingRttStats_MinMaxAvg",
            L"min=" + std::to_wstring(h.minRttMs) + L", avg=" + std::to_wstring(h.avgRttMs) + L", max=" + std::to_wstring(h.maxRttMs));
    }

    // Test 8.2: Packet Loss Tracking & Timeout Recovery
    {
        LanHost h;
        h.ip = L"192.168.1.50";
        LanScanner::UpdateRollingPingStats(h, 12);
        LanScanner::UpdateRollingPingStats(h, -1); // timeout
        bool okLoss = (h.pingSent == 2 && h.pingLost == 1 && h.rttMs == -1 && h.minRttMs == 12 && h.avgRttMs == 12);
        LanScanner::UpdateRollingPingStats(h, 16); // recovery
        bool okRec = (h.pingSent == 3 && h.pingLost == 1 && h.rttMs == 16 && h.minRttMs == 12 && h.maxRttMs == 16 && h.avgRttMs == 14);
        tc.Record(okLoss && okRec, L"Stage8.Test2_PacketLossTracking_And_Recovery",
            L"sent=" + std::to_wstring(h.pingSent) + L", lost=" + std::to_wstring(h.pingLost));
    }

    // Test 8.3: Ping Auto-Refresh Interval Cycling (1s -> 2s -> 5s -> 10s -> 1s) & Clamping
    {
        LanScanner lan;
        lan.SetPingAutoRefreshInterval(1000);
        int i1 = lan.CyclePingAutoRefreshInterval(); // 2000
        int i2 = lan.CyclePingAutoRefreshInterval(); // 5000
        int i3 = lan.CyclePingAutoRefreshInterval(); // 10000
        int i4 = lan.CyclePingAutoRefreshInterval(); // 1000
        lan.SetPingAutoRefreshInterval(100); // clamped to 500
        int clampedLow = lan.GetPingAutoRefreshIntervalMs();
        bool ok = (i1 == 2000 && i2 == 5000 && i3 == 10000 && i4 == 1000 && clampedLow == 500);
        tc.Record(ok, L"Stage8.Test3_PingIntervalCycling_And_Clamping");
    }

    // Test 8.4: Localhost (127.0.0.1) Live ICMP Ping & All-Hosts Sweep
    {
        LanScanner lan;
        int rtt1 = lan.PingSingleHost(L"127.0.0.1");
        lan.PingAllHostsOnce();
        auto hosts = lan.SnapshotHosts();
        bool ok = (rtt1 >= 0 && rtt1 < 200 && hosts.size() == 1 &&
                   hosts[0].ip == L"127.0.0.1" && hosts[0].pingSent == 2 &&
                   hosts[0].viaPing && hosts[0].minRttMs >= 0);
        tc.Record(ok, L"Stage8.Test4_LocalhostIcmpPing_SingleAndSweep",
            L"rtt=" + std::to_wstring(rtt1) + L"ms, sent=" + std::to_wstring(hosts.empty() ? 0 : hosts[0].pingSent));
    }

    // Test 8.5: Non-Blocking Auto-Refresh Thread Start / Toggle / Stop Lifecycle
    {
        LanScanner lan;
        lan.PingSingleHost(L"127.0.0.1");
        lan.StartPingAutoRefresh(500);
        bool wasOn = lan.IsPingAutoRefreshEnabled();
        lan.TogglePingAutoRefresh();
        bool afterToggleOff = !lan.IsPingAutoRefreshEnabled();
        lan.TogglePingAutoRefresh();
        bool afterToggleOn = lan.IsPingAutoRefreshEnabled();
        lan.StopPingAutoRefresh();
        bool afterStop = !lan.IsPingAutoRefreshEnabled();
        tc.Record(wasOn && afterToggleOff && afterToggleOn && afterStop, L"Stage8.Test5_AutoRefreshThread_Lifecycle");
    }
}

// ============================================================================
// STAGE 9 TESTS: Backups Tab, Rclone Command Builder, Filters & Incremental Sync
// ============================================================================
void RunStage9Tests(TestCounter& tc) {
    wchar_t tmpDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmpDir);

    // Test 9.1: BackupJob CRUD & UTF-16LE INI Persistence Round-Trip
    {
        std::wstring iniPath = std::wstring(tmpDir) + L"e2_stage9_backups_test.ini";
        DeleteFileW(iniPath.c_str());

        BackupManager bm1;
        bm1.LoadJobs(iniPath); // seeds 2 default sample jobs when INI doesn't exist
        BackupJob custom;
        custom.name = L"Kopia Projektu C++ (Zażółć)";
        custom.sourcePath = L"W:\\GIT\\Explorer2\\src";
        custom.targetPath = L"gdrive:Explorer2/Backups";
        custom.scheduleMinutes = 15;
        custom.whenTargetAvailable = true;
        custom.useRclone = true;
        custom.includeFilter = L"*.cpp;*.h";
        custom.excludeFilter = L"*.tmp;build/*";
        custom.enabled = true;
        int newId = bm1.AddJob(custom);
        bm1.SaveJobs(iniPath);

        BackupManager bm2;
        bm2.LoadJobs(iniPath);
        BackupJob loaded;
        bool ok = bm2.GetJobById(newId, loaded) &&
                  loaded.name == custom.name &&
                  loaded.sourcePath == custom.sourcePath &&
                  loaded.targetPath == custom.targetPath &&
                  loaded.scheduleMinutes == 15 &&
                  loaded.whenTargetAvailable &&
                  loaded.useRclone &&
                  loaded.includeFilter == L"*.cpp;*.h" &&
                  loaded.excludeFilter == L"*.tmp;build/*";
        DeleteFileW(iniPath.c_str());
        tc.Record(ok, L"Stage9.Test1_BackupJob_INIPersistence_And_CRUD");
    }

    // Test 9.2: Wildcard Include / Exclude Filter Engine
    {
        std::wstring inc = L"*.cpp;*.h;*.md";
        std::wstring exc = L"*.tmp;*.bak;.git/*;node_modules/*";
        bool m1 = BackupManager::MatchesFilterRules(L"src/main.cpp", inc, exc);
        bool m2 = BackupManager::MatchesFilterRules(L"README.md", inc, exc);
        bool m3 = !BackupManager::MatchesFilterRules(L"src/main.cpp.tmp", inc, exc);
        bool m4 = !BackupManager::MatchesFilterRules(L"web/node_modules/pkg/index.cpp", inc, exc);
        bool m5 = !BackupManager::MatchesFilterRules(L"assets/logo.png", inc, exc);
        tc.Record(m1 && m2 && m3 && m4 && m5, L"Stage9.Test2_IncludeExclude_WildcardFilterEngine");
    }

    // Test 9.3: "When Target Available" Rising-Edge Detector, Periodic Schedule & Rclone CLI Builder
    {
        BackupJob job;
        job.name = L"USB Drive Backup";
        job.sourcePath = L"C:\\Work";
        job.targetPath = L"E:\\Backup";
        job.scheduleMinutes = 60; // 3600s
        job.whenTargetAvailable = true;
        job.enabled = true;
        job.includeFilter = L"*.docx";
        job.excludeFilter = L"*.tmp";
        job.targetWasOnline = false;
        job.lastRunEpochSec = 10000;

        // 1. Target transitions offline -> online at t=10500 (before 3600s schedule): must trigger immediately!
        std::wstring reason1;
        bool trigConnect = BackupManager::ShouldAutoRunJob(job, 10500, true, &reason1);
        job.lastRunEpochSec = 10500;

        // 2. Target stays online at t=11000 (only 500s elapsed): must NOT trigger yet
        bool trigEarly = BackupManager::ShouldAutoRunJob(job, 11000, true, nullptr);

        // 3. Target stays online at t=14200 (3700s elapsed >= 3600s): must trigger by schedule!
        std::wstring reason3;
        bool trigSched = BackupManager::ShouldAutoRunJob(job, 14200, true, &reason3);

        std::wstring rcloneCmd = BackupManager::BuildRcloneCommandLine(job, true);
        bool okCmd = (rcloneCmd.find(L"--dry-run") != std::wstring::npos &&
                      rcloneCmd.find(L"--exclude \"*.tmp\"") != std::wstring::npos &&
                      rcloneCmd.find(L"--include \"*.docx\"") != std::wstring::npos);

        tc.Record(trigConnect && !trigEarly && trigSched && okCmd,
            L"Stage9.Test3_WhenTargetAvailable_And_PeriodicSchedule_StateMachine");
    }

    // Test 9.4 & 9.5: Dry-Run Simulation & Native Incremental Sync Execution
    {
        std::wstring srcDir = std::wstring(tmpDir) + L"e2_stage9_src";
        std::wstring dstDir = std::wstring(tmpDir) + L"e2_stage9_dst";
        CreateDirectoryW(srcDir.c_str(), nullptr);
        CreateDirectoryW((srcDir + L"\\sub").c_str(), nullptr);

        const char* f1 = "int main() { return 0; }";
        const char* f2 = "#pragma once\n";
        const char* f3 = "temporary scratch file";
        PathUtils::WriteFileBytes(srcDir + L"\\app.cpp", f1, std::strlen(f1));
        PathUtils::WriteFileBytes(srcDir + L"\\sub\\util.h", f2, std::strlen(f2));
        PathUtils::WriteFileBytes(srcDir + L"\\ignore.tmp", f3, std::strlen(f3));

        BackupJob job;
        job.name = L"Incremental Sync Test";
        job.sourcePath = srcDir;
        job.targetPath = dstDir;
        job.useRclone = false;
        job.includeFilter = L"*.cpp;*.h";
        job.excludeFilter = L"*.tmp";

        // Test 9.4: Dry-Run must report 2 files to copy and 1 filtered out, without creating dstDir
        DeleteFileW((dstDir + L"\\sub\\util.h").c_str());
        RemoveDirectoryW((dstDir + L"\\sub").c_str());
        DeleteFileW((dstDir + L"\\app.cpp").c_str());
        RemoveDirectoryW(dstDir.c_str());

        BackupSyncResult dryRes = BackupManager::ExecuteJobSync(job, true);
        bool dstAbsentAfterDry = (GetFileAttributesW(dstDir.c_str()) == INVALID_FILE_ATTRIBUTES);
        bool okDry = (dryRes.ok && dryRes.dryRun && dryRes.filesCopied == 2 &&
                      dryRes.filesFilteredOut == 1 && dstAbsentAfterDry);
        tc.Record(okDry, L"Stage9.Test4_DryRun_Simulation_NoDiskWrites",
            L"wouldCopy=" + std::to_wstring(dryRes.filesCopied) + L", filtered=" + std::to_wstring(dryRes.filesFilteredOut));

        // Test 9.5: Live Incremental Sync copies 2 files on 1st pass, then skips 2 unchanged files on 2nd pass
        BackupSyncResult sync1 = BackupManager::ExecuteJobSync(job, false);
        BackupSyncResult sync2 = BackupManager::ExecuteJobSync(job, false);
        bool okSync = (sync1.ok && sync1.filesCopied == 2 && sync1.filesFilteredOut == 1 &&
                       sync2.ok && sync2.filesCopied == 0 && sync2.filesSkipped == 2);

        // Cleanup temp folders
        DeleteFileW((dstDir + L"\\sub\\util.h").c_str());
        RemoveDirectoryW((dstDir + L"\\sub").c_str());
        DeleteFileW((dstDir + L"\\app.cpp").c_str());
        RemoveDirectoryW(dstDir.c_str());
        DeleteFileW((srcDir + L"\\sub\\util.h").c_str());
        RemoveDirectoryW((srcDir + L"\\sub").c_str());
        DeleteFileW((srcDir + L"\\app.cpp").c_str());
        DeleteFileW((srcDir + L"\\ignore.tmp").c_str());
        RemoveDirectoryW(srcDir.c_str());

        tc.Record(okSync, L"Stage9.Test5_NativeIncrementalSync_ExecutionAndSkipUnchanged",
            L"pass1_copied=" + std::to_wstring(sync1.filesCopied) + L", pass2_skipped=" + std::to_wstring(sync2.filesSkipped));
    }
}

// ============================================================================
// STAGE 10 TESTS: FTP Client (URL Parser, UNIX/IIS/MLSD Listing, Bookmarks, Virtual FileEngine)
// ============================================================================
void RunStage10Tests(TestCounter& tc) {
    wchar_t tmpDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmpDir);

    // Test 10.1: FTP URL Parser (Credentials, Custom Port, IPv6/Host, Remote Path & Round-Trip)
    {
        FtpUrlInfo p1, p2;
        bool ok1 = FtpClient::ParseFtpUrl(L"ftp://admin:s3cr3t@192.168.1.50:2121/pub/firmware/v2", p1);
        bool ok2 = FtpClient::ParseFtpUrl(L"ftp://ftp.gnu.org/gnu/gcc/", p2);
        std::wstring rebuilt1 = FtpClient::BuildFtpUrl(p1);
        bool ok = (ok1 && p1.user == L"admin" && p1.password == L"s3cr3t" &&
                   p1.host == L"192.168.1.50" && p1.port == 2121 &&
                   p1.remotePath == L"/pub/firmware/v2" &&
                   rebuilt1 == L"ftp://admin:s3cr3t@192.168.1.50:2121/pub/firmware/v2" &&
                   ok2 && p2.user == L"anonymous" && p2.port == 21 && p2.host == L"ftp.gnu.org" &&
                   p2.remotePath == L"/gnu/gcc");
        tc.Record(ok, L"Stage10.Test1_FtpUrlParser_CredentialsPortAndRoundTrip", rebuilt1);
    }

    // Test 10.2: FTP Path Navigation (JoinFtpPath, GetParentFtpUrl, GetFtpLeafName)
    {
        std::wstring base = L"ftp://dev:pass@10.0.0.5:21/projects/explorer2";
        std::wstring child = FtpClient::JoinFtpPath(base, L"build_output.zip");
        std::wstring parent1 = FtpClient::GetParentFtpUrl(base);
        std::wstring parent2 = FtpClient::GetParentFtpUrl(parent1);
        std::wstring parentRoot = FtpClient::GetParentFtpUrl(parent2); // at "/" -> "THIS_PC"
        std::wstring leaf = FtpClient::GetFtpLeafName(child);
        bool ok = (child == L"ftp://dev:pass@10.0.0.5/projects/explorer2/build_output.zip" &&
                   parent1 == L"ftp://dev:pass@10.0.0.5/projects" &&
                   parent2 == L"ftp://dev:pass@10.0.0.5/" &&
                   parentRoot == L"THIS_PC" &&
                   leaf == L"build_output.zip");
        tc.Record(ok, L"Stage10.Test2_FtpPathNavigation_JoinAndParentHierarchy");
    }

    // Test 10.3: Multi-OS FTP Directory Listing Parser (Unix ls -l, Windows/IIS <DIR>, RFC 3659 MLSD)
    {
        const char* listing =
            "drwxr-xr-x   5 root root      4096 Nov 08 12:30 docs\r\n"
            "-rw-r--r--   1 root root    128512 Nov 08 12:31 schematic.kicad_pcb\r\n"
            "11-08-26  03:15PM       <DIR>          win_backups\r\n"
            "11-08-26  03:16PM                65536 firmware.bin\r\n"
            "type=file;size=4096;modify=20261108150000; readme.md\r\n";

        auto entries = FtpClient::ParseFtpDirectoryListingText(listing, L"ftp://10.0.0.1/pub", true, true, Lang::PL);
        bool hasDocs = false, hasPcb = false, hasWinDir = false, hasBin = false, hasReadme = false;
        for (const auto& e : entries) {
            if (e.name == L"docs" && e.isDirectory) hasDocs = true;
            if (e.name == L"schematic.kicad_pcb" && !e.isDirectory && e.size == 128512) hasPcb = true;
            if (e.name == L"win_backups" && e.isDirectory) hasWinDir = true;
            if (e.name == L"firmware.bin" && !e.isDirectory && e.size == 65536) hasBin = true;
            if (e.name == L"readme.md" && !e.isDirectory && e.size == 4096) hasReadme = true;
        }
        bool ok = (entries.size() == 5 && hasDocs && hasPcb && hasWinDir && hasBin && hasReadme);
        tc.Record(ok, L"Stage10.Test3_FtpDirectoryListingParser_UnixWindowsMlsd",
            L"entries=" + std::to_wstring(entries.size()));
    }

    // Test 10.4: FTP Bookmarks INI Save & Load Round-Trip
    {
        std::wstring iniPath = std::wstring(tmpDir) + L"e2_stage10_ftp_bookmarks.ini";
        DeleteFileW(iniPath.c_str());

        std::vector<FtpBookmark> list;
        list.push_back({ L"Serwer Produkcyjny", L"ftp://deploy:key123@10.10.0.20:21/var/www" });
        list.push_back({ L"Archiwum GNU", L"ftp://anonymous@ftp.gnu.org/gnu" });
        bool saved = FtpClient::SaveBookmarks(list, iniPath);
        auto loaded = FtpClient::LoadBookmarks(iniPath);
        DeleteFileW(iniPath.c_str());

        bool ok = (saved && loaded.size() == 2 &&
                   loaded[0].name == L"Serwer Produkcyjny" &&
                   loaded[0].url == L"ftp://deploy:key123@10.10.0.20:21/var/www" &&
                   loaded[1].name == L"Archiwum GNU");
        tc.Record(ok, L"Stage10.Test4_FtpBookmarks_INIPersistence");
    }

    // Test 10.5: End-to-End FileEngine FTP Enumeration & File Preview Temp Cache Download
    {
        FtpClient::ClearMockFtpData();
        const char* mockListing =
            "drwxr-xr-x 2 user group 4096 Nov 08 10:00 hardware\r\n"
            "-rw-r--r-- 1 user group   29 Nov 08 10:05 notes.txt\r\n";
        FtpClient::RegisterMockFtpListing(L"ftp://test.local/project", mockListing);
        std::string payload = "Hello from FTP Server 12345!\n";
        FtpClient::RegisterMockFtpFileContent(L"ftp://test.local/project/notes.txt",
            std::vector<uint8_t>(payload.begin(), payload.end()));

        FileEngine fe;
        auto items = fe.EnumerateDirectory(L"ftp://test.local/project", true, true, Lang::PL);
        std::wstring tempPreview;
        bool dlOk = FtpClient::DownloadFtpFileToTempCache(L"ftp://test.local/project/notes.txt", tempPreview);
        std::vector<uint8_t> readBack;
        bool readOk = dlOk && PathUtils::ReadFileBytes(tempPreview, 1024 * 1024, readBack);
        if (!tempPreview.empty()) DeleteFileW(tempPreview.c_str());
        FtpClient::ClearMockFtpData();

        bool ok = (items.size() == 2 && readOk && readBack.size() == payload.size() &&
                   std::memcmp(readBack.data(), payload.data(), payload.size()) == 0);
        tc.Record(ok, L"Stage10.Test5_FileEngine_FtpEnumeration_And_PreviewDownload",
            L"items=" + std::to_wstring(items.size()) + L", bytes=" + std::to_wstring(readBack.size()));
    }
}

static void RunStage12Tests(TestCounter& tc) {
    wchar_t tmpDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmpDir);

    // Test 12.1: PDF Inspector (Version, Title/Author metadata, MediaBox, Page count, BT..ET text stream)
    {
        std::wstring pdfPath = std::wstring(tmpDir) + L"e2_stage12_test.pdf";
        const char* rawPdf =
            "%PDF-1.7\n"
            "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
            "2 0 obj\n<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>\nendobj\n"
            "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 5 0 R >>\nendobj\n"
            "4 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 6 0 R >>\nendobj\n"
            "5 0 obj\n<< /Length 55 >>\nstream\n"
            "BT /F1 14 Tf 72 720 Td (Explorer2 PDF Page One!) Tj ET\n"
            "endstream\nendobj\n"
            "6 0 obj\n<< /Length 64 >>\nstream\n"
            "BT /F1 12 Tf 72 700 Td [(Second) -200 (Page) -200 (Content)] TJ ET\n"
            "endstream\nendobj\n"
            "7 0 obj\n<< /Title (Specyfikacja Techniczna PCB) /Author (Inzynier) /Producer (Explorer2) >>\nendobj\n"
            "%%EOF\n";
        PathUtils::WriteFileBytes(pdfPath, rawPdf, std::strlen(rawPdf));

        PdfDocumentSummary sum;
        bool ok = PdfViewer::InspectPdfFile(pdfPath, sum) &&
                  sum.valid &&
                  sum.pdfVersion == L"1.7" &&
                  sum.pageCount == 2 &&
                  sum.title == L"Specyfikacja Techniczna PCB" &&
                  sum.author == L"Inzynier" &&
                  sum.pageTexts.size() == 2 &&
                  sum.pageTexts[0].find(L"Explorer2 PDF Page One!") != std::wstring::npos &&
                  sum.pageTexts[1].find(L"Second Page Content") != std::wstring::npos;
        DeleteFileW(pdfPath.c_str());
        tc.Record(ok, L"Stage12.Test1_PdfInspector_MetadataAndPageStreams",
            L"pages=" + std::to_wstring(sum.pageCount) + L", title=" + sum.title);
    }

    // Test 12.2: WinRT Windows.Data.Pdf Visual Page Rasterizer (renders valid PDF page to BGRA32 bitmap)
    {
        std::wstring pdfPath = std::wstring(tmpDir) + L"e2_stage12_render.pdf";
        // Valid minimal PDF with xref table so Windows.Data.Pdf rasterizes page 0
        WriteRenderablePdfForTest(pdfPath);

        std::vector<uint8_t> bgra;
        uint32_t w = 0, h = 0, totalPages = 0;
        bool ok = PdfViewer::RenderPdfPageToBgra(pdfPath, 0, 300, bgra, w, h, &totalPages) &&
                  w == 300 && h == 300 && totalPages == 1 && bgra.size() == 300 * 300 * 4;
        DeleteFileW(pdfPath.c_str());
        tc.Record(ok, L"Stage12.Test2_PdfVisualPageRasterizer_WinRT",
            L"w=" + std::to_wstring(w) + L", h=" + std::to_wstring(h));
    }

    // Test 12.3: Real-World Large RAR5 Archive Speed, Caching & Compressed Entry Extraction (opt-in)
    // Needs a private archive, so it only runs when EXPLORER2_TEST_RAR5 points at one.
    // EXPLORER2_TEST_RAR5_FILES optionally pins the number of files the archive must list.
    {
        std::wstring rarPath = GetTestEnvVar(L"EXPLORER2_TEST_RAR5");
        const long long expectedFiles = _wtoi64(GetTestEnvVar(L"EXPLORER2_TEST_RAR5_FILES").c_str());
        if (!rarPath.empty() && GetFileAttributesW(rarPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            ULONGLONG t0 = GetTickCount64();
            ArchiveSummaryInfo info;
            bool ok1 = ArchiveExplorer::InspectArchiveFile(rarPath, info);
            ULONGLONG firstMs = GetTickCount64() - t0;

            ULONGLONG t1 = GetTickCount64();
            std::vector<ArchiveItemEntry> rootChildren;
            bool ok2 = ArchiveExplorer::EnumerateArchiveFolder(rarPath, L"", rootChildren);
            ULONGLONG cachedMs = GetTickCount64() - t1;

            std::wstring firstFileInner;
            for (const auto& e : info.entries) {
                if (!e.isDirectory) {
                    firstFileInner = e.pathInArchive;
                    break;
                }
            }
            std::wstring extractedTemp;
            bool ok3 = !firstFileInner.empty() &&
                       ArchiveExplorer::ExtractSingleEntryToTempFile(rarPath, firstFileInner, extractedTemp);
            uint64_t extSize = 0;
            if (ok3) {
                WIN32_FILE_ATTRIBUTE_DATA fad = {};
                if (GetFileAttributesExW(extractedTemp.c_str(), GetFileExInfoStandard, &fad)) {
                    extSize = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
                }
                DeleteFileW(extractedTemp.c_str());
            }
            const bool countOk = expectedFiles > 0
                ? (static_cast<long long>(info.totalFiles) == expectedFiles)
                : (info.totalFiles > 0);
            bool ok = ok1 && ok2 && ok3 && countOk && cachedMs <= 35 && extSize > 0;
            tc.Record(ok, L"Stage12.Test3_RealWorld_LargeRAR5_FastMap_Cache_And_Extract",
                L"firstMs=" + std::to_wstring(firstMs) +
                L"ms, cachedMs=" + std::to_wstring(cachedMs) +
                L"ms, files=" + std::to_wstring(info.totalFiles) +
                L", extractedBytes=" + std::to_wstring(extSize));
        } else {
            tc.Record(true, L"Stage12.Test3_RealWorld_LargeRAR5_FastMap_Cache_And_Extract",
                L"skipped (set EXPLORER2_TEST_RAR5 to a large .rar archive to run it)");
        }
    }

    // Test 12.4: Hardware Pro USB Topology TreeView & COM / Serial Ports Discovery
    {
        ULONGLONG t0 = GetTickCount64();
        HardwareInspector hw;
        hw.RequestUsbComRefresh(true);
        ULONGLONG elapsedMs = GetTickCount64() - t0;

        const auto& usbTree = hw.GetUsbTree();
        const auto& comPorts = hw.GetComPorts();

        int controllers = 0, rootHubs = 0, devices = 0;
        for (const auto& u : usbTree) {
            if (u.isController) ++controllers;
            else if (u.isRootHub) ++rootHubs;
            else if (!u.isHub && !u.isInterface) ++devices;
        }
        bool ok = !usbTree.empty() && controllers >= 1 && rootHubs >= 1;
        tc.Record(ok, L"Stage12.Test4_HardwareUsbTreeView_And_ComPorts_Enumeration",
            L"usbNodes=" + std::to_wstring(usbTree.size()) +
            L", controllers=" + std::to_wstring(controllers) +
            L", rootHubs=" + std::to_wstring(rootHubs) +
            L", devices=" + std::to_wstring(devices) +
            L", comPorts=" + std::to_wstring(comPorts.size()) +
            L", scanMs=" + std::to_wstring(elapsedMs) + L"ms");
    }

    // Test 5: Virtual Archive Image Extraction & Thumbnail Decoding (e.g. photos.rar/photo.png), opt-in:
    // runs when EXPLORER2_TEST_IMAGE_RAR points at an archive whose first image is a PNG.
    {
        std::wstring sampleRar = GetTestEnvVar(L"EXPLORER2_TEST_IMAGE_RAR");
        if (!sampleRar.empty() && GetFileAttributesW(sampleRar.c_str()) != INVALID_FILE_ATTRIBUTES) {
            FileEngine fe;
            auto items = fe.EnumerateDirectory(sampleRar, false, true, Lang::PL);
            bool foundImg = false;
            std::wstring virtualImgPath;
            for (const auto& it : items) {
                if (it.isImage) {
                    foundImg = true;
                    virtualImgPath = it.fullPath;
                    break;
                }
            }
            std::wstring arcFile, innerEntry, tempFile1, tempFile2;
            bool splitOk = ArchiveExplorer::SplitVirtualArchivePath(virtualImgPath, arcFile, innerEntry) && !innerEntry.empty();
            ULONGLONG t0 = GetTickCount64();
            bool ext1Ok = splitOk && ArchiveExplorer::ExtractSingleEntryToTempFile(arcFile, innerEntry, tempFile1);
            ULONGLONG firstMs = GetTickCount64() - t0;

            ULONGLONG t1 = GetTickCount64();
            bool ext2Ok = splitOk && ArchiveExplorer::ExtractSingleEntryToTempFile(arcFile, innerEntry, tempFile2);
            ULONGLONG cachedMs = GetTickCount64() - t1;

            std::vector<uint8_t> imgBytes;
            bool bytesOk = ext1Ok && PathUtils::ReadFileBytes(tempFile1, 64ULL * 1024 * 1024, imgBytes) && imgBytes.size() > 8 &&
                           imgBytes[0] == 0x89 && imgBytes[1] == 'P' && imgBytes[2] == 'N' && imgBytes[3] == 'G';

            tc.Record(foundImg && splitOk && ext1Ok && ext2Ok && bytesOk,
                L"Stage12.Test5_ArchiveVirtualImageThumbnailDecode",
                L"virtualPath=" + virtualImgPath +
                L", bytes=" + std::to_wstring(imgBytes.size()) +
                L", firstExtractMs=" + std::to_wstring(firstMs) +
                L"ms, cachedExtractMs=" + std::to_wstring(cachedMs) + L"ms");
        } else {
            tc.Record(true, L"Stage12.Test5_ArchiveVirtualImageThumbnailDecode",
                L"skipped (set EXPLORER2_TEST_IMAGE_RAR to an archive containing a PNG to run it)");
        }
    }

    // Test 6: FileZilla XML Auto-Import (Base64 passwords, '@' usernames, FTPS) & Navigation Tree Under Ten komputer
    {
        wchar_t tempDir[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempDir);
        std::wstring synthFzDir = std::wstring(tempDir) + L"Explorer2_FZ_Test";
        CreateDirectoryW(synthFzDir.c_str(), nullptr);

        // "I0Zha2U7UGFzczEy" is Base64 for "#Fake;Pass12"
        const char* sampleXml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<FileZilla3 version=\"3.66.5\" platform=\"windows\">\n"
            "  <Servers>\n"
            "    <Server>\n"
            "      <Host>ftps.example.com</Host>\n"
            "      <Port>21</Port>\n"
            "      <Protocol>4</Protocol>\n"
            "      <User>deploy@example.com</User>\n"
            "      <Pass encoding=\"base64\">I0Zha2U7UGFzczEy</Pass>\n"
            "      <Name>Example FTPS</Name>\n"
            "    </Server>\n"
            "  </Servers>\n"
            "</FileZilla3>\n";
        std::wstring xmlPath = synthFzDir + L"\\sitemanager.xml";
        PathUtils::WriteFileBytes(xmlPath, sampleXml, strlen(sampleXml));

        auto synthSites = FtpClient::ImportFileZillaSites(synthFzDir);
        DeleteFileW(xmlPath.c_str());
        RemoveDirectoryW(synthFzDir.c_str());

        bool synthOk = (synthSites.size() == 1 &&
                        synthSites[0].host == L"ftps.example.com" &&
                        synthSites[0].user == L"deploy@example.com" &&
                        synthSites[0].password == L"#Fake;Pass12" &&
                        synthSites[0].useFtps);

        FtpUrlInfo roundTrip;
        bool urlRoundTripOk = synthOk && FtpClient::ParseFtpUrl(synthSites[0].url, roundTrip) &&
                              roundTrip.host == L"ftps.example.com" &&
                              roundTrip.user == L"deploy@example.com" &&
                              roundTrip.password == L"#Fake;Pass12" &&
                              roundTrip.useFtps;

        auto realFzSites = FtpClient::ImportFileZillaSites();
        auto allBookmarks = FtpClient::LoadBookmarks();

        FileEngine fe;
        std::vector<std::pair<std::wstring, std::wstring>> emptyPins;
        auto treeNodes = fe.BuildNavigationTree(Lang::PL, emptyPins);
        bool hasFtpHeader = false;
        int ftpChildNodes = 0;
        for (const auto& tn : treeNodes) {
            if (tn.normKey == L"__HEADER_FTP__") hasFtpHeader = true;
            else if (tn.isFtpNode && tn.indent == 2) ++ftpChildNodes;
        }

        tc.Record(synthOk && urlRoundTripOk && hasFtpHeader && ftpChildNodes == static_cast<int>(allBookmarks.size()),
            L"Stage12.Test6_FileZillaAutoImport_And_FtpsTreeIntegration",
            L"synthDecodedPass=" + (synthOk ? synthSites[0].password : L"FAIL") +
            L", realFileZillaSites=" + std::to_wstring(realFzSites.size()) +
            L", treeFtpNodes=" + std::to_wstring(ftpChildNodes));
    }

    // Test 7: Fast Directory Enumeration (< 50ms) & FTP Split-View Dual-Pane Operations (Local<->FTP, FTP<->FTP, Rename, Delete, Recursive Tree)
    {
        FileEngine fe;
        // The project's own src folder when the exe sits in the repository root, otherwise the exe's folder
        std::wstring enumDir = GetExeDir() + L"\\src";
        DWORD enumAttr = GetFileAttributesW(enumDir.c_str());
        if (enumAttr == INVALID_FILE_ATTRIBUTES || !(enumAttr & FILE_ATTRIBUTE_DIRECTORY)) {
            enumDir = GetExeDir();
        }
        ULONGLONG t0 = GetTickCount64();
        auto srcEntries = fe.EnumerateDirectory(enumDir, true, true, Lang::PL);
        FileEngine::SortEntries(srcEntries, SortColumn::Name, true);
        ULONGLONG enumMs = GetTickCount64() - t0;
        bool fastEnumOk = (!srcEntries.empty() && enumMs <= 50);

        wchar_t tempDir[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempDir);
        std::wstring localStage = std::wstring(tempDir) + L"Explorer2_LocalStage";
        CreateDirectoryW(localStage.c_str(), nullptr);

        FtpClient::ClearMockFtpData();
        FtpClient::RegisterMockFtpListing(L"ftp://mock.local/paneB",
            "drwxr-xr-x 2 user group 4096 Nov 08 12:00 subdir\r\n");

        // 1. Upload single file to FTP directory URL and explicit file URL (verifying no double-filename bug)
        std::wstring localFile1 = localStage + L"\\alpha.txt";
        std::wstring localFile2 = localStage + L"\\beta.txt";
        const char* payload1 = "Explorer2 DualPane FTP Test Payload";
        PathUtils::WriteFileBytes(localFile1, payload1, strlen(payload1));
        PathUtils::WriteFileBytes(localFile2, payload1, strlen(payload1));
        bool up1 = FtpClient::UploadLocalFileToFtp(localFile1, L"ftp://mock.local/paneB");
        bool up2 = FtpClient::UploadLocalFileToFtp(localFile2, L"ftp://mock.local/paneB/beta.txt");

        // 2. Upload recursive directory tree Local -> FTP and Download recursive tree FTP -> Local
        std::wstring subDir = localStage + L"\\subdir";
        CreateDirectoryW(subDir.c_str(), nullptr);
        std::wstring subFile = subDir + L"\\nested.txt";
        PathUtils::WriteFileBytes(subFile, payload1, strlen(payload1));
        bool treeUpOk = FtpClient::UploadLocalTreeToFtp(subDir, L"ftp://mock.local/paneB");

        std::wstring dlRestoreDir = localStage + L"\\restored_subdir";
        CreateDirectoryW(dlRestoreDir.c_str(), nullptr);
        bool treeDlOk = FtpClient::DownloadFtpTreeToLocal(L"ftp://mock.local/paneB/subdir", true, dlRestoreDir);
        std::vector<uint8_t> restoredBytes;
        bool restoredContentOk = PathUtils::ReadFileBytes(dlRestoreDir + L"\\subdir\\nested.txt", 4096, restoredBytes) &&
                                 restoredBytes.size() == strlen(payload1);

        // 3. Rename & Delete on FTP
        std::wstring newFtpUrl;
        bool renOk = FtpClient::RenameFtpItem(L"ftp://mock.local/paneB/beta.txt", L"beta_renamed.txt", newFtpUrl);
        std::vector<FileEntry> paneBEntries;
        FtpClient::EnumerateFtpDirectory(L"ftp://mock.local/paneB", true, true, Lang::PL, paneBEntries);
        bool foundAlpha = false, foundRenamed = false;
        for (const auto& e : paneBEntries) {
            if (e.name == L"alpha.txt") foundAlpha = true;
            if (e.name == L"beta_renamed.txt") foundRenamed = true;
        }
        bool delOk = FtpClient::DeleteFtpItem(L"ftp://mock.local/paneB/beta_renamed.txt", false) &&
                     FtpClient::DeleteFtpItem(L"ftp://mock.local/paneB/subdir", true);

        FtpClient::ClearMockFtpData();

        tc.Record(fastEnumOk && up1 && up2 && treeUpOk && treeDlOk && restoredContentOk && renOk && foundAlpha && foundRenamed && delOk,
            L"Stage12.Test7_FtpSplitViewDualPane_And_FastDirEnum",
            L"srcEnumFiles=" + std::to_wstring(srcEntries.size()) +
            L", enumMs=" + std::to_wstring(enumMs) + L"ms, ftpDualPaneTreeAndRename=OK");
    }

    // Test 8: Next 12 File Formats Inspectors (.dds, .svg, .xbm, .xpm, .dbf, .dif, .slk, .sqlite, .fb2, .dxf, .vtk + PPTX/EPUB ext registration)
    {
        wchar_t tempDir[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempDir);
        std::wstring fmtDir = std::wstring(tempDir) + L"Explorer2_Fmt12Test";
        CreateDirectoryW(fmtDir.c_str(), nullptr);

        // 1. SVG Vector Image
        const char* svgSample =
            "<svg width=\"64\" height=\"64\" viewBox=\"0 0 64 64\">"
            "<rect x=\"4\" y=\"4\" width=\"56\" height=\"56\" fill=\"#2563EB\"/>"
            "<circle cx=\"32\" cy=\"32\" r=\"18\" fill=\"#FACC15\"/>"
            "</svg>";
        ExtendedImageInfo svgImg;
        bool svgOk = ImageFormatLoader::DecodeImageMemory(reinterpret_cast<const uint8_t*>(svgSample), strlen(svgSample), L".svg", 128, svgImg) &&
                     svgImg.valid && svgImg.width >= 64 && svgImg.height >= 64;

        // 2. XBM Image
        const char* xbmSample =
            "#define icon_width 8\n#define icon_height 8\n"
            "static unsigned char icon_bits[] = { 0xFF, 0x81, 0x81, 0x99, 0x99, 0x81, 0x81, 0xFF };\n";
        ExtendedImageInfo xbmImg;
        bool xbmOk = ImageFormatLoader::DecodeImageMemory(reinterpret_cast<const uint8_t*>(xbmSample), strlen(xbmSample), L".xbm", 64, xbmImg) &&
                     xbmImg.valid && xbmImg.width == 8 && xbmImg.height == 8;

        // 3. XPM Image
        const char* xpmSample =
            "/* XPM */\nstatic char * sample_xpm[] = {\n"
            "\"4 4 2 1\",\n\". c #000000\",\n\"# c #FF0000\",\n"
            "\"####\",\n\"#..#\",\n\"#..#\",\n\"####\"\n};\n";
        ExtendedImageInfo xpmImg;
        bool xpmOk = ImageFormatLoader::DecodeImageMemory(reinterpret_cast<const uint8_t*>(xpmSample), strlen(xpmSample), L".xpm", 64, xpmImg) &&
                     xpmImg.valid && xpmImg.width == 4 && xpmImg.height == 4;

        // 4. DDS Texture (Uncompressed 4x4 32-bit RGBA)
        std::vector<uint8_t> ddsBuf(128 + 4 * 4 * 4, 0);
        std::memcpy(ddsBuf.data(), "DDS ", 4);
        uint32_t hdrSize = 124, hVal = 4, wVal = 4, pfSize = 32, pfFlags = 0x41, bpp = 32;
        uint32_t rMask = 0x00FF0000u, gMask = 0x0000FF00u, bMask = 0x000000FFu, aMask = 0xFF000000u;
        std::memcpy(&ddsBuf[4],  &hdrSize, 4);
        std::memcpy(&ddsBuf[12], &hVal, 4);
        std::memcpy(&ddsBuf[16], &wVal, 4);
        std::memcpy(&ddsBuf[76], &pfSize, 4);
        std::memcpy(&ddsBuf[80], &pfFlags, 4);
        std::memcpy(&ddsBuf[88], &bpp, 4);
        std::memcpy(&ddsBuf[92], &rMask, 4);
        std::memcpy(&ddsBuf[96], &gMask, 4);
        std::memcpy(&ddsBuf[100], &bMask, 4);
        std::memcpy(&ddsBuf[104], &aMask, 4);
        for (size_t i = 128; i < ddsBuf.size(); i += 4) {
            ddsBuf[i] = 0x20; ddsBuf[i + 1] = 0x80; ddsBuf[i + 2] = 0xE0; ddsBuf[i + 3] = 0xFF;
        }
        ExtendedImageInfo ddsImg;
        bool ddsOk = ImageFormatLoader::DecodeImageMemory(ddsBuf.data(), ddsBuf.size(), L".dds", 64, ddsImg) &&
                     ddsImg.valid && ddsImg.width == 4 && ddsImg.height == 4;

        // 5. SQLite 3 Database (.sqlite)
        std::vector<uint8_t> sqliteBuf(512, 0);
        std::memcpy(sqliteBuf.data(), "SQLite format 3\0", 16);
        sqliteBuf[16] = 0x10; sqliteBuf[17] = 0x00; // 4096 page size
        sqliteBuf[31] = 1; // 1 page
        sqliteBuf[59] = 1; // UTF-8
        const char* ddlSample = "CREATE TABLE sensors (id INTEGER PRIMARY KEY, label TEXT, temp REAL)";
        std::memcpy(&sqliteBuf[140], ddlSample, strlen(ddlSample));
        std::wstring sqlitePath = fmtDir + L"\\sample.sqlite";
        PathUtils::WriteFileBytes(sqlitePath, sqliteBuf.data(), sqliteBuf.size());
        ParsedOfficeDocument sqliteDoc;
        bool sqliteOk = SpreadsheetDocViewer::InspectOfficeFile(sqlitePath, sqliteDoc) &&
                        sqliteDoc.valid && !sqliteDoc.sheets.empty() && sqliteDoc.sheets[0].rows.size() >= 6;
        DeleteFileW(sqlitePath.c_str());

        // 6. dBASE III Table (.dbf)
        std::vector<uint8_t> dbfBuf(65 + 16, 0);
        dbfBuf[0] = 0x03;
        dbfBuf[4] = 1; // 1 record
        dbfBuf[8] = 65; dbfBuf[9] = 0; // header size = 32 + 32 + 1 = 65
        dbfBuf[10] = 16; dbfBuf[11] = 0; // record size = 1 + 15 = 16
        std::memcpy(&dbfBuf[32], "NAME", 4);
        dbfBuf[32 + 11] = 'C';
        dbfBuf[32 + 16] = 15;
        dbfBuf[64] = 0x0D;
        dbfBuf[65] = ' ';
        std::memcpy(&dbfBuf[66], "Explorer2_DBF  ", 15);
        std::wstring dbfPath = fmtDir + L"\\sample.dbf";
        PathUtils::WriteFileBytes(dbfPath, dbfBuf.data(), dbfBuf.size());
        ParsedOfficeDocument dbfDoc;
        bool dbfOk = SpreadsheetDocViewer::InspectOfficeFile(dbfPath, dbfDoc) &&
                     dbfDoc.valid && !dbfDoc.sheets.empty() && dbfDoc.sheets[0].rows.size() == 2 &&
                     dbfDoc.sheets[0].rows[1][0] == L"Explorer2_DBF";
        DeleteFileW(dbfPath.c_str());

        // 7. DIF & SYLK Spreadsheets (.dif, .slk)
        const char* difSample =
            "TABLE\n0,1\n\"EXCEL\"\nVECTORS\n0,2\n\"\"\nTUPLES\n0,2\n\"\"\nDATA\n0,0\n\"\"\n"
            "-1,0\nBOT\n1,0\n\"Item\"\n0,42\nV\n-1,0\nEOD\n";
        std::wstring difPath = fmtDir + L"\\sample.dif";
        PathUtils::WriteFileBytes(difPath, difSample, strlen(difSample));
        ParsedOfficeDocument difDoc;
        bool difOk = SpreadsheetDocViewer::InspectOfficeFile(difPath, difDoc) &&
                     difDoc.valid && !difDoc.sheets.empty() && !difDoc.sheets[0].rows.empty();
        DeleteFileW(difPath.c_str());

        const char* slkSample = "ID;PWXL\nC;Y1;X1;K\"Header\"\nC;Y1;X2;K123.45\nE\n";
        std::wstring slkPath = fmtDir + L"\\sample.slk";
        PathUtils::WriteFileBytes(slkPath, slkSample, strlen(slkSample));
        ParsedOfficeDocument slkDoc;
        bool slkOk = SpreadsheetDocViewer::InspectOfficeFile(slkPath, slkDoc) &&
                     slkDoc.valid && !slkDoc.sheets.empty() && slkDoc.sheets[0].rows[0].size() == 2;
        DeleteFileW(slkPath.c_str());

        // 8. FictionBook 2 eBook (.fb2) + PPTX/ODP/EPUB extension checks
        const char* fb2Sample =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
            "<FictionBook><description><title-info><book-title>Cyber Book</book-title>"
            "<author><first-name>Jan</first-name><last-name>Kowalski</last-name></author></title-info></description>"
            "<body><section><title><p>Rozdzial 1</p></title><p>Tekst akapitu testowego.</p></section></body></FictionBook>";
        std::wstring fb2Path = fmtDir + L"\\sample.fb2";
        PathUtils::WriteFileBytes(fb2Path, fb2Sample, strlen(fb2Sample));
        ParsedOfficeDocument fb2Doc;
        bool fb2Ok = SpreadsheetDocViewer::InspectOfficeFile(fb2Path, fb2Doc) &&
                     fb2Doc.valid && fb2Doc.paragraphs.size() >= 3 &&
                     SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".pptx") &&
                     SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".odp") &&
                     SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".epub");
        DeleteFileW(fb2Path.c_str());

        // 9. AutoCAD DXF (.dxf)
        const char* dxfSample =
            "0\nSECTION\n2\nENTITIES\n0\nLINE\n8\n0\n10\n0.0\n20\n0.0\n11\n50.0\n21\n25.0\n"
            "0\nCIRCLE\n8\n0\n10\n25.0\n20\n12.5\n40\n10.0\n0\nENDSEC\n0\nEOF\n";
        std::wstring dxfPath = fmtDir + L"\\sample.dxf";
        PathUtils::WriteFileBytes(dxfPath, dxfSample, strlen(dxfSample));
        ParsedEdaBoard dxfBoard;
        bool dxfOk = EdaViewer::InspectEdaFile(dxfPath, dxfBoard) &&
                     dxfBoard.valid && !dxfBoard.traces.empty() && !dxfBoard.circles.empty();
        DeleteFileW(dxfPath.c_str());

        // 10. Visualization Toolkit 3D Mesh (.vtk)
        const char* vtkSample =
            "# vtk DataFile Version 3.0\nSample Tetrahedron\nASCII\nDATASET POLYDATA\n"
            "POINTS 4 float\n0 0 0\n1 0 0\n0 1 0\n0 0 1\n"
            "POLYGONS 4 16\n3 0 1 2\n3 0 1 3\n3 1 2 3\n3 2 0 3\n";
        std::wstring vtkPath = fmtDir + L"\\sample.vtk";
        PathUtils::WriteFileBytes(vtkPath, vtkSample, strlen(vtkSample));
        LoadedMesh3D vtkMesh;
        bool vtkOk = Model3DViewer::InspectMeshFile(vtkPath, vtkMesh) &&
                     vtkMesh.loadedOk && vtkMesh.vertices.size() == 4 && vtkMesh.tris.size() == 4;
        DeleteFileW(vtkPath.c_str());
        RemoveDirectoryW(fmtDir.c_str());

        tc.Record(svgOk && xbmOk && xpmOk && ddsOk && sqliteOk && dbfOk && difOk && slkOk && fb2Ok && dxfOk && vtkOk,
            L"Stage12.Test8_Next12FileFormats_Inspectors",
            L"svg=" + std::to_wstring(svgOk) + L", dds=" + std::to_wstring(ddsOk) +
            L", xbm=" + std::to_wstring(xbmOk) + L", xpm=" + std::to_wstring(xpmOk) +
            L", sqlite=" + std::to_wstring(sqliteOk) + L", dbf=" + std::to_wstring(dbfOk) +
            L", dif=" + std::to_wstring(difOk) + L", slk=" + std::to_wstring(slkOk) +
            L", fb2/pptx/epub=" + std::to_wstring(fb2Ok) + L", dxf=" + std::to_wstring(dxfOk) +
            L", vtk=" + std::to_wstring(vtkOk));

        // 11. Test9: Real-World Repository Samples in testdata/
        wchar_t exePathBuf[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH);
        std::wstring repoRoot = PathUtils::GetParentPath(exePathBuf);
        std::wstring tdDir = repoRoot + L"\\testdata";
        DWORD tdAttr = GetFileAttributesW(tdDir.c_str());
        if (tdAttr == INVALID_FILE_ATTRIBUTES || !(tdAttr & FILE_ATTRIBUTE_DIRECTORY)) {
            // Exe was copied out of the repository: look for testdata in the current working directory instead
            wchar_t cwdBuf[MAX_PATH] = {};
            GetCurrentDirectoryW(MAX_PATH, cwdBuf);
            tdDir = std::wstring(cwdBuf) + L"\\testdata";
        }

        // EDA & CAD samples: Eagle .sch/.brd, KiCad .kicad_sch/.kicad_pcb/legacy .sch, AutoCAD .dxf
        ParsedEdaBoard eagleSch, eagleBrd, kicadSch, kicadPcb, kicadLeg, cadDxf;
        bool okEagleSch = EdaViewer::InspectEdaFile(tdDir + L"\\eagle_schematic.sch", eagleSch) &&
                          eagleSch.valid && (!eagleSch.traces.empty() || !eagleSch.components.empty());
        bool okEagleBrd = EdaViewer::InspectEdaFile(tdDir + L"\\eagle_board.brd", eagleBrd) &&
                          eagleBrd.valid && (!eagleBrd.traces.empty() || !eagleBrd.pads.empty());
        bool okKicadSch = EdaViewer::InspectEdaFile(tdDir + L"\\interf_u.kicad_sch", kicadSch) &&
                          kicadSch.valid && !kicadSch.traces.empty();
        bool okKicadPcb = EdaViewer::InspectEdaFile(tdDir + L"\\interf_u.kicad_pcb", kicadPcb) &&
                          kicadPcb.valid && (!kicadPcb.traces.empty() || !kicadPcb.pads.empty());
        bool okKicadLeg = EdaViewer::InspectEdaFile(tdDir + L"\\kicad_legacy_regulator.sch", kicadLeg) &&
                          kicadLeg.valid && !kicadLeg.traces.empty() && !kicadLeg.components.empty();
        bool okCadDxf   = EdaViewer::InspectEdaFile(tdDir + L"\\bracket_cad.dxf", cadDxf) &&
                          cadDxf.valid && (!cadDxf.traces.empty() || !cadDxf.circles.empty());

        // Image & Vector samples: .psd, .xbm, .xpm, .svg, .dds
        ExtendedImageInfo imgPsd, imgXbm, imgXpm, imgSvg, imgDds;
        bool okImages = ImageFormatLoader::DecodeImageFile(tdDir + L"\\sample_2layers.psd", 512, imgPsd) && imgPsd.valid && imgPsd.width > 0 &&
                        ImageFormatLoader::DecodeImageFile(tdDir + L"\\hopper.xbm", 512, imgXbm) && imgXbm.valid && imgXbm.width > 0 &&
                        ImageFormatLoader::DecodeImageFile(tdDir + L"\\hopper.xpm", 512, imgXpm) && imgXpm.valid && imgXpm.width > 0 &&
                        ImageFormatLoader::DecodeImageFile(tdDir + L"\\vector_illustration.svg", 512, imgSvg) && imgSvg.valid && imgSvg.width > 0 &&
                        ImageFormatLoader::DecodeImageFile(tdDir + L"\\dxt1_gradient.dds", 512, imgDds) && imgDds.valid && imgDds.width > 0;

        // 3D Model samples: Quake 2 .md2, Stereolithography .stl, Visualization Toolkit .vtk
        LoadedMesh3D meshMd2, meshStl, meshVtk;
        bool okMd2 = Model3DViewer::InspectMeshFile(tdDir + L"\\quake2_sphere.md2", meshMd2) &&
                     meshMd2.loadedOk && meshMd2.vertices.size() > 50 && meshMd2.tris.size() > 50;
        // A real Quake II player model as well. Its author does not allow commercial distribution, so a
        // copy of the repository may come without it; the generated model above then stands alone.
        LoadedMesh3D meshMd2Real;
        const std::wstring realMd2Path = tdDir + L"\\quake2_ratamahatta.md2";
        const bool hasRealMd2 = GetFileAttributesW(realMd2Path.c_str()) != INVALID_FILE_ATTRIBUTES;
        if (hasRealMd2) {
            okMd2 = okMd2 && Model3DViewer::InspectMeshFile(realMd2Path, meshMd2Real) && meshMd2Real.loadedOk &&
                    meshMd2Real.vertices.size() >= 300 && meshMd2Real.tris.size() >= 600 &&
                    meshMd2Real.animFrameCount >= 100;
        }
        bool okStl = Model3DViewer::InspectMeshFile(tdDir + L"\\torus_ascii.stl", meshStl) &&
                     meshStl.loadedOk && meshStl.vertices.size() > 50 && meshStl.tris.size() > 50;
        bool okVtk = Model3DViewer::InspectMeshFile(tdDir + L"\\torus_mesh.vtk", meshVtk) &&
                     meshVtk.loadedOk && meshVtk.vertices.size() >= 20 && !meshVtk.tris.empty();

        // Office, Presentation, eBook & Database samples: .odp, .pptx, .epub, .fb2, .sqlite, .dbf, .dif, .slk
        ParsedOfficeDocument docOdp, docPptx, docEpub, docFb2, docSqlite, docDbf, docDif, docSlk;
        bool okOdp    = SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\shapes_presentation.odp", docOdp) &&
                        docOdp.valid && !docOdp.paragraphs.empty();
        bool okPptx   = SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample_deck.pptx", docPptx) &&
                        docPptx.valid && !docPptx.paragraphs.empty();
        bool okEpub   = SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample_book.epub", docEpub) &&
                        docEpub.valid && !docEpub.paragraphs.empty();
        bool okFb2    = SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\cybernetics_ebook.fb2", docFb2) &&
                        docFb2.valid && !docFb2.paragraphs.empty();
        bool okSqlite = SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\chinook_sample.sqlite", docSqlite) &&
                        docSqlite.valid && !docSqlite.sheets.empty();
        bool okDbf    = SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\parcels_gis.dbf", docDbf) &&
                        docDbf.valid && !docDbf.sheets.empty() && docDbf.sheets[0].rows.size() >= 2;
        bool okDif    = SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\telemetry_sheet.dif", docDif) &&
                        docDif.valid && !docDif.sheets.empty() && docDif.sheets[0].rows.size() >= 2;
        bool okSlk    = SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\benchmark_sheet.slk", docSlk) &&
                        docSlk.valid && !docSlk.sheets.empty() && docSlk.sheets[0].rows.size() >= 2;

        bool allRealOk = okEagleSch && okEagleBrd && okKicadSch && okKicadPcb && okKicadLeg && okCadDxf &&
                         okImages && okMd2 && okStl && okVtk &&
                         okOdp && okPptx && okEpub && okFb2 && okSqlite && okDbf && okDif && okSlk;
        tc.Record(allRealOk,
            L"Stage12.Test9_RealWorld_TestData_Repository_Samples",
            L"eagleSch=" + std::to_wstring(eagleSch.traces.size()) +
            L", eagleBrd=" + std::to_wstring(eagleBrd.traces.size()) +
            L", kicadSch=" + std::to_wstring(kicadSch.traces.size()) +
            L", kicadPcb=" + std::to_wstring(kicadPcb.traces.size()) +
            L", dxf=" + std::to_wstring(cadDxf.traces.size()) +
            L", psd=" + std::to_wstring(imgPsd.width) + L"x" + std::to_wstring(imgPsd.height) +
            L", xbm=" + std::to_wstring(imgXbm.width) + L"x" + std::to_wstring(imgXbm.height) +
            L", md2Tris=" + std::to_wstring(meshMd2.tris.size()) +
            L", md2RealTris=" + (hasRealMd2 ? std::to_wstring(meshMd2Real.tris.size()) : std::wstring(L"absent")) +
            L", stlTris=" + std::to_wstring(meshStl.tris.size()) +
            L", odpParas=" + std::to_wstring(docOdp.paragraphs.size()) +
            L", pptxParas=" + std::to_wstring(docPptx.paragraphs.size()) +
            L", epubParas=" + std::to_wstring(docEpub.paragraphs.size()) +
            L", sqliteSheets=" + std::to_wstring(docSqlite.sheets.size()) +
            L", dbfRows=" + std::to_wstring(docDbf.sheets.empty() ? 0 : docDbf.sheets[0].rows.size()));

        // 12. Test10: Extended 27 Additional File Format Samples in testdata/ (49 total samples)
        LoadedMesh3D mObj, m3ds, mPly, mOff, mDae, m3mf, mBlend, mMd3, mGlb;
        bool okExt3D =
            Model3DViewer::InspectMeshFile(tdDir + L"\\wavefront_torus.obj", mObj) && mObj.loadedOk && !mObj.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\icosahedron_model.3ds", m3ds) && m3ds.loadedOk && !m3ds.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\octahedron_mesh.ply", mPly) && mPly.loadedOk && !mPly.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\geomview_pyramid.off", mOff) && mOff.loadedOk && !mOff.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\collada_elf_girl.dae", mDae) && mDae.loadedOk && !mDae.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\box_part.3mf", m3mf) && m3mf.loadedOk && !m3mf.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\blender_box.blend", mBlend) && mBlend.loadedOk && !mBlend.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\quake3_mesh.md3", mMd3) && mMd3.loadedOk && !mMd3.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\khronos_box.glb", mGlb) && mGlb.loadedOk && !mGlb.tris.empty();

        ParsedEdaBoard gbrBoard, drlBoard;
        bool okExtEda =
            EdaViewer::InspectEdaFile(tdDir + L"\\board_top_copper.gbr", gbrBoard) && gbrBoard.valid && (!gbrBoard.traces.empty() || !gbrBoard.pads.empty()) &&
            EdaViewer::InspectEdaFile(tdDir + L"\\board_nc_drill.drl", drlBoard) && drlBoard.valid && !drlBoard.vias.empty();

        ExtendedImageInfo imgTga, imgXcf, imgHdr, imgPpm, imgPgm, imgPcx, imgQoi;
        bool okExtImg =
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\truevision_abc.tga", 512, imgTga) && imgTga.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\gimp_multilayer.xcf", 512, imgXcf) && imgXcf.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\radiance_gradient.hdr", 512, imgHdr) && imgHdr.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\hopper_color.ppm", 512, imgPpm) && imgPpm.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\gradient_gray.pgm", 512, imgPgm) && imgPgm.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\gradient_paintbrush.pcx", 512, imgPcx) && imgPcx.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\hopper_lossless.qoi", 512, imgQoi) && imgQoi.valid;

        ParsedOfficeDocument docXlsx, docDocx, docOds, docOdt, docRtf, docCsv;
        bool okExtDoc =
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\spreadsheet_workbook.xlsx", docXlsx) && docXlsx.valid && !docXlsx.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\word_document.docx", docDocx) && docDocx.valid && !docDocx.paragraphs.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\calc_spreadsheet.ods", docOds) && docOds.valid && !docOds.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\writer_document.odt", docOdt) && docOdt.valid && !docOdt.paragraphs.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\rich_text_notes.rtf", docRtf) && docRtf.valid && !docRtf.paragraphs.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\hardware_telemetry.csv", docCsv) && docCsv.valid && !docCsv.sheets.empty();

        PdfDocumentSummary pdfDoc;
        bool okExtPdf = PdfViewer::InspectPdfFile(tdDir + L"\\minimal_document.pdf", pdfDoc) && pdfDoc.valid && pdfDoc.pageCount >= 1;

        ArchiveSummaryInfo zipArc, tarArc;
        bool okExtArc =
            ArchiveExplorer::InspectArchiveFile(tdDir + L"\\sample_bundle.zip", zipArc) && zipArc.valid && !zipArc.entries.empty() &&
            ArchiveExplorer::InspectArchiveFile(tdDir + L"\\posix_archive.tar", tarArc) && tarArc.valid && !tarArc.entries.empty();

        tc.Record(okExt3D && okExtEda && okExtImg && okExtDoc && okExtPdf && okExtArc,
            L"Stage12.Test10_RealWorld_TestData_Extended27Formats",
            L"3D(obj=" + std::to_wstring(mObj.tris.size()) + L",3ds=" + std::to_wstring(m3ds.tris.size()) +
            L",dae=" + std::to_wstring(mDae.tris.size()) + L",blend=" + std::to_wstring(mBlend.tris.size()) +
            L") EDA(gbr=" + std::to_wstring(gbrBoard.traces.size()) + L",drl=" + std::to_wstring(drlBoard.vias.size()) +
            L") IMG(tga,xcf,hdr,ppm,pgm,pcx,qoi=" + std::to_wstring(okExtImg) +
            L") DOC(xlsx,docx,ods,odt,rtf,csv=" + std::to_wstring(okExtDoc) +
            L") PDF(pages=" + std::to_wstring(pdfDoc.pageCount) +
            L") ARC(zip=" + std::to_wstring(zipArc.entries.size()) + L",tar=" + std::to_wstring(tarArc.entries.size()) + L")");

        // 13. Test11: Next 30 File Formats (Format01 .. Format30 in src/formats/ + 30 separate samples in testdata/)
        ExtendedImageInfo iPam, iPfm, iSgi, iRas, iKtx, iVtf, iFf, iFits;
        bool okNext8Img =
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\netpbm_arbitrary.pam", 512, iPam) && iPam.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\hdr_floatmap.pfm", 512, iPfm) && iPfm.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\silicon_graphics.sgi", 512, iSgi) && iSgi.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\sun_raster.ras", 512, iRas) && iRas.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\khronos_texture.ktx", 512, iKtx) && iKtx.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\source_engine.vtf", 512, iVtf) && iVtf.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\suckless_image.ff", 512, iFf) && iFf.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\astronomy_observation.fits", 512, iFits) && iFits.valid;

        LoadedMesh3D mAmf, mX3d, mWrl, mScad, mPcd, mXyz;
        bool okNext6Mesh =
            Model3DViewer::InspectMeshFile(tdDir + L"\\additive_tetrahedron.amf", mAmf) && mAmf.loadedOk && !mAmf.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\web3d_pyramid.x3d", mX3d) && mX3d.loadedOk && !mX3d.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\virtual_reality.wrl", mWrl) && mWrl.loadedOk && !mWrl.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\parametric_bracket.scad", mScad) && mScad.loadedOk && !mScad.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\lidar_scan.pcd", mPcd) && mPcd.loadedOk && !mPcd.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\molecule_methane.xyz", mXyz) && mXyz.loadedOk && !mXyz.tris.empty();

        ParsedEdaBoard eAsc, eDsn, eD356, eHex, eSrec, eCir;
        bool okNext6Eda =
            EdaViewer::InspectEdaFile(tdDir + L"\\ltspice_filter.asc", eAsc) && eAsc.valid && !eAsc.traces.empty() &&
            EdaViewer::InspectEdaFile(tdDir + L"\\specctra_board.dsn", eDsn) && eDsn.valid && !eDsn.traces.empty() &&
            EdaViewer::InspectEdaFile(tdDir + L"\\bareboard_test.d356", eD356) && eD356.valid && !eD356.pads.empty() &&
            EdaViewer::InspectEdaFile(tdDir + L"\\firmware_cortexm.hex", eHex) && eHex.valid && !eHex.traces.empty() &&
            EdaViewer::InspectEdaFile(tdDir + L"\\bootloader_image.srec", eSrec) && eSrec.valid && !eSrec.traces.empty() &&
            EdaViewer::InspectEdaFile(tdDir + L"\\rlc_filter.cir", eCir) && eCir.valid && !eCir.components.empty();

        ParsedOfficeDocument dParquet, dAvro, dBson, dIcs, dVcf, dSrt, dVtt, dAss, dGpx, dKml;
        bool okNext10Doc =
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\telemetry_columnar.parquet", dParquet) && dParquet.valid && !dParquet.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\device_events.avro", dAvro) && dAvro.valid && !dAvro.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\config_document.bson", dBson) && dBson.valid && !dBson.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\engineering_schedule.ics", dIcs) && dIcs.valid && !dIcs.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\contact_card.vcf", dVcf) && dVcf.valid && !dVcf.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\video_captions.srt", dSrt) && dSrt.valid && !dSrt.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\stream_subtitles.vtt", dVtt) && dVtt.valid && !dVtt.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\styled_subtitles.ass", dAss) && dAss.valid && !dAss.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\gps_telemetry_track.gpx", dGpx) && dGpx.valid && !dGpx.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\geographic_placemarks.kml", dKml) && dKml.valid && !dKml.sheets.empty();

        tc.Record(okNext8Img && okNext6Mesh && okNext6Eda && okNext10Doc,
            L"Stage12.Test11_Next30FileFormats_And_TestData",
            L"IMG8(pam,pfm,sgi,ras,ktx,vtf,ff,fits=" + std::to_wstring(okNext8Img) +
            L") MESH6(amf,x3d,wrl,scad,pcd,xyz=" + std::to_wstring(okNext6Mesh) +
            L") EDA6(asc,dsn,d356,hex,srec,cir=" + std::to_wstring(okNext6Eda) +
            L") DATA10(parquet,avro,bson,ics,vcf,srt,vtt,ass,gpx,kml=" + std::to_wstring(okNext10Doc) + L")");

        // 14. Test12: Next 50 File Formats (12 Image + 12 3D/CAD/GIS + 8 EDA/IC/RF + 18 Data/Audio/Binary in src/formats/ + 50 samples in testdata/)
        ExtendedImageInfo iPkm, iAstc, iPvr, iIlbm, iTim, iWbmp, iBdf, iPsf, iY4m, iAni, iGpl, iDcm;
        bool ok50Img =
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\sample.pkm", 512, iPkm) && iPkm.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\sample.astc", 512, iAstc) && iAstc.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\sample.pvr", 512, iPvr) && iPvr.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\sample.ilbm", 512, iIlbm) && iIlbm.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\sample.tim", 512, iTim) && iTim.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\sample.wbmp", 512, iWbmp) && iWbmp.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\sample.bdf", 512, iBdf) && iBdf.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\sample.psf", 512, iPsf) && iPsf.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\sample.y4m", 512, iY4m) && iY4m.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\sample.ani", 512, iAni) && iAni.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\sample.gpl", 512, iGpl) && iGpl.valid &&
            ImageFormatLoader::DecodeImageFile(tdDir + L"\\sample.dcm", 512, iDcm) && iDcm.valid;

        LoadedMesh3D mStep, mIges, mGcode, mPdb, mMol, mMsh, mBvh, mSmd, mDem, mHgt, mLas, mVox;
        bool ok50Mesh =
            Model3DViewer::InspectMeshFile(tdDir + L"\\sample.step", mStep) && mStep.loadedOk && !mStep.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\sample.iges", mIges) && mIges.loadedOk && !mIges.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\sample.gcode", mGcode) && mGcode.loadedOk && !mGcode.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\sample.pdb", mPdb) && mPdb.loadedOk && !mPdb.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\sample.mol", mMol) && mMol.loadedOk && !mMol.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\sample.msh", mMsh) && mMsh.loadedOk && !mMsh.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\sample.bvh", mBvh) && mBvh.loadedOk && !mBvh.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\sample.smd", mSmd) && mSmd.loadedOk && !mSmd.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\sample.dem", mDem) && mDem.loadedOk && !mDem.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\sample.hgt", mHgt) && mHgt.loadedOk && !mHgt.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\sample.las", mLas) && mLas.loadedOk && !mLas.tris.empty() &&
            Model3DViewer::InspectMeshFile(tdDir + L"\\sample.vox", mVox) && mVox.loadedOk && !mVox.tris.empty();

        ParsedEdaBoard eHpgl, eVcd, eS2p, eLef, eDef, eGds, eBsdl, eSvd;
        bool ok50Eda =
            EdaViewer::InspectEdaFile(tdDir + L"\\sample.hpgl", eHpgl) && eHpgl.valid && !eHpgl.traces.empty() &&
            EdaViewer::InspectEdaFile(tdDir + L"\\sample.vcd", eVcd) && eVcd.valid && !eVcd.traces.empty() &&
            EdaViewer::InspectEdaFile(tdDir + L"\\sample.s2p", eS2p) && eS2p.valid && !eS2p.traces.empty() &&
            EdaViewer::InspectEdaFile(tdDir + L"\\sample.lef", eLef) && eLef.valid && !eLef.pads.empty() &&
            EdaViewer::InspectEdaFile(tdDir + L"\\sample.def", eDef) && eDef.valid && !eDef.components.empty() &&
            EdaViewer::InspectEdaFile(tdDir + L"\\sample.gds", eGds) && eGds.valid && !eGds.traces.empty() &&
            EdaViewer::InspectEdaFile(tdDir + L"\\sample.bsdl", eBsdl) && eBsdl.valid && !eBsdl.pads.empty() &&
            EdaViewer::InspectEdaFile(tdDir + L"\\sample.svd", eSvd) && eSvd.valid && !eSvd.components.empty();

        ParsedOfficeDocument dGeo, dOsm, dTcx, dBib, dRis, dArff, dMtx, dNdj, dHar, dPcap, dTor, dCue, dM3u, dMid, dWav, dEdf, dElf, dWasm;
        bool ok50Doc =
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.geojson", dGeo) && dGeo.valid && !dGeo.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.osm", dOsm) && dOsm.valid && !dOsm.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.tcx", dTcx) && dTcx.valid && !dTcx.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.bib", dBib) && dBib.valid && !dBib.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.ris", dRis) && dRis.valid && !dRis.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.arff", dArff) && dArff.valid && !dArff.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.mtx", dMtx) && dMtx.valid && !dMtx.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.ndjson", dNdj) && dNdj.valid && !dNdj.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.har", dHar) && dHar.valid && !dHar.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.pcap", dPcap) && dPcap.valid && !dPcap.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.torrent", dTor) && dTor.valid && !dTor.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.cue", dCue) && dCue.valid && !dCue.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.m3u8", dM3u) && dM3u.valid && !dM3u.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.mid", dMid) && dMid.valid && !dMid.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.wav", dWav) && dWav.valid && !dWav.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.edf", dEdf) && dEdf.valid && !dEdf.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.elf", dElf) && dElf.valid && !dElf.sheets.empty() &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.wasm", dWasm) && dWasm.valid && !dWasm.sheets.empty();

        ParsedOfficeDocument dNmea, dIgc, dFlac, dAiff;
        bool okGpsMap =
            dGpx.hasGeoTrack && dGpx.geoPoints.size() >= 2 && dGpx.totalDistanceKm > 0.0 &&
            dKml.hasGeoTrack && dKml.geoPoints.size() >= 2 &&
            dGeo.hasGeoTrack && dGeo.geoPoints.size() >= 2 &&
            dOsm.hasGeoTrack && dOsm.geoPoints.size() >= 2 &&
            dTcx.hasGeoTrack && dTcx.geoPoints.size() >= 2 &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.nmea", dNmea) && dNmea.hasGeoTrack && dNmea.geoPoints.size() >= 4 &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.igc", dIgc) && dIgc.hasGeoTrack && dIgc.geoPoints.size() >= 4 &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.flac", dFlac) && dFlac.valid &&
            SpreadsheetDocViewer::InspectOfficeFile(tdDir + L"\\sample.aiff", dAiff) && dAiff.valid;

        tc.Record(ok50Img && ok50Mesh && ok50Eda && ok50Doc && okGpsMap,
            L"Stage12.Test12_Next50FileFormats_And_TestData",
            L"IMG12=" + std::to_wstring(ok50Img) +
            L" MESH12=" + std::to_wstring(ok50Mesh) +
            L" EDA8=" + std::to_wstring(ok50Eda) +
            L" DOC18=" + std::to_wstring(ok50Doc));

        // 15. Test13: 100 New File Formats across Image, Archive, 3D/BIM, EDA/CAN/SPD & Binary/AI/Geo/Media
        {
            // A. ImageFormatLoader (EXR, KTX2, Aseprite, ICNS, JXL, BLP2, DPX, ACO, ASE, Cube 3D LUT, IES, TTF)
            ExtendedImageInfo iExr, iKtx2, iAse, iIcns, iJxl, iBlp, iDpx, iAco, iAsePal, iCube, iIes, iTtf;
            const uint8_t exrBytes[64] = { 0x76, 0x2F, 0x31, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00 };
            const uint8_t ktx2Bytes[80] = { 0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
            uint8_t aseBytes[144] = {};
            aseBytes[4] = 0xE0; aseBytes[5] = 0xA5; aseBytes[6] = 1; aseBytes[8] = 16; aseBytes[10] = 16; aseBytes[12] = 32;
            const uint8_t icnsBytes[16] = { 'i','c','n','s', 0,0,0,16, 'i','c','0','7', 0,0,0,8 };
            const uint8_t jxlBytes[12] = { 0xFF, 0x0A, 0x00, 0x00 };
            uint8_t blpBytes[160] = { 'B','L','P','2', 1,0,0,0, 1,8,0,0, 8,0,0,0, 8,0,0,0 };
            uint8_t dpxBytes[2048] = { 'S','D','P','X' };
            const uint8_t acoBytes[14] = { 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xFF, 0x00, 0x80, 0x00, 0x40, 0x00, 0x00, 0x00 };
            const uint8_t asePalBytes[40] = {
                'A','S','E','F', 0,1,0,0, 0,0,0,1,
                0,1, 0,0,0,22, 0,1, 0,0,
                'R','G','B',' ', 0x3F,0x80,0x00,0x00, 0x3F,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00
            };
            const char* cubeStr = "TITLE \"Test\"\nLUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
            const char* iesStr  = "IESNA:LM-63-2002\nTILT=NONE\n1 1000 1 3 1 1 2 0.5 0.5 0.1\n";
            const uint8_t ttfBytes[16] = { 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 };

            bool ok100Img =
                ImageFormatLoader::DecodeImageMemory(exrBytes, sizeof(exrBytes), L".exr", 256, iExr) && iExr.valid &&
                ImageFormatLoader::DecodeImageMemory(ktx2Bytes, sizeof(ktx2Bytes), L".ktx2", 256, iKtx2) && iKtx2.valid &&
                ImageFormatLoader::DecodeImageMemory(aseBytes, sizeof(aseBytes), L".aseprite", 256, iAse) && iAse.valid &&
                ImageFormatLoader::DecodeImageMemory(icnsBytes, sizeof(icnsBytes), L".icns", 256, iIcns) && iIcns.valid &&
                ImageFormatLoader::DecodeImageMemory(jxlBytes, sizeof(jxlBytes), L".jxl", 256, iJxl) && iJxl.valid &&
                ImageFormatLoader::DecodeImageMemory(blpBytes, sizeof(blpBytes), L".blp", 256, iBlp) && iBlp.valid &&
                ImageFormatLoader::DecodeImageMemory(dpxBytes, sizeof(dpxBytes), L".dpx", 256, iDpx) && iDpx.valid &&
                ImageFormatLoader::DecodeImageMemory(acoBytes, sizeof(acoBytes), L".aco", 256, iAco) && iAco.valid &&
                ImageFormatLoader::DecodeImageMemory(asePalBytes, sizeof(asePalBytes), L".ase", 256, iAsePal) && iAsePal.valid &&
                ImageFormatLoader::DecodeImageMemory(reinterpret_cast<const uint8_t*>(cubeStr), std::strlen(cubeStr), L".cube", 256, iCube) && iCube.valid &&
                ImageFormatLoader::DecodeImageMemory(reinterpret_cast<const uint8_t*>(iesStr), std::strlen(iesStr), L".ies", 256, iIes) && iIes.valid &&
                ImageFormatLoader::DecodeImageMemory(ttfBytes, sizeof(ttfBytes), L".ttf", 256, iTtf) && iTtf.valid;

            // B. ArchiveExplorer (PAK, WAD, ASAR, ZST + Universal Stored-Slice Extraction)
            CreateDirectoryW(fmtDir.c_str(), nullptr);
            std::wstring pakPath = fmtDir + L"\\test100.pak";
            std::vector<uint8_t> pakBuf(12 + 64 + 8, 0);
            std::memcpy(pakBuf.data(), "PACK", 4);
            uint32_t pakDirOff = 20, pakDirLen = 64;
            std::memcpy(pakBuf.data() + 4, &pakDirOff, 4);
            std::memcpy(pakBuf.data() + 8, &pakDirLen, 4);
            std::memcpy(pakBuf.data() + 12, "HELLO123", 8);
            std::memcpy(pakBuf.data() + 20, "maps/e1m1.bsp", 13);
            uint32_t fOff = 12, fLen = 8;
            std::memcpy(pakBuf.data() + 20 + 56, &fOff, 4);
            std::memcpy(pakBuf.data() + 20 + 60, &fLen, 4);
            PathUtils::WriteFileBytes(pakPath, pakBuf.data(), pakBuf.size());

            std::wstring wadPath = fmtDir + L"\\test100.wad";
            std::vector<uint8_t> wadBuf(12 + 4 + 16, 0);
            std::memcpy(wadBuf.data(), "IWAD", 4);
            uint32_t wadNum = 1, wadTblOff = 16, lumpPos = 12, lumpSz = 4;
            std::memcpy(wadBuf.data() + 4, &wadNum, 4);
            std::memcpy(wadBuf.data() + 8, &wadTblOff, 4);
            std::memcpy(wadBuf.data() + 12, "DOOM", 4);
            std::memcpy(wadBuf.data() + 16, &lumpPos, 4);
            std::memcpy(wadBuf.data() + 20, &lumpSz, 4);
            std::memcpy(wadBuf.data() + 24, "PLAYPAL\0", 8);
            PathUtils::WriteFileBytes(wadPath, wadBuf.data(), wadBuf.size());

            ArchiveSummaryInfo pakInfo, wadInfo;
            std::vector<uint8_t> pakExtracted, wadExtracted;
            bool ok100Arc =
                ArchiveExplorer::InspectArchiveFile(pakPath, pakInfo) && pakInfo.valid && pakInfo.entries.size() == 1 &&
                ArchiveExplorer::ExtractSingleEntryToMemory(pakPath, L"maps/e1m1.bsp", pakExtracted) &&
                pakExtracted.size() == 8 && std::memcmp(pakExtracted.data(), "HELLO123", 8) == 0 &&
                ArchiveExplorer::InspectArchiveFile(wadPath, wadInfo) && wadInfo.valid && wadInfo.entries.size() == 1 &&
                ArchiveExplorer::ExtractSingleEntryToMemory(wadPath, L"PLAYPAL.lmp", wadExtracted) &&
                wadExtracted.size() == 4 && std::memcmp(wadExtracted.data(), "DOOM", 4) == 0;
            DeleteFileW(pakPath.c_str());
            DeleteFileW(wadPath.c_str());

            // C. Model3DViewer (.ifc, .usda, .gltf with embedded base64 buffer)
            std::wstring ifcPath = fmtDir + L"\\test100.ifc";
            const char* ifcTxt = "ISO-10303-21;\nDATA;\n#1=IFCCARTESIANPOINT((0.,0.,0.));\n#2=IFCCARTESIANPOINT((10.,0.,0.));\n#3=IFCCARTESIANPOINT((10.,10.,5.));\n#4=IFCPOLYLOOP((#1,#2,#3));\nENDSEC;\n";
            PathUtils::WriteFileBytes(ifcPath, ifcTxt, std::strlen(ifcTxt));
            std::wstring usdaPath = fmtDir + L"\\test100.usda";
            const char* usdaTxt = "#usda 1.0\ndef Mesh \"Cube\" {\n point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0)]\n int[] faceVertexCounts = [4]\n int[] faceVertexIndices = [0, 1, 2, 3]\n}\n";
            PathUtils::WriteFileBytes(usdaPath, usdaTxt, std::strlen(usdaTxt));
            LoadedMesh3D mIfc, mUsda;
            bool ok100Mesh =
                Model3DViewer::InspectMeshFile(ifcPath, mIfc) && mIfc.loadedOk && !mIfc.tris.empty() &&
                Model3DViewer::InspectMeshFile(usdaPath, mUsda) && mUsda.loadedOk && mUsda.tris.size() == 2;
            DeleteFileW(ifcPath.c_str());
            DeleteFileW(usdaPath.c_str());

            // D. EdaViewer (.dbc, .spd 3D DIMM, .edid, CAN .asc)
            std::wstring dbcPath = fmtDir + L"\\test100.dbc";
            const char* dbcTxt = "VERSION \"\"\nBO_ 256 EngineData: 8 ECU1\n SG_ RPM : 0|16@1+ (0.25,0) [0|8000] \"rpm\" Vector__XXX\n";
            PathUtils::WriteFileBytes(dbcPath, dbcTxt, std::strlen(dbcTxt));
            std::wstring spdPath = fmtDir + L"\\test100.spd";
            std::vector<uint8_t> spdBuf(512, 0);
            spdBuf[2] = 0x12; // DDR5
            spdBuf[4] = 0x04;
            PathUtils::WriteFileBytes(spdPath, spdBuf.data(), spdBuf.size());
            std::wstring canAscPath = fmtDir + L"\\test100_can.asc";
            const char* canAscTxt = "date Mon Nov 10 02:00:00 2026\nbase hex timestamps absolute\n0.001000 1 100 Rx d 8 11 22 33 44 55 66 77 88\n";
            PathUtils::WriteFileBytes(canAscPath, canAscTxt, std::strlen(canAscTxt));
            ParsedEdaBoard eDbc, eSpd, eCanAsc;
            bool ok100Eda =
                EdaViewer::InspectEdaFile(dbcPath, eDbc) && eDbc.valid && !eDbc.components.empty() &&
                EdaViewer::InspectEdaFile(spdPath, eSpd) && eSpd.valid && eSpd.components.size() >= 8 &&
                EdaViewer::InspectEdaFile(canAscPath, eCanAsc) && eCanAsc.valid && !eCanAsc.traces.empty();
            DeleteFileW(dbcPath.c_str());
            DeleteFileW(spdPath.c_str());
            DeleteFileW(canAscPath.c_str());

            // E. SpreadsheetDocViewer (.exe PE, .uf2, .npy, .ipynb, .shp GeoTrack, .eml, .mp3 ID3v2)
            std::wstring uf2Path = fmtDir + L"\\test100.uf2";
            std::vector<uint8_t> uf2Buf(512, 0);
            std::memcpy(uf2Buf.data(), "UF2\n", 4);
            PathUtils::WriteFileBytes(uf2Path, uf2Buf.data(), uf2Buf.size());

            std::wstring npyPath = fmtDir + L"\\test100.npy";
            std::vector<uint8_t> npyBuf = { 0x93,'N','U','M','P','Y', 1, 0, 16, 0, '{','\'','d','e','s','c','r','\'',':','\'','<','f','4','\'','}',' ', 0,0,0x80,0x3F };
            PathUtils::WriteFileBytes(npyPath, npyBuf.data(), npyBuf.size());

            std::wstring shpPath = fmtDir + L"\\test100.shp";
            std::vector<uint8_t> shpBuf(100, 0);
            shpBuf[0] = 0x00; shpBuf[1] = 0x00; shpBuf[2] = 0x27; shpBuf[3] = 0x0A; // 9994 BE
            double minX = 20.9, minY = 52.1, maxX = 21.1, maxY = 52.3;
            std::memcpy(shpBuf.data() + 36, &minX, 8);
            std::memcpy(shpBuf.data() + 44, &minY, 8);
            std::memcpy(shpBuf.data() + 52, &maxX, 8);
            std::memcpy(shpBuf.data() + 60, &maxY, 8);
            PathUtils::WriteFileBytes(shpPath, shpBuf.data(), shpBuf.size());

            std::wstring ipynbPath = fmtDir + L"\\test100.ipynb";
            const char* ipynbTxt = "{\"nbformat\":4,\"nbformat_minor\":5,\"cells\":[{\"cell_type\":\"code\",\"source\":[\"import torch\\n\"]}]}";
            PathUtils::WriteFileBytes(ipynbPath, ipynbTxt, std::strlen(ipynbTxt));

            std::wstring emlPath = fmtDir + L"\\test100.eml";
            const char* emlTxt = "From: alice@example.com\r\nTo: bob@example.com\r\nSubject: Explorer2 Test\r\n\r\nHello from EML parser!\r\n";
            PathUtils::WriteFileBytes(emlPath, emlTxt, std::strlen(emlTxt));

            ParsedOfficeDocument dUf2, dNpy, dShp, dIpynb, dEml, dExe;
            bool ok100Doc =
                SpreadsheetDocViewer::InspectOfficeFile(uf2Path, dUf2) && dUf2.valid && !dUf2.sheets.empty() &&
                SpreadsheetDocViewer::InspectOfficeFile(npyPath, dNpy) && dNpy.valid && !dNpy.sheets.empty() &&
                SpreadsheetDocViewer::InspectOfficeFile(shpPath, dShp) && dShp.valid && dShp.hasGeoTrack &&
                SpreadsheetDocViewer::InspectOfficeFile(ipynbPath, dIpynb) && dIpynb.valid && !dIpynb.paragraphs.empty() &&
                SpreadsheetDocViewer::InspectOfficeFile(emlPath, dEml) && dEml.valid && !dEml.paragraphs.empty() &&
                SpreadsheetDocViewer::InspectOfficeFile(exePathBuf, dExe) && dExe.valid && !dExe.sheets.empty();
            DeleteFileW(uf2Path.c_str());
            DeleteFileW(npyPath.c_str());
            DeleteFileW(shpPath.c_str());
            DeleteFileW(ipynbPath.c_str());
            DeleteFileW(emlPath.c_str());
            RemoveDirectoryW(fmtDir.c_str());

            tc.Record(ok100Img && ok100Arc && ok100Mesh && ok100Eda && ok100Doc,
                L"Stage12.Test13_Next100FileFormats_And_ExtendedFormats",
                L"IMG=" + std::to_wstring(ok100Img) +
                L" ARC=" + std::to_wstring(ok100Arc) +
                L" MESH=" + std::to_wstring(ok100Mesh) +
                L" EDA=" + std::to_wstring(ok100Eda) +
                L" DOC=" + std::to_wstring(ok100Doc));
        }
    }
}

// ============================================================================
// STAGE 13 TESTS: Ultra-Fast CopyHandler Transfer Engine (Queue, Double-Buffer, .e2part Resume, Pause/Resume, XXH64)
// ============================================================================
// Test clean-up that does not depend on the engine under test. It uses extended-length paths so it can
// remove the long and oddly named items the delete tests create, and it removes a junction as a link
// instead of walking into it.
static void RemoveTestTreeRecursive(const std::wstring& dir) {
    const std::wstring ext = (dir.rfind(L"\\\\?\\", 0) == 0) ? dir : (L"\\\\?\\" + dir);
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((ext + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            std::wstring p = ext + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                    RemoveDirectoryW(p.c_str());
                } else {
                    RemoveTestTreeRecursive(p);
                }
            } else {
                SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(p.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    SetFileAttributesW(ext.c_str(), FILE_ATTRIBUTE_NORMAL);
    RemoveDirectoryW(ext.c_str());
}

static bool FindJobSnapshot(TransferManager& tm, int jobId, TransferJobSnapshot& out) {
    for (auto& s : tm.SnapshotJobs()) {
        if (s.id == jobId) {
            out = std::move(s);
            return true;
        }
    }
    return false;
}

// Makes linkDir a junction that points at targetDir (an absolute path). No administrator rights needed.
static bool CreateTestJunction(const std::wstring& linkDir, const std::wstring& targetDir) {
    if (!CreateDirectoryW(linkDir.c_str(), nullptr)) return false;
    HANDLE h = CreateFileW(linkDir.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        RemoveDirectoryW(linkDir.c_str());
        return false;
    }

    // REPARSE_DATA_BUFFER for a mount point: 8-byte header, four 16-bit name fields, then both names
    const std::wstring subst = L"\\??\\" + targetDir;
    const std::wstring print = targetDir;
    const USHORT substBytes = static_cast<USHORT>(subst.size() * sizeof(wchar_t));
    const USHORT printBytes = static_cast<USHORT>(print.size() * sizeof(wchar_t));
    const USHORT pathBytes = static_cast<USHORT>(substBytes + sizeof(wchar_t) + printBytes + sizeof(wchar_t));

    std::vector<BYTE> buf(static_cast<size_t>(16) + pathBytes, 0);
    auto put16 = [&](size_t off, USHORT v) { memcpy(buf.data() + off, &v, sizeof(v)); };
    const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
    memcpy(buf.data(), &tag, sizeof(tag));
    put16(4, static_cast<USHORT>(8 + pathBytes));                       // ReparseDataLength
    put16(8, 0);                                                        // SubstituteNameOffset
    put16(10, substBytes);                                              // SubstituteNameLength
    put16(12, static_cast<USHORT>(substBytes + sizeof(wchar_t)));       // PrintNameOffset
    put16(14, printBytes);                                              // PrintNameLength
    memcpy(buf.data() + 16, subst.data(), substBytes);
    memcpy(buf.data() + 16 + substBytes + sizeof(wchar_t), print.data(), printBytes);

    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, buf.data(), static_cast<DWORD>(buf.size()),
                                    nullptr, 0, &returned, nullptr);
    CloseHandle(h);
    if (!ok) RemoveDirectoryW(linkDir.c_str());
    return ok != FALSE;
}

// Looks up one original path in the current user's Recycle Bin on that path's drive (Windows 10+ record
// layout). On success returns the bin's record file ($I...) and data file ($R...).
static bool FindRecycleBinRecord(const std::wstring& originalPath, std::wstring& outInfoFile, std::wstring& outDataFile) {
    if (originalPath.size() < 3 || originalPath[1] != L':') return false;

    std::wstring sidStr;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        BYTE tokenBuf[256] = {};
        DWORD len = 0;
        if (GetTokenInformation(token, TokenUser, tokenBuf, sizeof(tokenBuf), &len)) {
            LPWSTR sidText = nullptr;
            if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(tokenBuf)->User.Sid, &sidText) && sidText) {
                sidStr = sidText;
                LocalFree(sidText);
            }
        }
        CloseHandle(token);
    }
    if (sidStr.empty()) return false;

    const std::wstring binDir = originalPath.substr(0, 2) + L"\\$Recycle.Bin\\" + sidStr;
    const std::wstring wantKey = PathUtils::NormalizePathKey(originalPath);
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((binDir + L"\\$I*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;

    bool found = false;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::wstring infoPath = binDir + L"\\" + fd.cFileName;
        std::vector<uint8_t> raw;
        if (!PathUtils::ReadFileBytes(infoPath, 8192, raw) || raw.size() < 30) continue;
        uint64_t version = 0;
        memcpy(&version, raw.data(), sizeof(version));
        if (version != 2) continue;
        uint32_t chars = 0;
        memcpy(&chars, raw.data() + 24, sizeof(chars));
        if (chars == 0 || 28 + static_cast<size_t>(chars) * sizeof(wchar_t) > raw.size()) continue;
        std::wstring recorded(chars, L'\0');
        memcpy(recorded.data(), raw.data() + 28, static_cast<size_t>(chars) * sizeof(wchar_t));
        while (!recorded.empty() && recorded.back() == L'\0') recorded.pop_back();
        if (PathUtils::NormalizePathKey(recorded) == wantKey) {
            outInfoFile = infoPath;
            outDataFile = binDir + L"\\$R" + (fd.cFileName + 2);
            found = true;
            break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

static std::vector<uint8_t> BuildStoreZipForTest(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files);

// Writes a PDF of `pages` pages (200 x 200 pt, a filled square on each) with a correct cross-reference
// table, which Windows.Data.Pdf accepts and renders
static bool WriteMultiPagePdfForTest(const std::wstring& path, int pages) {
    std::string pdf = "%PDF-1.4\n";
    std::vector<size_t> offsets;
    auto addObject = [&](const std::string& body) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
    };
    addObject("<< /Type /Catalog /Pages 2 0 R >>");
    std::string kids;
    for (int i = 0; i < pages; ++i) kids += std::to_string(3 + i * 2) + " 0 R ";
    addObject("<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pages) + " >>");
    for (int i = 0; i < pages; ++i) {
        addObject("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents " + std::to_string(4 + i * 2) + " 0 R >>");
        const std::string content = "0.1 0.4 0.8 rg " + std::to_string(20 + i * 10) + " 20 120 120 re f";
        addObject("<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream");
    }
    const size_t xrefPos = pdf.size();
    pdf += "xref\n0 " + std::to_string(offsets.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t off : offsets) {
        char line[32] = {};
        sprintf_s(line, "%010zu 00000 n \n", off);
        pdf += line;
    }
    pdf += "trailer\n<< /Size " + std::to_string(offsets.size() + 1) + " /Root 1 0 R >>\nstartxref\n" +
           std::to_string(xrefPos) + "\n%%EOF\n";
    return PathUtils::WriteFileBytes(path, pdf.data(), pdf.size());
}

// A list entry for a file or folder, as the file list hands it to the preview pane
static FileEntry MakePreviewTestEntry(const std::wstring& path, bool isDirectory) {
    FileEntry e;
    e.fullPath = path;
    e.name = PathUtils::GetFileName(path);
    e.displayName = e.name;
    e.isDirectory = isDirectory;
    if (!isDirectory) {
        e.ext = PathUtils::GetExtension(path);
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
            e.size = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        }
        e.sizeStr = FileEngine::FormatSizeKB(e.size);
    }
    return e;
}

// Waits for the preview worker's message to `window` and hands it to the pane, as the main window does.
// True when the pane took a result from it.
static bool WaitForPreviewResult(FileContentPane& pane, HWND window, DWORD timeoutMs) {
    const ULONGLONG until = GetTickCount64() + timeoutMs;
    for (;;) {
        MSG msg = {};
        while (PeekMessageW(&msg, window, WM_APP_PREVIEW_READY, WM_APP_PREVIEW_READY, PM_REMOVE)) {
            if (pane.OnPreviewLoaded()) return true;
        }
        if (GetTickCount64() >= until) return false;
        Sleep(5);
    }
}

// Hands every message the worker has posted so far to the pane. True when one of them brought a result.
static bool TakeLatePreviewResults(FileContentPane& pane, HWND window) {
    bool took = false;
    MSG msg = {};
    while (PeekMessageW(&msg, window, WM_APP_PREVIEW_READY, WM_APP_PREVIEW_READY, PM_REMOVE)) {
        took = pane.OnPreviewLoaded() || took;
    }
    return took;
}

static HWND CreatePreviewTestWindow() {
    return CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

static void RunStage13Tests(TestCounter& tc) {
    std::wstring baseDir = GetScratchTestDir() + L"\\Stage13_Transfers";
    RemoveTestTreeRecursive(baseDir);
    CreateDirectoryW(baseDir.c_str(), nullptr);

    TransferManager tm;
    tm.Initialize(nullptr);

    // Test 13.1: Deep folder tree + 9 MB Double-Buffered Ring + Multi-Worker Pool + Streamed XXH64 Verify
    {
        std::wstring srcRoot = baseDir + L"\\t1_src";
        std::wstring dstRoot = baseDir + L"\\t1_dst";
        CreateDirectoryW(srcRoot.c_str(), nullptr);
        CreateDirectoryW((srcRoot + L"\\subA").c_str(), nullptr);
        CreateDirectoryW((srcRoot + L"\\subA\\nested").c_str(), nullptr);
        CreateDirectoryW(dstRoot.c_str(), nullptr);

        std::string small1 = "Explorer2 CopyHandler Small File #1";
        std::vector<uint8_t> small2(65536);
        for (size_t i = 0; i < small2.size(); ++i) small2[i] = static_cast<uint8_t>((i * 37 + 11) & 0xFF);

        // 9 MB file (> 8 MB threshold) triggers 2-slot double-buffered async VirtualAlloc ring!
        std::vector<uint8_t> big9mb(9 * 1024 * 1024 + 12345);
        for (size_t i = 0; i < big9mb.size(); ++i) big9mb[i] = static_cast<uint8_t>((i * 131 + (i >> 9)) & 0xFF);

        PathUtils::WriteFileBytes(srcRoot + L"\\subA\\nested\\small1.txt", small1.data(), small1.size());
        PathUtils::WriteFileBytes(srcRoot + L"\\small2.bin", small2.data(), small2.size());
        PathUtils::WriteFileBytes(srcRoot + L"\\subA\\big_9mb.bin", big9mb.data(), big9mb.size());

        TransferConfig cfg;
        cfg.verifyMode = TransferVerifyMode::XxHash64;
        cfg.workerThreads = 4;
        cfg.usePartFiles = true;

        int jid = tm.EnqueueTransfer({ srcRoot }, dstRoot, TransferOp::Copy, &cfg);
        bool waited = tm.WaitForJob(jid, 30000);

        uint64_t hMem = TransferManager::ComputeXxHash64Memory(big9mb.data(), big9mb.size());
        uint64_t hDisk = 0;
        bool hashOk = TransferManager::ComputeXxHash64File(dstRoot + L"\\t1_src\\subA\\big_9mb.bin", hDisk) && (hMem == hDisk);

        bool snapOk = false;
        for (const auto& s : tm.SnapshotJobs()) {
            if (s.id == jid) {
                snapOk = (s.state == TransferJobState::Completed && s.doneFiles == 3 && s.verifiedFiles == 3 && s.errors.empty());
            }
        }
        tc.Record(waited && snapOk && hashOk, L"Stage13.Test1_DoubleBuffer9MB_MultiWorker_And_XXH64Verify");
    }

    // Test 13.2: Crash / Network-Drop Byte-Offset Partial Resume via .e2part
    {
        std::wstring srcDir = baseDir + L"\\t2_src";
        std::wstring dstDir = baseDir + L"\\t2_dst";
        CreateDirectoryW(srcDir.c_str(), nullptr);
        CreateDirectoryW(dstDir.c_str(), nullptr);

        std::vector<uint8_t> fullData(2 * 1024 * 1024 + 777);
        for (size_t i = 0; i < fullData.size(); ++i) fullData[i] = static_cast<uint8_t>((i * 71 + (i >> 7)) & 0xFF);

        std::wstring srcFile = srcDir + L"\\movie_chunk.bin";
        std::wstring dstFile = dstDir + L"\\movie_chunk.bin";
        std::wstring partFile = dstFile + L".e2part";
        PathUtils::WriteFileBytes(srcFile, fullData.data(), fullData.size());

        // Read source LastWriteTime and tag pre-existing 768 KB .e2part with matching CreationTime
        WIN32_FILE_ATTRIBUTE_DATA srcFad = {};
        GetFileAttributesExW(srcFile.c_str(), GetFileExInfoStandard, &srcFad);

        const size_t preBytes = 768 * 1024;
        PathUtils::WriteFileBytes(partFile, fullData.data(), preBytes);
        HANDLE hPart = CreateFileW(partFile.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hPart != INVALID_HANDLE_VALUE) {
            SetFileTime(hPart, &srcFad.ftLastWriteTime, nullptr, nullptr);
            CloseHandle(hPart);
        }

        TransferConfig cfg;
        cfg.usePartFiles = true;
        cfg.conflictPolicy = TransferConflictPolicy::ResumePartial;
        cfg.verifyMode = TransferVerifyMode::XxHash64;

        int jid = tm.EnqueueTransfer({ srcFile }, dstDir, TransferOp::Copy, &cfg);
        bool waited = tm.WaitForJob(jid, 20000);

        uint64_t hExpected = TransferManager::ComputeXxHash64Memory(fullData.data(), fullData.size());
        uint64_t hActual = 0;
        bool fileOk = (GetFileAttributesW(partFile.c_str()) == INVALID_FILE_ATTRIBUTES) &&
                      TransferManager::ComputeXxHash64File(dstFile, hActual) && (hActual == hExpected);
        bool resumedFlag = false;
        for (const auto& s : tm.SnapshotJobs()) {
            if (s.id == jid) {
                resumedFlag = (s.state == TransferJobState::Completed && s.resumedFiles == 1 && s.errors.empty());
            }
        }
        tc.Record(waited && fileOk && resumedFlag, L"Stage13.Test2_ByteOffsetResume_PartFileRecovery");
    }

    // Test 13.3: Live Pause & Resume + Token-Bucket Speed Limiter
    {
        std::wstring srcDir = baseDir + L"\\t3_src";
        std::wstring dstDir = baseDir + L"\\t3_dst";
        CreateDirectoryW(srcDir.c_str(), nullptr);
        CreateDirectoryW(dstDir.c_str(), nullptr);

        std::vector<uint8_t> payload(6 * 1024 * 1024, 0x5A);
        std::wstring srcFile = srcDir + L"\\throttled.bin";
        PathUtils::WriteFileBytes(srcFile, payload.data(), payload.size());

        TransferConfig cfg;
        cfg.speedLimitMBps = 25; // ~240 ms transfer window so Pause/Resume hits live I/O
        cfg.bufferSizeKB = 1024;
        cfg.verifyMode = TransferVerifyMode::XxHash64;

        int jid = tm.EnqueueTransfer({ srcFile }, dstDir, TransferOp::Copy, &cfg);
        Sleep(25);
        tm.PauseJob(jid);
        Sleep(35);
        tm.ResumeJob(jid);
        bool waited = tm.WaitForJob(jid, 20000);

        uint64_t hSrc = 0, hDst = 0;
        bool ok = waited &&
                  TransferManager::ComputeXxHash64File(srcFile, hSrc) &&
                  TransferManager::ComputeXxHash64File(dstDir + L"\\throttled.bin", hDst) &&
                  (hSrc == hDst);
        tc.Record(ok, L"Stage13.Test3_PauseResume_And_SpeedLimiter");
    }

    // Test 13.4: Conflict Policies (RenameCollision "name (2).ext", SkipExisting, OverwriteAll)
    {
        std::wstring srcDir = baseDir + L"\\t4_src";
        std::wstring dstDir = baseDir + L"\\t4_dst";
        CreateDirectoryW(srcDir.c_str(), nullptr);
        CreateDirectoryW(dstDir.c_str(), nullptr);

        std::string v1 = "ORIGINAL_VERSION_1";
        std::string v2 = "UPDATED_VERSION_2_LONGER";
        PathUtils::WriteFileBytes(dstDir + L"\\doc.txt", v1.data(), v1.size());
        PathUtils::WriteFileBytes(srcDir + L"\\doc.txt", v2.data(), v2.size());

        // 1) RenameCollision -> creates "doc (2).txt" and keeps "doc.txt" untouched
        TransferConfig cfgRen;
        cfgRen.conflictPolicy = TransferConflictPolicy::RenameCollision;
        int j1 = tm.EnqueueTransfer({ srcDir + L"\\doc.txt" }, dstDir, TransferOp::Copy, &cfgRen);
        tm.WaitForJob(j1, 10000);

        std::vector<uint8_t> rbOrig, rbRen;
        bool okRen = PathUtils::ReadFileBytes(dstDir + L"\\doc.txt", 1024, rbOrig) &&
                     PathUtils::ReadFileBytes(dstDir + L"\\doc (2).txt", 1024, rbRen) &&
                     rbOrig.size() == v1.size() && rbRen.size() == v2.size();

        // 2) SkipExisting -> skips "doc.txt"
        TransferConfig cfgSkip;
        cfgSkip.conflictPolicy = TransferConflictPolicy::SkipExisting;
        int j2 = tm.EnqueueTransfer({ srcDir + L"\\doc.txt" }, dstDir, TransferOp::Copy, &cfgSkip);
        tm.WaitForJob(j2, 10000);
        std::vector<uint8_t> rbAfterSkip;
        bool okSkip = PathUtils::ReadFileBytes(dstDir + L"\\doc.txt", 1024, rbAfterSkip) && rbAfterSkip.size() == v1.size();

        // 3) OverwriteAll -> overwrites "doc.txt" with v2
        TransferConfig cfgOvr;
        cfgOvr.conflictPolicy = TransferConflictPolicy::OverwriteAll;
        int j3 = tm.EnqueueTransfer({ srcDir + L"\\doc.txt" }, dstDir, TransferOp::Copy, &cfgOvr);
        tm.WaitForJob(j3, 10000);
        std::vector<uint8_t> rbAfterOvr;
        bool okOvr = PathUtils::ReadFileBytes(dstDir + L"\\doc.txt", 1024, rbAfterOvr) && rbAfterOvr.size() == v2.size();

        tc.Record(okRen && okSkip && okOvr, L"Stage13.Test4_ConflictPolicies_Rename_Skip_Overwrite");
    }

    // Test 13.5: Same-Volume O(1) Instant Move & Source Cleanup
    {
        std::wstring moveSrc = baseDir + L"\\t5_move_folder";
        std::wstring moveDst = baseDir + L"\\t5_target";
        CreateDirectoryW(moveSrc.c_str(), nullptr);
        CreateDirectoryW((moveSrc + L"\\deep").c_str(), nullptr);
        CreateDirectoryW(moveDst.c_str(), nullptr);

        std::string msg = "INSTANT_O1_MOVE_PAYLOAD";
        PathUtils::WriteFileBytes(moveSrc + L"\\deep\\item.dat", msg.data(), msg.size());

        int jid = tm.EnqueueTransfer({ moveSrc }, moveDst, TransferOp::Move, nullptr);
        bool waited = tm.WaitForJob(jid, 10000);

        std::vector<uint8_t> movedBytes;
        bool srcGone = (GetFileAttributesW(moveSrc.c_str()) == INVALID_FILE_ATTRIBUTES);
        bool dstHere = PathUtils::ReadFileBytes(moveDst + L"\\t5_move_folder\\deep\\item.dat", 1024, movedBytes) &&
                       movedBytes.size() == msg.size() &&
                       std::memcmp(movedBytes.data(), msg.data(), msg.size()) == 0;
        tc.Record(waited && srcGone && dstHere, L"Stage13.Test5_SameVolume_InstantMove_And_Cleanup");
    }

    // Test 13.6: Multi-Worker Concurrent Intra-Batch Duplicate Filenames (e.g. Search Results -> Single Target Folder)
    {
        std::wstring dA = baseDir + L"\\t6_a", dB = baseDir + L"\\t6_b", dC = baseDir + L"\\t6_c", dOut = baseDir + L"\\t6_out";
        CreateDirectoryW(dA.c_str(), nullptr);
        CreateDirectoryW(dB.c_str(), nullptr);
        CreateDirectoryW(dC.c_str(), nullptr);
        CreateDirectoryW(dOut.c_str(), nullptr);

        std::string pA(8192, 'A'), pB(8192, 'B'), pC(8192, 'C');
        PathUtils::WriteFileBytes(dA + L"\\dup.bin", pA.data(), pA.size());
        PathUtils::WriteFileBytes(dB + L"\\dup.bin", pB.data(), pB.size());
        PathUtils::WriteFileBytes(dC + L"\\dup.bin", pC.data(), pC.size());

        TransferConfig cfgDup;
        cfgDup.workerThreads = 4;
        cfgDup.conflictPolicy = TransferConflictPolicy::RenameCollision;
        cfgDup.verifyMode = TransferVerifyMode::XxHash64;
        int jid = tm.EnqueueTransfer({ dA + L"\\dup.bin", dB + L"\\dup.bin", dC + L"\\dup.bin" }, dOut, TransferOp::Copy, &cfgDup);
        bool waited = tm.WaitForJob(jid, 10000);

        std::vector<uint8_t> r1, r2, r3;
        bool allThreeExist = PathUtils::ReadFileBytes(dOut + L"\\dup.bin", 16384, r1) &&
                             PathUtils::ReadFileBytes(dOut + L"\\dup (2).bin", 16384, r2) &&
                             PathUtils::ReadFileBytes(dOut + L"\\dup (3).bin", 16384, r3) &&
                             r1.size() == 8192 && r2.size() == 8192 && r3.size() == 8192 &&
                             (r1[0] + r2[0] + r3[0]) == ('A' + 'B' + 'C');
        tc.Record(waited && allThreeExist, L"Stage13.Test6_MultiWorker_IntraBatch_DuplicateNames");
    }

    // Test 13.7: Partial .e2part Byte-Offset Resume under VerifyMode::None + FileAllocationInfo Pre-Allocation
    {
        std::wstring rSrc = baseDir + L"\\t7_src", rDst = baseDir + L"\\t7_dst";
        CreateDirectoryW(rSrc.c_str(), nullptr);
        CreateDirectoryW(rDst.c_str(), nullptr);

        std::vector<uint8_t> fullData(512 * 1024);
        for (size_t i = 0; i < fullData.size(); ++i) fullData[i] = static_cast<uint8_t>((i * 131u + 17u) & 0xFFu);
        std::wstring srcFile = rSrc + L"\\big.dat";
        std::wstring dstFile = rDst + L"\\big.dat";
        std::wstring partFile = dstFile + L".e2part";
        PathUtils::WriteFileBytes(srcFile, fullData.data(), fullData.size());
        PathUtils::WriteFileBytes(partFile, fullData.data(), 200 * 1024);

        WIN32_FILE_ATTRIBUTE_DATA srcFad = {};
        GetFileAttributesExW(srcFile.c_str(), GetFileExInfoStandard, &srcFad);
        HANDLE hPart = CreateFileW(partFile.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hPart != INVALID_HANDLE_VALUE) {
            FILE_BASIC_INFO bi = {};
            bi.CreationTime.LowPart = srcFad.ftLastWriteTime.dwLowDateTime;
            bi.CreationTime.HighPart = static_cast<LONG>(srcFad.ftLastWriteTime.dwHighDateTime);
            SetFileInformationByHandle(hPart, FileBasicInfo, &bi, sizeof(bi));
            CloseHandle(hPart);
        }

        TransferConfig cfgRes;
        cfgRes.usePartFiles = true;
        cfgRes.verifyMode = TransferVerifyMode::None;
        cfgRes.conflictPolicy = TransferConflictPolicy::ResumePartial;
        int jid = tm.EnqueueTransfer({ srcFile }, rDst, TransferOp::Copy, &cfgRes);
        bool waited = tm.WaitForJob(jid, 10000);

        uint64_t hExpected = TransferManager::ComputeXxHash64Memory(fullData.data(), fullData.size());
        uint64_t hActual = 0;
        bool hashMatch = TransferManager::ComputeXxHash64File(dstFile, hActual) && (hActual == hExpected);
        bool noPartLeft = (GetFileAttributesW(partFile.c_str()) == INVALID_FILE_ATTRIBUTES);
        tc.Record(waited && hashMatch && noPartLeft, L"Stage13.Test7_PartFile_ByteOffsetResume_And_CleanCommit");
    }

    // Test 13.8: Non-Zero Windows Shell Icon Indices for .exe and .dll (No Blank White Sheet Regression)
    {
        std::wstring icDir = baseDir + L"\\t8_icons";
        CreateDirectoryW(icDir.c_str(), nullptr);
        const char dummyMz[] = "MZ_DUMMY_PE";
        PathUtils::WriteFileBytes(icDir + L"\\DestructionBasicFirst.exe", dummyMz, sizeof(dummyMz));
        PathUtils::WriteFileBytes(icDir + L"\\SDL2.dll", dummyMz, sizeof(dummyMz));

        FileEngine fe;
        fe.Initialize(nullptr);
        auto entries = fe.EnumerateDirectory(icDir, true, true, Lang::PL);
        fe.WaitUntilIconsResolved(15000);       // icons of types not seen before come from the icon worker
        fe.RefreshIconIndices(entries);
        fe.Shutdown();

        int exeIcon = -1, dllIcon = -1;
        for (const auto& e : entries) {
            if (e.ext == L".exe") exeIcon = e.sysIconIndex;
            if (e.ext == L".dll") dllIcon = e.sysIconIndex;
        }
        tc.Record(exeIcon > 0 && dllIcon > 0,
            L"Stage13.Test8_ExeAndDll_RealWindowsSystemIcons",
            L"exeIcon=" + std::to_wstring(exeIcon) + L", dllIcon=" + std::to_wstring(dllIcon));
    }

    // Test 13.9: High-Speed Fresh-Tree Small-File IOPS + 4-Slot Persistent Async Ring + Work-Stealing Benchmark
    {
        std::wstring bSmallSrc = baseDir + L"\\t9_small_src";
        std::wstring bSmallDst = baseDir + L"\\t9_small_dst";
        CreateDirectoryW(bSmallSrc.c_str(), nullptr);

        std::string smallPayload(4096, 'S');
        for (int sub = 0; sub < 8; ++sub) {
            std::wstring subDir = bSmallSrc + L"\\sub_" + std::to_wstring(sub);
            CreateDirectoryW(subDir.c_str(), nullptr);
            for (int f = 0; f < 50; ++f) {
                smallPayload[0] = static_cast<char>('A' + (sub % 26));
                smallPayload[1] = static_cast<char>('0' + (f % 10));
                PathUtils::WriteFileBytes(subDir + L"\\f_" + std::to_wstring(f) + L".bin", smallPayload.data(), smallPayload.size());
            }
        }

        LARGE_INTEGER qpf = {}, t0 = {}, t1 = {};
        QueryPerformanceFrequency(&qpf);

        TransferConfig fastCfg;
        fastCfg.workerThreads = 0; // Auto (NVMe: up to 8 workers)
        fastCfg.bufferSizeKB = 0;  // Auto (4x4 MB Persistent Async Ring)
        fastCfg.verifyMode = TransferVerifyMode::None;

        QueryPerformanceCounter(&t0);
        int jSmall = tm.EnqueueTransfer({ bSmallSrc }, bSmallDst, TransferOp::Copy, &fastCfg);
        bool okSmallWait = tm.WaitForJob(jSmall, 20000);
        QueryPerformanceCounter(&t1);
        double smallSec = std::max(0.0001, static_cast<double>(t1.QuadPart - t0.QuadPart) / static_cast<double>(qpf.QuadPart));
        double smallIops = 400.0 / smallSec;

        // Mixed 4-Slot Persistent Ring + Small Work-Stealing batch (2 x 32 MB large files + 40 small files)
        std::wstring bMixSrc = baseDir + L"\\t9_mix_src";
        std::wstring bMixDst = baseDir + L"\\t9_mix_dst";
        CreateDirectoryW(bMixSrc.c_str(), nullptr);

        std::vector<uint8_t> bigBuf(32 * 1024 * 1024);
        for (size_t i = 0; i < bigBuf.size(); ++i) bigBuf[i] = static_cast<uint8_t>((i * 29u + (i >> 11)) & 0xFFu);
        uint64_t expectedBigHash = TransferManager::ComputeXxHash64Memory(bigBuf.data(), bigBuf.size());

        PathUtils::WriteFileBytes(bMixSrc + L"\\stream_A.pak", bigBuf.data(), bigBuf.size());
        PathUtils::WriteFileBytes(bMixSrc + L"\\stream_B.pak", bigBuf.data(), bigBuf.size());
        for (int f = 0; f < 40; ++f) {
            PathUtils::WriteFileBytes(bMixSrc + L"\\tiny_" + std::to_wstring(f) + L".cfg", smallPayload.data(), 512);
        }

        QueryPerformanceCounter(&t0);
        int jMix = tm.EnqueueTransfer({ bMixSrc }, bMixDst, TransferOp::Copy, &fastCfg);
        bool okMixWait = tm.WaitForJob(jMix, 20000);
        QueryPerformanceCounter(&t1);
        double mixSec = std::max(0.0001, static_cast<double>(t1.QuadPart - t0.QuadPart) / static_cast<double>(qpf.QuadPart));
        double mixMBps = 64.0 / mixSec;

        uint64_t hA = 0, hB = 0;
        bool mixIntegrity =
            (GetFileAttributesW((bSmallDst + L"\\t9_small_src\\sub_7\\f_49.bin").c_str()) != INVALID_FILE_ATTRIBUTES) &&
            (GetFileAttributesW((bMixDst + L"\\t9_mix_src\\tiny_39.cfg").c_str()) != INVALID_FILE_ATTRIBUTES) &&
            TransferManager::ComputeXxHash64File(bMixDst + L"\\t9_mix_src\\stream_A.pak", hA) && (hA == expectedBigHash) &&
            TransferManager::ComputeXxHash64File(bMixDst + L"\\t9_mix_src\\stream_B.pak", hB) && (hB == expectedBigHash);

        wchar_t perfNote[160] = {};
        swprintf_s(perfNote, L"400 small=%.1f ms (%.0f files/s), 2x32MB+40mix=%.1f ms (%.0f MB/s)",
            smallSec * 1000.0, smallIops, mixSec * 1000.0, mixMBps);
        tc.Record(okSmallWait && okMixWait && mixIntegrity,
            L"Stage13.Test9_HighSpeed_SmallFileIOPS_And_4SlotRingThroughput",
            perfNote);
    }

    // Test 13.10: NTFS Sparse File Fast-Path (FSCTL_SET_SPARSE + FSCTL_QUERY_ALLOCATED_RANGES + XXH64)
    {
        std::wstring spSrcDir = baseDir + L"\\t10_sparse_src";
        std::wstring spDstDir = baseDir + L"\\t10_sparse_dst";
        CreateDirectoryW(spSrcDir.c_str(), nullptr);
        CreateDirectoryW(spDstDir.c_str(), nullptr);

        std::wstring spSrc = spSrcDir + L"\\vhdx_sample.sparse";
        std::wstring spDst = spDstDir + L"\\vhdx_sample.sparse";
        constexpr uint64_t kLogicalSize = 64ULL * 1024ULL * 1024ULL; // 64 MB logical, only 128 KB physical data

        bool createdSparse = false;
        HANDLE hSp = CreateFileW(spSrc.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hSp != INVALID_HANDLE_VALUE) {
            DWORD tmpBytes = 0;
            BOOL sparseOk = DeviceIoControl(hSp, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &tmpBytes, nullptr);
            std::vector<uint8_t> islandA(64 * 1024, 0xA5);
            std::vector<uint8_t> islandB(64 * 1024, 0x3C);
            LARGE_INTEGER pos = {};
            DWORD wrA = 0, wrB = 0;
            pos.QuadPart = 4LL * 1024LL * 1024LL;
            SetFilePointerEx(hSp, pos, nullptr, FILE_BEGIN);
            WriteFile(hSp, islandA.data(), static_cast<DWORD>(islandA.size()), &wrA, nullptr);

            pos.QuadPart = 48LL * 1024LL * 1024LL;
            SetFilePointerEx(hSp, pos, nullptr, FILE_BEGIN);
            WriteFile(hSp, islandB.data(), static_cast<DWORD>(islandB.size()), &wrB, nullptr);

            // Leave a ~15.9 MB trailing hole up to 64 MB
            pos.QuadPart = static_cast<LONGLONG>(kLogicalSize);
            SetFilePointerEx(hSp, pos, nullptr, FILE_BEGIN);
            SetEndOfFile(hSp);
            CloseHandle(hSp);
            createdSparse = (sparseOk != FALSE) && (wrA == islandA.size()) && (wrB == islandB.size());
        }

        TransferConfig spCfg;
        spCfg.verifyMode = TransferVerifyMode::XxHash64;
        int jSp = tm.EnqueueTransfer({ spSrc }, spDstDir, TransferOp::Copy, &spCfg);
        bool waitedSp = tm.WaitForJob(jSp, 15000);

        DWORD dstAttrs = GetFileAttributesW(spDst.c_str());
        bool dstIsSparse = (dstAttrs != INVALID_FILE_ATTRIBUTES) && ((dstAttrs & FILE_ATTRIBUTE_SPARSE_FILE) != 0);

        WIN32_FILE_ATTRIBUTE_DATA dstFad = {};
        uint64_t dstLogical = 0;
        if (GetFileAttributesExW(spDst.c_str(), GetFileExInfoStandard, &dstFad)) {
            dstLogical = (static_cast<uint64_t>(dstFad.nFileSizeHigh) << 32) | dstFad.nFileSizeLow;
        }
        DWORD compHigh = 0;
        DWORD compLow = GetCompressedFileSizeW(spDst.c_str(), &compHigh);
        uint64_t dstAllocated = (compLow == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
            ? kLogicalSize
            : ((static_cast<uint64_t>(compHigh) << 32) | compLow);

        uint64_t hSrc = 0, hDst = 0;
        bool hashOk = TransferManager::ComputeXxHash64File(spSrc, hSrc) &&
                      TransferManager::ComputeXxHash64File(spDst, hDst) &&
                      (hSrc == hDst);

        tc.Record(createdSparse && waitedSp && dstIsSparse && dstLogical == kLogicalSize &&
                  dstAllocated < (4ULL * 1024ULL * 1024ULL) && hashOk,
            L"Stage13.Test10_SparseFile_FastRangeCopy_And_XXH64",
            L"logical=" + std::to_wstring(dstLogical >> 20) + L"MB, onDiskKB=" + std::to_wstring(dstAllocated >> 10));
    }

    // Test 13.11: Pre-Flight Low Disk Space Check -> Auto-Pause + Yellow Taskbar State + Resume After Space Freed
    {
        std::wstring dsSrcDir = baseDir + L"\\t11_disk_src";
        std::wstring dsDstDir = baseDir + L"\\t11_disk_dst";
        CreateDirectoryW(dsSrcDir.c_str(), nullptr);
        CreateDirectoryW(dsDstDir.c_str(), nullptr);

        std::vector<uint8_t> data2MB(2 * 1024 * 1024, 0x7E);
        std::wstring src2MB = dsSrcDir + L"\\payload_2mb.bin";
        PathUtils::WriteFileBytes(src2MB, data2MB.data(), data2MB.size());

        // Simulate only 512 KB available on destination volume
        tm.SetSimulatedFreeSpaceForTesting(512 * 1024, true);

        TransferConfig cfgLow;
        cfgLow.verifyMode = TransferVerifyMode::XxHash64;
        int jLow = tm.EnqueueTransfer({ src2MB }, dsDstDir, TransferOp::Copy, &cfgLow);

        bool autoPaused = false;
        std::wstring pauseSummary;
        for (int waitIter = 0; waitIter < 100; ++waitIter) {
            auto snaps = tm.SnapshotJobs();
            for (const auto& s : snaps) {
                if (s.id == jLow && s.state == TransferJobState::Paused) {
                    autoPaused = true;
                    pauseSummary = s.summaryText;
                    break;
                }
            }
            if (autoPaused) break;
            Sleep(20);
        }

        uint64_t tbDone = 0, tbTotal = 0;
        int tbStateWhilePaused = tm.GetTaskbarProgressInfo(tbDone, tbTotal);

        // Restore normal disk space and resume job
        tm.SetSimulatedFreeSpaceForTesting(0, false);
        tm.ResumeJob(jLow);
        bool resumedAndFinished = tm.WaitForJob(jLow, 15000);

        uint64_t hSrc = 0, hDst = 0;
        bool hashOk = TransferManager::ComputeXxHash64File(src2MB, hSrc) &&
                      TransferManager::ComputeXxHash64File(dsDstDir + L"\\payload_2mb.bin", hDst) &&
                      (hSrc == hDst);

        tc.Record(autoPaused && !pauseSummary.empty() && tbStateWhilePaused == 3 && resumedAndFinished && hashOk,
            L"Stage13.Test11_PreFlight_LowDiskSpace_AutoPause_And_TaskbarState",
            pauseSummary);
    }

    // Test 13.12: Crash-Safe Persistent Queue (transfer_queue.dat) + Mid-Flight .e2part Recovery Without Duplicate "(2)" Files
    {
        std::wstring qSrcDir = baseDir + L"\\t12_q_src";
        std::wstring qDstDir = baseDir + L"\\t12_q_dst";
        std::wstring queueDat = baseDir + L"\\t12_transfer_queue.dat";
        CreateDirectoryW(qSrcDir.c_str(), nullptr);
        CreateDirectoryW(qDstDir.c_str(), nullptr);
        DeleteFileW(queueDat.c_str());

        std::vector<uint8_t> liveBuf(8 * 1024 * 1024);
        for (size_t i = 0; i < liveBuf.size(); ++i) liveBuf[i] = static_cast<uint8_t>((i * 73u + 11u) & 0xFFu);
        std::vector<uint8_t> f1Buf(256 * 1024, 0x11);
        std::vector<uint8_t> f2Buf(2 * 1024 * 1024);
        for (size_t i = 0; i < f2Buf.size(); ++i) f2Buf[i] = static_cast<uint8_t>((i * 19u + 3u) & 0xFFu);
        std::vector<uint8_t> f3Buf(512 * 1024, 0x33);

        std::wstring sLive = qSrcDir + L"\\00_live_stream.bin";
        std::wstring sF1   = qSrcDir + L"\\01_already_done.bin";
        std::wstring sF2   = qSrcDir + L"\\02_half_part.bin";
        std::wstring sF3   = qSrcDir + L"\\03_unstarted.bin";

        PathUtils::WriteFileBytes(sLive, liveBuf.data(), liveBuf.size());
        PathUtils::WriteFileBytes(sF1,   f1Buf.data(),   f1Buf.size());
        PathUtils::WriteFileBytes(sF2,   f2Buf.data(),   f2Buf.size());
        PathUtils::WriteFileBytes(sF3,   f3Buf.data(),   f3Buf.size());

        // Pre-seed 01_already_done.bin in destination with identical size & writeTime (as if finished before crash)
        std::wstring dF1 = qDstDir + L"\\01_already_done.bin";
        PathUtils::WriteFileBytes(dF1, f1Buf.data(), f1Buf.size());
        {
            WIN32_FILE_ATTRIBUTE_DATA sfad = {};
            GetFileAttributesExW(sF1.c_str(), GetFileExInfoStandard, &sfad);
            HANDLE hDf1 = CreateFileW(dF1.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hDf1 != INVALID_HANDLE_VALUE) {
                FILE_BASIC_INFO bi = {};
                bi.LastWriteTime.LowPart = sfad.ftLastWriteTime.dwLowDateTime;
                bi.LastWriteTime.HighPart = static_cast<LONG>(sfad.ftLastWriteTime.dwHighDateTime);
                SetFileInformationByHandle(hDf1, FileBasicInfo, &bi, sizeof(bi));
                CloseHandle(hDf1);
            }
        }

        // Pre-seed 02_half_part.bin.e2part with valid 1 MB prefix & source mtime fingerprint
        std::wstring dF2Part = qDstDir + L"\\02_half_part.bin.e2part";
        PathUtils::WriteFileBytes(dF2Part, f2Buf.data(), 1024 * 1024);
        {
            WIN32_FILE_ATTRIBUTE_DATA sfad2 = {};
            GetFileAttributesExW(sF2.c_str(), GetFileExInfoStandard, &sfad2);
            HANDLE hPart2 = CreateFileW(dF2Part.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hPart2 != INVALID_HANDLE_VALUE) {
                FILE_BASIC_INFO bi = {};
                bi.CreationTime.LowPart = sfad2.ftLastWriteTime.dwLowDateTime;
                bi.CreationTime.HighPart = static_cast<LONG>(sfad2.ftLastWriteTime.dwHighDateTime);
                SetFileInformationByHandle(hPart2, FileBasicInfo, &bi, sizeof(bi));
                CloseHandle(hPart2);
            }
        }

        // Session 1: Start transfer and shut down mid-stream while 00_live_stream.bin is actively copying
        {
            TransferManager tm1;
            tm1.Initialize(nullptr);
            tm1.EnableQueuePersistence(queueDat);

            TransferConfig qCfg;
            qCfg.workerThreads = 1;
            qCfg.speedLimitMBps = 10; // ~800 ms for 8 MB so mid-flight shutdown is deterministic
            qCfg.bufferSizeKB = 512;
            qCfg.usePartFiles = true;
            qCfg.conflictPolicy = TransferConflictPolicy::RenameCollision;
            qCfg.verifyMode = TransferVerifyMode::None;

            int jid1 = tm1.EnqueueTransfer({ sLive, sF1, sF2, sF3 }, qDstDir, TransferOp::Copy, &qCfg);
            for (int w = 0; w < 80; ++w) {
                auto snaps = tm1.SnapshotJobs();
                if (!snaps.empty() && snaps[0].id == jid1 && snaps[0].doneBytes >= 512 * 1024) break;
                Sleep(15);
            }
            // Graceful shutdown / crash persistence: must NOT delete .e2part or drop unfinished job
            tm1.Shutdown();
        }

        bool queueFileSaved = (GetFileAttributesW(queueDat.c_str()) != INVALID_FILE_ATTRIBUTES);

        // Session 2: Fresh TransferManager reloads transfer_queue.dat as Paused, then resumes seamlessly
        size_t recoveredCount = 0;
        int tbPausedState = 0;
        bool resumedDone = false;
        uint64_t resumedFilesCount = 0;
        {
            TransferManager tm2;
            tm2.Initialize(nullptr);
            recoveredCount = tm2.EnableQueuePersistence(queueDat);

            uint64_t c0 = 0, t0 = 0;
            tbPausedState = tm2.GetTaskbarProgressInfo(c0, t0);

            auto snapsBefore = tm2.SnapshotJobs();
            int recJobId = snapsBefore.empty() ? 0 : snapsBefore[0].id;
            tm2.ResumeAll();
            if (recJobId > 0) {
                resumedDone = tm2.WaitForJob(recJobId, 25000);
            }
            auto snapsAfter = tm2.SnapshotJobs();
            if (!snapsAfter.empty()) {
                resumedFilesCount = snapsAfter[0].resumedFiles;
            }
            tm2.Shutdown();
        }

        bool queueCleanedUp = (GetFileAttributesW(queueDat.c_str()) == INVALID_FILE_ATTRIBUTES);
        bool noDuplicateCreated = (GetFileAttributesW((qDstDir + L"\\01_already_done (2).bin").c_str()) == INVALID_FILE_ATTRIBUTES);

        uint64_t hLive = 0, h1 = 0, h2 = 0, h3 = 0;
        bool allHashesMatch =
            TransferManager::ComputeXxHash64File(qDstDir + L"\\00_live_stream.bin", hLive) &&
            (hLive == TransferManager::ComputeXxHash64Memory(liveBuf.data(), liveBuf.size())) &&
            TransferManager::ComputeXxHash64File(qDstDir + L"\\01_already_done.bin", h1) &&
            (h1 == TransferManager::ComputeXxHash64Memory(f1Buf.data(), f1Buf.size())) &&
            TransferManager::ComputeXxHash64File(qDstDir + L"\\02_half_part.bin", h2) &&
            (h2 == TransferManager::ComputeXxHash64Memory(f2Buf.data(), f2Buf.size())) &&
            TransferManager::ComputeXxHash64File(qDstDir + L"\\03_unstarted.bin", h3) &&
            (h3 == TransferManager::ComputeXxHash64Memory(f3Buf.data(), f3Buf.size()));

        tc.Record(queueFileSaved && recoveredCount == 1 && tbPausedState == 3 && resumedDone &&
                  queueCleanedUp && noDuplicateCreated && resumedFilesCount >= 1 && allHashesMatch,
            L"Stage13.Test12_CrashSafe_QueuePersistence_And_PartResume_Recovery",
            L"recovered=" + std::to_wstring(recoveredCount) + L", resumedParts=" + std::to_wstring(resumedFilesCount));
    }

    // Test 13.13: Async Non-Blocking FTP Transfers (FTP->Local, Local->FTP, FTP->FTP Move) + Watcher Guard
    {
        FtpClient::ClearMockFtpData();
        std::vector<uint8_t> readmeBytes(130 * 1024, 0x42);
        std::vector<uint8_t> fwBytes(260 * 1024, 0x99);

        const char* pubListing =
            "drwxr-xr-x 2 root root   4096 Nov 10 12:00 firmware\r\n"
            "-rw-r--r-- 1 root root 133120 Nov 10 12:01 readme.txt\r\n";
        const char* fwListing =
            "-rw-r--r-- 1 root root 266240 Nov 10 12:02 boot.bin\r\n";

        FtpClient::RegisterMockFtpListing(L"ftp://xfer.local/pub", pubListing);
        FtpClient::RegisterMockFtpListing(L"ftp://xfer.local/pub/firmware", fwListing);
        FtpClient::RegisterMockFtpListing(L"ftp://xfer.local/upload", "");
        FtpClient::RegisterMockFtpListing(L"ftp://xfer.local/archive", "");
        FtpClient::RegisterMockFtpFileContent(L"ftp://xfer.local/pub/readme.txt", readmeBytes);
        FtpClient::RegisterMockFtpFileContent(L"ftp://xfer.local/pub/firmware/boot.bin", fwBytes);

        std::wstring ftpLocalDst = baseDir + L"\\t13_ftp_dl";
        CreateDirectoryW(ftpLocalDst.c_str(), nullptr);

        // 1) Async FTP -> Local recursive folder + file download
        int jDl = tm.EnqueueTransfer(
            { L"ftp://xfer.local/pub/firmware", L"ftp://xfer.local/pub/readme.txt" },
            ftpLocalDst, TransferOp::Copy, nullptr);
        bool okDl = tm.WaitForJob(jDl, 15000);

        uint64_t hDlReadme = 0, hDlBoot = 0;
        bool dlVerified = okDl &&
            TransferManager::ComputeXxHash64File(ftpLocalDst + L"\\readme.txt", hDlReadme) &&
            (hDlReadme == TransferManager::ComputeXxHash64Memory(readmeBytes.data(), readmeBytes.size())) &&
            TransferManager::ComputeXxHash64File(ftpLocalDst + L"\\firmware\\boot.bin", hDlBoot) &&
            (hDlBoot == TransferManager::ComputeXxHash64Memory(fwBytes.data(), fwBytes.size()));

        // 2) Async Local -> FTP recursive upload
        int jUl = tm.EnqueueTransfer({ ftpLocalDst + L"\\firmware" }, L"ftp://xfer.local/upload", TransferOp::Copy, nullptr);
        bool okUl = tm.WaitForJob(jUl, 15000);
        std::vector<FileEntry> ulEntries;
        FtpClient::EnumerateFtpDirectory(L"ftp://xfer.local/upload/firmware", true, true, Lang::PL, ulEntries);
        bool ulVerified = okUl && !ulEntries.empty() && ulEntries[0].name == L"boot.bin" && ulEntries[0].size == fwBytes.size();

        // 3) Async FTP -> FTP Move (stages via temp file, uploads to archive, deletes source from pub)
        int jMv = tm.EnqueueTransfer({ L"ftp://xfer.local/pub/readme.txt" }, L"ftp://xfer.local/archive", TransferOp::Move, nullptr);
        bool okMv = tm.WaitForJob(jMv, 15000);
        std::vector<FileEntry> arcEntries, pubAfterEntries;
        FtpClient::EnumerateFtpDirectory(L"ftp://xfer.local/archive", true, true, Lang::PL, arcEntries);
        FtpClient::EnumerateFtpDirectory(L"ftp://xfer.local/pub", true, true, Lang::PL, pubAfterEntries);
        bool mvInArchive = false, mvGoneFromPub = true;
        for (const auto& e : arcEntries) {
            if (e.name == L"readme.txt" && e.size == readmeBytes.size()) mvInArchive = true;
        }
        for (const auto& e : pubAfterEntries) {
            if (e.name == L"readme.txt") mvGoneFromPub = false;
        }

        // 4) Verify IsTouchingDirectory watcher storm guard:
        //    - During Copy: destination is guarded (true), read-only source is NOT suppressed (false)
        std::wstring wSrcDir = baseDir + L"\\t13_watch_src";
        std::wstring wDstDir = baseDir + L"\\t13_watch_dst";
        CreateDirectoryW(wSrcDir.c_str(), nullptr);
        CreateDirectoryW(wDstDir.c_str(), nullptr);
        std::vector<uint8_t> watchBuf(4 * 1024 * 1024, 0x55);
        PathUtils::WriteFileBytes(wSrcDir + L"\\watch.bin", watchBuf.data(), watchBuf.size());
        TransferConfig wCfg;
        wCfg.bufferSizeKB = 512;
        wCfg.speedLimitMBps = 8; // ~500 ms across 8 x 512 KB chunks
        int jWatch = tm.EnqueueTransfer({ wSrcDir + L"\\watch.bin" }, wDstDir, TransferOp::Copy, &wCfg);
        bool dstGuardedWhileActive = false;
        bool srcUnguardedDuringCopy = false;
        for (int i = 0; i < 40; ++i) {
            if (tm.IsTouchingDirectory(PathUtils::NormalizePathKey(wDstDir))) {
                dstGuardedWhileActive = true;
                srcUnguardedDuringCopy = !tm.IsTouchingDirectory(PathUtils::NormalizePathKey(wSrcDir));
                break;
            }
            Sleep(10);
        }
        tm.WaitForJob(jWatch, 15000);
        bool notTouchingAfterDone = !tm.IsTouchingDirectory(PathUtils::NormalizePathKey(wDstDir));

        FtpClient::ClearMockFtpData();
        wchar_t t13Detail[192] = {};
        swprintf_s(t13Detail, L"dl=%d, ul=%d, mv=%d(arc=%d,gone=%d), watchDst=%d, watchSrcCopy=%d, after=%d",
            dlVerified ? 1 : 0, ulVerified ? 1 : 0, okMv ? 1 : 0, mvInArchive ? 1 : 0, mvGoneFromPub ? 1 : 0,
            dstGuardedWhileActive ? 1 : 0, srcUnguardedDuringCopy ? 1 : 0, notTouchingAfterDone ? 1 : 0);
        tc.Record(dlVerified && ulVerified && okMv && mvInArchive && mvGoneFromPub &&
                  dstGuardedWhileActive && srcUnguardedDuringCopy && notTouchingAfterDone,
            L"Stage13.Test13_AsyncFtpTransfers_WatcherGuard_And_TaskbarStates",
            t13Detail);
    }

    // ------------------------------------------------------------------------
    // Delete engine (Tests 13.14 - 13.20). Every path used here lies inside baseDir.
    // ------------------------------------------------------------------------
    auto extPath = [](const std::wstring& p) { return L"\\\\?\\" + p; };
    auto existsOnDisk = [&](const std::wstring& p) {
        return GetFileAttributesW(extPath(p).c_str()) != INVALID_FILE_ATTRIBUTES;
    };

    // Test 13.14: paths the engine refuses outright. Only strings are checked here: a whole drive or a
    // share is never handed to the engine, not even to prove that it would say no.
    {
        bool refuses = true;
        for (const wchar_t* p : { L"", L"THIS_PC", L"C:", L"C:\\", L"c:/", L"\\\\?\\C:\\", L"\\\\server\\share",
                                  L"\\\\server\\share\\", L"\\\\?\\UNC\\server\\share", L"relative\\path.txt",
                                  L"C:relative.txt", L"C:\\a\\..\\b", L"C:\\a\\.\\b", L"C:\\a\\\\b" }) {
            refuses = refuses && TransferManager::IsDeleteRefused(p);
        }
        bool accepts = true;
        for (const wchar_t* p : { L"C:\\a", L"C:\\a\\b.txt", L"C:\\a\\trailing dot.", L"C:\\a\\...three",
                                  L"\\\\server\\share\\dir", L"\\\\?\\C:\\a", L"ftp://host/pub/file.txt" }) {
            accepts = accepts && !TransferManager::IsDeleteRefused(p);
        }
        // A list that holds nothing usable creates no job at all
        const bool noJob = tm.EnqueueDelete({ L"", L"relative\\nothing.txt" }, true) == 0;
        std::vector<TransferFileError> nowErrors;
        const bool nowRefuses = !TransferManager::DeleteTreeNow(L"relative\\nothing.txt", &nowErrors) && nowErrors.size() == 1;
        tc.Record(refuses && accepts && noJob && nowRefuses, L"Stage13.Test14_Delete_RefusesDrivesSharesAndRelativePaths",
            L"refuses=" + std::to_wstring(refuses) + L", accepts=" + std::to_wstring(accepts) +
            L", noJob=" + std::to_wstring(noJob) + L", now=" + std::to_wstring(nowRefuses));
    }

    // Test 13.15: permanent delete of a tree that is far deeper than MAX_PATH, with read-only and hidden
    // items, names the plain Win32 API cannot address, and enough files for the multi-threaded path
    {
        const std::wstring root = baseDir + L"\\t15_tree";
        uint64_t expectItems = 0;
        bool built = CreateDirectoryW(extPath(root).c_str(), nullptr) != FALSE;
        ++expectItems;

        std::wstring deep = root;
        for (int lvl = 0; lvl < 45; ++lvl) {
            wchar_t nm[32] = {};
            swprintf_s(nm, L"\\level_%02d_abcdefghi", lvl);
            deep += nm;
            built = built && CreateDirectoryW(extPath(deep).c_str(), nullptr) != FALSE;
            ++expectItems;
            const std::wstring note = deep + L"\\note.txt";
            built = built && PathUtils::WriteFileBytes(extPath(note), "x", 1);
            ++expectItems;
            if (lvl % 7 == 0) SetFileAttributesW(extPath(note).c_str(), FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN);
            if (lvl % 11 == 0) SetFileAttributesW(extPath(deep).c_str(), FILE_ATTRIBUTE_READONLY);
        }
        const size_t deepestLen = deep.size();

        const std::wstring odd = root + L"\\odd names";
        built = built && CreateDirectoryW(extPath(odd).c_str(), nullptr) != FALSE;
        ++expectItems;
        for (const wchar_t* n : { L"trailing dot.", L"trailing space ", L"nul" }) {
            built = built && PathUtils::WriteFileBytes(extPath(odd + L"\\" + n), "y", 1);
            ++expectItems;
        }

        const std::wstring wide = root + L"\\wide";
        built = built && CreateDirectoryW(extPath(wide).c_str(), nullptr) != FALSE;
        ++expectItems;
        for (int i = 0; i < 600; ++i) {
            wchar_t nm[32] = {};
            swprintf_s(nm, L"\\f_%04d.bin", i);
            built = built && PathUtils::WriteFileBytes(extPath(wide + nm), "zz", 2);
            ++expectItems;
        }

        const int jid = tm.EnqueueDelete({ root }, true);
        const bool waited = tm.WaitForJob(jid, 60000);
        TransferJobSnapshot snap;
        const bool have = FindJobSnapshot(tm, jid, snap);
        const bool gone = !existsOnDisk(root);
        const bool ok = built && waited && have && snap.state == TransferJobState::Completed && snap.errors.empty() &&
                        snap.totalFiles == expectItems && snap.doneFiles == expectItems && gone;
        wchar_t detail[256] = {};
        swprintf_s(detail, L"built=%d, deepestPath=%zu chars, items=%llu/%llu (expected %llu), errors=%zu, gone=%d",
            built ? 1 : 0, deepestLen, static_cast<unsigned long long>(snap.doneFiles),
            static_cast<unsigned long long>(snap.totalFiles), static_cast<unsigned long long>(expectItems),
            snap.errors.size(), gone ? 1 : 0);
        if (!gone) RemoveTestTreeRecursive(root);
        tc.Record(ok, L"Stage13.Test15_Delete_DeepTree_LongPaths_ReadOnly_OddNames", detail);
    }

    // Test 13.16: a junction inside the deleted tree is removed as a link; what it points to is not touched.
    // The same holds when the link itself is the item to delete.
    {
        const std::wstring keep = baseDir + L"\\t16_keep";
        const std::wstring victim = baseDir + L"\\t16_victim";
        const std::wstring link2 = baseDir + L"\\t16_link2";
        CreateDirectoryW(keep.c_str(), nullptr);
        PathUtils::WriteFileBytes(keep + L"\\keep.txt", "keep", 4);
        CreateDirectoryW(victim.c_str(), nullptr);
        CreateDirectoryW((victim + L"\\sub").c_str(), nullptr);
        PathUtils::WriteFileBytes(victim + L"\\sub\\gone.txt", "gone", 4);

        wchar_t longBuf[1024] = {};
        const std::wstring keepLong = (GetLongPathNameW(keep.c_str(), longBuf, 1024) > 0) ? std::wstring(longBuf) : keep;
        const bool linkMade = CreateTestJunction(victim + L"\\sub\\link_to_keep", keepLong);
        const bool link2Made = CreateTestJunction(link2, keepLong);
        const bool seenThroughLink = linkMade && existsOnDisk(victim + L"\\sub\\link_to_keep\\keep.txt");

        const int jid = tm.EnqueueDelete({ victim }, true);
        const bool waited = tm.WaitForJob(jid, 30000);
        TransferJobSnapshot snap;
        const bool have = FindJobSnapshot(tm, jid, snap);
        const bool victimGone = !existsOnDisk(victim);
        const bool keptAfterJob = existsOnDisk(keep + L"\\keep.txt");

        const bool link2Removed = !link2Made || TransferManager::DeleteTreeNow(link2);
        const bool link2Gone = !existsOnDisk(link2);
        const bool keptAfterNow = existsOnDisk(keep + L"\\keep.txt");

        const bool ok = waited && have && snap.state == TransferJobState::Completed && snap.errors.empty() &&
                        victimGone && keptAfterJob && link2Removed && link2Gone && keptAfterNow &&
                        (!linkMade || seenThroughLink);
        wchar_t detail[224] = {};
        swprintf_s(detail, L"junctions=%d+%d, seenThroughLink=%d, victimGone=%d, targetKept=%d/%d, linkOnlyRemoved=%d",
            linkMade ? 1 : 0, link2Made ? 1 : 0, seenThroughLink ? 1 : 0, victimGone ? 1 : 0,
            keptAfterJob ? 1 : 0, keptAfterNow ? 1 : 0, (link2Removed && link2Gone) ? 1 : 0);
        tc.Record(ok, L"Stage13.Test16_Delete_NeverFollowsJunctions", detail);
    }

    // Test 13.17: pause holds, resume continues, cancel stops; a job that starts paused deletes nothing
    {
        const std::wstring dir = baseDir + L"\\t17_slow";
        CreateDirectoryW(dir.c_str(), nullptr);
        const int kFiles = 150;
        for (int i = 0; i < kFiles; ++i) {
            wchar_t nm[32] = {};
            swprintf_s(nm, L"\\s_%03d.tmp", i);
            PathUtils::WriteFileBytes(dir + nm, "s", 1);
        }
        auto countLeft = [&]() {
            int n = 0;
            WIN32_FIND_DATAW fd = {};
            HANDLE h = FindFirstFileW((dir + L"\\*.tmp").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do { ++n; } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            return n;
        };
        auto doneOf = [&](int id) {
            TransferJobSnapshot s;
            return FindJobSnapshot(tm, id, s) ? s.doneFiles : 0ULL;
        };
        auto waitForDone = [&](int id, uint64_t atLeast, DWORD timeoutMs) {
            const ULONGLONG t0 = GetTickCount64();
            while (GetTickCount64() - t0 < timeoutMs) {
                if (doneOf(id) >= atLeast) return true;
                Sleep(5);
            }
            return false;
        };

        tm.SetDeleteItemDelayForTesting(5);
        const int jid = tm.EnqueueDelete({ dir }, true);
        const bool started = waitForDone(jid, 10, 10000);
        tm.PauseJob(jid);
        Sleep(150);
        const uint64_t doneA = doneOf(jid);
        const int leftA = countLeft();
        Sleep(250);
        const uint64_t doneB = doneOf(jid);
        const int leftB = countLeft();
        TransferJobSnapshot pausedSnap;
        FindJobSnapshot(tm, jid, pausedSnap);
        const bool pauseHolds = started && doneA == doneB && leftA == leftB && leftA > 0 &&
                                pausedSnap.state == TransferJobState::Paused;

        tm.ResumeJob(jid);
        const bool resumed = waitForDone(jid, doneB + 10, 10000);
        tm.CancelOrRemoveJob(jid);
        const bool waited = tm.WaitForJob(jid, 15000);
        TransferJobSnapshot cancelSnap;
        const bool have = FindJobSnapshot(tm, jid, cancelSnap);
        const int leftC = countLeft();
        const bool cancelStops = resumed && waited && have && cancelSnap.state == TransferJobState::Cancelled &&
                                 leftC > 0 && static_cast<uint64_t>(kFiles - leftC) == cancelSnap.doneFiles &&
                                 cancelSnap.errors.empty();
        tm.SetDeleteItemDelayForTesting(0);

        const std::wstring parkedDir = baseDir + L"\\t17_parked";
        CreateDirectoryW(parkedDir.c_str(), nullptr);
        PathUtils::WriteFileBytes(parkedDir + L"\\stay.txt", "s", 1);
        const int parkedId = tm.EnqueueDelete({ parkedDir }, true, nullptr, true);
        Sleep(200);
        TransferJobSnapshot parkedSnap;
        const bool parked = FindJobSnapshot(tm, parkedId, parkedSnap) && parkedSnap.state == TransferJobState::Paused &&
                            existsOnDisk(parkedDir + L"\\stay.txt");
        tm.CancelOrRemoveJob(parkedId);
        Sleep(100);
        const bool neverRan = existsOnDisk(parkedDir + L"\\stay.txt");
        // Even a job dropped before it ran reports back, so the window can show its items again
        bool droppedJobReported = false;
        for (const auto& ev : tm.TakeCompletionEvents()) {
            if (ev.jobId == parkedId && ev.op == TransferOp::Delete &&
                ev.finalState == TransferJobState::Cancelled && ev.sources.size() == 1) {
                droppedJobReported = true;
            }
        }

        wchar_t detail[288] = {};
        swprintf_s(detail, L"pauseHolds=%d (done %llu=%llu, left %d=%d), resumed=%d, cancelStops=%d (left %d, done %llu), parked=%d, neverRan=%d, droppedJobReported=%d",
            pauseHolds ? 1 : 0, static_cast<unsigned long long>(doneA), static_cast<unsigned long long>(doneB), leftA, leftB,
            resumed ? 1 : 0, cancelStops ? 1 : 0, leftC, static_cast<unsigned long long>(cancelSnap.doneFiles),
            parked ? 1 : 0, neverRan ? 1 : 0, droppedJobReported ? 1 : 0);
        tc.Record(pauseHolds && cancelStops && parked && neverRan && droppedJobReported,
            L"Stage13.Test17_Delete_Pause_Resume_Cancel", detail);
    }

    // Test 13.18: a file another program holds open is reported (once, with its real error), everything
    // else goes, and "retry" finishes the job after the file is released
    {
        const std::wstring dir = baseDir + L"\\t18_lock";
        CreateDirectoryW(dir.c_str(), nullptr);
        CreateDirectoryW((dir + L"\\sub").c_str(), nullptr);
        PathUtils::WriteFileBytes(dir + L"\\a.txt", "a", 1);
        PathUtils::WriteFileBytes(dir + L"\\sub\\other.txt", "o", 1);
        const std::wstring locked = dir + L"\\sub\\locked.bin";
        PathUtils::WriteFileBytes(locked, "L", 1);
        HANDLE hold = CreateFileW(locked.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);

        TransferConfig cfg;
        cfg.maxAutoRetries = 1;
        const int jid = tm.EnqueueDelete({ dir }, true, &cfg);
        const bool waited1 = tm.WaitForJob(jid, 30000);
        TransferJobSnapshot s1;
        const bool have1 = FindJobSnapshot(tm, jid, s1);
        const bool reported = hold != INVALID_HANDLE_VALUE && waited1 && have1 &&
                              s1.state == TransferJobState::CompletedWithErrors && s1.errors.size() == 1 &&
                              s1.errors[0].win32Error == ERROR_SHARING_VIOLATION &&
                              PathUtils::NormalizePathKey(s1.errors[0].srcPath) == PathUtils::NormalizePathKey(locked);
        const bool restGone = !existsOnDisk(dir + L"\\a.txt") && !existsOnDisk(dir + L"\\sub\\other.txt");
        const bool lockedKept = existsOnDisk(locked);

        if (hold != INVALID_HANDLE_VALUE) CloseHandle(hold);
        const bool retried = tm.RetryFailedInJob(jid);
        const bool waited2 = tm.WaitForJob(jid, 30000);
        TransferJobSnapshot s2;
        const bool have2 = FindJobSnapshot(tm, jid, s2);
        const bool finished = retried && waited2 && have2 && s2.state == TransferJobState::Completed &&
                              s2.errors.empty() && s2.doneFiles == s2.totalFiles && !existsOnDisk(dir);

        wchar_t detail[224] = {};
        swprintf_s(detail, L"reported=%d (errors=%zu, code=%lu), restGone=%d, lockedKept=%d, retryFinished=%d (%llu/%llu)",
            reported ? 1 : 0, s1.errors.size(), s1.errors.empty() ? 0UL : s1.errors[0].win32Error,
            restGone ? 1 : 0, lockedKept ? 1 : 0, finished ? 1 : 0,
            static_cast<unsigned long long>(s2.doneFiles), static_cast<unsigned long long>(s2.totalFiles));
        tc.Record(reported && restGone && lockedKept && finished, L"Stage13.Test18_Delete_LockedFile_Reported_Then_Retry", detail);
    }

    // Test 13.19: a selection that names a folder and a file inside it, a plain file and something that is
    // already gone; plus the engine's synchronous form
    {
        const std::wstring dir = baseDir + L"\\t19_mixed";
        CreateDirectoryW(dir.c_str(), nullptr);
        CreateDirectoryW((dir + L"\\folder").c_str(), nullptr);
        CreateDirectoryW((dir + L"\\folder\\sub").c_str(), nullptr);
        PathUtils::WriteFileBytes(dir + L"\\folder\\f1.txt", "1", 1);
        PathUtils::WriteFileBytes(dir + L"\\folder\\sub\\f2.txt", "2", 1);
        PathUtils::WriteFileBytes(dir + L"\\single.txt", "3", 1);

        const int jid = tm.EnqueueDelete({ dir + L"\\folder", dir + L"\\folder\\sub\\f2.txt", dir + L"\\single.txt",
                                           dir + L"\\was_never_here.txt", dir + L"\\FOLDER\\" }, true);
        const bool waited = tm.WaitForJob(jid, 30000);
        TransferJobSnapshot snap;
        const bool have = FindJobSnapshot(tm, jid, snap);
        const bool jobOk = waited && have && snap.state == TransferJobState::Completed && snap.errors.empty() &&
                           snap.sources.size() == 3 && snap.doneFiles == 5 && snap.totalFiles == 5 &&
                           !existsOnDisk(dir + L"\\folder") && !existsOnDisk(dir + L"\\single.txt") && existsOnDisk(dir);

        CreateDirectoryW((dir + L"\\now").c_str(), nullptr);
        PathUtils::WriteFileBytes(dir + L"\\now\\ro.txt", "r", 1);
        SetFileAttributesW((dir + L"\\now\\ro.txt").c_str(), FILE_ATTRIBUTE_READONLY);
        const bool nowTree = TransferManager::DeleteTreeNow(dir + L"\\now") && !existsOnDisk(dir + L"\\now");
        const bool nowMissing = TransferManager::DeleteTreeNow(dir + L"\\now");   // already gone counts as done

        // A path that runs through a file (an entry shown inside an archive) never was an item on disk.
        // That is an error to report, not a delete to announce.
        const std::wstring container = dir + L"\\container.zip";
        PathUtils::WriteFileBytes(container, "PK", 2);
        const int insideId = tm.EnqueueDelete({ container + L"\\docs\\inner.txt" }, true);
        const bool insideWaited = tm.WaitForJob(insideId, 30000);
        TransferJobSnapshot insideSnap;
        const bool insideReported = insideWaited && FindJobSnapshot(tm, insideId, insideSnap) &&
                                    insideSnap.state == TransferJobState::CompletedWithErrors &&
                                    insideSnap.errors.size() == 1 && insideSnap.errors[0].win32Error == ERROR_DIRECTORY &&
                                    existsOnDisk(container);

        wchar_t detail[224] = {};
        swprintf_s(detail, L"job=%d (sources=%zu, items=%llu/%llu, errors=%zu), nowTree=%d, nowMissing=%d, insideArchiveReported=%d",
            jobOk ? 1 : 0, snap.sources.size(), static_cast<unsigned long long>(snap.doneFiles),
            static_cast<unsigned long long>(snap.totalFiles), snap.errors.size(), nowTree ? 1 : 0, nowMissing ? 1 : 0,
            insideReported ? 1 : 0);
        tc.Record(jobOk && nowTree && nowMissing && insideReported,
            L"Stage13.Test19_Delete_NestedSelection_MissingItems_And_SyncForm", detail);
    }

    // Test 13.20: a Recycle Bin delete really ends up in the bin with its content intact. Where the volume has
    // no usable bin, the engine has to report that and keep the file; deleting it for good is never acceptable.
    {
        const std::wstring rDir = baseDir + L"\\t20_recycle";
        CreateDirectoryW(rDir.c_str(), nullptr);
        wchar_t longBuf[1024] = {};
        const std::wstring rDirLong = (GetLongPathNameW(rDir.c_str(), longBuf, 1024) > 0) ? std::wstring(longBuf) : rDir;
        const std::wstring rFile = rDirLong + L"\\e2_selftest_recycle_" + std::to_wstring(GetTickCount64()) + L".txt";
        const std::string body = "Explorer2 self-test: this file went to the Recycle Bin and was taken out again.";
        PathUtils::WriteFileBytes(rFile, body.data(), body.size());

        const int jid = tm.EnqueueDelete({ rFile }, false);
        const bool waited = tm.WaitForJob(jid, 30000);
        TransferJobSnapshot snap;
        const bool have = FindJobSnapshot(tm, jid, snap);
        const bool stillThere = existsOnDisk(rFile);

        bool ok = false;
        std::wstring detail;
        if (waited && have && snap.state == TransferJobState::Completed && !stillThere) {
            std::wstring infoFile, dataFile;
            bool inBin = false;
            for (int i = 0; i < 40 && !inBin; ++i) {
                inBin = FindRecycleBinRecord(rFile, infoFile, dataFile);
                if (!inBin) Sleep(50);
            }
            std::vector<uint8_t> back;
            const bool sameContent = inBin && PathUtils::ReadFileBytes(dataFile, 4096, back) &&
                                     back.size() == body.size() && memcmp(back.data(), body.data(), body.size()) == 0;
            if (inBin) {
                // Take the test file out of the user's Recycle Bin again
                DeleteFileW(infoFile.c_str());
                DeleteFileW(dataFile.c_str());
            }
            ok = inBin && sameContent && snap.recycle && snap.doneFiles == 1 && snap.errors.empty();
            detail = L"recycled: inBin=" + std::to_wstring(inBin) + L", contentIntact=" + std::to_wstring(sameContent);
        } else if (waited && have && snap.state == TransferJobState::CompletedWithErrors && stillThere) {
            ok = true;
            detail = L"no usable Recycle Bin on this volume: reported and kept, not deleted for good";
            DeleteFileW(rFile.c_str());
        } else {
            detail = L"unexpected: waited=" + std::to_wstring(waited) + L", state=" +
                     std::to_wstring(have ? static_cast<int>(snap.state) : -1) + L", stillThere=" + std::to_wstring(stillThere) +
                     (have && !snap.errors.empty() ? (L", error=" + snap.errors[0].message) : std::wstring());
        }
        tc.Record(ok, L"Stage13.Test20_Delete_RecycleBin_RoundTrip_NeverPermanent", detail);
    }

    // Test 13.21: a file that appears in a folder after the folder was listed (a thumbnail cache, a log,
    // a late download) does not leave the folder behind: the engine takes a second pass
    {
        const std::wstring dir = baseDir + L"\\t21_late";
        CreateDirectoryW(dir.c_str(), nullptr);
        CreateDirectoryW((dir + L"\\listed_empty").c_str(), nullptr);
        for (int i = 0; i < 40; ++i) {
            wchar_t nm[32] = {};
            swprintf_s(nm, L"\\l_%02d.tmp", i);
            PathUtils::WriteFileBytes(dir + nm, "l", 1);
        }

        tm.SetDeleteItemDelayForTesting(5);
        const int jid = tm.EnqueueDelete({ dir }, true);
        bool deleting = false;
        for (int i = 0; i < 2000 && !deleting; ++i) {
            TransferJobSnapshot s;
            deleting = FindJobSnapshot(tm, jid, s) && s.doneFiles >= 3;
            if (!deleting) Sleep(5);
        }
        // The folder was listed as empty; now something lands in it while the job is busy elsewhere
        const bool arrived = deleting && PathUtils::WriteFileBytes(dir + L"\\listed_empty\\arrived_late.txt", "late", 4);
        const bool waited = tm.WaitForJob(jid, 30000);
        tm.SetDeleteItemDelayForTesting(0);

        TransferJobSnapshot snap;
        const bool have = FindJobSnapshot(tm, jid, snap);
        const bool ok = arrived && waited && have && snap.state == TransferJobState::Completed &&
                        snap.errors.empty() && !existsOnDisk(dir);
        wchar_t detail[160] = {};
        swprintf_s(detail, L"arrivedMidJob=%d, state=%d, errors=%zu, gone=%d",
            arrived ? 1 : 0, have ? static_cast<int>(snap.state) : -1, snap.errors.size(), existsOnDisk(dir) ? 0 : 1);
        if (existsOnDisk(dir)) RemoveTestTreeRecursive(dir);
        tc.Record(ok, L"Stage13.Test21_Delete_LateArrivals_SecondPass", detail);
    }

    // Test 13.22: the hidden copies kept for Ctrl+Z. What an earlier session left behind is removed at the
    // next start; the copies of a running session, this session's own and anything foreign are not touched.
    {
        const std::wstring root = baseDir + L"\\t22_undo_staging";
        CreateDirectoryW(root.c_str(), nullptr);
        auto put = [&](const wchar_t* rel) { PathUtils::WriteFileBytes(root + L"\\" + rel, "u", 1); };
        auto makeDir = [&](const wchar_t* rel) { CreateDirectoryW((root + L"\\" + rel).c_str(), nullptr); };

        put(L"undo_7_old_picture.png");            // kept the old way, directly in the staging folder
        makeDir(L"s_4242_1a2b3c");                 // abandoned session: no lock file
        put(L"s_4242_1a2b3c\\undo_1_a.txt");
        put(L"s_4242_1a2b3c\\undo_2_b.txt");
        makeDir(L"s_4243_ffff");                   // abandoned session: a lock file that nobody holds
        put(L"s_4243_ffff\\undo_1_c.txt");
        put(L"s_4243_ffff\\session.lock");
        makeDir(L"s_4244_abcd");                   // running session: its lock file is held open
        put(L"s_4244_abcd\\undo_1_live.txt");
        HANDLE liveLock = CreateFileW((root + L"\\s_4244_abcd\\session.lock").c_str(), GENERIC_WRITE, 0, nullptr,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        makeDir(L"s_4245_own");                    // this session's own folder
        put(L"s_4245_own\\undo_1_mine.txt");
        put(L"notes.txt");                         // not Explorer2's
        makeDir(L"other");
        put(L"other\\x.txt");
        const std::wstring ownDir = root + L"\\s_4245_own";

        // A session folder that is only seconds old may belong to a session that is just starting
        const size_t removedYoung = FileEngine::SweepStaleUndoStaging(root, ownDir, 3600);
        const bool youngKept = existsOnDisk(root + L"\\s_4242_1a2b3c\\undo_1_a.txt") &&
                               !existsOnDisk(root + L"\\undo_7_old_picture.png");

        const size_t removed = FileEngine::SweepStaleUndoStaging(root, ownDir, 0);
        const bool staleGone = !existsOnDisk(root + L"\\s_4242_1a2b3c") && !existsOnDisk(root + L"\\s_4243_ffff");
        const bool liveKept = liveLock != INVALID_HANDLE_VALUE && existsOnDisk(root + L"\\s_4244_abcd\\undo_1_live.txt");
        const bool ownKept = existsOnDisk(root + L"\\s_4245_own\\undo_1_mine.txt");
        const bool foreignKept = existsOnDisk(root + L"\\notes.txt") && existsOnDisk(root + L"\\other\\x.txt");

        // When the running session ends, its lock file disappears with the handle and its copies are leftovers
        if (liveLock != INVALID_HANDLE_VALUE) CloseHandle(liveLock);
        const size_t removedAfterExit = FileEngine::SweepStaleUndoStaging(root, ownDir, 0);
        const bool liveGoneAfterExit = !existsOnDisk(root + L"\\s_4244_abcd");

        // The staging folder itself goes once nothing is left in it
        DeleteFileW((root + L"\\notes.txt").c_str());
        DeleteFileW((root + L"\\other\\x.txt").c_str());
        RemoveDirectoryW((root + L"\\other").c_str());
        const size_t removedLast = FileEngine::SweepStaleUndoStaging(root, L"", 0);
        const bool rootGone = !existsOnDisk(root);

        const bool ok = removedYoung == 1 && youngKept && removed == 2 && staleGone && liveKept && ownKept &&
                        foreignKept && removedAfterExit == 1 && liveGoneAfterExit && removedLast == 1 && rootGone;
        wchar_t detail[256] = {};
        swprintf_s(detail, L"removed=%zu+%zu+%zu+%zu, youngKept=%d, staleGone=%d, liveKept=%d, ownKept=%d, foreignKept=%d, liveGoneAfterExit=%d, folderGone=%d",
            removedYoung, removed, removedAfterExit, removedLast, youngKept ? 1 : 0, staleGone ? 1 : 0, liveKept ? 1 : 0,
            ownKept ? 1 : 0, foreignKept ? 1 : 0, liveGoneAfterExit ? 1 : 0, rootGone ? 1 : 0);
        if (!rootGone) RemoveTestTreeRecursive(root);
        tc.Record(ok, L"Stage13.Test22_Delete_UndoCopies_LeftoversRemoved_LiveKept", detail);
    }

    auto readSmallFile = [](const std::wstring& p) {
        std::vector<uint8_t> bytes;
        PathUtils::ReadFileBytes(p, 4096, bytes);
        return std::string(bytes.begin(), bytes.end());
    };

    // Test 13.23: unpacking an archive never replaces a file that is already in the destination, and an
    // entry whose name points outside the destination ("..", a drive) is refused instead of written
    {
        const std::wstring dir = baseDir + L"\\t23_extract";
        const std::wstring dest = dir + L"\\dest";
        CreateDirectoryW(dir.c_str(), nullptr);
        CreateDirectoryW(dest.c_str(), nullptr);
        CreateDirectoryW((dest + L"\\docs").c_str(), nullptr);
        PathUtils::WriteFileBytes(dest + L"\\docs\\readme.txt", "old", 3);

        auto bytesOf = [](const char* s) { return std::vector<uint8_t>(s, s + strlen(s)); };
        const std::vector<uint8_t> zip = BuildStoreZipForTest({
            { "docs/readme.txt", bytesOf("new") },
            { "docs/fresh.txt", bytesOf("fresh") },
            { "../outside.txt", bytesOf("escape 1") },
            { "docs/../../outside2.txt", bytesOf("escape 2") },
            { "C:/e2_selftest_absolute.txt", bytesOf("escape 3") },
        });
        const std::wstring zipPath = dir + L"\\bundle.zip";
        PathUtils::WriteFileBytes(zipPath, zip.data(), zip.size());

        size_t keptExisting = 0;
        size_t refused = 0;
        const bool extracted = ArchiveExplorer::ExtractEntireArchiveToFolder(zipPath, dest, &keptExisting, &refused);
        const bool oldKept = readSmallFile(dest + L"\\docs\\readme.txt") == "old";
        const bool newBeside = readSmallFile(dest + L"\\docs\\readme (2).txt") == "new";
        const bool freshOk = readSmallFile(dest + L"\\docs\\fresh.txt") == "fresh";
        const bool nothingOutside = !existsOnDisk(dir + L"\\outside.txt") && !existsOnDisk(dir + L"\\outside2.txt") &&
                                    !existsOnDisk(baseDir + L"\\outside2.txt") && !existsOnDisk(dest + L"\\outside.txt");

        wchar_t detail[192] = {};
        swprintf_s(detail, L"extracted=%d, oldKept=%d, newBeside=%d, fresh=%d, nothingOutside=%d, keptExisting=%zu, refused=%zu",
            extracted ? 1 : 0, oldKept ? 1 : 0, newBeside ? 1 : 0, freshOk ? 1 : 0, nothingOutside ? 1 : 0, keptExisting, refused);
        tc.Record(extracted && oldKept && newBeside && freshOk && nothingOutside && keptExisting == 1 && refused == 3,
            L"Stage13.Test23_Extract_KeepsExistingFiles_And_StaysInsideDestination", detail);
    }

    // Test 13.24: undoing a delete never replaces what is in the old place by then
    {
        const std::wstring dir = baseDir + L"\\t24_undo";
        CreateDirectoryW(dir.c_str(), nullptr);
        const std::wstring report = dir + L"\\report.txt";
        const std::wstring untouched = dir + L"\\untouched.txt";
        PathUtils::WriteFileBytes(report, "first version", 13);
        PathUtils::WriteFileBytes(untouched, "still here", 10);

        FileEngine fe;
        fe.Initialize(nullptr);
        std::wstring restoredDir;

        // 1) The file is deleted, and a new file takes its name before the user asks for the old one back
        fe.BeginDelete({ report }, false);
        DeleteFileW(report.c_str());                                   // stands in for the move to the Recycle Bin
        PathUtils::WriteFileBytes(report, "second version", 14);
        const bool undone = fe.UndoLastDelete(restoredDir);
        const bool newUntouched = readSmallFile(report) == "second version";
        const bool oldBeside = readSmallFile(dir + L"\\report (restored).txt") == "first version";

        // 2) The delete never went through, so the file is still in place: undo must not give it a second name
        fe.BeginDelete({ untouched }, false);
        const bool undoneInPlace = fe.UndoLastDelete(restoredDir);
        const bool noSecondName = readSmallFile(untouched) == "still here" && !existsOnDisk(dir + L"\\untouched (restored).txt");
        fe.EndDelete({ report, untouched });
        fe.Shutdown();

        if (!undone && !undoneInPlace) {
            // No undo copies can be kept on this drive (no hard links, or its root is not writable): nothing to test
            tc.Record(newUntouched, L"Stage13.Test24_Undo_NeverReplacesANewerFile", L"skipped: this drive keeps no undo copies");
        } else {
            wchar_t detail[160] = {};
            swprintf_s(detail, L"undone=%d, newerFileUntouched=%d, oldOneBeside=%d, inPlaceUndo=%d, noSecondName=%d",
                undone ? 1 : 0, newUntouched ? 1 : 0, oldBeside ? 1 : 0, undoneInPlace ? 1 : 0, noSecondName ? 1 : 0);
            tc.Record(undone && newUntouched && oldBeside && undoneInPlace && noSecondName,
                L"Stage13.Test24_Undo_NeverReplacesANewerFile", detail);
        }
    }

    // Test 13.25: damaged files. Four readers used to crash or run out of memory on these exact inputs
    // (found by tools/fuzz_samples.py); they now have to turn them down or read them, and without needing
    // the safety net. The safety net itself is checked with a reader that fails on purpose.
    {
        const std::wstring dir = baseDir + L"\\t25_damaged";
        CreateDirectoryW(dir.c_str(), nullptr);
        const std::wstring samples = GetTestDataDir();
        auto damagedCopy = [&](const wchar_t* sample, const std::function<bool(std::vector<uint8_t>&)>& damage, std::wstring& outPath) {
            std::vector<uint8_t> bytes;
            if (!PathUtils::ReadFileBytes(samples + L"\\" + sample, 8 * 1024 * 1024, bytes) || !damage(bytes)) return false;
            outPath = dir + L"\\damaged_" + sample;
            return PathUtils::WriteFileBytes(outPath, bytes.data(), bytes.size());
        };
        auto replaceText = [](std::vector<uint8_t>& bytes, const std::string& from, const std::string& to) {
            std::string text(bytes.begin(), bytes.end());
            const size_t at = text.find(from);
            if (at == std::string::npos) return false;
            text.replace(at, from.size(), to);
            bytes.assign(text.begin(), text.end());
            return true;
        };
        auto put32 = [](std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
            if (at + 4 > bytes.size()) return false;
            memcpy(bytes.data() + at, &value, 4);
            return true;
        };

        const uint64_t stoppedBefore = ReaderGuard::StoppedCount();
        std::wstring md2Path, plyPath, xpmPath, aniPath;
        const bool prepared =
            damagedCopy(L"quake2_sphere.md2", [&](std::vector<uint8_t>& b) { return put32(b, 24, 0x7FFFFFFFu); }, md2Path) &&   // vertex count
            damagedCopy(L"octahedron_mesh.ply", [&](std::vector<uint8_t>& b) { return replaceText(b, "element vertex 6", "element vertex 99999999999"); }, plyPath) &&
            damagedCopy(L"hopper.xpm", [&](std::vector<uint8_t>& b) { return replaceText(b, "128 128 44 1", "128 128 4294967295 1"); }, xpmPath) &&
            damagedCopy(L"sample.ani", [&](std::vector<uint8_t>& b) { return put32(b, 82, 0xFFFFFFFFu); }, aniPath);         // image offset

        LoadedMesh3D md2Mesh, plyMesh;
        ExtendedImageInfo xpmImage, aniImage;
        bool readersBehaved = false;
        if (prepared) {
            Model3DViewer::InspectMeshFile(md2Path, md2Mesh);
            Model3DViewer::InspectMeshFile(plyPath, plyMesh);
            const bool xpmAccepted = ImageFormatLoader::DecodeImageFile(xpmPath, 512, xpmImage);
            ImageFormatLoader::DecodeImageFile(aniPath, 512, aniImage);
            readersBehaved = md2Mesh.vertices.size() < 100000 && plyMesh.vertices.size() < 1000 && !xpmAccepted &&
                             aniImage.bgraPixels.size() <= 256u * 256u * 4u;
        }
        const bool netNotNeeded = ReaderGuard::StoppedCount() == stoppedBefore;

        // The safety net: an exception that escapes a reader ends there and is put on record
        struct FailingReader {
            static bool Read(const std::wstring& path, int& out) try {
                out = 1;
                throw std::bad_alloc();
            } E2_READER_CATCH(L"Test reader", path, { out = -1; return false; })
        };
        int netOut = 0;
        const bool netReturned = !FailingReader::Read(L"deliberately_failing.bin", netOut) && netOut == -1;
        const bool netCounted = ReaderGuard::StoppedCount() == stoppedBefore + 1 && !ReaderGuard::LastMessage().empty();
        bool netLogged = false;
        for (const auto& entry : AppLog::Snapshot()) {
            if (entry.category == L"Reader" && entry.message.find(L"deliberately_failing.bin") != std::wstring::npos) netLogged = true;
        }

        wchar_t detail[224] = {};
        swprintf_s(detail, L"prepared=%d, readersBehaved=%d (md2 verts=%zu, ply verts=%zu), safetyNetNotNeeded=%d, safetyNet: returned=%d, counted=%d, logged=%d",
            prepared ? 1 : 0, readersBehaved ? 1 : 0, md2Mesh.vertices.size(), plyMesh.vertices.size(), netNotNeeded ? 1 : 0,
            netReturned ? 1 : 0, netCounted ? 1 : 0, netLogged ? 1 : 0);
        tc.Record(prepared && readersBehaved && netNotNeeded && netReturned && netCounted && netLogged,
            L"Stage13.Test25_DamagedFiles_TurnedDown_And_ReaderSafetyNet", detail);
    }

    // Test 13.26: the program ends without an error after a PDF page was shown. Windows' PDF library has to
    // be unloaded before the process ends (PdfViewer::ReleaseRenderer); left loaded, it frees its graphics
    // device during process teardown and Windows ends the program with exception 0x87A. Checked twice: in
    // this process the library is loaded by a page and gone after the release, and a process of its own
    // shows a page and then ends the way the program ends, which has to give exit code 0.
    {
        const std::wstring pdfPath = baseDir + L"\\t26_page.pdf";
        const bool wrote = WriteRenderablePdfForTest(pdfPath);
        auto libraryLoaded = []() { return GetModuleHandleW(L"Windows.Data.Pdf.dll") != nullptr; };
        std::vector<uint8_t> bgra;
        uint32_t w = 0, h = 0, pages = 0;
        const bool rendered = wrote && PdfViewer::RenderPdfPageToBgra(pdfPath, 0, 300, bgra, w, h, &pages) && w == 300;
        const bool loadedByPage = libraryLoaded();
        PdfViewer::ReleaseRenderer();
        const bool released = !libraryLoaded();
        const bool renderedAgain = PdfViewer::RenderPdfPageToBgra(pdfPath, 0, 300, bgra, w, h, &pages) && w == 300;
        PdfViewer::ReleaseRenderer();
        const bool releasedAgain = !libraryLoaded();

        const std::wstring report = baseDir + L"\\t26_child_report.txt";
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(exePath) + L"\" --self-test pdf-exit-child \"" + report + L"\"";
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        DWORD childExit = 0xFFFFFFFFu;
        bool childEnded = false;
        if (CreateProcessW(exePath, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            if (WaitForSingleObject(pi.hProcess, 60000) == WAIT_OBJECT_0) {
                childEnded = GetExitCodeProcess(pi.hProcess, &childExit) != 0;
            } else {
                TerminateProcess(pi.hProcess, 1);
            }
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        std::vector<uint8_t> reportBytes;
        PathUtils::ReadFileBytes(report, 4096, reportBytes);
        const std::string reportText(reportBytes.begin(), reportBytes.end());
        const bool childShowedPage = reportText.find("drew=1 rendered=1 libraryLoaded=1") != std::string::npos;

        wchar_t detail[256] = {};
        swprintf_s(detail, L"here: page=%d, libraryLoaded=%d, released=%d, pageAgain=%d, releasedAgain=%d; own process: ended=%d, page=%d, exitCode=0x%lX",
            rendered ? 1 : 0, loadedByPage ? 1 : 0, released ? 1 : 0, renderedAgain ? 1 : 0, releasedAgain ? 1 : 0,
            childEnded ? 1 : 0, childShowedPage ? 1 : 0, childExit);
        tc.Record(rendered && loadedByPage && released && renderedAgain && releasedAgain && childEnded && childShowedPage && childExit == 0,
            L"Stage13.Test26_PdfPage_ThenCleanExit_LibraryReleased", detail);
    }

    // Test 13.27: packed data that is damaged or ends too early. The unpacker used to read zeros past the
    // end of its input and to turn them into output until it hit its size limit: a damaged stream of 580
    // bytes produced 128 MB and held the PDF reader up for seconds (found by tools/fuzz_samples.py). It now
    // stops where the input ends, so it can never produce more than DEFLATE allows (1032 bytes per byte).
    {
        std::vector<uint8_t> pdf;
        const bool haveSample = PathUtils::ReadFileBytes(GetTestDataDir() + L"\\minimal_document.pdf", 8 * 1024 * 1024, pdf);
        // The sample's first stream: zlib data between "stream\n" and "endstream"
        size_t streamStart = 0, streamLen = 0;
        if (haveSample) {
            const std::string text(pdf.begin(), pdf.end());
            const size_t keyword = text.find("stream\n");
            const size_t end = (keyword == std::string::npos) ? std::string::npos : text.find("endstream", keyword);
            if (end != std::string::npos) {
                streamStart = keyword + 7;
                streamLen = end - streamStart;
                while (streamLen > 0 && (pdf[streamStart + streamLen - 1] == '\n' || pdf[streamStart + streamLen - 1] == '\r')) --streamLen;
            }
        }
        const bool haveStream = streamLen > 64 && streamStart < 90 && streamStart + streamLen <= pdf.size();
        const size_t mostPossible = streamLen * 1032 + 1024;

        std::vector<uint8_t> whole, fromDamaged, fromCut;
        bool wholeOk = false, damagedBounded = false, cutTurnedDown = false, pdfStillReadQuickly = false;
        ULONGLONG pdfMs = 0;
        if (haveStream) {
            wholeOk = ZipDeflate::InflateZlib(pdf.data() + streamStart, streamLen, whole) && !whole.empty() && whole.size() <= mostPossible;

            // The damage the robustness run found: a 32-bit value written over the block's code tables
            std::vector<uint8_t> damaged = pdf;
            const uint32_t extreme = 0x7FFFFFFFu;
            memcpy(damaged.data() + 90, &extreme, 4);
            ZipDeflate::InflateZlib(damaged.data() + streamStart, streamLen, fromDamaged);
            damagedBounded = fromDamaged.size() <= mostPossible;

            // The same stream cut off in the middle of its block
            cutTurnedDown = !ZipDeflate::InflateZlib(pdf.data() + streamStart, streamLen / 2, fromCut) && fromCut.size() <= mostPossible;

            const std::wstring damagedPath = baseDir + L"\\t27_damaged_stream.pdf";
            PathUtils::WriteFileBytes(damagedPath, damaged.data(), damaged.size());
            PdfDocumentSummary summary;
            const ULONGLONG started = GetTickCount64();
            const bool stillAPdf = PdfViewer::InspectPdfFile(damagedPath, summary) && summary.valid;
            pdfMs = GetTickCount64() - started;
            pdfStillReadQuickly = stillAPdf && pdfMs < 1500;
        }

        wchar_t detail[288] = {};
        swprintf_s(detail, L"sample=%d, stream=%zu bytes, whole: ok=%d (%zu bytes), damaged: %zu bytes out (limit %zu) bounded=%d, cut: turnedDown=%d, damaged PDF read in %llu ms ok=%d",
            haveSample ? 1 : 0, streamLen, wholeOk ? 1 : 0, whole.size(), fromDamaged.size(), mostPossible, damagedBounded ? 1 : 0,
            cutTurnedDown ? 1 : 0, pdfMs, pdfStillReadQuickly ? 1 : 0);
        tc.Record(haveSample && haveStream && wholeOk && damagedBounded && cutTurnedDown && pdfStillReadQuickly,
            L"Stage13.Test27_Unpacker_StopsWhereItsInputEnds", detail);
    }

    // Test 13.28: external archive tools (7-Zip, UnRAR, tar) are run with a time limit when a preview waits
    // for them. A tool that runs over its time has to be ended, not left working in the background while
    // the program moves on. Checked with programs every Windows has: one that succeeds, one that fails,
    // and one that takes half a minute and gets one second.
    {
        wchar_t sysDir[MAX_PATH] = {};
        GetSystemDirectoryW(sysDir, MAX_PATH);
        const std::wstring cmdExe = std::wstring(sysDir) + L"\\cmd.exe";
        const std::wstring pingExe = std::wstring(sysDir) + L"\\PING.EXE";

        // Number of ping.exe processes that this process started and that are still running
        auto ourPingsRunning = []() {
            int count = 0;
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snap == INVALID_HANDLE_VALUE) return -1;
            PROCESSENTRY32W pe = {};
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(snap, &pe)) {
                do {
                    if (pe.th32ParentProcessID == GetCurrentProcessId() && _wcsicmp(pe.szExeFile, L"PING.EXE") == 0) ++count;
                } while (Process32NextW(snap, &pe));
            }
            CloseHandle(snap);
            return count;
        };

        const bool succeeded = ArchiveExplorer::RunToolAndWait(cmdExe, L"/c exit 0", 15000) == ArchiveExplorer::ToolRun::Succeeded;
        const bool failed = ArchiveExplorer::RunToolAndWait(cmdExe, L"/c exit 3", 15000) == ArchiveExplorer::ToolRun::Failed;
        const bool missing = ArchiveExplorer::RunToolAndWait(baseDir + L"\\no_such_tool.exe", L"", 15000) == ArchiveExplorer::ToolRun::Failed;

        const int pingsBefore = ourPingsRunning();
        const ULONGLONG started = GetTickCount64();
        const bool timedOut = ArchiveExplorer::RunToolAndWait(pingExe, L"-n 30 127.0.0.1", 1000) == ArchiveExplorer::ToolRun::TimedOut;
        const ULONGLONG tookMs = GetTickCount64() - started;
        const int pingsAfter = ourPingsRunning();

        wchar_t detail[224] = {};
        swprintf_s(detail, L"succeeded=%d, failed=%d, missingTool=%d, slow tool: timedOut=%d after %llu ms, still running: %d before / %d after",
            succeeded ? 1 : 0, failed ? 1 : 0, missing ? 1 : 0, timedOut ? 1 : 0, tookMs, pingsBefore, pingsAfter);
        tc.Record(succeeded && failed && missing && timedOut && tookMs < 10000 && pingsBefore == 0 && pingsAfter == 0,
            L"Stage13.Test28_ExternalTool_EndedWhenItsTimeIsUp", detail);
    }

    // Test 13.29: the main window draws its text through TextLayoutCache, which keeps the layouts of earlier
    // paints. Drawing through the cache has to give exactly the picture DrawText gives, also after the
    // shared format's alignment was changed between calls (the drawing code does that all the time); a
    // second paint has to reuse every layout; and the cache has to stay within its size.
    {
        Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
        Microsoft::WRL::ComPtr<ID2D1Factory> d2d;
        Microsoft::WRL::ComPtr<IDWriteFactory> dwrite;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        bool ready = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
                     SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())) &&
                     SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()))) &&
                     SUCCEEDED(dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                                        DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"pl-PL", &format));
        if (ready) {
            // Set up like the window's own formats: one line, centred vertically, cut off with an ellipsis
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
            dwrite->CreateEllipsisTrimmingSign(format.Get(), &ellipsis);
            format->SetTrimming(&trim, ellipsis.Get());
        }
        const wchar_t* texts[] = {
            L"Dokumenty", L"Zażółć gęślą jaźń", L"\x263E", L"PL \x21C4 EN", L"\x25BE", L"348.1 MB",
            L"A name long enough to be cut off with an ellipsis at the end of its box.txt", L""
        };
        const DWRITE_TEXT_ALIGNMENT alignments[] = { DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_TEXT_ALIGNMENT_TRAILING };
        constexpr UINT kW = 420, kH = 8 * 24;
        TextLayoutCache cache;
        auto paint = [&](bool useCache, std::vector<uint8_t>& pixels) -> bool {
            Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
            Microsoft::WRL::ComPtr<ID2D1RenderTarget> rt;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
            if (FAILED(wic->CreateBitmap(kW, kH, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bitmap)) ||
                FAILED(d2d->CreateWicBitmapRenderTarget(bitmap.Get(), D2D1::RenderTargetProperties(), &rt)) ||
                FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(0x1E1E1E), &brush))) {
                return false;
            }
            rt->BeginDraw();
            rt->Clear(D2D1::ColorF(D2D1::ColorF::White));
            for (int i = 0; i < 8; ++i) {
                for (int a = 0; a < 3; ++a) {
                    format->SetTextAlignment(alignments[a]);      // changed between calls, as the window does
                    const D2D1_RECT_F box = D2D1::RectF(4.0f + a * 138.0f, i * 24.0f, 4.0f + a * 138.0f + 130.0f, i * 24.0f + 22.0f);
                    const UINT32 len = static_cast<UINT32>(wcslen(texts[i]));
                    if (useCache) cache.Draw(dwrite.Get(), rt.Get(), texts[i], len, format.Get(), box, brush.Get());
                    else rt->DrawText(texts[i], len, format.Get(), box, brush.Get());
                }
            }
            if (FAILED(rt->EndDraw())) return false;
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            pixels.assign(static_cast<size_t>(kW) * kH * 4, 0);
            return SUCCEEDED(bitmap->CopyPixels(nullptr, kW * 4, static_cast<UINT>(pixels.size()), pixels.data()));
        };
        std::vector<uint8_t> direct, firstPaint, secondPaint;
        bool painted = ready && paint(false, direct) && paint(true, firstPaint);
        const uint64_t missesAfterFirst = cache.Misses();
        const uint64_t hitsBefore = cache.Hits();
        painted = painted && paint(true, secondPaint);
        const bool samePicture = painted && direct == firstPaint && direct == secondPaint;
        size_t inkPixels = 0;
        for (size_t p = 0; p < direct.size(); p += 4) {
            if (direct[p] < 0x80) ++inkPixels;
        }
        const bool reused = painted && missesAfterFirst == 24 && cache.Size() == 24 &&
                            cache.Hits() - hitsBefore == 24 && cache.Misses() == missesAfterFirst;

        TextLayoutCache fourOnly(4);       // ("small" is a macro in the Windows headers)
        for (int i = 0; ready && i < 7; ++i) {
            fourOnly.Get(dwrite.Get(), texts[i], static_cast<UINT32>(wcslen(texts[i])), format.Get(), D2D1::RectF(0, 0, 100, 20));
        }
        bool bounded = fourOnly.Size() == 4;

        // A box turned inside out draws nothing, as with DrawText; a long text (a git log) is drawn but not kept
        bool invertedDrawsNothing = false, longDrawnNotKept = false;
        if (ready) {
            Microsoft::WRL::ComPtr<IWICBitmap> bmp;
            Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ink;
            if (SUCCEEDED(wic->CreateBitmap(kW, kH, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bmp)) &&
                SUCCEEDED(d2d->CreateWicBitmapRenderTarget(bmp.Get(), D2D1::RenderTargetProperties(), &target)) &&
                SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(0x1E1E1E), &ink))) {
                auto inkAfter = [&](const std::function<void()>& draw) {
                    target->BeginDraw();
                    target->Clear(D2D1::ColorF(D2D1::ColorF::White));
                    draw();
                    target->EndDraw();
                    std::vector<uint8_t> px(static_cast<size_t>(kW) * kH * 4);
                    bmp->CopyPixels(nullptr, kW * 4, static_cast<UINT>(px.size()), px.data());
                    size_t n = 0;
                    for (size_t i = 0; i < px.size(); i += 4) if (px[i] < 0x80) ++n;
                    return n;
                };
                const size_t keptBefore = cache.Size();
                const size_t invertedInk = inkAfter([&]() {
                    cache.Draw(dwrite.Get(), target.Get(), L"\x203A", 1, format.Get(), D2D1::RectF(200.0f, 10.0f, 190.0f, 30.0f), ink.Get());
                });
                const std::wstring longText(600, L'W');
                const size_t longInk = inkAfter([&]() {
                    cache.Draw(dwrite.Get(), target.Get(), longText.c_str(), static_cast<UINT32>(longText.size()), format.Get(),
                               D2D1::RectF(0.0f, 0.0f, static_cast<float>(kW), 40.0f), ink.Get());
                });
                invertedDrawsNothing = invertedInk == 0;
                longDrawnNotKept = longInk > 100 && cache.Size() == keptBefore;
            }
        }
        bounded = bounded && invertedDrawsNothing && longDrawnNotKept;

        wchar_t detail[224] = {};
        swprintf_s(detail, L"ready=%d, painted=%d, samePicture=%d (%zu ink pixels), layouts: made=%llu, kept=%zu, reused on 2nd paint=%llu, bounded/inverted/long=%d",
            ready ? 1 : 0, painted ? 1 : 0, samePicture ? 1 : 0, inkPixels, static_cast<unsigned long long>(missesAfterFirst), cache.Size(),
            static_cast<unsigned long long>(cache.Hits() - hitsBefore), bounded ? 1 : 0);
        tc.Record(ready && painted && samePicture && inkPixels > 500 && reused && bounded,
            L"Stage13.Test29_TextLayouts_ReusedBetweenPaints_SamePicture", detail);
    }

    // Test 13.30: opening a folder does not wait for file-type icons. Asking Windows for a type's icon reads the
    // registry and took a few hundred milliseconds on the UI thread for a folder full of types not seen before.
    // Types not seen before are now listed with the generic icon at once, a worker looks the real icons up, the
    // window is told (WM_APP_EXT_ICONS_READY), and the listing then shows exactly the icons Windows gives.
    {
        const std::wstring dir = baseDir + L"\\t30_icons";
        CreateDirectoryW(dir.c_str(), nullptr);
        const wchar_t* candidates[] = { L".reg", L".vbs", L".cab", L".ttf", L".msc", L".hta", L".wsf", L".cpl", L".theme", L".evtx", L".diagcab" };
        std::vector<std::wstring> fresh;
        for (const wchar_t* ext : candidates) {
            if (fresh.size() < 4 && !FileEngine::IsTypeIconKnown(ext)) fresh.push_back(ext);
        }
        for (const auto& ext : fresh) {
            const char data[] = "sample";
            PathUtils::WriteFileBytes(dir + L"\\sample" + ext, data, sizeof(data));
        }
        HWND notify = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        FileEngine fe;
        fe.Initialize(notify);
        auto entries = fe.EnumerateDirectory(dir, true, true, Lang::EN);
        int standIn = -2;
        bool standInFirst = entries.size() == fresh.size() && !fresh.empty();
        for (const auto& e : entries) {
            if (standIn == -2) standIn = e.sysIconIndex;
            standInFirst = standInFirst && e.sysIconIndex == standIn;
        }
        const bool resolved = fe.WaitUntilIconsResolved(15000);
        const bool changed = fe.RefreshIconIndices(entries);
        bool matchesWindows = !entries.empty();
        int ownIcons = 0;
        for (const auto& e : entries) {
            SHFILEINFOW sfi = {};
            SHGetFileInfoW((L"file" + e.ext).c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES);
            if (e.sysIconIndex != sfi.iIcon) matchesWindows = false;
            if (e.sysIconIndex != standIn) ++ownIcons;
        }
        bool notified = false;
        MSG msg = {};
        while (notify && PeekMessageW(&msg, notify, WM_APP_EXT_ICONS_READY, WM_APP_EXT_ICONS_READY, PM_REMOVE)) notified = true;
        fe.Shutdown();
        if (notify) DestroyWindow(notify);

        wchar_t detail[224] = {};
        swprintf_s(detail, L"types not seen before: %zu, listed at once with the generic icon=%d, looked up=%d, icons updated=%d, same as Windows=%d (%d with an icon of their own), window told=%d",
            fresh.size(), standInFirst ? 1 : 0, resolved ? 1 : 0, changed ? 1 : 0, matchesWindows ? 1 : 0, ownIcons, notified ? 1 : 0);
        tc.Record(fresh.size() >= 2 && standInFirst && resolved && changed && matchesWindows && ownIcons >= 1 && notified,
            L"Stage13.Test30_FolderListing_DoesNotWaitForTypeIcons", detail);
    }

    // Test 13.31: a paint makes only as many new icons as fit in its time budget and says that it left some for
    // the next paint; with time left, or outside a paint (no budget), the icon is made at once. Every paint
    // makes at least two, even with its budget spent before it started, so that a slow machine still gets
    // its icons instead of repainting for ever. (Switching to a new icon size used to make every icon of the
    // folder in one paint: 138 ms measured.)
    {
        Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
        Microsoft::WRL::ComPtr<ID2D1Factory> d2d;
        Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> rt;
        const bool ready = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
                           SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())) &&
                           SUCCEEDED(wic->CreateBitmap(64, 64, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bitmap)) &&
                           SUCCEEDED(d2d->CreateWicBitmapRenderTarget(bitmap.Get(), D2D1::RenderTargetProperties(), &rt));
        // Three different icons
        std::vector<int> three;
        for (const wchar_t* name : { L"file.txt", L"file.pdf", L"file.zip", L"file.dll", L"file.bat", L"file.ini" }) {
            SHFILEINFOW sfi = {};
            SHGetFileInfoW(name, FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES);
            if (three.size() < 3 && std::find(three.begin(), three.end(), sfi.iIcon) == three.end()) three.push_back(sfi.iIcon);
        }
        SHFILEINFOW dir = {};
        SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &dir, sizeof(dir), SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES);
        bool firstTwoMade = false, thirdLeft = false, deferredFlag = false, madeWithTime = false, noRepaintNeeded = false, madeOutsidePaint = false;
        {
            ImageCache icons;                       // not initialized: system icons need no worker threads
            if (ready && three.size() == 3) {
                icons.BeginPaintBudget(0.0);        // a paint whose budget is already spent
                firstTwoMade = static_cast<bool>(icons.GetSystemIcon(rt.Get(), three[0], 1)) &&
                               static_cast<bool>(icons.GetSystemIcon(rt.Get(), three[1], 1));
                thirdLeft = !icons.GetSystemIcon(rt.Get(), three[2], 1);
                deferredFlag = icons.TakePaintBudgetExceeded();
                icons.BeginPaintBudget(1000.0);
                madeWithTime = static_cast<bool>(icons.GetSystemIcon(rt.Get(), three[2], 1));
                noRepaintNeeded = !icons.TakePaintBudgetExceeded();
                madeOutsidePaint = static_cast<bool>(icons.GetSystemIcon(rt.Get(), dir.iIcon, 0));
            }
            icons.DiscardDeviceResources();
        }
        wchar_t detail[224] = {};
        swprintf_s(detail, L"ready=%d, budget spent: first two made anyway=%d, third left for the next paint=%d (told=%d); time left: made=%d (no repaint needed=%d); outside a paint: made=%d",
            ready ? 1 : 0, firstTwoMade ? 1 : 0, thirdLeft ? 1 : 0, deferredFlag ? 1 : 0, madeWithTime ? 1 : 0, noRepaintNeeded ? 1 : 0, madeOutsidePaint ? 1 : 0);
        tc.Record(ready && firstTwoMade && thirdLeft && deferredFlag && madeWithTime && noRepaintNeeded && madeOutsidePaint,
            L"Stage13.Test31_Paint_MakesNewIconsWithinItsBudget", detail);
    }

    // Test 13.32: in the running program, system icons are made by the icon worker, not by the paint. Windows
    // extracts some icons from their files on first use, up to tens of milliseconds for a single icon, which
    // made switching to another icon size stall for 130-190 ms (measured). The first request returns nothing
    // (the window draws a placeholder), the worker tells the window, and the next request has the icon.
    {
        Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
        Microsoft::WRL::ComPtr<ID2D1Factory> d2d;
        Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> rt;
        const bool ready = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
                           SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())) &&
                           SUCCEEDED(wic->CreateBitmap(64, 64, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bitmap)) &&
                           SUCCEEDED(d2d->CreateWicBitmapRenderTarget(bitmap.Get(), D2D1::RenderTargetProperties(), &rt));
        SHFILEINFOW pdf = {};
        SHGetFileInfoW(L"file.pdf", FILE_ATTRIBUTE_NORMAL, &pdf, sizeof(pdf), SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES);
        HWND notify = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        bool notAtOnce = false, told = false, madeAfter = false, sameAgain = false;
        ULONGLONG waitedMs = 0;
        {
            ImageCache icons;
            icons.Initialize(notify);
            if (ready && notify) {
                notAtOnce = !icons.GetSystemIcon(rt.Get(), pdf.iIcon, 2);
                const ULONGLONG started = GetTickCount64();
                MSG msg = {};
                while (!told && GetTickCount64() - started < 15000) {
                    if (PeekMessageW(&msg, notify, WM_APP_THUMB_READY, WM_APP_THUMB_READY, PM_REMOVE)) told = true;
                    else Sleep(5);
                }
                waitedMs = GetTickCount64() - started;
                const ImageCache::SystemIcon made = icons.GetSystemIcon(rt.Get(), pdf.iIcon, 2);
                madeAfter = static_cast<bool>(made);
                const ImageCache::SystemIcon again = icons.GetSystemIcon(rt.Get(), pdf.iIcon, 2);
                sameAgain = madeAfter && again.texture == made.texture && again.source.left == made.source.left &&
                            again.source.top == made.source.top;
            }
            icons.DiscardDeviceResources();
            icons.Shutdown();
        }
        if (notify) DestroyWindow(notify);
        wchar_t detail[192] = {};
        swprintf_s(detail, L"ready=%d, first request: nothing yet=%d; worker told the window=%d after %llu ms; then made=%d, kept=%d",
            ready ? 1 : 0, notAtOnce ? 1 : 0, told ? 1 : 0, waitedMs, madeAfter ? 1 : 0, sameAgain ? 1 : 0);
        tc.Record(ready && notAtOnce && told && madeAfter && sameAgain,
            L"Stage13.Test32_SystemIcons_MadeByTheIconWorker", detail);
    }

    // Test 13.33: the icons of one size share a texture (a new icon is copied into a free cell instead of
    // getting a texture of its own, which made the graphics driver stall when a view showed many new icons).
    // Every icon gets a place of its own, places keep a free pixel between them (so that drawing one scaled
    // up never picks up a neighbour), and drawing one stays within the box it was given.
    {
        Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
        Microsoft::WRL::ComPtr<ID2D1Factory> d2d;
        Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> rt;
        constexpr UINT kSide = 256;
        const bool ready = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
                           SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())) &&
                           SUCCEEDED(wic->CreateBitmap(kSide, kSide, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bitmap)) &&
                           SUCCEEDED(d2d->CreateWicBitmapRenderTarget(bitmap.Get(), D2D1::RenderTargetProperties(), &rt));
        const wchar_t* names[] = { L"a.txt", L"a.pdf", L"a.zip", L"a.dll", L"a.bat", L"a.ini", L"a.png", L"a.html", L"a.reg", L"a.cab", L"a.ttf", L"a.wav" };
        std::vector<int> indices;
        for (const wchar_t* name : names) {
            SHFILEINFOW sfi = {};
            SHGetFileInfoW(name, FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES);
            if (std::find(indices.begin(), indices.end(), sfi.iIcon) == indices.end()) indices.push_back(sfi.iIcon);
        }
        bool allMade = ready && indices.size() >= 6, oneTexture = true, apart = true, inPlace = false;
        size_t inkInside = 0, inkOutside = 0;
        {
            ImageCache icons;                       // not initialized: icons are made at once
            std::vector<ImageCache::SystemIcon> made;
            for (int index : indices) {
                const ImageCache::SystemIcon icon = ready ? icons.GetSystemIcon(rt.Get(), index, 1) : ImageCache::SystemIcon();
                if (!icon) allMade = false;
                else made.push_back(icon);
            }
            for (size_t i = 0; i < made.size(); ++i) {
                if (made[i].texture != made[0].texture) oneTexture = false;
                for (size_t j = i + 1; j < made.size(); ++j) {
                    const D2D1_RECT_F& a = made[i].source;
                    const D2D1_RECT_F& b = made[j].source;
                    // apart: not even touching (each keeps its border)
                    if (a.left < b.right + 1.0f && b.left < a.right + 1.0f && a.top < b.bottom + 1.0f && b.top < a.bottom + 1.0f) apart = false;
                }
            }
            if (allMade && !made.empty()) {
                // Each icon drawn three times its size in the middle of an empty picture
                const D2D1_RECT_F box = D2D1::RectF(64.0f, 64.0f, 64.0f + 128.0f, 64.0f + 128.0f);
                inPlace = true;
                for (const auto& icon : made) {
                    rt->BeginDraw();
                    rt->Clear(D2D1::ColorF(0, 0.0f));
                    ImageCache::DrawSystemIcon(rt.Get(), icon, PathUtils::FitRectCentered(box, icon.width, icon.height), 1.0f);
                    if (FAILED(rt->EndDraw())) { inPlace = false; break; }
                    std::vector<uint8_t> px(static_cast<size_t>(kSide) * kSide * 4);
                    bitmap->CopyPixels(nullptr, kSide * 4, static_cast<UINT>(px.size()), px.data());
                    for (UINT y = 0; y < kSide; ++y) {
                        for (UINT x = 0; x < kSide; ++x) {
                            if (px[(static_cast<size_t>(y) * kSide + x) * 4 + 3] == 0) continue;
                            const bool inside = x >= 63 && x < 64 + 128 + 1 && y >= 63 && y < 64 + 128 + 1;
                            if (inside) ++inkInside; else ++inkOutside;
                        }
                    }
                }
                inPlace = inPlace && inkInside > 0 && inkOutside == 0;
            }
            icons.DiscardDeviceResources();
        }
        wchar_t detail[224] = {};
        swprintf_s(detail, L"icons=%zu, all made=%d, one shared texture=%d, places apart=%d, drawn in place=%d (pixels inside %zu, outside %zu)",
            indices.size(), allMade ? 1 : 0, oneTexture ? 1 : 0, apart ? 1 : 0, inPlace ? 1 : 0, inkInside, inkOutside);
        tc.Record(allMade && oneTexture && apart && inPlace, L"Stage13.Test33_SystemIcons_ShareOneTexture", detail);
    }

    // Test 13.34: the floating transfer cards show the jobs still to finish, one card each: running ones first,
    // then paused ones, at most as many as there is room for, plus how many there are in all (for the
    // "+ 4 zadania" line). Finished jobs do not count.
    {
        const std::wstring dir = baseDir + L"\\t34_pending";
        CreateDirectoryW(dir.c_str(), nullptr);
        const std::wstring slow = dir + L"\\slow";
        CreateDirectoryW(slow.c_str(), nullptr);
        for (int i = 0; i < 60; ++i) {
            PathUtils::WriteFileBytes(slow + L"\\f" + std::to_wstring(i) + L".txt", "x", 1);
        }
        TransferManager pm;                  // a queue of its own: only these jobs
        pm.Initialize(nullptr);
        std::vector<int> parkedIds;
        for (int i = 0; i < 4; ++i) {
            const std::wstring parked = dir + L"\\parked" + std::to_wstring(i);
            CreateDirectoryW(parked.c_str(), nullptr);
            PathUtils::WriteFileBytes(parked + L"\\stay.txt", "s", 1);
            parkedIds.push_back(pm.EnqueueDelete({ parked }, true, nullptr, true));     // waits, paused
        }
        pm.SetDeleteItemDelayForTesting(40);
        const int runningId = pm.EnqueueDelete({ slow }, true);
        bool running = false;
        for (int i = 0; i < 200 && !running; ++i) {
            TransferJobSnapshot snap;
            running = FindJobSnapshot(pm, runningId, snap) &&
                      (snap.state == TransferJobState::Running || snap.state == TransferJobState::Scanning);
            if (!running) Sleep(10);
        }
        size_t total = 0;
        const std::vector<TransferJobSnapshot> cards = pm.SnapshotPendingJobs(3, &total);
        const bool order = cards.size() == 3 && cards[0].id == runningId &&
                           cards[1].state == TransferJobState::Paused && cards[2].state == TransferJobState::Paused;

        pm.SetDeleteItemDelayForTesting(0);
        for (int id : parkedIds) pm.CancelOrRemoveJob(id);
        pm.CancelOrRemoveJob(runningId);
        // A job cancelled before it ever ran is simply removed, so wait for the list to be empty
        size_t totalAfter = 99;
        std::vector<TransferJobSnapshot> after;
        const ULONGLONG cancelStarted = GetTickCount64();
        do {
            after = pm.SnapshotPendingJobs(3, &totalAfter);
            if (totalAfter != 0) Sleep(20);
        } while (totalAfter != 0 && GetTickCount64() - cancelStarted < 15000);
        const bool allEnded = totalAfter == 0;
        pm.Shutdown();

        wchar_t detail[192] = {};
        swprintf_s(detail, L"started=%d, jobs to finish=%zu, cards=%zu, running first then paused=%d; after cancelling: jobs to finish=%zu, cards=%zu",
            running ? 1 : 0, total, cards.size(), order ? 1 : 0, totalAfter, after.size());
        tc.Record(running && total == 5 && order && allEnded && totalAfter == 0 && after.empty(),
            L"Stage13.Test34_TransferCards_PendingJobsInOrder", detail);
    }

    // Test 13.35: selecting a file whose preview takes long to read does not hold the window up. The pane
    // hands the reading to its worker thread, shows a note meanwhile, and shows the preview once the worker
    // says that it is done (WM_APP_PREVIEW_READY). A PDF, an archive or a document of some size used to hold
    // the window for a second or two, a file on an FTP server for as long as its download took. Closing the
    // pane while a file is being read does not wait for the file either.
    {
        const std::wstring dir = baseDir + L"\\t35_preview";
        CreateDirectoryW(dir.c_str(), nullptr);
        const std::wstring pdfPath = dir + L"\\slow.pdf";
        const bool wrote = WriteMultiPagePdfForTest(pdfPath, 2);
        HWND window = CreatePreviewTestWindow();
        FileContentPane pane;
        pane.Initialize(window, GetModuleHandleW(nullptr), nullptr, nullptr);
        pane.SetPreviewDelayForTesting(800);        // every file now takes 0.8 s to read

        const FileEntry entry = MakePreviewTestEntry(pdfPath, false);
        const ULONGLONG t0 = GetTickCount64();
        pane.SyncToSelection(&entry, true);
        const ULONGLONG heldMs = GetTickCount64() - t0;
        const FileContentPane::PreviewTestState during = pane.GetPreviewStateForTesting();
        const bool arrived = WaitForPreviewResult(pane, window, 20000);
        const FileContentPane::PreviewTestState after = pane.GetPreviewStateForTesting();

        const std::wstring secondPath = dir + L"\\second.pdf";
        WriteMultiPagePdfForTest(secondPath, 1);
        pane.SetPreviewDelayForTesting(5000);
        const FileEntry second = MakePreviewTestEntry(secondPath, false);
        pane.SyncToSelection(&second, true);
        const ULONGLONG t1 = GetTickCount64();
        pane.Shutdown();
        const ULONGLONG closeMs = GetTickCount64() - t1;
        if (window) DestroyWindow(window);

        const bool shown = arrived && !after.loading && after.pdfPath == pdfPath && after.pdfPages == 2 &&
                           after.pdfPicturePage == 0 && after.pdfPictureWidth > 0;
        wchar_t detail[256] = {};
        swprintf_s(detail, L"window held %llu ms (the file takes 800), loading meanwhile=%d, then shown=%d (%u pages, picture %u px wide), closing during a read took %llu ms",
            heldMs, during.loading ? 1 : 0, shown ? 1 : 0, after.pdfPages, after.pdfPictureWidth, closeMs);
        tc.Record(wrote && heldMs < 300 && during.loading && during.kind == ResolvedContentKind::PdfDocument && during.pdfPath.empty() &&
                  shown && closeMs < 2000,
            L"Stage13.Test35_Preview_SlowFile_DoesNotHoldTheWindow", detail);
    }

    // Test 13.36: only the newest selection is read. Files selected quickly one after another (walking a
    // folder with the arrow keys) are not all read: a job not begun yet is replaced by the newer one, and
    // one that is waiting to be read is given up. A file shown without the worker (Hex) ends the wait for
    // the PDF before it, and the PDF is read again when it is selected again: it never stays "loading".
    {
        const std::wstring dir = baseDir + L"\\t36_newest";
        CreateDirectoryW(dir.c_str(), nullptr);
        const std::wstring pdfPath = dir + L"\\a.pdf";
        WriteMultiPagePdfForTest(pdfPath, 1);
        const std::wstring zipPath = dir + L"\\b.zip";
        const std::vector<uint8_t> zip = BuildStoreZipForTest({ { "inner.txt", { 'h', 'i' } } });
        PathUtils::WriteFileBytes(zipPath, zip.data(), zip.size());
        const std::wstring folderPath = dir + L"\\c_folder";
        CreateDirectoryW(folderPath.c_str(), nullptr);
        for (int i = 0; i < 3; ++i) {
            PathUtils::WriteFileBytes(folderPath + L"\\f" + std::to_wstring(i) + L".dat", "x", 1);
        }
        const std::wstring binPath = dir + L"\\d.bin";
        const uint8_t binBytes[4] = { 0, 1, 2, 3 };
        PathUtils::WriteFileBytes(binPath, binBytes, sizeof(binBytes));

        HWND window = CreatePreviewTestWindow();
        FileContentPane pane;
        pane.Initialize(window, GetModuleHandleW(nullptr), nullptr, nullptr);
        pane.SetPreviewDelayForTesting(400);
        const FileEntry ePdf = MakePreviewTestEntry(pdfPath, false);
        const FileEntry eZip = MakePreviewTestEntry(zipPath, false);
        const FileEntry eFolder = MakePreviewTestEntry(folderPath, true);
        const FileEntry eBin = MakePreviewTestEntry(binPath, false);

        // a.pdf, b.zip and the folder, one right after the other: only the folder is read and shown
        pane.SyncToSelection(&ePdf, true);
        pane.SyncToSelection(&eZip, true);
        pane.SyncToSelection(&eFolder, true);
        const bool arrived = WaitForPreviewResult(pane, window, 20000);
        Sleep(300);
        const bool more = TakeLatePreviewResults(pane, window);
        const FileContentPane::PreviewTestState s1 = pane.GetPreviewStateForTesting();
        const bool newest = arrived && !more && s1.kind == ResolvedContentKind::DirectoryInfo && s1.folderPath == folderPath &&
                            s1.folderItems == 3 && s1.pdfPath.empty() && s1.archivePath.empty() &&
                            s1.jobsRead == 1 && s1.resultsShown == 1;

        // a.pdf again, and the binary file (Hex, no worker) before a.pdf is read: the wait ends at once
        pane.SyncToSelection(&ePdf, true);
        const bool pdfLoading = pane.GetPreviewStateForTesting().loading;
        pane.SyncToSelection(&eBin, true);
        const FileContentPane::PreviewTestState s2 = pane.GetPreviewStateForTesting();
        Sleep(700);
        const bool late = TakeLatePreviewResults(pane, window);
        const FileContentPane::PreviewTestState s3 = pane.GetPreviewStateForTesting();
        const bool waitEnded = pdfLoading && !s2.loading && s2.kind == ResolvedContentKind::HexInspector && !late &&
                               s3.pdfPath.empty() && s3.jobsRead == 1;

        // a.pdf once more: read, and shown
        pane.SetPreviewDelayForTesting(0);
        pane.SyncToSelection(&ePdf, true);
        bool again = !pane.GetPreviewStateForTesting().loading || WaitForPreviewResult(pane, window, 20000);
        const FileContentPane::PreviewTestState s4 = pane.GetPreviewStateForTesting();
        again = again && s4.kind == ResolvedContentKind::PdfDocument && s4.pdfPath == pdfPath && s4.pdfPages == 1;
        pane.Shutdown();
        if (window) DestroyWindow(window);

        wchar_t detail[256] = {};
        swprintf_s(detail, L"three quick selections: files read=%llu, results shown=%llu, the folder shown=%d; Hex selected while the PDF waited: wait ended=%d; the PDF selected again: shown=%d",
            static_cast<unsigned long long>(s1.jobsRead), static_cast<unsigned long long>(s1.resultsShown), newest ? 1 : 0, waitEnded ? 1 : 0, again ? 1 : 0);
        tc.Record(newest && waitEnded && again, L"Stage13.Test36_Preview_NewestSelectionWins", detail);
    }

    // Test 13.37: turning a PDF page does not hold the window up either. The view keeps the picture of the
    // page it had (with a note) until the worker has drawn the new page; pages turned past quickly are not
    // drawn at all, only the one the view stops at.
    {
        const std::wstring dir = baseDir + L"\\t37_pages";
        CreateDirectoryW(dir.c_str(), nullptr);
        const std::wstring pdfPath = dir + L"\\pages.pdf";
        const bool wrote = WriteMultiPagePdfForTest(pdfPath, 3);
        HWND window = CreatePreviewTestWindow();
        FileContentPane pane;
        pane.Initialize(window, GetModuleHandleW(nullptr), nullptr, nullptr);
        const FileEntry entry = MakePreviewTestEntry(pdfPath, false);
        pane.SyncToSelection(&entry, true);
        bool opened = !pane.GetPreviewStateForTesting().loading || WaitForPreviewResult(pane, window, 20000);
        const FileContentPane::PreviewTestState s0 = pane.GetPreviewStateForTesting();
        opened = opened && s0.pdfPath == pdfPath && s0.pdfPages == 3 && s0.pdfPicturePage == 0 && s0.pdfPictureWidth > 0;

        pane.SetPreviewDelayForTesting(400);
        const ULONGLONG t0 = GetTickCount64();
        pane.TurnPdfPageForTesting(+1);
        pane.TurnPdfPageForTesting(+1);
        const ULONGLONG heldMs = GetTickCount64() - t0;
        const FileContentPane::PreviewTestState s1 = pane.GetPreviewStateForTesting();
        const bool keptOld = s1.loading && s1.kind == ResolvedContentKind::PdfDocument && s1.pdfCurrentPage == 2 && s1.pdfPicturePage == 0;
        const bool drawn = WaitForPreviewResult(pane, window, 20000);
        const FileContentPane::PreviewTestState s2 = pane.GetPreviewStateForTesting();
        const bool newPage = drawn && !s2.loading && s2.pdfCurrentPage == 2 && s2.pdfPicturePage == 2 && s2.pdfPictureWidth > 0;
        const uint64_t pagesRead = s2.jobsRead - s0.jobsRead;
        pane.Shutdown();
        if (window) DestroyWindow(window);

        wchar_t detail[256] = {};
        swprintf_s(detail, L"opened=%d (%u pages); two pages turned: window held %llu ms, old picture kept meanwhile=%d, page 3 drawn=%d, pages drawn for it=%llu",
            opened ? 1 : 0, s0.pdfPages, heldMs, keptOld ? 1 : 0, newPage ? 1 : 0, static_cast<unsigned long long>(pagesRead));
        tc.Record(wrote && opened && heldMs < 300 && keptOld && newPage && pagesRead == 1,
            L"Stage13.Test37_Preview_PdfPagesTurnInTheBackground", detail);
    }

    // Test 13.38: a file inside an archive is unpacked by the worker too (as is a file on an FTP server,
    // which is downloaded), and its viewer is chosen and read once the copy is there.
    {
        const std::wstring dir = baseDir + L"\\t38_inarchive";
        CreateDirectoryW(dir.c_str(), nullptr);
        const std::wstring pdfSource = dir + L"\\source.pdf";
        WriteMultiPagePdfForTest(pdfSource, 2);
        std::vector<uint8_t> pdfBytes;
        PathUtils::ReadFileBytes(pdfSource, 1u << 20, pdfBytes);
        const std::wstring zipPath = dir + L"\\docs.zip";
        const std::vector<uint8_t> zip = BuildStoreZipForTest({ { "docs/inside.pdf", pdfBytes } });
        PathUtils::WriteFileBytes(zipPath, zip.data(), zip.size());

        HWND window = CreatePreviewTestWindow();
        FileContentPane pane;
        pane.Initialize(window, GetModuleHandleW(nullptr), nullptr, nullptr);
        pane.SetPreviewDelayForTesting(300);
        const FileEntry entry = MakePreviewTestEntry(zipPath + L"\\docs\\inside.pdf", false);   // as the archive's listing gives it
        const ULONGLONG t0 = GetTickCount64();
        pane.SyncToSelection(&entry, true);
        const ULONGLONG heldMs = GetTickCount64() - t0;
        const FileContentPane::PreviewTestState s0 = pane.GetPreviewStateForTesting();
        const bool unpacking = s0.fetching && s0.loading;
        const bool unpacked = WaitForPreviewResult(pane, window, 20000);
        const FileContentPane::PreviewTestState s1 = pane.GetPreviewStateForTesting();
        const bool readNext = unpacked && !s1.fetching && s1.loading && s1.kind == ResolvedContentKind::PdfDocument;
        const bool arrived = WaitForPreviewResult(pane, window, 20000);
        const FileContentPane::PreviewTestState s2 = pane.GetPreviewStateForTesting();
        const bool shown = arrived && !s2.loading && s2.pdfPath == s2.currentPath && s2.pdfPath != entry.fullPath &&
                           s2.pdfPages == 2 && GetFileAttributesW(s2.pdfPath.c_str()) != INVALID_FILE_ATTRIBUTES;
        pane.Shutdown();
        if (window) DestroyWindow(window);
        // The unpacked copy is in the preview cache, in a folder of its own
        if (!s2.currentPath.empty() && s2.currentPath != entry.fullPath) {
            DeleteFileW(s2.currentPath.c_str());
            RemoveDirectoryW(PathUtils::GetParentPath(s2.currentPath).c_str());
        }

        wchar_t detail[256] = {};
        swprintf_s(detail, L"window held %llu ms, unpacking meanwhile=%d, then the PDF read=%d and shown=%d (%u pages)",
            heldMs, unpacking ? 1 : 0, readNext ? 1 : 0, shown ? 1 : 0, s2.pdfPages);
        tc.Record(heldMs < 300 && unpacking && readNext && shown, L"Stage13.Test38_Preview_FileInsideArchive_UnpackedInTheBackground", detail);
    }

    tm.Shutdown();
    RemoveTestTreeRecursive(baseDir);
}

// ============================================================================
// Stage 14: Next-100 File Formats Verification (#1–#100 across all 5 engines)
// ============================================================================
static std::vector<uint8_t> BuildStoreZipForTest(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
    std::vector<uint8_t> z;
    auto p16 = [&](uint16_t v) { z.push_back(v & 0xFFu); z.push_back((v >> 8) & 0xFFu); };
    auto p32 = [&](uint32_t v) {
        z.push_back(v & 0xFFu); z.push_back((v >> 8) & 0xFFu);
        z.push_back((v >> 16) & 0xFFu); z.push_back((v >> 24) & 0xFFu);
    };
    std::vector<uint32_t> offs;
    for (const auto& kv : files) {
        offs.push_back(static_cast<uint32_t>(z.size()));
        uint16_t fnLen = static_cast<uint16_t>(kv.first.size());
        uint32_t dLen  = static_cast<uint32_t>(kv.second.size());
        p32(0x04034B50u); p16(20); p16(0); p16(0); p16(0); p16(0);
        p32(0); p32(dLen); p32(dLen); p16(fnLen); p16(0);
        z.insert(z.end(), kv.first.begin(), kv.first.end());
        z.insert(z.end(), kv.second.begin(), kv.second.end());
    }
    uint32_t cdOff = static_cast<uint32_t>(z.size());
    for (size_t i = 0; i < files.size(); ++i) {
        uint16_t fnLen = static_cast<uint16_t>(files[i].first.size());
        uint32_t dLen  = static_cast<uint32_t>(files[i].second.size());
        p32(0x02014B50u); p16(20); p16(20); p16(0); p16(0); p16(0); p16(0);
        p32(0); p32(dLen); p32(dLen); p16(fnLen); p16(0); p16(0);
        p16(0); p16(0); p32(0); p32(offs[i]);
        z.insert(z.end(), files[i].first.begin(), files[i].first.end());
    }
    uint32_t cdSz = static_cast<uint32_t>(z.size()) - cdOff;
    p32(0x06054B50u); p16(0); p16(0);
    p16(static_cast<uint16_t>(files.size())); p16(static_cast<uint16_t>(files.size()));
    p32(cdSz); p32(cdOff); p16(0);
    return z;
}

static std::vector<uint8_t> BuildPosixTarForTest(const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<uint8_t> tar;
    for (const auto& kv : files) {
        uint8_t hdr[512] = {};
        std::memcpy(hdr, kv.first.data(), std::min<size_t>(kv.first.size(), 99));
        snprintf(reinterpret_cast<char*>(hdr + 100), 8, "%07o", 0644);
        snprintf(reinterpret_cast<char*>(hdr + 124), 12, "%011llo", static_cast<unsigned long long>(kv.second.size()));
        hdr[156] = '0';
        std::memcpy(hdr + 257, "ustar", 5);
        std::memset(hdr + 148, ' ', 8);
        uint32_t sum = 0;
        for (int i = 0; i < 512; ++i) sum += hdr[i];
        snprintf(reinterpret_cast<char*>(hdr + 148), 8, "%06o", sum);
        tar.insert(tar.end(), hdr, hdr + 512);
        tar.insert(tar.end(), kv.second.begin(), kv.second.end());
        size_t rem = kv.second.size() % 512;
        if (rem != 0) tar.insert(tar.end(), 512 - rem, 0);
    }
    tar.insert(tar.end(), 1024, 0);
    return tar;
}

static std::vector<uint8_t> BuildMiniValidPng4x4() {
    auto crc32 = [](const uint8_t* p, size_t n) -> uint32_t {
        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = 0; i < n; ++i) {
            c ^= p[i];
            for (int b = 0; b < 8; ++b) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
        return ~c;
    };
    std::vector<uint8_t> png = { 0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A };
    auto addChunk = [&](const char type[4], const std::vector<uint8_t>& d) {
        auto p32be = [&](uint32_t v) {
            png.push_back((v >> 24) & 0xFFu); png.push_back((v >> 16) & 0xFFu);
            png.push_back((v >> 8) & 0xFFu);  png.push_back(v & 0xFFu);
        };
        p32be(static_cast<uint32_t>(d.size()));
        size_t tStart = png.size();
        png.insert(png.end(), type, type + 4);
        png.insert(png.end(), d.begin(), d.end());
        p32be(crc32(png.data() + tStart, 4 + d.size()));
    };
    // IHDR: 4x4, 8-bit RGB (colorType=2)
    addChunk("IHDR", { 0,0,0,4, 0,0,0,4, 8, 2, 0, 0, 0 });
    // Raw scanlines: 4 rows * (1 filter byte 0 + 4*3 RGB bytes) = 52 bytes
    std::vector<uint8_t> raw(52, 0);
    for (int y = 0; y < 4; ++y) {
        raw[y * 13] = 0;
        for (int x = 0; x < 4; ++x) {
            raw[y * 13 + 1 + x * 3 + 0] = 220;
            raw[y * 13 + 1 + x * 3 + 1] = static_cast<uint8_t>(60 + x * 40);
            raw[y * 13 + 1 + x * 3 + 2] = static_cast<uint8_t>(80 + y * 40);
        }
    }
    // Wrap in stored zlib stream: 0x78 0x01 + [0x01 LEN NLEN] + raw + Adler32BE
    uint32_t s1 = 1, s2 = 0;
    for (uint8_t b : raw) { s1 = (s1 + b) % 65521u; s2 = (s2 + s1) % 65521u; }
    uint32_t adler = (s2 << 16) | s1;
    uint16_t ulen = static_cast<uint16_t>(raw.size()), nlen = ~ulen;
    std::vector<uint8_t> z = {
        0x78, 0x01, 0x01,
        static_cast<uint8_t>(ulen & 0xFFu), static_cast<uint8_t>(ulen >> 8),
        static_cast<uint8_t>(nlen & 0xFFu), static_cast<uint8_t>(nlen >> 8)
    };
    z.insert(z.end(), raw.begin(), raw.end());
    z.push_back((adler >> 24) & 0xFFu); z.push_back((adler >> 16) & 0xFFu);
    z.push_back((adler >> 8) & 0xFFu);  z.push_back(adler & 0xFFu);
    addChunk("IDAT", z);
    addChunk("IEND", {});
    return png;
}

static void RunStage14Tests(TestCounter& tc) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::wstring baseDir = GetScratchTestDir() + L"\\Stage14_Formats200";
    RemoveTestTreeRecursive(baseDir);
    CreateDirectoryW(baseDir.c_str(), nullptr);

    auto p16le = [](std::vector<uint8_t>& b, uint16_t v) { b.push_back(v & 0xFFu); b.push_back((v >> 8) & 0xFFu); };
    auto p32le = [](std::vector<uint8_t>& b, uint32_t v) {
        b.push_back(v & 0xFFu); b.push_back((v >> 8) & 0xFFu);
        b.push_back((v >> 16) & 0xFFu); b.push_back((v >> 24) & 0xFFu);
    };
    auto p64le = [&](std::vector<uint8_t>& b, uint64_t v) { p32le(b, static_cast<uint32_t>(v)); p32le(b, static_cast<uint32_t>(v >> 32)); };
    auto p16be = [](std::vector<uint8_t>& b, uint16_t v) { b.push_back((v >> 8) & 0xFFu); b.push_back(v & 0xFFu); };
    auto p32be = [](std::vector<uint8_t>& b, uint32_t v) {
        b.push_back((v >> 24) & 0xFFu); b.push_back((v >> 16) & 0xFFu);
        b.push_back((v >> 8) & 0xFFu);  b.push_back(v & 0xFFu);
    };
    auto p64be = [&](std::vector<uint8_t>& b, uint64_t v) { p32be(b, static_cast<uint32_t>(v >> 32)); p32be(b, static_cast<uint32_t>(v)); };
    auto pf32  = [](std::vector<uint8_t>& b, float f) { uint8_t t[4]; std::memcpy(t, &f, 4); b.insert(b.end(), t, t + 4); };

    std::vector<uint8_t> miniPng = BuildMiniValidPng4x4();

    // ------------------------------------------------------------------------
    // Test 14.1: Formats #1–#20 — ExtendedImages200 (20 Camera RAW, Vector, Design & Stitch formats)
    // ------------------------------------------------------------------------
    {
        auto buildTiffCfa = [&](const char magic[4], uint16_t photometric) -> std::vector<uint8_t> {
            // 8x8 uncompressed Bayer RGGB / Grayscale TIFF
            std::vector<uint8_t> t;
            t.insert(t.end(), magic, magic + 4);
            p32le(t, 8); // IFD0 @ 8
            p16le(t, 7); // 7 entries (8 + 2 + 7*12 + 4 = 98; pixel data @ 100)
            auto tag = [&](uint16_t id, uint16_t tp, uint32_t cnt, uint32_t val) {
                p16le(t, id); p16le(t, tp); p32le(t, cnt); p32le(t, val);
            };
            tag(0x0100, 4, 1, 8);           // Width = 8
            tag(0x0101, 4, 1, 8);           // Height = 8
            tag(0x0102, 3, 1, 8);           // BitsPerSample = 8
            tag(0x0103, 3, 1, 1);           // Compression = 1 (Uncompressed)
            tag(0x0106, 3, 1, photometric); // Photometric (32803 = CFA Bayer RGGB)
            tag(0x0111, 4, 1, 100);         // StripOffsets = 100
            tag(0x0117, 4, 1, 64);          // StripByteCounts = 64
            p32le(t, 0); // nextIFD = 0
            while (t.size() < 100) t.push_back(0);
            for (int i = 0; i < 64; ++i) t.push_back(static_cast<uint8_t>((i * 17 + 40) & 0xFFu));
            return t;
        };

        std::vector<uint8_t> tiffLeCfa = buildTiffCfa("II*\0", 32803);
        std::vector<uint8_t> orfBytes  = buildTiffCfa("IIRO", 32803);
        std::vector<uint8_t> rw2Bytes  = buildTiffCfa("IIU\0", 32803);

        // #2 CR3: ftypcrx  + PRVW box containing miniPng
        std::vector<uint8_t> cr3;
        p32be(cr3, 16); cr3.insert(cr3.end(), {'f','t','y','p','c','r','x',' '}); p32be(cr3, 1);
        p32be(cr3, static_cast<uint32_t>(8 + miniPng.size()));
        cr3.insert(cr3.end(), {'P','R','V','W'});
        cr3.insert(cr3.end(), miniPng.begin(), miniPng.end());

        // #6 RAF: FUJIFILMCCD-RAW + offset @ 0x54 + len @ 0x58
        std::vector<uint8_t> raf(0x80, 0);
        std::memcpy(raf.data(), "FUJIFILMCCD-RAW 0201", 20);
        uint32_t rafOff = 0x80, rafLen = static_cast<uint32_t>(miniPng.size());
        raf[0x54] = (rafOff >> 24) & 0xFF; raf[0x55] = (rafOff >> 16) & 0xFF; raf[0x56] = (rafOff >> 8) & 0xFF; raf[0x57] = rafOff & 0xFF;
        raf[0x58] = (rafLen >> 24) & 0xFF; raf[0x59] = (rafLen >> 16) & 0xFF; raf[0x5A] = (rafLen >> 8) & 0xFF; raf[0x5B] = rafLen & 0xFF;
        raf.insert(raf.end(), miniPng.begin(), miniPng.end());

        // #14 Real Win32 EMF created via CreateEnhMetaFileW
        std::vector<uint8_t> emfBytes;
        {
            HDC hEmfDc = CreateEnhMetaFileW(nullptr, nullptr, nullptr, L"Explorer2\0Test\0\0");
            if (hEmfDc) {
                Rectangle(hEmfDc, 10, 10, 120, 90);
                Ellipse(hEmfDc, 20, 20, 100, 80);
                HENHMETAFILE hEmf = CloseEnhMetaFile(hEmfDc);
                if (hEmf) {
                    UINT sz = GetEnhMetaFileBits(hEmf, 0, nullptr);
                    if (sz > 0) {
                        emfBytes.resize(sz);
                        GetEnhMetaFileBits(hEmf, sz, emfBytes.data());
                    }
                    DeleteEnhMetaFile(hEmf);
                }
            }
        }

        // #13 Affinity .afphoto: 00 FF 4B 41 + miniPng
        std::vector<uint8_t> afp = { 0x00, 0xFF, 0x4B, 0x41, 0x0B, 0x00, 0x00, 0x00 };
        afp.insert(afp.end(), miniPng.begin(), miniPng.end());

        // #16 Photoshop .abr v1 brush
        std::vector<uint8_t> abr;
        p16be(abr, 1); p16be(abr, 1); // v1, 1 brush
        p16be(abr, 2); p32be(abr, 6 + 9 + 19 + 16); // sampled brush, 4x4
        abr.insert(abr.end(), 6 + 9, 0);
        p32be(abr, 0); p32be(abr, 0); p32be(abr, 4); p32be(abr, 4); p16be(abr, 8); abr.push_back(0);
        abr.insert(abr.end(), 16, 220);

        // #17 GIMP .pat ("GPAT" @ 20, 4x4 RGB)
        std::vector<uint8_t> pat;
        p32be(pat, 28); p32be(pat, 1); p32be(pat, 4); p32be(pat, 4); p32be(pat, 3);
        pat.insert(pat.end(), {'G','P','A','T','T','e','s','t'});
        pat.insert(pat.end(), 4 * 4 * 3, 180);

        // #18 Tajima .dst (512B header + balanced-ternary stitches + 00 00 F3)
        std::vector<uint8_t> dst(512, ' ');
        std::memcpy(dst.data(), "LA:LOGO\r", 8);
        dst.insert(dst.end(), { 0x05, 0x01, 0x03, 0x24, 0x20, 0x03, 0x0A, 0x42, 0x03, 0x00, 0x00, 0xF3 });

        // #19 Brother .pes ("#PES0001" + pecOff @ 8 + 532B PEC header + stitch bytes + FF 00)
        std::vector<uint8_t> pes(64 + 532, 0);
        std::memcpy(pes.data(), "#PES0001", 8);
        pes[8] = 64; // pecOff = 64
        pes[64 + 48] = 1; pes[64 + 49] = 2; pes[64 + 50] = 5;
        pes.insert(pes.end(), { 20, 20, 20, 0x7F, 0x6C, 20, 0xFF, 0x00 });

        struct ImgCase { const wchar_t* ext; std::vector<uint8_t> bytes; };
        auto strVec = [](const char* s) { return std::vector<uint8_t>(s, s + std::strlen(s)); };

        std::vector<ImgCase> cases = {
            { L".cr2",     tiffLeCfa },
            { L".cr3",     cr3 },
            { L".nef",     tiffLeCfa },
            { L".arw",     tiffLeCfa },
            { L".dng",     tiffLeCfa },
            { L".raf",     raf },
            { L".orf",     orfBytes },
            { L".rw2",     rw2Bytes },
            { L".pef",     tiffLeCfa },
            { L".eps",     strVec("%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 100 100\n1 0 0 setrgbcolor\n10 10 moveto 90 10 lineto 50 90 lineto closepath fill\n") },
            { L".ai",      strVec("%!PS-Adobe-3.0\n%%BoundingBox: 0 0 120 120\n20 20 m 100 20 l 60 100 l f\n") },
            { L".cdr",     BuildStoreZipForTest({ { "previews/thumbnail.png", miniPng } }) },
            { L".afphoto", afp },
            { L".emf",     emfBytes },
            { L".cgm",     strVec("BEGMF 'Test'; VDCEXT (0,0) (100,100); POLYGON (10,10, 90,10, 50,90); ENDMF;") },
            { L".abr",     abr },
            { L".pat",     pat },
            { L".dst",     dst },
            { L".pes",     pes },
            { L".sixel",   strVec("\x1BPq#1;2;100;20;20!12~-#2;2;20;90;40!12~\x1B\\") }
        };
        // Also test .ans alongside .sixel (#20)
        cases.push_back({ L".ans", strVec("\x1B[1;31m\xDB\xDB\xDB Explorer2 ANSI BBS \xDB\xDB\xDB\r\n\x1B[32m\xB2\xB2\xB2 CP437 OK \xB2\xB2\xB2\r\n") });

        int okCnt = 0;
        std::wstring firstFail;
        for (size_t i = 0; i < cases.size(); ++i) {
            std::wstring fp = baseDir + L"\\img_" + std::to_wstring(i) + cases[i].ext;
            PathUtils::WriteFileBytes(fp, cases[i].bytes.data(), cases[i].bytes.size());
            ExtendedImageInfo info;
            bool ok = IsImageExtension(cases[i].ext) &&
                      ImageFormatLoader::IsExtendedImageExtension(cases[i].ext) &&
                      ImageFormatLoader::DecodeImageFile(fp, 512, info) &&
                      info.valid && info.width > 0 && info.height > 0 &&
                      info.bgraPixels.size() == static_cast<size_t>(info.width) * info.height * 4;
            if (ok) ++okCnt;
            else if (firstFail.empty()) firstFail = cases[i].ext;
        }
        tc.Record(okCnt == static_cast<int>(cases.size()),
            L"Stage14.Test1_ExtendedImages200_All20Formats",
            firstFail.empty() ? (std::to_wstring(okCnt) + L"/" + std::to_wstring(cases.size()) + L" decoded")
                              : (L"FAILED at " + firstFail));
    }

    // ------------------------------------------------------------------------
    // Test 14.2: Formats #21–#40 — ExtendedArchives200 (20 Game, Linux FS & Retro Archives)
    // ------------------------------------------------------------------------
    {
        // #21 .unitypackage (TAR with <guid>/pathname + <guid>/asset)
        std::vector<uint8_t> upkg = BuildPosixTarForTest({
            { "0123456789abcdef0123456789abcdef/pathname", "Assets/Scripts/Player.cs\n" },
            { "0123456789abcdef0123456789abcdef/asset",    "public class Player {}" }
        });

        // #22 .bundle (UnityFS v6 uncompressed blocksInfo)
        std::vector<uint8_t> ufs = { 'U','n','i','t','y','F','S',0 };
        p32be(ufs, 6);
        const char* pv = "5.x.x"; ufs.insert(ufs.end(), pv, pv + 6);
        const char* ev = "2022.3.10f1"; ufs.insert(ufs.end(), ev, ev + 12);
        std::vector<uint8_t> blkInfo(16, 0);
        p32be(blkInfo, 1); // 1 block
        p32be(blkInfo, 8); p32be(blkInfo, 8); p16be(blkInfo, 0);
        p32be(blkInfo, 1); // 1 directory node
        p64be(blkInfo, 0); p64be(blkInfo, 8); p32be(blkInfo, 4);
        const char* cab = "CAB-test123"; blkInfo.insert(blkInfo.end(), cab, cab + 12);
        uint64_t totalBundleSz = ufs.size() + 20 + blkInfo.size() + 8;
        p64be(ufs, totalBundleSz);
        p32be(ufs, static_cast<uint32_t>(blkInfo.size()));
        p32be(ufs, static_cast<uint32_t>(blkInfo.size()));
        p32be(ufs, 0); // compMode = 0
        ufs.insert(ufs.end(), blkInfo.begin(), blkInfo.end());
        ufs.insert(ufs.end(), 8, 0xAA);

        // #23 .pck (Godot GDPC v1)
        std::vector<uint8_t> pck = { 'G','D','P','C' };
        p32le(pck, 1); p32le(pck, 4); p32le(pck, 2); p32le(pck, 0);
        pck.insert(pck.end(), 64, 0); // 16 reserved u32s
        p32le(pck, 1); // 1 file
        std::string gpath = "res://scenes/main.tscn\0\0"; // 24 bytes (4B aligned)
        p32le(pck, 24);
        pck.insert(pck.end(), gpath.data(), gpath.data() + 24);
        uint64_t pckDataOff = pck.size() + 8 + 8 + 16;
        p64le(pck, pckDataOff); p64le(pck, 6);
        pck.insert(pck.end(), 16, 0); // MD5
        const char* gdBody = "GODOT4";
        pck.insert(pck.end(), gdBody, gdBody + 6);

        // #24 .rgss3a (RPG Maker VX Ace XOR table)
        std::vector<uint8_t> rgss = { 'R','G','S','S','A','D',0, 3 };
        uint32_t seedKey = 0x12345678u;
        p32le(rgss, seedKey);
        uint32_t rKey = seedKey * 9u + 3u;
        std::string rName = "Data/Actors.rvdata2";
        uint32_t rOff = 12 + 16 + static_cast<uint32_t>(rName.size()) + 16;
        p32le(rgss, rOff ^ rKey);
        p32le(rgss, 8u ^ rKey);
        p32le(rgss, 0u ^ rKey);
        p32le(rgss, static_cast<uint32_t>(rName.size()) ^ rKey);
        for (size_t i = 0; i < rName.size(); ++i) {
            rgss.push_back(static_cast<uint8_t>(rName[i] ^ ((rKey >> (8 * (i & 3))) & 0xFFu)));
        }
        p32le(rgss, 0u ^ rKey); p32le(rgss, 0); p32le(rgss, 0); p32le(rgss, 0); // sentinel fOff==0
        rgss.insert(rgss.end(), 8, 0x55);

        // #25 .xp3 (KiriKiri raw index)
        std::vector<uint8_t> xp3 = { 'X','P','3','\r','\n',' ','\n',0x1A,0x8B,0x67,0x01 };
        p64le(xp3, 19); // idxOff = 19
        std::vector<uint8_t> xpIdx;
        // File chunk -> info (4+8+8+2 + 8*2 = 38B) + segm (28B) => File payload = 12+38 + 12+28 = 90B
        xpIdx.insert(xpIdx.end(), {'F','i','l','e'}); p64le(xpIdx, 90);
        xpIdx.insert(xpIdx.end(), {'i','n','f','o'}); p64le(xpIdx, 38);
        p32le(xpIdx, 0); p64le(xpIdx, 16); p64le(xpIdx, 16); p16le(xpIdx, 8);
        const wchar_t* xpFn = L"main.ks_";
        for (int i = 0; i < 8; ++i) p16le(xpIdx, static_cast<uint16_t>(xpFn[i]));
        xpIdx.insert(xpIdx.end(), {'s','e','g','m'}); p64le(xpIdx, 28);
        p32le(xpIdx, 0); p64le(xpIdx, 0); p64le(xpIdx, 16); p64le(xpIdx, 16);
        xp3.push_back(0); // uncompressed index flag
        p64le(xp3, xpIdx.size());
        xp3.insert(xp3.end(), xpIdx.begin(), xpIdx.end());

        // #26 .mpq (Blizzard MPQ with unencrypted block table + listfile)
        std::vector<uint8_t> mpq = { 'M','P','Q',0x1A };
        p32le(mpq, 32); p32le(mpq, 96); p16le(mpq, 0); p16le(mpq, 3);
        p32le(mpq, 32); // hashTblPos
        p32le(mpq, 48); // blockTblPos @ 48
        p32le(mpq, 1);  // hashTblCount
        p32le(mpq, 1);  // blockCount = 1
        mpq.insert(mpq.end(), 16, 0xFF); // dummy hash entry
        const char* lf = "Units\\footman.mdx\r\n";
        uint32_t lfLen = static_cast<uint32_t>(std::strlen(lf));
        p32le(mpq, 64); p32le(mpq, lfLen); p32le(mpq, lfLen); p32le(mpq, 0x80000000u);
        mpq.insert(mpq.end(), lf, lf + lfLen);

        // #27 .big (EA BIGF)
        std::vector<uint8_t> big = { 'B','I','G','F' };
        p32le(big, 64); p32be(big, 1); p32be(big, 40);
        p32be(big, 40); p32be(big, 5);
        const char* bfn = "data/ini.big";
        big.insert(big.end(), bfn, bfn + 13);
        while (big.size() < 40) big.push_back(0);
        big.insert(big.end(), {'H','E','L','L','O'});

        // #28 .grp (KenSilverman)
        std::vector<uint8_t> grp(12, 0);
        std::memcpy(grp.data(), "KenSilverman", 12);
        p32le(grp, 1);
        uint8_t gent[12] = { 'D','U','K','E','.','C','O','N',0,0,0,0 };
        grp.insert(grp.end(), gent, gent + 12);
        p32le(grp, 4);
        grp.insert(grp.end(), {'G','R','P','1'});

        // #29 .pbo (Bohemia PBO)
        std::vector<uint8_t> pbo = { 0 }; // empty name -> sreV header
        p32le(pbo, 0x56657273u); p32le(pbo, 0); p32le(pbo, 0); p32le(pbo, 0); p32le(pbo, 0);
        const char* pk = "prefix"; pbo.insert(pbo.end(), pk, pk + 7);
        const char* pv2 = "dayz_code"; pbo.insert(pbo.end(), pv2, pv2 + 10);
        pbo.push_back(0); // end properties
        const char* pbf = "config.cpp"; pbo.insert(pbo.end(), pbf, pbf + 11);
        p32le(pbo, 0); p32le(pbo, 4); p32le(pbo, 0); p32le(pbo, 0); p32le(pbo, 4);
        pbo.insert(pbo.end(), 21, 0); // end of header table
        pbo.insert(pbo.end(), {'A','R','M','A'});

        // #30 .scs (SCS# HashFS v1)
        std::vector<uint8_t> scs = { 'S','C','S','#' };
        p16le(scs, 1); p16le(scs, 0); p32le(scs, 0x43495459u);
        p32le(scs, 1); p32le(scs, 20); // 1 entry @ offset 20
        p64le(scs, 0x1122334455667788ULL); p64le(scs, 52);
        p32le(scs, 0); p32le(scs, 0); p32le(scs, 4); p32le(scs, 4);
        scs.insert(scs.end(), {'S','C','S','1'});

        // #31 .squashfs ("hsqs" superblock)
        std::vector<uint8_t> sqfs(128, 0);
        std::memcpy(sqfs.data(), "hsqs", 4);
        sqfs[4] = 3; sqfs[12] = 0; sqfs[13] = 0x10; sqfs[20] = 1; sqfs[28] = 4; sqfs[40] = 128; sqfs[72] = 96;

        // #32 .erofs (0xE0F5E1E2 @ 1024)
        std::vector<uint8_t> erofs(1200, 0);
        uint32_t emag = 0xE0F5E1E2u;
        std::memcpy(erofs.data() + 1024, &emag, 4);
        erofs[1024 + 12] = 12; erofs[1024 + 16] = 5; erofs[1024 + 36] = 2;
        std::memcpy(erofs.data() + 1024 + 48, "system_a", 8);

        // #33 .cpio ("070701" newc)
        auto buildCpioEntry = [](const char* name, const char* body) {
            char hdr[111] = {};
            size_t nlen = std::strlen(name) + 1;
            size_t blen = std::strlen(body);
            snprintf(hdr, sizeof(hdr),
                "070701%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X",
                1u, 0100644u, 0u, 0u, 1u, 0u, static_cast<unsigned>(blen), 0u, 0u, 0u, 0u, static_cast<unsigned>(nlen), 0u);
            std::vector<uint8_t> v(hdr, hdr + 110);
            v.insert(v.end(), name, name + nlen);
            while (v.size() & 3u) v.push_back(0);
            v.insert(v.end(), body, body + blen);
            while (v.size() & 3u) v.push_back(0);
            return v;
        };
        std::vector<uint8_t> cpio = buildCpioEntry("init", "#!/bin/sh\n");
        std::vector<uint8_t> cpioEnd = buildCpioEntry("TRAILER!!!", "");
        cpio.insert(cpio.end(), cpioEnd.begin(), cpioEnd.end());

        // #34 .ar ("!<arch>\n")
        std::vector<uint8_t> ar = { '!','<','a','r','c','h','>','\n' };
        char arHdr[61] = {};
        snprintf(arHdr, sizeof(arHdr), "%-16s%-12s%-6s%-6s%-8s%-10s`\n", "math.o/", "0", "0", "0", "100644", "4");
        ar.insert(ar.end(), arHdr, arHdr + 60);
        ar.insert(ar.end(), {'C','O','F','F'});

        // #35 .xar ("xar!" + uncompressed XML TOC)
        std::string xarXml = "<xar><toc><file id=\"1\"><name>Payload</name><type>file</type><data><size>4</size><length>4</length><offset>0</offset><encoding style=\"application/octet-stream\"/></data></file></toc></xar>";
        std::vector<uint8_t> xar = { 'x','a','r','!' };
        p16be(xar, 28); p16be(xar, 1);
        p64be(xar, xarXml.size()); p64be(xar, xarXml.size()); p32be(xar, 0);
        xar.insert(xar.end(), xarXml.begin(), xarXml.end());
        xar.insert(xar.end(), {'X','A','R','0'});

        // #36 .lzh (Level-0 "-lh0-")
        std::vector<uint8_t> lzh(2 + 29, 0);
        lzh[0] = 29; lzh[1] = 0;
        std::memcpy(lzh.data() + 2, "-lh0-", 5);
        lzh[7] = 4; lzh[11] = 4; lzh[20] = 0; lzh[21] = 8;
        std::memcpy(lzh.data() + 22, "GAME.DAT", 8);
        lzh.insert(lzh.end(), {'L','Z','H','0'});
        lzh.push_back(0); // end marker

        // #37 .arj (0x60 0xEA main header + file header)
        auto makeArjHdr = [&](uint8_t fType, const char* fn, uint32_t sz) {
            std::vector<uint8_t> h = { 0x60, 0xEA };
            size_t fnLen = std::strlen(fn) + 1;
            uint16_t basicSz = static_cast<uint16_t>(30 + fnLen + 1);
            p16le(h, basicSz);
            std::vector<uint8_t> body(basicSz, 0);
            body[0] = 30; body[1] = 6; body[2] = 1; body[3] = 0; body[4] = fType;
            std::memcpy(body.data() + 12, &sz, 4);
            std::memcpy(body.data() + 16, &sz, 4);
            std::memcpy(body.data() + 30, fn, fnLen);
            h.insert(h.end(), body.begin(), body.end());
            p32le(h, 0); // header CRC
            p16le(h, 0); // no ext header
            return h;
        };
        std::vector<uint8_t> arj = makeArjHdr(2, "TEST.ARJ", 0);
        auto arjFile = makeArjHdr(0, "README.TXT", 4);
        arj.insert(arj.end(), arjFile.begin(), arjFile.end());
        arj.insert(arj.end(), {'A','R','J','!'});
        arj.insert(arj.end(), { 0x60, 0xEA, 0x00, 0x00 });

        // #38 .ace ("**ACE**" @ +7 + file block)
        std::vector<uint8_t> ace(4 + 24, 0);
        ace[2] = 24; ace[4] = 0; // archive header type 0
        std::memcpy(ace.data() + 7, "**ACE**", 7);
        // File header: hdrType=1, hdrFlags=1 (ADDSIZE), fn="DOC.TXT" (7B) => hdrSz = 31 + 7 = 38
        std::vector<uint8_t> aceFh(4 + 38, 0);
        aceFh[2] = 38; aceFh[4] = 1; aceFh[5] = 1; // hdrType=1, flag ADDSIZE
        aceFh[7] = 4; aceFh[11] = 4; // packSz=4, origSz=4
        aceFh[4 + 29] = 7;
        std::memcpy(aceFh.data() + 4 + 31, "DOC.TXT", 7);
        ace.insert(ace.end(), aceFh.begin(), aceFh.end());
        ace.insert(ace.end(), {'A','C','E','1'});

        struct ArcCase { const wchar_t* ext; std::vector<uint8_t> bytes; };
        std::vector<ArcCase> acases = {
            { L".unitypackage", upkg },
            { L".bundle",       ufs },
            { L".pck",          pck },
            { L".rgss3a",       rgss },
            { L".xp3",          xp3 },
            { L".mpq",          mpq },
            { L".big",          big },
            { L".grp",          grp },
            { L".pbo",          pbo },
            { L".scs",          scs },
            { L".squashfs",     sqfs },
            { L".erofs",        erofs },
            { L".cpio",         cpio },
            { L".ar",           ar },
            { L".xar",          xar },
            { L".lzh",          lzh },
            { L".arj",          arj },
            { L".ace",          ace },
            { L".ipa",          BuildStoreZipForTest({ { "Payload/Demo.app/Info.plist", {'P','L','S','T'} } }) },
            { L".ova",          BuildPosixTarForTest({ { "appliance.ovf", "<Envelope/>" } }) }
        };

        int okCnt = 0;
        std::wstring firstFail;
        for (size_t i = 0; i < acases.size(); ++i) {
            std::wstring fp = baseDir + L"\\arc_" + std::to_wstring(i) + acases[i].ext;
            PathUtils::WriteFileBytes(fp, acases[i].bytes.data(), acases[i].bytes.size());
            ArchiveSummaryInfo info;
            bool ok = ArchiveExplorer::IsArchiveExtension(acases[i].ext) &&
                      ArchiveExplorer::InspectArchiveFile(fp, info) &&
                      info.valid && !info.entries.empty();
            if (ok && i == 0) { // Verify .unitypackage GUID->pathname virtual extraction!
                std::vector<uint8_t> extOut;
                ok = ArchiveExplorer::ExtractSingleEntryToMemory(fp, L"Assets/Scripts/Player.cs", extOut) &&
                     extOut.size() == 22;
            }
            if (ok) ++okCnt;
            else if (firstFail.empty()) firstFail = acases[i].ext;
        }
        tc.Record(okCnt == static_cast<int>(acases.size()),
            L"Stage14.Test2_ExtendedArchives200_All20Formats",
            firstFail.empty() ? (std::to_wstring(okCnt) + L"/20 archives inspected + extracted")
                              : (L"FAILED at " + firstFail));
    }

    // ------------------------------------------------------------------------
    // Test 14.3: Formats #41–#60 — Extended3D200 (20 Game, CAD, LiDAR, Robotics & Chemistry 3D formats)
    // ------------------------------------------------------------------------
    {
        auto strVec = [](const char* s) { return std::vector<uint8_t>(s, s + std::strlen(s)); };

        // #42 .ms3d ("MS3D000000" + v4 + 3 verts + 1 tri)
        std::vector<uint8_t> ms3d = { 'M','S','3','D','0','0','0','0','0','0' };
        p32le(ms3d, 4);
        p16le(ms3d, 3);
        auto addMsVert = [&](float x, float y, float z) {
            ms3d.push_back(0); pf32(ms3d, x); pf32(ms3d, y); pf32(ms3d, z);
            ms3d.push_back(0xFF); ms3d.push_back(0);
        };
        addMsVert(0,0,0); addMsVert(1,0,0); addMsVert(0,1,0);
        p16le(ms3d, 1);
        p16le(ms3d, 0); p16le(ms3d, 0); p16le(ms3d, 1); p16le(ms3d, 2);
        ms3d.insert(ms3d.end(), 36 + 24 + 2, 0); // normals(36)+s(12)+t(12)+smooth/group(2)

        // #43 .iqm ("INTERQUAKEMODEL\0" 124B header + 1 vertexarray + 1 tri + 3 float3s)
        std::vector<uint8_t> iqm(124, 0);
        std::memcpy(iqm.data(), "INTERQUAKEMODEL\0", 16);
        auto wr32 = [](std::vector<uint8_t>& b, size_t off, uint32_t v) { std::memcpy(b.data() + off, &v, 4); };
        wr32(iqm, 16, 2);   // version 2
        wr32(iqm, 36, 1);   // num_vertexarrays = 1
        wr32(iqm, 40, 3);   // num_vertexes = 3
        wr32(iqm, 44, 124); // ofs_vertexarrays = 124
        wr32(iqm, 48, 1);   // num_triangles = 1
        wr32(iqm, 52, 144); // ofs_triangles = 144
        // vertexarray @ 124 (20 bytes): type=0(POS), flags=0, format=7(FLOAT), size=3, offset=156
        p32le(iqm, 0); p32le(iqm, 0); p32le(iqm, 7); p32le(iqm, 3); p32le(iqm, 156);
        // triangle @ 144 (12 bytes)
        p32le(iqm, 0); p32le(iqm, 1); p32le(iqm, 2);
        // 3 vertices @ 156 (36 bytes)
        pf32(iqm, 0); pf32(iqm, 0); pf32(iqm, 0);
        pf32(iqm, 2); pf32(iqm, 0); pf32(iqm, 0);
        pf32(iqm, 0); pf32(iqm, 2); pf32(iqm, 1);

        // #44 .mdl (Quake "IDPO" v6)
        std::vector<uint8_t> mdl(84, 0);
        std::memcpy(mdl.data(), "IDPO", 4);
        wr32(mdl, 4, 6);
        float one = 1.0f; std::memcpy(mdl.data() + 8, &one, 4); std::memcpy(mdl.data() + 12, &one, 4); std::memcpy(mdl.data() + 16, &one, 4);
        wr32(mdl, 60, 3); wr32(mdl, 64, 1); wr32(mdl, 68, 1); // 0 skins, 3 verts, 1 tri, 1 frame
        mdl.insert(mdl.end(), 3 * 12, 0); // stverts
        p32le(mdl, 1); p32le(mdl, 0); p32le(mdl, 1); p32le(mdl, 2); // 1 tri (16B)
        p32le(mdl, 0); // simple frame
        mdl.insert(mdl.end(), 24, 0); // bbox + name
        mdl.insert(mdl.end(), { 10,10,10,0,  50,10,10,0,  10,50,30,0 }); // 3 trivertx_t

        // #45 .nif ("Gamebryo File Format, Version 20.2.0.7\n")
        std::vector<uint8_t> nif = strVec("Gamebryo File Format, Version 20.2.0.7\n");
        p32le(nif, 0x14020007u); nif.push_back(1); p32le(nif, 12); p32le(nif, 1);
        nif.insert(nif.end(), 16, 0);

        // #46 .b3d ("BB3D" + MESH -> VRTS + TRIS)
        std::vector<uint8_t> b3dVerts;
        p32le(b3dVerts, 0); p32le(b3dVerts, 0); p32le(b3dVerts, 0);
        pf32(b3dVerts, 0); pf32(b3dVerts, 0); pf32(b3dVerts, 0);
        pf32(b3dVerts, 1); pf32(b3dVerts, 0); pf32(b3dVerts, 0);
        pf32(b3dVerts, 0); pf32(b3dVerts, 1); pf32(b3dVerts, 0);
        std::vector<uint8_t> b3dTris;
        p32le(b3dTris, 0xFFFFFFFFu); p32le(b3dTris, 0); p32le(b3dTris, 1); p32le(b3dTris, 2);
        std::vector<uint8_t> b3d = { 'B','B','3','D' };
        uint32_t mLen = static_cast<uint32_t>(4 + 8 + b3dVerts.size() + 8 + b3dTris.size());
        p32le(b3d, 4 + 8 + mLen); p32le(b3d, 1);
        b3d.insert(b3d.end(), {'M','E','S','H'}); p32le(b3d, mLen); p32le(b3d, 0xFFFFFFFFu);
        b3d.insert(b3d.end(), {'V','R','T','S'}); p32le(b3d, static_cast<uint32_t>(b3dVerts.size()));
        b3d.insert(b3d.end(), b3dVerts.begin(), b3dVerts.end());
        b3d.insert(b3d.end(), {'T','R','I','S'}); p32le(b3d, static_cast<uint32_t>(b3dTris.size()));
        b3d.insert(b3d.end(), b3dTris.begin(), b3dTris.end());

        // #48 .ctm ("OCTM" v5 "RAW\0" 3 verts 1 tri)
        std::vector<uint8_t> ctm = { 'O','C','T','M' };
        p32le(ctm, 5); ctm.insert(ctm.end(), {'R','A','W',0});
        p32le(ctm, 3); p32le(ctm, 1); p32le(ctm, 0); p32le(ctm, 0); p32le(ctm, 0); p32le(ctm, 0);
        ctm.insert(ctm.end(), {'I','N','D','X'}); p32le(ctm, 0); p32le(ctm, 1); p32le(ctm, 2);
        ctm.insert(ctm.end(), {'V','E','R','T'});
        pf32(ctm, 0); pf32(ctm, 0); pf32(ctm, 0);
        pf32(ctm, 1); pf32(ctm, 0); pf32(ctm, 0);
        pf32(ctm, 0); pf32(ctm, 1); pf32(ctm, 0);

        // #49 .drc ("DRACO" 1.5 header)
        std::vector<uint8_t> drc = { 'D','R','A','C','O', 1, 5, 1, 2, 0, 0 };
        drc.insert(drc.end(), 16, 0);

        // #51 .jt (80-byte "Version 9.5 JT " header)
        std::string jtHdr = "Version 9.5 JT                                                                  ";
        std::vector<uint8_t> jt(jtHdr.begin(), jtHdr.end());
        jt.insert(jt.end(), 32, 0);

        // #55 .e57 ("ASTM-E57" 48B header + 3 float3 points)
        std::vector<uint8_t> e57(48, 0);
        std::memcpy(e57.data(), "ASTM-E57", 8);
        e57[8] = 1; e57[24] = 96; e57[40] = 0; e57[41] = 4; // pageSize = 1024
        pf32(e57, 1.0f); pf32(e57, 2.0f); pf32(e57, 0.5f);
        pf32(e57, -1.0f); pf32(e57, 1.5f); pf32(e57, 2.0f);
        e57.insert(e57.end(), 32, 0);

        // #56 .laz ("LASF" 227B header)
        std::vector<uint8_t> laz(240, 0);
        std::memcpy(laz.data(), "LASF", 4);
        laz[24] = 1; laz[25] = 4; laz[107] = 100;
        double mx = 50.0, mn = 0.0;
        std::memcpy(laz.data() + 179, &mx, 8); std::memcpy(laz.data() + 187, &mn, 8);
        std::memcpy(laz.data() + 195, &mx, 8); std::memcpy(laz.data() + 203, &mn, 8);
        std::memcpy(laz.data() + 211, &mx, 8); std::memcpy(laz.data() + 219, &mn, 8);

        // #58 .vrm (GLB 2.0 header + JSON + BIN)
        std::string vrmJson = "{\"asset\":{\"version\":\"2.0\"},\"extensionsUsed\":[\"VRM\"],\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}]}";
        while (vrmJson.size() & 3u) vrmJson.push_back(' ');
        std::vector<uint8_t> vrmBin;
        pf32(vrmBin, 0); pf32(vrmBin, 0); pf32(vrmBin, 0);
        pf32(vrmBin, 1); pf32(vrmBin, 0); pf32(vrmBin, 0);
        pf32(vrmBin, 0); pf32(vrmBin, 1); pf32(vrmBin, 0);
        p16le(vrmBin, 0); p16le(vrmBin, 1); p16le(vrmBin, 2);
        while (vrmBin.size() & 3u) vrmBin.push_back(0);
        std::vector<uint8_t> vrm = { 'g','l','T','F' };
        p32le(vrm, 2);
        p32le(vrm, static_cast<uint32_t>(12 + 8 + vrmJson.size() + 8 + vrmBin.size()));
        p32le(vrm, static_cast<uint32_t>(vrmJson.size()));
        vrm.insert(vrm.end(), {'J','S','O','N'});
        vrm.insert(vrm.end(), vrmJson.begin(), vrmJson.end());
        p32le(vrm, static_cast<uint32_t>(vrmBin.size()));
        vrm.insert(vrm.end(), {'B','I','N',0});
        vrm.insert(vrm.end(), vrmBin.begin(), vrmBin.end());

        struct M3dCase { const wchar_t* ext; std::vector<uint8_t> bytes; };
        std::vector<M3dCase> mcases = {
            { L".x",     strVec("xof 0303txt 0032\nMesh {\n 3;\n 0.0;0.0;0.0;,\n 1.0;0.0;0.0;,\n 0.0;1.0;0.0;;\n 1;\n 3;0,1,2;;\n}\n") },
            { L".ms3d",  ms3d },
            { L".iqm",   iqm },
            { L".mdl",   mdl },
            { L".nif",   nif },
            { L".b3d",   b3d },
            { L".ac",    strVec("AC3Db\nOBJECT poly\nnumvert 3\n0 0 0\n1 0 0\n0 1 0\nnumsurf 1\nSURF 0\nrefs 3\n0 0 0\n1 0 0\n2 0 0\nkids 0\n") },
            { L".ctm",   ctm },
            { L".drc",   drc },
            { L".3dxml", strVec("<Model_3dxml><Positions>0 0 0 1 0 0 0 1 0</Positions><Faces><Face triangles=\"0 1 2\"/></Faces></Model_3dxml>") },
            { L".jt",    jt },
            { L".brep",  strVec("DBRep_DrawableShape\nTriangulations 1\n3 1 0 0.01\n0 0 0 1 0 0 0 1 0\n1 2 3\n") },
            { L".sat",   strVec("700 0 1 0\nACIS 7.0\npoint $-1 0 0 0 #\npoint $-1 10 0 0 #\npoint $-1 0 10 5 #\n") },
            { L".x_t",   strVec("SCH_1000\npoint 1 2 3\npoint 4 5 6\npoint 0 5 2\n") },
            { L".e57",   e57 },
            { L".laz",   laz },
            { L".urdf",  strVec("<robot name=\"arm\"><link name=\"base\"><visual><geometry><box size=\"0.5 0.5 0.2\"/></geometry></visual></link></robot>") },
            { L".vrm",   vrm },
            { L".cif",   strVec("data_Si\n_cell_length_a 5.43\n_cell_length_b 5.43\n_cell_length_c 5.43\nloop_\n_atom_site_fract_x\n_atom_site_fract_y\n_atom_site_fract_z\n0.0 0.0 0.0\n0.25 0.25 0.25\n") },
            { L".mol2",  strVec("@<TRIPOS>MOLECULE\nH2O\n@<TRIPOS>ATOM\n1 O 0.0 0.0 0.0 O.3\n2 H 0.96 0.0 0.0 H\n3 H -0.24 0.93 0.0 H\n@<TRIPOS>BOND\n1 1 2 1\n2 1 3 1\n") }
        };
        // Also test .gro (#60)
        mcases.push_back({ L".gro", strVec("Water\n3\n    1SOL     OW    1   0.100   0.200   0.300\n    1SOL    HW1    2   0.190   0.200   0.300\n    1SOL    HW2    3   0.080   0.290   0.300\n   1.0 1.0 1.0\n") });

        int okCnt = 0;
        std::wstring firstFail;
        for (size_t i = 0; i < mcases.size(); ++i) {
            std::wstring fp = baseDir + L"\\m3d_" + std::to_wstring(i) + mcases[i].ext;
            PathUtils::WriteFileBytes(fp, mcases[i].bytes.data(), mcases[i].bytes.size());
            LoadedMesh3D mesh;
            bool ok = Model3DViewer::Is3DModelExtension(mcases[i].ext) &&
                      Model3DViewer::InspectMeshFile(fp, mesh) &&
                      mesh.loadedOk && !mesh.vertices.empty() && !mesh.tris.empty();
            if (ok) ++okCnt;
            else if (firstFail.empty()) firstFail = mcases[i].ext;
        }
        tc.Record(okCnt == static_cast<int>(mcases.size()),
            L"Stage14.Test3_Extended3D200_All20Formats",
            firstFail.empty() ? (std::to_wstring(okCnt) + L"/" + std::to_wstring(mcases.size()) + L" 3D models parsed")
                              : (L"FAILED at " + firstFail));
    }

    // ------------------------------------------------------------------------
    // Test 14.4: Formats #61–#80 — ExtendedEda200 (20 Schematics, OASIS, FPGA, Scope, Auto & PLC formats)
    // ------------------------------------------------------------------------
    {
        auto strVec = [](const char* s) { return std::vector<uint8_t>(s, s + std::strlen(s)); };

        // #66 .oas ("%SEMI-OASIS\r\n" + START + CELL + RECTANGLE)
        std::vector<uint8_t> oas = strVec("%SEMI-OASIS\r\n");
        oas.insert(oas.end(), { 1, '1','.','0', 100, 0 }); // START
        oas.insert(oas.end(), { 14, 3, 'T','O','P' });     // CELL "TOP"
        oas.insert(oas.end(), { 20, 0xFF, 1, 0, 40, 20, 0, 0 }); // RECTANGLE layer=1 w=40 h=20 x=0 y=0
        oas.push_back(2); // END

        // #71 .sr (Sigrok ZIP with metadata + logic-1-1)
        std::string srMeta = "[device 1]\ncapturefile=logic-1\ntotal probes=4\nsamplerate=1 MHz\nprobe1=CLK\nprobe2=MOSI\n";
        std::vector<uint8_t> srSamples = { 0x01, 0x03, 0x02, 0x00, 0x05, 0x07, 0x01, 0x00 };
        std::vector<uint8_t> srZip = BuildStoreZipForTest({
            { "metadata", std::vector<uint8_t>(srMeta.begin(), srMeta.end()) },
            { "logic-1-1", srSamples }
        });

        // #72 .sal (Saleae Logic 2 ZIP with meta.json + digital-0.bin)
        std::string salMeta = "{\"name\":\"SPI\",\"channels\":[{\"name\":\"SCLK\"},{\"name\":\"MISO\"}]}";
        std::vector<uint8_t> salZip = BuildStoreZipForTest({
            { "meta.json", std::vector<uint8_t>(salMeta.begin(), salMeta.end()) },
            { "digital-0.bin", { 0,1,0,1,1,0,1,0,0,1,1,1,0,0,1,0 } }
        });

        // #73 .wfm (Tektronix WFM#003 binary)
        std::vector<uint8_t> wfm(900, 0);
        std::memcpy(wfm.data(), ":WFM#003", 8);
        for (int i = 0; i < 64; ++i) wfm[838 + i] = static_cast<uint8_t>(128 + (i % 16) * 4);

        // #74 .mf4 (ASAM MDF 4.x "MDF     " 64B ID + "##HD" block)
        std::vector<uint8_t> mf4(168, 0);
        std::memcpy(mf4.data(), "MDF     4.10    ", 16);
        std::memcpy(mf4.data() + 64, "##HD", 4);
        mf4[64 + 8] = 104;

        // #80 .sof (Intel/Altera SRAM Object File)
        std::vector<uint8_t> sof(128, 0x5A);
        std::memcpy(sof.data(), "SOF\0EP4CE22F17C6\0", 17);

        struct EdaCase { const wchar_t* ext; std::vector<uint8_t> bytes; };
        std::vector<EdaCase> ecases = {
            { L".kicad_sym", strVec("(kicad_symbol_lib (symbol \"MCU\" (rectangle (start -5 5) (end 5 -5)) (pin input line (at -7.62 2.54 0) (length 2.54) (name \"CLK\") (number \"1\"))))") },
            { L".ses",       strVec("(session \"board.ses\" (routes (network_out (net VCC (wire (path F.Cu 250 0 0 10000 5000))))))") },
            { L".pos",       strVec("# Ref Val Package PosX PosY Rot Side\nU1 STM32 LQFP-48 25.0 30.0 90.0 top\nR1 10k 0603 12.5 18.0 0.0 top\n") },
            { L".ibs",       strVec("[IBIS Ver] 5.0\n[Component] DDR4_DRAM\n[Pulldown]\n-1.0 -0.05 -0.04 -0.06\n0.0 0.0 0.0 0.0\n1.2 0.04 0.03 0.05\n") },
            { L".s4p",       strVec("# GHZ S MA R 50\n1.0  0.1 0  0.9 -20  0.02 0  0.01 0   0.9 -20 0.1 0 0.01 0 0.02 0   0.02 0 0.01 0 0.1 0 0.88 -22   0.01 0 0.02 0 0.88 -22 0.1 0\n5.0  0.2 0  0.8 -80  0.04 0  0.02 0   0.8 -80 0.2 0 0.02 0 0.04 0   0.04 0 0.02 0 0.2 0 0.79 -85   0.02 0 0.04 0 0.79 -85 0.2 0\n") },
            { L".oas",       oas },
            { L".xdc",       strVec("set_property PACKAGE_PIN A1 [get_ports clk_100m]\nset_property PACKAGE_PIN B4 [get_ports rst_n]\ncreate_clock -period 10.0 [get_ports clk_100m]\n") },
            { L".qsf",       strVec("set_global_assignment -name DEVICE 5CSEMA5F31C6\nset_location_assignment PIN_C3 -to led[0]\nset_location_assignment PIN_D5 -to sw[0]\n") },
            { L".jed",       strVec("\x02GAL16V8*\nQP20*QF2194*\nL0000 1010110011110000*\n\x03") },
            { L".svf",       strVec("! JTAG Test\nSIR 6 TDI (09);\nSDR 32 TDI (12345678) TDO (87654321);\nRUNTEST 100 TCK;\n") },
            { L".sr",        srZip },
            { L".sal",       salZip },
            { L".wfm",       wfm },
            { L".mf4",       mf4 },
            { L".ldf",       strVec("LIN_description_file;\nNodes { Master: BCM_Master, 5 ms, 0.1 ms ; Slaves: Wiper_Slave, Rain_Sensor ; }\n") },
            { L".arxml",     strVec("<AUTOSAR><AR-PACKAGES><SHORT-NAME>ChassisGateway</SHORT-NAME><SHORT-NAME>RadarECU</SHORT-NAME></AR-PACKAGES></AUTOSAR>") },
            { L".eds",       strVec("[DeviceInfo]\nVendorName=Beckhoff\n[1000]\nParameterName=DeviceType\n[1018]\nParameterName=IdentityObject\n") },
            { L".gsdml",     strVec("<ISO15745Profile><ProfileBody><ModuleItem ID=\"DI_16x24VDC\"/><ModuleItem ID=\"DQ_16x24VDC\"/></ProfileBody></ISO15745Profile>") },
            { L".l5x",       strVec("<RSLogix5000Content><Rung Number=\"0\"><Text>XIC(Start_Btn)XIO(Fault_Bit)OTE(Pump_Run);</Text></Rung></RSLogix5000Content>") },
            { L".sof",       sof }
        };

        int okCnt = 0;
        std::wstring firstFail;
        for (size_t i = 0; i < ecases.size(); ++i) {
            std::wstring fp = baseDir + L"\\eda_" + std::to_wstring(i) + ecases[i].ext;
            PathUtils::WriteFileBytes(fp, ecases[i].bytes.data(), ecases[i].bytes.size());
            ParsedEdaBoard brd;
            bool ok = EdaViewer::IsEdaExtension(ecases[i].ext) &&
                      EdaViewer::InspectEdaFile(fp, brd) &&
                      brd.valid &&
                      (!brd.traces.empty() || !brd.pads.empty() || !brd.components.empty() || !brd.circles.empty());
            if (ok) ++okCnt;
            else if (firstFail.empty()) firstFail = ecases[i].ext;
        }
        tc.Record(okCnt == static_cast<int>(ecases.size()),
            L"Stage14.Test4_ExtendedEda200_All20Formats",
            firstFail.empty() ? (std::to_wstring(okCnt) + L"/20 EDA/Silicon/FPGA/Auto/PLC boards rendered")
                              : (L"FAILED at " + firstFail));
    }

    // ------------------------------------------------------------------------
    // Test 14.5: Formats #81–#100 — ExtendedDocs200 (20 DB, Protobuf, SecOps, Forensics, Audio & Doc formats)
    // ------------------------------------------------------------------------
    {
        auto strVec = [](const char* s) { return std::vector<uint8_t>(s, s + std::strlen(s)); };

        // #81 .accdb (Microsoft Access ACE header)
        std::vector<uint8_t> accdb(4096, 0);
        accdb[0] = 0x00; accdb[1] = 0x01;
        std::memcpy(accdb.data() + 4, "Standard ACE DB", 15);
        accdb[0x14] = 2;

        // #82 .rdb (Redis v0009 snapshot with AUX + DB 0 + key "user:1" = "Alice")
        std::vector<uint8_t> rdb = { 'R','E','D','I','S','0','0','0','9',
            0xFA, 9, 'r','e','d','i','s','-','v','e','r', 5, '7','.','2','.','0',
            0xFE, 0,
            0x00, 6, 'u','s','e','r',':','1', 5, 'A','l','i','c','e',
            0xFF };

        // #83 .sst (LevelDB SSTable with 1 KV + 48-byte footer 0xDB4775248B80FB57ULL)
        std::vector<uint8_t> sst;
        // shared=0, nonShared=4 ("key1"), valLen=4 ("val1")
        sst.insert(sst.end(), { 0, 4, 'k','e','y','1', 4, 'v','a','l','1' });
        p32le(sst, 0); p32le(sst, 1); // restart[0]=0, numRestarts=1
        sst.insert(sst.end(), { 0, 0,0,0,0 }); // trailer
        uint8_t metaOffByte = static_cast<uint8_t>(sst.size());
        size_t footerStart = sst.size();
        sst.insert(sst.end(), { metaOffByte, 5, metaOffByte, 5 });
        while (sst.size() < footerStart + 40) sst.push_back(0);
        p64le(sst, 0xDB4775248B80FB57ULL);

        // #84 .pmtiles ("PMTiles\x03" 127-byte v3 header with Warsaw bounding box)
        std::vector<uint8_t> pmt(160, 0);
        std::memcpy(pmt.data(), "PMTiles\x03", 8);
        pmt[98] = 2; pmt[99] = 1; pmt[100] = 0; pmt[101] = 14;
        auto wrI32 = [&](size_t off, int32_t v) { std::memcpy(pmt.data() + off, &v, 4); };
        wrI32(102, 208000000); wrI32(106, 521000000); // minLon=20.8, minLat=52.1
        wrI32(110, 212000000); wrI32(114, 524000000); // maxLon=21.2, maxLat=52.4
        pmt[118] = 11;
        wrI32(119, 210122000); wrI32(123, 522297000); // center Warsaw

        // #85 .pb (Protobuf: field 1 varint=42, field 2 string="Explorer2")
        std::vector<uint8_t> pb = { 0x08, 42, 0x12, 9, 'E','x','p','l','o','r','e','r','2' };

        // #89 .gpg (OpenPGP old-format Tag 13 User ID packet: ctb = 0x80 | (13<<2) = 0xB4)
        std::vector<uint8_t> gpg = { 0xB4, 19 };
        const char* uid = "Alice <a@test.local>";
        gpg.insert(gpg.end(), uid, uid + 19);

        // #90 .jks (Java KeyStore 0xFEEDFEED v2 with 1 TrustedCert entry)
        std::vector<uint8_t> jks;
        p32be(jks, 0xFEEDFEEDu); p32be(jks, 2); p32be(jks, 1);
        p32be(jks, 2); // tag=2 TrustedCert
        p16be(jks, 4); jks.insert(jks.end(), {'r','o','o','t'});
        p64be(jks, 1700000000000ULL);
        p16be(jks, 5); jks.insert(jks.end(), {'X','.','5','0','9'});
        p32be(jks, 4); jks.insert(jks.end(), {0x30, 0x02, 0x05, 0x00});

        // #91 .cat (ASN.1 DER PKCS#7 + SPC_CATALOG_LIST OID + SHA-256 OCTET STRING)
        std::vector<uint8_t> cat = {
            0x30, 0x34,
            0x06, 0x0A, 0x2B,0x06,0x01,0x04,0x01,0x82,0x37,0x0C,0x01,0x01,
            0x13, 0x0C, 'M','S','_','W','i','n','_','D','r','v','6','4',
            0x04, 0x14, 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20
        };

        // #92 .hiv + NTUSER.DAT ("regf" 4096B header + "hbin" + "nk" cell)
        std::vector<uint8_t> hiv(4096 + 512, 0);
        std::memcpy(hiv.data(), "regf", 4);
        hiv[4] = 1; hiv[8] = 1; hiv[20] = 1; hiv[24] = 5; hiv[36] = 0x20;
        std::memcpy(hiv.data() + 4096, "hbin", 4);
        uint32_t hbSz = 512; std::memcpy(hiv.data() + 4096 + 8, &hbSz, 4);
        int32_t nkCellSz = -96; std::memcpy(hiv.data() + 4096 + 32, &nkCellSz, 4);
        uint8_t* nk = hiv.data() + 4096 + 36;
        nk[0] = 'n'; nk[1] = 'k'; nk[2] = 0x20; nk[20] = 3; nk[36] = 2; nk[72] = 8;
        std::memcpy(nk + 76, "Software", 8);

        // #93 .etl (ETW buffer header)
        std::vector<uint8_t> etl(256, 0);
        uint32_t etlBufSz = 65536; std::memcpy(etl.data(), &etlBufSz, 4);
        const wchar_t* etlStr = L"NT Kernel Logger";
        std::memcpy(etl.data() + 72, etlStr, 16 * sizeof(wchar_t));

        // #94 .pol ("PReg\x01\0\0\0" + UTF-16 [Software\Policies;NoAutorun;4;4;1])
        std::vector<uint8_t> pol = { 'P','R','e','g', 1,0,0,0 };
        auto pushW = [&](const wchar_t* s, bool nul) {
            while (*s) p16le(pol, static_cast<uint16_t>(*s++));
            if (nul) p16le(pol, 0);
        };
        p16le(pol, L'['); pushW(L"Software\\Policies", true);
        p16le(pol, L';'); pushW(L"NoAutorun", true);
        p16le(pol, L';'); p32le(pol, 4);
        p16le(pol, L';'); p32le(pol, 4);
        p16le(pol, L';'); p32le(pol, 1);
        p16le(pol, L']');

        // #95 .sid ("PSID" 124B header)
        std::vector<uint8_t> sid(128, 0);
        std::memcpy(sid.data(), "PSID", 4);
        sid[5] = 2; sid[7] = 0x7C; sid[15] = 3; sid[17] = 1;
        std::memcpy(sid.data() + 0x16, "Commando", 8);
        std::memcpy(sid.data() + 0x36, "Rob Hubbard", 11);

        // #98 .uasset (0x9E2A83C1 UE4 package summary + NameMap)
        std::vector<uint8_t> uas;
        p32le(uas, 0x9E2A83C1u);
        p32le(uas, static_cast<uint32_t>(-7)); // LegacyFileVersion -7 (UE4)
        p32le(uas, 0); p32le(uas, 522); p32le(uas, 0);
        p32le(uas, 0); // 0 custom versions
        p32le(uas, 128); // TotalHeaderSize
        p32le(uas, 5); uas.insert(uas.end(), {'N','o','n','e',0}); // FolderName FString
        p32le(uas, 0); // PackageFlags
        p32le(uas, 1); // NameCount = 1
        uint32_t nmOff = static_cast<uint32_t>(uas.size() + 4 + 16);
        p32le(uas, nmOff);
        uas.insert(uas.end(), 16, 0);
        p32le(uas, 11); uas.insert(uas.end(), {'B','P','_','P','l','a','y','e','r','1',0});
        p32le(uas, 0);

        // #100 .vsdx (ZIP with visio/pages/page1.xml)
        std::string vxml = "<PageContents><Shapes><Shape ID='1'><Text>Load Balancer</Text></Shape></Shapes></PageContents>";
        std::vector<uint8_t> vsdx = BuildStoreZipForTest({
            { "visio/pages/page1.xml", std::vector<uint8_t>(vxml.begin(), vxml.end()) }
        });

        struct DocCase { const wchar_t* ext; std::vector<uint8_t> bytes; bool expectGeo = false; };
        std::vector<DocCase> dcases = {
            { L".accdb",    accdb },
            { L".rdb",      rdb },
            { L".sst",      sst },
            { L".pmtiles",  pmt, true },
            { L".pb",       pb },
            { L".tfstate",  strVec("{\"version\":4,\"terraform_version\":\"1.7.0\",\"resources\":[{\"mode\":\"managed\",\"type\":\"aws_s3_bucket\",\"name\":\"logs\",\"provider\":\"aws\"}]}") },
            { L".sarif",    strVec("{\"version\":\"2.1.0\",\"runs\":[{\"tool\":{\"driver\":{\"name\":\"ClangTidy\"}},\"results\":[{\"ruleId\":\"bugprone-use-after-move\",\"level\":\"error\",\"message\":{\"text\":\"Moved\"}}]}]}") },
            { L".spdx",     strVec("SPDXVersion: SPDX-2.3\nPackageName: zlib\nPackageVersion: 1.3.1\nPackageLicenseConcluded: Zlib\n") },
            { L".gpg",      gpg },
            { L".jks",      jks },
            { L".cat",      cat },
            { L".hiv",      hiv },
            { L".etl",      etl },
            { L".pol",      pol },
            { L".sid",      sid },
            { L".musicxml", strVec("<score-partwise><work><work-title>Prelude</work-title></work><part id='P1'><measure number='1'><note><pitch><step>C</step><octave>4</octave></pitch><type>quarter</type></note></measure></part></score-partwise>") },
            { L".xmp",      strVec("<x:xmpmeta><rdf:RDF><rdf:Description crs:Temperature=\"5400\" crs:Exposure2012=\"+0.65\" xmp:Rating=\"5\"/></rdf:RDF></x:xmpmeta>") },
            { L".uasset",   uas },
            { L".prefab",   strVec("%YAML 1.1\n--- !u!1 &1001\nGameObject:\n  m_Name: HeroCharacter\n--- !u!4 &1002\nTransform:\n  m_LocalPosition: {x: 0, y: 1.8, z: 0}\n") },
            { L".vsdx",     vsdx }
        };

        int okCnt = 0;
        std::wstring firstFail;
        for (size_t i = 0; i < dcases.size(); ++i) {
            std::wstring fp = baseDir + L"\\doc_" + std::to_wstring(i) + dcases[i].ext;
            PathUtils::WriteFileBytes(fp, dcases[i].bytes.data(), dcases[i].bytes.size());
            ParsedOfficeDocument doc;
            bool ok = SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(dcases[i].ext) &&
                      SpreadsheetDocViewer::InspectOfficeFile(fp, doc) &&
                      doc.valid && !doc.sheets.empty() && doc.sheets[0].rows.size() >= 2 &&
                      (!dcases[i].expectGeo || (doc.hasGeoTrack && doc.geoPoints.size() >= 4));
            if (ok) ++okCnt;
            else if (firstFail.empty()) firstFail = dcases[i].ext;
        }

        // Also verify NTUSER.DAT ("regf" magic with .dat extension)
        std::wstring ntuserPath = baseDir + L"\\NTUSER.DAT";
        PathUtils::WriteFileBytes(ntuserPath, hiv.data(), hiv.size());
        ParsedOfficeDocument ntDoc;
        bool ntOk = SpreadsheetDocViewer::InspectOfficeFile(ntuserPath, ntDoc) && ntDoc.valid && !ntDoc.sheets.empty();

        tc.Record(okCnt == static_cast<int>(dcases.size()) && ntOk,
            L"Stage14.Test5_ExtendedDocs200_All20Formats_And_NTUSER_DAT",
            firstFail.empty() ? (std::to_wstring(okCnt) + L"/20 doc/db/secops formats + NTUSER.DAT + PMTiles GeoMap")
                              : (L"FAILED at " + firstFail));
    }

    RemoveTestTreeRecursive(baseDir);
}

} // namespace

int RunStageTests(const std::wstring& stageName, const std::wstring& outReportPath) {
    TestCounter tc;
    std::wstring stageLower = PathUtils::ToLower(stageName);

    // Not a stage: the child process of Stage13.Test26
    if (stageLower == L"pdf-exit-child") {
        return RunPdfExitChild(outReportPath);
    }

    // The report is written when the run is over. Until then the file says so, so that a run that never
    // finished cannot be mistaken for the previous, complete one.
    if (!outReportPath.empty()) {
        const std::string unfinished = PathUtils::WideToUtf8(
            L"SELF-TEST RUN NOT FINISHED (" + stageName + L"). If this is all the report says after the program has ended, "
            L"a test took the program down; " + outReportPath + L".crash.txt says where.\r\n");
        PathUtils::WriteFileBytes(outReportPath, unfinished.data(), unfinished.size());
    }

    if (stageLower == L"100x" || stageLower == L"stress" || stageLower == L"stage11") {
        ULONGLONG t0 = GetTickCount64();
        int passesCompleted = 0;
        for (int pass = 1; pass <= 100; ++pass) {
            tc.quietPass = (pass < 100); // log any failure on any pass, and log full 50 test lines on pass 100
            int beforeFailed = tc.failed;
            RunStage1Tests(tc);
            RunStage2Tests(tc);
            RunStage3Tests(tc);
            RunStage4Tests(tc);
            RunStage5Tests(tc);
            RunStage6Tests(tc);
            RunStage7Tests(tc);
            RunStage8Tests(tc);
            RunStage9Tests(tc);
            RunStage10Tests(tc);
            if (tc.failed == beforeFailed) {
                ++passesCompleted;
            }
        }
        ULONGLONG elapsedMs = GetTickCount64() - t0;
        wchar_t summary[384] = {};
        swprintf_s(summary,
            L"=== 100x DEEP STRESS TEST SUMMARY: %d/100 PASSES (%d PASSED, %d FAILED) in %llu ms ===\r\n",
            passesCompleted, tc.passed, tc.failed, elapsedMs);
        tc.log += summary;

        if (!outReportPath.empty()) {
            std::string u8 = PathUtils::WideToUtf8(tc.log);
            PathUtils::WriteFileBytes(outReportPath, u8.data(), u8.size());
        }
        return (tc.failed == 0 && passesCompleted == 100 && tc.passed == 5000) ? 0 : 1;
    }

    if (stageLower == L"1" || stageLower == L"stage1" || stageLower == L"all") {
        RunStage1Tests(tc);
    }
    if (stageLower == L"2" || stageLower == L"stage2" || stageLower == L"all") {
        RunStage2Tests(tc);
    }
    if (stageLower == L"3" || stageLower == L"stage3" || stageLower == L"all") {
        RunStage3Tests(tc);
    }
    if (stageLower == L"4" || stageLower == L"stage4" || stageLower == L"all") {
        RunStage4Tests(tc);
    }
    if (stageLower == L"5" || stageLower == L"stage5" || stageLower == L"all") {
        RunStage5Tests(tc);
    }
    if (stageLower == L"6" || stageLower == L"stage6" || stageLower == L"all") {
        RunStage6Tests(tc);
    }
    if (stageLower == L"7" || stageLower == L"stage7" || stageLower == L"all") {
        RunStage7Tests(tc);
    }
    if (stageLower == L"8" || stageLower == L"stage8" || stageLower == L"all") {
        RunStage8Tests(tc);
    }
    if (stageLower == L"9" || stageLower == L"stage9" || stageLower == L"all") {
        RunStage9Tests(tc);
    }
    if (stageLower == L"10" || stageLower == L"stage10" || stageLower == L"all") {
        RunStage10Tests(tc);
    }
    if (stageLower == L"12" || stageLower == L"stage12" || stageLower == L"pdf" || stageLower == L"all") {
        RunStage12Tests(tc);
    }
    if (stageLower == L"13" || stageLower == L"stage13" || stageLower == L"transfers" || stageLower == L"all") {
        RunStage13Tests(tc);
    }
    if (stageLower == L"14" || stageLower == L"stage14" || stageLower == L"formats200" || stageLower == L"all") {
        RunStage14Tests(tc);
    }

    wchar_t summary[256] = {};
    swprintf_s(summary, L"=== SELF-TEST SUMMARY (%s): %d PASSED, %d FAILED ===\r\n",
        stageName.c_str(), tc.passed, tc.failed);
    tc.log += summary;

    if (!outReportPath.empty()) {
        std::string u8 = PathUtils::WideToUtf8(tc.log);
        PathUtils::WriteFileBytes(outReportPath, u8.data(), u8.size());
    }
    return (tc.failed == 0 && tc.passed > 0) ? 0 : 1;
}

// ============================================================================
// Robustness diagnostics: run the file readers on a list of files (see SelfTestSuite.h)
// ============================================================================
namespace {

HANDLE g_inspectOut = INVALID_HANDLE_VALUE;
volatile LONG g_inspectIndex = -1;
const char* volatile g_inspectReader = "";
std::atomic<ULONGLONG> g_inspectStepStart{ 0 };

void InspectWrite(const char* text, int len) {
    if (len <= 0) return;
    DWORD written = 0;
    WriteFile(g_inspectOut, text, static_cast<DWORD>(len), &written, nullptr);
}

// Records where the process died and ends it at once, without Windows Error Reporting getting involved
LONG WINAPI InspectCrashFilter(EXCEPTION_POINTERS* ep) {
    const DWORD code = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionCode : 0;
    const ULONG_PTR addr = (ep && ep->ExceptionRecord) ? reinterpret_cast<ULONG_PTR>(ep->ExceptionRecord->ExceptionAddress) : 0;
    char moduleName[64] = "?";
    ULONG_PTR offset = 0;
    HMODULE mod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(addr), &mod) && mod) {
        offset = addr - reinterpret_cast<ULONG_PTR>(mod);
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(mod, path, MAX_PATH);
        const wchar_t* leaf = wcsrchr(path, L'\\');
        WideCharToMultiByte(CP_UTF8, 0, leaf ? leaf + 1 : path, -1, moduleName, sizeof(moduleName) - 1, nullptr, nullptr);
    }
    char line[224] = {};
    const int n = sprintf_s(line, "X\t%ld\t%s\t%08lX\t%s\t%llX\n", static_cast<long>(g_inspectIndex), g_inspectReader,
                            code, moduleName, static_cast<unsigned long long>(offset));
    InspectWrite(line, n);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

int InspectFiles(const std::wstring& listFile, const std::wstring& resultFile) {
    std::vector<uint8_t> raw;
    if (!PathUtils::ReadFileBytes(listFile, 64 * 1024 * 1024, raw)) return 2;
    std::vector<std::wstring> paths;
    {
        std::string_view text(reinterpret_cast<const char*>(raw.data()), raw.size());
        size_t pos = 0;
        while (pos < text.size()) {
            size_t eol = text.find('\n', pos);
            if (eol == std::string_view::npos) eol = text.size();
            std::string_view line = text.substr(pos, eol - pos);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
            if (!line.empty()) paths.push_back(PathUtils::Utf8ToWide(line));
            pos = eol + 1;
        }
    }

    g_inspectOut = CreateFileW(resultFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_inspectOut == INVALID_HANDLE_VALUE) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(InspectCrashFilter);

    // A reader that never returns is a fault too: note it and end the process so the driver can go on
    std::atomic<bool> finished{ false };
    std::thread watchdog([&finished]() {
        while (!finished.load()) {
            Sleep(250);
            const ULONGLONG started = g_inspectStepStart.load();
            if (started != 0 && GetTickCount64() - started > 20000) {
                char line[128] = {};
                const int n = sprintf_s(line, "H\t%ld\t%s\n", static_cast<long>(g_inspectIndex), g_inspectReader);
                InspectWrite(line, n);
                TerminateProcess(GetCurrentProcess(), 4);
            }
        }
    });

    for (size_t i = 0; i < paths.size(); ++i) {
        const std::wstring& path = paths[i];
        const std::wstring ext = PathUtils::GetExtension(path);
        bool anyReader = false;

        auto step = [&](const char* reader, const std::function<bool()>& run) {
            anyReader = true;
            g_inspectIndex = static_cast<LONG>(i);
            g_inspectReader = reader;
            char line[512] = {};
            InspectWrite(line, sprintf_s(line, "B\t%zu\t%s\n", i, reader));
            const ULONGLONG started = GetTickCount64();
            g_inspectStepStart.store(started);
            bool accepted = false;
            std::string thrown;
            bool didThrow = false;
            const uint64_t stoppedBefore = ReaderGuard::StoppedCount();
            try {
                accepted = run();
            } catch (const std::exception& e) {
                didThrow = true;
                thrown = e.what();
            } catch (...) {
                didThrow = true;
                thrown = "unknown C++ exception";
            }
            // An exception that the reader's own safety net stopped is still a fault of the reader
            if (!didThrow && ReaderGuard::StoppedCount() != stoppedBefore) {
                didThrow = true;
                thrown = ReaderGuard::LastMessage();
            }
            g_inspectStepStart.store(0);
            if (didThrow) {
                if (thrown.size() > 300) thrown.resize(300);
                InspectWrite(line, sprintf_s(line, "T\t%zu\t%s\t%s\n", i, reader, thrown.c_str()));
            } else {
                InspectWrite(line, sprintf_s(line, "E\t%zu\t%s\t%d\t%llu\n", i, reader, accepted ? 1 : 0,
                                             static_cast<unsigned long long>(GetTickCount64() - started)));
            }
        };

        // Self-check of this diagnostic itself (tools/fuzz_samples.py --selfcheck): three file names make a
        // "reader" crash, throw and hang on purpose, to prove that each kind of fault is caught and reported.
        if (path.find(L"__inspect_selfcheck_") != std::wstring::npos) {
            step("selfcheck", [&]() -> bool {
                if (path.find(L"__inspect_selfcheck_crash") != std::wstring::npos) {
                    volatile int* nowhere = nullptr;
                    *nowhere = 1;
                } else if (path.find(L"__inspect_selfcheck_throw") != std::wstring::npos) {
                    throw std::runtime_error("selfcheck exception");
                } else if (path.find(L"__inspect_selfcheck_hang") != std::wstring::npos) {
                    for (;;) Sleep(1000);
                }
                return true;
            });
            continue;
        }

        if (ImageFormatLoader::IsExtendedImageExtension(ext)) {
            step("image", [&]() {
                ExtendedImageInfo info;
                return ImageFormatLoader::DecodeImageFile(path, 512, info) && info.valid;
            });
        }
        if (Model3DViewer::Is3DModelExtension(ext)) {
            step("mesh", [&]() {
                LoadedMesh3D mesh;
                return Model3DViewer::InspectMeshFile(path, mesh) && mesh.loadedOk;
            });
        }
        if (EdaViewer::IsEdaExtension(ext)) {
            step("eda", [&]() {
                ParsedEdaBoard board;
                return EdaViewer::InspectEdaFile(path, board) && board.valid;
            });
        }
        if (SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(ext)) {
            step("doc", [&]() {
                ParsedOfficeDocument doc;
                return SpreadsheetDocViewer::InspectOfficeFile(path, doc) && doc.valid;
            });
        }
        if (PdfViewer::IsPdfExtension(ext)) {
            step("pdf", [&]() {
                PdfDocumentSummary pdf;
                return PdfViewer::InspectPdfFile(path, pdf) && pdf.valid;
            });
        }
        if (ArchiveExplorer::IsArchiveExtension(ext)) {
            step("archive", [&]() {
                ArchiveSummaryInfo arc;
                if (!ArchiveExplorer::InspectArchiveFile(path, arc) || !arc.valid) return false;
                // Unpack the first file as well, for the formats Explorer2 unpacks with its own code
                if (arc.formatName.rfind(L"ZIP", 0) == 0 || arc.formatName.rfind(L"TAR", 0) == 0) {
                    for (const auto& entry : arc.entries) {
                        if (entry.isDirectory) continue;
                        std::vector<uint8_t> bytes;
                        ArchiveExplorer::ExtractSingleEntryToMemory(path, entry.pathInArchive, bytes);
                        break;
                    }
                }
                return true;
            });
        }
        if (!anyReader) {
            char line[64] = {};
            InspectWrite(line, sprintf_s(line, "N\t%zu\n", i));
        }
    }

    InspectWrite("D\n", 2);
    finished.store(true);
    watchdog.join();
    CloseHandle(g_inspectOut);
    g_inspectOut = INVALID_HANDLE_VALUE;
    return 0;
}

} // namespace Explorer2::SelfTestSuite
