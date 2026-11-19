// DocReaders.cpp - the document readers behind SpreadsheetDocViewer: src/formats/*.h, dispatched by extension.
// Moved out of SpreadsheetDocViewer.cpp so that they build without the viewer, also on Linux (see linux/).
#include "DocReaders.h"
#include "FormatSupport.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "Ole2Compound.h"
#include "formats/common/AiModelDispatch.h"
#ifdef _WIN32
#include <wincrypt.h>   // certificates (X509Cert.h, Pkcs7Bundle.h, AuthenticodeCat.h)
#include <bcrypt.h>     // SHA-1 of torrent info (BitTorrent.h)
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

#include "formats/data/Parquet.h"
#include "formats/data/Avro.h"
#include "formats/data/Bson.h"
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
#include "formats/data/WekaArff.h"
#include "formats/data/MatrixMarket.h"
#include "formats/data/NdJson.h"
#include "formats/data/HttpArchiveHar.h"
#include "formats/data/BitTorrent.h"
#include "formats/audio/CueSheet.h"
#include "formats/audio/PlaylistM3u.h"
#include "formats/audio/MidiFile.h"
#include "formats/data/BioMedicalEdf.h"
#include "formats/security/ElfBinary.h"
#include "formats/security/WebAssemblyWasm.h"
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
#include "formats/audio/WavAudio.h"
#include "formats/audio/FlacAudio.h"
#include "formats/audio/AiffAudio.h"
#include "formats/security/WindowsPeBinary.h"
#include "formats/security/WindowsShortcutLnk.h"
#include "formats/security/WindowsPrefetchPf.h"
#include "formats/security/WindowsEvtxLog.h"
#include "formats/security/WindowsMinidumpDmp.h"
#include "formats/security/MsfPdbSymbols.h"
#include "formats/security/JavaClassFile.h"
#include "formats/security/AndroidDexFile.h"
#include "formats/security/AppleMachOBinary.h"
#include "formats/security/X509Cert.h"
#include "formats/security/Pkcs10Csr.h"
#include "formats/security/Pkcs7Bundle.h"
#include "formats/security/Pkcs12Keystore.h"
#include "formats/security/KeePassKdbx.h"
#include "formats/security/LibPcap.h"
#include "formats/security/PcapNgCapture.h"
#include "formats/security/Uf2Firmware.h"
#include "formats/ai/NumPyNpy.h"
#include "formats/ai/NumPyNpz.h"
#include "formats/ai/JupyterIpynb.h"
#include "formats/data/CborDoc.h"
#include "formats/data/MessagePackDoc.h"
#include "formats/ai/PyTorchCheckpoint.h"
#include "formats/ai/TfLiteModel.h"
#include "formats/data/ApacheArrowIpc.h"
#include "formats/data/FeatherTable.h"
#include "formats/data/ApacheOrc.h"
#include "formats/data/Hdf5Data.h"
#include "formats/data/MatlabMat.h"
#include "formats/data/SpssSav.h"
#include "formats/data/StataDta.h"
#include "formats/data/Sas7bdat.h"
#include "formats/data/RSerializedRds.h"
#include "formats/data/DuckDbDatabase.h"
#include "formats/gis/EsriShapefileDoc.h"
#include "formats/gis/GeoPackageGpkg.h"
#include "formats/gis/GarminFit.h"
#include "formats/gis/KmzArchiveDoc.h"
#include "formats/data/NetCdfData.h"
#include "formats/gis/WmoGrib.h"
#include "formats/data/NiftiVolume.h"
#include "formats/docs/EmailEml.h"
#include "formats/docs/UnixMbox.h"
#include "formats/audio/Mp3Audio.h"
#include "formats/audio/OggVorbisAudio.h"
#include "formats/audio/OpusAudio.h"
#include "formats/audio/SpeexAudio.h"
#include "formats/audio/TrackerMod.h"
#include "formats/audio/TrackerXm.h"
#include "formats/audio/TrackerS3m.h"
#include "formats/audio/TrackerIt.h"
#include "formats/audio/SoundFontSf2.h"
#include "formats/audio/SoundFontSfz.h"
#include "formats/audio/MuseScoreMscz.h"
#include "formats/audio/AbletonAls.h"
#include "formats/audio/FlStudioFlp.h"
#include "formats/audio/GuitarProTab.h"
#include "formats/audio/ReaperRpp.h"
#include "formats/docs/CmxEdl.h"
#include "formats/docs/FinalCutFcpxml.h"
#include "formats/docs/OpenTimelineOtio.h"
#include "formats/docs/VobSubIdx.h"
#include "formats/docs/VobSubMpegSub.h"
#include "formats/docs/BluraySup.h"
#include "formats/docs/MicroDvdSub.h"
#include "formats/docs/SubViewerSub.h"
#include "formats/docs/OutlookMsg.h"
#include "formats/docs/KindleMobi.h"
#include "formats/docs/DjVuDoc.h"
#include "formats/docs/ChmHelp.h"
#include "formats/data/AccessDatabaseMdb.h"
#include "formats/data/RedisRdb.h"
#include "formats/data/RocksLevelDbSst.h"
#include "formats/gis/PmTilesMap.h"
#include "formats/gis/MbTilesMap.h"
#include "formats/gis/OsmPbfMap.h"
#include "formats/data/ProtobufWire.h"
#include "formats/data/TerraformState.h"
#include "formats/security/SarifLog.h"
#include "formats/security/SpdxSbom.h"
#include "formats/security/CycloneDxSbom.h"
#include "formats/security/OpenPgpPacket.h"
#include "formats/security/JavaKeyStoreJks.h"
#include "formats/security/AuthenticodeCat.h"
#include "formats/security/WindowsRegistryHive.h"
#include "formats/security/WindowsEtlTrace.h"
#include "formats/security/GroupPolicyPol.h"
#include "formats/audio/ChiptuneSid.h"
#include "formats/audio/ChiptuneNsf.h"
#include "formats/audio/ChiptuneSpc.h"
#include "formats/audio/ChiptuneVgm.h"
#include "formats/audio/MusicXmlScore.h"
#include "formats/docs/AdobeXmp.h"
#include "formats/docs/UnrealAssetDoc.h"
#include "formats/docs/UnityYamlScene.h"
#include "formats/docs/VisioVsdx.h"
#include "formats/docs/AppleIwa.h"
#include "formats/docs/ApplePages.h"
#include "formats/docs/AppleNumbers.h"
#include "formats/docs/AppleKeynote.h"
#include "formats/docs/ExcelXlsb.h"
#include "formats/docs/OutlookPst.h"
#include "formats/docs/AdobeIdml.h"
#include "formats/docs/MimeHtmlMht.h"
#include "formats/docs/ApplePlist.h"
#include "formats/docs/ValveMaterialVmt.h"

