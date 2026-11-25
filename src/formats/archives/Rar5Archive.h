#pragma once
// Rar5Archive.h — RAR 5.0 ('Rar!\x1A\x07\x01\x00') variable-int block header reader & pure-C++ v5 LZ+Huffman+Filter decompressor.
#include "../common/ArchiveFormatCommon.h"
#include <array>

namespace Explorer2::Formats {

namespace DetailRar5 {

struct Rar5ParsedEntry {
    std::wstring pathInArchive;
    uint64_t unpSize = 0;
    uint64_t packSize = 0;
    uint64_t rawDataOffset = 0;
    uint32_t dataCrc = 0;
    bool hasDataCrc = false;
    uint16_t dosDate = 0;
    uint16_t dosTime = 0;
    uint8_t algVer = 0;
    uint8_t method = 0; // 0 = Store, 1..5 = RAR5 LZ+Huffman
    bool isSolid = false;
    bool isDirectory = false;
    bool isEncrypted = false;
    bool isSplitBefore = false;
    bool isSplitAfter = false;
    size_t dictSize = 128u * 1024u;
};

inline bool EnumerateRar5Entries(const uint8_t* data, size_t size, std::vector<Rar5ParsedEntry>& outEntries) {
    outEntries.clear();
    if (!data || size < 14 || std::memcmp(data, "Rar!\x1A\x07\x01\x00", 8) != 0) return false;

    size_t pos = 8;
    while (size - pos >= 7u) {
        pos += 4u; // Header CRC32
        uint64_t hdrSize = 0;
        if (!ByteIO::RdLeb128(data, size, pos, hdrSize) || hdrSize == 0 ||
            hdrSize > 16ULL * 1024 * 1024 || hdrSize > size - pos)
        {
            break;
        }
        const size_t hdrEnd = pos + static_cast<size_t>(hdrSize);

        size_t hpos = pos;
        uint64_t hdrType = 0, hdrFlags = 0, extraAreaSize = 0, dataSize = 0;
        if (!ByteIO::RdLeb128(data, hdrEnd, hpos, hdrType)) break;
        if (!ByteIO::RdLeb128(data, hdrEnd, hpos, hdrFlags)) break;
        if ((hdrFlags & 0x0001u) != 0u && !ByteIO::RdLeb128(data, hdrEnd, hpos, extraAreaSize)) break;
        if ((hdrFlags & 0x0002u) != 0u && !ByteIO::RdLeb128(data, hdrEnd, hpos, dataSize)) break;

        // Reject -hp archive encryption header (Type 4)
        if (hdrType == 4u) {
            outEntries.clear();
            return false;
        }

        if (dataSize > size - hdrEnd) break;
        if (extraAreaSize > hdrEnd - hpos) break;
        const size_t mainFieldEnd = hdrEnd - static_cast<size_t>(extraAreaSize);

        if (hdrType == 2u) { // File header
            uint64_t fileFlags = 0, unpSize = 0, attr = 0, compInfo = 0, hostOs = 0, nameLen = 0;
            if (ByteIO::RdLeb128(data, mainFieldEnd, hpos, fileFlags) &&
                ByteIO::RdLeb128(data, mainFieldEnd, hpos, unpSize) &&
                ByteIO::RdLeb128(data, mainFieldEnd, hpos, attr))
            {
                uint16_t dosDate = 0, dosTime = 0;
                if ((fileFlags & 0x0002u) != 0u) {
                    if (mainFieldEnd - hpos < 4u) break;
                    const uint32_t unixMtime = ByteIO::RdU32LE(data + hpos);
                    DetailDiskAndGameArc::UnixSecondsToDosDateTime(unixMtime, dosDate, dosTime);
                    hpos += 4u;
                }
                uint32_t dataCrc = 0;
                const bool hasCrc = (fileFlags & 0x0004u) != 0u;
                if (hasCrc) {
                    if (mainFieldEnd - hpos < 4u) break;
                    dataCrc = ByteIO::RdU32LE(data + hpos);
                    hpos += 4u;
                }
                if (ByteIO::RdLeb128(data, mainFieldEnd, hpos, compInfo) &&
                    ByteIO::RdLeb128(data, mainFieldEnd, hpos, hostOs) &&
                    ByteIO::RdLeb128(data, mainFieldEnd, hpos, nameLen))
                {
                    if (nameLen > 0u && nameLen < 4096u && nameLen <= mainFieldEnd - hpos) {
                        std::string utf8Name(reinterpret_cast<const char*>(data + hpos), static_cast<size_t>(nameLen));
                        std::wstring normPath = ArchivePath::Normalize(PathUtils::Utf8ToWide(utf8Name));
                        if (!normPath.empty()) {
                            bool isEncrypted = false;
                            // Inspect file extra area for File Encryption (0x01) or File Time (0x02)
                            size_t epos = mainFieldEnd;
                            while (epos < hdrEnd) {
                                uint64_t recSize = 0;
                                if (!ByteIO::RdLeb128(data, hdrEnd, epos, recSize) || recSize == 0 || recSize > hdrEnd - epos) break;
                                const size_t recEnd = epos + static_cast<size_t>(recSize);
                                size_t rcur = epos;
                                uint64_t recType = 0;
                                if (ByteIO::RdLeb128(data, recEnd, rcur, recType)) {
                                    if (recType == 0x01u) {
                                        isEncrypted = true;
                                    } else if (recType == 0x02u && dosDate == 0u) {
                                        uint64_t tFlags = 0;
                                        if (ByteIO::RdLeb128(data, recEnd, rcur, tFlags) && (tFlags & 0x02u) != 0u) {
                                            const bool isUnix = (tFlags & 0x01u) != 0u;
                                            if (isUnix && recEnd - rcur >= 4u) {
                                                DetailDiskAndGameArc::UnixSecondsToDosDateTime(ByteIO::RdU32LE(data + rcur), dosDate, dosTime);
                                            } else if (!isUnix && recEnd - rcur >= 8u) {
                                                const uint64_t ticks = ByteIO::RdU64LE(data + rcur);
                                                if (ticks > 116444736000000000ULL) {
                                                    DetailDiskAndGameArc::UnixSecondsToDosDateTime(
                                                        (ticks - 116444736000000000ULL) / 10000000ULL, dosDate, dosTime);
                                                }
                                            }
                                        }
                                    }
                                }
                                epos = recEnd;
                            }

                            Rar5ParsedEntry ent;
                            ent.pathInArchive = std::move(normPath);
                            ent.unpSize       = ((fileFlags & 0x0008u) != 0u) ? 0ULL : unpSize;
                            ent.packSize      = dataSize;
                            ent.rawDataOffset = hdrEnd;
                            ent.dataCrc       = dataCrc;
                            ent.hasDataCrc    = hasCrc;
                            ent.dosDate       = dosDate;
                            ent.dosTime       = dosTime;
                            ent.algVer        = static_cast<uint8_t>(compInfo & 0x3Fu);
                            ent.isSolid       = (compInfo & 0x40u) != 0u;
                            ent.method        = static_cast<uint8_t>((compInfo >> 7u) & 0x07u);
                            ent.isDirectory   = (fileFlags & 0x0001u) != 0u;
                            ent.isEncrypted   = isEncrypted;
                            ent.isSplitBefore = (hdrFlags & 0x0008u) != 0u;
                            ent.isSplitAfter  = (hdrFlags & 0x0010u) != 0u;

                            if (ent.algVer == 0u) {
                                const uint32_t dictPow = static_cast<uint32_t>((compInfo >> 10u) & 0x0Fu);
                                const uint64_t ds64 = 0x20000ULL << dictPow;
                                ent.dictSize = static_cast<size_t>(std::min<uint64_t>(ds64, 128ULL * 1024 * 1024));
                            } else if (ent.algVer == 1u) {
                                const uint64_t base = 32ULL + ((compInfo >> 10u) & 1ULL);
                                const uint32_t sh   = static_cast<uint32_t>(((compInfo >> 11u) & 0x1Fu) + 11u);
                                const uint64_t ds64 = (sh < 36u) ? (base << sh) : (128ULL * 1024 * 1024);
                                ent.dictSize = static_cast<size_t>(std::min<uint64_t>(ds64, 128ULL * 1024 * 1024));
                            }
                            outEntries.push_back(std::move(ent));
                        }
                    }
                }
            }
        } else if (hdrType == 5u) { // End of archive header
            break;
        }

        pos = hdrEnd + static_cast<size_t>(dataSize);
    }

    return !outEntries.empty();
}

// MSB-first bit reader matching UnRAR BitInput (getbits.hpp)
struct Rar5BitReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t inAddr = 0;
    int inBit = 0; // 0..7

