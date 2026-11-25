#pragma once
// SiemensJt.h — ISO 14306 Siemens JT Open (.jt) Shape LOD & Int32CDP 3D CAD reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// ============================================================================
// ISO 14306 Siemens JT Open v8.x / v9.x / v10.x Shape LOD & Int32CDP Decoder
// ============================================================================
namespace DetailJt14306 {

struct JtGuid {
    uint32_t d1 = 0;
    uint16_t d2 = 0;
    uint16_t d3 = 0;
    uint8_t  b[8] = {};

    bool Equals(uint32_t a1, uint16_t a2, uint16_t a3,
                uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3,
                uint8_t b4, uint8_t b5, uint8_t b6, uint8_t b7) const {
        return d1 == a1 && d2 == a2 && d3 == a3 &&
               b[0] == b0 && b[1] == b1 && b[2] == b2 && b[3] == b3 &&
               b[4] == b4 && b[5] == b5 && b[6] == b6 && b[7] == b7;
    }
};

struct JtStream {
    const uint8_t* buf = nullptr;
    size_t len = 0;
    size_t pos = 0;
    bool isBE = false;
    bool ok = true;

    size_t Remaining() const { return (ok && pos <= len) ? (len - pos) : 0; }

    bool Skip(size_t n) {
        if (!ok || n > len - pos) { ok = false; return false; }
        pos += n;
        return true;
    }

    uint8_t ReadU8() {
        if (!ok || pos + 1 > len) { ok = false; return 0; }
        return buf[pos++];
    }

    uint16_t ReadU16() {
        if (!ok || pos + 2 > len) { ok = false; return 0; }
        uint16_t v = isBE ? DetailMeshSciCad::RdU16BE(buf + pos) : DetailMeshSciCad::RdU16LE(buf + pos);
        pos += 2;
        return v;
    }

    int16_t ReadI16() { return static_cast<int16_t>(ReadU16()); }

    uint32_t ReadU32() {
        if (!ok || pos + 4 > len) { ok = false; return 0; }
        uint32_t v = isBE ? DetailMeshSciCad::RdU32BE(buf + pos) : DetailMeshSciCad::RdU32LE(buf + pos);
        pos += 4;
        return v;
    }

    int32_t ReadI32() { return static_cast<int32_t>(ReadU32()); }

    uint64_t ReadU64() {
        if (!ok || pos + 8 > len) { ok = false; return 0; }
        uint64_t v = isBE ? DetailMeshSciCad::RdU64BE(buf + pos) : DetailMeshSciCad::RdU64LE(buf + pos);
        pos += 8;
        return v;
    }

    float ReadRawF32() {
        uint32_t u = ReadU32();
        float f = 0.0f;
        std::memcpy(&f, &u, 4);
        if (!std::isfinite(f)) ok = false;
        return f;
    }

    JtGuid ReadGuid() {
        JtGuid g;
        g.d1 = ReadU32();
        g.d2 = ReadU16();
        g.d3 = ReadU16();
        for (int i = 0; i < 8; ++i) g.b[i] = ReadU8();
        return g;
    }
};

struct JtWordBitReader {
    const uint32_t* words = nullptr;
    size_t totalBits = 0;
    size_t bitPos = 0;
    bool ok = true;

    uint32_t ReadU(uint32_t n) {
        if (n == 0u) return 0u;
        if (!ok || n > 32u || bitPos + n > totalBits) { ok = false; return 0u; }
        uint32_t val = 0u;
        for (uint32_t i = 0; i < n; ++i) {
            size_t wi = bitPos >> 5u;
            uint32_t bi = 31u - static_cast<uint32_t>(bitPos & 31u);
            val = (val << 1u) | ((words[wi] >> bi) & 1u);
            ++bitPos;
        }
        return val;
    }

    int32_t ReadS(uint32_t n) {
        if (n == 0u) return 0;
        uint32_t u = ReadU(n);
        if (n < 32u && (u & (1u << (n - 1u))) != 0u) {
            u |= (~0u << n);
        }
        return static_cast<int32_t>(u);
    }
};

struct JtByteBitReader {
    const uint8_t* bytes = nullptr;
    size_t totalBytes = 0;
    size_t bitPos = 0;
    bool ok = true;

    uint32_t ReadU(uint32_t n) {
        if (n == 0u) return 0u;
        if (!ok || n > 32u || bitPos + n > totalBytes * 8ULL) { ok = false; return 0u; }
        uint32_t val = 0u;
        for (uint32_t i = 0; i < n; ++i) {
            size_t bi = bitPos >> 3u;
            uint32_t sh = 7u - static_cast<uint32_t>(bitPos & 7u);
            val = (val << 1u) | ((bytes[bi] >> sh) & 1u);
            ++bitPos;
        }
        return val;
    }

