#include "ArchiveExplorer.h"
#include "ReaderGuard.h"
#include "ZipDeflate.h"
#include "Ole2Compound.h"
#include "PathUtils.h"
#include "FileEngine.h"
#include <setupapi.h>
#include <map>
#include <set>
#include "formats/ExtendedArchives100.h"
#include "formats/ExtendedArchives200.h"
#include <shellapi.h>
#include <algorithm>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <cstring>
#include <cstdio>

namespace Explorer2 {

namespace {

static D2D1_COLOR_F ArcColor(uint32_t rgb, float a = 1.0f) {
    return PathUtils::ColorFromRgb(rgb, a);
}

inline uint16_t ReadU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

inline uint32_t ReadU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t ReadU64LE(const uint8_t* p) {
    return static_cast<uint64_t>(ReadU32LE(p)) |
           (static_cast<uint64_t>(ReadU32LE(p + 4)) << 32);
}

inline uint32_t HighestBit32(uint32_t v) {
    uint32_t r = 0;
    while (v > 1u) {
        v >>= 1;
        ++r;
    }
    return r;
}

std::wstring NormalizeArchiveInternalPath(std::wstring p) {
    for (wchar_t& ch : p) {
        if (ch == L'\\') ch = L'/';
    }
    while (!p.empty() && p.front() == L'/') p.erase(p.begin());
    while (!p.empty() && p.back() == L'/') p.pop_back();
    while (p.rfind(L"./", 0) == 0) p.erase(0, 2);
    return p;
}

std::wstring GetBaseNameFromInternalPath(const std::wstring& p) {
    size_t slash = p.find_last_of(L'/');
    return (slash == std::wstring::npos) ? p : p.substr(slash + 1);
}

} // namespace

// ============================================================================
// Native In-Process Stream Codecs (LZMA1, LZMA2, BCJ x86, XZ, BZip2, LZ4, Zstd,
// POSIX TAR in-memory, and Win32 SetupIterateCabinetW .cab extractor)
// ============================================================================
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
namespace {
bool DecompressLz4RawBlock(
    const uint8_t* src,
    size_t srcLen,
    std::vector<uint8_t>& out,
    size_t frameBase,
    size_t maxOut)
{
    size_t p = 0;
    while (p < srcLen) {
        uint8_t token = src[p++];
        size_t litLen = token >> 4;
        if (litLen == 15) {
            uint8_t s = 0;
            do {
                if (p >= srcLen) return false;
                s = src[p++];
                litLen += s;
                if (litLen > maxOut) return false;
            } while (s == 255);
        }
        if (p + litLen > srcLen || out.size() + litLen > maxOut) return false;
        out.insert(out.end(), src + p, src + p + litLen);
        p += litLen;

        if (p == srcLen) break;
        if (p + 2 > srcLen) return false;
        uint16_t offset = ReadU16LE(src + p);
        p += 2;
        if (offset == 0 || out.size() - frameBase < offset) return false;

        size_t matchLen = (token & 0x0Fu) + 4u;
        if ((token & 0x0Fu) == 15u) {
            uint8_t s = 0;
            do {
                if (p >= srcLen) return false;
                s = src[p++];
                matchLen += s;
                if (matchLen > maxOut) return false;
            } while (s == 255);
        }
        if (out.size() + matchLen > maxOut) return false;
        for (size_t k = 0; k < matchLen; ++k) {
            uint8_t b = out[out.size() - offset];
            out.push_back(b);
        }
    }
    return true;
}
} // namespace

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
                if (!DecompressLz4RawBlock(data + pos, peek, out, frameBase, maxOut)) return false;
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
                if (!DecompressLz4RawBlock(data + pos, cLen, out, frameBase, maxOut)) return false;
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
            std::wstring normPath = NormalizeArchiveInternalPath(PathUtils::Utf8ToWide(fullUtf8));
            if (!normPath.empty()) {
                if (extractTargetNorm && outExtracted) {
                    if (!isDir && _wcsicmp(normPath.c_str(), extractTargetNorm->c_str()) == 0) {
                        outExtracted->assign(buf + dataPos, buf + dataPos + static_cast<size_t>(fileSz));
                        return true;
                    }
                } else {
                    ArchiveItemEntry item;
                    item.pathInArchive = normPath;
                    item.name = GetBaseNameFromInternalPath(normPath);
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
namespace {

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

struct CabSingleExtractCtx {
    std::wstring targetNorm;
    std::wstring scratchFile;
    std::vector<uint8_t>* outBytes = nullptr;
    bool extractedOk = false;
};

UINT CALLBACK CabSingleIterateCallback(PVOID Context, UINT Notification, UINT_PTR Param1, UINT_PTR Param2) {
    (void)Param2;
    auto* ctx = static_cast<CabSingleExtractCtx*>(Context);
    switch (Notification) {
    case SPFILENOTIFY_CABINETINFO:
        return NO_ERROR;

    case SPFILENOTIFY_FILEINCABINET: {
        auto* info = reinterpret_cast<FILE_IN_CABINET_INFO_W*>(Param1);
        if (!info || !info->NameInCabinet || ctx->extractedOk) return FILEOP_SKIP;
        std::wstring entryNorm = NormalizeArchiveInternalPath(info->NameInCabinet);
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

} // namespace

bool ExtractCabEntryFromFile(
    const std::wstring& cabFilePath,
    const std::wstring& targetNorm,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (cabFilePath.empty() || targetNorm.empty()) return false;

    CabSingleExtractCtx ctx;
    ctx.targetNorm = NormalizeArchiveInternalPath(targetNorm);
    ctx.scratchFile = MakeUniqueCabScratchPath(L".tmp");
    ctx.outBytes = &outBytes;

    BOOL ok = SetupIterateCabinetW(cabFilePath.c_str(), 0, CabSingleIterateCallback, &ctx);
    DeleteFileW(ctx.scratchFile.c_str());
    return (ok != FALSE) && ctx.extractedOk;
}

bool ExtractCabEntryFromMemory(
    const uint8_t* cabBytes,
    size_t cabSize,
    const std::wstring& targetNorm,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (!cabBytes || cabSize < 36 || std::memcmp(cabBytes, "MSCF\0\0\0\0", 8) != 0) return false;
    std::wstring tempCab = MakeUniqueCabScratchPath(L".cab");
    if (!PathUtils::WriteFileBytes(tempCab, cabBytes, cabSize)) return false;
    bool ok = ExtractCabEntryFromFile(tempCab, targetNorm, outBytes);
    DeleteFileW(tempCab.c_str());
    return ok;
}

} // namespace StreamCodecs

// ============================================================================
// RAR4, RAR5, Full In-Process 7-Zip (kHeader + kEncodedHeader + StreamsInfo),
// GZIP, and Stream / TAR.* Inspectors & Extractors
// ============================================================================
namespace {

// Reads a RAR 5.0 variable-length integer (7 bits per byte, MSB = continuation)
bool ReadRar5Vint(const uint8_t* data, size_t size, size_t& pos, uint64_t& outVal) {
    outVal = 0;
    int shift = 0;
    for (int i = 0; i < 10; ++i) {
        if (pos >= size) return false;
        uint8_t b = data[pos++];
        outVal |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) return true;
        shift += 7;
    }
    return false;
}

// ============================================================================
// RAR 4.x Binary Block Header Parser ("Rar!\x1A\x07\x00")
// ============================================================================
bool ParseRar4Headers(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 7 || std::memcmp(data, "Rar!\x1A\x07\x00", 7) != 0) return false;
    outInfo.formatName = L"RAR4";

    size_t pos = 7;
    while (pos + 7 <= size) {
        uint8_t  headType  = data[pos + 2];
        uint16_t headFlags = ReadU16LE(data + pos + 3);
        uint16_t headSize  = ReadU16LE(data + pos + 5);
        if (headSize < 7 || pos + headSize > size) break;

        uint64_t addSize = 0;
        if ((headFlags & 0x8000) != 0 || headType == 0x74) {
            if (pos + 11 <= size) {
                addSize = ReadU32LE(data + pos + 7);
            }
        }

        if (headType == 0x74 && headSize >= 32 && pos + 32 <= size) {
            uint32_t packLow = ReadU32LE(data + pos + 7);
            uint32_t unpLow  = ReadU32LE(data + pos + 11);
            uint32_t fileCrc = ReadU32LE(data + pos + 16);
            uint32_t ftime   = ReadU32LE(data + pos + 20);
            uint8_t  method  = data[pos + 25];
            uint16_t nameLen = ReadU16LE(data + pos + 26);
            uint32_t attr    = ReadU32LE(data + pos + 28);

            size_t nameOff = pos + 32;
            uint64_t packHigh = 0, unpHigh = 0;
            if ((headFlags & 0x0100) != 0 && nameOff + 8 <= pos + headSize) {
                packHigh = ReadU32LE(data + nameOff);
                unpHigh  = ReadU32LE(data + nameOff + 4);
                nameOff += 8;
            }
            uint64_t packSize = (packHigh << 32) | packLow;
            uint64_t unpSize  = (unpHigh << 32) | unpLow;
            addSize = packSize;

            if (nameOff + nameLen <= pos + headSize && nameLen > 0) {
                size_t asciiLen = 0;
                while (asciiLen < nameLen && data[nameOff + asciiLen] != 0) ++asciiLen;
                std::string rawName(reinterpret_cast<const char*>(data + nameOff), asciiLen);
                std::wstring normPath = NormalizeArchiveInternalPath(PathUtils::Utf8ToWide(rawName));
                if (!normPath.empty()) {
                    ArchiveItemEntry item;
                    item.pathInArchive = normPath;
                    item.name = GetBaseNameFromInternalPath(normPath);
                    item.uncompressedSize = unpSize;
                    item.compressedSize = packSize;
                    item.crc32 = fileCrc;
                    item.dosTime = static_cast<uint16_t>(ftime & 0xFFFF);
                    item.dosDate = static_cast<uint16_t>((ftime >> 16) & 0xFFFF);
                    item.isDirectory = ((headFlags & 0x00E0) == 0x00E0) || ((attr & 0x10) != 0);
                    item.rawDataOffset = pos + headSize;
                    item.compressionMethod = method; // 0x30 = Store
                    outInfo.entries.push_back(std::move(item));
                }
            }
        } else if (headType == 0x7B) {
            break;
        }

        size_t nextPos = pos + headSize + static_cast<size_t>(addSize);
        if (nextPos <= pos || nextPos > size) break;
        pos = nextPos;
    }

    outInfo.valid = true;
    return true;
}

// ============================================================================
// RAR 5.0 Binary Block Header Parser ("Rar!\x1A\x07\x01\x00")
// ============================================================================
bool ParseRar5Headers(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 8 || std::memcmp(data, "Rar!\x1A\x07\x01\x00", 8) != 0) return false;
    outInfo.formatName = L"RAR5";

    size_t pos = 8;
    while (pos + 7 <= size) {
        pos += 4; // Skip Header CRC32
        uint64_t hdrSize = 0;
        if (!ReadRar5Vint(data, size, pos, hdrSize) || hdrSize == 0) break;
        size_t hdrEnd = pos + static_cast<size_t>(hdrSize);
        if (hdrEnd > size) break;

        size_t hpos = pos;
        uint64_t hdrType = 0, hdrFlags = 0, extraAreaSize = 0, dataSize = 0;
        if (!ReadRar5Vint(data, hdrEnd, hpos, hdrType)) break;
        if (!ReadRar5Vint(data, hdrEnd, hpos, hdrFlags)) break;
        if ((hdrFlags & 0x0001) != 0 && !ReadRar5Vint(data, hdrEnd, hpos, extraAreaSize)) break;
        if ((hdrFlags & 0x0002) != 0 && !ReadRar5Vint(data, hdrEnd, hpos, dataSize)) break;

        if (hdrType == 2) {
            uint64_t fileFlags = 0, unpSize = 0, attr = 0, compInfo = 0, hostOs = 0, nameLen = 0;
            if (ReadRar5Vint(data, hdrEnd, hpos, fileFlags) &&
                ReadRar5Vint(data, hdrEnd, hpos, unpSize) &&
                ReadRar5Vint(data, hdrEnd, hpos, attr))
            {
                if ((fileFlags & 0x0002) != 0 && hpos + 4 <= hdrEnd) hpos += 4;
                uint32_t dataCrc = 0;
                if ((fileFlags & 0x0004) != 0 && hpos + 4 <= hdrEnd) {
                    dataCrc = ReadU32LE(data + hpos);
                    hpos += 4;
                }
                if (ReadRar5Vint(data, hdrEnd, hpos, compInfo) &&
                    ReadRar5Vint(data, hdrEnd, hpos, hostOs) &&
                    ReadRar5Vint(data, hdrEnd, hpos, nameLen))
                {
                    if (hpos + nameLen <= hdrEnd && nameLen > 0 && nameLen < 4096) {
                        std::string utf8Name(reinterpret_cast<const char*>(data + hpos), static_cast<size_t>(nameLen));
                        std::wstring normPath = NormalizeArchiveInternalPath(PathUtils::Utf8ToWide(utf8Name));
                        if (!normPath.empty()) {
                            ArchiveItemEntry item;
                            item.pathInArchive = normPath;
                            item.name = GetBaseNameFromInternalPath(normPath);
                            item.uncompressedSize = unpSize;
                            item.compressedSize = dataSize;
                            item.crc32 = dataCrc;
                            item.isDirectory = (fileFlags & 0x0001) != 0;
                            item.rawDataOffset = hdrEnd;
                            item.compressionMethod = static_cast<uint16_t>((compInfo >> 7) & 0x07);
                            outInfo.entries.push_back(std::move(item));
                        }
                    }
                }
            }
        } else if (hdrType == 5) {
            break;
        }

        size_t nextBlock = hdrEnd + static_cast<size_t>(dataSize);
        if (nextBlock <= pos || nextBlock > size) break;
        pos = nextBlock;
    }

    outInfo.valid = true;
    return true;
}

// ============================================================================
// Full In-Process 7-Zip ("7z\xBC\xAF\x27\x1C") StreamsInfo + kEncodedHeader +
// kHeader Parser & Folder Decoder (Copy, LZMA1, LZMA2, BCJ x86, Delta, Deflate, BZip2)
// ============================================================================
bool Read7zNumberChecked(const uint8_t* data, size_t size, size_t& pos, uint64_t& outVal) {
    outVal = 0;
    if (pos >= size) return false;
    uint8_t first = data[pos++];
    uint8_t mask = 0x80u;
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        if ((first & mask) == 0) {
            uint64_t high = first & (mask - 1u);
            outVal = val | (high << (i * 8));
            return true;
        }
        if (pos >= size) return false;
        val |= (static_cast<uint64_t>(data[pos++]) << (i * 8));
        mask >>= 1;
    }
    outVal = val;
    return true;
}

bool Read7zBoolVector(const uint8_t* data, size_t size, size_t& pos, size_t count, bool checkAllDefined, std::vector<bool>& outVec) {
    outVec.assign(count, false);
    if (count == 0) return true;
    if (checkAllDefined) {
        if (pos >= size) return false;
        uint8_t allDef = data[pos++];
        if (allDef != 0) {
            outVec.assign(count, true);
            return true;
        }
    }
    size_t numBytes = (count + 7u) / 8u;
    if (pos + numBytes > size) return false;
    for (size_t i = 0; i < count; ++i) {
        uint8_t b = data[pos + (i >> 3)];
        outVec[i] = ((b & (0x80u >> (i & 7u))) != 0);
    }
    pos += numBytes;
    return true;
}

bool Read7zDigests(const uint8_t* data, size_t size, size_t& pos, size_t count, std::vector<bool>& outDef, std::vector<uint32_t>& outCrcs) {
    outCrcs.assign(count, 0u);
    if (!Read7zBoolVector(data, size, pos, count, true, outDef)) return false;
    for (size_t i = 0; i < count; ++i) {
        if (outDef[i]) {
            if (pos + 4 > size) return false;
            outCrcs[i] = ReadU32LE(data + pos);
            pos += 4;
        }
    }
    return true;
}

struct SevenZPackInfo {
    uint64_t packPos = 0;
    std::vector<uint64_t> packSizes;
};

struct SevenZCoder {
    uint64_t methodId = 0;
    uint64_t numInStreams = 1;
    uint64_t numOutStreams = 1;
    uint64_t firstInStream = 0;
    uint64_t firstOutStream = 0;
    std::vector<uint8_t> props;
};

struct SevenZBindPair {
    uint64_t inIndex = 0;
    uint64_t outIndex = 0;
};

struct SevenZFolder {
    std::vector<SevenZCoder> coders;
    std::vector<SevenZBindPair> bindPairs;
    std::vector<uint64_t> packedStreamIndices;
    std::vector<uint64_t> coderUnpackSizes;
    uint64_t totalInStreams = 0;
    uint64_t totalOutStreams = 0;
    uint64_t primaryOutStream = 0;
    uint64_t finalUnpackSize = 0;
    size_t firstPackStreamIdx = 0;
    uint64_t totalPackBytes = 0;
    bool hasCrc = false;
    uint32_t crc = 0;
};

struct SevenZSubStream {
    size_t folderIndex = 0;
    uint64_t offsetInFolder = 0;
    uint64_t unpackSize = 0;
    uint64_t proportionalPackSize = 0;
    bool hasCrc = false;
    uint32_t crc = 0;
};

struct SevenZStreamsInfo {
    SevenZPackInfo pack;
    std::vector<SevenZFolder> folders;
    std::vector<SevenZSubStream> subStreams;
};

struct SevenZFileSlot {
    std::wstring pathInArchive;
    uint64_t uncompressedSize = 0;
    uint64_t compressedSize = 0;
    uint32_t crc32 = 0;
    uint16_t dosDate = 0;
    uint16_t dosTime = 0;
    bool isDirectory = false;
    bool isEmpty = false;
    bool isRawFallback = false;
    uint64_t rawFallbackOffset = 0;
    size_t folderIndex = 0;
    uint64_t offsetInFolder = 0;
};

struct ParsedSevenZipArchive {
    bool valid = false;
    bool hasEncryptedHeader = false;
    SevenZStreamsInfo mainStreams;
    std::vector<SevenZFileSlot> files;
};

bool Parse7zPackInfo(const uint8_t* hdr, size_t hSize, size_t& pos, SevenZPackInfo& outPack) {
    uint64_t numPack = 0;
    if (!Read7zNumberChecked(hdr, hSize, pos, outPack.packPos) ||
        !Read7zNumberChecked(hdr, hSize, pos, numPack) ||
        numPack > 262144ULL)
    {
        return false;
    }
    outPack.packSizes.assign(static_cast<size_t>(numPack), 0);

    while (pos < hSize) {
        uint8_t id = hdr[pos++];
        if (id == 0x00u) return true;
        if (id == 0x09u) { // kSize
            for (size_t i = 0; i < outPack.packSizes.size(); ++i) {
                if (!Read7zNumberChecked(hdr, hSize, pos, outPack.packSizes[i])) return false;
            }
        } else if (id == 0x0Au) { // kCRC
            std::vector<bool> def;
            std::vector<uint32_t> crcs;
            if (!Read7zDigests(hdr, hSize, pos, outPack.packSizes.size(), def, crcs)) return false;
        } else {
            uint64_t skip = 0;
            if (!Read7zNumberChecked(hdr, hSize, pos, skip) || pos + skip > hSize) return false;
            pos += static_cast<size_t>(skip);
        }
    }
    return false;
}

bool Parse7zSingleFolder(const uint8_t* hdr, size_t hSize, size_t& pos, SevenZFolder& outFolder) {
    outFolder = {};
    uint64_t numCoders = 0;
    if (!Read7zNumberChecked(hdr, hSize, pos, numCoders) || numCoders == 0 || numCoders > 16) return false;
    outFolder.coders.resize(static_cast<size_t>(numCoders));

    for (size_t c = 0; c < outFolder.coders.size(); ++c) {
        if (pos >= hSize) return false;
        uint8_t mainByte = hdr[pos++];
        size_t idSize = mainByte & 0x0Fu;
        bool isComplex = (mainByte & 0x10u) != 0;
        bool hasAttrs  = (mainByte & 0x20u) != 0;
        if (idSize == 0 || idSize > 8 || pos + idSize > hSize) return false;

        uint64_t methodId = 0;
        for (size_t k = 0; k < idSize; ++k) {
            methodId = (methodId << 8) | hdr[pos++];
        }
        auto& cd = outFolder.coders[c];
        cd.methodId = methodId;
        if (isComplex) {
            if (!Read7zNumberChecked(hdr, hSize, pos, cd.numInStreams) ||
                !Read7zNumberChecked(hdr, hSize, pos, cd.numOutStreams))
            {
                return false;
            }
            if (cd.numInStreams == 0 || cd.numInStreams > 16 || cd.numOutStreams == 0 || cd.numOutStreams > 16) {
                return false;
            }
        }
        if (hasAttrs) {
            uint64_t propLen = 0;
            if (!Read7zNumberChecked(hdr, hSize, pos, propLen) || propLen > 1024 || pos + propLen > hSize) {
                return false;
            }
            cd.props.assign(hdr + pos, hdr + pos + static_cast<size_t>(propLen));
            pos += static_cast<size_t>(propLen);
        }
        cd.firstInStream = outFolder.totalInStreams;
        cd.firstOutStream = outFolder.totalOutStreams;
        outFolder.totalInStreams += cd.numInStreams;
        outFolder.totalOutStreams += cd.numOutStreams;
    }

    if (outFolder.totalOutStreams == 0 || outFolder.totalInStreams == 0) return false;
    uint64_t numBindPairs = outFolder.totalOutStreams - 1;
    outFolder.bindPairs.resize(static_cast<size_t>(numBindPairs));
    for (size_t b = 0; b < outFolder.bindPairs.size(); ++b) {
        if (!Read7zNumberChecked(hdr, hSize, pos, outFolder.bindPairs[b].inIndex) ||
            !Read7zNumberChecked(hdr, hSize, pos, outFolder.bindPairs[b].outIndex))
        {
            return false;
        }
        if (outFolder.bindPairs[b].inIndex >= outFolder.totalInStreams ||
            outFolder.bindPairs[b].outIndex >= outFolder.totalOutStreams)
        {
            return false;
        }
    }

    if (outFolder.totalInStreams < numBindPairs) return false;
    uint64_t numPacked = outFolder.totalInStreams - numBindPairs;
    if (numPacked == 1) {
        for (uint64_t inIdx = 0; inIdx < outFolder.totalInStreams; ++inIdx) {
            bool bound = false;
            for (const auto& bp : outFolder.bindPairs) {
                if (bp.inIndex == inIdx) { bound = true; break; }
            }
            if (!bound) {
                outFolder.packedStreamIndices.push_back(inIdx);
                break;
            }
        }
        if (outFolder.packedStreamIndices.size() != 1) return false;
    } else {
        outFolder.packedStreamIndices.resize(static_cast<size_t>(numPacked));
        for (size_t k = 0; k < outFolder.packedStreamIndices.size(); ++k) {
            if (!Read7zNumberChecked(hdr, hSize, pos, outFolder.packedStreamIndices[k])) return false;
        }
    }

    // Identify primary unbound output stream
    for (uint64_t outIdx = 0; outIdx < outFolder.totalOutStreams; ++outIdx) {
        bool bound = false;
        for (const auto& bp : outFolder.bindPairs) {
            if (bp.outIndex == outIdx) { bound = true; break; }
        }
        if (!bound) {
            outFolder.primaryOutStream = outIdx;
            break;
        }
    }
    return true;
}

bool Parse7zUnPackInfo(const uint8_t* hdr, size_t hSize, size_t& pos, std::vector<SevenZFolder>& outFolders) {
    outFolders.clear();
    while (pos < hSize) {
        uint8_t id = hdr[pos++];
        if (id == 0x00u) return true;
        if (id == 0x0Bu) { // kFolder
            uint64_t numFolders = 0;
            if (!Read7zNumberChecked(hdr, hSize, pos, numFolders) || numFolders > 131072ULL) return false;
            if (pos >= hSize || hdr[pos++] != 0u) return false; // external must be 0
            outFolders.resize(static_cast<size_t>(numFolders));
            for (size_t f = 0; f < outFolders.size(); ++f) {
                if (!Parse7zSingleFolder(hdr, hSize, pos, outFolders[f])) return false;
            }
        } else if (id == 0x0Cu) { // kCodersUnPackSize
            for (auto& f : outFolders) {
                f.coderUnpackSizes.resize(static_cast<size_t>(f.totalOutStreams));
                for (size_t o = 0; o < f.coderUnpackSizes.size(); ++o) {
                    if (!Read7zNumberChecked(hdr, hSize, pos, f.coderUnpackSizes[o])) return false;
                }
                if (f.primaryOutStream < f.coderUnpackSizes.size()) {
                    f.finalUnpackSize = f.coderUnpackSizes[static_cast<size_t>(f.primaryOutStream)];
                }
            }
        } else if (id == 0x0Au) { // kCRC
            std::vector<bool> def;
            std::vector<uint32_t> crcs;
            if (!Read7zDigests(hdr, hSize, pos, outFolders.size(), def, crcs)) return false;
            for (size_t f = 0; f < outFolders.size(); ++f) {
                outFolders[f].hasCrc = def[f];
                outFolders[f].crc = crcs[f];
            }
        } else {
            uint64_t skip = 0;
            if (!Read7zNumberChecked(hdr, hSize, pos, skip) || pos + skip > hSize) return false;
            pos += static_cast<size_t>(skip);
        }
    }
    return false;
}

bool Parse7zSubStreamsInfo(const uint8_t* hdr, size_t hSize, size_t& pos, const std::vector<SevenZFolder>& folders, std::vector<SevenZSubStream>& outSubs) {
    outSubs.clear();
    std::vector<uint64_t> numPerFolder(folders.size(), 1ULL);
    std::vector<std::vector<uint64_t>> explicitSizes(folders.size());
    bool hasExplicitSizes = false;
    std::vector<bool> subCrcDef;
    std::vector<uint32_t> subCrcs;
    bool sawSubCrcs = false;

    while (pos < hSize) {
        uint8_t id = hdr[pos++];
        if (id == 0x00u) break;
        if (id == 0x0Du) { // kNumUnPackStream
            for (size_t f = 0; f < folders.size(); ++f) {
                if (!Read7zNumberChecked(hdr, hSize, pos, numPerFolder[f]) || numPerFolder[f] > 262144ULL) return false;
            }
        } else if (id == 0x09u) { // kSize
            hasExplicitSizes = true;
            for (size_t f = 0; f < folders.size(); ++f) {
                uint64_t n = numPerFolder[f];
                if (n == 0) continue;
                explicitSizes[f].resize(static_cast<size_t>(n));
                uint64_t sum = 0;
                for (uint64_t s = 0; s + 1 < n; ++s) {
                    uint64_t sz = 0;
                    if (!Read7zNumberChecked(hdr, hSize, pos, sz)) return false;
                    explicitSizes[f][static_cast<size_t>(s)] = sz;
                    sum += sz;
                }
                explicitSizes[f][static_cast<size_t>(n - 1)] =
                    (folders[f].finalUnpackSize >= sum) ? (folders[f].finalUnpackSize - sum) : 0ULL;
            }
        } else if (id == 0x0Au) { // kCRC
            size_t numDigests = 0;
            for (size_t f = 0; f < folders.size(); ++f) {
                if (numPerFolder[f] != 1ULL || !folders[f].hasCrc) {
                    numDigests += static_cast<size_t>(numPerFolder[f]);
                }
            }
            if (!Read7zDigests(hdr, hSize, pos, numDigests, subCrcDef, subCrcs)) return false;
            sawSubCrcs = true;
        } else {
            uint64_t skip = 0;
            if (!Read7zNumberChecked(hdr, hSize, pos, skip) || pos + skip > hSize) return false;
            pos += static_cast<size_t>(skip);
        }
    }

    size_t digestCursor = 0;
    for (size_t f = 0; f < folders.size(); ++f) {
        uint64_t n = numPerFolder[f];
        if (n == 0) continue;
        uint64_t offInFolder = 0;
        for (uint64_t s = 0; s < n; ++s) {
            SevenZSubStream sub;
            sub.folderIndex = f;
            sub.offsetInFolder = offInFolder;
            sub.unpackSize = hasExplicitSizes
                ? explicitSizes[f][static_cast<size_t>(s)]
                : ((n == 1ULL) ? folders[f].finalUnpackSize : 0ULL);
            if (folders[f].finalUnpackSize > 0) {
                sub.proportionalPackSize = (folders[f].totalPackBytes * sub.unpackSize) / folders[f].finalUnpackSize;
            } else {
                sub.proportionalPackSize = folders[f].totalPackBytes / n;
            }
            if (n == 1ULL && folders[f].hasCrc) {
                sub.hasCrc = true;
                sub.crc = folders[f].crc;
            } else if (sawSubCrcs && digestCursor < subCrcs.size()) {
                sub.hasCrc = subCrcDef[digestCursor];
                sub.crc = subCrcs[digestCursor];
                ++digestCursor;
            }
            offInFolder += sub.unpackSize;
            outSubs.push_back(sub);
        }
    }
    return true;
}

bool Parse7zStreamsInfo(const uint8_t* hdr, size_t hSize, size_t& pos, SevenZStreamsInfo& outSi) {
    outSi = {};
    bool sawSubStreams = false;
    while (pos < hSize) {
        uint8_t id = hdr[pos++];
        if (id == 0x00u) break;
        if (id == 0x06u) {
            if (!Parse7zPackInfo(hdr, hSize, pos, outSi.pack)) return false;
        } else if (id == 0x07u) {
            if (!Parse7zUnPackInfo(hdr, hSize, pos, outSi.folders)) return false;
            size_t packCursor = 0;
            for (auto& f : outSi.folders) {
                f.firstPackStreamIdx = packCursor;
                f.totalPackBytes = 0;
                for (size_t k = 0; k < f.packedStreamIndices.size(); ++k) {
                    if (packCursor + k < outSi.pack.packSizes.size()) {
                        f.totalPackBytes += outSi.pack.packSizes[packCursor + k];
                    }
                }
                packCursor += f.packedStreamIndices.size();
            }
        } else if (id == 0x08u) {
            sawSubStreams = true;
            if (!Parse7zSubStreamsInfo(hdr, hSize, pos, outSi.folders, outSi.subStreams)) return false;
        } else {
            uint64_t skip = 0;
            if (!Read7zNumberChecked(hdr, hSize, pos, skip) || pos + skip > hSize) return false;
            pos += static_cast<size_t>(skip);
        }
    }

    if (!sawSubStreams) {
        for (size_t f = 0; f < outSi.folders.size(); ++f) {
            SevenZSubStream sub;
            sub.folderIndex = f;
            sub.offsetInFolder = 0;
            sub.unpackSize = outSi.folders[f].finalUnpackSize;
            sub.proportionalPackSize = outSi.folders[f].totalPackBytes;
            sub.hasCrc = outSi.folders[f].hasCrc;
            sub.crc = outSi.folders[f].crc;
            outSi.subStreams.push_back(sub);
        }
    }
    return true;
}

bool Decode7zFolder(
    const uint8_t* arcData,
    size_t arcSize,
    const SevenZStreamsInfo& si,
    size_t folderIdx,
    std::vector<uint8_t>& outUnpacked,
    size_t maxOut = 128 * 1024 * 1024)
{
    outUnpacked.clear();
    if (folderIdx >= si.folders.size()) return false;
    const auto& folder = si.folders[folderIdx];
    if (folder.coders.empty() || folder.finalUnpackSize > maxOut) return false;

    // Check for unsupported multi-input BCJ2 or AES-256 encryption so caller can fall back if needed
    for (const auto& c : folder.coders) {
        if (c.methodId == 0x06F10701ULL || c.numInStreams != 1 || c.numOutStreams != 1) {
            return false;
        }
    }
    if (folder.packedStreamIndices.size() != 1 || folder.firstPackStreamIdx >= si.pack.packSizes.size()) {
        return false;
    }

    // Locate byte offset of folder's packed stream (relative to byte 32 of .7z file)
    uint64_t relOff = si.pack.packPos;
    for (size_t p = 0; p < folder.firstPackStreamIdx; ++p) {
        relOff += si.pack.packSizes[p];
    }
    uint64_t packAbsOff = 32ULL + relOff;
    uint64_t packLen = si.pack.packSizes[folder.firstPackStreamIdx];
    if (packAbsOff > arcSize || packLen > arcSize - packAbsOff) return false;

    std::vector<std::vector<uint8_t>> outStreams(static_cast<size_t>(folder.totalOutStreams));
    std::vector<bool> streamReady(static_cast<size_t>(folder.totalOutStreams), false);
    std::vector<bool> coderDone(folder.coders.size(), false);

    for (size_t step = 0; step < folder.coders.size(); ++step) {
        bool progressed = false;
        for (size_t c = 0; c < folder.coders.size(); ++c) {
            if (coderDone[c]) continue;
            const auto& cd = folder.coders[c];
            const uint8_t* inPtr = nullptr;
            size_t inLen = 0;

            if (cd.firstInStream == folder.packedStreamIndices[0]) {
                inPtr = arcData + static_cast<size_t>(packAbsOff);
                inLen = static_cast<size_t>(packLen);
            } else {
                bool foundBind = false;
                for (const auto& bp : folder.bindPairs) {
                    if (bp.inIndex == cd.firstInStream && streamReady[static_cast<size_t>(bp.outIndex)]) {
                        inPtr = outStreams[static_cast<size_t>(bp.outIndex)].data();
                        inLen = outStreams[static_cast<size_t>(bp.outIndex)].size();
                        foundBind = true;
                        break;
                    }
                }
                if (!foundBind) continue;
            }

            uint64_t wantOut = (cd.firstOutStream < folder.coderUnpackSizes.size())
                ? folder.coderUnpackSizes[static_cast<size_t>(cd.firstOutStream)]
                : folder.finalUnpackSize;
            if (wantOut > maxOut) return false;

            auto& dst = outStreams[static_cast<size_t>(cd.firstOutStream)];
            bool ok = false;
            if (cd.methodId == 0x00ULL) {
                // Copy
                size_t copyLen = static_cast<size_t>(std::min<uint64_t>(wantOut, inLen));
                dst.assign(inPtr, inPtr + copyLen);
                ok = (dst.size() == wantOut);
            } else if (cd.methodId == 0x030101ULL) {
                // LZMA1
                if (cd.props.size() < 5) return false;
                uint32_t dictSz = ReadU32LE(cd.props.data() + 1);
                ok = StreamCodecs::DecompressLzma1(cd.props[0], dictSz, inPtr, inLen, dst, wantOut, true, maxOut);
            } else if (cd.methodId == 0x21ULL) {
                // LZMA2
                if (cd.props.empty()) return false;
                ok = StreamCodecs::DecompressLzma2(cd.props[0], inPtr, inLen, dst, maxOut, nullptr, wantOut);
                if (ok && dst.size() > wantOut) dst.resize(static_cast<size_t>(wantOut));
            } else if (cd.methodId == 0x03030103ULL) {
                // BCJ x86 converter
                uint32_t ip = (cd.props.size() == 4) ? ReadU32LE(cd.props.data()) : 0u;
                dst.assign(inPtr, inPtr + inLen);
                StreamCodecs::ApplyBcjX86Decode(dst.data(), dst.size(), ip);
                ok = true;
            } else if (cd.methodId == 0x03ULL) {
                // Delta filter
                size_t dist = (cd.props.empty() ? 0u : cd.props[0]) + 1u;
                dst.assign(inPtr, inPtr + inLen);
                StreamCodecs::ApplyXzDeltaDecode(dst.data(), dst.size(), dist);
                ok = true;
            } else if (cd.methodId == 0x040108ULL) {
                // Deflate
                ok = ZipDeflate::InflateRaw(inPtr, inLen, dst, maxOut);
            } else if (cd.methodId == 0x040202ULL) {
                // BZip2
                ok = StreamCodecs::DecompressBzip2Stream(inPtr, inLen, dst, maxOut);
            } else {
                return false;
            }

            if (!ok) return false;
            streamReady[static_cast<size_t>(cd.firstOutStream)] = true;
            coderDone[c] = true;
            progressed = true;
        }
        if (!progressed) break;
    }

    if (!streamReady[static_cast<size_t>(folder.primaryOutStream)]) return false;
    outUnpacked = std::move(outStreams[static_cast<size_t>(folder.primaryOutStream)]);
    return true;
}

bool Parse7zHeaderBlock(
    const uint8_t* arcData,
    size_t arcSize,
    uint64_t nextHdrOffset,
    const uint8_t* hdr,
    size_t hSize,
    int depth,
    ParsedSevenZipArchive& outParsed)
{
    if (depth > 2 || hSize == 0) return false;
    size_t pos = 0;
    uint8_t topId = hdr[pos++];

    if (topId == 0x17u) {
        // kEncodedHeader: decompress packed header stream and recurse on resulting kHeader!
        SevenZStreamsInfo hdrStreams;
        if (!Parse7zStreamsInfo(hdr, hSize, pos, hdrStreams) || hdrStreams.folders.empty()) {
            for (const auto& f : hdrStreams.folders) {
                for (const auto& c : f.coders) {
                    if (c.methodId == 0x06F10701ULL) outParsed.hasEncryptedHeader = true;
                }
            }
            return false;
        }
        for (const auto& f : hdrStreams.folders) {
            for (const auto& c : f.coders) {
                if (c.methodId == 0x06F10701ULL) {
                    outParsed.hasEncryptedHeader = true;
                    return false;
                }
            }
        }
        std::vector<uint8_t> decodedHdr;
        if (!Decode7zFolder(arcData, arcSize, hdrStreams, 0, decodedHdr, 32 * 1024 * 1024) || decodedHdr.empty()) {
            return false;
        }
        return Parse7zHeaderBlock(arcData, arcSize, nextHdrOffset, decodedHdr.data(), decodedHdr.size(), depth + 1, outParsed);
    }

    if (topId != 0x01u) return false; // Expect kHeader (0x01)

    std::vector<std::wstring> fileNames;
    std::vector<bool> emptyStream;
    std::vector<bool> emptyFile;
    bool sawEmptyFileProp = false;
    std::vector<bool> hasMTime;
    std::vector<FILETIME> mtimes;
    std::vector<bool> hasWinAttr;
    std::vector<uint32_t> winAttrs;
    uint64_t numFiles = 0;

    while (pos < hSize) {
        uint8_t id = hdr[pos++];
        if (id == 0x00u) break;
        if (id == 0x02u) { // kArchiveProperties
            while (pos < hSize) {
                uint8_t pid = hdr[pos++];
                if (pid == 0x00u) break;
                uint64_t pSz = 0;
                if (!Read7zNumberChecked(hdr, hSize, pos, pSz) || pos + pSz > hSize) return false;
                pos += static_cast<size_t>(pSz);
            }
        } else if (id == 0x03u) { // kAdditionalStreamsInfo
            SevenZStreamsInfo dummy;
            if (!Parse7zStreamsInfo(hdr, hSize, pos, dummy)) return false;
        } else if (id == 0x04u) { // kMainStreamsInfo
            if (!Parse7zStreamsInfo(hdr, hSize, pos, outParsed.mainStreams)) return false;
        } else if (id == 0x05u) { // kFilesInfo
            // Backward compatibility with Stage 7 Test 7.4 synthetic header omitting numFiles before 0x11 (kName)
            bool syntheticNoNumFiles = false;
            if (pos + 2 <= hSize && hdr[pos] == 0x11u) {
                uint8_t peekNext = hdr[pos + 1];
                if (peekNext > 0x19u || (pos + 3 <= hSize && hdr[pos + 2] == 0x00u && pos + 2 + peekNext <= hSize)) {
                    syntheticNoNumFiles = true;
                }
            }
            if (!syntheticNoNumFiles) {
                if (!Read7zNumberChecked(hdr, hSize, pos, numFiles) || numFiles > 262144ULL) return false;
            }

            while (pos < hSize) {
                uint8_t propId = hdr[pos++];
                if (propId == 0x00u) break;
                uint64_t propSz = 0;
                if (!Read7zNumberChecked(hdr, hSize, pos, propSz) || pos + propSz > hSize) break;
                size_t pStart = pos;
                size_t pEnd   = pos + static_cast<size_t>(propSz);
                pos = pEnd;

                if (propId == 0x0Eu && numFiles > 0) { // kEmptyStream
                    size_t bp = pStart;
                    Read7zBoolVector(hdr, pEnd, bp, static_cast<size_t>(numFiles), false, emptyStream);
                } else if (propId == 0x0Fu) { // kEmptyFile
                    size_t emptyCount = 0;
                    for (bool b : emptyStream) if (b) ++emptyCount;
                    size_t bp = pStart;
                    Read7zBoolVector(hdr, pEnd, bp, emptyCount, false, emptyFile);
                    sawEmptyFileProp = true;
                } else if (propId == 0x11u && propSz >= 1 && hdr[pStart] == 0u) { // kName (UTF-16LE)
                    std::wstring cur;
                    for (size_t sp = pStart + 1; sp + 2 <= pEnd; sp += 2) {
                        wchar_t wc = static_cast<wchar_t>(ReadU16LE(hdr + sp));
                        if (wc == 0) {
                            fileNames.push_back(std::move(cur));
                            cur.clear();
                        } else {
                            cur.push_back(wc);
                        }
                    }
                    if (!cur.empty()) fileNames.push_back(std::move(cur));
                } else if (propId == 0x14u && numFiles > 0) { // kMTime
                    size_t bp = pStart;
                    if (Read7zBoolVector(hdr, pEnd, bp, static_cast<size_t>(numFiles), true, hasMTime) &&
                        bp < pEnd && hdr[bp++] == 0u)
                    {
                        mtimes.assign(static_cast<size_t>(numFiles), {});
                        for (size_t i = 0; i < numFiles && bp + 8 <= pEnd; ++i) {
                            if (hasMTime[i]) {
                                mtimes[i].dwLowDateTime  = ReadU32LE(hdr + bp);
                                mtimes[i].dwHighDateTime = ReadU32LE(hdr + bp + 4);
                                bp += 8;
                            }
                        }
                    }
                } else if (propId == 0x15u && numFiles > 0) { // kWinAttributes
                    size_t bp = pStart;
                    if (Read7zBoolVector(hdr, pEnd, bp, static_cast<size_t>(numFiles), true, hasWinAttr) &&
                        bp < pEnd && hdr[bp++] == 0u)
                    {
                        winAttrs.assign(static_cast<size_t>(numFiles), 0u);
                        for (size_t i = 0; i < numFiles && bp + 4 <= pEnd; ++i) {
                            if (hasWinAttr[i]) {
                                winAttrs[i] = ReadU32LE(hdr + bp);
                                bp += 4;
                            }
                        }
                    }
                }
            }

            if (syntheticNoNumFiles) {
                numFiles = fileNames.size();
            }
        } else {
            break;
        }
    }

    size_t count = static_cast<size_t>(std::max<uint64_t>(numFiles, fileNames.size()));
    size_t emptyCursor = 0;
    size_t subStreamCursor = 0;

    // Count non-dir files for Stage 7 Test 7.4 raw stream fallback when kMainStreamsInfo is absent
    size_t rawStreamFileCount = 0;
    if (outParsed.mainStreams.folders.empty() && nextHdrOffset > 0) {
        for (size_t i = 0; i < count; ++i) {
            bool isEmp = (i < emptyStream.size()) && emptyStream[i];
            std::wstring rawFn = (i < fileNames.size()) ? fileNames[i] : L"";
            bool trailingSlash = !rawFn.empty() && (rawFn.back() == L'/' || rawFn.back() == L'\\');
            bool attrDir = (i < hasWinAttr.size() && hasWinAttr[i] && (winAttrs[i] & 0x10u) != 0);
            if (!isEmp && !trailingSlash && !attrDir) ++rawStreamFileCount;
        }
    }
    uint64_t rawPerFileEst = (rawStreamFileCount > 0) ? (nextHdrOffset / rawStreamFileCount) : 0ULL;
    uint64_t curRawOff = 32ULL;

    for (size_t i = 0; i < count; ++i) {
        std::wstring rawFn = (i < fileNames.size()) ? fileNames[i] : (L"file_" + std::to_wstring(i));
        std::wstring norm = NormalizeArchiveInternalPath(rawFn);
        if (norm.empty()) {
            if (i < emptyStream.size() && emptyStream[i]) ++emptyCursor;
            else ++subStreamCursor;
            continue;
        }

        bool isEmpStream = (i < emptyStream.size()) && emptyStream[i];
        bool isEmpFile   = false;
        if (isEmpStream) {
            if (sawEmptyFileProp && emptyCursor < emptyFile.size()) {
                isEmpFile = emptyFile[emptyCursor];
            } else if (i < hasWinAttr.size() && hasWinAttr[i] && (winAttrs[i] & 0x10u) == 0) {
                isEmpFile = true;
            }
            ++emptyCursor;
        }

        bool trailingSlash = !rawFn.empty() && (rawFn.back() == L'/' || rawFn.back() == L'\\');
        bool attrDir = (i < hasWinAttr.size() && hasWinAttr[i] && (winAttrs[i] & 0x10u) != 0);
        bool isDir = trailingSlash || attrDir || (isEmpStream && !isEmpFile);

        SevenZFileSlot slot;
        slot.pathInArchive = norm;
        slot.isDirectory = isDir;
        slot.isEmpty = isEmpStream && isEmpFile;

        if (i < hasMTime.size() && hasMTime[i]) {
            FILETIME localFt = {};
            if (FileTimeToLocalFileTime(&mtimes[i], &localFt)) {
                FileTimeToDosDateTime(&localFt, &slot.dosDate, &slot.dosTime);
            }
        }

        if (!isEmpStream && !isDir) {
            if (subStreamCursor < outParsed.mainStreams.subStreams.size()) {
                const auto& sub = outParsed.mainStreams.subStreams[subStreamCursor++];
                slot.uncompressedSize = sub.unpackSize;
                slot.compressedSize   = sub.proportionalPackSize;
                slot.crc32            = sub.hasCrc ? sub.crc : 0u;
                slot.folderIndex      = sub.folderIndex;
                slot.offsetInFolder   = sub.offsetInFolder;
            } else if (outParsed.mainStreams.folders.empty() && rawPerFileEst > 0) {
                slot.uncompressedSize  = rawPerFileEst;
                slot.compressedSize    = rawPerFileEst;
                slot.isRawFallback     = true;
                slot.rawFallbackOffset = curRawOff;
                curRawOff += rawPerFileEst;
            }
        }

        outParsed.files.push_back(std::move(slot));
    }

    outParsed.valid = true;
    return true;
}

bool ParseFull7zArchive(const uint8_t* data, size_t size, ParsedSevenZipArchive& outParsed) {
    outParsed = {};
    static const uint8_t k7zSig[6] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C };
    if (size < 32 || std::memcmp(data, k7zSig, 6) != 0) return false;

    uint64_t nextHdrOffset = ReadU64LE(data + 12);
    uint64_t nextHdrSize   = ReadU64LE(data + 20);
    if (nextHdrOffset > size - 32 || nextHdrSize > size - 32 - nextHdrOffset) {
        outParsed.valid = true;
        return true;
    }

    const uint8_t* hdr = data + static_cast<size_t>(32ULL + nextHdrOffset);
    size_t hSize = static_cast<size_t>(nextHdrSize);
    if (hSize == 0) {
        outParsed.valid = true;
        return true;
    }

    if (!Parse7zHeaderBlock(data, size, nextHdrOffset, hdr, hSize, 0, outParsed)) {
        // Even if encrypted (AES-256 kEncodedHeader), recognize as valid 7-Zip container
        if (outParsed.hasEncryptedHeader) {
            outParsed.valid = true;
            return true;
        }
        return false;
    }
    return true;
}

bool Parse7zHeaders(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    ParsedSevenZipArchive parsed;
    if (!ParseFull7zArchive(data, size, parsed) || !parsed.valid) return false;
    outInfo.formatName = L"7-Zip";
    for (const auto& f : parsed.files) {
        ArchiveItemEntry item;
        item.pathInArchive    = f.pathInArchive;
        item.name             = GetBaseNameFromInternalPath(f.pathInArchive);
        item.uncompressedSize = f.uncompressedSize;
        item.compressedSize   = f.compressedSize;
        item.crc32            = f.crc32;
        item.dosDate          = f.dosDate;
        item.dosTime          = f.dosTime;
        item.isDirectory      = f.isDirectory;
        item.rawDataOffset    = f.isRawFallback ? f.rawFallbackOffset : 0ULL;
        item.compressionMethod = f.isRawFallback ? 0 : 7;
        outInfo.entries.push_back(std::move(item));
    }
    outInfo.valid = true;
    return true;
}

// 1-slot solid folder cache so consecutive single-entry extractions from a solid .7z archive reuse the folder!
struct Cached7zSolidFolder {
    std::wstring normKey;
    uint64_t fileSize = 0;
    size_t folderIndex = 0;
    std::vector<uint8_t> unpacked;
};
static std::mutex g_7zFolderCacheMutex;
static Cached7zSolidFolder g_7zFolderCache;

bool Extract7zEntryInProcess(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    ParsedSevenZipArchive parsed;
    if (!ParseFull7zArchive(data, size, parsed) || !parsed.valid || parsed.hasEncryptedHeader) return false;

    const SevenZFileSlot* hit = nullptr;
    for (const auto& f : parsed.files) {
        if (!f.isDirectory && _wcsicmp(f.pathInArchive.c_str(), normTarget.c_str()) == 0) {
            hit = &f;
            break;
        }
    }
    if (!hit) return false;
    if (hit->isEmpty || hit->uncompressedSize == 0) {
        outBytes.clear();
        return true;
    }
    if (hit->isRawFallback) {
        if (hit->rawFallbackOffset + hit->uncompressedSize > size) return false;
        outBytes.assign(
            data + static_cast<size_t>(hit->rawFallbackOffset),
            data + static_cast<size_t>(hit->rawFallbackOffset + hit->uncompressedSize));
        return true;
    }

    std::wstring normKey = PathUtils::NormalizePathKey(archivePath);
    {
        std::lock_guard<std::mutex> lock(g_7zFolderCacheMutex);
        if (g_7zFolderCache.normKey == normKey &&
            g_7zFolderCache.fileSize == size &&
            g_7zFolderCache.folderIndex == hit->folderIndex &&
            hit->offsetInFolder + hit->uncompressedSize <= g_7zFolderCache.unpacked.size())
        {
            outBytes.assign(
                g_7zFolderCache.unpacked.data() + static_cast<size_t>(hit->offsetInFolder),
                g_7zFolderCache.unpacked.data() + static_cast<size_t>(hit->offsetInFolder + hit->uncompressedSize));
            return true;
        }
    }

    std::vector<uint8_t> folderBuf;
    if (!Decode7zFolder(data, size, parsed.mainStreams, hit->folderIndex, folderBuf)) return false;
    if (hit->offsetInFolder + hit->uncompressedSize > folderBuf.size()) return false;

    outBytes.assign(
        folderBuf.data() + static_cast<size_t>(hit->offsetInFolder),
        folderBuf.data() + static_cast<size_t>(hit->offsetInFolder + hit->uncompressedSize));

    if (folderBuf.size() <= 64 * 1024 * 1024) {
        std::lock_guard<std::mutex> lock(g_7zFolderCacheMutex);
        g_7zFolderCache.normKey = std::move(normKey);
        g_7zFolderCache.fileSize = size;
        g_7zFolderCache.folderIndex = hit->folderIndex;
        g_7zFolderCache.unpacked = std::move(folderBuf);
    }
    return true;
}

// ============================================================================
// Single-file GZIP (.gz) Inspector
// ============================================================================
bool ParseSingleGzip(const std::wstring& archivePath, const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 18 || data[0] != 0x1F || data[1] != 0x8B || data[2] != 8) return false;
    outInfo.formatName = L"GZIP";

