#pragma once
// ChiptuneVgm.h — Video Game Music & Gzipped VGZ Chip Register Log (.vgm / .vgz) Structural Inspector

namespace Formats {

inline bool ParseChiptuneVgmDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::vector<uint8_t> inflated;
    const uint8_t* vgm = raw.data();
    size_t vgmSize = raw.size();
    bool isVgz = false;

    if (raw.size() >= 18 && raw[0] == 0x1Fu && raw[1] == 0x8Bu) {
        if (!ZipDeflate::InflateGzip(raw.data(), raw.size(), inflated, 64u * 1024u * 1024u) || inflated.size() < 0x40) {
            return false;
        }
        vgm = inflated.data();
        vgmSize = inflated.size();
        isVgz = true;
    }

    if (vgmSize < 0x40 || std::memcmp(vgm, "Vgm ", 4) != 0) return false;
    using DetailStructuredDataDoc::RdU16LE;
    using DetailStructuredDataDoc::RdU32LE;

    SpreadsheetSheet sh;
    sh.name = L"Chiptune_Header";
    sh.maxCols = 2;
    sh.rows.push_back({ L"Chiptune Field", L"Value" });
    out.formatTitle = isVgz
        ? L"Compressed Video Game Music Chip Log (.vgz)"
        : L"Video Game Music Chip Register Log (.vgm)";

    const uint32_t eofRel   = RdU32LE(vgm + 0x04);
    const uint32_t verBcd   = RdU32LE(vgm + 0x08);
    const uint32_t gd3Rel   = RdU32LE(vgm + 0x14);
    const uint32_t samples  = RdU32LE(vgm + 0x18);
    const uint32_t loopRel  = RdU32LE(vgm + 0x1C);
    const uint32_t loopSmp  = RdU32LE(vgm + 0x20);
    const uint32_t rateHz   = (verBcd >= 0x101u) ? RdU32LE(vgm + 0x24) : 0u;
    const uint32_t dataRel  = (verBcd >= 0x150u) ? RdU32LE(vgm + 0x34) : 0u;
    const size_t   dataOff  = (verBcd >= 0x150u && dataRel > 0) ? (0x34u + static_cast<size_t>(dataRel)) : 0x40u;
    if (dataOff > vgmSize) return false;

    wchar_t vbuf[32] = {};
    swprintf_s(vbuf, L"%X.%02X", (verBcd >> 8) & 0xFFu, verBcd & 0xFFu);
    sh.rows.push_back({ L"VGM Version", vbuf });
    if (isVgz) {
        sh.rows.push_back({ L"VGZ Compression", PathUtils::FormatByteSize(raw.size()) + L" -> " + PathUtils::FormatByteSize(vgmSize) });
    }
    if (eofRel > 0) sh.rows.push_back({ L"Declared VGM Size", PathUtils::FormatByteSize(4ULL + eofRel) });

    wchar_t durBuf[96] = {};
    swprintf_s(durBuf, L"%u.%02u s (%u samples)", samples / 44100u, (samples % 44100u) / 441u, samples);
    sh.rows.push_back({ L"Duration (@44.1 kHz)", durBuf });
    if (loopRel > 0 && loopSmp > 0) {
        wchar_t lpBuf[96] = {};
        swprintf_s(lpBuf, L"%.2f s (%u samples @ offset 0x%X)", static_cast<double>(loopSmp) / 44100.0, loopSmp, 0x1Cu + loopRel);
        sh.rows.push_back({ L"Loop Region", lpBuf });
    }
    if (rateHz > 0) sh.rows.push_back({ L"Recording Frame Rate", std::to_wstring(rateHz) + L" Hz" });

