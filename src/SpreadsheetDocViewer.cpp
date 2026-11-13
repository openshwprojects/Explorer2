#include "SpreadsheetDocViewer.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace Explorer2 {

namespace {

static D2D1_COLOR_F SdvColor(uint32_t rgb, float a = 1.0f) {
    return PathUtils::ColorFromRgb(rgb, a);
}

static std::wstring DecodeXmlEntities(std::wstring_view in) {
    std::wstring out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == L'&') {
            if (in.substr(i, 5) == L"&amp;")  { out.push_back(L'&');  i += 4; continue; }
            if (in.substr(i, 4) == L"&lt;")   { out.push_back(L'<');  i += 3; continue; }
            if (in.substr(i, 4) == L"&gt;")   { out.push_back(L'>');  i += 3; continue; }
            if (in.substr(i, 6) == L"&quot;") { out.push_back(L'"');  i += 5; continue; }
            if (in.substr(i, 6) == L"&apos;") { out.push_back(L'\''); i += 5; continue; }
        }
        out.push_back(in[i]);
    }
    return out;
}

static std::wstring ColIndexToExcelLetters(int colIdx) {
    std::wstring out;
    int c = colIdx;
    while (c >= 0) {
        out.insert(out.begin(), static_cast<wchar_t>(L'A' + (c % 26)));
        c = (c / 26) - 1;
    }
    return out;
}

static int ExcelCellRefToColIndex(std::string_view cellRef, int* outRowIdx = nullptr) {
    int col = 0;
    size_t i = 0;
    while (i < cellRef.size() && ((cellRef[i] >= 'A' && cellRef[i] <= 'Z') || (cellRef[i] >= 'a' && cellRef[i] <= 'z'))) {
        char ch = (cellRef[i] >= 'a') ? static_cast<char>(cellRef[i] - 'a' + 'A') : cellRef[i];
        col = col * 26 + (ch - 'A' + 1);
        ++i;
    }
    if (outRowIdx) {
        int r = std::atoi(std::string(cellRef.substr(i)).c_str());
        *outRowIdx = std::max(0, r - 1);
    }
    return std::max(0, col - 1);
}

#include "formats/CsvTsv.h"
#include "formats/ExcelXlsxOds.h"
#include "formats/WordDocxOdt.h"
#include "formats/Rtf.h"
#include "formats/PowerPointPptxOdp.h"
#include "formats/Epub.h"
#include "formats/Fb2.h"
#include "formats/Sqlite.h"
#include "formats/Dbf.h"
#include "formats/Dif.h"
#include "formats/Sylk.h"

} // namespace

#include "formats/Parquet.h"
#include "formats/Avro.h"
#include "formats/Bson.h"
#include "formats/Ics.h"
#include "formats/Vcf.h"
#include "formats/Srt.h"
#include "formats/Vtt.h"
#include "formats/Ass.h"
#include "formats/Gpx.h"
#include "formats/Kml.h"
#include "formats/GeoJson.h"
#include "formats/OpenStreetMap.h"
#include "formats/GarminTcx.h"
#include "formats/BibTex.h"
#include "formats/RisCitation.h"
#include "formats/WekaArff.h"
#include "formats/MatrixMarket.h"
#include "formats/NdJson.h"
#include "formats/HttpArchiveHar.h"
#include "formats/LibPcap.h"
#include "formats/BitTorrent.h"
#include "formats/CueSheet.h"
#include "formats/PlaylistM3u.h"
#include "formats/MidiFile.h"
#include "formats/AudioContainer.h"
#include "formats/BioMedicalEdf.h"
#include "formats/ElfBinary.h"
#include "formats/WebAssemblyWasm.h"
#include "formats/NmeaGpsLog.h"
#include "formats/IgcFlightLog.h"
#include "formats/GpsTrackMap.h"
#include "formats/ExtendedSysSec100.h"
#include "formats/ExtendedAiData100.h"
#include "formats/ExtendedGeoSci100.h"
#include "formats/ExtendedMediaDoc100.h"
#include "formats/ExtendedDocs200.h"
#include <shellapi.h>

