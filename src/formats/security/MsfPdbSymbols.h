#pragma once
// MsfPdbSymbols.h — Microsoft Program Database Symbols (.pdb — MSF 7.00)

namespace Formats {

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


} // namespace Formats
