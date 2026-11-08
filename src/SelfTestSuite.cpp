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
#include "LanScanner.h"
#include "BackupManager.h"
#include "FtpClient.h"
#include "FileEngine.h"
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>

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

} // namespace

int RunStageTests(const std::wstring& stageName, const std::wstring& outReportPath) {
    TestCounter tc;
    std::wstring stageLower = PathUtils::ToLower(stageName);

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

} // namespace Explorer2::SelfTestSuite
