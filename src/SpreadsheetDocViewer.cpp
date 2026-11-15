#include "SpreadsheetDocViewer.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "Ole2Compound.h"
#include <wincrypt.h>
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
           extLower == L".cdf"     || extLower == L".grib"    || extLower == L".grib2"|| extLower == L".grb"  || extLower == L".nii"  ||
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

namespace BiffXlsDetail {

inline uint16_t Rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint32_t Rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
inline double RdF64(const uint8_t* p) {
    double d = 0.0;
    std::memcpy(&d, p, 8);
    return d;
}

inline double DecodeBiffRk(uint32_t rk) {
    double v = 0.0;
    if ((rk & 0x02u) != 0) {
        int32_t iv = static_cast<int32_t>(rk) >> 2;
        v = static_cast<double>(iv);
    } else {
        uint64_t bits = static_cast<uint64_t>(rk & ~3u) << 34;
        std::memcpy(&v, &bits, 8);
    }
    if ((rk & 0x01u) != 0) {
        v /= 100.0;
    }
    return v;
}

inline std::wstring FormatXlsNumber(double v) {
    if (std::isnan(v)) return L"#NUM!";
    if (std::isinf(v)) return v < 0 ? L"-Inf" : L"Inf";
    if (v == 0.0) return L"0";
    wchar_t buf[64] = {};
    double av = std::fabs(v);
    if (av >= 1e-4 && av < 1e12 && std::floor(v) == v) {
        swprintf_s(buf, L"%.0f", v);
    } else {
        swprintf_s(buf, L"%.12g", v);
    }
    return buf;
}

inline std::wstring BiffErrCodeToStr(uint8_t err) {
    switch (err) {
        case 0x00: return L"#NULL!";
        case 0x07: return L"#DIV/0!";
        case 0x0F: return L"#VALUE!";
        case 0x17: return L"#REF!";
        case 0x1D: return L"#NAME?";
        case 0x24: return L"#NUM!";
        case 0x2A: return L"#N/A";
        default:   return L"#ERR!";
    }
}

struct BiffChunk {
    const uint8_t* ptr = nullptr;
    size_t len = 0;
};

// MS-XLS 2.5.293 XLUnicodeRichExtendedString cursor spanning SST + CONTINUE record boundaries
struct SstStreamCursor {
    const std::vector<BiffChunk>& chunks;
    size_t cIdx = 0;
    size_t off  = 0;

    explicit SstStreamCursor(const std::vector<BiffChunk>& c) : chunks(c) {}

    bool EnsureAvail() {
        while (cIdx < chunks.size() && off >= chunks[cIdx].len) {
            ++cIdx;
            off = 0;
        }
        return cIdx < chunks.size();
    }

    bool ReadRawBytes(uint8_t* dst, size_t count) {
        size_t got = 0;
        while (got < count) {
            if (!EnsureAvail()) return false;
            size_t avail = chunks[cIdx].len - off;
            size_t take = std::min(count - got, avail);
            std::memcpy(dst + got, chunks[cIdx].ptr + off, take);
            off += take;
            got += take;
        }
        return true;
    }

    bool SkipRawBytes(size_t count) {
        size_t rem = count;
        while (rem > 0) {
            if (!EnsureAvail()) return false;
            size_t avail = chunks[cIdx].len - off;
            size_t take = std::min(rem, avail);
            off += take;
            rem -= take;
        }
        return true;
    }

