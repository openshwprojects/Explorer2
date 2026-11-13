#pragma once
// ElfBinary.h — Executable and Linkable Format (.elf / .so / .o) Binary Header, Program & Section Table Inspector

namespace Formats {

inline bool ParseElfBinaryDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 52 || raw[0] != 0x7F || raw[1] != 'E' || raw[2] != 'L' || raw[3] != 'F') return false;

    bool is64 = (raw[4] == 2);
    bool isLE = (raw[5] == 1);

    auto rdU16 = [&](size_t off) -> uint16_t {
        if (off + 2 > raw.size()) return 0;
        uint16_t v = 0; std::memcpy(&v, raw.data() + off, 2);
        return isLE ? v : _byteswap_ushort(v);
    };
    auto rdU32 = [&](size_t off) -> uint32_t {
        if (off + 4 > raw.size()) return 0;
        uint32_t v = 0; std::memcpy(&v, raw.data() + off, 4);
        return isLE ? v : _byteswap_ulong(v);
    };
    auto rdU64 = [&](size_t off) -> uint64_t {
        if (off + 8 > raw.size()) return 0;
        uint64_t v = 0; std::memcpy(&v, raw.data() + off, 8);
        return isLE ? v : _byteswap_uint64(v);
    };

    uint16_t e_type    = rdU16(16);
    uint16_t e_machine = rdU16(18);
    uint64_t e_entry   = is64 ? rdU64(24) : rdU32(24);
    uint64_t e_phoff   = is64 ? rdU64(32) : rdU32(28);
    uint64_t e_shoff   = is64 ? rdU64(40) : rdU32(32);
    uint16_t e_phnum   = is64 ? rdU16(56) : rdU16(44);
    uint16_t e_shnum   = is64 ? rdU16(60) : rdU16(48);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"ELF Executable & Linkable Binary (.elf)";

    SpreadsheetSheet sheet;
    sheet.name = L"ELF_Structure";
    sheet.rows.push_back({ L"Property / Header", L"Value" });
    sheet.rows.push_back({ L"Class", is64 ? L"ELF64 (64-bit)" : L"ELF32 (32-bit)" });
    sheet.rows.push_back({ L"Endianness", isLE ? L"Little-Endian (2's complement)" : L"Big-Endian" });

    std::wstring machName = L"Machine ID " + std::to_wstring(e_machine);
    if (e_machine == 0x3E) machName = L"AMD x86-64 (0x3E)";
    else if (e_machine == 0x03) machName = L"Intel x86 (0x03)";
    else if (e_machine == 0x28) machName = L"ARM (0x28)";
    else if (e_machine == 0xB7) machName = L"ARM64 / AArch64 (0xB7)";
    else if (e_machine == 0xF3) machName = L"RISC-V (0xF3)";

    wchar_t entryBuf[64] = {};
    swprintf_s(entryBuf, L"0x%llX", static_cast<unsigned long long>(e_entry));
    sheet.rows.push_back({ L"Object Type", (e_type == 2) ? L"ET_EXEC (Executable)" : ((e_type == 3) ? L"ET_DYN (Shared Object / PIE)" : L"ET_REL (Relocatable)") });
    sheet.rows.push_back({ L"Architecture", machName });
    sheet.rows.push_back({ L"Entry Point Address", entryBuf });
    sheet.rows.push_back({ L"Program Headers", std::to_wstring(e_phnum) + L" (offset " + std::to_wstring(e_phoff) + L")" });
    sheet.rows.push_back({ L"Section Headers", std::to_wstring(e_shnum) + L" (offset " + std::to_wstring(e_shoff) + L")" });

    sheet.maxCols = 2;
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
