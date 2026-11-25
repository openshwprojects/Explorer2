#pragma once
// OutlookPst.h — Zero-dependency C++20 Microsoft Outlook Personal & Offline Folders (.pst / .ost, [MS-PST] NDB + LTP) reader.

namespace Formats {

namespace DetailPst {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdI16LE;
using ::Explorer2::ByteIO::RdI16BE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdI32LE;
using ::Explorer2::ByteIO::RdI32BE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU64BE;
using ::Explorer2::ByteIO::RdI64LE;
using ::Explorer2::ByteIO::RdI64BE;
using ::Explorer2::ByteIO::RdF32BE;
using ::Explorer2::ByteIO::RdF64LE;
using ::Explorer2::ByteIO::RdF64BE;


using ::Explorer2::ReaderText::DecodeUtf16Span;
using ::Explorer2::ReaderText::CivilFromDays1970;
using ::Explorer2::ReaderText::FormatUnixUtc;

// [MS-PST] Section 5.1.1: Exact 256-byte mpbbR encryption permutation table
inline constexpr std::array<uint8_t, 256> kPstMpbbR = {
    0x47, 0xF1, 0xB4, 0xE6, 0x0B, 0x6A, 0x72, 0x48, 0x85, 0x4E, 0x9E, 0xEB, 0xE2, 0xF8, 0x94, 0x53,
    0xE0, 0xBB, 0xA0, 0x02, 0xE8, 0x5A, 0x09, 0xAB, 0xDB, 0xE3, 0xBA, 0xC6, 0x7C, 0xC3, 0x10, 0xDD,
    0x39, 0x05, 0x96, 0x30, 0xF5, 0x37, 0x60, 0x82, 0x8C, 0xC9, 0x13, 0x4A, 0x6B, 0x1D, 0xF3, 0xFB,
    0x8F, 0x26, 0x97, 0xCA, 0x91, 0x17, 0x01, 0xC4, 0x32, 0x2D, 0x6E, 0x31, 0x95, 0xFF, 0xD9, 0x23,
    0xD1, 0x00, 0x5E, 0x79, 0xDC, 0x44, 0x3B, 0x1A, 0x28, 0xC5, 0x61, 0x57, 0x20, 0x90, 0x3D, 0x83,
    0xB9, 0x43, 0xBE, 0x67, 0xD2, 0x46, 0x42, 0x76, 0xC0, 0x6D, 0x5B, 0x7E, 0xB2, 0x0F, 0x16, 0x29,
    0x3C, 0xA9, 0x03, 0x54, 0x0D, 0xDA, 0x5D, 0xDF, 0xF6, 0xB7, 0xC7, 0x62, 0xCD, 0x8D, 0x06, 0xD3,
    0x69, 0x5C, 0x86, 0xD6, 0x14, 0xF7, 0xA5, 0x66, 0x75, 0xAC, 0xB1, 0xE9, 0x45, 0x21, 0x70, 0x0C,
    0x87, 0x9F, 0x74, 0xA4, 0x22, 0x4C, 0x6F, 0xBF, 0x1F, 0x56, 0xAA, 0x2E, 0xB3, 0x78, 0x33, 0x50,
    0xB0, 0xA3, 0x92, 0xBC, 0xCF, 0x19, 0x1C, 0xA7, 0x63, 0xCB, 0x1E, 0x4D, 0x3E, 0x4B, 0x1B, 0x9B,
    0x4F, 0xE7, 0xF0, 0xEE, 0xAD, 0x3A, 0xB5, 0x59, 0x04, 0xEA, 0x40, 0x55, 0x25, 0x51, 0xE5, 0x7A,
    0x89, 0x38, 0x68, 0x52, 0x7B, 0xFC, 0x27, 0xAE, 0xD7, 0xBD, 0xFA, 0x07, 0xF4, 0xCC, 0x8E, 0x5F,
    0xEF, 0x35, 0x9C, 0x84, 0x2B, 0x15, 0xD5, 0x77, 0x34, 0x49, 0xB6, 0x12, 0x0A, 0x7F, 0x71, 0x88,
    0xFD, 0x9D, 0x18, 0x41, 0x7D, 0x93, 0xD8, 0x58, 0x2C, 0xCE, 0xFE, 0x24, 0xAF, 0xDE, 0xB8, 0x36,
    0xC8, 0xA1, 0x80, 0xA6, 0x99, 0x98, 0xA8, 0x2F, 0x0E, 0x81, 0x65, 0x73, 0xE4, 0xC2, 0xA2, 0x8A,
    0xD4, 0xE1, 0x11, 0xD0, 0x08, 0x8B, 0x2A, 0xF2, 0xED, 0x9A, 0x64, 0x3F, 0xC1, 0x6C, 0xF9, 0xEC
};

// [MS-PST] Section 5.1.1: Exact 256-byte mpbbS involution table (for bCryptMethod == 2 Cyclic Encryption)
inline constexpr std::array<uint8_t, 256> kPstMpbbS = {
    0x14, 0x53, 0x0F, 0x56, 0xB3, 0xC8, 0x7A, 0x9C, 0xEB, 0x65, 0x48, 0x17, 0x16, 0x15, 0x9F, 0x02,
    0xCC, 0x54, 0x7C, 0x83, 0x00, 0x0D, 0x0C, 0x0B, 0xA2, 0x62, 0xA8, 0x76, 0xDB, 0xD9, 0xED, 0xC7,
    0xC5, 0xA4, 0xDC, 0xAC, 0x85, 0x74, 0xD6, 0xD0, 0xA7, 0x9B, 0xAE, 0x9A, 0x96, 0x71, 0x66, 0xC3,
    0x63, 0x99, 0xB8, 0xDD, 0x73, 0x92, 0x8E, 0x84, 0x7D, 0xA5, 0x5E, 0xD1, 0x5D, 0x93, 0xB1, 0x57,
    0x51, 0x50, 0x80, 0x89, 0x52, 0x94, 0x4F, 0x4E, 0x0A, 0x6B, 0xBC, 0x8D, 0x7F, 0x6E, 0x47, 0x46,
    0x41, 0x40, 0x44, 0x01, 0x11, 0xCB, 0x03, 0x3F, 0xF7, 0xF4, 0xE1, 0xA9, 0x8F, 0x3C, 0x3A, 0xF9,
    0xFB, 0xF0, 0x19, 0x30, 0x82, 0x09, 0x2E, 0xC9, 0x9D, 0xA0, 0x86, 0x49, 0xEE, 0x6F, 0x4D, 0x6D,
    0xEF, 0x2D, 0xA3, 0x34, 0x25, 0x87, 0x1B, 0x88, 0xAA, 0xFC, 0x06, 0xA6, 0x12, 0x38, 0xFD, 0x4C,
    0x42, 0xB0, 0x64, 0x13, 0x37, 0x24, 0x6A, 0x75, 0x77, 0x43, 0xFF, 0xE6, 0xB4, 0x4B, 0x36, 0x5C,
    0xCE, 0xE3, 0x35, 0x3D, 0x45, 0xB9, 0x2C, 0xEC, 0xB7, 0x31, 0x2B, 0x29, 0x07, 0x68, 0xA1, 0x0E,
    0x69, 0x9E, 0x18, 0x72, 0x21, 0x39, 0x7B, 0x28, 0x1A, 0x5B, 0x78, 0xF5, 0x23, 0xCA, 0x2A, 0xB2,
    0x81, 0x3E, 0xAF, 0x04, 0x8C, 0xC1, 0xE7, 0x98, 0x32, 0x95, 0xDA, 0xE0, 0x4A, 0xE8, 0xFE, 0xEA,
    0xE5, 0xB5, 0xD5, 0x2F, 0xF8, 0x20, 0xF6, 0x1F, 0x05, 0x67, 0xAD, 0x55, 0x10, 0xE9, 0x90, 0xFA,
    0x27, 0x3B, 0xE2, 0xF3, 0xE4, 0xC2, 0x26, 0xF1, 0xF2, 0x1D, 0xBA, 0x1C, 0x22, 0x33, 0xDF, 0xDE,
    0xBB, 0x5A, 0xD2, 0x91, 0xD4, 0xC0, 0x8B, 0xB6, 0xBD, 0xCD, 0xBF, 0x08, 0x97, 0x1E, 0x6C, 0x70,
    0x61, 0xD7, 0xD8, 0xD3, 0x59, 0xAB, 0xC6, 0x58, 0xC4, 0x5F, 0xCF, 0x60, 0x79, 0x7E, 0xBE, 0x8A
};

constexpr bool VerifyPstBijectionAndInvolution() {
    bool seenR[256] = {};
    for (size_t i = 0; i < 256; ++i) {
        if (seenR[kPstMpbbR[i]]) return false;
        seenR[kPstMpbbR[i]] = true;
        if (kPstMpbbS[kPstMpbbS[i]] != i) return false;
    }
    return true;
}
static_assert(VerifyPstBijectionAndInvolution(), "[MS-PST] mpbbR must be a bijection and mpbbS an involution");

constexpr std::array<uint8_t, 256> BuildPstMpbbI() {
    std::array<uint8_t, 256> inv{};
    for (size_t i = 0; i < 256; ++i) inv[kPstMpbbR[i]] = static_cast<uint8_t>(i);
    return inv;
}
inline constexpr std::array<uint8_t, 256> kPstMpbbI = BuildPstMpbbI();

// [MS-PST] Section 5.3: Reflected CRC-32 with initial/final XOR == 0 (NOT ~0)
inline uint32_t ComputePstCrc(uint32_t crc, const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        uint32_t c = (crc ^ p[i]) & 0xFFu;
        for (int b = 0; b < 8; ++b) c = (c & 1u) ? ((c >> 1) ^ 0xEDB88320u) : (c >> 1);
        crc = (crc >> 8) ^ c;
    }
    return crc;
}

inline void DecryptPstBlock(uint8_t* data, size_t len, uint8_t cryptMethod, uint64_t bid) {
    if (cryptMethod == 1u) { // NDB_CRYPT_PERMUTE
        for (size_t i = 0; i < len; ++i) data[i] = kPstMpbbI[data[i]];
    } else if (cryptMethod == 2u) { // NDB_CRYPT_CYCLIC
        uint8_t bidBytes[4] = {
            static_cast<uint8_t>(bid & 0xFFu),
            static_cast<uint8_t>((bid >> 8) & 0xFFu),
            static_cast<uint8_t>((bid >> 16) & 0xFFu),
            static_cast<uint8_t>((bid >> 24) & 0xFFu)
        };
        uint32_t dwKey = ComputePstCrc(0, bidBytes, 4);
        uint16_t w = static_cast<uint16_t>(dwKey ^ (dwKey >> 16));
        for (size_t i = 0; i < len; ++i) {
            uint8_t b = data[i];
            b = static_cast<uint8_t>(b + static_cast<uint8_t>(w & 0xFFu));
            b = kPstMpbbR[b];
            b = static_cast<uint8_t>(b + static_cast<uint8_t>(w >> 8));
            b = kPstMpbbS[b];
            b = static_cast<uint8_t>(b - static_cast<uint8_t>(w >> 8));
            b = kPstMpbbI[b];
            b = static_cast<uint8_t>(b - static_cast<uint8_t>(w & 0xFFu));
            data[i] = b;
            ++w;
        }
    }
}

struct PstBbtEntry {
    uint64_t ib = 0;
    uint16_t cb = 0;
};

struct PstNbtEntry {
    uint64_t nid = 0;
    uint64_t bidData = 0;
    uint64_t bidSub = 0;
    uint32_t nidParent = 0;
};

struct PstSubEntry {
    uint64_t nid = 0;
    uint64_t bidData = 0;
    uint64_t bidSub = 0;
};

struct PstContext {
    SourceBytes& src;
    bool isUnicode = true;
    bool isVer36 = false;
    uint8_t cryptMethod = 0;
    size_t pageSize = 512;
    std::unordered_map<uint64_t, PstBbtEntry> bbt;
    std::vector<PstNbtEntry> nbt;

