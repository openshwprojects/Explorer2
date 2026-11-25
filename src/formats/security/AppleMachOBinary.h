#pragma once
// AppleMachOBinary.h — Apple Mach-O 32/64 & Universal Fat Binary (.macho / .dylib)

namespace Formats {

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
    const uint32_t m = DetailBinarySysDoc::RdU32BE(raw.data());
    if (m != 0xCAFEBABEu && m != 0xCAFEBABFu) return false;
    const uint32_t nArch = DetailBinarySysDoc::RdU32BE(raw.data() + 4);
    if (nArch == 0 || nArch > 32) return false;
    const size_t entSz = (m == 0xCAFEBABFu) ? 32 : 20;
    if (8 + static_cast<size_t>(nArch) * entSz > raw.size()) return false;
    for (uint32_t i = 0; i < nArch; ++i) {
        const uint8_t* ap = raw.data() + 8 + i * entSz;
        const uint64_t off = (m == 0xCAFEBABFu) ? DetailBinarySysDoc::RdU64BE(ap + 8) : DetailBinarySysDoc::RdU32BE(ap + 8);
        if (off > fileSize || fileSize - off < 28) return false;
        if (off > raw.size() || raw.size() - off < 28) continue;
        const uint32_t sliceMagic = DetailBinarySysDoc::RdU32LE(raw.data() + static_cast<size_t>(off));
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
            default:          return DetailBinarySysDoc::Hex32(cpu);
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
        const uint32_t magicLE = DetailBinarySysDoc::RdU32LE(hdr);
        if (!IsValidMachOMagic(magicLE)) return false;

        const bool is64 = (magicLE == 0xFEEDFACFu || magicLE == 0xCFFAEDFEu);
        const bool isBE = (magicLE == 0xCEFAEDFEu || magicLE == 0xCFFAEDFEu);
        const size_t machHdrSz = is64 ? 32 : 28;
        if (sliceSz < machHdrSz) return false;
        auto ld32 = [&](const uint8_t* p) -> uint32_t { return isBE ? DetailBinarySysDoc::RdU32BE(p) : DetailBinarySysDoc::RdU32LE(p); };
        auto ld64 = [&](const uint8_t* p) -> uint64_t { return isBE ? DetailBinarySysDoc::RdU64BE(p) : DetailBinarySysDoc::RdU64LE(p); };
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
        hdrSh.rows.push_back({ sliceLabel + L" Header Flags", DetailBinarySysDoc::Hex32(flags) });

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
                    DetailBinarySysDoc::Hex64(vmaddr) + L" (" + PathUtils::FormatByteSize(vmsize) + L")" + (secList.empty() ? L"" : (L" [" + secList + L"]"))
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
                cmdSh.rows.push_back({ sliceLabel, L"LC_MAIN", L"Entry Offset", DetailBinarySysDoc::Hex64(entryOff) });
            } else if (cmd == 0x1Bu && cmdSz >= 24) { // LC_UUID
                cmdSh.rows.push_back({ sliceLabel, L"LC_UUID", L"Image UUID", DetailBinarySysDoc::HexBytes(base + lp + 8, 16, L'-') });
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
                        symSh.rows.push_back({ sliceLabel, PathUtils::Utf8ToWide(sname), DetailBinarySysDoc::Hex64(nval), kind });
                    }
                }
            }
            lp += cmdSz;
        }
        return true;
    };

    if (IsValidMachOFatArchive(raw, out.sourceFileSize)) {
        const uint32_t m = DetailBinarySysDoc::RdU32BE(raw.data());
        const uint32_t nArch = DetailBinarySysDoc::RdU32BE(raw.data() + 4);
        const size_t entSz = (m == 0xCAFEBABFu) ? 32 : 20;
        hdrSh.rows.push_back({ L"Container", L"Universal Fat Binary (" + std::to_wstring(nArch) + L" architectures)" });
        for (uint32_t i = 0; i < nArch; ++i) {
            const uint8_t* ap = raw.data() + 8 + i * entSz;
            const uint32_t cpu = DetailBinarySysDoc::RdU32BE(ap + 0);
            const uint64_t off = (m == 0xCAFEBABFu) ? DetailBinarySysDoc::RdU64BE(ap + 8) : DetailBinarySysDoc::RdU32BE(ap + 8);
            const uint64_t sz  = (m == 0xCAFEBABFu) ? DetailBinarySysDoc::RdU64BE(ap + 16): DetailBinarySysDoc::RdU32BE(ap + 12);
            if (off <= fileSize && sz <= fileSize - off) {
                parseThinSlice(off, sz, cpuName(cpu));
            }
        }
    } else if (raw.size() >= 28 && IsValidMachOMagic(DetailBinarySysDoc::RdU32LE(raw.data()))) {
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


} // namespace Formats
