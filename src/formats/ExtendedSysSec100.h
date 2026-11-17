#pragma once
#include "../ByteIO.h"
// ExtendedSysSec100.h — Executables, Reverse Engineering, Security, Forensics & Firmware Formats (37–50)
// Covers: .exe/.dll/.sys/.ocx/.scr/.efi (PE32/PE32+ Imports & Exports), .lnk (MS-SHLLINK),
//         .pf (SCCA & MAM XPRESS_HUFF), .evtx (ElfFile/ElfChnk/BinXML), .dmp/.mdmp, .pdb (MSF 7.00),
//         .class (JVM Constant Pool/Fields/Methods), .dex (Dalvik MUTF-8/Classes/Methods),
//         .macho/.dylib/.so (Mach-O 32/64 & Fat), .crt/.cer/.pem/.der/.csr/.p7b/.p12/.pfx (X.509 & ASN.1),
//         .kdbx (KeePass v3/v4 VariantDictionary), .pcap/.pcapng (L2/L3/L4 Dissector), .uf2

#ifndef CERT_SHA256_HASH_PROP_ID
#define CERT_SHA256_HASH_PROP_ID 107
#endif

namespace Formats {

namespace DetailSys100 {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64BE;

inline std::wstring Hex32(uint32_t v) {
    wchar_t buf[32] = {};
    swprintf_s(buf, L"0x%08X", v);
    return buf;
}
inline std::wstring Hex64(uint64_t v) {
    wchar_t buf[32] = {};
    swprintf_s(buf, L"0x%016llX", static_cast<unsigned long long>(v));
    return buf;
}

inline std::wstring HexBytes(const uint8_t* p, size_t len, wchar_t sep = 0) { return PathUtils::HexBytes(p, len, sep); }

inline std::wstring FormatFileTimeUtc(uint64_t ftVal) {
    if (ftVal == 0 || ftVal >= 0x7FFFFFFFFFFFFFFFULL) return L"(not set)";
    FILETIME ft = {};
    ft.dwLowDateTime  = static_cast<DWORD>(ftVal & 0xFFFFFFFFu);
    ft.dwHighDateTime = static_cast<DWORD>(ftVal >> 32);
    SYSTEMTIME st = {};
    if (!FileTimeToSystemTime(&ft, &st) || st.wYear < 1970 || st.wYear > 2200) return L"(invalid)";
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u:%02u.%03u UTC",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}

inline std::wstring FormatUnixTimeUtc(uint32_t unixSec) {
    if (unixSec == 0) return L"0 (not set)";
    const uint64_t ftVal = (static_cast<uint64_t>(unixSec) + 11644473600ULL) * 10000000ULL;
    return FormatFileTimeUtc(ftVal) + L" (" + std::to_wstring(unixSec) + L")";
}

inline std::wstring FormatGuidLE(const uint8_t* g) {
    wchar_t buf[64] = {};
    swprintf_s(buf, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        RdU32LE(g), RdU16LE(g + 4), RdU16LE(g + 6),
        g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    return buf;
}

inline std::wstring ReadUtf16LEBounded(const uint8_t* p, size_t maxBytes, bool stopAtNul = true) {
    std::wstring out;
    const size_t nChars = maxBytes / 2;
    out.reserve(std::min<size_t>(nChars, 512));
    for (size_t i = 0; i < nChars; ++i) {
        const wchar_t wc = static_cast<wchar_t>(RdU16LE(p + i * 2));
        if (wc == 0 && stopAtNul) break;
        if (wc >= 0x20 || wc == L'\t') out.push_back(wc);
    }
    return out;
}

inline std::string ReadCStrBounded(const std::vector<uint8_t>& raw, size_t off, size_t maxLen = 512) {
    if (off >= raw.size()) return {};
    const size_t lim = std::min<size_t>(raw.size() - off, maxLen);
    size_t n = 0;
    while (n < lim && raw[off + n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(raw.data() + off), n);
}

} // namespace DetailSys100

// ============================================================================
// 37. Windows PE32 / PE32+ Portable Executable (.exe / .dll / .sys / .ocx / .scr / .efi)
// ============================================================================
inline bool ParseWindowsPeBinary(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 64 || raw[0] != 'M' || raw[1] != 'Z') return false;
    const uint32_t peOff = DetailSys100::RdU32LE(raw.data() + 60);
    if (peOff + 24 > raw.size() || std::memcmp(raw.data() + peOff, "PE\0\0", 4) != 0) return false;

    const uint8_t* coff = raw.data() + peOff + 4;
    const uint16_t machine      = DetailSys100::RdU16LE(coff + 0);
    const uint16_t numSections  = DetailSys100::RdU16LE(coff + 2);
    const uint32_t timeStamp    = DetailSys100::RdU32LE(coff + 4);
    const uint16_t optHdrSize   = DetailSys100::RdU16LE(coff + 16);
    const uint16_t chars        = DetailSys100::RdU16LE(coff + 18);

    std::wstring arch = L"Unknown (" + DetailSys100::Hex32(machine) + L")";
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
    hdrSheet.rows.push_back({ L"Compiler Timestamp", DetailSys100::FormatUnixTimeUtc(timeStamp) });

    bool isPe64 = false;
    uint32_t expRva = 0, expSize = 0;
    uint32_t impRva = 0, impSize = 0;

    const size_t optOff = peOff + 24;
    if (optHdrSize >= 24 && optOff + optHdrSize <= raw.size()) {
        const uint16_t optMagic = DetailSys100::RdU16LE(raw.data() + optOff);
        if (optMagic != 0x010B && optMagic != 0x020B && optMagic != 0x0107) return false;
        isPe64 = (optMagic == 0x020B);
        const uint32_t entryRva = DetailSys100::RdU32LE(raw.data() + optOff + 16);
        const uint64_t imgBase  = (isPe64 && optHdrSize >= 32) ? DetailSys100::RdU64LE(raw.data() + optOff + 24)
                                                               : ((optHdrSize >= 32) ? DetailSys100::RdU32LE(raw.data() + optOff + 28) : 0);
        const uint16_t subsys   = optHdrSize >= 70 ? DetailSys100::RdU16LE(raw.data() + optOff + 68) : 0;
        const uint16_t dllChars = optHdrSize >= 72 ? DetailSys100::RdU16LE(raw.data() + optOff + 70) : 0;

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
        hdrSheet.rows.push_back({ L"Entry Point RVA", DetailSys100::Hex32(entryRva) });
        hdrSheet.rows.push_back({ L"Image Base", DetailSys100::Hex64(imgBase) });
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
            const uint32_t numRva = std::min<uint32_t>(DetailSys100::RdU32LE(raw.data() + ddCountOff), 16u);
            if (numRva >= 1 && ddTableOff + 8 <= optOff + optHdrSize && ddTableOff + 8 <= raw.size()) {
                expRva  = DetailSys100::RdU32LE(raw.data() + ddTableOff + 0);
                expSize = DetailSys100::RdU32LE(raw.data() + ddTableOff + 4);
            }
            if (numRva >= 2 && ddTableOff + 16 <= optOff + optHdrSize && ddTableOff + 16 <= raw.size()) {
                impRva  = DetailSys100::RdU32LE(raw.data() + ddTableOff + 8);
                impSize = DetailSys100::RdU32LE(raw.data() + ddTableOff + 12);
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
            ps.vsize   = DetailSys100::RdU32LE(s + 8);
            ps.vaddr   = DetailSys100::RdU32LE(s + 12);
            ps.rawSize = DetailSys100::RdU32LE(s + 16);
            ps.rawPtr  = DetailSys100::RdU32LE(s + 20);
            ps.flags   = DetailSys100::RdU32LE(s + 36);
            if (ps.rawPtr > 0 && ps.rawPtr < minRawPtr) minRawPtr = ps.rawPtr;
            secs.push_back(ps);

            std::wstring perm;
            if (ps.flags & 0x40000000u) perm += L"R";
            if (ps.flags & 0x80000000u) perm += L"W";
            if (ps.flags & 0x20000000u) perm += L"X";
            if (ps.flags & 0x00000020u) perm += L" [CODE]";
            secSheet.rows.push_back({
                PathUtils::Utf8ToWide(ps.name),
                DetailSys100::Hex32(ps.vaddr),
                PathUtils::FormatByteSize(ps.vsize),
                DetailSys100::Hex32(ps.rawPtr),
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
            const uint32_t origThunk = DetailSys100::RdU32LE(raw.data() + dOff + 0);
            const uint32_t nameRva   = DetailSys100::RdU32LE(raw.data() + dOff + 12);
            const uint32_t firstThk  = DetailSys100::RdU32LE(raw.data() + dOff + 16);
            if (origThunk == 0 && nameRva == 0 && firstThk == 0) break;
            if (impSize > 0 && di * 20 >= impSize) break;

            const size_t nameOff = rvaToOff(nameRva);
            const std::string dllStr = DetailSys100::ReadCStrBounded(raw, nameOff, 256);
            if (dllStr.empty()) continue;
            const std::wstring dllWide = PathUtils::Utf8ToWide(dllStr);
            ++importedDllCount;

            const uint32_t thunkRva = (origThunk != 0) ? origThunk : firstThk;
            size_t tOff = rvaToOff(thunkRva);
            const size_t thunkSz = isPe64 ? 8 : 4;
            bool anyInDll = false;

            for (size_t ti = 0; ti < 4096 && tOff + thunkSz <= raw.size() && impSheet.rows.size() <= 2000; ++ti, tOff += thunkSz) {
                const uint64_t val = isPe64 ? DetailSys100::RdU64LE(raw.data() + tOff)
                                            : DetailSys100::RdU32LE(raw.data() + tOff);
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
                        const uint16_t hint = DetailSys100::RdU16LE(raw.data() + hnOff);
                        const std::string fn = DetailSys100::ReadCStrBounded(raw, hnOff + 2, 256);
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
            const uint32_t expNameRva   = DetailSys100::RdU32LE(raw.data() + eOff + 12);
            const uint32_t ordBase      = DetailSys100::RdU32LE(raw.data() + eOff + 16);
            const uint32_t numFuncs     = std::min<uint32_t>(DetailSys100::RdU32LE(raw.data() + eOff + 20), 16384u);
            const uint32_t numNames     = std::min<uint32_t>(DetailSys100::RdU32LE(raw.data() + eOff + 24), 16384u);
            const uint32_t funcsRva     = DetailSys100::RdU32LE(raw.data() + eOff + 28);
            const uint32_t namesRva     = DetailSys100::RdU32LE(raw.data() + eOff + 32);
            const uint32_t ordsRva      = DetailSys100::RdU32LE(raw.data() + eOff + 36);

            const std::string modNameStr = DetailSys100::ReadCStrBounded(raw, rvaToOff(expNameRva), 256);
            const std::wstring modWide   = modNameStr.empty() ? L"(module)" : PathUtils::Utf8ToWide(modNameStr);

            const size_t funcsOff = rvaToOff(funcsRva);
            const size_t namesOff = rvaToOff(namesRva);
            const size_t ordsOff  = rvaToOff(ordsRva);

            std::vector<uint8_t> covered(numFuncs, 0);
            if (namesOff < raw.size() && ordsOff < raw.size()) {
                for (uint32_t i = 0; i < numNames && expSheet.rows.size() <= 2000; ++i) {
                    if (namesOff + static_cast<size_t>(i) * 4 + 4 > raw.size() ||
                        ordsOff + static_cast<size_t>(i) * 2 + 2 > raw.size()) break;
                    const uint32_t fnRva  = DetailSys100::RdU32LE(raw.data() + namesOff + static_cast<size_t>(i) * 4);
                    const uint16_t ordIdx = DetailSys100::RdU16LE(raw.data() + ordsOff + static_cast<size_t>(i) * 2);
                    const std::string fnStr = DetailSys100::ReadCStrBounded(raw, rvaToOff(fnRva), 256);

                    uint32_t symRva = 0;
                    if (ordIdx < numFuncs && funcsOff + static_cast<size_t>(ordIdx) * 4 + 4 <= raw.size()) {
                        symRva = DetailSys100::RdU32LE(raw.data() + funcsOff + static_cast<size_t>(ordIdx) * 4);
                        covered[ordIdx] = 1;
                    }
                    std::wstring targetStr = DetailSys100::Hex32(symRva);
                    if (expSize > 0 && symRva >= expRva && symRva < expRva + expSize) {
                        const std::string fwd = DetailSys100::ReadCStrBounded(raw, rvaToOff(symRva), 256);
                        if (!fwd.empty()) targetStr = L"-> " + PathUtils::Utf8ToWide(fwd);
                    }
                    expSheet.rows.push_back({ modWide, std::to_wstring(ordBase + ordIdx), PathUtils::Utf8ToWide(fnStr), targetStr });
                    ++exportedSymCount;
                }
            }
            for (uint32_t fi = 0; fi < numFuncs && expSheet.rows.size() <= 2000; ++fi) {
                if (covered[fi] || funcsOff + static_cast<size_t>(fi) * 4 + 4 > raw.size()) continue;
                const uint32_t symRva = DetailSys100::RdU32LE(raw.data() + funcsOff + static_cast<size_t>(fi) * 4);
                if (symRva == 0) continue;
                expSheet.rows.push_back({ modWide, std::to_wstring(ordBase + fi), L"(ordinal export)", DetailSys100::Hex32(symRva) });
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

// ============================================================================
// 38. MS-SHLLINK Windows Shell Shortcut (.lnk)
// ============================================================================
inline bool ParseWindowsLnkShortcut(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    static const uint8_t kShlLinkClsid[16] = {
        0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46
    };
    if (raw.size() < 76 || DetailSys100::RdU32LE(raw.data()) != 0x0000004Cu) return false;
    if (std::memcmp(raw.data() + 4, kShlLinkClsid, 16) != 0) return false;

    const uint32_t linkFlags = DetailSys100::RdU32LE(raw.data() + 20);
    const uint32_t fileAttrs = DetailSys100::RdU32LE(raw.data() + 24);
    const uint64_t tCreate   = DetailSys100::RdU64LE(raw.data() + 28);
    const uint64_t tAccess   = DetailSys100::RdU64LE(raw.data() + 36);
    const uint64_t tWrite    = DetailSys100::RdU64LE(raw.data() + 44);
    const uint32_t fileSize  = DetailSys100::RdU32LE(raw.data() + 52);
    const int32_t  iconIdx   = static_cast<int32_t>(DetailSys100::RdU32LE(raw.data() + 56));
    const uint32_t showCmd   = DetailSys100::RdU32LE(raw.data() + 60);
    const uint16_t hotKey    = DetailSys100::RdU16LE(raw.data() + 64);

    std::wstring flagDesc;
    if (linkFlags & 0x0001u) flagDesc += L"HasTargetIDList ";
    if (linkFlags & 0x0002u) flagDesc += L"HasLinkInfo ";
    if (linkFlags & 0x0004u) flagDesc += L"HasName ";
    if (linkFlags & 0x0008u) flagDesc += L"HasRelativePath ";
    if (linkFlags & 0x0010u) flagDesc += L"HasWorkingDir ";
    if (linkFlags & 0x0020u) flagDesc += L"HasArguments ";
    if (linkFlags & 0x0040u) flagDesc += L"HasIconLocation ";
    if (linkFlags & 0x0080u) flagDesc += L"IsUnicode ";
    if (linkFlags & 0x2000u) flagDesc += L"RunAsUser ";

    std::wstring attrDesc;
    if (fileAttrs & 0x01u) attrDesc += L"READONLY ";
    if (fileAttrs & 0x02u) attrDesc += L"HIDDEN ";
    if (fileAttrs & 0x04u) attrDesc += L"SYSTEM ";
    if (fileAttrs & 0x10u) attrDesc += L"DIRECTORY ";
    if (fileAttrs & 0x20u) attrDesc += L"ARCHIVE ";

    std::wstring showStr = std::to_wstring(showCmd);
    if (showCmd == 1) showStr = L"SW_SHOWNORMAL (1)";
    else if (showCmd == 3) showStr = L"SW_SHOWMAXIMIZED (3)";
    else if (showCmd == 7) showStr = L"SW_SHOWMINNOACTIVE (7)";

    std::wstring hkStr = L"None";
    if (hotKey != 0) {
        const uint8_t vk = static_cast<uint8_t>(hotKey & 0xFFu);
        const uint8_t mod = static_cast<uint8_t>((hotKey >> 8) & 0xFFu);
        hkStr.clear();
        if (mod & 0x02u) hkStr += L"Ctrl+";
        if (mod & 0x04u) hkStr += L"Alt+";
        if (mod & 0x01u) hkStr += L"Shift+";
        if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) hkStr.push_back(static_cast<wchar_t>(vk));
        else hkStr += L"VK_" + std::to_wstring(vk);
    }

    std::vector<std::vector<std::wstring>> rows = {
        { L"Field", L"Value" },
        { L"Format", L"MS-SHLLINK Windows Shell Shortcut (.lnk)" },
        { L"Link Flags", DetailSys100::Hex32(linkFlags) + (flagDesc.empty() ? L"" : (L" (" + PathUtils::Trim(flagDesc) + L")")) },
        { L"Target Attributes", attrDesc.empty() ? DetailSys100::Hex32(fileAttrs) : PathUtils::Trim(attrDesc) },
        { L"Creation Time", DetailSys100::FormatFileTimeUtc(tCreate) },
        { L"Access Time", DetailSys100::FormatFileTimeUtc(tAccess) },
        { L"Write Time", DetailSys100::FormatFileTimeUtc(tWrite) },
        { L"Target File Size", PathUtils::FormatByteSize(fileSize) },
        { L"Icon Index", std::to_wstring(iconIdx) },
        { L"Show Command", showStr },
        { L"HotKey", hkStr }
    };

    size_t pos = 76;

    // 1. LinkTargetIDList
    if ((linkFlags & 0x0001u) != 0 && pos + 2 <= raw.size()) {
        const uint16_t idListSize = DetailSys100::RdU16LE(raw.data() + pos);
        pos += 2;
        if (pos + idListSize <= raw.size()) {
            size_t ip = pos;
            const size_t idEnd = pos + idListSize;
            int itemCnt = 0;
            std::wstring idPath;
            while (ip + 2 <= idEnd) {
                const uint16_t itemSz = DetailSys100::RdU16LE(raw.data() + ip);
                if (itemSz == 0) break;
                if (itemSz < 2 || ip + itemSz > idEnd) break;
                const uint8_t typeByte = (itemSz >= 3) ? raw[ip + 2] : 0;
                if (typeByte == 0x2F && itemSz >= 6) {
                    std::string drv = DetailSys100::ReadCStrBounded(raw, ip + 3, itemSz - 3);
                    if (!drv.empty()) idPath += PathUtils::Utf8ToWide(drv);
                } else if ((typeByte & 0x70u) == 0x30u && itemSz >= 15) {
                    std::string shortNm = DetailSys100::ReadCStrBounded(raw, ip + 14, itemSz - 14);
                    if (!shortNm.empty()) {
                        if (!idPath.empty() && idPath.back() != L'\\') idPath.push_back(L'\\');
                        idPath += PathUtils::Utf8ToWide(shortNm);
                    }
                }
                ++itemCnt;
                ip += itemSz;
            }
            rows.push_back({ L"IDList Items", std::to_wstring(itemCnt) + (idPath.empty() ? L"" : (L" (" + idPath + L")")) });
            pos += idListSize;
        }
    }

    // 2. LinkInfo
    if ((linkFlags & 0x0002u) != 0 && pos + 28 <= raw.size()) {
        const uint32_t liSize    = DetailSys100::RdU32LE(raw.data() + pos + 0);
        const uint32_t liHdrSize = DetailSys100::RdU32LE(raw.data() + pos + 4);
        const uint32_t liFlags   = DetailSys100::RdU32LE(raw.data() + pos + 8);
        const uint32_t volOff    = DetailSys100::RdU32LE(raw.data() + pos + 12);
        const uint32_t baseOff   = DetailSys100::RdU32LE(raw.data() + pos + 16);
        const uint32_t netOff    = DetailSys100::RdU32LE(raw.data() + pos + 20);
        const uint32_t sufOff    = DetailSys100::RdU32LE(raw.data() + pos + 24);

        if (liSize >= 28 && pos + liSize <= raw.size()) {
            if ((liFlags & 0x01u) != 0) {
                if (volOff >= 28 && volOff + 16 <= liSize) {
                    const uint32_t drvSerial = DetailSys100::RdU32LE(raw.data() + pos + volOff + 8);
                    const uint32_t lblOff    = DetailSys100::RdU32LE(raw.data() + pos + volOff + 12);
                    std::wstring volLbl;
                    if (lblOff < liSize - volOff) {
                        volLbl = PathUtils::Utf8ToWide(DetailSys100::ReadCStrBounded(raw, pos + volOff + lblOff, liSize - volOff - lblOff));
                    }
                    rows.push_back({ L"Volume Serial / Label", DetailSys100::Hex32(drvSerial) + (volLbl.empty() ? L"" : (L" (" + volLbl + L")")) });
                }
                std::wstring localBase;
                if (liHdrSize >= 0x24 && pos + 32 <= raw.size()) {
                    const uint32_t baseUniOff = DetailSys100::RdU32LE(raw.data() + pos + 28);
                    if (baseUniOff > 0 && baseUniOff < liSize) {
                        localBase = DetailSys100::ReadUtf16LEBounded(raw.data() + pos + baseUniOff, liSize - baseUniOff, true);
                    }
                }
                if (localBase.empty() && baseOff > 0 && baseOff < liSize) {
                    localBase = PathUtils::Utf8ToWide(DetailSys100::ReadCStrBounded(raw, pos + baseOff, liSize - baseOff));
                }
                if (sufOff > 0 && sufOff < liSize) {
                    std::wstring suf = PathUtils::Utf8ToWide(DetailSys100::ReadCStrBounded(raw, pos + sufOff, liSize - sufOff));
                    if (!suf.empty()) localBase += suf;
                }
                if (!localBase.empty()) rows.push_back({ L"Local Base Path", localBase });
            }
            if ((liFlags & 0x02u) != 0 && netOff >= 28 && netOff + 20 <= liSize) {
                const uint32_t netNameOff = DetailSys100::RdU32LE(raw.data() + pos + netOff + 8);
                if (netNameOff < liSize - netOff) {
                    std::wstring unc = PathUtils::Utf8ToWide(DetailSys100::ReadCStrBounded(raw, pos + netOff + netNameOff, liSize - netOff - netNameOff));
                    if (sufOff > 0 && sufOff < liSize) {
                        std::wstring suf = PathUtils::Utf8ToWide(DetailSys100::ReadCStrBounded(raw, pos + sufOff, liSize - sufOff));
                        if (!suf.empty()) {
                            if (!unc.empty() && unc.back() != L'\\' && suf.front() != L'\\') unc.push_back(L'\\');
                            unc += suf;
                        }
                    }
                    if (!unc.empty()) rows.push_back({ L"UNC Network Target", unc });
                }
            }
            pos += liSize;
        }
    }

    // 3. StringData counted strings
    const bool isUnicode = (linkFlags & 0x0080u) != 0;
    auto readCountedStr = [&](const wchar_t* label) {
        if (pos + 2 > raw.size()) return;
        const uint16_t cch = DetailSys100::RdU16LE(raw.data() + pos);
        pos += 2;
        if (isUnicode) {
            const size_t nb = static_cast<size_t>(cch) * 2;
            if (pos + nb > raw.size()) { pos = raw.size(); return; }
            std::wstring s = DetailSys100::ReadUtf16LEBounded(raw.data() + pos, nb, false);
            pos += nb;
            rows.push_back({ label, s });
        } else {
            if (pos + cch > raw.size()) { pos = raw.size(); return; }
            std::wstring s = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + pos), cch));
            pos += cch;
            rows.push_back({ label, s });
        }
    };
    if (linkFlags & 0x0004u) readCountedStr(L"Description (Name)");
    if (linkFlags & 0x0008u) readCountedStr(L"Relative Path");
    if (linkFlags & 0x0010u) readCountedStr(L"Working Directory");
    if (linkFlags & 0x0020u) readCountedStr(L"Command-Line Arguments");
    if (linkFlags & 0x0040u) readCountedStr(L"Icon Location");

    // 4. ExtraData blocks
    while (pos + 8 <= raw.size()) {
        const uint32_t blkSz = DetailSys100::RdU32LE(raw.data() + pos);
        if (blkSz < 8 || pos + blkSz > raw.size()) break;
        const uint32_t blkSig = DetailSys100::RdU32LE(raw.data() + pos + 4);
        if (blkSig == 0xA0000001u && blkSz >= 788) { // EnvironmentVariableDataBlock
            std::wstring envTarget = DetailSys100::ReadUtf16LEBounded(raw.data() + pos + 268, 520, true);
            if (!envTarget.empty()) rows.push_back({ L"Environment Target", envTarget });
        } else if (blkSig == 0xA0000003u && blkSz >= 96) { // TrackerDataBlock
            std::string machId = DetailSys100::ReadCStrBounded(raw, pos + 16, 16);
            if (!machId.empty()) rows.push_back({ L"Tracker Machine ID", PathUtils::Utf8ToWide(machId) });
        }
        pos += blkSz;
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Windows Shell Shortcut (.lnk)";
    SpreadsheetSheet sh;
    sh.name = L"Shortcut_Info";
    sh.maxCols = 2;
    sh.rows = std::move(rows);
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}

// ============================================================================
// 39. Windows Execution Prefetch (.pf — SCCA & Win10/11 MAM XPRESS_HUFF)
// ============================================================================
inline bool ParseWindowsPrefetch(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    const bool isMam = (raw.size() >= 12 && raw[0] == 'M' && raw[1] == 'A' && raw[2] == 'M' && (raw[3] & 0x0Fu) == 0x04u);
    const bool isRawScca = (raw.size() >= 84 && std::memcmp(raw.data() + 4, "SCCA", 4) == 0);
    if (!isMam && !isRawScca) return false;

    std::vector<uint8_t> decompressed;
    const uint8_t* scca = raw.data();
    size_t sccaLen = raw.size();

    if (isMam) {
        const uint32_t uncompSz = DetailSys100::RdU32LE(raw.data() + 4);
        if (uncompSz < 84 || uncompSz > 32u * 1024u * 1024u) return false;

        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (!hNtdll) return false;

        using FnRtlGetCompressionWorkSpaceSize = LONG(NTAPI*)(USHORT, PULONG, PULONG);
        using FnRtlDecompressBufferEx = LONG(NTAPI*)(USHORT, PUCHAR, ULONG, PUCHAR, ULONG, PULONG, PVOID);

        auto pGetWs = reinterpret_cast<FnRtlGetCompressionWorkSpaceSize>(GetProcAddress(hNtdll, "RtlGetCompressionWorkSpaceSize"));
        auto pDecomp = reinterpret_cast<FnRtlDecompressBufferEx>(GetProcAddress(hNtdll, "RtlDecompressBufferEx"));
        if (!pGetWs || !pDecomp) return false;

        constexpr USHORT kXpressHuff = 4;
        ULONG bufWs = 0, fragWs = 0;
        if (pGetWs(kXpressHuff, &bufWs, &fragWs) != 0) return false;

        std::vector<uint8_t> ws(std::max<ULONG>(fragWs, 4096u), 0);
        decompressed.resize(uncompSz, 0);

        const size_t tryOffs[2] = { (raw[3] & 0x80u) ? size_t(12) : size_t(8), (raw[3] & 0x80u) ? size_t(8) : size_t(12) };
        bool okDecomp = false;
        for (size_t hdrOff : tryOffs) {
            if (hdrOff >= raw.size()) continue;
            ULONG finalSz = 0;
            const LONG st = pDecomp(
                kXpressHuff,
                decompressed.data(),
                static_cast<ULONG>(decompressed.size()),
                const_cast<PUCHAR>(raw.data() + hdrOff),
                static_cast<ULONG>(raw.size() - hdrOff),
                &finalSz,
                ws.data());
            if (st == 0 && finalSz >= 84 && std::memcmp(decompressed.data() + 4, "SCCA", 4) == 0) {
                decompressed.resize(finalSz);
                okDecomp = true;
                break;
            }
        }
        if (!okDecomp) return false;
        scca = decompressed.data();
        sccaLen = decompressed.size();
    }

    const uint32_t sccaVer = DetailSys100::RdU32LE(scca + 0);
    if (sccaVer != 17 && sccaVer != 23 && sccaVer != 26 && sccaVer != 30 && sccaVer != 31) return false;

    const std::wstring exeName = DetailSys100::ReadUtf16LEBounded(scca + 16, 60, true);
    const uint32_t pfHash      = DetailSys100::RdU32LE(scca + 76);

    uint32_t metricsOff = 0, metricsCnt = 0, fnStringsOff = 0, fnStringsSz = 0, volsCnt = 0, runCount = 0;
    std::vector<std::wstring> runTimes;

    if (sccaLen >= 156) {
        metricsOff   = DetailSys100::RdU32LE(scca + 84);
        metricsCnt   = DetailSys100::RdU32LE(scca + 88);
        fnStringsOff = DetailSys100::RdU32LE(scca + 100);
        fnStringsSz  = DetailSys100::RdU32LE(scca + 104);
        volsCnt      = DetailSys100::RdU32LE(scca + 112);

        if (sccaVer == 17) {
            const uint64_t ft = DetailSys100::RdU64LE(scca + 120);
            if (ft != 0) runTimes.push_back(DetailSys100::FormatFileTimeUtc(ft));
            runCount = DetailSys100::RdU32LE(scca + 144);
        } else if (sccaVer == 23 && sccaLen >= 160) {
            const uint64_t ft = DetailSys100::RdU64LE(scca + 128);
            if (ft != 0) runTimes.push_back(DetailSys100::FormatFileTimeUtc(ft));
            runCount = DetailSys100::RdU32LE(scca + 152);
        } else if ((sccaVer == 26 || sccaVer >= 30) && sccaLen >= 212) {
            for (int t = 0; t < 8; ++t) {
                const uint64_t ft = DetailSys100::RdU64LE(scca + 128 + t * 8);
                if (ft != 0) runTimes.push_back(DetailSys100::FormatFileTimeUtc(ft));
            }
            const size_t rcOff = (sccaVer >= 30 && metricsOff == 296) ? 200 : 208;
            if (rcOff + 4 <= sccaLen) runCount = DetailSys100::RdU32LE(scca + rcOff);
        }
    }

    std::wstring verLabel = L"SCCA v" + std::to_wstring(sccaVer);
    if (sccaVer == 17) verLabel += L" (Windows XP / 2003)";
    else if (sccaVer == 23) verLabel += L" (Windows Vista / 7)";
    else if (sccaVer == 26) verLabel += L" (Windows 8 / 8.1)";
    else if (sccaVer >= 30) verLabel += L" (Windows 10 / 11)";

    SpreadsheetSheet infoSh;
    infoSh.name = L"Prefetch_Forensics";
    infoSh.maxCols = 2;
    infoSh.rows.push_back({ L"Property", L"Value" });
    infoSh.rows.push_back({ L"Container Encoding", isMam ? L"MAM XPRESS Huffman Compressed" : L"Raw Uncompressed SCCA" });
    infoSh.rows.push_back({ L"Prefetch Version", verLabel });
    infoSh.rows.push_back({ L"Executable Name", exeName });
    infoSh.rows.push_back({ L"Prefetch Hash", DetailSys100::Hex32(pfHash) });
    infoSh.rows.push_back({ L"Execution Run Count", std::to_wstring(runCount) });
    infoSh.rows.push_back({ L"File Metrics Count", std::to_wstring(metricsCnt) });
    infoSh.rows.push_back({ L"Volumes Count", std::to_wstring(volsCnt) });
    infoSh.rows.push_back({ L"Uncompressed SCCA Size", PathUtils::FormatByteSize(sccaLen) });
    for (size_t i = 0; i < runTimes.size(); ++i) {
        infoSh.rows.push_back({ L"Last Execution #" + std::to_wstring(i + 1), runTimes[i] });
    }

    SpreadsheetSheet filesSh;
    filesSh.name = L"Referenced_Files";
    filesSh.maxCols = 2;
    filesSh.rows.push_back({ L"#", L"Loaded Module / File Path" });

    if (fnStringsOff > 0 && fnStringsSz >= 2 && static_cast<uint64_t>(fnStringsOff) < sccaLen) {
        const size_t avail = std::min<size_t>(fnStringsSz, sccaLen - fnStringsOff);
        const uint8_t* fp = scca + fnStringsOff;
        std::wstring cur;
        int idx = 1;
        for (size_t off = 0; off + 2 <= avail && filesSh.rows.size() <= 3000; off += 2) {
            const wchar_t wc = static_cast<wchar_t>(DetailSys100::RdU16LE(fp + off));
            if (wc == 0) {
                if (!cur.empty()) {
                    filesSh.rows.push_back({ std::to_wstring(idx++), cur });
                    cur.clear();
                }
            } else if (wc >= 0x20) {
                cur.push_back(wc);
            }
        }
        if (!cur.empty() && filesSh.rows.size() <= 3000) {
            filesSh.rows.push_back({ std::to_wstring(idx++), cur });
        }
    }
    infoSh.rows.push_back({ L"Decoded File Paths", std::to_wstring(filesSh.rows.size() - 1) });

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Windows Execution Prefetch (.pf)";
    out.sheets.push_back(std::move(infoSh));
    if (filesSh.rows.size() > 1) out.sheets.push_back(std::move(filesSh));
    out.valid = true;
    return true;
}

// ============================================================================
// 40. Windows XML Event Log (.evtx — ElfFile + ElfChnk + BinXML Records)
// ============================================================================
inline bool ParseWindowsEvtxLog(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 128 || std::memcmp(raw.data(), "ElfFile\0", 8) != 0) return false;

    const uint64_t firstChunk = DetailSys100::RdU64LE(raw.data() + 8);
    const uint64_t lastChunk  = DetailSys100::RdU64LE(raw.data() + 16);
    const uint64_t nextRecId  = DetailSys100::RdU64LE(raw.data() + 24);
    const uint16_t minVer     = DetailSys100::RdU16LE(raw.data() + 36);
    const uint16_t majVer     = DetailSys100::RdU16LE(raw.data() + 38);
    const uint16_t hdrBlkSz   = DetailSys100::RdU16LE(raw.data() + 40);
    const uint16_t numChunks  = DetailSys100::RdU16LE(raw.data() + 42);

    SpreadsheetSheet hdrSh;
    hdrSh.name = L"EVTX_Header";
    hdrSh.maxCols = 2;
    hdrSh.rows.push_back({ L"Property", L"Value" });
    hdrSh.rows.push_back({ L"Signature", L"ElfFile\\0 (Windows Binary XML Event Log)" });
    hdrSh.rows.push_back({ L"Format Version", std::to_wstring(majVer) + L"." + std::to_wstring(minVer) });
    hdrSh.rows.push_back({ L"Chunk Range", std::to_wstring(firstChunk) + L" .. " + std::to_wstring(lastChunk) + L" (" + std::to_wstring(numChunks) + L" chunks)" });
    hdrSh.rows.push_back({ L"Next Event Record ID", std::to_wstring(nextRecId) });
    hdrSh.rows.push_back({ L"Total File Size", PathUtils::FormatByteSize(std::max<uint64_t>(out.sourceFileSize, raw.size())) });

    SpreadsheetSheet recSh;
    recSh.name = L"Event_Records";
    recSh.maxCols = 5;
    recSh.rows.push_back({ L"Record ID", L"Written Timestamp (UTC)", L"Event ID", L"Provider / Source", L"Extracted Payload Strings" });

    size_t chunkPos = (hdrBlkSz >= 128 && hdrBlkSz <= raw.size()) ? hdrBlkSz : 4096;
    size_t parsedChunks = 0;

    while (chunkPos + 512 <= raw.size() && recSh.rows.size() <= 2000) {
        if (std::memcmp(raw.data() + chunkPos, "ElfChnk\0", 8) != 0) {
            chunkPos += 65536;
            continue;
        }
        ++parsedChunks;
        const size_t chunkEnd = std::min<size_t>(raw.size(), chunkPos + 65536);
        const uint32_t freeSpaceOff = DetailSys100::RdU32LE(raw.data() + chunkPos + 48);
        const size_t recLimit = (freeSpaceOff > 512 && chunkPos + freeSpaceOff <= chunkEnd) ? (chunkPos + freeSpaceOff) : chunkEnd;

        // Pre-collect chunk NameStrings referenced from chunk string table / inline BinXML
        std::vector<std::wstring> chunkStrings;
        for (int sSlot = 0; sSlot < 64; ++sSlot) {
            uint32_t strOff = DetailSys100::RdU32LE(raw.data() + chunkPos + 128 + sSlot * 4);
            int chainGuard = 0;
            while (strOff >= 512 && chunkPos + strOff + 8 <= chunkEnd && chainGuard++ < 64) {
                const uint32_t nextOff = DetailSys100::RdU32LE(raw.data() + chunkPos + strOff);
                const uint16_t cch     = DetailSys100::RdU16LE(raw.data() + chunkPos + strOff + 6);
                if (cch > 0 && cch <= 256 && chunkPos + strOff + 8 + static_cast<size_t>(cch) * 2 <= chunkEnd) {
                    chunkStrings.push_back(DetailSys100::ReadUtf16LEBounded(raw.data() + chunkPos + strOff + 8, static_cast<size_t>(cch) * 2, false));
                }
                if (nextOff == strOff) break;
                strOff = nextOff;
            }
        }

        size_t rPos = chunkPos + 512;
        while (rPos + 28 <= recLimit && recSh.rows.size() <= 2000) {
            if (DetailSys100::RdU32LE(raw.data() + rPos) != 0x00002A2Au) break;
            const uint32_t recSz = DetailSys100::RdU32LE(raw.data() + rPos + 4);
            if (recSz < 28 || rPos + recSz > chunkEnd) break;

            const uint64_t recId   = DetailSys100::RdU64LE(raw.data() + rPos + 8);
            const uint64_t wrTime  = DetailSys100::RdU64LE(raw.data() + rPos + 16);
            const uint8_t* bx      = raw.data() + rPos + 24;
            const size_t   bxLen   = recSz - 28;

            std::wstring eventIdStr = L"-";
            std::wstring providerStr;
            std::vector<std::wstring> texts;

            // Inspect BinXML TemplateInstance (0x0C) value descriptors if present, plus UTF-16 runs
            for (size_t i = 0; i + 10 <= bxLen; ++i) {
                if (bx[i] == 0x0C && i + 14 <= bxLen) {
                    // TemplateInstance: [0x0C][u8 unk][u32 tmplId][u32 tmplDefDataOff]
                    const uint32_t tmplOff = DetailSys100::RdU32LE(bx + i + 6);
                    size_t valTblPos = i + 10;
                    const size_t recRel = (rPos + 24 + i + 10) - chunkPos;
                    if (tmplOff == recRel && valTblPos + 24 <= bxLen) {
                        const uint32_t tmplDataSz = DetailSys100::RdU32LE(bx + valTblPos + 20);
                        valTblPos += 24 + tmplDataSz;
                    }
                    if (valTblPos + 4 <= bxLen) {
                        const uint32_t numVals = DetailSys100::RdU32LE(bx + valTblPos);
                        if (numVals > 0 && numVals <= 128 && valTblPos + 4 + static_cast<size_t>(numVals) * 4 <= bxLen) {
                            size_t dPos = valTblPos + 4;
                            size_t vPos = dPos + static_cast<size_t>(numVals) * 4;
                            for (uint32_t vi = 0; vi < numVals && vPos <= bxLen; ++vi) {
                                const uint16_t vSz   = DetailSys100::RdU16LE(bx + dPos + vi * 4);
                                const uint8_t  vType = bx[dPos + vi * 4 + 2];
                                if (vPos + vSz > bxLen) break;
                                if (vType == 0x01 && vSz >= 2) { // StringType
                                    std::wstring ws = DetailSys100::ReadUtf16LEBounded(bx + vPos, vSz, true);
                                    if (!ws.empty()) {
                                        if (providerStr.empty()) providerStr = ws;
                                        else texts.push_back(ws);
                                    }
                                } else if ((vType == 0x06 || vType == 0x05) && vSz == 2 && eventIdStr == L"-") { // UInt16 EventID
                                    const uint16_t eid = DetailSys100::RdU16LE(bx + vPos);
                                    if (eid > 0) eventIdStr = std::to_wstring(eid);
                                } else if ((vType == 0x08 || vType == 0x07) && vSz == 4 && eventIdStr == L"-") {
                                    const uint32_t eid = DetailSys100::RdU32LE(bx + vPos);
                                    if (eid > 0 && eid <= 65535) eventIdStr = std::to_wstring(eid);
                                }
                                vPos += vSz;
                            }
                            break;
                        }
                    }
                }
            }

            // Scan inline BinXML ValueText (0x05, 0x01, u16 cch, UTF-16) and printable UTF-16 runs
            if (texts.empty() && providerStr.empty()) {
                for (size_t i = 0; i + 6 <= bxLen; ) {
                    if ((bx[i] == 0x05 || bx[i] == 0x45) && bx[i + 1] == 0x01) {
                        const uint16_t cch = DetailSys100::RdU16LE(bx + i + 2);
                        if (cch > 0 && cch <= 512 && i + 4 + static_cast<size_t>(cch) * 2 <= bxLen) {
                            std::wstring ws = DetailSys100::ReadUtf16LEBounded(bx + i + 4, static_cast<size_t>(cch) * 2, true);
                            if (!ws.empty()) {
                                if (providerStr.empty()) providerStr = ws;
                                else if (eventIdStr == L"-" && ws.size() <= 5 && ws[0] >= L'0' && ws[0] <= L'9') eventIdStr = ws;
                                else texts.push_back(ws);
                            }
                            i += 4 + static_cast<size_t>(cch) * 2;
                            continue;
                        }
                    }
                    // Also check UTF-16LE ASCII runs >= 4 chars
                    std::wstring run;
                    size_t j = i;
                    while (j + 2 <= bxLen && bx[j + 1] == 0 && bx[j] >= 32 && bx[j] <= 126) {
                        run.push_back(static_cast<wchar_t>(bx[j]));
                        j += 2;
                    }
                    if (run.size() >= 4) {
                        if (run != L"Event" && run != L"System" && run != L"EventData" && run != L"Data") {
                            if (providerStr.empty()) providerStr = run;
                            else texts.push_back(run);
                        }
                        i = j;
                    } else {
                        ++i;
                    }
                }
            }

            if (providerStr.empty() && !chunkStrings.empty()) providerStr = chunkStrings.front();

            std::wstring joined;
            for (size_t ti = 0; ti < texts.size() && joined.size() < 240; ++ti) {
                if (!joined.empty()) joined += L" | ";
                joined += texts[ti];
            }

            recSh.rows.push_back({
                std::to_wstring(recId),
                DetailSys100::FormatFileTimeUtc(wrTime),
                eventIdStr,
                providerStr.empty() ? L"(BinXML Event)" : providerStr,
                joined.empty() ? (std::to_wstring(recSz) + L" B BinXML") : joined
            });

            rPos += recSz;
        }

        chunkPos += 65536;
    }

    hdrSh.rows.push_back({ L"Verified Chunks / Events", std::to_wstring(parsedChunks) + L" / " + std::to_wstring(recSh.rows.size() - 1) });
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Windows XML Event Log (.evtx)";
    out.sheets.push_back(std::move(hdrSh));
    if (recSh.rows.size() > 1) out.sheets.push_back(std::move(recSh));
    out.valid = true;
    return true;
}

// ============================================================================
// 41. Windows Crash Minidump (.dmp / .mdmp — MDMP)
// ============================================================================
inline bool ParseWindowsMinidump(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 32 || std::memcmp(raw.data(), "MDMP", 4) != 0) return false;

    const uint16_t ver        = DetailSys100::RdU16LE(raw.data() + 4);
    const uint32_t numStreams = DetailSys100::RdU32LE(raw.data() + 8);
    const uint32_t dirRva     = DetailSys100::RdU32LE(raw.data() + 12);
    const uint32_t timeStamp  = DetailSys100::RdU32LE(raw.data() + 20);
    const uint64_t flags      = DetailSys100::RdU64LE(raw.data() + 24);

    SpreadsheetSheet dirSh;
    dirSh.name = L"Minidump_Streams";
    dirSh.maxCols = 2;
    dirSh.rows = {
        { L"Minidump Property", L"Value" },
        { L"Signature", L"MDMP v" + std::to_wstring(ver) },
        { L"Crash Timestamp", DetailSys100::FormatUnixTimeUtc(timeStamp) },
        { L"Dump Flags", DetailSys100::Hex64(flags) },
        { L"Directory Stream Count", std::to_wstring(numStreams) },
        { L"Dump File Size", PathUtils::FormatByteSize(std::max<uint64_t>(out.sourceFileSize, raw.size())) }
    };

    SpreadsheetSheet modSh;
    modSh.name = L"Minidump_Modules";
    modSh.maxCols = 3;
    modSh.rows.push_back({ L"Module Path", L"Base Address", L"Image Size" });

    if (dirRva > 0 && numStreams <= 4096 && static_cast<uint64_t>(dirRva) + static_cast<uint64_t>(numStreams) * 12ULL <= raw.size()) {
        for (uint32_t i = 0; i < std::min(numStreams, 128u); ++i) {
            const size_t dp = dirRva + static_cast<size_t>(i) * 12;
            const uint32_t stype = DetailSys100::RdU32LE(raw.data() + dp + 0);
            const uint32_t slen  = DetailSys100::RdU32LE(raw.data() + dp + 4);
            const uint32_t srva  = DetailSys100::RdU32LE(raw.data() + dp + 8);

            std::wstring sname = L"Stream Type " + std::to_wstring(stype);
            if (stype == 3) sname = L"ThreadListStream (3)";
            else if (stype == 4) sname = L"ModuleListStream (4)";
            else if (stype == 6) sname = L"ExceptionStream (6)";
            else if (stype == 7) sname = L"SystemInfoStream (7)";
            else if (stype == 8) sname = L"ThreadExListStream (8)";
            else if (stype == 9) sname = L"Memory64ListStream (9)";
            else if (stype == 15) sname = L"MiscInfoStream (15)";

            dirSh.rows.push_back({ sname, PathUtils::FormatByteSize(slen) + L" @ " + DetailSys100::Hex32(srva) });

            if (stype == 6 && slen >= 32 && static_cast<uint64_t>(srva) + 32ULL <= raw.size()) {
                const uint32_t excThread = DetailSys100::RdU32LE(raw.data() + srva + 0);
                const uint32_t excCode   = DetailSys100::RdU32LE(raw.data() + srva + 8);
                const uint64_t excAddr   = DetailSys100::RdU64LE(raw.data() + srva + 24);
                dirSh.rows.push_back({ L"Exception Code / Address", DetailSys100::Hex32(excCode) + L" @ " + DetailSys100::Hex64(excAddr) + L" (TID " + std::to_wstring(excThread) + L")" });
            } else if (stype == 4 && slen >= 4 && static_cast<uint64_t>(srva) + slen <= raw.size()) {
                const uint32_t nMods = DetailSys100::RdU32LE(raw.data() + srva);
                for (uint32_t m = 0; m < std::min(nMods, 512u); ++m) {
                    const size_t mp = srva + 4 + static_cast<size_t>(m) * 108;
                    if (mp + 108 > raw.size()) break;
                    const uint64_t baseAddr = DetailSys100::RdU64LE(raw.data() + mp + 0);
                    const uint32_t imgSz    = DetailSys100::RdU32LE(raw.data() + mp + 8);
                    const uint32_t nameRva  = DetailSys100::RdU32LE(raw.data() + mp + 20);
                    std::wstring mname = L"(module)";
                    if (static_cast<uint64_t>(nameRva) + 4ULL <= raw.size()) {
                        const uint32_t byteLen = std::min<uint32_t>(DetailSys100::RdU32LE(raw.data() + nameRva), 1024u);
                        if (static_cast<uint64_t>(nameRva) + 4ULL + byteLen <= raw.size()) {
                            mname = DetailSys100::ReadUtf16LEBounded(raw.data() + nameRva + 4, byteLen, true);
                        }
                    }
                    modSh.rows.push_back({ mname, DetailSys100::Hex64(baseAddr), PathUtils::FormatByteSize(imgSz) });
                }
            }
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Windows Crash Minidump (.dmp)";
    out.sheets.push_back(std::move(dirSh));
    if (modSh.rows.size() > 1) out.sheets.push_back(std::move(modSh));
    out.valid = true;
    return true;
}

// ============================================================================
// 42. Microsoft Program Database Symbols (.pdb — MSF 7.00 Stream 1 + Stream 3 DBI)
// ============================================================================
inline bool ParseMsfPdbSymbols(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 56 || std::memcmp(raw.data(), "Microsoft C/C++ MSF 7.00", 24) != 0) return false;

    const uint32_t blkSize      = DetailSys100::RdU32LE(raw.data() + 32);
    const uint32_t numBlocks    = DetailSys100::RdU32LE(raw.data() + 40);
    const uint32_t dirBytes     = DetailSys100::RdU32LE(raw.data() + 44);
    const uint32_t blockMapAddr = DetailSys100::RdU32LE(raw.data() + 52);

    if (blkSize < 512 || blkSize > 8192 || (blkSize & (blkSize - 1u)) != 0) return false;
    // A PDB is usually far larger than the 32 MB a reader is handed, its stream directory near the end: every block
    // is read through SourceBytes
    SourceBytes src(raw, out);
    const uint64_t fileSize = src.Size();
    std::vector<uint8_t> blockBuf;
    const uint64_t mapOff = static_cast<uint64_t>(blockMapAddr) * blkSize;
    if (mapOff >= fileSize) return false;

    // Reassemble Stream Directory via blockMapAddr
    const uint32_t numDirBlocks = (dirBytes + blkSize - 1u) / blkSize;
    if (numDirBlocks > 16384 || mapOff + static_cast<uint64_t>(numDirBlocks) * 4ULL > fileSize) return false;
    std::vector<uint8_t> mapBuf;
    const uint8_t* blockMap = numDirBlocks ? src.Read(mapOff, static_cast<size_t>(numDirBlocks) * 4, mapBuf) : nullptr;
    if (numDirBlocks && !blockMap) return false;

    std::vector<uint8_t> dirStream;
    dirStream.reserve(std::min<size_t>(dirBytes, 4 * 1024 * 1024));
    for (uint32_t i = 0; i < numDirBlocks && dirStream.size() < dirBytes; ++i) {
        const uint32_t blkIdx = DetailSys100::RdU32LE(blockMap + static_cast<size_t>(i) * 4);
        const uint64_t bOff   = static_cast<uint64_t>(blkIdx) * blkSize;
        if (bOff + blkSize > fileSize) break;
        const size_t take = std::min<size_t>(blkSize, dirBytes - dirStream.size());
        const uint8_t* b = src.Read(bOff, take, blockBuf);
        if (!b) break;
        dirStream.insert(dirStream.end(), b, b + take);
    }

    uint32_t numStreams = 0;
    std::vector<uint32_t> streamSizes;
    std::vector<std::vector<uint32_t>> streamBlocks;

    if (dirStream.size() >= 4) {
        numStreams = std::min<uint32_t>(DetailSys100::RdU32LE(dirStream.data()), 16384u);
        size_t dp = 4;
        if (dp + static_cast<size_t>(numStreams) * 4 <= dirStream.size()) {
            streamSizes.resize(numStreams);
            for (uint32_t s = 0; s < numStreams; ++s) {
                const uint32_t sz = DetailSys100::RdU32LE(dirStream.data() + dp + s * 4);
                streamSizes[s] = (sz == 0xFFFFFFFFu) ? 0u : sz;
            }
            dp += static_cast<size_t>(numStreams) * 4;
            streamBlocks.resize(numStreams);
            for (uint32_t s = 0; s < numStreams; ++s) {
                // every stream's block list is stepped over (a large PDB's type stream alone can span more than
                // 65536 blocks); only those of the streams read are kept: 1 (PDB info) and 3 (DBI)
                const uint64_t nb = (static_cast<uint64_t>(streamSizes[s]) + blkSize - 1u) / blkSize;
                if (nb * 4 > dirStream.size() - dp) break;
                if (s == 1 || s == 3) {
                    streamBlocks[s].resize(static_cast<size_t>(nb));
                    for (size_t b = 0; b < nb; ++b) {
                        streamBlocks[s][b] = DetailSys100::RdU32LE(dirStream.data() + dp + b * 4);
                    }
                }
                dp += static_cast<size_t>(nb) * 4;
            }
        }
    }

    auto readStream = [&](uint32_t sIdx, size_t maxOut = 4 * 1024 * 1024) -> std::vector<uint8_t> {
        std::vector<uint8_t> res;
        if (sIdx >= streamSizes.size() || sIdx >= streamBlocks.size() || streamSizes[sIdx] == 0) return res;
        const size_t want = std::min<size_t>(streamSizes[sIdx], maxOut);
        res.reserve(want);
        for (uint32_t blkIdx : streamBlocks[sIdx]) {
            if (res.size() >= want) break;
            const uint64_t bOff = static_cast<uint64_t>(blkIdx) * blkSize;
            if (bOff >= fileSize) break;
            const size_t take = static_cast<size_t>(std::min<uint64_t>({ blkSize, fileSize - bOff, want - res.size() }));
            const uint8_t* b = src.Read(bOff, take, blockBuf);
            if (!b) break;
            res.insert(res.end(), b, b + take);
        }
        return res;
    };

    SpreadsheetSheet hdrSh;
    hdrSh.name = L"PDB_MSF7";
    hdrSh.maxCols = 2;
    hdrSh.rows = {
        { L"MSF / PDB Field", L"Value" },
        { L"Signature", L"Microsoft C/C++ MSF 7.00" },
        { L"Page / Block Size", std::to_wstring(blkSize) + L" B" },
        { L"Total Blocks", std::to_wstring(numBlocks) },
        { L"Stream Directory Size", PathUtils::FormatByteSize(dirBytes) },
        { L"Total Streams", std::to_wstring(numStreams) }
    };

    // Stream 1: PDB Info Stream (Version, Signature, Age, GUID)
    std::vector<uint8_t> s1 = readStream(1, 65536);
    if (s1.size() >= 28) {
        const uint32_t pdbVer = DetailSys100::RdU32LE(s1.data() + 0);
        const uint32_t pdbSig = DetailSys100::RdU32LE(s1.data() + 4);
        const uint32_t pdbAge = DetailSys100::RdU32LE(s1.data() + 8);
        const std::wstring guidStr = DetailSys100::FormatGuidLE(s1.data() + 12);
        hdrSh.rows.push_back({ L"PDB Stream Version", std::to_wstring(pdbVer) });
        hdrSh.rows.push_back({ L"PDB Timestamp", DetailSys100::FormatUnixTimeUtc(pdbSig) });
        hdrSh.rows.push_back({ L"PDB Age", std::to_wstring(pdbAge) });
        hdrSh.rows.push_back({ L"PDB GUID", guidStr });
    }

    // Stream 3: DBI Debug Info Stream -> ModInfo compiled .obj modules
    SpreadsheetSheet modSh;
    modSh.name = L"PDB_Modules";
    modSh.maxCols = 3;
    modSh.rows.push_back({ L"Module #", L"Module / Object Name", L"Archive / Library Path" });

    std::vector<uint8_t> s3 = readStream(3, 8 * 1024 * 1024);
    if (s3.size() >= 64) {
        const int32_t modInfoSz = static_cast<int32_t>(DetailSys100::RdU32LE(s3.data() + 24));
        const uint16_t machine  = DetailSys100::RdU16LE(s3.data() + 60);
        std::wstring machStr = DetailSys100::Hex32(machine);
        if (machine == 0x8664) machStr = L"x64 / AMD64 (0x8664)";
        else if (machine == 0x014C) machStr = L"x86 / i386 (0x014C)";
        else if (machine == 0xAA64) machStr = L"ARM64 (0xAA64)";
        hdrSh.rows.push_back({ L"DBI Target Machine", machStr });

        if (modInfoSz > 0) {
            const size_t endMod = std::min<size_t>(s3.size(), 64 + static_cast<size_t>(modInfoSz));
            size_t mp = 64;
            int mIdx = 0;
            while (mp + 64 < endMod && modSh.rows.size() <= 3000) {
                size_t sPos = mp + 64;
                size_t n1 = 0;
                while (sPos + n1 < endMod && s3[sPos + n1] != 0) ++n1;
                if (sPos + n1 >= endMod) break;
                std::string modName(reinterpret_cast<const char*>(s3.data() + sPos), n1);
                sPos += n1 + 1;

                size_t n2 = 0;
                while (sPos + n2 < endMod && s3[sPos + n2] != 0) ++n2;
                if (sPos + n2 >= endMod) break;
                std::string objName(reinterpret_cast<const char*>(s3.data() + sPos), n2);
                sPos += n2 + 1;

                modSh.rows.push_back({
                    std::to_wstring(mIdx++),
                    PathUtils::Utf8ToWide(modName),
                    PathUtils::Utf8ToWide(objName)
                });

                mp = (sPos + 3u) & ~size_t(3u);
            }
        }
    }
    hdrSh.rows.push_back({ L"Compiled Object Modules", std::to_wstring(modSh.rows.size() - 1) });

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Microsoft Program Database Symbols (.pdb)";
    out.sheets.push_back(std::move(hdrSh));
    if (modSh.rows.size() > 1) out.sheets.push_back(std::move(modSh));
    if (!src.ReadFailed()) out.sourceReadFully = true; // every stream shown was read, wherever its blocks lie
    out.valid = true;
    return true;
}

// ============================================================================
// 43. Java JVM Bytecode Class File (.class — 0xCAFEBABE Constant Pool, Fields & Methods)
// ============================================================================
inline bool ParseJavaClassFile(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 24 || DetailSys100::RdU32BE(raw.data()) != 0xCAFEBABEu) return false;

    const uint16_t minorV = DetailSys100::RdU16BE(raw.data() + 4);
    const uint16_t majorV = DetailSys100::RdU16BE(raw.data() + 6);
    const uint16_t cpCnt  = DetailSys100::RdU16BE(raw.data() + 8);
    if (majorV < 45 || majorV > 100 || cpCnt < 1) return false;

    std::vector<std::string> cpUtf8(cpCnt);
    std::vector<uint16_t> cpClassRef(cpCnt, 0);
    size_t pos = 10;

    for (uint16_t i = 1; i < cpCnt; ++i) {
        if (pos >= raw.size()) return false;
        const uint8_t tag = raw[pos++];
        switch (tag) {
            case 1: { // CONSTANT_Utf8
                if (pos + 2 > raw.size()) return false;
                const uint16_t len = DetailSys100::RdU16BE(raw.data() + pos);
                pos += 2;
                if (pos + len > raw.size()) return false;
                cpUtf8[i].assign(reinterpret_cast<const char*>(raw.data() + pos), len);
                pos += len;
                break;
            }
            case 3: // Integer
            case 4: // Float
            case 9: // Fieldref
            case 10:// Methodref
            case 11:// InterfaceMethodref
            case 12:// NameAndType
            case 17:// Dynamic
            case 18:// InvokeDynamic
                if (pos + 4 > raw.size()) return false;
                pos += 4;
                break;
            case 5: // Long (2 slots!)
            case 6: // Double (2 slots!)
                if (pos + 8 > raw.size()) return false;
                pos += 8;
                ++i;
                break;
            case 7: // Class
            case 19:// Module
            case 20:// Package
                if (pos + 2 > raw.size()) return false;
                cpClassRef[i] = DetailSys100::RdU16BE(raw.data() + pos);
                pos += 2;
                break;
            case 8: // String
            case 16:// MethodType
                if (pos + 2 > raw.size()) return false;
                pos += 2;
                break;
            case 15:// MethodHandle
                if (pos + 3 > raw.size()) return false;
                pos += 3;
                break;
            default:
                return false;
        }
    }

    if (pos + 8 > raw.size()) return false;
    const uint16_t accessFlags = DetailSys100::RdU16BE(raw.data() + pos + 0);
    const uint16_t thisClass   = DetailSys100::RdU16BE(raw.data() + pos + 2);
    const uint16_t superClass  = DetailSys100::RdU16BE(raw.data() + pos + 4);
    const uint16_t ifaceCount  = DetailSys100::RdU16BE(raw.data() + pos + 6);
    pos += 8;

    auto getUtf8 = [&](uint16_t idx) -> std::wstring {
        if (idx > 0 && idx < cpUtf8.size()) return PathUtils::Utf8ToWide(cpUtf8[idx]);
        return L"";
    };
    auto getClassName = [&](uint16_t clsIdx) -> std::wstring {
        if (clsIdx == 0 || clsIdx >= cpClassRef.size()) return L"(none)";
        std::wstring s = getUtf8(cpClassRef[clsIdx]);
        for (wchar_t& ch : s) if (ch == L'/') ch = L'.';
        return s.empty() ? L"(unknown)" : s;
    };

    if (pos + static_cast<size_t>(ifaceCount) * 2 > raw.size()) return false;
    std::wstring ifaceList;
    for (uint16_t i = 0; i < ifaceCount; ++i) {
        const uint16_t ii = DetailSys100::RdU16BE(raw.data() + pos + i * 2);
        if (i > 0) ifaceList += L", ";
        ifaceList += getClassName(ii);
    }
    pos += static_cast<size_t>(ifaceCount) * 2;

    auto fmtMemberFlags = [](uint16_t f, bool isMethod) -> std::wstring {
        std::wstring s;
        if (f & 0x0001u) s += L"public ";
        if (f & 0x0002u) s += L"private ";
        if (f & 0x0004u) s += L"protected ";
        if (f & 0x0008u) s += L"static ";
        if (f & 0x0010u) s += L"final ";
        if (isMethod && (f & 0x0020u)) s += L"synchronized ";
        if (!isMethod && (f & 0x0040u)) s += L"volatile ";
        if (isMethod && (f & 0x0100u)) s += L"native ";
        if (f & 0x0400u) s += L"abstract ";
        return PathUtils::Trim(s);
    };

    SpreadsheetSheet memSh;
    memSh.name = L"Fields_And_Methods";
    memSh.maxCols = 5;
    memSh.rows.push_back({ L"Member Kind", L"Modifiers", L"Name", L"JVM Descriptor", L"Bytecode / Details" });

    uint16_t fieldsCnt = 0, methodsCnt = 0;
    if (pos + 2 <= raw.size()) {
        fieldsCnt = DetailSys100::RdU16BE(raw.data() + pos);
        pos += 2;
        for (uint16_t f = 0; f < fieldsCnt && pos + 8 <= raw.size(); ++f) {
            const uint16_t ff   = DetailSys100::RdU16BE(raw.data() + pos + 0);
            const uint16_t fn   = DetailSys100::RdU16BE(raw.data() + pos + 2);
            const uint16_t fd   = DetailSys100::RdU16BE(raw.data() + pos + 4);
            const uint16_t acnt = DetailSys100::RdU16BE(raw.data() + pos + 6);
            pos += 8;
            for (uint16_t a = 0; a < acnt && pos + 6 <= raw.size(); ++a) {
                const uint32_t alen = DetailSys100::RdU32BE(raw.data() + pos + 2);
                pos += 6;
                if (pos + alen > raw.size()) { pos = raw.size(); break; }
                pos += alen;
            }
            if (memSh.rows.size() <= 2000) {
                memSh.rows.push_back({ L"Field", fmtMemberFlags(ff, false), getUtf8(fn), getUtf8(fd), L"-" });
            }
        }
    }

    if (pos + 2 <= raw.size()) {
        methodsCnt = DetailSys100::RdU16BE(raw.data() + pos);
        pos += 2;
        for (uint16_t m = 0; m < methodsCnt && pos + 8 <= raw.size(); ++m) {
            const uint16_t mf   = DetailSys100::RdU16BE(raw.data() + pos + 0);
            const uint16_t mn   = DetailSys100::RdU16BE(raw.data() + pos + 2);
            const uint16_t md   = DetailSys100::RdU16BE(raw.data() + pos + 4);
            const uint16_t acnt = DetailSys100::RdU16BE(raw.data() + pos + 6);
            pos += 8;
            std::wstring codeInfo = L"(abstract/native)";
            for (uint16_t a = 0; a < acnt && pos + 6 <= raw.size(); ++a) {
                const uint16_t aname = DetailSys100::RdU16BE(raw.data() + pos + 0);
                const uint32_t alen  = DetailSys100::RdU32BE(raw.data() + pos + 2);
                pos += 6;
                if (pos + alen > raw.size()) { pos = raw.size(); break; }
                if (aname < cpUtf8.size() && cpUtf8[aname] == "Code") {
                    if (majorV >= 46 && alen >= 8) {
                        const uint16_t maxStack  = DetailSys100::RdU16BE(raw.data() + pos + 0);
                        const uint16_t maxLocals = DetailSys100::RdU16BE(raw.data() + pos + 2);
                        const uint32_t codeLen   = DetailSys100::RdU32BE(raw.data() + pos + 4);
                        codeInfo = L"stack=" + std::to_wstring(maxStack) + L", locals=" + std::to_wstring(maxLocals) + L", code=" + std::to_wstring(codeLen) + L" B";
                    } else if (alen >= 4) {
                        codeInfo = L"stack=" + std::to_wstring(raw[pos]) + L", locals=" + std::to_wstring(raw[pos + 1]) + L", code=" + std::to_wstring(DetailSys100::RdU16BE(raw.data() + pos + 2)) + L" B";
                    }
                }
                pos += alen;
            }
            if (memSh.rows.size() <= 2000) {
                memSh.rows.push_back({ L"Method", fmtMemberFlags(mf, true), getUtf8(mn), getUtf8(md), codeInfo });
            }
        }
    }

    std::wstring sourceFile;
    if (pos + 2 <= raw.size()) {
        const uint16_t clsAttrs = DetailSys100::RdU16BE(raw.data() + pos);
        pos += 2;
        for (uint16_t a = 0; a < clsAttrs && pos + 6 <= raw.size(); ++a) {
            const uint16_t aname = DetailSys100::RdU16BE(raw.data() + pos + 0);
            const uint32_t alen  = DetailSys100::RdU32BE(raw.data() + pos + 2);
            pos += 6;
            if (pos + alen > raw.size()) break;
            if (aname < cpUtf8.size() && cpUtf8[aname] == "SourceFile" && alen == 2) {
                sourceFile = getUtf8(DetailSys100::RdU16BE(raw.data() + pos));
            }
            pos += alen;
        }
    }

    std::wstring jdkVer = (majorV >= 49) ? (L"Java SE " + std::to_wstring(majorV - 44)) : (L"Java 1." + std::to_wstring(majorV - 44));
    std::wstring clsFlags;
    if (accessFlags & 0x0001u) clsFlags += L"public ";
    if (accessFlags & 0x0010u) clsFlags += L"final ";
    if (accessFlags & 0x0200u) clsFlags += L"interface ";
    if (accessFlags & 0x0400u) clsFlags += L"abstract ";
    if (accessFlags & 0x2000u) clsFlags += L"@interface ";
    if (accessFlags & 0x4000u) clsFlags += L"enum ";
    if (accessFlags & 0x8000u) clsFlags += L"module ";

    SpreadsheetSheet clsSh;
    clsSh.name = L"JVM_Class";
    clsSh.maxCols = 2;
    clsSh.rows = {
        { L"JVM Class Property", L"Value" },
        { L"Class File Version", std::to_wstring(majorV) + L"." + std::to_wstring(minorV) + L" (" + jdkVer + L")" },
        { L"Class Name", getClassName(thisClass) },
        { L"Superclass", getClassName(superClass) },
        { L"Class Modifiers", clsFlags.empty() ? L"(package-private)" : PathUtils::Trim(clsFlags) },
        { L"Implemented Interfaces", ifaceList.empty() ? L"(none)" : ifaceList },
        { L"Constant Pool Count", std::to_wstring(cpCnt) },
        { L"Declared Fields / Methods", std::to_wstring(fieldsCnt) + L" / " + std::to_wstring(methodsCnt) }
    };
    if (!sourceFile.empty()) clsSh.rows.push_back({ L"Source File", sourceFile });

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Java Bytecode Class (.class)";
    out.sheets.push_back(std::move(clsSh));
    if (memSh.rows.size() > 1) out.sheets.push_back(std::move(memSh));
    out.valid = true;
    return true;
}

// ============================================================================
// 44. Android Dalvik Executable (.dex — dex\n035\0 .. 041\0)
// ============================================================================
inline bool ParseAndroidDexFile(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 112 || std::memcmp(raw.data(), "dex\n", 4) != 0 || raw[7] != 0) return false;
    const uint32_t hdrSize   = DetailSys100::RdU32LE(raw.data() + 36);
    const uint32_t endianTag = DetailSys100::RdU32LE(raw.data() + 40);
    if (hdrSize != 0x70u || endianTag != 0x12345678u) return false;

    const std::wstring dexVer = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + 4), 3));
    const uint32_t strCnt   = DetailSys100::RdU32LE(raw.data() + 56);
    const uint32_t strOff   = DetailSys100::RdU32LE(raw.data() + 60);
    const uint32_t typCnt   = DetailSys100::RdU32LE(raw.data() + 64);
    const uint32_t typOff   = DetailSys100::RdU32LE(raw.data() + 68);
    const uint32_t prtCnt   = DetailSys100::RdU32LE(raw.data() + 72);
    const uint32_t prtOff   = DetailSys100::RdU32LE(raw.data() + 76);
    const uint32_t fldCnt   = DetailSys100::RdU32LE(raw.data() + 80);
    const uint32_t mthCnt   = DetailSys100::RdU32LE(raw.data() + 88);
    const uint32_t mthOff   = DetailSys100::RdU32LE(raw.data() + 92);
    const uint32_t clsCnt   = DetailSys100::RdU32LE(raw.data() + 96);
    const uint32_t clsOff   = DetailSys100::RdU32LE(raw.data() + 100);