    explicit PstContext(SourceBytes& s) : src(s) {}

    bool ParsePageMeta(const uint8_t* pg, uint16_t& outCEnt, uint8_t& outCbEnt, uint8_t& outLevel, uint8_t& outPtype) const {
        if (isVer36) { // OST 2013+ (4096B page)
            outCEnt  = DetailPst::RdU16LE(pg + 4056);
            outCbEnt = pg[4060];
            outLevel = pg[4061];
            outPtype = pg[4072];
            return outCbEnt > 0 && static_cast<size_t>(outCEnt) * outCbEnt <= 4056u;
        }
        if (isUnicode) { // Unicode v23 (512B page)
            outCEnt  = pg[488];
            outCbEnt = pg[490];
            outLevel = pg[491];
            outPtype = pg[496];
            return outCbEnt > 0 && static_cast<size_t>(outCEnt) * outCbEnt <= 488u;
        }
        // ANSI v14/v15 (512B page)
        outCEnt  = pg[496];
        outCbEnt = pg[498];
        outLevel = pg[499];
        outPtype = pg[500];
        return outCbEnt > 0 && static_cast<size_t>(outCEnt) * outCbEnt <= 496u;
    }

    void WalkBTree(uint64_t pageOff, bool isBbt, int depth, std::set<uint64_t>& visited) {
        if (depth > 16 || pageOff == 0 || pageOff + pageSize > src.Size()) return;
        if (visited.size() > 65536 || !visited.insert(pageOff).second) return;

        std::vector<uint8_t> pgBuf;
        const uint8_t* pg = src.Read(pageOff, pageSize, pgBuf);
        if (!pg) return;

        uint16_t cEnt = 0;
        uint8_t cbEnt = 0, cLevel = 0, ptype = 0;
        if (!ParsePageMeta(pg, cEnt, cbEnt, cLevel, ptype)) return;
        if (ptype != 0x80u && ptype != 0x81u) return;

        if (cLevel > 0) {
            size_t need = isUnicode ? 24u : 12u;
            if (cbEnt < need) return;
            for (uint16_t i = 0; i < cEnt; ++i) {
                const uint8_t* ep = pg + static_cast<size_t>(i) * cbEnt;
                uint64_t childIb = isUnicode ? DetailPst::RdU64LE(ep + 16) : DetailPst::RdU32LE(ep + 8);
                WalkBTree(childIb, isBbt, depth + 1, visited);
            }
            return;
        }

        if (isBbt) {
            size_t need = isUnicode ? 20u : 10u;
            if (cbEnt < need) return;
            for (uint16_t i = 0; i < cEnt && bbt.size() < 250000; ++i) {
                const uint8_t* ep = pg + static_cast<size_t>(i) * cbEnt;
                uint64_t bid = isUnicode ? DetailPst::RdU64LE(ep + 0) : DetailPst::RdU32LE(ep + 0);
                uint64_t ib  = isUnicode ? DetailPst::RdU64LE(ep + 8) : DetailPst::RdU32LE(ep + 4);
                uint16_t cb  = DetailPst::RdU16LE(ep + (isUnicode ? 16 : 8));
                bbt[bid & ~1ULL] = { ib, cb };
            }
        } else {
            size_t need = isUnicode ? 28u : 16u;
            if (cbEnt < need) return;
            for (uint16_t i = 0; i < cEnt && nbt.size() < 250000; ++i) {
                const uint8_t* ep = pg + static_cast<size_t>(i) * cbEnt;
                PstNbtEntry ne;
                if (isUnicode) {
                    ne.nid       = DetailPst::RdU64LE(ep + 0);
                    ne.bidData   = DetailPst::RdU64LE(ep + 8);
                    ne.bidSub    = DetailPst::RdU64LE(ep + 16);
                    ne.nidParent = DetailPst::RdU32LE(ep + 24);
                } else {
                    ne.nid       = DetailPst::RdU32LE(ep + 0);
                    ne.bidData   = DetailPst::RdU32LE(ep + 4);
                    ne.bidSub    = DetailPst::RdU32LE(ep + 8);
                    ne.nidParent = DetailPst::RdU32LE(ep + 12);
                }
                nbt.push_back(ne);
            }
        }
    }

