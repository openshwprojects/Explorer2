#pragma once
// GoogleDraco.h — Google Draco Edgebreaker / Valence / KD-Tree (.drc & glTF KHR_draco_mesh_compression).
#include "../common/MeshFormatCommon.h"

namespace Formats {

namespace DracoMeshoptDetail {

#ifndef E2_DRACO_NOINLINE
#if defined(_MSC_VER)
#define E2_DRACO_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define E2_DRACO_NOINLINE __attribute__((noinline))
#else
#define E2_DRACO_NOINLINE
#endif
#endif

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdI16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdI32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdF32LE;
using ::Explorer2::ByteIO::RdF64LE;

constexpr uint16_t DracoVer(uint8_t maj, uint8_t min) {
    return static_cast<uint16_t>((static_cast<uint16_t>(maj) << 8) | min);
}

inline int32_t AddAsUnsigned32(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

inline int DracoMostSignificantBit(uint32_t n) {
    if (n == 0u) return -1;
    int msb = 0;
    while (n >>= 1u) ++msb;
    return msb;
}

inline bool ReadLeb128U32(const uint8_t* data, size_t size, size_t& pos, uint32_t& out) {
    uint64_t v = 0;
    bool ok = ::Explorer2::ByteIO::RdLeb128(data, size, pos, v, 5);
    out = static_cast<uint32_t>(v);
    return ok;
}

inline bool ReadLeb128U64(const uint8_t* data, size_t size, size_t& pos, uint64_t& out) { return ::Explorer2::ByteIO::RdLeb128(data, size, pos, out, 10); }

inline bool ReadDracoU32OrVarint(const uint8_t* data, size_t size, size_t& pos, uint16_t ver, uint32_t minVer, uint32_t& out) {
    if (ver < minVer) {
        if (pos + 4 > size) return false;
        out = RdU32LE(data + pos);
        pos += 4;
        return true;
    }
    return ReadLeb128U32(data, size, pos, out);
}

inline int32_t DracoConvertSymbolToSignedInt(uint32_t val) {
    if ((val & 1u) == 0u) return static_cast<int32_t>(val >> 1);
    return -static_cast<int32_t>(val >> 1) - 1;
}

// LSB-first bit reader matching Draco DecoderBuffer::BitDecoder
struct DracoBitReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t bitOffset = 0;

    DracoBitReader() = default;
    DracoBitReader(const uint8_t* p, size_t sz) : data(p), size(sz), bitOffset(0) {}

    bool ReadBits(uint32_t nBits, uint32_t& out) {
        out = 0;
        if (nBits == 0) return true;
        if (nBits > 32 || bitOffset + nBits > size * 8ULL) return false;
        for (uint32_t i = 0; i < nBits; ++i) {
            size_t byteIdx = bitOffset >> 3;
            uint32_t bitIdx = static_cast<uint32_t>(bitOffset & 7u);
            uint32_t bit = (data[byteIdx] >> bitIdx) & 1u;
            out |= (bit << i);
            ++bitOffset;
        }
        return true;
    }

    size_t BytesConsumed() const { return (bitOffset + 7u) >> 3; }
};

inline bool StartDracoBitBuffer(
    const uint8_t* data,
    size_t size,
    size_t& pos,
    uint16_t ver,
    DracoBitReader& outBr)
{
    uint64_t byteSz = 0;
    if (ver < DracoVer(2, 2)) {
        if (pos + 8 > size) return false;
        byteSz = RdU64LE(data + pos);
        pos += 8;
    } else {
        if (!ReadLeb128U64(data, size, pos, byteSz)) return false;
    }
    if (byteSz > size - pos) return false;
    outBr = DracoBitReader(data + pos, static_cast<size_t>(byteSz));
    pos += static_cast<size_t>(byteSz);
    return true;
}

// MSB-first 32-bit word reader matching Draco DirectBitDecoder (used by KD-tree point cloud decoder)
struct DracoDirectBitDecoder {
    std::vector<uint32_t> words;
    size_t wordIdx = 0;
    int numUsedBits = 0;

    bool StartDecoding(const uint8_t* data, size_t size, size_t& pos) {
        words.clear();
        wordIdx = 0;
        numUsedBits = 0;
        if (pos + 4 > size) return false;
        uint32_t szBytes = RdU32LE(data + pos);
        pos += 4;
        if (szBytes == 0 || (szBytes & 3u) != 0u || szBytes > size - pos) return false;
        uint32_t nWords = szBytes >> 2;
        words.resize(nWords);
        for (uint32_t i = 0; i < nWords; ++i) {
            words[i] = RdU32LE(data + pos + static_cast<size_t>(i) * 4u);
        }
        pos += szBytes;
        return true;
    }

    bool DecodeNextBit() {
        if (wordIdx >= words.size()) return false;
        uint32_t sel = 1u << (31 - numUsedBits);
        bool bit = (words[wordIdx] & sel) != 0u;
        if (++numUsedBits == 32) {
            ++wordIdx;
            numUsedBits = 0;
        }
        return bit;
    }

    bool DecodeLeastSignificantBits32(int nbits, uint32_t& out) {
        if (nbits <= 0) { out = 0; return true; }
        if (nbits > 32 || wordIdx >= words.size()) return false;
        int rem = 32 - numUsedBits;
        if (nbits <= rem) {
            out = (words[wordIdx] << numUsedBits) >> (32 - nbits);
            numUsedBits += nbits;
            if (numUsedBits == 32) {
                ++wordIdx;
                numUsedBits = 0;
            }
            return true;
        }
        if (wordIdx + 1 >= words.size()) return false;
        uint32_t valueL = words[wordIdx] << numUsedBits;
        numUsedBits = nbits - rem;
        ++wordIdx;
        uint32_t valueR = words[wordIdx] >> (32 - numUsedBits);
        out = (valueL >> (32 - numUsedBits - rem)) | valueR;
        return true;
    }
};

// Google Draco rABS (RAnsBitDecoder, p8 precision = 256, l_base = 4096)
struct DracoRAbsDecoder {
    const uint8_t* buf = nullptr;
    size_t offset = 0;
    uint32_t state = 0;
    uint8_t probZero = 128;

    bool Init(const uint8_t* p, size_t sz) {
        if (!p || sz == 0) return false;
        buf = p;
        constexpr uint32_t kLBase = 4096u;
        uint8_t top = p[sz - 1] >> 6;
        if (top == 0) {
            offset = sz - 1;
            state = static_cast<uint32_t>(p[sz - 1] & 0x3Fu) + kLBase;
        } else if (top == 1) {
            if (sz < 2) return false;
            offset = sz - 2;
            state = ((static_cast<uint32_t>(p[sz - 1] & 0x3Fu) << 8) | p[sz - 2]) + kLBase;
        } else if (top == 2) {
            if (sz < 3) return false;
            offset = sz - 3;
            state = ((static_cast<uint32_t>(p[sz - 1] & 0x3Fu) << 16) |
                     (static_cast<uint32_t>(p[sz - 2]) << 8) | p[sz - 3]) + kLBase;
        } else {
            if (sz < 4) return false;
            offset = sz - 4;
            state = ((static_cast<uint32_t>(p[sz - 1] & 0x3Fu) << 24) |
                     (static_cast<uint32_t>(p[sz - 2]) << 16) |
                     (static_cast<uint32_t>(p[sz - 3]) << 8) | p[sz - 4]) + kLBase;
        }
        return state < kLBase * 256u;
    }

    bool StartDecoding(const uint8_t* data, size_t size, size_t& pos, uint16_t ver) {
        if (pos >= size) return false;
        probZero = data[pos++];
        uint32_t szBytes = 0;
        if (!ReadDracoU32OrVarint(data, size, pos, ver, DracoVer(2, 2), szBytes)) return false;
        if (szBytes == 0 || szBytes > size - pos) return false;
        if (!Init(data + pos, szBytes)) return false;
        pos += szBytes;
        return true;
    }

    uint8_t ReadBit(uint8_t pz) {
        constexpr uint32_t kLBase = 4096u;
        while (state < kLBase && offset > 0) {
            state = (state << 8) | buf[--offset];
        }
        uint32_t p = 256u - static_cast<uint32_t>(pz);
        if (p == 0) return 0;
        if (p == 256u) return 1;
        uint32_t x = state;
        uint32_t quot = x >> 8;
        uint32_t rem  = x & 0xFFu;
        uint32_t xn   = quot * p;
        if (rem < p) {
            state = xn + rem;
            return 1;
        }
        state = x - xn - p;
        return 0;
    }

    bool DecodeNextBit() { return ReadBit(probZero) != 0u; }

    bool DecodeLeastSignificantBits32(int nbits, uint32_t& out) {
        if (nbits < 0 || nbits > 32) return false;
        uint32_t res = 0;
        for (int i = 0; i < nbits; ++i) {
            res = (res << 1) | (DecodeNextBit() ? 1u : 0u);
        }
        out = res;
        return true;
    }
};

// FoldedBit32Decoder<RAnsBitDecoder> (used by KD-tree compression levels 4..6)
struct DracoFoldedRAnsBitDecoder {
    std::array<DracoRAbsDecoder, 32> folded;
    DracoRAbsDecoder bitDec;

    bool StartDecoding(const uint8_t* data, size_t size, size_t& pos, uint16_t ver) {
        for (int i = 0; i < 32; ++i) {
            if (!folded[static_cast<size_t>(i)].StartDecoding(data, size, pos, ver)) return false;
        }
        return bitDec.StartDecoding(data, size, pos, ver);
    }

    bool DecodeNextBit() { return bitDec.DecodeNextBit(); }

    bool DecodeLeastSignificantBits32(int nbits, uint32_t& out) {
        if (nbits < 0 || nbits > 32) return false;
        uint32_t res = 0;
        for (int i = 0; i < nbits; ++i) {
            res = (res << 1) | (folded[static_cast<size_t>(i)].DecodeNextBit() ? 1u : 0u);
        }
        out = res;
        return true;
    }
};

// Google Draco rANS multi-symbol decoder (RAnsSymbolDecoder<unique_symbols_bit_length_t>)
struct DracoRAnsSymbolDecoder {
    struct SymEntry {
        uint32_t prob = 0;
        uint32_t cumProb = 0;
    };
    uint32_t precision = 4096;
    uint32_t lBase = 16384;
    uint32_t numSymbols = 0;
    std::vector<SymEntry> symTable;
    std::vector<uint32_t> lut;
    const uint8_t* buf = nullptr;
    size_t offset = 0;
    uint32_t state = 0;

    bool ReadTable(const uint8_t* data, size_t size, size_t& pos, uint16_t ver, uint32_t uniqueSymbolsBitLen) {
        uint32_t precBits = std::clamp<uint32_t>((3u * uniqueSymbolsBitLen) / 2u, 12u, 20u);
        precision = 1u << precBits;
        lBase = precision * 4u;

        numSymbols = 0;
        if (!ReadDracoU32OrVarint(data, size, pos, ver, DracoVer(2, 0), numSymbols)) return false;
        if (numSymbols > (1u << 20) || (numSymbols / 64u) > (size - pos)) return false;
        if (numSymbols == 0) {
            symTable.clear();
            lut.clear();
            return true;
        }
        symTable.assign(numSymbols, {});
        lut.assign(precision, 0u);

        uint32_t cumProb = 0;
        for (uint32_t i = 0; i < numSymbols; ++i) {
            if (pos >= size) return false;
            uint8_t probData = data[pos++];
            uint8_t token = probData & 3u;
            if (token == 3u) {
                uint32_t skip = probData >> 2;
                if (i + skip >= numSymbols) return false;
                i += skip;
            } else {
                uint32_t prob = static_cast<uint32_t>(probData >> 2);
                for (uint8_t eb = 0; eb < token; ++eb) {
                    if (pos >= size) return false;
                    prob |= static_cast<uint32_t>(data[pos++]) << (8u * (eb + 1u) - 2u);
                }
                if (cumProb + prob > precision) return false;
                symTable[i].prob = prob;
                symTable[i].cumProb = cumProb;
                for (uint32_t k = cumProb; k < cumProb + prob; ++k) {
                    lut[k] = i;
                }
                cumProb += prob;
            }
        }
        return cumProb == precision;
    }

    bool StartStream(const uint8_t* data, size_t size, size_t& pos, uint16_t ver) {
        uint64_t ansByteLen = 0;
        if (ver < DracoVer(2, 0)) {
            if (pos + 8 > size) return false;
            ansByteLen = RdU64LE(data + pos);
            pos += 8;
        } else {
            if (!ReadLeb128U64(data, size, pos, ansByteLen)) return false;
        }
        if (ansByteLen == 0 || ansByteLen > size - pos) return false;
        const uint8_t* p = data + pos;
        size_t sz = static_cast<size_t>(ansByteLen);
        pos += sz;

        buf = p;
        uint8_t top = p[sz - 1] >> 6;
        if (top == 0) {
            offset = sz - 1;
            state = static_cast<uint32_t>(p[sz - 1] & 0x3Fu) + lBase;
        } else if (top == 1) {
            if (sz < 2) return false;
            offset = sz - 2;
            state = ((static_cast<uint32_t>(p[sz - 1] & 0x3Fu) << 8) | p[sz - 2]) + lBase;
        } else if (top == 2) {
            if (sz < 3) return false;
            offset = sz - 3;
            state = ((static_cast<uint32_t>(p[sz - 1] & 0x3Fu) << 16) |
                     (static_cast<uint32_t>(p[sz - 2]) << 8) | p[sz - 3]) + kLBaseScaled();
        } else {
            if (sz < 4) return false;
            offset = sz - 4;
            state = ((static_cast<uint32_t>(p[sz - 1] & 0x3Fu) << 24) |
                     (static_cast<uint32_t>(p[sz - 2]) << 16) |
                     (static_cast<uint32_t>(p[sz - 3]) << 8) | p[sz - 4]) + lBase;
        }
        return state < static_cast<uint64_t>(lBase) * 256ULL;
    }

    uint32_t kLBaseScaled() const { return lBase; }

