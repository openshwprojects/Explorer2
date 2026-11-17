#pragma once
// ElfBinary.h — Executable and Linkable Format (.elf / .so / .o) Binary Header, Program, Section, Dynamic & Symbol Inspector
// The section header table and the string and symbol tables usually lie at the end of the file, past the 32 MB a
// reader is handed for a large binary: every table is read through SourceBytes

namespace Formats {

inline bool ParseElfBinaryDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 52 || raw[0] != 0x7F || raw[1] != 'E' || raw[2] != 'L' || raw[3] != 'F') return false;

    const uint8_t eiClass = raw[4];
    const uint8_t eiData  = raw[5];
    const uint8_t eiVer   = raw[6];
    const uint8_t eiOsAbi = raw[7];
    if ((eiClass != 1 && eiClass != 2) || (eiData != 1 && eiData != 2) || eiVer != 1) return false;

    const bool is64 = (eiClass == 2);
    const bool isLE = (eiData == 1);
    if (is64 && raw.size() < 64) return false;

    auto ld16 = [&](const uint8_t* p) -> uint16_t { uint16_t v; std::memcpy(&v, p, 2); return isLE ? v : _byteswap_ushort(v); };
    auto ld32 = [&](const uint8_t* p) -> uint32_t { uint32_t v; std::memcpy(&v, p, 4); return isLE ? v : _byteswap_ulong(v); };
    auto ld64 = [&](const uint8_t* p) -> uint64_t { uint64_t v; std::memcpy(&v, p, 8); return isLE ? v : _byteswap_uint64(v); };
    // ELF header fields (the header lies within the bytes handed in)
    auto rdU16 = [&](size_t off) -> uint16_t { return off + 2 <= raw.size() ? ld16(raw.data() + off) : 0; };
    auto rdU32 = [&](size_t off) -> uint32_t { return off + 4 <= raw.size() ? ld32(raw.data() + off) : 0; };
    auto rdU64 = [&](size_t off) -> uint64_t { return off + 8 <= raw.size() ? ld64(raw.data() + off) : 0; };
    auto hexAddr = [&](uint64_t v) -> std::wstring {
        wchar_t buf[32] = {};
        if (is64) swprintf_s(buf, L"0x%016llX", static_cast<unsigned long long>(v));
        else      swprintf_s(buf, L"0x%08X", static_cast<uint32_t>(v));
        return buf;
    };

    SourceBytes src(raw, out);
    const uint64_t fileSize = src.Size();
    auto cstrAt = [&](uint64_t off, uint64_t maxLen) -> std::string {
        std::vector<uint8_t> buf;
        size_t got = 0;
        const uint8_t* p = src.ReadSome(off, maxLen, 512, buf, got);
        if (!p) return {};
        size_t k = 0;
        while (k < got && p[k] != 0) ++k;
        return std::string(reinterpret_cast<const char*>(p), k);
    };

    const uint16_t e_type      = rdU16(16);
    const uint16_t e_machine   = rdU16(18);
    const uint32_t e_version   = rdU32(20);
    const uint64_t e_entry     = is64 ? rdU64(24) : rdU32(24);
    const uint64_t e_phoff     = is64 ? rdU64(32) : rdU32(28);
    const uint64_t e_shoff     = is64 ? rdU64(40) : rdU32(32);
    const uint32_t e_flags     = is64 ? rdU32(48) : rdU32(36);
    const uint16_t e_phentsize = is64 ? rdU16(54) : rdU16(42);
    const uint16_t e_phnum     = is64 ? rdU16(56) : rdU16(44);
    const uint16_t e_shentsize = is64 ? rdU16(58) : rdU16(46);
    const uint16_t e_shnum     = is64 ? rdU16(60) : rdU16(48);
    const uint16_t e_shstrndx  = is64 ? rdU16(62) : rdU16(50);

    if (e_version != 1) return false;
    if (e_phnum == 0 && e_shnum == 0) return false;

    const size_t minPhEnt = is64 ? 56 : 32;
    const size_t minShEnt = is64 ? 64 : 40;
    if (e_phnum > 0) {
        if (e_phentsize < minPhEnt || e_phoff > fileSize || static_cast<uint64_t>(e_phnum) * e_phentsize > fileSize - e_phoff) {
            return false;
        }
    }
    if (e_shnum > 0) {
        if (e_shentsize < minShEnt || e_shoff > fileSize || static_cast<uint64_t>(e_shnum) * e_shentsize > fileSize - e_shoff) {
            return false;
        }
    }
    const uint16_t phCount = std::min<uint16_t>(e_phnum, 512);
    const uint16_t shCount = std::min<uint16_t>(e_shnum, 2048);
    std::vector<uint8_t> phBuf, shBuf;
    size_t phGot = 0, shGot = 0;
    const uint8_t* phTab = phCount ? src.ReadSome(e_phoff, static_cast<uint64_t>(phCount) * e_phentsize, UINT64_MAX, phBuf, phGot) : nullptr;
    const uint8_t* shTab = shCount ? src.ReadSome(e_shoff, static_cast<uint64_t>(shCount) * e_shentsize, UINT64_MAX, shBuf, shGot) : nullptr;
    if ((phCount && !phTab) || (shCount && !shTab)) return false;

    std::wstring osAbiName = L"UNIX - System V (0)";
    switch (eiOsAbi) {
        case 0:   osAbiName = L"UNIX - System V (0)"; break;
        case 1:   osAbiName = L"HP-UX (1)"; break;
        case 2:   osAbiName = L"NetBSD (2)"; break;
        case 3:   osAbiName = L"GNU / Linux (3)"; break;
        case 6:   osAbiName = L"Solaris (6)"; break;
        case 9:   osAbiName = L"FreeBSD (9)"; break;
        case 12:  osAbiName = L"OpenBSD (12)"; break;
        case 64:  osAbiName = L"ARM EABI (64)"; break;
        case 97:  osAbiName = L"ARM (97)"; break;
        case 255: osAbiName = L"Standalone Embedded (255)"; break;
        default:  osAbiName = L"OS/ABI " + std::to_wstring(eiOsAbi); break;
    }

    std::wstring typeName = L"Unknown (" + std::to_wstring(e_type) + L")";
    switch (e_type) {
        case 1: typeName = L"ET_REL (Relocatable Object)"; break;
        case 2: typeName = L"ET_EXEC (Executable)"; break;
        case 3: typeName = L"ET_DYN (Shared Object / PIE)"; break;
        case 4: typeName = L"ET_CORE (Core Dump)"; break;
    }

    std::wstring machName = L"Machine ID " + std::to_wstring(e_machine);
    switch (e_machine) {
        case 0x02:  machName = L"SPARC (0x02)"; break;
        case 0x03:  machName = L"Intel x86 / i386 (0x03)"; break;
        case 0x08:  machName = L"MIPS (0x08)"; break;
        case 0x14:  machName = L"PowerPC (0x14)"; break;
        case 0x15:  machName = L"PowerPC 64-bit (0x15)"; break;
        case 0x16:  machName = L"IBM S/390 (0x16)"; break;
        case 0x28:  machName = L"ARM (0x28)"; break;
        case 0x2A:  machName = L"SuperH (0x2A)"; break;
        case 0x3E:  machName = L"AMD x86-64 (0x3E)"; break;
        case 0x53:  machName = L"Atmel AVR (0x53)"; break;
        case 0x5E:  machName = L"Tensilica Xtensa (0x5E)"; break;
        case 0xB7:  machName = L"ARM64 / AArch64 (0xB7)"; break;
        case 0xF3:  machName = L"RISC-V (0xF3)"; break;
        case 0xF7:  machName = L"Linux eBPF (0xF7)"; break;
        case 0x102: machName = L"LoongArch (0x102)"; break;
    }

    // Parse Program Headers
    struct LoadSeg { uint64_t vaddr, filesz, offset; };
    std::vector<LoadSeg> loadSegs;
    std::wstring interpPath;
    uint64_t ptDynOff = 0, ptDynSz = 0;

    SpreadsheetSheet phSheet;
    phSheet.name = L"Program_Headers";
    phSheet.maxCols = 7;
    phSheet.rows.push_back({ L"Index", L"Type", L"Flags", L"Offset", L"VirtAddr", L"FileSz / MemSz", L"Details" });

    for (uint16_t i = 0; i < phCount; ++i) {
        const uint8_t* p = phTab + static_cast<size_t>(i) * e_phentsize;
        const uint32_t p_type   = ld32(p + 0);
        const uint32_t p_flags  = is64 ? ld32(p + 4)  : ld32(p + 24);
        const uint64_t p_offset = is64 ? ld64(p + 8)  : ld32(p + 4);
        const uint64_t p_vaddr  = is64 ? ld64(p + 16) : ld32(p + 8);
        const uint64_t p_filesz = is64 ? ld64(p + 32) : ld32(p + 16);
        const uint64_t p_memsz  = is64 ? ld64(p + 40) : ld32(p + 20);
        const uint64_t p_align  = is64 ? ld64(p + 48) : ld32(p + 28);

        std::wstring ptName;
        switch (p_type) {
            case 0:          ptName = L"PT_NULL"; break;
            case 1:          ptName = L"PT_LOAD"; break;
            case 2:          ptName = L"PT_DYNAMIC"; break;
            case 3:          ptName = L"PT_INTERP"; break;
            case 4:          ptName = L"PT_NOTE"; break;
            case 5:          ptName = L"PT_SHLIB"; break;
            case 6:          ptName = L"PT_PHDR"; break;
            case 7:          ptName = L"PT_TLS"; break;
            case 0x6474E550: ptName = L"PT_GNU_EH_FRAME"; break;
            case 0x6474E551: ptName = L"PT_GNU_STACK"; break;
            case 0x6474E552: ptName = L"PT_GNU_RELRO"; break;
            case 0x6474E553: ptName = L"PT_GNU_PROPERTY"; break;
            default: {
                wchar_t tb[32] = {};
                swprintf_s(tb, L"0x%08X", p_type);
                ptName = tb;
                break;
            }
        }

        std::wstring perm;
        perm += (p_flags & 4u) ? L"R" : L"-";
        perm += (p_flags & 2u) ? L"W" : L"-";
        perm += (p_flags & 1u) ? L"X" : L"-";

        std::wstring detail = L"Align " + hexAddr(p_align);
        if (p_type == 1 && p_filesz > 0 && p_offset < fileSize) {
            loadSegs.push_back({ p_vaddr, p_filesz, p_offset });
        } else if (p_type == 2 && p_filesz > 0 && p_offset < fileSize) {
            ptDynOff = p_offset;
            ptDynSz  = p_filesz;
        } else if (p_type == 3 && p_filesz > 0 && p_offset < fileSize) {
            std::string ip = cstrAt(p_offset, p_filesz);
            interpPath = PathUtils::Utf8ToWide(ip);
            if (!interpPath.empty()) detail = L"Interpreter: " + interpPath;
        }

        phSheet.rows.push_back({
            std::to_wstring(i),
            ptName,
            perm,
            hexAddr(p_offset),
            hexAddr(p_vaddr),
            PathUtils::FormatByteSize(p_filesz) + L" / " + PathUtils::FormatByteSize(p_memsz),
            detail
        });
    }

    auto vaddrToFileOff = [&](uint64_t vma) -> uint64_t {
        for (const auto& seg : loadSegs) {
            if (vma >= seg.vaddr && vma < seg.vaddr + seg.filesz) {
                const uint64_t fo = seg.offset + (vma - seg.vaddr);
                if (fo < fileSize) return fo;
            }
        }
        return fileSize;
    };

    // Parse Section Headers
    struct ElfSec {
        std::string name;
        uint32_t sh_name = 0;
        uint32_t sh_type = 0;
        uint64_t sh_flags = 0;
        uint64_t sh_addr = 0;
        uint64_t sh_offset = 0;
        uint64_t sh_size = 0;
        uint32_t sh_link = 0;
        uint32_t sh_info = 0;
        uint64_t sh_entsize = 0;
    };
    std::vector<ElfSec> secs;
    secs.reserve(shCount);
    for (uint16_t i = 0; i < shCount; ++i) {
        const uint8_t* s = shTab + static_cast<size_t>(i) * e_shentsize;
        ElfSec es;
        es.sh_name    = ld32(s + 0);
        es.sh_type    = ld32(s + 4);
        es.sh_flags   = is64 ? ld64(s + 8)  : ld32(s + 8);
        es.sh_addr    = is64 ? ld64(s + 16) : ld32(s + 12);
        es.sh_offset  = is64 ? ld64(s + 24) : ld32(s + 16);
        es.sh_size    = is64 ? ld64(s + 32) : ld32(s + 20);
        es.sh_link    = is64 ? ld32(s + 40) : ld32(s + 24);
        es.sh_info    = is64 ? ld32(s + 44) : ld32(s + 28);
        es.sh_entsize = is64 ? ld64(s + 56) : ld32(s + 36);
        secs.push_back(es);
    }

    // A string table up to 4 MB is read once, a larger one's strings one at a time
    struct StrTab {
        std::vector<uint8_t> buf;
        const uint8_t* p = nullptr;
        size_t n = 0;
        bool loaded = false;
    };
    std::vector<StrTab> strTabs(secs.size());
    auto readStrFromSec = [&](uint32_t secIdx, uint32_t strOff) -> std::string {
        if (secIdx >= secs.size()) return {};
        const auto& st = secs[secIdx];
        if (st.sh_type == 8 || st.sh_offset >= fileSize || strOff >= st.sh_size) return {};
        StrTab& tab = strTabs[secIdx];
        if (!tab.loaded) {
            tab.loaded = true;
            if (st.sh_size <= (4u << 20)) tab.p = src.ReadSome(st.sh_offset, st.sh_size, UINT64_MAX, tab.buf, tab.n);
        }
        if (!tab.p) return cstrAt(st.sh_offset + strOff, st.sh_size - strOff);
        if (strOff >= tab.n) return {};
        const size_t lim = std::min<size_t>(tab.n - strOff, 512);
        size_t k = 0;
        while (k < lim && tab.p[strOff + k] != 0) ++k;
        return std::string(reinterpret_cast<const char*>(tab.p + strOff), k);
    };

    if (e_shstrndx < secs.size()) {
        for (auto& es : secs) {
            es.name = readStrFromSec(e_shstrndx, es.sh_name);
        }
    }

    SpreadsheetSheet shSheet;
    shSheet.name = L"Sections";
    shSheet.maxCols = 6;
    shSheet.rows.push_back({ L"Index", L"Section Name", L"Type", L"Flags", L"Address", L"Offset / Size" });

    uint64_t shDynOff = 0, shDynSz = 0;
    uint32_t shDynStrIdx = 0xFFFFFFFFu;

    for (size_t i = 0; i < secs.size(); ++i) {
        const auto& es = secs[i];
        std::wstring stName;
        switch (es.sh_type) {
            case 0:          stName = L"SHT_NULL"; break;
            case 1:          stName = L"SHT_PROGBITS"; break;
            case 2:          stName = L"SHT_SYMTAB"; break;
            case 3:          stName = L"SHT_STRTAB"; break;
            case 4:          stName = L"SHT_RELA"; break;
            case 5:          stName = L"SHT_HASH"; break;
            case 6:          stName = L"SHT_DYNAMIC"; break;
            case 7:          stName = L"SHT_NOTE"; break;
            case 8:          stName = L"SHT_NOBITS"; break;
            case 9:          stName = L"SHT_REL"; break;
            case 11:         stName = L"SHT_DYNSYM"; break;
            case 14:         stName = L"SHT_INIT_ARRAY"; break;
            case 15:         stName = L"SHT_FINI_ARRAY"; break;
            case 0x6FFFFFF6: stName = L"SHT_GNU_HASH"; break;
            case 0x6FFFFFFE: stName = L"SHT_GNU_verneed"; break;
            case 0x6FFFFFFF: stName = L"SHT_GNU_versym"; break;
            default: {
                wchar_t tb[32] = {};
                swprintf_s(tb, L"0x%X", es.sh_type);
                stName = tb;
                break;
            }
        }

        std::wstring flg;
        if (es.sh_flags & 0x2u)   flg += L"A";
        if (es.sh_flags & 0x1u)   flg += L"W";
        if (es.sh_flags & 0x4u)   flg += L"X";
        if (es.sh_flags & 0x10u)  flg += L"M";
        if (es.sh_flags & 0x20u)  flg += L"S";
        if (es.sh_flags & 0x400u) flg += L"T";
        if (flg.empty()) flg = L"-";

        if (es.sh_type == 6 && es.sh_offset < fileSize && es.sh_size > 0) {
            shDynOff = es.sh_offset;
            shDynSz  = es.sh_size;
            shDynStrIdx = es.sh_link;
        }
        if (es.name == ".interp" && interpPath.empty() && es.sh_offset < fileSize && es.sh_size > 0) {
            interpPath = PathUtils::Utf8ToWide(cstrAt(es.sh_offset, es.sh_size));
        }

        shSheet.rows.push_back({
            std::to_wstring(i),
            es.name.empty() ? L"(unnamed)" : PathUtils::Utf8ToWide(es.name),
            stName,
            flg,
            hexAddr(es.sh_addr),
            hexAddr(es.sh_offset) + L" (" + PathUtils::FormatByteSize(es.sh_size) + L")"
        });
    }

    // Parse .dynamic / PT_DYNAMIC for DT_NEEDED, DT_SONAME, DT_RPATH, DT_RUNPATH
    std::vector<std::wstring> neededLibs;
    std::wstring soName, runPath;
    SpreadsheetSheet dynSheet;
    dynSheet.name = L"Dynamic_Tags";
    dynSheet.maxCols = 3;
    dynSheet.rows.push_back({ L"Tag", L"Raw Value", L"Resolved String / Info" });

    const uint64_t dynOff = (shDynOff > 0) ? shDynOff : ptDynOff;
    const uint64_t dynSz  = (shDynOff > 0) ? shDynSz  : ptDynSz;
    const size_t entSz = is64 ? 16 : 8;
    std::vector<uint8_t> dynBuf;
    size_t dynGot = 0;
    const uint8_t* dyn = (dynOff > 0 && dynSz >= entSz) ? src.ReadSome(dynOff, dynSz, 1024 * entSz, dynBuf, dynGot) : nullptr;
    if (dyn) {
        const size_t maxDyn = dynGot / entSz;
        uint64_t fallbackDynStrFileOff = fileSize;
        for (size_t i = 0; i < maxDyn; ++i) {
            const uint8_t* dp = dyn + i * entSz;
            const uint64_t d_tag = is64 ? ld64(dp) : ld32(dp);
            const uint64_t d_val = is64 ? ld64(dp + 8) : ld32(dp + 4);
            if (d_tag == 0) break;
            if (d_tag == 5) { // DT_STRTAB
                fallbackDynStrFileOff = vaddrToFileOff(d_val);
            }
        }
        auto resolveDynStr = [&](uint64_t strOff) -> std::wstring {
            if (shDynStrIdx < secs.size()) {
                std::string s = readStrFromSec(shDynStrIdx, static_cast<uint32_t>(strOff));
                if (!s.empty()) return PathUtils::Utf8ToWide(s);
            }
            if (fallbackDynStrFileOff < fileSize && strOff < fileSize - fallbackDynStrFileOff) {
                return PathUtils::Utf8ToWide(cstrAt(fallbackDynStrFileOff + strOff, 512));
            }
            return {};
        };

        for (size_t i = 0; i < maxDyn; ++i) {
            const uint8_t* dp = dyn + i * entSz;
            const uint64_t d_tag = is64 ? ld64(dp) : ld32(dp);
            const uint64_t d_val = is64 ? ld64(dp + 8) : ld32(dp + 4);
            if (d_tag == 0) break;

            std::wstring tagName;
            std::wstring resolved;
            switch (d_tag) {
                case 1:  tagName = L"DT_NEEDED";  resolved = resolveDynStr(d_val); if (!resolved.empty()) neededLibs.push_back(resolved); break;
                case 14: tagName = L"DT_SONAME";  resolved = resolveDynStr(d_val); if (!resolved.empty()) soName = resolved; break;
                case 15: tagName = L"DT_RPATH";   resolved = resolveDynStr(d_val); if (!resolved.empty() && runPath.empty()) runPath = resolved; break;
                case 29: tagName = L"DT_RUNPATH"; resolved = resolveDynStr(d_val); if (!resolved.empty()) runPath = resolved; break;
                case 4:  tagName = L"DT_HASH"; break;
                case 5:  tagName = L"DT_STRTAB"; break;
                case 6:  tagName = L"DT_SYMTAB"; break;
                case 12: tagName = L"DT_INIT"; break;
                case 13: tagName = L"DT_FINI"; break;
                case 30: tagName = L"DT_FLAGS"; break;
                case 0x6FFFFFFB: tagName = L"DT_FLAGS_1"; break;
                default: continue;
            }
            dynSheet.rows.push_back({ tagName, hexAddr(d_val), resolved });
        }
    }

    // Parse Symbol Tables (SHT_SYMTAB = 2, SHT_DYNSYM = 11)
    SpreadsheetSheet symSheet;
    symSheet.name = L"Symbols";
    symSheet.maxCols = 7;
    symSheet.rows.push_back({ L"Table", L"Symbol Name", L"Value", L"Size", L"Bind", L"Type", L"Section" });

    size_t totalSymsDecoded = 0;
    for (size_t si = 0; si < secs.size() && symSheet.rows.size() <= 2000; ++si) {
        const auto& es = secs[si];
        if (es.sh_type != 2 && es.sh_type != 11) continue;
        if (es.sh_offset >= fileSize || es.sh_size == 0) continue;

        const size_t symEntSz = is64 ? 24 : 16;
        const size_t stride = (es.sh_entsize >= symEntSz) ? static_cast<size_t>(es.sh_entsize) : symEntSz;
        std::vector<uint8_t> symBuf;
        size_t availBytes = 0;
        const uint8_t* symTab = src.ReadSome(es.sh_offset, es.sh_size, 4u << 20, symBuf, availBytes); // far more entries than rows shown
        if (!symTab) continue;
        const size_t count = availBytes / stride;
        const std::wstring tblLabel = (es.sh_type == 11) ? L".dynsym" : L".symtab";

        for (size_t k = 0; k < count && symSheet.rows.size() <= 2000; ++k) {
            const uint8_t* sp = symTab + k * stride;
            const uint32_t st_name  = ld32(sp + 0);
            const uint8_t  st_info  = is64 ? sp[4] : sp[12];
            const uint16_t st_shndx = is64 ? ld16(sp + 6) : ld16(sp + 14);
            const uint64_t st_value = is64 ? ld64(sp + 8) : ld32(sp + 4);
            const uint64_t st_size  = is64 ? ld64(sp + 16) : ld32(sp + 8);

            if (k == 0 && st_name == 0 && st_value == 0 && st_info == 0) continue;

            std::string sname = readStrFromSec(es.sh_link, st_name);
            if (sname.empty() && st_value == 0 && (st_info & 0x0Fu) == 3u) continue; // unnamed SECTION symbol

            const uint8_t bind = st_info >> 4;
            const uint8_t type = st_info & 0x0Fu;
            const wchar_t* bindStr = (bind == 0) ? L"LOCAL" : ((bind == 1) ? L"GLOBAL" : ((bind == 2) ? L"WEAK" : L"OTHER"));
            const wchar_t* typeStr = (type == 0) ? L"NOTYPE" :
                                     (type == 1) ? L"OBJECT" :
                                     (type == 2) ? L"FUNC" :
                                     (type == 3) ? L"SECTION" :
                                     (type == 4) ? L"FILE" :
                                     (type == 6) ? L"TLS" :
                                     (type == 10)? L"IFUNC" : L"OTHER";

            std::wstring secRef;
            if (st_shndx == 0) secRef = L"UND";
            else if (st_shndx == 0xFFF1) secRef = L"ABS";
            else if (st_shndx == 0xFFF2) secRef = L"COM";
            else if (st_shndx < secs.size() && !secs[st_shndx].name.empty()) secRef = PathUtils::Utf8ToWide(secs[st_shndx].name);
            else secRef = std::to_wstring(st_shndx);

            symSheet.rows.push_back({
                tblLabel,
                sname.empty() ? L"(unnamed)" : PathUtils::Utf8ToWide(sname),
                hexAddr(st_value),
                std::to_wstring(st_size) + L" B",
                bindStr,
                typeStr,
                secRef
            });
            ++totalSymsDecoded;
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"ELF Executable & Linkable Binary (.elf)";

    SpreadsheetSheet sheet;
    sheet.name = L"ELF_Structure";
    sheet.maxCols = 2;
    sheet.rows.push_back({ L"Property / Header", L"Value" });
    sheet.rows.push_back({ L"Class", is64 ? L"ELF64 (64-bit)" : L"ELF32 (32-bit)" });
    sheet.rows.push_back({ L"Endianness", isLE ? L"Little-Endian (2's complement)" : L"Big-Endian" });
    sheet.rows.push_back({ L"OS / ABI", osAbiName });
    sheet.rows.push_back({ L"Object Type", typeName });
    sheet.rows.push_back({ L"Architecture", machName });
    sheet.rows.push_back({ L"Entry Point Address", hexAddr(e_entry) });
    wchar_t flagsHex[16];
    swprintf_s(flagsHex, L"0x%08X", e_flags);
    sheet.rows.push_back({ L"Header Flags", flagsHex });
    sheet.rows.push_back({ L"Program Headers", std::to_wstring(e_phnum) + L" (offset " + std::to_wstring(e_phoff) + L")" });
    sheet.rows.push_back({ L"Section Headers", std::to_wstring(e_shnum) + L" (offset " + std::to_wstring(e_shoff) + L")" });
    sheet.rows.push_back({ L"Decoded Symbols", std::to_wstring(totalSymsDecoded) });
    if (!interpPath.empty()) {
        sheet.rows.push_back({ L"Program Interpreter (PT_INTERP)", interpPath });
    }
    if (!soName.empty()) {
        sheet.rows.push_back({ L"Shared Object Name (DT_SONAME)", soName });
    }
    if (!runPath.empty()) {
        sheet.rows.push_back({ L"Library Search Path (RPATH/RUNPATH)", runPath });
    }
    if (!neededLibs.empty()) {
        std::wstring joined;
        for (size_t i = 0; i < neededLibs.size(); ++i) {
            if (i > 0) joined += L", ";
            joined += neededLibs[i];
        }
        sheet.rows.push_back({ L"Needed Shared Libraries (DT_NEEDED)", joined });
    }

    out.sheets.push_back(std::move(sheet));
    if (phSheet.rows.size() > 1)  out.sheets.push_back(std::move(phSheet));
    if (shSheet.rows.size() > 1)  out.sheets.push_back(std::move(shSheet));
    if (dynSheet.rows.size() > 1) out.sheets.push_back(std::move(dynSheet));
    if (symSheet.rows.size() > 1) out.sheets.push_back(std::move(symSheet));

    if (!src.ReadFailed()) out.sourceReadFully = true; // every table shown was read, wherever it lies in the file
    out.valid = !out.sheets.empty();
    return out.valid;
}

} // namespace Formats
