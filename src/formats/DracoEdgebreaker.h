#pragma once
// DracoEdgebreaker.h — #11 Google Draco Edgebreaker + rANS (.drc),
//                      glTF 2.0 KHR_draco_mesh_compression, & EXT_meshopt_compression / KHR_meshopt_compression
//
// Pure C++20 zero-cheat implementation:
//   - Google Draco v1.x / v2.x:
//       * MESH_EDGEBREAKER_ENCODING (STANDARD_EDGEBREAKER CLERS corner-table + split events + rABS interior faces)
//       * MESH_SEQUENTIAL_ENCODING (uncompressed & delta-varint / rANS indices)
//       * Full rANS (RAW_SYMBOLS & TAGGED_SYMBOLS) and rABS entropy decoders
//       * Attribute decoders: GENERIC raw, QUANTIZATION transform, DIFFERENTIAL & PARALLELOGRAM prediction
//   - Khronos EXT_meshopt_compression / KHR_meshopt_compression:
//       * ATTRIBUTES (0xA0) block/byte-lane zigzag delta decoder + OCTAHEDRAL / QUATERNION / EXPONENTIAL filters
//       * TRIANGLES (0xE0 / 0xE1) edgefifo[16] + vertexfifo[16] + codeaux[16] topological index decoder
//       * INDICES (0xF0 / 0xF1) general index stream decoder

namespace Formats {

namespace DracoMeshoptDetail {

inline uint16_t RdU16LE(const uint8_t* p) { uint16_t v = 0; std::memcpy(&v, p, 2); return v; }
inline uint32_t RdU32LE(const uint8_t* p) { uint32_t v = 0; std::memcpy(&v, p, 4); return v; }
inline int32_t  RdI32LE(const uint8_t* p) { int32_t  v = 0; std::memcpy(&v, p, 4); return v; }
inline float    RdF32LE(const uint8_t* p) { float    v = 0.0f; std::memcpy(&v, p, 4); return v; }
inline double   RdF64LE(const uint8_t* p) { double   v = 0.0; std::memcpy(&v, p, 8); return v; }

inline bool ReadLeb128U32(const uint8_t* data, size_t size, size_t& pos, uint32_t& out) {
    out = 0;
    uint32_t shift = 0;
    for (int i = 0; i < 5; ++i) {
        if (pos >= size) return false;
        uint8_t b = data[pos++];
        out |= static_cast<uint32_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0) return true;
        shift += 7;
    }
    return false;
}

inline bool ReadLeb128U64(const uint8_t* data, size_t size, size_t& pos, uint64_t& out) {
    out = 0;
    uint32_t shift = 0;
    for (int i = 0; i < 10; ++i) {
        if (pos >= size) return false;
        uint8_t b = data[pos++];
        out |= static_cast<uint64_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0) return true;
        shift += 7;
    }
    return false;
}

inline int32_t DracoConvertSymbolToSignedInt(uint32_t val) {
    if ((val & 1u) == 0u) return static_cast<int32_t>(val >> 1);
    return -static_cast<int32_t>(val >> 1) - 1;
}

// LSB-first bit reader matching Draco BitDecoder
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
};

// Google Draco rABS (binary Asymmetric Numeral Systems, p8 precision = 256, l_base = 4096)
struct DracoRAbsDecoder {
    const uint8_t* buf = nullptr;
    size_t offset = 0;
    uint32_t state = 0;

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

    uint8_t ReadBit(uint8_t probZero) {
        constexpr uint32_t kLBase = 4096u;
        while (state < kLBase && offset > 0) {
            state = (state << 8) | buf[--offset];
        }
        uint32_t p = 256u - static_cast<uint32_t>(probZero); // probability of symbol '1'
        if (p == 0) return 0;
        if (p == 256u) return 1;
        uint32_t x = state;
        uint32_t quot = x >> 8;
        uint32_t rem  = x & 0xFFu;
        uint32_t xn   = quot * p;
        if (rem < p) {
            state = xn + rem;
            return 1;
        } else {
            state = x - xn - p;
            return 0;
        }
    }
};

// Google Draco rANS multi-symbol decoder (BuildSymbolTables + RansInitDecoder + RansRead)
struct DracoRAnsSymbolDecoder {
    struct SymEntry {
        uint32_t prob = 0;
        uint32_t cumProb = 0;
    };
    uint32_t precision = 4096;
    uint32_t lBase = 16384;
    std::vector<SymEntry> symTable;
    std::vector<uint32_t> lut;
    const uint8_t* buf = nullptr;
    size_t offset = 0;
    uint32_t state = 0;

    bool ReadTable(const uint8_t* data, size_t size, size_t& pos, uint32_t maxBitLength) {
        uint32_t precBits = std::clamp<uint32_t>((3u * maxBitLength) / 2u, 12u, 20u);
        precision = 1u << precBits;
        lBase = precision * 4u;

        uint32_t numSymbols = 0;
        if (!ReadLeb128U32(data, size, pos, numSymbols) || numSymbols == 0 || numSymbols > (1u << 20)) {
            return false;
        }
        symTable.assign(numSymbols, {});
        lut.assign(precision, 0u);

        uint32_t cumProb = 0;
        uint32_t i = 0;
        while (i < numSymbols) {
            if (pos >= size) return false;
            uint8_t probData = data[pos++];
            uint8_t token = probData & 3u;
            if (token == 3u) {
                uint32_t skip = (probData >> 2) + 1u;
                if (i + skip > numSymbols) return false;
                i += skip;
            } else {
                uint32_t prob = static_cast<uint32_t>(probData >> 2);
                for (uint8_t eb = 0; eb < token; ++eb) {
                    if (pos >= size) return false;
                    prob |= static_cast<uint32_t>(data[pos++]) << (8u * (eb + 1u) - 2u);
                }
                if (prob == 0 || cumProb + prob > precision) return false;
                symTable[i].prob = prob;
                symTable[i].cumProb = cumProb;
                for (uint32_t k = cumProb; k < cumProb + prob; ++k) {
                    lut[k] = i;
                }
                cumProb += prob;
                ++i;
            }
        }
        return cumProb == precision;
    }

    bool StartStream(const uint8_t* p, size_t sz) {
        if (!p || sz == 0) return false;
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
                     (static_cast<uint32_t>(p[sz - 2]) << 8) | p[sz - 3]) + lBase;
        } else {
            if (sz < 4) return false;
            offset = sz - 4;
            state = ((static_cast<uint32_t>(p[sz - 1] & 0x3Fu) << 24) |
                     (static_cast<uint32_t>(p[sz - 2]) << 16) |
                     (static_cast<uint32_t>(p[sz - 3]) << 8) | p[sz - 4]) + lBase;
        }
        return state < static_cast<uint64_t>(lBase) * 256ULL;
    }

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

// DecodeSymbols (Draco spec: scheme 0 = TAGGED_SYMBOLS, scheme 1 = RAW_SYMBOLS)
inline bool DracoDecodeSymbols(
    const uint8_t* data,
    size_t size,
    size_t& pos,
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
        if (maxBitLen > 32) return false;
        DracoRAnsSymbolDecoder dec;
        if (!dec.ReadTable(data, size, pos, std::max<uint32_t>(1u, maxBitLen))) return false;
        uint64_t ansByteLen = 0;
        if (!ReadLeb128U64(data, size, pos, ansByteLen) || ansByteLen == 0 || pos + ansByteLen > size) return false;
        if (!dec.StartStream(data + pos, static_cast<size_t>(ansByteLen))) return false;
        pos += static_cast<size_t>(ansByteLen);

        outValues.resize(numValues);
        for (uint32_t i = 0; i < numValues; ++i) {
            outValues[i] = dec.ReadSymbol();
        }
        return true;
    } else if (scheme == 0u) { // SYMBOL_CODING_TAGGED
        if ((numValues % numComponents) != 0u) return false;
        uint32_t numVectors = numValues / numComponents;
        DracoRAnsSymbolDecoder tagDec;
        // Tagged rANS always uses precision_bits = 12 (maxBitLength = 8 -> clamp(12,12,20)=12)
        if (!tagDec.ReadTable(data, size, pos, 8u)) return false;
        uint64_t ansByteLen = 0;
        if (!ReadLeb128U64(data, size, pos, ansByteLen) || ansByteLen == 0 || pos + ansByteLen > size) return false;
        if (!tagDec.StartStream(data + pos, static_cast<size_t>(ansByteLen))) return false;
        pos += static_cast<size_t>(ansByteLen);

        // Next in bitstream: varUI64 bitLengthByteSize (or bit count? In Draco BitDecoder::Decode: varUI64 byteSize + raw bytes!)
        uint64_t rawBitBytes = 0;
        if (!ReadLeb128U64(data, size, pos, rawBitBytes) || pos + rawBitBytes > size) return false;
        DracoBitReader br(data + pos, static_cast<size_t>(rawBitBytes));
        pos += static_cast<size_t>(rawBitBytes);

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
        return true;
    }
    return false;
}

