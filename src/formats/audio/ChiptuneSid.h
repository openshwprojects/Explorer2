#pragma once
// ChiptuneSid.h — Commodore 64 PSID/RSID v1-v4 MOS 6581/8580 Multi-SID (.sid) Structural Inspector

namespace Formats {

inline bool ParseChiptuneSidDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 0x76 || (std::memcmp(raw.data(), "PSID", 4) != 0 && std::memcmp(raw.data(), "RSID", 4) != 0)) {
        return false;
    }
    const bool isRsid = (std::memcmp(raw.data(), "RSID", 4) == 0);
    using DetailStructuredDataDoc::RdU16BE;
    using DetailStructuredDataDoc::RdU32BE;
    using DetailStructuredDataDoc::RdU16LE;

    const uint16_t version    = RdU16BE(raw.data() + 0x04);
    const uint16_t dataOffset = RdU16BE(raw.data() + 0x06);
    if (version < 1 || version > 4 || dataOffset < 0x76) return false;
    if (version >= 2 && (dataOffset < 0x7C || raw.size() < 0x7C)) return false;

    uint16_t loadAddr = RdU16BE(raw.data() + 0x08);
    const uint16_t initAddr  = RdU16BE(raw.data() + 0x0A);
    const uint16_t playAddr  = RdU16BE(raw.data() + 0x0C);
    const uint16_t songs     = RdU16BE(raw.data() + 0x0E);
    const uint16_t startSong = RdU16BE(raw.data() + 0x10);
    const uint32_t speedBits = RdU32BE(raw.data() + 0x12);

    bool embeddedLoad = false;
    if (loadAddr == 0 && static_cast<size_t>(dataOffset) + 2u <= raw.size()) {
        loadAddr = RdU16LE(raw.data() + dataOffset);
        embeddedLoad = true;
    }

    auto readSidCp1252 = [&](size_t off) -> std::wstring {
        size_t n = 0;
        while (n < 32 && raw[off + n] != 0) ++n;
        return PathUtils::Trim(DetailMediaAndScoreDoc::Latin1OrCp1252ToWide(raw.data() + off, n));
    };

    SpreadsheetSheet sh;
    sh.name = L"Chiptune_Header";
    sh.maxCols = 2;
    sh.rows.push_back({ L"Chiptune Field", L"Value" });
    out.formatTitle = L"Commodore 64 MOS 6581/8580 SID Music (.sid)";

    sh.rows.push_back({ L"Container Magic", std::wstring(isRsid ? L"RSID (Real C64 IRQ/DMA Environment) v" : L"PSID (PlaySID) v") + std::to_wstring(version) });
    sh.rows.push_back({ L"Title", readSidCp1252(0x16) });
    sh.rows.push_back({ L"Author", readSidCp1252(0x36) });
    sh.rows.push_back({ L"Released", readSidCp1252(0x56) });
    sh.rows.push_back({ L"Sub-Songs", std::to_wstring(songs) + L" (Start #" + std::to_wstring(startSong) + L")" });

    wchar_t addrs[128] = {};
    swprintf_s(addrs, L"Load=$%04X%s, Init=$%04X, Play=$%04X",
        loadAddr, embeddedLoad ? L" (PRG header)" : L"", initAddr, playAddr);
    sh.rows.push_back({ L"6510 Addresses", addrs });
    sh.rows.push_back({ L"Timer Speed Mask", speedBits == 0 ? L"VBI (50/60 Hz Raster)" : (L"CIA 1 Timer A (Mask=" + DetailBinarySysDoc::Hex32(speedBits) + L")") });

    if (version >= 2 && raw.size() >= 0x7C) {
        const uint16_t flags = RdU16BE(raw.data() + 0x76);
        const uint8_t  startPage = raw[0x78];
        const uint8_t  pageLen   = raw[0x79];
        const uint8_t  sid2Byte  = raw[0x7A];
        const uint8_t  sid3Byte  = raw[0x7B];

        auto sidModelName = [](uint16_t bits2) -> std::wstring {
            switch (bits2 & 0x03u) {
                case 1: return L"MOS 6581";
                case 2: return L"MOS 8580";
                case 3: return L"MOS 6581 & MOS 8580";
                default: return L"Unspecified";
            }
        };
        auto videoStandard = [](uint16_t bits2) -> std::wstring {
            switch (bits2 & 0x03u) {
                case 1: return L"PAL (50 Hz, 985248 Hz Φ2)";
                case 2: return L"NTSC (60 Hz, 1022727 Hz Φ2)";
                case 3: return L"PAL & NTSC Dual";
                default: return L"Unknown";
            }
        };
        auto fmtSidIoAddr = [](uint8_t b) -> std::wstring {
            if (b >= 0x42u && b <= 0xFEu && (b & 1u) == 0 && (b < 0x80u || b >= 0xE0u)) {
                wchar_t abuf[32] = {};
                swprintf_s(abuf, L"$D%02X0", b);
                return abuf;
            }
            return {};
        };

        std::wstring playerFmt = (flags & 0x01u) ? L"Compute!'s Sidplayer MUS" : L"Built-in 6510 Machine Code";
        if (flags & 0x02u) {
            playerFmt += isRsid ? L" + C64 BASIC ROM Required" : L" + PlaySID 4th-Voice Samples";
        }
        sh.rows.push_back({ L"Player Environment", playerFmt });
        sh.rows.push_back({ L"Video Standard", videoStandard((flags >> 2) & 0x03u) });
        sh.rows.push_back({ L"Primary SID ($D400)", sidModelName((flags >> 4) & 0x03u) });

        if (version >= 3) {
            std::wstring s2Addr = fmtSidIoAddr(sid2Byte);
            if (!s2Addr.empty()) {
                sh.rows.push_back({ L"2nd SID (" + s2Addr + L")", sidModelName((flags >> 6) & 0x03u) });
            }
        }
        if (version >= 4) {
            std::wstring s3Addr = fmtSidIoAddr(sid3Byte);
            if (!s3Addr.empty()) {
                sh.rows.push_back({ L"3rd SID (" + s3Addr + L")", sidModelName((flags >> 8) & 0x03u) });
            }
        }

        if (startPage > 0 && pageLen > 0) {
            wchar_t rbuf[96] = {};
            swprintf_s(rbuf, L"$%02X00..$%02XFF (%u pages)", startPage, static_cast<uint32_t>(startPage) + pageLen - 1u, pageLen);
            sh.rows.push_back({ L"Driver Relocation Pages", rbuf });
        }
    }

    if (raw.size() > dataOffset) {
        sh.rows.push_back({ L"C64 Payload Size", PathUtils::FormatByteSize(raw.size() - dataOffset) });
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}

} // namespace Formats