namespace DocReaders {

bool IsSpreadsheetOrDocExtension(const std::wstring& extLower) {
    return extLower == L".csv"     || extLower == L".tsv"     || extLower == L".psv"  ||
           extLower == L".xlsx"    || extLower == L".xlsm"    || extLower == L".xlsb" || extLower == L".xls"  ||
           extLower == L".ods"     || extLower == L".docx"    || extLower == L".docm" ||
           extLower == L".doc"     || extLower == L".dot"     || extLower == L".ppt"  || extLower == L".pps" ||
           extLower == L".odt"     || extLower == L".rtf"     || extLower == L".pptx" ||
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
           extLower == L".wav"     || extLower == L".flac"    || extLower == L".aiff" ||
           extLower == L".edf"     || extLower == L".elf"     || extLower == L".wasm" ||
           extLower == L".nmea"    || extLower == L".igc"     ||
           extLower == L".exe"     || extLower == L".dll"     || extLower == L".sys"  || extLower == L".ocx"  ||
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
           extLower == L".mpk"     || extLower == L".cbor"    || extLower == L".ipynb"   || extLower == L".sav"  ||
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
           extLower == L".pol"     || extLower == L".sid"     || extLower == L".nsf"  || extLower == L".spc"  ||
           extLower == L".vgm"     || extLower == L".musicxml"|| extLower == L".mxl"  || extLower == L".xmp"  ||
           extLower == L".uasset"  || extLower == L".umap"    || extLower == L".prefab"|| extLower == L".unity"||
           extLower == L".pages"   || extLower == L".numbers" || extLower == L".key"  || extLower == L".iwa"  || extLower == L".vsdx" ||
           extLower == L".vmt";
}

// How much of a file the in-memory document readers get (the container readers open the file themselves)
constexpr size_t kDocReadLimit = 32 * 1024 * 1024;

// The last `want` bytes of a file (all of it when it is shorter)
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
        // The reader saw only the start of the file: the title says so, whatever the reader made of it
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
            outDoc.hasGeoTrack = (outDoc.geoPoints.size() >= 2);
        }
    }
    return ok;
} E2_READER_CATCH(L"Document reader", fullPath, { outDoc = ParsedOfficeDocument(); return false; })