    uint32_t Peek16() const noexcept {
        const uint32_t b0 = (inAddr < size) ? data[inAddr] : 0u;
        const uint32_t b1 = (inAddr + 1u < size) ? data[inAddr + 1u] : 0u;
        const uint32_t b2 = (inAddr + 2u < size) ? data[inAddr + 2u] : 0u;
        const uint32_t field = (b0 << 16u) | (b1 << 8u) | b2;
        return (field >> (8 - inBit)) & 0xFFFFu;
    }

    void AddBits(int bits) noexcept {
        const int total = inBit + bits;
        inAddr += static_cast<size_t>(total >> 3);
        inBit = total & 7;
    }

    uint32_t GetBits(int bits) noexcept {
        if (bits <= 0) return 0u;
        if (bits <= 16) {
            const uint32_t v = Peek16() >> (16 - bits);
            AddBits(bits);
            return v;
        }
        const int hi = bits - 16;
        const uint32_t top = GetBits(hi);
        return (top << 16u) | GetBits(16);
    }

    void AlignToByte() noexcept {
        if (inBit != 0) {
            ++inAddr;
            inBit = 0;
        }
    }
};

// Canonical 15-bit MSB Huffman decoder matching UnRAR MakeDecodeTables (permits single-symbol & incomplete trees)
struct Rar5HuffTable {
    static constexpr int kMaxBits = 15;
    static constexpr size_t kFastSize = 1u << kMaxBits;
    std::vector<uint16_t> sym;
    std::vector<uint8_t>  bits;
    bool valid = false;

