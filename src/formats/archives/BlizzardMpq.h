#pragma once
// BlizzardMpq.h — Blizzard MoPaQ archive reader & Storm sector extractor (.mpq — "MPQ\x1A" / "MPQ\x1B").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailUnixAndLegacyArc {

// ============================================================================
// #26. Blizzard MoPaQ Archive (.mpq — "MPQ\x1A" / "MPQ\x1B")
// ============================================================================
inline bool ParseOrExtractBlizzardMpq(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetPath,
    std::vector<uint8_t>* outExtractedBytes)
{
    if (size < 32) return false;
    size_t mpqOff = SIZE_MAX;
    if (std::memcmp(data, "MPQ\x1A", 4) == 0) {
        mpqOff = 0;
    } else if (std::memcmp(data, "MPQ\x1B", 4) == 0 && size >= 16) {
        uint32_t hdrOff = RdU32LE(data + 8);
        if (static_cast<uint64_t>(hdrOff) + 32ULL <= size && std::memcmp(data + hdrOff, "MPQ\x1A", 4) == 0) {
            mpqOff = hdrOff;
        }
    }
    if (mpqOff == SIZE_MAX || mpqOff + 32 > size) return false;
    if (outInfo) outInfo->formatName = L"Blizzard MoPaQ Archive (.mpq)";

    const uint8_t* mpqHdr   = data + mpqOff;
    uint32_t hdrSz          = RdU32LE(mpqHdr + 4);
    uint16_t secShift       = RdU16LE(mpqHdr + 14);
    uint32_t hashTblRelPos  = RdU32LE(mpqHdr + 16);
    uint32_t blockTblRelPos = RdU32LE(mpqHdr + 20);
    uint32_t hashTblCount   = RdU32LE(mpqHdr + 24);
    uint32_t blockCount     = RdU32LE(mpqHdr + 28);

    if (hdrSz < 32 || secShift > 15 || blockCount == 0 || blockCount > 100000) return false;
    uint64_t absBlockTblPos = mpqOff + static_cast<uint64_t>(blockTblRelPos);
    if (absBlockTblPos + static_cast<uint64_t>(blockCount) * 16ULL > size) return false;
    const uint32_t sectorSize = 512u << secShift;

    // Build Storm MPQ encryption & hash lookup table
    uint32_t cryptTbl[0x500] = {};
    uint32_t seed = 0x00100001u;
    for (uint32_t idx1 = 0; idx1 < 0x100u; ++idx1) {
        uint32_t idx2 = idx1;
        for (int i = 0; i < 5; ++i, idx2 += 0x100u) {
            seed = (seed * 125u + 3u) % 0x2AAAABu;
            uint32_t t1 = (seed & 0xFFFFu) << 16;
            seed = (seed * 125u + 3u) % 0x2AAAABu;
            uint32_t t2 = (seed & 0xFFFFu);
            cryptTbl[idx2] = t1 | t2;
        }
    }

    auto mpqHashEx = [&](std::string_view sv, uint32_t hashType, bool slashToBackslash) -> uint32_t {
        uint32_t s1 = 0x7FED7FEDu, s2 = 0xEEEEEEEEu;
        for (char rawCh : sv) {
            char c = (slashToBackslash && rawCh == '/') ? '\\' : rawCh;
            uint8_t ch = static_cast<uint8_t>(toupper(static_cast<unsigned char>(c)));
            s1 = cryptTbl[hashType + ch] ^ (s1 + s2);
            s2 = ch + s1 + s2 + (s2 << 5) + 3u;
        }
        return s1;
    };

    auto mpqDecryptWords = [&](uint32_t* words, size_t dwordCount, uint32_t k1) {
        uint32_t k2 = 0xEEEEEEEEu;
        for (size_t i = 0; i < dwordCount; ++i) {
            k2 += cryptTbl[0x400u + (k1 & 0xFFu)];
            uint32_t plain = words[i] ^ (k1 + k2);
            k1 = ((~k1 << 0x15) + 0x11111111u) | (k1 >> 0x0B);
            k2 = plain + k2 + (k2 << 5) + 3u;
            words[i] = plain;
        }
    };

    auto mpqDecryptBytes = [&](uint8_t* buf, size_t byteLen, uint32_t k1) {
        uint32_t k2 = 0xEEEEEEEEu;
        size_t dwords = byteLen / 4u;
        for (size_t i = 0; i < dwords; ++i) {
            k2 += cryptTbl[0x400u + (k1 & 0xFFu)];
            uint32_t enc = RdU32LE(buf + i * 4u);
            uint32_t plain = enc ^ (k1 + k2);
            k1 = ((~k1 << 0x15) + 0x11111111u) | (k1 >> 0x0B);
            k2 = plain + k2 + (k2 << 5) + 3u;
            buf[i * 4u + 0] = static_cast<uint8_t>(plain & 0xFFu);
            buf[i * 4u + 1] = static_cast<uint8_t>((plain >> 8) & 0xFFu);
            buf[i * 4u + 2] = static_cast<uint8_t>((plain >> 16) & 0xFFu);
            buf[i * 4u + 3] = static_cast<uint8_t>((plain >> 24) & 0xFFu);
        }
    };

    // Read & validate/decrypt Block Table (detecting unencrypted synthetic vs Storm-encrypted "(block table)")
    std::vector<uint32_t> rawBt(static_cast<size_t>(blockCount) * 4u);
    for (size_t i = 0; i < rawBt.size(); ++i) {
        rawBt[i] = RdU32LE(data + static_cast<size_t>(absBlockTblPos) + i * 4u);
    }
    std::vector<uint32_t> decBt = rawBt;
    mpqDecryptWords(decBt.data(), decBt.size(), mpqHashEx("(block table)", 0x300u, false));

    auto isBlockTableValid = [&](const std::vector<uint32_t>& bt) -> bool {
        uint32_t validExisting = 0;
        for (uint32_t i = 0; i < blockCount; ++i) {
            uint32_t fRel = bt[i * 4u + 0];
            uint32_t cSz  = bt[i * 4u + 1];
            uint32_t fSz  = bt[i * 4u + 2];
            uint32_t flg  = bt[i * 4u + 3];
            if ((flg & 0x80000000u) == 0) continue;
            if (mpqOff + static_cast<uint64_t>(fRel) + static_cast<uint64_t>(cSz) > size) return false;
            if (fSz > 1024ULL * 1024ULL * 1024ULL) return false;
            ++validExisting;
        }
        return validExisting > 0;
    };

    bool decBtOk = isBlockTableValid(decBt);
    bool rawBtOk = isBlockTableValid(rawBt);
    if (!decBtOk && !rawBtOk) return false;
    const std::vector<uint32_t>& btWords = decBtOk ? decBt : rawBt;

    // Read & validate/decrypt Hash Table if present
    uint64_t absHashTblPos = mpqOff + static_cast<uint64_t>(hashTblRelPos);
    bool hasHashTable = (hashTblCount > 0 && hashTblCount <= 262144u &&
                         absHashTblPos + static_cast<uint64_t>(hashTblCount) * 16ULL <= size);
    std::vector<uint32_t> htWords;
    if (hasHashTable) {
        std::vector<uint32_t> rawHt(static_cast<size_t>(hashTblCount) * 4u);
        for (size_t i = 0; i < rawHt.size(); ++i) {
            rawHt[i] = RdU32LE(data + static_cast<size_t>(absHashTblPos) + i * 4u);
        }
        std::vector<uint32_t> decHt = rawHt;
        mpqDecryptWords(decHt.data(), decHt.size(), mpqHashEx("(hash table)", 0x300u, false));

        auto isHashTableValid = [&](const std::vector<uint32_t>& ht, uint32_t& outUsed) -> bool {
            outUsed = 0;
            for (uint32_t i = 0; i < hashTblCount; ++i) {
                uint32_t bIdx = ht[i * 4u + 3u];
                if (bIdx == 0xFFFFFFFFu || bIdx == 0xFFFFFFFEu) continue;
                if (bIdx >= blockCount) return false;
                ++outUsed;
            }
            return true;
        };
        uint32_t decUsed = 0, rawUsed = 0;
        bool decHtOk = isHashTableValid(decHt, decUsed);
        bool rawHtOk = isHashTableValid(rawHt, rawUsed);
        if (decHtOk && (decUsed > 0 || !rawHtOk)) {
            htWords = std::move(decHt);
        } else if (rawHtOk) {
            htWords = std::move(rawHt);
        } else {
            hasHashTable = false;
        }
    }

    auto lookupMpqHash = [&](std::string_view filePath) -> int32_t {
        if (!hasHashTable || htWords.empty()) return -1;
        for (int pass = 0; pass < 2; ++pass) {
            bool normSlash = (pass == 0);
            if (pass == 1 && filePath.find('/') == std::string_view::npos) break;
            uint32_t startIdx = mpqHashEx(filePath, 0x000u, normSlash) % hashTblCount;
            uint32_t nameA    = mpqHashEx(filePath, 0x100u, normSlash);
            uint32_t nameB    = mpqHashEx(filePath, 0x200u, normSlash);
            for (uint32_t step = 0; step < hashTblCount; ++step) {
                uint32_t idx = (startIdx + step) % hashTblCount;
                uint32_t bIdx = htWords[idx * 4u + 3u];
                if (bIdx == 0xFFFFFFFFu) break;
                if (bIdx == 0xFFFFFFFEu) continue;
                if (htWords[idx * 4u + 0u] == nameA && htWords[idx * 4u + 1u] == nameB && bIdx < blockCount) {
                    return static_cast<int32_t>(bIdx);
                }
            }
        }
        return -1;
    };

    constexpr uint32_t kMpqImplode    = 0x00000100u;
    constexpr uint32_t kMpqCompress   = 0x00000200u;
    constexpr uint32_t kMpqEncrypted  = 0x00010000u;
    constexpr uint32_t kMpqFixKey     = 0x00020000u;
    constexpr uint32_t kMpqSingleUnit = 0x01000000u;
    constexpr uint32_t kMpqDeleteMark = 0x02000000u;
    constexpr uint32_t kMpqSectorCrc  = 0x04000000u;
    constexpr uint32_t kMpqExists     = 0x80000000u;

    auto decompressMpqSector = [&](const uint8_t* secPtr, size_t secCompLen, size_t secUncompLen, uint32_t flg, std::vector<uint8_t>& secOut) -> bool {
        if (secCompLen == secUncompLen || (flg & (kMpqCompress | kMpqImplode)) == 0) {
            if (secCompLen < secUncompLen) return false;
            secOut.assign(secPtr, secPtr + secUncompLen);
            return true;
        }
        if ((flg & kMpqCompress) == 0 || secCompLen < 2) return false;
        uint8_t compMask = secPtr[0];
        const uint8_t* payload = secPtr + 1;
        size_t payloadLen = secCompLen - 1;
        if (compMask == 0x00u) {
            if (payloadLen != secUncompLen) return false;
            secOut.assign(payload, payload + payloadLen);
            return true;
        }
        if ((compMask & 0x02u) != 0) { // Zlib
            if (!ZipDeflate::InflateZlib(payload, payloadLen, secOut, secUncompLen + 64) &&
                !ZipDeflate::InflateRaw(payload, payloadLen, secOut, secUncompLen + 64))
            {
                return false;
            }
            return secOut.size() == secUncompLen;
        }
        if ((compMask & 0x10u) != 0) { // BZip2
            if (!StreamCodecs::DecompressBzip2Stream(payload, payloadLen, secOut, secUncompLen + 64)) return false;
            return secOut.size() == secUncompLen;
        }
        if (compMask == 0x12u) { // LZMA
            if (payloadLen >= 5 &&
                StreamCodecs::DecompressLzma1(payload[0], RdU32LE(payload + 1), payload + 5, payloadLen - 5, secOut, secUncompLen, true, secUncompLen + 64) &&
                secOut.size() == secUncompLen)
            {
                return true;
            }
            if (payloadLen >= 6 && payload[0] == 0 &&
                StreamCodecs::DecompressLzma1(payload[1], RdU32LE(payload + 2), payload + 6, payloadLen - 6, secOut, secUncompLen, true, secUncompLen + 64) &&
                secOut.size() == secUncompLen)
            {
                return true;
            }
            return false;
        }
        return false;
    };

    auto extractMpqBlock = [&](uint32_t blockIdx, std::string_view fileNameForKey, std::vector<uint8_t>& outBuf) -> bool {
        if (blockIdx >= blockCount) return false;
        uint32_t fRelPos = btWords[blockIdx * 4u + 0u];
        uint32_t cSz     = btWords[blockIdx * 4u + 1u];
        uint32_t fSz     = btWords[blockIdx * 4u + 2u];
        uint32_t flg     = btWords[blockIdx * 4u + 3u];
        if ((flg & kMpqExists) == 0 || (flg & kMpqDeleteMark) != 0) return false;
        uint64_t absFilePos = mpqOff + static_cast<uint64_t>(fRelPos);
        if (absFilePos + static_cast<uint64_t>(cSz) > size || fSz > 256 * 1024 * 1024) return false;
        if (fSz == 0) {
            outBuf.clear();
            return true;
        }

        uint32_t fileKey = 0;
        if ((flg & kMpqEncrypted) != 0) {
            if (fileNameForKey.empty()) return false;
            size_t lastSep = fileNameForKey.find_last_of("\\/");
            std::string_view plainBase = (lastSep == std::string_view::npos) ? fileNameForKey : fileNameForKey.substr(lastSep + 1);
            fileKey = mpqHashEx(plainBase, 0x300u, false);
            if ((flg & kMpqFixKey) != 0) {
                fileKey = (fileKey + fRelPos) ^ fSz;
            }
        }

        const uint8_t* fData = data + static_cast<size_t>(absFilePos);
        if ((flg & kMpqSingleUnit) != 0) {
            std::vector<uint8_t> rawUnit(fData, fData + cSz);
            if ((flg & kMpqEncrypted) != 0) {
                mpqDecryptBytes(rawUnit.data(), rawUnit.size(), fileKey);
            }
            return decompressMpqSector(rawUnit.data(), rawUnit.size(), fSz, flg, outBuf);
        }

        if ((flg & (kMpqCompress | kMpqImplode)) == 0) {
            if (cSz < fSz) return false;
            outBuf.assign(fData, fData + fSz);
            if ((flg & kMpqEncrypted) != 0) {
                uint32_t numSecs = (fSz + sectorSize - 1u) / sectorSize;
                for (uint32_t s = 0; s < numSecs; ++s) {
                    size_t sOff = static_cast<size_t>(s) * sectorSize;
                    size_t sLen = std::min<size_t>(sectorSize, fSz - sOff);
                    mpqDecryptBytes(outBuf.data() + sOff, sLen, fileKey + s);
                }
            }
            return true;
        }

        uint32_t numSectors = (fSz + sectorSize - 1u) / sectorSize;
        uint32_t numOffsets = numSectors + 1u + ((flg & kMpqSectorCrc) ? 1u : 0u);
        size_t tblBytes = static_cast<size_t>(numOffsets) * 4u;
        if (tblBytes > cSz) return false;

        std::vector<uint8_t> tblBuf(fData, fData + tblBytes);
        if ((flg & kMpqEncrypted) != 0) {
            mpqDecryptBytes(tblBuf.data(), tblBytes, fileKey - 1u);
        }
        std::vector<uint32_t> secOffsets(numSectors + 1u);
        for (uint32_t s = 0; s <= numSectors; ++s) {
            secOffsets[s] = RdU32LE(tblBuf.data() + static_cast<size_t>(s) * 4u);
        }
        size_t minExpectedOff0 = static_cast<size_t>(numSectors + 1u) * 4u;
        if (secOffsets[0] != tblBytes && secOffsets[0] != minExpectedOff0) return false;

        outBuf.clear();
        outBuf.reserve(fSz);
        std::vector<uint8_t> secRaw, secDec;
        for (uint32_t s = 0; s < numSectors; ++s) {
            uint32_t sStart = secOffsets[s];
            uint32_t sEnd   = secOffsets[s + 1u];
            if (sStart > sEnd || sEnd > cSz) return false;
            size_t sCompLen   = sEnd - sStart;
            size_t sUncompLen = std::min<size_t>(sectorSize, fSz - outBuf.size());
            secRaw.assign(fData + sStart, fData + sEnd);
            if ((flg & kMpqEncrypted) != 0) {
                mpqDecryptBytes(secRaw.data(), secRaw.size(), fileKey + s);
            }
            if (!decompressMpqSector(secRaw.data(), sCompLen, sUncompLen, flg, secDec)) return false;
            outBuf.insert(outBuf.end(), secDec.begin(), secDec.end());
        }
        return outBuf.size() == fSz;
    };

    int32_t lfBlock   = lookupMpqHash("(listfile)");
    int32_t attrBlock = lookupMpqHash("(attributes)");
    int32_t sigBlock  = lookupMpqHash("(signature)");
    int32_t udatBlock = lookupMpqHash("(user data)");

    std::vector<std::string> listNames;
    auto parseListfileBytes = [&](std::string_view sv) {
        size_t lp = 0;
        while (lp < sv.size() && listNames.size() < 100000) {
            size_t le = sv.find_first_of("\r\n;", lp);
            if (le == std::string_view::npos) le = sv.size();
            std::string_view line = sv.substr(lp, le - lp);
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
            if (!line.empty()) listNames.emplace_back(line);
            lp = le + 1;
        }
    };

    if (lfBlock >= 0) {
        std::vector<uint8_t> lfBytes;
        if (extractMpqBlock(static_cast<uint32_t>(lfBlock), "(listfile)", lfBytes) && !lfBytes.empty()) {
            parseListfileBytes(std::string_view(reinterpret_cast<const char*>(lfBytes.data()), lfBytes.size()));
        }
    } else if (!hasHashTable) {
        for (uint32_t i = 0; i < blockCount; ++i) {
            uint32_t fRel = btWords[i * 4u + 0u];
            uint32_t cSz  = btWords[i * 4u + 1u];
            uint32_t fSz  = btWords[i * 4u + 2u];
            uint32_t flg  = btWords[i * 4u + 3u];
            uint64_t absP = mpqOff + static_cast<uint64_t>(fRel);
            if ((flg & kMpqExists) && cSz == fSz && fSz > 4 && fSz < 1024 * 1024 && absP + fSz <= size) {
                std::string_view sv(reinterpret_cast<const char*>(data + static_cast<size_t>(absP)), fSz);
                if ((sv.find('\\') != std::string_view::npos || sv.find('.') != std::string_view::npos) &&
                    sv.find('\n') != std::string_view::npos)
                {
                    bool printable = true;
                    for (size_t c = 0; c < std::min<size_t>(fSz, 64); ++c) {
                        uint8_t b = static_cast<uint8_t>(sv[c]);
                        if (b < 9 || (b > 13 && b < 32) || b > 126) { printable = false; break; }
                    }
                    if (printable) {
                        parseListfileBytes(sv);
                        lfBlock = static_cast<int32_t>(i);
                        break;
                    }
                }
            }
        }
    }

    std::vector<std::string> blockToName(blockCount);
    if (hasHashTable) {
        for (const std::string& nm : listNames) {
            if (_stricmp(nm.c_str(), "(listfile)") == 0 ||
                _stricmp(nm.c_str(), "(attributes)") == 0 ||
                _stricmp(nm.c_str(), "(signature)") == 0 ||
                _stricmp(nm.c_str(), "(user data)") == 0)
            {
                continue;
            }
            int32_t bIdx = lookupMpqHash(nm);
            if (bIdx >= 0 && static_cast<uint32_t>(bIdx) < blockCount && blockToName[static_cast<size_t>(bIdx)].empty()) {
                blockToName[static_cast<size_t>(bIdx)] = nm;
            }
        }
    } else {
        size_t nameCursor = 0;
        for (uint32_t i = 0; i < blockCount && nameCursor < listNames.size(); ++i) {
            if (static_cast<int32_t>(i) == lfBlock) continue;
            blockToName[i] = listNames[nameCursor++];
        }
    }

    std::wstring wantNorm = extractTargetPath ? ArchivePath::Normalize(*extractTargetPath) : std::wstring{};

    for (uint32_t i = 0; i < blockCount; ++i) {
        if (static_cast<int32_t>(i) == lfBlock ||
            static_cast<int32_t>(i) == attrBlock ||
            static_cast<int32_t>(i) == sigBlock ||
            static_cast<int32_t>(i) == udatBlock)
        {
            continue;
        }
        uint32_t fRelPos = btWords[i * 4u + 0u];
        uint32_t cSz     = btWords[i * 4u + 1u];
        uint32_t fSz     = btWords[i * 4u + 2u];
        uint32_t flg     = btWords[i * 4u + 3u];
        uint64_t absP    = mpqOff + static_cast<uint64_t>(fRelPos);
        if ((flg & kMpqExists) == 0 || (flg & kMpqDeleteMark) != 0 || absP + cSz > size) continue;

        std::wstring displayPath;
        if (!blockToName[i].empty()) {
            displayPath = ArchivePath::Normalize(PathUtils::Utf8ToWide(blockToName[i]));
        } else {
            wchar_t nb[64] = {};
            swprintf_s(nb, L"Files/Block_%04u.dat", i);
            displayPath = nb;
        }

        if (extractTargetPath && outExtractedBytes) {
            if (_wcsicmp(displayPath.c_str(), wantNorm.c_str()) == 0) {
                return extractMpqBlock(i, blockToName[i], *outExtractedBytes);
            }
            continue;
        }

        if (outInfo) {
            ArchiveItemEntry item;
            item.pathInArchive = std::move(displayPath);
            item.name = ArchivePath::BaseName(item.pathInArchive);
            item.compressedSize = cSz;
            item.uncompressedSize = fSz;
            item.rawDataOffset = absP;
            item.compressionMethod = (((flg & (kMpqCompress | kMpqImplode | kMpqEncrypted)) == 0) && cSz == fSz) ? 0 : 8;
            outInfo->entries.push_back(std::move(item));
        }
    }

    if (extractTargetPath) return false;
    if (!outInfo || outInfo->entries.empty()) return false;
    outInfo->valid = true;
    return true;
}

inline bool ParseBlizzardMpq(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractBlizzardMpq(data, size, &outInfo, nullptr, nullptr);
}

} // namespace DetailUnixAndLegacyArc

inline bool InspectBlizzardMpq(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailUnixAndLegacyArc::ParseBlizzardMpq(data, size, outInfo);
}

inline bool ExtractBlizzardMpqEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& want,
    std::vector<uint8_t>& outBytes)
{
    return DetailUnixAndLegacyArc::ParseOrExtractBlizzardMpq(data, size, nullptr, &want, &outBytes);
}

} // namespace Explorer2::Formats
