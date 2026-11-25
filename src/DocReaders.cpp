// DocReaders.cpp - the document readers behind SpreadsheetDocViewer: src/formats/*.h, dispatched by extension.
// Moved out of SpreadsheetDocViewer.cpp so that they build without the viewer, also on Linux (see linux/).
// Binary/security, scientific/AI, and audio/publishing readers compile in dedicated DocReaders*.cpp units.
#include "DocReaders.h"
#include "FormatSupport.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "Ole2Compound.h"
#include "formats/common/AiModelDispatch.h"
#ifdef _WIN32
#include <wincrypt.h>
#endif
#include <cstdio>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <array>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace Explorer2 {

namespace {

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

#include "formats/docs/Rtf.h"
#include "formats/docs/Epub.h"
#include "formats/docs/Fb2.h"
#include "formats/data/Sqlite.h"
#include "formats/data/Dbf.h"
#include "formats/data/Dif.h"
#include "formats/data/Sylk.h"

} // namespace

#include "formats/docs/Ics.h"
#include "formats/docs/Vcf.h"
#include "formats/docs/Srt.h"
#include "formats/docs/Vtt.h"
#include "formats/docs/Ass.h"
#include "formats/gis/Gpx.h"
#include "formats/gis/Kml.h"
#include "formats/gis/GeoJson.h"
#include "formats/gis/OpenStreetMap.h"
#include "formats/gis/GarminTcx.h"
#include "formats/docs/BibTex.h"
#include "formats/docs/RisCitation.h"
#include "formats/gis/NmeaGpsLog.h"
#include "formats/gis/IgcFlightLog.h"
#include "formats/common/GpsTrackMap.h"
#include "formats/common/DocFormatCommon.h"
#include "formats/data/CsvDoc.h"
#include "formats/data/TsvDoc.h"
#include "formats/docs/ExcelXlsx.h"
#include "formats/docs/OpenDocumentOds.h"
#include "formats/docs/WordDocx.h"
#include "formats/docs/OpenDocumentOdt.h"
#include "formats/docs/PowerPointPptx.h"
#include "formats/docs/OpenDocumentOdp.h"
#include "formats/docs/WordLegacyDoc.h"
#include "formats/docs/ExcelLegacyXls.h"
#include "formats/docs/PowerPointLegacyPpt.h"
#include "formats/gis/EsriShapefileDoc.h"
#include "formats/gis/GeoPackageGpkg.h"
#include "formats/gis/KmzArchiveDoc.h"
#include "formats/gis/MbTilesMap.h"
#include "formats/docs/OutlookMsg.h"
#include "formats/docs/ExcelXlsb.h"
#include "formats/docs/OutlookPst.h"
#include "formats/docs/AdobeIdml.h"
#include "formats/docs/MimeHtmlMht.h"
#include "formats/docs/ApplePlist.h"
#include "formats/docs/ValveMaterialVmt.h"