    bool ReadRichExtendedString(std::wstring& out) {
        out.clear();
        uint8_t hdr[3] = {};
        if (!ReadRawBytes(hdr, 3)) return false;
        uint16_t cch = Rd16(hdr);
        uint8_t flags = hdr[2];
        bool fHighByte = (flags & 0x01u) != 0;
        bool fExtSt    = (flags & 0x04u) != 0;
        bool fRichSt   = (flags & 0x08u) != 0;

        uint16_t cRun = 0;
        if (fRichSt) {
            uint8_t rb[2] = {};
            if (!ReadRawBytes(rb, 2)) return false;
            cRun = Rd16(rb);
        }
        uint32_t cbExtRst = 0;
        if (fExtSt) {
            uint8_t eb[4] = {};
            if (!ReadRawBytes(eb, 4)) return false;
            cbExtRst = Rd32(eb);
        }

        out.reserve(cch);
        size_t remChars = cch;
        while (remChars > 0) {
            if (cIdx >= chunks.size()) return false;
            if (off >= chunks[cIdx].len) {
                // Crossing into a CONTINUE record mid-string: first byte of CONTINUE gives new fHighByte!
                ++cIdx;
                off = 0;
                while (cIdx < chunks.size() && chunks[cIdx].len == 0) ++cIdx;
                if (cIdx >= chunks.size()) return false;
                uint8_t contFlags = chunks[cIdx].ptr[off++];
                fHighByte = (contFlags & 0x01u) != 0;
            }
            size_t availBytes = chunks[cIdx].len - off;
            if (fHighByte) {
                if (availBytes < 2) {
                    uint8_t b2[2] = {};
                    if (!ReadRawBytes(b2, 2)) return false;
                    out.push_back(static_cast<wchar_t>(Rd16(b2)));
                    --remChars;
                    continue;
                }
                size_t takeChars = std::min(remChars, availBytes / 2);
                const uint8_t* p = chunks[cIdx].ptr + off;
                for (size_t i = 0; i < takeChars; ++i) {
                    out.push_back(static_cast<wchar_t>(Rd16(p + i * 2)));
                }
                off += takeChars * 2;
                remChars -= takeChars;
            } else {
                size_t takeChars = std::min(remChars, availBytes);
                const uint8_t* p = chunks[cIdx].ptr + off;
                for (size_t i = 0; i < takeChars; ++i) {
                    out.push_back(static_cast<wchar_t>(p[i]));
                }
                off += takeChars;
                remChars -= takeChars;
            }
        }

        if (cRun > 0 && !SkipRawBytes(static_cast<size_t>(cRun) * 4)) return false;
        if (cbExtRst > 0 && !SkipRawBytes(cbExtRst)) return false;
        return true;
    }
};

inline std::wstring ReadInlineBiffString(const uint8_t* p, size_t avail, bool isBiff8, bool lenIs16Bit) {
    if (avail == 0) return {};
    size_t pos = 0;
    uint16_t cch = 0;
    if (lenIs16Bit) {
        if (avail < 2) return {};
        cch = Rd16(p);
        pos = 2;
    } else {
        cch = p[0];
        pos = 1;
    }
    if (cch == 0) return {};

    if (!isBiff8) {
        size_t take = std::min<size_t>(cch, avail - pos);
        std::wstring out;
        out.reserve(take);
        for (size_t i = 0; i < take; ++i) out.push_back(static_cast<wchar_t>(p[pos + i]));
        return out;
    }

    if (pos >= avail) return {};
    uint8_t flags = p[pos++];
    bool fHighByte = (flags & 0x01u) != 0;
    bool fExtSt    = (flags & 0x04u) != 0;
    bool fRichSt   = (flags & 0x08u) != 0;
    if (fRichSt) { if (pos + 2 > avail) return {}; pos += 2; }
    if (fExtSt)  { if (pos + 4 > avail) return {}; pos += 4; }

    std::wstring out;
    if (fHighByte) {
        size_t take = std::min<size_t>(cch, (avail - pos) / 2);
        out.reserve(take);
        for (size_t i = 0; i < take; ++i) out.push_back(static_cast<wchar_t>(Rd16(p + pos + i * 2)));
    } else {
        size_t take = std::min<size_t>(cch, avail - pos);
        out.reserve(take);
        for (size_t i = 0; i < take; ++i) out.push_back(static_cast<wchar_t>(p[pos + i]));
    }
    return out;
}

inline bool ParseBiffWorkbook(const std::vector<uint8_t>& raw, ParsedOfficeDocument& outDoc) {
    std::vector<uint8_t> streamBuf;
    const uint8_t* biff = nullptr;
    size_t biffSize = 0;

    Ole2::CompoundFile cf;
    if (Ole2::ParseCompoundFile(raw.data(), raw.size(), cf)) {
        const Ole2::DirEntry* wb = Ole2::FindStreamCaseInsensitive(cf, L"Workbook");
        if (!wb) wb = Ole2::FindStreamCaseInsensitive(cf, L"Book");
        if (!wb || !Ole2::ExtractStream(cf, *wb, streamBuf) || streamBuf.size() < 8) {
            return false;
        }
        biff = streamBuf.data();
        biffSize = streamBuf.size();
    } else {
        if (raw.size() < 8) return false;
        uint16_t rec0 = Rd16(raw.data());
        uint16_t len0 = Rd16(raw.data() + 2);
        if ((rec0 != 0x0809 && rec0 != 0x0409 && rec0 != 0x0209 && rec0 != 0x0009) || len0 < 4 || len0 > 64 || 4u + len0 > raw.size()) {
            return false;
        }
        biff = raw.data();
        biffSize = raw.size();
    }

    uint16_t firstRec = Rd16(biff);
    uint16_t firstLen = Rd16(biff + 2);
    if ((firstRec != 0x0809 && firstRec != 0x0409 && firstRec != 0x0209 && firstRec != 0x0009) || firstLen < 4 || 4u + firstLen > biffSize) {
        return false;
    }

    uint16_t biffVer = Rd16(biff + 4);
    uint16_t firstSubType = Rd16(biff + 6);
    bool isBiff8 = (firstRec == 0x0809 && biffVer >= 0x0600);

    struct BoundSheetMeta {
        uint32_t bofOffset = 0;
        uint8_t visibility = 0;
        uint8_t sheetType  = 0;
        std::wstring name;
    };
    std::vector<BoundSheetMeta> boundSheets;
    std::vector<std::wstring> sst;

    size_t pos = 0;
    if (firstSubType == 0x0005) {
        while (pos + 4 <= biffSize) {
            uint16_t recId  = Rd16(biff + pos);
            uint16_t recLen = Rd16(biff + pos + 2);
            pos += 4;
            if (recLen > biffSize - pos) break;
            const uint8_t* p = biff + pos;
            size_t nextPos = pos + recLen;

            if (recId == 0x000A) {
                pos = nextPos;
                break;
            }
            if (recId == 0x0085 && recLen >= 7) {
                BoundSheetMeta bs;
                bs.bofOffset  = Rd32(p);
                bs.visibility = p[4];
                bs.sheetType  = p[5];
                bs.name       = ReadInlineBiffString(p + 6, recLen - 6, isBiff8, false);
                if (bs.name.empty()) bs.name = L"Sheet" + std::to_wstring(boundSheets.size() + 1);
                boundSheets.push_back(std::move(bs));
            } else if (recId == 0x00FC && recLen >= 8) {
                uint32_t cstUnique = Rd32(p + 4);
                std::vector<BiffChunk> chunks;
                chunks.push_back({ p + 8, static_cast<size_t>(recLen - 8) });

                while (nextPos + 4 <= biffSize && Rd16(biff + nextPos) == 0x003C) {
                    uint16_t contLen = Rd16(biff + nextPos + 2);
                    if (contLen > biffSize - (nextPos + 4)) break;
                    chunks.push_back({ biff + nextPos + 4, contLen });
                    nextPos += 4 + contLen;
                }

                uint32_t capUnique = std::min<uint32_t>(cstUnique, 500000u);
                sst.reserve(std::min<uint32_t>(capUnique, 4096u));
                SstStreamCursor cur(chunks);
                for (uint32_t i = 0; i < capUnique; ++i) {
                    std::wstring s;
                    if (!cur.ReadRichExtendedString(s)) break;
                    sst.push_back(std::move(s));
                }
            }
            pos = nextPos;
        }
    }

    auto parseWorksheetAt = [&](size_t startOff, const std::wstring& sheetName, size_t& outEndOff) -> bool {
        outEndOff = startOff;
        if (startOff + 8 > biffSize) return false;
        uint16_t bofId  = Rd16(biff + startOff);
        uint16_t bofLen = Rd16(biff + startOff + 2);
        if ((bofId != 0x0809 && bofId != 0x0409 && bofId != 0x0209 && bofId != 0x0009) ||
            bofLen < 4 || startOff + 4 + bofLen > biffSize) {
            return false;
        }
        uint16_t subVer  = Rd16(biff + startOff + 4);
        uint16_t subType = Rd16(biff + startOff + 6);
        bool subBiff8 = (bofId == 0x0809 && subVer >= 0x0600) || isBiff8;
        if (subType != 0x0010 && subType != 0x0040 && bofId == 0x0809) {
            size_t cur = startOff + 4 + bofLen;
            int bofDepth = 1;
            while (cur + 4 <= biffSize && bofDepth > 0) {
                uint16_t rid = Rd16(biff + cur);
                uint16_t rln = Rd16(biff + cur + 2);
                cur += 4;
                if (rln > biffSize - cur) break;
                if (rid == 0x0809 || rid == 0x0409 || rid == 0x0209 || rid == 0x0009) ++bofDepth;
                else if (rid == 0x000A) --bofDepth;
                cur += rln;
            }
            outEndOff = cur;
            return false;
        }

        SpreadsheetSheet sheet;
        sheet.name = sheetName;

        auto setCell = [&](uint16_t r, uint16_t c, std::wstring val) {
            if (r >= 16384 || c >= 512) return;
            if (sheet.rows.size() <= r) sheet.rows.resize(static_cast<size_t>(r) + 1);
            if (sheet.rows[r].size() <= c) sheet.rows[r].resize(static_cast<size_t>(c) + 1);
            sheet.rows[r][c] = std::move(val);
            sheet.maxCols = std::max(sheet.maxCols, static_cast<size_t>(c) + 1);
        };

        bool hasPendingFormulaStr = false;
        uint16_t pendingFormulaRow = 0;
        uint16_t pendingFormulaCol = 0;

        size_t cur = startOff + 4 + bofLen;
        int nestedBof = 0;
        while (cur + 4 <= biffSize) {
            uint16_t rid = Rd16(biff + cur);
            uint16_t rln = Rd16(biff + cur + 2);
            cur += 4;
            if (rln > biffSize - cur) break;
            const uint8_t* rp = biff + cur;
            size_t nextCur = cur + rln;

            if (rid == 0x0809 || rid == 0x0409 || rid == 0x0209 || rid == 0x0009) {
                ++nestedBof;
                cur = nextCur;
                continue;
            }
            if (rid == 0x000A) {
                if (nestedBof > 0) {
                    --nestedBof;
                    cur = nextCur;
                    continue;
                }
                outEndOff = nextCur;
                break;
            }
            if (nestedBof > 0) {
                cur = nextCur;
                continue;
            }

            switch (rid) {
                case 0x00FD: // LABELSST
                    if (rln >= 10) {
                        uint16_t row = Rd16(rp);
                        uint16_t col = Rd16(rp + 2);
                        uint32_t idx = Rd32(rp + 6);
                        if (idx < sst.size()) setCell(row, col, sst[idx]);
                    }
                    hasPendingFormulaStr = false;
                    break;

                case 0x0204: // LABEL
                case 0x00D6: // RSTRING
                    if (rln >= 8) {
                        uint16_t row = Rd16(rp);
                        uint16_t col = Rd16(rp + 2);
                        setCell(row, col, ReadInlineBiffString(rp + 6, rln - 6, subBiff8, true));
                    }
                    hasPendingFormulaStr = false;
                    break;

                case 0x0004: // BIFF2 LABEL
                    if (rln >= 8) {
                        uint16_t row = Rd16(rp);
                        uint16_t col = Rd16(rp + 2);
                        setCell(row, col, ReadInlineBiffString(rp + 7, rln - 7, false, false));
                    }
                    hasPendingFormulaStr = false;
                    break;

                case 0x0203: // NUMBER
                    if (rln >= 14) {
                        uint16_t row = Rd16(rp);
                        uint16_t col = Rd16(rp + 2);
                        setCell(row, col, FormatXlsNumber(RdF64(rp + 6)));
                    }
                    hasPendingFormulaStr = false;
                    break;

                case 0x027E: // RK
                case 0x007E:
                    if (rln >= 10) {
                        uint16_t row = Rd16(rp);
                        uint16_t col = Rd16(rp + 2);
                        setCell(row, col, FormatXlsNumber(DecodeBiffRk(Rd32(rp + 6))));
                    }
                    hasPendingFormulaStr = false;
                    break;

                case 0x00BD: // MULRK
                    if (rln >= 6) {
                        uint16_t row = Rd16(rp);
                        uint16_t colFirst = Rd16(rp + 2);
                        uint16_t colLast  = Rd16(rp + rln - 2);
                        size_t count = (colLast >= colFirst) ? static_cast<size_t>(colLast - colFirst + 1) : 0;
                        for (size_t i = 0; i < count && 4 + (i + 1) * 6 + 2 <= rln; ++i) {
                            uint32_t rk = Rd32(rp + 4 + i * 6 + 2);
                            setCell(row, static_cast<uint16_t>(colFirst + i), FormatXlsNumber(DecodeBiffRk(rk)));
                        }
                    }
                    hasPendingFormulaStr = false;
                    break;

                case 0x0205: // BOOLERR
                    if (rln >= 8) {
                        uint16_t row = Rd16(rp);
                        uint16_t col = Rd16(rp + 2);
                        if (rp[7] == 0) setCell(row, col, rp[6] != 0 ? L"TRUE" : L"FALSE");
                        else setCell(row, col, BiffErrCodeToStr(rp[6]));
                    }
                    hasPendingFormulaStr = false;
                    break;

                case 0x0006: // FORMULA
                case 0x0406:
                case 0x0206:
                    hasPendingFormulaStr = false;
                    if (rln >= 14) {
                        uint16_t row = Rd16(rp);
                        uint16_t col = Rd16(rp + 2);
                        if (Rd16(rp + 12) != 0xFFFFu) {
                            setCell(row, col, FormatXlsNumber(RdF64(rp + 6)));
                        } else {
                            uint8_t resType = rp[6];
                            if (resType == 0x00) {
                                hasPendingFormulaStr = true;
                                pendingFormulaRow = row;
                                pendingFormulaCol = col;
                            } else if (resType == 0x01) {
                                setCell(row, col, rp[8] != 0 ? L"TRUE" : L"FALSE");
                            } else if (resType == 0x02) {
                                setCell(row, col, BiffErrCodeToStr(rp[8]));
                            } else if (resType == 0x03) {
                                setCell(row, col, L"");
                            }
                        }
                    }
                    break;

                case 0x0207: // STRING
                    if (hasPendingFormulaStr && rln >= 2) {
                        std::vector<BiffChunk> schunks;
                        schunks.push_back({ rp, rln });
                        while (nextCur + 4 <= biffSize && Rd16(biff + nextCur) == 0x003C) {
                            uint16_t cl = Rd16(biff + nextCur + 2);
                            if (cl > biffSize - (nextCur + 4)) break;
                            schunks.push_back({ biff + nextCur + 4, cl });
                            nextCur += 4 + cl;
                        }
                        if (subBiff8) {
                            SstStreamCursor scur(schunks);
                            std::wstring s;
                            if (scur.ReadRichExtendedString(s)) setCell(pendingFormulaRow, pendingFormulaCol, std::move(s));
                        } else {
                            setCell(pendingFormulaRow, pendingFormulaCol, ReadInlineBiffString(rp, rln, false, true));
                        }
                        hasPendingFormulaStr = false;
                    }
                    break;

                case 0x04BC: // SHRFMLA
                case 0x0221: // ARRAY
                case 0x0236: // TABLE
                    break;

                default:
                    hasPendingFormulaStr = false;
                    break;
            }
            cur = nextCur;
        }

        if (sheet.rows.empty()) {
            sheet.rows.push_back({ L"" });
            sheet.maxCols = 1;
        }
        outDoc.sheets.push_back(std::move(sheet));
        outEndOff = cur;
        return true;
    };

    if (!boundSheets.empty()) {
        for (const auto& bs : boundSheets) {
            if (bs.sheetType == 0x00 || bs.sheetType == 0x01) {
                size_t endOff = 0;
                parseWorksheetAt(bs.bofOffset, bs.name, endOff);
            }
        }
    } else {
        size_t scan = (firstSubType == 0x0005) ? pos : 0;
        size_t idx = 1;
        while (scan + 8 <= biffSize && outDoc.sheets.size() < 32) {
            uint16_t rid = Rd16(biff + scan);
            uint16_t rln = Rd16(biff + scan + 2);
            if (rid == 0x0809 || rid == 0x0409 || rid == 0x0209 || rid == 0x0009) {
                size_t endOff = scan;
                parseWorksheetAt(scan, L"Sheet" + std::to_wstring(idx++), endOff);
                if (endOff <= scan) break;
                scan = endOff;
            } else {
                if (4u + rln > biffSize - scan) break;
                scan += 4 + rln;
            }
        }
    }

    if (outDoc.sheets.empty()) return false;
    outDoc.kind = OfficeFileKind::ExcelXls;
    outDoc.isSpreadsheet = true;
    outDoc.formatTitle = isBiff8 ? L"Microsoft Excel 97-2003 BIFF8 (.xls)" : L"Microsoft Excel BIFF5 Workbook (.xls)";
    outDoc.valid = true;
    return true;
}

} // namespace BiffXlsDetail

bool SpreadsheetDocViewer::InspectOfficeFile(const std::wstring& fullPath, ParsedOfficeDocument& outDoc) try {
    bool ok = InspectOfficeFileRaw(fullPath, outDoc);
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

static bool InspectOfficeFileRaw(const std::wstring& fullPath, ParsedOfficeDocument& outDoc) {
    outDoc = ParsedOfficeDocument{};
    outDoc.filePath = fullPath;
    std::wstring ext = PathUtils::GetExtension(fullPath);
    if (ext == L".gz" && fullPath.size() >= 7) {
        if (PathUtils::ToLower(fullPath.substr(fullPath.size() - 7)) == L".nii.gz") {
            ext = L".nii.gz";
        }
    }

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
    if (ext == L".gpkg") {
        if (raw.size() < 100 || std::memcmp(raw.data(), "SQLite format 3\0", 16) != 0) return false;
        return ParseSqliteDatabase(raw, outDoc, /*isGeoPackage=*/true);
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

    // Legacy .xls (OLE2 BIFF8 / BIFF5 Workbook & Shared String Table parser)
    if (ext == L".xls") {
        return BiffXlsDetail::ParseBiffWorkbook(raw, outDoc);
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
