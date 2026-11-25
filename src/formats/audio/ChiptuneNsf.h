#pragma once
// ChiptuneNsf.h — Nintendo NES 2A03 / Expansion Chiptune (.nsf & .nsfe) Structural Inspector

namespace Formats {
namespace DetailChiptuneNsf {

inline std::wstring FormatNsfExpansionChips(uint8_t exp) {
    std::wstring chips = L"Ricoh 2A03 APU";
    if (exp & 0x01u) chips += L" + Konami VRC6";
    if (exp & 0x02u) chips += L" + Konami VRC7 (FM)";
    if (exp & 0x04u) chips += L" + Famicom Disk System (FDS)";
    if (exp & 0x08u) chips += L" + Nintendo MMC5";
    if (exp & 0x10u) chips += L" + Namco 163 (Wavetable)";
    if (exp & 0x20u) chips += L" + Sunsoft 5B (YM2149F)";
    return chips;
}

inline std::wstring FormatNsfRegion(uint8_t reg) {
    if (reg & 0x02u) return L"Dual NTSC / PAL";
    return (reg & 0x01u) ? L"PAL (50 Hz)" : L"NTSC (60 Hz)";
}

} // namespace DetailChiptuneNsf

inline bool ParseChiptuneNsfDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using DetailStructuredDataDoc::RdU16LE;
    using DetailStructuredDataDoc::RdU32LE;
    using DetailStructuredDataDoc::RdI32LE;

