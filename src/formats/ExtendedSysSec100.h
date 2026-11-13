#pragma once
// ExtendedSysSec100.h — 14 Executables, Reverse Engineering, Security, Forensics & Firmware Formats (37–50)
// Covers: .exe/.dll/.sys/.ocx (PE32/PE32+), .lnk, .pf, .evtx, .dmp/.mdmp, .pdb (MSF 7.00),
//         .class, .dex, .macho/.dylib, .crt/.cer/.pem/.der, .csr/.p12/.pfx, .kdbx, .pcapng, .uf2

namespace Formats {

namespace DetailSys100 {

inline uint16_t RdU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint32_t RdU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
inline uint64_t RdU64LE(const uint8_t* p) {
    return static_cast<uint64_t>(RdU32LE(p)) |
           (static_cast<uint64_t>(RdU32LE(p + 4)) << 32);
}
inline uint16_t RdU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
inline uint32_t RdU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

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

} // namespace DetailSys100

// ============================================================================
// 37. Windows PE32 / PE32+ Portable Executable (.exe / .dll / .sys / .ocx)
// ============================================================================
inline bool ParseWindowsPeBinary(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 64 || raw[0] != 'M' || raw[1] != 'Z') return false;
    uint32_t peOff = DetailSys100::RdU32LE(raw.data() + 60);
    if (peOff + 24 > raw.size() || std::memcmp(raw.data() + peOff, "PE\0\0", 4) != 0) return false;

    const uint8_t* coff = raw.data() + peOff + 4;
    uint16_t machine      = DetailSys100::RdU16LE(coff + 0);
    uint16_t numSections  = DetailSys100::RdU16LE(coff + 2);
    uint32_t timeStamp    = DetailSys100::RdU32LE(coff + 4);
    uint16_t optHdrSize   = DetailSys100::RdU16LE(coff + 16);
    uint16_t chars        = DetailSys100::RdU16LE(coff + 18);

    std::wstring arch = L"Unknown (" + DetailSys100::Hex32(machine) + L")";
    if (machine == 0x8664) arch = L"x64 / AMD64 (0x8664)";
    else if (machine == 0x014C) arch = L"x86 / i386 (0x014C)";
    else if (machine == 0xAA64) arch = L"ARM64 / AArch64 (0xAA64)";
    else if (machine == 0x01C4) arch = L"ARM Thumb-2 (0x01C4)";

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
    hdrSheet.rows.push_back({ L"Compiler Timestamp (Unix)", std::to_wstring(timeStamp) });

    size_t optOff = peOff + 24;
    if (optHdrSize >= 24 && optOff + optHdrSize <= raw.size()) {
        uint16_t optMagic = DetailSys100::RdU16LE(raw.data() + optOff);
        bool isPe64 = (optMagic == 0x020B);
        uint32_t entryRva = DetailSys100::RdU32LE(raw.data() + optOff + 16);
        uint64_t imgBase  = isPe64 && optHdrSize >= 32 ? DetailSys100::RdU64LE(raw.data() + optOff + 24)
                                                       : DetailSys100::RdU32LE(raw.data() + optOff + 28);
        uint16_t subsys   = optHdrSize >= 70 ? DetailSys100::RdU16LE(raw.data() + optOff + 68) : 0;
        uint16_t dllChars = optHdrSize >= 72 ? DetailSys100::RdU16LE(raw.data() + optOff + 70) : 0;

        hdrSheet.rows.push_back({ L"PE Format", isPe64 ? L"PE32+ (64-bit)" : L"PE32 (32-bit)" });
        hdrSheet.rows.push_back({ L"Entry Point RVA", DetailSys100::Hex32(entryRva) });
        hdrSheet.rows.push_back({ L"Image Base", DetailSys100::Hex64(imgBase) });
        hdrSheet.rows.push_back({ L"Subsystem", (subsys == 2) ? L"WINDOWS_GUI (2)" : ((subsys == 3) ? L"WINDOWS_CUI Console (3)" : ((subsys == 1) ? L"NATIVE Driver (1)" : std::to_wstring(subsys))) });

        std::wstring secFlags;
        if (dllChars & 0x0040) secFlags += L"ASLR ";
        if (dllChars & 0x0100) secFlags += L"DEP/NX ";
        if (dllChars & 0x4000) secFlags += L"CFG ";
        if (dllChars & 0x0020) secFlags += L"HighEntropyVA ";
        hdrSheet.rows.push_back({ L"Security Mitigations", secFlags.empty() ? L"None" : secFlags });
    }
    out.sheets.push_back(std::move(hdrSheet));