bool SpreadsheetDocViewer::IsSpreadsheetOrDocExtension(const std::wstring& extLower) {
    return extLower == L".csv"     || extLower == L".tsv"     || extLower == L".psv"  ||
           extLower == L".xlsx"    || extLower == L".xlsm"    || extLower == L".xls"  ||
           extLower == L".ods"     || extLower == L".docx"    || extLower == L".docm" ||
           extLower == L".odt"     || extLower == L".rtf"     || extLower == L".pptx" ||
           extLower == L".ppsx"    || extLower == L".odp"     || extLower == L".epub" ||
           extLower == L".fb2"     || extLower == L".sqlite"  || extLower == L".sqlite3" ||
           extLower == L".db"      || extLower == L".dbf"     || extLower == L".dif"  ||
           extLower == L".slk"     || extLower == L".sylk"    || extLower == L".parquet" ||
           extLower == L".avro"    || extLower == L".bson"    || extLower == L".ics"  ||
           extLower == L".ical"    || extLower == L".vcf"     || extLower == L".vcard" ||
           extLower == L".srt"     || extLower == L".vtt"     || extLower == L".ass"  ||
           extLower == L".ssa"     || extLower == L".gpx"     || extLower == L".kml"  ||
           extLower == L".geojson" || extLower == L".osm"     || extLower == L".tcx"  ||
           extLower == L".bib"     || extLower == L".ris"     || extLower == L".arff" ||
           extLower == L".mtx"     || extLower == L".ndjson"  || extLower == L".jsonl" ||
           extLower == L".har"     || extLower == L".pcap"    || extLower == L".torrent" ||
           extLower == L".cue"     || extLower == L".m3u"     || extLower == L".m3u8" ||
           extLower == L".pls"     || extLower == L".mid"     || extLower == L".midi" ||
           extLower == L".wav"     || extLower == L".flac"    || extLower == L".aiff" ||
           extLower == L".edf"     || extLower == L".elf"     || extLower == L".wasm" ||
           extLower == L".nmea"    || extLower == L".igc"     ||
           extLower == L".exe"     || extLower == L".dll"     || extLower == L".sys"  || extLower == L".ocx"  ||
           extLower == L".lnk"     || extLower == L".pf"      || extLower == L".evtx" || extLower == L".dmp"  ||
           extLower == L".mdmp"    || extLower == L".class"   || extLower == L".dex"  || extLower == L".macho"||
           extLower == L".dylib"   || extLower == L".crt"     || extLower == L".cer"  || extLower == L".pem"  ||
           extLower == L".der"     || extLower == L".csr"     || extLower == L".p12"  || extLower == L".pfx"  ||
           extLower == L".kdbx"    || extLower == L".pcapng"  || extLower == L".uf2"  ||
           extLower == L".onnx"    || extLower == L".tflite"  || extLower == L".pt"   || extLower == L".pth"  ||
           extLower == L".ckpt"    || extLower == L".npy"     || extLower == L".npz"  || extLower == L".h5"   ||
           extLower == L".hdf5"    || extLower == L".mat"     || extLower == L".feather" || extLower == L".arrow" ||
           extLower == L".ipc"     || extLower == L".orc"     || extLower == L".duckdb"  || extLower == L".msgpack" ||
           extLower == L".mpk"     || extLower == L".cbor"    || extLower == L".ipynb"   || extLower == L".sav"  ||
           extLower == L".dta"     || extLower == L".sas7bdat"|| extLower == L".rds"  ||
           extLower == L".shp"     || extLower == L".shx"     || extLower == L".gpkg" || extLower == L".kmz"  ||
           extLower == L".cdf"     || extLower == L".grib"    || extLower == L".grib2"|| extLower == L".nii"  ||
           extLower == L".ogg"     || extLower == L".opus"    || extLower == L".spx"  || extLower == L".mp3"  ||
           extLower == L".mod"     || extLower == L".xm"      || extLower == L".it"   || extLower == L".s3m"  ||
           extLower == L".sf2"     || extLower == L".sfz"     || extLower == L".gp"   || extLower == L".gp5"  ||
           extLower == L".mscz"    || extLower == L".als"     || extLower == L".flp"  || extLower == L".rpp"  ||
           extLower == L".edl"     || extLower == L".fcpxml"  || extLower == L".otio" || extLower == L".sub"  ||
           extLower == L".idx"     || extLower == L".sup"     || extLower == L".eml"  || extLower == L".mbox" ||
           extLower == L".msg"     || extLower == L".mobi"    || extLower == L".azw3" || extLower == L".djvu" ||
           extLower == L".chm"     ||
           extLower == L".accdb"   || extLower == L".mdb"     || extLower == L".rdb"  || extLower == L".sst"  ||
           extLower == L".ldb"     || extLower == L".mbtiles" || extLower == L".pmtiles" || extLower == L".pb" ||
           extLower == L".pbf"     || extLower == L".tfstate" || extLower == L".sarif"|| extLower == L".spdx" ||
           extLower == L".cdx"     || extLower == L".gpg"     || extLower == L".pgp"  || extLower == L".sig"  ||
           extLower == L".jks"     || extLower == L".cat"     || extLower == L".hiv"  || extLower == L".etl"  ||
           extLower == L".pol"     || extLower == L".sid"     || extLower == L".nsf"  || extLower == L".spc"  ||
           extLower == L".vgm"     || extLower == L".musicxml"|| extLower == L".mxl"  || extLower == L".xmp"  ||
           extLower == L".uasset"  || extLower == L".umap"    || extLower == L".prefab"|| extLower == L".unity"||
           extLower == L".pages"   || extLower == L".numbers" || extLower == L".key"  || extLower == L".vsdx";
}

static bool InspectOfficeFileRaw(const std::wstring& fullPath, ParsedOfficeDocument& outDoc);

bool SpreadsheetDocViewer::InspectOfficeFile(const std::wstring& fullPath, ParsedOfficeDocument& outDoc) try {
    bool ok = InspectOfficeFileRaw(fullPath, outDoc);
    if (ok && outDoc.valid) {
        Formats::ExtractGeoTrackFromSheets(outDoc);
    }
    return ok;
} E2_READER_CATCH(L"Document reader", fullPath, { outDoc = ParsedOfficeDocument(); return false; })