    // Reads a single physical block from BBT; decrypts if it is an external data block ((bid & 2) == 0)
    bool ReadRawBlock(uint64_t bid, std::vector<uint8_t>& outBlk) {
        outBlk.clear();
        if (bid == 0) return false;
        auto it = bbt.find(bid & ~1ULL);
        if (it == bbt.end() || it->second.cb == 0 || it->second.ib + it->second.cb > src.Size()) return false;
        std::vector<uint8_t> tmp;
        const uint8_t* p = src.Read(it->second.ib, it->second.cb, tmp);
        if (!p) return false;
        outBlk.assign(p, p + it->second.cb);
        if ((bid & 2ULL) == 0 && cryptMethod != 0) {
            DecryptPstBlock(outBlk.data(), outBlk.size(), cryptMethod, bid & ~1ULL);
        }
        return true;
    }

    // Resolves a data block OR XBLOCK / XXBLOCK multi-block tree into individual HN/stream blocks
    bool ReadDataBlocks(uint64_t bid, std::vector<std::vector<uint8_t>>& outBlocks, int depth = 0) {
        if (depth == 0) outBlocks.clear();
        if (bid == 0 || depth > 4 || outBlocks.size() >= 2048) return false;

        std::vector<uint8_t> blk;
        if (!ReadRawBlock(bid, blk)) return false;

        if ((bid & 2ULL) == 0) { // External data block
            outBlocks.push_back(std::move(blk));
            return true;
        }
        // Internal XBLOCK (cLevel=1) or XXBLOCK (cLevel=2): btype == 0x01
        if (blk.size() < 8 || blk[0] != 0x01u) return false;
        uint8_t cLevel = blk[1];
        uint16_t cEnt  = DetailPst::RdU16LE(blk.data() + 2);
        size_t bidSz   = isUnicode ? 8u : 4u;
        if (8u + static_cast<size_t>(cEnt) * bidSz > blk.size()) return false;
        for (uint16_t i = 0; i < cEnt && outBlocks.size() < 2048; ++i) {
            uint64_t childBid = isUnicode
                ? DetailPst::RdU64LE(blk.data() + 8 + static_cast<size_t>(i) * 8u)
                : DetailPst::RdU32LE(blk.data() + 8 + static_cast<size_t>(i) * 4u);
            if (cLevel == 1) {
                std::vector<uint8_t> dblk;
                if (ReadRawBlock(childBid, dblk)) outBlocks.push_back(std::move(dblk));
            } else if (cLevel == 2) {
                ReadDataBlocks(childBid, outBlocks, depth + 1);
            }
        }
        return !outBlocks.empty();
    }

