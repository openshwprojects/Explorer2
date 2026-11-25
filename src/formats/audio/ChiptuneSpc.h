#pragma once
// ChiptuneSpc.h — Super Nintendo Sony SPC700 + S-DSP + ID666/xid6 (.spc) Structural Inspector

namespace Formats {

inline bool ParseChiptuneSpcDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 0x100 || std::memcmp(raw.data(), "SNES-SPC700", 11) != 0) return false;
    auto trimNullAscii = DetailStructuredDataDoc::TrimNullAscii;
    using DetailStructuredDataDoc::RdU16LE;
    using DetailStructuredDataDoc::RdU32LE;

    SpreadsheetSheet sh;
    sh.name = L"Chiptune_Header";
    sh.maxCols = 2;
    sh.rows.push_back({ L"Chiptune Field", L"Value" });
    out.formatTitle = L"Super Nintendo Sony SPC700 S-DSP State (.spc)";

    // SPC700 CPU Registers (0x25..0x2C)
    const uint16_t regPC = RdU16LE(raw.data() + 0x25);
    const uint8_t  regA  = raw[0x27];
    const uint8_t  regX  = raw[0x28];
    const uint8_t  regY  = raw[0x29];
    const uint8_t  regPSW = raw[0x2A];
    const uint8_t  regSP = raw[0x2B];
    wchar_t cpuBuf[128] = {};
    swprintf_s(cpuBuf, L"PC=$%04X, A=$%02X, X=$%02X, Y=$%02X, PSW=$%02X, SP=$01%02X",
        regPC, regA, regX, regY, regPSW, regSP);
    sh.rows.push_back({ L"SPC700 CPU Registers", cpuBuf });
    sh.rows.push_back({ L"SPC700 PC Register", DetailBinarySysDoc::Hex32(regPC) });

    // ID666 Tag (0x2E..0xD1) when raw[0x23] == 26 (0x1A) or printable metadata present
    const bool hasId666 = (raw[0x23] == 26u) || (raw[0x2E] >= 0x20u && raw[0x2E] <= 0x7Eu);
    if (hasId666) {
        // Detect ID666 Text vs Binary layout:
        // Text layout has ASCII date at 0x9E[11], ASCII song_sec at 0xA9[3], ASCII fade_ms at 0xAC[5], Artist at 0xB1[32]
        // Binary layout has binary u32 date at 0x9E[4], unused 0xA2[7] == 0, u24 song_sec at 0xA9[3], u32 fade_ms at 0xAC[4], Artist at 0xB0[32]
        auto isDigitOrNulOrSlash = [](const uint8_t* p, size_t n) -> bool {
            for (size_t i = 0; i < n; ++i) {
                uint8_t c = p[i];
                if (c != 0 && c != '/' && c != '-' && c != '.' && c != ' ' && (c < '0' || c > '9')) return false;
            }
            return true;
        };
        bool isBinaryTag = false;
        if (!isDigitOrNulOrSlash(raw.data() + 0x9E, 11) || !isDigitOrNulOrSlash(raw.data() + 0xA9, 8)) {
            isBinaryTag = true;
        } else if (raw[0xA2] == 0 && raw[0xA3] == 0 && raw[0xA4] == 0 && raw[0xA9] < 0x20u && (raw[0xA9] | raw[0xAA]) != 0) {
            isBinaryTag = true;
        }

        sh.rows.push_back({ L"ID666 Format", isBinaryTag ? L"Binary ID666" : L"Text ID666" });
        sh.rows.push_back({ L"Song Title", trimNullAscii(raw.data() + 0x2E, 32) });
        sh.rows.push_back({ L"Game Title", trimNullAscii(raw.data() + 0x4E, 32) });
        sh.rows.push_back({ L"Dumper", trimNullAscii(raw.data() + 0x6E, 16) });
        sh.rows.push_back({ L"Comments", trimNullAscii(raw.data() + 0x7E, 32) });

        if (!isBinaryTag) {
            std::wstring dumpDate = trimNullAscii(raw.data() + 0x9E, 11);
            std::wstring songSec  = trimNullAscii(raw.data() + 0xA9, 3);
            std::wstring fadeMs   = trimNullAscii(raw.data() + 0xAC, 5);
            sh.rows.push_back({ L"Artist", trimNullAscii(raw.data() + 0xB1, 32) });
            if (!dumpDate.empty()) sh.rows.push_back({ L"Dump Date", dumpDate });
            if (!songSec.empty())  sh.rows.push_back({ L"Song Length", songSec + L" s" });
            if (!fadeMs.empty())   sh.rows.push_back({ L"Fade Length", fadeMs + L" ms" });
            sh.rows.push_back({ L"Channel Disables", DetailBinarySysDoc::Hex32(raw[0xD1]) });
            sh.rows.push_back({ L"Emulator ID", std::to_wstring(raw[0xD2]) });
        } else {
            uint16_t yr = RdU16LE(raw.data() + 0x9E);
            uint8_t  mo = raw[0xA0], dy = raw[0xA1];
            uint32_t songSec = static_cast<uint32_t>(raw[0xA9]) | (static_cast<uint32_t>(raw[0xAA]) << 8) | (static_cast<uint32_t>(raw[0xAB]) << 16);
            uint32_t fadeMs  = RdU32LE(raw.data() + 0xAC);
            sh.rows.push_back({ L"Artist", trimNullAscii(raw.data() + 0xB0, 32) });
            if (yr >= 1990 && yr <= 2100 && mo >= 1 && mo <= 12 && dy >= 1 && dy <= 31) {
                wchar_t dbuf[32] = {};
                swprintf_s(dbuf, L"%04u-%02u-%02u", yr, mo, dy);
                sh.rows.push_back({ L"Dump Date", dbuf });
            }
            if (songSec > 0) sh.rows.push_back({ L"Song Length", std::to_wstring(songSec) + L" s" });
            if (fadeMs > 0)  sh.rows.push_back({ L"Fade Length", std::to_wstring(fadeMs) + L" ms" });
            sh.rows.push_back({ L"Channel Disables", DetailBinarySysDoc::Hex32(raw[0xD0]) });
            sh.rows.push_back({ L"Emulator ID", std::to_wstring(raw[0xD1]) });
        }
    }

    // Extended ID666 ("xid6" chunk at 0x10200)
    if (raw.size() >= 0x10208 && std::memcmp(raw.data() + 0x10200, "xid6", 4) == 0) {
        const uint32_t xidSize = RdU32LE(raw.data() + 0x10204);
        const size_t xidEnd = std::min<size_t>(raw.size(), 0x10208u + xidSize);
        size_t xp = 0x10208u;
        while (xp + 4 <= xidEnd) {
            const uint8_t id   = raw[xp];
            const uint8_t type = raw[xp + 1];
            const uint16_t d16 = RdU16LE(raw.data() + xp + 2);
            xp += 4;

            auto xidName = [](uint8_t xid) -> const wchar_t* {
                switch (xid) {
                    case 0x01: return L"xid6 Song Name";
                    case 0x02: return L"xid6 Game Name";
                    case 0x03: return L"xid6 Artist";
                    case 0x04: return L"xid6 Dumper";
                    case 0x05: return L"xid6 Date Dumped";
                    case 0x07: return L"xid6 Comments";
                    case 0x10: return L"xid6 OST Title";
                    case 0x11: return L"xid6 OST Disc";
                    case 0x12: return L"xid6 OST Track";
                    case 0x13: return L"xid6 Publisher";
                    case 0x14: return L"xid6 Copyright Year";
                    case 0x30: return L"xid6 Intro Ticks (1/64000s)";
                    case 0x31: return L"xid6 Loop Ticks (1/64000s)";
                    case 0x32: return L"xid6 End Ticks (1/64000s)";
                    case 0x33: return L"xid6 Fade Ticks (1/64000s)";
                    case 0x35: return L"xid6 Loop Count";
                    default: return nullptr;
                }
            };
            const wchar_t* label = xidName(id);
            if (type == 0) { // Inline 16-bit value in d16
                if (label) sh.rows.push_back({ label, std::to_wstring(d16) });
            } else if (type == 1) { // String of length d16 (4-byte aligned)
                if (xp + d16 > xidEnd) break;
                if (label) {
                    std::wstring sv = trimNullAscii(raw.data() + xp, d16);
                    if (!sv.empty()) sh.rows.push_back({ label, std::move(sv) });
                }
                xp += (static_cast<size_t>(d16) + 3u) & ~size_t(3u);
            } else if (type == 4) { // 32-bit integer (d16 == 4)
                if (xp + 4 > xidEnd) break;
                uint32_t v32 = RdU32LE(raw.data() + xp);
                if (label) {
                    if (id >= 0x30 && id <= 0x33) {
                        wchar_t sbuf[64] = {};
                        swprintf_s(sbuf, L"%.3f s (%u ticks)", static_cast<double>(v32) / 64000.0, v32);
                        sh.rows.push_back({ label, sbuf });
                    } else {
                        sh.rows.push_back({ label, std::to_wstring(v32) });
                    }
                }
                xp += (static_cast<size_t>(std::max<uint16_t>(d16, 4u)) + 3u) & ~size_t(3u);
            } else {
                if (xp + d16 > xidEnd) break;
                xp += (static_cast<size_t>(d16) + 3u) & ~size_t(3u);
            }
        }
    }

    // 128-byte S-DSP Register File at 0x10100 + 64 KB SPC RAM at 0x100..0x100FF
    SpreadsheetSheet voiceSh;
    voiceSh.name = L"SDSP_Voices";
    voiceSh.maxCols = 8;
    voiceSh.rows.push_back({ L"Voice", L"Vol L / R", L"14-bit Pitch (Hz)", L"SRCN Sample", L"Envelope (ADSR / GAIN)", L"KON / KOFF", L"Echo / Noise / PMod", L"BRR Sample Addr" });

    if (raw.size() >= 0x10180) {
        const uint8_t* dsp = raw.data() + 0x10100;
        const uint8_t* ram = raw.data() + 0x00100;

        const int8_t mvoll = static_cast<int8_t>(dsp[0x0C]);
        const int8_t mvolr = static_cast<int8_t>(dsp[0x1C]);
        const int8_t evoll = static_cast<int8_t>(dsp[0x2C]);
        const int8_t evolr = static_cast<int8_t>(dsp[0x3C]);
        const uint8_t kon  = dsp[0x4C];
        const uint8_t koff = dsp[0x5C];
        const uint8_t flg  = dsp[0x6C];
        const int8_t  efb  = static_cast<int8_t>(dsp[0x0D]);
        const uint8_t pmon = dsp[0x2D];
        const uint8_t non  = dsp[0x3D];
        const uint8_t eon  = dsp[0x4D];
        const uint8_t dir  = dsp[0x5D];
        const uint8_t esa  = dsp[0x6D];
        const uint8_t edl  = dsp[0x7D] & 0x0Fu;

        const uint16_t dirBase = static_cast<uint16_t>(dir) * 0x100u;
        // Count distinct valid BRR sample directory entries referenced or in first 64 slots
        size_t validBrrCount = 0;
        for (uint32_t s = 0; s < 64; ++s) {
            uint32_t entOff = static_cast<uint32_t>(dirBase) + s * 4u;
            if (entOff + 4u > 0x10000u) break;
            uint16_t brrStart = RdU16LE(ram + entOff);
            uint16_t brrLoop  = RdU16LE(ram + entOff + 2);
            if (brrStart >= 0x0200u && brrStart < 0xFF00u && brrStart + 9u <= 0x10000u &&
                (brrLoop == 0 || (brrLoop >= brrStart && brrLoop < 0xFFFCu)))
            {
                ++validBrrCount;
            }
        }

        wchar_t mvolBuf[96] = {};
        swprintf_s(mvolBuf, L"Main=(%d, %d), Echo=(%d, %d), Feedback=%d", mvoll, mvolr, evoll, evolr, efb);
        sh.rows.push_back({ L"S-DSP Master & Echo Vol", mvolBuf });

        wchar_t dirBuf[128] = {};
        swprintf_s(dirBuf, L"$%04X (DIR=$%02X, %zu valid BRR table entries); Echo Buf=$%04X (%u ms)",
            dirBase, dir, validBrrCount, static_cast<uint16_t>(esa) * 0x100u, static_cast<uint32_t>(edl) * 16u);
        sh.rows.push_back({ L"S-DSP BRR Sample Directory", dirBuf });

        std::wstring firStr;
        for (int tap = 0; tap < 8; ++tap) {
            if (tap > 0) firStr += L", ";
            firStr += std::to_wstring(static_cast<int8_t>(dsp[tap * 0x10 + 0x0F]));
        }
        sh.rows.push_back({ L"S-DSP 8-Tap Echo FIR", firStr });
        sh.rows.push_back({ L"S-DSP FLG / KON / EON", L"FLG=$" + DetailBinarySysDoc::HexBytes(&flg, 1) +
            L", KON=$" + DetailBinarySysDoc::HexBytes(&kon, 1) + L", EON=$" + DetailBinarySysDoc::HexBytes(&eon, 1) });

        for (int v = 0; v < 8; ++v) {
            const uint8_t* vr = dsp + v * 0x10;
            const int8_t voll = static_cast<int8_t>(vr[0]);
            const int8_t volr = static_cast<int8_t>(vr[1]);
            const uint16_t pitch = RdU16LE(vr + 2) & 0x3FFFu;
            const double hz = static_cast<double>(pitch) * 32000.0 / 4096.0;
            const uint8_t srcn = vr[4];
            const uint8_t adsr1 = vr[5];
            const uint8_t adsr2 = vr[6];
            const uint8_t gain  = vr[7];

            wchar_t pBuf[64] = {};
            swprintf_s(pBuf, L"$%04X (%.1f Hz)", pitch, hz);

            wchar_t envBuf[96] = {};
            if (adsr1 & 0x80u) {
                swprintf_s(envBuf, L"ADSR ($%02X/$%02X: AR=%u, DR=%u, SL=%u, SR=%u)",
                    adsr1, adsr2, adsr1 & 0x0Fu, (adsr1 >> 4) & 0x07u, (adsr2 >> 5) & 0x07u, adsr2 & 0x1Fu);
            } else {
                swprintf_s(envBuf, L"GAIN ($%02X)", gain);
            }

            std::wstring fxFlags;
            if (eon & (1u << v)) fxFlags += L"Echo ";
            if (non & (1u << v)) fxFlags += L"Noise ";
            if (pmon & (1u << v)) fxFlags += L"PMod ";
            if (fxFlags.empty()) fxFlags = L"-";
            else fxFlags = PathUtils::Trim(fxFlags);

            wchar_t brrBuf[64] = L"-";
            uint32_t entOff = static_cast<uint32_t>(dirBase) + static_cast<uint32_t>(srcn) * 4u;
            if (entOff + 4u <= 0x10000u) {
                swprintf_s(brrBuf, L"Start=$%04X, Loop=$%04X", RdU16LE(ram + entOff), RdU16LE(ram + entOff + 2));
            }

            voiceSh.rows.push_back({
                L"Voice " + std::to_wstring(v),
                std::to_wstring(voll) + L" / " + std::to_wstring(volr),
                pBuf,
                L"#" + std::to_wstring(srcn),
                envBuf,
                std::wstring((kon & (1u << v)) ? L"KON " : L"") + ((koff & (1u << v)) ? L"KOFF" : ((kon & (1u << v)) ? L"" : L"-")),
                fxFlags,
                brrBuf
            });
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.sheets.push_back(std::move(sh));
    if (voiceSh.rows.size() > 1) out.sheets.push_back(std::move(voiceSh));
    out.valid = true;
    return true;
}

} // namespace Formats
