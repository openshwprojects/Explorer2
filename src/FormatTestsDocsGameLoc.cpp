// FormatTestsDocsGameLoc.cpp — Stage 15 self-tests for Game RPG Tables, Localization Catalogs, World Maps & Bitmap Fonts
#include "SelfTestCommon.h"
#include "TestBytes.h"
#include "SpreadsheetDocViewer.h"
#include "PathUtils.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace Explorer2::SelfTestSuite {

namespace {

using namespace ::Explorer2::TestBytes;

inline void PushU16LE(std::vector<uint8_t>& b, uint16_t v) { AppendU16LE(b, v); }
inline void PushU32LE(std::vector<uint8_t>& b, uint32_t v) { AppendU32LE(b, v); }
inline void PushI16LE(std::vector<uint8_t>& b, int16_t v)  { AppendU16LE(b, static_cast<uint16_t>(v)); }
inline void PushI32LE(std::vector<uint8_t>& b, int32_t v)  { AppendI32LE(b, v); }
inline void PushU32BE(std::vector<uint8_t>& b, uint32_t v) { AppendU32BE(b, v); }

inline void PushFixedAscii(std::vector<uint8_t>& b, std::string_view s, size_t fixedLen) {
    const size_t take = (s.size() < fixedLen) ? s.size() : fixedLen;
    AppendBytes(b, s.data(), take);
    if (fixedLen > take) {
        b.insert(b.end(), fixedLen - take, 0u);
    }
}

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

} // namespace

