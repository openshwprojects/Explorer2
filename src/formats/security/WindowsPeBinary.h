#pragma once
// WindowsPeBinary.h — Windows PE32 / PE32+ Portable Executable (.exe/.dll/.sys/.ocx/.scr/.efi)

namespace Formats {

// ============================================================================
// 37. Windows PE32 / PE32+ Portable Executable (.exe / .dll / .sys / .ocx / .scr / .efi)
// ============================================================================
inline bool ParseWindowsPeBinary(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 64 || raw[0] != 'M' || raw[1] != 'Z') return false;
    const uint32_t peOff = DetailBinarySysDoc::RdU32LE(raw.data() + 60);
    if (peOff + 24 > raw.size() || std::memcmp(raw.data() + peOff, "PE\0\0", 4) != 0) return false;

    const uint8_t* coff = raw.data() + peOff + 4;
    const uint16_t machine      = DetailBinarySysDoc::RdU16LE(coff + 0);
    const uint16_t numSections  = DetailBinarySysDoc::RdU16LE(coff + 2);
    const uint32_t timeStamp    = DetailBinarySysDoc::RdU32LE(coff + 4);
    const uint16_t optHdrSize   = DetailBinarySysDoc::RdU16LE(coff + 16);
    const uint16_t chars        = DetailBinarySysDoc::RdU16LE(coff + 18);

    std::wstring arch = L"Unknown (" + DetailBinarySysDoc::Hex32(machine) + L")";
    if (machine == 0x8664) arch = L"x64 / AMD64 (0x8664)";
    else if (machine == 0x014C) arch = L"x86 / i386 (0x014C)";
    else if (machine == 0xAA64) arch = L"ARM64 / AArch64 (0xAA64)";
    else if (machine == 0x01C4) arch = L"ARM Thumb-2 (0x01C4)";
    else if (machine == 0x5064) arch = L"RISC-V 64-bit (0x5064)";

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Windows PE32/PE32+ Executable Inspector";

    SpreadsheetSheet hdrSheet;
    hdrSheet.name = L"PE_Headers";
    hdrSheet.maxCols = 2;
    hdrSheet.rows.push_back({ L"PE Header Field", L"Value" });
    hdrSheet.rows.push_back({ L"Target Architecture", arch });
    hdrSheet.rows.push_back({ L"File Type", (chars & 0x2000) ? L"Dynamic-Link Library (DLL)" : L"Executable Image (EXE/SYS)" });
    hdrSheet.rows.push_back({ L"Section Count", std::to_wstring(numSections) });
    hdrSheet.rows.push_back({ L"Compiler Timestamp", DetailBinarySysDoc::FormatUnixTimeUtc(timeStamp) });

    bool isPe64 = false;
    uint32_t expRva = 0, expSize = 0;
    uint32_t impRva = 0, impSize = 0;