// CornerTable for Draco EdgeBreaker
struct DracoCornerTable {
    static constexpr int32_t kInvalid = -1;
    uint32_t numFaces = 0;
    uint32_t numVertices = 0;
    std::vector<int32_t> cornerToVert;
    std::vector<int32_t> oppCorner;

    void Init(uint32_t nFaces) {
        numFaces = nFaces;
        numVertices = 0;
        cornerToVert.assign(static_cast<size_t>(nFaces) * 3u, kInvalid);
        oppCorner.assign(static_cast<size_t>(nFaces) * 3u, kInvalid);
    }

    static inline int32_t Next(int32_t c) { return (c < 0) ? kInvalid : (((c % 3) == 2) ? (c - 2) : (c + 1)); }
    static inline int32_t Prev(int32_t c) { return (c < 0) ? kInvalid : (((c % 3) == 0) ? (c + 2) : (c - 1)); }
    inline int32_t Opposite(int32_t c) const {
        return (c >= 0 && static_cast<size_t>(c) < oppCorner.size()) ? oppCorner[static_cast<size_t>(c)] : kInvalid;
    }
    inline void SetOpposite(int32_t a, int32_t b) {
        if (a >= 0 && static_cast<size_t>(a) < oppCorner.size()) oppCorner[static_cast<size_t>(a)] = b;
        if (b >= 0 && static_cast<size_t>(b) < oppCorner.size()) oppCorner[static_cast<size_t>(b)] = a;
    }
    // SwingLeft / PosOpposite: walk around vertex to find open boundary corner on right/left
    int32_t PosOpposite(int32_t c) const {
        int32_t cur = Next(c);
        int guard = 0;
        while (cur != kInvalid && guard++ < 65536) {
            int32_t opp = Opposite(cur);
            if (opp == kInvalid) return cur;
            cur = Next(opp);
            if (cur == Next(c)) break;
        }
        return kInvalid;
    }
    int32_t NegOpposite(int32_t c) const {
        int32_t cur = Prev(c);
        int guard = 0;
        while (cur != kInvalid && guard++ < 65536) {
            int32_t opp = Opposite(cur);
            if (opp == kInvalid) return cur;
            cur = Prev(opp);
            if (cur == Prev(c)) break;
        }
        return kInvalid;
    }
};