void RunFormatTestsDocsGameLoc(TestCounter& tc) {
    const std::wstring docsDir   = TestDataDir() + L"\\docs";
    const std::wstring dataDir   = TestDataDir() + L"\\data";
    const std::wstring imagesDir = TestDataDir() + L"\\images";
    const std::wstring workDir   = ScratchTestDir() + L"\\fmt_doc_game_loc";
    CreateDirectoryW(workDir.c_str(), nullptr);
    std::vector<std::wstring> createdDocFiles;

    auto writeDocBytes = [&](const wchar_t* name, const std::vector<uint8_t>& bytes) -> std::wstring {
        std::wstring p = workDir + L"\\" + name;
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
    // 1. BioWare Aurora / Odyssey 2D Array Table (.2da — Binary & ASCII)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".2da"),
                  L"Stage15.Docs.BioWare2DA_ExtensionRegistered");

        ParsedOfficeDocument d1;
        bool ok1 = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\kotor_lightsabers.2da", d1);
        // 1 header row + 2 data rows = 3 rows, 4 cols (Row, label, modelname, sabercolor)
        tc.Record(ok1 && d1.valid && d1.isSpreadsheet && d1.sheets.size() == 1 &&
                  d1.sheets[0].rows.size() == 3 && d1.sheets[0].maxCols == 4 &&
                  d1.sheets[0].rows[2][1] == L"ShortSaber" && d1.sheets[0].rows[2][3] == L"Purple",
                  L"Stage15.Docs.BioWare2DA_KotorBinaryParsed");

        // Second variant: Neverwinter Nights ASCII "2DA V2.0" with DEFAULT: & quoted string cells
        const char* nwn2da =
            "2DA V2.0\n"
            "DEFAULT: NONE\n\n"
            "     Label             CR     SoundSetFile\n"
            "0    \"Goblin Chief\"  2.0    c_goblin\n"
            "1    \"Dire Wolf\"     3.0    ****\n";
        ParsedOfficeDocument d2;
        bool ok2 = SpreadsheetDocViewer::InspectOfficeFile(writeDocText(L"creatures.2da", nwn2da), d2);
        tc.Record(ok2 && d2.valid && d2.sheets.size() == 1 && d2.sheets[0].rows.size() == 3 &&
                  d2.sheets[0].rows[1][1] == L"Goblin Chief" && d2.sheets[0].rows[2][3] == L"NONE" &&
                  HashDocContent(d1) != HashDocContent(d2),
                  L"Stage15.Docs.BioWare2DA_NwnAsciiDefaultAndDistinctHash");

        // Negative 1: wrong column count in ASCII data row
        assertDocRejected(L"Stage15.Docs.BioWare2DA_RejectMismatchedColumnCount",
                          writeDocText(L"bad_cols.2da", "2DA V2.0\n\nColA ColB ColC\n0 OnlyOneCell\n"));

        // Negative 2: cell offset out of bounds in binary 2DA
        std::vector<uint8_t> badBin2da = { '2','D','A',' ','V','2','.','b','\n','A','\t',0 };
        PushU32LE(badBin2da, 1u); // 1 row
        badBin2da.push_back('0'); badBin2da.push_back('\t');
        PushU16LE(badBin2da, 999u); // relOff = 999 > dataSize(2)
        PushU16LE(badBin2da, 2u);
        badBin2da.push_back('X'); badBin2da.push_back(0u);
        assertDocRejected(L"Stage15.Docs.BioWare2DA_RejectBinaryCellOffsetOob",
                          writeDocBytes(L"bad_off.2da", badBin2da));
    }

    // ========================================================================
    // 2. BioWare Dialog Talk Table (.tlk — Aurora/Odyssey & Infinity Engine)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".tlk"),
                  L"Stage15.Docs.BioWareTLK_ExtensionRegistered");

        ParsedOfficeDocument d1;
        bool ok1 = SpreadsheetDocViewer::InspectOfficeFile(docsDir + L"\\kotor_dialog.tlk", d1);
        tc.Record(ok1 && d1.valid && d1.isSpreadsheet && d1.sheets.size() == 1 &&
                  d1.sheets[0].rows.size() == 3 &&
                  d1.sheets[0].rows[1][1] == L"vo_hk47_001" &&
                  d1.sheets[0].rows[1][3].find(L"HK-47") != std::wstring::npos,
                  L"Stage15.Docs.BioWareTLK_ResRefDurationAndTextParsed");

        // Second variant: Infinity Engine "TLK V1  " (Baldur's Gate / Planescape: Torment)
        const char* bgLine = "You must gather your party before venturing forth.";
        const uint32_t bgLen = static_cast<uint32_t>(std::strlen(bgLine));
        std::vector<uint8_t> tlkInf = { 'T','L','K',' ','V','1',' ',' ' };
        PushU16LE(tlkInf, 0u);  // langId = English
        PushU32LE(tlkInf, 1u);  // 1 string
        PushU32LE(tlkInf, 44u); // poolOff = 18 + 26 = 44
        // 26-byte entry: flags(2) + snd[8] + vol(4) + pitch(4) + relOff(4) + strLen(4)
        PushU16LE(tlkInf, 3u);
        PushFixedAscii(tlkInf, "GAM_06", 8);
        PushU32LE(tlkInf, 0u);
        PushU32LE(tlkInf, 0u);
        PushU32LE(tlkInf, 0u);
        PushU32LE(tlkInf, bgLen);
        tlkInf.insert(tlkInf.end(), bgLine, bgLine + bgLen);

        ParsedOfficeDocument d2;
        bool ok2 = SpreadsheetDocViewer::InspectOfficeFile(writeDocBytes(L"bg2_dialog.tlk", tlkInf), d2);
        tc.Record(ok2 && d2.valid && d2.sheets.size() == 1 &&
                  d2.sheets[0].rows[1][1] == L"GAM_06" &&
                  d2.sheets[0].rows[1][2] == L"You must gather your party before venturing forth." &&
                  HashDocContent(d1) != HashDocContent(d2),
                  L"Stage15.Docs.BioWareTLK_InfinityEngineAndDistinctHash");

        // Negative 1: string slice running past EOF
        std::vector<uint8_t> badSlice = tlkInf;
        badSlice[18 + 22] = 0xFFu; // strLen = 255 > actual remaining
        assertDocRejected(L"Stage15.Docs.BioWareTLK_RejectStringPastEof",
                          writeDocBytes(L"bad_slice.tlk", badSlice));

        // Negative 2: poolOff overlapping entry table
        std::vector<uint8_t> badPool = tlkInf;
        badPool[0x0E] = 20u; // poolOff = 20 < 44
        assertDocRejected(L"Stage15.Docs.BioWareTLK_RejectOverlappingPoolOffset",
                          writeDocBytes(L"bad_pool.tlk", badPool));
    }

    // ========================================================================
    // 3. BioWare Generic File Format (.gff)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".gff"),
                  L"Stage15.Docs.BioWareGFF_ExtensionRegistered");

        ParsedOfficeDocument d1;
        bool ok1 = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\hk47_creature.gff", d1);
        // Header + Tag + TemplateResRef + HitPoints + Equip_ItemList + Equip_ItemList[0].EquippedRes = 6 rows
        tc.Record(ok1 && d1.valid && d1.isSpreadsheet && d1.sheets.size() == 1 &&
                  d1.sheets[0].rows.size() == 6 &&
                  d1.sheets[0].rows[1][2] == L"HK47_DROID" &&
                  d1.sheets[0].rows[5][0] == L"Equip_ItemList[0].EquippedRes" &&
                  d1.sheets[0].rows[5][2] == L"w_blstr_004",
                  L"Stage15.Docs.BioWareGFF_UtcCreatureListAndResRefsParsed");

        // Second variant: single-field "DLG V3.2" with CExoLocString
        std::vector<uint8_t> dlgGff;
        const char* dlgMagic = "DLG V3.2";
        dlgGff.insert(dlgGff.end(), dlgMagic, dlgMagic + 8);
        // Struct[0] @ 56 (12B): 1 field -> direct fieldIdx 0
        // Field[0]  @ 68 (12B): type=12 (CExoLocString), lbl=0, off=0
        // Label[0]  @ 80 (16B): "Greeting"
        // FData     @ 96 (25B): totSz(21), strRef(0xFFFFFFFF), cnt(1), id(0=English), len(5), "Hello"
        PushU32LE(dlgGff, 56u); PushU32LE(dlgGff, 1u);
        PushU32LE(dlgGff, 68u); PushU32LE(dlgGff, 1u);
        PushU32LE(dlgGff, 80u); PushU32LE(dlgGff, 1u);
        PushU32LE(dlgGff, 96u); PushU32LE(dlgGff, 25u);
        PushU32LE(dlgGff, 0u);  PushU32LE(dlgGff, 0u);
        PushU32LE(dlgGff, 0u);  PushU32LE(dlgGff, 0u);
        // Struct[0]
        PushU32LE(dlgGff, 0xFFFFFFFFu); PushU32LE(dlgGff, 0u); PushU32LE(dlgGff, 1u);
        // Field[0]
        PushU32LE(dlgGff, 12u); PushU32LE(dlgGff, 0u); PushU32LE(dlgGff, 0u);
        // Label[0]
        PushFixedAscii(dlgGff, "Greeting", 16);
        // FData: CExoLocString (4 + 21 = 25 bytes)
        PushU32LE(dlgGff, 21u); PushU32LE(dlgGff, 0xFFFFFFFFu); PushU32LE(dlgGff, 1u);
        PushU32LE(dlgGff, 0u);  PushU32LE(dlgGff, 5u);
        const char* hi = "Hello";
        dlgGff.insert(dlgGff.end(), hi, hi + 5);

        ParsedOfficeDocument d2;
        bool ok2 = SpreadsheetDocViewer::InspectOfficeFile(writeDocBytes(L"greet.gff", dlgGff), d2);
        tc.Record(ok2 && d2.valid && d2.sheets.size() == 1 &&
                  d2.sheets[0].rows[1][2] == L"English: Hello" &&
                  HashDocContent(d1) != HashDocContent(d2),
                  L"Stage15.Docs.BioWareGFF_DlgLocStringAndDistinctHash");

        // Negative 1: Struct field pointing back to Struct 0 (recursive cycle bomb!) -> must reject cleanly
        std::vector<uint8_t> cycGff = dlgGff;
        cycGff[68] = 14u; // FieldType = 14 (Struct), fValOff = 0 (points to Struct 0!)
        assertDocRejected(L"Stage15.Docs.BioWareGFF_RejectStructSelfCycleBomb",
                          writeDocBytes(L"cycle.gff", cycGff));

        // Negative 2: pool offset past EOF
        std::vector<uint8_t> badPool = dlgGff;
        badPool[0x21] = 0x40u; // fdataOff huge
        assertDocRejected(L"Stage15.Docs.BioWareGFF_RejectFieldDataPastEof",
                          writeDocBytes(L"bad_fdata.gff", badPool));
    }

    // ========================================================================
    // 4. GNU Gettext Compiled Machine Object Catalog (.mo — LE & BE)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".mo"),
                  L"Stage15.Docs.GettextMO_ExtensionRegistered");

        ParsedOfficeDocument d1;
        bool ok1 = SpreadsheetDocViewer::InspectOfficeFile(docsDir + L"\\godot_mod_pl.mo", d1);
        // Header + 2 translated entries (HUD context + plural) = 3 rows
        tc.Record(ok1 && d1.valid && d1.isSpreadsheet && d1.sheets.size() == 1 &&
                  d1.sheets[0].rows.size() == 3 &&
                  d1.sheets[0].rows[1][1] == L"HUD" &&
                  d1.sheets[0].rows[1][2] == L"Health" &&
                  d1.sheets[0].rows[2][4] == L"plural",
                  L"Stage15.Docs.GettextMO_LeContextPluralsAndUtf8Parsed");

        // Second variant: Big-Endian 0x950412DE .mo catalog
        std::vector<uint8_t> beMo;
        PushU32BE(beMo, 0x950412DEu);
        PushU32BE(beMo, 0u);  // rev 0.0
        PushU32BE(beMo, 1u);  // 1 string
        PushU32BE(beMo, 28u); // origTable @ 28
        PushU32BE(beMo, 36u); // transTable @ 36
        PushU32BE(beMo, 0u);
        PushU32BE(beMo, 0u);
        // orig @ 44 ("Save", len=4), trans @ 49 ("Speichern", len=9)
        PushU32BE(beMo, 4u); PushU32BE(beMo, 44u);
        PushU32BE(beMo, 9u); PushU32BE(beMo, 49u);
        const char* sOrig = "Save";
        const char* sTran = "Speichern";
        beMo.insert(beMo.end(), sOrig, sOrig + 5); // includes NUL
        beMo.insert(beMo.end(), sTran, sTran + 10);

        ParsedOfficeDocument d2;
        bool ok2 = SpreadsheetDocViewer::InspectOfficeFile(writeDocBytes(L"german_be.mo", beMo), d2);
        tc.Record(ok2 && d2.valid && d2.sheets.size() == 1 &&
                  d2.sheets[0].rows[1][3] == L"Speichern" &&
                  HashDocContent(d1) != HashDocContent(d2),
                  L"Stage15.Docs.GettextMO_BigEndianAndDistinctHash");

        // Negative 1: missing NUL terminator byte after string
        std::vector<uint8_t> noNul = beMo;
        noNul.back() = 'X';
        assertDocRejected(L"Stage15.Docs.GettextMO_RejectMissingNulTerminator",
                          writeDocBytes(L"no_nul.mo", noNul));

        // Negative 2: unsupported major revision (> 1)
        std::vector<uint8_t> badRev = beMo;
        badRev[5] = 5u; // revMajor = 5
        assertDocRejected(L"Stage15.Docs.GettextMO_RejectUnsupportedMajorRevision",
                          writeDocBytes(L"bad_rev.mo", badRev));
    }

    // ========================================================================
    // 5. GNU Gettext Portable Object Catalog (.po / .pot)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".po") &&
                  SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".pot"),
                  L"Stage15.Docs.GettextPO_ExtensionsRegistered");

        ParsedOfficeDocument d1;
        bool ok1 = SpreadsheetDocViewer::InspectOfficeFile(docsDir + L"\\kicad_router_de.po", d1);
        // Header + 2 active messages (obsolete #~ skipped) = 3 rows
        tc.Record(ok1 && d1.valid && d1.isSpreadsheet && d1.sheets.size() == 1 &&
                  d1.sheets[0].rows.size() == 3 &&
                  d1.sheets[0].rows[1][1] == L"Router" &&
                  d1.sheets[0].rows[1][4].find(L"fuzzy") != std::wstring::npos,
                  L"Stage15.Docs.GettextPO_MultilinePluralsFlagsAndObsoleteSkipped");

        const char* potTxt =
            "msgid \"\"\n"
            "msgstr \"Project-Id-Version: Wesnoth 1.18\\n\"\n\n"
            "msgid \"End Turn\"\n"
            "msgstr \"\"\n";
        ParsedOfficeDocument d2;
        bool ok2 = SpreadsheetDocViewer::InspectOfficeFile(writeDocText(L"template.pot", potTxt), d2);
        tc.Record(ok2 && d2.valid && d2.sheets[0].rows.size() == 2 &&
                  d2.sheets[0].rows[1][2] == L"End Turn" &&
                  HashDocContent(d1) != HashDocContent(d2),
                  L"Stage15.Docs.GettextPO_PotTemplateAndDistinctHash");

        // Negative 1: unterminated string quote
        assertDocRejected(L"Stage15.Docs.GettextPO_RejectUnterminatedQuote",
                          writeDocText(L"bad_quote.po", "msgid \"Unclosed\nmsgstr \"Ok\"\n"));

        // Negative 2: orphan msgid without msgstr
        assertDocRejected(L"Stage15.Docs.GettextPO_RejectOrphanMsgidWithoutMsgstr",
                          writeDocText(L"orphan.po", "msgid \"MissingTranslation\"\n"));
    }

    // ========================================================================
    // 6. ARM Open-CMSIS-Pack MCU Device & Memory Map Description (.pdsc)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".pdsc"),
                  L"Stage15.Docs.ArmCmsisPDSC_ExtensionRegistered");

        ParsedOfficeDocument d1;
        bool ok1 = SpreadsheetDocViewer::InspectOfficeFile(
            docsDir + L"\\STMicroelectronics.STM32H7xx_DFP.pdsc", d1);
        // 3 sheets: Devices (STM32H743ZITx @ 480 MHz), Memory_Map (3 regions), Components_Boards (Board + Component)
        tc.Record(ok1 && d1.valid && d1.isSpreadsheet && d1.sheets.size() == 3 &&
                  d1.sheets[0].rows[1][0] == L"STM32H743ZITx" &&
                  d1.sheets[0].rows[1][3] == L"480" &&
                  d1.sheets[1].rows.size() == 4 &&
                  d1.sheets[2].rows.size() == 3,
                  L"Stage15.Docs.ArmCmsisPDSC_DevicesMemoryBoardsAndComponentsParsed");

        const char* nrfPdsc =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<package schemaVersion=\"1.7.2\">\n"
            "  <vendor>NordicSemiconductor</vendor>\n"
            "  <name>nRF_DeviceFamilyPack</name>\n"
            "  <devices>\n"
            "    <family Dfamily=\"nRF52 Series\">\n"
            "      <processor Dcore=\"Cortex-M4\" Dfpu=\"SP_FPU\" Dclock=\"64000000\"/>\n"
            "      <device Dname=\"nRF52840_xxAA\">\n"
            "        <memory name=\"IROM1\" access=\"rx\" start=\"0x00000000\" size=\"0x00100000\" startup=\"1\"/>\n"
            "      </device>\n"
            "    </family>\n"
            "  </devices>\n"
            "</package>\n";
        ParsedOfficeDocument d2;
        bool ok2 = SpreadsheetDocViewer::InspectOfficeFile(writeDocText(L"Nordic.nRF52.pdsc", nrfPdsc), d2);
        tc.Record(ok2 && d2.valid && d2.sheets.size() == 2 &&
                  d2.sheets[0].rows[1][0] == L"nRF52840_xxAA" &&
                  HashDocContent(d1) != HashDocContent(d2),
                  L"Stage15.Docs.ArmCmsisPDSC_NordicNrf52AndDistinctHash");

        // Negative 1: unrelated XML (<svg> instead of <package>)
        assertDocRejected(L"Stage15.Docs.ArmCmsisPDSC_RejectNonPackageXml",
                          writeDocText(L"icon.pdsc", "<svg width=\"16\" height=\"16\"><rect/></svg>\n"));

        // Negative 2: truncated unclosed <package> XML
        assertDocRejected(L"Stage15.Docs.ArmCmsisPDSC_RejectTruncatedXml",
                          writeDocText(L"trunc.pdsc",
                              "<package><vendor>ARM</vendor><name>CMSIS</name><devices><family Dfamily=\"M0\">"));
    }

    // ========================================================================
    // 7. Unreal Engine Binary Localization Resource (.locres)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".locres"),
                  L"Stage15.Docs.UnrealLOCRES_ExtensionRegistered");

        ParsedOfficeDocument d1;
        bool ok1 = SpreadsheetDocViewer::InspectOfficeFile(docsDir + L"\\ue5_game_en.locres", d1);
        tc.Record(ok1 && d1.valid && d1.sheets.size() == 1 && d1.sheets[0].rows.size() == 3 &&
                  d1.sheets[0].rows[1][0] == L"GameUI" && d1.sheets[0].rows[1][3] == L"Start Campaign",
                  L"Stage15.Docs.UnrealLOCRES_OptimizedCrcSample");

        // Second variant: LocRes Compact (version 1, no hash prefixes, UTF-16LE FString in pool)
        std::vector<uint8_t> locresCompact = {
            0x0E, 0x14, 0x74, 0x75, 0x67, 0x4A, 0x03, 0xFC,
            0x4A, 0x15, 0x90, 0x9D, 0xC3, 0x37, 0x7F, 0x1B
        };
        locresCompact.push_back(1u); // version 1
        const size_t strOffPos = locresCompact.size();
        for (int i = 0; i < 8; ++i) locresCompact.push_back(0u);
        PushU32LE(locresCompact, 1u);  // nsCount = 1
        PushI32LE(locresCompact, 4); locresCompact.insert(locresCompact.end(), { 'H','U','D', 0 });
        PushU32LE(locresCompact, 1u);  // keyCount = 1
        PushI32LE(locresCompact, 3); locresCompact.insert(locresCompact.end(), { 'H','P', 0 });
        PushU32LE(locresCompact, 0x1234u); PushI32LE(locresCompact, 0);
        const uint64_t poolOff = locresCompact.size();
        std::memcpy(locresCompact.data() + strOffPos, &poolOff, 8);
        PushU32LE(locresCompact, 1u);
        PushI32LE(locresCompact, -3); // UTF-16LE "OK\0" (3 code units)
        PushU16LE(locresCompact, L'O'); PushU16LE(locresCompact, L'K'); PushU16LE(locresCompact, 0u);

        ParsedOfficeDocument d2;
        bool ok2 = SpreadsheetDocViewer::InspectOfficeFile(writeDocBytes(L"compact_utf16.locres", locresCompact), d2);
        tc.Record(ok2 && d2.valid && d2.sheets[0].rows[1][3] == L"OK" &&
                  HashDocContent(d1) != HashDocContent(d2),
                  L"Stage15.Docs.UnrealLOCRES_CompactUtf16AndDistinctHash");

        locresCompact[16] = 99u; // invalid version
        assertDocRejected(L"Stage15.Docs.UnrealLOCRES_RejectUnsupportedVersion",
                          writeDocBytes(L"bad.locres", locresCompact));
    }

    // ========================================================================
    // 8. Maxis The Sims String Table (.stbl)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".stbl"),
                  L"Stage15.Docs.SimsSTBL_ExtensionRegistered");

        ParsedOfficeDocument d1;
        bool ok1 = SpreadsheetDocViewer::InspectOfficeFile(docsDir + L"\\sims4_strings_eng.stbl", d1);
        tc.Record(ok1 && d1.valid && d1.sheets.size() == 1 && d1.sheets[0].rows.size() == 3 &&
                  d1.sheets[0].rows[1][2] == L"Reticulating Splines...",
                  L"Stage15.Docs.SimsSTBL_Sims4Sample");

        // Second variant: Sims 3 STBL (17B header + u64 hash + u32 charCount + UTF-16LE)
        std::vector<uint8_t> stblSims3 = { 'S','T','B','L', 2u, 0u, 0u };
        PushU32LE(stblSims3, 1u); PushU32LE(stblSims3, 0u); // u64 count = 1
        PushU16LE(stblSims3, 0u);
        PushU32LE(stblSims3, 0xDEADBEEFu); PushU32LE(stblSims3, 0x01020304u);
        PushU32LE(stblSims3, 4u); // 4 UTF-16 chars: "Noob"
        for (wchar_t wc : { L'N', L'o', L'o', L'b' }) PushU16LE(stblSims3, static_cast<uint16_t>(wc));

        ParsedOfficeDocument d2;
        bool ok2 = SpreadsheetDocViewer::InspectOfficeFile(writeDocBytes(L"sims3_utf16.stbl", stblSims3), d2);
        tc.Record(ok2 && d2.valid && d2.sheets[0].rows[1][2] == L"Noob" &&
                  HashDocContent(d1) != HashDocContent(d2),
                  L"Stage15.Docs.SimsSTBL_Sims3Utf16AndDistinctHash");

        stblSims3.push_back(0xAAu); // trailing garbage mismatching exact STBL size
        assertDocRejected(L"Stage15.Docs.SimsSTBL_RejectTrailingGarbage",
                          writeDocBytes(L"bad.stbl", stblSims3));
    }

    // ========================================================================
    // 9. Nintendo JSystem Binary CSV Table (.bcsv)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".bcsv"),
                  L"Stage15.Docs.NintendoBCSV_ExtensionRegistered");

        ParsedOfficeDocument d1;
        bool ok1 = SpreadsheetDocViewer::InspectOfficeFile(dataDir + L"\\galaxy_stage_obj.bcsv", d1);
        tc.Record(ok1 && d1.valid && d1.sheets.size() == 1 && d1.sheets[0].rows.size() == 3 &&
                  d1.sheets[0].rows[1][0] == L"StarPieceCluster" && d1.sheets[0].rows[2][0] == L"LaunchStar",
                  L"Stage15.Docs.NintendoBCSV_BigEndianGalaxySample");

        // Second variant: 3DS little-endian BCSV (1 row, 1 S32 field -> dataOff = 28)
        std::vector<uint8_t> bcsvLe;
        PushU32LE(bcsvLe, 1u); PushU32LE(bcsvLe, 1u); PushU32LE(bcsvLe, 28u); PushU32LE(bcsvLe, 4u);
        PushU32LE(bcsvLe, 0xAABBCCDDu); PushU32LE(bcsvLe, 0xFFFFFFFFu); PushU16LE(bcsvLe, 0u);
        bcsvLe.push_back(0u); bcsvLe.push_back(0u); // shift=0, type=0 (S32)
        PushI32LE(bcsvLe, 777);

        ParsedOfficeDocument d2;
        bool ok2 = SpreadsheetDocViewer::InspectOfficeFile(writeDocBytes(L"3ds_le.bcsv", bcsvLe), d2);
        tc.Record(ok2 && d2.valid && d2.sheets[0].rows[1][0] == L"777" &&
                  HashDocContent(d1) != HashDocContent(d2),
                  L"Stage15.Docs.NintendoBCSV_LittleEndianAndDistinctHash");

        bcsvLe[8] = 99u; // dataOff != 16 + fieldCount*12
        assertDocRejected(L"Stage15.Docs.NintendoBCSV_RejectInvalidDataOffset",
                          writeDocBytes(L"bad.bcsv", bcsvLe));
    }

    // ========================================================================
    // 10. SCS Software ETS2 / ATS SiiNunit (.sii)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".sii"),
                  L"Stage15.Docs.ScsSII_ExtensionRegistered");

        ParsedOfficeDocument d1;
        bool ok1 = SpreadsheetDocViewer::InspectOfficeFile(docsDir + L"\\ets2_truck_engine.sii", d1);
        tc.Record(ok1 && d1.valid && d1.sheets.size() == 1 && d1.sheets[0].rows.size() == 7 &&
                  d1.sheets[0].rows[1][0] == L"accessory_engine_data",
                  L"Stage15.Docs.ScsSII_TruckEngineSample");

        ParsedOfficeDocument d2;
        bool ok2 = SpreadsheetDocViewer::InspectOfficeFile(writeDocText(L"chassis.sii",
            "SiiNunit\n{\naccessory_chassis_data : 6x4.kenworth.w900.chassis {\n  tank_size: 1135\n}\n}\n"), d2);
        tc.Record(ok2 && d2.valid && d2.sheets[0].rows[1][3] == L"1135" &&
                  HashDocContent(d1) != HashDocContent(d2),
                  L"Stage15.Docs.ScsSII_ChassisAndDistinctHash");

        assertDocRejected(L"Stage15.Docs.ScsSII_RejectUnbalancedBraces",
                          writeDocText(L"bad.sii", "SiiNunit\n{\nunit : a {\n  x: 1\n}\n"));
    }

    // ========================================================================
    // 11. Tiled Map Editor XML Tilemap (.tmx)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".tmx"),
                  L"Stage15.Docs.TiledTMX_ExtensionRegistered");

        ParsedOfficeDocument d1;
        bool ok1 = SpreadsheetDocViewer::InspectOfficeFile(docsDir + L"\\rpg_overworld.tmx", d1);
        // 10 non-zero GIDs in 4x3 CSV layer + 2 objects on Objects sheet
        tc.Record(ok1 && d1.valid && d1.sheets.size() == 2 &&
                  d1.sheets[0].rows[2][4] == L"10" && d1.sheets[1].rows.size() == 3,
                  L"Stage15.Docs.TiledTMX_CsvAndObjectGroupSample");

        // Second variant: base64 uncompressed 2x1 layer (`[3, 0]` -> 1 filled tile)
        ParsedOfficeDocument d2;
        bool ok2 = SpreadsheetDocViewer::InspectOfficeFile(writeDocText(L"b64.tmx",
            "<map version=\"1.10\" orientation=\"isometric\" width=\"2\" height=\"1\" tilewidth=\"32\" tileheight=\"16\">"
            "<layer id=\"1\" name=\"Iso\" width=\"2\" height=\"1\">"
            "<data encoding=\"base64\">AwAAAAAAAAA=</data></layer></map>"), d2);
        tc.Record(ok2 && d2.valid && d2.sheets[0].rows[1][4] == L"1" &&
                  HashDocContent(d1) != HashDocContent(d2),
                  L"Stage15.Docs.TiledTMX_Base64AndDistinctHash");

        assertDocRejected(L"Stage15.Docs.TiledTMX_RejectNonMapRoot",
                          writeDocText(L"bad.tmx", "<tileset name=\"ts\" tilewidth=\"16\" tileheight=\"16\"/>"));
    }

    // ========================================================================
    // 12. LDtk 2D Game World Project (.ldtk)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".ldtk"),
                  L"Stage15.Docs.Ldtk_ExtensionRegistered");

        ParsedOfficeDocument d1;
        bool ok1 = SpreadsheetDocViewer::InspectOfficeFile(docsDir + L"\\metroidvania_world.ldtk", d1);
        tc.Record(ok1 && d1.valid && d1.sheets.size() == 2 && d1.sheets[0].rows.size() == 3 &&
                  d1.sheets[0].rows[1][0] == L"Level_0_Entrance",
                  L"Stage15.Docs.Ldtk_GridVaniaWorldSample");

        ParsedOfficeDocument d2;
        bool ok2 = SpreadsheetDocViewer::InspectOfficeFile(writeDocText(L"arena.ldtk",
            "{\"jsonVersion\":\"1.5.3\",\"worldLayout\":\"Free\","
            "\"levels\":[{\"identifier\":\"BossRoom\",\"pxWid\":320,\"pxHei\":180,\"layerInstances\":[]}]}"), d2);
        tc.Record(ok2 && d2.valid && d2.sheets[0].rows[1][0] == L"BossRoom" &&
                  HashDocContent(d1) != HashDocContent(d2),
                  L"Stage15.Docs.Ldtk_BossRoomAndDistinctHash");

        assertDocRejected(L"Stage15.Docs.Ldtk_RejectMissingJsonVersion",
                          writeDocText(L"bad.ldtk", "{\"levels\":[{\"identifier\":\"L0\",\"pxWid\":64,\"pxHei\":64}]}"));
    }

    // ========================================================================
    // 13. AngelCode BMFont Binary Font (.bmf)
    // ========================================================================
    {
        tc.Record(SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(L".bmf"),
                  L"Stage15.Docs.BmFontBMF_ExtensionRegistered");

        ParsedOfficeDocument d1;
        bool ok1 = SpreadsheetDocViewer::InspectOfficeFile(imagesDir + L"\\hud_pixel_font.bmf", d1);
        tc.Record(ok1 && d1.valid && d1.sheets.size() == 2 && d1.sheets[0].rows.size() == 3 &&
                  d1.sheets[1].rows.size() == 2 && d1.sheets[1].rows[1][2] == L"-2",
                  L"Stage15.Docs.BmFontBMF_GlyphsAndKerningSample");

        // Second variant: single-glyph BMF without kerning block -> 1 sheet + distinct hash
        std::vector<uint8_t> bmfSingleGlyph = { 'B','M','F', 3u };
        // Block 1 (info): 14B + "UI\0"(3B) = 17B
        bmfSingleGlyph.push_back(1u); PushU32LE(bmfSingleGlyph, 17u);
        PushI16LE(bmfSingleGlyph, -12);
        for (int i = 0; i < 12; ++i) bmfSingleGlyph.push_back(0u);
        bmfSingleGlyph.insert(bmfSingleGlyph.end(), { 'U','I', 0u });
        // Block 2 (common): 15B
        bmfSingleGlyph.push_back(2u); PushU32LE(bmfSingleGlyph, 15u);
        PushU16LE(bmfSingleGlyph, 12u); PushU16LE(bmfSingleGlyph, 10u);
        PushU16LE(bmfSingleGlyph, 128u); PushU16LE(bmfSingleGlyph, 128u);
        for (int i = 0; i < 7; ++i) bmfSingleGlyph.push_back(0u);
        // Block 4 (chars): 20B (U+0030 '0')
        bmfSingleGlyph.push_back(4u); PushU32LE(bmfSingleGlyph, 20u);
        PushU32LE(bmfSingleGlyph, 0x30u);
        PushU16LE(bmfSingleGlyph, 0u); PushU16LE(bmfSingleGlyph, 0u);
        PushU16LE(bmfSingleGlyph, 8u); PushU16LE(bmfSingleGlyph, 12u);
        PushI16LE(bmfSingleGlyph, 0); PushI16LE(bmfSingleGlyph, 0); PushI16LE(bmfSingleGlyph, 8);
        bmfSingleGlyph.push_back(0u); bmfSingleGlyph.push_back(15u);

        ParsedOfficeDocument d2;
        bool ok2 = SpreadsheetDocViewer::InspectOfficeFile(writeDocBytes(L"digits.bmf", bmfSingleGlyph), d2);
        tc.Record(ok2 && d2.valid && d2.sheets.size() == 1 && d2.sheets[0].rows[1][1] == L"0" &&
                  HashDocContent(d1) != HashDocContent(d2),
                  L"Stage15.Docs.BmFontBMF_SingleGlyphAndDistinctHash");

        bmfSingleGlyph[bmfSingleGlyph.size() - 21u] = 19u; // chars block size 19 (not divisible by 20!)
        assertDocRejected(L"Stage15.Docs.BmFontBMF_RejectMisalignedCharsBlock",
                          writeDocBytes(L"bad.bmf", bmfSingleGlyph));
    }

    for (const auto& f : createdDocFiles) DeleteFileW(f.c_str());
    RemoveDirectoryW(workDir.c_str());
}

} // namespace Explorer2::SelfTestSuite