    uint32_t ReadSymbol() {
        while (state < lBase && offset > 0) {
            state = (state << 8) | buf[--offset];
        }
        uint32_t quo = state / precision;
        uint32_t rem = state % precision;
        uint32_t sym = lut[rem];
        const SymEntry& se = symTable[sym];
        state = quo * se.prob + rem - se.cumProb;
        return sym;
    }
};

// Draco DecodeSymbols (symbol_decoding.cc: scheme 0 = SYMBOL_CODING_TAGGED, scheme 1 = SYMBOL_CODING_RAW)
inline bool DracoDecodeSymbols(
    const uint8_t* data,
    size_t size,
    size_t& pos,
    uint16_t ver,
    uint32_t numValues,
    uint32_t numComponents,
    std::vector<uint32_t>& outValues)
{
    outValues.clear();
    if (numValues == 0) return true;
    if (numComponents == 0 || pos >= size) return false;

    uint8_t scheme = data[pos++];
    if (scheme == 1u) { // SYMBOL_CODING_RAW
        if (pos >= size) return false;
        uint8_t maxBitLen = data[pos++];
        if (maxBitLen < 1u || maxBitLen > 18u) return false;
        DracoRAnsSymbolDecoder dec;
        if (!dec.ReadTable(data, size, pos, ver, maxBitLen)) return false;
        if (dec.numSymbols == 0) return false;
        if (!dec.StartStream(data, size, pos, ver)) return false;

        outValues.resize(numValues);
        for (uint32_t i = 0; i < numValues; ++i) {
            outValues[i] = dec.ReadSymbol();
        }
        return true;
    }
    if (scheme == 0u) { // SYMBOL_CODING_TAGGED (SymbolDecoderT<5> + unprefixed bit buffer!)
        if ((numValues % numComponents) != 0u) return false;
        uint32_t numVectors = numValues / numComponents;
        DracoRAnsSymbolDecoder tagDec;
        if (!tagDec.ReadTable(data, size, pos, ver, 5u)) return false;
        if (tagDec.numSymbols == 0) return false;
        if (!tagDec.StartStream(data, size, pos, ver)) return false;

        // StartBitDecoding(false, nullptr): raw bits follow immediately; EndBitDecoding advances (bits + 7) / 8!
        DracoBitReader br(data + pos, size - pos);
        outValues.resize(numValues);
        size_t outIdx = 0;
        for (uint32_t v = 0; v < numVectors; ++v) {
            uint32_t bitLen = tagDec.ReadSymbol();
            if (bitLen > 32u) return false;
            for (uint32_t c = 0; c < numComponents; ++c) {
                uint32_t val = 0;
                if (!br.ReadBits(bitLen, val)) return false;
                outValues[outIdx++] = val;
            }
        }
        pos += br.BytesConsumed();
        return true;
    }
    return false;
}

// ============================================================================
// Draco CornerTable & MeshAttributeCornerTable (exact match to Google Draco)
// ============================================================================
struct DracoCornerTable {
    static constexpr int32_t kInvalid = -1;
    uint32_t numFaces = 0;
    std::vector<int32_t> cornerToVert;
    std::vector<int32_t> oppCorner;
    std::vector<int32_t> vertexCorners; // LeftMostCorner per vertex

    void Reset(uint32_t nFaces, uint32_t reserveVerts) {
        numFaces = nFaces;
        cornerToVert.assign(static_cast<size_t>(nFaces) * 3u, kInvalid);
        oppCorner.assign(static_cast<size_t>(nFaces) * 3u, kInvalid);
        vertexCorners.clear();
        vertexCorners.reserve(reserveVerts);
    }

    int32_t NumFaces() const { return static_cast<int32_t>(numFaces); }
    int32_t NumCorners() const { return static_cast<int32_t>(cornerToVert.size()); }
    int32_t NumVertices() const { return static_cast<int32_t>(vertexCorners.size()); }

    static inline int32_t Next(int32_t c) { return (c < 0) ? kInvalid : (((c % 3) == 2) ? (c - 2) : (c + 1)); }
    static inline int32_t Prev(int32_t c) { return (c < 0) ? kInvalid : (((c % 3) == 0) ? (c + 2) : (c - 1)); }

    inline int32_t Opposite(int32_t c) const {
        return (c >= 0 && static_cast<size_t>(c) < oppCorner.size()) ? oppCorner[static_cast<size_t>(c)] : kInvalid;
    }
    inline int32_t Vertex(int32_t c) const {
        return (c >= 0 && static_cast<size_t>(c) < cornerToVert.size()) ? cornerToVert[static_cast<size_t>(c)] : kInvalid;
    }
    inline void MapCornerToVertex(int32_t c, int32_t v) {
        if (c >= 0 && static_cast<size_t>(c) < cornerToVert.size()) cornerToVert[static_cast<size_t>(c)] = v;
    }
    inline void SetOppositeCorners(int32_t a, int32_t b) {
        if (a >= 0 && static_cast<size_t>(a) < oppCorner.size()) oppCorner[static_cast<size_t>(a)] = b;
        if (b >= 0 && static_cast<size_t>(b) < oppCorner.size()) oppCorner[static_cast<size_t>(b)] = a;
    }
    inline int32_t AddNewVertex() {
        vertexCorners.push_back(kInvalid);
        return static_cast<int32_t>(vertexCorners.size() - 1);
    }
    inline int32_t LeftMostCorner(int32_t v) const {
        return (v >= 0 && static_cast<size_t>(v) < vertexCorners.size()) ? vertexCorners[static_cast<size_t>(v)] : kInvalid;
    }
    inline void SetLeftMostCorner(int32_t v, int32_t c) {
        if (v >= 0 && static_cast<size_t>(v) < vertexCorners.size()) vertexCorners[static_cast<size_t>(v)] = c;
    }
    inline void MakeVertexIsolated(int32_t v) {
        if (v >= 0 && static_cast<size_t>(v) < vertexCorners.size()) vertexCorners[static_cast<size_t>(v)] = kInvalid;
    }

    // corner_table.h: GetLeftCorner = Opposite(Previous(c)), GetRightCorner = Opposite(Next(c))
    inline int32_t GetLeftCorner(int32_t c) const { return (c < 0) ? kInvalid : Opposite(Prev(c)); }
    inline int32_t GetRightCorner(int32_t c) const { return (c < 0) ? kInvalid : Opposite(Next(c)); }

    // corner_table.h: SwingRight = Previous(Opposite(Previous(c))), SwingLeft = Next(Opposite(Next(c)))
    inline int32_t SwingRight(int32_t c) const { return Prev(Opposite(Prev(c))); }
    inline int32_t SwingLeft(int32_t c) const { return Next(Opposite(Next(c))); }

    inline bool IsOnBoundary(int32_t v) const {
        int32_t c = LeftMostCorner(v);
        return c == kInvalid || SwingLeft(c) == kInvalid;
    }
};

struct DracoAttrCornerTable {
    static constexpr int32_t kInvalid = -1;
    const DracoCornerTable* base = nullptr;
    std::vector<uint8_t> isEdgeOnSeam;
    std::vector<uint8_t> isVertexOnSeam;
    std::vector<int32_t> cornerToVert;
    std::vector<int32_t> vertToLeftMostCorner;

    void InitEmpty(const DracoCornerTable* tbl) {
        base = tbl;
        isEdgeOnSeam.assign(static_cast<size_t>(tbl->NumCorners()), 0u);
        isVertexOnSeam.assign(static_cast<size_t>(tbl->NumVertices()), 0u);
        cornerToVert.assign(static_cast<size_t>(tbl->NumCorners()), kInvalid);
        vertToLeftMostCorner.clear();
    }

    void AddSeamEdge(int32_t c) {
        if (!base || c < 0 || c >= base->NumCorners()) return;
        isEdgeOnSeam[static_cast<size_t>(c)] = 1u;
        int32_t vn = base->Vertex(DracoCornerTable::Next(c));
        int32_t vp = base->Vertex(DracoCornerTable::Prev(c));
        if (vn >= 0 && static_cast<size_t>(vn) < isVertexOnSeam.size()) isVertexOnSeam[static_cast<size_t>(vn)] = 1u;
        if (vp >= 0 && static_cast<size_t>(vp) < isVertexOnSeam.size()) isVertexOnSeam[static_cast<size_t>(vp)] = 1u;

        int32_t opp = base->Opposite(c);
        if (opp != kInvalid) {
            isEdgeOnSeam[static_cast<size_t>(opp)] = 1u;
            int32_t ovn = base->Vertex(DracoCornerTable::Next(opp));
            int32_t ovp = base->Vertex(DracoCornerTable::Prev(opp));
            if (ovn >= 0 && static_cast<size_t>(ovn) < isVertexOnSeam.size()) isVertexOnSeam[static_cast<size_t>(ovn)] = 1u;
            if (ovp >= 0 && static_cast<size_t>(ovp) < isVertexOnSeam.size()) isVertexOnSeam[static_cast<size_t>(ovp)] = 1u;
        }
    }

    inline bool IsCornerOppositeToSeamEdge(int32_t c) const {
        return c >= 0 && static_cast<size_t>(c) < isEdgeOnSeam.size() && isEdgeOnSeam[static_cast<size_t>(c)] != 0u;
    }
    inline bool IsCornerOnSeam(int32_t c) const {
        int32_t v = base ? base->Vertex(c) : kInvalid;
        return v >= 0 && static_cast<size_t>(v) < isVertexOnSeam.size() && isVertexOnSeam[static_cast<size_t>(v)] != 0u;
    }

    static inline int32_t Next(int32_t c) { return DracoCornerTable::Next(c); }
    static inline int32_t Prev(int32_t c) { return DracoCornerTable::Prev(c); }

    inline int32_t Opposite(int32_t c) const {
        if (!base || c < 0 || IsCornerOppositeToSeamEdge(c)) return kInvalid;
        return base->Opposite(c);
    }
    inline int32_t GetLeftCorner(int32_t c) const { return Opposite(Prev(c)); }
    inline int32_t GetRightCorner(int32_t c) const { return Opposite(Next(c)); }
    inline int32_t SwingRight(int32_t c) const { return Prev(Opposite(Prev(c))); }
    inline int32_t SwingLeft(int32_t c) const { return Next(Opposite(Next(c))); }

    int32_t NumFaces() const { return base ? base->NumFaces() : 0; }
    int32_t NumCorners() const { return base ? base->NumCorners() : 0; }
    int32_t NumVertices() const { return static_cast<int32_t>(vertToLeftMostCorner.size()); }

    inline int32_t Vertex(int32_t c) const {
        return (c >= 0 && static_cast<size_t>(c) < cornerToVert.size()) ? cornerToVert[static_cast<size_t>(c)] : kInvalid;
    }
    inline int32_t LeftMostCorner(int32_t v) const {
        return (v >= 0 && static_cast<size_t>(v) < vertToLeftMostCorner.size()) ? vertToLeftMostCorner[static_cast<size_t>(v)] : kInvalid;
    }
    inline bool IsOnBoundary(int32_t v) const {
        int32_t c = LeftMostCorner(v);
        return c == kInvalid || SwingLeft(c) == kInvalid;
    }