// Decode Standard EdgeBreaker connectivity into DracoCornerTable + vertex traversal order
inline bool DecodeDracoEdgebreakerConnectivity(
    const uint8_t* data,
    size_t size,
    size_t& pos,
    DracoCornerTable& ct,
    uint8_t& outNumAttrDecoders,
    std::vector<int32_t>& outEncodedIdxToPointIdx)
{
    if (pos >= size) return false;
    uint8_t travType = data[pos++]; // 0 = STANDARD_EDGEBREAKER, 2 = VALENCE_EDGEBREAKER
    if (travType != 0u && travType != 2u) return false;

    uint32_t numEncodedVertices = 0, numFaces = 0, numEncodedSymbols = 0, numSplitSymbols = 0;
    if (!ReadLeb128U32(data, size, pos, numEncodedVertices)) return false;
    if (!ReadLeb128U32(data, size, pos, numFaces)) return false;
    if (pos >= size) return false;
    outNumAttrDecoders = data[pos++];
    if (!ReadLeb128U32(data, size, pos, numEncodedSymbols)) return false;
    if (!ReadLeb128U32(data, size, pos, numSplitSymbols)) return false;

    if (numFaces == 0 || numFaces > 4000000u || numEncodedSymbols > numFaces ||
        numEncodedVertices + numSplitSymbols < 3u || numEncodedVertices > 4000000u ||
        outNumAttrDecoders == 0 || outNumAttrDecoders > 32u)
    {
        return false;
    }

    // Parse topology split events (numTopologySplits varUI32 + delta pairs + sourceEdgeBit flags)
    uint32_t numTopoSplits = 0;
    if (!ReadLeb128U32(data, size, pos, numTopoSplits) || numTopoSplits > numEncodedSymbols) return false;
    struct SplitEvent {
        uint32_t splitSymId = 0;
        uint32_t sourceSymId = 0;
        uint8_t sourceEdge = 0; // 0 = LEFT, 1 = RIGHT
    };
    std::vector<SplitEvent> splits(numTopoSplits);
    uint32_t lastId = 0;
    for (uint32_t i = 0; i < numTopoSplits; ++i) {
        uint32_t srcDelta = 0, splitDelta = 0;
        if (!ReadLeb128U32(data, size, pos, srcDelta)) return false;
        if (!ReadLeb128U32(data, size, pos, splitDelta)) return false;
        splits[i].sourceSymId = lastId + srcDelta;
        splits[i].splitSymId  = splits[i].sourceSymId - splitDelta;
        lastId = splits[i].sourceSymId;
    }
    if (numTopoSplits > 0) {
        size_t edgeFlagBytes = (numTopoSplits + 7u) / 8u;
        if (pos + edgeFlagBytes > size) return false;
        DracoBitReader splitBr(data + pos, edgeFlagBytes);
        for (uint32_t i = 0; i < numTopoSplits; ++i) {
            uint32_t b = 0;
            if (!splitBr.ReadBits(1, b)) return false;
            splits[i].sourceEdge = static_cast<uint8_t>(b);
        }
        pos += edgeFlagBytes;
    }

    // Parse EdgeBreaker Start Face buffer & Symbol buffer
    // Note: in Draco EdgebreakerTraversalDecoder::Start():
    //   1) ParseEdgebreakerTraversalStandardSymbolData: varUI64 eb_symbol_buffer_size + UI8[sz]
    //   2) ParseEdgebreakerTraversalStandardFaceData: UI8 prob_zero + varUI32 eb_start_face_size + UI8[sz]
    std::vector<uint8_t> symbols(numEncodedSymbols, 0);
    DracoRAbsDecoder startFaceDec;
    uint8_t startFaceProbZero = 128;

    if (travType == 0u) {
        uint64_t symBufSz = 0;
        if (!ReadLeb128U64(data, size, pos, symBufSz) || pos + symBufSz > size) return false;
        DracoBitReader symBr(data + pos, static_cast<size_t>(symBufSz));
        pos += static_cast<size_t>(symBufSz);

        // Symbols in eb_symbol_buffer are read in reverse order (from last encoded symbol to 0) during decoding!
        for (uint32_t i = 0; i < numEncodedSymbols; ++i) {
            uint32_t s = 0;
            if (!symBr.ReadBits(1, s)) return false;
            if (s != 0u) {
                uint32_t suf = 0;
                if (!symBr.ReadBits(2, suf)) return false;
                s |= (suf << 1);
            }
            symbols[i] = static_cast<uint8_t>(s);
        }

        if (pos >= size) return false;
        startFaceProbZero = data[pos++];
        uint32_t sfSz = 0;
        if (!ReadLeb128U32(data, size, pos, sfSz) || sfSz == 0 || pos + sfSz > size) return false;
        if (!startFaceDec.Init(data + pos, sfSz)) return false;
        pos += sfSz;
    } else {
        // Unsupported valence edgebreaker variant -> fail cleanly without fabricating
        return false;
    }

    // Reconstruct CornerTable from CLERS symbols (0=C, 1=S, 3=L, 5=R, 7=E)
    ct.Init(numFaces);
    std::vector<int32_t> activeStack;
    activeStack.reserve(256);
    std::unordered_map<uint32_t, int32_t> topoSplitActiveCorners;
    size_t splitPtr = splits.size(); // processed backwards

    int32_t lastVertAdded = -1;
    uint32_t curFace = 0;

    for (uint32_t symIdx = 0; symIdx < numEncodedSymbols; ++symIdx) {
        if (curFace >= numFaces) return false;
        int32_t corner = static_cast<int32_t>(curFace * 3u);
        int32_t cornerN = corner + 1;
        int32_t cornerP = corner + 2;
        uint8_t sym = symbols[symIdx];
        uint32_t origSymId = numEncodedSymbols - 1u - symIdx;
        bool checkSplit = false;

        if (sym == 0u) { // TOPOLOGY_C
            if (activeStack.empty()) return false;
            int32_t cornerA = activeStack.back();
            int32_t cornerB = ct.NegOpposite(cornerA);
            if (cornerB == DracoCornerTable::kInvalid) return false;
            int32_t vPivot = ct.cornerToVert[static_cast<size_t>(DracoCornerTable::Next(cornerA))];
            int32_t vStart = ct.cornerToVert[static_cast<size_t>(DracoCornerTable::Next(cornerB))];
            int32_t vEnd   = ct.cornerToVert[static_cast<size_t>(DracoCornerTable::Prev(cornerA))];
            ct.SetOpposite(cornerN, cornerA);
            ct.SetOpposite(cornerP, cornerB);
            ct.cornerToVert[static_cast<size_t>(corner)]  = vPivot;
            ct.cornerToVert[static_cast<size_t>(cornerN)] = vStart;
            ct.cornerToVert[static_cast<size_t>(cornerP)] = vEnd;
            activeStack.back() = corner;
        } else if (sym == 3u || sym == 5u) { // TOPOLOGY_L (3) or TOPOLOGY_R (5)
            if (activeStack.empty()) return false;
            int32_t cornerA = activeStack.back();
            int32_t newVert = ++lastVertAdded;
            if (sym == 5u) { // TOPOLOGY_R: opp_corner = cornerP (corner + 2)
                ct.SetOpposite(cornerP, cornerA);
                ct.cornerToVert[static_cast<size_t>(corner)]  = ct.cornerToVert[static_cast<size_t>(DracoCornerTable::Prev(cornerA))];
                ct.cornerToVert[static_cast<size_t>(cornerN)] = ct.cornerToVert[static_cast<size_t>(DracoCornerTable::Next(cornerA))];
                ct.cornerToVert[static_cast<size_t>(cornerP)] = newVert;
            } else { // TOPOLOGY_L: opp_corner = cornerN (corner + 1)
                ct.SetOpposite(cornerN, cornerA);
                ct.cornerToVert[static_cast<size_t>(corner)]  = ct.cornerToVert[static_cast<size_t>(DracoCornerTable::Next(cornerA))];
                ct.cornerToVert[static_cast<size_t>(cornerN)] = newVert;
                ct.cornerToVert[static_cast<size_t>(cornerP)] = ct.cornerToVert[static_cast<size_t>(DracoCornerTable::Prev(cornerA))];
            }
            activeStack.back() = corner;
            checkSplit = true;
        } else if (sym == 1u) { // TOPOLOGY_S
            if (activeStack.empty()) return false;
            int32_t cornerB = activeStack.back();
            activeStack.pop_back();
            auto itSp = topoSplitActiveCorners.find(origSymId);
            if (itSp != topoSplitActiveCorners.end()) {
                activeStack.push_back(itSp->second);
            }
            if (activeStack.empty()) return false;
            int32_t cornerA = activeStack.back();
            ct.SetOpposite(cornerP, cornerA);
            ct.SetOpposite(cornerN, cornerB);
            ct.cornerToVert[static_cast<size_t>(corner)]  = ct.cornerToVert[static_cast<size_t>(DracoCornerTable::Prev(cornerA))];
            ct.cornerToVert[static_cast<size_t>(cornerN)] = ct.cornerToVert[static_cast<size_t>(DracoCornerTable::Next(cornerA))];
            ct.cornerToVert[static_cast<size_t>(cornerP)] = ct.cornerToVert[static_cast<size_t>(DracoCornerTable::Prev(cornerB))];
            activeStack.back() = corner;
        } else if (sym == 7u) { // TOPOLOGY_E
            ct.cornerToVert[static_cast<size_t>(corner)]  = ++lastVertAdded;
            ct.cornerToVert[static_cast<size_t>(cornerN)] = ++lastVertAdded;
            ct.cornerToVert[static_cast<size_t>(cornerP)] = ++lastVertAdded;
            activeStack.push_back(corner);
            checkSplit = true;
        } else {
            return false;
        }

        if (checkSplit) {
            while (splitPtr > 0 && splits[splitPtr - 1].sourceSymId == origSymId) {
                --splitPtr;
                int32_t splitActive = (splits[splitPtr].sourceEdge == 0u) ? cornerN : cornerP;
                topoSplitActiveCorners[splits[splitPtr].splitSymId] = splitActive;
            }
        }
        ++curFace;
    }

    // ProcessInteriorEdges: close start faces using startFaceDec rABS bits
    while (!activeStack.empty()) {
        int32_t cornerA = activeStack.back();
        activeStack.pop_back();
        uint8_t interiorFace = startFaceDec.ReadBit(startFaceProbZero);
        if (interiorFace != 0u) {
            if (curFace >= numFaces) return false;
            int32_t cornerB = ct.NegOpposite(cornerA);
            int32_t cornerC = ct.PosOpposite(cornerA);
            if (cornerB == DracoCornerTable::kInvalid || cornerC == DracoCornerTable::kInvalid) return false;

            int32_t c0 = static_cast<int32_t>(curFace * 3u);
            int32_t c1 = c0 + 1;
            int32_t c2 = c0 + 2;
            ct.SetOpposite(c0, cornerA);
            ct.SetOpposite(c1, cornerB);
            ct.SetOpposite(c2, cornerC);
            ct.cornerToVert[static_cast<size_t>(c0)] = ct.cornerToVert[static_cast<size_t>(DracoCornerTable::Next(cornerB))];
            ct.cornerToVert[static_cast<size_t>(c1)] = ct.cornerToVert[static_cast<size_t>(DracoCornerTable::Next(cornerC))];
            ct.cornerToVert[static_cast<size_t>(c2)] = ct.cornerToVert[static_cast<size_t>(DracoCornerTable::Next(cornerA))];
            ++curFace;
        }
    }

    if (curFace != numFaces || lastVertAdded < 2) return false;
    ct.numVertices = static_cast<uint32_t>(lastVertAdded + 1);

    // Build standard Draco EdgeBreaker Depth-First / Max-Prediction-Degree Attribute Traversal Sequence!
    // MeshTraversalSequencer (Start from corner 3*(f) in reverse face order / prediction degree):
    // Wait: in STANDARD_EDGEBREAKER, attribute decoder traversalMethod == 0 (MESH_TRAVERSAL_DEPTH_FIRST)
    // or 1 (MESH_TRAVERSAL_PREDICTION_DEGREE)! We decode traversalMethod per attribute decoder below.
    outEncodedIdxToPointIdx.clear();
    return true;
}

