#pragma once
// Uf2Firmware.h — USB Flashing Format (.uf2 — 512B Microcontroller Blocks)

namespace Formats {

// ============================================================================
// 50. USB Flashing Format (.uf2 — "UF2\n" 512-byte Microcontroller Blocks)
// ============================================================================
inline bool ParseUf2Firmware(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 512 || std::memcmp(raw.data(), "UF2\n", 4) != 0) return false;

    const size_t totalBlocks = raw.size() / 512;
    uint32_t minAddr = 0xFFFFFFFFu, maxAddr = 0;
    uint64_t totalPayloadBytes = 0;
    uint32_t familyId = DetailBinarySysDoc::RdU32LE(raw.data() + 28);
    uint32_t hdrNumBlocks = DetailBinarySysDoc::RdU32LE(raw.data() + 24);
    uint64_t fnvHash = 14695981039346656037ULL;
    std::wstring extDeviceDesc, extFwVer;

    for (size_t b = 0; b < totalBlocks && b < 65536; ++b) {
        const uint8_t* blk = raw.data() + b * 512;
        if (std::memcmp(blk, "UF2\n", 4) != 0) break;
        const uint32_t flags = DetailBinarySysDoc::RdU32LE(blk + 8);
        const uint32_t addr  = DetailBinarySysDoc::RdU32LE(blk + 12);
        const uint32_t paySz = std::min<uint32_t>(DetailBinarySysDoc::RdU32LE(blk + 16), 476u);
        if (familyId == 0 && (flags & 0x2000u) != 0) familyId = DetailBinarySysDoc::RdU32LE(blk + 28);

        minAddr = std::min(minAddr, addr);
        maxAddr = std::max(maxAddr, addr + paySz);
        totalPayloadBytes += paySz;

        for (uint32_t i = 0; i < paySz; ++i) {
            fnvHash ^= blk[32 + i];
            fnvHash *= 1099511628211ULL;
        }

        // Optional UF2 extension tags after payload when flags & 0x8000
        if ((flags & 0x8000u) != 0 && paySz < 476) {
            size_t tp = 32 + ((paySz + 3u) & ~3u);
            while (tp + 4 <= 508) {
                const uint8_t tLen = blk[tp];
                if (tLen < 4 || tp + tLen > 508) break;
                const uint32_t tagId = static_cast<uint32_t>(blk[tp + 1]) |
                                       (static_cast<uint32_t>(blk[tp + 2]) << 8) |
                                       (static_cast<uint32_t>(blk[tp + 3]) << 16);
                std::wstring tStr = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(blk + tp + 4), tLen - 4));
                if (tagId == 0x650D9Du && extDeviceDesc.empty()) extDeviceDesc = tStr;
                else if (tagId == 0x9FC7BCu && extFwVer.empty()) extFwVer = tStr;
                tp += (static_cast<size_t>(tLen) + 3u) & ~size_t(3u);
            }
        }
    }

    if (minAddr == 0xFFFFFFFFu) minAddr = DetailBinarySysDoc::RdU32LE(raw.data() + 12);
    if (hdrNumBlocks == 0) hdrNumBlocks = static_cast<uint32_t>(totalBlocks);

    std::wstring famName = DetailBinarySysDoc::Hex32(familyId);
    if (familyId == 0xE48BFF56u) famName = L"Raspberry Pi RP2040 (0xE48BFF56)";
    else if (familyId == 0xE48BFF59u) famName = L"Raspberry Pi RP2350 ARM-S (0xE48BFF59)";
    else if (familyId == 0xE48BFF5Au) famName = L"Raspberry Pi RP2350 RISC-V (0xE48BFF5A)";
    else if (familyId == 0xADA52840u) famName = L"Nordic nRF52840 (0xADA52840)";
    else if (familyId == 0x57755A57u) famName = L"STM32F4xx (0x57755A57)";
    else if (familyId == 0xBFDD4EEEu) famName = L"Espressif ESP32-S2 (0xBFDD4EEE)";
    else if (familyId == 0xC47E5767u) famName = L"Espressif ESP32-S3 (0xC47E5767)";

    SpreadsheetSheet sh;
    sh.name = L"UF2_Firmware";
    sh.maxCols = 2;
    sh.rows = {
        { L"UF2 Firmware Property", L"Value" },
        { L"Target MCU Family", famName },
        { L"Base Flash Address", DetailBinarySysDoc::Hex32(minAddr) },
        { L"End Flash Address", DetailBinarySysDoc::Hex32(maxAddr) },
        { L"Total 512B Blocks", std::to_wstring(hdrNumBlocks) + L" (" + std::to_wstring(totalBlocks) + L" present)" },
        { L"Total Firmware Payload", PathUtils::FormatByteSize(totalPayloadBytes) },
        { L"Payload FNV-1a Digest", DetailBinarySysDoc::Hex64(fnvHash) },
        { L"Initial Vector Bytes", DetailBinarySysDoc::HexBytes(raw.data() + 32, 16, L' ') }
    };
    if (!extDeviceDesc.empty()) sh.rows.push_back({ L"Device Description Tag", extDeviceDesc });
    if (!extFwVer.empty())      sh.rows.push_back({ L"Firmware Version Tag", extFwVer });

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"UF2 Microcontroller Bootloader Firmware (.uf2)";
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}


} // namespace Formats