    bool Build(const uint8_t* lengths, size_t numSyms) {
        valid = false;
        sym.assign(kFastSize, 0u);
        bits.assign(kFastSize, 1u);

        uint32_t lenCount[kMaxBits + 1] = {};
        bool anyNonZero = false;
        for (size_t i = 0; i < numSyms; ++i) {
            const uint8_t L = lengths[i];
            if (L > kMaxBits) return false;
            if (L > 0u) {
                ++lenCount[L];
                anyNonZero = true;
            }
        }
        if (!anyNonZero) {
            // UnRAR initializes empty tables to decode symbol 0 using 1 bit
            valid = true;
            return true;
        }

        uint32_t nextCode[kMaxBits + 1] = {};
        uint32_t code = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            code = (code + lenCount[len - 1]) << 1u;
            if (code + lenCount[len] > (1u << len)) return false; // Over-subscribed tree
            nextCode[len] = code;
        }

        for (size_t i = 0; i < numSyms; ++i) {
            const uint8_t L = lengths[i];
            if (L == 0u) continue;
            const uint32_t c = nextCode[L]++;
            const size_t fill = static_cast<size_t>(1) << (kMaxBits - L);
            const size_t start = static_cast<size_t>(c) << (kMaxBits - L);
            if (start + fill > kFastSize) return false;
            for (size_t k = 0; k < fill; ++k) {
                sym[start + k]  = static_cast<uint16_t>(i);
                bits[start + k] = L;
            }
        }
        valid = true;
        return true;
    }

    uint32_t Decode(Rar5BitReader& br) const noexcept {
        const uint32_t idx = br.Peek16() >> 1u; // Top 15 bits
        br.AddBits(bits[idx]);
        return sym[idx];
    }
};