    bool RecomputeVertices() {
        if (!base) return false;
        vertToLeftMostCorner.clear();
        vertToLeftMostCorner.reserve(static_cast<size_t>(base->NumVertices()));
        int32_t numNewVerts = 0;
        const int32_t maxRingGuard = base->NumCorners() + 4;

        for (int32_t v = 0; v < base->NumVertices(); ++v) {
            int32_t c = base->LeftMostCorner(v);
            if (c == kInvalid) continue;
            int32_t firstVertId = numNewVerts++;
            int32_t firstC = c;
            if (isVertexOnSeam[static_cast<size_t>(v)] != 0u) {
                int32_t actC = SwingLeft(firstC);
                int guard = 0;
                while (actC != kInvalid) {
                    if (++guard > maxRingGuard) return false;
                    firstC = actC;
                    actC = SwingLeft(actC);
                    if (actC == c) return false;
                }
            }
            cornerToVert[static_cast<size_t>(firstC)] = firstVertId;
            vertToLeftMostCorner.push_back(firstC);
            int32_t actC = base->SwingRight(firstC);
            int guard = 0;
            while (actC != kInvalid && actC != firstC) {
                if (++guard > maxRingGuard) return false;
                if (IsCornerOppositeToSeamEdge(DracoCornerTable::Next(actC))) {
                    firstVertId = numNewVerts++;
                    vertToLeftMostCorner.push_back(actC);
                }
                cornerToVert[static_cast<size_t>(actC)] = firstVertId;
                actC = base->SwingRight(actC);
            }
        }
        return true;
    }
};

struct DracoEdgebreakerState {
    DracoCornerTable cornerTable;
    int32_t numConnectivityVerts = 0;
    std::vector<DracoAttrCornerTable> attrTables;
    std::vector<int32_t> pointToCorner;
    std::vector<int32_t> cornerToPoint;
    uint32_t numPoints = 0;
};

// Decode Standard (0) & Valence (2) EdgeBreaker connectivity + non-position attribute seams + point deduplication
E2_DRACO_NOINLINE inline bool DecodeDracoEdgebreakerFull(
    const uint8_t* data,
    size_t size,
    size_t& pos,
    uint16_t ver,
    DracoEdgebreakerState& st)
{
    if (pos >= size) return false;
    uint8_t travType = data[pos++]; // 0 = STANDARD_EDGEBREAKER, 2 = VALENCE_EDGEBREAKER
    if (travType != 0u && travType != 2u) return false;

    if (ver < DracoVer(2, 2)) {
        uint32_t numNewVerts = 0;
        if (!ReadDracoU32OrVarint(data, size, pos, ver, DracoVer(2, 0), numNewVerts)) return false;
        (void)numNewVerts;
    }

    uint32_t numEncodedVertices = 0, numFaces = 0, numEncodedSymbols = 0, numSplitSymbols = 0;
    if (!ReadDracoU32OrVarint(data, size, pos, ver, DracoVer(2, 0), numEncodedVertices)) return false;
    if (!ReadDracoU32OrVarint(data, size, pos, ver, DracoVer(2, 0), numFaces)) return false;
    if (pos >= size) return false;
    uint8_t numAttrData = data[pos++]; // Number of NON-POSITION attribute connectivity streams!
    if (!ReadDracoU32OrVarint(data, size, pos, ver, DracoVer(2, 0), numEncodedSymbols)) return false;
    if (!ReadDracoU32OrVarint(data, size, pos, ver, DracoVer(2, 0), numSplitSymbols)) return false;

    if (numFaces == 0 || numFaces > 4000000u || numEncodedVertices > numFaces * 3u ||
        numFaces < numEncodedSymbols || numFaces > numEncodedSymbols + (numEncodedSymbols / 3u) ||
        numSplitSymbols > numEncodedSymbols || numAttrData > 64u)
    {
        return false;
    }
    const uint64_t minFaceEdges = (3ULL * numFaces) / 2ULL;
    const uint64_t maxVertEdges = (static_cast<uint64_t>(numEncodedVertices) * (numEncodedVertices > 0 ? numEncodedVertices - 1 : 0)) / 2ULL;
    if (maxVertEdges < minFaceEdges) return false;

    const uint32_t maxVerts = numEncodedVertices + numSplitSymbols;
    if (maxVerts < 3u || maxVerts > 8000000u) return false;

    struct SplitEvent {
        uint32_t splitSymId = 0;
        uint32_t sourceSymId = 0;
        uint8_t sourceEdge = 0; // 0 = LEFT_FACE_EDGE, 1 = RIGHT_FACE_EDGE
    };
    std::vector<SplitEvent> splits;

    auto decodeSplitAndHoleEvents = [&](size_t& evPos) -> bool {
        uint32_t numTopoSplits = 0;
        if (!ReadDracoU32OrVarint(data, size, evPos, ver, DracoVer(2, 0), numTopoSplits)) return false;
        if (numTopoSplits > numFaces) return false;
        splits.resize(numTopoSplits);
        if (numTopoSplits > 0) {
            if (ver < DracoVer(1, 2)) {
                for (uint32_t i = 0; i < numTopoSplits; ++i) {
                    if (evPos + 9 > size) return false;
                    splits[i].splitSymId  = RdU32LE(data + evPos);
                    splits[i].sourceSymId = RdU32LE(data + evPos + 4);
                    splits[i].sourceEdge  = data[evPos + 8] & 1u;
                    evPos += 9;
                }
            } else {
                uint32_t lastSrcId = 0;
                for (uint32_t i = 0; i < numTopoSplits; ++i) {
                    uint32_t srcDelta = 0, splitDelta = 0;
                    if (!ReadLeb128U32(data, size, evPos, srcDelta)) return false;
                    if (!ReadLeb128U32(data, size, evPos, splitDelta)) return false;
                    uint32_t srcId = lastSrcId + srcDelta;
                    if (splitDelta > srcId) return false;
                    splits[i].sourceSymId = srcId;
                    splits[i].splitSymId  = srcId - splitDelta;
                    lastSrcId = srcId;
                }
                DracoBitReader splitBr(data + evPos, size - evPos);
                uint32_t bitsPerEdge = (ver < DracoVer(2, 2)) ? 2u : 1u;
                for (uint32_t i = 0; i < numTopoSplits; ++i) {
                    uint32_t eb = 0;
                    if (!splitBr.ReadBits(bitsPerEdge, eb)) return false;
                    splits[i].sourceEdge = static_cast<uint8_t>(eb & 1u);
                }
                evPos += splitBr.BytesConsumed();
            }
        }
        if (ver < DracoVer(2, 1)) {
            uint32_t numHoleEvents = 0;
            if (!ReadDracoU32OrVarint(data, size, evPos, ver, DracoVer(2, 0), numHoleEvents)) return false;
            if (numHoleEvents > numFaces) return false;
            if (numHoleEvents > 0) {
                if (ver < DracoVer(1, 2)) {
                    if (evPos + static_cast<size_t>(numHoleEvents) * 4u > size) return false;
                    evPos += static_cast<size_t>(numHoleEvents) * 4u;
                } else {
                    for (uint32_t i = 0; i < numHoleEvents; ++i) {
                        uint32_t d = 0;
                        if (!ReadLeb128U32(data, size, evPos, d)) return false;
                    }
                }
            }
        }
        return true;
    };

    size_t legacyTopoSplitBytes = 0;
    if (ver < DracoVer(2, 2)) {
        uint32_t encConnSz = 0;
        if (!ReadDracoU32OrVarint(data, size, pos, ver, DracoVer(2, 0), encConnSz)) return false;
        if (encConnSz == 0 || encConnSz > size - pos) return false;
        size_t evStart = pos + encConnSz;
        size_t evPos = evStart;
        if (!decodeSplitAndHoleEvents(evPos)) return false;
        legacyTopoSplitBytes = evPos - evStart;
    } else {
        if (!decodeSplitAndHoleEvents(pos)) return false;
    }

    // TraversalDecoder::Start()
    DracoBitReader stdSymBr;
    DracoBitReader legacyStartFaceBr;
    DracoRAbsDecoder startFaceDec;
    std::vector<DracoRAbsDecoder> attrSeamDecs(numAttrData);

    constexpr int kMinValence = 2;
    constexpr int kMaxValence = 7;
    constexpr int kNumValenceContexts = kMaxValence - kMinValence + 1;
    std::vector<uint32_t> valenceContextSyms[kNumValenceContexts];
    int32_t valenceContextCounters[kNumValenceContexts] = {};

    auto decodeStdSym = [&](uint32_t& outSym) -> bool {
        uint32_t s = 0;
        if (!stdSymBr.ReadBits(1, s)) return false;
        if (s != 0u) {
            uint32_t suf = 0;
            if (!stdSymBr.ReadBits(2, suf)) return false;
            s |= (suf << 1);
        }
        outSym = s;
        return true;
    };

    if (travType == 0u || ver < DracoVer(2, 2)) {
        if (!StartDracoBitBuffer(data, size, pos, ver, stdSymBr)) return false;
    }
    if (ver < DracoVer(2, 2)) {
        if (!StartDracoBitBuffer(data, size, pos, ver, legacyStartFaceBr)) return false;
    } else {
        if (!startFaceDec.StartDecoding(data, size, pos, ver)) return false;
    }
    for (uint8_t i = 0; i < numAttrData; ++i) {
        if (!attrSeamDecs[i].StartDecoding(data, size, pos, ver)) return false;
    }

    if (travType == 2u) {
        if (ver < DracoVer(2, 2)) {
            uint32_t vSplitSyms = 0;
            if (!ReadDracoU32OrVarint(data, size, pos, ver, DracoVer(2, 0), vSplitSyms)) return false;
            if (vSplitSyms >= maxVerts || pos >= size) return false;
            int8_t vMode = static_cast<int8_t>(data[pos++]);
            if (vMode != 0) return false; // EDGEBREAKER_VALENCE_MODE_2_7
        }
        for (int ctx = 0; ctx < kNumValenceContexts; ++ctx) {
            uint32_t nCtxSyms = 0;
            if (!ReadLeb128U32(data, size, pos, nCtxSyms) || nCtxSyms > numFaces) return false;
            if (nCtxSyms > 0) {
                if (!DracoDecodeSymbols(data, size, pos, ver, nCtxSyms, 1u, valenceContextSyms[ctx])) return false;
                valenceContextCounters[ctx] = static_cast<int32_t>(nCtxSyms);
            }
        }
    }

    if (ver < DracoVer(2, 2)) {
        if (legacyTopoSplitBytes > size - pos) return false;
        pos += legacyTopoSplitBytes;
    }

    // MeshEdgebreakerDecoderImpl::DecodeConnectivity()
    DracoCornerTable& ct = st.cornerTable;
    ct.Reset(numFaces, maxVerts);
    std::vector<uint8_t> isVertHole(maxVerts, 1u);
    std::vector<int32_t> vertexValences(maxVerts, 0);
    int activeContext = -1;
    uint32_t lastSymbol = 7u;

    static constexpr uint32_t kSymbolIdToTopology[5] = { 0u, 1u, 3u, 5u, 7u }; // C, S, L, R, E

    auto decodeNextTopologySymbol = [&](uint32_t& outSym) -> bool {
        if (travType == 0u) {
            return decodeStdSym(outSym);
        }
        if (activeContext != -1) {
            int32_t cnt = --valenceContextCounters[activeContext];
            if (cnt < 0) return false;
            uint32_t symId = valenceContextSyms[activeContext][static_cast<size_t>(cnt)];
            if (symId > 4u) return false;
            lastSymbol = kSymbolIdToTopology[symId];
        } else {
            if (ver < DracoVer(2, 2)) {
                if (!decodeStdSym(lastSymbol)) return false;
            } else {
                lastSymbol = 7u; // TOPOLOGY_E
            }
        }
        outSym = lastSymbol;
        return true;
    };

    auto onNewActiveCorner = [&](int32_t corner) {
        if (travType != 2u) return;
        int32_t next = DracoCornerTable::Next(corner);
        int32_t prev = DracoCornerTable::Prev(corner);
        int32_t vC = ct.Vertex(corner);
        int32_t vN = ct.Vertex(next);
        int32_t vP = ct.Vertex(prev);
        switch (lastSymbol) {
            case 0u: // TOPOLOGY_C
            case 1u: // TOPOLOGY_S
                vertexValences[static_cast<size_t>(vN)] += 1;
                vertexValences[static_cast<size_t>(vP)] += 1;
                break;
            case 5u: // TOPOLOGY_R
                vertexValences[static_cast<size_t>(vC)] += 1;
                vertexValences[static_cast<size_t>(vN)] += 1;
                vertexValences[static_cast<size_t>(vP)] += 2;
                break;
            case 3u: // TOPOLOGY_L
                vertexValences[static_cast<size_t>(vC)] += 1;
                vertexValences[static_cast<size_t>(vN)] += 2;
                vertexValences[static_cast<size_t>(vP)] += 1;
                break;
            case 7u: // TOPOLOGY_E
                vertexValences[static_cast<size_t>(vC)] += 2;
                vertexValences[static_cast<size_t>(vN)] += 2;
                vertexValences[static_cast<size_t>(vP)] += 2;
                break;
            default: break;
        }
        int actVal = vertexValences[static_cast<size_t>(vN)];
        int clamped = std::clamp(actVal, kMinValence, kMaxValence);
        activeContext = clamped - kMinValence;
    };

    std::vector<int32_t> activeStack;
    activeStack.reserve(256);
    std::unordered_map<int32_t, int32_t> topoSplitActiveCorners;
    std::vector<int32_t> invalidVertices;
    const bool removeInvalidVertices = (numAttrData == 0u);
    size_t splitIdx = splits.size(); // IsTopologySplit consumes from back!

    uint32_t curFace = 0;
    for (uint32_t symId = 0; symId < numEncodedSymbols; ++symId) {
        if (curFace >= numFaces) return false;
        int32_t corner = static_cast<int32_t>(curFace * 3u);
        ++curFace;
        bool checkSplit = false;
        uint32_t sym = 0;
        if (!decodeNextTopologySymbol(sym)) return false;

        if (sym == 0u) { // TOPOLOGY_C
            if (activeStack.empty()) return false;
            int32_t cornerA = activeStack.back();
            int32_t vertexX = ct.Vertex(DracoCornerTable::Next(cornerA));
            if (vertexX < 0) return false;
            int32_t cornerB = DracoCornerTable::Next(ct.LeftMostCorner(vertexX));
            if (cornerB < 0 || cornerA == cornerB ||
                ct.Opposite(cornerA) != DracoCornerTable::kInvalid ||
                ct.Opposite(cornerB) != DracoCornerTable::kInvalid)
            {
                return false;
            }
            ct.SetOppositeCorners(cornerA, corner + 1);
            ct.SetOppositeCorners(cornerB, corner + 2);
            int32_t vertAPrev = ct.Vertex(DracoCornerTable::Prev(cornerA));
            int32_t vertBNext = ct.Vertex(DracoCornerTable::Next(cornerB));
            if (vertAPrev < 0 || vertBNext < 0 || vertexX == vertAPrev || vertexX == vertBNext) return false;
            ct.MapCornerToVertex(corner, vertexX);
            ct.MapCornerToVertex(corner + 1, vertBNext);
            ct.MapCornerToVertex(corner + 2, vertAPrev);
            ct.SetLeftMostCorner(vertAPrev, corner + 2);
            isVertHole[static_cast<size_t>(vertexX)] = 0u;
            activeStack.back() = corner;
        } else if (sym == 5u || sym == 3u) { // TOPOLOGY_R (5) or TOPOLOGY_L (3)
            if (activeStack.empty()) return false;
            int32_t cornerA = activeStack.back();
            if (ct.Opposite(cornerA) != DracoCornerTable::kInvalid) return false;
            int32_t oppC = (sym == 5u) ? (corner + 2) : (corner + 1);
            int32_t cornerL = (sym == 5u) ? (corner + 1) : corner;
            int32_t cornerR = (sym == 5u) ? corner : (corner + 2);
            ct.SetOppositeCorners(oppC, cornerA);
            int32_t newVert = ct.AddNewVertex();
            if (static_cast<uint32_t>(ct.NumVertices()) > maxVerts) return false;
            ct.MapCornerToVertex(oppC, newVert);
            ct.SetLeftMostCorner(newVert, oppC);
            int32_t vertR = ct.Vertex(DracoCornerTable::Prev(cornerA));
            int32_t vertL = ct.Vertex(DracoCornerTable::Next(cornerA));
            if (vertR < 0 || vertL < 0) return false;
            ct.MapCornerToVertex(cornerR, vertR);
            ct.SetLeftMostCorner(vertR, cornerR);
            ct.MapCornerToVertex(cornerL, vertL);
            activeStack.back() = corner;
            checkSplit = true;
        } else if (sym == 1u) { // TOPOLOGY_S
            if (activeStack.empty()) return false;
            int32_t cornerB = activeStack.back();
            activeStack.pop_back();
            auto itSp = topoSplitActiveCorners.find(static_cast<int32_t>(symId));
            if (itSp != topoSplitActiveCorners.end()) {
                activeStack.push_back(itSp->second);
            }
            if (activeStack.empty()) return false;
            int32_t cornerA = activeStack.back();
            if (cornerA == cornerB ||
                ct.Opposite(cornerA) != DracoCornerTable::kInvalid ||
                ct.Opposite(cornerB) != DracoCornerTable::kInvalid)
            {
                return false;
            }
            ct.SetOppositeCorners(cornerA, corner + 2);
            ct.SetOppositeCorners(cornerB, corner + 1);
            int32_t vertP = ct.Vertex(DracoCornerTable::Prev(cornerA));
            int32_t vertANext = ct.Vertex(DracoCornerTable::Next(cornerA));
            int32_t vertBPrev = ct.Vertex(DracoCornerTable::Prev(cornerB));
            int32_t cornerN = DracoCornerTable::Next(cornerB);
            int32_t vertN = ct.Vertex(cornerN);
            if (vertP < 0 || vertANext < 0 || vertBPrev < 0 || vertN < 0) return false;
            ct.MapCornerToVertex(corner, vertP);
            ct.MapCornerToVertex(corner + 1, vertANext);
            ct.MapCornerToVertex(corner + 2, vertBPrev);
            ct.SetLeftMostCorner(vertBPrev, corner + 2);
            if (travType == 2u) {
                vertexValences[static_cast<size_t>(vertP)] += vertexValences[static_cast<size_t>(vertN)];
            }
            ct.SetLeftMostCorner(vertP, ct.LeftMostCorner(vertN));
            int32_t firstCorner = cornerN;
            int guard = 0;
            while (cornerN != DracoCornerTable::kInvalid) {
                if (++guard > ct.NumCorners() + 4) return false;
                ct.MapCornerToVertex(cornerN, vertP);
                cornerN = ct.SwingLeft(cornerN);
                if (cornerN == firstCorner) return false;
            }
            ct.MakeVertexIsolated(vertN);
            if (removeInvalidVertices) invalidVertices.push_back(vertN);
            activeStack.back() = corner;
        } else if (sym == 7u) { // TOPOLOGY_E
            int32_t v0 = ct.AddNewVertex();
            int32_t v1 = ct.AddNewVertex();
            int32_t v2 = ct.AddNewVertex();
            if (static_cast<uint32_t>(ct.NumVertices()) > maxVerts) return false;
            ct.MapCornerToVertex(corner, v0);
            ct.MapCornerToVertex(corner + 1, v1);
            ct.MapCornerToVertex(corner + 2, v2);
            ct.SetLeftMostCorner(v0, corner);
            ct.SetLeftMostCorner(v1, corner + 1);
            ct.SetLeftMostCorner(v2, corner + 2);
            activeStack.push_back(corner);
            checkSplit = true;
        } else {
            return false;
        }

        onNewActiveCorner(activeStack.back());

        if (checkSplit) {
            uint32_t encSymId = numEncodedSymbols - 1u - symId;
            while (splitIdx > 0 && splits[splitIdx - 1].sourceSymId == encSymId) {
                --splitIdx;
                if (splits[splitIdx].splitSymId >= numEncodedSymbols) return false;
                int32_t actTop = activeStack.back();
                int32_t newAct = (splits[splitIdx].sourceEdge == 1u)
                    ? DracoCornerTable::Next(actTop)
                    : DracoCornerTable::Prev(actTop);
                int32_t decSplitSymId = static_cast<int32_t>(numEncodedSymbols - 1u - splits[splitIdx].splitSymId);
                topoSplitActiveCorners[decSplitSymId] = newAct;
            }
        }
    }

    // Start faces
    while (!activeStack.empty()) {
        int32_t cornerA = activeStack.back();
        activeStack.pop_back();
        bool interiorFace = false;
        if (ver < DracoVer(2, 2)) {
            uint32_t fc = 0;
            if (!legacyStartFaceBr.ReadBits(1, fc)) return false;
            interiorFace = (fc != 0u);
        } else {
            interiorFace = startFaceDec.DecodeNextBit();
        }
        if (interiorFace) {
            if (curFace >= numFaces) return false;
            int32_t vertN = ct.Vertex(DracoCornerTable::Next(cornerA));
            if (vertN < 0) return false;
            int32_t cornerB = DracoCornerTable::Next(ct.LeftMostCorner(vertN));
            if (cornerB < 0) return false;
            int32_t vertX = ct.Vertex(DracoCornerTable::Next(cornerB));
            if (vertX < 0) return false;
            int32_t cornerC = DracoCornerTable::Next(ct.LeftMostCorner(vertX));
            if (cornerC < 0) return false;
            if (cornerA == cornerB || cornerA == cornerC || cornerB == cornerC) return false;
            if (ct.Opposite(cornerA) != DracoCornerTable::kInvalid ||
                ct.Opposite(cornerB) != DracoCornerTable::kInvalid ||
                ct.Opposite(cornerC) != DracoCornerTable::kInvalid)
            {
                return false;
            }
            int32_t vertP = ct.Vertex(DracoCornerTable::Next(cornerC));
            if (vertP < 0) return false;
            int32_t newCorner = static_cast<int32_t>(curFace * 3u);
            ++curFace;
            ct.SetOppositeCorners(newCorner, cornerA);
            ct.SetOppositeCorners(newCorner + 1, cornerB);
            ct.SetOppositeCorners(newCorner + 2, cornerC);
            ct.MapCornerToVertex(newCorner, vertX);
            ct.MapCornerToVertex(newCorner + 1, vertP);
            ct.MapCornerToVertex(newCorner + 2, vertN);
            isVertHole[static_cast<size_t>(vertX)] = 0u;
            isVertHole[static_cast<size_t>(vertP)] = 0u;
            isVertHole[static_cast<size_t>(vertN)] = 0u;
        }
    }
    if (curFace != numFaces) return false;

    // Compact isolated vertices when numAttrData == 0
    int32_t numConnectivityVerts = ct.NumVertices();
    for (int32_t invVert : invalidVertices) {
        int32_t srcVert = numConnectivityVerts - 1;
        while (srcVert >= 0 && ct.LeftMostCorner(srcVert) == DracoCornerTable::kInvalid) {
            srcVert = --numConnectivityVerts - 1;
        }
        if (srcVert < invVert) continue;
        int32_t startC = ct.LeftMostCorner(srcVert);
        int32_t cid = startC;
        bool swungRight = false;
        int guard = 0;
        while (cid != DracoCornerTable::kInvalid) {
            if (++guard > ct.NumCorners() + 4) return false;
            if (ct.Vertex(cid) != srcVert) return false;
            ct.MapCornerToVertex(cid, invVert);
            if (!swungRight) {
                cid = ct.SwingLeft(cid);
                if (cid == startC) break;
                if (cid == DracoCornerTable::kInvalid) {
                    swungRight = true;
                    cid = ct.SwingRight(startC);
                }
            } else {
                cid = ct.SwingRight(cid);
                if (cid == startC) break;
            }
        }
        ct.SetLeftMostCorner(invVert, ct.LeftMostCorner(srcVert));
        ct.MakeVertexIsolated(srcVert);
        isVertHole[static_cast<size_t>(invVert)] = isVertHole[static_cast<size_t>(srcVert)];
        isVertHole[static_cast<size_t>(srcVert)] = 0u;
        --numConnectivityVerts;
    }
    if (numConnectivityVerts < 3) return false;
    st.numConnectivityVerts = numConnectivityVerts;

    // Decode non-position attribute seams & build MeshAttributeCornerTables
    st.attrTables.resize(numAttrData);
    if (numAttrData > 0u) {
        std::vector<std::vector<int32_t>> seamCorners(numAttrData);
        const int32_t nCorners = ct.NumCorners();
        for (int32_t baseC = 0; baseC < nCorners; baseC += 3) {
            const int32_t cs[3] = { baseC, DracoCornerTable::Next(baseC), DracoCornerTable::Prev(baseC) };
            const int32_t srcFace = baseC / 3;
            for (int k = 0; k < 3; ++k) {
                int32_t opp = ct.Opposite(cs[k]);
                if (opp == DracoCornerTable::kInvalid) {
                    for (uint8_t a = 0; a < numAttrData; ++a) seamCorners[a].push_back(cs[k]);
                    continue;
                }
                if (ver >= DracoVer(2, 1) && (opp / 3) < srcFace) continue;
                for (uint8_t a = 0; a < numAttrData; ++a) {
                    if (attrSeamDecs[a].DecodeNextBit()) seamCorners[a].push_back(cs[k]);
                }
            }
        }
        for (uint8_t a = 0; a < numAttrData; ++a) {
            st.attrTables[a].InitEmpty(&ct);
            for (int32_t sc : seamCorners[a]) st.attrTables[a].AddSeamEdge(sc);
            if (!st.attrTables[a].RecomputeVertices()) return false;
        }
    }

    // AssignPointsToCorners
    const int32_t nCorners = ct.NumCorners();
    st.pointToCorner.clear();
    st.cornerToPoint.assign(static_cast<size_t>(nCorners), -1);

    if (numAttrData == 0u) {
        st.numPoints = static_cast<uint32_t>(numConnectivityVerts);
        for (int32_t c = 0; c < nCorners; ++c) {
            int32_t v = ct.Vertex(c);
            if (v < 0 || v >= numConnectivityVerts) return false;
            st.cornerToPoint[static_cast<size_t>(c)] = v;
        }
        return true;
    }

    const int32_t maxRingGuard = nCorners + 4;
    for (int32_t v = 0; v < ct.NumVertices(); ++v) {
        int32_t c = ct.LeftMostCorner(v);
        if (c == DracoCornerTable::kInvalid) continue;
        int32_t dedupFirst = c;
        if (isVertHole[static_cast<size_t>(v)] == 0u) {
            for (uint8_t a = 0; a < numAttrData; ++a) {
                if (!st.attrTables[a].IsCornerOnSeam(c)) continue;
                int32_t vertId = st.attrTables[a].Vertex(c);
                int32_t actC = ct.SwingRight(c);
                bool found = false;
                int guard = 0;
                while (actC != c) {
                    if (actC == DracoCornerTable::kInvalid || ++guard > maxRingGuard) return false;
                    if (st.attrTables[a].Vertex(actC) != vertId) {
                        dedupFirst = actC;
                        found = true;
                        break;
                    }
                    actC = ct.SwingRight(actC);
                }
                if (found) break;
            }
        }
        c = dedupFirst;
        st.cornerToPoint[static_cast<size_t>(c)] = static_cast<int32_t>(st.pointToCorner.size());
        st.pointToCorner.push_back(c);
        int32_t prevC = c;
        c = ct.SwingRight(c);
        int guard = 0;
        while (c != DracoCornerTable::kInvalid && c != dedupFirst) {
            if (++guard > maxRingGuard) return false;
            bool seam = false;
            for (uint8_t a = 0; a < numAttrData; ++a) {
                if (st.attrTables[a].Vertex(c) != st.attrTables[a].Vertex(prevC)) {
                    seam = true;
                    break;
                }
            }
            if (seam) {
                st.cornerToPoint[static_cast<size_t>(c)] = static_cast<int32_t>(st.pointToCorner.size());
                st.pointToCorner.push_back(c);
            } else {
                st.cornerToPoint[static_cast<size_t>(c)] = st.cornerToPoint[static_cast<size_t>(prevC)];
            }
            prevC = c;
            c = ct.SwingRight(c);
        }
    }
    for (int32_t c = 0; c < nCorners; ++c) {
        if (st.cornerToPoint[static_cast<size_t>(c)] < 0) return false;
    }
    st.numPoints = static_cast<uint32_t>(st.pointToCorner.size());
    return st.numPoints >= 3u;
}

// ============================================================================
// Draco Mesh Traversal Sequencer (DepthFirstTraverser & MaxPredictionDegreeTraverser)
// ============================================================================
template <class TableT>
inline bool BuildDracoTraversalSequence(
    const TableT& tbl,
    const std::vector<int32_t>& cornerToPoint,
    uint8_t traversalMethod,
    std::vector<uint32_t>& outPointIds,
    std::vector<int32_t>& outEncodedToCorner,
    std::vector<int32_t>& outVertToEncoded)
{
    const int32_t nFaces = tbl.NumFaces();
    const int32_t nVerts = tbl.NumVertices();
    outPointIds.clear();
    outEncodedToCorner.clear();
    outPointIds.reserve(static_cast<size_t>(nVerts));
    outEncodedToCorner.reserve(static_cast<size_t>(nVerts));
    outVertToEncoded.assign(static_cast<size_t>(nVerts), -1);
    if (nFaces <= 0 || nVerts <= 0) return false;

    std::vector<uint8_t> faceVisited(static_cast<size_t>(nFaces), 0u);
    std::vector<uint8_t> vertVisited(static_cast<size_t>(nVerts), 0u);

    auto isFaceVisited = [&](int32_t f) -> bool {
        return f < 0 || f >= nFaces || faceVisited[static_cast<size_t>(f)] != 0u;
    };
    auto onNewVertexVisited = [&](int32_t v, int32_t c) {
        outPointIds.push_back(static_cast<uint32_t>(cornerToPoint[static_cast<size_t>(c)]));
        outEncodedToCorner.push_back(c);
        outVertToEncoded[static_cast<size_t>(v)] = static_cast<int32_t>(outEncodedToCorner.size() - 1);
    };

    if (traversalMethod == 0u) {
        // DepthFirstTraverser (depth_first_traverser.h)
        std::vector<int32_t> stack;
        for (int32_t f = 0; f < nFaces; ++f) {
            int32_t cornerId = 3 * f;
            if (isFaceVisited(cornerId / 3)) continue;
            stack.clear();
            stack.push_back(cornerId);

            int32_t nextV = tbl.Vertex(TableT::Next(cornerId));
            int32_t prevV = tbl.Vertex(TableT::Prev(cornerId));
            if (nextV < 0 || prevV < 0) return false;
            if (!vertVisited[static_cast<size_t>(nextV)]) {
                vertVisited[static_cast<size_t>(nextV)] = 1u;
                onNewVertexVisited(nextV, TableT::Next(cornerId));
            }
            if (!vertVisited[static_cast<size_t>(prevV)]) {
                vertVisited[static_cast<size_t>(prevV)] = 1u;
                onNewVertexVisited(prevV, TableT::Prev(cornerId));
            }

            while (!stack.empty()) {
                cornerId = stack.back();
                if (cornerId < 0 || isFaceVisited(cornerId / 3)) {
                    stack.pop_back();
                    continue;
                }
                for (;;) {
                    int32_t faceId = cornerId / 3;
                    faceVisited[static_cast<size_t>(faceId)] = 1u;
                    int32_t vertId = tbl.Vertex(cornerId);
                    if (vertId < 0) return false;
                    if (!vertVisited[static_cast<size_t>(vertId)]) {
                        bool onBoundary = tbl.IsOnBoundary(vertId);
                        vertVisited[static_cast<size_t>(vertId)] = 1u;
                        onNewVertexVisited(vertId, cornerId);
                        if (!onBoundary) {
                            cornerId = tbl.GetRightCorner(cornerId);
                            if (cornerId < 0) { stack.pop_back(); break; }
                            continue;
                        }
                    }
                    int32_t rightC = tbl.GetRightCorner(cornerId);
                    int32_t leftC  = tbl.GetLeftCorner(cornerId);
                    bool rightVis = (rightC < 0) || isFaceVisited(rightC / 3);
                    bool leftVis  = (leftC  < 0) || isFaceVisited(leftC / 3);
                    if (rightVis) {
                        if (leftVis) {
                            stack.pop_back();
                            break;
                        }
                        cornerId = leftC;
                    } else {
                        if (leftVis) {
                            cornerId = rightC;
                        } else {
                            stack.back() = leftC;
                            stack.push_back(rightC);
                            break;
                        }
                    }
                }
            }
        }
        return true;
    }

    if (traversalMethod == 1u) {
        // MaxPredictionDegreeTraverser (max_prediction_degree_traverser.h)
        constexpr int kMaxPriority = 3;
        std::vector<int32_t> stacks[kMaxPriority];
        std::vector<int> predDegree(static_cast<size_t>(nVerts), 0);
        int bestPriority = 0;

        auto popNext = [&]() -> int32_t {
            for (int i = bestPriority; i < kMaxPriority; ++i) {
                if (!stacks[i].empty()) {
                    int32_t ret = stacks[i].back();
                    stacks[i].pop_back();
                    bestPriority = i;
                    return ret;
                }
            }
            return TableT::kInvalid;
        };
        auto addCorner = [&](int32_t ci, int prio) {
            stacks[prio].push_back(ci);
            if (prio < bestPriority) bestPriority = prio;
        };
        auto computePriority = [&](int32_t ci) -> int {
            int32_t vTip = tbl.Vertex(ci);
            int prio = 0;
            if (vTip >= 0 && !vertVisited[static_cast<size_t>(vTip)]) {
                int deg = ++predDegree[static_cast<size_t>(vTip)];
                prio = (deg > 1) ? 1 : 2;
            }
            return (prio >= kMaxPriority) ? (kMaxPriority - 1) : prio;
        };

        for (int32_t f = 0; f < nFaces; ++f) {
            int32_t cornerId = 3 * f;
            stacks[0].push_back(cornerId);
            bestPriority = 0;

            int32_t nextV = tbl.Vertex(TableT::Next(cornerId));
            int32_t prevV = tbl.Vertex(TableT::Prev(cornerId));
            int32_t tipV  = tbl.Vertex(cornerId);
            if (nextV < 0 || prevV < 0 || tipV < 0) return false;
            if (!vertVisited[static_cast<size_t>(nextV)]) {
                vertVisited[static_cast<size_t>(nextV)] = 1u;
                onNewVertexVisited(nextV, TableT::Next(cornerId));
            }
            if (!vertVisited[static_cast<size_t>(prevV)]) {
                vertVisited[static_cast<size_t>(prevV)] = 1u;
                onNewVertexVisited(prevV, TableT::Prev(cornerId));
            }
            if (!vertVisited[static_cast<size_t>(tipV)]) {
                vertVisited[static_cast<size_t>(tipV)] = 1u;
                onNewVertexVisited(tipV, cornerId);
            }

            while ((cornerId = popNext()) != TableT::kInvalid) {
                if (isFaceVisited(cornerId / 3)) continue;
                for (;;) {
                    faceVisited[static_cast<size_t>(cornerId / 3)] = 1u;
                    int32_t vertId = tbl.Vertex(cornerId);
                    if (vertId < 0) return false;
                    if (!vertVisited[static_cast<size_t>(vertId)]) {
                        vertVisited[static_cast<size_t>(vertId)] = 1u;
                        onNewVertexVisited(vertId, cornerId);
                    }
                    int32_t rightC = tbl.GetRightCorner(cornerId);
                    int32_t leftC  = tbl.GetLeftCorner(cornerId);
                    bool rightVis = (rightC < 0) || isFaceVisited(rightC / 3);
                    bool leftVis  = (leftC  < 0) || isFaceVisited(leftC / 3);
                    if (!leftVis) {
                        int prio = computePriority(leftC);
                        if (rightVis && prio <= bestPriority) {
                            cornerId = leftC;
                            continue;
                        }
                        addCorner(leftC, prio);
                    }
                    if (!rightVis) {
                        int prio = computePriority(rightC);
                        if (prio <= bestPriority) {
                            cornerId = rightC;
                            continue;
                        }
                        addCorner(rightC, prio);
                    }
                    break;
                }
            }
        }
        return true;
    }
    return false;
}

// Prediction transform helper (DELTA = 0, WRAP = 1, NORMAL_OCTAHEDRON = 2, NORMAL_OCTAHEDRON_CANONICALIZED = 3)
struct DracoPredTransform {
    int8_t type = -1;
    int32_t wrapMin = 0;
    int32_t wrapMax = 0;
    int64_t wrapMaxDif = 0;