static bool InspectOfficeFileRaw(const std::wstring& fullPath, ParsedOfficeDocument& outDoc) {
    outDoc = ParsedOfficeDocument{};
    outDoc.filePath = fullPath;
    std::wstring ext = PathUtils::GetExtension(fullPath);

    if (ext == L".xlsx" || ext == L".xlsm" || ext == L".ods") {
        return ParseExcelXlsx(fullPath, outDoc);
    }
    if (ext == L".docx" || ext == L".docm" || ext == L".odt") {
        return ParseWordDocx(fullPath, outDoc);
    }
    if (ext == L".pptx" || ext == L".ppsx" || ext == L".odp") {
        return ParsePowerPointPptx(fullPath, ext, outDoc);
    }
    if (ext == L".epub") {
        return ParseEpubDocument(fullPath, outDoc);
    }

    std::vector<uint8_t> raw;
    if (!PathUtils::ReadFileBytes(fullPath, 32 * 1024 * 1024, raw) || raw.empty()) {
        return false;
    }

    if (ext == L".parquet") return Formats::ParseApacheParquet(raw, outDoc);
    if (ext == L".avro")    return Formats::ParseApacheAvro(raw, outDoc);
    if (ext == L".bson")    return Formats::ParseBsonDocument(raw, outDoc);
    if (ext == L".ics" || ext == L".ical")   return Formats::ParseICalendar(raw, outDoc);
    if (ext == L".vcf" || ext == L".vcard")  return Formats::ParseVCard(raw, outDoc);
    if (ext == L".srt")     return Formats::ParseSubRipSrt(raw, outDoc);
    if (ext == L".vtt")     return Formats::ParseWebVtt(raw, outDoc);
    if (ext == L".ass" || ext == L".ssa")    return Formats::ParseSubStationAss(raw, outDoc);
    if (ext == L".gpx")     return Formats::ParseGpxTrack(raw, outDoc);
    if (ext == L".kml")     return Formats::ParseKmlGeography(raw, outDoc);
    if (ext == L".geojson") return Formats::ParseGeoJsonDoc(raw, outDoc);
    if (ext == L".osm")     return Formats::ParseOpenStreetMapDoc(raw, outDoc);
    if (ext == L".tcx")     return Formats::ParseGarminTcxDoc(raw, outDoc);
    if (ext == L".bib")     return Formats::ParseBibTexDoc(raw, outDoc);
    if (ext == L".ris")     return Formats::ParseRisCitationDoc(raw, outDoc);
    if (ext == L".arff")    return Formats::ParseWekaArffDoc(raw, outDoc);
    if (ext == L".mtx")     return Formats::ParseMatrixMarketDoc(raw, outDoc);
    if (ext == L".ndjson" || ext == L".jsonl") return Formats::ParseNdJsonDoc(raw, outDoc);
    if (ext == L".har")     return Formats::ParseHttpArchiveHarDoc(raw, outDoc);
    if (ext == L".pcap")    return Formats::ParseLibPcapDoc(raw, outDoc);
    if (ext == L".torrent") return Formats::ParseBitTorrentDoc(raw, outDoc);
    if (ext == L".cue")     return Formats::ParseCueSheetDoc(raw, outDoc);
    if (ext == L".m3u" || ext == L".m3u8" || ext == L".pls") return Formats::ParsePlaylistM3uDoc(raw, outDoc);
    if (ext == L".mid" || ext == L".midi") return Formats::ParseMidiFileDoc(raw, outDoc);
    if (ext == L".wav" || ext == L".flac" || ext == L".aiff") return Formats::ParseAudioContainerDoc(raw, outDoc);
    if (ext == L".edf")     return Formats::ParseBioMedicalEdfDoc(raw, outDoc);
    if (ext == L".elf")     return Formats::ParseElfBinaryDoc(raw, outDoc);
    if (ext == L".wasm")    return Formats::ParseWebAssemblyWasmDoc(raw, outDoc);
    if (ext == L".nmea")    return Formats::ParseNmeaGpsLogDoc(raw, outDoc);
    if (ext == L".igc")     return Formats::ParseIgcFlightLogDoc(raw, outDoc);
    if (Formats::ParseExtendedSysSecDoc(raw, ext, outDoc))  return true;
    if (Formats::ParseExtendedAiDataDoc(raw, ext, outDoc))  return true;
    if (Formats::ParseExtendedGeoSciDoc(raw, ext, outDoc))  return true;
    if (Formats::ParseExtendedMediaDoc(raw, ext, outDoc))   return true;
    if (Formats::ParseExtendedDocs200(raw, ext, outDoc))    return true;

    if (ext == L".fb2") {
        return ParseFb2Document(raw, outDoc);
    }
    if (ext == L".sqlite" || ext == L".sqlite3" || ext == L".db" ||
        (raw.size() >= 16 && std::memcmp(raw.data(), "SQLite format 3\0", 16) == 0)) {
        return ParseSqliteDatabase(raw, outDoc);
    }
    if (ext == L".dbf") {
        return ParseDbfDatabase(raw, outDoc);
    }
    if (ext == L".dif") {
        return ParseDifSpreadsheet(raw, outDoc);
    }
    if (ext == L".slk" || ext == L".sylk") {
        return ParseSylkSpreadsheet(raw, outDoc);
    }
    if (ext == L".rtf" || (raw.size() >= 5 && std::memcmp(raw.data(), "{\\rtf", 5) == 0)) {
        return ParseRtfDocument(raw, outDoc);
    }
    if (ext == L".csv" || ext == L".tsv" || ext == L".psv") {
        return ParseCsvOrTsv(raw, ext, outDoc);
    }

    // Legacy .xls (OLE2 BIFF8) string table extractor
    if (ext == L".xls") {
        outDoc.kind = OfficeFileKind::ExcelXls;
        outDoc.isSpreadsheet = true;
        outDoc.formatTitle = L"Microsoft Excel 97-2003 (.xls)";
        SpreadsheetSheet sheet;
        sheet.name = L"XLS";
        std::vector<std::wstring> curRow;
        std::string curAscii;
        for (size_t i = 0; i < raw.size(); ++i) {
            uint8_t b = raw[i];
            if (b >= 32 && b <= 126) {
                curAscii.push_back(static_cast<char>(b));
            } else {
                if (curAscii.size() >= 3) {
                    curRow.push_back(PathUtils::Utf8ToWide(curAscii));
                    if (curRow.size() >= 6) {
                        sheet.maxCols = 6;
                        sheet.rows.push_back(std::move(curRow));
                        curRow.clear();
                    }
                }
                curAscii.clear();
            }
        }
        if (!curRow.empty()) {
            sheet.maxCols = std::max(sheet.maxCols, curRow.size());
            sheet.rows.push_back(std::move(curRow));
        }
        if (sheet.rows.empty()) {
            sheet.rows.push_back({ L"(Binary XLS Workbook)" });
            sheet.maxCols = 1;
        }
        outDoc.sheets.push_back(std::move(sheet));
        outDoc.valid = true;
        return true;
    }

    return false;
}