struct Rar5PendingFilter {
    size_t blockStart = 0; // Offset within current file's output
    size_t blockLength = 0;
    uint8_t type = 0;      // 0 = Delta, 1 = E8, 2 = E8E9, 3 = ARM
    uint8_t channels = 1;
};

inline void ApplyRar5FilterInPlace(uint8_t* data, size_t blockLength, size_t fileOffset, uint8_t type, uint8_t channels) {
    if (!data || blockLength == 0) return;
    if (type == 0u) { // FILTER_DELTA
        const size_t chCount = std::max<size_t>(1u, channels);
        std::vector<uint8_t> tmp(blockLength);
        size_t srcPos = 0;
        for (size_t ch = 0; ch < chCount; ++ch) {
            uint8_t prevByte = 0;
            for (size_t dstPos = ch; dstPos < blockLength && srcPos < blockLength; dstPos += chCount) {
                prevByte = static_cast<uint8_t>(prevByte - data[srcPos++]);
                tmp[dstPos] = prevByte;
            }
        }
        std::memcpy(data, tmp.data(), blockLength);
    } else if (type == 1u || type == 2u) { // FILTER_E8 / FILTER_E8E9
        if (blockLength < 5u) return;
        constexpr uint32_t kFileSize = 0x1000000u;
        const uint8_t cmpByte2 = (type == 2u) ? 0xE9u : 0xE8u;
        for (size_t curPos = 0; curPos + 4u < blockLength; ) {
            const uint8_t curByte = data[curPos++];
            if (curByte == 0xE8u || curByte == cmpByte2) {
                const uint32_t offset = static_cast<uint32_t>((curPos + fileOffset) % kFileSize);
                const uint32_t addr   = ByteIO::RdU32LE(data + curPos);
                if ((addr & 0x80000000u) != 0u) {
                    if (((addr + offset) & 0x80000000u) == 0u) {
                        const uint32_t patched = addr + kFileSize;
                        data[curPos + 0] = static_cast<uint8_t>(patched);
                        data[curPos + 1] = static_cast<uint8_t>(patched >> 8u);
                        data[curPos + 2] = static_cast<uint8_t>(patched >> 16u);
                        data[curPos + 3] = static_cast<uint8_t>(patched >> 24u);
                    }
                } else {
                    if (((addr - kFileSize) & 0x80000000u) != 0u) {
                        const uint32_t patched = addr - offset;
                        data[curPos + 0] = static_cast<uint8_t>(patched);
                        data[curPos + 1] = static_cast<uint8_t>(patched >> 8u);
                        data[curPos + 2] = static_cast<uint8_t>(patched >> 16u);
                        data[curPos + 3] = static_cast<uint8_t>(patched >> 24u);
                    }
                }
                curPos += 4u;
            }
        }
    } else if (type == 3u) { // FILTER_ARM
        for (size_t curPos = 0; curPos + 3u < blockLength; curPos += 4u) {
            if (data[curPos + 3u] == 0xEBu) {
                uint32_t offset = static_cast<uint32_t>(data[curPos]) |
                                  (static_cast<uint32_t>(data[curPos + 1u]) << 8u) |
                                  (static_cast<uint32_t>(data[curPos + 2u]) << 16u);
                offset -= static_cast<uint32_t>((fileOffset + curPos) / 4u);
                data[curPos + 0u] = static_cast<uint8_t>(offset);
                data[curPos + 1u] = static_cast<uint8_t>(offset >> 8u);
                data[curPos + 2u] = static_cast<uint8_t>(offset >> 16u);
            }
        }
    }
}

struct Rar5UnpackState {
    std::vector<uint8_t> window; // Ring buffer of pre-filter LZ77 bytes
    size_t winMask = 0;
    size_t winPos = 0;
    std::array<size_t, 4> oldDist = { 1u, 1u, 1u, 1u };
    uint32_t lastLength = 0;
    Rar5HuffTable LD;
    Rar5HuffTable DD;
    Rar5HuffTable LDD;
    Rar5HuffTable RD;
    bool tablesValid = false;