namespace DocReaders {

bool IsSpreadsheetOrDocExtension(const std::wstring& extLower) {
    return extLower == L".csv"     || extLower == L".tsv"     || extLower == L".psv"  ||
           extLower == L".xlsx"    || extLower == L".xlsm"    || extLower == L".xltx" || extLower == L".xlsb" || extLower == L".xls"  ||
           extLower == L".ods"     || extLower == L".docx"    || extLower == L".docm" || extLower == L".dotx" || extLower == L".dotm" ||
           extLower == L".doc"     || extLower == L".dot"     || extLower == L".ppt"  || extLower == L".pps"  ||
           extLower == L".odt"     || extLower == L".ott"     || extLower == L".rtf"  || extLower == L".pptx" ||
           extLower == L".ppsx"    || extLower == L".pptm"    || extLower == L".ppsm" ||
           extLower == L".potx"    || extLower == L".potm"    || extLower == L".odp"  ||
           extLower == L".otp"     || extLower == L".epub"    || extLower == L".idml" ||
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
           extLower == L".wav"     || extLower == L".flac"    || extLower == L".aiff" || extLower == L".aif"  || extLower == L".aifc" ||
           extLower == L".edf"     || extLower == L".elf"     || extLower == L".o"    || extLower == L".so"   || extLower == L".wasm" ||
           extLower == L".nmea"    || extLower == L".igc"     ||
           extLower == L".exe"     || extLower == L".dll"     || extLower == L".sys"  || extLower == L".ocx"  || extLower == L".efi"  || extLower == L".drv"  ||
           extLower == L".lnk"     || extLower == L".pf"      || extLower == L".evtx" || extLower == L".dmp"  ||
           extLower == L".mdmp"    || extLower == L".class"   || extLower == L".dex"  || extLower == L".macho"||
           extLower == L".dylib"   || extLower == L".crt"     || extLower == L".cer"  || extLower == L".pem"  ||
           extLower == L".der"     || extLower == L".csr"     || extLower == L".p7b"  || extLower == L".p7c"  ||
           extLower == L".p12"     || extLower == L".pfx"     ||
           extLower == L".kdbx"    || extLower == L".pcapng"  || extLower == L".uf2"  ||
           extLower == L".onnx"    || extLower == L".gguf"    || extLower == L".safetensors" ||
           extLower == L".tflite"  || extLower == L".pt"      || extLower == L".pth"  ||
           extLower == L".ckpt"    || extLower == L".npy"     || extLower == L".npz"  || extLower == L".h5"   ||
           extLower == L".hdf5"    || extLower == L".mat"     || extLower == L".feather" || extLower == L".arrow" ||
           extLower == L".ipc"     || extLower == L".orc"     || extLower == L".duckdb"  || extLower == L".msgpack" ||
           extLower == L".mpk"     || extLower == L".cbor"    || extLower == L".ipynb"   || extLower == L".sav"  || extLower == L".zsav" ||
           extLower == L".dta"     || extLower == L".sas7bdat"|| extLower == L".rds"  ||
           extLower == L".shp"     || extLower == L".shx"     || extLower == L".gpkg" || extLower == L".kmz"  ||
           extLower == L".nc"      || extLower == L".cdf"     || extLower == L".grib" || extLower == L".grib2"|| extLower == L".grb"  || extLower == L".nii"  ||
           extLower == L".ogg"     || extLower == L".opus"    || extLower == L".spx"  || extLower == L".mp3"  ||
           extLower == L".mod"     || extLower == L".xm"      || extLower == L".it"   || extLower == L".s3m"  ||
           extLower == L".sf2"     || extLower == L".sfz"     || extLower == L".gp"   || extLower == L".gp3"  || extLower == L".gp4" || extLower == L".gp5"  ||
           extLower == L".mscz"    || extLower == L".als"     || extLower == L".flp"  || extLower == L".rpp"  ||
           extLower == L".edl"     || extLower == L".fcpxml"  || extLower == L".otio" || extLower == L".sub"  ||
           extLower == L".idx"     || extLower == L".sup"     || extLower == L".eml"  || extLower == L".mbox" ||
           extLower == L".msg"     || extLower == L".pst"     || extLower == L".ost"  || extLower == L".mht"  || extLower == L".mhtml" ||
           extLower == L".mobi"    || extLower == L".azw"     || extLower == L".azw3" || extLower == L".prc"  ||
           extLower == L".djvu"    || extLower == L".djv"     ||
           extLower == L".chm"     || extLower == L".plist"   ||
           extLower == L".accdb"   || extLower == L".mdb"     || extLower == L".rdb"  || extLower == L".sst"  ||
           extLower == L".ldb"     || extLower == L".mbtiles" || extLower == L".pmtiles" || extLower == L".pb" ||
           extLower == L".pbf"     || extLower == L".tfstate" || extLower == L".sarif"|| extLower == L".spdx" ||
           extLower == L".cdx"     || extLower == L".gpg"     || extLower == L".pgp"  || extLower == L".sig"  ||
           extLower == L".jks"     || extLower == L".cat"     || extLower == L".hiv"  || extLower == L".etl"  ||
           extLower == L".pol"     || extLower == L".sid"     || extLower == L".nsf"  || extLower == L".nsfe" || extLower == L".spc"  ||
           extLower == L".vgm"     || extLower == L".vgz"     || extLower == L".musicxml"|| extLower == L".mxl"  || extLower == L".xmp"  ||
           extLower == L".uasset"  || extLower == L".umap"    || extLower == L".prefab"|| extLower == L".unity"||
           extLower == L".pages"   || extLower == L".numbers" || extLower == L".key"  || extLower == L".iwa"  || extLower == L".vsdx" ||
           extLower == L".vmt"     || IsCatalogOrRpgDocExtension(extLower);
}

constexpr size_t kDocReadLimit = 32 * 1024 * 1024;

static std::vector<uint8_t> ReadFileTail(const std::wstring& path, uint64_t fileSize, size_t want) {
    std::vector<uint8_t> tail;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return tail;
    const uint64_t start = (fileSize > want) ? fileSize - want : 0;
    LARGE_INTEGER at;
    at.QuadPart = static_cast<LONGLONG>(start);
    if (SetFilePointerEx(h, at, nullptr, FILE_BEGIN)) {
        tail.resize(static_cast<size_t>(fileSize - start));
        DWORD got = 0;
        if (!ReadFile(h, tail.data(), static_cast<DWORD>(tail.size()), &got, nullptr)) got = 0;
        tail.resize(got);
    }
    CloseHandle(h);
    return tail;
}

static bool InspectOfficeFileRaw(const std::wstring& fullPath, ParsedOfficeDocument& outDoc, uint64_t& bytesNotRead);

bool InspectOfficeFile(const std::wstring& fullPath, ParsedOfficeDocument& outDoc) try {
    uint64_t bytesNotRead = 0;
    bool ok = InspectOfficeFileRaw(fullPath, outDoc, bytesNotRead);
    if (ok && outDoc.valid && bytesNotRead > 0 && !outDoc.sourceReadFully) {
        outDoc.formatTitle += L" — only the first " + PathUtils::FormatByteSize(kDocReadLimit) + L" of " +
                              PathUtils::FormatByteSize(kDocReadLimit + bytesNotRead) + L" read";
    }
    if (ok && outDoc.valid) {
        if (outDoc.geoPoints.empty()) {
            Formats::ExtractGeoTrackFromSheets(outDoc);
        } else {
            outDoc.totalDistanceKm = 0.0;
            outDoc.totalAscentM = 0.0;
            for (size_t i = 0; i < outDoc.geoPoints.size(); ++i) {
                if (i > 0) {
                    const auto& prev = outDoc.geoPoints[i - 1];
                    double segKm = Formats::HaversineKm(prev.lat, prev.lon, outDoc.geoPoints[i].lat, outDoc.geoPoints[i].lon);
                    if (segKm < 500.0) {
                        outDoc.totalDistanceKm += segKm;
                    }
                    float dElev = outDoc.geoPoints[i].elevM - prev.elevM;
                    if (dElev > 0.2f && dElev < 2000.0f) {
                        outDoc.totalAscentM += dElev;
                    }
                }
                outDoc.geoPoints[i].cumDistKm = static_cast<float>(outDoc.totalDistanceKm);
            }
            outDoc.hasGeoTrack = outDoc.hasGeoTrack || (outDoc.geoPoints.size() >= 2);
        }
    }
    return ok;
} E2_READER_CATCH(L"Document reader", fullPath, { outDoc = ParsedOfficeDocument(); return false; })

#if defined(_MSC_VER)
#define E2_DOCCORE_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define E2_DOCCORE_NOINLINE __attribute__((noinline))
#else
#define E2_DOCCORE_NOINLINE
#endif

namespace {

E2_DOCCORE_NOINLINE bool DispatchOfficeZipContainers(const std::wstring& fullPath, const std::wstring& ext, ParsedOfficeDocument& outDoc, bool& outMatched) {
    outMatched = true;
    if (ext == L".ods") return Formats::ParseOpenDocumentOds(fullPath, outDoc);
    if (ext == L".xlsx" || ext == L".xlsm" || ext == L".xltx") {
        return Formats::ParseExcelXlsx(fullPath, outDoc) || Formats::ParseOpenDocumentOds(fullPath, outDoc);
    }
    if (ext == L".odt" || ext == L".ott") return Formats::ParseOpenDocumentOdt(fullPath, outDoc);
    if (ext == L".docx" || ext == L".docm" || ext == L".dotx" || ext == L".dotm") return Formats::ParseWordDocx(fullPath, outDoc);
    if (ext == L".odp" || ext == L".otp") return Formats::ParseOpenDocumentOdp(fullPath, outDoc);
    if (ext == L".pptx" || ext == L".ppsx" || ext == L".pptm" || ext == L".ppsm" || ext == L".potx" || ext == L".potm") {
        return Formats::ParsePowerPointPptx(fullPath, ext, outDoc);
    }
    if (ext == L".epub") return ParseEpubDocument(fullPath, outDoc);
    if (ext == L".xlsb") return Formats::ParseExcelXlsbFile(fullPath, {}, outDoc);
    if (ext == L".idml") return Formats::ParseAdobeIdml(fullPath, {}, outDoc);
    outMatched = false;
    return false;
}

E2_DOCCORE_NOINLINE bool DispatchTextCalAndGisCore(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& outDoc, bool& outMatched) {
    outMatched = true;
    if (ext == L".pst" || ext == L".ost")   return Formats::ParseOutlookPstOst(raw, ext, outDoc);
    if (ext == L".msg")                     return Formats::ParseOutlookMsgDoc(raw, outDoc);
    if (ext == L".mht" || ext == L".mhtml") return Formats::ParseMimeHtmlMht(raw, ext, outDoc);
    if (ext == L".plist")                   return Formats::ParseApplePlist(raw, outDoc);
    if (ext == L".vmt")                     return Formats::ParseValveMaterialVmt(raw, outDoc);
    if (ext == L".ics" || ext == L".ical")  return Formats::ParseICalendar(raw, outDoc);
    if (ext == L".vcf" || ext == L".vcard") return Formats::ParseVCard(raw, outDoc);
    if (ext == L".srt")                     return Formats::ParseSubRipSrt(raw, outDoc);
    if (ext == L".vtt")                     return Formats::ParseWebVtt(raw, outDoc);
    if (ext == L".ass" || ext == L".ssa")   return Formats::ParseSubStationAss(raw, outDoc);
    if (ext == L".gpx")                     return Formats::ParseGpxTrack(raw, outDoc);
    if (ext == L".kml")                     return Formats::ParseKmlGeography(raw, outDoc);
    if (ext == L".kmz")                     return Formats::ParseKmzArchiveDoc(raw, outDoc);
    if (ext == L".geojson")                 return Formats::ParseGeoJsonDoc(raw, outDoc);
    if (ext == L".osm")                     return Formats::ParseOpenStreetMapDoc(raw, outDoc);
    if (ext == L".tcx")                     return Formats::ParseGarminTcxDoc(raw, outDoc);
    if (ext == L".bib")                     return Formats::ParseBibTexDoc(raw, outDoc);
    if (ext == L".ris")                     return Formats::ParseRisCitationDoc(raw, outDoc);
    if (ext == L".nmea")                    return Formats::ParseNmeaGpsLogDoc(raw, outDoc);
    if (ext == L".igc")                     return Formats::ParseIgcFlightLogDoc(raw, outDoc);
    if (ext == L".mbtiles")                 return Formats::ParseMbTilesMapDoc(raw, ext, outDoc);
    if (ext == L".gpkg")                    return Formats::ParseGeoPackageGpkg(raw, outDoc);
    if (ext == L".shp" || ext == L".shx")   return Formats::ParseEsriShapefileDoc(raw, ext, outDoc);
    outMatched = false;
    return false;
}

} // namespace

static bool InspectOfficeFileRaw(const std::wstring& fullPath, ParsedOfficeDocument& outDoc, uint64_t& bytesNotRead) {
    bytesNotRead = 0;
    outDoc = ParsedOfficeDocument{};
    outDoc.filePath = fullPath;
    std::wstring ext = PathUtils::GetExtension(fullPath);
    if (ext == L".gz" && fullPath.size() >= 7) {
        if (PathUtils::ToLower(fullPath.substr(fullPath.size() - 7)) == L".nii.gz") {
            ext = L".nii.gz";
        }
    }

    bool matchedDirect = false;
    bool okDirect = DispatchOfficeZipContainers(fullPath, ext, outDoc, matchedDirect);
    if (matchedDirect) return okDirect;
    okDirect = DispatchDocDirectColumnarFile(fullPath, ext, outDoc, matchedDirect);
    if (matchedDirect) return okDirect;

    if (AiModels::IsSupportedAiModelExt(ext)) {
        return AiModels::ParseAiModelFileToDocument(fullPath, ext, outDoc);
    }

    std::vector<uint8_t> raw;
    if (!PathUtils::ReadFileBytes(fullPath, kDocReadLimit, raw) || raw.empty()) {
        return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(fullPath.c_str(), GetFileExInfoStandard, &fad)) {
        const uint64_t fileSize = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        outDoc.sourceFileSize = std::max<uint64_t>(fileSize, raw.size());
        if (raw.size() >= kDocReadLimit && fileSize > raw.size()) {
            bytesNotRead = fileSize - raw.size();
            outDoc.sourceTail = ReadFileTail(fullPath, fileSize, 256 * 1024);
        }
    }
    static const uint8_t kOle2Magic[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    if (bytesNotRead > 0 && raw.size() >= 8 && std::memcmp(raw.data(), kOle2Magic, 8) == 0 &&
        outDoc.sourceFileSize <= 256ull * 1024 * 1024) {
        std::vector<uint8_t> whole;
        if (PathUtils::ReadFileBytes(fullPath, static_cast<size_t>(outDoc.sourceFileSize), whole) && whole.size() == outDoc.sourceFileSize) {
            raw.swap(whole);
            bytesNotRead = 0;
            outDoc.sourceTail.clear();
        }
    }

    bool matchedCore = false;
    bool okCore = DispatchTextCalAndGisCore(raw, ext, outDoc, matchedCore);
    if (matchedCore) return okCore;

    if (DispatchDocAudioAndPublishing(raw, ext, outDoc))  return true;
    if (DispatchDocSecurityAndSys(raw, ext, outDoc))      return true;
    if (DispatchDocSciAiAndData(raw, ext, outDoc))        return true;
    if (raw.size() >= 100 && Formats::GeoSciDetail::RdU32BE(raw.data()) == 9994u) {
        return Formats::ParseEsriShapefileDoc(raw, ext, outDoc);
    }
    if (TryParseCatalogOrRpgDoc(raw, ext, fullPath, outDoc)) return true;

    if (ext == L".fb2") {
        return ParseFb2Document(raw, outDoc);
    }
    if (ext == L".sqlite" || ext == L".sqlite3" || ext == L".db" ||
        (ext != L".mbtiles" && raw.size() >= 16 && std::memcmp(raw.data(), "SQLite format 3\0", 16) == 0)) {
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
    if (ext == L".tsv") {
        return Formats::ParseTsvDoc(raw, outDoc);
    }
    if (ext == L".csv" || ext == L".psv") {
        return Formats::ParseCsvDoc(raw, outDoc);
    }
    if (ext == L".xls") {
        return Formats::ParseExcelLegacyXls(raw, outDoc);
    }
    if (ext == L".doc" || ext == L".dot") {
        return Formats::ParseWordLegacyDoc(raw, ext, outDoc);
    }
    if (ext == L".ppt" || ext == L".pps") {
        return Formats::ParsePowerPointLegacyPpt(raw, ext, outDoc);
    }

    return false;
}

} // namespace DocReaders

} // namespace Explorer2