    auto getDexStr = [&](uint32_t idx) -> std::wstring {
        if (idx >= strCnt || static_cast<uint64_t>(strOff) + static_cast<uint64_t>(idx) * 4ULL + 4ULL > raw.size()) return L"";
        size_t p = DetailSys100::RdU32LE(raw.data() + strOff + idx * 4);
        if (p >= raw.size()) return L"";
        // Skip ULEB128 utf16_size
        for (int k = 0; k < 5 && p < raw.size(); ++k) {
            if ((raw[p++] & 0x80u) == 0) break;
        }
        std::string s;
        while (p < raw.size() && raw[p] != 0 && s.size() < 512) {
            if (raw[p] == 0xC0 && p + 1 < raw.size() && raw[p + 1] == 0x80) { p += 2; continue; }
            s.push_back(static_cast<char>(raw[p++]));
        }
        return PathUtils::Utf8ToWide(s);
    };

    auto prettyDesc = [](std::wstring d) -> std::wstring {
        int arr = 0;
        while (!d.empty() && d[0] == L'[') { ++arr; d.erase(d.begin()); }
        std::wstring base = d;
        if (d == L"V") base = L"void";
        else if (d == L"Z") base = L"boolean";
        else if (d == L"B") base = L"byte";
        else if (d == L"S") base = L"short";
        else if (d == L"C") base = L"char";
        else if (d == L"I") base = L"int";
        else if (d == L"J") base = L"long";
        else if (d == L"F") base = L"float";
        else if (d == L"D") base = L"double";
        else if (d.size() > 2 && d.front() == L'L' && d.back() == L';') {
            base = d.substr(1, d.size() - 2);
            for (wchar_t& c : base) if (c == L'/') c = L'.';
        }
        for (int i = 0; i < arr; ++i) base += L"[]";
        return base;
    };

