// StreamCodecs.cpp — In-process stream decompressors (LZMA1, LZMA2, BCJ x86, XZ, BZip2, LZ4 frame,
// Zstd frame, POSIX TAR in-memory, and Win32 SetupIterateCabinetW .cab helper).
#include "StreamCodecs.h"
#include "FormatSupport.h"
#include "ArchiveTypes.h"
#include "PathUtils.h"
#include "formats/common/ArchiveFormatCommon.h"
#ifdef _WIN32
#include <setupapi.h>
#endif
#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

namespace Explorer2 {
namespace {
inline uint16_t ReadU16LE(const uint8_t* p) { return ::Explorer2::ByteIO::RdU16LE(p); }
inline uint32_t ReadU32LE(const uint8_t* p) { return ::Explorer2::ByteIO::RdU32LE(p); }
inline uint64_t ReadU64LE(const uint8_t* p) { return ::Explorer2::ByteIO::RdU64LE(p); }
inline uint32_t HighestBit32(uint32_t v) {
    uint32_t r = 0;
    while (v > 1u) { v >>= 1; ++r; }
    return r;
}
} // namespace

namespace StreamCodecs {

namespace {

struct LzmaDecoderState {
    int lc = 3;
    int lp = 0;
    int pb = 2;
    bool hasProps = false;

    uint32_t state = 0;
    uint32_t rep0 = 0;
    uint32_t rep1 = 0;
    uint32_t rep2 = 0;
    uint32_t rep3 = 0;

    uint16_t isMatch[12][16];
    uint16_t isRep[12];
    uint16_t isRepG0[12];
    uint16_t isRepG1[12];
    uint16_t isRepG2[12];
    uint16_t isRep0Long[12][16];
    uint16_t posSlotDecoder[4][64];
    uint16_t posDecoders[128];
    uint16_t alignDecoder[16];

    struct LenProbs {
        uint16_t choice;
        uint16_t choice2;
        uint16_t low[16][8];
        uint16_t mid[16][8];
        uint16_t high[256];
        void Reset() {
            choice = 1024;
            choice2 = 1024;
            for (int p = 0; p < 16; ++p) {
                for (int i = 0; i < 8; ++i) {
                    low[p][i] = 1024;
                    mid[p][i] = 1024;
                }
            }
            for (int i = 0; i < 256; ++i) high[i] = 1024;
        }
    };

    LenProbs lenDec;
    LenProbs repLenDec;
    std::vector<uint16_t> litProbs;

    bool SetProps(uint8_t propsByte, bool enforceLzma2Limit) {
        if (propsByte >= 9 * 5 * 5) return false;
        int nlc = propsByte % 9;
        int rem = propsByte / 9;
        int nlp = rem % 5;
        int npb = rem / 5;
        if (enforceLzma2Limit && (nlc + nlp > 4)) return false;
        lc = nlc;
        lp = nlp;
        pb = npb;
        size_t numLit = (static_cast<size_t>(1) << (lc + lp)) * 0x300u;
        litProbs.assign(numLit, 1024);
        hasProps = true;
        return true;
    }

    void ResetStateAndProbs() {
        state = 0;
        rep0 = rep1 = rep2 = rep3 = 0;
        for (int s = 0; s < 12; ++s) {
            isRep[s] = 1024;
            isRepG0[s] = 1024;
            isRepG1[s] = 1024;
            isRepG2[s] = 1024;
            for (int p = 0; p < 16; ++p) {
                isMatch[s][p] = 1024;
                isRep0Long[s][p] = 1024;
            }
        }
        for (int l = 0; l < 4; ++l) {
            for (int i = 0; i < 64; ++i) posSlotDecoder[l][i] = 1024;
        }
        for (int i = 0; i < 128; ++i) posDecoders[i] = 1024;
        for (int i = 0; i < 16; ++i) alignDecoder[i] = 1024;
        lenDec.Reset();
        repLenDec.Reset();
        std::fill(litProbs.begin(), litProbs.end(), static_cast<uint16_t>(1024));
    }