static bool DispatchSysSecFormats(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (extLower == L".exe" || extLower == L".dll" || extLower == L".sys" || extLower == L".ocx" ||
        extLower == L".scr" || extLower == L".efi" || extLower == L".drv" || extLower == L".cpl" ||
        (raw.size() >= 64 && raw[0] == 'M' && raw[1] == 'Z' &&
         Formats::DetailSys100::RdU32LE(raw.data() + 60) + 24 <= raw.size() &&
         std::memcmp(raw.data() + Formats::DetailSys100::RdU32LE(raw.data() + 60), "PE\0\0", 4) == 0))
    {
        return Formats::ParseWindowsPeBinary(raw, out);
    }
    if ((extLower == L".so" || extLower == L".o") && raw.size() >= 52 &&
        raw[0] == 0x7F && raw[1] == 'E' && raw[2] == 'L' && raw[3] == 'F')
    {
        return Formats::ParseElfBinaryDoc(raw, out);
    }
    if (extLower == L".lnk" || (raw.size() >= 76 && Formats::DetailSys100::RdU32LE(raw.data()) == 0x0000004Cu && raw[4] == 0x01 && raw[5] == 0x14)) {
        return Formats::ParseWindowsLnkShortcut(raw, out);
    }
    if (extLower == L".pf" || (raw.size() >= 84 && std::memcmp(raw.data() + 4, "SCCA", 4) == 0) ||
        (raw.size() >= 12 && raw[0] == 'M' && raw[1] == 'A' && raw[2] == 'M' && (raw[3] & 0x0Fu) == 0x04u))
    {
        return Formats::ParseWindowsPrefetch(raw, out);
    }
    if (extLower == L".evtx" || (raw.size() >= 128 && std::memcmp(raw.data(), "ElfFile\0", 8) == 0)) {
        return Formats::ParseWindowsEvtxLog(raw, out);
    }
    if (extLower == L".dmp" || extLower == L".mdmp" || (raw.size() >= 32 && std::memcmp(raw.data(), "MDMP", 4) == 0)) {
        return Formats::ParseWindowsMinidump(raw, out);
    }
    if (extLower == L".pdb" || (raw.size() >= 56 && std::memcmp(raw.data(), "Microsoft C/C++ MSF 7.00", 24) == 0)) {
        return Formats::ParseMsfPdbSymbols(raw, out);
    }
    if (extLower == L".macho" || extLower == L".dylib" || extLower == L".so") {
        return Formats::ParseAppleMachOBinary(raw, out);
    }
    if (extLower == L".class") {
        return Formats::ParseJavaClassFile(raw, out);
    }
    if (raw.size() >= 28 && Formats::IsValidMachOFatArchive(raw, out.sourceFileSize)) {
        return Formats::ParseAppleMachOBinary(raw, out);
    }
    if (raw.size() >= 24 && Formats::DetailSys100::RdU32BE(raw.data()) == 0xCAFEBABEu) {
        return Formats::ParseJavaClassFile(raw, out);
    }
    if (raw.size() >= 28 && Formats::IsValidMachOMagic(Formats::DetailSys100::RdU32LE(raw.data()))) {
        return Formats::ParseAppleMachOBinary(raw, out);
    }
    if (extLower == L".dex" || (raw.size() >= 112 && std::memcmp(raw.data(), "dex\n", 4) == 0)) {
        return Formats::ParseAndroidDexFile(raw, out);
    }
    if (extLower == L".crt" || extLower == L".cer" || extLower == L".pem" || extLower == L".der") {
        return Formats::ParseX509CertDoc(raw, extLower, out);
    }
    if (extLower == L".csr") return Formats::ParsePkcs10CsrDoc(raw, extLower, out);
    if (extLower == L".p7b" || extLower == L".p7c") return Formats::ParsePkcs7BundleDoc(raw, extLower, out);
    if (extLower == L".p12" || extLower == L".pfx") return Formats::ParsePkcs12KeystoreDoc(raw, extLower, out);
    if (extLower == L".kdbx" || (raw.size() >= 16 && Formats::DetailSys100::RdU32LE(raw.data()) == 0x9AA2D903u && Formats::DetailSys100::RdU32LE(raw.data() + 4) == 0xB54BFB67u)) {
        return Formats::ParseKeePassKdbx(raw, out);
    }
    if (extLower == L".pcapng" || (raw.size() >= 28 && Formats::DetailSys100::RdU32LE(raw.data()) == 0x0A0D0D0Au)) {
        return Formats::ParsePcapNgCapture(raw, out) || Formats::ParseLibPcapDoc(raw, out);
    }
    if (extLower == L".pcap") {
        return Formats::ParseLibPcapDoc(raw, out) || Formats::ParsePcapNgCapture(raw, out);
    }
    if (extLower == L".uf2" || (raw.size() >= 512 && std::memcmp(raw.data(), "UF2\n", 4) == 0)) {
        return Formats::ParseUf2Firmware(raw, out);
    }
    return false;
}