    void ResetForNewSolidChain(size_t dictBytes) {
        size_t pow2 = 128u * 1024u;
        while (pow2 < dictBytes && pow2 < 128u * 1024u * 1024u) pow2 <<= 1u;
        window.assign(pow2, 0u);
        winMask = pow2 - 1u;
        winPos = 0;
        oldDist = { 1u, 1u, 1u, 1u };
        lastLength = 0;
        tablesValid = false;
    }

    void EnsureDictCapacity(size_t dictBytes) {
        size_t pow2 = 128u * 1024u;
        while (pow2 < dictBytes && pow2 < 128u * 1024u * 1024u) pow2 <<= 1u;
        if (window.size() >= pow2) return;
        std::vector<uint8_t> grown(pow2, 0u);
        if (!window.empty()) {
            // Preserve existing linear history up to winPos when growing solid dictionary
            const size_t copyCount = std::min<size_t>(winPos, window.size());
            for (size_t k = 0; k < copyCount; ++k) {
                grown[(winPos - 1u - k) & (pow2 - 1u)] = window[(winPos - 1u - k) & winMask];
            }
        }
        window = std::move(grown);
        winMask = pow2 - 1u;
    }
};

inline uint32_t Rar5SlotToLength(Rar5BitReader& br, uint32_t slot) {
    uint32_t lBits = 0;
    uint32_t length = 0;
    if (slot < 8u) {
        lBits = 0u;
        length = 2u + slot;
    } else {
        lBits = slot / 4u - 1u;
        length = 2u + ((4u | (slot & 3u)) << lBits);
    }
    if (lBits > 0u) {
        length += br.GetBits(static_cast<int>(lBits));
    }
    return length;
}

inline uint32_t ReadRar5FilterData(Rar5BitReader& br) {
    const uint32_t byteCount = br.GetBits(2) + 1u;
    uint32_t val = 0;
    for (uint32_t i = 0; i < byteCount; ++i) {
        val += br.GetBits(8) << (i * 8u);
    }
    return val;
}

inline bool ReadRar5HuffmanTables(Rar5BitReader& br, uint8_t algVer, Rar5UnpackState& st) {
    uint8_t bitLength[20] = {};
    for (int i = 0; i < 20; ) {
        const uint32_t len = br.GetBits(4);
        if (len == 15u) {
            uint32_t zc = br.GetBits(4);
            if (zc == 0u) {
                bitLength[i++] = 15u;
            } else {
                zc += 2u;
                while (zc-- > 0u && i < 20) bitLength[i++] = 0u;
            }
        } else {
            bitLength[i++] = static_cast<uint8_t>(len);
        }
    }

    Rar5HuffTable BD;
    if (!BD.Build(bitLength, 20u)) return false;

    const size_t dcCount = (algVer >= 1u) ? 80u : 64u;
    const size_t totalSyms = 306u + dcCount + 16u + 44u;
    std::vector<uint8_t> table(totalSyms, 0u);

    for (size_t i = 0; i < totalSyms; ) {
        if (br.inAddr > br.size) return false;
        const uint32_t num = BD.Decode(br);
        if (num < 16u) {
            table[i++] = static_cast<uint8_t>(num);
        } else if (num < 18u) {
            uint32_t rep = (num == 16u) ? (br.GetBits(3) + 3u) : (br.GetBits(7) + 11u);
            const uint8_t prev = (i > 0u) ? table[i - 1u] : 0u;
            while (rep-- > 0u && i < totalSyms) table[i++] = prev;
        } else {
            uint32_t zc = (num == 18u) ? (br.GetBits(3) + 3u) : (br.GetBits(7) + 11u);
            while (zc-- > 0u && i < totalSyms) table[i++] = 0u;
        }
    }

    if (!st.LD.Build(table.data(), 306u)) return false;
    if (!st.DD.Build(table.data() + 306u, dcCount)) return false;
    if (!st.LDD.Build(table.data() + 306u + dcCount, 16u)) return false;
    if (!st.RD.Build(table.data() + 306u + dcCount + 16u, 44u)) return false;
    st.tablesValid = true;
    return true;
}