void SpreadsheetDocViewer::Initialize(HWND parentHwnd) {
    m_parentHwnd = parentHwnd;
}

void SpreadsheetDocViewer::Shutdown() {
    Clear();
}

void SpreadsheetDocViewer::Clear() {
    m_currentPath.clear();
    m_doc = ParsedOfficeDocument{};
    m_activeSheetIdx = 0;
    m_selectedRow = 0;
    m_selectedCol = 0;
    m_sortedCol = -1;
    m_topRow = 0;
    m_leftCol = 0;
    m_docScrollY = 0.0f;
}

void SpreadsheetDocViewer::LoadFile(const std::wstring& fullPath, bool forceReload) {
    if (!forceReload && m_currentPath == fullPath && !m_currentPath.empty()) {
        return;
    }
    ParsedOfficeDocument doc;
    InspectOfficeFile(fullPath, doc);
    ShowLoadedDocument(fullPath, std::move(doc));
}

void SpreadsheetDocViewer::ShowLoadedDocument(const std::wstring& fullPath, ParsedOfficeDocument&& doc) {
    m_currentPath = fullPath;
    m_activeSheetIdx = 0;
    m_selectedRow = 0;
    m_selectedCol = 0;
    m_sortedCol = -1;
    m_topRow = 0;
    m_leftCol = 0;
    m_docScrollY = 0.0f;
    m_mapZoom = 1.0f;
    m_mapPanX = 0.0f;
    m_mapPanY = 0.0f;
    m_mapDragging = false;
    m_doc = std::move(doc);
    m_showMapView = m_doc.hasGeoTrack;
    RecomputeColumnStats(0);
}

void SpreadsheetDocViewer::RecomputeColumnStats(int colIdx) {
    m_statNumericCount = 0;
    m_statSum = 0.0;
    m_statAvg = 0.0;
    m_statMin = 0.0;
    m_statMax = 0.0;

    if (!m_doc.isSpreadsheet || m_activeSheetIdx < 0 || static_cast<size_t>(m_activeSheetIdx) >= m_doc.sheets.size()) {
        return;
    }
    const auto& rows = m_doc.sheets[m_activeSheetIdx].rows;
    size_t startRow = (rows.size() > 1) ? 1 : 0; // Skip header row if >1 row
    for (size_t r = startRow; r < rows.size(); ++r) {
        if (colIdx < 0 || static_cast<size_t>(colIdx) >= rows[r].size()) continue;
        const std::wstring& cell = rows[r][colIdx];
        if (cell.empty()) continue;
        wchar_t* endPtr = nullptr;
        double v = std::wcstod(cell.c_str(), &endPtr);
        if (endPtr != cell.c_str() && std::isfinite(v)) {
            if (m_statNumericCount == 0) {
                m_statMin = v;
                m_statMax = v;
            } else {
                m_statMin = std::min(m_statMin, v);
                m_statMax = std::max(m_statMax, v);
            }
            m_statSum += v;
            ++m_statNumericCount;
        }
    }
    if (m_statNumericCount > 0) {
        m_statAvg = m_statSum / static_cast<double>(m_statNumericCount);
    }
}

void SpreadsheetDocViewer::SortActiveSheetByColumn(int colIdx) {
    if (!m_doc.isSpreadsheet || m_activeSheetIdx < 0 || static_cast<size_t>(m_activeSheetIdx) >= m_doc.sheets.size()) {
        return;
    }
    auto& rows = m_doc.sheets[m_activeSheetIdx].rows;
    if (rows.size() <= 2) return;

    if (m_sortedCol == colIdx) {
        m_sortAscending = !m_sortAscending;
    } else {
        m_sortedCol = colIdx;
        m_sortAscending = true;
    }

    bool asc = m_sortAscending;
    // Keep row 0 as frozen header row, sort rows[1..end]
    std::sort(rows.begin() + 1, rows.end(), [colIdx, asc](const std::vector<std::wstring>& a, const std::vector<std::wstring>& b) {
        std::wstring_view sa = (colIdx >= 0 && static_cast<size_t>(colIdx) < a.size()) ? std::wstring_view(a[colIdx]) : std::wstring_view{};
        std::wstring_view sb = (colIdx >= 0 && static_cast<size_t>(colIdx) < b.size()) ? std::wstring_view(b[colIdx]) : std::wstring_view{};
        wchar_t* endA = nullptr;
        wchar_t* endB = nullptr;
        double da = sa.empty() ? 0.0 : std::wcstod(sa.data(), &endA);
        double db = sb.empty() ? 0.0 : std::wcstod(sb.data(), &endB);
        if (!sa.empty() && !sb.empty() && endA != sa.data() && endB != sb.data()) {
            return asc ? (da < db) : (da > db);
        }
        int cmp = CompareStringOrdinal(sa.data(), static_cast<int>(sa.size()), sb.data(), static_cast<int>(sb.size()), TRUE);
        return asc ? (cmp == CSTR_LESS_THAN) : (cmp == CSTR_GREATER_THAN);
    });
}