    bool DecodeChunk(
        const uint8_t* src,
        size_t srcLen,
        std::vector<uint8_t>& out,
        uint64_t chunkUnpLen,
        bool hasKnownSize,
        size_t dictResetBase,
        uint32_t dictSize,
        size_t maxOut)
    {
        if (!hasProps || srcLen < 5 || src[0] != 0) return false;
        uint32_t range = 0xFFFFFFFFu;
        uint32_t code = (static_cast<uint32_t>(src[1]) << 24) |
                        (static_cast<uint32_t>(src[2]) << 16) |
                        (static_cast<uint32_t>(src[3]) << 8) |
                        static_cast<uint32_t>(src[4]);
        size_t srcPos = 5;
        bool overread = false;

        auto normalize = [&]() {
            if (range < 0x01000000u) {
                range <<= 8;
                if (srcPos < srcLen) {
                    code = (code << 8) | src[srcPos++];
                } else {
                    code <<= 8;
                    overread = true;
                }
            }
        };

        auto decodeBit = [&](uint16_t& prob) -> uint32_t {
            normalize();
            uint32_t bound = (range >> 11) * prob;
            if (code < bound) {
                range = bound;
                prob = static_cast<uint16_t>(prob + ((2048u - prob) >> 5));
                return 0u;
            } else {
                range -= bound;
                code -= bound;
                prob = static_cast<uint16_t>(prob - (prob >> 5));
                return 1u;
            }
        };

        auto decodeBitTree = [&](uint16_t* probs, int numBits) -> uint32_t {
            uint32_t m = 1u;
            for (int i = 0; i < numBits; ++i) {
                m = (m << 1) | decodeBit(probs[m]);
            }
            return m - (1u << numBits);
        };

        auto decodeRevBitTree = [&](uint16_t* probs, int numBits) -> uint32_t {
            uint32_t m = 1u;
            uint32_t sym = 0u;
            for (int i = 0; i < numBits; ++i) {
                uint32_t b = decodeBit(probs[m]);
                m = (m << 1) | b;
                sym |= (b << i);
            }
            return sym;
        };

        auto decodeDirectBits = [&](int numBits) -> uint32_t {
            uint32_t res = 0u;
            for (int i = 0; i < numBits; ++i) {
                normalize();
                range >>= 1;
                res <<= 1;
                if (code >= range) {
                    code -= range;
                    res |= 1u;
                }
            }
            return res;
        };

        auto decodeLen = [&](LenProbs& lpTbl, uint32_t posState) -> uint32_t {
            if (decodeBit(lpTbl.choice) == 0u) {
                return decodeBitTree(lpTbl.low[posState], 3);
            }
            if (decodeBit(lpTbl.choice2) == 0u) {
                return 8u + decodeBitTree(lpTbl.mid[posState], 3);
            }
            return 16u + decodeBitTree(lpTbl.high, 8);
        };

        const size_t startOut = out.size();
        if (hasKnownSize) {
            if (chunkUnpLen > maxOut || startOut + chunkUnpLen > maxOut) return false;
            out.reserve(startOut + static_cast<size_t>(std::min<uint64_t>(chunkUnpLen, 4 * 1024 * 1024)));
        }

        const uint32_t posStateMask = (1u << pb) - 1u;
        const uint32_t lpMask = (1u << lp) - 1u;

        while (!hasKnownSize || (out.size() - startOut) < chunkUnpLen) {
            if (overread && srcPos >= srcLen) return false;
            if (out.size() >= maxOut) return false;

            const size_t relPos = out.size() - dictResetBase;
            const uint32_t posState = static_cast<uint32_t>(relPos) & posStateMask;

            if (decodeBit(isMatch[state][posState]) == 0u) {
                // Literal
                uint8_t prevByte = (out.size() > dictResetBase) ? out.back() : 0u;
                uint32_t litState = ((static_cast<uint32_t>(relPos) & lpMask) << lc) |
                                    (static_cast<uint32_t>(prevByte) >> (8 - lc));
                uint16_t* probs = &litProbs[static_cast<size_t>(litState) * 0x300u];
                uint32_t sym = 1u;
                if (state >= 7u) {
                    size_t availHist = out.size() - dictResetBase;
                    uint32_t matchByte = (rep0 < availHist) ? out[out.size() - rep0 - 1] : 0u;
                    bool matched = true;
                    while (sym < 0x100u) {
                        uint32_t matchBit = (matchByte >> 7) & 1u;
                        matchByte <<= 1;
                        if (matched) {
                            uint32_t b = decodeBit(probs[((1u + matchBit) << 8) + sym]);
                            sym = (sym << 1) | b;
                            if (b != matchBit) matched = false;
                        } else {
                            sym = (sym << 1) | decodeBit(probs[sym]);
                        }
                    }
                } else {
                    while (sym < 0x100u) {
                        sym = (sym << 1) | decodeBit(probs[sym]);
                    }
                }
                out.push_back(static_cast<uint8_t>(sym & 0xFFu));
                state = (state < 4u) ? 0u : ((state < 10u) ? (state - 3u) : (state - 6u));
            } else {
                uint32_t len = 0u;
                if (decodeBit(isRep[state]) == 1u) {
                    if (decodeBit(isRepG0[state]) == 0u) {
                        if (decodeBit(isRep0Long[state][posState]) == 0u) {
                            // ShortRep: 1 byte from rep0 + 1
                            size_t availHist = out.size() - dictResetBase;
                            if (rep0 >= availHist || (dictSize != 0 && rep0 >= dictSize)) return false;
                            state = (state < 7u) ? 9u : 11u;
                            uint8_t b = out[out.size() - rep0 - 1];
                            out.push_back(b);
                            continue;
                        }
                    } else {
                        uint32_t dist = 0u;
                        if (decodeBit(isRepG1[state]) == 0u) {
                            dist = rep1;
                        } else {
                            if (decodeBit(isRepG2[state]) == 0u) {
                                dist = rep2;
                            } else {
                                dist = rep3;
                                rep3 = rep2;
                            }
                            rep2 = rep1;
                        }
                        rep1 = rep0;
                        rep0 = dist;
                    }
                    len = 2u + decodeLen(repLenDec, posState);
                    state = (state < 7u) ? 8u : 11u;
                } else {
                    rep3 = rep2;
                    rep2 = rep1;
                    rep1 = rep0;
                    len = 2u + decodeLen(lenDec, posState);
                    state = (state < 7u) ? 7u : 10u;
                    uint32_t lenToPos = (len - 2u < 4u) ? (len - 2u) : 3u;
                    uint32_t posSlot = decodeBitTree(posSlotDecoder[lenToPos], 6);
                    if (posSlot < 4u) {
                        rep0 = posSlot;
                    } else {
                        int numDirect = static_cast<int>((posSlot >> 1) - 1u);
                        uint32_t dist = (2u | (posSlot & 1u)) << numDirect;
                        if (posSlot < 14u) {
                            dist |= decodeRevBitTree(posDecoders + dist - posSlot - 1, numDirect);
                        } else {
                            dist |= (decodeDirectBits(numDirect - 4) << 4);
                            dist |= decodeRevBitTree(alignDecoder, 4);
                            if (dist == 0xFFFFFFFFu) {
                                // End-Of-Payload Marker (EOPM)
                                break;
                            }
                        }
                        rep0 = dist;
                    }
                }

                size_t availHist = out.size() - dictResetBase;
                if (rep0 >= availHist || (dictSize != 0 && rep0 >= dictSize)) return false;
                uint64_t toCopy = len;
                if (hasKnownSize && (out.size() - startOut) + toCopy > chunkUnpLen) {
                    toCopy = chunkUnpLen - (out.size() - startOut);
                }
                if (out.size() + toCopy > maxOut) return false;
                for (uint64_t k = 0; k < toCopy; ++k) {
                    uint8_t b = out[out.size() - rep0 - 1];
                    out.push_back(b);
                }
            }
        }

        normalize();
        return !hasKnownSize || ((out.size() - startOut) == chunkUnpLen);
    }
};

} // namespace

bool DecompressLzma1(
    uint8_t propsByte,
    uint32_t dictSize,
    const uint8_t* src,
    size_t srcLen,
    std::vector<uint8_t>& out,
    uint64_t uncompressedLen,
    bool hasKnownSize,
    size_t maxOut)
{
    out.clear();
    if (!src || srcLen < 5) return false;
    if (dictSize < 4096u) dictSize = 4096u;
    LzmaDecoderState dec;
    if (!dec.SetProps(propsByte, false)) return false;
    dec.ResetStateAndProbs();
    return dec.DecodeChunk(src, srcLen, out, uncompressedLen, hasKnownSize, 0, dictSize, maxOut);
}

bool DecompressLzma2(
    uint8_t dictPropByte,
    const uint8_t* src,
    size_t srcLen,
    std::vector<uint8_t>& out,
    size_t maxOut,
    size_t* outBytesConsumed,
    uint64_t expectedOut)
{
    out.clear();
    if (outBytesConsumed) *outBytesConsumed = 0;
    if (!src || dictPropByte > 40u) return false;

    uint32_t dictSize = (dictPropByte == 40u)
        ? 0xFFFFFFFFu
        : ((2u | (dictPropByte & 1u)) << (dictPropByte / 2u + 11u));

    if (expectedOut > 0 && expectedOut <= maxOut) {
        out.reserve(static_cast<size_t>(std::min<uint64_t>(expectedOut, 4 * 1024 * 1024)));
    }

    LzmaDecoderState dec;
    size_t dictResetBase = 0;
    bool stateInitialized = false;
    size_t pos = 0;

    while (pos < srcLen) {
        uint8_t ctrl = src[pos++];
        if (ctrl == 0x00u) {
            if (outBytesConsumed) *outBytesConsumed = pos;
            return true;
        }
        if (ctrl == 0x01u || ctrl == 0x02u) {
            // Uncompressed chunk
            if (ctrl == 0x02u && !stateInitialized && out.empty()) {
                // First chunk in LZMA2 must reset dictionary (0x01 or >=0xE0)
                // Allow leniently if dictResetBase==0
            }
            if (pos + 2 > srcLen) return false;
            size_t chunkLen = (static_cast<size_t>(src[pos]) << 8) | src[pos + 1];
            chunkLen += 1;
            pos += 2;
            if (pos + chunkLen > srcLen || out.size() + chunkLen > maxOut) return false;
            if (ctrl == 0x01u) {
                dictResetBase = out.size();
                stateInitialized = false;
            }
            out.insert(out.end(), src + pos, src + pos + chunkLen);
            pos += chunkLen;
        } else if (ctrl >= 0x80u) {
            uint32_t resetMode = (ctrl >> 5) & 0x03u;
            if (pos + 4 > srcLen) return false;
            uint64_t unpLen = ((static_cast<uint64_t>(ctrl & 0x1Fu) << 16) |
                               (static_cast<uint64_t>(src[pos]) << 8) |
                               static_cast<uint64_t>(src[pos + 1])) + 1ULL;
            size_t packLen = ((static_cast<size_t>(src[pos + 2]) << 8) | src[pos + 3]) + 1u;
            pos += 4;

            if (resetMode == 3u) {
                dictResetBase = out.size();
            }
            if (resetMode >= 2u) {
                if (pos >= srcLen) return false;
                uint8_t pb = src[pos++];
                if (!dec.SetProps(pb, true)) return false;
                dec.ResetStateAndProbs();
                stateInitialized = true;
            } else if (resetMode == 1u) {
                if (!dec.hasProps) return false;
                dec.ResetStateAndProbs();
                stateInitialized = true;
            } else {
                if (!stateInitialized || !dec.hasProps) return false;
            }

            if (pos + packLen > srcLen) return false;
            if (!dec.DecodeChunk(src + pos, packLen, out, unpLen, true, dictResetBase, dictSize, maxOut)) {
                return false;
            }
            pos += packLen;
        } else {
            return false;
        }

        if (expectedOut > 0 && out.size() >= expectedOut && pos == srcLen) {
            if (outBytesConsumed) *outBytesConsumed = pos;
            return true;
        }
    }

    if (expectedOut > 0 && out.size() == expectedOut) {
        if (outBytesConsumed) *outBytesConsumed = pos;
        return true;
    }
    return false;
}

void ApplyBcjX86Decode(uint8_t* data, size_t size, uint32_t ip) {
    if (!data || size < 5) return;
    static const bool kMaskToAllowedStatus[8] = { true, true, true, false, true, false, false, false };
    static const uint8_t kMaskToBitNumber[8] = { 0, 1, 2, 2, 3, 3, 3, 3 };

    size_t pos = 0;
    size_t prevPos = static_cast<size_t>(0) - 5;
    uint32_t prevMask = 0;

    while (pos + 5 <= size) {
        if ((data[pos] & 0xFEu) != 0xE8u) {
            ++pos;
            continue;
        }
        size_t d = pos - prevPos;
        prevPos = pos;
        if (d > 3) {
            prevMask = 0;
        } else {
            prevMask >>= (d - 1);
            if (prevMask != 0) {
                uint8_t b = data[pos + 4 - kMaskToBitNumber[prevMask]];
                if (!kMaskToAllowedStatus[prevMask] || b == 0x00u || b == 0xFFu) {
                    prevMask = ((prevMask << 1) & 0x7u) | 1u;
                    ++pos;
                    continue;
                }
            }
        }

        if (data[pos + 4] == 0x00u || data[pos + 4] == 0xFFu) {
            uint32_t src = static_cast<uint32_t>(data[pos + 1]) |
                           (static_cast<uint32_t>(data[pos + 2]) << 8) |
                           (static_cast<uint32_t>(data[pos + 3]) << 16) |
                           (static_cast<uint32_t>(data[pos + 4]) << 24);
            uint32_t dest = 0;
            for (;;) {
                dest = src - (ip + static_cast<uint32_t>(pos) + 5u);
                if (prevMask == 0) break;
                uint32_t shift = kMaskToBitNumber[prevMask] * 8u;
                uint8_t b = static_cast<uint8_t>(dest >> (24u - shift));
                if (b != 0x00u && b != 0xFFu) break;
                src = dest ^ ((1u << (32u - shift)) - 1u);
            }
            data[pos + 1] = static_cast<uint8_t>(dest & 0xFFu);
            data[pos + 2] = static_cast<uint8_t>((dest >> 8) & 0xFFu);
            data[pos + 3] = static_cast<uint8_t>((dest >> 16) & 0xFFu);
            data[pos + 4] = ((dest & 0x01000000u) != 0) ? 0xFFu : 0x00u;
            pos += 5;
        } else {
            prevMask = ((prevMask << 1) & 0x7u) | 1u;
            ++pos;
        }
    }
}

void ApplyXzDeltaDecode(uint8_t* data, size_t size, size_t dist) {
    if (!data || dist == 0 || dist > 256) return;
    for (size_t i = dist; i < size; ++i) {
        data[i] = static_cast<uint8_t>(data[i] + data[i - dist]);
    }
}

// ----------------------------------------------------------------------------
// XZ Stream Decompressor (.xz / .txz)
// ----------------------------------------------------------------------------
namespace {
bool ReadXzVli(const uint8_t* data, size_t end, size_t& pos, uint64_t& outVal) {
    outVal = 0;
    for (int i = 0; i < 9; ++i) {
        if (pos >= end) return false;
        uint8_t b = data[pos++];
        if (b == 0 && i > 0) return false;
        outVal |= static_cast<uint64_t>(b & 0x7Fu) << (i * 7);
        if ((b & 0x80u) == 0) return true;
    }
    return false;
}
} // namespace

bool DecompressXzStream(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t maxOut) {
    out.clear();
    static const uint8_t kXzMagic[6] = { 0xFD, '7', 'z', 'X', 'Z', 0x00 };
    if (!data || size < 24 || std::memcmp(data, kXzMagic, 6) != 0) return false;

    size_t pos = 0;
    bool decodedAnyStream = false;

    while (pos < size) {
        while (pos < size && data[pos] == 0x00u) ++pos;
        if (pos == size) break;
        if (pos + 24 > size || std::memcmp(data + pos, kXzMagic, 6) != 0) {
            return false;
        }
        if (data[pos + 6] != 0x00u || (data[pos + 7] & 0xF0u) != 0) return false;
        uint8_t checkType = data[pos + 7] & 0x0Fu;
        size_t checkSize = (checkType == 0) ? 0u : (static_cast<size_t>(4) << ((checkType - 1u) / 3u));
        pos += 12; // Skip Stream Header + CRC32

        bool sawIndex = false;
        while (pos < size) {
            uint8_t b0 = data[pos];
            if (b0 == 0x00u) {
                // Index Indicator
                ++pos;
                uint64_t numRecs = 0;
                if (!ReadXzVli(data, size, pos, numRecs) || numRecs > 1000000ULL) return false;
                for (uint64_t r = 0; r < numRecs; ++r) {
                    uint64_t unpad = 0, unp = 0;
                    if (!ReadXzVli(data, size, pos, unpad) || !ReadXzVli(data, size, pos, unp)) return false;
                }
                while ((pos & 3u) != 0) {
                    if (pos >= size || data[pos] != 0) return false;
                    ++pos;
                }
                if (pos + 4 + 12 > size) return false;
                pos += 4; // Index CRC32
                if (data[pos + 10] != 'Y' || data[pos + 11] != 'Z') return false;
                pos += 12; // Stream Footer
                sawIndex = true;
                break;
            }

            // Block Header
            size_t blkHdrStart = pos;
            size_t blkHdrSize = (static_cast<size_t>(b0) + 1u) * 4u;
            if (blkHdrStart + blkHdrSize > size || blkHdrSize < 8) return false;

            uint8_t blkFlags = data[pos + 1];
            if ((blkFlags & 0x3Cu) != 0) return false;
            int numFilters = static_cast<int>(blkFlags & 0x03u) + 1;
            bool hasCompSize = (blkFlags & 0x40u) != 0;
            bool hasUnpSize  = (blkFlags & 0x80u) != 0;

            size_t hpos = pos + 2;
            size_t hLimit = blkHdrStart + blkHdrSize - 4; // Exclude 4B header CRC32
            uint64_t compSizeField = 0, unpSizeField = 0;
            if (hasCompSize && !ReadXzVli(data, hLimit, hpos, compSizeField)) return false;
            if (hasUnpSize  && !ReadXzVli(data, hLimit, hpos, unpSizeField)) return false;

            struct XzFilter {
                uint64_t id = 0;
                uint8_t prop0 = 0;
                uint32_t startOff = 0;
            };
            XzFilter filters[4] = {};
            for (int f = 0; f < numFilters; ++f) {
                uint64_t fid = 0, pLen = 0;
                if (!ReadXzVli(data, hLimit, hpos, fid) || !ReadXzVli(data, hLimit, hpos, pLen)) return false;
                if (hpos + pLen > hLimit) return false;
                filters[f].id = fid;
                if (fid == 0x21u) { // LZMA2
                    if (pLen != 1) return false;
                    filters[f].prop0 = data[hpos];
                } else if (fid == 0x04u) { // BCJ x86
                    if (pLen == 4) filters[f].startOff = ReadU32LE(data + hpos);
                    else if (pLen != 0) return false;
                } else if (fid == 0x03u) { // Delta
                    if (pLen != 1) return false;
                    filters[f].prop0 = data[hpos];
                } else {
                    return false;
                }
                hpos += static_cast<size_t>(pLen);
            }
            if (filters[numFilters - 1].id != 0x21u) return false;

            pos = blkHdrStart + blkHdrSize;
            size_t availIn = hasCompSize ? static_cast<size_t>(compSizeField) : (size - pos);
            if (hasCompSize && pos + availIn > size) return false;
            if (out.size() >= maxOut) return false;

            std::vector<uint8_t> blkOut;
            size_t consumed = 0;
            if (!DecompressLzma2(
                    filters[numFilters - 1].prop0,
                    data + pos,
                    availIn,
                    blkOut,
                    maxOut - out.size(),
                    &consumed,
                    hasUnpSize ? unpSizeField : 0))
            {
                return false;
            }
            if (hasCompSize && consumed != availIn) return false;
            if (hasUnpSize && blkOut.size() != unpSizeField) return false;

            for (int f = numFilters - 2; f >= 0; --f) {
                if (filters[f].id == 0x04u) {
                    ApplyBcjX86Decode(blkOut.data(), blkOut.size(), filters[f].startOff);
                } else if (filters[f].id == 0x03u) {
                    ApplyXzDeltaDecode(blkOut.data(), blkOut.size(), static_cast<size_t>(filters[f].prop0) + 1u);
                }
            }

            if (out.size() + blkOut.size() > maxOut) return false;
            out.insert(out.end(), blkOut.begin(), blkOut.end());

            pos += consumed;
            while ((pos & 3u) != 0) {
                if (pos >= size || data[pos] != 0) return false;
                ++pos;
            }
            if (pos + checkSize > size) return false;
            pos += checkSize;
        }

        if (!sawIndex) return false;
        decodedAnyStream = true;
    }

    return decodedAnyStream;
}

// ----------------------------------------------------------------------------
// BZip2 Stream Decompressor (.bz2 / .tbz2 / .tbz)
// ----------------------------------------------------------------------------
namespace {

struct Bz2BitReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t bytePos = 0;
    uint32_t bitBuf = 0;
    int bitCount = 0;