    int32_t ReadS(uint32_t n) {
        if (n == 0u) return 0;
        uint32_t u = ReadU(n);
        if (n < 32u && (u & (1u << (n - 1u))) != 0u) {
            u |= (~0u << n);
        }
        return static_cast<int32_t>(u);
    }
};

enum class JtPredictor : uint8_t {
    Lag1 = 0,
    Lag2 = 1,
    Xor1 = 2,
    Xor2 = 3,
    Stride1 = 4,
    Stride2 = 5,
    StripIndex = 6,
    Ramp = 7,
    Null = 8
};

inline int32_t WrapAddI32(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

inline int32_t WrapSubI32(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

inline void UnpackJtResiduals(const std::vector<int32_t>& res, JtPredictor pred, std::vector<int32_t>& out) {
    out.resize(res.size());
    for (size_t i = 0; i < res.size(); ++i) {
        switch (pred) {
        case JtPredictor::Lag1:
            out[i] = (i < 1u) ? res[i] : WrapAddI32(res[i], out[i - 1u]);
            break;
        case JtPredictor::Lag2:
            out[i] = (i < 2u) ? res[i] : WrapAddI32(res[i], out[i - 2u]);
            break;
        case JtPredictor::Xor1:
            out[i] = (i < 1u) ? res[i] : static_cast<int32_t>(static_cast<uint32_t>(res[i]) ^ static_cast<uint32_t>(out[i - 1u]));
            break;
        case JtPredictor::Xor2:
            out[i] = (i < 2u) ? res[i] : static_cast<int32_t>(static_cast<uint32_t>(res[i]) ^ static_cast<uint32_t>(out[i - 2u]));
            break;
        case JtPredictor::Stride1:
            out[i] = (i < 2u) ? res[i] : WrapAddI32(res[i], WrapAddI32(out[i - 1u], WrapSubI32(out[i - 1u], out[i - 2u])));
            break;
        case JtPredictor::Stride2:
            out[i] = (i < 4u) ? res[i] : WrapAddI32(res[i], WrapAddI32(out[i - 2u], WrapSubI32(out[i - 2u], out[i - 4u])));
            break;
        case JtPredictor::StripIndex:
            if (i < 2u) out[i] = res[i];
            else if (i < 4u) out[i] = WrapAddI32(res[i], out[i - 1u]);
            else {
                int32_t diff = WrapSubI32(out[i - 2u], out[i - 4u]);
                int32_t step = (diff > -2 && diff < 8) ? 2 : 1;
                out[i] = WrapAddI32(res[i], WrapAddI32(out[i - 2u], step));
            }
            break;
        case JtPredictor::Ramp:
            out[i] = WrapAddI32(res[i], static_cast<int32_t>(i));
            break;
        case JtPredictor::Null:
        default:
            out[i] = res[i];
            break;
        }
    }
}

inline bool DecodeJtBitlengthWords(
    const std::vector<uint32_t>& words,
    size_t codeTextBits,
    size_t valueCount,
    std::vector<int32_t>& outRes)
{
    outRes.clear();
    if (valueCount == 0u) return true;
    if (codeTextBits > words.size() * 32ULL) return false;
    outRes.reserve(valueCount);

    JtWordBitReader br{ words.data(), codeTextBits, 0, true };
    uint32_t mode = br.ReadU(1);
    if (!br.ok) return false;

    if (mode == 0u) {
        // Fixed-width bitlength mode: [6-bit minBits][6-bit maxBits][minSym][maxSym][unsigned offsets...]
        uint32_t minBits = br.ReadU(6);
        uint32_t maxBits = br.ReadU(6);
        if (!br.ok || minBits > 32u || maxBits > 32u) return false;
        int32_t minSym = br.ReadS(minBits);
        int32_t maxSym = br.ReadS(maxBits);
        if (!br.ok || maxSym < minSym) return false;

        uint64_t range = static_cast<uint64_t>(static_cast<int64_t>(maxSym) - static_cast<int64_t>(minSym));
        uint32_t fieldW = 0u;
        while (fieldW < 32u && (1ULL << fieldW) <= range) ++fieldW;

        for (size_t i = 0; i < valueCount; ++i) {
            uint32_t off = br.ReadU(fieldW);
            if (!br.ok) return false;
            outRes.push_back(static_cast<int32_t>(static_cast<int64_t>(minSym) + static_cast<int64_t>(off)));
        }
        return true;
    }

    // Variable-width adaptive step mode: [8-bit signed mean][3-bit valBits][3-bit lenBits]
    int32_t iMean = br.ReadS(8);
    uint32_t cBlkValBits = br.ReadU(3);
    uint32_t cBlkLenBits = br.ReadU(3);
    if (!br.ok) return false;

    int32_t cFieldWidth = 0;
    while (outRes.size() < valueCount) {
        size_t prevBit = br.bitPos;
        size_t prevOut = outRes.size();
        if (br.ReadU(1) != 0u) {
            do {
                cFieldWidth += br.ReadS(cBlkValBits);
                if (!br.ok || cFieldWidth < 0 || cFieldWidth > 32) return false;
            } while (br.ReadU(1) != 0u);
        }
        uint32_t runLen = (cBlkLenBits == 0u) ? 1u : br.ReadU(cBlkLenBits);
        if (!br.ok) return false;
        for (uint32_t j = 0; j < runLen && outRes.size() < valueCount; ++j) {
            int32_t sym = (cFieldWidth == 0) ? 0 : br.ReadS(static_cast<uint32_t>(cFieldWidth));
            if (!br.ok) return false;
            outRes.push_back(WrapAddI32(sym, iMean));
        }
        if (br.bitPos == prevBit && outRes.size() == prevOut) return false;
    }
    return true;
}

inline bool DecodeJtInt32CDPv9Residuals(JtStream& sr, std::vector<int32_t>& outRes, int depth = 0) {
    outRes.clear();
    if (!sr.ok || depth > 4) return false;
    uint8_t codecType = sr.ReadU8();
    if (!sr.ok) return false;

    if (codecType == 0u) { // Null codec: [int32 byteCount][int32 values...]
        int32_t rawBytes = sr.ReadI32();
        if (!sr.ok || rawBytes < 0 || (rawBytes % 4) != 0 || rawBytes > 16 * 1024 * 1024 ||
            static_cast<size_t>(rawBytes) > sr.Remaining())
        {
            return false;
        }
        size_t cnt = static_cast<size_t>(rawBytes / 4);
        outRes.resize(cnt);
        for (size_t i = 0; i < cnt; ++i) outRes[i] = sr.ReadI32();
        return sr.ok;
    }

    if (codecType == 1u) { // Bitlength codec
        int32_t codeTextBits = sr.ReadI32();
        int32_t valueCount   = sr.ReadI32();
        int32_t numWords     = sr.ReadI32();
        if (!sr.ok || codeTextBits < 0 || valueCount < 0 || valueCount > 4000000 ||
            numWords < 0 || numWords > 4000000 ||
            static_cast<size_t>(numWords) * 4u > sr.Remaining() ||
            static_cast<int64_t>(numWords) * 32LL < static_cast<int64_t>(codeTextBits))
        {
            return false;
        }
        std::vector<uint32_t> words(static_cast<size_t>(numWords));
        for (int32_t i = 0; i < numWords; ++i) words[static_cast<size_t>(i)] = sr.ReadU32();
        return sr.ok && DecodeJtBitlengthWords(words, static_cast<size_t>(codeTextBits),
                                               static_cast<size_t>(valueCount), outRes);
    }

    if (codecType == 2u) { // Huffman codec
        uint8_t probTableCount = sr.ReadU8();
        if (!sr.ok || probTableCount != 1u) return false;

        JtByteBitReader pb{ sr.buf + sr.pos, sr.Remaining(), 0, true };
        uint32_t entryCount = pb.ReadU(16);
        uint32_t symBits    = pb.ReadU(6);
        uint32_t occBits    = pb.ReadU(6);
        uint32_t valBits    = pb.ReadU(6);
        uint32_t nextBits   = pb.ReadU(6);
        int32_t  minValue   = static_cast<int32_t>(pb.ReadU(32));
        if (!pb.ok || entryCount == 0u || entryCount > 65535u ||
            symBits > 32u || occBits == 0u || occBits > 32u || valBits > 32u || nextBits > 32u)
        {
            return false;
        }

        struct HuffNode {
            uint64_t occ = 0;
            int32_t sym = 0;
            int32_t assocVal = 0;
            int32_t left = -1;  // bit 1
            int32_t right = -1; // bit 0
            uint32_t order = 0;
        };
        std::vector<HuffNode> nodes;
        nodes.reserve(static_cast<size_t>(entryCount) * 2u);
        for (uint32_t i = 0; i < entryCount; ++i) {
            HuffNode lf;
            lf.sym = pb.ReadS(symBits);
            lf.occ = pb.ReadU(occBits);
            uint32_t vOff = pb.ReadU(valBits);
            (void)pb.ReadU(nextBits);
            if (!pb.ok) return false;
            lf.assocVal = WrapAddI32(minValue, static_cast<int32_t>(vOff));
            lf.order = i;
            nodes.push_back(lf);
        }
        size_t pbBytes = (pb.bitPos + 7u) / 8u;
        if (!sr.Skip(pbBytes)) return false;

        int32_t oobCount = sr.ReadI32();
        if (!sr.ok || oobCount < 0 || oobCount > 4000000) return false;
        std::vector<int32_t> oobVals;
        if (oobCount > 0) {
            if (!DecodeJtInt32CDPv9Residuals(sr, oobVals, depth + 1) ||
                oobVals.size() != static_cast<size_t>(oobCount))
            {
                return false;
            }
        }

        int32_t codeTextBits = sr.ReadI32();
        int32_t valueCount   = sr.ReadI32();
        int32_t numWords     = sr.ReadI32();
        if (!sr.ok || codeTextBits < 0 || valueCount < 0 || valueCount > 4000000 ||
            numWords < 0 || numWords > 4000000 ||
            static_cast<size_t>(numWords) * 4u > sr.Remaining() ||
            static_cast<int64_t>(numWords) * 32LL < static_cast<int64_t>(codeTextBits))
        {
            return false;
        }
        std::vector<uint32_t> words(static_cast<size_t>(numWords));
        for (int32_t i = 0; i < numWords; ++i) words[static_cast<size_t>(i)] = sr.ReadU32();
        if (!sr.ok) return false;
        if (valueCount == 0) return true;

        if (entryCount == 1u) {
            outRes.reserve(static_cast<size_t>(valueCount));
            size_t oobIdx = 0;
            for (int32_t i = 0; i < valueCount; ++i) {
                if (nodes[0].sym == -2) {
                    if (oobIdx >= oobVals.size()) return false;
                    outRes.push_back(oobVals[oobIdx++]);
                } else {
                    outRes.push_back(nodes[0].assocVal);
                }
            }
            return true;
        }

        // Build ISO 14306 §8.1.2.1.2 Huffman tree from symbol occurrence counts
        std::vector<int32_t> active(entryCount);
        for (uint32_t i = 0; i < entryCount; ++i) active[i] = static_cast<int32_t>(i);
        uint32_t nextOrder = entryCount;
        while (active.size() > 1u) {
            auto cmp = [&](int32_t a, int32_t b) {
                const auto& na = nodes[static_cast<size_t>(a)];
                const auto& nb = nodes[static_cast<size_t>(b)];
                if (na.occ != nb.occ) return na.occ < nb.occ;
                return na.order < nb.order;
            };
            std::sort(active.begin(), active.end(), cmp);
            int32_t n0 = active[0];
            int32_t n1 = active[1];
            HuffNode parent;
            parent.occ   = nodes[static_cast<size_t>(n0)].occ + nodes[static_cast<size_t>(n1)].occ;
            parent.left  = n0; // bit 1
            parent.right = n1; // bit 0
            parent.order = nextOrder++;
            int32_t pIdx = static_cast<int32_t>(nodes.size());
            nodes.push_back(parent);
            active.erase(active.begin(), active.begin() + 2);
            active.push_back(pIdx);
        }
        int32_t rootIdx = active[0];

        JtWordBitReader br{ words.data(), static_cast<size_t>(codeTextBits), 0, true };
        outRes.reserve(static_cast<size_t>(valueCount));
        size_t oobIdx = 0;
        for (int32_t i = 0; i < valueCount; ++i) {
            int32_t cur = rootIdx;
            while (nodes[static_cast<size_t>(cur)].left >= 0) {
                uint32_t bit = br.ReadU(1);
                if (!br.ok) return false;
                cur = (bit != 0u) ? nodes[static_cast<size_t>(cur)].left
                                  : nodes[static_cast<size_t>(cur)].right;
            }
            const auto& lf = nodes[static_cast<size_t>(cur)];
            if (lf.sym == -2) {
                if (oobIdx >= oobVals.size()) return false;
                outRes.push_back(oobVals[oobIdx++]);
            } else {
                outRes.push_back(lf.assocVal);
            }
        }
        return true;
    }

    return false;
}

inline bool DecodeJtInt32CDP2Residuals(JtStream& sr, std::vector<int32_t>& outRes, int depth = 0) {
    outRes.clear();
    if (!sr.ok || depth > 4) return false;
    int32_t valueCount = sr.ReadI32();
    if (!sr.ok || valueCount < 0 || valueCount > 4000000) return false;
    if (valueCount == 0) return true;
    uint8_t codecType = sr.ReadU8();
    if (!sr.ok) return false;

    if (codecType == 0u) { // Null
        if (static_cast<size_t>(valueCount) * 4u > sr.Remaining()) return false;
        outRes.resize(static_cast<size_t>(valueCount));
        for (int32_t i = 0; i < valueCount; ++i) outRes[static_cast<size_t>(i)] = sr.ReadI32();
        return sr.ok;
    }
    if (codecType == 1u) { // Bitlength2
        int32_t codeTextBits = sr.ReadI32();
        int32_t numWords     = sr.ReadI32();
        if (!sr.ok || codeTextBits < 0 || numWords < 0 || numWords > 4000000 ||
            static_cast<size_t>(numWords) * 4u > sr.Remaining() ||
            static_cast<int64_t>(numWords) * 32LL < static_cast<int64_t>(codeTextBits))
        {
            return false;
        }
        std::vector<uint32_t> words(static_cast<size_t>(numWords));
        for (int32_t i = 0; i < numWords; ++i) words[static_cast<size_t>(i)] = sr.ReadU32();
        return sr.ok && DecodeJtBitlengthWords(words, static_cast<size_t>(codeTextBits),
                                               static_cast<size_t>(valueCount), outRes);
    }
    if (codecType == 2u) { // Chopper: chop bits -> bias + (valMSB << chop) | valLSB
        uint8_t chop = sr.ReadU8();
        if (!sr.ok) return false;
        if (chop == 0u) return DecodeJtInt32CDP2Residuals(sr, outRes, depth + 1);
        int32_t bias  = sr.ReadI32();
        uint8_t spanB = sr.ReadU8();
        if (!sr.ok || chop > 31u || spanB > 32u || spanB < chop) return false;
        std::vector<int32_t> msb, lsb;
        if (!DecodeJtInt32CDP2Residuals(sr, msb, depth + 1) ||
            !DecodeJtInt32CDP2Residuals(sr, lsb, depth + 1) ||
            msb.size() != static_cast<size_t>(valueCount) ||
            lsb.size() != static_cast<size_t>(valueCount))
        {
            return false;
        }
        outRes.resize(static_cast<size_t>(valueCount));
        uint32_t lsbMask = (1u << chop) - 1u;
        for (size_t i = 0; i < static_cast<size_t>(valueCount); ++i) {
            uint32_t combined = (static_cast<uint32_t>(msb[i]) << chop) | (static_cast<uint32_t>(lsb[i]) & lsbMask);
            outRes[i] = WrapAddI32(static_cast<int32_t>(combined), bias);
        }
        return true;
    }
    return false;
}

inline bool DecodeJtInt32CDP(JtStream& sr, int majorVer, JtPredictor pred, std::vector<int32_t>& outVals, std::vector<int32_t>* outRawRes = nullptr) {
    std::vector<int32_t> res;
    size_t saved = sr.pos;
    bool decoded = false;
    if (majorVer >= 10) {
        decoded = DecodeJtInt32CDP2Residuals(sr, res);
        if (!decoded) {
            sr.pos = saved;
            sr.ok = true;
            decoded = DecodeJtInt32CDPv9Residuals(sr, res);
        }
    } else {
        decoded = DecodeJtInt32CDPv9Residuals(sr, res);
    }
    if (!decoded) return false;
    if (outRawRes) *outRawRes = res;
    UnpackJtResiduals(res, pred, outVals);
    return true;
}

inline bool ValidateStripOffsets(const std::vector<int32_t>& strips, size_t minElemPerStrip = 3u) {
    if (strips.size() < 2u || strips[0] != 0) return false;
    for (size_t s = 0; s + 1u < strips.size(); ++s) {
        if (strips[s] < 0 || strips[s + 1u] < strips[s]) return false;
        if (static_cast<size_t>(strips[s + 1u] - strips[s]) < minElemPerStrip) return false;
    }
    return strips.back() > 0 && strips.back() <= 2000000;
}

inline bool ParseVertexBasedShapeCompressedRepAt(
    const uint8_t* buf,
    size_t endPos,
    size_t startPos,
    bool isBE,
    int majorVer,
    bool v10ByteVer,
    bool isPolyLine,
    LoadedMesh3D& mesh)
{
    JtStream sr{ buf, endPos, startPos, isBE, true };
    int repVer = v10ByteVer ? static_cast<int>(sr.ReadU8()) : static_cast<int>(sr.ReadI16());
    if (!sr.ok || (repVer != 1 && repVer != 2)) return false;

    uint8_t normalBinding = sr.ReadU8();
    uint8_t texBinding    = sr.ReadU8();
    uint8_t colorBinding  = sr.ReadU8();
    uint8_t bitsPerVertex = sr.ReadU8();
    uint8_t normBitsFac   = sr.ReadU8();
    uint8_t bitsPerTex    = sr.ReadU8();
    uint8_t bitsPerCol    = sr.ReadU8();
    (void)normBitsFac; (void)bitsPerTex; (void)bitsPerCol;
    if (!sr.ok || normalBinding > 3u || texBinding > 3u || colorBinding > 3u || bitsPerVertex > 24u) {
        return false;
    }

    std::vector<int32_t> stripRawRes, stripOffsets;
    if (!DecodeJtInt32CDP(sr, majorVer, JtPredictor::Stride1, stripOffsets, &stripRawRes)) return false;

    size_t minPerPrim = isPolyLine ? 2u : 3u;
    bool okStride1 = ValidateStripOffsets(stripOffsets, minPerPrim);
    std::vector<int32_t> stripLag1;
    UnpackJtResiduals(stripRawRes, JtPredictor::Lag1, stripLag1);
    bool okLag1 = ValidateStripOffsets(stripLag1, minPerPrim);
    if (!okStride1 && !okLag1) return false;
    if (!okStride1 && okLag1) stripOffsets = stripLag1;

    std::vector<Vec3f> stripVerts;

    if (bitsPerVertex == 0u) {
        // Lossless Compressed Raw Vertex Data (ISO 14306 §7.2.2.1.2.6 / §8.1.3.2.2)
        int32_t uncompSize = sr.ReadI32();
        int32_t compSize   = sr.ReadI32();
        if (!sr.ok || uncompSize <= 0 || uncompSize > 64 * 1024 * 1024) return false;

        size_t floatsPerVert = 3u + (normalBinding == 1u ? 3u : 0u) +
                                    (texBinding == 1u ? 2u : 0u) +
                                    (colorBinding == 1u ? 3u : 0u);
        size_t vtxStride = floatsPerVert * 4u;
        if ((static_cast<size_t>(uncompSize) % vtxStride) != 0u) return false;
        size_t totalStripVerts = static_cast<size_t>(uncompSize) / vtxStride;

        if (okStride1 && static_cast<size_t>(stripOffsets.back()) != totalStripVerts &&
            okLag1 && static_cast<size_t>(stripLag1.back()) == totalStripVerts)
        {
            stripOffsets = stripLag1;
        }
        if (static_cast<size_t>(stripOffsets.back()) != totalStripVerts) return false;

        std::vector<uint8_t> rawBuf;
        const uint8_t* vPtr = nullptr;
        if (compSize > 0) {
            if (static_cast<size_t>(compSize) > sr.Remaining()) return false;
            if (!ZipDeflate::InflateZlib(sr.buf + sr.pos, static_cast<size_t>(compSize),
                                         rawBuf, static_cast<size_t>(uncompSize) + 64u) ||
                rawBuf.size() < static_cast<size_t>(uncompSize))
            {
                return false;
            }
            sr.Skip(static_cast<size_t>(compSize));
            vPtr = rawBuf.data();
        } else {
            if (static_cast<size_t>(uncompSize) > sr.Remaining()) return false;
            vPtr = sr.buf + sr.pos;
            sr.Skip(static_cast<size_t>(uncompSize));
        }

        size_t posByteOff = (floatsPerVert - 3u) * 4u;
        stripVerts.resize(totalStripVerts);
        for (size_t v = 0; v < totalStripVerts; ++v) {
            const uint8_t* vp = vPtr + v * vtxStride + posByteOff;
            JtStream vs{ vp, 12u, 0u, isBE, true };
            float x = vs.ReadRawF32(), y = vs.ReadRawF32(), z = vs.ReadRawF32();
            if (!vs.ok) return false;
            stripVerts[v] = { x, y, z };
        }
    } else {
        // Lossy Quantized Raw Vertex Data (PointQuantizerData + 3x Lag1 Int32CDP + optional vertexDataIndices)
        float minC[3] = {}, maxC[3] = {};
        uint8_t nBits[3] = {};
        for (int a = 0; a < 3; ++a) {
            minC[a]  = sr.ReadRawF32();
            maxC[a]  = sr.ReadRawF32();
            nBits[a] = sr.ReadU8();
            if (!sr.ok || nBits[a] > 24u || maxC[a] < minC[a]) return false;
        }
        int32_t uniqueVertCount = sr.ReadI32();
        if (!sr.ok || uniqueVertCount <= 0 || uniqueVertCount > 2000000) return false;

        std::vector<int32_t> qAxis[3];
        for (int a = 0; a < 3; ++a) {
            if (!DecodeJtInt32CDP(sr, majorVer, JtPredictor::Lag1, qAxis[a]) ||
                qAxis[a].size() != static_cast<size_t>(uniqueVertCount))
            {
                return false;
            }
        }

        std::vector<Vec3f> uniqueVerts(static_cast<size_t>(uniqueVertCount));
        for (int32_t i = 0; i < uniqueVertCount; ++i) {
            float xyz[3] = {};
            for (int a = 0; a < 3; ++a) {
                if (nBits[a] == 0u) {
                    xyz[a] = minC[a];
                } else {
                    double maxCode = static_cast<double>((1ULL << nBits[a]) - 1ULL);
                    double t = std::clamp(static_cast<double>(qAxis[a][static_cast<size_t>(i)]) / maxCode, 0.0, 1.0);
                    xyz[a] = static_cast<float>(minC[a] + t * (static_cast<double>(maxC[a]) - static_cast<double>(minC[a])));
                }
            }
            uniqueVerts[static_cast<size_t>(i)] = { xyz[0], xyz[1], xyz[2] };
        }

        // Skip per-vertex quantized normals (Deering normal codec: U8 numBits + I32 count + 4 Int32CDP packets)
        if (normalBinding == 1u && sr.Remaining() >= 5u) {
            uint8_t deeringBits = sr.ReadU8();
            int32_t normCount   = sr.ReadI32();
            (void)deeringBits;
            if (!sr.ok || normCount < 0 || normCount > 2000000) return false;
            std::vector<int32_t> dummy;
            for (int p = 0; p < 4; ++p) {
                if (!DecodeJtInt32CDP(sr, majorVer, JtPredictor::Lag1, dummy)) return false;
            }
        }

        // Read optional vertexDataIndices (maps strip vertex slots to uniqueVerts); if absent, 1:1 mapping
        if (sr.Remaining() > 0u) {
            std::vector<int32_t> idxRaw, vtxIndices;
            size_t idxPos = sr.pos;
            if (DecodeJtInt32CDP(sr, majorVer, JtPredictor::Stride1, vtxIndices, &idxRaw) && !vtxIndices.empty()) {
                auto validIdxVec = [&](const std::vector<int32_t>& iv, const std::vector<int32_t>& st) -> bool {
                    if (iv.size() != static_cast<size_t>(st.back())) return false;
                    for (int32_t vi : iv) if (vi < 0 || vi >= uniqueVertCount) return false;
                    return true;
                };
                bool matched = false;
                for (JtPredictor pr : { JtPredictor::Stride1, JtPredictor::StripIndex, JtPredictor::Lag1, JtPredictor::Null }) {
                    std::vector<int32_t> cand;
                    UnpackJtResiduals(idxRaw, pr, cand);
                    if (okStride1 && validIdxVec(cand, stripOffsets)) { vtxIndices = std::move(cand); matched = true; break; }
                    if (okLag1 && validIdxVec(cand, stripLag1)) { stripOffsets = stripLag1; vtxIndices = std::move(cand); matched = true; break; }
                }
                if (matched) {
                    stripVerts.resize(vtxIndices.size());
                    for (size_t k = 0; k < vtxIndices.size(); ++k) {
                        stripVerts[k] = uniqueVerts[static_cast<size_t>(vtxIndices[k])];
                    }
                }
            } else {
                sr.pos = idxPos;
                sr.ok = true;
            }
        }

        if (stripVerts.empty()) {
            if (okStride1 && static_cast<size_t>(stripOffsets.back()) != uniqueVerts.size() &&
                okLag1 && static_cast<size_t>(stripLag1.back()) == uniqueVerts.size())
            {
                stripOffsets = stripLag1;
            }
            if (static_cast<size_t>(stripOffsets.back()) != uniqueVerts.size()) return false;
            stripVerts = std::move(uniqueVerts);
        }
    }

    if (stripVerts.empty()) return false;
    if (isPolyLine) {
        float r = DetailMeshSciCad::ComputePointCloudSpan(stripVerts) * 0.012f;
        for (size_t s = 0; s + 1u < stripOffsets.size(); ++s) {
            uint32_t a = static_cast<uint32_t>(stripOffsets[s]);
            uint32_t b = static_cast<uint32_t>(stripOffsets[s + 1u]);
            for (uint32_t k = a; k + 1u < b && k + 1u < stripVerts.size(); ++k) {
                DetailMeshSciCad::AddStrut3D(mesh, stripVerts[k], stripVerts[k + 1u], r);
            }
        }
        return !mesh.tris.empty();
    }

    uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), stripVerts.begin(), stripVerts.end());
    for (size_t s = 0; s + 1u < stripOffsets.size(); ++s) {
        uint32_t a = static_cast<uint32_t>(stripOffsets[s]);
        uint32_t b = static_cast<uint32_t>(stripOffsets[s + 1u]);
        for (uint32_t k = a; k + 2u < b && k + 2u < stripVerts.size(); ++k) {
            if ((k - a) & 1u) mesh.tris.push_back({ vBase + k + 1u, vBase + k, vBase + k + 2u, {} });
            else              mesh.tris.push_back({ vBase + k, vBase + k + 1u, vBase + k + 2u, {} });
        }
    }
    return !mesh.tris.empty();
}

inline bool DecompressJtSegmentPayload(
    const uint8_t* rawSegPay,
    size_t rawSegPayLen,
    bool isBE,
    std::vector<uint8_t>& storage,
    const uint8_t*& outPay,
    size_t& outLen)
{
    outPay = rawSegPay;
    outLen = rawSegPayLen;
    if (rawSegPayLen < 9u) return true;

    auto rdU32 = [&](const uint8_t* p) -> uint32_t { return isBE ? DetailMeshSciCad::RdU32BE(p) : DetailMeshSciCad::RdU32LE(p); };
    uint32_t compFlag = rdU32(rawSegPay);
    uint32_t compLen  = rdU32(rawSegPay + 4);
    uint8_t  compAlg  = rawSegPay[8];

    if (compLen < 1u || 8ULL + static_cast<uint64_t>(compLen) > rawSegPayLen) return true;
    const uint8_t* cSrc = rawSegPay + 9;
    size_t cLen = static_cast<size_t>(compLen - 1u);

    // Zlib-compressed segment (JT v8 / v9 / v10: compFlag == 2, compAlg == 2)
    if (compFlag == 2u && compAlg == 2u) {
        if (!ZipDeflate::InflateZlib(cSrc, cLen, storage, 64 * 1024 * 1024)) return false;
        outPay = storage.data();
        outLen = storage.size();
        return true;
    }

    // LZMA / XZ-compressed segment (JT v10.x: compFlag == 3 or 2, compAlg == 3)
    if ((compFlag == 3u || compFlag == 2u) && compAlg == 3u) {
        if (cLen >= 12u && cSrc[0] == 0xFDu && std::memcmp(cSrc + 1, "7zXZ\0", 5) == 0) {
            if (!StreamCodecs::DecompressXzStream(cSrc, cLen, storage, 64 * 1024 * 1024)) return false;
        } else if (cLen >= 13u && cSrc[0] < 225u) {
            uint32_t dictSz = DetailMeshSciCad::RdU32LE(cSrc + 1);
            uint64_t unpSz  = DetailMeshSciCad::RdU64LE(cSrc + 5);
            bool known = (unpSz != 0xFFFFFFFFFFFFFFFFULL && unpSz <= 64ULL * 1024ULL * 1024ULL);
            if (!StreamCodecs::DecompressLzma1(cSrc[0], dictSz, cSrc + 13, cLen - 13u,
                                               storage, known ? unpSz : 0ULL, known, 64 * 1024 * 1024))
            {
                // Retry as 5-byte [props][dictSize] headerless LZMA1 stream
                if (!StreamCodecs::DecompressLzma1(cSrc[0], dictSz, cSrc + 5, cLen - 5u,
                                                   storage, 0ULL, false, 64 * 1024 * 1024))
                {
                    return false;
                }
            }
        } else if (cLen >= 6u && cSrc[0] < 225u) {
            uint32_t dictSz = DetailMeshSciCad::RdU32LE(cSrc + 1);
            if (!StreamCodecs::DecompressLzma1(cSrc[0], dictSz, cSrc + 5, cLen - 5u,
                                               storage, 0ULL, false, 64 * 1024 * 1024))
            {
                return false;
            }
        } else {
            return false;
        }
        outPay = storage.data();
        outLen = storage.size();
        return true;
    }

    // Explicit uncompressed Data Segment wrapper (compFlag == 1, compAlg == 1)
    if (compFlag == 1u && compAlg == 1u && 8u + static_cast<size_t>(compLen) == rawSegPayLen && cLen >= 21u) {
        outPay = cSrc;
        outLen = cLen;
    }
    return true;
}

inline bool TryParseJtIso14306Elements(
    const uint8_t* payload,
    size_t payLen,
    bool isBE,
    int majorVer,
    LoadedMesh3D& mesh)
{
    size_t pos = 0;
    bool parsedAny = false;
    while (pos + 21u <= payLen) {
        JtStream hdr{ payload, payLen, pos, isBE, true };
        int32_t elemLen = hdr.ReadI32();
        if (!hdr.ok || elemLen < 17 || pos + 4u + static_cast<size_t>(elemLen) > payLen) break;

        size_t elemEnd = pos + 4u + static_cast<size_t>(elemLen);
        JtGuid guid = hdr.ReadGuid();
        uint8_t objBaseType = hdr.ReadU8();
        (void)objBaseType;
        if (!hdr.ok) break;

        if (guid.Equals(0xffffffffu, 0xffffu, 0xffffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu)) {
            break; // End-Of-Elements marker
        }

        // ISO 14306 TriStripSetShapeLODElement {0x10dd10ab, 0x2ac8, 0x11d1, {0x9b,0x6b,0x00,0x80,0xc7,0xbb,0x59,0x97}}
        // and PolyLineSetShapeLODElement       {0x10dd10a1, 0x2ac8, 0x11d1, {0x9b,0x6b,0x00,0x80,0xc7,0xbb,0x59,0x97}}
        bool isTriStrip = guid.Equals(0x10dd10abu, 0x2ac8u, 0x11d1u, 0x9bu, 0x6bu, 0x00u, 0x80u, 0xc7u, 0xbbu, 0x59u, 0x97u);
        bool isPolyLine = guid.Equals(0x10dd10a1u, 0x2ac8u, 0x11d1u, 0x9bu, 0x6bu, 0x00u, 0x80u, 0xc7u, 0xbbu, 0x59u, 0x97u);

        if (isTriStrip || isPolyLine) {
            size_t bodyPos = hdr.pos; // pos + 21
            // Candidate header offsets from bodyPos to VertexBasedShapeCompressedRepData:
            //   1. JT v8: [I16 vtxLodVer=1][U32 bindingAttrs][I16 triStripVer=1] -> +8
            //   2. JT v9 (no objId, U64 bindings): [I16 vtxLodVer=1][U64 vertexBindings][I16 triStripVer=1] -> +12
            //   3. JT v9 (with I32 objId + U32/U64 bindings): +12 or +16
            //   4. Direct VertexBasedShapeCompressedRepData right after Element Header (+0 or +4 with I32 objId)
            //   5. JT v10 (I32 objId + U8 versions + U64 vertexBindings): +14 or +10
            struct CandLayout { size_t skip; bool byteVer; };
            static const CandLayout kCands[] = {
                { 8u,  false },
                { 12u, false },
                { 16u, false },
                { 0u,  false },
                { 4u,  false },
                { 14u, true  },
                { 10u, true  },
                { 0u,  true  },
                { 4u,  true  }
            };
            for (const auto& c : kCands) {
                if (bodyPos + c.skip < elemEnd &&
                    ParseVertexBasedShapeCompressedRepAt(payload, elemEnd, bodyPos + c.skip,
                                                         isBE, majorVer, c.byteVer, isPolyLine, mesh))
                {
                    parsedAny = true;
                    break;
                }
            }
        }

        pos = elemEnd;
    }
    return parsedAny;
}

} // namespace DetailJt14306


