#pragma once
// What the document readers produce and SpreadsheetDocViewer shows (tables, paragraphs, GPS tracks). No
// Windows headers beyond PlatformCompat.h: the readers also build on Linux (see linux/).
#include "PlatformCompat.h"   // <windows.h> on Windows (SourceBytes reads files with CreateFileW)
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Explorer2 {

enum class OfficeFileKind : int {
    None = 0,
    CsvTable = 1,
    ExcelXlsx = 2,
    ExcelXls = 3,
    WordDocx = 4,
    RichTextRtf = 5,
    PowerPointPptx = 6,
    EpubBook = 7,
    SqliteDb = 8,
    DbfTable = 9,
    DifSylkTable = 10
};

struct SpreadsheetSheet {
    std::wstring name;
    std::vector<std::vector<std::wstring>> rows;
    size_t maxCols = 0;
};

struct DocParagraph {
    std::wstring text;
    int headingLevel = 0; // 0 = normal, 1 = H1, 2 = H2, 3 = H3
    bool isBullet = false;
    bool isTableRow = false;
};

struct GpsTrackPoint {
    double lat = 0.0;
    double lon = 0.0;
    float elevM = 0.0f;
    float cumDistKm = 0.0f;
    int rowIndex = 0;
    std::wstring label;
};

struct ParsedOfficeDocument {
    std::wstring filePath;
    std::wstring formatTitle;
    OfficeFileKind kind = OfficeFileKind::None;
    bool isSpreadsheet = false;
    bool valid = false;

    // For Spreadsheets (.csv, .tsv, .xlsx, .xls)
    std::vector<SpreadsheetSheet> sheets;

    // For GPS / Telemetry Map Logs (.gpx, .kml, .geojson, .osm, .tcx, .nmea, .igc)
    bool hasGeoTrack = false;
    std::vector<GpsTrackPoint> geoPoints;
    uint64_t geoPointsOffered = 0; // valid points offered to AddGeoPoint (it keeps every geoPointStride-th)
    uint32_t geoPointStride = 1;
    double totalDistanceKm = 0.0;
    double totalAscentM = 0.0;

    // For Documents (.docx, .odt, .rtf)
    std::vector<DocParagraph> paragraphs;
    size_t wordCount = 0;
    size_t charCount = 0;

    // Set before a reader runs: the file's size on disk and, when the reader is handed only the start of a large
    // file, its last bytes (trailing metadata such as an ID3v1 tag or the final Ogg page)
    uint64_t sourceFileSize = 0;
    std::vector<uint8_t> sourceTail;
    bool sourceReadFully = false; // set by a reader that needed nothing past those bytes it did not read from the file itself
};

// Random access to a document's whole file, for readers handed only its start: ranges those bytes cover come from
// them, the rest from the file itself (opened on first use)
class SourceBytes {
public:
    SourceBytes(const std::vector<uint8_t>& head, const ParsedOfficeDocument& doc)
        : m_head(head), m_path(doc.filePath), m_size(std::max<uint64_t>(doc.sourceFileSize, head.size())) {}
    ~SourceBytes() {
        if (m_file != INVALID_HANDLE_VALUE) CloseHandle(m_file);
    }
    SourceBytes(const SourceBytes&) = delete;
    SourceBytes& operator=(const SourceBytes&) = delete;

    uint64_t Size() const { return m_size; }
    size_t HeadSize() const { return m_head.size(); }
    bool HasMoreThanHead() const { return !m_path.empty() && m_size > m_head.size(); }
    bool ReadPastHead() const { return m_readPastHead; }
    bool ReadFailed() const { return m_readFailed; } // a range inside the file could not be read

    // Bytes [off, off + len): a pointer into the handed bytes, or into buf after reading them from the file;
    // nullptr when the range lies outside the file or cannot be read
    const uint8_t* Read(uint64_t off, size_t len, std::vector<uint8_t>& buf) {
        if (off > m_size || len > m_size - off) return nullptr;
        if (off + len <= m_head.size()) return m_head.data() + off;
        if (!HasMoreThanHead() || m_openFailed || len > 64u * 1024 * 1024) return Failed();
        if (m_file == INVALID_HANDLE_VALUE) {
            m_file = CreateFileW(m_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (m_file == INVALID_HANDLE_VALUE) {
                m_openFailed = true;
                return Failed();
            }
        }
        LARGE_INTEGER at;
        at.QuadPart = static_cast<LONGLONG>(off);
        buf.resize(len);
        DWORD got = 0;
        if (!SetFilePointerEx(m_file, at, nullptr, FILE_BEGIN) ||
            !ReadFile(m_file, buf.data(), static_cast<DWORD>(len), &got, nullptr) || got != len) {
            return Failed();
        }
        m_readPastHead = true;
        return buf.data();
    }

    // As many of the bytes [off, off + min(len, cap)) as the file has, got set to their count: for a structure
    // whose stated size may run past the end of the file (nullptr and 0 at or past the end, or for a failed read)
    const uint8_t* ReadSome(uint64_t off, uint64_t len, uint64_t cap, std::vector<uint8_t>& buf, size_t& got) {
        got = 0;
        if (off >= m_size) return nullptr;
        const size_t want = static_cast<size_t>(std::min<uint64_t>({ len, m_size - off, cap }));
        if (want == 0) return nullptr;
        const uint8_t* p = Read(off, want, buf);
        if (p) got = want;
        return p;
    }

private:
    const uint8_t* Failed() {
        m_readFailed = true;
        return nullptr;
    }

    const std::vector<uint8_t>& m_head;
    std::wstring m_path;
    uint64_t m_size;
    HANDLE m_file = INVALID_HANDLE_VALUE;
    bool m_openFailed = false;
    bool m_readPastHead = false;
    bool m_readFailed = false;
};

// Widens a sheet's column count to its widest row
inline void FinalizeSheetCols(SpreadsheetSheet& sh) {
    for (const auto& r : sh.rows) sh.maxCols = std::max(sh.maxCols, r.size());
}

// Adds a paragraph's text to a document's counts: its characters, and its words (runs of characters above U+0020)
inline void CountDocText(ParsedOfficeDocument& doc, std::wstring_view text) {
    doc.charCount += text.size();
    bool inWord = false;
    for (wchar_t ch : text) {
        if (ch > L' ') {
            if (!inWord) {
                ++doc.wordCount;
                inWord = true;
            }
        } else {
            inWord = false;
        }
    }
}

// The counts over all of a document's paragraphs, from scratch
inline void RecountDocText(ParsedOfficeDocument& doc) {
    doc.wordCount = 0;
    doc.charCount = 0;
    for (const auto& p : doc.paragraphs) CountDocText(doc, p.text);
}

// Spreadsheet column letters: 0 -> A, 25 -> Z, 26 -> AA
inline std::wstring ColIndexToExcelLetters(int colIdx) {
    std::wstring out;
    int c = colIdx;
    while (c >= 0) {
        out.insert(out.begin(), static_cast<wchar_t>(L'A' + (c % 26)));
        c = (c / 26) - 1;
    }
    return out;
}

} // namespace Explorer2