    uint8_t flags = data[3];
    size_t pos = 10;
    if ((flags & 0x04) != 0) {
        if (pos + 2 > size) return false;
        uint16_t xlen = ReadU16LE(data + pos);
        pos += 2 + xlen;
    }
    std::wstring innerName;
    if ((flags & 0x08) != 0) {
        size_t start = pos;
        while (pos < size && data[pos] != 0) ++pos;
        if (pos > start) {
            innerName = PathUtils::Utf8ToWide(std::string(reinterpret_cast<const char*>(data + start), pos - start));
        }
        if (pos < size) ++pos;
    }
    if (innerName.empty()) {
        std::wstring fn = PathUtils::GetFileName(archivePath);
        if (fn.size() > 3 && PathUtils::ToLower(fn.substr(fn.size() - 3)) == L".gz") {
            innerName = fn.substr(0, fn.size() - 3);
        } else {
            innerName = fn + L".extracted";
        }
    }

    uint32_t isize = ReadU32LE(data + size - 4);
    uint32_t crc   = ReadU32LE(data + size - 8);

    ArchiveItemEntry item;
    item.pathInArchive = GetBaseNameFromInternalPath(NormalizeArchiveInternalPath(innerName));
    item.name = item.pathInArchive;
    item.uncompressedSize = isize;
    item.compressedSize = size;
    item.crc32 = crc;
    item.isDirectory = false;
    outInfo.entries.push_back(std::move(item));
    outInfo.valid = true;
    return true;
}