    bool ReadConcatenatedStream(uint64_t bid, std::vector<uint8_t>& outBytes, size_t maxBytes = 8u * 1024u * 1024u) {
        outBytes.clear();
        std::vector<std::vector<uint8_t>> blks;
        if (!ReadDataBlocks(bid, blks)) return false;
        for (const auto& b : blks) {
            size_t take = std::min<size_t>(b.size(), maxBytes - outBytes.size());
            outBytes.insert(outBytes.end(), b.begin(), b.begin() + static_cast<std::ptrdiff_t>(take));
            if (outBytes.size() >= maxBytes) break;
        }
        return !outBytes.empty();
    }

    // Walks Subnode B-Tree (SLBLOCK / SIBLOCK, btype == 0x02)
    void WalkSubnodes(uint64_t bidSub, std::vector<PstSubEntry>& outSubs, int depth = 0) {
        if (bidSub == 0 || depth > 6 || outSubs.size() > 4096) return;
        std::vector<uint8_t> blk;
        if (!ReadRawBlock(bidSub, blk) || blk.size() < 8 || blk[0] != 0x02u) return;
        uint8_t cLevel = blk[1];
        uint16_t cEnt  = DetailPst::RdU16LE(blk.data() + 2);
        size_t hdrSz   = isUnicode ? 8u : 4u;
        if (cLevel == 0) { // SLBLOCK
            size_t entSz = isUnicode ? 24u : 12u;
            if (hdrSz + static_cast<size_t>(cEnt) * entSz > blk.size()) return;
            for (uint16_t i = 0; i < cEnt && outSubs.size() < 4096; ++i) {
                const uint8_t* ep = blk.data() + hdrSz + static_cast<size_t>(i) * entSz;
                PstSubEntry se;
                if (isUnicode) {
                    se.nid     = DetailPst::RdU64LE(ep + 0);
                    se.bidData = DetailPst::RdU64LE(ep + 8);
                    se.bidSub  = DetailPst::RdU64LE(ep + 16);
                } else {
                    se.nid     = DetailPst::RdU32LE(ep + 0);
                    se.bidData = DetailPst::RdU32LE(ep + 4);
                    se.bidSub  = DetailPst::RdU32LE(ep + 8);
                }
                outSubs.push_back(se);
            }
        } else { // SIBLOCK
            size_t entSz = isUnicode ? 16u : 8u;
            if (hdrSz + static_cast<size_t>(cEnt) * entSz > blk.size()) return;
            for (uint16_t i = 0; i < cEnt; ++i) {
                const uint8_t* ep = blk.data() + hdrSz + static_cast<size_t>(i) * entSz;
                uint64_t childBid = isUnicode ? DetailPst::RdU64LE(ep + 8) : DetailPst::RdU32LE(ep + 4);
                WalkSubnodes(childBid, outSubs, depth + 1);
            }
        }
    }
};

// LTP Layer: Heap-on-Node (HN), BTree-on-Heap (BTH), Property Context (PC), Table Context (TC)
struct PstHeapOnNode {
    std::vector<std::vector<uint8_t>> blocks;
    std::vector<PstSubEntry> subnodes;
    uint8_t clientSig = 0;
    uint32_t hidUserRoot = 0;

    bool Open(PstContext& ctx, uint64_t bidData, uint64_t bidSub) {
        blocks.clear();
        subnodes.clear();
        if (!ctx.ReadDataBlocks(bidData, blocks) || blocks.empty() || blocks[0].size() < 16) return false;
        if (blocks[0][2] != 0xECu) return false; // bSig == 0xEC (HN)
        clientSig   = blocks[0][3];
        hidUserRoot = DetailPst::RdU32LE(blocks[0].data() + 4);
        if (bidSub != 0) ctx.WalkSubnodes(bidSub, subnodes);
        return true;
    }