    auto getDexType = [&](uint32_t tIdx) -> std::wstring {
        if (tIdx >= typCnt || static_cast<uint64_t>(typOff) + static_cast<uint64_t>(tIdx) * 4ULL + 4ULL > raw.size()) return L"";
        const uint32_t sIdx = DetailSys100::RdU32LE(raw.data() + typOff + tIdx * 4);
        return prettyDesc(getDexStr(sIdx));
    };

    auto getDexProto = [&](uint32_t pIdx) -> std::wstring {
        if (pIdx >= prtCnt || static_cast<uint64_t>(prtOff) + static_cast<uint64_t>(pIdx) * 12ULL + 12ULL > raw.size()) return L"()";
        const size_t pp = prtOff + static_cast<size_t>(pIdx) * 12;
        const uint32_t retTypeIdx = DetailSys100::RdU32LE(raw.data() + pp + 4);
        const uint32_t paramsOff  = DetailSys100::RdU32LE(raw.data() + pp + 8);
        std::wstring sig = L"(";
        if (paramsOff > 0 && static_cast<uint64_t>(paramsOff) + 4ULL <= raw.size()) {
            const uint32_t nParams = std::min<uint32_t>(DetailSys100::RdU32LE(raw.data() + paramsOff), 64u);
            for (uint32_t k = 0; k < nParams && static_cast<uint64_t>(paramsOff) + 4ULL + (k + 1) * 2ULL <= raw.size(); ++k) {
                if (k > 0) sig += L", ";
                sig += getDexType(DetailSys100::RdU16LE(raw.data() + paramsOff + 4 + k * 2));
            }
        }
        sig += L"): " + getDexType(retTypeIdx);
        return sig;
    };