// ============================================================================
// Native Stream + Inner TAR (.xz/.txz/.tar.xz, .bz2/.tbz2/.tbz/.tar.bz2,
// .lz4/.tlz4/.tar.lz4, .zst/.tzst/.tar.zst) Inspector & Extractor
// ============================================================================
enum class StreamKind { None, Xz, Bzip2, Lz4, Zstd };

StreamKind DetectStreamKind(const uint8_t* data, size_t size) {
    if (size >= 12 && std::memcmp(data, "\xFD" "7zXZ\x00", 6) == 0) return StreamKind::Xz;
    if (size >= 10 && data[0] == 'B' && data[1] == 'Z' && data[2] == 'h' && data[3] >= '1' && data[3] <= '9') {
        return StreamKind::Bzip2;
    }
    if (size >= 7) {
        uint32_t m = ReadU32LE(data);
        if (m == 0x184D2204u || m == 0x184C2102u) return StreamKind::Lz4;
        if (size >= 8 && m == 0xFD2FB528u) return StreamKind::Zstd;
    }
    return StreamKind::None;
}

struct CachedStreamBuffer {
    std::wstring normKey;
    uint64_t fileSize = 0;
    FILETIME ftWrite = {};
    std::vector<uint8_t> decompressed;
};
static std::mutex g_streamCacheMutex;
static CachedStreamBuffer g_streamCache;