void SpreadsheetDocViewer::Draw(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtRegular,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& rect,
    Lang lang,
    D2D1_POINT_2F mousePt)
{
    const bool pl = (lang == Lang::PL);
    m_toolbarHits.clear();

    float tbH = 30.0f;
    float statusH = 26.0f;
    D2D1_RECT_F tbR = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + tbH);
    brush->SetColor(SdvColor(0xF8FAFC));
    rt->FillRectangle(tbR, brush);
    brush->SetColor(SdvColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(tbR.left, tbR.bottom - 0.5f), D2D1::Point2F(tbR.right, tbR.bottom - 0.5f), brush, 1.0f);

    float bx = tbR.left + 6.0f;
    auto addBtn = [&](int id, float w, const wchar_t* label, bool active = false) {
        if (bx + w > tbR.right - 4.0f) return;
        D2D1_RECT_F br = D2D1::RectF(bx, tbR.top + 4.0f, bx + w, tbR.bottom - 4.0f);
        m_toolbarHits.push_back({ id, br });
        PathUtils::DrawToolbarButton(rt, brush, fmtSmall, br, label, active, m_hoverBtnId == id);
        bx += w + 4.0f;
    };

    if (m_doc.isSpreadsheet) {
        if (m_doc.hasGeoTrack) {
            addBtn(BTN_TOGGLE_MAP_VIEW, pl ? 104.0f : 98.0f,
                m_showMapView ? (pl ? L"\xD83D\xDDFA Mapa GPS" : L"\xD83D\xDDFA GPS Map")
                              : (pl ? L"\xD83D\xDCCA Tabela" : L"\xD83D\xDCCA Table"),
                m_showMapView);
            addBtn(BTN_OPEN_OSM_WEB, pl ? 124.0f : 118.0f, pl ? L"\xD83C\xDF0D OpenStreetMap" : L"\xD83C\xDF0D OpenStreetMap");
            if (m_showMapView) {
                addBtn(BTN_RESET_MAP_CAM, pl ? 72.0f : 68.0f, pl ? L"\xD83C\xDFAF Reset" : L"\xD83C\xDFAF Fit");
            }
        }
        for (size_t s = 0; s < std::min<size_t>(m_doc.sheets.size(), 5); ++s) {
            std::wstring sName = L"\xD83D\xDCCA " + m_doc.sheets[s].name;
            float sw = std::clamp(static_cast<float>(sName.size()) * 7.5f + 16.0f, 64.0f, 120.0f);
            addBtn(BTN_SHEET_BASE + static_cast<int>(s), sw, sName.c_str(), !m_showMapView && static_cast<int>(s) == m_activeSheetIdx);
        }
        addBtn(BTN_SORT_ASC_DESC, pl ? 78.0f : 72.0f, pl ? L"\x21C5 Sortuj kol." : L"\x21C5 Sort Col", m_sortedCol >= 0);
        addBtn(BTN_COPY_CELL,     pl ? 82.0f : 76.0f, pl ? L"Kopiuj kom." : L"Copy Cell");
        addBtn(BTN_COPY_ALL,      pl ? 76.0f : 72.0f, pl ? L"Kopiuj CSV" : L"Copy CSV");
    } else {
        addBtn(BTN_COPY_ALL, pl ? 110.0f : 100.0f, pl ? L"\xD83D\xDCCB Kopiuj tekst" : L"\xD83D\xDCCB Copy Text");
    }

    D2D1_RECT_F bodyR = D2D1::RectF(rect.left, tbR.bottom, rect.right, std::max(tbR.bottom + 20.0f, rect.bottom - statusH));

    if (m_doc.isSpreadsheet && m_doc.hasGeoTrack && m_showMapView) {
        m_gridBodyRect = bodyR;
        Formats::DrawGpsMapAndElevationView(rt, brush, fmtRegular, fmtSmall, fmtBold,
            bodyR, m_doc, m_mapZoom, m_mapPanX, m_mapPanY, m_selectedRow, mousePt, pl);
    } else if (m_doc.isSpreadsheet && !m_doc.sheets.empty()) {
        const auto& sheet = m_doc.sheets[std::clamp(m_activeSheetIdx, 0, static_cast<int>(m_doc.sheets.size()) - 1)];

        // Formula / Active Cell Inspector Strip (24px)
        float fxH = 24.0f;
        D2D1_RECT_F fxR = D2D1::RectF(bodyR.left, bodyR.top, bodyR.right, bodyR.top + fxH);
        brush->SetColor(SdvColor(0xEFF6FF));
        rt->FillRectangle(fxR, brush);
        brush->SetColor(SdvColor(0xDBEAFE));
        rt->DrawLine(D2D1::Point2F(fxR.left, fxR.bottom - 0.5f), D2D1::Point2F(fxR.right, fxR.bottom - 0.5f), brush, 1.0f);

        std::wstring cellRef = ColIndexToExcelLetters(m_selectedCol) + std::to_wstring(m_selectedRow + 1);
        std::wstring cellVal;
        if (m_selectedRow >= 0 && static_cast<size_t>(m_selectedRow) < sheet.rows.size() &&
            m_selectedCol >= 0 && static_cast<size_t>(m_selectedCol) < sheet.rows[m_selectedRow].size()) {
            cellVal = sheet.rows[m_selectedRow][m_selectedCol];
        }
        std::wstring fxText = L"[" + cellRef + L"]   fx = " + cellVal;
        brush->SetColor(SdvColor(0x1E3A8A));
        rt->DrawText(fxText.c_str(), static_cast<UINT32>(fxText.size()), fmtSmall,
            D2D1::RectF(fxR.left + 8.0f, fxR.top, fxR.right - 8.0f, fxR.bottom), brush);

        // Column Letters Header (A, B, C...) + Grid
        float colHdrH = 22.0f;
        float rowNumW = 46.0f;
        float cellW = 108.0f;
        float rowH = 21.0f;

        m_colHeaderRect = D2D1::RectF(bodyR.left + rowNumW, fxR.bottom, bodyR.right, fxR.bottom + colHdrH);
        m_gridBodyRect = D2D1::RectF(bodyR.left, m_colHeaderRect.bottom, bodyR.right, bodyR.bottom);

        brush->SetColor(SdvColor(0xF1F5F9));
        rt->FillRectangle(D2D1::RectF(bodyR.left, fxR.bottom, bodyR.right, m_colHeaderRect.bottom), brush);
        brush->SetColor(SdvColor(0xCBD5E1));
        rt->DrawLine(D2D1::Point2F(bodyR.left, m_colHeaderRect.bottom - 0.5f), D2D1::Point2F(bodyR.right, m_colHeaderRect.bottom - 0.5f), brush, 1.0f);

        int visCols = std::max(1, static_cast<int>((m_colHeaderRect.right - m_colHeaderRect.left) / cellW) + 1);
        int visRows = std::max(1, static_cast<int>((m_gridBodyRect.bottom - m_gridBodyRect.top) / rowH));

        rt->PushAxisAlignedClip(D2D1::RectF(bodyR.left, fxR.bottom, bodyR.right, bodyR.bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        // Draw Column Letters
        fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        for (int c = 0; c < visCols; ++c) {
            int colIdx = m_leftCol + c;
            if (colIdx >= static_cast<int>( std::max<size_t>(sheet.maxCols, 8) )) break;
            float cx1 = m_colHeaderRect.left + c * cellW;
            D2D1_RECT_F chR = D2D1::RectF(cx1, m_colHeaderRect.top, cx1 + cellW, m_colHeaderRect.bottom);
            if (colIdx == m_selectedCol) {
                brush->SetColor(SdvColor(0xDBEAFE));
                rt->FillRectangle(chR, brush);
            }
            std::wstring colLbl = ColIndexToExcelLetters(colIdx);
            if (colIdx == m_sortedCol) colLbl += (m_sortAscending ? L" \x25B2" : L" \x25BC");
            brush->SetColor(SdvColor(0x334155));
            rt->DrawText(colLbl.c_str(), static_cast<UINT32>(colLbl.size()), fmtBold, chR, brush);
            brush->SetColor(SdvColor(0xCBD5E1));
            rt->DrawLine(D2D1::Point2F(chR.right, chR.top), D2D1::Point2F(chR.right, m_gridBodyRect.bottom), brush, 0.8f);
        }
        fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        // Draw Rows
        for (int r = 0; r <= visRows; ++r) {
            int rowIdx = (r == 0) ? 0 : (m_topRow + r); // Freeze Row 0 at top!
            if (r > 0 && m_topRow == 0) rowIdx = r;
            if (rowIdx >= static_cast<int>(sheet.rows.size())) break;

            float ry = m_gridBodyRect.top + r * rowH;
            D2D1_RECT_F rowNumR = D2D1::RectF(bodyR.left, ry, bodyR.left + rowNumW, ry + rowH);
            brush->SetColor(rowIdx == 0 ? SdvColor(0xE2E8F0) : SdvColor(0xF8FAFC));
            rt->FillRectangle(rowNumR, brush);

            wchar_t rnBuf[24] = {};
            swprintf_s(rnBuf, L"%d", rowIdx + 1);
            fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            brush->SetColor(SdvColor(0x64748B));
            rt->DrawText(rnBuf, static_cast<UINT32>(wcslen(rnBuf)), fmtSmall, rowNumR, brush);
            fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

            const auto& rowVec = sheet.rows[rowIdx];
            for (int c = 0; c < visCols; ++c) {
                int colIdx = m_leftCol + c;
                if (colIdx >= static_cast<int>(std::max<size_t>(sheet.maxCols, 8))) break;
                float cx1 = m_colHeaderRect.left + c * cellW;
                D2D1_RECT_F cellR = D2D1::RectF(cx1, ry, cx1 + cellW, ry + rowH);

                if (rowIdx == m_selectedRow && colIdx == m_selectedCol) {
                    brush->SetColor(SdvColor(0xDBEAFE));
                    rt->FillRectangle(cellR, brush);
                    brush->SetColor(SdvColor(0x0078D7));
                    rt->DrawRectangle(D2D1::RectF(cellR.left + 1.0f, cellR.top + 1.0f, cellR.right - 1.0f, cellR.bottom - 1.0f), brush, 1.6f);
                } else if (rowIdx == 0) {
                    brush->SetColor(SdvColor(0xF8FAFC));
                    rt->FillRectangle(cellR, brush);
                } else if (rowIdx % 2 == 1) {
                    brush->SetColor(SdvColor(0xFCFDFF));
                    rt->FillRectangle(cellR, brush);
                }

                if (static_cast<size_t>(colIdx) < rowVec.size() && !rowVec[colIdx].empty()) {
                    brush->SetColor(rowIdx == 0 ? SdvColor(0x0F172A) : SdvColor(0x1E293B));
                    IDWriteTextFormat* tf = (rowIdx == 0) ? fmtBold : fmtSmall;
                    rt->DrawText(rowVec[colIdx].c_str(), static_cast<UINT32>(rowVec[colIdx].size()), tf,
                        D2D1::RectF(cellR.left + 5.0f, cellR.top, cellR.right - 4.0f, cellR.bottom), brush);
                }
            }

            brush->SetColor(rowIdx == 0 ? SdvColor(0x94A3B8) : SdvColor(0xE2E8F0));
            rt->DrawLine(D2D1::Point2F(bodyR.left, ry + rowH), D2D1::Point2F(bodyR.right, ry + rowH), brush, rowIdx == 0 ? 1.2f : 0.7f);
        }

        rt->PopAxisAlignedClip();
    } else {
        // Rich Document View (.docx, .odt, .rtf)
        m_gridBodyRect = bodyR;
        brush->SetColor(SdvColor(0xF1F5F9));
        rt->FillRectangle(bodyR, brush);

        D2D1_RECT_F pageR = D2D1::RectF(bodyR.left + 12.0f, bodyR.top + 10.0f, bodyR.right - 12.0f, bodyR.bottom - 10.0f);
        brush->SetColor(SdvColor(0xFFFFFF));
        rt->FillRoundedRectangle(D2D1::RoundedRect(pageR, 4.0f, 4.0f), brush);
        brush->SetColor(SdvColor(0xCBD5E1));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(pageR, 4.0f, 4.0f), brush, 1.0f);

        rt->PushAxisAlignedClip(pageR, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float y = pageR.top + 14.0f - m_docScrollY;
        float textW = std::max(80.0f, pageR.right - pageR.left - 32.0f);
        int charsPerLine = std::max(20, static_cast<int>(textW / 6.8f));

        for (const auto& para : m_doc.paragraphs) {
            int lines = std::max(1, static_cast<int>((para.text.size() + charsPerLine - 1) / charsPerLine));
            float blockH = lines * 20.0f + (para.headingLevel > 0 ? 10.0f : 6.0f);
            if (y + blockH >= pageR.top && y <= pageR.bottom) {
                float xLeft = pageR.left + 16.0f + (para.isBullet ? 14.0f : 0.0f);
                if (para.isBullet) {
                    brush->SetColor(SdvColor(0x0078D7));
                    rt->DrawText(L"\x2022", 1, fmtBold, D2D1::RectF(pageR.left + 16.0f, y, xLeft, y + 20.0f), brush);
                }
                IDWriteTextFormat* tf = (para.headingLevel > 0) ? fmtBold : fmtRegular;
                brush->SetColor(para.headingLevel == 1 ? SdvColor(0x1E3A8A) : SdvColor(0x0F172A));
                DWRITE_WORD_WRAPPING prevWrap = tf->GetWordWrapping();
                tf->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                rt->DrawText(para.text.c_str(), static_cast<UINT32>(para.text.size()), tf,
                    D2D1::RectF(xLeft, y, pageR.right - 16.0f, y + blockH), brush);
                tf->SetWordWrapping(prevWrap);
            }
            y += blockH;
        }
        rt->PopAxisAlignedClip();
    }

    // Bottom Status Strip (Rows x Cols + Live Numeric Column Stats: Sum, Avg, Min, Max, or Word/Char Count)
    D2D1_RECT_F stR = D2D1::RectF(rect.left, bodyR.bottom, rect.right, rect.bottom);
    brush->SetColor(SdvColor(0xF1F5F9));
    rt->FillRectangle(stR, brush);
    brush->SetColor(SdvColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(stR.left, stR.top), D2D1::Point2F(stR.right, stR.top), brush, 1.0f);

    wchar_t stBuf[320] = {};
    if (m_doc.isSpreadsheet && !m_doc.sheets.empty()) {
        const auto& sh = m_doc.sheets[std::clamp(m_activeSheetIdx, 0, static_cast<int>(m_doc.sheets.size()) - 1)];
        if (m_statNumericCount > 0) {
            swprintf_s(stBuf,
                pl ? L"%s (%zu wierszy \xD7 %zu kol.)   \x2022   Kol %s: Suma = %.2f  |  Śr = %.2f  |  Min = %.2f  |  Max = %.2f"
                   : L"%s (%zu rows \xD7 %zu cols)   \x2022   Col %s: Sum = %.2f  |  Avg = %.2f  |  Min = %.2f  |  Max = %.2f",
                m_doc.formatTitle.c_str(), sh.rows.size(), sh.maxCols,
                ColIndexToExcelLetters(m_selectedCol).c_str(),
                m_statSum, m_statAvg, m_statMin, m_statMax);
        } else {
            swprintf_s(stBuf,
                pl ? L"%s   \x2022   Wierszy: %zu   |   Kolumn: %zu   |   Arkuszy: %zu"
                   : L"%s   \x2022   Rows: %zu   |   Columns: %zu   |   Sheets: %zu",
                m_doc.formatTitle.c_str(), sh.rows.size(), sh.maxCols, m_doc.sheets.size());
        }
    } else {
        swprintf_s(stBuf,
            pl ? L"%s   \x2022   Akapitów: %zu   |   Słów: %zu   |   Znaków: %zu"
               : L"%s   \x2022   Paragraphs: %zu   |   Words: %zu   |   Characters: %zu",
            m_doc.formatTitle.c_str(), m_doc.paragraphs.size(), m_doc.wordCount, m_doc.charCount);
    }
    brush->SetColor(SdvColor(0x334155));
    rt->DrawText(stBuf, static_cast<UINT32>(wcslen(stBuf)), fmtSmall,
        D2D1::RectF(stR.left + 8.0f, stR.top, stR.right - 8.0f, stR.bottom), brush);
}

bool SpreadsheetDocViewer::OnMouseDown(float x, float y, bool rightButton, Lang lang, std::wstring* outStatusToast) {
    if (rightButton) return false;
    const bool pl = (lang == Lang::PL);

    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            if (hit.id == BTN_TOGGLE_MAP_VIEW) {
                m_showMapView = !m_showMapView;
                return true;
            }
            if (hit.id == BTN_RESET_MAP_CAM) {
                m_mapZoom = 1.0f;
                m_mapPanX = 0.0f;
                m_mapPanY = 0.0f;
                return true;
            }
            if (hit.id == BTN_OPEN_OSM_WEB && !m_doc.geoPoints.empty()) {
                double cLat = m_doc.geoPoints[m_doc.geoPoints.size() / 2].lat;
                double cLon = m_doc.geoPoints[m_doc.geoPoints.size() / 2].lon;
                wchar_t url[256] = {};
                swprintf_s(url, L"https://www.openstreetmap.org/?mlat=%.6f&mlon=%.6f#map=14/%.6f/%.6f", cLat, cLon, cLat, cLon);
                ShellExecuteW(m_parentHwnd, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
                if (outStatusToast) *outStatusToast = pl ? L"\xD83C\xDF0D Otwarto trasę w OpenStreetMap" : L"\xD83C\xDF0D Opened track in OpenStreetMap";
                return true;
            }
            if (hit.id >= BTN_SHEET_BASE && hit.id < BTN_SHEET_BASE + 32) {
                m_showMapView = false;
                m_activeSheetIdx = hit.id - BTN_SHEET_BASE;
                m_topRow = 0;
                m_leftCol = 0;
                m_selectedRow = 0;
                m_selectedCol = 0;
                RecomputeColumnStats(0);
                return true;
            }
            if (hit.id == BTN_SORT_ASC_DESC) {
                SortActiveSheetByColumn(m_selectedCol);
                return true;
            }
            if (hit.id == BTN_COPY_CELL && m_doc.isSpreadsheet && !m_doc.sheets.empty()) {
                const auto& sh = m_doc.sheets[m_activeSheetIdx];
                if (m_selectedRow >= 0 && static_cast<size_t>(m_selectedRow) < sh.rows.size() &&
                    m_selectedCol >= 0 && static_cast<size_t>(m_selectedCol) < sh.rows[m_selectedRow].size()) {
                    PathUtils::CopyWideToClipboard(m_parentHwnd, sh.rows[m_selectedRow][m_selectedCol]);
                    if (outStatusToast) *outStatusToast = pl ? L"\x2713 Skopiowano komórkę do schowka" : L"\x2713 Copied cell to clipboard";
                }
                return true;
            }
            if (hit.id == BTN_COPY_ALL) {
                std::wstring out;
                if (m_doc.isSpreadsheet && !m_doc.sheets.empty()) {
                    const auto& sh = m_doc.sheets[m_activeSheetIdx];
                    for (const auto& r : sh.rows) {
                        for (size_t c = 0; c < r.size(); ++c) {
                            if (c > 0) out += L"\t";
                            out += r[c];
                        }
                        out += L"\r\n";
                    }
                } else {
                    for (const auto& p : m_doc.paragraphs) {
                        out += p.text + L"\r\n\r\n";
                    }
                }
                PathUtils::CopyWideToClipboard(m_parentHwnd, out);
                if (outStatusToast) *outStatusToast = pl ? L"\x2713 Skopiowano zawartość do schowka" : L"\x2713 Copied content to clipboard";
                return true;
            }
        }
    }

    if (m_doc.isSpreadsheet && m_doc.hasGeoTrack && m_showMapView && PathUtils::PtInRectF(x, y, m_gridBodyRect)) {
        m_mapDragging = true;
        m_mapDragStartX = x - m_mapPanX;
        m_mapDragStartY = y - m_mapPanY;
        return true;
    }

    if (m_doc.isSpreadsheet && !m_doc.sheets.empty()) {
        if (PathUtils::PtInRectF(x, y, m_colHeaderRect)) {
            int c = static_cast<int>((x - m_colHeaderRect.left) / 108.0f);
            int colIdx = m_leftCol + std::max(0, c);
            m_selectedCol = colIdx;
            SortActiveSheetByColumn(colIdx);
            RecomputeColumnStats(colIdx);
            return true;
        }
        if (PathUtils::PtInRectF(x, y, m_gridBodyRect)) {
            int r = static_cast<int>((y - m_gridBodyRect.top) / 21.0f);
            int c = static_cast<int>((x - (m_gridBodyRect.left + 46.0f)) / 108.0f);
            const auto& sh = m_doc.sheets[m_activeSheetIdx];
            int rowIdx = (r == 0) ? 0 : (m_topRow + r);
            if (rowIdx >= 0 && static_cast<size_t>(rowIdx) < sh.rows.size()) {
                m_selectedRow = rowIdx;
            }
            if (c >= 0) {
                m_selectedCol = m_leftCol + c;
                RecomputeColumnStats(m_selectedCol);
            }
            return true;
        }
    }
    return false;
}