inline bool ParseSiemensJt(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    // #51. Siemens JT Open (.jt — 80-char header + byteOrder + TOC + uncompressed TriStripSet/Shape segments)
    if (ext == L".jt" || (size >= 89 && sv.substr(0, 8) == "Version " && sv.substr(0, 80).find("JT") != std::string_view::npos)) {
        if (size < 89 || sv.substr(0, 8) != "Version " || sv.substr(0, 80).find("JT") == std::string_view::npos) {
            return false;
        }
        uint8_t byteOrder = data[80];
        if (byteOrder > 1) return false;
        bool isBE = (byteOrder == 1);
        auto rdU32 = [&](const uint8_t* p) -> uint32_t { return isBE ? DetailMeshSciCad::RdU32BE(p) : DetailMeshSciCad::RdU32LE(p); };
        auto rdU64 = [&](const uint8_t* p) -> uint64_t { return isBE ? DetailMeshSciCad::RdU64BE(p) : DetailMeshSciCad::RdU64LE(p); };
        auto rdF32 = [&](const uint8_t* p) -> float    { return isBE ? DetailMeshSciCad::RdF32BE(p) : DetailMeshSciCad::RdF32LE(p); };

        int majorVer = std::atoi(std::string(sv.substr(8, 4)).c_str());
        uint64_t tocOffset = (majorVer >= 10 && size >= 93) ? rdU64(data + 85) : static_cast<uint64_t>(rdU32(data + 85));
        if (tocOffset < 89 || tocOffset + 4 > size) return false;

        uint32_t entryCount = rdU32(data + static_cast<size_t>(tocOffset));
        size_t entryStride = (majorVer >= 10) ? 32 : 28;
        if (entryCount == 0 || entryCount > 65536 || tocOffset + 4 + static_cast<uint64_t>(entryCount) * entryStride > size) {
            return false;
        }
        mesh.formatName = L"Siemens JT Open CAD Assembly (.jt)";

        for (uint32_t e = 0; e < entryCount; ++e) {
            const uint8_t* ep = data + static_cast<size_t>(tocOffset) + 4 + static_cast<size_t>(e) * entryStride;
            uint64_t segOff = (majorVer >= 10) ? rdU64(ep + 16) : static_cast<uint64_t>(rdU32(ep + 16));
            uint32_t segLen = (majorVer >= 10) ? rdU32(ep + 24) : rdU32(ep + 20);
            uint32_t segAttr= (majorVer >= 10) ? rdU32(ep + 28) : rdU32(ep + 24);
            uint32_t segType = (segAttr >> 24) & 0xFFu;
            // Shape / Tri-Strip Set / PolyLine / Wireframe LOD segment types: 4, 6..16, 18
            bool isShapeSeg = (segType == 4u || (segType >= 6u && segType <= 16u) || segType == 18u);
            if (!isShapeSeg || segLen < 28u || segOff + segLen > size) continue;

            const uint8_t* sp = data + static_cast<size_t>(segOff);
            // Verify 24-byte JT Segment Header GUID matches TOC entry GUID
            if (std::memcmp(sp, ep, 16) != 0) continue;

            std::vector<uint8_t> inflated;
            const uint8_t* payload = nullptr;
            size_t payLen = 0;
            if (!DetailJt14306::DecompressJtSegmentPayload(sp + 24, segLen - 24u, isBE, inflated, payload, payLen)) {
                continue;
            }

            // 1. Genuine ISO 14306 Logical Element Header + TriStripSetShapeLODElement / Int32CDP decoding
            if (DetailJt14306::TryParseJtIso14306Elements(payload, payLen, isBE, majorVer, mesh)) {
                continue;
            }

            // 2. Legacy uncompressed fallback: [uint32 numStrips][uint32 stripOffsets[numStrips+1]][float3 verts[sLast]]
            if (payLen >= 20u) {
                uint32_t numStrips = rdU32(payload);
                if (numStrips >= 1u && numStrips <= 65536u && 4u + static_cast<size_t>(numStrips + 1u) * 4u + 12u <= payLen) {
                    size_t offTable = 4u;
                    uint32_t s0 = rdU32(payload + offTable);
                    uint32_t sLast = rdU32(payload + offTable + static_cast<size_t>(numStrips) * 4u);
                    size_t afterStrips = offTable + static_cast<size_t>(numStrips + 1u) * 4u;
                    if (s0 == 0u && sLast >= numStrips * 3u && sLast <= 1000000u &&
                        afterStrips + static_cast<size_t>(sLast) * 12u <= payLen)
                    {
                        bool monotonic = true;
                        for (uint32_t s = 0; s < numStrips; ++s) {
                            uint32_t a = rdU32(payload + offTable + static_cast<size_t>(s) * 4u);
                            uint32_t b = rdU32(payload + offTable + static_cast<size_t>(s + 1u) * 4u);
                            if (b < a || (b - a) < 3u) { monotonic = false; break; }
                        }
                        bool finiteVerts = monotonic;
                        for (uint32_t v = 0; finiteVerts && v < sLast; ++v) {
                            const uint8_t* vp = payload + afterStrips + static_cast<size_t>(v) * 12u;
                            float fx = isBE ? ::Explorer2::ByteIO::RdF32BE(vp + 0) : ::Explorer2::ByteIO::RdF32LE(vp + 0);
                            float fy = isBE ? ::Explorer2::ByteIO::RdF32BE(vp + 4) : ::Explorer2::ByteIO::RdF32LE(vp + 4);
                            float fz = isBE ? ::Explorer2::ByteIO::RdF32BE(vp + 8) : ::Explorer2::ByteIO::RdF32LE(vp + 8);
                            if (!std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(fz) ||
                                std::fabs(fx) > 1e12f || std::fabs(fy) > 1e12f || std::fabs(fz) > 1e12f)
                            {
                                finiteVerts = false;
                            }
                        }
                        if (finiteVerts) {
                            uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                            for (uint32_t v = 0; v < sLast; ++v) {
                                const uint8_t* vp = payload + afterStrips + static_cast<size_t>(v) * 12u;
                                mesh.vertices.push_back({ rdF32(vp + 0), rdF32(vp + 4), rdF32(vp + 8) });
                            }
                            for (uint32_t s = 0; s < numStrips; ++s) {
                                uint32_t a = rdU32(payload + offTable + static_cast<size_t>(s) * 4u);
                                uint32_t b = rdU32(payload + offTable + static_cast<size_t>(s + 1u) * 4u);
                                for (uint32_t k = a; k + 2u < b; ++k) {
                                    if ((k - a) & 1u) mesh.tris.push_back({ vBase + k + 1u, vBase + k, vBase + k + 2u, {} });
                                    else              mesh.tris.push_back({ vBase + k, vBase + k + 1u, vBase + k + 2u, {} });
                                }
                            }
                        }
                    }
                }
            }
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }
    return false;
}

} // namespace Formats
