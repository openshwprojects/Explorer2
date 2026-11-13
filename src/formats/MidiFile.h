#pragma once
// MidiFile.h — Standard MIDI File (.mid / .midi) MThd/MTrk Track & Event Inspector

namespace Formats {

inline bool ParseMidiFileDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 14 || std::memcmp(raw.data(), "MThd", 4) != 0) return false;

    auto rdU16BE = [&](size_t off) -> uint16_t {
        return static_cast<uint16_t>((static_cast<uint16_t>(raw[off]) << 8) | raw[off + 1]);
    };
    auto rdU32BE = [&](size_t off) -> uint32_t {
        return (static_cast<uint32_t>(raw[off]) << 24) |
               (static_cast<uint32_t>(raw[off + 1]) << 16) |
               (static_cast<uint32_t>(raw[off + 2]) << 8) |
               static_cast<uint32_t>(raw[off + 3]);
    };

    uint16_t fmt = rdU16BE(8);
    uint16_t ntrks = rdU16BE(10);
    uint16_t division = rdU16BE(12);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Standard MIDI Music Sequence (.mid)";

    SpreadsheetSheet sheet;
    sheet.name = L"MIDI_Tracks";
    sheet.rows.push_back({ L"Chunk / Track", L"Offset", L"Size (Bytes)", L"Format / Division", L"Track Name / Note Events" });
    sheet.rows.push_back({
        L"MThd (Header)", L"0x0000", L"6",
        L"Format " + std::to_wstring(fmt) + L", " + std::to_wstring(ntrks) + L" tracks",
        std::to_wstring(division) + L" ticks/quarter-note"
    });

    size_t pos = 8 + rdU32BE(4);
    int trkIdx = 1;
    while (pos + 8 <= raw.size() && trkIdx <= 256) {
        char id[5] = {};
        std::memcpy(id, raw.data() + pos, 4);
        uint32_t len = rdU32BE(pos + 4);
        size_t dataStart = pos + 8;
        size_t dataEnd = std::min(raw.size(), dataStart + len);

        int noteCount = 0;
        std::wstring trkName;
        for (size_t i = dataStart; i + 2 < dataEnd; ++i) {
            if (raw[i] == 0xFF && raw[i + 1] == 0x03 && i + 3 <= dataEnd) {
                uint8_t slen = raw[i + 2];
                if (i + 3 + slen <= dataEnd) {
                    trkName = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + i + 3), slen));
                }
            } else if ((raw[i] & 0xF0) == 0x90 && raw[i + 2] > 0) {
                ++noteCount;
            }
        }

        wchar_t offBuf[32] = {};
        swprintf_s(offBuf, L"0x%06zX", pos);
        sheet.rows.push_back({
            PathUtils::Utf8ToWide(id) + L" #" + std::to_wstring(trkIdx++),
            offBuf,
            std::to_wstring(len),
            L"MIDI Track",
            (trkName.empty() ? L"" : (trkName + L" — ")) + std::to_wstring(noteCount) + L" NoteOn events"
        });
        pos = dataEnd;
    }

    sheet.maxCols = 5;
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