    bool ReadBits(int n, uint32_t& out) {
        while (bitCount < n) {
            if (bytePos >= size) return false;
            bitBuf = (bitBuf << 8) | data[bytePos++];
            bitCount += 8;
        }
        bitCount -= n;
        out = (bitBuf >> bitCount) & ((n == 32) ? 0xFFFFFFFFu : ((1u << n) - 1u));
        return true;
    }

    int ReadBit() {
        if (bitCount == 0) {
            if (bytePos >= size) return -1;
            bitBuf = data[bytePos++];
            bitCount = 8;
        }
        --bitCount;
        return static_cast<int>((bitBuf >> bitCount) & 1u);
    }

    void AlignToByte() {
        int rem = bitCount & 7;
        bitCount -= rem;
    }
};

struct Bz2HuffGroup {
    int minLen = 1;
    int maxLen = 1;
    int32_t limit[24] = {};
    int32_t base[24] = {};
    uint16_t perm[258] = {};

    bool Build(const uint8_t* length, int alphaSize) {
        minLen = 23;
        maxLen = 0;
        for (int i = 0; i < alphaSize; ++i) {
            if (length[i] > maxLen) maxLen = length[i];
            if (length[i] < minLen) minLen = length[i];
        }
        if (minLen < 1 || maxLen > 20 || minLen > maxLen) return false;

        int pp = 0;
        for (int i = minLen; i <= maxLen; ++i) {
            for (int j = 0; j < alphaSize; ++j) {
                if (length[j] == i) perm[pp++] = static_cast<uint16_t>(j);
            }
        }
        for (int i = 0; i < 23; ++i) {
            base[i] = 0;
            limit[i] = 0;
        }
        for (int i = 0; i < alphaSize; ++i) {
            base[length[i] + 1]++;
        }
        for (int i = 1; i < 23; ++i) {
            base[i] += base[i - 1];
        }
        int32_t vec = 0;
        for (int i = minLen; i <= maxLen; ++i) {
            vec += (base[i + 1] - base[i]);
            limit[i] = vec - 1;
            vec <<= 1;
        }
        for (int i = minLen + 1; i <= maxLen; ++i) {
            base[i] = ((limit[i - 1] + 1) << 1) - base[i];
        }
        return true;
    }