    bool AreCorrectionsPositive() const { return type == 2 || type == 3; }

    bool DecodeTransformData(const uint8_t* data, size_t size, size_t& pos, uint16_t ver) {
        if (type <= 0) return true; // NONE (-1) or DELTA (0)
        if (type == 1) { // WRAP
            if (pos + 8 > size) return false;
            wrapMin = RdI32LE(data + pos + 0);
            wrapMax = RdI32LE(data + pos + 4);
            pos += 8;
            if (wrapMin > wrapMax) return false;
            wrapMaxDif = 1LL + static_cast<int64_t>(wrapMax) - static_cast<int64_t>(wrapMin);
            if (wrapMaxDif <= 0 || wrapMaxDif >= INT32_MAX) return false;
            return true;
        }
        if (type == 2) { // NORMAL_OCTAHEDRON
            if (pos + 4 > size) return false;
            int32_t maxQ = RdI32LE(data + pos);
            pos += 4;
            if (ver < DracoVer(2, 2)) {
                if (pos + 4 > size) return false;
                pos += 4; // center_value
            }
            return maxQ > 0;
        }
        if (type == 3) { // NORMAL_OCTAHEDRON_CANONICALIZED
            if (pos + 8 > size) return false;
            int32_t maxQ = RdI32LE(data + pos);
            pos += 8; // max_quantized_value + center_value
            return maxQ > 0;
        }
        return false;
    }

