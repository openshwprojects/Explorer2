#pragma once
// PentaxPef.h — Pentax PEF Lossless Huffman & MakerNote CFA RAW Decoder (.pef)

namespace Formats {

namespace DetailVectorAndRawImg {

// ============================================================================
// 6. Pentax PEF Lossless Huffman Sensor Unpacker (Compression == 65535)
// ============================================================================
inline void ParsePentaxMakerNote(
    const uint8_t* tiffData, size_t tiffSize,
    const uint8_t* mn, size_t mnLen,
    bool parentBe, PentaxMetaParams& out)
{
    if (!mn || mnLen < 10) return;
    size_t ifdOff = 0;
    bool be = parentBe;
    if (mnLen >= 8 && std::memcmp(mn, "AOC\0", 4) == 0) {
        if (mn[4] == 'M' && mn[5] == 'M') be = true;
        else if (mn[4] == 'I' && mn[5] == 'I') be = false;
        ifdOff = 6;
    } else if (mnLen >= 12 && std::memcmp(mn, "PENTAX \0", 8) == 0) {
        if (mn[8] == 'M' && mn[9] == 'M') be = true;
        else if (mn[8] == 'I' && mn[9] == 'I') be = false;
        ifdOff = 10;
    } else {
        return;
    }
    if (ifdOff + 2u > mnLen) return;
    uint16_t nEnt = Rd16(mn + ifdOff, be);
    if (nEnt == 0 || nEnt > 512 || ifdOff + 2u + static_cast<size_t>(nEnt) * 12u > mnLen) return;

    for (uint16_t e = 0; e < nEnt; ++e) {
        const uint8_t* ent = mn + ifdOff + 2u + static_cast<size_t>(e) * 12u;
        uint16_t tag = Rd16(ent, be);
        if (tag != 0x0220u) continue; // Pentax Huffman Table tag

        uint16_t ftype = Rd16(ent + 2, be);
        uint32_t fcnt  = Rd32(ent + 4, be);
        uint32_t elemSz = TiffTypeElemSize(ftype);
        if (elemSz == 0 || fcnt == 0) continue;
        uint64_t totalBytes = static_cast<uint64_t>(fcnt) * elemSz;
        if (totalBytes < 16 || totalBytes > 4096) continue;

        const uint8_t* hp = nullptr;
        uint32_t rawOff = Rd32(ent + 8, be);
        if (tiffData && rawOff >= 8u && static_cast<uint64_t>(rawOff) + totalBytes <= tiffSize) {
            hp = tiffData + rawOff;
        } else if (static_cast<uint64_t>(rawOff) + totalBytes <= mnLen) {
            hp = mn + rawOff;
        }
        if (!hp) continue;

        uint32_t depth = static_cast<uint32_t>(Rd16(hp, be)) + 12u;
        if (depth == 0 || depth > 15u || 14u + static_cast<size_t>(depth) * 3u > totalBytes) continue;

        const uint8_t* sp = hp + 14u; // Skip depth(2) + 12 header bytes
        uint32_t v0[16] = {}, v1[16] = {}, v2[16] = {};
        uint8_t newCounts[17] = {};
        bool ok = true;
        for (uint32_t i = 0; i < depth; ++i) {
            v0[i] = Rd16(sp + i * 2u, be);
        }
        sp += static_cast<size_t>(depth) * 2u;
        for (uint32_t i = 0; i < depth; ++i) {
            v1[i] = sp[i];
            if (v1[i] == 0u || v1[i] > 12u) { ok = false; break; }
            v2[i] = v0[i] >> (12u - v1[i]);
            ++newCounts[v1[i]];
        }
        if (!ok) continue;

        uint8_t newSyms[16] = {};
        for (uint32_t i = 0; i < depth; ++i) {
            uint32_t smVal = 0x0FFFFFFFu;
            uint32_t smNum = 0xFFu;
            for (uint32_t j = 0; j < depth; ++j) {
                if (v2[j] <= smVal) {
                    smNum = j;
                    smVal = v2[j];
                }
            }
            if (smNum >= 16u) { ok = false; break; }
            newSyms[i] = static_cast<uint8_t>(smNum);
            v2[smNum] = 0xFFFFFFFFu;
        }
        if (ok) {
            std::memcpy(out.counts, &newCounts[1], 16u);
            std::memcpy(out.syms, newSyms, 16u);
            out.symCount = depth;
            out.hasCustomTree = true;
            return;
        }
    }
}

inline bool UnpackPentaxPefHuffmanSensor(
    const uint8_t* src, size_t srcLen,
    uint32_t width, uint32_t height, uint16_t bps,
    const PentaxMetaParams& pm,
    std::vector<uint16_t>& outSamples)
{
    if (!src || srcLen < 4 || width < 2 || (width & 1u) != 0u || height == 0 || bps < 8 || bps > 16) return false;

    int32_t minCode[17] = {}, maxCode[17] = {}, valPtr[17] = {};
    int32_t code = 0, symCursor = 0;
    for (int len = 1; len <= 16; ++len) {
        int cnt = pm.counts[len - 1];
        if (cnt == 0) {
            maxCode[len] = -1;
        } else {
            valPtr[len] = symCursor;
            minCode[len] = code;
            symCursor = std::min(16, symCursor + cnt);
            code += cnt - 1;
            maxCode[len] = code;
            ++code;
        }
        code <<= 1;
    }

    size_t bytePos = 0;
    uint32_t bitBuf = 0;
    int bitCnt = 0;
    bool eof = false;

    auto getBits = [&](int n) -> int32_t {
        if (n == 0) return 0;
        while (bitCnt < n) {
            if (bytePos >= srcLen) {
                eof = true;
                return 0;
            }
            bitBuf = (bitBuf << 8u) | src[bytePos++];
            bitCnt += 8;
        }
        bitCnt -= n;
        return static_cast<int32_t>((bitBuf >> bitCnt) & ((1u << n) - 1u));
    };

    auto decodePentaxDiff = [&]() -> int32_t {
        int32_t c = 0;
        int ssss = -1;
        for (int len = 1; len <= 16; ++len) {
            int32_t b = getBits(1);
            if (eof) return 0;
            c = (c << 1) | b;
            if (c <= maxCode[len]) {
                int idx = valPtr[len] + (c - minCode[len]);
                if (idx >= 0 && idx < static_cast<int>(pm.symCount)) ssss = pm.syms[idx];
                break;
            }
        }
        if (ssss < 0 || ssss > 16) { eof = true; return 0; }
        if (ssss == 0) return 0;
        if (ssss == 16) return -32768;
        int32_t v = getBits(ssss);
        if ((v & (1 << (ssss - 1))) == 0) {
            v -= (1 << ssss) - 1;
        }
        return v;
    };

    const int32_t maxVal = (1 << bps) - 1;
    outSamples.assign(static_cast<size_t>(width) * height, 0u);

    for (uint32_t row = 0; row < height; ++row) {
        int32_t pred[2] = { 0, 0 };
        if (row >= 2u) {
            pred[0] = outSamples[static_cast<size_t>(row - 2u) * width + 0u];
            pred[1] = outSamples[static_cast<size_t>(row - 2u) * width + 1u];
        }
        for (uint32_t col = 0; col < width; ++col) {
            int32_t diff = decodePentaxDiff();
            if (eof) return false;
            pred[col & 1u] += diff;
            if (pred[col & 1u] < 0 || pred[col & 1u] > 65535) return false;
            outSamples[static_cast<size_t>(row) * width + col] =
                static_cast<uint16_t>(std::clamp<int32_t>(pred[col & 1u], 0, maxVal));
        }
    }
    return true;
}

} // namespace DetailVectorAndRawImg

inline bool DecodePentaxPef(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return DetailVectorAndRawImg::ParseTiffRawIfds(data, size, L"Pentax PEF RAW (.pef)", outInfo);
}

} // namespace Formats