    bool GetHidSlice(uint32_t hid, const uint8_t*& outPtr, size_t& outLen) const {
        outPtr = nullptr;
        outLen = 0;
        if (hid == 0 || (hid & 0x1Fu) != 0) return false;
        uint32_t idx1 = (hid >> 5) & 0x7FFu;
        uint32_t blkIdx = hid >> 16;
        if (idx1 == 0 || blkIdx >= blocks.size()) return false;
        const auto& blk = blocks[blkIdx];
        if (blk.size() < 4) return false;
        uint16_t ibHnpm = DetailPst::RdU16LE(blk.data());
        if (ibHnpm + 4u > blk.size()) return false;
        uint16_t cAlloc = DetailPst::RdU16LE(blk.data() + ibHnpm);
        if (idx1 > cAlloc || ibHnpm + 4u + static_cast<size_t>(cAlloc + 1u) * 2u > blk.size()) return false;
        uint16_t off0 = DetailPst::RdU16LE(blk.data() + ibHnpm + 4u + static_cast<size_t>(idx1 - 1u) * 2u);
        uint16_t off1 = DetailPst::RdU16LE(blk.data() + ibHnpm + 4u + static_cast<size_t>(idx1) * 2u);
        if (off1 < off0 || off1 > ibHnpm) return false;
        outPtr = blk.data() + off0;
        outLen = off1 - off0;
        return true;
    }

    bool ResolveHnid(PstContext& ctx, uint32_t hnid, std::vector<uint8_t>& outBytes) const {
        outBytes.clear();
        if (hnid == 0) return false;
        if ((hnid & 0x1Fu) == 0) {
            const uint8_t* p = nullptr; size_t n = 0;
            if (!GetHidSlice(hnid, p, n)) return false;
            outBytes.assign(p, p + n);
            return true;
        }
        for (const auto& sn : subnodes) {
            if ((sn.nid & 0xFFFFFFFFULL) == hnid) {
                return ctx.ReadConcatenatedStream(sn.bidData, outBytes);
            }
        }
        return false;
    }

    void WalkBthLeaves(uint32_t bthHdrHid, std::vector<std::pair<const uint8_t*, size_t>>& outLeaves) const {
        outLeaves.clear();
        const uint8_t* hdr = nullptr; size_t hdrLen = 0;
        if (!GetHidSlice(bthHdrHid, hdr, hdrLen) || hdrLen < 8 || hdr[0] != 0xB5u) return;
        uint8_t cbKey    = hdr[1];
        uint8_t cbEnt    = hdr[2];
        uint8_t nLevels  = hdr[3];
        uint32_t hidRoot = DetailPst::RdU32LE(hdr + 4);
        if (hidRoot == 0 || cbKey == 0 || cbKey > 16 || cbEnt == 0 || cbEnt > 64) return;

        auto descend = [&](auto& self, uint32_t hid, uint8_t lvl, int guard) -> void {
            if (guard > 8 || outLeaves.size() > 8192) return;
            const uint8_t* p = nullptr; size_t n = 0;
            if (!GetHidSlice(hid, p, n) || n == 0) return;
            if (lvl == 0) {
                size_t recSz = static_cast<size_t>(cbKey) + cbEnt;
                for (size_t off = 0; off + recSz <= n && outLeaves.size() < 8192; off += recSz) {
                    outLeaves.emplace_back(p + off, recSz);
                }
            } else {
                size_t idxSz = static_cast<size_t>(cbKey) + 4u;
                for (size_t off = 0; off + idxSz <= n; off += idxSz) {
                    uint32_t childHid = DetailPst::RdU32LE(p + off + cbKey);
                    self(self, childHid, static_cast<uint8_t>(lvl - 1u), guard + 1);
                }
            }
        };
        descend(descend, hidRoot, nLevels, 0);
    }
};

struct PstPropMap {
    struct PropVal { uint16_t type = 0; uint32_t rawHnid = 0; };
    std::unordered_map<uint16_t, PropVal> props;

    bool LoadFromNode(PstContext& ctx, PstHeapOnNode& hn, uint64_t bidData, uint64_t bidSub) {
        props.clear();
        if (!hn.Open(ctx, bidData, bidSub) || hn.clientSig != 0xBCu) return false;
        std::vector<std::pair<const uint8_t*, size_t>> leaves;
        hn.WalkBthLeaves(hn.hidUserRoot, leaves);
        for (const auto& [lp, ln] : leaves) {
            if (ln >= 8) {
                uint16_t pid   = DetailPst::RdU16LE(lp + 0);
                uint16_t ptype = DetailPst::RdU16LE(lp + 2);
                uint32_t val   = DetailPst::RdU32LE(lp + 4);
                props[pid] = { ptype, val };
            }
        }
        return true;
    }

    int32_t GetInt32(uint16_t pid, int32_t defVal = 0) const {
        auto it = props.find(pid);
        if (it == props.end()) return defVal;
        if (it->second.type == 0x0003u) return static_cast<int32_t>(it->second.rawHnid);
        if (it->second.type == 0x0002u) return static_cast<int16_t>(it->second.rawHnid & 0xFFFFu);
        return defVal;
    }