    SpreadsheetSheet hdrSh;
    hdrSh.name = L"DEX_Header";
    hdrSh.maxCols = 2;
    hdrSh.rows = {
        { L"DEX Header Field", L"Value" },
        { L"DEX Version", L"0" + dexVer },
        { L"String IDs", std::to_wstring(strCnt) },
        { L"Type IDs", std::to_wstring(typCnt) },
        { L"Proto IDs", std::to_wstring(prtCnt) },
        { L"Field IDs", std::to_wstring(fldCnt) },
        { L"Method IDs", std::to_wstring(mthCnt) },
        { L"Class Definitions", std::to_wstring(clsCnt) }
    };

    SpreadsheetSheet clsSh;
    clsSh.name = L"DEX_Classes";
    clsSh.maxCols = 4;
    clsSh.rows.push_back({ L"#", L"Class Name", L"Superclass", L"Source File" });
    for (uint32_t i = 0; i < std::min(clsCnt, 2000u); ++i) {
        const size_t cp = clsOff + static_cast<size_t>(i) * 32;
        if (cp + 32 > raw.size()) break;
        const uint32_t cIdx   = DetailSys100::RdU32LE(raw.data() + cp + 0);
        const uint32_t supIdx = DetailSys100::RdU32LE(raw.data() + cp + 8);
        const uint32_t srcIdx = DetailSys100::RdU32LE(raw.data() + cp + 16);
        clsSh.rows.push_back({
            std::to_wstring(i),
            getDexType(cIdx),
            (supIdx == 0xFFFFFFFFu) ? L"(none)" : getDexType(supIdx),
            (srcIdx == 0xFFFFFFFFu) ? L"-" : getDexStr(srcIdx)
        });
    }