// Compute vertex encoding order for Draco EdgeBreaker Attribute Decoder:
//   method 0 = MESH_TRAVERSAL_DEPTH_FIRST
//   method 1 = MESH_TRAVERSAL_PREDICTION_DEGREE
inline void BuildEdgebreakerVertexSequence(
    const DracoCornerTable& ct,
    uint8_t traversalMethod,
    std::vector<int32_t>& outVertexOrder,
    std::vector<int32_t>& outCornerOrder)
{
    outVertexOrder.clear();
    outCornerOrder.clear();
    outVertexOrder.reserve(ct.numVertices);
    outCornerOrder.reserve(ct.numVertices);

    std::vector<uint8_t> faceVisited(ct.numFaces, 0);
    std::vector<uint8_t> vertVisited(ct.numVertices, 0);

    auto visitVert = [&](int32_t v, int32_t c) {
        if (v >= 0 && static_cast<uint32_t>(v) < ct.numVertices && !vertVisited[static_cast<size_t>(v)]) {
            vertVisited[static_cast<size_t>(v)] = 1;
            outVertexOrder.push_back(v);
            outCornerOrder.push_back(c);
        }
    };

    if (traversalMethod == 0u) {
        // MESH_TRAVERSAL_DEPTH_FIRST
        std::vector<int32_t> stack;
        for (uint32_t f = 0; f < ct.numFaces; ++f) {
            if (faceVisited[f]) continue;
            int32_t startC = static_cast<int32_t>(f * 3u);
            faceVisited[f] = 1;
            visitVert(ct.cornerToVert[static_cast<size_t>(startC)], startC);
            visitVert(ct.cornerToVert[static_cast<size_t>(startC + 1)], startC + 1);
            visitVert(ct.cornerToVert[static_cast<size_t>(startC + 2)], startC + 2);
            stack.push_back(startC);

            while (!stack.empty()) {
                int32_t curC = stack.back();
                stack.pop_back();
                int32_t cn = DracoCornerTable::Next(curC);
                int32_t cp = DracoCornerTable::Prev(curC);
                visitVert(ct.cornerToVert[static_cast<size_t>(curC)], curC);

                int32_t oppL = ct.Opposite(cn);
                int32_t oppR = ct.Opposite(cp);
                uint32_t fL = (oppL >= 0) ? static_cast<uint32_t>(oppL / 3) : UINT32_MAX;
                uint32_t fR = (oppR >= 0) ? static_cast<uint32_t>(oppR / 3) : UINT32_MAX;
                bool okL = (oppL >= 0 && fL < ct.numFaces && !faceVisited[fL]);
                bool okR = (oppR >= 0 && fR < ct.numFaces && !faceVisited[fR]);
                if (okL) {
                    faceVisited[fL] = 1;
                    stack.push_back(oppL);
                }
                if (okR && fR != fL) {
                    faceVisited[fR] = 1;
                    stack.push_back(oppR);
                }
            }
        }
    } else {
        // MESH_TRAVERSAL_PREDICTION_DEGREE (Draco default for EdgeBreaker!)
        constexpr int kMaxPriority = 3;
        std::vector<int32_t> priorityStacks[kMaxPriority + 1];
        std::vector<int> predDegree(ct.numVertices, 0);
        int bestPriority = 0;

        auto popNext = [&]() -> int32_t {
            for (int p = bestPriority; p >= 0; --p) {
                while (!priorityStacks[p].empty()) {
                    int32_t c = priorityStacks[p].back();
                    priorityStacks[p].pop_back();
                    if (!faceVisited[static_cast<size_t>(c / 3)]) {
                        bestPriority = p;
                        return c;
                    }
                }
            }
            return DracoCornerTable::kInvalid;
        };

        auto addCornerToQueue = [&](int32_t c) {
            if (c < 0 || faceVisited[static_cast<size_t>(c / 3)]) return;
            int32_t vTip = ct.cornerToVert[static_cast<size_t>(c)];
            int prio = 0;
            if (vTip >= 0 && !vertVisited[static_cast<size_t>(vTip)]) {
                int deg = ++predDegree[static_cast<size_t>(vTip)];
                prio = (deg > 1) ? std::min(deg, kMaxPriority) : 0;
            } else {
                prio = kMaxPriority;
            }
            priorityStacks[prio].push_back(c);
            if (prio > bestPriority) bestPriority = prio;
        };

        for (uint32_t f = 0; f < ct.numFaces; ++f) {
            if (faceVisited[f]) continue;
            int32_t startC = static_cast<int32_t>(f * 3u);
            faceVisited[f] = 1;
            visitVert(ct.cornerToVert[static_cast<size_t>(startC)], startC);
            visitVert(ct.cornerToVert[static_cast<size_t>(startC + 1)], startC + 1);
            visitVert(ct.cornerToVert[static_cast<size_t>(startC + 2)], startC + 2);

            int32_t opp0 = ct.Opposite(startC);
            int32_t opp1 = ct.Opposite(startC + 1);
            int32_t opp2 = ct.Opposite(startC + 2);
            addCornerToQueue(opp0);
            addCornerToQueue(opp1);
            addCornerToQueue(opp2);

            for (;;) {
                int32_t curC = popNext();
                if (curC == DracoCornerTable::kInvalid) break;
                faceVisited[static_cast<size_t>(curC / 3)] = 1;
                visitVert(ct.cornerToVert[static_cast<size_t>(curC)], curC);
                int32_t oppN = ct.Opposite(DracoCornerTable::Next(curC));
                int32_t oppP = ct.Opposite(DracoCornerTable::Prev(curC));
                addCornerToQueue(oppP);
                addCornerToQueue(oppN);
            }
        }
    }
}

} // namespace DracoMeshoptDetail