bool SpreadsheetDocViewer::OnMouseMove(float x, float y) {
    if (m_mapDragging) {
        m_mapPanX = x - m_mapDragStartX;
        m_mapPanY = y - m_mapDragStartY;
        return true;
    }
    int newHov = 0;
    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            newHov = hit.id;
            break;
        }
    }
    if (newHov != m_hoverBtnId) {
        m_hoverBtnId = newHov;
        return true;
    }
    return false;
}

bool SpreadsheetDocViewer::OnMouseUp() {
    if (m_mapDragging) {
        m_mapDragging = false;
        return true;
    }
    return false;
}

bool SpreadsheetDocViewer::OnMouseWheel(short delta, float x, float y) {
    if (!PathUtils::PtInRectF(x, y, m_gridBodyRect)) return false;
    int notches = delta / WHEEL_DELTA;
    if (notches == 0) notches = (delta > 0) ? 1 : -1;

    if (m_doc.isSpreadsheet && m_doc.hasGeoTrack && m_showMapView) {
        float factor = (notches > 0) ? 1.22f : (1.0f / 1.22f);
        m_mapZoom = std::clamp(m_mapZoom * factor, 0.25f, 30.0f);
        return true;
    }
    if (m_doc.isSpreadsheet && !m_doc.sheets.empty()) {
        const auto& sh = m_doc.sheets[m_activeSheetIdx];
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
            int maxLeft = std::max(0, static_cast<int>(sh.maxCols) - 2);
            m_leftCol = std::clamp(m_leftCol - notches, 0, maxLeft);
        } else {
            int maxTop = std::max(0, static_cast<int>(sh.rows.size()) - 4);
            m_topRow = std::clamp(m_topRow - notches * 3, 0, maxTop);
        }
        return true;
    } else {
        m_docScrollY = std::max(0.0f, m_docScrollY - notches * 48.0f);
        return true;
    }
}

} // namespace Explorer2