    SpreadsheetSheet mthSh;
    mthSh.name = L"DEX_Methods";
    mthSh.maxCols = 3;
    mthSh.rows.push_back({ L"Defining Class", L"Method Name", L"Prototype" });
    for (uint32_t i = 0; i < std::min(mthCnt, 2000u); ++i) {
        const size_t mp = mthOff + static_cast<size_t>(i) * 8;
        if (mp + 8 > raw.size()) break;
        const uint16_t cIdx = DetailSys100::RdU16LE(raw.data() + mp + 0);
        const uint16_t pIdx = DetailSys100::RdU16LE(raw.data() + mp + 2);
        const uint32_t nIdx = DetailSys100::RdU32LE(raw.data() + mp + 4);
        mthSh.rows.push_back({ getDexType(cIdx), getDexStr(nIdx), getDexProto(pIdx) });
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Android Dalvik Executable (.dex)";
    out.sheets.push_back(std::move(hdrSh));
    if (clsSh.rows.size() > 1) out.sheets.push_back(std::move(clsSh));
    if (mthSh.rows.size() > 1) out.sheets.push_back(std::move(mthSh));
    out.valid = true;
    return true;
}

// ============================================================================
// 45. Apple Mach-O 32/64 & Universal Fat Binary (.macho / .dylib / .so)
// ============================================================================
inline bool IsValidMachOMagic(uint32_t m) {
    return m == 0xFEEDFACEu || m == 0xFEEDFACFu || m == 0xCEFAEDFEu || m == 0xCFFAEDFEu;
}

// fileSize: the whole file's, when raw holds only its start (a slice past raw is checked when it is read)
inline bool IsValidMachOFatArchive(const std::vector<uint8_t>& raw, uint64_t fileSize = 0) {
    if (raw.size() < 28) return false;
    fileSize = std::max<uint64_t>(fileSize, raw.size());
    const uint32_t m = DetailSys100::RdU32BE(raw.data());
    if (m != 0xCAFEBABEu && m != 0xCAFEBABFu) return false;
    const uint32_t nArch = DetailSys100::RdU32BE(raw.data() + 4);
    if (nArch == 0 || nArch > 32) return false;
    const size_t entSz = (m == 0xCAFEBABFu) ? 32 : 20;
    if (8 + static_cast<size_t>(nArch) * entSz > raw.size()) return false;
    for (uint32_t i = 0; i < nArch; ++i) {
        const uint8_t* ap = raw.data() + 8 + i * entSz;
        const uint64_t off = (m == 0xCAFEBABFu) ? DetailSys100::RdU64BE(ap + 8) : DetailSys100::RdU32BE(ap + 8);
        if (off > fileSize || fileSize - off < 28) return false;
        if (off > raw.size() || raw.size() - off < 28) continue;
        const uint32_t sliceMagic = DetailSys100::RdU32LE(raw.data() + static_cast<size_t>(off));
        if (!IsValidMachOMagic(sliceMagic)) return false;
    }
    return true;
}

inline bool ParseAppleMachOBinary(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    auto cpuName = [](uint32_t cpu) -> std::wstring {
        switch (cpu) {
            case 7:           return L"x86 / i386";
            case 0x01000007u: return L"x86_64";
            case 12:          return L"ARM (32-bit)";
            case 0x0100000Cu: return L"ARM64 / Apple Silicon";
            case 18:          return L"PowerPC";
            case 0x01000012u: return L"PowerPC 64";
            default:          return DetailSys100::Hex32(cpu);
        }
    };

    SpreadsheetSheet hdrSh;
    hdrSh.name = L"MachO_Header";
    hdrSh.maxCols = 2;
    hdrSh.rows.push_back({ L"Mach-O Property", L"Value" });

    SpreadsheetSheet cmdSh;
    cmdSh.name = L"Load_Commands";
    cmdSh.maxCols = 4;
    cmdSh.rows.push_back({ L"Slice", L"Command", L"Target / Segment", L"Addresses / Details" });

    SpreadsheetSheet symSh;
    symSh.name = L"MachO_Symbols";
    symSh.maxCols = 4;
    symSh.rows.push_back({ L"Slice", L"Symbol Name", L"Address", L"Type" });

    // A universal binary's slices follow one another, each with its symbol and string tables at its end: in a large
    // binary they lie past the 32 MB a reader is handed, so slices and tables are read through SourceBytes
    SourceBytes src(raw, out);
    const uint64_t fileSize = src.Size();

    auto parseThinSlice = [&](uint64_t baseOff, uint64_t sliceSz, const std::wstring& sliceLabel) -> bool {
        if (sliceSz < 28 || baseOff > fileSize || sliceSz > fileSize - baseOff) return false;
        std::vector<uint8_t> hdrBuf;
        const uint8_t* hdr = src.Read(baseOff, 28, hdrBuf);
        if (!hdr) return false;
        const uint32_t magicLE = DetailSys100::RdU32LE(hdr);
        if (!IsValidMachOMagic(magicLE)) return false;

        const bool is64 = (magicLE == 0xFEEDFACFu || magicLE == 0xCFFAEDFEu);
        const bool isBE = (magicLE == 0xCEFAEDFEu || magicLE == 0xCFFAEDFEu);
        const size_t machHdrSz = is64 ? 32 : 28;
        if (sliceSz < machHdrSz) return false;
        auto ld32 = [&](const uint8_t* p) -> uint32_t { return isBE ? DetailSys100::RdU32BE(p) : DetailSys100::RdU32LE(p); };
        auto ld64 = [&](const uint8_t* p) -> uint64_t { return isBE ? DetailSys100::RdU64BE(p) : DetailSys100::RdU64LE(p); };
        const uint32_t sizeofcmds = ld32(hdr + 20);
        if (machHdrSz + sizeofcmds > sliceSz || sizeofcmds > (16u << 20)) return false;
        std::vector<uint8_t> cmdBuf; // the header and the load commands
        const uint8_t* base = src.Read(baseOff, machHdrSz + sizeofcmds, cmdBuf);
        if (!base) return false;

        auto rd32 = [&](size_t o) -> uint32_t { return ld32(base + o); };
        auto rd64 = [&](size_t o) -> uint64_t { return ld64(base + o); };

        const uint32_t cpuType    = rd32(4);
        const uint32_t fileType   = rd32(12);
        const uint32_t ncmds      = rd32(16);
        const uint32_t flags      = rd32(24);

        std::wstring ftStr = std::to_wstring(fileType);
        if (fileType == 1) ftStr = L"MH_OBJECT (Relocatable)";
        else if (fileType == 2) ftStr = L"MH_EXECUTE (Executable)";
        else if (fileType == 6) ftStr = L"MH_DYLIB (Dynamic Library)";
        else if (fileType == 8) ftStr = L"MH_BUNDLE";
        else if (fileType == 10) ftStr = L"MH_DSYM (Debug Symbols)";

        hdrSh.rows.push_back({ sliceLabel + L" Architecture", cpuName(cpuType) + (is64 ? L" (64-bit)" : L" (32-bit)") });
        hdrSh.rows.push_back({ sliceLabel + L" File Type", ftStr });
        hdrSh.rows.push_back({ sliceLabel + L" Load Commands", std::to_wstring(ncmds) + L" (" + std::to_wstring(sizeofcmds) + L" B)" });
        hdrSh.rows.push_back({ sliceLabel + L" Header Flags", DetailSys100::Hex32(flags) });

        size_t lp = machHdrSz;
        const size_t lEnd = machHdrSz + sizeofcmds;
        for (uint32_t c = 0; c < ncmds && c < 2048 && lp + 8 <= lEnd; ++c) {
            const uint32_t cmd   = rd32(lp + 0);
            const uint32_t cmdSz = rd32(lp + 4);
            if (cmdSz < 8 || lp + cmdSz > lEnd) break;

            if ((cmd == 0x19u && cmdSz >= 72) || (cmd == 0x01u && cmdSz >= 56)) { // LC_SEGMENT_64 / LC_SEGMENT
                const bool seg64 = (cmd == 0x19u);
                std::string segName(reinterpret_cast<const char*>(base + lp + 8), strnlen(reinterpret_cast<const char*>(base + lp + 8), 16));
                const uint64_t vmaddr = seg64 ? rd64(lp + 24) : rd32(lp + 24);
                const uint64_t vmsize = seg64 ? rd64(lp + 32) : rd32(lp + 28);
                const uint32_t nsects = seg64 ? rd32(lp + 64) : rd32(lp + 48);
                std::wstring secList;
                size_t sp = lp + (seg64 ? 72 : 56);
                const size_t secEntSz = seg64 ? 80 : 68;
                for (uint32_t s = 0; s < nsects && s < 64 && sp + secEntSz <= lp + cmdSz; ++s, sp += secEntSz) {
                    std::string sn(reinterpret_cast<const char*>(base + sp), strnlen(reinterpret_cast<const char*>(base + sp), 16));
                    if (!secList.empty()) secList += L", ";
                    secList += PathUtils::Utf8ToWide(sn);
                }
                cmdSh.rows.push_back({
                    sliceLabel,
                    seg64 ? L"LC_SEGMENT_64" : L"LC_SEGMENT",
                    PathUtils::Utf8ToWide(segName),
                    DetailSys100::Hex64(vmaddr) + L" (" + PathUtils::FormatByteSize(vmsize) + L")" + (secList.empty() ? L"" : (L" [" + secList + L"]"))
                });
            } else if ((cmd == 0x0Cu || cmd == 0x0Du || cmd == 0x80000018u || cmd == 0x8000001Cu) && cmdSz >= 12) {
                const uint32_t strOff = rd32(lp + 8);
                std::string dylib;
                if (strOff < cmdSz) {
                    const char* name = reinterpret_cast<const char*>(base + lp + strOff);
                    dylib.assign(name, strnlen(name, cmdSz - strOff));
                }
                const wchar_t* cname = (cmd == 0x0Cu) ? L"LC_LOAD_DYLIB" :
                                       (cmd == 0x0Du) ? L"LC_ID_DYLIB" :
                                       (cmd == 0x80000018u) ? L"LC_LOAD_WEAK_DYLIB" : L"LC_RPATH";
                cmdSh.rows.push_back({ sliceLabel, cname, PathUtils::Utf8ToWide(dylib), L"-" });
            } else if (cmd == 0x80000028u && cmdSz >= 16) { // LC_MAIN
                const uint64_t entryOff = rd64(lp + 8);
                cmdSh.rows.push_back({ sliceLabel, L"LC_MAIN", L"Entry Offset", DetailSys100::Hex64(entryOff) });
            } else if (cmd == 0x1Bu && cmdSz >= 24) { // LC_UUID
                cmdSh.rows.push_back({ sliceLabel, L"LC_UUID", L"Image UUID", DetailSys100::HexBytes(base + lp + 8, 16, L'-') });
            } else if (cmd == 0x02u && cmdSz >= 24) { // LC_SYMTAB
                const uint32_t symOff  = rd32(lp + 8);
                const uint32_t nSyms   = rd32(lp + 12);
                const uint32_t strOff  = rd32(lp + 16);
                const uint32_t strSize = rd32(lp + 20);
                cmdSh.rows.push_back({ sliceLabel, L"LC_SYMTAB", std::to_wstring(nSyms) + L" symbols", L"symoff=" + std::to_wstring(symOff) });

                const size_t nlSz = is64 ? 16 : 12;
                if (symOff < sliceSz && strOff < sliceSz) {
                    // far more entries and names than the rows shown
                    std::vector<uint8_t> symBuf, strBuf;
                    size_t symAvail = 0, strAvail = 0;
                    const uint8_t* syms = src.ReadSome(baseOff + symOff, std::min<uint64_t>(static_cast<uint64_t>(nSyms) * nlSz, sliceSz - symOff),
                                                       4u << 20, symBuf, symAvail);
                    const uint8_t* strs = src.ReadSome(baseOff + strOff, std::min<uint64_t>(strSize, sliceSz - strOff), 4u << 20, strBuf, strAvail);
                    for (uint32_t si = 0; syms && si < nSyms && symSh.rows.size() <= 2000; ++si) {
                        const size_t np = static_cast<size_t>(si) * nlSz;
                        if (np + nlSz > symAvail) break;
                        const uint32_t strx  = ld32(syms + np);
                        const uint8_t  ntype = syms[np + 4];
                        const uint64_t nval  = is64 ? ld64(syms + np + 8) : ld32(syms + np + 8);
                        if ((ntype & 0xE0u) != 0) continue; // skip STAB debug entries
                        std::string sname;
                        if (strs && strx < strAvail) {
                            const char* name = reinterpret_cast<const char*>(strs + strx);
                            sname.assign(name, strnlen(name, std::min<size_t>(256, strAvail - strx)));
                        }
                        if (sname.empty()) continue;
                        const wchar_t* kind = ((ntype & 0x0Eu) == 0u) ? L"UNDEF" : ((ntype & 0x01u) ? L"EXT" : L"LOCAL");
                        symSh.rows.push_back({ sliceLabel, PathUtils::Utf8ToWide(sname), DetailSys100::Hex64(nval), kind });
                    }
                }
            }
            lp += cmdSz;
        }
        return true;
    };

    if (IsValidMachOFatArchive(raw, out.sourceFileSize)) {
        const uint32_t m = DetailSys100::RdU32BE(raw.data());
        const uint32_t nArch = DetailSys100::RdU32BE(raw.data() + 4);
        const size_t entSz = (m == 0xCAFEBABFu) ? 32 : 20;
        hdrSh.rows.push_back({ L"Container", L"Universal Fat Binary (" + std::to_wstring(nArch) + L" architectures)" });
        for (uint32_t i = 0; i < nArch; ++i) {
            const uint8_t* ap = raw.data() + 8 + i * entSz;
            const uint32_t cpu = DetailSys100::RdU32BE(ap + 0);
            const uint64_t off = (m == 0xCAFEBABFu) ? DetailSys100::RdU64BE(ap + 8) : DetailSys100::RdU32BE(ap + 8);
            const uint64_t sz  = (m == 0xCAFEBABFu) ? DetailSys100::RdU64BE(ap + 16): DetailSys100::RdU32BE(ap + 12);
            if (off <= fileSize && sz <= fileSize - off) {
                parseThinSlice(off, sz, cpuName(cpu));
            }
        }
    } else if (raw.size() >= 28 && IsValidMachOMagic(DetailSys100::RdU32LE(raw.data()))) {
        if (!parseThinSlice(0, fileSize, L"Main")) return false;
    } else {
        return false;
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Apple Mach-O Executable / Dylib";
    out.sheets.push_back(std::move(hdrSh));
    if (cmdSh.rows.size() > 1) out.sheets.push_back(std::move(cmdSh));
    if (symSh.rows.size() > 1) out.sheets.push_back(std::move(symSh));
    if (!src.ReadFailed()) out.sourceReadFully = true; // every slice and table shown was read
    out.valid = true;
    return true;
}

// ============================================================================
// 46 & 47. X.509 Certificates, CSR, PKCS#7 & PKCS#12 (.crt, .cer, .pem, .der, .csr, .p7b, .p12, .pfx)
// ============================================================================
namespace DetailX509 {

inline bool DecodeBase64Clean(std::string_view b64, std::vector<uint8_t>& outBytes) {
    ::Explorer2::Base64::Decode(b64, outBytes);
    return !outBytes.empty();
}

inline std::wstring MapKnownOid(const std::string& oid) {
    if (oid == "2.5.4.3")  return L"commonName (CN)";
    if (oid == "2.5.4.6")  return L"countryName (C)";
    if (oid == "2.5.4.7")  return L"localityName (L)";
    if (oid == "2.5.4.8")  return L"stateOrProvinceName (ST)";
    if (oid == "2.5.4.10") return L"organizationName (O)";
    if (oid == "2.5.4.11") return L"organizationalUnitName (OU)";
    if (oid == "1.2.840.113549.1.9.1")  return L"emailAddress";
    if (oid == "1.2.840.113549.1.1.1")  return L"rsaEncryption";
    if (oid == "1.2.840.113549.1.1.5")  return L"sha1WithRSAEncryption";
    if (oid == "1.2.840.113549.1.1.11") return L"sha256WithRSAEncryption";
    if (oid == "1.2.840.113549.1.1.12") return L"sha384WithRSAEncryption";
    if (oid == "1.2.840.113549.1.1.13") return L"sha512WithRSAEncryption";
    if (oid == "1.2.840.10045.2.1")     return L"ecPublicKey";
    if (oid == "1.2.840.10045.3.1.7")   return L"prime256v1 (secp256r1)";
    if (oid == "1.3.132.0.34")          return L"secp384r1";
    if (oid == "1.2.840.10045.4.3.2")   return L"ecdsa-with-SHA256";
    if (oid == "1.2.840.10045.4.3.3")   return L"ecdsa-with-SHA384";
    if (oid == "1.3.101.112")           return L"Ed25519";
    if (oid == "1.2.840.113549.1.7.1")  return L"pkcs7-data";
    if (oid == "1.2.840.113549.1.7.2")  return L"pkcs7-signedData";
    if (oid == "1.2.840.113549.1.7.6")  return L"pkcs7-encryptedData";
    if (oid == "1.2.840.113549.1.12.10.1.2") return L"pkcs8ShroudedKeyBag";
    if (oid == "1.2.840.113549.1.12.10.1.3") return L"certBag";
    if (oid == "2.5.29.17") return L"subjectAltName";
    if (oid == "2.5.29.19") return L"basicConstraints";
    if (oid == "2.5.29.15") return L"keyUsage";
    return PathUtils::Utf8ToWide(oid);
}

inline std::string DecodeDerOid(const uint8_t* p, size_t len) {
    if (len == 0) return {};
    std::string out = std::to_string(p[0] / 40) + "." + std::to_string(p[0] % 40);
    uint64_t arc = 0;
    for (size_t i = 1; i < len; ++i) {
        arc = (arc << 7) | (p[i] & 0x7Fu);
        if ((p[i] & 0x80u) == 0) {
            out += "." + std::to_string(arc);
            arc = 0;
        }
    }
    return out;
}

inline bool ReadDerTlv(const uint8_t* p, size_t maxLen, uint8_t& tag, size_t& hdrLen, size_t& valLen) {
    if (maxLen < 2) return false;
    tag = p[0];
    if ((p[1] & 0x80u) == 0) {
        valLen = p[1];
        hdrLen = 2;
    } else {
        const size_t nBytes = p[1] & 0x7Fu;
        if (nBytes == 0 || nBytes > 4 || 2 + nBytes > maxLen) return false;
        valLen = 0;
        for (size_t i = 0; i < nBytes; ++i) valLen = (valLen << 8) | p[2 + i];
        hdrLen = 2 + nBytes;
    }
    return hdrLen + valLen <= maxLen;
}

inline void WalkAsn1Der(const uint8_t* p, size_t len, int depth, std::wstring& lastOidLabel, std::vector<std::vector<std::wstring>>& rows) {
    if (depth > 16 || rows.size() > 500) return;
    size_t pos = 0;
    while (pos < len && rows.size() <= 500) {
        uint8_t tag = 0;
        size_t hdrLen = 0, valLen = 0;
        if (!ReadDerTlv(p + pos, len - pos, tag, hdrLen, valLen)) break;
        const uint8_t* val = p + pos + hdrLen;

        if ((tag & 0x20u) != 0) { // Constructed (SEQUENCE / SET / explicit context tag)
            WalkAsn1Der(val, valLen, depth + 1, lastOidLabel, rows);
        } else if (tag == 0x06) { // OBJECT IDENTIFIER
            std::string oid = DecodeDerOid(val, valLen);
            std::wstring mapped = MapKnownOid(oid);
            lastOidLabel = mapped;
            rows.push_back({ L"ASN.1 OID", mapped + L" (" + PathUtils::Utf8ToWide(oid) + L")" });
        } else if (tag == 0x0C || tag == 0x13 || tag == 0x16 || tag == 0x14 || tag == 0x17 || tag == 0x18) {
            std::wstring s = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(val), valLen));
            std::wstring key = lastOidLabel.empty() ? L"ASN.1 String" : lastOidLabel;
            lastOidLabel.clear();
            rows.push_back({ key, s });
        } else if (tag == 0x1E && (valLen % 2 == 0)) { // BMPString (UTF-16BE)
            std::wstring s;
            for (size_t i = 0; i + 2 <= valLen; i += 2) {
                wchar_t wc = static_cast<wchar_t>(DetailSys100::RdU16BE(val + i));
                if (wc != 0) s.push_back(wc);
            }
            rows.push_back({ lastOidLabel.empty() ? L"ASN.1 BMPString" : lastOidLabel, s });
            lastOidLabel.clear();
        } else if (tag == 0x02 && valLen > 0) { // INTEGER
            if (valLen <= 4) {
                uint32_t iv = 0;
                for (size_t i = 0; i < valLen; ++i) iv = (iv << 8) | val[i];
                rows.push_back({ L"ASN.1 Integer", std::to_wstring(iv) });
            } else {
                rows.push_back({ L"ASN.1 BigInteger (" + std::to_wstring(valLen * 8) + L"-bit)",
                    DetailSys100::HexBytes(val, std::min<size_t>(valLen, 24), L':') + (valLen > 24 ? L"..." : L"") });
            }
        } else if (tag == 0x03 && valLen > 1) { // BIT STRING
            rows.push_back({ L"ASN.1 BitString", std::to_wstring((valLen - 1) * 8) + L" bits (" + DetailSys100::HexBytes(val + 1, std::min<size_t>(valLen - 1, 16)) + L"...)" });
        } else if (tag == 0x04 && valLen > 0) { // OCTET STRING
            // Check if encapsulated ASN.1 SEQUENCE inside OCTET STRING
            uint8_t innerTag = 0; size_t ih = 0, iv = 0;
            if (val[0] == 0x30 && ReadDerTlv(val, valLen, innerTag, ih, iv) && ih + iv == valLen) {
                WalkAsn1Der(val, valLen, depth + 1, lastOidLabel, rows);
            } else {
                rows.push_back({ L"ASN.1 OctetString (" + std::to_wstring(valLen) + L" B)",
                    DetailSys100::HexBytes(val, std::min<size_t>(valLen, 20), L':') + (valLen > 20 ? L"..." : L"") });
            }
        }
        pos += hdrLen + valLen;
    }
}

inline void AppendCertContextRows(PCCERT_CONTEXT pCert, const std::wstring& prefix, std::vector<std::vector<std::wstring>>& rows) {
    if (!pCert || !pCert->pCertInfo) return;

    wchar_t subjBuf[512] = {}, issBuf[512] = {};
    CertNameToStrW(X509_ASN_ENCODING, &pCert->pCertInfo->Subject, CERT_X500_NAME_STR, subjBuf, 512);
    CertNameToStrW(X509_ASN_ENCODING, &pCert->pCertInfo->Issuer,  CERT_X500_NAME_STR, issBuf,  512);

    rows.push_back({ prefix + L"X.509 Version", L"v" + std::to_wstring(pCert->pCertInfo->dwVersion + 1) });
    rows.push_back({ prefix + L"Subject DN", subjBuf });
    rows.push_back({ prefix + L"Issuer DN", issBuf });

    // SerialNumber is stored little-endian in CERT_INFO
    std::vector<uint8_t> serRev(pCert->pCertInfo->SerialNumber.cbData);
    for (size_t i = 0; i < serRev.size(); ++i) {
        serRev[i] = pCert->pCertInfo->SerialNumber.pbData[serRev.size() - 1 - i];
    }
    rows.push_back({ prefix + L"Serial Number", DetailSys100::HexBytes(serRev.data(), serRev.size(), L':') });

    if (pCert->pCertInfo->SignatureAlgorithm.pszObjId) {
        std::string sigOid = pCert->pCertInfo->SignatureAlgorithm.pszObjId;
        rows.push_back({ prefix + L"Signature Algorithm", MapKnownOid(sigOid) + L" (" + PathUtils::Utf8ToWide(sigOid) + L")" });
    }
    if (pCert->pCertInfo->SubjectPublicKeyInfo.Algorithm.pszObjId) {
        std::string pkOid = pCert->pCertInfo->SubjectPublicKeyInfo.Algorithm.pszObjId;
        const DWORD keyBits = CertGetPublicKeyLength(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, &pCert->pCertInfo->SubjectPublicKeyInfo);
        rows.push_back({ prefix + L"Public Key", MapKnownOid(pkOid) + L" (" + std::to_wstring(keyBits) + L"-bit)" });
    }

    uint64_t ftBefore = (static_cast<uint64_t>(pCert->pCertInfo->NotBefore.dwHighDateTime) << 32) | pCert->pCertInfo->NotBefore.dwLowDateTime;
    uint64_t ftAfter  = (static_cast<uint64_t>(pCert->pCertInfo->NotAfter.dwHighDateTime)  << 32) | pCert->pCertInfo->NotAfter.dwLowDateTime;
    rows.push_back({ prefix + L"Valid Not Before", DetailSys100::FormatFileTimeUtc(ftBefore) });
    rows.push_back({ prefix + L"Valid Not After",  DetailSys100::FormatFileTimeUtc(ftAfter) });

    // Subject Alternative Names (SAN)
    PCERT_EXTENSION pSanExt = CertFindExtension(szOID_SUBJECT_ALT_NAME2, pCert->pCertInfo->cExtension, pCert->pCertInfo->rgExtension);
    if (pSanExt) {
        CERT_ALT_NAME_INFO* pAltInfo = nullptr;
        DWORD cbAltInfo = 0;
        if (CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, szOID_SUBJECT_ALT_NAME2,
                pSanExt->Value.pbData, pSanExt->Value.cbData, CRYPT_DECODE_ALLOC_FLAG, nullptr, &pAltInfo, &cbAltInfo) && pAltInfo)
        {
            std::wstring sanStr;
            for (DWORD i = 0; i < pAltInfo->cAltEntry; ++i) {
                const auto& e = pAltInfo->rgAltEntry[i];
                std::wstring item;
                if (e.dwAltNameChoice == CERT_ALT_NAME_DNS_NAME && e.pwszDNSName) item = std::wstring(L"DNS:") + e.pwszDNSName;
                else if (e.dwAltNameChoice == CERT_ALT_NAME_RFC822_NAME && e.pwszRfc822Name) item = std::wstring(L"Email:") + e.pwszRfc822Name;
                else if (e.dwAltNameChoice == CERT_ALT_NAME_URL && e.pwszURL) item = std::wstring(L"URI:") + e.pwszURL;
                else if (e.dwAltNameChoice == CERT_ALT_NAME_IP_ADDRESS && e.IPAddress.cbData == 4) {
                    wchar_t ipb[32] = {};
                    swprintf_s(ipb, L"IP:%u.%u.%u.%u", e.IPAddress.pbData[0], e.IPAddress.pbData[1], e.IPAddress.pbData[2], e.IPAddress.pbData[3]);
                    item = ipb;
                }
                if (!item.empty()) {
                    if (!sanStr.empty()) sanStr += L", ";
                    sanStr += item;
                }
            }
            if (!sanStr.empty()) rows.push_back({ prefix + L"Subject Alt Names (SAN)", sanStr });
            LocalFree(pAltInfo);
        }
    }