    int Decode(Bz2BitReader& br) const {
        uint32_t zvec = 0;
        if (!br.ReadBits(minLen, zvec)) return -1;
        int zn = minLen;
        while (static_cast<int32_t>(zvec) > limit[zn]) {
            ++zn;
            if (zn > maxLen) return -1;
            int b = br.ReadBit();
            if (b < 0) return -1;
            zvec = (zvec << 1) | static_cast<uint32_t>(b);
        }
        int32_t idx = static_cast<int32_t>(zvec) - base[zn];
        if (idx < 0 || idx >= 258) return -1;
        return perm[idx];
    }
};

} // namespace

bool DecompressBzip2Stream(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t maxOut) {
    out.clear();
    if (!data || size < 10) return false;
    if (data[0] != 'B' || data[1] != 'Z' || data[2] != 'h' || data[3] < '1' || data[3] > '9') return false;

    Bz2BitReader br{ data, size, 0, 0, 0 };
    bool decodedAny = false;

    while (br.bytePos < size || br.bitCount >= 32) {
        // Expect 'BZh1'..'BZh9' stream header
        br.AlignToByte();
        size_t curByteOff = br.bytePos - static_cast<size_t>(br.bitCount / 8);
        if (curByteOff >= size) break;
        uint32_t hdr32 = 0;
        if (!br.ReadBits(32, hdr32)) break;
        uint8_t b0 = static_cast<uint8_t>((hdr32 >> 24) & 0xFFu);
        uint8_t b1 = static_cast<uint8_t>((hdr32 >> 16) & 0xFFu);
        uint8_t b2 = static_cast<uint8_t>((hdr32 >> 8) & 0xFFu);
        uint8_t b3 = static_cast<uint8_t>(hdr32 & 0xFFu);
        if (b0 != 'B' || b1 != 'Z' || b2 != 'h' || b3 < '1' || b3 > '9') {
            if (decodedAny && b0 == 0) break;
            return false;
        }
        const size_t maxBlockBytes = static_cast<size_t>(b3 - '0') * 100000u + 16u;

        bool sawStreamEnd = false;
        for (;;) {
            uint32_t magHi = 0, magLo = 0;
            if (!br.ReadBits(24, magHi) || !br.ReadBits(24, magLo)) return false;
            uint64_t magic = (static_cast<uint64_t>(magHi) << 24) | magLo;

            if (magic == 0x177245385090ULL) {
                uint32_t streamCrc = 0;
                if (!br.ReadBits(32, streamCrc)) return false;
                sawStreamEnd = true;
                break;
            }
            if (magic != 0x314159265359ULL) return false;

            uint32_t blockCrc = 0, randBit = 0, origPtr = 0;
            if (!br.ReadBits(32, blockCrc) || !br.ReadBits(1, randBit) || !br.ReadBits(24, origPtr)) {
                return false;
            }
            if (randBit != 0) return false;

            uint32_t inUse16 = 0;
            if (!br.ReadBits(16, inUse16)) return false;
            uint8_t seqToUnseq[256] = {};
            int nInUse = 0;
            for (int g = 0; g < 16; ++g) {
                if ((inUse16 & (1u << (15 - g))) != 0) {
                    uint32_t grp16 = 0;
                    if (!br.ReadBits(16, grp16)) return false;
                    for (int j = 0; j < 16; ++j) {
                        if ((grp16 & (1u << (15 - j))) != 0) {
                            seqToUnseq[nInUse++] = static_cast<uint8_t>(g * 16 + j);
                        }
                    }
                }
            }
            if (nInUse == 0) return false;
            const int alphaSize = nInUse + 2;
            const int eobSym = nInUse + 1;

            uint32_t nGroups = 0, nSelectors = 0;
            if (!br.ReadBits(3, nGroups) || !br.ReadBits(15, nSelectors)) return false;
            if (nGroups < 2 || nGroups > 6 || nSelectors < 1 || nSelectors > 32767) return false;

            std::vector<uint8_t> selector(nSelectors);
            uint8_t mtfSel[6] = { 0, 1, 2, 3, 4, 5 };
            for (uint32_t i = 0; i < nSelectors; ++i) {
                uint32_t j = 0;
                for (;;) {
                    int bit = br.ReadBit();
                    if (bit < 0) return false;
                    if (bit == 0) break;
                    ++j;
                    if (j >= nGroups) return false;
                }
                uint8_t v = mtfSel[j];
                while (j > 0) {
                    mtfSel[j] = mtfSel[j - 1];
                    --j;
                }
                mtfSel[0] = v;
                selector[i] = v;
            }

            std::vector<Bz2HuffGroup> groups(nGroups);
            uint8_t lengths[258] = {};
            for (uint32_t t = 0; t < nGroups; ++t) {
                uint32_t curr = 0;
                if (!br.ReadBits(5, curr)) return false;
                int lenVal = static_cast<int>(curr);
                for (int i = 0; i < alphaSize; ++i) {
                    for (;;) {
                        int b = br.ReadBit();
                        if (b < 0) return false;
                        if (b == 0) break;
                        int dir = br.ReadBit();
                        if (dir < 0) return false;
                        lenVal += (dir == 0) ? 1 : -1;
                        if (lenVal < 1 || lenVal > 20) return false;
                    }
                    lengths[i] = static_cast<uint8_t>(lenVal);
                }
                if (!groups[t].Build(lengths, alphaSize)) return false;
            }

            uint8_t yy[256] = {};
            for (int i = 0; i < nInUse; ++i) yy[i] = static_cast<uint8_t>(i);
            uint32_t unzftab[256] = {};
            std::vector<uint8_t> ll8;
            ll8.reserve(std::min<size_t>(maxBlockBytes, 65536));

            uint32_t groupNo = 0;
            int groupPos = 0;
            const Bz2HuffGroup* curGroup = nullptr;
            auto getNextSym = [&]() -> int {
                if (groupPos == 0) {
                    if (groupNo >= nSelectors) return -1;
                    curGroup = &groups[selector[groupNo++]];
                    groupPos = 50;
                }
                --groupPos;
                return curGroup->Decode(br);
            };

            int nextSym = getNextSym();
            if (nextSym < 0) return false;

            while (nextSym != eobSym) {
                if (nextSym == 0 || nextSym == 1) {
                    // Bijective base-2 run of yy[0]
                    uint32_t es = 0;
                    uint32_t mult = 1;
                    do {
                        if (nextSym == 0) es += mult;
                        else es += 2u * mult;
                        mult <<= 1;
                        if (mult > 2097152u) return false;
                        nextSym = getNextSym();
                        if (nextSym < 0) return false;
                    } while (nextSym == 0 || nextSym == 1);

                    uint8_t uc = seqToUnseq[yy[0]];
                    if (ll8.size() + es > maxBlockBytes) return false;
                    ll8.insert(ll8.end(), es, uc);
                    unzftab[uc] += es;
                    continue;
                }

                if (nextSym < 2 || nextSym > nInUse) return false;
                if (ll8.size() + 1 > maxBlockBytes) return false;

                int nn = nextSym - 1;
                uint8_t idx = yy[nn];
                for (int k = nn; k > 0; --k) yy[k] = yy[k - 1];
                yy[0] = idx;
                uint8_t uc = seqToUnseq[idx];
                ll8.push_back(uc);
                unzftab[uc]++;

                nextSym = getNextSym();
                if (nextSym < 0) return false;
            }

            if (ll8.empty()) continue;
            const uint32_t nblock = static_cast<uint32_t>(ll8.size());
            if (origPtr >= nblock) return false;

            // Inverse Burrows-Wheeler Transform
            uint32_t cftab[257] = {};
            for (int i = 1; i <= 256; ++i) cftab[i] = cftab[i - 1] + unzftab[i - 1];
            std::vector<uint32_t> T(nblock);
            for (uint32_t i = 0; i < nblock; ++i) {
                T[cftab[ll8[i]]++] = i;
            }

            uint32_t tPos = T[origPtr];
            int prevCh = -1;
            int runLen = 0;
            for (uint32_t k = 0; k < nblock; ++k) {
                if (tPos >= nblock) return false;
                uint8_t uc = ll8[tPos];
                tPos = T[tPos];

                if (runLen == 4) {
                    if (out.size() + uc > maxOut) return false;
                    out.insert(out.end(), uc, static_cast<uint8_t>(prevCh));
                    runLen = 0;
                    prevCh = -1;
                } else {
                    if (out.size() + 1 > maxOut) return false;
                    out.push_back(uc);
                    if (static_cast<int>(uc) == prevCh) {
                        ++runLen;
                    } else {
                        prevCh = uc;
                        runLen = 1;
                    }
                }
            }
        }

        if (!sawStreamEnd) return false;
        decodedAny = true;
    }

    return decodedAny;
}

// ----------------------------------------------------------------------------
// LZ4 Frame Decompressor (.lz4 / .tlz4 / .tar.lz4)
// ----------------------------------------------------------------------------

bool DecompressLz4Frame(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t maxOut) {
    out.clear();
    if (!data || size < 7) return false;

    size_t pos = 0;
    bool decodedAny = false;

    while (pos + 4 <= size) {
        uint32_t magic = ReadU32LE(data + pos);
        if ((magic & 0xFFFFFFF0u) == 0x184D2A50u) {
            // Skippable LZ4 frame
            if (pos + 8 > size) return false;
            uint32_t skipSz = ReadU32LE(data + pos + 4);
            if (pos + 8ULL + skipSz > size) return false;
            pos += 8 + static_cast<size_t>(skipSz);
            decodedAny = true;
            continue;
        }
        if (magic == 0x184C2102u) {
            // Legacy LZ4 frame
            pos += 4;
            size_t frameBase = out.size();
            while (pos + 4 <= size) {
                uint32_t peek = ReadU32LE(data + pos);
                if (peek == 0x184D2204u || peek == 0x184C2102u || (peek & 0xFFFFFFF0u) == 0x184D2A50u) break;
                pos += 4;
                if (peek == 0) break;
                if (pos + peek > size) return false;
                if (!::Explorer2::Lz4::DecodeBlockAppend(data + pos, peek, out, frameBase, maxOut)) return false;
                pos += peek;
            }
            decodedAny = true;
            continue;
        }
        if (magic != 0x184D2204u) {
            if (decodedAny) break;
            return false;
        }
        pos += 4;
        if (pos + 3 > size) return false;

        uint8_t flg = data[pos++];
        uint8_t bd  = data[pos++];
        (void)bd;
        if ((flg >> 6) != 0x01u) return false;
        bool bChecksum = (flg & 0x10u) != 0;
        bool hasCSize  = (flg & 0x08u) != 0;
        bool cChecksum = (flg & 0x04u) != 0;
        bool hasDictId = (flg & 0x01u) != 0;

        if (hasCSize) {
            if (pos + 8 > size) return false;
            uint64_t csz = ReadU64LE(data + pos);
            pos += 8;
            if (csz <= maxOut) out.reserve(out.size() + static_cast<size_t>(std::min<uint64_t>(csz, 4 * 1024 * 1024)));
        }
        if (hasDictId) {
            if (pos + 4 > size) return false;
            pos += 4;
        }
        if (pos >= size) return false;
        ++pos; // Header checksum HC

        const size_t frameBase = out.size();
        bool sawEndMark = false;
        while (pos + 4 <= size) {
            uint32_t blkSz = ReadU32LE(data + pos);
            pos += 4;
            if (blkSz == 0u) {
                sawEndMark = true;
                break;
            }
            bool uncompressed = (blkSz & 0x80000000u) != 0;
            uint32_t cLen = blkSz & 0x7FFFFFFFu;
            if (pos + cLen > size) return false;
            if (uncompressed) {
                if (out.size() + cLen > maxOut) return false;
                out.insert(out.end(), data + pos, data + pos + cLen);
            } else {
                if (!::Explorer2::Lz4::DecodeBlockAppend(data + pos, cLen, out, frameBase, maxOut)) return false;
            }
            pos += cLen;
            if (bChecksum) {
                if (pos + 4 > size) return false;
                pos += 4;
            }
        }
        if (!sawEndMark) return false;
        if (cChecksum) {
            if (pos + 4 > size) return false;
            pos += 4;
        }
        decodedAny = true;
    }

    return decodedAny;
}

// ----------------------------------------------------------------------------
// Zstandard (RFC 8878) Frame Decompressor (.zst / .tzst / .tar.zst)
// ----------------------------------------------------------------------------
namespace {

struct ZstdRevBitStream {
    const uint8_t* data = nullptr;
    int64_t bitPos = 0; // Total remaining bits from bit 0 of data[0]

    bool Init(const uint8_t* buf, size_t len) {
        if (!buf || len == 0) return false;
        uint8_t lastByte = buf[len - 1];
        if (lastByte == 0) return false;
        uint32_t highBit = HighestBit32(lastByte);
        data = buf;
        bitPos = static_cast<int64_t>(len - 1) * 8 + static_cast<int64_t>(highBit);
        return true;
    }

    uint32_t ReadBits(int n) {
        if (n <= 0) return 0u;
        bitPos -= n;
        int64_t curPos = bitPos;
        uint32_t res = 0u;
        for (int i = n - 1; i >= 0; --i) {
            int64_t idx = curPos + i;
            if (idx >= 0) {
                uint8_t b = data[static_cast<size_t>(idx >> 3)];
                uint32_t bit = (b >> (idx & 7)) & 1u;
                res = (res << 1) | bit;
            } else {
                res <<= 1;
            }
        }
        return res;
    }

    uint32_t PeekBits(int n) const {
        if (n <= 0) return 0u;
        int64_t curPos = bitPos - n;
        uint32_t res = 0u;
        for (int i = n - 1; i >= 0; --i) {
            int64_t idx = curPos + i;
            if (idx >= 0) {
                uint8_t b = data[static_cast<size_t>(idx >> 3)];
                uint32_t bit = (b >> (idx & 7)) & 1u;
                res = (res << 1) | bit;
            } else {
                res <<= 1;
            }
        }
        return res;
    }

    void Consume(int n) {
        bitPos -= n;
    }
};

struct ZstdFseEntry {
    uint16_t newStateBase = 0;
    uint8_t sym = 0;
    uint8_t nbBits = 0;
};

struct ZstdFseTable {
    uint32_t accuracyLog = 0;
    std::vector<ZstdFseEntry> entries;

    bool BuildFromNormalized(const int16_t* norm, uint32_t numSyms, uint32_t accLog) {
        if (accLog > 15 || numSyms == 0 || numSyms > 256) return false;
        accuracyLog = accLog;
        const uint32_t tableSize = 1u << accLog;
        entries.assign(tableSize, {});

        uint16_t symbolNext[256] = {};
        uint32_t highThreshold = tableSize - 1u;
        uint32_t totalAssigned = 0;

        for (uint32_t s = 0; s < numSyms; ++s) {
            if (norm[s] == -1) {
                if (totalAssigned >= tableSize) return false;
                entries[highThreshold--].sym = static_cast<uint8_t>(s);
                symbolNext[s] = 1;
                ++totalAssigned;
            } else if (norm[s] > 0) {
                totalAssigned += static_cast<uint32_t>(norm[s]);
                if (totalAssigned > tableSize) return false;
                symbolNext[s] = static_cast<uint16_t>(norm[s]);
            }
        }
        if (totalAssigned != tableSize) return false;

        const uint32_t step = (tableSize >> 1) + (tableSize >> 3) + 3u;
        const uint32_t mask = tableSize - 1u;
        uint32_t pos = 0;
        for (uint32_t s = 0; s < numSyms; ++s) {
            if (norm[s] <= 0) continue;
            for (int16_t i = 0; i < norm[s]; ++i) {
                entries[pos].sym = static_cast<uint8_t>(s);
                do {
                    pos = (pos + step) & mask;
                } while (pos > highThreshold);
            }
        }
        if (pos != 0u) return false;

        for (uint32_t u = 0; u < tableSize; ++u) {
            uint8_t s = entries[u].sym;
            uint16_t nextState = symbolNext[s]++;
            if (nextState == 0) return false;
            uint8_t nb = static_cast<uint8_t>(accLog - HighestBit32(nextState));
            entries[u].nbBits = nb;
            entries[u].newStateBase = static_cast<uint16_t>((static_cast<uint32_t>(nextState) << nb) - tableSize);
        }
        return true;
    }

