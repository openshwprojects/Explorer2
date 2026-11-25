#pragma once
// MsfPdbSymbols.h — Microsoft Program Database Symbols (.pdb — MSF 7.00 + TPI + DBI + CodeView Symbols)

namespace Formats {

inline bool ParseMsfPdbSymbols(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 56 || std::memcmp(raw.data(), "Microsoft C/C++ MSF 7.00", 24) != 0) return false;

    const uint32_t blkSize      = DetailBinarySysDoc::RdU32LE(raw.data() + 32);
    const uint32_t numBlocks    = DetailBinarySysDoc::RdU32LE(raw.data() + 40);
    const uint32_t dirBytes     = DetailBinarySysDoc::RdU32LE(raw.data() + 44);
    const uint32_t blockMapAddr = DetailBinarySysDoc::RdU32LE(raw.data() + 52);

    if (blkSize < 512 || blkSize > 8192 || (blkSize & (blkSize - 1u)) != 0) return false;
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
        const uint32_t blkIdx = DetailBinarySysDoc::RdU32LE(blockMap + static_cast<size_t>(i) * 4);
        const uint64_t bOff   = static_cast<uint64_t>(blkIdx) * blkSize;
        if (bOff + blkSize > fileSize) break;
        const size_t take = std::min<size_t>(blkSize, dirBytes - dirStream.size());
        const uint8_t* b = src.Read(bOff, take, blockBuf);
        if (!b) break;
        dirStream.insert(dirStream.end(), b, b + take);
    }

    uint32_t numStreams = 0;
    std::vector<uint32_t> streamSizes;
    std::vector<size_t> streamDirBlockOffsets; // Offset inside dirStream to each stream's u32 block list

    if (dirStream.size() >= 4) {
        numStreams = std::min<uint32_t>(DetailBinarySysDoc::RdU32LE(dirStream.data()), 16384u);
        size_t dp = 4;
        if (dp + static_cast<size_t>(numStreams) * 4 <= dirStream.size()) {
            streamSizes.resize(numStreams);
            for (uint32_t s = 0; s < numStreams; ++s) {
                const uint32_t sz = DetailBinarySysDoc::RdU32LE(dirStream.data() + dp + s * 4);
                streamSizes[s] = (sz == 0xFFFFFFFFu) ? 0u : sz;
            }
            dp += static_cast<size_t>(numStreams) * 4;
            streamDirBlockOffsets.assign(numStreams, SIZE_MAX);
            for (uint32_t s = 0; s < numStreams; ++s) {
                const uint64_t nb = (static_cast<uint64_t>(streamSizes[s]) + blkSize - 1u) / blkSize;
                if (nb * 4ULL > dirStream.size() - dp) break;
                streamDirBlockOffsets[s] = dp;
                dp += static_cast<size_t>(nb) * 4u;
            }
        }
    }

    auto readStream = [&](uint32_t sIdx, size_t maxOut = 4 * 1024 * 1024) -> std::vector<uint8_t> {
        std::vector<uint8_t> res;
        if (sIdx >= streamSizes.size() || sIdx >= streamDirBlockOffsets.size() ||
            streamSizes[sIdx] == 0 || streamDirBlockOffsets[sIdx] == SIZE_MAX) {
            return res;
        }
        const size_t want = std::min<size_t>(streamSizes[sIdx], maxOut);
        const uint64_t nb = (static_cast<uint64_t>(want) + blkSize - 1u) / blkSize;
        const size_t listBase = streamDirBlockOffsets[sIdx];
        res.reserve(want);
        for (uint64_t bIdx = 0; bIdx < nb && res.size() < want; ++bIdx) {
            const size_t ptrOff = listBase + static_cast<size_t>(bIdx) * 4u;
            if (ptrOff + 4u > dirStream.size()) break;
            const uint32_t blkIdx = DetailBinarySysDoc::RdU32LE(dirStream.data() + ptrOff);
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
        const uint32_t pdbVer = DetailBinarySysDoc::RdU32LE(s1.data() + 0);
        const uint32_t pdbSig = DetailBinarySysDoc::RdU32LE(s1.data() + 4);
        const uint32_t pdbAge = DetailBinarySysDoc::RdU32LE(s1.data() + 8);
        const std::wstring guidStr = DetailBinarySysDoc::FormatGuidLE(s1.data() + 12);
        hdrSh.rows.push_back({ L"PDB Stream Version", std::to_wstring(pdbVer) });
        hdrSh.rows.push_back({ L"PDB Timestamp", DetailBinarySysDoc::FormatUnixTimeUtc(pdbSig) });
        hdrSh.rows.push_back({ L"PDB Age", std::to_wstring(pdbAge) });
        hdrSh.rows.push_back({ L"PDB GUID", guidStr });
    }

    // Stream 2: TPI (Type Info) Stream Header (56 bytes)
    std::vector<uint8_t> s2 = readStream(2, 64);
    if (s2.size() >= 56) {
        const uint32_t tpiVer   = DetailBinarySysDoc::RdU32LE(s2.data() + 0);
        const uint32_t tpiHdrSz = DetailBinarySysDoc::RdU32LE(s2.data() + 4);
        const uint32_t tiBegin  = DetailBinarySysDoc::RdU32LE(s2.data() + 8);
        const uint32_t tiEnd    = DetailBinarySysDoc::RdU32LE(s2.data() + 12);
        const uint32_t tiBytes  = DetailBinarySysDoc::RdU32LE(s2.data() + 16);
        if (tpiHdrSz == 56 && tiEnd >= tiBegin && tiBegin >= 0x1000u) {
            wchar_t tpiBuf[128] = {};
            swprintf_s(tpiBuf, L"%u types (TI 0x%X..0x%X, %s, v%u)",
                tiEnd - tiBegin, tiBegin, tiEnd, PathUtils::FormatByteSize(tiBytes).c_str(), tpiVer);
            hdrSh.rows.push_back({ L"TPI Type Stream (Stream 2)", tpiBuf });
        }
    }

    // Stream 3: DBI Debug Info Stream -> ModInfo compiled .obj modules + symRecStream index (+20)
    SpreadsheetSheet modSh;
    modSh.name = L"PDB_Modules";
    modSh.maxCols = 3;
    modSh.rows.push_back({ L"Module #", L"Module / Object Name", L"Archive / Library Path" });

    uint16_t symRecStreamIdx = 0xFFFFu;
    std::vector<uint8_t> s3 = readStream(3, 8 * 1024 * 1024);
    if (s3.size() >= 64) {
        symRecStreamIdx = DetailBinarySysDoc::RdU16LE(s3.data() + 20);
        const int32_t modInfoSz = static_cast<int32_t>(DetailBinarySysDoc::RdU32LE(s3.data() + 24));
        const uint16_t machine  = DetailBinarySysDoc::RdU16LE(s3.data() + 60);
        std::wstring machStr = DetailBinarySysDoc::Hex32(machine);
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

    // CodeView Symbol Record Stream (symRecStream from DBI header +20)
    SpreadsheetSheet symSh;
    symSh.name = L"Symbols";
    symSh.maxCols = 4;
    symSh.rows.push_back({ L"Symbol Kind", L"Name", L"Location / TypeIndex", L"Details" });

    if (symRecStreamIdx != 0xFFFFu && symRecStreamIdx > 0 && symRecStreamIdx < numStreams) {
        std::vector<uint8_t> symBuf = readStream(symRecStreamIdx, 8 * 1024 * 1024);
        auto readNullStr = [](const uint8_t* p, size_t maxLen) -> std::wstring {
            size_t n = 0;
            while (n < maxLen && p[n] != 0) ++n;
            return PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), n));
        };
        auto fmtSecOff = [](uint16_t seg, uint32_t off) -> std::wstring {
            wchar_t b[48] = {};
            swprintf_s(b, L"%04X:%08X", seg, off);
            return b;
        };

        size_t sp = 0;
        while (sp + 4 <= symBuf.size() && symSh.rows.size() <= 4000) {
            const uint16_t recLen = DetailBinarySysDoc::RdU16LE(symBuf.data() + sp);
            if (recLen < 2 || sp + 2u + recLen > symBuf.size()) break;
            const uint16_t recTyp = DetailBinarySysDoc::RdU16LE(symBuf.data() + sp + 2);
            const uint8_t* pay = symBuf.data() + sp + 4;
            const size_t payLen = recLen - 2u;
            sp += 2u + recLen;

            if (recTyp == 0x110Eu && payLen > 10) { // S_PUB32
                const uint32_t pubFlags = DetailBinarySysDoc::RdU32LE(pay + 0);
                const uint32_t off      = DetailBinarySysDoc::RdU32LE(pay + 4);
                const uint16_t seg      = DetailBinarySysDoc::RdU16LE(pay + 8);
                std::wstring nm = readNullStr(pay + 10, payLen - 10);
                if (!nm.empty()) {
                    symSh.rows.push_back({
                        L"S_PUB32",
                        std::move(nm),
                        fmtSecOff(seg, off),
                        (pubFlags & 2u) ? L"Function" : ((pubFlags & 1u) ? L"Code" : L"Public Data")
                    });
                }
            } else if ((recTyp == 0x1110u || recTyp == 0x110Fu) && payLen > 35) { // S_GPROC32 / S_LPROC32
                const uint32_t codeLen = DetailBinarySysDoc::RdU32LE(pay + 12);
                const uint32_t typeIdx = DetailBinarySysDoc::RdU32LE(pay + 24);
                const uint32_t off     = DetailBinarySysDoc::RdU32LE(pay + 28);
                const uint16_t seg     = DetailBinarySysDoc::RdU16LE(pay + 32);
                std::wstring nm = readNullStr(pay + 35, payLen - 35);
                if (!nm.empty()) {
                    symSh.rows.push_back({
                        (recTyp == 0x1110u) ? L"S_GPROC32" : L"S_LPROC32",
                        std::move(nm),
                        fmtSecOff(seg, off),
                        L"Size=" + std::to_wstring(codeLen) + L" B, Type=" + DetailBinarySysDoc::Hex32(typeIdx)
                    });
                }
            } else if ((recTyp == 0x110Du || recTyp == 0x110Cu) && payLen > 10) { // S_GDATA32 / S_LDATA32
                const uint32_t typeIdx = DetailBinarySysDoc::RdU32LE(pay + 0);
                const uint32_t off     = DetailBinarySysDoc::RdU32LE(pay + 4);
                const uint16_t seg     = DetailBinarySysDoc::RdU16LE(pay + 8);
                std::wstring nm = readNullStr(pay + 10, payLen - 10);
                if (!nm.empty()) {
                    symSh.rows.push_back({
                        (recTyp == 0x110Du) ? L"S_GDATA32" : L"S_LDATA32",
                        std::move(nm),
                        fmtSecOff(seg, off),
                        L"Type=" + DetailBinarySysDoc::Hex32(typeIdx)
                    });
                }
            } else if (recTyp == 0x1108u && payLen > 4) { // S_UDT
                const uint32_t typeIdx = DetailBinarySysDoc::RdU32LE(pay + 0);
                std::wstring nm = readNullStr(pay + 4, payLen - 4);
                if (!nm.empty()) {
                    symSh.rows.push_back({ L"S_UDT", std::move(nm), DetailBinarySysDoc::Hex32(typeIdx), L"User-Defined Type" });
                }
            } else if (recTyp == 0x1107u && payLen > 6) { // S_CONSTANT
                const uint32_t typeIdx = DetailBinarySysDoc::RdU32LE(pay + 0);
                const uint16_t leaf    = DetailBinarySysDoc::RdU16LE(pay + 4);
                size_t valBytes = 0;
                std::wstring valStr;
                if (leaf < 0x8000u) {
                    valStr = std::to_wstring(leaf);
                } else if (leaf == 0x8000u && payLen > 7) { // LF_CHAR
                    valBytes = 1; valStr = std::to_wstring(static_cast<int8_t>(pay[6]));
                } else if (leaf == 0x8001u && payLen > 8) { // LF_SHORT
                    valBytes = 2; valStr = std::to_wstring(static_cast<int16_t>(DetailBinarySysDoc::RdU16LE(pay + 6)));
                } else if (leaf == 0x8002u && payLen > 8) { // LF_USHORT
                    valBytes = 2; valStr = std::to_wstring(DetailBinarySysDoc::RdU16LE(pay + 6));
                } else if (leaf == 0x8003u && payLen > 10) { // LF_LONG
                    valBytes = 4; valStr = std::to_wstring(static_cast<int32_t>(DetailBinarySysDoc::RdU32LE(pay + 6)));
                } else if (leaf == 0x8004u && payLen > 10) { // LF_ULONG
                    valBytes = 4; valStr = std::to_wstring(DetailBinarySysDoc::RdU32LE(pay + 6));
                } else if ((leaf == 0x8009u || leaf == 0x800Au) && payLen > 14) { // LF_QUADWORD / LF_UQUADWORD
                    valBytes = 8; valStr = std::to_wstring(DetailBinarySysDoc::RdU64LE(pay + 6));
                } else {
                    continue;
                }
                if (6u + valBytes < payLen) {
                    std::wstring nm = readNullStr(pay + 6 + valBytes, payLen - 6 - valBytes);
                    if (!nm.empty()) {
                        symSh.rows.push_back({ L"S_CONSTANT", std::move(nm), DetailBinarySysDoc::Hex32(typeIdx), L"Value=" + valStr });
                    }
                }
            }
        }
        if (symSh.rows.size() > 1) {
            hdrSh.rows.push_back({ L"Decoded Symbol Records", std::to_wstring(symSh.rows.size() - 1) + L" (Stream #" + std::to_wstring(symRecStreamIdx) + L")" });
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Microsoft Program Database Symbols (.pdb)";
    out.sheets.push_back(std::move(hdrSh));
    if (modSh.rows.size() > 1) out.sheets.push_back(std::move(modSh));
    if (symSh.rows.size() > 1) out.sheets.push_back(std::move(symSh));
    if (!src.ReadFailed()) out.sourceReadFully = true;
    out.valid = true;
    return true;
}

} // namespace Formats