    void ComputeOriginal(const int32_t* pred, const int32_t* corr, int32_t* out, int numComps) const {
        if (type == 1) {
            for (int c = 0; c < numComps; ++c) {
                int64_t clamped = std::clamp<int64_t>(pred[c], wrapMin, wrapMax);
                int64_t v = clamped + static_cast<int64_t>(corr[c]);
                if (v > wrapMax) v -= wrapMaxDif;
                else if (v < wrapMin) v += wrapMaxDif;
                out[c] = static_cast<int32_t>(v);
            }
        } else {
            for (int c = 0; c < numComps; ++c) {
                out[c] = AddAsUnsigned32(pred[c], corr[c]);
            }
        }
    }
};

template <class TableT>
inline bool ComputeDracoParallelogram(
    int dataEntryId,
    int32_t ci,
    const TableT& tbl,
    const std::vector<int32_t>& vertToData,
    const int32_t* inData,
    int numComps,
    int32_t* outPred)
{
    int32_t oci = tbl.Opposite(ci);
    if (oci < 0) return false;
    int32_t vO = tbl.Vertex(oci);
    int32_t vN = tbl.Vertex(TableT::Next(oci));
    int32_t vP = tbl.Vertex(TableT::Prev(oci));
    if (vO < 0 || vN < 0 || vP < 0) return false;
    int32_t dO = vertToData[static_cast<size_t>(vO)];
    int32_t dN = vertToData[static_cast<size_t>(vN)];
    int32_t dP = vertToData[static_cast<size_t>(vP)];
    if (dO >= 0 && dN >= 0 && dP >= 0 && dO < dataEntryId && dN < dataEntryId && dP < dataEntryId) {
        const size_t offO = static_cast<size_t>(dO) * numComps;
        const size_t offN = static_cast<size_t>(dN) * numComps;
        const size_t offP = static_cast<size_t>(dP) * numComps;
        for (int c = 0; c < numComps; ++c) {
            int64_t r = (static_cast<int64_t>(inData[offN + c]) + static_cast<int64_t>(inData[offP + c])) -
                        static_cast<int64_t>(inData[offO + c]);
            outPred[c] = static_cast<int32_t>(r);
        }
        return true;
    }
    return false;
}

template <class TableT>
inline bool ReconstructDracoMeshPrediction(
    int8_t predScheme,
    const DracoPredTransform& tr,
    const TableT& tbl,
    const std::vector<int32_t>& dataToCorner,
    const std::vector<int32_t>& vertToData,
    const std::vector<uint8_t> creaseFlags[4],
    int numComps,
    std::vector<int32_t>& vals)
{
    const int nEntries = static_cast<int>(dataToCorner.size());
    if (nEntries <= 0 || static_cast<size_t>(nEntries) * numComps > vals.size()) return false;

    std::vector<int32_t> corr = vals; // copy corrections
    std::vector<int32_t> zeroPred(static_cast<size_t>(numComps), 0);
    tr.ComputeOriginal(zeroPred.data(), corr.data(), vals.data(), numComps);

    if (predScheme == 1) { // MESH_PREDICTION_PARALLELOGRAM
        std::vector<int32_t> pred(static_cast<size_t>(numComps), 0);
        for (int p = 1; p < nEntries; ++p) {
            size_t dstOff = static_cast<size_t>(p) * numComps;
            if (ComputeDracoParallelogram(p, dataToCorner[static_cast<size_t>(p)], tbl, vertToData, vals.data(), numComps, pred.data())) {
                tr.ComputeOriginal(pred.data(), corr.data() + dstOff, vals.data() + dstOff, numComps);
            } else {
                tr.ComputeOriginal(vals.data() + dstOff - numComps, corr.data() + dstOff, vals.data() + dstOff, numComps);
            }
        }
        return true;
    }
    if (predScheme == 2) { // MESH_PREDICTION_MULTI_PARALLELOGRAM
        std::vector<int32_t> pred(static_cast<size_t>(numComps), 0);
        std::vector<int32_t> single(static_cast<size_t>(numComps), 0);
        const int maxRing = tbl.NumCorners() + 4;
        for (int p = 1; p < nEntries; ++p) {
            int32_t startC = dataToCorner[static_cast<size_t>(p)];
            int32_t cid = startC;
            int count = 0;
            std::fill(pred.begin(), pred.end(), 0);
            int guard = 0;
            while (cid != TableT::kInvalid) {
                if (++guard > maxRing) return false;
                if (ComputeDracoParallelogram(p, cid, tbl, vertToData, vals.data(), numComps, single.data())) {
                    for (int c = 0; c < numComps; ++c) pred[static_cast<size_t>(c)] = AddAsUnsigned32(pred[static_cast<size_t>(c)], single[static_cast<size_t>(c)]);
                    ++count;
                }
                cid = tbl.SwingRight(cid);
                if (cid == startC) break;
            }
            size_t dstOff = static_cast<size_t>(p) * numComps;
            if (count == 0) {
                tr.ComputeOriginal(vals.data() + dstOff - numComps, corr.data() + dstOff, vals.data() + dstOff, numComps);
            } else {
                for (int c = 0; c < numComps; ++c) pred[static_cast<size_t>(c)] /= count;
                tr.ComputeOriginal(pred.data(), corr.data() + dstOff, vals.data() + dstOff, numComps);
            }
        }
        return true;
    }
    if (predScheme == 4) { // MESH_PREDICTION_CONSTRAINED_MULTI_PARALLELOGRAM
        constexpr int kMaxPar = 4;
        std::vector<int32_t> predVals[kMaxPar];
        for (int i = 0; i < kMaxPar; ++i) predVals[i].assign(static_cast<size_t>(numComps), 0);
        std::vector<int32_t> multiPred(static_cast<size_t>(numComps), 0);
        size_t creasePos[kMaxPar] = {};
        const int maxRing = tbl.NumCorners() + 4;

        for (int p = 1; p < nEntries; ++p) {
            int32_t startC = dataToCorner[static_cast<size_t>(p)];
            int32_t cid = startC;
            int nPar = 0;
            bool firstPass = true;
            int guard = 0;
            while (cid != TableT::kInvalid) {
                if (++guard > maxRing) return false;
                if (ComputeDracoParallelogram(p, cid, tbl, vertToData, vals.data(), numComps, predVals[nPar].data())) {
                    if (++nPar == kMaxPar) break;
                }
                cid = firstPass ? tbl.SwingLeft(cid) : tbl.SwingRight(cid);
                if (cid == startC) break;
                if (cid == TableT::kInvalid && firstPass) {
                    firstPass = false;
                    cid = tbl.SwingRight(startC);
                }
            }
            int nUsed = 0;
            if (nPar > 0) {
                std::fill(multiPred.begin(), multiPred.end(), 0);
                int ctx = nPar - 1;
                for (int i = 0; i < nPar; ++i) {
                    size_t cp = creasePos[ctx]++;
                    if (cp >= creaseFlags[ctx].size()) return false;
                    if (creaseFlags[ctx][cp] == 0u) {
                        ++nUsed;
                        for (int c = 0; c < numComps; ++c) {
                            multiPred[static_cast<size_t>(c)] = AddAsUnsigned32(multiPred[static_cast<size_t>(c)], predVals[i][static_cast<size_t>(c)]);
                        }
                    }
                }
            }
            size_t dstOff = static_cast<size_t>(p) * numComps;
            if (nUsed == 0) {
                tr.ComputeOriginal(vals.data() + dstOff - numComps, corr.data() + dstOff, vals.data() + dstOff, numComps);
            } else {
                for (int c = 0; c < numComps; ++c) multiPred[static_cast<size_t>(c)] /= nUsed;
                tr.ComputeOriginal(multiPred.data(), corr.data() + dstOff, vals.data() + dstOff, numComps);
            }
        }
        return true;
    }
    return false;
}

// ============================================================================
// Draco Dynamic Integer Points KD-Tree Decoder (compression levels 0..6)
// ============================================================================
E2_DRACO_NOINLINE inline bool DispatchDracoKdTreePoints(
    uint8_t compLevel,
    const uint8_t* data,
    size_t size,
    size_t& pos,
    uint16_t ver,
    uint32_t dimension,
    uint32_t expectedPoints,
    std::vector<uint32_t>& outFlatCoords)
{
    if (compLevel > 6u || dimension == 0 || dimension > 64 || pos + 8 > size) return false;
    uint32_t bitLength = RdU32LE(data + pos);
    uint32_t numPoints = RdU32LE(data + pos + 4);
    pos += 8;
    if (bitLength > 32u || numPoints != expectedPoints) return false;
    outFlatCoords.clear();
    if (numPoints == 0) return true;
    outFlatCoords.reserve(static_cast<size_t>(numPoints) * dimension);

    DracoDirectBitDecoder numDirectDec;
    DracoRAbsDecoder numRabsDec;
    DracoFoldedRAnsBitDecoder numFoldedDec;
    DracoDirectBitDecoder remDec, axisDec, halfDec;

    if (compLevel >= 4u) {
        if (!numFoldedDec.StartDecoding(data, size, pos, ver)) return false;
    } else if (compLevel >= 2u) {
        if (!numRabsDec.StartDecoding(data, size, pos, ver)) return false;
    } else {
        if (!numDirectDec.StartDecoding(data, size, pos)) return false;
    }
    if (!remDec.StartDecoding(data, size, pos)) return false;
    if (!axisDec.StartDecoding(data, size, pos)) return false;
    if (!halfDec.StartDecoding(data, size, pos)) return false;

    const bool selectAxis = (compLevel == 6u);
    auto getAxis = [&](uint32_t numRemPts, const std::vector<uint32_t>& levels, uint32_t lastAxis, uint32_t& outAxis) -> bool {
        if (!selectAxis) {
            outAxis = (lastAxis + 1u == dimension) ? 0u : (lastAxis + 1u);
            return true;
        }
        uint32_t best = 0;
        if (numRemPts < 64u) {
            for (uint32_t ax = 1; ax < dimension; ++ax) {
                if (levels[best] > levels[ax]) best = ax;
            }
        } else {
            if (!axisDec.DecodeLeastSignificantBits32(4, best)) return false;
        }
        outAxis = best;
        return best < dimension;
    };

    auto decodeNumberBits = [&](int nbits, uint32_t& outVal) -> bool {
        if (compLevel >= 4u) return numFoldedDec.DecodeLeastSignificantBits32(nbits, outVal);
        if (compLevel >= 2u) return numRabsDec.DecodeLeastSignificantBits32(nbits, outVal);
        return numDirectDec.DecodeLeastSignificantBits32(nbits, outVal);
    };

    struct Status { uint32_t numRem; uint32_t lastAxis; uint32_t depth; };
    struct SplitUndo { uint32_t depth; uint32_t axis; uint32_t oldLevel; uint32_t oldBase; };

    std::vector<uint32_t> base(dimension, 0u), levels(dimension, 0u), pt(dimension, 0u), axes(dimension, 0u);
    std::vector<SplitUndo> undo;
    std::vector<Status> stack;
    stack.push_back({ numPoints, 0u, 0u });
    uint32_t depth = 0u;
    uint32_t decodedPts = 0u;

    while (!stack.empty()) {
        Status st = stack.back();
        stack.pop_back();
        if (st.depth != depth) {
            if (st.depth > depth) return false;
            while (!undo.empty()) {
                const SplitUndo& sp = undo.back();
                if (sp.depth < st.depth) break;
                base[sp.axis] = sp.oldBase;
                if (sp.depth == st.depth) break;
                levels[sp.axis] = sp.oldLevel;
                undo.pop_back();
            }
            depth = st.depth;
        }
        if (st.numRem > numPoints) return false;
        uint32_t axis = 0;
        if (!getAxis(st.numRem, levels, st.lastAxis, axis)) return false;
        uint32_t level = levels[axis];
        if (level > bitLength) return false;

        if (bitLength - level == 0u) {
            if (decodedPts + st.numRem > numPoints) return false;
            for (uint32_t i = 0; i < st.numRem; ++i) {
                outFlatCoords.insert(outFlatCoords.end(), base.begin(), base.end());
                ++decodedPts;
            }
            continue;
        }
        if (st.numRem <= 2u) {
            if (decodedPts + st.numRem > numPoints) return false;
            axes[0] = axis;
            for (uint32_t i = 1; i < dimension; ++i) {
                axes[i] = (axes[i - 1] + 1u == dimension) ? 0u : (axes[i - 1] + 1u);
            }
            for (uint32_t i = 0; i < st.numRem; ++i) {
                for (uint32_t j = 0; j < dimension; ++j) {
                    uint32_t aj = axes[j];
                    uint32_t remBits = bitLength - levels[aj];
                    uint32_t v = 0;
                    if (remBits > 0u && !remDec.DecodeLeastSignificantBits32(static_cast<int>(remBits), v)) return false;
                    pt[aj] = base[aj] | v;
                }
                outFlatCoords.insert(outFlatCoords.end(), pt.begin(), pt.end());
                ++decodedPts;
            }
            continue;
        }
        if (decodedPts > numPoints) return false;
        int remBits = static_cast<int>(bitLength - level);
        uint32_t modifier = 1u << (remBits - 1);
        int incomingBits = DracoMostSignificantBit(st.numRem);
        uint32_t number = 0;
        if (incomingBits > 0 && !decodeNumberBits(incomingBits, number)) return false;
        uint32_t firstHalf = st.numRem / 2u;
        if (firstHalf < number) return false;
        firstHalf -= number;
        uint32_t secondHalf = st.numRem - firstHalf;
        if (firstHalf != secondHalf) {
            if (!halfDec.DecodeNextBit()) std::swap(firstHalf, secondHalf);
        }
        if (depth == UINT32_MAX) return false;
        undo.push_back({ depth, axis, levels[axis], base[axis] });
        levels[axis] += 1u;
        base[axis] += modifier;
        if (firstHalf > 0u) stack.push_back({ firstHalf, axis, depth });
        if (secondHalf > 0u) stack.push_back({ secondHalf, axis, depth + 1u });
        ++depth;
    }
    return decodedPts == numPoints;
}

} // namespace DracoMeshoptDetail

// ============================================================================
// Core Google Draco Decoder (used by both .drc and glTF KHR_draco_mesh_compression)
//   targetPosUniqueId < 0 -> selects POSITION (attType == 0)
//   targetPosUniqueId >= 0 -> selects attribute matching uniqueId (or fallback attType == 0)
// ============================================================================
E2_DRACO_NOINLINE inline bool DecodeDracoBufferToMesh(
    const uint8_t* data,
    size_t size,
    int targetPosUniqueId,
    std::vector<Vec3f>& outPositions,
    std::vector<std::array<uint32_t, 3>>& outTris,
    uint8_t& outEncType,
    uint8_t& outMaj,
    uint8_t& outMin)
{
    using namespace DracoMeshoptDetail;
    outPositions.clear();
    outTris.clear();
    if (!data || size < 12 || std::memcmp(data, "DRACO", 5) != 0) return false;

    outMaj = data[5];
    outMin = data[6];
    outEncType = data[7];          // 0 = POINT_CLOUD, 1 = TRIANGULAR_MESH
    uint8_t encMethod = data[8];   // 0 = SEQUENTIAL, 1 = EDGEBREAKER (mesh) / KD_TREE (point cloud)
    uint16_t flags = RdU16LE(data + 9);
    if (outMaj < 1 || outMaj > 2) return false;
    if ((outEncType != 0u && outEncType != 1u) || (encMethod != 0u && encMethod != 1u)) return false;
    const uint16_t ver = DracoVer(outMaj, outMin);

    size_t pos = 11;

    // 1. Optional Metadata block (METADATA_FLAG_MASK = 0x8000, supported since v1.3)
    if (ver >= DracoVer(1, 3) && (flags & 0x8000u) != 0u) {
        auto skipMeta = [&](auto& self, int depth) -> bool {
            if (depth > 16) return false;
            uint32_t nEntries = 0;
            if (!ReadLeb128U32(data, size, pos, nEntries) || nEntries > 4096) return false;
            for (uint32_t e = 0; e < nEntries; ++e) {
                if (pos >= size) return false;
                uint8_t kl = data[pos++];
                if (kl > size - pos) return false;
                pos += kl;
                if (pos >= size) return false;
                uint8_t vl = data[pos++];
                if (vl > size - pos) return false;
                pos += vl;
            }
            uint32_t nSub = 0;
            if (!ReadLeb128U32(data, size, pos, nSub) || nSub > 1024) return false;
            for (uint32_t s = 0; s < nSub; ++s) {
                if (pos >= size) return false;
                uint8_t sl = data[pos++];
                if (sl > size - pos) return false;
                pos += sl;
                if (!self(self, depth + 1)) return false;
            }
            return true;
        };
        uint32_t nAttMeta = 0;
        if (!ReadLeb128U32(data, size, pos, nAttMeta) || nAttMeta > 1024) return false;
        for (uint32_t a = 0; a < nAttMeta; ++a) {
            uint32_t uid = 0;
            if (!ReadLeb128U32(data, size, pos, uid)) return false;
            if (!skipMeta(skipMeta, 0)) return false;
        }
        if (!skipMeta(skipMeta, 0)) return false;
    }

    auto dtElemBytes = [](uint8_t dt) -> size_t {
        switch (dt) {
            case 1: case 2: case 11: return 1; // INT8, UINT8, BOOL
            case 3: case 4: return 2;          // INT16, UINT16
            case 5: case 6: case 9: return 4;  // INT32, UINT32, FLOAT32
            case 7: case 8: case 10: return 8; // INT64, UINT64, FLOAT64
            default: return 0;
        }
    };

    struct DracoAttrDesc {
        uint8_t attType = 0;
        uint8_t dataType = 0;
        uint8_t numComps = 0;
        uint8_t normalized = 0;
        uint32_t uniqueId = 0;
        uint8_t seqType = 0;
    };

    auto decodeAttrDescriptors = [&](uint32_t& outNAttrs, std::vector<DracoAttrDesc>& attrs) -> bool {
        outNAttrs = 0;
        if (!ReadDracoU32OrVarint(data, size, pos, ver, DracoVer(2, 0), outNAttrs)) return false;
        if (outNAttrs == 0 || outNAttrs > 64 || outNAttrs > 5u * (size - pos)) return false;
        attrs.resize(outNAttrs);
        for (uint32_t a = 0; a < outNAttrs; ++a) {
            if (pos + 4 > size) return false;
            attrs[a].attType    = data[pos++];
            attrs[a].dataType   = data[pos++];
            attrs[a].numComps   = data[pos++];
            attrs[a].normalized = data[pos++];
            if (ver < DracoVer(1, 3)) {
                if (pos + 2 > size) return false;
                attrs[a].uniqueId = RdU16LE(data + pos);
                pos += 2;
            } else {
                if (!ReadLeb128U32(data, size, pos, attrs[a].uniqueId)) return false;
            }
            if (dtElemBytes(attrs[a].dataType) == 0 || attrs[a].numComps == 0 || attrs[a].numComps > 16) {
                return false;
            }
        }
        return true;
    };

    // ========================================================================
    // 2A. Point Cloud KD-Tree Path (encType == 0, encMethod == 1)
    // ========================================================================
    if (outEncType == 0u && encMethod == 1u) {
        if (pos + 5 > size) return false;
        int32_t nPtsI = RdI32LE(data + pos);
        pos += 4;
        if (nPtsI <= 0 || nPtsI > 4000000) return false;
        uint32_t nPts = static_cast<uint32_t>(nPtsI);

        uint8_t numDecoders = data[pos++];
        if (numDecoders != 1u) return false;
        uint32_t nAttrs = 0;
        std::vector<DracoAttrDesc> attrs;
        if (!decodeAttrDescriptors(nAttrs, attrs)) return false;

        int posAttrIdx = -1;
        for (size_t a = 0; a < attrs.size(); ++a) {
            bool match = (targetPosUniqueId >= 0)
                ? (static_cast<int>(attrs[a].uniqueId) == targetPosUniqueId)
                : (attrs[a].attType == 0u);
            if (match && attrs[a].numComps >= 3) { posAttrIdx = static_cast<int>(a); break; }
        }
        if (posAttrIdx < 0) return false;

        if (ver >= DracoVer(2, 3)) {
            if (pos >= size) return false;
            uint8_t compLevel = data[pos++];
            uint32_t totalDim = 0;
            std::vector<uint32_t> attOffsets(attrs.size(), 0);
            size_t numSignedComps = 0;
            for (size_t a = 0; a < attrs.size(); ++a) {
                attOffsets[a] = totalDim;
                totalDim += attrs[a].numComps;
                uint8_t dt = attrs[a].dataType;
                if (dt == 1 || dt == 3 || dt == 5) numSignedComps += attrs[a].numComps;
                else if (dt != 2 && dt != 4 && dt != 6 && dt != 9) return false;
            }
            std::vector<uint32_t> flatCoords;
            if (!DispatchDracoKdTreePoints(compLevel, data, size, pos, ver, totalDim, nPts, flatCoords)) return false;

            std::vector<std::vector<float>> minVals(attrs.size());
            std::vector<float> ranges(attrs.size(), 0.0f);
            std::vector<uint8_t> qBits(attrs.size(), 0u);
            for (size_t a = 0; a < attrs.size(); ++a) {
                if (attrs[a].dataType == 9u) {
                    size_t qb = static_cast<size_t>(attrs[a].numComps) * 4u + 5u;
                    if (qb > size - pos) return false;
                    minVals[a].resize(attrs[a].numComps);
                    for (uint8_t c = 0; c < attrs[a].numComps; ++c) minVals[a][c] = RdF32LE(data + pos + c * 4u);
                    ranges[a] = RdF32LE(data + pos + attrs[a].numComps * 4u);
                    qBits[a]  = data[pos + attrs[a].numComps * 4u + 4u];
                    pos += qb;
                    if (qBits[a] < 1u || qBits[a] > 30u || !std::isfinite(ranges[a]) || ranges[a] < 0.0f) return false;
                }
            }
            std::vector<int32_t> minSigned(numSignedComps, 0);
            for (size_t s = 0; s < numSignedComps; ++s) {
                uint32_t rawU = 0;
                if (!ReadLeb128U32(data, size, pos, rawU)) return false;
                minSigned[s] = DracoConvertSymbolToSignedInt(rawU);
            }

            const auto& pa = attrs[static_cast<size_t>(posAttrIdx)];
            const uint32_t pOff = attOffsets[static_cast<size_t>(posAttrIdx)];
            outPositions.resize(nPts);
            if (pa.dataType == 9u) {
                float step = (ranges[posAttrIdx] > 0.0f)
                    ? (ranges[posAttrIdx] / static_cast<float>((1ULL << qBits[posAttrIdx]) - 1ULL))
                    : 0.0f;
                for (uint32_t i = 0; i < nPts; ++i) {
                    const uint32_t* row = flatCoords.data() + static_cast<size_t>(i) * totalDim + pOff;
                    outPositions[i] = {
                        minVals[posAttrIdx][0] + static_cast<float>(row[0]) * step,
                        minVals[posAttrIdx][1] + static_cast<float>(row[1]) * step,
                        minVals[posAttrIdx][2] + static_cast<float>(row[2]) * step
                    };
                }
            } else {
                size_t signedBase = 0;
                for (int a = 0; a < posAttrIdx; ++a) {
                    uint8_t dt = attrs[a].dataType;
                    if (dt == 1 || dt == 3 || dt == 5) signedBase += attrs[a].numComps;
                }
                bool isSigned = (pa.dataType == 1 || pa.dataType == 3 || pa.dataType == 5);
                for (uint32_t i = 0; i < nPts; ++i) {
                    const uint32_t* row = flatCoords.data() + static_cast<size_t>(i) * totalDim + pOff;
                    int32_t x = static_cast<int32_t>(row[0]) + (isSigned ? minSigned[signedBase + 0] : 0);
                    int32_t y = static_cast<int32_t>(row[1]) + (isSigned ? minSigned[signedBase + 1] : 0);
                    int32_t z = static_cast<int32_t>(row[2]) + (isSigned ? minSigned[signedBase + 2] : 0);
                    outPositions[i] = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) };
                }
            }
            return true;
        }