    const size_t optOff = peOff + 24;
    if (optHdrSize >= 24 && optOff + optHdrSize <= raw.size()) {
        const uint16_t optMagic = DetailBinarySysDoc::RdU16LE(raw.data() + optOff);
        if (optMagic != 0x010B && optMagic != 0x020B && optMagic != 0x0107) return false;
        isPe64 = (optMagic == 0x020B);
        const uint32_t entryRva = DetailBinarySysDoc::RdU32LE(raw.data() + optOff + 16);
        const uint64_t imgBase  = (isPe64 && optHdrSize >= 32) ? DetailBinarySysDoc::RdU64LE(raw.data() + optOff + 24)
                                                               : ((optHdrSize >= 32) ? DetailBinarySysDoc::RdU32LE(raw.data() + optOff + 28) : 0);
        const uint16_t subsys   = optHdrSize >= 70 ? DetailBinarySysDoc::RdU16LE(raw.data() + optOff + 68) : 0;
        const uint16_t dllChars = optHdrSize >= 72 ? DetailBinarySysDoc::RdU16LE(raw.data() + optOff + 70) : 0;

        std::wstring subsysName = std::to_wstring(subsys);
        switch (subsys) {
            case 1:  subsysName = L"NATIVE Driver (1)"; break;
            case 2:  subsysName = L"WINDOWS_GUI (2)"; break;
            case 3:  subsysName = L"WINDOWS_CUI Console (3)"; break;
            case 10: subsysName = L"EFI_APPLICATION (10)"; break;
            case 11: subsysName = L"EFI_BOOT_SERVICE_DRIVER (11)"; break;
            case 12: subsysName = L"EFI_RUNTIME_DRIVER (12)"; break;
        }

        hdrSheet.rows.push_back({ L"PE Format", isPe64 ? L"PE32+ (64-bit)" : L"PE32 (32-bit)" });
        hdrSheet.rows.push_back({ L"Entry Point RVA", DetailBinarySysDoc::Hex32(entryRva) });
        hdrSheet.rows.push_back({ L"Image Base", DetailBinarySysDoc::Hex64(imgBase) });
        hdrSheet.rows.push_back({ L"Subsystem", subsysName });

        std::wstring secFlags;
        if (dllChars & 0x0020) secFlags += L"HighEntropyVA ";
        if (dllChars & 0x0040) secFlags += L"ASLR ";
        if (dllChars & 0x0100) secFlags += L"DEP/NX ";
        if (dllChars & 0x0400) secFlags += L"NoSEH ";
        if (dllChars & 0x4000) secFlags += L"CFG ";
        hdrSheet.rows.push_back({ L"Security Mitigations", secFlags.empty() ? L"None" : PathUtils::Trim(secFlags) });

        const size_t ddCountOff = isPe64 ? (optOff + 108) : (optOff + 92);
        const size_t ddTableOff = isPe64 ? (optOff + 112) : (optOff + 96);
        if (ddTableOff <= optOff + optHdrSize && ddCountOff + 4 <= raw.size()) {
            const uint32_t numRva = std::min<uint32_t>(DetailBinarySysDoc::RdU32LE(raw.data() + ddCountOff), 16u);
            if (numRva >= 1 && ddTableOff + 8 <= optOff + optHdrSize && ddTableOff + 8 <= raw.size()) {
                expRva  = DetailBinarySysDoc::RdU32LE(raw.data() + ddTableOff + 0);
                expSize = DetailBinarySysDoc::RdU32LE(raw.data() + ddTableOff + 4);
            }
            if (numRva >= 2 && ddTableOff + 16 <= optOff + optHdrSize && ddTableOff + 16 <= raw.size()) {
                impRva  = DetailBinarySysDoc::RdU32LE(raw.data() + ddTableOff + 8);
                impSize = DetailBinarySysDoc::RdU32LE(raw.data() + ddTableOff + 12);
            }
        }
    }

    struct PeSec { uint32_t vaddr, vsize, rawPtr, rawSize, flags; std::string name; };
    std::vector<PeSec> secs;
    uint32_t minRawPtr = 0xFFFFFFFFu;

    SpreadsheetSheet secSheet;
    secSheet.name = L"Sections";
    secSheet.maxCols = 6;
    secSheet.rows.push_back({ L"Section Name", L"Virtual Address", L"Virtual Size", L"Raw Offset", L"Raw Size", L"Flags" });

    const size_t secOff = optOff + optHdrSize;
    if (numSections > 0 && secOff + static_cast<size_t>(numSections) * 40 <= raw.size()) {
        for (uint16_t i = 0; i < numSections && i < 128; ++i) {
            const uint8_t* s = raw.data() + secOff + static_cast<size_t>(i) * 40;
            PeSec ps;
            ps.name    = std::string(reinterpret_cast<const char*>(s), strnlen(reinterpret_cast<const char*>(s), 8));
            ps.vsize   = DetailBinarySysDoc::RdU32LE(s + 8);
            ps.vaddr   = DetailBinarySysDoc::RdU32LE(s + 12);
            ps.rawSize = DetailBinarySysDoc::RdU32LE(s + 16);
            ps.rawPtr  = DetailBinarySysDoc::RdU32LE(s + 20);
            ps.flags   = DetailBinarySysDoc::RdU32LE(s + 36);
            if (ps.rawPtr > 0 && ps.rawPtr < minRawPtr) minRawPtr = ps.rawPtr;
            secs.push_back(ps);

            std::wstring perm;
            if (ps.flags & 0x40000000u) perm += L"R";
            if (ps.flags & 0x80000000u) perm += L"W";
            if (ps.flags & 0x20000000u) perm += L"X";
            if (ps.flags & 0x00000020u) perm += L" [CODE]";
            secSheet.rows.push_back({
                PathUtils::Utf8ToWide(ps.name),
                DetailBinarySysDoc::Hex32(ps.vaddr),
                PathUtils::FormatByteSize(ps.vsize),
                DetailBinarySysDoc::Hex32(ps.rawPtr),
                PathUtils::FormatByteSize(ps.rawSize),
                perm
            });
        }
    }