    // 1. Extended Nintendo Sound Format (.nsfe — "NSFE" RIFF-like chunk stream)
    if (raw.size() >= 16 && std::memcmp(raw.data(), "NSFE", 4) == 0) {
        SpreadsheetSheet sh;
        sh.name = L"Chiptune_Header";
        sh.maxCols = 2;
        sh.rows.push_back({ L"Chiptune Field", L"Value" });

        bool sawInfo = false;
        uint8_t totalSongs = 0, startSong = 0;
        std::vector<std::wstring> trackTitles;
        std::vector<int32_t> trackTimesMs;
        std::vector<int32_t> trackFadesMs;
        std::vector<uint8_t> playlist;

        auto splitNulUtf8 = [](const uint8_t* p, size_t n, std::vector<std::wstring>& dst) {
            size_t pos = 0;
            while (pos < n) {
                size_t end = pos;
                while (end < n && p[end] != 0) ++end;
                dst.push_back(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p + pos), end - pos)));
                pos = end + 1;
            }
        };

        size_t pos = 4;
        while (pos + 8 <= raw.size()) {
            const uint32_t csz = RdU32LE(raw.data() + pos);
            std::string cid(reinterpret_cast<const char*>(raw.data() + pos + 4), 4);
            pos += 8;
            if (cid == "NEND") break;
            if (csz > raw.size() - pos) return false;
            const uint8_t* cp = raw.data() + pos;
            pos += csz;

            if (cid == "INFO" && csz >= 8) {
                sawInfo = true;
                const uint16_t loadAddr = RdU16LE(cp + 0);
                const uint16_t initAddr = RdU16LE(cp + 2);
                const uint16_t playAddr = RdU16LE(cp + 4);
                const uint8_t  regFlags = cp[6];
                const uint8_t  expFlags = cp[7];
                totalSongs = (csz >= 9) ? cp[8] : 1u;
                startSong  = (csz >= 10) ? cp[9] : 0u;

                wchar_t addrs[96] = {};
                swprintf_s(addrs, L"Load=$%04X, Init=$%04X, Play=$%04X", loadAddr, initAddr, playAddr);
                sh.rows.push_back({ L"Container Format", L"NSFE (Extended Nintendo Sound Format)" });
                sh.rows.push_back({ L"6502 Entry Addresses", addrs });
                sh.rows.push_back({ L"Region Standard", DetailChiptuneNsf::FormatNsfRegion(regFlags) });
                sh.rows.push_back({ L"Sound Chips", DetailChiptuneNsf::FormatNsfExpansionChips(expFlags) });
                sh.rows.push_back({ L"Total Songs", std::to_wstring(totalSongs) + L" (Start #" + std::to_wstring(startSong + 1u) + L")" });
            } else if (cid == "DATA") {
                sh.rows.push_back({ L"6502 Program ROM Size", PathUtils::FormatByteSize(csz) });
            } else if (cid == "BANK" && csz >= 8) {
                sh.rows.push_back({ L"4KB Bankswitch Init", DetailBinarySysDoc::HexBytes(cp, 8, L' ') });
            } else if (cid == "RATE" && csz >= 2) {
                uint16_t ntscUs = RdU16LE(cp);
                if (ntscUs > 0) {
                    wchar_t rbuf[64] = {};
                    swprintf_s(rbuf, L"%u us (%.2f Hz)", ntscUs, 1000000.0 / static_cast<double>(ntscUs));
                    sh.rows.push_back({ L"NTSC Play Speed", rbuf });
                }
                if (csz >= 4 && RdU16LE(cp + 2) > 0) {
                    uint16_t palUs = RdU16LE(cp + 2);
                    wchar_t rbuf[64] = {};
                    swprintf_s(rbuf, L"%u us (%.2f Hz)", palUs, 1000000.0 / static_cast<double>(palUs));
                    sh.rows.push_back({ L"PAL Play Speed", rbuf });
                }
            } else if (cid == "auth") {
                std::vector<std::wstring> authFields;
                splitNulUtf8(cp, csz, authFields);
                static const wchar_t* const kAuthLabels[4] = { L"Title", L"Artist", L"Copyright", L"NSFE Ripper" };
                for (size_t i = 0; i < std::min<size_t>(authFields.size(), 4); ++i) {
                    if (!authFields[i].empty()) sh.rows.push_back({ kAuthLabels[i], authFields[i] });
                }
            } else if (cid == "tlbl") {
                splitNulUtf8(cp, csz, trackTitles);
            } else if (cid == "time") {
                for (size_t i = 0; i + 4 <= csz; i += 4) trackTimesMs.push_back(RdI32LE(cp + i));
            } else if (cid == "fade") {
                for (size_t i = 0; i + 4 <= csz; i += 4) trackFadesMs.push_back(RdI32LE(cp + i));
            } else if (cid == "plst") {
                playlist.assign(cp, cp + csz);
            } else if (cid == "text" && csz > 0) {
                std::wstring note = PathUtils::Trim(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(cp), std::min<size_t>(csz, 1024))));
                if (!note.empty()) sh.rows.push_back({ L"Notes", std::move(note) });
            }
        }
        if (!sawInfo) return false;

        if (!playlist.empty()) {
            std::wstring plStr;
            for (size_t i = 0; i < std::min<size_t>(playlist.size(), 32); ++i) {
                if (i > 0) plStr += L", ";
                plStr += L"#" + std::to_wstring(playlist[i] + 1u);
            }
            sh.rows.push_back({ L"Playlist Order (plst)", plStr });
        }

        SpreadsheetSheet trkSh;
        trkSh.name = L"NSFE_Tracks";
        trkSh.maxCols = 4;
        trkSh.rows.push_back({ L"Track #", L"Title", L"Duration", L"Fade" });
        const size_t nTrk = std::max<size_t>(totalSongs, std::max(trackTitles.size(), trackTimesMs.size()));
        for (size_t i = 0; i < std::min<size_t>(nTrk, 512); ++i) {
            std::wstring nm = (i < trackTitles.size() && !trackTitles[i].empty())
                ? trackTitles[i] : (L"Track " + std::to_wstring(i + 1));
            std::wstring dur = L"-";
            if (i < trackTimesMs.size() && trackTimesMs[i] >= 0) {
                int32_t ms = trackTimesMs[i];
                wchar_t dbuf[48] = {};
                swprintf_s(dbuf, L"%d:%02d.%03d", ms / 60000, (ms / 1000) % 60, ms % 1000);
                dur = dbuf;
            }
            std::wstring fade = L"-";
            if (i < trackFadesMs.size() && trackFadesMs[i] >= 0) {
                fade = std::to_wstring(trackFadesMs[i]) + L" ms";
            }
            trkSh.rows.push_back({ std::to_wstring(i + 1), std::move(nm), std::move(dur), std::move(fade) });
        }

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Extended Nintendo Sound Format (.nsfe)";
        out.sheets.push_back(std::move(sh));
        if (trkSh.rows.size() > 1) out.sheets.push_back(std::move(trkSh));
        out.valid = true;
        return true;
    }

    // 2. Classic NSF / NSF2 ("NESM\x1A", 128-byte header)
    if (raw.size() < 0x80 || std::memcmp(raw.data(), "NESM\x1A", 5) != 0) return false;
    auto trimNullAscii = DetailStructuredDataDoc::TrimNullAscii;

    SpreadsheetSheet sh;
    sh.name = L"Chiptune_Header";
    sh.maxCols = 2;
    sh.rows.push_back({ L"Chiptune Field", L"Value" });

    const uint8_t  nsfVer    = raw[5];
    const uint8_t  totalSngs = raw[6];
    const uint8_t  startSng  = raw[7];
    const uint16_t loadAddr  = RdU16LE(raw.data() + 0x08);
    const uint16_t initAddr  = RdU16LE(raw.data() + 0x0A);
    const uint16_t playAddr  = RdU16LE(raw.data() + 0x0C);
    const uint16_t ntscUs    = RdU16LE(raw.data() + 0x6E);
    const uint16_t palUs     = RdU16LE(raw.data() + 0x78);
    const uint8_t  regFlags  = raw[0x7A];
    const uint8_t  expFlags  = raw[0x7B];
    const uint32_t prgLen24  = static_cast<uint32_t>(raw[0x7D]) |
                               (static_cast<uint32_t>(raw[0x7E]) << 8) |
                               (static_cast<uint32_t>(raw[0x7F]) << 16);

    out.formatTitle = (nsfVer == 2)
        ? L"Nintendo NES NSF2 Chiptune (.nsf)"
        : L"Nintendo NES 2A03 / Expansion Chiptune (.nsf)";

    sh.rows.push_back({ L"NSF Version", std::to_wstring(nsfVer) });
    sh.rows.push_back({ L"Title", trimNullAscii(raw.data() + 0x0E, 32) });
    sh.rows.push_back({ L"Artist", trimNullAscii(raw.data() + 0x2E, 32) });
    sh.rows.push_back({ L"Copyright", trimNullAscii(raw.data() + 0x4E, 32) });
    sh.rows.push_back({ L"Total Songs", std::to_wstring(totalSngs) + L" (Start #" + std::to_wstring(startSng) + L")" });

    wchar_t addrs[96] = {};
    swprintf_s(addrs, L"Load=$%04X, Init=$%04X, Play=$%04X", loadAddr, initAddr, playAddr);
    sh.rows.push_back({ L"6502 Entry Addresses", addrs });

    if (ntscUs > 0) {
        wchar_t nbuf[64] = {};
        swprintf_s(nbuf, L"%u us (%.2f Hz)", ntscUs, 1000000.0 / static_cast<double>(ntscUs));
        sh.rows.push_back({ L"NTSC Play Speed", nbuf });
    }
    if (palUs > 0) {
        wchar_t pbuf[64] = {};
        swprintf_s(pbuf, L"%u us (%.2f Hz)", palUs, 1000000.0 / static_cast<double>(palUs));
        sh.rows.push_back({ L"PAL Play Speed", pbuf });
    }

    bool usesBankswitch = false;
    for (size_t b = 0; b < 8; ++b) {
        if (raw[0x70 + b] != 0) usesBankswitch = true;
    }
    sh.rows.push_back({ L"4KB Bankswitch Init ($5FF8..$5FFF)",
        usesBankswitch ? DetailBinarySysDoc::HexBytes(raw.data() + 0x70, 8, L' ') : L"Disabled (Flat $8000..$FFFF)" });
    sh.rows.push_back({ L"Region Standard", DetailChiptuneNsf::FormatNsfRegion(regFlags) });
    sh.rows.push_back({ L"Sound Chips", DetailChiptuneNsf::FormatNsfExpansionChips(expFlags) });
    sh.rows.push_back({ L"Program ROM Data", PathUtils::FormatByteSize(prgLen24 > 0 ? prgLen24 : (raw.size() - 0x80u)) });

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}

} // namespace Formats