        // Legacy < v2.3 KD-tree point cloud (FloatPointsTreeDecoder / kKdTreeIntegerEncoding)
        if (pos >= size) return false;
        uint8_t kdMethod = data[pos++];
        if (kdMethod == 0u) { // kKdTreeQuantizationEncoding
            if (attrs.size() != 1 || attrs[0].numComps != 3 || pos + 5 > size) return false;
            pos += 1; // outer compression_level
            uint32_t hdrPts = RdU32LE(data + pos);
            pos += 4;
            if (hdrPts != nPts || pos + 13 > size) return false;
            uint32_t qb = RdU32LE(data + pos);
            float qRange = RdF32LE(data + pos + 4);
            uint32_t treePts = RdU32LE(data + pos + 8);
            uint8_t treeLvl = data[pos + 12];
            pos += 13;
            if (qb < 1u || qb > 31u || treePts != nPts || !std::isfinite(qRange) || qRange < 0.0f) return false;
            std::vector<uint32_t> flatCoords;
            if (!DispatchDracoKdTreePoints(treeLvl, data, size, pos, ver, 3u, nPts, flatCoords)) return false;
            float step = (qRange > 0.0f) ? (qRange / static_cast<float>((1ULL << qb) - 1ULL)) : 0.0f;
            float halfRange = qRange * 0.5f;
            outPositions.resize(nPts);
            for (uint32_t i = 0; i < nPts; ++i) {
                outPositions[i] = {
                    static_cast<float>(flatCoords[i * 3 + 0]) * step - halfRange,
                    static_cast<float>(flatCoords[i * 3 + 1]) * step - halfRange,
                    static_cast<float>(flatCoords[i * 3 + 2]) * step - halfRange
                };
            }
            return true;
        }
        return false;
    }

    // ========================================================================
    // 2B. Connectivity Decode (Sequential Mesh/PointCloud or Edgebreaker Mesh)
    // ========================================================================
    uint64_t numFaces = 0, numPoints = 0;
    std::vector<uint32_t> seqIndices;
    DracoEdgebreakerState ebState;
    uint8_t numDecoders = 0;

    if (encMethod == 0u) {
        if (outEncType == 1u) {
            if (ver < DracoVer(2, 0)) {
                if (pos + 8 > size) return false;
                numFaces  = RdU32LE(data + pos);
                numPoints = RdU32LE(data + pos + 4);
                pos += 8;
            } else {
                uint32_t nf = 0, np = 0;
                if (!ReadLeb128U32(data, size, pos, nf) || !ReadLeb128U32(data, size, pos, np)) return false;
                numFaces = nf; numPoints = np;
            }
            if (numFaces == 0 || numFaces > 4000000ULL || numPoints < 3 || numPoints > 4000000ULL) return false;
            if (pos >= size) return false;
            uint8_t connMethod = data[pos++];
            size_t totalCorners = static_cast<size_t>(numFaces) * 3u;
            seqIndices.resize(totalCorners);

            if (connMethod == 1u) { // SEQUENTIAL_UNCOMPRESSED_INDICES
                if (numPoints < 256ULL) {
                    if (totalCorners > size - pos) return false;
                    for (size_t i = 0; i < totalCorners; ++i) seqIndices[i] = data[pos++];
                } else if (numPoints < 65536ULL) {
                    if (totalCorners * 2u > size - pos) return false;
                    for (size_t i = 0; i < totalCorners; ++i) { seqIndices[i] = RdU16LE(data + pos); pos += 2; }
                } else if (numPoints < (1ULL << 21) && ver >= DracoVer(2, 2)) {
                    for (size_t i = 0; i < totalCorners; ++i) {
                        uint32_t idx = 0;
                        if (!ReadLeb128U32(data, size, pos, idx) || idx >= numPoints) return false;
                        seqIndices[i] = idx;
                    }
                } else {
                    if (totalCorners * 4u > size - pos) return false;
                    for (size_t i = 0; i < totalCorners; ++i) { seqIndices[i] = RdU32LE(data + pos); pos += 4; }
                }
            } else if (connMethod == 0u) { // SEQUENTIAL_COMPRESSED_INDICES
                std::vector<uint32_t> symDeltas;
                if (!DracoDecodeSymbols(data, size, pos, ver, static_cast<uint32_t>(totalCorners), 1u, symDeltas)) return false;
                int64_t lastIdx = 0;
                for (size_t i = 0; i < totalCorners; ++i) {
                    int32_t d = DracoConvertSymbolToSignedInt(symDeltas[i]);
                    int64_t v = lastIdx + d;
                    if (v < 0 || static_cast<uint64_t>(v) >= numPoints) return false;
                    seqIndices[i] = static_cast<uint32_t>(v);
                    lastIdx = v;
                }
            } else {
                return false;
            }
        } else {
            if (pos + 4 > size) return false;
            int32_t np = RdI32LE(data + pos);
            pos += 4;
            if (np <= 0 || np > 4000000) return false;
            numPoints = static_cast<uint64_t>(np);
        }
        if (pos >= size) return false;
        numDecoders = data[pos++];
    } else {
        if (outEncType != 1u) return false;
        if (!DecodeDracoEdgebreakerFull(data, size, pos, ver, ebState)) return false;
        numFaces  = ebState.cornerTable.numFaces;
        numPoints = ebState.numPoints;
        if (pos >= size) return false;
        numDecoders = data[pos++];
    }

    if (numDecoders == 0 || numDecoders > 32) return false;

    // ========================================================================
    // 3. Attribute Decoder Headers
    // ========================================================================
    struct DecoderGroup {
        int8_t dataId = -1;          // -1 = pos_encoding_data_, >= 0 = attribute_data_[dataId]
        uint8_t decoderType = 0;     // 0 = MESH_VERTEX_ATTRIBUTE, 1 = MESH_CORNER_ATTRIBUTE
        uint8_t traversalMethod = 0; // 0 = DEPTH_FIRST, 1 = PREDICTION_DEGREE
        std::vector<DracoAttrDesc> attrs;
    };

    std::vector<DecoderGroup> groups(numDecoders);
    if (encMethod == 1u) {
        bool posUsed = false;
        std::vector<uint8_t> attDataUsed(ebState.attrTables.size(), 0u);
        for (uint8_t d = 0; d < numDecoders; ++d) {
            if (pos + 2 > size) return false;
            groups[d].dataId = static_cast<int8_t>(data[pos++]);
            groups[d].decoderType = data[pos++];
            if (ver >= DracoVer(1, 2)) {
                if (pos >= size) return false;
                groups[d].traversalMethod = data[pos++];
                if (groups[d].traversalMethod >= 2u) return false;
            }
            if (groups[d].dataId < 0) {
                if (groups[d].dataId != -1 || posUsed || groups[d].decoderType != 0u) return false;
                posUsed = true;
            } else {
                size_t ad = static_cast<size_t>(groups[d].dataId);
                if (ad >= ebState.attrTables.size() || attDataUsed[ad]) return false;
                attDataUsed[ad] = 1u;
                if (groups[d].decoderType == 1u && groups[d].traversalMethod != 0u) return false;
            }
        }
    }

    for (uint8_t d = 0; d < numDecoders; ++d) {
        uint32_t nAttrs = 0;
        if (!decodeAttrDescriptors(nAttrs, groups[d].attrs)) return false;
        if (nAttrs > size - pos) return false;
        for (uint32_t a = 0; a < nAttrs; ++a) {
            groups[d].attrs[a].seqType = data[pos++];
            if (groups[d].attrs[a].seqType > 3u) return false;
        }
    }

    // Identify which decoder group contains the requested POSITION attribute so we can stop once it is decoded
    int targetGroup = -1;
    for (uint8_t d = 0; d < numDecoders && targetGroup < 0; ++d) {
        for (const auto& attr : groups[d].attrs) {
            bool match = (targetPosUniqueId >= 0)
                ? (static_cast<int>(attr.uniqueId) == targetPosUniqueId)
                : (attr.attType == 0u);
            if (match) { targetGroup = static_cast<int>(d); break; }
        }
    }
    if (targetGroup < 0 && targetPosUniqueId >= 0) {
        // Fallback to attType == 0 if caller's uniqueId was absent
        for (uint8_t d = 0; d < numDecoders && targetGroup < 0; ++d) {
            for (const auto& attr : groups[d].attrs) {
                if (attr.attType == 0u) { targetGroup = static_cast<int>(d); break; }
            }
        }
    }
    if (targetGroup < 0) return false;

    // ========================================================================
    // 4. Decode Attribute Groups Up Through targetGroup
    // ========================================================================
    for (int d = 0; d <= targetGroup; ++d) {
        const auto& grp = groups[static_cast<size_t>(d)];
        std::vector<uint32_t> pointIds;
        std::vector<int32_t> dataToCorner;
        std::vector<int32_t> vertToData;
        const DracoAttrCornerTable* activeAttrTbl = nullptr;

        if (encMethod == 0u) {
            pointIds.resize(static_cast<size_t>(numPoints));
            for (size_t i = 0; i < pointIds.size(); ++i) pointIds[i] = static_cast<uint32_t>(i);
        } else {
            if (grp.decoderType == 0u) {
                if (!BuildDracoTraversalSequence(ebState.cornerTable, ebState.cornerToPoint, grp.traversalMethod,
                                                 pointIds, dataToCorner, vertToData)) {
                    return false;
                }
            } else {
                activeAttrTbl = &ebState.attrTables[static_cast<size_t>(grp.dataId)];
                if (!BuildDracoTraversalSequence(*activeAttrTbl, ebState.cornerToPoint, grp.traversalMethod,
                                                 pointIds, dataToCorner, vertToData)) {
                    return false;
                }
            }
        }

        const size_t nEntries = pointIds.size();
        const uint32_t maxCorners = (encMethod == 1u) ? static_cast<uint32_t>(ebState.cornerTable.NumCorners()) : 0u;

        struct DecodedPortAttr {
            bool isRaw = false;
            std::vector<Vec3f> rawPos;
            std::vector<int32_t> vals;
            float inlineMin[16] = {};
            float inlineRange = 0.0f;
            uint8_t inlineQBits = 0;
        };
        std::vector<DecodedPortAttr> portAttrs(grp.attrs.size());

        // 4a. DecodePortableAttributes for all attributes in group d
        for (size_t a = 0; a < grp.attrs.size(); ++a) {
            const auto& attr = grp.attrs[a];
            auto& pa = portAttrs[a];
            bool wantsPos = (d == targetGroup) && ((targetPosUniqueId >= 0)
                ? (static_cast<int>(attr.uniqueId) == targetPosUniqueId || attr.attType == 0u)
                : (attr.attType == 0u));

            if (attr.seqType == 0u) { // SEQUENTIAL_ATTRIBUTE_ENCODER_GENERIC
                size_t elemSz = dtElemBytes(attr.dataType);
                size_t rawBytes = nEntries * attr.numComps * elemSz;
                if (rawBytes > size - pos) return false;
                if (wantsPos && attr.numComps >= 3) {
                    pa.isRaw = true;
                    pa.rawPos.resize(nEntries);
                    for (size_t v = 0; v < nEntries; ++v) {
                        const uint8_t* vp = data + pos + v * attr.numComps * elemSz;
                        if (attr.dataType == 9u) {
                            pa.rawPos[v] = { RdF32LE(vp + 0), RdF32LE(vp + 4), RdF32LE(vp + 8) };
                        } else if (attr.dataType == 10u) {
                            pa.rawPos[v] = {
                                static_cast<float>(RdF64LE(vp + 0)),
                                static_cast<float>(RdF64LE(vp + 8)),
                                static_cast<float>(RdF64LE(vp + 16))
                            };
                        } else {
                            return false;
                        }
                    }
                }
                pos += rawBytes;
                continue;
            }

            // seqType 1 = INTEGER, 2 = QUANTIZATION, 3 = NORMALS (2 octahedral components!)
            const int valComps = (attr.seqType == 3u) ? 2 : static_cast<int>(attr.numComps);
            if (attr.seqType == 3u && (attr.numComps != 3u || attr.dataType != 9u)) return false;
            if (attr.seqType == 2u && attr.dataType != 9u) return false;

            if (pos >= size) return false;
            int8_t predScheme = static_cast<int8_t>(data[pos++]);
            if (predScheme < -2 || predScheme >= 7) return false;
            DracoPredTransform predTr;
            if (predScheme != -2) {
                if (pos >= size) return false;
                predTr.type = static_cast<int8_t>(data[pos++]);
                if (predTr.type < -1 || predTr.type >= 4) return false;
            }

            // Legacy < v2.0 decodes quantization / octahedral parameters BEFORE integer symbols!
            if (ver < DracoVer(2, 0)) {
                if (attr.seqType == 2u) {
                    size_t qSz = static_cast<size_t>(attr.numComps) * 4u + 5u;
                    if (qSz > size - pos) return false;
                    for (uint8_t c = 0; c < attr.numComps; ++c) pa.inlineMin[c] = RdF32LE(data + pos + c * 4u);
                    pa.inlineRange = RdF32LE(data + pos + attr.numComps * 4u);
                    pa.inlineQBits = data[pos + attr.numComps * 4u + 4u];
                    pos += qSz;
                    if (pa.inlineQBits < 1u || pa.inlineQBits > 30u || !std::isfinite(pa.inlineRange) || pa.inlineRange < 0.0f) return false;
                } else if (attr.seqType == 3u) {
                    if (pos >= size) return false;
                    pa.inlineQBits = data[pos++];
                    if (pa.inlineQBits < 1u || pa.inlineQBits > 30u) return false;
                }
            }

            if (pos >= size) return false;
            uint8_t compressed = data[pos++];
            const size_t totalScalars = nEntries * static_cast<size_t>(valComps);
            if (wantsPos) pa.vals.resize(totalScalars, 0);

            std::vector<uint32_t> syms;
            if (compressed > 0u) {
                if (!DracoDecodeSymbols(data, size, pos, ver, static_cast<uint32_t>(totalScalars), static_cast<uint32_t>(valComps), syms)) {
                    return false;
                }
                if (wantsPos) {
                    const bool keepUnsigned = (predScheme != -2 && predTr.AreCorrectionsPositive());
                    for (size_t k = 0; k < totalScalars; ++k) {
                        pa.vals[k] = keepUnsigned ? static_cast<int32_t>(syms[k]) : DracoConvertSymbolToSignedInt(syms[k]);
                    }
                }
            } else {
                if (pos >= size) return false;
                uint8_t numBytes = data[pos++];
                if (numBytes != 1u && numBytes != 2u && numBytes != 4u) return false;
                if (totalScalars * numBytes > size - pos) return false;
                if (wantsPos) {
                    const bool keepUnsigned = (predScheme != -2 && predTr.AreCorrectionsPositive());
                    for (size_t k = 0; k < totalScalars; ++k) {
                        uint32_t u = 0;
                        if (numBytes == 1u)      u = data[pos + k];
                        else if (numBytes == 2u) u = RdU16LE(data + pos + k * 2u);
                        else                     u = RdU32LE(data + pos + k * 4u);
                        pa.vals[k] = keepUnsigned ? static_cast<int32_t>(u) : DracoConvertSymbolToSignedInt(u);
                    }
                }
                pos += totalScalars * numBytes;
            }

            // DecodePredictionData (exact byte order per prediction scheme)
            std::vector<uint8_t> creaseFlags[4];
            if (predScheme != -2) {
                if (predScheme == 6) { // MESH_PREDICTION_GEOMETRIC_NORMAL: transform FIRST, then mode (<2.2), then flip rABS!
                    if (!predTr.DecodeTransformData(data, size, pos, ver)) return false;
                    if (ver < DracoVer(2, 2)) {
                        if (pos >= size) return false;
                        uint8_t pMode = data[pos++];
                        if (pMode > 1u) return false;
                    }
                    DracoRAbsDecoder flipDec;
                    if (!flipDec.StartDecoding(data, size, pos, ver)) return false;
                } else {
                    if (predScheme == 3) { // MESH_PREDICTION_TEX_COORDS_DEPRECATED
                        uint32_t numOri = 0;
                        if (!ReadDracoU32OrVarint(data, size, pos, ver, DracoVer(2, 2), numOri)) return false;
                        if (numOri == 0 || numOri > maxCorners) return false;
                        DracoRAbsDecoder oriDec;
                        if (!oriDec.StartDecoding(data, size, pos, ver)) return false;
                    } else if (predScheme == 4) { // MESH_PREDICTION_CONSTRAINED_MULTI_PARALLELOGRAM
                        if (ver < DracoVer(2, 2)) {
                            if (pos >= size || data[pos++] != 0u) return false;
                        }
                        for (int ctx = 0; ctx < 4; ++ctx) {
                            uint32_t nFlags = 0;
                            if (!ReadLeb128U32(data, size, pos, nFlags) || nFlags > maxCorners) return false;
                            if (nFlags > 0u) {
                                DracoRAbsDecoder crDec;
                                if (!crDec.StartDecoding(data, size, pos, ver)) return false;
                                if (wantsPos) {
                                    creaseFlags[ctx].resize(nFlags);
                                    for (uint32_t j = 0; j < nFlags; ++j) creaseFlags[ctx][j] = crDec.DecodeNextBit() ? 1u : 0u;
                                }
                            }
                        }
                    } else if (predScheme == 5) { // MESH_PREDICTION_TEX_COORDS_PORTABLE
                        if (pos + 4 > size) return false;
                        int32_t numOri = RdI32LE(data + pos);
                        pos += 4;
                        if (numOri < 0 || static_cast<size_t>(numOri) > totalScalars) return false;
                        DracoRAbsDecoder oriDec;
                        if (!oriDec.StartDecoding(data, size, pos, ver)) return false;
                    }
                    if (!predTr.DecodeTransformData(data, size, pos, ver)) return false;
                }

                // ComputeOriginalValues for POSITION
                if (wantsPos && nEntries > 0) {
                    if (predScheme == 0) { // PREDICTION_DIFFERENCE
                        std::vector<int32_t> corr = pa.vals;
                        std::vector<int32_t> zeroPred(static_cast<size_t>(valComps), 0);
                        predTr.ComputeOriginal(zeroPred.data(), corr.data(), pa.vals.data(), valComps);
                        for (size_t i = 1; i < nEntries; ++i) {
                            size_t off = i * static_cast<size_t>(valComps);
                            predTr.ComputeOriginal(pa.vals.data() + off - valComps, corr.data() + off, pa.vals.data() + off, valComps);
                        }
                    } else if ((predScheme == 1 || predScheme == 2 || predScheme == 4) && encMethod == 1u) {
                        bool okPred = (activeAttrTbl != nullptr)
                            ? ReconstructDracoMeshPrediction(predScheme, predTr, *activeAttrTbl, dataToCorner, vertToData, creaseFlags, valComps, pa.vals)
                            : ReconstructDracoMeshPrediction(predScheme, predTr, ebState.cornerTable, dataToCorner, vertToData, creaseFlags, valComps, pa.vals);
                        if (!okPred) return false;
                    } else {
                        return false;
                    }
                }
            }
        }

        // 4b. DecodeDataNeededByPortableTransforms (v2.0+) & Reconstruct Final Positions
        for (size_t a = 0; a < grp.attrs.size(); ++a) {
            const auto& attr = grp.attrs[a];
            auto& pa = portAttrs[a];
            bool wantsPos = (d == targetGroup) && ((targetPosUniqueId >= 0)
                ? (static_cast<int>(attr.uniqueId) == targetPosUniqueId || (outPositions.empty() && attr.attType == 0u))
                : (attr.attType == 0u));

            float minVals[16] = {};
            float maxRange = 0.0f;
            uint8_t qBits = 0;

            if (ver >= DracoVer(2, 0)) {
                if (attr.seqType == 2u) {
                    size_t qSz = static_cast<size_t>(attr.numComps) * 4u + 5u;
                    if (qSz > size - pos) return false;
                    for (uint8_t c = 0; c < attr.numComps; ++c) minVals[c] = RdF32LE(data + pos + c * 4u);
                    maxRange = RdF32LE(data + pos + attr.numComps * 4u);
                    qBits    = data[pos + attr.numComps * 4u + 4u];
                    pos += qSz;
                    if (qBits < 1u || qBits > 30u || !std::isfinite(maxRange) || maxRange < 0.0f) return false;
                } else if (attr.seqType == 3u) {
                    if (pos >= size) return false;
                    qBits = data[pos++];
                    if (qBits < 1u || qBits > 30u) return false;
                }
            } else {
                std::memcpy(minVals, pa.inlineMin, sizeof(minVals));
                maxRange = pa.inlineRange;
                qBits    = pa.inlineQBits;
            }

            if (!wantsPos || attr.numComps < 3u) continue;

            std::vector<Vec3f> entryPos;
            if (pa.isRaw) {
                entryPos = std::move(pa.rawPos);
            } else if (attr.seqType == 2u) {
                float step = (maxRange > 0.0f)
                    ? (maxRange / static_cast<float>((1ULL << qBits) - 1ULL))
                    : 0.0f;
                entryPos.resize(nEntries);
                for (size_t i = 0; i < nEntries; ++i) {
                    float x = minVals[0] + static_cast<float>(pa.vals[i * attr.numComps + 0]) * step;
                    float y = minVals[1] + static_cast<float>(pa.vals[i * attr.numComps + 1]) * step;
                    float z = minVals[2] + static_cast<float>(pa.vals[i * attr.numComps + 2]) * step;
                    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
                    entryPos[i] = { x, y, z };
                }
            } else if (attr.seqType == 1u) {
                float scale = 1.0f;
                if (attr.normalized != 0u) {
                    if (attr.dataType == 1u) scale = 1.0f / 127.0f;
                    else if (attr.dataType == 2u) scale = 1.0f / 255.0f;
                    else if (attr.dataType == 3u) scale = 1.0f / 32767.0f;
                    else if (attr.dataType == 4u) scale = 1.0f / 65535.0f;
                }
                entryPos.resize(nEntries);
                for (size_t i = 0; i < nEntries; ++i) {
                    entryPos[i] = {
                        static_cast<float>(pa.vals[i * attr.numComps + 0]) * scale,
                        static_cast<float>(pa.vals[i * attr.numComps + 1]) * scale,
                        static_cast<float>(pa.vals[i * attr.numComps + 2]) * scale
                    };
                }
            }
            if (entryPos.empty()) continue;

            // Map encoded attribute entries to final mesh/point-cloud points (UpdatePointToAttributeIndexMapping)
            if (encMethod == 0u) {
                outPositions = std::move(entryPos);
            } else {
                outPositions.assign(static_cast<size_t>(numPoints), { 0.0f, 0.0f, 0.0f });
                const int32_t nCorners = ebState.cornerTable.NumCorners();
                for (int32_t c = 0; c < nCorners; ++c) {
                    int32_t ptId = ebState.cornerToPoint[static_cast<size_t>(c)];
                    int32_t vId  = (activeAttrTbl != nullptr) ? activeAttrTbl->Vertex(c) : ebState.cornerTable.Vertex(c);
                    if (ptId < 0 || static_cast<size_t>(ptId) >= outPositions.size() ||
                        vId < 0 || static_cast<size_t>(vId) >= vertToData.size()) {
                        return false;
                    }
                    int32_t encIdx = vertToData[static_cast<size_t>(vId)];
                    if (encIdx < 0 || static_cast<size_t>(encIdx) >= entryPos.size()) return false;
                    outPositions[static_cast<size_t>(ptId)] = entryPos[static_cast<size_t>(encIdx)];
                }
            }
            if (targetPosUniqueId >= 0 && static_cast<int>(attr.uniqueId) == targetPosUniqueId) break;
        }
    }

    if (outPositions.empty()) return false;

    if (outEncType == 1u) {
        if (encMethod == 0u) {
            outTris.reserve(static_cast<size_t>(numFaces));
            for (size_t f = 0; f < static_cast<size_t>(numFaces); ++f) {
                uint32_t i0 = seqIndices[f * 3 + 0];
                uint32_t i1 = seqIndices[f * 3 + 1];
                uint32_t i2 = seqIndices[f * 3 + 2];
                if (i0 >= outPositions.size() || i1 >= outPositions.size() || i2 >= outPositions.size()) return false;
                if (i0 != i1 && i1 != i2 && i0 != i2) {
                    outTris.push_back({ i0, i1, i2 });
                }
            }
        } else {
            outTris.reserve(ebState.cornerTable.numFaces);
            for (uint32_t f = 0; f < ebState.cornerTable.numFaces; ++f) {
                int32_t p0 = ebState.cornerToPoint[static_cast<size_t>(f) * 3u + 0];
                int32_t p1 = ebState.cornerToPoint[static_cast<size_t>(f) * 3u + 1];
                int32_t p2 = ebState.cornerToPoint[static_cast<size_t>(f) * 3u + 2];
                if (p0 < 0 || p1 < 0 || p2 < 0) return false;
                uint32_t u0 = static_cast<uint32_t>(p0);
                uint32_t u1 = static_cast<uint32_t>(p1);
                uint32_t u2 = static_cast<uint32_t>(p2);
                if (u0 >= outPositions.size() || u1 >= outPositions.size() || u2 >= outPositions.size()) return false;
                if (u0 != u1 && u1 != u2 && u0 != u2) {
                    outTris.push_back({ u0, u1, u2 });
                }
            }
        }
        return !outTris.empty();
    }
    return true;
}