    auto rvaToOff = [&](uint32_t rva) -> size_t {
        if (rva == 0) return raw.size();
        for (const auto& s : secs) {
            const uint32_t span = std::max(s.vsize, s.rawSize);
            if (rva >= s.vaddr && rva < s.vaddr + span) {
                const uint64_t delta = rva - s.vaddr;
                if (delta < s.rawSize && static_cast<uint64_t>(s.rawPtr) + delta < raw.size()) {
                    return static_cast<size_t>(s.rawPtr + delta);
                }
                return raw.size();
            }
        }
        if (rva < minRawPtr && rva < raw.size()) return rva;
        return raw.size();
    };

    // Parse Import Table (DataDirectory[1])
    SpreadsheetSheet impSheet;
    impSheet.name = L"Imports";
    impSheet.maxCols = 3;
    impSheet.rows.push_back({ L"DLL Module", L"Hint / Ordinal", L"Imported Symbol" });
    size_t importedDllCount = 0, importedSymCount = 0;

    if (impRva > 0) {
        size_t dOff = rvaToOff(impRva);
        for (size_t di = 0; di < 256 && dOff + 20 <= raw.size(); ++di, dOff += 20) {
            const uint32_t origThunk = DetailBinarySysDoc::RdU32LE(raw.data() + dOff + 0);
            const uint32_t nameRva   = DetailBinarySysDoc::RdU32LE(raw.data() + dOff + 12);
            const uint32_t firstThk  = DetailBinarySysDoc::RdU32LE(raw.data() + dOff + 16);
            if (origThunk == 0 && nameRva == 0 && firstThk == 0) break;
            if (impSize > 0 && di * 20 >= impSize) break;

            const size_t nameOff = rvaToOff(nameRva);
            const std::string dllStr = DetailBinarySysDoc::ReadCStrBounded(raw, nameOff, 256);
            if (dllStr.empty()) continue;
            const std::wstring dllWide = PathUtils::Utf8ToWide(dllStr);
            ++importedDllCount;

            const uint32_t thunkRva = (origThunk != 0) ? origThunk : firstThk;
            size_t tOff = rvaToOff(thunkRva);
            const size_t thunkSz = isPe64 ? 8 : 4;
            bool anyInDll = false;

            for (size_t ti = 0; ti < 4096 && tOff + thunkSz <= raw.size() && impSheet.rows.size() <= 2000; ++ti, tOff += thunkSz) {
                const uint64_t val = isPe64 ? DetailBinarySysDoc::RdU64LE(raw.data() + tOff)
                                            : DetailBinarySysDoc::RdU32LE(raw.data() + tOff);
                if (val == 0) break;
                const bool byOrdinal = isPe64 ? ((val & (1ULL << 63)) != 0) : ((val & 0x80000000u) != 0);
                if (byOrdinal) {
                    const uint16_t ord = static_cast<uint16_t>(val & 0xFFFFu);
                    impSheet.rows.push_back({ dllWide, L"Ordinal #" + std::to_wstring(ord), L"(by ordinal)" });
                    ++importedSymCount;
                    anyInDll = true;
                } else {
                    const uint32_t hnRva = static_cast<uint32_t>(val & 0x7FFFFFFFu);
                    const size_t hnOff = rvaToOff(hnRva);
                    if (hnOff + 3 <= raw.size()) {
                        const uint16_t hint = DetailBinarySysDoc::RdU16LE(raw.data() + hnOff);
                        const std::string fn = DetailBinarySysDoc::ReadCStrBounded(raw, hnOff + 2, 256);
                        impSheet.rows.push_back({ dllWide, std::to_wstring(hint), PathUtils::Utf8ToWide(fn) });
                        ++importedSymCount;
                        anyInDll = true;
                    }
                }
            }
            if (!anyInDll && impSheet.rows.size() <= 2000) {
                impSheet.rows.push_back({ dllWide, L"-", L"(import descriptor)" });
            }
        }
    }

    // Parse Export Table (DataDirectory[0])
    SpreadsheetSheet expSheet;
    expSheet.name = L"Exports";
    expSheet.maxCols = 4;
    expSheet.rows.push_back({ L"Export Module", L"Ordinal", L"Exported Name", L"Entry RVA / Forwarder" });
    size_t exportedSymCount = 0;