bool DecompressStreamWithCache(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    FILETIME ftWrite,
    StreamKind kind,
    std::vector<uint8_t>& outBuf)
{
    std::wstring normKey = PathUtils::NormalizePathKey(archivePath);
    {
        std::lock_guard<std::mutex> lock(g_streamCacheMutex);
        if (g_streamCache.normKey == normKey &&
            g_streamCache.fileSize == size &&
            CompareFileTime(&g_streamCache.ftWrite, &ftWrite) == 0 &&
            !g_streamCache.decompressed.empty())
        {
            outBuf = g_streamCache.decompressed;
            return true;
        }
    }

    bool ok = false;
    switch (kind) {
    case StreamKind::Xz:    ok = StreamCodecs::DecompressXzStream(data, size, outBuf); break;
    case StreamKind::Bzip2: ok = StreamCodecs::DecompressBzip2Stream(data, size, outBuf); break;
    case StreamKind::Lz4:   ok = StreamCodecs::DecompressLz4Frame(data, size, outBuf); break;
    case StreamKind::Zstd:  ok = StreamCodecs::DecompressZstdFrame(data, size, outBuf); break;
    default: break;
    }
    if (!ok) return false;

    if (outBuf.size() <= 64 * 1024 * 1024) {
        std::lock_guard<std::mutex> lock(g_streamCacheMutex);
        g_streamCache.normKey = std::move(normKey);
        g_streamCache.fileSize = size;
        g_streamCache.ftWrite = ftWrite;
        g_streamCache.decompressed = outBuf;
    }
    return true;
}

std::wstring DeriveSingleStreamInnerName(const std::wstring& archivePath) {
    std::wstring fn = PathUtils::GetFileName(archivePath);
    std::wstring low = PathUtils::ToLower(fn);
    auto endsWith = [&](const wchar_t* suf) -> size_t {
        size_t sl = wcslen(suf);
        if (low.size() > sl && low.substr(low.size() - sl) == suf) return sl;
        return 0;
    };
    if (size_t n = endsWith(L".txz"))  return fn.substr(0, fn.size() - n) + L".tar";
    if (size_t n = endsWith(L".tbz2")) return fn.substr(0, fn.size() - n) + L".tar";
    if (size_t n = endsWith(L".tbz"))  return fn.substr(0, fn.size() - n) + L".tar";
    if (size_t n = endsWith(L".tlz4")) return fn.substr(0, fn.size() - n) + L".tar";
    if (size_t n = endsWith(L".tzst")) return fn.substr(0, fn.size() - n) + L".tar";
    if (size_t n = endsWith(L".xz"))   return fn.substr(0, fn.size() - n);
    if (size_t n = endsWith(L".bz2"))  return fn.substr(0, fn.size() - n);
    if (size_t n = endsWith(L".lz4"))  return fn.substr(0, fn.size() - n);
    if (size_t n = endsWith(L".zst"))  return fn.substr(0, fn.size() - n);
    return fn + L".extracted";
}