    void BuildRle(uint8_t rleSym) {
        accuracyLog = 0;
        entries.assign(1, { 0, rleSym, 0 });
    }
};

bool ReadZstdFseTableHeader(
    const uint8_t* src,
    size_t srcLen,
    uint32_t maxSymAllowed,
    uint32_t maxAccLog,
    ZstdFseTable& outTable,
    size_t& outHeaderBytes)
{
    if (!src || srcLen == 0) return false;
    size_t bitPos = 0;
    auto peekBitsLSB = [&](int n) -> uint32_t {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            size_t idx = bitPos + i;
            size_t bOff = idx >> 3;
            if (bOff < srcLen) {
                v |= (static_cast<uint32_t>((src[bOff] >> (idx & 7)) & 1u) << i);
            }
        }
        return v;
    };

    uint32_t accLog = peekBitsLSB(4) + 5u;
    bitPos += 4;
    if (accLog > maxAccLog) return false;

    int32_t remaining = static_cast<int32_t>(1u << accLog) + 1;
    uint32_t threshold = 1u << accLog;
    int nbBits = static_cast<int>(accLog) + 1;

    int16_t norm[256] = {};
    uint32_t sym = 0;
    while (remaining > 1 && sym <= maxSymAllowed) {
        if ((bitPos >> 3) >= srcLen) return false;
        uint32_t maxSmall = (2u * threshold - 1u) - static_cast<uint32_t>(remaining);
        uint32_t low = peekBitsLSB(nbBits - 1);
        uint32_t count = 0;
        if (low < maxSmall) {
            count = low;
            bitPos += static_cast<size_t>(nbBits - 1);
        } else {
            count = peekBitsLSB(nbBits);
            if (count >= threshold) count -= maxSmall;
            bitPos += static_cast<size_t>(nbBits);
        }

        int32_t proba = static_cast<int32_t>(count) - 1;
        remaining -= (proba < 0) ? 1 : proba;
        if (remaining < 1) return false;
        norm[sym++] = static_cast<int16_t>(proba);

        if (proba == 0) {
            for (;;) {
                if ((bitPos >> 3) >= srcLen) return false;
                uint32_t repeat = peekBitsLSB(2);
                bitPos += 2;
                for (uint32_t r = 0; r < repeat && sym <= maxSymAllowed; ++r) {
                    norm[sym++] = 0;
                }
                if (repeat < 3u || sym > maxSymAllowed) break;
            }
        }

        while (remaining < static_cast<int32_t>(threshold) && threshold > 1u) {
            --nbBits;
            threshold >>= 1;
        }
    }

    if (remaining != 1 || sym == 0) return false;
    outHeaderBytes = (bitPos + 7u) >> 3;
    if (outHeaderBytes > srcLen) return false;
    return outTable.BuildFromNormalized(norm, sym, accLog);
}

struct ZstdHuffTable {
    uint32_t maxBits = 0;
    struct Entry {
        uint8_t sym = 0;
        uint8_t nbBits = 0;
    };
    std::vector<Entry> table;

    bool BuildFromWeights(const std::vector<uint8_t>& weightsWithoutLast) {
        if (weightsWithoutLast.empty() || weightsWithoutLast.size() >= 256) return false;
        uint32_t weightSum = 0;
        for (uint8_t w : weightsWithoutLast) {
            if (w > 11u) return false;
            if (w > 0) weightSum += (1u << (w - 1u));
        }
        if (weightSum == 0) return false;

        maxBits = HighestBit32(weightSum) + 1u;
        if (maxBits > 11u) return false;
        uint32_t totalSpace = 1u << maxBits;
        uint32_t leftOver = totalSpace - weightSum;
        if (leftOver == 0 || (leftOver & (leftOver - 1u)) != 0) return false;
        uint8_t lastWeight = static_cast<uint8_t>(HighestBit32(leftOver) + 1u);

        std::vector<uint8_t> weights = weightsWithoutLast;
        weights.push_back(lastWeight);

        table.assign(totalSpace, {});
        uint32_t idx = 0;
        for (uint32_t w = 1; w <= maxBits; ++w) {
            uint32_t span = 1u << (w - 1u);
            uint8_t nb = static_cast<uint8_t>(maxBits + 1u - w);
            for (size_t s = 0; s < weights.size(); ++s) {
                if (weights[s] == w) {
                    if (idx + span > totalSpace) return false;
                    for (uint32_t k = 0; k < span; ++k) {
                        table[idx + k] = { static_cast<uint8_t>(s), nb };
                    }
                    idx += span;
                }
            }
        }
        return idx == totalSpace;
    }

    bool DecodeSingleStream(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen) const {
        if (dstLen == 0) return true;
        if (maxBits == 0 || table.empty()) return false;
        ZstdRevBitStream bs;
        if (!bs.Init(src, srcLen)) return false;
        for (size_t i = 0; i < dstLen; ++i) {
            if (bs.bitPos <= 0 && i < dstLen) return false;
            uint32_t peek = bs.PeekBits(static_cast<int>(maxBits));
            const auto& e = table[peek];
            if (e.nbBits == 0) return false;
            bs.Consume(e.nbBits);
            dst[i] = e.sym;
        }
        return bs.bitPos == 0;
    }
};

bool ParseZstdHuffTree(const uint8_t* src, size_t srcLen, ZstdHuffTable& outHuff, size_t& outTreeBytes) {
    if (!src || srcLen < 2) return false;
    uint8_t h0 = src[0];
    std::vector<uint8_t> weights;
    if (h0 >= 128u) {
        size_t numSyms = static_cast<size_t>(h0 - 127u);
        size_t weightBytes = (numSyms + 1u) / 2u;
        if (1u + weightBytes > srcLen) return false;
        weights.resize(numSyms);
        for (size_t i = 0; i < numSyms; ++i) {
            uint8_t b = src[1u + (i >> 1)];
            weights[i] = ((i & 1u) == 0) ? (b >> 4) : (b & 0x0Fu);
        }
        outTreeBytes = 1u + weightBytes;
    } else {
        size_t compSize = h0;
        if (compSize == 0 || 1u + compSize > srcLen) return false;
        ZstdFseTable fseWeights;
        size_t fseHdrBytes = 0;
        if (!ReadZstdFseTableHeader(src + 1, compSize, 12, 6, fseWeights, fseHdrBytes)) return false;
        if (fseHdrBytes >= compSize) return false;

        ZstdRevBitStream bs;
        if (!bs.Init(src + 1 + fseHdrBytes, compSize - fseHdrBytes)) return false;
        uint32_t state1 = bs.ReadBits(static_cast<int>(fseWeights.accuracyLog));
        uint32_t state2 = bs.ReadBits(static_cast<int>(fseWeights.accuracyLog));

        while (weights.size() < 255) {
            const auto& e1 = fseWeights.entries[state1];
            weights.push_back(e1.sym);
            if (bs.bitPos < static_cast<int64_t>(e1.nbBits)) break;
            state1 = e1.newStateBase + bs.ReadBits(e1.nbBits);

            if (weights.size() >= 255) break;
            const auto& e2 = fseWeights.entries[state2];
            weights.push_back(e2.sym);
            if (bs.bitPos < static_cast<int64_t>(e2.nbBits)) break;
            state2 = e2.newStateBase + bs.ReadBits(e2.nbBits);
        }
        outTreeBytes = 1u + compSize;
    }
    return outHuff.BuildFromWeights(weights);
}

const int16_t kZstdDefaultLLNorm[36] = {
    4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1,
   -1,-1,-1,-1
};
const int16_t kZstdDefaultOFNorm[29] = {
    1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,-1,-1,-1,-1,-1
};
const int16_t kZstdDefaultMLNorm[53] = {
    1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,-1,-1,
   -1,-1,-1,-1,-1
};

const uint8_t kZstdLLBits[36] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,2,2,3,3,4,6,7,8,9,10,11,12,
    13,14,15,16
};
const uint32_t kZstdLLBase[36] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
    16,18,20,22,24,28,32,40,48,64,128,256,512,1024,2048,4096,
    8192,16384,32768,65536
};

const uint8_t kZstdMLBits[53] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,2,2,3,3,4,4,5,7,8,9,10,11,
    12,13,14,15,16
};
const uint32_t kZstdMLBase[53] = {
    3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,
    19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,
    35,37,39,41,43,47,51,59,67,83,99,131,259,515,1027,2051,
    4099,8195,16387,32771,65539
};

} // namespace

