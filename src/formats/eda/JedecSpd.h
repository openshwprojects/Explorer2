#pragma once
// JedecSpd.h — JEDEC DDR2/DDR3/DDR4/DDR5 Serial Presence Detect EEPROM (.spd) Reader
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailHwBin {

inline std::wstring DecodeJedecMfg(uint8_t bankRaw, uint8_t codeRaw) {
    if (bankRaw == 0 && codeRaw == 0) return L"JEDEC";
    uint8_t bank = bankRaw & 0x7Fu;
    uint8_t c7   = codeRaw & 0x7Fu;
    if (bank == 0) {
        if (c7 == 0x2C) return L"Micron";
        if (c7 == 0x2D) return L"SK Hynix"; // 0xAD & 0x7F
        if (c7 == 0x4E) return L"Samsung";  // 0xCE & 0x7F
        if (c7 == 0x40 || c7 == 0x41) return L"Infineon";
    } else if (bank == 1) {
        if (c7 == 0x18) return L"Kingston"; // 0x98 & 0x7F
        if (c7 == 0x7E) return L"Elpida";
    } else if (bank == 2) {
        if (c7 == 0x1E) return L"Corsair";  // 0x9E & 0x7F
    } else if (bank == 3) {
        if (c7 == 0x0B) return L"Nanya";    // 0x8B / 0x0B
    } else if (bank == 4) {
        if (c7 == 0x4D) return L"G.Skill";  // 0xCD & 0x7F
        if (c7 == 0x4B) return L"ADATA";    // 0xCB & 0x7F
    } else if (bank == 5) {
        if (c7 == 0x1B) return L"Crucial";  // 0x9B & 0x7F
    } else if (bank == 6) {
        if (c7 == 0x43) return L"TeamGroup";
    }
    wchar_t hexBuf[32] = {};
    swprintf_s(hexBuf, L"Mfg_%02X%02X", bankRaw, codeRaw);
    return hexBuf;
}

inline int SnapJedecMtPerSec(double rawMt) {
    static const int kBins[] = {
        400, 533, 667, 800, 1066, 1333, 1600, 1866, 2133, 2400, 2666, 2933, 3200,
        3600, 4000, 4400, 4800, 5200, 5600, 6000, 6400, 6800, 7200, 7600, 8000, 8400, 8800
    };
    int rounded = static_cast<int>(std::llround(rawMt));
    for (int bin : kBins) {
        if (std::abs(rounded - bin) <= 16) return bin;
    }
    return rounded;
}

// JEDEC DDR2 / DDR3 / DDR4 / DDR5 SPD EEPROM (.spd) Parser
inline bool ParseJedecSpd(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || (size != 128 && size != 256 && size != 512 && size != 1024)) return false;

    uint8_t dramType = data[2];
    if (dramType != 0x08 && dramType != 0x0B && dramType != 0x0C && dramType != 0x0E &&
        dramType != 0x10 && dramType != 0x11 && dramType != 0x12 && dramType != 0x13)
    {
        return false;
    }
    uint8_t modType = data[3] & 0x0Fu;
    if ((data[3] & 0xE0u) != 0 || modType > 0x0Du) return false;

    out.kind = EdaDocKind::KiCadPcb;

    std::wstring genName = L"DDR4";
    std::wstring modTypeStr = L"UDIMM";
    switch (modType) {
    case 0x01: modTypeStr = L"RDIMM"; break;
    case 0x02: modTypeStr = L"UDIMM"; break;
    case 0x03: modTypeStr = L"SODIMM"; break;
    case 0x04: modTypeStr = L"LRDIMM"; break;
    case 0x0A: modTypeStr = L"DDIMM"; break;
    case 0x0B: modTypeStr = L"Soldered"; break;
    default:   modTypeStr = L"UDIMM"; break;
    }

    uint64_t dieDensityMb = 8192;
    int diePerPkg = 1;
    int sdramWidth = 8; // x4 / x8 / x16
    int ranks = 1;
    int busWidth = 64;
    double tCK_ps = 625.0;
    double tAA_ps = 13750.0;
    std::wstring mfgName = L"JEDEC";
    std::wstring partNumber;

    if (dramType == 0x12 || dramType == 0x13) { // DDR5 / LPDDR5 (JESD400-5)
        genName = (dramType == 0x13) ? L"LPDDR5" : L"DDR5";
        static const uint64_t kDdr5DensMb[16] = {
            0, 4096, 8192, 12288, 16384, 24576, 32768, 49152, 65536, 0,0,0,0,0,0,0
        };
        uint8_t dIdx = data[4] & 0x1Fu;
        dieDensityMb = (dIdx < 16 && kDdr5DensMb[dIdx] > 0) ? kDdr5DensMb[dIdx] : 16384ULL;
        uint8_t pkgRaw = (data[4] >> 5) & 0x07u;
        if (pkgRaw == 1 || pkgRaw == 2) diePerPkg = 2;
        else if (pkgRaw == 3) diePerPkg = 4;
        else if (pkgRaw == 4) diePerPkg = 8;

        // Byte 6 bits 7..5: First SDRAM I/O width (0=x4, 1=x8, 2=x16, 3=x32)
        // Note: if bytes 6, 234, 235 are all 0 (minimal synthetic DDR5 stub), default I/O width to x8
        bool minimalStub = (data[6] == 0 && (size <= 235 || (data[234] == 0 && data[235] == 0)));
        sdramWidth = minimalStub ? 8 : (4 << std::min<int>((data[6] >> 5) & 0x03u, 3));

        if (size > 235) {
            ranks = static_cast<int>((data[234] >> 3) & 0x07u) + 1;
            if (data[235] != 0) {
                int subChWidth = 8 << std::min<int>(data[235] & 0x03u, 3); // 2 -> 32 bits
                int numCh = 1 << std::min<int>((data[235] >> 5) & 0x03u, 2); // 1 -> 2 channels
                busWidth = std::clamp(subChWidth * numCh, 16, 128);
            }
        }
        uint16_t tckRaw = DetailEda100::RdU16LE(data + 20);
        uint16_t taaRaw = DetailEda100::RdU16LE(data + 30);
        tCK_ps = (tckRaw >= 100 && tckRaw <= 2000) ? static_cast<double>(tckRaw) : 416.0; // default DDR5-4800
        tAA_ps = (taaRaw >= 1000 && taaRaw <= 60000) ? static_cast<double>(taaRaw) : (tCK_ps * 40.0);

        if (size >= 552) {
            mfgName = DecodeJedecMfg(data[512], data[513]);
            if (mfgName == L"JEDEC" && size >= 554) mfgName = DecodeJedecMfg(data[552], data[553]);
            std::string pn = DetailEda100::TrimAsciiView(std::string_view(reinterpret_cast<const char*>(data + 521), 30));
            partNumber = PathUtils::Utf8ToWide(pn);
        }
    } else if (dramType == 0x0C || dramType == 0x0E || dramType == 0x10 || dramType == 0x11) { // DDR4 (JESD21-C Annex L)
        genName = (dramType == 0x10 || dramType == 0x11) ? L"LPDDR4" : L"DDR4";
        uint8_t dIdx = data[4] & 0x0Fu;
        dieDensityMb = (dIdx <= 8) ? (256ULL << dIdx) : 8192ULL;
        if ((data[6] & 0x03u) == 0x02u) diePerPkg = static_cast<int>((data[6] >> 4) & 0x07u) + 1;

        sdramWidth = (data[12] == 0 && data[13] == 0) ? 8 : (4 << std::min<int>(data[12] & 0x03u, 3));
        ranks = static_cast<int>((data[12] >> 3) & 0x07u) + 1;
        busWidth = (data[13] != 0) ? (8 << std::min<int>(data[13] & 0x07u, 3)) : 64;

        double mtbPs = 125.0, ftbPs = 1.0;
        double tck = static_cast<double>(data[18]) * mtbPs + static_cast<double>(static_cast<int8_t>(data[125])) * ftbPs;
        double taa = static_cast<double>(data[24]) * mtbPs + static_cast<double>(static_cast<int8_t>(data[123])) * ftbPs;
        tCK_ps = (tck >= 200.0 && tck <= 3000.0) ? tck : 625.0;
        tAA_ps = (taa >= 2000.0 && taa <= 50000.0) ? taa : 13750.0;

        if (size >= 349) {
            mfgName = DecodeJedecMfg(data[320], data[321]);
            if (mfgName == L"JEDEC" && size >= 352) mfgName = DecodeJedecMfg(data[350], data[351]);
            std::string pn = DetailEda100::TrimAsciiView(std::string_view(reinterpret_cast<const char*>(data + 329), 20));
            partNumber = PathUtils::Utf8ToWide(pn);
        }
    } else if (dramType == 0x0B) { // DDR3 (JESD21-C Annex K)
        genName = L"DDR3";
        uint8_t dIdx = data[4] & 0x0Fu;
        dieDensityMb = (dIdx <= 7) ? (256ULL << dIdx) : 4096ULL;
        sdramWidth = (data[7] == 0 && data[8] == 0) ? 8 : (4 << std::min<int>(data[7] & 0x03u, 3));
        ranks = static_cast<int>((data[7] >> 3) & 0x07u) + 1;
        busWidth = (data[8] != 0) ? (8 << std::min<int>(data[8] & 0x07u, 3)) : 64;

        double mtbPs = (data[10] > 0 && data[11] > 0)
            ? (1000.0 * static_cast<double>(data[10]) / static_cast<double>(data[11])) : 125.0;
        double ftbPs = ((data[9] & 0x0Fu) > 0 && (data[9] >> 4) > 0)
            ? (static_cast<double>(data[9] >> 4) / static_cast<double>(data[9] & 0x0Fu)) : 1.0;
        double tck = static_cast<double>(data[12]) * mtbPs + ((size > 34) ? static_cast<double>(static_cast<int8_t>(data[34])) * ftbPs : 0.0);
        double taa = static_cast<double>(data[16]) * mtbPs + ((size > 35) ? static_cast<double>(static_cast<int8_t>(data[35])) * ftbPs : 0.0);
        tCK_ps = (tck >= 400.0 && tck <= 5000.0) ? tck : 1250.0;
        tAA_ps = (taa >= 4000.0 && taa <= 50000.0) ? taa : 13750.0;

        if (size >= 146) {
            mfgName = DecodeJedecMfg(data[117], data[118]);
            std::string pn = DetailEda100::TrimAsciiView(std::string_view(reinterpret_cast<const char*>(data + 128), 18));
            partNumber = PathUtils::Utf8ToWide(pn);
        }
    } else { // DDR2 (0x08)
        genName = L"DDR2";
        ranks = static_cast<int>(data[5] & 0x07u) + 1;
        sdramWidth = (data[13] == 4 || data[13] == 8 || data[13] == 16) ? data[13] : 8;
        busWidth = 64;
        dieDensityMb = 1024;
        uint8_t bcd = data[9];
        double tck = static_cast<double>((bcd >> 4) * 1000 + (bcd & 0x0Fu) * 100);
        tCK_ps = (tck >= 1000.0 && tck <= 8000.0) ? tck : 2500.0;
        tAA_ps = 15000.0;
        if (size >= 91) {
            uint8_t cont = 0, code = 0;
            for (size_t k = 64; k < 72; ++k) {
                if (data[k] == 0x7F) ++cont;
                else { code = data[k]; break; }
            }
            mfgName = DecodeJedecMfg(cont, code);
            std::string pn = DetailEda100::TrimAsciiView(std::string_view(reinterpret_cast<const char*>(data + 73), 18));
            partNumber = PathUtils::Utf8ToWide(pn);
        }
    }

    int chipsPerRank = std::clamp(busWidth / std::max(4, sdramWidth), 1, 36);
    int totalDramChips = chipsPerRank * ranks;
    uint64_t totalMegabits = dieDensityMb * static_cast<uint64_t>(diePerPkg) * static_cast<uint64_t>(totalDramChips);
    double capGB = static_cast<double>(totalMegabits) / 8192.0; // 8192 Mb = 1 GB
    int mtPerSec = SnapJedecMtPerSec(2000000.0 / tCK_ps);
    int casLatency = static_cast<int>(std::ceil((tAA_ps - 0.5) / tCK_ps));

    bool isSoDimm = (modType == 0x03);
    float pcbW = isSoDimm ? 69.60f : 133.35f;
    float pcbH = isSoDimm ? 30.00f : 31.25f;
    DetailEda100::AddRectOutline(out, 0.0f, 0.0f, pcbW, pcbH, EdaLayerId::EdgeCuts, 0.50f);

    bool isDdr5 = (dramType == 0x12 || dramType == 0x13);
    wchar_t chipVal[64] = {};
    swprintf_s(chipVal, L"%s_%llux%d", genName.c_str(), static_cast<unsigned long long>(dieDensityMb / 1024), sdramWidth);

    // Place exact SDRAM IC packages per rank (Rank 0 on Top, Rank 1+ on Bottom)
    for (int rk = 0; rk < ranks && rk < 4; ++rk) {
        bool bot = (rk % 2 == 1);
        float yRow = bot ? -2.2f : 3.2f;
        float spanX = pcbW - 14.0f;
        for (int ci = 0; ci < chipsPerRank; ++ci) {
            // Leave center gap for PMIC / SPD Hub / RCD controller
            float frac = (chipsPerRank > 1) ? (static_cast<float>(ci) / static_cast<float>(chipsPerRank - 1)) : 0.5f;
            float cx = -spanX * 0.5f + frac * spanX;
            if (!isSoDimm && chipsPerRank >= 4) {
                cx += (ci < chipsPerRank / 2) ? -3.5f : 3.5f;
            }
            EdaComponent3D dram;
            dram.refDes = L"U" + std::to_wstring(rk * chipsPerRank + ci + 1);
            dram.value = chipVal;
            dram.footprint = (sdramWidth == 16) ? L"FBGA-96" : L"FBGA-78";
            dram.x = cx;
            dram.y = yRow;
            dram.widthMm = isSoDimm ? std::min(7.5f, spanX / (chipsPerRank + 1)) : std::min(9.5f, spanX / (chipsPerRank + 1));
            dram.lengthMm = 11.5f;
            dram.heightMm = 1.15f;
            dram.bottomSide = bot;
            dram.category = EdaPartCategory::IC_Package;
            out.components.push_back(dram);
        }
    }

    // Add DDR5 PMIC + SPD5118 Hub (or DDR3/DDR4 SPD EEPROM + optional RDIMM RCD)
    if (isDdr5) {
        EdaComponent3D pmic;
        pmic.refDes = L"U_PMIC";
        pmic.value = L"PMIC5100";
        pmic.footprint = L"QFN-36";
        pmic.x = 0.0f; pmic.y = 5.5f;
        pmic.widthMm = 5.0f; pmic.lengthMm = 5.0f; pmic.heightMm = 1.3f;
        pmic.category = EdaPartCategory::IC_Package;
        out.components.push_back(pmic);

        EdaComponent3D hub;
        hub.refDes = L"U_SPD_HUB";
        hub.value = L"SPD5118";
        hub.footprint = L"DFN-8";
        hub.x = 0.0f; hub.y = -2.5f;
        hub.widthMm = 3.0f; hub.lengthMm = 2.0f; hub.heightMm = 0.9f;
        hub.category = EdaPartCategory::IC_Package;
        out.components.push_back(hub);
    } else {
        EdaComponent3D spdIc;
        spdIc.refDes = L"U_SPD";
        spdIc.value = L"EE1004";
        spdIc.footprint = L"TDFN-8";
        spdIc.x = 0.0f; spdIc.y = -4.0f;
        spdIc.widthMm = 3.0f; spdIc.lengthMm = 2.0f; spdIc.heightMm = 0.9f;
        spdIc.category = EdaPartCategory::IC_Package;
        out.components.push_back(spdIc);
    }

    // Gold edge connector fingers
    int nFingers = isSoDimm ? 26 : 44;
    float fPitch = (pcbW - 8.0f) / static_cast<float>(nFingers);
    int keyIdx = isDdr5 ? (nFingers / 2 - 2) : (nFingers / 2 + 3);
    for (int f = 0; f < nFingers; ++f) {
        if (f == keyIdx) continue;
        float fx = -(pcbW - 8.0f) * 0.5f + (static_cast<float>(f) + 0.5f) * fPitch;
        out.pads.push_back({ fx, -pcbH * 0.5f + 1.4f, fPitch * 0.62f, 2.4f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, L"", L"" });
    }

    wchar_t capStr[32] = {};
    if (capGB >= 1.0 && std::fabs(capGB - std::round(capGB)) < 0.05) {
        swprintf_s(capStr, L"%d GB", static_cast<int>(std::llround(capGB)));
    } else {
        swprintf_s(capStr, L"%.1f GB", capGB);
    }

    wchar_t summary[240] = {};
    swprintf_s(summary, L"%s %s-%d CL%d %s (%dRx%d, %s%s%s)",
        capStr, genName.c_str(), mtPerSec, casLatency, modTypeStr.c_str(),
        ranks, sdramWidth, mfgName.c_str(),
        partNumber.empty() ? L"" : L" ",
        partNumber.c_str());

    out.formatTitle = std::wstring(L"JEDEC SPD — ") + summary;
    out.labels.push_back({ 0.0f, pcbH * 0.5f + 2.5f, 1.25f, EdaLayerId::SchLabel, summary });
    out.netCount = static_cast<size_t>(totalDramChips);
    return true;
}

} // namespace DetailHwBin

inline bool ParseJedecSpdBoard(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 16) return false;
    return DetailHwBin::ParseJedecSpd(data, size, out);
}

} // namespace Formats