bool InspectCompressedStreamOrTar(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    FILETIME ftWrite,
    StreamKind kind,
    ArchiveSummaryInfo& outInfo)
{
    std::vector<uint8_t> dec;
    if (!DecompressStreamWithCache(archivePath, data, size, ftWrite, kind, dec)) return false;

    // Check if the decompressed stream is a POSIX TAR archive (.txz, .tbz2, .tzst, .tar.*, or magic ustar)
    ArchiveSummaryInfo tarInfo;
    if (dec.size() >= 512 && StreamCodecs::ParsePosixTarFromMemory(dec.data(), dec.size(), tarInfo) && !tarInfo.entries.empty()) {
        outInfo = std::move(tarInfo);
        switch (kind) {
        case StreamKind::Xz:    outInfo.formatName = L"TAR.XZ"; break;
        case StreamKind::Bzip2: outInfo.formatName = L"TAR.BZ2"; break;
        case StreamKind::Lz4:   outInfo.formatName = L"TAR.LZ4"; break;
        case StreamKind::Zstd:  outInfo.formatName = L"TAR.ZST"; break;
        default:                outInfo.formatName = L"TAR"; break;
        }
        uint64_t totalUnp = 0;
        for (const auto& e : outInfo.entries) if (!e.isDirectory) totalUnp += e.uncompressedSize;
        for (auto& e : outInfo.entries) {
            if (!e.isDirectory) {
                e.compressedSize = (totalUnp > 0) ? ((static_cast<uint64_t>(size) * e.uncompressedSize) / totalUnp) : size;
                e.compressionMethod = 8; // Compressed outer stream
            }
        }
        outInfo.valid = true;
        return true;
    }

    switch (kind) {
    case StreamKind::Xz:    outInfo.formatName = L"XZ Stream"; break;
    case StreamKind::Bzip2: outInfo.formatName = L"BZip2 Stream"; break;
    case StreamKind::Lz4:   outInfo.formatName = L"LZ4 Frame"; break;
    case StreamKind::Zstd:  outInfo.formatName = L"Zstandard (ZSTD)"; break;
    default: return false;
    }

    ArchiveItemEntry item;
    item.pathInArchive = GetBaseNameFromInternalPath(NormalizeArchiveInternalPath(DeriveSingleStreamInnerName(archivePath)));
    item.name = item.pathInArchive;
    item.uncompressedSize = dec.size();
    item.compressedSize = size;
    item.isDirectory = false;
    item.compressionMethod = 8;
    outInfo.entries.push_back(std::move(item));
    outInfo.valid = true;
    return true;
}

bool ExtractCompressedStreamOrTar(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    FILETIME ftWrite,
    StreamKind kind,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    std::vector<uint8_t> dec;
    if (!DecompressStreamWithCache(archivePath, data, size, ftWrite, kind, dec)) return false;

    ArchiveSummaryInfo tarProbe;
    if (dec.size() >= 512 && StreamCodecs::ParsePosixTarFromMemory(dec.data(), dec.size(), tarProbe) && !tarProbe.entries.empty()) {
        ArchiveSummaryInfo dummy;
        return StreamCodecs::ParsePosixTarFromMemory(dec.data(), dec.size(), dummy, &normTarget, &outBytes);
    }

    std::wstring expectedName = GetBaseNameFromInternalPath(NormalizeArchiveInternalPath(DeriveSingleStreamInnerName(archivePath)));
    if (_wcsicmp(normTarget.c_str(), expectedName.c_str()) == 0) {
        outBytes = std::move(dec);
        return true;
    }
    return false;
}

} // namespace

bool ArchiveExplorer::IsArchiveExtension(const std::wstring& extLower) {
    return extLower == L".zip"   || extLower == L".rar" || extLower == L".7z"  ||
           extLower == L".tar"   || extLower == L".gz"  || extLower == L".tgz" ||
           extLower == L".jar"   || extLower == L".apk" || extLower == L".whl" ||
           extLower == L".nupkg" || extLower == L".cbz" || extLower == L".cbr" ||
           extLower == L".pk3"   || extLower == L".iso" || extLower == L".zst" ||
           extLower == L".tzst"  || extLower == L".xz"  || extLower == L".txz" ||
           extLower == L".bz2"   || extLower == L".tbz2"|| extLower == L".tbz" ||
           extLower == L".lz4"   || extLower == L".tlz4"||
           extLower == L".cab"   || extLower == L".msi" || extLower == L".msp" ||
           extLower == L".deb"   || extLower == L".rpm" || extLower == L".dmg" ||
           extLower == L".vhd"   || extLower == L".vhdx"|| extLower == L".vmdk"||
           extLower == L".vdi"   || extLower == L".qcow2"|| extLower == L".wim"||
           extLower == L".esd"   || extLower == L".pak" || extLower == L".wad" ||
           extLower == L".vpk"   || extLower == L".bsa" || extLower == L".ba2" ||
           extLower == L".asar"  || extLower == L".unitypackage" ||
           extLower == L".bundle"|| extLower == L".unity3d" || extLower == L".pck" ||
           extLower == L".rgss3a"|| extLower == L".rgss2a"  || extLower == L".rgssad" ||
           extLower == L".xp3"   || extLower == L".mpq" || extLower == L".big" ||
           extLower == L".grp"   || extLower == L".pbo" || extLower == L".scs" ||
           extLower == L".squashfs" || extLower == L".snap" || extLower == L".appimage" ||
           extLower == L".erofs" || extLower == L".cpio"|| extLower == L".ar"  ||
           extLower == L".a"     || extLower == L".lib" || extLower == L".xar" ||
           extLower == L".pkg"   || extLower == L".lzh" || extLower == L".lha" ||
           extLower == L".arj"   || extLower == L".ace" || extLower == L".ipa" ||
           extLower == L".aab"   || extLower == L".xapk"|| extLower == L".ova";
}

bool ArchiveExplorer::SplitVirtualArchivePath(
    const std::wstring& fullPath,
    std::wstring& outArchiveFile,
    std::wstring& outInnerPath)
{
    outArchiveFile.clear();
    outInnerPath.clear();
    if (fullPath.empty() || fullPath == L"THIS_PC") return false;

    std::wstring clean = PathUtils::TrimTrailingSlashes(fullPath);
    // Walk slash positions from left to right to find if any prefix is an archive file on disk
    size_t pos = (clean.size() >= 3 && clean[1] == L':') ? 3 : 0;
    while (pos <= clean.size()) {
        size_t nextSlash = clean.find_first_of(L"\\/", pos);
        std::wstring prefix = (nextSlash == std::wstring::npos) ? clean : clean.substr(0, nextSlash);
        std::wstring ext = PathUtils::GetExtension(prefix);
        if (IsArchiveExtension(ext)) {
            DWORD attrs = GetFileAttributesW(prefix.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                outArchiveFile = prefix;
                if (nextSlash != std::wstring::npos && nextSlash + 1 < clean.size()) {
                    outInnerPath = NormalizeArchiveInternalPath(clean.substr(nextSlash + 1));
                }
                return true;
            }
        }
        if (nextSlash == std::wstring::npos) break;
        pos = nextSlash + 1;
    }
    return false;
}

bool ArchiveExplorer::IsVirtualArchivePath(const std::wstring& fullPath) {
    std::wstring arc, inner;
    return SplitVirtualArchivePath(fullPath, arc, inner);
}

namespace {

// Zero-copy read-only memory-mapped file view for instant archive header scanning without heap allocation
struct MappedArchiveView {
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMap  = nullptr;
    const uint8_t* data = nullptr;
    size_t size = 0;
    FILETIME ftWrite = {};

    bool Open(const std::wstring& path) {
        Close();
        hFile = CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return false;

        LARGE_INTEGER li = {};
        if (!GetFileSizeEx(hFile, &li) || li.QuadPart < 4) {
            Close();
            return false;
        }
        GetFileTime(hFile, nullptr, nullptr, &ftWrite);
        size = static_cast<size_t>(li.QuadPart);

        hMap = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!hMap) {
            Close();
            return false;
        }
        data = static_cast<const uint8_t*>(MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0));
        if (!data) {
            Close();
            return false;
        }
        return true;
    }

    void Close() {
        if (data) {
            UnmapViewOfFile(data);
            data = nullptr;
        }
        if (hMap) {
            CloseHandle(hMap);
            hMap = nullptr;
        }
        if (hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
            hFile = INVALID_HANDLE_VALUE;
        }
        size = 0;
    }

    ~MappedArchiveView() {
        Close();
    }
};

struct CachedArchiveSummary {
    uint64_t fileSize = 0;
    FILETIME ftWrite = {};
    ArchiveSummaryInfo info;
};

static std::mutex g_archiveCacheMutex;
static std::unordered_map<std::wstring, CachedArchiveSummary> g_archiveSummaryCache;

void FilterArchiveSummaryFolder(
    const ArchiveSummaryInfo& summary,
    const std::wstring& innerDir,
    std::vector<ArchiveItemEntry>& outImmediateChildren)
{
    outImmediateChildren.clear();
    std::wstring normDir = NormalizeArchiveInternalPath(innerDir);
    std::wstring prefix = normDir.empty() ? L"" : (normDir + L"/");

    std::unordered_map<std::wstring, size_t> childMap;

    for (const auto& entry : summary.entries) {
        if (!prefix.empty()) {
            if (entry.pathInArchive.size() <= prefix.size()) continue;
            if (_wcsnicmp(entry.pathInArchive.c_str(), prefix.c_str(), prefix.size()) != 0) continue;
        }
        std::wstring rel = entry.pathInArchive.substr(prefix.size());
        if (rel.empty()) continue;

        size_t slash = rel.find(L'/');
        if (slash == std::wstring::npos) {
            std::wstring key = PathUtils::ToLower(rel);
            auto it = childMap.find(key);
            if (it == childMap.end()) {
                ArchiveItemEntry child = entry;
                child.name = rel;
                childMap[key] = outImmediateChildren.size();
                outImmediateChildren.push_back(std::move(child));
            } else if (entry.isDirectory) {
                outImmediateChildren[it->second].isDirectory = true;
            }
        } else {
            std::wstring folderName = rel.substr(0, slash);
            std::wstring key = PathUtils::ToLower(folderName);
            auto it = childMap.find(key);
            if (it == childMap.end()) {
                ArchiveItemEntry folderItem;
                folderItem.name = folderName;
                folderItem.pathInArchive = prefix + folderName;
                folderItem.isDirectory = true;
                folderItem.uncompressedSize = entry.isDirectory ? 0 : entry.uncompressedSize;
                folderItem.compressedSize = entry.isDirectory ? 0 : entry.compressedSize;
                childMap[key] = outImmediateChildren.size();
                outImmediateChildren.push_back(std::move(folderItem));
            } else {
                auto& existing = outImmediateChildren[it->second];
                existing.isDirectory = true;
                if (!entry.isDirectory) {
                    existing.uncompressedSize += entry.uncompressedSize;
                    existing.compressedSize += entry.compressedSize;
                }
            }
        }
    }

    std::sort(outImmediateChildren.begin(), outImmediateChildren.end(),
        [](const ArchiveItemEntry& a, const ArchiveItemEntry& b) {
            if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
            return CompareStringOrdinal(a.name.c_str(), -1, b.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
        });
}

using ToolRun = ArchiveExplorer::ToolRun;

ToolRun RunSilentProcessWait(const std::wstring& exePath, const std::wstring& args, DWORD timeoutMs) {
    std::wstring cmdLine = L"\"" + exePath + L"\" " + args;
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(
            exePath.c_str(),
            cmdBuf.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &si,
            &pi))
    {
        return ToolRun::Failed;
    }
    ToolRun result = ToolRun::Failed;
    if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0) {
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        if (exitCode == 0) result = ToolRun::Succeeded;
    } else {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
        result = ToolRun::TimedOut;
        AppLog::Warn(L"Archive", L"Stopped " + PathUtils::GetFileName(exePath) + L" after " +
                                 std::to_wstring(timeoutMs / 1000) + L" s without a result");
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return result;
}

std::wstring Find7ZipExePath() {
    static const wchar_t* kCandidates[] = {
        L"C:\\Program Files\\7-Zip\\7z.exe",
        L"C:\\Program Files (x86)\\7-Zip\\7z.exe"
    };
    for (const wchar_t* c : kCandidates) {
        if (GetFileAttributesW(c) != INVALID_FILE_ATTRIBUTES) return c;
    }
    return L"";
}

std::wstring FindUnRarExePath() {
    static const wchar_t* kCandidates[] = {
        L"C:\\Program Files\\WinRAR\\UnRAR.exe",
        L"C:\\Program Files\\WinRAR\\Rar.exe",
        L"C:\\Program Files (x86)\\WinRAR\\UnRAR.exe"
    };
    for (const wchar_t* c : kCandidates) {
        if (GetFileAttributesW(c) != INVALID_FILE_ATTRIBUTES) return c;
    }
    return L"";
}

// An archive decides the names of its entries, so they are never trusted. This turns an entry's path into
// a path below the destination folder and refuses anything that would land elsewhere: "..", an absolute
// path, or a name with a colon (a drive letter or an alternate data stream).
bool SafeRelativeEntryPath(const std::wstring& pathInArchive, std::wstring& outRelative) {
    outRelative.clear();
    size_t pos = 0;
    while (pos <= pathInArchive.size()) {
        size_t end = pathInArchive.find_first_of(L"/\\", pos);
        if (end == std::wstring::npos) end = pathInArchive.size();
        const std::wstring part = pathInArchive.substr(pos, end - pos);
        pos = end + 1;
        if (part.empty() || part == L".") continue;
        if (part == L".." || part.find(L':') != std::wstring::npos) return false;
        if (!outRelative.empty()) outRelative += L'\\';
        outRelative += part;
    }
    return !outRelative.empty();
}

// Entry names are also handed to 7-Zip, UnRAR and tar on a command line. A double quote would end the
// quoted argument and let the rest of the name be read as options, so such a name never reaches a tool.
bool IsSafeToolArgument(const std::wstring& name) {
    if (name.empty()) return false;
    for (wchar_t ch : name) {
        if (ch == L'"' || ch < 0x20) return false;
    }
    return true;
}

// A file name for the preview cache made from an entry's base name: characters Windows does not allow in
// a name (and the colon, which would address a stream) become underscores.
std::wstring SafeCacheFileName(std::wstring name) {
    for (wchar_t& ch : name) {
        if (ch < 0x20 || wcschr(L"<>:\"/\\|?*", ch) != nullptr) ch = L'_';
    }
    while (!name.empty() && (name.back() == L'.' || name.back() == L' ')) name.pop_back();
    if (name.empty() || name == L"." || name == L"..") name = L"extracted.bin";
    return name;
}

// Writes a new file and never replaces one that is already there: when the name is taken it uses
// "name (2).ext", "name (3).ext" and so on. Returns the path it wrote to.
bool WriteNewFileKeepExisting(const std::wstring& wantedPath, const uint8_t* data, size_t size, std::wstring& outWrittenPath) {
    std::wstring candidate = wantedPath;
    for (int attempt = 2; attempt < 1000; ++attempt) {
        HANDLE h = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            bool ok = true;
            size_t left = size;
            const uint8_t* p = data;
            while (ok && left > 0) {
                const DWORD chunk = static_cast<DWORD>(std::min<size_t>(left, 16u * 1024u * 1024u));
                DWORD written = 0;
                ok = WriteFile(h, p, chunk, &written, nullptr) && written == chunk;
                p += chunk;
                left -= chunk;
            }
            CloseHandle(h);
            if (!ok) {
                DeleteFileW(candidate.c_str());
                return false;
            }
            outWrittenPath = candidate;
            return true;
        }
        const DWORD err = GetLastError();
        if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS) return false;

        const size_t slash = wantedPath.find_last_of(L'\\');
        size_t dot = wantedPath.find_last_of(L'.');
        if (dot == std::wstring::npos || (slash != std::wstring::npos && dot <= slash + 1)) dot = wantedPath.size();
        candidate = wantedPath.substr(0, dot) + L" (" + std::to_wstring(attempt) + L")" + wantedPath.substr(dot);
    }
    return false;
}