bool DecompressZstdFrame(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t maxOut) {
    out.clear();
    if (!data || size < 8) return false;

    size_t pos = 0;
    bool decodedAny = false;

    while (pos + 4 <= size) {
        uint32_t magic = ReadU32LE(data + pos);
        if ((magic & 0xFFFFFFF0u) == 0x184D2A50u) {
            if (pos + 8 > size) return false;
            uint32_t skipSz = ReadU32LE(data + pos + 4);
            if (pos + 8ULL + skipSz > size) return false;
            pos += 8 + static_cast<size_t>(skipSz);
            decodedAny = true;
            continue;
        }
        if (magic != 0xFD2FB528u) {
            if (decodedAny) break;
            return false;
        }
        pos += 4;
        if (pos >= size) return false;

        uint8_t fhd = data[pos++];
        if ((fhd & 0x18u) != 0) return false; // Reserved / unused bit checks
        uint32_t fcsFlag = (fhd >> 6) & 3u;
        bool singleSeg   = (fhd & 0x20u) != 0;
        bool hasCrc      = (fhd & 0x04u) != 0;
        uint32_t didFlag = fhd & 3u;

        if (!singleSeg) {
            if (pos >= size) return false;
            ++pos; // Window_Descriptor
        }
        static const size_t kDidBytes[4] = { 0, 1, 2, 4 };
        if (pos + kDidBytes[didFlag] > size) return false;
        pos += kDidBytes[didFlag];

        uint64_t contentSize = 0;
        bool hasContentSize = false;
        if (fcsFlag == 0 && singleSeg) {
            if (pos + 1 > size) return false;
            contentSize = data[pos++];
            hasContentSize = true;
        } else if (fcsFlag == 1) {
            if (pos + 2 > size) return false;
            contentSize = static_cast<uint64_t>(ReadU16LE(data + pos)) + 256ULL;
            pos += 2;
            hasContentSize = true;
        } else if (fcsFlag == 2) {
            if (pos + 4 > size) return false;
            contentSize = ReadU32LE(data + pos);
            pos += 4;
            hasContentSize = true;
        } else if (fcsFlag == 3) {
            if (pos + 8 > size) return false;
            contentSize = ReadU64LE(data + pos);
            pos += 8;
            hasContentSize = true;
        }

        if (hasContentSize && contentSize <= maxOut) {
            out.reserve(out.size() + static_cast<size_t>(std::min<uint64_t>(contentSize, 4 * 1024 * 1024)));
        }

        const size_t frameBase = out.size();
        uint32_t rep[3] = { 1u, 4u, 8u };
        ZstdHuffTable prevHuff;
        bool hasPrevHuff = false;
        ZstdFseTable prevLL, prevOF, prevML;
        bool hasPrevLL = false, hasPrevOF = false, hasPrevML = false;

        bool lastBlock = false;
        while (!lastBlock) {
            if (pos + 3 > size) return false;
            uint32_t bh = static_cast<uint32_t>(data[pos]) |
                          (static_cast<uint32_t>(data[pos + 1]) << 8) |
                          (static_cast<uint32_t>(data[pos + 2]) << 16);
            pos += 3;
            lastBlock = (bh & 1u) != 0;
            uint32_t blockType = (bh >> 1) & 3u;
            uint32_t blockSize = bh >> 3;

            if (blockType == 0u) {
                // Raw_Block
                if (pos + blockSize > size || out.size() + blockSize > maxOut) return false;
                out.insert(out.end(), data + pos, data + pos + blockSize);
                pos += blockSize;
            } else if (blockType == 1u) {
                // RLE_Block
                if (pos >= size || out.size() + blockSize > maxOut) return false;
                uint8_t rleByte = data[pos++];
                out.insert(out.end(), blockSize, rleByte);
            } else if (blockType == 2u) {
                // Compressed_Block
                if (pos + blockSize > size || blockSize < 2) return false;
                const uint8_t* blk = data + pos;
                size_t blkLen = blockSize;
                pos += blockSize;

                // 1. Literals Section
                uint32_t litType = blk[0] & 3u;
                uint32_t sizeFmt = (blk[0] >> 2) & 3u;
                std::vector<uint8_t> literals;
                size_t bpos = 0;

                if (litType == 0u || litType == 1u) {
                    size_t regenSize = 0;
                    if (sizeFmt == 0u || sizeFmt == 2u) {
                        regenSize = blk[0] >> 3;
                        bpos = 1;
                    } else if (sizeFmt == 1u) {
                        if (blkLen < 2) return false;
                        regenSize = ((static_cast<size_t>(blk[0]) >> 4) | (static_cast<size_t>(blk[1]) << 4)) & 0xFFFu;
                        bpos = 2;
                    } else {
                        if (blkLen < 3) return false;
                        regenSize = (static_cast<size_t>(blk[0]) >> 4) |
                                    (static_cast<size_t>(blk[1]) << 4) |
                                    (static_cast<size_t>(blk[2]) << 12);
                        bpos = 3;
                    }
                    if (regenSize > maxOut) return false;
                    if (litType == 0u) {
                        if (bpos + regenSize > blkLen) return false;
                        literals.assign(blk + bpos, blk + bpos + regenSize);
                        bpos += regenSize;
                    } else {
                        if (bpos + 1 > blkLen) return false;
                        literals.assign(regenSize, blk[bpos]);
                        bpos += 1;
                    }
                } else {
                    size_t regenSize = 0, compSize = 0;
                    int numStreams = 4;
                    if (sizeFmt == 0u || sizeFmt == 1u) {
                        if (blkLen < 3) return false;
                        uint32_t h = blk[0] | (static_cast<uint32_t>(blk[1]) << 8) | (static_cast<uint32_t>(blk[2]) << 16);
                        numStreams = (sizeFmt == 0u) ? 1 : 4;
                        regenSize = (h >> 4) & 0x3FFu;
                        compSize  = (h >> 14) & 0x3FFu;
                        bpos = 3;
                    } else if (sizeFmt == 2u) {
                        if (blkLen < 4) return false;
                        uint32_t h = ReadU32LE(blk);
                        regenSize = (h >> 4) & 0x3FFFu;
                        compSize  = (h >> 18) & 0x3FFFu;
                        bpos = 4;
                    } else {
                        if (blkLen < 5) return false;
                        uint64_t h = static_cast<uint64_t>(ReadU32LE(blk)) | (static_cast<uint64_t>(blk[4]) << 32);
                        regenSize = static_cast<size_t>((h >> 4) & 0x3FFFFULL);
                        compSize  = static_cast<size_t>((h >> 22) & 0x3FFFFULL);
                        bpos = 5;
                    }
                    if (bpos + compSize > blkLen || regenSize > maxOut || compSize == 0) return false;
                    const uint8_t* huffSrc = blk + bpos;
                    size_t huffLen = compSize;
                    bpos += compSize;

                    size_t treeBytes = 0;
                    if (litType == 2u) {
                        if (!ParseZstdHuffTree(huffSrc, huffLen, prevHuff, treeBytes)) return false;
                        hasPrevHuff = true;
                    } else if (!hasPrevHuff) {
                        return false;
                    }
                    if (treeBytes >= huffLen) return false;
                    const uint8_t* streamSrc = huffSrc + treeBytes;
                    size_t streamLen = huffLen - treeBytes;
                    literals.resize(regenSize);

                    if (numStreams == 1) {
                        if (!prevHuff.DecodeSingleStream(streamSrc, streamLen, literals.data(), regenSize)) return false;
                    } else {
                        if (streamLen < 6 || regenSize < 4) return false;
                        size_t c1 = ReadU16LE(streamSrc + 0);
                        size_t c2 = ReadU16LE(streamSrc + 2);
                        size_t c3 = ReadU16LE(streamSrc + 4);
                        if (6 + c1 + c2 + c3 >= streamLen) return false;
                        size_t c4 = streamLen - 6 - c1 - c2 - c3;
                        size_t seg = (regenSize + 3u) / 4u;
                        if (seg * 3u > regenSize) return false;
                        size_t seg4 = regenSize - seg * 3u;
                        if (!prevHuff.DecodeSingleStream(streamSrc + 6, c1, literals.data(), seg) ||
                            !prevHuff.DecodeSingleStream(streamSrc + 6 + c1, c2, literals.data() + seg, seg) ||
                            !prevHuff.DecodeSingleStream(streamSrc + 6 + c1 + c2, c3, literals.data() + seg * 2, seg) ||
                            !prevHuff.DecodeSingleStream(streamSrc + 6 + c1 + c2 + c3, c4, literals.data() + seg * 3, seg4))
                        {
                            return false;
                        }
                    }
                }

                // 2. Sequences Section
                if (bpos >= blkLen) return false;
                uint32_t numSeq = blk[bpos++];
                if (numSeq >= 128u && numSeq < 255u) {
                    if (bpos >= blkLen) return false;
                    numSeq = ((numSeq - 128u) << 8) + blk[bpos++];
                } else if (numSeq == 255u) {
                    if (bpos + 2 > blkLen) return false;
                    numSeq = static_cast<uint32_t>(blk[bpos]) + (static_cast<uint32_t>(blk[bpos + 1]) << 8) + 0x7F00u;
                    bpos += 2;
                }

                if (numSeq == 0u) {
                    if (bpos != blkLen) return false;
                    if (out.size() + literals.size() > maxOut) return false;
                    out.insert(out.end(), literals.begin(), literals.end());
                    continue;
                }

                if (bpos >= blkLen) return false;
                uint8_t modes = blk[bpos++];
                if ((modes & 3u) != 0) return false;

                auto resolveModeTable = [&](uint32_t mode, ZstdFseTable& tbl, bool& hasPrev,
                                            const int16_t* defNorm, uint32_t defSyms, uint32_t defLog,
                                            uint32_t maxSym, uint32_t maxLog) -> bool
                {
                    if (mode == 0u) {
                        hasPrev = tbl.BuildFromNormalized(defNorm, defSyms, defLog);
                        return hasPrev;
                    } else if (mode == 1u) {
                        if (bpos >= blkLen || blk[bpos] > maxSym) return false;
                        tbl.BuildRle(blk[bpos++]);
                        hasPrev = true;
                        return true;
                    } else if (mode == 2u) {
                        size_t hdrBytes = 0;
                        if (!ReadZstdFseTableHeader(blk + bpos, blkLen - bpos, maxSym, maxLog, tbl, hdrBytes)) return false;
                        bpos += hdrBytes;
                        hasPrev = true;
                        return true;
                    } else {
                        return hasPrev;
                    }
                };

                if (!resolveModeTable((modes >> 6) & 3u, prevLL, hasPrevLL, kZstdDefaultLLNorm, 36, 6, 35, 9) ||
                    !resolveModeTable((modes >> 4) & 3u, prevOF, hasPrevOF, kZstdDefaultOFNorm, 29, 5, 31, 8) ||
                    !resolveModeTable((modes >> 2) & 3u, prevML, hasPrevML, kZstdDefaultMLNorm, 53, 6, 52, 9))
                {
                    return false;
                }

                if (bpos >= blkLen) return false;
                ZstdRevBitStream seqBs;
                if (!seqBs.Init(blk + bpos, blkLen - bpos)) return false;

                uint32_t stateLL = seqBs.ReadBits(static_cast<int>(prevLL.accuracyLog));
                uint32_t stateOF = seqBs.ReadBits(static_cast<int>(prevOF.accuracyLog));
                uint32_t stateML = seqBs.ReadBits(static_cast<int>(prevML.accuracyLog));

                size_t litPos = 0;
                for (uint32_t s = 0; s < numSeq; ++s) {
                    const auto& eLL = prevLL.entries[stateLL];
                    const auto& eOF = prevOF.entries[stateOF];
                    const auto& eML = prevML.entries[stateML];
                    if (eLL.sym > 35u || eOF.sym > 31u || eML.sym > 52u) return false;

                    // RFC 8878 §3.1.1.3.2: read extra bits in order OF, ML, LL
                    uint32_t ofExtra = seqBs.ReadBits(eOF.sym);
                    uint32_t mlExtra = seqBs.ReadBits(kZstdMLBits[eML.sym]);
                    uint32_t llExtra = seqBs.ReadBits(kZstdLLBits[eLL.sym]);

                    uint32_t rawOffset = (1u << eOF.sym) + ofExtra;
                    uint32_t matchLen  = kZstdMLBase[eML.sym] + mlExtra;
                    uint32_t litLen    = kZstdLLBase[eLL.sym] + llExtra;

                    uint32_t actualOffset = 0;
                    if (rawOffset > 3u) {
                        actualOffset = rawOffset - 3u;
                        rep[2] = rep[1];
                        rep[1] = rep[0];
                        rep[0] = actualOffset;
                    } else {
                        if (litLen == 0u) {
                            if (rawOffset == 3u) {
                                if (rep[0] <= 1u) return false;
                                actualOffset = rep[0] - 1u;
                                rep[2] = rep[1];
                                rep[1] = rep[0];
                                rep[0] = actualOffset;
                            } else if (rawOffset == 2u) {
                                actualOffset = rep[2];
                                rep[2] = rep[1];
                                rep[1] = rep[0];
                                rep[0] = actualOffset;
                            } else {
                                actualOffset = rep[1];
                                rep[1] = rep[0];
                                rep[0] = actualOffset;
                            }
                        } else {
                            uint32_t idx = rawOffset - 1u;
                            actualOffset = rep[idx];
                            if (idx == 2u) {
                                rep[2] = rep[1];
                                rep[1] = rep[0];
                                rep[0] = actualOffset;
                            } else if (idx == 1u) {
                                rep[1] = rep[0];
                                rep[0] = actualOffset;
                            }
                        }
                    }

                    if (litPos + litLen > literals.size()) return false;
                    if (out.size() + litLen + matchLen > maxOut) return false;
                    out.insert(out.end(), literals.data() + litPos, literals.data() + litPos + litLen);
                    litPos += litLen;

                    if (actualOffset == 0u || out.size() - frameBase < actualOffset) return false;
                    for (uint32_t k = 0; k < matchLen; ++k) {
                        uint8_t b = out[out.size() - actualOffset];
                        out.push_back(b);
                    }

                    // RFC 8878 §3.1.1.3.2: update FSE states in order LL, ML, OF
                    if (s + 1u < numSeq) {
                        stateLL = eLL.newStateBase + seqBs.ReadBits(eLL.nbBits);
                        stateML = eML.newStateBase + seqBs.ReadBits(eML.nbBits);
                        stateOF = eOF.newStateBase + seqBs.ReadBits(eOF.nbBits);
                    }
                }

                if (seqBs.bitPos < 0) return false;
                if (litPos < literals.size()) {
                    size_t remLit = literals.size() - litPos;
                    if (out.size() + remLit > maxOut) return false;
                    out.insert(out.end(), literals.data() + litPos, literals.data() + literals.size());
                }
            } else {
                return false;
            }
        }

        if (hasCrc) {
            if (pos + 4 > size) return false;
            pos += 4;
        }
        decodedAny = true;
    }

    return decodedAny;
}