    uint8_t sha256[32] = {};
    DWORD cbSha256 = sizeof(sha256);
    if (CertGetCertificateContextProperty(pCert, CERT_SHA256_HASH_PROP_ID, sha256, &cbSha256) && cbSha256 == 32) {
        rows.push_back({ prefix + L"SHA-256 Thumbprint", DetailSys100::HexBytes(sha256, 32, L':') });
    } else {
        uint8_t sha1[20] = {};
        DWORD cbSha1 = sizeof(sha1);
        if (CertGetCertificateContextProperty(pCert, CERT_SHA1_HASH_PROP_ID, sha1, &cbSha1) && cbSha1 == 20) {
            rows.push_back({ prefix + L"SHA-1 Thumbprint", DetailSys100::HexBytes(sha1, 20, L':') });
        }
    }
}

} // namespace DetailX509

inline bool ParseX509AndPkcsDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (raw.size() < 4) return false;
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());

    struct DerBlock { std::wstring label; std::vector<uint8_t> der; };
    std::vector<DerBlock> blocks;
    bool isPem = false;

    size_t searchPos = 0;
    while (blocks.size() < 32) {
        const size_t bPos = sv.find("-----BEGIN ", searchPos);
        if (bPos == std::string_view::npos) break;
        const size_t bEnd = sv.find("-----", bPos + 11);
        if (bEnd == std::string_view::npos || bEnd - (bPos + 11) > 64) break;
        const std::string_view labelSv = sv.substr(bPos + 11, bEnd - (bPos + 11));
        const std::string endMarker = "-----END " + std::string(labelSv) + "-----";
        const size_t ePos = sv.find(endMarker, bEnd + 5);
        if (ePos == std::string_view::npos) break;

        std::string_view payloadSv = sv.substr(bEnd + 5, ePos - (bEnd + 5));
        // Strip optional RFC 1421 Proc-Type / DEK-Info armor headers before blank line
        if (payloadSv.find(':') != std::string_view::npos) {
            size_t blank = payloadSv.find("\r\n\r\n");
            if (blank != std::string_view::npos) payloadSv = payloadSv.substr(blank + 4);
            else {
                blank = payloadSv.find("\n\n");
                if (blank != std::string_view::npos) payloadSv = payloadSv.substr(blank + 2);
            }
        }

        std::vector<uint8_t> decoded;
        DWORD cbDecoded = 0;
        if (CryptStringToBinaryA(payloadSv.data(), static_cast<DWORD>(payloadSv.size()), CRYPT_STRING_BASE64, nullptr, &cbDecoded, nullptr, nullptr) && cbDecoded > 0) {
            decoded.resize(cbDecoded);
            if (!CryptStringToBinaryA(payloadSv.data(), static_cast<DWORD>(payloadSv.size()), CRYPT_STRING_BASE64, decoded.data(), &cbDecoded, nullptr, nullptr)) {
                decoded.clear();
            } else {
                decoded.resize(cbDecoded);
            }
        }
        if (decoded.empty()) {
            DetailX509::DecodeBase64Clean(payloadSv, decoded);
        }
        if (!decoded.empty()) {
            isPem = true;
            blocks.push_back({ PathUtils::Utf8ToWide(labelSv), std::move(decoded) });
        }
        searchPos = ePos + endMarker.size();
    }

    if (!isPem) {
        uint8_t outerTag = 0;
        size_t hdrLen = 0, valLen = 0;
        if (raw[0] != 0x30 || !DetailX509::ReadDerTlv(raw.data(), raw.size(), outerTag, hdrLen, valLen) || valLen == 0) {
            return false;
        }
        blocks.push_back({ L"DER ASN.1 Binary", raw });
    }

    std::vector<std::vector<std::wstring>> rows = {
        { L"Certificate / PKI Field", L"Decoded Value" },
        { L"Container Encoding", isPem ? (L"PEM Base64 Armored (" + std::to_wstring(blocks.size()) + L" block(s))") : L"ASN.1 DER Binary" },
        { L"File Extension", extLower }
    };

    bool parsedAny = false;
    for (size_t bi = 0; bi < blocks.size(); ++bi) {
        const auto& blk = blocks[bi];
        const std::wstring prefix = (blocks.size() > 1) ? (L"[#" + std::to_wstring(bi + 1) + L"] ") : L"";
        rows.push_back({ prefix + L"Block Type", blk.label + L" (" + PathUtils::FormatByteSize(blk.der.size()) + L")" });

        bool handledAsCert = false;
        const bool looksNonCert = (blk.label.find(L"REQUEST") != std::wstring::npos ||
                                   blk.label.find(L"KEY") != std::wstring::npos ||
                                   extLower == L".csr" || extLower == L".p12" || extLower == L".pfx");

        if (!looksNonCert) {
            PCCERT_CONTEXT pCert = CertCreateCertificateContext(
                X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                blk.der.data(),
                static_cast<DWORD>(blk.der.size()));
            if (pCert) {
                DetailX509::AppendCertContextRows(pCert, prefix, rows);
                CertFreeCertificateContext(pCert);
                handledAsCert = true;
                parsedAny = true;
            } else if (extLower == L".p7b" || blk.label == L"PKCS7") {
                CRYPT_DATA_BLOB blob = { static_cast<DWORD>(blk.der.size()), const_cast<BYTE*>(blk.der.data()) };
                HCERTSTORE hStore = CertOpenStore(CERT_STORE_PROV_PKCS7, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, &blob);
                if (hStore) {
                    PCCERT_CONTEXT pCur = nullptr;
                    int cIdx = 1;
                    while ((pCur = CertEnumCertificatesInStore(hStore, pCur)) != nullptr && cIdx <= 16) {
                        DetailX509::AppendCertContextRows(pCur, L"[Cert #" + std::to_wstring(cIdx++) + L"] ", rows);
                        handledAsCert = true;
                        parsedAny = true;
                    }
                    CertCloseStore(hStore, 0);
                }
            }
        }

        if (!handledAsCert) {
            uint8_t t = 0; size_t hl = 0, vl = 0;
            if (blk.der[0] == 0x30 && DetailX509::ReadDerTlv(blk.der.data(), blk.der.size(), t, hl, vl)) {
                std::wstring lastOid;
                DetailX509::WalkAsn1Der(blk.der.data(), blk.der.size(), 0, lastOid, rows);
                parsedAny = true;
            }
        }
    }

    if (!parsedAny) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"X.509 Certificate / PKCS Inspector (" + extLower + L")";
    SpreadsheetSheet sh;
    sh.name = L"X509_PKI_Info";
    sh.maxCols = 2;
    sh.rows = std::move(rows);
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}