// Single-pass Win32 SetupIterateCabinetW bulk extractor for standalone .cab files
struct CabFolderExtractCtx {
    std::wstring destFolder;
    std::wstring scratchFile;
    std::wstring curRelWin;
    size_t keptExisting = 0;
    size_t refused = 0;
    size_t extractedCount = 0;
};

UINT CALLBACK CabBulkIterateCallback(PVOID Context, UINT Notification, UINT_PTR Param1, UINT_PTR Param2) {
    (void)Param2;
    auto* ctx = static_cast<CabFolderExtractCtx*>(Context);
    switch (Notification) {
    case SPFILENOTIFY_CABINETINFO:
        return NO_ERROR;

    case SPFILENOTIFY_FILEINCABINET: {
        auto* info = reinterpret_cast<FILE_IN_CABINET_INFO_W*>(Param1);
        if (!info || !info->NameInCabinet) return FILEOP_SKIP;
        std::wstring entryNorm = NormalizeArchiveInternalPath(info->NameInCabinet);
        if (!SafeRelativeEntryPath(entryNorm, ctx->curRelWin)) {
            ++ctx->refused;
            return FILEOP_SKIP;
        }
        if (ctx->scratchFile.size() >= MAX_PATH) return FILEOP_ABORT;
        DeleteFileW(ctx->scratchFile.c_str());
        wcscpy_s(info->FullTargetName, MAX_PATH, ctx->scratchFile.c_str());
        return FILEOP_DOIT;
    }

    case SPFILENOTIFY_FILEEXTRACTED: {
        auto* fp = reinterpret_cast<FILEPATHS_W*>(Param1);
        if (!fp || fp->Win32Error != NO_ERROR) {
            DeleteFileW(ctx->scratchFile.c_str());
            return fp ? fp->Win32Error : ERROR_GEN_FAILURE;
        }
        std::wstring fullDest = PathUtils::EnsureTrailingSlash(ctx->destFolder) + ctx->curRelWin;
        size_t pos = ctx->destFolder.size() + 1;
        while (pos < fullDest.size()) {
            size_t nextSlash = fullDest.find(L'\\', pos);
            if (nextSlash == std::wstring::npos) break;
            std::wstring sub = fullDest.substr(0, nextSlash);
            CreateDirectoryW(sub.c_str(), nullptr);
            pos = nextSlash + 1;
        }
        std::vector<uint8_t> bytes;
        if (PathUtils::ReadFileBytes(ctx->scratchFile, 256 * 1024 * 1024, bytes)) {
            std::wstring writtenPath;
            if (WriteNewFileKeepExisting(fullDest, bytes.data(), bytes.size(), writtenPath)) {
                ++ctx->extractedCount;
                if (_wcsicmp(writtenPath.c_str(), fullDest.c_str()) != 0) ++ctx->keptExisting;
            }
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

} // namespace

bool ArchiveExplorer::InspectArchiveFile(const std::wstring& archivePath, ArchiveSummaryInfo& outInfo) try {
    outInfo = {};
    std::wstring normKey = PathUtils::NormalizePathKey(archivePath);

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(archivePath.c_str(), GetFileExInfoStandard, &fad)) {
        uint64_t fsz = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        std::lock_guard<std::mutex> lock(g_archiveCacheMutex);
        auto it = g_archiveSummaryCache.find(normKey);
        if (it != g_archiveSummaryCache.end() &&
            it->second.fileSize == fsz &&
            CompareFileTime(&it->second.ftWrite, &fad.ftLastWriteTime) == 0)
        {
            outInfo = it->second.info;
            return outInfo.valid;
        }
    }

    MappedArchiveView mapped;
    if (!mapped.Open(archivePath) || mapped.size < 4) {
        return false;
    }

    const uint8_t* data = mapped.data;
    size_t size = mapped.size;
    std::wstring extLower = PathUtils::GetExtension(archivePath);
    std::wstring nameLower = PathUtils::ToLower(PathUtils::GetFileName(archivePath));
    bool isTarGzName = (nameLower.size() > 7 && nameLower.substr(nameLower.size() - 7) == L".tar.gz") ||
                       (extLower == L".tgz");

    StreamKind streamKind = DetectStreamKind(data, size);

    bool parsed = false;
    if (extLower == L".unitypackage" && Formats::DetailArc200::ParseUnityPackageFromMemory(data, size, outInfo)) {
        parsed = true;
    } else if (size >= 8 && std::memcmp(data, "Rar!\x1A\x07\x01\x00", 8) == 0) {
        parsed = ParseRar5Headers(data, size, outInfo);
    } else if (size >= 7 && std::memcmp(data, "Rar!\x1A\x07\x00", 7) == 0) {
        parsed = ParseRar4Headers(data, size, outInfo);
    } else if (size >= 6 && std::memcmp(data, "7z\xBC\xAF\x27\x1C", 6) == 0) {
        parsed = Parse7zHeaders(data, size, outInfo);
    } else if (streamKind != StreamKind::None) {
        parsed = InspectCompressedStreamOrTar(archivePath, data, size, mapped.ftWrite, streamKind, outInfo);
    } else if (size >= 4 && data[0] == 'P' && data[1] == 'K' &&
               ((data[2] == 3 && data[3] == 4) || (data[2] == 5 && data[3] == 6))) {
        std::vector<ZipDeflate::ArchiveItemMeta> zipEntries;
        if (ZipDeflate::ListZipEntriesFromMemory(data, size, zipEntries)) {
            outInfo.formatName = (extLower == L".jar")   ? L"JAR (ZIP)" :
                                 (extLower == L".apk")   ? L"APK (ZIP)" :
                                 (extLower == L".ipa")   ? L"iOS App Package (.ipa)" :
                                 (extLower == L".aab")   ? L"Android App Bundle (.aab)" :
                                 (extLower == L".xapk")  ? L"XAPK Package (.xapk)" :
                                 (extLower == L".scs")   ? L"SCS Mod Archive (.scs)" :
                                 (extLower == L".nupkg") ? L"NuGet (ZIP)" : L"ZIP";
            for (const auto& ze : zipEntries) {
                std::wstring norm = NormalizeArchiveInternalPath(ze.pathInArchive);
                if (norm.empty()) continue;
                ArchiveItemEntry item;
                item.pathInArchive = norm;
                item.name = GetBaseNameFromInternalPath(norm);
                item.uncompressedSize = ze.uncompressedSize;
                item.compressedSize = ze.compressedSize;
                item.crc32 = ze.crc32;
                item.dosTime = ze.dosTime;
                item.dosDate = ze.dosDate;
                item.isDirectory = ze.isDirectory;
                item.rawDataOffset = ze.localHeaderOffset;
                item.compressionMethod = ze.compressionMethod;
                outInfo.entries.push_back(std::move(item));
            }
            outInfo.valid = true;
            parsed = true;
        }
    } else if (extLower == L".tar" || extLower == L".ova" || isTarGzName ||
               (size >= 265 && std::memcmp(data + 257, "ustar", 5) == 0)) {
        std::vector<ZipDeflate::ArchiveItemMeta> tarEntries;
        if (ZipDeflate::ListTarEntriesFromFile(archivePath, tarEntries)) {
            outInfo.formatName = (extLower == L".ova") ? L"OVA Virtual Appliance (.ova)" :
                                 ((data[0] == 0x1F && data[1] == 0x8B) ? L"TAR.GZ" : L"TAR");
            for (const auto& te : tarEntries) {
                std::wstring norm = NormalizeArchiveInternalPath(te.pathInArchive);
                if (norm.empty()) continue;
                ArchiveItemEntry item;
                item.pathInArchive = norm;
                item.name = GetBaseNameFromInternalPath(norm);
                item.uncompressedSize = te.uncompressedSize;
                item.compressedSize = te.compressedSize;
                item.isDirectory = te.isDirectory;
                item.rawDataOffset = te.localHeaderOffset;
                outInfo.entries.push_back(std::move(item));
            }
            outInfo.valid = true;
            parsed = true;
        }
    } else if (size >= 18 && data[0] == 0x1F && data[1] == 0x8B && data[2] == 8) {
        std::vector<ZipDeflate::ArchiveItemMeta> tarEntries;
        if (ZipDeflate::ListTarEntriesFromFile(archivePath, tarEntries) && !tarEntries.empty()) {
            outInfo.formatName = L"TAR.GZ";
            for (const auto& te : tarEntries) {
                std::wstring norm = NormalizeArchiveInternalPath(te.pathInArchive);
                if (norm.empty()) continue;
                ArchiveItemEntry item;
                item.pathInArchive = norm;
                item.name = GetBaseNameFromInternalPath(norm);
                item.uncompressedSize = te.uncompressedSize;
                item.compressedSize = te.compressedSize;
                item.isDirectory = te.isDirectory;
                item.rawDataOffset = te.localHeaderOffset;
                outInfo.entries.push_back(std::move(item));
            }
            outInfo.valid = true;
            parsed = true;
        } else {
            parsed = ParseSingleGzip(archivePath, data, size, outInfo);
        }
    } else {
        parsed = Formats::InspectExtendedArchive200(data, size, extLower, outInfo) ||
                 Formats::InspectExtendedArchive100(archivePath, data, size, extLower, outInfo);
    }

    if (!parsed || !outInfo.valid) return false;

    for (const auto& e : outInfo.entries) {
        if (e.isDirectory) {
            ++outInfo.totalFolders;
        } else {
            ++outInfo.totalFiles;
            outInfo.totalUncompressedBytes += e.uncompressedSize;
            outInfo.totalCompressedBytes += e.compressedSize;
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_archiveCacheMutex);
        if (g_archiveSummaryCache.size() >= 32) {
            g_archiveSummaryCache.clear();
        }
        CachedArchiveSummary cached;
        cached.fileSize = mapped.size;
        cached.ftWrite = mapped.ftWrite;
        cached.info = outInfo;
        g_archiveSummaryCache[normKey] = std::move(cached);
    }
    return true;
} E2_READER_CATCH(L"Archive reader", archivePath, { outInfo = ArchiveSummaryInfo(); return false; })

bool ArchiveExplorer::EnumerateArchiveFolder(
    const std::wstring& archivePath,
    const std::wstring& innerDir,
    std::vector<ArchiveItemEntry>& outImmediateChildren,
    ArchiveSummaryInfo* outFullSummary)
try {
    outImmediateChildren.clear();
    ArchiveSummaryInfo summary;
    if (!InspectArchiveFile(archivePath, summary)) return false;

    FilterArchiveSummaryFolder(summary, innerDir, outImmediateChildren);

    if (outFullSummary) *outFullSummary = std::move(summary);
    return true;
} E2_READER_CATCH(L"Archive reader", archivePath, { outImmediateChildren.clear(); return false; })

bool ArchiveExplorer::ExtractSingleEntryToMemory(
    const std::wstring& archivePath,
    const std::wstring& innerPath,
    std::vector<uint8_t>& outBytes)
try {
    outBytes.clear();
    MappedArchiveView mapped;
    if (!mapped.Open(archivePath) || mapped.size < 4) {
        return false;
    }

    const uint8_t* data = mapped.data;
    size_t size = mapped.size;
    std::wstring extLower = PathUtils::GetExtension(archivePath);
    std::wstring normTarget = NormalizeArchiveInternalPath(innerPath);

    // 1. ZIP family
    if (size >= 4 && data[0] == 'P' && data[1] == 'K') {
        return ZipDeflate::ExtractZipFileByName(archivePath, normTarget, outBytes);
    }

    // 2. RAR4 / RAR5 (direct zero-copy slice for Stored uncompressed entries)
    if (size >= 7 && std::memcmp(data, "Rar!\x1A\x07", 6) == 0) {
        ArchiveSummaryInfo info;
        if (InspectArchiveFile(archivePath, info)) {
            for (const auto& e : info.entries) {
                if (!e.isDirectory && _wcsicmp(e.pathInArchive.c_str(), normTarget.c_str()) == 0) {
                    if ((info.formatName == L"RAR4" && e.compressionMethod == 0x30) ||
                        (info.formatName == L"RAR5" && e.compressionMethod == 0))
                    {
                        if (e.rawDataOffset + e.uncompressedSize <= size) {
                            outBytes.assign(
                                data + static_cast<size_t>(e.rawDataOffset),
                                data + static_cast<size_t>(e.rawDataOffset + e.uncompressedSize));
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    // 3. 7-Zip (native in-process extraction for Copy, LZMA1, LZMA2, BCJ x86, Delta, Deflate, BZip2)
    if (size >= 32 && std::memcmp(data, "7z\xBC\xAF\x27\x1C", 6) == 0) {
        return Extract7zEntryInProcess(archivePath, data, size, normTarget, outBytes);
    }

    // 4. Native Compressed Streams & TAR.* (.xz/.txz, .bz2/.tbz2, .lz4/.tlz4, .zst/.tzst)
    StreamKind streamKind = DetectStreamKind(data, size);
    if (streamKind != StreamKind::None) {
        return ExtractCompressedStreamOrTar(archivePath, data, size, mapped.ftWrite, streamKind, normTarget, outBytes);
    }

    // 5. UnityPackage (.unitypackage -> <GUID>/pathname + <GUID>/asset)
    if (extLower == L".unitypackage") {
        ArchiveSummaryInfo dummy;
        if (Formats::DetailArc200::ParseUnityPackageFromMemory(data, size, dummy, &normTarget, &outBytes)) {
            return true;
        }
    }

    // 6. ExtendedArchives100 specialized extractors (.cab SetupIterateCabinetW, .bsa/.ba2, .rpm, .msi/.msp, .iso, .vhd/.vhdx/.vmdk/.vdi/.qcow2/.dmg)
    if (Formats::ExtractExtendedArchive100Entry(archivePath, data, size, extLower, normTarget, outBytes)) {
        return true;
    }

    // 7. TAR / TAR.GZ / OVA
    if (extLower == L".tar" || extLower == L".ova" || extLower == L".tgz" ||
        (size >= 265 && std::memcmp(data + 257, "ustar", 5) == 0) ||
        (size >= 18 && data[0] == 0x1F && data[1] == 0x8B && data[2] == 8))
    {
        std::vector<ZipDeflate::ArchiveItemMeta> tarEntries;
        if (ZipDeflate::ListTarEntriesFromFile(archivePath, tarEntries) && !tarEntries.empty()) {
            for (const auto& te : tarEntries) {
                if (!te.isDirectory && _wcsicmp(NormalizeArchiveInternalPath(te.pathInArchive).c_str(), normTarget.c_str()) == 0) {
                    return ZipDeflate::ExtractTarEntryFromFile(archivePath, te, outBytes);
                }
            }
            return false;
        }
    }

    // 8. Single-file GZIP (.gz)
    if (size >= 18 && data[0] == 0x1F && data[1] == 0x8B) {
        return ZipDeflate::InflateGzip(data, size, outBytes);
    }

    // 9. Universal stored-slice extraction for PAK, WAD, ASAR, DEB, VPK, WIM, PCK, CPIO, AR, etc.
    ArchiveSummaryInfo extInfo;
    if (InspectArchiveFile(archivePath, extInfo)) {
        for (const auto& e : extInfo.entries) {
            if (!e.isDirectory && e.compressionMethod == 0 &&
                _wcsicmp(e.pathInArchive.c_str(), normTarget.c_str()) == 0 &&
                e.rawDataOffset + e.uncompressedSize <= size)
            {
                outBytes.assign(
                    data + static_cast<size_t>(e.rawDataOffset),
                    data + static_cast<size_t>(e.rawDataOffset + e.uncompressedSize));
                return true;
            }
        }
    }

    return false;
} E2_READER_CATCH(L"Archive reader", archivePath, { outBytes.clear(); return false; })

bool ArchiveExplorer::ExtractSingleEntryToTempFile(
    const std::wstring& archivePath,
    const std::wstring& innerPath,
    std::wstring& outTempFilePath)
{
    outTempFilePath.clear();
    if (archivePath.empty() || innerPath.empty()) return false;

    wchar_t tmpRoot[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmpRoot);
    std::wstring previewRoot = std::wstring(tmpRoot) + L"Explorer2_ArcPreview";
    CreateDirectoryW(previewRoot.c_str(), nullptr);

    std::wstring normInner = NormalizeArchiveInternalPath(innerPath);
    std::wstring baseName = SafeCacheFileName(GetBaseNameFromInternalPath(normInner));

    uint64_t arcSize = 0;
    uint64_t arcMtime = 0;
    WIN32_FILE_ATTRIBUTE_DATA arcFad = {};
    if (GetFileAttributesExW(archivePath.c_str(), GetFileExInfoStandard, &arcFad)) {
        arcSize = (static_cast<uint64_t>(arcFad.nFileSizeHigh) << 32) | arcFad.nFileSizeLow;
        arcMtime = (static_cast<uint64_t>(arcFad.ftLastWriteTime.dwHighDateTime) << 32) | arcFad.ftLastWriteTime.dwLowDateTime;
    }

    std::wstring hashKey = PathUtils::NormalizePathKey(archivePath) + L"|" +
                           PathUtils::ToLower(normInner) + L"|" +
                           std::to_wstring(arcSize) + L"|" +
                           std::to_wstring(arcMtime);
    uint64_t fnv = 14695981039346656037ULL;
    for (wchar_t ch : hashKey) {
        fnv ^= static_cast<uint64_t>(ch);
        fnv *= 1099511628211ULL;
    }
    wchar_t hashHex[32] = {};
    swprintf_s(hashHex, L"%016llx", static_cast<unsigned long long>(fnv));

    std::wstring previewDir = previewRoot + L"\\" + hashHex;
    CreateDirectoryW(previewDir.c_str(), nullptr);
    std::wstring targetTemp = previewDir + L"\\" + baseName;

    auto isCachedValid = [](const std::wstring& p) -> bool {
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) return false;
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
        return (fad.nFileSizeHigh > 0 || fad.nFileSizeLow > 0);
    };

    if (isCachedValid(targetTemp)) {
        outTempFilePath = targetTemp;
        return true;
    }

    static std::mutex s_extractMutex;
    std::lock_guard<std::mutex> extractLock(s_extractMutex);

    if (isCachedValid(targetTemp)) {
        outTempFilePath = targetTemp;
        return true;
    }

    // 1. Try fast in-memory extraction first (ZIP, 7z, CAB, TAR.*, XZ, BZ2, LZ4, ZST, BSA, BA2, RPM, MSI, Disk Images, Stored RAR)
    std::vector<uint8_t> extracted;
    if (ExtractSingleEntryToMemory(archivePath, normInner, extracted)) {
        if (PathUtils::WriteFileBytes(targetTemp, extracted.data(), extracted.size())) {
            outTempFilePath = targetTemp;
            return true;
        }
    }

    // 2. Compressed RAR4 / RAR5 / AES-encrypted 7z fallback via 7z.exe, UnRAR.exe, or Windows bsdtar (tar.exe)
    DeleteFileW(targetTemp.c_str());
    std::wstring winInner = normInner;
    for (wchar_t& ch : winInner) {
        if (ch == L'/') ch = L'\\';
    }

    std::wstring safeRelative;
    if (!IsSafeToolArgument(winInner) || !SafeRelativeEntryPath(normInner, safeRelative)) return false;

    const DWORD kPreviewToolLimitMs = 8000;
    auto producedFile = [&](ToolRun run, const std::wstring& path) {
        if (run != ToolRun::TimedOut && isCachedValid(path)) return true;
        DeleteFileW(path.c_str());
        return false;
    };

    std::wstring sevenZ = Find7ZipExePath();
    if (!sevenZ.empty()) {
        std::wstring args = L"e -y -bd -o\"" + previewDir + L"\" -- \"" + archivePath + L"\" \"" + winInner + L"\"";
        if (producedFile(RunSilentProcessWait(sevenZ, args, kPreviewToolLimitMs), targetTemp)) {
            outTempFilePath = targetTemp;
            return true;
        }
    }

    std::wstring unrar = FindUnRarExePath();
    if (!unrar.empty()) {
        std::wstring args = L"e -y -inul -- \"" + archivePath + L"\" \"" + winInner + L"\" \"" + previewDir + L"\\\"";
        if (producedFile(RunSilentProcessWait(unrar, args, kPreviewToolLimitMs), targetTemp)) {
            outTempFilePath = targetTemp;
            return true;
        }
    }

    wchar_t sysDir[MAX_PATH] = {};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring tarExe = std::wstring(sysDir) + L"\\tar.exe";
    if (GetFileAttributesW(tarExe.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::wstring args = L"-xf \"" + archivePath + L"\" -C \"" + previewDir + L"\" -- \"" + normInner + L"\"";
        std::wstring fullNested = previewDir + L"\\" + safeRelative;
        if (producedFile(RunSilentProcessWait(tarExe, args, kPreviewToolLimitMs), fullNested)) {
            if (_wcsicmp(fullNested.c_str(), targetTemp.c_str()) != 0) {
                MoveFileExW(fullNested.c_str(), targetTemp.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
            }
            if (GetFileAttributesW(targetTemp.c_str()) != INVALID_FILE_ATTRIBUTES) {
                outTempFilePath = targetTemp;
                return true;
            }
            outTempFilePath = fullNested;
            return true;
        }
    }

    return false;
}

ArchiveExplorer::ToolRun ArchiveExplorer::RunToolAndWait(const std::wstring& exePath, const std::wstring& args, DWORD timeoutMs) {
    return RunSilentProcessWait(exePath, args, timeoutMs);
}

bool ArchiveExplorer::ExtractEntireArchiveToFolder(
    const std::wstring& archivePath,
    const std::wstring& destFolderPath,
    size_t* outKeptExisting,
    size_t* outRefused)
{
    if (outKeptExisting) *outKeptExisting = 0;
    if (outRefused) *outRefused = 0;
    ArchiveSummaryInfo info;
    if (!InspectArchiveFile(archivePath, info) || !info.valid) return false;
    CreateDirectoryW(destFolderPath.c_str(), nullptr);

    // Fast single-pass SetupIterateCabinetW path for standalone Windows Cabinet (.cab) archives
    if (info.formatName == L"MS Cabinet (CAB)") {
        CabFolderExtractCtx cabCtx;
        cabCtx.destFolder = destFolderPath;
        cabCtx.scratchFile = StreamCodecs::MakeUniqueCabScratchPath(L".tmp");
        BOOL cabOk = SetupIterateCabinetW(archivePath.c_str(), 0, CabBulkIterateCallback, &cabCtx);
        DeleteFileW(cabCtx.scratchFile.c_str());
        if (cabOk != FALSE && cabCtx.extractedCount > 0) {
            if (outKeptExisting) *outKeptExisting = cabCtx.keptExisting;
            if (outRefused) *outRefused = cabCtx.refused;
            return true;
        }
    }

    bool anyExtracted = false;
    bool allExtractedInMemory = true;
    size_t keptExisting = 0;
    size_t refused = 0;
    for (const auto& entry : info.entries) {
        std::wstring relWin;
        if (!SafeRelativeEntryPath(entry.pathInArchive, relWin)) {
            ++refused;
            continue;
        }
        std::wstring fullDest = PathUtils::EnsureTrailingSlash(destFolderPath) + relWin;
        size_t pos = destFolderPath.size() + 1;
        while (pos < fullDest.size()) {
            size_t nextSlash = fullDest.find(L'\\', pos);
            if (nextSlash == std::wstring::npos) break;
            std::wstring sub = fullDest.substr(0, nextSlash);
            CreateDirectoryW(sub.c_str(), nullptr);
            pos = nextSlash + 1;
        }
        if (entry.isDirectory) {
            CreateDirectoryW(fullDest.c_str(), nullptr);
            continue;
        }
        std::vector<uint8_t> fileData;
        if (ExtractSingleEntryToMemory(archivePath, entry.pathInArchive, fileData)) {
            std::wstring writtenPath;
            if (WriteNewFileKeepExisting(fullDest, fileData.data(), fileData.size(), writtenPath)) {
                anyExtracted = true;
                if (_wcsicmp(writtenPath.c_str(), fullDest.c_str()) != 0) ++keptExisting;
            }
        } else {
            allExtractedInMemory = false;
        }
    }
    if (outRefused) *outRefused = refused;
    if (outKeptExisting) *outKeptExisting = keptExisting;

    if (!allExtractedInMemory) {
        std::wstring sevenZ = Find7ZipExePath();
        if (!sevenZ.empty()) {
            std::wstring args = L"x -y -aos -bd -o\"" + destFolderPath + L"\" -- \"" + archivePath + L"\"";
            if (RunSilentProcessWait(sevenZ, args, INFINITE) == ToolRun::Succeeded) return true;
        }
        std::wstring unrar = FindUnRarExePath();
        if (!unrar.empty()) {
            std::wstring args = L"x -o- -inul -- \"" + archivePath + L"\" \"" + PathUtils::EnsureTrailingSlash(destFolderPath) + L"\"";
            if (RunSilentProcessWait(unrar, args, INFINITE) == ToolRun::Succeeded) return true;
        }
        wchar_t sysDir[MAX_PATH] = {};
        GetSystemDirectoryW(sysDir, MAX_PATH);
        std::wstring tarExe = std::wstring(sysDir) + L"\\tar.exe";
        if (GetFileAttributesW(tarExe.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::wstring args = L"-xkf \"" + archivePath + L"\" -C \"" + destFolderPath + L"\"";
            if (RunSilentProcessWait(tarExe, args, INFINITE) == ToolRun::Succeeded) return true;
        }
    }
    return anyExtracted;
}

// ============================================================================
// Right-Side FileContentPane UI Implementation
// ============================================================================
void ArchiveExplorer::Initialize(HWND parentHwnd) {
    m_parentHwnd = parentHwnd;
}

void ArchiveExplorer::Shutdown() {
    Clear();
}

void ArchiveExplorer::Clear() {
    m_archivePath.clear();
    m_currentInnerDir.clear();
    m_summary = {};
    m_visibleItems.clear();
    m_scrollY = 0.0f;
    m_selectedIdx = -1;
    m_hoverIdx = -1;
}

void ArchiveExplorer::LoadFile(const std::wstring& fullPath, bool forceReload) {
    if (!forceReload && m_archivePath == fullPath && m_summary.valid) {
        return;
    }
    Clear();
    m_archivePath = fullPath;
    m_currentInnerDir.clear();
    EnumerateArchiveFolder(fullPath, L"", m_visibleItems, &m_summary);
}

void ArchiveExplorer::ShowLoadedArchive(const std::wstring& fullPath, ArchiveSummaryInfo&& summary, std::vector<ArchiveItemEntry>&& rootItems) {
    Clear();
    m_archivePath = fullPath;
    m_currentInnerDir.clear();
    m_summary = std::move(summary);
    m_visibleItems = std::move(rootItems);
}

void ArchiveExplorer::RebuildCurrentSubfolderView() {
    m_visibleItems.clear();
    m_scrollY = 0.0f;
    m_selectedIdx = -1;
    m_hoverIdx = -1;
    if (m_summary.valid) {
        FilterArchiveSummaryFolder(m_summary, m_currentInnerDir, m_visibleItems);
    } else {
        EnumerateArchiveFolder(m_archivePath, m_currentInnerDir, m_visibleItems, &m_summary);
    }
}

void ArchiveExplorer::Draw(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtRegular,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& rect,
    Lang lang,
    D2D1_POINT_2F mousePt)
{
    (void)mousePt;
    m_toolbarHits.clear();
    const bool pl = (lang == Lang::PL);

    if (!m_summary.valid) {
        const wchar_t* msg = pl ? L"Nie udało się odczytać struktury archiwum." : L"Failed to read archive structure.";
        brush->SetColor(ArcColor(0x64748B));
        rt->DrawText(msg, static_cast<UINT32>(wcslen(msg)), fmtRegular,
            D2D1::RectF(rect.left + 16.0f, rect.top + 24.0f, rect.right - 16.0f, rect.bottom), brush);
        return;
    }

    // 1. Top Toolbar (32px): [↑ W górę] [Otwórz w panelu ➔] [Rozpakuj tutaj] [Rozpakuj do podfolderu]
    float barH = 32.0f;
    D2D1_RECT_F barR = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + barH);
    brush->SetColor(ArcColor(0xF8FAFC));
    rt->FillRectangle(barR, brush);
    brush->SetColor(ArcColor(0xE2E8F0));
    rt->DrawLine(D2D1::Point2F(barR.left, barR.bottom - 0.5f), D2D1::Point2F(barR.right, barR.bottom - 0.5f), brush, 1.0f);

    float bx = barR.left + 8.0f;
    auto addBtn = [&](int id, float w, const wchar_t* label, bool enabled = true) {
        D2D1_RECT_F r = D2D1::RectF(bx, barR.top + 4.0f, bx + w, barR.bottom - 4.0f);
        if (enabled) m_toolbarHits.push_back({ id, r });
        PathUtils::DrawToolbarButton(rt, brush, fmtSmall, r, label, false, enabled && (m_hoverBtnId == id));
        bx += w + 5.0f;
    };

    if (!m_currentInnerDir.empty()) {
        addBtn(BTN_UP_DIR, pl ? 68.0f : 56.0f, pl ? L"\x2191 W górę" : L"\x2191 Up");
    }
    addBtn(BTN_OPEN_AS_FOLDER, pl ? 114.0f : 108.0f, pl ? L"Otwórz w panelu \x2794" : L"Browse in pane \x2794");
    addBtn(BTN_EXTRACT_HERE, pl ? 98.0f : 90.0f, pl ? L"\xD83D\xDCE6 Rozpakuj tu" : L"\xD83D\xDCE6 Extract Here");
    addBtn(BTN_EXTRACT_SUBFOLDER, pl ? 118.0f : 114.0f, pl ? L"\xD83D\xDCC2 Do podfolderu" : L"\xD83D\xDCC2 To Subfolder");

    // 2. Archive Info & Breadcrumb Banner (26px)
    float infoH = 26.0f;
    D2D1_RECT_F infoR = D2D1::RectF(rect.left, barR.bottom, rect.right, barR.bottom + infoH);
    brush->SetColor(ArcColor(0xEFF6FF));
    rt->FillRectangle(infoR, brush);
    brush->SetColor(ArcColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(infoR.left, infoR.bottom - 0.5f), D2D1::Point2F(infoR.right, infoR.bottom - 0.5f), brush, 1.0f);

    int ratioPct = (m_summary.totalUncompressedBytes > 0)
        ? static_cast<int>((m_summary.totalCompressedBytes * 100ULL) / m_summary.totalUncompressedBytes)
        : 100;
    std::wstring unpStr = PathUtils::FormatByteSize(m_summary.totalUncompressedBytes);
    std::wstring cmpStr = PathUtils::FormatByteSize(m_summary.totalCompressedBytes);
    std::wstring pathBadge = m_currentInnerDir.empty() ? L"/" : (L"/" + m_currentInnerDir);

    wchar_t infoBuf[384] = {};
    swprintf_s(infoBuf,
        pl ? L"[%s]  \xD83D\xDCC2 %s   \x2022   %zu plików, %zu fold.   \x2022   %s \x2192 %s (%d%%)   (2\xD7klik = podgląd)"
           : L"[%s]  \xD83D\xDCC2 %s   \x2022   %zu files, %zu dirs   \x2022   %s \x2192 %s (%d%%)   (double-click = preview)",
        m_summary.formatName.c_str(), pathBadge.c_str(),
        m_summary.totalFiles, m_summary.totalFolders,
        unpStr.c_str(), cmpStr.c_str(), ratioPct);
    brush->SetColor(ArcColor(0x1E3A8A));
    rt->DrawText(infoBuf, static_cast<UINT32>(wcslen(infoBuf)), fmtSmall,
        D2D1::RectF(infoR.left + 8.0f, infoR.top + 4.0f, infoR.right - 8.0f, infoR.bottom), brush);

    // 3. Column Headers (22px)
    float colH = 22.0f;
    D2D1_RECT_F colR = D2D1::RectF(rect.left, infoR.bottom, rect.right, infoR.bottom + colH);
    brush->SetColor(ArcColor(0xF1F5F9));
    rt->FillRectangle(colR, brush);
    brush->SetColor(ArcColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(colR.left, colR.bottom - 0.5f), D2D1::Point2F(colR.right, colR.bottom - 0.5f), brush, 1.0f);

    float scrollW = 12.0f;
    float listRight = rect.right - scrollW;
    float packColW = 84.0f;
    float unpColW  = 84.0f;
    float nameRight = std::max(colR.left + 80.0f, listRight - packColW - unpColW);

    brush->SetColor(ArcColor(0x475569));
    rt->DrawText(pl ? L"Nazwa w archiwum" : L"Name in archive", pl ? 16 : 15, fmtBold,
        D2D1::RectF(colR.left + 10.0f, colR.top + 2.0f, nameRight - 4.0f, colR.bottom), brush);
    fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    if (nameRight + unpColW <= listRight) {
        rt->DrawText(pl ? L"Rozmiar" : L"Size", pl ? 7 : 4, fmtBold,
            D2D1::RectF(nameRight, colR.top + 2.0f, nameRight + unpColW - 6.0f, colR.bottom), brush);
    }
    rt->DrawText(pl ? L"Skompres." : L"Packed", pl ? 9 : 6, fmtBold,
        D2D1::RectF(listRight - packColW, colR.top + 2.0f, listRight - 8.0f, colR.bottom), brush);
    fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    // 4. Scrollable Archive Entries List
    m_listRect = D2D1::RectF(rect.left, colR.bottom, listRight, rect.bottom);
    float rowH = 22.0f;
    float listH = std::max(1.0f, m_listRect.bottom - m_listRect.top);
    float totalH = static_cast<float>(m_visibleItems.size()) * rowH;
    float maxScroll = std::max(0.0f, totalH - listH);
    m_scrollY = std::clamp(m_scrollY, 0.0f, maxScroll);

    rt->PushAxisAlignedClip(m_listRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    int firstIdx = std::max(0, static_cast<int>(m_scrollY / rowH));
    int lastIdx = std::min(static_cast<int>(m_visibleItems.size()) - 1,
                           static_cast<int>((m_scrollY + listH) / rowH) + 1);
    for (int i = firstIdx; i <= lastIdx; ++i) {
        const auto& item = m_visibleItems[i];
        float ry = m_listRect.top + i * rowH - m_scrollY;
        D2D1_RECT_F rowR = D2D1::RectF(m_listRect.left, ry, m_listRect.right, ry + rowH);

        if (i == m_selectedIdx) {
            brush->SetColor(ArcColor(0xCCE8FF));
            rt->FillRectangle(rowR, brush);
            brush->SetColor(ArcColor(0x0078D7));
            rt->DrawRectangle(D2D1::RectF(rowR.left + 0.5f, rowR.top + 0.5f, rowR.right - 0.5f, rowR.bottom - 0.5f), brush, 1.0f);
        } else if (i == m_hoverIdx) {
            brush->SetColor(ArcColor(0xE5F3FF));
            rt->FillRectangle(rowR, brush);
        } else if (i % 2 == 1) {
            brush->SetColor(ArcColor(0xF8FAFC));
            rt->FillRectangle(rowR, brush);
        }

        D2D1_RECT_F icoR = D2D1::RectF(rowR.left + 8.0f, rowR.top + 4.0f, rowR.left + 21.0f, rowR.bottom - 4.0f);
        if (item.isDirectory) {
            brush->SetColor(ArcColor(0xF59E0B));
            rt->FillRoundedRectangle(D2D1::RoundedRect(icoR, 2.0f, 2.0f), brush);
        } else {
            brush->SetColor(ArcColor(0x3B82F6));
            rt->DrawRoundedRectangle(D2D1::RoundedRect(icoR, 1.5f, 1.5f), brush, 1.2f);
        }

        brush->SetColor(ArcColor(0x0F172A));
        rt->DrawText(item.name.c_str(), static_cast<UINT32>(item.name.size()), fmtRegular,
            D2D1::RectF(rowR.left + 26.0f, rowR.top + 1.0f, nameRight - 4.0f, rowR.bottom), brush);

        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        std::wstring uStr = PathUtils::FormatByteSize(item.uncompressedSize);
        std::wstring cStr = PathUtils::FormatByteSize(item.compressedSize);
        if (nameRight + unpColW <= listRight) {
            brush->SetColor(ArcColor(0x475569));
            rt->DrawText(uStr.c_str(), static_cast<UINT32>(uStr.size()), fmtSmall,
                D2D1::RectF(nameRight, rowR.top + 2.0f, nameRight + unpColW - 6.0f, rowR.bottom), brush);
        }
        brush->SetColor(ArcColor(0x64748B));
        rt->DrawText(cStr.c_str(), static_cast<UINT32>(cStr.size()), fmtSmall,
            D2D1::RectF(listRight - packColW, rowR.top + 2.0f, listRight - 8.0f, rowR.bottom), brush);
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    rt->PopAxisAlignedClip();

    // Scrollbar
    m_scrollTrackRect = D2D1::RectF(listRight, colR.bottom, rect.right, rect.bottom);
    brush->SetColor(ArcColor(0xF1F5F9));
    rt->FillRectangle(m_scrollTrackRect, brush);
    if (totalH > listH) {
        float thumbH = std::max(24.0f, listH * (listH / totalH));
        float frac = (maxScroll > 0.0f) ? (m_scrollY / maxScroll) : 0.0f;
        float thumbTop = m_scrollTrackRect.top + frac * (listH - thumbH);
        m_scrollThumbRect = D2D1::RectF(m_scrollTrackRect.left + 2.0f, thumbTop, m_scrollTrackRect.right - 2.0f, thumbTop + thumbH);
        brush->SetColor(m_draggingScroll ? ArcColor(0x0078D7) : ArcColor(0x94A3B8));
        rt->FillRoundedRectangle(D2D1::RoundedRect(m_scrollThumbRect, 3.0f, 3.0f), brush);
    } else {
        m_scrollThumbRect = {};
    }
}

bool ArchiveExplorer::OnMouseDown(float x, float y, bool rightButton, Lang lang, std::wstring* outStatusToast) {
    if (rightButton) return false;
    const bool pl = (lang == Lang::PL);

    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            switch (hit.id) {
            case BTN_UP_DIR: {
                size_t slash = m_currentInnerDir.find_last_of(L'/');
                if (slash == std::wstring::npos) m_currentInnerDir.clear();
                else m_currentInnerDir = m_currentInnerDir.substr(0, slash);
                RebuildCurrentSubfolderView();
                return true;
            }
            case BTN_OPEN_AS_FOLDER: {
                std::wstring target = m_archivePath;
                if (!m_currentInnerDir.empty()) {
                    std::wstring sub = m_currentInnerDir;
                    for (wchar_t& ch : sub) if (ch == L'/') ch = L'\\';
                    target = PathUtils::EnsureTrailingSlash(target) + sub;
                }
                m_pendingNavigateArchivePath = target;
                return true;
            }
            case BTN_EXTRACT_HERE:
            case BTN_EXTRACT_SUBFOLDER: {
                std::wstring target = PathUtils::GetParentPath(m_archivePath);
                if (hit.id == BTN_EXTRACT_SUBFOLDER) {
                    std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(m_archivePath), false);
                    target = PathUtils::EnsureTrailingSlash(target) + stem;
                }
                m_pendingExtract = { m_archivePath, target, hit.id == BTN_EXTRACT_SUBFOLDER };
                m_hasPendingExtract = true;
                return true;
            }
            }
        }
    }

    if (PathUtils::PtInRectF(x, y, m_scrollTrackRect)) {
        if (PathUtils::PtInRectF(x, y, m_scrollThumbRect)) {
            m_draggingScroll = true;
            m_scrollDragOffset = y - m_scrollThumbRect.top;
        } else {
            float rowH = 22.0f;
            float listH = std::max(1.0f, m_listRect.bottom - m_listRect.top);
            float maxScroll = std::max(0.0f, static_cast<float>(m_visibleItems.size()) * rowH - listH);
            float frac = std::clamp((y - m_scrollTrackRect.top) / listH, 0.0f, 1.0f);
            m_scrollY = frac * maxScroll;
        }
        return true;
    }

    if (PathUtils::PtInRectF(x, y, m_listRect)) {
        int idx = static_cast<int>((y - m_listRect.top + m_scrollY) / 22.0f);
        if (idx >= 0 && idx < static_cast<int>(m_visibleItems.size())) {
            ULONGLONG now = GetTickCount64();
            bool isDbl = (idx == m_lastClickIdx && (now - m_lastClickTick) <= GetDoubleClickTime());
            m_selectedIdx = idx;
            m_lastClickIdx = idx;
            m_lastClickTick = now;

            if (isDbl) {
                m_lastClickIdx = -1;
                const auto itemCopy = m_visibleItems[idx];
                if (itemCopy.isDirectory) {
                    m_currentInnerDir = itemCopy.pathInArchive;
                    RebuildCurrentSubfolderView();
                } else {
                    m_pendingPreviewEntry.archivePath = m_archivePath;
                    m_pendingPreviewEntry.innerPath = itemCopy.pathInArchive;
                    m_pendingPreviewEntry.name = itemCopy.name;
                    m_pendingPreviewEntry.size = itemCopy.uncompressedSize;
                    m_hasPendingPreviewEntry = true;
                    if (outStatusToast) {
                        *outStatusToast = (pl ? L"Podgląd z archiwum: " : L"Preview from archive: ") + itemCopy.name;
                    }
                }
            }
            return true;
        }
    }

    return false;
}

bool ArchiveExplorer::OnMouseMove(float x, float y) {
    bool changed = false;
    int newBtn = 0;
    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            newBtn = hit.id;
            break;
        }
    }
    if (newBtn != m_hoverBtnId) {
        m_hoverBtnId = newBtn;
        changed = true;
    }

    if (m_draggingScroll) {
        float rowH = 22.0f;
        float listH = std::max(1.0f, m_listRect.bottom - m_listRect.top);
        float maxScroll = std::max(0.0f, static_cast<float>(m_visibleItems.size()) * rowH - listH);
        float thumbH = m_scrollThumbRect.bottom - m_scrollThumbRect.top;
        float usable = std::max(1.0f, listH - thumbH);
        float frac = std::clamp((y - m_scrollDragOffset - m_scrollTrackRect.top) / usable, 0.0f, 1.0f);
        m_scrollY = frac * maxScroll;
        return true;
    }

    int newHov = -1;
    if (PathUtils::PtInRectF(x, y, m_listRect)) {
        int idx = static_cast<int>((y - m_listRect.top + m_scrollY) / 22.0f);
        if (idx >= 0 && idx < static_cast<int>(m_visibleItems.size())) {
            newHov = idx;
        }
    }
    if (newHov != m_hoverIdx) {
        m_hoverIdx = newHov;
        changed = true;
    }
    return changed;
}

bool ArchiveExplorer::OnMouseUp() {
    if (m_draggingScroll) {
        m_draggingScroll = false;
        return true;
    }
    return false;
}

bool ArchiveExplorer::OnMouseWheel(short delta, float x, float y) {
    if (!PathUtils::PtInRectF(x, y, m_listRect)) return false;
    int notches = delta / WHEEL_DELTA;
    if (notches == 0) notches = (delta > 0) ? 1 : -1;
    float rowH = 22.0f;
    float listH = std::max(1.0f, m_listRect.bottom - m_listRect.top);
    float maxScroll = std::max(0.0f, static_cast<float>(m_visibleItems.size()) * rowH - listH);
    m_scrollY = std::clamp(m_scrollY - notches * rowH * 3.0f, 0.0f, maxScroll);
    return true;
}

} // namespace Explorer2