    // Decode all ~28 standard VGM v1.00 - v1.71 chip clock fields bounded by header length (dataOff) & version
    struct VgmChipSpec { uint32_t minVer; size_t hdrOff; const wchar_t* label; };
    static const VgmChipSpec kChips[] = {
        { 0x100u, 0x0Cu, L"TI SN76489 PSG Clock" },
        { 0x100u, 0x10u, L"Yamaha YM2413 OPLL Clock" },
        { 0x110u, 0x2Cu, L"Yamaha YM2612 OPN2 FM Clock" },
        { 0x110u, 0x30u, L"Yamaha YM2151 OPM Clock" },
        { 0x151u, 0x38u, L"Sega PCM Clock" },
        { 0x151u, 0x40u, L"Ricoh RF5C68 PCM Clock" },
        { 0x151u, 0x44u, L"Yamaha YM2203 OPN Clock" },
        { 0x151u, 0x48u, L"Yamaha YM2608 OPNA Clock" },
        { 0x151u, 0x4Cu, L"Yamaha YM2610/B OPNB Clock" },
        { 0x151u, 0x50u, L"Yamaha YM3812 OPL2 Clock" },
        { 0x151u, 0x54u, L"Yamaha YM3526 OPL Clock" },
        { 0x151u, 0x58u, L"Yamaha Y8950 MSX-AUDIO Clock" },
        { 0x151u, 0x5Cu, L"Yamaha YMF262 OPL3 Clock" },
        { 0x151u, 0x60u, L"Yamaha YMF278B OPL4 Clock" },
        { 0x151u, 0x64u, L"Yamaha YMF271 OPX Clock" },
        { 0x151u, 0x68u, L"Yamaha YMZ280B PCMD8 Clock" },
        { 0x151u, 0x6Cu, L"Ricoh RF5C164 MegaCD PCM Clock" },
        { 0x151u, 0x70u, L"Sega 32X PWM Clock" },
        { 0x151u, 0x74u, L"General Instrument AY-3-8910 Clock" },
        { 0x161u, 0x80u, L"Nintendo GameBoy DMG APU Clock" },
        { 0x161u, 0x84u, L"Nintendo NES 2A03 APU Clock" },
        { 0x161u, 0x88u, L"Yamaha YMW258 MultiPCM Clock" },
        { 0x161u, 0x8Cu, L"NEC uPD7759 ADPCM Clock" },
        { 0x161u, 0x90u, L"OKI MSM6258 ADPCM Clock" },
        { 0x161u, 0x98u, L"OKI MSM6295 ADPCM Clock" },
        { 0x161u, 0x9Cu, L"Konami K051649 SCC Clock" },
        { 0x161u, 0xA0u, L"Konami K054539 PCM Clock" },
        { 0x161u, 0xA4u, L"Hudson HuC6280 PC Engine Clock" },
        { 0x161u, 0xA8u, L"Namco C140 PCM Clock" },
        { 0x161u, 0xACu, L"Konami K053260 Clock" },
        { 0x161u, 0xB0u, L"Atari POKEY Clock" },
        { 0x161u, 0xB4u, L"Capcom QSound DSP Clock" },
        { 0x171u, 0xB8u, L"Yamaha YMF292 SCSP Clock" },
        { 0x171u, 0xC0u, L"Bandai WonderSwan Audio Clock" },
        { 0x171u, 0xC4u, L"Nintendo Virtual Boy VSU Clock" },
        { 0x171u, 0xC8u, L"Philips SAA1099 Clock" },
    };
    for (const auto& cs : kChips) {
        if (verBcd < cs.minVer || cs.hdrOff + 4u > dataOff || cs.hdrOff + 4u > vgmSize) continue;
        uint32_t rawClk = RdU32LE(vgm + cs.hdrOff);
        if (rawClk == 0) continue;
        const bool dualChip = (rawClk & 0x40000000u) != 0;
        const uint32_t hz = rawClk & 0x3FFFFFFFu;
        sh.rows.push_back({ cs.label, std::to_wstring(hz) + L" Hz" + (dualChip ? L" (Dual-Chip 2x)" : L"") });
    }