// ============================================================================
// 48. KeePass Password Database (.kdbx — v3.1 & v4.0/v4.1 Outer Header & KDF)
// ============================================================================
inline bool ParseKeePassKdbx(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 16 || DetailSys100::RdU32LE(raw.data()) != 0x9AA2D903u ||
        DetailSys100::RdU32LE(raw.data() + 4) != 0xB54BFB67u) {
        return false;
    }

    const uint16_t minVer = DetailSys100::RdU16LE(raw.data() + 8);
    const uint16_t majVer = DetailSys100::RdU16LE(raw.data() + 10);
    if (majVer != 3 && majVer != 4) return false;

    std::vector<std::vector<std::wstring>> rows = {
        { L"KeePass Header Field", L"Value" },
        { L"Signature", L"0x9AA2D903 0xB54BFB67 (KeePass 2.x KDBX)" },
        { L"Format Version", L"KDBX " + std::to_wstring(majVer) + L"." + std::to_wstring(minVer) }
    };

    static const uint8_t kUuidAes256[16]   = { 0x31,0xC1,0xF2,0xE6,0xBF,0x71,0x43,0x50,0xBE,0x58,0x05,0x21,0x6A,0xFC,0x5A,0xFF };
    static const uint8_t kUuidChaCha20[16] = { 0xD6,0x03,0x8A,0x2B,0x8B,0x6F,0x4C,0xB5,0xA5,0x24,0x33,0x9A,0x31,0xDB,0xB5,0x9A };
    static const uint8_t kUuidTwofish[16]  = { 0xAD,0x68,0xF2,0x9F,0x57,0x6F,0x4B,0xB9,0xA3,0x6A,0xD4,0x7A,0xF9,0x65,0x34,0x6C };

    static const uint8_t kKdfArgon2d[16]   = { 0xEF,0x63,0x6D,0xDF,0x8C,0x29,0x44,0x4B,0x91,0xF7,0xA9,0xA4,0x03,0xE3,0x0A,0x0C };
    static const uint8_t kKdfArgon2id[16]  = { 0x9E,0x29,0x8B,0x19,0x56,0xDB,0x47,0x73,0xB2,0x3D,0xFC,0x3E,0xC6,0xF0,0xA1,0xE6 };
    static const uint8_t kKdfAes[16]       = { 0xC9,0xD9,0xF3,0x9A,0x62,0x8A,0x44,0x60,0xBF,0x74,0x0D,0x08,0xC1,0x8A,0x4F,0xEA };

    size_t pos = 12;
    const size_t lenFieldSz = (majVer >= 4) ? 4 : 2;
    bool sawEndOfHeader = false;

    while (pos + 1 + lenFieldSz <= raw.size()) {
        const uint8_t fid = raw[pos++];
        const uint32_t flen = (majVer >= 4) ? DetailSys100::RdU32LE(raw.data() + pos)
                                            : DetailSys100::RdU16LE(raw.data() + pos);
        pos += lenFieldSz;
        if (pos + flen > raw.size()) return false;
        const uint8_t* fp = raw.data() + pos;

        if (fid == 0) {
            pos += flen;
            sawEndOfHeader = true;
            break;
        } else if (fid == 2 && flen == 16) { // CipherID
            std::wstring cname = DetailSys100::HexBytes(fp, 16);
            if (std::memcmp(fp, kUuidAes256, 16) == 0) cname = L"AES-256-CBC (31C1F2E6...)";
            else if (std::memcmp(fp, kUuidChaCha20, 16) == 0) cname = L"ChaCha20 (D6038A2B...)";
            else if (std::memcmp(fp, kUuidTwofish, 16) == 0) cname = L"Twofish-256 (AD68F29F...)";
            rows.push_back({ L"Outer Cipher Algorithm", cname });
        } else if (fid == 3 && flen == 4) { // CompressionFlags
            const uint32_t cf = DetailSys100::RdU32LE(fp);
            rows.push_back({ L"Inner Compression", (cf == 1) ? L"GZip (1)" : ((cf == 0) ? L"None (0)" : std::to_wstring(cf)) });
        } else if (fid == 4 && flen > 0) { // MasterSeed
            rows.push_back({ L"Master Seed (" + std::to_wstring(flen) + L" B)", DetailSys100::HexBytes(fp, flen) });
        } else if (fid == 5 && flen > 0) { // TransformSeed (v3)
            rows.push_back({ L"KDF Transform Seed", DetailSys100::HexBytes(fp, flen) });
        } else if (fid == 6 && flen == 8) { // TransformRounds (v3)
            rows.push_back({ L"AES-KDF Transform Rounds", std::to_wstring(DetailSys100::RdU64LE(fp)) });
        } else if (fid == 7 && flen > 0) { // EncryptionIV
            rows.push_back({ L"Encryption IV (" + std::to_wstring(flen) + L" B)", DetailSys100::HexBytes(fp, flen) });
        } else if (fid == 10 && flen == 4) { // InnerRandomStreamID (v3)
            const uint32_t irs = DetailSys100::RdU32LE(fp);
            rows.push_back({ L"Inner Protected Stream", (irs == 2) ? L"Salsa20" : ((irs == 3) ? L"ChaCha20" : std::to_wstring(irs)) });
        } else if (fid == 11 && flen >= 3) { // KdfParameters VariantDictionary (v4)
            const uint16_t vdVer = DetailSys100::RdU16LE(fp);
            if ((vdVer & 0xFF00u) == 0x0100u) {
                size_t vp = 2;
                while (vp < flen) {
                    const uint8_t vtype = fp[vp++];
                    if (vtype == 0) break;
                    if (vp + 4 > flen) break;
                    const uint32_t nLen = DetailSys100::RdU32LE(fp + vp);
                    vp += 4;
                    if (vp + nLen + 4 > flen) break;
                    std::string key(reinterpret_cast<const char*>(fp + vp), nLen);
                    vp += nLen;
                    const uint32_t vLen = DetailSys100::RdU32LE(fp + vp);
                    vp += 4;
                    if (vp + vLen > flen) break;
                    const uint8_t* vpData = fp + vp;

                    if (key == "$UUID" && vLen == 16) {
                        std::wstring kdfAlg = DetailSys100::HexBytes(vpData, 16);
                        if (std::memcmp(vpData, kKdfArgon2d, 16) == 0) kdfAlg = L"Argon2d";
                        else if (std::memcmp(vpData, kKdfArgon2id, 16) == 0) kdfAlg = L"Argon2id";
                        else if (std::memcmp(vpData, kKdfAes, 16) == 0) kdfAlg = L"AES-KDF";
                        rows.push_back({ L"KDF Algorithm", kdfAlg });
                    } else if (key == "M" && vLen == 8) {
                        const uint64_t memBytes = DetailSys100::RdU64LE(vpData);
                        rows.push_back({ L"KDF Memory (M)", std::to_wstring(memBytes / 1024ULL) + L" KiB (" + PathUtils::FormatByteSize(memBytes) + L")" });
                    } else if ((key == "I" || key == "R") && vLen == 8) {
                        rows.push_back({ L"KDF Iterations (" + PathUtils::Utf8ToWide(key) + L")", std::to_wstring(DetailSys100::RdU64LE(vpData)) });
                    } else if (key == "P" && vLen == 4) {
                        rows.push_back({ L"KDF Parallelism (P)", std::to_wstring(DetailSys100::RdU32LE(vpData)) + L" lanes" });
                    } else if (key == "V" && vLen == 4) {
                        rows.push_back({ L"Argon2 Version", DetailSys100::Hex32(DetailSys100::RdU32LE(vpData)) });
                    } else if (key == "S" && vLen > 0) {
                        rows.push_back({ L"KDF Salt (" + std::to_wstring(vLen) + L" B)", DetailSys100::HexBytes(vpData, vLen) });
                    } else if (vtype == 0x18) {
                        rows.push_back({ L"KDF[" + PathUtils::Utf8ToWide(key) + L"]", PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(vpData), vLen)) });
                    }
                    vp += vLen;
                }
            }
        }
        pos += flen;
    }

    if (!sawEndOfHeader) return false;
    rows.push_back({ L"Outer Header End Offset", std::to_wstring(pos) + L" B" });
    rows.push_back({ L"Encrypted Vault Payload", PathUtils::FormatByteSize(raw.size() - pos) });

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"KeePass Password Database (.kdbx)";
    SpreadsheetSheet sh;
    sh.name = L"KDBX_Header";
    sh.maxCols = 2;
    sh.rows = std::move(rows);
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}

// ============================================================================
// 49. Wireshark Packet Capture (.pcapng & .pcap — L2/L3/L4 Protocol Dissector)
// ============================================================================
namespace DetailPcap {

inline void DissectPacketL2L4(
    uint16_t linkType,
    const uint8_t* pkt,
    size_t capLen,
    std::wstring& outProto,
    std::wstring& outSrc,
    std::wstring& outDst,
    std::wstring& outInfo)
{
    outProto = L"RAW";
    outSrc   = L"-";
    outDst   = L"-";
    outInfo  = std::to_wstring(capLen) + L" bytes captured";

    size_t l3Off = 0;
    uint16_t ethType = 0;

    if (linkType == 1) { // Ethernet II
        if (capLen < 14) return;
        outSrc = DetailSys100::HexBytes(pkt + 6, 6, L':');
        outDst = DetailSys100::HexBytes(pkt + 0, 6, L':');
        ethType = DetailSys100::RdU16BE(pkt + 12);
        l3Off = 14;
        if ((ethType == 0x8100 || ethType == 0x88A8) && capLen >= 18) { // 802.1Q VLAN
            ethType = DetailSys100::RdU16BE(pkt + 16);
            l3Off = 18;
        }
        outProto = L"Ethernet";
    } else if (linkType == 113 && capLen >= 16) { // Linux SLL
        ethType = DetailSys100::RdU16BE(pkt + 14);
        l3Off = 16;
    } else if (linkType == 101 || linkType == 228 || linkType == 229) { // Raw IP
        if (capLen < 1) return;
        const uint8_t v = pkt[0] >> 4;
        ethType = (v == 4) ? 0x0800 : ((v == 6) ? 0x86DD : 0);
        l3Off = 0;
    } else if (linkType == 0 && capLen >= 4) { // BSD Loopback
        const uint32_t af = DetailSys100::RdU32LE(pkt);
        ethType = (af == 2) ? 0x0800 : 0x86DD;
        l3Off = 4;
    }

    if (ethType == 0x0806 && l3Off + 28 <= capLen) { // ARP
        const uint8_t* arp = pkt + l3Off;
        const uint16_t oper = DetailSys100::RdU16BE(arp + 6);
        wchar_t sIp[32] = {}, tIp[32] = {};
        swprintf_s(sIp, L"%u.%u.%u.%u", arp[14], arp[15], arp[16], arp[17]);
        swprintf_s(tIp, L"%u.%u.%u.%u", arp[24], arp[25], arp[26], arp[27]);
        outProto = L"ARP";
        outSrc = sIp;
        outDst = tIp;
        if (oper == 1) outInfo = std::wstring(L"Who has ") + tIp + L"? Tell " + sIp;
        else if (oper == 2) outInfo = std::wstring(sIp) + L" is at " + DetailSys100::HexBytes(arp + 8, 6, L':');
        return;
    }

    uint8_t ipProto = 0;
    size_t l4Off = 0;

    if (ethType == 0x0800 && l3Off + 20 <= capLen) { // IPv4
        const uint8_t* ip = pkt + l3Off;
        const size_t ihl = static_cast<size_t>(ip[0] & 0x0Fu) * 4;
        if (ihl < 20 || l3Off + ihl > capLen) return;
        ipProto = ip[9];
        wchar_t sBuf[32] = {}, dBuf[32] = {};
        swprintf_s(sBuf, L"%u.%u.%u.%u", ip[12], ip[13], ip[14], ip[15]);
        swprintf_s(dBuf, L"%u.%u.%u.%u", ip[16], ip[17], ip[18], ip[19]);
        outSrc = sBuf;
        outDst = dBuf;
        outProto = L"IPv4";
        l4Off = l3Off + ihl;
    } else if (ethType == 0x86DD && l3Off + 40 <= capLen) { // IPv6
        const uint8_t* ip6 = pkt + l3Off;
        ipProto = ip6[6];
        auto fmtIp6 = [](const uint8_t* a) -> std::wstring {
            wchar_t b[64] = {};
            swprintf_s(b, L"%x:%x:%x:%x:%x:%x:%x:%x",
                DetailSys100::RdU16BE(a + 0), DetailSys100::RdU16BE(a + 2),
                DetailSys100::RdU16BE(a + 4), DetailSys100::RdU16BE(a + 6),
                DetailSys100::RdU16BE(a + 8), DetailSys100::RdU16BE(a + 10),
                DetailSys100::RdU16BE(a + 12), DetailSys100::RdU16BE(a + 14));
            return b;
        };
        outSrc = fmtIp6(ip6 + 8);
        outDst = fmtIp6(ip6 + 24);
        outProto = L"IPv6";
        l4Off = l3Off + 40;
    } else {
        return;
    }

    if (ipProto == 6 && l4Off + 20 <= capLen) { // TCP
        const uint8_t* tcp = pkt + l4Off;
        const uint16_t sp = DetailSys100::RdU16BE(tcp + 0);
        const uint16_t dp = DetailSys100::RdU16BE(tcp + 2);
        const uint32_t seq = DetailSys100::RdU32BE(tcp + 4);
        const size_t doff = static_cast<size_t>(tcp[12] >> 4) * 4;
        const uint8_t flg = tcp[13];
        const uint16_t win = DetailSys100::RdU16BE(tcp + 14);

        outProto = L"TCP";
        std::wstring fstr;
        if (flg & 0x02u) fstr += L"SYN, ";
        if (flg & 0x10u) fstr += L"ACK, ";
        if (flg & 0x08u) fstr += L"PSH, ";
        if (flg & 0x01u) fstr += L"FIN, ";
        if (flg & 0x04u) fstr += L"RST, ";
        if (fstr.size() >= 2) fstr.resize(fstr.size() - 2);

        outInfo = std::to_wstring(sp) + L" -> " + std::to_wstring(dp) + L" [" + fstr + L"] Seq=" + std::to_wstring(seq) + L" Win=" + std::to_wstring(win);

        if (doff >= 20 && l4Off + doff < capLen) {
            const uint8_t* app = pkt + l4Off + doff;
            const size_t appLen = capLen - (l4Off + doff);
            if (appLen >= 5 && (std::memcmp(app, "GET ", 4) == 0 || std::memcmp(app, "POST ", 5) == 0 || std::memcmp(app, "HTTP/", 5) == 0)) {
                outProto = L"HTTP";
                size_t eol = 0;
                while (eol < appLen && eol < 96 && app[eol] != '\r' && app[eol] != '\n') ++eol;
                outInfo = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(app), eol));
            } else if (appLen >= 6 && app[0] == 0x16 && app[1] == 0x03) {
                outProto = L"TLS";
                outInfo += L" (TLS Handshake)";
            }
        }
    } else if (ipProto == 17 && l4Off + 8 <= capLen) { // UDP
        const uint8_t* udp = pkt + l4Off;
        const uint16_t sp = DetailSys100::RdU16BE(udp + 0);
        const uint16_t dp = DetailSys100::RdU16BE(udp + 2);
        const uint16_t ulen = DetailSys100::RdU16BE(udp + 4);
        outProto = L"UDP";
        outInfo = std::to_wstring(sp) + L" -> " + std::to_wstring(dp) + L" Len=" + std::to_wstring(ulen);

        if ((sp == 53 || dp == 53 || sp == 5353 || dp == 5353) && l4Off + 8 + 12 <= capLen) {
            const uint8_t* dns = udp + 8;
            const size_t dnsLen = capLen - (l4Off + 8);
            const uint16_t txId  = DetailSys100::RdU16BE(dns + 0);
            const uint16_t flags = DetailSys100::RdU16BE(dns + 2);
            const uint16_t qdCnt = DetailSys100::RdU16BE(dns + 4);
            outProto = (sp == 5353 || dp == 5353) ? L"mDNS" : L"DNS";
            std::string qname;
            if (qdCnt > 0 && dnsLen > 12) {
                size_t qp = 12;
                while (qp < dnsLen && dns[qp] != 0 && (dns[qp] & 0xC0u) == 0 && qname.size() < 200) {
                    const uint8_t lblLen = dns[qp++];
                    if (qp + lblLen > dnsLen) break;
                    if (!qname.empty()) qname.push_back('.');
                    qname.append(reinterpret_cast<const char*>(dns + qp), lblLen);
                    qp += lblLen;
                }
            }
            wchar_t txb[24] = {};
            swprintf_s(txb, L"0x%04X", txId);
            outInfo = std::wstring((flags & 0x8000u) ? L"Standard query response " : L"Standard query ") + txb +
                      (qname.empty() ? L"" : (L" " + PathUtils::Utf8ToWide(qname)));
        }
    } else if ((ipProto == 1 || ipProto == 58) && l4Off + 4 <= capLen) { // ICMP / ICMPv6
        const uint8_t icmpType = pkt[l4Off];
        const uint8_t icmpCode = pkt[l4Off + 1];
        outProto = (ipProto == 58) ? L"ICMPv6" : L"ICMP";
        if ((ipProto == 1 && icmpType == 8) || (ipProto == 58 && icmpType == 128)) outInfo = L"Echo (ping) request";
        else if ((ipProto == 1 && icmpType == 0) || (ipProto == 58 && icmpType == 129)) outInfo = L"Echo (ping) reply";
        else outInfo = L"Type=" + std::to_wstring(icmpType) + L" Code=" + std::to_wstring(icmpCode);
    }
}

} // namespace DetailPcap

