#pragma once
// TrackerXm.h — FastTracker II Extended Module (.xm) Inspector

namespace Formats {
namespace DetailMediaAndScoreDoc {

inline bool ParseTrackerXm(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 80 || std::memcmp(raw.data(), "Extended Module: ", 17) != 0 || raw[37] != 0x1A) return false;

    std::wstring modName     = FixedAsciiField(raw.data() + 17, 20);
    std::wstring trackerName = FixedAsciiField(raw.data() + 38, 20);
    uint16_t xmVer       = DetailBinarySysDoc::RdU16LE(raw.data() + 58);
    uint32_t hdrSize     = DetailBinarySysDoc::RdU32LE(raw.data() + 60);
    uint16_t songLen     = DetailBinarySysDoc::RdU16LE(raw.data() + 64);
    uint16_t restartPos  = DetailBinarySysDoc::RdU16LE(raw.data() + 66);
    uint16_t numChans    = DetailBinarySysDoc::RdU16LE(raw.data() + 68);
    uint16_t numPats     = DetailBinarySysDoc::RdU16LE(raw.data() + 70);
    uint16_t numInsts    = DetailBinarySysDoc::RdU16LE(raw.data() + 72);
    uint16_t flags       = DetailBinarySysDoc::RdU16LE(raw.data() + 74);
    uint16_t defTempo    = DetailBinarySysDoc::RdU16LE(raw.data() + 76);
    uint16_t defBpm      = DetailBinarySysDoc::RdU16LE(raw.data() + 78);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"FastTracker II Extended Module (.xm)";

    SpreadsheetSheet hdr;
    hdr.name = L"XM_Header";
    hdr.rows.push_back({ L"Property", L"Value" });
    hdr.rows.push_back({ L"Module Title", modName.empty() ? L"(Untitled)" : modName });
    hdr.rows.push_back({ L"Tracker", trackerName });
    hdr.rows.push_back({ L"XM Version", std::to_wstring(xmVer >> 8) + L"." + std::to_wstring(xmVer & 0xFF) });
    hdr.rows.push_back({ L"Channels", std::to_wstring(numChans) });
    hdr.rows.push_back({ L"Song Length / Restart", std::to_wstring(songLen) + L" / " + std::to_wstring(restartPos) });
    hdr.rows.push_back({ L"Patterns / Instruments", std::to_wstring(numPats) + L" / " + std::to_wstring(numInsts) });
    hdr.rows.push_back({ L"Default Tempo / BPM", std::to_wstring(defTempo) + L" / " + std::to_wstring(defBpm) });
    hdr.rows.push_back({ L"Frequency Table", (flags & 1u) ? L"Linear" : L"Amiga" });
    FinalizeSheetCols(hdr);

    SpreadsheetSheet patSh;
    patSh.name = L"Patterns";
    patSh.rows.push_back({ L"Pattern #", L"Rows", L"Packed Bytes" });

    size_t pos = 60ULL + hdrSize;
    for (uint16_t p = 0; p < numPats && pos + 9 <= raw.size(); ++p) {
        uint32_t pHdrLen = DetailBinarySysDoc::RdU32LE(raw.data() + pos);
        uint16_t nRows   = DetailBinarySysDoc::RdU16LE(raw.data() + pos + 5);
        uint16_t pkSize  = DetailBinarySysDoc::RdU16LE(raw.data() + pos + 7);
        patSh.rows.push_back({ std::to_wstring(p), std::to_wstring(nRows), std::to_wstring(pkSize) });
        if (pHdrLen < 7 || pos + pHdrLen + pkSize > raw.size()) { pos = raw.size(); break; }
        pos += pHdrLen + pkSize;
    }
    FinalizeSheetCols(patSh);

    SpreadsheetSheet insSh;
    insSh.name = L"Instruments";
    insSh.rows.push_back({ L"Inst #", L"Instrument Name", L"Num Samples", L"Total Sample Bytes" });
    for (uint16_t i = 0; i < numInsts && pos + 29 <= raw.size(); ++i) {
        uint32_t iSize = DetailBinarySysDoc::RdU32LE(raw.data() + pos);
        if (iSize < 29 || pos + iSize > raw.size()) break;
        std::wstring iName = FixedAsciiField(raw.data() + pos + 4, 22);
        uint16_t nSmp = DetailBinarySysDoc::RdU16LE(raw.data() + pos + 27);
        uint32_t smpHdrSize = (nSmp > 0 && iSize >= 33) ? DetailBinarySysDoc::RdU32LE(raw.data() + pos + 29) : 40;
        if (smpHdrSize == 0 || smpHdrSize > 4096) smpHdrSize = 40;
        pos += iSize;

        uint64_t totalSmpBytes = 0;
        for (uint16_t s = 0; s < nSmp && pos + smpHdrSize <= raw.size(); ++s) {
            if (smpHdrSize >= 4) totalSmpBytes += DetailBinarySysDoc::RdU32LE(raw.data() + pos);
            pos += smpHdrSize;
        }
        insSh.rows.push_back({ std::to_wstring(i + 1), iName, std::to_wstring(nSmp), std::to_wstring(totalSmpBytes) });
        if (totalSmpBytes > raw.size() - pos) { pos = raw.size(); break; }
        pos += static_cast<size_t>(totalSmpBytes);
    }
    FinalizeSheetCols(insSh);

    out.sheets.push_back(std::move(hdr));
    if (patSh.rows.size() > 1) out.sheets.push_back(std::move(patSh));
    if (insSh.rows.size() > 1) out.sheets.push_back(std::move(insSh));
    out.valid = true;
    return true;
}

} // namespace DetailMediaAndScoreDoc

inline bool ParseTrackerXmDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMediaAndScoreDoc::ParseTrackerXm(raw, out); }

} // namespace Formats