static bool DispatchAiDataFormats(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (extLower == L".npy" || (raw.size() >= 10 && std::memcmp(raw.data(), "\x93NUMPY", 6) == 0)) {
        return Formats::ParseNumPyNpy(raw, out);
    }
    if (extLower == L".npz") return Formats::ParseNumPyNpz(raw, out);
    if (extLower == L".ipynb") return Formats::ParseJupyterIpynb(raw, out);
    if (extLower == L".cbor") return Formats::ParseCborDoc(raw, out);
    if (extLower == L".msgpack" || extLower == L".mpk") return Formats::ParseMessagePackDoc(raw, extLower, out);
    if (extLower == L".onnx" || extLower == L".gguf" || extLower == L".safetensors") {
        return ::Explorer2::AiModels::ParseAiModelMemoryToDocument(raw.data(), raw.size(), extLower, out);
    }
    if (extLower == L".pt" || extLower == L".pth" || extLower == L".ckpt") return Formats::ParsePyTorchCheckpoint(raw, extLower, out);
    if (extLower == L".tflite") return Formats::ParseTfLiteModel(raw, out);
    if (extLower == L".feather") return Formats::ParseFeatherTable(raw, extLower, out);
    if (extLower == L".arrow" || extLower == L".ipc") return Formats::ParseApacheArrowIpc(raw, extLower, out);
    if (extLower == L".orc") return Formats::ParseApacheOrc(raw, out);
    if (extLower == L".h5" || extLower == L".hdf5") return Formats::ParseHdf5Doc(raw, extLower, out);
    if (extLower == L".mat") return Formats::ParseMatlabMat(raw, out);
    if (extLower == L".sav") return Formats::ParseSpssSav(raw, out);
    if (extLower == L".dta") return Formats::ParseStataDta(raw, out);
    if (extLower == L".sas7bdat") return Formats::ParseSas7bdat(raw, out);
    if (extLower == L".rds") return Formats::ParseRSerializedRds(raw, out);
    if (extLower == L".duckdb") return Formats::ParseDuckDbDatabase(raw, out);
    return false;
}

static bool DispatchGeoSciFormats(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (extLower == L".shp" || extLower == L".shx" ||
        (raw.size() >= 100 && Formats::GeoSciDetail::RdU32BE(raw.data()) == 9994u))
    {
        return Formats::ParseEsriShapefileDoc(raw, extLower, out);
    }
    if (extLower == L".gpkg") {
        return Formats::ParseGeoPackageGpkg(raw, out);
    }
    if (extLower == L".fit" || (raw.size() >= 14 && (raw[0] == 12 || raw[0] == 14) && std::memcmp(raw.data() + 8, ".FIT", 4) == 0)) {
        return Formats::ParseGarminFitDoc(raw, out);
    }
    if (extLower == L".kmz") {
        return Formats::ParseKmzArchiveDoc(raw, out);
    }
    if (extLower == L".nc" || extLower == L".cdf" ||
        (raw.size() >= 16 && (std::memcmp(raw.data(), "CDF\x01", 4) == 0 ||
                              std::memcmp(raw.data(), "CDF\x02", 4) == 0 ||
                              std::memcmp(raw.data(), "CDF\x05", 4) == 0)))
    {
        return Formats::ParseNetCdfData(raw, extLower, out);
    }
    if (extLower == L".grib" || extLower == L".grib2" || extLower == L".grb" ||
        (raw.size() >= 16 && std::memcmp(raw.data(), "GRIB", 4) == 0 && (raw[7] == 1 || raw[7] == 2)))
    {
        return Formats::ParseWmoGribDoc(raw, out);
    }
    if (extLower == L".nii" || extLower == L".nii.gz") {
        return Formats::ParseNiftiVolumeDoc(raw, out);
    }
    return false;
}