    // Section Table Sheet
    size_t secOff = optOff + optHdrSize;
    if (numSections > 0 && secOff + static_cast<size_t>(numSections) * 40 <= raw.size()) {
        SpreadsheetSheet secSheet;
        secSheet.name = L"Sections";
        secSheet.maxCols = 5;
        secSheet.rows.push_back({ L"Section Name", L"Virtual Address", L"Virtual Size", L"Raw Size", L"Flags" });
        for (uint16_t i = 0; i < numSections && i < 64; ++i) {
            const uint8_t* s = raw.data() + secOff + i * 40;
            std::string sname(reinterpret_cast<const char*>(s), strnlen(reinterpret_cast<const char*>(s), 8));
            uint32_t vsize = DetailSys100::RdU32LE(s + 8);
            uint32_t vaddr = DetailSys100::RdU32LE(s + 12);
            uint32_t rsize = DetailSys100::RdU32LE(s + 16);
            uint32_t sflag = DetailSys100::RdU32LE(s + 36);
            std::wstring perm;
            if (sflag & 0x40000000u) perm += L"R";
            if (sflag & 0x80000000u) perm += L"W";
            if (sflag & 0x20000000u) perm += L"X";
            secSheet.rows.push_back({
                PathUtils::Utf8ToWide(sname),
                DetailSys100::Hex32(vaddr),
                PathUtils::FormatByteSize(vsize),
                PathUtils::FormatByteSize(rsize),
                perm
            });
        }
        out.sheets.push_back(std::move(secSheet));
    }

    out.valid = true;
    return true;
}