inline bool ParsePcapAndPcapNg(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 24) return false;

    const uint32_t magicLE = DetailSys100::RdU32LE(raw.data());

    // Branch A: PCAP-NG (Section Header Block 0x0A0D0D0A)
    if (magicLE == 0x0A0D0D0Au) {
        if (raw.size() < 28) return false;
        const uint32_t bom = DetailSys100::RdU32LE(raw.data() + 8);
        if (bom != 0x1A2B3C4Du && bom != 0x4D3C2B1Au) return false;
        const bool isBE = (bom == 0x4D3C2B1Au);

        auto rd16 = [&](size_t o) -> uint16_t { return isBE ? DetailSys100::RdU16BE(raw.data() + o) : DetailSys100::RdU16LE(raw.data() + o); };
        auto rd32 = [&](size_t o) -> uint32_t { return isBE ? DetailSys100::RdU32BE(raw.data() + o) : DetailSys100::RdU32LE(raw.data() + o); };

        struct IfaceDesc { uint16_t linkType = 1; std::wstring name; double tsScaleSec = 1e-6; };
        std::vector<IfaceDesc> ifaces;
        std::wstring shbHardware, shbOs, shbApp;

        SpreadsheetSheet pktSh;
        pktSh.name = L"Packets";
        pktSh.maxCols = 8;
        pktSh.rows.push_back({ L"No.", L"Timestamp (s)", L"Interface", L"Protocol", L"Source", L"Destination", L"Length", L"Info" });

        SpreadsheetSheet sumSh;
        sumSh.name = L"Capture_Summary";
        sumSh.maxCols = 2;
        sumSh.rows.push_back({ L"Capture Property", L"Value" });

        size_t pos = 0;
        int pktNo = 1;
        size_t blockCount = 0;

        while (pos + 12 <= raw.size() && blockCount < 100000) {
            const uint32_t btype = rd32(pos + 0);
            const uint32_t blen  = rd32(pos + 4);
            if (blen < 12 || pos + blen > raw.size()) break;
            ++blockCount;

            auto walkOptions = [&](size_t optStart, size_t optEnd, const auto& onOpt) {
                size_t op = optStart;
                while (op + 4 <= optEnd) {
                    const uint16_t oc = rd16(op + 0);
                    const uint16_t ol = rd16(op + 2);
                    op += 4;
                    if (oc == 0) break;
                    if (op + ol > optEnd) break;
                    onOpt(oc, raw.data() + op, ol);
                    op += (static_cast<size_t>(ol) + 3u) & ~size_t(3u);
                }
            };

            if (btype == 0x0A0D0D0Au && blen >= 28) { // SHB
                ifaces.clear();
                walkOptions(pos + 24, pos + blen - 4, [&](uint16_t oc, const uint8_t* od, uint16_t ol) {
                    std::wstring val = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(od), ol));
                    if (oc == 2) shbHardware = val;
                    else if (oc == 3) shbOs = val;
                    else if (oc == 4) shbApp = val;
                });
            } else if (btype == 0x00000001u && blen >= 20) { // IDB
                IfaceDesc idb;
                idb.linkType = rd16(pos + 8);
                idb.name = L"eth" + std::to_wstring(ifaces.size());
                walkOptions(pos + 16, pos + blen - 4, [&](uint16_t oc, const uint8_t* od, uint16_t ol) {
                    if (oc == 2 && ol > 0) {
                        idb.name = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(od), strnlen(reinterpret_cast<const char*>(od), ol)));
                    } else if (oc == 9 && ol >= 1) {
                        const uint8_t r = od[0];
                        if ((r & 0x80u) == 0 && r <= 12) idb.tsScaleSec = std::pow(10.0, -static_cast<int>(r));
                        else if ((r & 0x80u) != 0 && (r & 0x7Fu) <= 32) idb.tsScaleSec = std::pow(2.0, -static_cast<int>(r & 0x7Fu));
                    }
                });
                sumSh.rows.push_back({ L"Interface #" + std::to_wstring(ifaces.size()), idb.name + L" (LinkType " + std::to_wstring(idb.linkType) + L")" });
                ifaces.push_back(std::move(idb));
            } else if (btype == 0x00000006u && blen >= 32 && pktSh.rows.size() <= 5000) { // EPB
                const uint32_t ifId    = rd32(pos + 8);
                const uint32_t tsHigh  = rd32(pos + 12);
                const uint32_t tsLow   = rd32(pos + 16);
                const uint32_t capLen  = rd32(pos + 20);
                const uint32_t origLen = rd32(pos + 24);
                if (28ULL + capLen + 4ULL <= blen) {
                    const uint16_t lt = (ifId < ifaces.size()) ? ifaces[ifId].linkType : 1;
                    const std::wstring ifName = (ifId < ifaces.size()) ? ifaces[ifId].name : (L"if" + std::to_wstring(ifId));
                    const double scale = (ifId < ifaces.size()) ? ifaces[ifId].tsScaleSec : 1e-6;
                    const uint64_t tsRaw = (static_cast<uint64_t>(tsHigh) << 32) | tsLow;
                    wchar_t tsBuf[48] = {};
                    swprintf_s(tsBuf, L"%.6f", static_cast<double>(tsRaw) * scale);

                    std::wstring proto, src, dst, info;
                    DetailPcap::DissectPacketL2L4(lt, raw.data() + pos + 28, capLen, proto, src, dst, info);
                    pktSh.rows.push_back({
                        std::to_wstring(pktNo++), tsBuf, ifName, proto, src, dst, std::to_wstring(origLen), info
                    });
                }
            } else if (btype == 0x00000003u && blen >= 16 && pktSh.rows.size() <= 5000) { // SPB
                const uint32_t origLen = rd32(pos + 8);
                const uint32_t capLen  = std::min<uint32_t>(origLen, blen - 16);
                const uint16_t lt = !ifaces.empty() ? ifaces[0].linkType : 1;
                const std::wstring ifName = !ifaces.empty() ? ifaces[0].name : L"if0";
                std::wstring proto, src, dst, info;
                DetailPcap::DissectPacketL2L4(lt, raw.data() + pos + 12, capLen, proto, src, dst, info);
                pktSh.rows.push_back({
                    std::to_wstring(pktNo++), L"0.000000", ifName, proto, src, dst, std::to_wstring(origLen), info
                });
            }

            pos += (static_cast<size_t>(blen) + 3u) & ~size_t(3u);
        }

        if (blockCount == 0) return false;
        if (!shbHardware.empty()) sumSh.rows.push_back({ L"Capture Hardware", shbHardware });
        if (!shbOs.empty())       sumSh.rows.push_back({ L"Capture OS", shbOs });
        if (!shbApp.empty())      sumSh.rows.push_back({ L"Capture Application", shbApp });
        sumSh.rows.push_back({ L"Total Blocks / Packets", std::to_wstring(blockCount) + L" / " + std::to_wstring(pktSh.rows.size() - 1) });

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Wireshark PCAP-NG Capture (.pcapng)";
        if (pktSh.rows.size() > 1) out.sheets.push_back(std::move(pktSh));
        out.sheets.push_back(std::move(sumSh));
        out.valid = true;
        return true;
    }

    // Branch B: Classic libpcap (.pcap)
    const bool pcapLE = (magicLE == 0xA1B2C3D4u || magicLE == 0xA1B23C4Du);
    const bool pcapBE = (magicLE == 0xD4C3B2A1u || magicLE == 0x4D3CB2A1u);
    if (!pcapLE && !pcapBE) return false;

    auto rd32 = [&](size_t o) -> uint32_t { return pcapBE ? DetailSys100::RdU32BE(raw.data() + o) : DetailSys100::RdU32LE(raw.data() + o); };
    const uint16_t linkType = static_cast<uint16_t>(rd32(20) & 0xFFFFu);

    SpreadsheetSheet pktSh;
    pktSh.name = L"Packets";
    pktSh.maxCols = 8;
    pktSh.rows.push_back({ L"No.", L"Timestamp (s)", L"Interface", L"Protocol", L"Source", L"Destination", L"Length", L"Info" });

    size_t pos = 24;
    int pktNo = 1;
    while (pos + 16 <= raw.size() && pktNo <= 5000) {
        const uint32_t tsSec   = rd32(pos + 0);
        const uint32_t tsSub   = rd32(pos + 4);
        const uint32_t inclLen = rd32(pos + 8);
        const uint32_t origLen = rd32(pos + 12);
        pos += 16;
        if (pos + inclLen > raw.size()) break;

        wchar_t tsBuf[48] = {};
        swprintf_s(tsBuf, L"%u.%06u", tsSec, tsSub);
        std::wstring proto, src, dst, info;
        DetailPcap::DissectPacketL2L4(linkType, raw.data() + pos, inclLen, proto, src, dst, info);
        pktSh.rows.push_back({
            std::to_wstring(pktNo++), tsBuf, L"pcap0", proto, src, dst, std::to_wstring(origLen), info
        });
        pos += inclLen;
    }

    if (pktSh.rows.size() <= 1) return false;
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"libpcap Network Packet Capture (.pcap)";
    out.sheets.push_back(std::move(pktSh));
    out.valid = true;
    return true;
}

// ============================================================================
// 50. USB Flashing Format (.uf2 — "UF2\n" 512-byte Microcontroller Blocks)
// ============================================================================
inline bool ParseUf2Firmware(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 512 || std::memcmp(raw.data(), "UF2\n", 4) != 0) return false;

    const size_t totalBlocks = raw.size() / 512;
    uint32_t minAddr = 0xFFFFFFFFu, maxAddr = 0;
    uint64_t totalPayloadBytes = 0;
    uint32_t familyId = DetailSys100::RdU32LE(raw.data() + 28);
    uint32_t hdrNumBlocks = DetailSys100::RdU32LE(raw.data() + 24);
    uint64_t fnvHash = 14695981039346656037ULL;
    std::wstring extDeviceDesc, extFwVer;

    for (size_t b = 0; b < totalBlocks && b < 65536; ++b) {
        const uint8_t* blk = raw.data() + b * 512;
        if (std::memcmp(blk, "UF2\n", 4) != 0) break;
        const uint32_t flags = DetailSys100::RdU32LE(blk + 8);
        const uint32_t addr  = DetailSys100::RdU32LE(blk + 12);
        const uint32_t paySz = std::min<uint32_t>(DetailSys100::RdU32LE(blk + 16), 476u);
        if (familyId == 0 && (flags & 0x2000u) != 0) familyId = DetailSys100::RdU32LE(blk + 28);

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

    if (minAddr == 0xFFFFFFFFu) minAddr = DetailSys100::RdU32LE(raw.data() + 12);
    if (hdrNumBlocks == 0) hdrNumBlocks = static_cast<uint32_t>(totalBlocks);

    std::wstring famName = DetailSys100::Hex32(familyId);
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
        { L"Base Flash Address", DetailSys100::Hex32(minAddr) },
        { L"End Flash Address", DetailSys100::Hex32(maxAddr) },
        { L"Total 512B Blocks", std::to_wstring(hdrNumBlocks) + L" (" + std::to_wstring(totalBlocks) + L" present)" },
        { L"Total Firmware Payload", PathUtils::FormatByteSize(totalPayloadBytes) },
        { L"Payload FNV-1a Digest", DetailSys100::Hex64(fnvHash) },
        { L"Initial Vector Bytes", DetailSys100::HexBytes(raw.data() + 32, 16, L' ') }
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

// ============================================================================
// Dispatcher for Formats 37–50
// ============================================================================
inline bool ParseExtendedSysSecDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    // PE32 / PE32+
    if (extLower == L".exe" || extLower == L".dll" || extLower == L".sys" || extLower == L".ocx" ||
        extLower == L".scr" || extLower == L".efi" || extLower == L".drv" || extLower == L".cpl" ||
        (raw.size() >= 64 && raw[0] == 'M' && raw[1] == 'Z' &&
         DetailSys100::RdU32LE(raw.data() + 60) + 24 <= raw.size() &&
         std::memcmp(raw.data() + DetailSys100::RdU32LE(raw.data() + 60), "PE\0\0", 4) == 0))
    {
        return ParseWindowsPeBinary(raw, out);
    }

    // ELF shared object (.so / .o) if routed here
    if ((extLower == L".so" || extLower == L".o") && raw.size() >= 52 &&
        raw[0] == 0x7F && raw[1] == 'E' && raw[2] == 'L' && raw[3] == 'F')
    {
        return ParseElfBinaryDoc(raw, out);
    }

    if (extLower == L".lnk" || (raw.size() >= 76 && DetailSys100::RdU32LE(raw.data()) == 0x0000004Cu && raw[4] == 0x01 && raw[5] == 0x14)) {
        return ParseWindowsLnkShortcut(raw, out);
    }
    if (extLower == L".pf" || (raw.size() >= 84 && std::memcmp(raw.data() + 4, "SCCA", 4) == 0) ||
        (raw.size() >= 12 && raw[0] == 'M' && raw[1] == 'A' && raw[2] == 'M' && (raw[3] & 0x0Fu) == 0x04u))
    {
        return ParseWindowsPrefetch(raw, out);
    }
    if (extLower == L".evtx" || (raw.size() >= 128 && std::memcmp(raw.data(), "ElfFile\0", 8) == 0)) {
        return ParseWindowsEvtxLog(raw, out);
    }
    if (extLower == L".dmp" || extLower == L".mdmp" || (raw.size() >= 32 && std::memcmp(raw.data(), "MDMP", 4) == 0)) {
        return ParseWindowsMinidump(raw, out);
    }
    if (extLower == L".pdb" || (raw.size() >= 56 && std::memcmp(raw.data(), "Microsoft C/C++ MSF 7.00", 24) == 0)) {
        return ParseMsfPdbSymbols(raw, out);
    }

    // Check Mach-O before Java .class when extension is .macho/.dylib/.so or when 0xCAFEBABE is a validated Fat archive
    if (extLower == L".macho" || extLower == L".dylib" || extLower == L".so") {
        return ParseAppleMachOBinary(raw, out);
    }
    if (extLower == L".class") {
        return ParseJavaClassFile(raw, out);
    }
    if (raw.size() >= 28 && IsValidMachOFatArchive(raw, out.sourceFileSize)) {
        return ParseAppleMachOBinary(raw, out);
    }
    if (raw.size() >= 24 && DetailSys100::RdU32BE(raw.data()) == 0xCAFEBABEu) {
        return ParseJavaClassFile(raw, out);
    }
    if (raw.size() >= 28 && IsValidMachOMagic(DetailSys100::RdU32LE(raw.data()))) {
        return ParseAppleMachOBinary(raw, out);
    }

    if (extLower == L".dex" || (raw.size() >= 112 && std::memcmp(raw.data(), "dex\n", 4) == 0)) {
        return ParseAndroidDexFile(raw, out);
    }
    if (extLower == L".crt" || extLower == L".cer" || extLower == L".pem" || extLower == L".der" ||
        extLower == L".csr" || extLower == L".p7b" || extLower == L".p12" || extLower == L".pfx")
    {
        return ParseX509AndPkcsDoc(raw, extLower, out);
    }
    if (extLower == L".kdbx" || (raw.size() >= 16 && DetailSys100::RdU32LE(raw.data()) == 0x9AA2D903u && DetailSys100::RdU32LE(raw.data() + 4) == 0xB54BFB67u)) {
        return ParseKeePassKdbx(raw, out);
    }
    if (extLower == L".pcapng" || extLower == L".pcap" || (raw.size() >= 28 && DetailSys100::RdU32LE(raw.data()) == 0x0A0D0D0Au)) {
        return ParsePcapAndPcapNg(raw, out);
    }
    if (extLower == L".uf2" || (raw.size() >= 512 && std::memcmp(raw.data(), "UF2\n", 4) == 0)) {
        return ParseUf2Firmware(raw, out);
    }

    return false;
}

} // namespace Formats