static bool DispatchMediaDocFormats(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (extLower == L".eml")   return Formats::ParseEmailEmlDoc(raw, out);
    if (extLower == L".mbox")  return Formats::ParseUnixMboxDoc(raw, out);
    if (extLower == L".mp3")   return Formats::ParseMp3AudioDoc(raw, out);
    if (extLower == L".opus")  return Formats::ParseOpusAudioDoc(raw, extLower, out) || Formats::ParseOggVorbisAudioDoc(raw, extLower, out);
    if (extLower == L".spx")   return Formats::ParseSpeexAudioDoc(raw, extLower, out) || Formats::ParseOggVorbisAudioDoc(raw, extLower, out);
    if (extLower == L".ogg")   return Formats::ParseOggVorbisAudioDoc(raw, extLower, out) || Formats::ParseOpusAudioDoc(raw, extLower, out) || Formats::ParseSpeexAudioDoc(raw, extLower, out);
    if (extLower == L".mod")   return Formats::ParseTrackerModDoc(raw, out);
    if (extLower == L".xm")    return Formats::ParseTrackerXmDoc(raw, out);
    if (extLower == L".s3m")   return Formats::ParseTrackerS3mDoc(raw, out);
    if (extLower == L".it")    return Formats::ParseTrackerItDoc(raw, out);
    if (extLower == L".sf2")   return Formats::ParseSoundFontSf2Doc(raw, out);
    if (extLower == L".sfz")   return Formats::ParseSoundFontSfzDoc(raw, out);
    if (extLower == L".mscz")  return Formats::ParseMuseScoreMsczDoc(raw, out);
    if (extLower == L".als")   return Formats::ParseAbletonAlsDoc(raw, out);
    if (extLower == L".flp")   return Formats::ParseFlStudioFlpDoc(raw, out);
    if (extLower == L".gp" || extLower == L".gp3" || extLower == L".gp4" || extLower == L".gp5") return Formats::ParseGuitarProTabDoc(raw, extLower, out);
    if (extLower == L".rpp")   return Formats::ParseReaperRppDoc(raw, out);
    if (extLower == L".edl")   return Formats::ParseCmxEdlDoc(raw, out);
    if (extLower == L".fcpxml") return Formats::ParseFinalCutFcpxmlDoc(raw, out);
    if (extLower == L".otio")  return Formats::ParseOpenTimelineOtioDoc(raw, out);
    if (extLower == L".idx")   return Formats::ParseVobSubIdxDoc(raw, out);
    if (extLower == L".sup")   return Formats::ParseBluraySupDoc(raw, out);
    if (extLower == L".sub")   return Formats::ParseVobSubSpuDoc(raw, out) || Formats::ParseMicroDvdSubDoc(raw, out) || Formats::ParseSubViewerSubDoc(raw, out);
    if (extLower == L".msg")   return Formats::ParseOutlookMsgDoc(raw, out);
    if (extLower == L".mobi" || extLower == L".azw" || extLower == L".azw3" || extLower == L".prc") return Formats::ParseKindleMobiDoc(raw, extLower, out);
    if (extLower == L".djvu" || extLower == L".djv") return Formats::ParseDjVuDoc(raw, out);
    if (extLower == L".chm")   return Formats::ParseChmHelpDoc(raw, out);
    return false;
}