inline bool DecompressRar5FileStream(
    const uint8_t* compData,
    size_t compLen,
    const Rar5ParsedEntry& ent,
    Rar5UnpackState& st,
    std::vector<uint8_t>* outFileBytes,
    size_t maxOutBytes = 128u * 1024u * 1024u)
{
    if (ent.isEncrypted || ent.isSplitBefore || ent.isSplitAfter || ent.algVer > 1u) return false;
    if (ent.unpSize > maxOutBytes) return false;
    if (outFileBytes) {
        outFileBytes->clear();
        outFileBytes->reserve(static_cast<size_t>(ent.unpSize));
    }
    if (ent.unpSize == 0u && compLen == 0u) return true;
    if (!compData || compLen < 3u) return false;

    std::vector<uint8_t> rawOut;
    std::vector<uint8_t>& fileBuf = outFileBytes ? *outFileBytes : rawOut;
    if (!outFileBytes) fileBuf.reserve(static_cast<size_t>(ent.unpSize));

    std::vector<Rar5PendingFilter> filters;
    const size_t targetUnp = static_cast<size_t>(ent.unpSize);

    Rar5BitReader br{ compData, compLen, 0u, 0 };
    bool lastBlock = false;
    size_t blockGuard = 0;

    while (!lastBlock && fileBuf.size() < targetUnp && blockGuard++ < 262144u) {
        br.AlignToByte();
        if (br.inAddr + 2u > compLen) return false;

        const uint8_t blockFlags = compData[br.inAddr++];
        const uint32_t byteCount = ((blockFlags >> 3u) & 3u) + 1u;
        if (byteCount == 4u || br.inAddr + 1u + byteCount > compLen) return false;

        const uint8_t savedCheck = compData[br.inAddr++];
        uint32_t blockSize = 0;
        for (uint32_t i = 0; i < byteCount; ++i) {
            blockSize |= static_cast<uint32_t>(compData[br.inAddr++]) << (i * 8u);
        }
        const uint8_t calcCheck = static_cast<uint8_t>(
            0x5Au ^ blockFlags ^ blockSize ^ (blockSize >> 8u) ^ (blockSize >> 16u));
        if (calcCheck != savedCheck) return false;

        const int blockBitSize   = static_cast<int>(blockFlags & 7u) + 1;
        const bool tablePresent  = (blockFlags & 0x80u) != 0u;
        lastBlock                = (blockFlags & 0x40u) != 0u;

        const size_t blockStart = br.inAddr;
        if (blockSize > compLen - blockStart) return false;
        if (blockSize == 0u) break;
        const size_t blockLastByte = blockStart + static_cast<size_t>(blockSize) - 1u;

        // Limit bit reader peeking strictly inside this block so trailing bytes cannot corrupt checksum/decode
        Rar5BitReader blockBr{ compData, blockStart + static_cast<size_t>(blockSize), blockStart, 0 };
        if (tablePresent) {
            if (!ReadRar5HuffmanTables(blockBr, ent.algVer, st)) return false;
        }
        if (!st.tablesValid) return false;

        auto blockExhausted = [&]() noexcept -> bool {
            return blockBr.inAddr > blockLastByte ||
                   (blockBr.inAddr == blockLastByte && blockBr.inBit >= blockBitSize);
        };

        auto emitLiteral = [&](uint8_t b) {
            st.window[st.winPos & st.winMask] = b;
            ++st.winPos;
            if (fileBuf.size() < targetUnp) {
                fileBuf.push_back(b);
            }
        };

        auto copyMatch = [&](uint32_t length, size_t distance) -> bool {
            if (distance == 0u || distance > st.window.size()) return false;
            for (uint32_t k = 0; k < length; ++k) {
                const uint8_t b = st.window[(st.winPos - distance) & st.winMask];
                st.window[st.winPos & st.winMask] = b;
                ++st.winPos;
                if (fileBuf.size() < targetUnp) {
                    fileBuf.push_back(b);
                }
            }
            return true;
        };

        while (!blockExhausted() && fileBuf.size() < targetUnp) {
            const uint32_t sym = st.LD.Decode(blockBr);
            if (sym < 256u) {
                emitLiteral(static_cast<uint8_t>(sym));
            } else if (sym >= 262u) {
                uint32_t length = Rar5SlotToLength(blockBr, sym - 262u);
                const uint32_t dSlot = st.DD.Decode(blockBr);
                size_t distance = 1u;
                if (dSlot < 4u) {
                    distance += dSlot;
                } else {
                    const uint32_t dBits = dSlot / 2u - 1u;
                    distance += (static_cast<size_t>(2u | (dSlot & 1u)) << dBits);
                    if (dBits >= 4u) {
                        if (dBits > 4u) {
                            distance += static_cast<size_t>(blockBr.GetBits(static_cast<int>(dBits - 4u))) << 4u;
                        }
                        distance += st.LDD.Decode(blockBr);
                    } else {
                        distance += blockBr.GetBits(static_cast<int>(dBits));
                    }
                }

                st.oldDist[3] = st.oldDist[2];
                st.oldDist[2] = st.oldDist[1];
                st.oldDist[1] = st.oldDist[0];
                st.oldDist[0] = distance;

                if (distance > 0x100u) {
                    ++length;
                    if (distance > 0x2000u) {
                        ++length;
                        if (distance > 0x40000u) {
                            ++length;
                        }
                    }
                }
                st.lastLength = length;
                if (!copyMatch(length, distance)) return false;
            } else if (sym == 256u) {
                if (filters.size() >= 8192u) return false;
                const uint32_t relStart = ReadRar5FilterData(blockBr);
                const uint32_t blkLen   = ReadRar5FilterData(blockBr);
                const uint8_t  fType    = static_cast<uint8_t>(blockBr.GetBits(3));
                const uint8_t  chans    = (fType == 0u) ? static_cast<uint8_t>(blockBr.GetBits(5) + 1u) : 1u;
                if (blkLen > 0u && blkLen <= 64u * 1024u * 1024u && fType <= 3u && relStart <= targetUnp - fileBuf.size()) {
                    filters.push_back(Rar5PendingFilter{
                        fileBuf.size() + static_cast<size_t>(relStart),
                        static_cast<size_t>(blkLen),
                        fType,
                        chans
                    });
                }
            } else if (sym == 257u) {
                if (st.lastLength != 0u) {
                    if (!copyMatch(st.lastLength, st.oldDist[0])) return false;
                }
            } else { // 258..261: repeat oldDist[0..3] with RD length
                const size_t idx = sym - 258u;
                const size_t distance = st.oldDist[idx];
                for (size_t k = idx; k > 0u; --k) st.oldDist[k] = st.oldDist[k - 1u];
                st.oldDist[0] = distance;
                const uint32_t length = Rar5SlotToLength(blockBr, st.RD.Decode(blockBr));
                st.lastLength = length;
                if (!copyMatch(length, distance)) return false;
            }
        }

        br.inAddr = blockStart + static_cast<size_t>(blockSize);
        br.inBit  = 0;
    }

    if (fileBuf.size() != targetUnp) return false;

    if (outFileBytes && !filters.empty()) {
        for (const auto& flt : filters) {
            if (flt.blockStart < outFileBytes->size() && flt.blockLength <= outFileBytes->size() - flt.blockStart) {
                ApplyRar5FilterInPlace(
                    outFileBytes->data() + flt.blockStart,
                    flt.blockLength,
                    flt.blockStart,
                    flt.type,
                    flt.channels);
            }
        }
    }

    if (outFileBytes && ent.hasDataCrc) {
        if (Checksum::Crc32(outFileBytes->data(), outFileBytes->size()) != ent.dataCrc) {
            outFileBytes->clear();
            return false;
        }
    }
    return true;
}

} // namespace DetailRar5