// ----------------------------------------------------------------------------
// In-Memory POSIX TAR ("ustar" / GNU LongLink / PAX) Reader & Extractor
// ----------------------------------------------------------------------------
namespace {
uint64_t ParseTarOctalOrBase256(const uint8_t* p, size_t fieldLen) {
    if (fieldLen > 0 && (p[0] & 0x80u) != 0) {
        uint64_t v = p[0] & 0x7Fu;
        for (size_t i = 1; i < fieldLen; ++i) v = (v << 8) | p[i];
        return v;
    }
    uint64_t v = 0;
    size_t i = 0;
    while (i < fieldLen && (p[i] == ' ' || p[i] == 0)) ++i;
    for (; i < fieldLen; ++i) {
        if (p[i] < '0' || p[i] > '7') break;
        v = (v << 3) | static_cast<uint64_t>(p[i] - '0');
    }
    return v;
}
} // namespace

bool ParsePosixTarFromMemory(
    const uint8_t* buf,
    size_t len,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (!buf || len < 512) return false;

    auto looksLikeTarHeader = [](const uint8_t* h) -> bool {
        if (std::memcmp(h + 257, "ustar", 5) == 0) return true;
        // Also accept classic V7 tar headers whose header checksum matches
        uint32_t stored = static_cast<uint32_t>(ParseTarOctalOrBase256(h + 148, 8));
        if (stored == 0) return false;
        uint32_t unsignedSum = 0;
        int32_t signedSum = 0;
        for (size_t i = 0; i < 512; ++i) {
            uint8_t b = (i >= 148 && i < 156) ? static_cast<uint8_t>(' ') : h[i];
            unsignedSum += b;
            signedSum += static_cast<int8_t>(b);
        }
        return (stored == unsignedSum || static_cast<int32_t>(stored) == signedSum) && h[0] != 0;
    };

    if (!looksLikeTarHeader(buf)) return false;

    size_t pos = 0;
    std::string pendingLongName;
    size_t entriesAdded = 0;

    while (pos + 512 <= len) {
        const uint8_t* hdr = buf + pos;
        bool allZero = true;
        for (size_t i = 0; i < 512; ++i) {
            if (hdr[i] != 0) { allZero = false; break; }
        }
        if (allZero) break;
        if (!looksLikeTarHeader(hdr)) break;

        uint64_t fileSz = ParseTarOctalOrBase256(hdr + 124, 12);
        uint8_t typeFlag = hdr[156];
        size_t dataPos = pos + 512;
        uint64_t paddedSz = (fileSz + 511ULL) & ~511ULL;
        if (dataPos + fileSz > len) break;

        if (typeFlag == 'L') {
            // GNU @LongLink
            size_t sLen = static_cast<size_t>(std::min<uint64_t>(fileSz, 4096));
            while (sLen > 0 && buf[dataPos + sLen - 1] == 0) --sLen;
            pendingLongName.assign(reinterpret_cast<const char*>(buf + dataPos), sLen);
        } else if (typeFlag == 'x' || typeFlag == 'g') {
            // PAX extended header: extract "path=" if present
            std::string_view pax(reinterpret_cast<const char*>(buf + dataPos),
                                 static_cast<size_t>(std::min<uint64_t>(fileSz, 16384)));
            size_t pPos = pax.find(" path=");
            if (pPos != std::string_view::npos) {
                size_t nl = pax.find('\n', pPos + 6);
                if (nl != std::string_view::npos && typeFlag == 'x') {
                    pendingLongName.assign(pax.substr(pPos + 6, nl - (pPos + 6)));
                }
            }
        } else {
            std::string fullUtf8;
            if (!pendingLongName.empty()) {
                fullUtf8 = std::move(pendingLongName);
                pendingLongName.clear();
            } else {
                size_t nLen = 0;
                while (nLen < 100 && hdr[nLen] != 0) ++nLen;
                size_t pLen = 0;
                if (std::memcmp(hdr + 257, "ustar", 5) == 0) {
                    while (pLen < 155 && hdr[345 + pLen] != 0) ++pLen;
                }
                if (pLen > 0) {
                    fullUtf8.assign(reinterpret_cast<const char*>(hdr + 345), pLen);
                    if (fullUtf8.back() != '/') fullUtf8.push_back('/');
                }
                fullUtf8.append(reinterpret_cast<const char*>(hdr), nLen);
            }

            bool isDir = (typeFlag == '5') || (!fullUtf8.empty() && fullUtf8.back() == '/');
            std::wstring normPath = ArchivePath::Normalize(PathUtils::Utf8ToWide(fullUtf8));
            if (!normPath.empty()) {
                if (extractTargetNorm && outExtracted) {
                    if (!isDir && _wcsicmp(normPath.c_str(), extractTargetNorm->c_str()) == 0) {
                        outExtracted->assign(buf + dataPos, buf + dataPos + static_cast<size_t>(fileSz));
                        return true;
                    }
                } else {
                    ArchiveItemEntry item;
                    item.pathInArchive = normPath;
                    item.name = ArchivePath::BaseName(normPath);
                    item.isDirectory = isDir;
                    item.uncompressedSize = isDir ? 0 : fileSz;
                    item.compressedSize = item.uncompressedSize;
                    item.rawDataOffset = dataPos;
                    item.compressionMethod = 0;
                    outInfo.entries.push_back(std::move(item));
                    ++entriesAdded;
                }
            }
        }

        if (dataPos + paddedSz > len || dataPos + paddedSz < pos) break;
        pos = dataPos + static_cast<size_t>(paddedSz);
    }

    if (extractTargetNorm) return false;
    outInfo.valid = (entriesAdded > 0);
    return outInfo.valid;
}

// ----------------------------------------------------------------------------
// Native Windows Cabinet (.cab) Extraction via Win32 SetupIterateCabinetW
// ----------------------------------------------------------------------------
static std::atomic<uint32_t> g_cabTempCounter{ 1 };

std::wstring MakeUniqueCabScratchPath(const wchar_t* ext) {
    wchar_t tmpDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmpDir);
    std::wstring dir = std::wstring(tmpDir) + L"Explorer2_CabScratch";
    CreateDirectoryW(dir.c_str(), nullptr);
    uint32_t seq = g_cabTempCounter.fetch_add(1, std::memory_order_relaxed);
    wchar_t buf[96] = {};
    swprintf_s(buf, L"\\cab_%lu_%llu_%u%s",
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long long>(GetTickCount64()),
        seq,
        ext);
    return dir + buf;
}