static bool DispatchExtended200Formats(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (extLower == L".accdb" || extLower == L".mdb") return Formats::ParseAccessDatabase200(raw, extLower, out);
    if (extLower == L".rdb") return Formats::ParseRedisRdb200(raw, out);
    if (extLower == L".sst" || extLower == L".ldb") return Formats::ParseRocksLevelDbSst200(raw, extLower, out);
    if (extLower == L".pmtiles") return Formats::ParsePmTilesMapDoc(raw, extLower, out);
    if (extLower == L".mbtiles") return Formats::ParseMbTilesMapDoc(raw, extLower, out);
    if (extLower == L".pbf") return Formats::ParseOsmPbfMapDoc(raw, extLower, out) || Formats::ParseProtobufWireDoc(raw, extLower, out);
    if (extLower == L".pb") return Formats::ParseProtobufWireDoc(raw, extLower, out);
    if (extLower == L".tfstate") return Formats::ParseTerraformStateDoc(raw, out);
    if (extLower == L".sarif") return Formats::ParseSarifLogDoc(raw, out);
    if (extLower == L".spdx") return Formats::ParseSpdxSbomDoc(raw, out);
    if (extLower == L".cdx") return Formats::ParseCycloneDxSbomDoc(raw, out);
    if (extLower == L".gpg" || extLower == L".pgp" || extLower == L".sig") return Formats::ParseOpenPgpPacketDoc(raw, extLower, out);
    if (extLower == L".jks") return Formats::ParseJavaKeyStoreJksDoc(raw, extLower, out);
    if (extLower == L".cat") return Formats::ParseAuthenticodeCatDoc(raw, extLower, out);
    if (extLower == L".hiv" || extLower == L".dat") {
        return Formats::ParseWindowsRegistryHiveDoc(raw, extLower, out) || Formats::ParseGroupPolicyPolDoc(raw, extLower, out);
    }
    if (extLower == L".etl") return Formats::ParseWindowsEtlTraceDoc(raw, extLower, out);
    if (extLower == L".pol") return Formats::ParseGroupPolicyPolDoc(raw, extLower, out);
    if (extLower == L".sid") return Formats::ParseChiptuneSidDoc(raw, out);
    if (extLower == L".nsf") return Formats::ParseChiptuneNsfDoc(raw, out);
    if (extLower == L".spc") return Formats::ParseChiptuneSpcDoc(raw, out);
    if (extLower == L".vgm") return Formats::ParseChiptuneVgmDoc(raw, out);
    if (extLower == L".musicxml" || extLower == L".mxl") return Formats::ParseMusicXmlScoreDoc(raw, extLower, out);
    if (extLower == L".xmp") return Formats::ParseAdobeXmpDoc(raw, extLower, out);
    if (extLower == L".uasset" || extLower == L".umap") return Formats::ParseUnrealAssetDoc(raw, extLower, out);
    if (extLower == L".prefab" || extLower == L".unity") return Formats::ParseUnityYamlSceneDoc(raw, extLower, out);
    if (extLower == L".vsdx") return Formats::ParseVisioVsdxDoc(raw, extLower, out);
    if (extLower == L".pages")   return Formats::ParseApplePagesDoc(raw, out);
    if (extLower == L".numbers") return Formats::ParseAppleNumbersDoc(raw, out);
    if (extLower == L".key")     return Formats::ParseAppleKeynoteDoc(raw, out);
    if (extLower == L".iwa")     return Formats::ParseAppleIwaDoc(raw, out);
    return false;
}

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

    if (ext == L".ods") {
        return Formats::ParseOpenDocumentOds(fullPath, outDoc);
    }
    if (ext == L".xlsx" || ext == L".xlsm" || ext == L".xltx") {
        return Formats::ParseExcelXlsx(fullPath, outDoc) || Formats::ParseOpenDocumentOds(fullPath, outDoc);
    }
    if (ext == L".odt" || ext == L".ott") {
        return Formats::ParseOpenDocumentOdt(fullPath, outDoc);
    }
    if (ext == L".docx" || ext == L".docm" || ext == L".dotx" || ext == L".dotm") {
        return Formats::ParseWordDocx(fullPath, outDoc);
    }
    if (ext == L".odp" || ext == L".otp") {
        return Formats::ParseOpenDocumentOdp(fullPath, outDoc);
    }
    if (ext == L".pptx" || ext == L".ppsx" || ext == L".pptm" || ext == L".ppsm" ||
        ext == L".potx" || ext == L".potm")
    {
        return Formats::ParsePowerPointPptx(fullPath, ext, outDoc);
    }
    if (ext == L".epub") {
        return ParseEpubDocument(fullPath, outDoc);
    }
    if (ext == L".xlsb") {
        return Formats::ParseExcelXlsbFile(fullPath, {}, outDoc);
    }
    if (ext == L".idml") {
        return Formats::ParseAdobeIdml(fullPath, {}, outDoc);
    }
    if (ext == L".parquet") {
        return Formats::ParseApacheParquetFile(fullPath, outDoc);
    }
    if (ext == L".feather") {
        return Formats::ParseFeatherTableFile(fullPath, ext, outDoc);
    }
    if (ext == L".arrow" || ext == L".ipc") {
        return Formats::ParseApacheArrowIpcFile(fullPath, ext, outDoc);
    }
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
    // A compound file (.xls, .doc, .ppt, .msg, ...) keeps its sector tables and streams anywhere in the file, so one
    // up to 256 MB is read whole rather than only its start
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

    if (ext == L".pst" || ext == L".ost")   return Formats::ParseOutlookPstOst(raw, ext, outDoc);
    if (ext == L".mht" || ext == L".mhtml") return Formats::ParseMimeHtmlMht(raw, ext, outDoc);
    if (ext == L".plist")                   return Formats::ParseApplePlist(raw, outDoc);
    if (ext == L".vmt")     return Formats::ParseValveMaterialVmt(raw, outDoc);
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
    if (ext == L".pcap")    return Formats::ParseLibPcapDoc(raw, outDoc) || Formats::ParsePcapNgCapture(raw, outDoc);
    if (ext == L".torrent") return Formats::ParseBitTorrentDoc(raw, outDoc);
    if (ext == L".cue")     return Formats::ParseCueSheetDoc(raw, outDoc);
    if (ext == L".m3u" || ext == L".m3u8" || ext == L".pls") return Formats::ParsePlaylistM3uDoc(raw, outDoc);
    if (ext == L".mid" || ext == L".midi") return Formats::ParseMidiFileDoc(raw, outDoc);
    if (ext == L".wav")     return Formats::ParseWavAudioDoc(raw, outDoc);
    if (ext == L".flac")    return Formats::ParseFlacAudioDoc(raw, outDoc);
    if (ext == L".aiff" || ext == L".aif" || ext == L".aifc") return Formats::ParseAiffAudioDoc(raw, outDoc);
    if (ext == L".edf")     return Formats::ParseBioMedicalEdfDoc(raw, outDoc);
    if (ext == L".elf" || ext == L".so" ||
        (ext.empty() && raw.size() >= 16 && raw[0] == 0x7Fu && raw[1] == 'E' && raw[2] == 'L' && raw[3] == 'F')) {
        return Formats::ParseElfBinaryDoc(raw, outDoc);
    }
    if (ext == L".wasm")    return Formats::ParseWebAssemblyWasmDoc(raw, outDoc);
    if (ext == L".nmea")    return Formats::ParseNmeaGpsLogDoc(raw, outDoc);
    if (ext == L".igc")     return Formats::ParseIgcFlightLogDoc(raw, outDoc);
    if (DispatchSysSecFormats(raw, ext, outDoc))      return true;
    if (DispatchAiDataFormats(raw, ext, outDoc))      return true;
    if (DispatchGeoSciFormats(raw, ext, outDoc))      return true;
    if (DispatchMediaDocFormats(raw, ext, outDoc))    return true;
    if (DispatchExtended200Formats(raw, ext, outDoc)) return true;

    if (ext == L".fb2") {
        return ParseFb2Document(raw, outDoc);
    }
    if (ext == L".gpkg") {
        if (raw.size() < 100 || std::memcmp(raw.data(), "SQLite format 3\0", 16) != 0) return false;
        return ParseSqliteDatabase(raw, outDoc, /*isGeoPackage=*/true);
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

    // Legacy .xls (OLE2 BIFF8 / BIFF5 Workbook & Shared String Table parser)
    if (ext == L".xls") {
        return Formats::ParseExcelLegacyXls(raw, outDoc);
    }


    // Legacy .doc/.dot ([MS-DOC] Piece Table) & .ppt/.pps ([MS-PPT] Record Tree + Escher Shapes)
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