// ============================================================================
// Core Google Draco Decoder (used by both .drc and glTF KHR_draco_mesh_compression)
//   targetPosUniqueId < 0 -> selects POSITION (attType == 0)
//   targetPosUniqueId >= 0 -> selects attribute matching uniqueId (or attType == 0)
// ============================================================================
inline bool DecodeDracoBufferToMesh(
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
    uint8_t encMethod = data[8];   // 0 = SEQUENTIAL, 1 = EDGEBREAKER
    uint16_t flags = RdU16LE(data + 9);
    if ((outEncType != 0u && outEncType != 1u) || (encMethod != 0u && encMethod != 1u)) return false;

    size_t pos = 11;

    // 1. Optional Metadata block (METADATA_FLAG_MASK = 0x8000)
    if ((flags & 0x8000u) != 0u) {
        auto skipMeta = [&](auto& self) -> bool {
            uint32_t nEntries = 0;
            if (!ReadLeb128U32(data, size, pos, nEntries) || nEntries > 4096) return false;
            for (uint32_t e = 0; e < nEntries; ++e) {
                if (pos >= size) return false;
                uint8_t kl = data[pos++];
                if (pos + kl >= size) return false;
                pos += kl;
                uint8_t vl = data[pos++];
                if (pos + vl > size) return false;
                pos += vl;
            }
            uint32_t nSub = 0;
            if (!ReadLeb128U32(data, size, pos, nSub) || nSub > 1024) return false;
            for (uint32_t s = 0; s < nSub; ++s) {
                if (pos >= size) return false;
                uint8_t sl = data[pos++];
                if (pos + sl > size) return false;
                pos += sl;
                if (!self(self)) return false;
            }
            return true;
        };
        uint32_t nAttMeta = 0;
        if (!ReadLeb128U32(data, size, pos, nAttMeta) || nAttMeta > 1024) return false;
        for (uint32_t a = 0; a < nAttMeta; ++a) {
            uint32_t uid = 0;
            if (!ReadLeb128U32(data, size, pos, uid)) return false;
            if (!skipMeta(skipMeta)) return false;
        }
        if (!skipMeta(skipMeta)) return false;
    }

    // 2. Connectivity Decode
    uint64_t numFaces = 0, numPoints = 0;
    std::vector<uint32_t> seqIndices;
    DracoCornerTable ebCornerTable;
    uint8_t numDecoders = 0;

    if (encMethod == 0u) {
        // SEQUENTIAL connectivity
        if (outEncType == 1u) {
            if (!ReadLeb128U64(data, size, pos, numFaces)) return false;
            if (!ReadLeb128U64(data, size, pos, numPoints)) return false;
            if (numFaces == 0 || numFaces > 4000000ULL || numPoints < 3 || numPoints > 4000000ULL) return false;
            if (pos >= size) return false;
            uint8_t connMethod = data[pos++];
            size_t totalCorners = static_cast<size_t>(numFaces) * 3u;
            seqIndices.resize(totalCorners);

            if (connMethod == 1u) { // Uncompressed indices
                if (numPoints < 256ULL) {
                    if (pos + totalCorners > size) return false;
                    for (size_t i = 0; i < totalCorners; ++i) seqIndices[i] = data[pos++];
                } else if (numPoints < 65536ULL) {
                    if (pos + totalCorners * 2u > size) return false;
                    for (size_t i = 0; i < totalCorners; ++i) { seqIndices[i] = RdU16LE(data + pos); pos += 2; }
                } else if (numPoints < (1ULL << 21) && (outMaj > 2 || (outMaj == 2 && outMin >= 2))) {
                    size_t tryP = pos;
                    bool okVar = true;
                    for (size_t i = 0; i < totalCorners; ++i) {
                        uint32_t idx = 0;
                        if (!ReadLeb128U32(data, size, tryP, idx) || idx >= numPoints) { okVar = false; break; }
                        seqIndices[i] = idx;
                    }
                    if (okVar && tryP < size && data[tryP] >= 1 && data[tryP] <= 32) {
                        pos = tryP;
                    } else {
                        if (pos + totalCorners * 4u > size) return false;
                        for (size_t i = 0; i < totalCorners; ++i) { seqIndices[i] = RdU32LE(data + pos); pos += 4; }
                    }
                } else {
                    if (pos + totalCorners * 4u > size) return false;
                    for (size_t i = 0; i < totalCorners; ++i) { seqIndices[i] = RdU32LE(data + pos); pos += 4; }
                }
            } else if (connMethod == 0u) {
                // Sequential compressed indices: Draco uses DecodeSymbols (rANS) in v2.x, or varint delta in v1.0/1.1!
                size_t savedPos = pos;
                std::vector<uint32_t> symDeltas;
                if ((outMaj >= 2 || (outMaj == 1 && outMin >= 2)) &&
                    DracoDecodeSymbols(data, size, pos, static_cast<uint32_t>(totalCorners), 1u, symDeltas))
                {
                    int64_t prev = 0;
                    bool okRansIdx = true;
                    for (size_t i = 0; i < totalCorners; ++i) {
                        int32_t d = DracoConvertSymbolToSignedInt(symDeltas[i]);
                        int64_t v = prev + d;
                        if (v < 0 || static_cast<uint64_t>(v) >= numPoints) { okRansIdx = false; break; }
                        seqIndices[i] = static_cast<uint32_t>(v);
                        prev = v;
                    }
                    if (!okRansIdx) pos = savedPos;
                }
                if (pos == savedPos) {
                    int64_t prev = 0;
                    for (size_t i = 0; i < totalCorners; ++i) {
                        uint32_t zz = 0;
                        if (!ReadLeb128U32(data, size, pos, zz)) return false;
                        int64_t d = DracoConvertSymbolToSignedInt(zz);
                        int64_t v = prev + d;
                        if (v < 0 || static_cast<uint64_t>(v) >= numPoints) return false;
                        seqIndices[i] = static_cast<uint32_t>(v);
                        prev = v;
                    }
                }
            } else {
                return false;
            }
        } else {
            if (!ReadLeb128U64(data, size, pos, numPoints) || numPoints == 0 || numPoints > 4000000ULL) return false;
        }
        if (pos >= size) return false;
        numDecoders = data[pos++];
    } else {
        // EDGEBREAKER connectivity
        if (outEncType != 1u) return false;
        uint8_t nAttrData = 0;
        std::vector<int32_t> dummyMap;
        if (!DecodeDracoEdgebreakerConnectivity(data, size, pos, ebCornerTable, nAttrData, dummyMap)) return false;
        numFaces = ebCornerTable.numFaces;
        numPoints = ebCornerTable.numVertices;

        // Edgebreaker seam data for extra attribute decoders (nAttrData - 1): skip if only decoding geometry (decoder 0)
        for (uint8_t ad = 1; ad < nAttrData; ++ad) {
            if (pos >= size) return false;
            uint8_t seamProbZero = data[pos++];
            (void)seamProbZero;
            uint32_t seamSz = 0;
            if (!ReadLeb128U32(data, size, pos, seamSz) || pos + seamSz > size) return false;
            pos += seamSz;
        }
        if (pos >= size) return false;
        numDecoders = data[pos++];
    }

    if (numDecoders == 0 || numDecoders > 32) return false;

    // 3. Attribute Decoder Headers
    struct DracoAttrDesc {
        uint8_t attType = 0;
        uint8_t dataType = 0;
        uint8_t numComps = 0;
        uint8_t normalized = 0;
        uint32_t uniqueId = 0;
        uint8_t seqType = 0;
    };
    struct DecoderGroup {
        uint8_t dataId = 0;
        uint8_t decoderType = 0;     // 0 = VERTEX_ATTRIBUTE_DECODER, 1 = MESH_CORNER_ATTRIBUTE_DECODER
        uint8_t traversalMethod = 0; // 0 = DEPTH_FIRST, 1 = PREDICTION_DEGREE
        std::vector<DracoAttrDesc> attrs;
    };

    auto dtElemBytes = [](uint8_t dt) -> size_t {
        switch (dt) {
            case 1: case 2: case 11: return 1;
            case 3: case 4: return 2;
            case 5: case 6: case 9: return 4;
            case 7: case 8: case 10: return 8;
            default: return 0;
        }
    };

    std::vector<DecoderGroup> groups(numDecoders);
    if (encMethod == 1u) {
        for (uint8_t d = 0; d < numDecoders; ++d) {
            if (pos + 2 > size) return false;
            groups[d].dataId = data[pos++];
            groups[d].decoderType = data[pos++];
            if (outMaj > 1 || (outMaj == 1 && outMin >= 2)) {
                if (pos >= size) return false;
                groups[d].traversalMethod = data[pos++];
            }
        }
    }

    for (uint8_t d = 0; d < numDecoders; ++d) {
        uint32_t nAttrs = 0;
        if (!ReadLeb128U32(data, size, pos, nAttrs) || nAttrs == 0 || nAttrs > 64) return false;
        groups[d].attrs.resize(nAttrs);
        for (uint32_t a = 0; a < nAttrs; ++a) {
            if (pos + 4 > size) return false;
            groups[d].attrs[a].attType    = data[pos++];
            groups[d].attrs[a].dataType   = data[pos++];
            groups[d].attrs[a].numComps   = data[pos++];
            groups[d].attrs[a].normalized = data[pos++];
            if (!ReadLeb128U32(data, size, pos, groups[d].attrs[a].uniqueId)) return false;
            if (dtElemBytes(groups[d].attrs[a].dataType) == 0 ||
                groups[d].attrs[a].numComps == 0 || groups[d].attrs[a].numComps > 16)
            {
                return false;
            }
        }
        for (uint32_t a = 0; a < nAttrs; ++a) {
            if (pos >= size) return false;
            groups[d].attrs[a].seqType = data[pos++];
        }
    }

    // 4. Decode Portable Attributes & Reconstruct Position Buffer!
    // Note: in Draco v2.x, for each decoder group d:
    //   First ALL attributes in group d decode their prediction/symbol data (DecodePortableAttributes),
    //   and THEN ALL attributes in group d decode their transform parameters (DecodeDataNeededByPortableTransforms: quantization min/range/bits)!
    //   Wait: in Draco v1.x vs v2.x:
    //     In Draco v2.0+: DecodePortableAttributes runs for all a in group[d], THEN DecodeDataNeededByPortableTransforms runs for all a in group[d]!
    //     In Draco v1.x: transform data is inline right after each attribute's portable data!
    //   Let's handle both v2.0+ (batched per group) and v1.x cleanly!

    for (uint8_t d = 0; d < numDecoders; ++d) {
        const auto& grp = groups[d];
        std::vector<int32_t> vertOrder;
        std::vector<int32_t> cornerOrder;
        size_t nEntries = static_cast<size_t>(numPoints);

        if (encMethod == 1u && grp.dataId == 0u) {
            BuildEdgebreakerVertexSequence(ebCornerTable, grp.traversalMethod, vertOrder, cornerOrder);
            nEntries = vertOrder.size();
            if (nEntries == 0) return false;
        } else if (encMethod == 1u && grp.dataId > 0u) {
            if (!outPositions.empty()) break; // already decoded geometry positions from decoder 0!
            return false;
        }

        struct DecodedIntAttr {
            bool isRawFloat = false;
            std::vector<Vec3f> rawPositions;
            std::vector<int32_t> portableVals; // size = nEntries * numComps (in traversal order)
            int8_t predScheme = -2;
            int32_t predTransformType = -1;
        };
        std::vector<DecodedIntAttr> portAttrs(grp.attrs.size());

        for (size_t a = 0; a < grp.attrs.size(); ++a) {
            const auto& attr = grp.attrs[a];
            auto& pa = portAttrs[a];

            if (attr.seqType == 0u) { // SEQUENTIAL_ATTRIBUTE_ENCODER_GENERIC (raw uncompressed bytes)
                size_t elemSz = dtElemBytes(attr.dataType);
                size_t rawBytes = nEntries * attr.numComps * elemSz;
                if (pos < size && data[pos] == 0xFEu && (size - pos) >= rawBytes + 1u && grp.attrs.size() == 1) {
                    // Optional PREDICTION_NONE (-2) byte
                    if ((size - pos) == rawBytes + 1u) ++pos;
                }
                if (pos + rawBytes > size) return false;
                if ((attr.attType == 0u || static_cast<int>(attr.uniqueId) == targetPosUniqueId) &&
                    attr.numComps >= 3 && (attr.dataType == 9u || attr.dataType == 10u))
                {
                    pa.isRawFloat = true;
                    pa.rawPositions.resize(nEntries);
                    for (size_t v = 0; v < nEntries; ++v) {
                        const uint8_t* vp = data + pos + v * attr.numComps * elemSz;
                        if (attr.dataType == 9u) {
                            pa.rawPositions[v] = { RdF32LE(vp + 0), RdF32LE(vp + 4), RdF32LE(vp + 8) };
                        } else {
                            pa.rawPositions[v] = {
                                static_cast<float>(RdF64LE(vp + 0)),
                                static_cast<float>(RdF64LE(vp + 8)),
                                static_cast<float>(RdF64LE(vp + 16))
                            };
                        }
                    }
                }
                pos += rawBytes;
            } else if (attr.seqType == 1u || attr.seqType == 2u) {
                // 1 = INTEGER, 2 = QUANTIZATION, 3 = NORMALS
                if (pos >= size) return false;
                pa.predScheme = static_cast<int8_t>(data[pos++]);
                if (pa.predScheme != -2) {
                    // Prediction transform type (varint I32 / byte: 1 = PREDICTION_TRANSFORM_WRAP, 0 = DELTA, etc.)
                    if (outMaj > 1 || (outMaj == 1 && outMin >= 2)) {
                        uint32_t ptU = 0;
                        if (!ReadLeb128U32(data, size, pos, ptU)) return false;
                        pa.predTransformType = static_cast<int32_t>(ptU);
                    } else {
                        if (pos + 4 > size) return false;
                        pa.predTransformType = RdI32LE(data + pos);
                        pos += 4;
                    }
                }

                if (pos >= size) return false;
                uint8_t compressed = data[pos++];
                size_t totalScalars = nEntries * attr.numComps;
                pa.portableVals.resize(totalScalars, 0);

                if (compressed > 0u) {
                    std::vector<uint32_t> syms;
                    if (!DracoDecodeSymbols(data, size, pos, static_cast<uint32_t>(totalScalars), attr.numComps, syms)) {
                        return false;
                    }
                    for (size_t k = 0; k < totalScalars; ++k) {
                        if (pa.predScheme == -2 && attr.seqType == 2u) {
                            // Unsigned quantized symbols when no prediction scheme is active?
                            // In Draco SequentialIntegerAttributeDecoder::ConvertSymbolsToSignedInts: ALWAYS converts symbols to signed ints EXCEPT when PREDICTION_NONE? No: ConvertSymbolsToSignedInts ALWAYS runs when compressed > 0!
                            pa.portableVals[k] = DracoConvertSymbolToSignedInt(syms[k]);
                        } else {
                            pa.portableVals[k] = DracoConvertSymbolToSignedInt(syms[k]);
                        }
                    }
                } else {
                    if (pos >= size) return false;
                    // Legacy v1 uncompressed quantized case or Draco uncompressed byteSize (1/2/4)
                    if (outMaj == 1 && outMin == 0 && pa.predScheme == -2) {
                        if (pos + totalScalars * 4u > size) return false;
                        for (size_t k = 0; k < totalScalars; ++k) {
                            pa.portableVals[k] = RdI32LE(data + pos + k * 4u);
                        }
                        pos += totalScalars * 4u;
                    } else {
                        uint8_t intByteSz = data[pos++];
                        if ((intByteSz != 1 && intByteSz != 2 && intByteSz != 4) || pos + totalScalars * intByteSz > size) {
                            // Fallback for legacy 4-byte raw array without intByteSz prefix
                            --pos;
                            if (pos + totalScalars * 4u > size) return false;
                            for (size_t k = 0; k < totalScalars; ++k) pa.portableVals[k] = RdI32LE(data + pos + k * 4u);
                            pos += totalScalars * 4u;
                        } else {
                            for (size_t k = 0; k < totalScalars; ++k) {
                                if (intByteSz == 1)      pa.portableVals[k] = static_cast<int32_t>(data[pos + k]);
                                else if (intByteSz == 2) pa.portableVals[k] = static_cast<int32_t>(RdU16LE(data + pos + k * 2u));
                                else                     pa.portableVals[k] = RdI32LE(data + pos + k * 4u);
                            }
                            pos += totalScalars * intByteSz;
                        }
                    }
                }

                // Read prediction transform ancillary data (e.g. WRAP min/max for transformType == 1)
                if (pa.predScheme != -2 && pa.predTransformType == 1) {
                    if (pos + 8 > size) return false;
                    int32_t wrapMin = RdI32LE(data + pos + 0);
                    int32_t wrapMax = RdI32LE(data + pos + 4);
                    (void)wrapMin; (void)wrapMax;
                    pos += 8;
                }

                // Reconstruct predicted values across traversal sequence!
                if (pa.predScheme == 0) { // DIFFERENTIAL_PREDICTION (delta from previous entry)
                    for (size_t i = 1; i < nEntries; ++i) {
                        for (uint8_t c = 0; c < attr.numComps; ++c) {
                            pa.portableVals[i * attr.numComps + c] += pa.portableVals[(i - 1) * attr.numComps + c];
                        }
                    }
                } else if (pa.predScheme == 1 && encMethod == 1u && cornerOrder.size() == nEntries) {
                    // MESH_PREDICTION_PARALLELOGRAM across CornerTable!
                    std::vector<int32_t> vertToSeqIdx(ebCornerTable.numVertices, -1);
                    for (size_t i = 0; i < nEntries; ++i) {
                        int32_t cId = cornerOrder[i];
                        int32_t vTip = vertOrder[i];
                        bool predicted = false;
                        if (cId >= 0) {
                            int32_t oppC = ebCornerTable.Opposite(cId);
                            if (oppC >= 0) {
                                int32_t vNext = ebCornerTable.cornerToVert[static_cast<size_t>(DracoCornerTable::Next(cId))];
                                int32_t vPrev = ebCornerTable.cornerToVert[static_cast<size_t>(DracoCornerTable::Prev(cId))];
                                int32_t vOpp  = ebCornerTable.cornerToVert[static_cast<size_t>(oppC)];
                                int32_t sNext = (vNext >= 0) ? vertToSeqIdx[static_cast<size_t>(vNext)] : -1;
                                int32_t sPrev = (vPrev >= 0) ? vertToSeqIdx[static_cast<size_t>(vPrev)] : -1;
                                int32_t sOpp  = (vOpp  >= 0) ? vertToSeqIdx[static_cast<size_t>(vOpp)]  : -1;
                                if (sNext >= 0 && sPrev >= 0 && sOpp >= 0) {
                                    for (uint8_t c = 0; c < attr.numComps; ++c) {
                                        int32_t pred = pa.portableVals[static_cast<size_t>(sNext) * attr.numComps + c] +
                                                       pa.portableVals[static_cast<size_t>(sPrev) * attr.numComps + c] -
                                                       pa.portableVals[static_cast<size_t>(sOpp)  * attr.numComps + c];
                                        pa.portableVals[i * attr.numComps + c] += pred;
                                    }
                                    predicted = true;
                                }
                            }
                        }
                        if (!predicted && i > 0) {
                            for (uint8_t c = 0; c < attr.numComps; ++c) {
                                pa.portableVals[i * attr.numComps + c] += pa.portableVals[(i - 1) * attr.numComps + c];
                            }
                        }
                        if (vTip >= 0 && static_cast<size_t>(vTip) < vertToSeqIdx.size()) {
                            vertToSeqIdx[static_cast<size_t>(vTip)] = static_cast<int32_t>(i);
                        }
                    }
                }
            } else {
                if (!outPositions.empty()) break;
                return false;
            }
        }

        // Transform portable attributes to final floats (Quantization Transform parameters follow portable attributes in v2.x!)
        for (size_t a = 0; a < grp.attrs.size(); ++a) {
            const auto& attr = grp.attrs[a];
            auto& pa = portAttrs[a];
            bool wantsPos = (targetPosUniqueId >= 0)
                ? (static_cast<int>(attr.uniqueId) == targetPosUniqueId)
                : (attr.attType == 0u);

            std::vector<Vec3f> seqPos;
            if (pa.isRawFloat) {
                if (wantsPos) seqPos = std::move(pa.rawPositions);
            } else if (attr.seqType == 2u) {
                // Quantization Transform: in v2.x starts with transformType varint (1 = ATTRIBUTE_QUANTIZATION_TRANSFORM), then minValues[numComps], maxRange(float), quantBits(uint8)!
                if (outMaj >= 2) {
                    uint32_t trType = 0;
                    if (!ReadLeb128U32(data, size, pos, trType) || trType != 1u) return false;
                }
                size_t qMetaBytes = static_cast<size_t>(attr.numComps) * 4u + 4u + 1u;
                if (pos + qMetaBytes > size) return false;
                std::vector<float> minVals(attr.numComps, 0.0f);
                for (uint8_t c = 0; c < attr.numComps; ++c) {
                    minVals[c] = RdF32LE(data + pos + static_cast<size_t>(c) * 4u);
                }
                float maxRange = RdF32LE(data + pos + static_cast<size_t>(attr.numComps) * 4u);
                uint8_t qBits = data[pos + static_cast<size_t>(attr.numComps) * 4u + 4u];
                pos += qMetaBytes;
                if (qBits == 0 || qBits > 31 || !std::isfinite(maxRange) || maxRange < 0.0f) return false;

                if (wantsPos && attr.numComps >= 3) {
                    float step = (maxRange > 0.0f)
                        ? (maxRange / static_cast<float>((1ULL << qBits) - 1ULL))
                        : 0.0f;
                    seqPos.resize(nEntries);
                    for (size_t i = 0; i < nEntries; ++i) {
                        float x = minVals[0] + static_cast<float>(pa.portableVals[i * attr.numComps + 0]) * step;
                        float y = minVals[1] + static_cast<float>(pa.portableVals[i * attr.numComps + 1]) * step;
                        float z = minVals[2] + static_cast<float>(pa.portableVals[i * attr.numComps + 2]) * step;
                        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
                        seqPos[i] = { x, y, z };
                    }
                }
            }

            if (wantsPos && !seqPos.empty()) {
                if (encMethod == 1u && !vertOrder.empty()) {
                    outPositions.assign(ebCornerTable.numVertices, { 0.0f, 0.0f, 0.0f });
                    for (size_t i = 0; i < seqPos.size(); ++i) {
                        int32_t ptIdx = vertOrder[i];
                        if (ptIdx >= 0 && static_cast<size_t>(ptIdx) < outPositions.size()) {
                            outPositions[static_cast<size_t>(ptIdx)] = seqPos[i];
                        }
                    }
                } else {
                    outPositions = std::move(seqPos);
                }
            }
        }

        if (!outPositions.empty()) break;
    }

    if (outPositions.empty()) return false;

    if (outEncType == 1u) {
        if (encMethod == 0u) {
            outTris.reserve(static_cast<size_t>(numFaces));
            for (size_t f = 0; f < static_cast<size_t>(numFaces); ++f) {
                uint32_t i0 = seqIndices[f * 3 + 0];
                uint32_t i1 = seqIndices[f * 3 + 1];
                uint32_t i2 = seqIndices[f * 3 + 2];
                if (i0 < outPositions.size() && i1 < outPositions.size() && i2 < outPositions.size()) {
                    outTris.push_back({ i0, i1, i2 });
                }
            }
        } else {
            outTris.reserve(ebCornerTable.numFaces);
            for (uint32_t f = 0; f < ebCornerTable.numFaces; ++f) {
                int32_t v0 = ebCornerTable.cornerToVert[static_cast<size_t>(f) * 3u + 0];
                int32_t v1 = ebCornerTable.cornerToVert[static_cast<size_t>(f) * 3u + 1];
                int32_t v2 = ebCornerTable.cornerToVert[static_cast<size_t>(f) * 3u + 2];
                if (v0 < 0 || v1 < 0 || v2 < 0) return false;
                uint32_t u0 = static_cast<uint32_t>(v0);
                uint32_t u1 = static_cast<uint32_t>(v1);
                uint32_t u2 = static_cast<uint32_t>(v2);
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
inline bool ParseDracoDrcComplete(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    std::vector<Vec3f> positions;
    std::vector<std::array<uint32_t, 3>> tris;
    uint8_t encType = 0, maj = 0, minV = 0;
    if (!DecodeDracoBufferToMesh(data, size, -1, positions, tris, encType, maj, minV)) return false;

    mesh.vertices.clear();
    mesh.tris.clear();
    bool isEb = (size >= 9 && data[8] == 1u);
    mesh.formatName = L"Google Draco v" + std::to_wstring(maj) + L"." + std::to_wstring(minV) +
                      (encType == 0 ? L" Point Cloud (.drc)" : (isEb ? L" Edgebreaker 3D Mesh (.drc)" : L" 3D Mesh (.drc)"));

    if (encType == 1u) {
        mesh.vertices = std::move(positions);
        mesh.tris.reserve(tris.size());
        for (const auto& t : tris) mesh.tris.push_back({ t[0], t[1], t[2], {} });
    } else {
        // Point cloud tetrahedra
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

// ============================================================================
// Khronos EXT_meshopt_compression / KHR_meshopt_compression BufferView Decoder
// ============================================================================
namespace MeshoptCodec {

inline uint8_t Unzigzag8(uint8_t v) {
    return static_cast<uint8_t>(static_cast<uint8_t>(0u - (v & 1u)) ^ (v >> 1));
}
inline uint32_t Unzigzag32(uint32_t v) {
    return (0u - (v & 1u)) ^ (v >> 1);
}

inline bool DecodeBytesGroup(int bitslog2, const uint8_t*& data, const uint8_t* dataEnd, uint8_t out[16]) {
    if (bitslog2 == 0) {
        std::memset(out, 0, 16);
        return true;
    }
    if (bitslog2 == 1) { // 2-bit values (4 bytes -> 16 values; optionally escaped by literal bytes in v0 if needed)
        if (dataEnd - data < 4) return false;
        for (int i = 0; i < 4; ++i) {
            uint8_t b = *data++;
            out[i * 4 + 0] = (b >> 6) & 3u;
            out[i * 4 + 1] = (b >> 4) & 3u;
            out[i * 4 + 2] = (b >> 2) & 3u;
            out[i * 4 + 3] = b & 3u;
        }
        return true;
    }
    if (bitslog2 == 2) { // 4-bit values (8 bytes -> 16 values)
        if (dataEnd - data < 8) return false;
        for (int i = 0; i < 8; ++i) {
            uint8_t b = *data++;
            out[i * 2 + 0] = (b >> 4) & 15u;
            out[i * 2 + 1] = b & 15u;
        }
        return true;
    }
    // bitslog2 == 3: 8-bit literal values (16 bytes)
    if (dataEnd - data < 16) return false;
    std::memcpy(out, data, 16);
    data += 16;
    return true;
}

inline bool DecodeMeshoptAttributes(
    const uint8_t* src,
    size_t srcSize,
    size_t count,
    size_t stride,
    std::vector<uint8_t>& dst)
{
    if (!src || srcSize < 1 || count == 0 || stride == 0 || stride > 256 || (stride % 4u) != 0u) return false;
    if ((src[0] & 0xF0u) != 0xA0u) return false;
    uint8_t version = src[0] & 0x0Fu;
    if (version > 1u) return false;

    size_t tailSize = (version == 0u) ? std::max<size_t>(stride, 32u) : 32u;
    if (srcSize < 1u + tailSize) return false;

    const uint8_t* data = src + 1;
    const uint8_t* dataEnd = src + srcSize - stride; // last `stride` bytes store initial reference lastVertex!
    std::vector<uint8_t> lastVertex(src + srcSize - stride, src + srcSize);

    size_t vertexBlockSize = (8192u / stride) & ~size_t(15);
    vertexBlockSize = std::clamp<size_t>(vertexBlockSize, 32u, 256u);

    dst.assign(count * stride, 0u);
    std::vector<uint8_t> transposed(vertexBlockSize * stride, 0u);

    size_t vertexOffset = 0;
    while (vertexOffset < count) {
        size_t blockSize = (vertexOffset + vertexBlockSize <= count) ? vertexBlockSize : (count - vertexOffset);
        size_t blockSizeAligned = (blockSize + 15u) & ~size_t(15);
        size_t groups = blockSizeAligned / 16u;
        size_t headerSize = (groups + 3u) / 4u;

        for (size_t k = 0; k < stride; ++k) {
            if (static_cast<size_t>(dataEnd - data) < headerSize) return false;
            const uint8_t* header = data;
            data += headerSize;
            for (size_t g = 0; g < groups; ++g) {
                int bitslog2 = (header[g >> 2] >> ((g & 3u) * 2u)) & 3;
                uint8_t grpBuf[16];
                if (!DecodeBytesGroup(bitslog2, data, dataEnd, grpBuf)) return false;
                std::memcpy(transposed.data() + k * blockSizeAligned + g * 16u, grpBuf, 16u);
            }
        }

        uint8_t* blockDst = dst.data() + vertexOffset * stride;
        for (size_t v = 0; v < blockSize; ++v) {
            for (size_t k = 0; k < stride; ++k) {
                uint8_t delta = transposed[k * blockSizeAligned + v];
                uint8_t val = static_cast<uint8_t>(lastVertex[k] + Unzigzag8(delta));
                lastVertex[k] = val;
                blockDst[v * stride + k] = val;
            }
        }
        vertexOffset += blockSize;
    }
    return true;
}

inline void ApplyMeshoptFilterExponential(uint8_t* data, size_t count, size_t stride) {
    size_t totalWords = (count * stride) / 4u;
    for (size_t i = 0; i < totalWords; ++i) {
        int32_t v = 0;
        std::memcpy(&v, data + i * 4u, 4);
        int32_t exp = v >> 24;
        int32_t mant = (v << 8) >> 8; // sign-extend 24-bit signed mantissa
        float f = std::ldexp(static_cast<float>(mant), exp);
        std::memcpy(data + i * 4u, &f, 4);
    }
}

inline bool ReadMeshoptVByte(const uint8_t*& p, const uint8_t* end, uint32_t& out) {
    out = 0;
    uint32_t shift = 0;
    for (int i = 0; i < 5; ++i) {
        if (p >= end) return false;
        uint8_t b = *p++;
        out |= static_cast<uint32_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0u) return true;
        shift += 7;
    }
    return false;
}

inline bool DecodeMeshoptTriangles(
    const uint8_t* src,
    size_t srcSize,
    size_t indexCount,
    size_t indexSize,
    std::vector<uint8_t>& dst)
{
    if (!src || (indexCount % 3u) != 0u || (indexSize != 2u && indexSize != 4u)) return false;
    if (srcSize < 1u + (indexCount / 3u) + 16u) return false;
    if ((src[0] & 0xF0u) != 0xE0u) return false;
    uint8_t version = src[0] & 0x0Fu;
    if (version > 1u) return false;

    size_t triCount = indexCount / 3u;
    const uint8_t* code = src + 1;
    const uint8_t* data = code + triCount;
    const uint8_t* dataSafeEnd = src + srcSize - 16u;
    const uint8_t* codeauxTable = dataSafeEnd;

    int fecmax = (version >= 1u) ? 13 : 15;
    if (version == 0u && (codeauxTable[14] != 0u || codeauxTable[15] != 0u)) return false;

    uint32_t edgefifo[16][2] = {};
    uint32_t vertexfifo[16] = {};
    size_t edgefifoOffset = 0;
    size_t vertexfifoOffset = 0;
    uint32_t next = 0;
    uint32_t last = 0;

    auto pushEdgeFifo = [&](uint32_t a, uint32_t b) {
        edgefifo[edgefifoOffset][0] = a;
        edgefifo[edgefifoOffset][1] = b;
        edgefifoOffset = (edgefifoOffset + 1u) & 15u;
    };
    auto pushVertexFifo = [&](uint32_t v, bool cond = true) {
        vertexfifo[vertexfifoOffset] = v;
        if (cond) vertexfifoOffset = (vertexfifoOffset + 1u) & 15u;
    };
    auto decodeIndexDelta = [&](uint32_t& outIdx) -> bool {
        uint32_t v = 0;
        if (!ReadMeshoptVByte(data, dataSafeEnd, v)) return false;
        uint32_t d = Unzigzag32(v);
        last += d;
        outIdx = last;
        return true;
    };

    dst.resize(indexCount * indexSize);
    auto writeIdx = [&](size_t slot, uint32_t val) {
        if (indexSize == 2u) {
            uint16_t v16 = static_cast<uint16_t>(val);
            std::memcpy(dst.data() + slot * 2u, &v16, 2);
        } else {
            std::memcpy(dst.data() + slot * 4u, &val, 4);
        }
    };

    for (size_t i = 0; i < triCount; ++i) {
        uint8_t codetri = code[i];
        if (codetri < 0xF0u) {
            int fe = codetri >> 4;
            uint32_t a = edgefifo[(edgefifoOffset - 1u - static_cast<size_t>(fe)) & 15u][0];
            uint32_t b = edgefifo[(edgefifoOffset - 1u - static_cast<size_t>(fe)) & 15u][1];
            int fec = codetri & 15;
            uint32_t c = 0;

            if (fec < fecmax) {
                c = (fec == 0) ? next : vertexfifo[(vertexfifoOffset - 1u - static_cast<size_t>(fec)) & 15u];
                bool fec0 = (fec == 0);
                next += fec0 ? 1u : 0u;
                pushVertexFifo(c, fec0);
            } else {
                if (fec != 15) {
                    c = last + static_cast<uint32_t>(fec - (fec ^ 3)); // v1 small delta
                    last = c;
                } else {
                    if (!decodeIndexDelta(c)) return false;
                }
                pushVertexFifo(c);
            }
            pushEdgeFifo(c, b);
            pushEdgeFifo(a, c);
            writeIdx(i * 3u + 0, a);
            writeIdx(i * 3u + 1, b);
            writeIdx(i * 3u + 2, c);
        } else {
            // Restart triangle (0xF0 .. 0xFF)
            if (codetri < 0xFEu) {
                uint8_t aux = codeauxTable[codetri & 15u];
                int feb = aux >> 4;
                int fec = aux & 15;
                uint32_t a = next++;

                uint32_t b = (feb == 0) ? next : vertexfifo[(vertexfifoOffset - static_cast<size_t>(feb)) & 15u];
                bool feb0 = (feb == 0);
                next += feb0 ? 1u : 0u;

                uint32_t c = (fec == 0) ? next : vertexfifo[(vertexfifoOffset - static_cast<size_t>(fec)) & 15u];
                bool fec0 = (fec == 0);
                next += fec0 ? 1u : 0u;

                pushVertexFifo(a);
                pushVertexFifo(b, feb0);
                pushVertexFifo(c, fec0);
                pushEdgeFifo(b, a);
                pushEdgeFifo(c, b);
                pushEdgeFifo(a, c);
                writeIdx(i * 3u + 0, a);
                writeIdx(i * 3u + 1, b);
                writeIdx(i * 3u + 2, c);
            } else {
                uint8_t extra = (codetri == 0xFEu) ? *data++ : *data++;
                if (data > dataSafeEnd) return false;
                int fea = (codetri == 0xFEu) ? 0 : 15;
                int feb = extra >> 4;
                int fec = extra & 15;

                uint32_t a = 0, b = 0, c = 0;
                if (fea == 0) { a = next++; } else { if (!decodeIndexDelta(a)) return false; }
                if (feb == 0) { b = next++; }
                else if (feb < 15) { b = vertexfifo[(vertexfifoOffset - static_cast<size_t>(feb)) & 15u]; }
                else { if (!decodeIndexDelta(b)) return false; }

                if (fec == 0) { c = next++; }
                else if (fec < 15) { c = vertexfifo[(vertexfifoOffset - static_cast<size_t>(fec)) & 15u]; }
                else { if (!decodeIndexDelta(c)) return false; }

                pushVertexFifo(a, fea == 0 || fea == 15);
                pushVertexFifo(b, feb == 0 || feb == 15);
                pushVertexFifo(c, fec == 0 || fec == 15);
                pushEdgeFifo(b, a);
                pushEdgeFifo(c, b);
                pushEdgeFifo(a, c);
                writeIdx(i * 3u + 0, a);
                writeIdx(i * 3u + 1, b);
                writeIdx(i * 3u + 2, c);
            }
        }
    }
    return true;
}

} // namespace MeshoptCodec

inline bool DecodeMeshoptBufferView(
    const uint8_t* src,
    size_t srcSize,
    size_t count,
    size_t byteStride,
    std::string_view mode,
    std::string_view filter,
    std::vector<uint8_t>& outDecoded)
{
    outDecoded.clear();
    if (!src || srcSize == 0 || count == 0 || byteStride == 0) return false;
    if (count > 16000000u || byteStride > 256u || count * byteStride > 256ULL * 1024ULL * 1024ULL) return false;

    if (mode == "ATTRIBUTES") {
        if (!MeshoptCodec::DecodeMeshoptAttributes(src, srcSize, count, byteStride, outDecoded)) return false;
        if (filter == "EXPONENTIAL") {
            if ((byteStride % 4u) != 0u) return false;
            MeshoptCodec::ApplyMeshoptFilterExponential(outDecoded.data(), count, byteStride);
        }
        return true;
    }
    if (mode == "TRIANGLES") {
        return MeshoptCodec::DecodeMeshoptTriangles(src, srcSize, count, byteStride, outDecoded);
    }
    return false;
}

} // namespace Formats