namespace {

struct CabSingleExtractCtx {
    std::wstring targetNorm;
    std::wstring scratchFile;
    std::vector<uint8_t>* outBytes = nullptr;
    bool extractedOk = false;
};

#ifdef _WIN32   // CAB extraction (SetupAPI): not part of the Linux build of the readers
UINT CALLBACK CabSingleIterateCallback(PVOID Context, UINT Notification, UINT_PTR Param1, UINT_PTR Param2) {
    (void)Param2;
    auto* ctx = static_cast<CabSingleExtractCtx*>(Context);
    switch (Notification) {
    case SPFILENOTIFY_CABINETINFO:
        return NO_ERROR;

    case SPFILENOTIFY_FILEINCABINET: {
        auto* info = reinterpret_cast<FILE_IN_CABINET_INFO_W*>(Param1);
        if (!info || !info->NameInCabinet || ctx->extractedOk) return FILEOP_SKIP;
        std::wstring entryNorm = ArchivePath::Normalize(info->NameInCabinet);
        if (_wcsicmp(entryNorm.c_str(), ctx->targetNorm.c_str()) == 0) {
            if (ctx->scratchFile.size() >= MAX_PATH) return FILEOP_ABORT;
            DeleteFileW(ctx->scratchFile.c_str());
            wcscpy_s(info->FullTargetName, MAX_PATH, ctx->scratchFile.c_str());
            return FILEOP_DOIT;
        }
        return FILEOP_SKIP;
    }

    case SPFILENOTIFY_FILEEXTRACTED: {
        auto* fp = reinterpret_cast<FILEPATHS_W*>(Param1);
        if (!fp || fp->Win32Error != NO_ERROR) {
            DeleteFileW(ctx->scratchFile.c_str());
            return fp ? fp->Win32Error : ERROR_GEN_FAILURE;
        }
        if (ctx->outBytes && PathUtils::ReadFileBytes(ctx->scratchFile, 256 * 1024 * 1024, *ctx->outBytes)) {
            ctx->extractedOk = true;
        }
        DeleteFileW(ctx->scratchFile.c_str());
        return NO_ERROR;
    }

    case SPFILENOTIFY_NEEDNEWCABINET:
        return ERROR_NOT_SUPPORTED;

    default:
        return NO_ERROR;
    }
}
#endif

bool ExtractCabEntryInMemoryPure(
    const uint8_t* cabPtr,
    size_t sz,
    const std::wstring& targetNorm,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (!cabPtr || sz < 36 || targetNorm.empty()) return false;
    if (std::memcmp(cabPtr, "MSCF\0\0\0\0", 8) != 0) return false;

    auto rd16 = [](const uint8_t* p) -> uint16_t { return static_cast<uint16_t>(p[0] | (p[1] << 8)); };
    auto rd32 = [](const uint8_t* p) -> uint32_t {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    };
    auto cabCsum = [](const uint8_t* pv, size_t cb, uint32_t seed) -> uint32_t {
        uint32_t csum = seed;
        size_t words = cb / 4;
        for (size_t i = 0; i < words; ++i) {
            csum ^= static_cast<uint32_t>(pv[0]) | (static_cast<uint32_t>(pv[1]) << 8) |
                    (static_cast<uint32_t>(pv[2]) << 16) | (static_cast<uint32_t>(pv[3]) << 24);
            pv += 4;
        }
        uint32_t rem = 0;
        switch (cb & 3u) {
        case 3: rem |= (static_cast<uint32_t>(*pv++) << 16); [[fallthrough]];
        case 2: rem |= (static_cast<uint32_t>(*pv++) << 8);  [[fallthrough]];
        case 1: rem |= static_cast<uint32_t>(*pv++); break;
        default: break;
        }
        return csum ^ rem;
    };

    const uint32_t coffFiles = rd32(cabPtr + 16);
    const uint16_t cFolders  = rd16(cabPtr + 26);
    const uint16_t cFiles    = rd16(cabPtr + 28);
    const uint16_t flags     = rd16(cabPtr + 30);
    if (cFolders == 0 || cFiles == 0 || coffFiles >= sz) return false;
    if ((flags & 0x0003u) != 0) return false; // Multi-volume spanning cabinets unsupported

    size_t hdrPos = 36;
    uint16_t cbCFHeader = 0;
    uint8_t cbCFFolder = 0, cbCFData = 0;
    if (flags & 0x0004u) {
        if (hdrPos + 4 > sz) return false;
        cbCFHeader = rd16(cabPtr + hdrPos);
        cbCFFolder = cabPtr[hdrPos + 2];
        cbCFData   = cabPtr[hdrPos + 3];
        if (4u + cbCFHeader > sz - hdrPos) return false;
        hdrPos += 4u + cbCFHeader;
    }
    if (hdrPos > sz) return false;

    struct CabFolderRec { uint32_t coffCabStart; uint16_t cCFData; uint16_t typeCompress; };
    std::vector<CabFolderRec> folders;
    folders.reserve(cFolders);
    for (uint16_t i = 0; i < cFolders; ++i) {
        if (8u + cbCFFolder > sz - hdrPos) return false;
        folders.push_back({ rd32(cabPtr + hdrPos), rd16(cabPtr + hdrPos + 4), rd16(cabPtr + hdrPos + 6) });
        hdrPos += 8u + cbCFFolder;
    }

    const std::wstring wantedNorm = ArchivePath::Normalize(targetNorm);
    bool found = false;
    uint32_t targetSize = 0, targetOffset = 0;
    uint16_t targetFolder = 0;

    size_t fpos = coffFiles;
    for (uint16_t i = 0; i < cFiles && fpos + 16 < sz; ++i) {
        const uint32_t cbFile = rd32(cabPtr + fpos);
        const uint32_t uoff   = rd32(cabPtr + fpos + 4);
        const uint16_t iFold  = rd16(cabPtr + fpos + 8);
        const uint16_t attr   = rd16(cabPtr + fpos + 14);
        fpos += 16;
        size_t nameStart = fpos;
        while (fpos < sz && cabPtr[fpos] != 0) ++fpos;
        if (fpos >= sz) return false;
        std::string rawName(reinterpret_cast<const char*>(cabPtr + nameStart), fpos - nameStart);
        ++fpos;

        std::wstring wName;
        if (attr & 0x0080u) {
            wName = PathUtils::Utf8ToWide(rawName);
        } else {
            int wl = MultiByteToWideChar(CP_ACP, 0, rawName.data(), static_cast<int>(rawName.size()), nullptr, 0);
            if (wl > 0) {
                wName.resize(static_cast<size_t>(wl));
                MultiByteToWideChar(CP_ACP, 0, rawName.data(), static_cast<int>(rawName.size()), wName.data(), wl);
            } else {
                wName = PathUtils::Utf8ToWide(rawName);
            }
        }
        std::wstring entryNorm = ArchivePath::Normalize(wName);
        if (_wcsicmp(entryNorm.c_str(), wantedNorm.c_str()) == 0) {
            if (iFold >= folders.size()) return false;
            found = true;
            targetSize = cbFile;
            targetOffset = uoff;
            targetFolder = iFold;
            break;
        }
    }
    if (!found) return false;
    if (targetSize == 0) return true;
    const uint64_t needEnd = static_cast<uint64_t>(targetOffset) + targetSize;
    if (needEnd > 256ULL * 1024 * 1024) return false;

    const auto& fld = folders[targetFolder];
    if (fld.coffCabStart >= sz || fld.cCFData == 0) return false;
    const uint16_t compKind = fld.typeCompress & 0x000Fu;

    std::vector<uint8_t> folderUncomp;
    size_t dpos = fld.coffCabStart;

    if (compKind == 0u || compKind == 1u) {
        folderUncomp.reserve(static_cast<size_t>(needEnd) + 32768u);
        for (uint16_t b = 0; b < fld.cCFData && folderUncomp.size() < needEnd; ++b) {
            if (8u + cbCFData > sz - dpos) return false;
            const uint32_t csum     = rd32(cabPtr + dpos);
            const uint16_t cbData   = rd16(cabPtr + dpos + 4);
            const uint16_t cbUncomp = rd16(cabPtr + dpos + 6);
            if (cbData == 0 || cbUncomp == 0 || cbUncomp > 32768u) return false;
            if (8u + cbCFData + cbData > sz - dpos) return false;
            if (csum != 0) {
                uint32_t calc = cabCsum(cabPtr + dpos + 4, 4, cabCsum(cabPtr + dpos + 8, cbCFData + cbData, 0));
                if (calc != csum) return false;
            }
            const uint8_t* payload = cabPtr + dpos + 8 + cbCFData;
            dpos += 8u + cbCFData + cbData;

            if (compKind == 0u) {
                if (cbData != cbUncomp) return false;
                folderUncomp.insert(folderUncomp.end(), payload, payload + cbData);
            } else {
                if (cbData < 2 || payload[0] != 'C' || payload[1] != 'K') return false;
                const size_t before = folderUncomp.size();
                if (!ZipDeflate::InflateRawAppend(payload + 2, cbData - 2, folderUncomp, static_cast<size_t>(needEnd) + 65536u)) {
                    return false;
                }
                if (folderUncomp.size() - before != cbUncomp) return false;
            }
        }
    } else if (compKind == 3u) {
        const int winBits = static_cast<int>((fld.typeCompress >> 8) & 0x1Fu);
        std::vector<uint8_t> lzxStream;
        size_t totalBlockUncomp = 0;
        for (uint16_t b = 0; b < fld.cCFData && totalBlockUncomp < needEnd; ++b) {
            if (8u + cbCFData > sz - dpos) return false;
            const uint32_t csum     = rd32(cabPtr + dpos);
            const uint16_t cbData   = rd16(cabPtr + dpos + 4);
            const uint16_t cbUncomp = rd16(cabPtr + dpos + 6);
            if (cbData == 0 || 8u + cbCFData + cbData > sz - dpos) return false;
            if (csum != 0) {
                uint32_t calc = cabCsum(cabPtr + dpos + 4, 4, cabCsum(cabPtr + dpos + 8, cbCFData + cbData, 0));
                if (calc != csum) return false;
            }
            const uint8_t* payload = cabPtr + dpos + 8 + cbCFData;
            lzxStream.insert(lzxStream.end(), payload, payload + cbData);
            totalBlockUncomp += cbUncomp;
            dpos += 8u + cbCFData + cbData;
        }
        Lzx::Decoder dec(lzxStream.data(), lzxStream.size(), winBits, 0, {});
        if (!dec.Decode(totalBlockUncomp, folderUncomp)) return false;
    } else {
        return false;
    }

    if (folderUncomp.size() < needEnd) return false;
    outBytes.assign(folderUncomp.data() + targetOffset, folderUncomp.data() + needEnd);
    return true;
}

} // namespace

bool ExtractCabEntryFromFile(
    const std::wstring& cabFilePath,
    const std::wstring& targetNorm,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (cabFilePath.empty() || targetNorm.empty()) return false;
    std::vector<uint8_t> cab;
    if (PathUtils::ReadFileBytes(cabFilePath, 256 * 1024 * 1024, cab) &&
        ExtractCabEntryInMemoryPure(cab.data(), cab.size(), targetNorm, outBytes))
    {
        return true;
    }
#ifdef _WIN32
    CabSingleExtractCtx ctx;
    ctx.targetNorm = ArchivePath::Normalize(targetNorm);
    ctx.scratchFile = MakeUniqueCabScratchPath(L".tmp");
    ctx.outBytes = &outBytes;

    BOOL ok = SetupIterateCabinetW(cabFilePath.c_str(), 0, CabSingleIterateCallback, &ctx);
    DeleteFileW(ctx.scratchFile.c_str());
    return (ok != FALSE) && ctx.extractedOk;
#else
    return false;
#endif
}

bool ExtractCabEntryFromMemory(
    const uint8_t* cabBytes,
    size_t cabSize,
    const std::wstring& targetNorm,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (!cabBytes || cabSize < 36 || std::memcmp(cabBytes, "MSCF\0\0\0\0", 8) != 0) return false;
    if (ExtractCabEntryInMemoryPure(cabBytes, cabSize, targetNorm, outBytes)) {
        return true;
    }
#ifdef _WIN32
    std::wstring tempCab = MakeUniqueCabScratchPath(L".cab");
    if (!PathUtils::WriteFileBytes(tempCab, cabBytes, cabSize)) return false;
    CabSingleExtractCtx ctx;
    ctx.targetNorm = ArchivePath::Normalize(targetNorm);
    ctx.scratchFile = MakeUniqueCabScratchPath(L".tmp");
    ctx.outBytes = &outBytes;
    BOOL ok = SetupIterateCabinetW(tempCab.c_str(), 0, CabSingleIterateCallback, &ctx);
    DeleteFileW(ctx.scratchFile.c_str());
    DeleteFileW(tempCab.c_str());
    return (ok != FALSE) && ctx.extractedOk;
#else
    return false;
#endif
}

} // namespace StreamCodecs

} // namespace Explorer2

#include "BrotliStaticDict.h"