    if (expRva > 0) {
        const size_t eOff = rvaToOff(expRva);
        if (eOff + 40 <= raw.size()) {
            const uint32_t expNameRva   = DetailBinarySysDoc::RdU32LE(raw.data() + eOff + 12);
            const uint32_t ordBase      = DetailBinarySysDoc::RdU32LE(raw.data() + eOff + 16);
            const uint32_t numFuncs     = std::min<uint32_t>(DetailBinarySysDoc::RdU32LE(raw.data() + eOff + 20), 16384u);
            const uint32_t numNames     = std::min<uint32_t>(DetailBinarySysDoc::RdU32LE(raw.data() + eOff + 24), 16384u);
            const uint32_t funcsRva     = DetailBinarySysDoc::RdU32LE(raw.data() + eOff + 28);
            const uint32_t namesRva     = DetailBinarySysDoc::RdU32LE(raw.data() + eOff + 32);
            const uint32_t ordsRva      = DetailBinarySysDoc::RdU32LE(raw.data() + eOff + 36);

            const std::string modNameStr = DetailBinarySysDoc::ReadCStrBounded(raw, rvaToOff(expNameRva), 256);
            const std::wstring modWide   = modNameStr.empty() ? L"(module)" : PathUtils::Utf8ToWide(modNameStr);

            const size_t funcsOff = rvaToOff(funcsRva);
            const size_t namesOff = rvaToOff(namesRva);
            const size_t ordsOff  = rvaToOff(ordsRva);

            std::vector<uint8_t> covered(numFuncs, 0);
            if (namesOff < raw.size() && ordsOff < raw.size()) {
                for (uint32_t i = 0; i < numNames && expSheet.rows.size() <= 2000; ++i) {
                    if (namesOff + static_cast<size_t>(i) * 4 + 4 > raw.size() ||
                        ordsOff + static_cast<size_t>(i) * 2 + 2 > raw.size()) break;
                    const uint32_t fnRva  = DetailBinarySysDoc::RdU32LE(raw.data() + namesOff + static_cast<size_t>(i) * 4);
                    const uint16_t ordIdx = DetailBinarySysDoc::RdU16LE(raw.data() + ordsOff + static_cast<size_t>(i) * 2);
                    const std::string fnStr = DetailBinarySysDoc::ReadCStrBounded(raw, rvaToOff(fnRva), 256);

                    uint32_t symRva = 0;
                    if (ordIdx < numFuncs && funcsOff + static_cast<size_t>(ordIdx) * 4 + 4 <= raw.size()) {
                        symRva = DetailBinarySysDoc::RdU32LE(raw.data() + funcsOff + static_cast<size_t>(ordIdx) * 4);
                        covered[ordIdx] = 1;
                    }
                    std::wstring targetStr = DetailBinarySysDoc::Hex32(symRva);
                    if (expSize > 0 && symRva >= expRva && symRva < expRva + expSize) {
                        const std::string fwd = DetailBinarySysDoc::ReadCStrBounded(raw, rvaToOff(symRva), 256);
                        if (!fwd.empty()) targetStr = L"-> " + PathUtils::Utf8ToWide(fwd);
                    }
                    expSheet.rows.push_back({ modWide, std::to_wstring(ordBase + ordIdx), PathUtils::Utf8ToWide(fnStr), targetStr });
                    ++exportedSymCount;
                }
            }
            for (uint32_t fi = 0; fi < numFuncs && expSheet.rows.size() <= 2000; ++fi) {
                if (covered[fi] || funcsOff + static_cast<size_t>(fi) * 4 + 4 > raw.size()) continue;
                const uint32_t symRva = DetailBinarySysDoc::RdU32LE(raw.data() + funcsOff + static_cast<size_t>(fi) * 4);
                if (symRva == 0) continue;
                expSheet.rows.push_back({ modWide, std::to_wstring(ordBase + fi), L"(ordinal export)", DetailBinarySysDoc::Hex32(symRva) });
                ++exportedSymCount;
            }
        }
    }

    hdrSheet.rows.push_back({ L"Imported DLLs / Symbols", std::to_wstring(importedDllCount) + L" DLLs / " + std::to_wstring(importedSymCount) + L" functions" });
    hdrSheet.rows.push_back({ L"Exported Symbols", std::to_wstring(exportedSymCount) });

    out.sheets.push_back(std::move(hdrSheet));
    if (secSheet.rows.size() > 1) out.sheets.push_back(std::move(secSheet));
    if (impSheet.rows.size() > 1) out.sheets.push_back(std::move(impSheet));
    if (expSheet.rows.size() > 1) out.sheets.push_back(std::move(expSheet));

    out.valid = true;
    return true;
}


} // namespace Formats