    std::wstring GetString(PstContext& ctx, const PstHeapOnNode& hn, uint16_t pid) const {
        auto it = props.find(pid);
        if (it == props.end() || (it->second.type != 0x001Fu && it->second.type != 0x001Eu)) return {};
        std::vector<uint8_t> bytes;
        if (!hn.ResolveHnid(ctx, it->second.rawHnid, bytes) || bytes.empty()) return {};
        std::wstring s;
        if (it->second.type == 0x001Fu) {
            s = DetailPst::DecodeUtf16Span(bytes.data(), bytes.size() / 2u, false);
        } else {
            size_t n = bytes.size();
            while (n > 0 && bytes[n - 1] == 0) --n;
            s = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(bytes.data()), n));
        }
        // [MS-PST] §2.5.3.1.1.1: PidTagSubject (0x0037) control-char prefix \x0001 + length char
        if (pid == 0x0037u && s.size() >= 2 && s[0] == 0x0001) {
            s.erase(0, 2);
        }
        return PathUtils::Trim(s);
    }

    std::wstring GetFileTimeStr(PstContext& ctx, const PstHeapOnNode& hn, uint16_t pid) const {
        auto it = props.find(pid);
        if (it == props.end() || it->second.type != 0x0040u) return {};
        std::vector<uint8_t> bytes;
        if (!hn.ResolveHnid(ctx, it->second.rawHnid, bytes) || bytes.size() < 8) return {};
        uint64_t ft = DetailPst::RdU64LE(bytes.data());
        if (ft <= 116444736000000000ULL) return {};
        int64_t unixSec = static_cast<int64_t>((ft - 116444736000000000ULL) / 10000000ULL);
        return DetailPst::FormatUnixUtc(unixSec);
    }

    bool GetBinary(PstContext& ctx, const PstHeapOnNode& hn, uint16_t pid, std::vector<uint8_t>& outBin) const {
        outBin.clear();
        auto it = props.find(pid);
        if (it == props.end() || (it->second.type != 0x0102u && it->second.type != 0x001Eu && it->second.type != 0x001Fu)) return false;
        return hn.ResolveHnid(ctx, it->second.rawHnid, outBin) && !outBin.empty();
    }
};

// Table Context (TC, bClientSig == 0x7C) row matrix decoder for Recipient / Attachment tables
inline void ExtractTcStringColumn(PstContext& ctx, uint64_t bidData, uint64_t bidSub,
                                  uint16_t wantPropId1, uint16_t wantPropId2,
                                  std::vector<std::wstring>& outValues) {
    PstHeapOnNode hn;
    if (!hn.Open(ctx, bidData, bidSub) || hn.clientSig != 0x7Cu) return;
    const uint8_t* tcInfo = nullptr; size_t tcLen = 0;
    if (!hn.GetHidSlice(hn.hidUserRoot, tcInfo, tcLen) || tcLen < 22 || tcInfo[0] != 0x7Cu) return;
    uint8_t cCols      = tcInfo[1];
    uint16_t rowStride = DetailPst::RdU16LE(tcInfo + 8); // rgib[TCI_bm] = total row size
    uint32_t hnidRows  = DetailPst::RdU32LE(tcInfo + 14);
    if (rowStride == 0 || 22u + static_cast<size_t>(cCols) * 8u > tcLen) return;

    struct TcCol { uint16_t pid; uint16_t ptype; uint16_t ibData; uint8_t cbData; uint8_t iBit; };
    std::vector<TcCol> cols;
    for (uint8_t c = 0; c < cCols; ++c) {
        const uint8_t* cp = tcInfo + 22u + static_cast<size_t>(c) * 8u;
        uint32_t tag = DetailPst::RdU32LE(cp + 0);
        uint16_t pid = static_cast<uint16_t>(tag >> 16);
        uint16_t ptp = static_cast<uint16_t>(tag & 0xFFFFu);
        if ((pid == wantPropId1 || pid == wantPropId2) && (ptp == 0x001Fu || ptp == 0x001Eu) && cp[6] == 4u) {
            cols.push_back({ pid, ptp, DetailPst::RdU16LE(cp + 4), cp[6], cp[7] });
        }
    }
    if (cols.empty()) return;

    std::vector<uint8_t> rowMatrix;
    if (!hn.ResolveHnid(ctx, hnidRows, rowMatrix) || rowMatrix.size() < rowStride) return;
    uint16_t cebOff = DetailPst::RdU16LE(tcInfo + 6); // rgib[TCI_1b] = start of CEB bitmap

    for (size_t rOff = 0; rOff + rowStride <= rowMatrix.size() && outValues.size() < 64; rOff += rowStride) {
        const uint8_t* row = rowMatrix.data() + rOff;
        for (const auto& col : cols) {
            if (col.ibData + 4u > rowStride) continue;
            size_t cebByte = cebOff + (col.iBit >> 3);
            if (cebByte < rowStride) {
                uint8_t mask = static_cast<uint8_t>(0x80u >> (col.iBit & 7u));
                if ((row[cebByte] & mask) == 0) continue;
            }
            uint32_t valHnid = DetailPst::RdU32LE(row + col.ibData);
            std::vector<uint8_t> sbytes;
            if (hn.ResolveHnid(ctx, valHnid, sbytes) && !sbytes.empty()) {
                std::wstring sv = (col.ptype == 0x001Fu)
                    ? DetailPst::DecodeUtf16Span(sbytes.data(), sbytes.size() / 2u, false)
                    : PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(sbytes.data()), sbytes.size()));
                sv = PathUtils::Trim(sv);
                if (!sv.empty()) {
                    outValues.push_back(std::move(sv));
                    break;
                }
            }
        }
    }
}

} // namespace DetailPst