// Standalone .drc entry point
E2_DRACO_NOINLINE inline bool ParseGoogleDracoMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    std::vector<Vec3f> positions;
    std::vector<std::array<uint32_t, 3>> tris;
    uint8_t encType = 0, maj = 0, minV = 0;
    if (!DecodeDracoBufferToMesh(data, size, -1, positions, tris, encType, maj, minV)) return false;

    mesh.vertices.clear();
    mesh.tris.clear();
    bool isEb = (size >= 9 && data[8] == 1u);
    mesh.formatName = L"Google Draco v" + std::to_wstring(maj) + L"." + std::to_wstring(minV) +
                      (encType == 0 ? (isEb ? L" KD-Tree Point Cloud (.drc)" : L" Point Cloud (.drc)")
                                    : (isEb ? L" Edgebreaker 3D Mesh (.drc)" : L" 3D Mesh (.drc)"));

    if (encType == 1u) {
        mesh.vertices = std::move(positions);
        mesh.tris.reserve(tris.size());
        for (const auto& t : tris) mesh.tris.push_back({ t[0], t[1], t[2], {} });
    } else {
        Vec3f bmin = positions[0], bmax = positions[0];
        for (const auto& p : positions) {
            bmin.x = std::min(bmin.x, p.x); bmin.y = std::min(bmin.y, p.y); bmin.z = std::min(bmin.z, p.z);
            bmax.x = std::max(bmax.x, p.x); bmax.y = std::max(bmax.y, p.y); bmax.z = std::max(bmax.z, p.z);
        }
        float r = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z, 1.0f }) * 0.02f;
        for (const auto& pt : positions) {
            uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back({ pt.x, pt.y + r, pt.z });
            mesh.vertices.push_back({ pt.x - r, pt.y - r, pt.z - r });
            mesh.vertices.push_back({ pt.x + r, pt.y - r, pt.z - r });
            mesh.vertices.push_back({ pt.x, pt.y - r, pt.z + r });
            mesh.tris.push_back({ b + 0, b + 1, b + 2, {} });
            mesh.tris.push_back({ b + 0, b + 2, b + 3, {} });
            mesh.tris.push_back({ b + 0, b + 3, b + 1, {} });
            mesh.tris.push_back({ b + 1, b + 3, b + 2, {} });
        }
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