inline bool ParseRar5Headers(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    std::vector<DetailRar5::Rar5ParsedEntry> entries;
    if (!DetailRar5::EnumerateRar5Entries(data, size, entries) || entries.empty()) return false;

    outInfo.formatName = L"RAR5";
    outInfo.entries.reserve(entries.size());
    for (const auto& ent : entries) {
        ArchiveItemEntry item;
        item.pathInArchive     = ent.pathInArchive;
        item.name              = ArchivePath::BaseName(ent.pathInArchive);
        item.uncompressedSize  = ent.unpSize;
        item.compressedSize    = ent.packSize;
        item.crc32             = ent.dataCrc;
        item.dosDate           = ent.dosDate;
        item.dosTime           = ent.dosTime;
        item.isDirectory       = ent.isDirectory;
        item.rawDataOffset     = ent.rawDataOffset;
        item.compressionMethod = ent.isEncrypted ? 99u : ent.method;
        outInfo.entries.push_back(std::move(item));
    }

    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractRar5EntryInProcess(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    std::vector<DetailRar5::Rar5ParsedEntry> entries;
    if (!DetailRar5::EnumerateRar5Entries(data, size, entries)) return false;

    const std::wstring want = ArchivePath::Normalize(normTarget);
    size_t hitIdx = SIZE_MAX;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].isDirectory && _wcsicmp(entries[i].pathInArchive.c_str(), want.c_str()) == 0) {
            hitIdx = i;
            break;
        }
    }
    if (hitIdx == SIZE_MAX) return false;

    const auto& hit = entries[hitIdx];
    if (hit.isEncrypted || hit.isSplitBefore || hit.isSplitAfter) return false;
    if (hit.unpSize == 0u && hit.packSize == 0u) {
        outBytes.clear();
        return true;
    }
    if (hit.rawDataOffset > size || hit.packSize > size - hit.rawDataOffset) return false;

    // Stored method (0)
    if (hit.method == 0u) {
        if (hit.unpSize > size - hit.rawDataOffset) return false;
        const uint8_t* p = data + static_cast<size_t>(hit.rawDataOffset);
        outBytes.assign(p, p + static_cast<size_t>(hit.unpSize));
        if (hit.hasDataCrc && Checksum::Crc32(outBytes.data(), outBytes.size()) != hit.dataCrc) {
            outBytes.clear();
            return false;
        }
        return true;
    }

    // Compressed RAR5 v5 LZ+Huffman stream (handle solid chains by walking backward to last non-solid compressed file)
    size_t startIdx = hitIdx;
    size_t maxDict  = hit.dictSize;
    while (startIdx > 0u && entries[startIdx].isSolid) {
        size_t prev = startIdx - 1u;
        while (prev > 0u && (entries[prev].isDirectory || entries[prev].method == 0u)) {
            --prev;
        }
        if (entries[prev].isDirectory || entries[prev].method == 0u) break;
        startIdx = prev;
        maxDict = std::max<size_t>(maxDict, entries[startIdx].dictSize);
    }

    DetailRar5::Rar5UnpackState state;
    state.ResetForNewSolidChain(maxDict);

    for (size_t i = startIdx; i <= hitIdx; ++i) {
        const auto& cur = entries[i];
        if (cur.isDirectory || cur.method == 0u) continue;
        if (cur.rawDataOffset > size || cur.packSize > size - cur.rawDataOffset) return false;
        if (!cur.isSolid && i > startIdx) {
            state.ResetForNewSolidChain(cur.dictSize);
        } else {
            state.EnsureDictCapacity(cur.dictSize);
        }
        std::vector<uint8_t>* dst = (i == hitIdx) ? &outBytes : nullptr;
        if (!DetailRar5::DecompressRar5FileStream(
                data + static_cast<size_t>(cur.rawDataOffset),
                static_cast<size_t>(cur.packSize),
                cur,
                state,
                dst))
        {
            outBytes.clear();
            return false;
        }
    }
    return true;
}

} // namespace Explorer2::Formats