inline bool ParseOutlookPstOst(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (raw.size() < 512 || DetailPst::RdU32LE(raw.data()) != 0x4E444221u) { // "!BDN"
        return false;
    }

    const uint16_t wVer = DetailPst::RdU16LE(raw.data() + 10);
    const bool isAnsi   = (wVer == 14u || wVer == 15u);
    const bool isUni    = (wVer >= 23u);
    if (!isAnsi && !isUni) return false;
    if (isUni && raw.size() < 528) return false;

    SourceBytes src(raw, out);
    DetailPst::PstContext ctx(src);
    ctx.isUnicode   = isUni;
    ctx.isVer36     = (wVer >= 36u);
    ctx.pageSize    = ctx.isVer36 ? 4096u : 512u;
    ctx.cryptMethod = isUni ? raw[513] : raw[461];

    uint64_t nbtRootOff = isUni ? DetailPst::RdU64LE(raw.data() + 224) : DetailPst::RdU32LE(raw.data() + 188);
    uint64_t bbtRootOff = isUni ? DetailPst::RdU64LE(raw.data() + 240) : DetailPst::RdU32LE(raw.data() + 196);
    if (nbtRootOff == 0 || bbtRootOff == 0 ||
        nbtRootOff + ctx.pageSize > src.Size() || bbtRootOff + ctx.pageSize > src.Size()) {
        return false;
    }

    std::set<uint64_t> visBbt, visNbt;
    ctx.WalkBTree(bbtRootOff, true,  0, visBbt);
    ctx.WalkBTree(nbtRootOff, false, 0, visNbt);
    if (ctx.bbt.empty() || ctx.nbt.empty()) return false;

    // 1. Reconstruct Folder Tree from NID_TYPE_NORMAL_FOLDER (0x02) & NID_TYPE_SEARCH_FOLDER (0x03)
    struct FolderInfo {
        uint32_t nid32 = 0;
        uint32_t parent32 = 0;
        std::wstring name;
        int32_t itemCount = 0;
        int32_t unreadCount = 0;
    };
    std::unordered_map<uint32_t, FolderInfo> folders;
    std::vector<uint32_t> folderOrder;

    for (const auto& ne : ctx.nbt) {
        uint8_t ntype = static_cast<uint8_t>(ne.nid & 0x1Fu);
        if (ntype == 0x02u || ntype == 0x03u) {
            DetailPst::PstHeapOnNode hn;
            DetailPst::PstPropMap pc;
            FolderInfo fi;
            fi.nid32    = static_cast<uint32_t>(ne.nid);
            fi.parent32 = ne.nidParent;
            if (pc.LoadFromNode(ctx, hn, ne.bidData, ne.bidSub)) {
                fi.name        = pc.GetString(ctx, hn, 0x3001u); // PidTagDisplayName
                fi.itemCount   = pc.GetInt32(0x3602u, 0);        // PidTagContentCount
                fi.unreadCount = pc.GetInt32(0x3603u, 0);        // PidTagContentUnreadCount
            }
            if (fi.name.empty()) {
                fi.name = (fi.nid32 == 0x0122u) ? L"Root" : (L"Folder_" + std::to_wstring(fi.nid32));
            }
            folders[fi.nid32] = fi;
            folderOrder.push_back(fi.nid32);
        }
    }

    auto buildFolderPath = [&](uint32_t fnid) -> std::wstring {
        std::vector<std::wstring> parts;
        uint32_t cur = fnid;
        for (int guard = 0; guard < 16 && cur != 0; ++guard) {
            auto it = folders.find(cur);
            if (it == folders.end()) break;
            parts.push_back(it->second.name);
            if (it->second.parent32 == cur) break;
            cur = it->second.parent32;
        }
        std::wstring path;
        for (size_t i = parts.size(); i > 0; --i) {
            if (!path.empty()) path += L" / ";
            path += parts[i - 1];
        }
        return path.empty() ? L"(Root)" : path;
    };

    SpreadsheetSheet fldSh;
    fldSh.name = L"PST_Folders";
    fldSh.rows.push_back({ L"Folder Path", L"Node ID", L"Items", L"Unread" });
    for (uint32_t fid : folderOrder) {
        const auto& f = folders[fid];
        wchar_t nidHex[24] = {};
        swprintf_s(nidHex, L"0x%X", f.nid32);
        fldSh.rows.push_back({
            buildFolderPath(fid),
            nidHex,
            std::to_wstring(f.itemCount),
            std::to_wstring(f.unreadCount)
        });
    }

    // 2. Decode Messages (NID_TYPE_NORMAL_MESSAGE == 0x04)
    SpreadsheetSheet msgSh;
    msgSh.name = L"PST_Messages";
    msgSh.rows.push_back({ L"Folder", L"Date", L"From", L"To", L"Subject", L"Body Preview", L"Attachments" });

    size_t msgCount = 0;
    for (const auto& ne : ctx.nbt) {
        if ((ne.nid & 0x1Fu) != 0x04u) continue;
        if (msgCount >= 1000) break;

        DetailPst::PstHeapOnNode hn;
        DetailPst::PstPropMap pc;
        if (!pc.LoadFromNode(ctx, hn, ne.bidData, ne.bidSub)) continue;

        std::wstring subject = pc.GetString(ctx, hn, 0x0037u);
        std::wstring from    = pc.GetString(ctx, hn, 0x0C1Au);
        if (from.empty()) from = pc.GetString(ctx, hn, 0x0042u);
        std::wstring fromAddr = pc.GetString(ctx, hn, 0x5D01u);
        if (fromAddr.empty()) fromAddr = pc.GetString(ctx, hn, 0x0C1Fu);
        if (!fromAddr.empty() && from.find(fromAddr) == std::wstring::npos) {
            from = from.empty() ? fromAddr : (from + L" <" + fromAddr + L">");
        }
        std::wstring dispTo = pc.GetString(ctx, hn, 0x0E04u);
        std::wstring dateStr = pc.GetFileTimeStr(ctx, hn, 0x0E06u);
        if (dateStr.empty()) dateStr = pc.GetFileTimeStr(ctx, hn, 0x0039u);

        // Body fallback chain: PidTagBody (0x1000) -> PidTagHtml (0x1013) -> PidTagRtfCompressed (0x1009 LZFu)
        std::wstring body = pc.GetString(ctx, hn, 0x1000u);
        if (body.empty()) {
            std::vector<uint8_t> htmlBin;
            if (pc.GetBinary(ctx, hn, 0x1013u, htmlBin)) {
                std::wstring htmlW = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(htmlBin.data()), htmlBin.size()));
                for (const auto& p : DetailMediaAndScoreDoc::StripHtmlToParagraphs(htmlW, 120)) {
                    if (!body.empty()) body += L"\n";
                    body += p;
                }
            }
        }
        if (body.empty()) {
            std::vector<uint8_t> rtfComp, rtfRaw;
            ParsedOfficeDocument rtfDoc;
            if (pc.GetBinary(ctx, hn, 0x1009u, rtfComp) &&
                DetailMediaAndScoreDoc::DecompressRtfLzfu(rtfComp, rtfRaw) &&
                ParseRtfDocument(rtfRaw, rtfDoc)) {
                for (const auto& p : rtfDoc.paragraphs) {
                    if (!body.empty()) body += L"\n";
                    body += p.text;
                }
            }
        }

        // Inspect subnodes for Recipients (0x0692 TC) & Attachments (NID_TYPE_ATTACHMENT 0x0F or 0x0671 TC)
        std::vector<std::wstring> attachNames;
        for (const auto& sn : hn.subnodes) {
            uint8_t sType = static_cast<uint8_t>(sn.nid & 0x1Fu);
            if (sType == 0x0Fu) { // Attachment PC subnode
                DetailPst::PstHeapOnNode ahn;
                DetailPst::PstPropMap apc;
                if (apc.LoadFromNode(ctx, ahn, sn.bidData, sn.bidSub)) {
                    std::wstring fn = apc.GetString(ctx, ahn, 0x3707u);
                    if (fn.empty()) fn = apc.GetString(ctx, ahn, 0x3704u);
                    if (!fn.empty()) attachNames.push_back(std::move(fn));
                }
            } else if ((sn.nid & 0xFFFFULL) == 0x0671ULL && attachNames.empty()) { // Attachment Table TC
                DetailPst::ExtractTcStringColumn(ctx, sn.bidData, sn.bidSub, 0x3707u, 0x3704u, attachNames);
            } else if ((sn.nid & 0xFFFFULL) == 0x0692ULL && dispTo.empty()) { // Recipient Table TC
                std::vector<std::wstring> recips;
                DetailPst::ExtractTcStringColumn(ctx, sn.bidData, sn.bidSub, 0x3001u, 0x39FEu, recips);
                for (size_t ri = 0; ri < recips.size(); ++ri) {
                    if (ri) dispTo += L"; ";
                    dispTo += recips[ri];
                }
            }
        }

        std::wstring attSummary;
        for (size_t ai = 0; ai < attachNames.size(); ++ai) {
            if (ai) attSummary += L", ";
            attSummary += attachNames[ai];
        }

        std::wstring preview = body.substr(0, std::min<size_t>(body.size(), 240u));
        for (wchar_t& ch : preview) if (ch == L'\r' || ch == L'\n') ch = L' ';

        std::wstring folderPath = buildFolderPath(ne.nidParent);
        msgSh.rows.push_back({ folderPath, dateStr, from, dispTo, subject, preview, attSummary });

        if (out.paragraphs.size() < 600) {
            std::wstring hdrLine = L"[" + folderPath + L"] " + (subject.empty() ? L"(No Subject)" : subject);
            out.paragraphs.push_back({ std::move(hdrLine), 2, false, false });
            if (!from.empty() || !dispTo.empty() || !dateStr.empty()) {
                out.paragraphs.push_back({ L"From: " + from + (dispTo.empty() ? L"" : L" | To: " + dispTo) +
                                           (dateStr.empty() ? L"" : L" | " + dateStr), 0, false, false });
            }
            if (!body.empty()) out.paragraphs.push_back({ body.substr(0, 4096), 0, false, false });
        }
        ++msgCount;
    }

    SpreadsheetSheet sumSh;
    sumSh.name = L"PST_Summary";
    sumSh.rows.push_back({ L"Property", L"Value" });
    sumSh.rows.push_back({ L"Format Version", std::to_wstring(wVer) + (ctx.isVer36 ? L" (OST 2013+ 4K Pages)" : (isUni ? L" (Unicode 64-bit)" : L" (ANSI 32-bit)")) });
    sumSh.rows.push_back({ L"Block Encryption",
        ctx.cryptMethod == 0 ? L"None (0)" : (ctx.cryptMethod == 1 ? L"Permutative / Compressible (1)" :
        (ctx.cryptMethod == 2 ? L"Cyclic CRC Keyed (2)" : std::to_wstring(ctx.cryptMethod))) });
    sumSh.rows.push_back({ L"BBT Blocks / NBT Nodes", std::to_wstring(ctx.bbt.size()) + L" / " + std::to_wstring(ctx.nbt.size()) });
    sumSh.rows.push_back({ L"Folders / Messages", std::to_wstring(folderOrder.size()) + L" / " + std::to_wstring(msgCount) });

    FinalizeSheetCols(msgSh);
    FinalizeSheetCols(fldSh);
    FinalizeSheetCols(sumSh);
    RecountDocText(out);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = (extLower == L".ost")
        ? L"Microsoft Outlook Offline Folder Store (.ost)"
        : L"Microsoft Outlook Personal Folders (.pst)";
    if (msgSh.rows.size() > 1) out.sheets.push_back(std::move(msgSh));
    out.sheets.push_back(std::move(fldSh));
    out.sheets.push_back(std::move(sumSh));
    if (!src.ReadFailed()) out.sourceReadFully = true;
    out.valid = true;
    return true;
}

} // namespace Formats