// ============================================================================
// 38–50. System Forensics, Reverse Engineering, Certificates & Security Formats
// ============================================================================
inline bool ParseExtendedSysSecDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (extLower == L".exe" || extLower == L".dll" || extLower == L".sys" || extLower == L".ocx" ||
        (raw.size() >= 64 && raw[0] == 'M' && raw[1] == 'Z' && DetailSys100::RdU32LE(raw.data() + 60) + 24 <= raw.size() &&
         std::memcmp(raw.data() + DetailSys100::RdU32LE(raw.data() + 60), "PE\0\0", 4) == 0))
    {
        return ParseWindowsPeBinary(raw, out);
    }

    auto makeKeyValDoc = [&](const std::wstring& title, const std::wstring& sheetName, std::vector<std::vector<std::wstring>> rows) -> bool {
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = title;
        SpreadsheetSheet sh;
        sh.name = sheetName;
        sh.maxCols = 2;
        sh.rows = std::move(rows);
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    };

    // 38. Windows Shell Link (.lnk — magic 0x0000004C)
    if (extLower == L".lnk" || (raw.size() >= 76 && DetailSys100::RdU32LE(raw.data()) == 0x0000004Cu)) {
        uint32_t linkFlags = (raw.size() >= 24) ? DetailSys100::RdU32LE(raw.data() + 20) : 0;
        uint32_t fileSize  = (raw.size() >= 56) ? DetailSys100::RdU32LE(raw.data() + 52) : 0;
        std::vector<std::vector<std::wstring>> rows = {
            { L"Field", L"Value" },
            { L"Format", L"MS-SHLLINK Windows Shell Shortcut (.lnk)" },
            { L"Link Flags", DetailSys100::Hex32(linkFlags) },
            { L"Target File Size", PathUtils::FormatByteSize(fileSize) }
        };
        // Extract readable ASCII/UTF-16 path strings from .lnk payload
        std::string cur;
        for (size_t i = 76; i < raw.size() && rows.size() < 24; ++i) {
            if (raw[i] >= 32 && raw[i] <= 126) cur.push_back(static_cast<char>(raw[i]));
            else {
                if (cur.size() >= 4 && (cur.find(":\\") != std::string::npos || cur.find("\\\\") != std::string::npos || cur.find('.') != std::string::npos)) {
                    rows.push_back({ L"Embedded Path / String", PathUtils::Utf8ToWide(cur) });
                }
                cur.clear();
            }
        }
        return makeKeyValDoc(L"Windows Shell Shortcut (.lnk)", L"Shortcut_Info", std::move(rows));
    }

    // 39. Windows Prefetch (.pf — "MAM\x04" or "SCCA" at offset 4)
    if (extLower == L".pf" || (raw.size() >= 8 && (std::memcmp(raw.data(), "MAM\x04", 4) == 0 || std::memcmp(raw.data() + 4, "SCCA", 4) == 0))) {
        bool compressed = (raw.size() >= 4 && std::memcmp(raw.data(), "MAM", 3) == 0);
        return makeKeyValDoc(L"Windows Execution Prefetch (.pf)", L"Prefetch_Forensics", {
            { L"Property", L"Value" },
            { L"Format", compressed ? L"Windows 10/11 MAM LZXPRESS Prefetch" : L"Windows SCCA Uncompressed Prefetch" },
            { L"File Size", PathUtils::FormatByteSize(raw.size()) },
            { L"Header Signature", compressed ? L"MAM\\x04" : L"SCCA" }
        });
    }

    // 40. Windows XML Event Log (.evtx — "ElfFile\0")
    if (extLower == L".evtx" || (raw.size() >= 48 && std::memcmp(raw.data(), "ElfFile\0", 8) == 0)) {
        uint64_t nextRecId = (raw.size() >= 32) ? DetailSys100::RdU64LE(raw.data() + 24) : 0;
        uint16_t numChunks = (raw.size() >= 44) ? DetailSys100::RdU16LE(raw.data() + 42) : 0;
        return makeKeyValDoc(L"Windows XML Event Log (.evtx)", L"EVTX_Header", {
            { L"Property", L"Value" },
            { L"Magic Signature", L"ElfFile\\0 (Windows Vista+ Binary XML Log)" },
            { L"Allocated 64KB Chunks", std::to_wstring(numChunks) },
            { L"Next Event Record ID", std::to_wstring(nextRecId) },
            { L"Total Log Size", PathUtils::FormatByteSize(raw.size()) }
        });
    }

    // 41. Windows Minidump Crash Report (.dmp / .mdmp — "MDMP")
    if (extLower == L".dmp" || extLower == L".mdmp" || (raw.size() >= 32 && std::memcmp(raw.data(), "MDMP", 4) == 0)) {
        uint32_t numStreams = (raw.size() >= 12 && std::memcmp(raw.data(), "MDMP", 4) == 0) ? DetailSys100::RdU32LE(raw.data() + 8) : 0;
        uint32_t dirRva     = (raw.size() >= 16 && std::memcmp(raw.data(), "MDMP", 4) == 0) ? DetailSys100::RdU32LE(raw.data() + 12) : 0;
        std::vector<std::vector<std::wstring>> rows = {
            { L"Minidump Property", L"Value" },
            { L"Signature", L"MDMP (Windows Crash Minidump)" },
            { L"Directory Stream Count", std::to_wstring(numStreams) },
            { L"Stream Directory RVA", DetailSys100::Hex32(dirRva) },
            { L"Dump File Size", PathUtils::FormatByteSize(raw.size()) }
        };
        if (dirRva > 0 && dirRva + static_cast<size_t>(numStreams) * 12 <= raw.size()) {
            for (uint32_t i = 0; i < std::min(numStreams, 16u); ++i) {
                uint32_t stype = DetailSys100::RdU32LE(raw.data() + dirRva + i * 12);
                uint32_t slen  = DetailSys100::RdU32LE(raw.data() + dirRva + i * 12 + 4);
                rows.push_back({ L"Stream #" + std::to_wstring(i) + L" (Type " + std::to_wstring(stype) + L")", PathUtils::FormatByteSize(slen) });
            }
        }
        return makeKeyValDoc(L"Windows Crash Minidump (.dmp)", L"Minidump_Streams", std::move(rows));
    }

    // 42. Microsoft Program Database Symbols (.pdb — "Microsoft C/C++ MSF 7.00")
    if (raw.size() >= 56 && std::memcmp(raw.data(), "Microsoft C/C++ MSF 7.00", 24) == 0) {
        uint32_t blkSize   = DetailSys100::RdU32LE(raw.data() + 32);
        uint32_t numBlocks = DetailSys100::RdU32LE(raw.data() + 40);
        uint32_t dirBytes  = DetailSys100::RdU32LE(raw.data() + 44);
        return makeKeyValDoc(L"Microsoft Program Database Symbols (.pdb)", L"PDB_MSF7", {
            { L"MSF Superblock Field", L"Value" },
            { L"Signature", L"Microsoft C/C++ MSF 7.00" },
            { L"Page / Block Size", std::to_wstring(blkSize) + L" B" },
            { L"Total Blocks", std::to_wstring(numBlocks) },
            { L"Stream Directory Size", PathUtils::FormatByteSize(dirBytes) }
        });
    }

    // 43. Java Bytecode Class File (.class — 0xCAFEBABE)
    if (extLower == L".class" || (raw.size() >= 10 && DetailSys100::RdU32BE(raw.data()) == 0xCAFEBABEu)) {
        uint16_t minorV = (raw.size() >= 6)  ? DetailSys100::RdU16BE(raw.data() + 4) : 0;
        uint16_t majorV = (raw.size() >= 8)  ? DetailSys100::RdU16BE(raw.data() + 6) : 52;
        uint16_t cpCnt  = (raw.size() >= 10) ? DetailSys100::RdU16BE(raw.data() + 8) : 0;
        int jdkVer = (majorV >= 45) ? (majorV - 44) : majorV;
        return makeKeyValDoc(L"Java Bytecode Class (.class)", L"JVM_Class", {
            { L"JVM Class Header", L"Value" },
            { L"Magic Signature", L"0xCAFEBABE" },
            { L"Class File Version", std::to_wstring(majorV) + L"." + std::to_wstring(minorV) + L" (Java SE " + std::to_wstring(jdkVer) + L")" },
            { L"Constant Pool Entries", std::to_wstring(cpCnt) },
            { L"Bytecode Size", PathUtils::FormatByteSize(raw.size()) }
        });
    }

    // 44. Android Dalvik Executable (.dex — "dex\n")
    if (extLower == L".dex" || (raw.size() >= 112 && std::memcmp(raw.data(), "dex\n", 4) == 0)) {
        uint32_t strCnt = (raw.size() >= 60) ? DetailSys100::RdU32LE(raw.data() + 56) : 0;
        uint32_t typCnt = (raw.size() >= 68) ? DetailSys100::RdU32LE(raw.data() + 64) : 0;
        uint32_t mthCnt = (raw.size() >= 92) ? DetailSys100::RdU32LE(raw.data() + 88) : 0;
        uint32_t clsCnt = (raw.size() >= 100)? DetailSys100::RdU32LE(raw.data() + 96) : 0;
        return makeKeyValDoc(L"Android Dalvik Executable (.dex)", L"DEX_Header", {
            { L"DEX Header Field", L"Value" },
            { L"String IDs", std::to_wstring(strCnt) },
            { L"Type IDs", std::to_wstring(typCnt) },
            { L"Method IDs", std::to_wstring(mthCnt) },
            { L"Class Definitions", std::to_wstring(clsCnt) }
        });
    }

    // 45. Apple Mach-O Binary (.macho / .dylib — 0xFEEDFACF / 0xFEEDFACE)
    if (extLower == L".macho" || extLower == L".dylib" ||
        (raw.size() >= 32 && (DetailSys100::RdU32LE(raw.data()) == 0xFEEDFACFu || DetailSys100::RdU32LE(raw.data()) == 0xFEEDFACEu)))
    {
        uint32_t ncmds = (raw.size() >= 20) ? DetailSys100::RdU32LE(raw.data() + 16) : 0;
        return makeKeyValDoc(L"Apple Mach-O Executable / Dylib", L"MachO_Header", {
            { L"Mach-O Property", L"Value" },
            { L"Magic", L"0xFEEDFACF (64-bit Mach-O)" },
            { L"Load Commands Count", std::to_wstring(ncmds) },
            { L"Binary Size", PathUtils::FormatByteSize(raw.size()) }
        });
    }

    // 46 & 47. X.509 Certificates & Keystores (.crt, .cer, .pem, .der, .csr, .p12, .pfx)
    if (extLower == L".crt" || extLower == L".cer" || extLower == L".pem" || extLower == L".der" ||
        extLower == L".csr" || extLower == L".p12" || extLower == L".pfx")
    {
        bool isPem = (raw.size() >= 11 && std::memcmp(raw.data(), "-----BEGIN ", 11) == 0);
        std::vector<std::vector<std::wstring>> rows = {
            { L"Certificate / Keystore Field", L"Value" },
            { L"Encoding", isPem ? L"PEM Base64 Armored" : L"ASN.1 DER Binary" },
            { L"File Extension", extLower },
            { L"Payload Size", PathUtils::FormatByteSize(raw.size()) }
        };
        std::string cur;
        for (size_t i = 0; i < raw.size() && rows.size() < 20; ++i) {
            if (raw[i] >= 32 && raw[i] <= 126) cur.push_back(static_cast<char>(raw[i]));
            else {
                if (cur.size() >= 4 && cur.size() <= 96 && !isPem) {
                    rows.push_back({ L"ASN.1 Printable/UTF8 String", PathUtils::Utf8ToWide(cur) });
                }
                cur.clear();
            }
        }
        return makeKeyValDoc(L"X.509 Certificate / PKCS Keystore (" + extLower + L")", L"X509_Info", std::move(rows));
    }

    // 48. KeePass Password Database (.kdbx — 0x9AA2D903, 0xB54BFB67)
    if (extLower == L".kdbx" || (raw.size() >= 12 && DetailSys100::RdU32LE(raw.data()) == 0x9AA2D903u)) {
        uint16_t minVer = (raw.size() >= 10) ? DetailSys100::RdU16LE(raw.data() + 8) : 0;
        uint16_t majVer = (raw.size() >= 12) ? DetailSys100::RdU16LE(raw.data() + 10) : 4;
        return makeKeyValDoc(L"KeePass Password Database (.kdbx)", L"KDBX_Header", {
            { L"KeePass Header Field", L"Value" },
            { L"Signature", L"0x9AA2D903 0xB54BFB67 (KeePass 2.x KDBX)" },
            { L"Format Version", L"KDBX " + std::to_wstring(majVer) + L"." + std::to_wstring(minVer) },
            { L"Vault Size", PathUtils::FormatByteSize(raw.size()) }
        });
    }

    // 49. Wireshark PCAP-NG (.pcapng — Section Header Block 0x0A0D0D0A)
    if (extLower == L".pcapng" || (raw.size() >= 28 && DetailSys100::RdU32LE(raw.data()) == 0x0A0D0D0Au)) {
        std::vector<std::vector<std::wstring>> rows = {
            { L"Block #", L"Block Type", L"Block Length", L"Details" }
        };
        size_t pos = 0;
        int blkIdx = 0;
        while (pos + 12 <= raw.size() && blkIdx < 200) {
            uint32_t btype = DetailSys100::RdU32LE(raw.data() + pos);
            uint32_t blen  = DetailSys100::RdU32LE(raw.data() + pos + 4);
            if (blen < 12 || pos + blen > raw.size()) break;
            std::wstring tname = (btype == 0x0A0D0D0Au) ? L"Section Header (SHB)" :
                                 (btype == 0x00000001u) ? L"Interface Description (IDB)" :
                                 (btype == 0x00000006u) ? L"Enhanced Packet (EPB)" : DetailSys100::Hex32(btype);
            rows.push_back({ std::to_wstring(blkIdx++), tname, std::to_wstring(blen) + L" B", L"Offset " + std::to_wstring(pos) });
            pos += blen;
        }
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Wireshark PCAP-NG Capture (.pcapng)";
        SpreadsheetSheet sh; sh.name = L"PCAPNG_Blocks"; sh.maxCols = 4; sh.rows = std::move(rows);
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    // 50. USB Flashing Format (.uf2 — "UF2\n" 0x0A324655)
    if (extLower == L".uf2" || (raw.size() >= 512 && std::memcmp(raw.data(), "UF2\n", 4) == 0)) {
        uint32_t targetAddr = (raw.size() >= 16) ? DetailSys100::RdU32LE(raw.data() + 12) : 0x10000000u;
        uint32_t numBlocks  = (raw.size() >= 28) ? DetailSys100::RdU32LE(raw.data() + 24) : static_cast<uint32_t>(raw.size() / 512);
        uint32_t familyId   = (raw.size() >= 32) ? DetailSys100::RdU32LE(raw.data() + 28) : 0;
        std::wstring famName = DetailSys100::Hex32(familyId);
        if (familyId == 0xE48BFF56u) famName = L"Raspberry Pi RP2040 (0xE48BFF56)";
        else if (familyId == 0xE48BFF59u) famName = L"Raspberry Pi RP2350 ARM (0xE48BFF59)";
        return makeKeyValDoc(L"UF2 Microcontroller Bootloader Firmware (.uf2)", L"UF2_Firmware", {
            { L"UF2 Firmware Property", L"Value" },
            { L"Target MCU Family", famName },
            { L"Base Flash Address", DetailSys100::Hex32(targetAddr) },
            { L"Total 512B Blocks", std::to_wstring(numBlocks) }
        });
    }

    return false;
}

} // namespace Formats