    // Parse full 11-field UTF-16LE GD3 metadata block
    const size_t gd3Abs = (gd3Rel > 0) ? (0x14u + static_cast<size_t>(gd3Rel)) : vgmSize;
    if (gd3Rel > 0 && gd3Abs + 12u <= vgmSize && std::memcmp(vgm + gd3Abs, "Gd3 ", 4) == 0) {
        const uint32_t gd3Len = RdU32LE(vgm + gd3Abs + 8);
        const size_t gd3End = std::min<size_t>(vgmSize, gd3Abs + 12u + gd3Len);
        size_t gp = gd3Abs + 12u;
        static const wchar_t* const kGd3Names[11] = {
            L"Track (EN)", L"Track (JP)", L"Game (EN)", L"Game (JP)",
            L"System (EN)", L"System (JP)", L"Author (EN)", L"Author (JP)",
            L"Release Date", L"VGM Ripper", L"GD3 Notes"
        };
        for (int t = 0; t < 11 && gp + 2u <= gd3End; ++t) {
            size_t endW = gp;
            while (endW + 2u <= gd3End && RdU16LE(vgm + endW) != 0) endW += 2u;
            std::wstring s = DetailStructuredDataDoc::ReadUtf16LE(vgm + gp, endW - gp);
            gp = std::min<size_t>(gd3End, endW + 2u);
            if (!s.empty()) sh.rows.push_back({ kGd3Names[t], std::move(s) });
        }
    }

    // Walk VGM command stream to summarize per-chip register writes, PCM data blocks & wait samples
    std::map<std::wstring, uint64_t> chipWrites;
    uint64_t dataBlocks = 0, dataBlockBytes = 0, waitSamples = 0, totalCmds = 0;
    const size_t cmdLimit = std::min<size_t>(vgmSize, gd3Rel > 0 ? gd3Abs : vgmSize);
    size_t cp = dataOff;
    while (cp < cmdLimit) {
        const uint8_t op = vgm[cp++];
        ++totalCmds;
        if (op == 0x66u) break; // End of sound data
        else if (op == 0x4Fu || op == 0x50u || op == 0x30u) {
            if (cp + 1 > cmdLimit) break;
            ++cp;
            chipWrites[L"SN76489 PSG"]++;
        } else if (op >= 0x31u && op <= 0x3Fu) {
            if (cp + 1 > cmdLimit) break;
            ++cp;
        } else if (op >= 0x40u && op <= 0x4Eu) {
            if (cp + 2 > cmdLimit) break;
            cp += 2;
        } else if (op >= 0x51u && op <= 0x5Fu) {
            if (cp + 2 > cmdLimit) break;
            cp += 2;
            static const wchar_t* const k5x[15] = {
                L"YM2413 OPLL", L"YM2612 OPN2 (Port 0)", L"YM2612 OPN2 (Port 1)", L"YM2151 OPM",
                L"YM2203 OPN", L"YM2608 OPNA (Port 0)", L"YM2608 OPNA (Port 1)", L"YM2610 (Port 0)",
                L"YM2610 (Port 1)", L"YM3812 OPL2", L"YM3526 OPL", L"Y8950",
                L"YMZ280B", L"YMF262 OPL3 (Port 0)", L"YMF262 OPL3 (Port 1)"
            };
            chipWrites[k5x[op - 0x51u]]++;
        } else if (op == 0x61u) {
            if (cp + 2 > cmdLimit) break;
            waitSamples += RdU16LE(vgm + cp);
            cp += 2;
        } else if (op == 0x62u) {
            waitSamples += 735;
        } else if (op == 0x63u) {
            waitSamples += 882;
        } else if (op == 0x64u) {
            if (cp + 3 > cmdLimit) break;
            cp += 3;
        } else if (op == 0x67u) { // Data block: 0x67 0x66 <type> <u32 size> <bytes>
            if (cp + 6 > cmdLimit) break;
            uint32_t bsz = RdU32LE(vgm + cp + 2) & 0x7FFFFFFFu;
            cp += 6;
            if (bsz > cmdLimit - cp) break;
            cp += bsz;
            ++dataBlocks;
            dataBlockBytes += bsz;
        } else if (op == 0x68u) { // PCM RAM write (11 bytes)
            if (cp + 11 > cmdLimit) break;
            cp += 11;
        } else if (op >= 0x70u && op <= 0x7Fu) {
            waitSamples += (op & 0x0Fu) + 1u;
        } else if (op >= 0x80u && op <= 0x8Fu) {
            waitSamples += (op & 0x0Fu);
            chipWrites[L"YM2612 DAC Sample"]++;
        } else if (op == 0x90u || op == 0x91u || op == 0x95u) {
            if (cp + 4 > cmdLimit) break;
            cp += 4;
            chipWrites[L"DAC Stream Control"]++;
        } else if (op == 0x92u) {
            if (cp + 5 > cmdLimit) break;
            cp += 5;
            chipWrites[L"DAC Stream Control"]++;
        } else if (op == 0x93u) {
            if (cp + 10 > cmdLimit) break;
            cp += 10;
            chipWrites[L"DAC Stream Control"]++;
        } else if (op == 0x94u) {
            if (cp + 1 > cmdLimit) break;
            ++cp;
            chipWrites[L"DAC Stream Control"]++;
        } else if (op >= 0xA0u && op <= 0xBFu) {
            if (cp + 2 > cmdLimit) break;
            cp += 2;
            if (op == 0xA0u) chipWrites[L"AY-3-8910 PSG"]++;
            else if (op == 0xB3u) chipWrites[L"GameBoy DMG APU"]++;
            else if (op == 0xB4u) chipWrites[L"NES 2A03 APU"]++;
            else if (op == 0xB7u) chipWrites[L"OKI MSM6258"]++;
            else if (op == 0xB8u) chipWrites[L"OKI MSM6295"]++;
            else if (op == 0xB9u) chipWrites[L"HuC6280 PC Engine"]++;
            else if (op == 0xBBu) chipWrites[L"Atari POKEY"]++;
            else if (op == 0xBCu) chipWrites[L"WonderSwan Audio"]++;
            else if (op == 0xBDu) chipWrites[L"Philips SAA1099"]++;
            else chipWrites[L"2-Byte Chip Write (0xA0..0xBF)"]++;
        } else if (op >= 0xC0u && op <= 0xDFu) {
            if (cp + 3 > cmdLimit) break;
            cp += 3;
            if (op == 0xC0u) chipWrites[L"Sega PCM Memory"]++;
            else if (op == 0xC4u) chipWrites[L"Capcom QSound"]++;
            else if (op == 0xD2u) chipWrites[L"Konami K051649 SCC"]++;
            else if (op == 0xD4u) chipWrites[L"Namco C140"]++;
            else chipWrites[L"3-Byte Chip Write (0xC0..0xDF)"]++;
        } else if (op >= 0xE0u) {
            if (cp + 4 > cmdLimit) break;
            cp += 4;
            if (op == 0xE0u) chipWrites[L"PCM Data Bank Seek (0xE0)"]++;
            else chipWrites[L"4-Byte Chip Write (0xE1..0xFF)"]++;
        } else {
            break;
        }
    }

    SpreadsheetSheet cmdSh;
    cmdSh.name = L"VGM_Command_Stats";
    cmdSh.maxCols = 2;
    cmdSh.rows.push_back({ L"Command / Chip Stream", L"Count / Summary" });
    cmdSh.rows.push_back({ L"Total Commands Walked", std::to_wstring(totalCmds) });
    cmdSh.rows.push_back({ L"Accumulated Wait Samples", std::to_wstring(waitSamples) });
    if (dataBlocks > 0) {
        cmdSh.rows.push_back({ L"PCM / ROM Data Blocks (0x67)", std::to_wstring(dataBlocks) + L" block(s), " + PathUtils::FormatByteSize(dataBlockBytes) });
    }
    for (const auto& [chipName, cnt] : chipWrites) {
        cmdSh.rows.push_back({ chipName, std::to_wstring(cnt) + L" writes" });
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.sheets.push_back(std::move(sh));
    if (cmdSh.rows.size() > 1) out.sheets.push_back(std::move(cmdSh));
    out.valid = true;
    return true;
}

} // namespace Formats
