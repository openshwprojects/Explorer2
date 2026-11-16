#pragma once
// ExtendedImages200.h — 20 Camera RAW, Vector/Print, Creative Design, Embroidery & Retro Art Decoders (#1–#20)
// Covers:
//   Camera RAW (#1–#9): .cr2, .cr3, .nef, .arw, .dng, .raf, .orf, .rw2, .pef
//   Print/Vector/Design (#10–#17): .eps, .ai, .cdr, .afphoto/.afdesign/.afpub, .emf/.wmf, .cgm, .abr, .pat
//   Embroidery & Terminal Art (#18–#20): .dst, .pes, .ans/.sixel

namespace Formats {

namespace DetailImg200 {

inline uint16_t Rd16(const uint8_t* p, bool be) {
    return be ? DetailImg100::RdU16BE(p) : DetailImg100::RdU16LE(p);
}

inline uint32_t Rd32(const uint8_t* p, bool be) {
    return be ? DetailImg100::RdU32BE(p) : DetailImg100::RdU32LE(p);
}

inline int16_t RdS16(const uint8_t* p, bool be) {
    return static_cast<int16_t>(Rd16(p, be));
}

inline int32_t RdS32(const uint8_t* p, bool be) {
    return static_cast<int32_t>(Rd32(p, be));
}

inline bool CallInflateZlib(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, size_t maxOut = 64u * 1024u * 1024u) {
    return ZipDeflate::InflateZlib(src, srcLen, out, maxOut);
}

inline bool CallInflateRawDeflate(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, size_t maxOut = 64u * 1024u * 1024u) {
    return ZipDeflate::InflateRaw(src, srcLen, out, maxOut);
}

inline bool InflateZlibOrRaw(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, size_t maxOut = 64u * 1024u * 1024u) {
    if (srcLen == 0) return false;
    out.clear();
    if (CallInflateZlib(src, srcLen, out, maxOut) && !out.empty()) return true;
    out.clear();
    if (CallInflateRawDeflate(src, srcLen, out, maxOut) && !out.empty()) return true;
    return false;
}

inline void CollectZipEntriesViaApi(const uint8_t* data, size_t size, std::vector<std::string>& outNames) {
    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (ZipDeflate::ListZipEntriesFromMemory(data, size, entries)) {
        for (const auto& item : entries) {
            if (!item.isDirectory && !item.pathInArchive.empty()) {
                outNames.push_back(PathUtils::WideToUtf8(item.pathInArchive));
            }
        }
    }
}

inline void CollectAllZipEntryNames(const uint8_t* data, size_t size, std::vector<std::string>& outNames) {
    outNames.clear();
    CollectZipEntriesViaApi(data, size, outNames);
    if (!outNames.empty()) return;

    // Fallback Central Directory (0x06054B50 -> 0x02014B50) & Local Header (0x04034B50) walker
    if (size >= 22) {
        size_t minSearch = (size > 65557u) ? (size - 65557u) : 0u;
        for (size_t i = size - 22; ; --i) {
            if (DetailImg100::RdU32LE(data + i) == 0x06054B50u) {
                uint16_t cdCount = DetailImg100::RdU16LE(data + i + 10);
                uint32_t cdOff   = DetailImg100::RdU32LE(data + i + 16);
                size_t p = cdOff;
                for (uint16_t c = 0; c < cdCount && p + 46 <= size; ++c) {
                    if (DetailImg100::RdU32LE(data + p) != 0x02014B50u) break;
                    uint16_t fnLen = DetailImg100::RdU16LE(data + p + 28);
                    uint16_t exLen = DetailImg100::RdU16LE(data + p + 30);
                    uint16_t cmLen = DetailImg100::RdU16LE(data + p + 32);
                    if (p + 46u + fnLen > size) break;
                    outNames.emplace_back(reinterpret_cast<const char*>(data + p + 46), fnLen);
                    p += 46u + fnLen + exLen + cmLen;
                }
                break;
            }
            if (i == minSearch) break;
        }
    }
    if (outNames.empty()) {
        size_t p = 0;
        while (p + 30 <= size && DetailImg100::RdU32LE(data + p) == 0x04034B50u) {
            uint16_t flags = DetailImg100::RdU16LE(data + p + 6);
            uint32_t cSize = DetailImg100::RdU32LE(data + p + 18);
            uint16_t fnLen = DetailImg100::RdU16LE(data + p + 26);
            uint16_t exLen = DetailImg100::RdU16LE(data + p + 28);
            if (p + 30u + fnLen > size) break;
            outNames.emplace_back(reinterpret_cast<const char*>(data + p + 30), fnLen);
            if ((flags & 0x08u) != 0 || p + 30u + fnLen + exLen + cSize > size) break;
            p += 30u + fnLen + exLen + cSize;
        }
    }
}

// Anti-aliased / thick Bresenham line helper on BGRA canvas
inline void DrawLineBGRA(
    std::vector<uint8_t>& bgra, uint32_t w, uint32_t h,
    int x0, int y0, int x1, int y1,
    uint8_t r, uint8_t g, uint8_t b, int thickness = 1)
{
    int dx = std::abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    int rad = std::max(0, (thickness - 1) / 2);
    int steps = 0;
    const int maxSteps = static_cast<int>(w + h) * 4 + 64;
    while (steps++ < maxSteps) {
        for (int oy = -rad; oy <= rad; ++oy) {
            int py = y0 + oy;
            if (py < 0 || py >= static_cast<int>(h)) continue;
            for (int ox = -rad; ox <= rad; ++ox) {
                int px = x0 + ox;
                if (px < 0 || px >= static_cast<int>(w)) continue;
                uint8_t* dst = &bgra[(static_cast<size_t>(py) * w + static_cast<size_t>(px)) * 4];
                dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255;
            }
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

inline void FillRectBGRA(
    std::vector<uint8_t>& bgra, uint32_t w, uint32_t h,
    int x0, int y0, int x1, int y1,
    uint8_t r, uint8_t g, uint8_t b)
{
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    x0 = std::clamp(x0, 0, static_cast<int>(w));
    x1 = std::clamp(x1, 0, static_cast<int>(w));
    y0 = std::clamp(y0, 0, static_cast<int>(h));
    y1 = std::clamp(y1, 0, static_cast<int>(h));
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            uint8_t* dst = &bgra[(static_cast<size_t>(y) * w + static_cast<size_t>(x)) * 4];
            dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255;
        }
    }
}

struct Pt2f {
    float x = 0.0f;
    float y = 0.0f;
};

// Scanline polygon fill supporting both Even-Odd and Non-Zero Winding rules
inline bool FillPolygonRingsBGRA(
    std::vector<uint8_t>& bgra, uint32_t w, uint32_t h,
    const std::vector<std::vector<Pt2f>>& rings,
    bool evenOdd,
    uint8_t r, uint8_t g, uint8_t b)
{
    float minYf = 1e30f, maxYf = -1e30f;
    size_t validRings = 0;
    for (const auto& ring : rings) {
        if (ring.size() < 3) continue;
        ++validRings;
        for (const auto& p : ring) {
            minYf = std::min(minYf, p.y);
            maxYf = std::max(maxYf, p.y);
        }
    }
    if (validRings == 0 || minYf > maxYf) return false;

    int yStart = std::clamp(static_cast<int>(std::floor(minYf)), 0, static_cast<int>(h) - 1);
    int yEnd   = std::clamp(static_cast<int>(std::ceil(maxYf)),  0, static_cast<int>(h) - 1);

    struct EdgeHit {
        float x = 0.0f;
        int dir = 0;
    };
    std::vector<EdgeHit> hits;
    bool painted = false;

    for (int y = yStart; y <= yEnd; ++y) {
        float sy = static_cast<float>(y) + 0.5f;
        hits.clear();
        for (const auto& ring : rings) {
            if (ring.size() < 3) continue;
            size_t n = ring.size();
            for (size_t i = 0; i < n; ++i) {
                const Pt2f& a = ring[i];
                const Pt2f& c = ring[(i + 1) % n];
                if ((a.y <= sy && c.y > sy) || (c.y <= sy && a.y > sy)) {
                    float dy = c.y - a.y;
                    if (std::abs(dy) < 1e-9f) continue;
                    float t = (sy - a.y) / dy;
                    float ix = a.x + t * (c.x - a.x);
                    hits.push_back({ ix, (c.y > a.y) ? 1 : -1 });
                }
            }
        }
        if (hits.size() < 2) continue;
        std::sort(hits.begin(), hits.end(), [](const EdgeHit& a, const EdgeHit& c) { return a.x < c.x; });

        if (evenOdd) {
            for (size_t i = 0; i + 1 < hits.size(); i += 2) {
                int x0 = std::clamp(static_cast<int>(std::round(hits[i].x)),     0, static_cast<int>(w));
                int x1 = std::clamp(static_cast<int>(std::round(hits[i + 1].x)), 0, static_cast<int>(w));
                for (int x = x0; x < x1; ++x) {
                    uint8_t* dst = &bgra[(static_cast<size_t>(y) * w + static_cast<size_t>(x)) * 4];
                    dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255;
                    painted = true;
                }
            }
        } else {
            int winding = 0;
            float spanStart = 0.0f;
            for (const auto& hit : hits) {
                int prev = winding;
                winding += hit.dir;
                if (prev == 0 && winding != 0) {
                    spanStart = hit.x;
                } else if (prev != 0 && winding == 0) {
                    int x0 = std::clamp(static_cast<int>(std::round(spanStart)), 0, static_cast<int>(w));
                    int x1 = std::clamp(static_cast<int>(std::round(hit.x)),     0, static_cast<int>(w));
                    for (int x = x0; x < x1; ++x) {
                        uint8_t* dst = &bgra[(static_cast<size_t>(y) * w + static_cast<size_t>(x)) * 4];
                        dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255;
                        painted = true;
                    }
                }
            }
        }
    }
    return painted;
}

// Scans a memory buffer for the largest valid embedded PNG (89 50 4E 47 ... IEND) or JPEG (FF D8 FF ... SOF ... FF D9)
// Every candidate is strictly validated via WIC so random FF D8 FF bytes inside compressed data never false-match.
inline bool FindAndDecodeBestEmbeddedRaster(
    const uint8_t* data, size_t size,
    const std::wstring& fmtLabel,
    ExtendedImageInfo& outInfo,
    size_t startOffset = 0)
{
    ExtendedImageInfo bestCandidate;
    uint64_t bestArea = 0;
    size_t bestByteLen = 0;

    // 1. Scan for PNG signatures
    for (size_t i = startOffset; i + 24 < size; ++i) {
        if (data[i] == 0x89 && data[i + 1] == 'P' && data[i + 2] == 'N' && data[i + 3] == 'G' &&
            data[i + 4] == 0x0D && data[i + 5] == 0x0A && data[i + 6] == 0x1A && data[i + 7] == 0x0A)
        {
            size_t p = i + 8;
            bool validChunks = false;
            while (p + 12 <= size) {
                uint32_t cLen = DetailImg100::RdU32BE(data + p);
                if (cLen > size - p - 12) break;
                bool isIend = (std::memcmp(data + p + 4, "IEND", 4) == 0);
                p += 12 + static_cast<size_t>(cLen);
                if (isIend) { validChunks = true; break; }
            }
            if (validChunks && (p - i) >= 32) {
                ExtendedImageInfo cand;
                if (DetailImg100::DecodeEmbeddedWicBuffer(data + i, p - i, fmtLabel, cand) &&
                    cand.valid && cand.width > 0 && cand.height > 0 && !cand.bgraPixels.empty())
                {
                    uint64_t area = static_cast<uint64_t>(cand.width) * cand.height;
                    if (area > bestArea || (area == bestArea && (p - i) > bestByteLen)) {
                        bestArea = area;
                        bestByteLen = p - i;
                        bestCandidate = std::move(cand);
                    }
                }
            }
            if (p > i + 8) i = p - 1;
        }
    }

    // 2. Scan for JPEG streams (require valid SOF0/SOF1/SOF2 + SOS + EOI + WIC decode!)
    for (size_t i = startOffset; i + 16 < size; ++i) {
        if (data[i] == 0xFF && data[i + 1] == 0xD8 && data[i + 2] == 0xFF &&
            (data[i + 3] == 0xE0 || data[i + 3] == 0xE1 || data[i + 3] == 0xE2 || data[i + 3] == 0xDB ||
             data[i + 3] == 0xC0 || data[i + 3] == 0xC1 || data[i + 3] == 0xC2 || data[i + 3] == 0xC4 ||
             data[i + 3] == 0xEE || data[i + 3] == 0xFE))
        {
            size_t p = i + 2;
            size_t endJpg = 0;
            bool sawSof = false;
            while (p + 2 <= size) {
                if (data[p] != 0xFF) { ++p; continue; }
                while (p < size && data[p] == 0xFF) ++p;
                if (p >= size) break;
                uint8_t marker = data[p++];
                if (marker == 0xD9) { endJpg = p; break; }
                if (marker == 0x00 || (marker >= 0xD0 && marker <= 0xD8)) continue;
                if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2 || marker == 0xC9 || marker == 0xCA) {
                    sawSof = true;
                }
                if (p + 2 > size) break;
                uint16_t segLen = DetailImg100::RdU16BE(data + p);
                if (segLen < 2 || p + segLen > size) break;
                p += segLen;
                if (marker == 0xDA) {
                    if (!sawSof) break;
                    while (p + 1 < size) {
                        if (data[p] == 0xFF) {
                            uint8_t nx = data[p + 1];
                            if (nx == 0xD9) { endJpg = p + 2; break; }
                            if (nx != 0x00 && !(nx >= 0xD0 && nx <= 0xD7) && nx != 0xFF) {
                                break;
                            }
                            p += 2;
                        } else {
                            ++p;
                        }
                    }
                    if (endJpg != 0) break;
                }
            }
            if (sawSof && endJpg > i + 32) {
                size_t jpgLen = endJpg - i;
                ExtendedImageInfo cand;
                if (DetailImg100::DecodeEmbeddedWicBuffer(data + i, jpgLen, fmtLabel, cand) &&
                    cand.valid && cand.width > 0 && cand.height > 0 && !cand.bgraPixels.empty())
                {
                    uint64_t area = static_cast<uint64_t>(cand.width) * cand.height;
                    if (area > bestArea || (area == bestArea && jpgLen > bestByteLen)) {
                        bestArea = area;
                        bestByteLen = jpgLen;
                        bestCandidate = std::move(cand);
                    }
                }
                i = endJpg - 1;
            }
        }
    }

    if (bestCandidate.valid) {
        outInfo = std::move(bestCandidate);
        return true;
    }
    return false;
}

// ITU-T T.81 Annex H Lossless JPEG (SOF3 = 0xFFC3) Decoder
struct LosslessJpegResult {
    uint32_t width = 0;      // SOF3 X
    uint32_t height = 0;     // SOF3 Y
    uint32_t components = 0; // SOF3 Nf (1..4)
    uint16_t precision = 0;  // SOF3 P (2..16)
    bool isLossyJpeg = false;
    std::vector<uint16_t> samples; // Interleaved [Y][X][Nf]
};

inline bool DecodeLosslessJpegSOF3(const uint8_t* data, size_t size, LosslessJpegResult& out) {
    out = LosslessJpegResult{};
    if (size < 16 || data[0] != 0xFF || data[1] != 0xD8) return false;

    struct HuffTable {
        bool valid = false;
        int32_t minCode[17] = {};
        int32_t maxCode[17] = {};
        int32_t valPtr[17]  = {};
        uint8_t syms[256]   = {};
    };
    HuffTable huff[4];

    struct CompInfo {
        uint8_t id = 0;
        uint8_t hSamp = 1;
        uint8_t vSamp = 1;
        uint8_t td = 0;
    };
    CompInfo comps[4] = {};

    bool hasSof3 = false;
    uint16_t restartInterval = 0;
    uint8_t predictor = 1;
    uint8_t pointTransform = 0;
    size_t scanPos = 0;

    size_t p = 2;
    while (p + 2 <= size) {
        if (data[p] != 0xFF) { ++p; continue; }
        while (p < size && data[p] == 0xFF) ++p;
        if (p >= size) break;
        uint8_t marker = data[p++];
        if (marker == 0xD8 || marker == 0x00 || (marker >= 0xD0 && marker <= 0xD7)) continue;
        if (marker == 0xD9) break;
        if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2 || marker == 0xC9 || marker == 0xCA) {
            out.isLossyJpeg = true;
            return false;
        }
        if (p + 2 > size) return false;
        uint16_t segLen = DetailImg100::RdU16BE(data + p);
        if (segLen < 2 || p + segLen > size) return false;
        const uint8_t* seg = data + p + 2;
        size_t payloadLen = segLen - 2;

        if (marker == 0xC4) { // DHT
            size_t q = 0;
            while (q + 17 <= payloadLen) {
                uint8_t tcTh = seg[q++];
                uint8_t th = tcTh & 0x0Fu;
                if (th >= 4) return false;
                uint32_t totalSyms = 0;
                uint8_t counts[16];
                for (int i = 0; i < 16; ++i) {
                    counts[i] = seg[q + i];
                    totalSyms += counts[i];
                }
                q += 16;
                if (totalSyms > 256 || q + totalSyms > payloadLen) return false;

                HuffTable& ht = huff[th];
                ht = HuffTable{};
                std::memcpy(ht.syms, seg + q, totalSyms);
                q += totalSyms;

                int32_t code = 0;
                int32_t symIdx = 0;
                for (int len = 1; len <= 16; ++len) {
                    if (counts[len - 1] == 0) {
                        ht.maxCode[len] = -1;
                    } else {
                        ht.valPtr[len] = symIdx;
                        ht.minCode[len] = code;
                        code += static_cast<int32_t>(counts[len - 1]) - 1;
                        ht.maxCode[len] = code;
                        symIdx += counts[len - 1];
                        ++code;
                    }
                    code <<= 1;
                }
                ht.valid = true;
            }
        } else if (marker == 0xDD && payloadLen >= 2) { // DRI
            restartInterval = DetailImg100::RdU16BE(seg);
        } else if (marker == 0xC3) { // SOF3 — Lossless JPEG
            if (payloadLen < 6) return false;
            uint8_t prec = seg[0];
            uint16_t h = DetailImg100::RdU16BE(seg + 1);
            uint16_t w = DetailImg100::RdU16BE(seg + 3);
            uint8_t nf = seg[5];
            if (prec < 2 || prec > 16 || w == 0 || h == 0 || nf < 1 || nf > 4) return false;
            if (payloadLen < 6u + static_cast<size_t>(nf) * 3u) return false;
            if (static_cast<uint64_t>(w) * h * nf > 64ull * 1024ull * 1024ull) return false;

            out.precision = prec;
            out.height = h;
            out.width = w;
            out.components = nf;
            for (uint8_t c = 0; c < nf; ++c) {
                comps[c].id    = seg[6 + c * 3 + 0];
                uint8_t hv     = seg[6 + c * 3 + 1];
                comps[c].hSamp = std::max<uint8_t>(1, static_cast<uint8_t>((hv >> 4) & 0x0Fu));
                comps[c].vSamp = std::max<uint8_t>(1, static_cast<uint8_t>(hv & 0x0Fu));
                if (comps[c].hSamp > 4 || comps[c].vSamp > 4) return false;
            }
            hasSof3 = true;
        } else if (marker == 0xDA) { // SOS
            if (!hasSof3 || payloadLen < 4) return false;
            uint8_t ns = seg[0];
            if (ns != out.components || payloadLen < 1u + static_cast<size_t>(ns) * 2u + 3u) return false;
            for (uint8_t c = 0; c < ns; ++c) {
                uint8_t cs = seg[1 + c * 2 + 0];
                uint8_t td = static_cast<uint8_t>((seg[1 + c * 2 + 1] >> 4) & 0x0Fu);
                if (td >= 4 || !huff[td].valid) return false;
                bool matched = false;
                for (uint8_t k = 0; k < ns; ++k) {
                    if (comps[k].id == cs) { comps[k].td = td; matched = true; break; }
                }
                if (!matched) comps[c].td = td;
            }
            predictor = seg[1 + ns * 2 + 0];
            pointTransform = static_cast<uint8_t>(seg[1 + ns * 2 + 2] & 0x0Fu);
            if (predictor > 7 || pointTransform >= out.precision) return false;
            scanPos = p + segLen;
            break;
        }
        p += segLen;
    }

    if (!hasSof3 || scanPos == 0 || scanPos >= size) return false;

    uint32_t bitBuf = 0;
    int bitCount = 0;
    size_t sp = scanPos;
    bool hitMarker = false;

    auto refillBits = [&]() {
        while (bitCount <= 24) {
            if (hitMarker || sp >= size) {
                bitBuf <<= 8;
                bitCount += 8;
                continue;
            }
            uint8_t b = data[sp++];
            if (b == 0xFF) {
                if (sp >= size) {
                    hitMarker = true;
                    bitBuf <<= 8;
                    bitCount += 8;
                    continue;
                }
                uint8_t nx = data[sp];
                if (nx == 0x00) {
                    ++sp; // Stuffed 0xFF00 -> literal 0xFF byte
                } else {
                    --sp; // Rewind onto 0xFF marker for restart/EOI handling
                    hitMarker = true;
                    bitBuf <<= 8;
                    bitCount += 8;
                    continue;
                }
            }
            bitBuf = (bitBuf << 8) | b;
            bitCount += 8;
        }
    };

    auto getBits = [&](int n) -> uint32_t {
        if (n <= 0) return 0;
        if (bitCount < n) refillBits();
        bitCount -= n;
        return (bitBuf >> bitCount) & ((1u << n) - 1u);
    };

    auto decodeDiff = [&](const HuffTable& ht) -> int32_t {
        int32_t code = 0;
        int ssss = -1;
        for (int len = 1; len <= 16; ++len) {
            if (bitCount < 1) refillBits();
            --bitCount;
            code = (code << 1) | static_cast<int32_t>((bitBuf >> bitCount) & 1u);
            if (code <= ht.maxCode[len]) {
                int idx = ht.valPtr[len] + (code - ht.minCode[len]);
                if (idx >= 0 && idx < 256) ssss = ht.syms[idx];
                break;
            }
        }
        if (ssss < 0) return 0;
        if (ssss == 0) return 0;
        if (ssss == 16) return 32768; // ITU-T T.81 Table H.2: SSSS=16 uses 0 extra bits
        if (ssss > 16) return 0;
        uint32_t v = getBits(ssss);
        if ((v & (1u << (ssss - 1))) == 0) {
            return static_cast<int32_t>(v) - static_cast<int32_t>((1u << ssss) - 1u);
        }
        return static_cast<int32_t>(v);
    };

    uint8_t maxH = 1, maxV = 1;
    for (uint32_t c = 0; c < out.components; ++c) {
        maxH = std::max(maxH, comps[c].hSamp);
        maxV = std::max(maxV, comps[c].vSamp);
    }

    uint32_t mcuCols = (out.width + maxH - 1u) / maxH;
    uint32_t mcuRows = (out.height + maxV - 1u) / maxV;
    std::vector<uint16_t> planes[4];
    uint32_t planeW[4] = {}, planeH[4] = {};
    for (uint32_t c = 0; c < out.components; ++c) {
        planeW[c] = mcuCols * comps[c].hSamp;
        planeH[c] = mcuRows * comps[c].vSamp;
        planes[c].assign(static_cast<size_t>(planeW[c]) * planeH[c], 0);
    }

    const int32_t initPred = 1 << (out.precision - pointTransform - 1);
    uint32_t mcusSinceRestart = 0;
    uint32_t restartStartPx[4] = { 0, 0, 0, 0 };
    uint32_t restartStartPy[4] = { 0, 0, 0, 0 };

    for (uint32_t my = 0; my < mcuRows; ++my) {
        for (uint32_t mx = 0; mx < mcuCols; ++mx) {
            if (restartInterval > 0 && mcusSinceRestart == restartInterval) {
                bitBuf = 0;
                bitCount = 0;
                hitMarker = false;
                while (sp + 1 < size && !(data[sp] == 0xFF && data[sp + 1] >= 0xD0 && data[sp + 1] <= 0xD7)) {
                    if (data[sp] == 0xFF && data[sp + 1] == 0xD9) break;
                    ++sp;
                }
                if (sp + 1 < size && data[sp] == 0xFF && data[sp + 1] >= 0xD0 && data[sp + 1] <= 0xD7) {
                    sp += 2;
                }
                mcusSinceRestart = 0;
                for (uint32_t c = 0; c < out.components; ++c) {
                    restartStartPx[c] = mx * comps[c].hSamp;
                    restartStartPy[c] = my * comps[c].vSamp;
                }
            }

            for (uint32_t c = 0; c < out.components; ++c) {
                const HuffTable& ht = huff[comps[c].td];
                uint32_t pw = planeW[c];
                for (uint8_t vy = 0; vy < comps[c].vSamp; ++vy) {
                    uint32_t py = my * comps[c].vSamp + vy;
                    for (uint8_t hx = 0; hx < comps[c].hSamp; ++hx) {
                        uint32_t px = mx * comps[c].hSamp + hx;
                        int32_t pred = 0;
                        if (mcusSinceRestart == 0 && vy == 0 && hx == 0) {
                            pred = initPred;
                        } else if (py == restartStartPy[c] && px > restartStartPx[c]) {
                            pred = planes[c][static_cast<size_t>(py) * pw + (px - 1u)];
                        } else if (px == 0) {
                            pred = planes[c][static_cast<size_t>(py - 1u) * pw + 0u];
                        } else {
                            int32_t ra = planes[c][static_cast<size_t>(py) * pw + (px - 1u)];
                            int32_t rb = planes[c][static_cast<size_t>(py - 1u) * pw + px];
                            int32_t rc = planes[c][static_cast<size_t>(py - 1u) * pw + (px - 1u)];
                            switch (predictor) {
                                case 0: pred = 0; break;
                                case 1: pred = ra; break;
                                case 2: pred = rb; break;
                                case 3: pred = rc; break;
                                case 4: pred = ra + rb - rc; break;
                                case 5: pred = ra + ((rb - rc) >> 1); break;
                                case 6: pred = rb + ((ra - rc) >> 1); break;
                                case 7: pred = (ra + rb) >> 1; break;
                                default: pred = ra; break;
                            }
                        }
                        int32_t diff = decodeDiff(ht);
                        planes[c][static_cast<size_t>(py) * pw + px] = static_cast<uint16_t>((pred + diff) & 0xFFFF);
                    }
                }
            }
            ++mcusSinceRestart;
        }
    }

    out.samples.resize(static_cast<size_t>(out.width) * out.height * out.components);
    for (uint32_t y = 0; y < out.height; ++y) {
        for (uint32_t x = 0; x < out.width; ++x) {
            size_t dstBase = (static_cast<size_t>(y) * out.width + x) * out.components;
            for (uint32_t c = 0; c < out.components; ++c) {
                uint32_t sx = (x * comps[c].hSamp) / maxH;
                uint32_t sy = (y * comps[c].vSamp) / maxV;
                uint16_t rawVal = planes[c][static_cast<size_t>(sy) * planeW[c] + sx];
                out.samples[dstBase + c] = static_cast<uint16_t>(rawVal << pointTransform);
            }
        }
    }
    return true;
}

inline uint32_t TiffTypeElemSize(uint16_t type) {
    switch (type) {
        case 1: case 2: case 6: case 7: return 1;
        case 3: case 8:                 return 2;
        case 4: case 9: case 11: case 13: return 4;
        case 5: case 10: case 12:       return 8;
        default: return 0;
    }
}

inline const uint8_t* GetTiffTagPayload(
    const uint8_t* data, size_t size, const uint8_t* ent, bool be,
    uint16_t& outType, uint32_t& outCount)
{
    outType  = Rd16(ent + 2, be);
    outCount = Rd32(ent + 4, be);
    uint32_t elemSz = TiffTypeElemSize(outType);
    if (elemSz == 0 || outCount == 0) return nullptr;
    uint64_t totalBytes = static_cast<uint64_t>(outCount) * elemSz;
    if (totalBytes <= 4) return ent + 8;
    uint32_t off = Rd32(ent + 8, be);
    if (static_cast<uint64_t>(off) + totalBytes > size) return nullptr;
    return data + off;
}

inline bool ReadTiffU32Values(
    const uint8_t* data, size_t size, const uint8_t* ent, bool be,
    std::vector<uint32_t>& outVals, uint32_t maxItems = 65536)
{
    uint16_t type = 0;
    uint32_t count = 0;
    const uint8_t* p = GetTiffTagPayload(data, size, ent, be, type, count);
    if (!p) return false;
    count = std::min(count, maxItems);
    outVals.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (type == 1 || type == 7)      outVals[i] = p[i];
        else if (type == 6)              outVals[i] = static_cast<uint32_t>(std::max<int>(0, static_cast<int8_t>(p[i])));
        else if (type == 3)              outVals[i] = Rd16(p + i * 2, be);
        else if (type == 8)              outVals[i] = static_cast<uint32_t>(std::max<int>(0, RdS16(p + i * 2, be)));
        else if (type == 4 || type == 13) outVals[i] = Rd32(p + i * 4, be);
        else if (type == 9)              outVals[i] = static_cast<uint32_t>(std::max<int32_t>(0, RdS32(p + i * 4, be)));
        else return false;
    }
    return !outVals.empty();
}

inline bool ReadTiffFloatValues(
    const uint8_t* data, size_t size, const uint8_t* ent, bool be,
    std::vector<float>& outVals, uint32_t maxItems = 64)
{
    uint16_t type = 0;
    uint32_t count = 0;
    const uint8_t* p = GetTiffTagPayload(data, size, ent, be, type, count);
    if (!p) return false;
    count = std::min(count, maxItems);
    outVals.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (type == 1 || type == 7)       outVals[i] = static_cast<float>(p[i]);
        else if (type == 6)               outVals[i] = static_cast<float>(static_cast<int8_t>(p[i]));
        else if (type == 3)               outVals[i] = static_cast<float>(Rd16(p + i * 2, be));
        else if (type == 8)               outVals[i] = static_cast<float>(RdS16(p + i * 2, be));
        else if (type == 4 || type == 13) outVals[i] = static_cast<float>(Rd32(p + i * 4, be));
        else if (type == 9)               outVals[i] = static_cast<float>(RdS32(p + i * 4, be));
        else if (type == 5) {
            uint32_t num = Rd32(p + i * 8 + 0, be);
            uint32_t den = Rd32(p + i * 8 + 4, be);
            outVals[i] = (den != 0) ? (static_cast<float>(num) / static_cast<float>(den)) : 0.0f;
        } else if (type == 10) {
            int32_t num = RdS32(p + i * 8 + 0, be);
            int32_t den = RdS32(p + i * 8 + 4, be);
            outVals[i] = (den != 0) ? (static_cast<float>(num) / static_cast<float>(den)) : 0.0f;
        } else if (type == 11) {
            uint32_t u = Rd32(p + i * 4, be);
            float f = 0.0f;
            std::memcpy(&f, &u, 4);
            outVals[i] = f;
        } else if (type == 12) {
            uint64_t hi = Rd32(p + i * 8 + (be ? 0 : 4), be);
            uint64_t lo = Rd32(p + i * 8 + (be ? 4 : 0), be);
            uint64_t u = (hi << 32) | lo;
            double d = 0.0;
            std::memcpy(&d, &u, 8);
            outVals[i] = static_cast<float>(d);
        } else {
            return false;
        }
    }
    return !outVals.empty();
}

inline bool UnpackRawSamplesFromBytes(
    const uint8_t* src, size_t srcLen,
    uint32_t blockW, uint32_t blockH, uint32_t blockSpp,
    uint16_t bps, bool be, uint16_t predictorTag,
    std::vector<uint16_t>& outSamples)
{
    const size_t rowSamples = static_cast<size_t>(blockW) * blockSpp;
    const size_t totalSamples = rowSamples * blockH;
    if (totalSamples == 0 || bps == 0 || bps > 16) return false;
    outSamples.assign(totalSamples, 0);

    if (bps == 8) {
        if (srcLen < totalSamples) return false;
        for (size_t i = 0; i < totalSamples; ++i) outSamples[i] = src[i];
    } else if (bps == 16) {
        if (srcLen < totalSamples * 2u) return false;
        for (size_t i = 0; i < totalSamples; ++i) outSamples[i] = Rd16(src + i * 2u, be);
    } else {
        // Check if stored unpacked inside 16-bit words vs packed MSB-first big-endian bitstream (DNG spec)
        const size_t rowBytesPadded = (rowSamples * bps + 7u) / 8u;
        const size_t totalBytesPadded = rowBytesPadded * blockH;
        const size_t totalBytesContig = (totalSamples * bps + 7u) / 8u;

        if (srcLen == totalSamples * 2u && totalSamples * 2u > totalBytesPadded + blockH) {
            uint16_t mask = static_cast<uint16_t>((1u << bps) - 1u);
            for (size_t i = 0; i < totalSamples; ++i) {
                outSamples[i] = static_cast<uint16_t>(Rd16(src + i * 2u, be) & mask);
            }
        } else if (srcLen == totalBytesPadded && totalBytesPadded > totalBytesContig) {
            const uint32_t mask = (1u << bps) - 1u;
            for (uint32_t y = 0; y < blockH; ++y) {
                const uint8_t* rp = src + static_cast<size_t>(y) * rowBytesPadded;
                uint32_t bitBuf = 0;
                int bits = 0;
                size_t bi = 0;
                for (size_t x = 0; x < rowSamples; ++x) {
                    while (bits < static_cast<int>(bps)) {
                        bitBuf = (bitBuf << 8) | ((bi < rowBytesPadded) ? rp[bi++] : 0u);
                        bits += 8;
                    }
                    bits -= static_cast<int>(bps);
                    outSamples[static_cast<size_t>(y) * rowSamples + x] = static_cast<uint16_t>((bitBuf >> bits) & mask);
                }
            }
        } else {
            if (srcLen < totalBytesContig) return false;
            const uint32_t mask = (1u << bps) - 1u;
            uint32_t bitBuf = 0;
            int bits = 0;
            size_t bi = 0;
            for (size_t i = 0; i < totalSamples; ++i) {
                while (bits < static_cast<int>(bps)) {
                    bitBuf = (bitBuf << 8) | ((bi < srcLen) ? src[bi++] : 0u);
                    bits += 8;
                }
                bits -= static_cast<int>(bps);
                outSamples[i] = static_cast<uint16_t>((bitBuf >> bits) & mask);
            }
        }
    }

    if (predictorTag == 2 && blockW > 1) {
        uint32_t modMask = (bps == 16) ? 0xFFFFu : ((1u << bps) - 1u);
        for (uint32_t y = 0; y < blockH; ++y) {
            uint16_t* row = &outSamples[static_cast<size_t>(y) * rowSamples];
            for (size_t x = blockSpp; x < rowSamples; ++x) {
                row[x] = static_cast<uint16_t>((row[x] + row[x - blockSpp]) & modMask);
            }
        }
    }
    return true;
}

inline bool DecodePackBitsBuffer(const uint8_t* src, size_t srcLen, size_t expectedBytes, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(expectedBytes);
    size_t p = 0;
    while (p < srcLen && out.size() < expectedBytes) {
        int8_t n = static_cast<int8_t>(src[p++]);
        if (n >= 0) {
            size_t cnt = static_cast<size_t>(n) + 1u;
            if (p + cnt > srcLen) cnt = srcLen - p;
            out.insert(out.end(), src + p, src + p + cnt);
            p += cnt;
        } else if (n != -128) {
            if (p >= srcLen) break;
            size_t cnt = static_cast<size_t>(1 - static_cast<int>(n));
            uint8_t val = src[p++];
            out.insert(out.end(), cnt, val);
        }
    }
    return out.size() >= expectedBytes;
}

inline void ApplyExifOrientationBGRA(uint16_t orientation, uint32_t& w, uint32_t& h, std::vector<uint8_t>& bgra) {
    if (orientation <= 1 || orientation > 8 || w == 0 || h == 0) return;
    const uint32_t srcW = w, srcH = h;
    const bool swapDims = (orientation >= 5 && orientation <= 8);
    const uint32_t dstW = swapDims ? srcH : srcW;
    const uint32_t dstH = swapDims ? srcW : srcH;
    std::vector<uint8_t> rotated(static_cast<size_t>(dstW) * dstH * 4);

    for (uint32_t y = 0; y < srcH; ++y) {
        for (uint32_t x = 0; x < srcW; ++x) {
            uint32_t dx = x, dy = y;
            switch (orientation) {
                case 2: dx = srcW - 1u - x; dy = y;              break;
                case 3: dx = srcW - 1u - x; dy = srcH - 1u - y;  break;
                case 4: dx = x;             dy = srcH - 1u - y;  break;
                case 5: dx = y;             dy = x;              break;
                case 6: dx = srcH - 1u - y; dy = x;              break;
                case 7: dx = srcH - 1u - y; dy = srcW - 1u - x;  break;
                case 8: dx = y;             dy = srcW - 1u - x;  break;
                default: break;
            }
            std::memcpy(&rotated[(static_cast<size_t>(dy) * dstW + dx) * 4],
                        &bgra[(static_cast<size_t>(y) * srcW + x) * 4], 4);
        }
    }
    w = dstW;
    h = dstH;
    bgra = std::move(rotated);
}

inline const uint8_t* GetSrgbGammaLut16384() {
    static uint8_t lut[16385] = {};
    static bool initialized = false;
    if (!initialized) {
        for (int i = 0; i <= 16384; ++i) {
            float lin = static_cast<float>(i) / 16384.0f;
            float srgb = (lin <= 0.0031308f)
                ? (12.92f * lin)
                : (1.055f * std::pow(lin, 1.0f / 2.4f) - 0.055f);
            lut[i] = static_cast<uint8_t>(std::clamp(std::round(srgb * 255.0f), 0.0f, 255.0f));
        }
        initialized = true;
    }
    return lut;
}

#include "CameraRawUpgrade.h"

inline void DemosaicAndToneMapSensor(
    const std::vector<uint16_t>& fullSamples,
    uint32_t fullW, uint32_t fullH, uint16_t spp,
    uint16_t photometric, uint16_t nominalBps,
    uint16_t cfaRows, uint16_t cfaCols, const std::vector<uint8_t>& cfaPat,
    uint16_t blRows, uint16_t blCols, const std::vector<float>& blackLevels,
    float explicitWhiteLevel,
    bool hasNeutral, const float neutral[3],
    bool hasActiveArea, const uint32_t activeRect[4],
    uint16_t orientation,
    uint32_t origSensorW, uint32_t origSensorH,
    const std::wstring& fmtLabel,
    ExtendedImageInfo& outInfo,
    const float* optCamToSrgb = nullptr)
{
    uint32_t aTop = 0, aLeft = 0, aBot = fullH, aRight = fullW;
    if (hasActiveArea && activeRect[0] < activeRect[2] && activeRect[1] < activeRect[3] &&
        activeRect[2] <= fullH && activeRect[3] <= fullW)
    {
        aTop = activeRect[0]; aLeft = activeRect[1];
        aBot = activeRect[2]; aRight = activeRect[3];
    }
    uint32_t W = aRight - aLeft;
    uint32_t H = aBot - aTop;

    float whiteLevel = explicitWhiteLevel;
    if (whiteLevel <= 0.0f) {
        uint16_t maxSamp = 0;
        for (uint16_t v : fullSamples) maxSamp = std::max(maxSamp, v);
        if (nominalBps == 16 && maxSamp > 0 && maxSamp <= 16383) {
            if (maxSamp >= 512 && maxSamp <= 4095)       whiteLevel = 4095.0f;
            else if (maxSamp >= 2048 && maxSamp <= 16383) whiteLevel = 16383.0f;
            else                                         whiteLevel = 65535.0f;
        } else {
            whiteLevel = static_cast<float>((1u << std::clamp<uint16_t>(nominalBps, 1, 16)) - 1u);
        }
    }

    float wb[3] = { 1.0f, 1.0f, 1.0f };
    if (hasNeutral && neutral[0] > 1e-5f && neutral[1] > 1e-5f && neutral[2] > 1e-5f) {
        wb[0] = std::clamp(neutral[1] / neutral[0], 0.1f, 8.0f);
        wb[1] = 1.0f;
        wb[2] = std::clamp(neutral[1] / neutral[2], 0.1f, 8.0f);
    }

    const bool applySrgbCurve = (photometric == 32803 || photometric == 34892 || hasNeutral || !blackLevels.empty());
    const uint8_t* srgbLut = GetSrgbGammaLut16384();
    auto toOutByte = [&](float v) -> uint8_t {
        v = std::clamp(v, 0.0f, 1.0f);
        if (!applySrgbCurve) {
            return static_cast<uint8_t>(std::round(v * 255.0f));
        }
        return srgbLut[static_cast<size_t>(std::round(v * 16384.0f))];
    };

    std::vector<uint8_t> bgra(static_cast<size_t>(W) * H * 4);

    if (photometric == 32803 && spp == 1 && W >= 2 && H >= 2) {
        uint16_t pr = (cfaRows >= 1) ? cfaRows : 2;
        uint16_t pc = (cfaCols >= 1) ? cfaCols : 2;
        std::vector<uint8_t> pat = cfaPat;
        if (pat.size() < static_cast<size_t>(pr) * pc) {
            pr = 2; pc = 2;
            pat = { 0, 1, 1, 2 }; // Default RGGB
        }
        auto colorAt = [&](uint32_t x, uint32_t y) -> int {
            uint8_t c = pat[(y % pr) * pc + (x % pc)];
            if (c <= 2) return static_cast<int>(c);
            return static_cast<int>(c % 3u);
        };

        uint16_t br = (blRows >= 1) ? blRows : ((blackLevels.size() >= 4) ? 2 : 1);
        uint16_t bc = (blCols >= 1) ? blCols : ((blackLevels.size() >= 4) ? 2 : 1);
        auto blackAt = [&](uint32_t x, uint32_t y) -> float {
            if (blackLevels.empty()) return 0.0f;
            size_t idx = (y % br) * bc + (x % bc);
            return blackLevels[idx % blackLevels.size()];
        };

        std::vector<float> plane(static_cast<size_t>(W) * H);
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                float raw = static_cast<float>(fullSamples[static_cast<size_t>(aTop + y) * fullW + (aLeft + x)]);
                float bl = blackAt(x, y);
                float denom = std::max(1.0f, whiteLevel - bl);
                int col = colorAt(x, y);
                plane[static_cast<size_t>(y) * W + x] = std::clamp(((raw - bl) / denom) * wb[col], 0.0f, 1.0f);
            }
        }

        bool isBayer2x2 = (pr == 2 && pc == 2);
        if (isBayer2x2) {
            int cnt[3] = {};
            for (int k = 0; k < 4; ++k) if (pat[k] <= 2) ++cnt[pat[k]];
            if (cnt[0] != 1 || cnt[1] != 2 || cnt[2] != 1) isBayer2x2 = false;
        }

        for (uint32_t y = 0; y < H; ++y) {
            uint32_t y0 = (y > 0) ? (y - 1u) : (y + 1u < H ? 1u : 0u);
            uint32_t y1 = (y + 1u < H) ? (y + 1u) : (y > 0 ? y - 1u : 0u);
            for (uint32_t x = 0; x < W; ++x) {
                uint32_t x0 = (x > 0) ? (x - 1u) : (x + 1u < W ? 1u : 0u);
                uint32_t x1 = (x + 1u < W) ? (x + 1u) : (x > 0 ? x - 1u : 0u);
                auto at = [&](uint32_t px, uint32_t py) -> float {
                    return plane[static_cast<size_t>(py) * W + px];
                };

                float r = 0.0f, g = 0.0f, b = 0.0f;
                if (isBayer2x2) {
                    int c = colorAt(x, y);
                    if (c == 1) {
                        g = at(x, y);
                        float hAvg = 0.5f * (at(x0, y) + at(x1, y));
                        float vAvg = 0.5f * (at(x, y0) + at(x, y1));
                        int cHoriz = colorAt(x ^ 1u, y);
                        if (cHoriz == 0) { r = hAvg; b = vAvg; }
                        else             { b = hAvg; r = vAvg; }
                    } else {
                        float center = at(x, y);
                        float gL = at(x0, y), gR = at(x1, y), gU = at(x, y0), gD = at(x, y1);
                        float gradH = std::abs(gL - gR), gradV = std::abs(gU - gD);
                        if (gradH * 1.5f < gradV)      g = 0.5f * (gL + gR);
                        else if (gradV * 1.5f < gradH) g = 0.5f * (gU + gD);
                        else                           g = 0.25f * (gL + gR + gU + gD);

                        float diag = 0.25f * (at(x0, y0) + at(x1, y0) + at(x0, y1) + at(x1, y1));
                        if (c == 0) { r = center; b = diag; }
                        else        { b = center; r = diag; }
                    }
                } else {
                    float sum[3] = {}, wt[3] = {};
                    int rad = 2;
                    for (int dy = -rad; dy <= rad; ++dy) {
                        uint32_t ny = static_cast<uint32_t>(std::clamp<int>(static_cast<int>(y) + dy, 0, static_cast<int>(H) - 1));
                        for (int dx = -rad; dx <= rad; ++dx) {
                            uint32_t nx = static_cast<uint32_t>(std::clamp<int>(static_cast<int>(x) + dx, 0, static_cast<int>(W) - 1));
                            int nc = colorAt(nx, ny);
                            float wgt = (dx == 0 && dy == 0) ? 4.0f : (1.0f / static_cast<float>(std::abs(dx) + std::abs(dy)));
                            sum[nc] += at(nx, ny) * wgt;
                            wt[nc] += wgt;
                        }
                    }
                    r = (wt[0] > 0.0f) ? (sum[0] / wt[0]) : 0.0f;
                    g = (wt[1] > 0.0f) ? (sum[1] / wt[1]) : r;
                    b = (wt[2] > 0.0f) ? (sum[2] / wt[2]) : r;
                }

                if (optCamToSrgb) {
                    float sr = optCamToSrgb[0] * r + optCamToSrgb[1] * g + optCamToSrgb[2] * b;
                    float sg = optCamToSrgb[3] * r + optCamToSrgb[4] * g + optCamToSrgb[5] * b;
                    float sb = optCamToSrgb[6] * r + optCamToSrgb[7] * g + optCamToSrgb[8] * b;
                    r = sr; g = sg; b = sb;
                }
                size_t di = (static_cast<size_t>(y) * W + x) * 4;
                bgra[di + 0] = toOutByte(b);
                bgra[di + 1] = toOutByte(g);
                bgra[di + 2] = toOutByte(r);
                bgra[di + 3] = 255;
            }
        }
    } else if (spp >= 3) {
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                size_t si = (static_cast<size_t>(aTop + y) * fullW + (aLeft + x)) * spp;
                float bl0 = (blackLevels.size() >= 1) ? blackLevels[0] : 0.0f;
                float bl1 = (blackLevels.size() >= 2) ? blackLevels[1] : bl0;
                float bl2 = (blackLevels.size() >= 3) ? blackLevels[2] : bl0;
                float r = ((static_cast<float>(fullSamples[si + 0]) - bl0) / std::max(1.0f, whiteLevel - bl0)) * wb[0];
                float g = ((static_cast<float>(fullSamples[si + 1]) - bl1) / std::max(1.0f, whiteLevel - bl1)) * wb[1];
                float b = ((static_cast<float>(fullSamples[si + 2]) - bl2) / std::max(1.0f, whiteLevel - bl2)) * wb[2];
                if (optCamToSrgb) {
                    float sr = optCamToSrgb[0] * r + optCamToSrgb[1] * g + optCamToSrgb[2] * b;
                    float sg = optCamToSrgb[3] * r + optCamToSrgb[4] * g + optCamToSrgb[5] * b;
                    float sb = optCamToSrgb[6] * r + optCamToSrgb[7] * g + optCamToSrgb[8] * b;
                    r = sr; g = sg; b = sb;
                }
                size_t di = (static_cast<size_t>(y) * W + x) * 4;
                bgra[di + 0] = toOutByte(b);
                bgra[di + 1] = toOutByte(g);
                bgra[di + 2] = toOutByte(r);
                bgra[di + 3] = 255;
            }
        }
    } else {
        float bl = blackLevels.empty() ? 0.0f : blackLevels[0];
        float denom = std::max(1.0f, whiteLevel - bl);
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                size_t si = (static_cast<size_t>(aTop + y) * fullW + (aLeft + x)) * std::max<uint16_t>(1, spp);
                float v = (static_cast<float>(fullSamples[si]) - bl) / denom;
                if (photometric == 0) v = 1.0f - v; // WhiteIsZero
                uint8_t outV = toOutByte(v);
                size_t di = (static_cast<size_t>(y) * W + x) * 4;
                bgra[di + 0] = outV;
                bgra[di + 1] = outV;
                bgra[di + 2] = outV;
                bgra[di + 3] = 255;
            }
        }
    }

    ApplyExifOrientationBGRA(orientation, W, H, bgra);

    outInfo.width = W;
    outInfo.height = H;
    uint32_t ow = (origSensorW > 0) ? origSensorW : W;
    uint32_t oh = (origSensorH > 0) ? origSensorH : H;
    if (orientation >= 5 && orientation <= 8) std::swap(ow, oh);
    outInfo.origWidth = ow;
    outInfo.origHeight = oh;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = std::clamp<uint32_t>(nominalBps, 8u, 16u);
    outInfo.formatName = fmtLabel;
    outInfo.bgraPixels = std::move(bgra);
    outInfo.valid = true;
}

// Parses TIFF-based RAW & DNG IFDs + SubIFDs (Canon CR2, Nikon NEF, Sony ARW, Adobe DNG, Olympus ORF, Panasonic RW2, Pentax PEF)
inline bool ParseTiffRawIfds(
    const uint8_t* data, size_t size,
    const std::wstring& fmtLabel,
    ExtendedImageInfo& outInfo)
{
    if (size < 16) return false;
    bool be = false;
    if (data[0] == 'I' && data[1] == 'I') be = false;
    else if (data[0] == 'M' && data[1] == 'M') be = true;
    else return false;

    uint16_t magic = Rd16(data + 2, be);
    // Standard TIFF=42, Olympus ORF=0x4F52 ('RO'/'OR') / 0x5352 ('RS'), Panasonic RW2=0x0055
    if (magic != 42 && magic != 0x4F52 && magic != 0x5352 && magic != 0x0055) return false;

    uint32_t firstIfd = Rd32(data + 4, be);
    if ((magic == 0x4F52 || magic == 0x5352) && (firstIfd < 8 || firstIfd + 2 > size)) {
        firstIfd = 8;
    }

    struct RawIfdDesc {
        uint32_t subfileType = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint16_t bps = 8;
        uint16_t compression = 1;
        uint16_t photometric = 2;
        uint16_t orientation = 0;
        uint16_t spp = 1;
        uint32_t rowsPerStrip = 0xFFFFFFFFu;
        uint16_t planarConfig = 1;
        uint16_t predictor = 1;
        std::vector<uint32_t> stripOffsets;
        std::vector<uint32_t> stripByteCounts;
        uint32_t tileWidth = 0;
        uint32_t tileLength = 0;
        std::vector<uint32_t> tileOffsets;
        std::vector<uint32_t> tileByteCounts;
        uint32_t jpegOffset = 0;
        uint32_t jpegLength = 0;
        uint32_t rw2JpgOff = 0;
        uint32_t rw2JpgLen = 0;
        uint16_t cfaRows = 2;
        uint16_t cfaCols = 2;
        std::vector<uint8_t> cfaPattern;
        uint16_t blRows = 0;
        uint16_t blCols = 0;
        std::vector<float> blackLevels;
        float whiteLevel = 0.0f;
        bool hasNeutral = false;
        float neutral[3] = { 1.0f, 1.0f, 1.0f };
        bool hasActiveArea = false;
        uint32_t activeArea[4] = {};
        bool hasCr2Slices = false;
        uint16_t cr2Slices[3] = {};
        bool hasColorMatrix1 = false;
        float colorMatrix1[9] = {};
        bool hasCamCal1 = false;
        float camCal1[9] = {};
        uint32_t rw2RawOff = 0;
        float rw2Wb[3] = {};
        float rw2Black[3] = { -1.0f, -1.0f, -1.0f };
    };

    std::vector<uint32_t> ifdQueue;
    std::vector<uint32_t> visitedOffsets;
    auto enqueueIfd = [&](uint32_t off) {
        if (off < 8 || off + 2 > size) return;
        for (uint32_t v : visitedOffsets) if (v == off) return;
        for (uint32_t q : ifdQueue) if (q == off) return;
        ifdQueue.push_back(off);
    };

    enqueueIfd(firstIfd);
    // Canon CR2 header at bytes 8..15 ("CR\x02\x00" + rawIfdOffset)
    if (!be && size >= 16 && data[8] == 'C' && data[9] == 'R' && data[10] == 2) {
        enqueueIfd(Rd32(data + 12, false));
    }

    uint32_t bestRawW = 0, bestRawH = 0;
    uint16_t globalOrientation = 1;
    bool globalHasNeutral = false;
    float globalNeutral[3] = { 1.0f, 1.0f, 1.0f };
    bool globalHasCm1 = false;
    float globalCm1[9] = {};
    bool globalHasCc1 = false;
    float globalCc1[9] = {};
    NikonMetaParams nikonMeta;
    std::vector<uint8_t> exifCfaPattern;
    uint16_t exifCfaRows = 2, exifCfaCols = 2;

    std::vector<RawIfdDesc> candidates;

    while (!ifdQueue.empty() && visitedOffsets.size() < 64) {
        uint32_t ifdOff = ifdQueue.front();
        ifdQueue.erase(ifdQueue.begin());
        visitedOffsets.push_back(ifdOff);

        if (ifdOff + 2 > size) continue;
        uint16_t numEntries = Rd16(data + ifdOff, be);
        if (numEntries == 0 || numEntries > 1024 || ifdOff + 2u + static_cast<size_t>(numEntries) * 12u > size) {
            continue;
        }

        RawIfdDesc d;
        std::vector<uint32_t> tmpU32;
        std::vector<float> tmpF32;

        for (uint16_t e = 0; e < numEntries; ++e) {
            const uint8_t* ent = data + ifdOff + 2u + static_cast<size_t>(e) * 12u;
            uint16_t tag = Rd16(ent + 0, be);
            uint16_t ftype = Rd16(ent + 2, be);
            uint32_t fcount = Rd32(ent + 4, be);
            uint32_t rawValOff = Rd32(ent + 8, be);

            switch (tag) {
                case 0x00FE: // NewSubfileType
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 1)) d.subfileType = tmpU32[0];
                    break;
                case 0x0100: // ImageWidth
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 1)) d.width = tmpU32[0];
                    break;
                case 0x0101: // ImageLength
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 1)) d.height = tmpU32[0];
                    break;
                case 0x0102: // BitsPerSample
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 8)) d.bps = static_cast<uint16_t>(tmpU32[0]);
                    break;
                case 0x0103: // Compression
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 1)) d.compression = static_cast<uint16_t>(tmpU32[0]);
                    break;
                case 0x0106: // PhotometricInterpretation
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 1)) d.photometric = static_cast<uint16_t>(tmpU32[0]);
                    break;
                case 0x0111: // StripOffsets
                    ReadTiffU32Values(data, size, ent, be, d.stripOffsets, 65536);
                    break;
                case 0x0112: // Orientation
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 1) && tmpU32[0] >= 1 && tmpU32[0] <= 8) {
                        d.orientation = static_cast<uint16_t>(tmpU32[0]);
                        if (globalOrientation == 1) globalOrientation = d.orientation;
                    }
                    break;
                case 0x0115: // SamplesPerPixel
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 1)) d.spp = static_cast<uint16_t>(std::max(1u, tmpU32[0]));
                    break;
                case 0x0116: // RowsPerStrip
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 1) && tmpU32[0] > 0) d.rowsPerStrip = tmpU32[0];
                    break;
                case 0x0117: // StripByteCounts
                    ReadTiffU32Values(data, size, ent, be, d.stripByteCounts, 65536);
                    break;
                case 0x011C: // PlanarConfiguration
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 1)) d.planarConfig = static_cast<uint16_t>(tmpU32[0]);
                    break;
                case 0x013D: // Predictor
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 1)) d.predictor = static_cast<uint16_t>(tmpU32[0]);
                    break;
                case 0x0142: // TileWidth
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 1)) d.tileWidth = tmpU32[0];
                    break;
                case 0x0143: // TileLength
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 1)) d.tileLength = tmpU32[0];
                    break;
                case 0x0144: // TileOffsets
                    ReadTiffU32Values(data, size, ent, be, d.tileOffsets, 65536);
                    break;
                case 0x0145: // TileByteCounts
                    ReadTiffU32Values(data, size, ent, be, d.tileByteCounts, 65536);
                    break;
                case 0x014A: // SubIFDs (inline when count==1, offset array when count>1)
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 64)) {
                        for (uint32_t subOff : tmpU32) enqueueIfd(subOff);
                    }
                    break;
                case 0x0201: // JPEGInterchangeFormat
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 1)) d.jpegOffset = tmpU32[0];
                    break;
                case 0x0202: // JPEGInterchangeFormatLength
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 1)) d.jpegLength = tmpU32[0];
                    break;
                case 0x002E: // Panasonic RW2 JpgFromRaw
                    if (fcount > 32 && static_cast<uint64_t>(rawValOff) + fcount <= size) {
                        d.rw2JpgOff = rawValOff;
                        d.rw2JpgLen = fcount;
                    }
                    break;
                case 0x0002: // Panasonic RW2 SensorWidth
                    if (magic == 0x0055 && ReadTiffU32Values(data, size, ent, be, tmpU32, 1) && d.width == 0) d.width = tmpU32[0];
                    break;
                case 0x0003: // Panasonic RW2 SensorHeight
                    if (magic == 0x0055 && ReadTiffU32Values(data, size, ent, be, tmpU32, 1) && d.height == 0) d.height = tmpU32[0];
                    break;
                case 0x0009: // Panasonic RW2 CFAPattern (1=RGGB, 2=GRBG, 3=GBRG, 4=BGGR)
                    if (magic == 0x0055 && ReadTiffU32Values(data, size, ent, be, tmpU32, 1)) {
                        d.photometric = 32803;
                        d.bps = 12;
                        if (tmpU32[0] == 1)      d.cfaPattern = { 0, 1, 1, 2 };
                        else if (tmpU32[0] == 2) d.cfaPattern = { 1, 0, 2, 1 };
                        else if (tmpU32[0] == 3) d.cfaPattern = { 1, 2, 0, 1 };
                        else if (tmpU32[0] == 4) d.cfaPattern = { 2, 1, 1, 0 };
                    }
                    break;
                case 0x001C: case 0x001D: case 0x001E:
                    if (magic == 0x0055 && ReadTiffFloatValues(data, size, ent, be, tmpF32, 1)) {
                        d.rw2Black[tag - 0x001C] = tmpF32[0];
                    }
                    break;
                case 0x0024: case 0x0025: case 0x0026:
                    if (magic == 0x0055 && ReadTiffFloatValues(data, size, ent, be, tmpF32, 1)) {
                        d.rw2Wb[tag - 0x0024] = tmpF32[0];
                    }
                    break;
                case 0x0118: // Panasonic RW2 RawDataOffset
                    if (magic == 0x0055 && ReadTiffU32Values(data, size, ent, be, tmpU32, 1)) {
                        d.rw2RawOff = tmpU32[0];
                    }
                    break;
                case 0x927C: { // MakerNote (Nikon NEF Huffman / Linearization table)
                    uint16_t pt = 0; uint32_t pc = 0;
                    const uint8_t* pp = GetTiffTagPayload(data, size, ent, be, pt, pc);
                    if (pp && pc >= 18) ParseNikonMakerNote(pp, pc, d.bps, nikonMeta);
                    break;
                }
                case 0xC621: // ColorMatrix1 (3x3 XYZ D50 -> Camera)
                    if (ReadTiffFloatValues(data, size, ent, be, tmpF32, 9) && tmpF32.size() == 9) {
                        d.hasColorMatrix1 = true;
                        for (int k = 0; k < 9; ++k) d.colorMatrix1[k] = tmpF32[k];
                        if (!globalHasCm1) {
                            globalHasCm1 = true;
                            for (int k = 0; k < 9; ++k) globalCm1[k] = tmpF32[k];
                        }
                    }
                    break;
                case 0xC623: // CameraCalibration1 (3x3)
                    if (ReadTiffFloatValues(data, size, ent, be, tmpF32, 9) && tmpF32.size() == 9) {
                        d.hasCamCal1 = true;
                        for (int k = 0; k < 9; ++k) d.camCal1[k] = tmpF32[k];
                        if (!globalHasCc1) {
                            globalHasCc1 = true;
                            for (int k = 0; k < 9; ++k) globalCc1[k] = tmpF32[k];
                        }
                    }
                    break;
                case 0x828D: // CFARepeatPatternDim [rows, cols]
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 2) && tmpU32.size() == 2 && tmpU32[0] > 0 && tmpU32[1] > 0 && tmpU32[0] <= 16 && tmpU32[1] <= 16) {
                        d.cfaRows = static_cast<uint16_t>(tmpU32[0]);
                        d.cfaCols = static_cast<uint16_t>(tmpU32[1]);
                    }
                    break;
                case 0x828E: { // CFAPattern (TIFF-EP / DNG)
                    uint16_t pt = 0; uint32_t pc = 0;
                    const uint8_t* pp = GetTiffTagPayload(data, size, ent, be, pt, pc);
                    if (pp && pc >= 4 && pc <= 256) {
                        d.cfaPattern.assign(pp, pp + pc);
                    }
                    break;
                }
                case 0xA302: { // Exif CFAPattern: [cols:u16][rows:u16][pattern...]
                    uint16_t pt = 0; uint32_t pc = 0;
                    const uint8_t* pp = GetTiffTagPayload(data, size, ent, be, pt, pc);
                    if (pp && pc >= 8) {
                        uint16_t cols = Rd16(pp + 0, be);
                        uint16_t rows = Rd16(pp + 2, be);
                        if (cols > 0 && rows > 0 && cols <= 16 && rows <= 16 && 4u + static_cast<uint32_t>(cols) * rows <= pc) {
                            exifCfaCols = cols;
                            exifCfaRows = rows;
                            exifCfaPattern.assign(pp + 4, pp + 4 + static_cast<size_t>(cols) * rows);
                        }
                    }
                    break;
                }
                case 0x8769: // ExifIFD
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 1)) enqueueIfd(tmpU32[0]);
                    break;
                case 0xC619: // BlackLevelRepeatDim [rows, cols]
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 2) && tmpU32.size() == 2) {
                        d.blRows = static_cast<uint16_t>(tmpU32[0]);
                        d.blCols = static_cast<uint16_t>(tmpU32[1]);
                    }
                    break;
                case 0xC61A: // BlackLevel
                    ReadTiffFloatValues(data, size, ent, be, d.blackLevels, 64);
                    break;
                case 0xC61D: // WhiteLevel
                    if (ReadTiffFloatValues(data, size, ent, be, tmpF32, 4) && tmpF32[0] > 0.0f) {
                        d.whiteLevel = tmpF32[0];
                    }
                    break;
                case 0xC628: // AsShotNeutral
                    if (ReadTiffFloatValues(data, size, ent, be, tmpF32, 3) && tmpF32.size() >= 3) {
                        d.hasNeutral = true;
                        d.neutral[0] = tmpF32[0]; d.neutral[1] = tmpF32[1]; d.neutral[2] = tmpF32[2];
                        if (!globalHasNeutral) {
                            globalHasNeutral = true;
                            globalNeutral[0] = tmpF32[0]; globalNeutral[1] = tmpF32[1]; globalNeutral[2] = tmpF32[2];
                        }
                    }
                    break;
                case 0xC640: // CR2Slices [count, sliceWidth, lastSliceWidth]
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 3) && tmpU32.size() == 3) {
                        d.hasCr2Slices = true;
                        d.cr2Slices[0] = static_cast<uint16_t>(tmpU32[0]);
                        d.cr2Slices[1] = static_cast<uint16_t>(tmpU32[1]);
                        d.cr2Slices[2] = static_cast<uint16_t>(tmpU32[2]);
                    }
                    break;
                case 0xC68D: // ActiveArea [top, left, bottom, right]
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 4) && tmpU32.size() == 4) {
                        d.hasActiveArea = true;
                        for (int k = 0; k < 4; ++k) d.activeArea[k] = tmpU32[k];
                    }
                    break;
                default:
                    (void)ftype;
                    break;
            }
        }

        if (d.width > 0 && d.height > 0 && d.width <= 32768 && d.height <= 32768 &&
            static_cast<uint64_t>(d.width) * d.height > static_cast<uint64_t>(bestRawW) * bestRawH)
        {
            bestRawW = d.width;
            bestRawH = d.height;
        }

        if (!d.stripOffsets.empty() || !d.tileOffsets.empty() || d.rw2RawOff > 0 ||
            (d.jpegOffset > 0 && d.jpegLength > 32) || (d.rw2JpgOff > 0 && d.rw2JpgLen > 32))
        {
            candidates.push_back(std::move(d));
        }

        size_t nextPtrPos = ifdOff + 2u + static_cast<size_t>(numEntries) * 12u;
        if (nextPtrPos + 4u <= size) {
            enqueueIfd(Rd32(data + nextPtrPos, be));
        }
    }

    for (auto& c : candidates) {
        if (c.orientation == 0) c.orientation = globalOrientation;
        if (!c.hasNeutral && c.rw2Wb[0] > 0.0f && c.rw2Wb[1] > 0.0f && c.rw2Wb[2] > 0.0f) {
            c.hasNeutral = true;
            c.neutral[0] = c.rw2Wb[1] / c.rw2Wb[0];
            c.neutral[1] = 1.0f;
            c.neutral[2] = c.rw2Wb[1] / c.rw2Wb[2];
        }
        if (c.blackLevels.empty() && c.rw2Black[0] >= 0.0f && c.rw2Black[1] >= 0.0f && c.rw2Black[2] >= 0.0f) {
            c.blRows = 2; c.blCols = 2;
            uint8_t p0 = c.cfaPattern.size() >= 4 ? c.cfaPattern[0] : 0;
            uint8_t p1 = c.cfaPattern.size() >= 4 ? c.cfaPattern[1] : 1;
            uint8_t p2 = c.cfaPattern.size() >= 4 ? c.cfaPattern[2] : 1;
            uint8_t p3 = c.cfaPattern.size() >= 4 ? c.cfaPattern[3] : 2;
            c.blackLevels = { c.rw2Black[p0 % 3], c.rw2Black[p1 % 3], c.rw2Black[p2 % 3], c.rw2Black[p3 % 3] };
        }
        if (!c.hasNeutral && globalHasNeutral) {
            c.hasNeutral = true;
            c.neutral[0] = globalNeutral[0];
            c.neutral[1] = globalNeutral[1];
            c.neutral[2] = globalNeutral[2];
        }
        if (!c.hasColorMatrix1 && globalHasCm1) {
            c.hasColorMatrix1 = true;
            for (int k = 0; k < 9; ++k) c.colorMatrix1[k] = globalCm1[k];
        }
        if (!c.hasCamCal1 && globalHasCc1) {
            c.hasCamCal1 = true;
            for (int k = 0; k < 9; ++k) c.camCal1[k] = globalCc1[k];
        }
        if (c.cfaPattern.empty() && !exifCfaPattern.empty()) {
            c.cfaRows = exifCfaRows;
            c.cfaCols = exifCfaCols;
            c.cfaPattern = exifCfaPattern;
        }
    }

    // Sort IFD candidates: full-res RAW CFA/LinearRaw/RGB first by area descending, then largest previews
    auto scoreCandidate = [](const RawIfdDesc& c) -> uint64_t {
        uint64_t area = static_cast<uint64_t>(c.width) * c.height;
        if (area == 0 && c.rw2JpgLen > 0) area = static_cast<uint64_t>(c.rw2JpgLen) * 8ull;
        if (area == 0 && c.jpegLength > 0) area = static_cast<uint64_t>(c.jpegLength) * 4ull;
        bool isFullResRaw = ((c.subfileType & 1u) == 0) &&
                            (c.photometric == 32803 || c.photometric == 34892 ||
                             c.photometric == 2 || c.photometric == 1 || c.photometric == 0);
        bool isSensorMode = (c.photometric == 32803 || c.photometric == 34892);
        uint64_t tier = isFullResRaw ? (isSensorMode ? 3ull : 2ull) : 1ull;
        return (tier << 56) | (area & 0x00FFFFFFFFFFFFFFull);
    };
    std::sort(candidates.begin(), candidates.end(), [&](const RawIfdDesc& a, const RawIfdDesc& b) {
        return scoreCandidate(a) > scoreCandidate(b);
    });

    auto tryDecodeIfd = [&](const RawIfdDesc& ifd) -> bool {
        float camToSrgb[9] = {};
        const float* camToSrgbPtr = nullptr;
        if (ifd.hasColorMatrix1) {
            float wbTmp[3] = { 1.0f, 1.0f, 1.0f };
            if (BuildCameraToSrgbMatrix(ifd.colorMatrix1, ifd.hasCamCal1, ifd.camCal1, wbTmp, camToSrgb)) {
                camToSrgbPtr = camToSrgb;
            }
        }

        // 0. Panasonic RW2 12-bit 14-pixel-per-16-byte Sensor RawDataOffset (tag 0x0118)
        if (magic == 0x0055 && ifd.rw2RawOff > 0 && ifd.rw2RawOff < size &&
            ifd.width >= 2 && ifd.height >= 2 && ifd.width <= 16384 && ifd.height <= 16384)
        {
            std::vector<uint16_t> rw2Samples;
            if (UnpackPanasonicRw2Sensor(data + ifd.rw2RawOff, size - ifd.rw2RawOff, ifd.width, ifd.height, rw2Samples)) {
                DemosaicAndToneMapSensor(
                    rw2Samples, ifd.width, ifd.height, 1, 32803, 12,
                    ifd.cfaRows, ifd.cfaCols, ifd.cfaPattern, ifd.blRows, ifd.blCols, ifd.blackLevels,
                    (ifd.whiteLevel > 0.0f ? ifd.whiteLevel : 4095.0f),
                    ifd.hasNeutral, ifd.neutral, ifd.hasActiveArea, ifd.activeArea,
                    ifd.orientation, bestRawW, bestRawH, fmtLabel, outInfo, camToSrgbPtr);
                return true;
            }
        }

        // 1. Panasonic RW2 JpgFromRaw tag (0x002E)
        if (ifd.rw2JpgOff > 0 && ifd.rw2JpgLen > 32 &&
            static_cast<uint64_t>(ifd.rw2JpgOff) + ifd.rw2JpgLen <= size)
        {
            if (DetailImg100::DecodeEmbeddedWicBuffer(data + ifd.rw2JpgOff, ifd.rw2JpgLen, fmtLabel, outInfo)) {
                if (bestRawW > outInfo.origWidth) { outInfo.origWidth = bestRawW; outInfo.origHeight = bestRawH; }
                return true;
            }
        }

        // 2. Explicit JPEGInterchangeFormat (0x0201 / 0x0202)
        if (ifd.jpegOffset > 0 && ifd.jpegLength > 32 &&
            static_cast<uint64_t>(ifd.jpegOffset) + ifd.jpegLength <= size)
        {
            const uint8_t* jp = data + ifd.jpegOffset;
            LosslessJpegResult lj;
            if (ifd.width > 0 && ifd.height > 0 && DecodeLosslessJpegSOF3(jp, ifd.jpegLength, lj)) {
                if (lj.samples.size() >= static_cast<size_t>(ifd.width) * ifd.height * ifd.spp) {
                    DemosaicAndToneMapSensor(
                        lj.samples, ifd.width, ifd.height, ifd.spp, ifd.photometric, lj.precision,
                        ifd.cfaRows, ifd.cfaCols, ifd.cfaPattern, ifd.blRows, ifd.blCols, ifd.blackLevels,
                        ifd.whiteLevel, ifd.hasNeutral, ifd.neutral, ifd.hasActiveArea, ifd.activeArea,
                        ifd.orientation, bestRawW, bestRawH, fmtLabel, outInfo, camToSrgbPtr);
                    return true;
                }
            }
            if (DetailImg100::DecodeEmbeddedWicBuffer(jp, ifd.jpegLength, fmtLabel, outInfo)) {
                if (bestRawW > outInfo.origWidth) { outInfo.origWidth = bestRawW; outInfo.origHeight = bestRawH; }
                return true;
            }
        }

        if (ifd.width == 0 || ifd.height == 0 || ifd.width > 16384 || ifd.height > 16384) return false;
        if (static_cast<uint64_t>(ifd.width) * ifd.height > 64ull * 1024ull * 1024ull) return false;

        // Single-strip baseline/lossy JPEG preview (Compression 6 or 7) -> WIC directly
        if ((ifd.compression == 6 || ifd.compression == 7 || ifd.compression == 34892) &&
            ifd.stripOffsets.size() == 1 && ifd.tileOffsets.empty())
        {
            uint32_t sOff = ifd.stripOffsets[0];
            uint32_t sLen = ifd.stripByteCounts.empty()
                ? ((sOff < size) ? static_cast<uint32_t>(size - sOff) : 0u)
                : ifd.stripByteCounts[0];
            if (sOff > 0 && sLen > 16 && static_cast<uint64_t>(sOff) + sLen <= size &&
                data[sOff] == 0xFF && data[sOff + 1] == 0xD8)
            {
                LosslessJpegResult ljCheck;
                if (!DecodeLosslessJpegSOF3(data + sOff, sLen, ljCheck)) {
                    if (DetailImg100::DecodeEmbeddedWicBuffer(data + sOff, sLen, fmtLabel, outInfo)) {
                        if (bestRawW > outInfo.origWidth) { outInfo.origWidth = bestRawW; outInfo.origHeight = bestRawH; }
                        return true;
                    }
                    return false;
                }
            }
        }

        // 3. Stripped or Tiled RAW/DNG/TIFF sample unpacking
        const bool isTiled = !ifd.tileOffsets.empty() && ifd.tileWidth > 0 && ifd.tileLength > 0;
        const bool isStripped = !isTiled && !ifd.stripOffsets.empty();
        if (!isTiled && !isStripped) return false;
        if (ifd.compression != 1 && ifd.compression != 7 && ifd.compression != 8 &&
            ifd.compression != 32946 && ifd.compression != 32773 && ifd.compression != 34892 &&
            ifd.compression != 32767 && ifd.compression != 34713 &&
            ifd.compression != 34316 && ifd.compression != 34826)
        {
            return false;
        }

        const uint16_t effSpp = std::clamp<uint16_t>(ifd.spp, 1, 4);
        const bool planar = (ifd.planarConfig == 2 && effSpp > 1);
        const uint16_t blockSpp = planar ? 1 : effSpp;
        uint16_t detectedNominalBps = ifd.bps;

        std::vector<uint16_t> fullSamples(static_cast<size_t>(ifd.width) * ifd.height * effSpp, 0);

        auto decodeBlock = [&](const uint8_t* cPtr, size_t cLen, uint32_t bw, uint32_t bh, std::vector<uint16_t>& blockOut) -> bool {
            if (cLen == 0) return false;
            if (ifd.compression == 32767 && blockSpp == 1) { // Sony ARW2 Delta-Compressed Raw
                detectedNominalBps = 12;
                return UnpackSonyArw2Sensor(cPtr, cLen, bw, bh, nullptr, blockOut);
            }
            if (ifd.compression == 34713 && blockSpp == 1) { // Nikon NEF Compressed / Lossless Huffman
                return UnpackNikonNefHuffmanSensor(cPtr, cLen, bw, bh, ifd.bps, nikonMeta, blockOut);
            }
            if ((ifd.compression == 34316 || ifd.compression == 34826) && blockSpp == 1) { // Panasonic RW2 12-bit
                detectedNominalBps = 12;
                return UnpackPanasonicRw2Sensor(cPtr, cLen, bw, bh, blockOut);
            }
            if (ifd.compression == 1) {
                return UnpackRawSamplesFromBytes(cPtr, cLen, bw, bh, blockSpp, ifd.bps, be, ifd.predictor, blockOut);
            }
            if (ifd.compression == 8 || ifd.compression == 32946) {
                std::vector<uint8_t> inflated;
                size_t maxExpected = static_cast<size_t>(bw) * bh * blockSpp * 2u + 4096u;
                if (!InflateZlibOrRaw(cPtr, cLen, inflated, maxExpected)) return false;
                return UnpackRawSamplesFromBytes(inflated.data(), inflated.size(), bw, bh, blockSpp, ifd.bps, be, ifd.predictor, blockOut);
            }
            if (ifd.compression == 32773) {
                size_t expectedBytes = (ifd.bps == 8)
                    ? (static_cast<size_t>(bw) * bh * blockSpp)
                    : ((ifd.bps == 16) ? (static_cast<size_t>(bw) * bh * blockSpp * 2u)
                                       : ((static_cast<size_t>(bw) * blockSpp * ifd.bps + 7u) / 8u) * bh);
                std::vector<uint8_t> unpackedRle;
                if (!DecodePackBitsBuffer(cPtr, cLen, expectedBytes, unpackedRle)) return false;
                return UnpackRawSamplesFromBytes(unpackedRle.data(), unpackedRle.size(), bw, bh, blockSpp, ifd.bps, be, ifd.predictor, blockOut);
            }
            if (ifd.compression == 7 || ifd.compression == 34892) {
                LosslessJpegResult lj;
                if (DecodeLosslessJpegSOF3(cPtr, cLen, lj)) {
                    detectedNominalBps = lj.precision;
                    const size_t targetCount = static_cast<size_t>(bw) * bh * blockSpp;
                    // Check DNG 2x2 Bayer quad packing (Nf==4, X==bw/2, Y==bh/2, blockSpp==1)
                    if (lj.components == 4 && blockSpp == 1 && lj.width * 2u == bw && lj.height * 2u == bh) {
                        blockOut.assign(targetCount, 0);
                        for (uint32_t qy = 0; qy < lj.height; ++qy) {
                            for (uint32_t qx = 0; qx < lj.width; ++qx) {
                                const uint16_t* q = &lj.samples[(static_cast<size_t>(qy) * lj.width + qx) * 4u];
                                blockOut[static_cast<size_t>(qy * 2u + 0u) * bw + (qx * 2u + 0u)] = q[0];
                                blockOut[static_cast<size_t>(qy * 2u + 0u) * bw + (qx * 2u + 1u)] = q[1];
                                blockOut[static_cast<size_t>(qy * 2u + 1u) * bw + (qx * 2u + 0u)] = q[2];
                                blockOut[static_cast<size_t>(qy * 2u + 1u) * bw + (qx * 2u + 1u)] = q[3];
                            }
                        }
                        return true;
                    }
                    if (lj.samples.size() == targetCount) {
                        blockOut = std::move(lj.samples);
                        return true;
                    }
                    if (lj.samples.size() > targetCount) {
                        // Padded SOF3 edge block
                        size_t sofRowElems = static_cast<size_t>(lj.width) * lj.components;
                        size_t dstRowElems = static_cast<size_t>(bw) * blockSpp;
                        if (sofRowElems >= dstRowElems && lj.height >= bh) {
                            blockOut.assign(targetCount, 0);
                            for (uint32_t ry = 0; ry < bh; ++ry) {
                                std::memcpy(&blockOut[static_cast<size_t>(ry) * dstRowElems],
                                            &lj.samples[static_cast<size_t>(ry) * sofRowElems],
                                            dstRowElems * sizeof(uint16_t));
                            }
                            return true;
                        }
                        blockOut.assign(lj.samples.begin(), lj.samples.begin() + targetCount);
                        return true;
                    }
                    return false;
                }
                if (lj.isLossyJpeg && blockSpp >= 1) {
                    ExtendedImageInfo tileImg;
                    if (DetailImg100::DecodeEmbeddedWicBuffer(cPtr, cLen, fmtLabel, tileImg) &&
                        tileImg.valid && tileImg.width >= bw && tileImg.height >= bh)
                    {
                        detectedNominalBps = 8;
                        blockOut.assign(static_cast<size_t>(bw) * bh * blockSpp, 0);
                        for (uint32_t ry = 0; ry < bh; ++ry) {
                            for (uint32_t rx = 0; rx < bw; ++rx) {
                                const uint8_t* spx = &tileImg.bgraPixels[(static_cast<size_t>(ry) * tileImg.width + rx) * 4u];
                                size_t di = (static_cast<size_t>(ry) * bw + rx) * blockSpp;
                                if (blockSpp >= 3) {
                                    blockOut[di + 0] = spx[2];
                                    blockOut[di + 1] = spx[1];
                                    blockOut[di + 2] = spx[0];
                                } else {
                                    blockOut[di] = spx[1];
                                }
                            }
                        }
                        return true;
                    }
                }
            }
            return false;
        };

        std::vector<uint16_t> blockBuf;
        if (isStripped) {
            uint32_t rps = (ifd.rowsPerStrip == 0 || ifd.rowsPerStrip > ifd.height) ? ifd.height : ifd.rowsPerStrip;
            uint32_t stripsPerPlane = (ifd.height + rps - 1u) / rps;
            uint32_t numPlanes = planar ? effSpp : 1u;
            if (ifd.stripOffsets.size() < static_cast<size_t>(stripsPerPlane) * numPlanes) return false;

            for (uint32_t pl = 0; pl < numPlanes; ++pl) {
                for (uint32_t s = 0; s < stripsPerPlane; ++s) {
                    size_t idx = static_cast<size_t>(pl) * stripsPerPlane + s;
                    uint32_t off = ifd.stripOffsets[idx];
                    uint32_t y0 = s * rps;
                    uint32_t sh = std::min(rps, ifd.height - y0);
                    uint32_t byteCnt = 0;
                    if (idx < ifd.stripByteCounts.size()) {
                        byteCnt = ifd.stripByteCounts[idx];
                    } else if (off < size) {
                        byteCnt = static_cast<uint32_t>(size - off);
                    }
                    if (off == 0 || byteCnt == 0 || static_cast<uint64_t>(off) + byteCnt > size) return false;
                    if (!decodeBlock(data + off, byteCnt, ifd.width, sh, blockBuf)) return false;

                    if (!planar) {
                        size_t rowElems = static_cast<size_t>(ifd.width) * effSpp;
                        std::memcpy(&fullSamples[static_cast<size_t>(y0) * rowElems],
                                    blockBuf.data(),
                                    static_cast<size_t>(sh) * rowElems * sizeof(uint16_t));
                    } else {
                        for (uint32_t ry = 0; ry < sh; ++ry) {
                            for (uint32_t rx = 0; rx < ifd.width; ++rx) {
                                fullSamples[((static_cast<size_t>(y0 + ry) * ifd.width + rx) * effSpp) + pl] =
                                    blockBuf[static_cast<size_t>(ry) * ifd.width + rx];
                            }
                        }
                    }
                }
            }
        } else {
            uint32_t tilesX = (ifd.width + ifd.tileWidth - 1u) / ifd.tileWidth;
            uint32_t tilesY = (ifd.height + ifd.tileLength - 1u) / ifd.tileLength;
            uint32_t tilesPerPlane = tilesX * tilesY;
            uint32_t numPlanes = planar ? effSpp : 1u;
            if (ifd.tileOffsets.size() < static_cast<size_t>(tilesPerPlane) * numPlanes ||
                ifd.tileByteCounts.size() < static_cast<size_t>(tilesPerPlane) * numPlanes)
            {
                return false;
            }
            for (uint32_t pl = 0; pl < numPlanes; ++pl) {
                for (uint32_t tyIdx = 0; tyIdx < tilesY; ++tyIdx) {
                    for (uint32_t txIdx = 0; txIdx < tilesX; ++txIdx) {
                        size_t idx = (static_cast<size_t>(pl) * tilesY + tyIdx) * tilesX + txIdx;
                        uint32_t off = ifd.tileOffsets[idx];
                        uint32_t byteCnt = ifd.tileByteCounts[idx];
                        if (off == 0 || byteCnt == 0 || static_cast<uint64_t>(off) + byteCnt > size) return false;
                        if (!decodeBlock(data + off, byteCnt, ifd.tileWidth, ifd.tileLength, blockBuf)) return false;

                        uint32_t x0 = txIdx * ifd.tileWidth;
                        uint32_t y0 = tyIdx * ifd.tileLength;
                        uint32_t copyW = std::min(ifd.tileWidth, ifd.width - x0);
                        uint32_t copyH = std::min(ifd.tileLength, ifd.height - y0);
                        for (uint32_t ry = 0; ry < copyH; ++ry) {
                            if (!planar) {
                                std::memcpy(
                                    &fullSamples[(static_cast<size_t>(y0 + ry) * ifd.width + x0) * effSpp],
                                    &blockBuf[static_cast<size_t>(ry) * ifd.tileWidth * effSpp],
                                    static_cast<size_t>(copyW) * effSpp * sizeof(uint16_t));
                            } else {
                                for (uint32_t rx = 0; rx < copyW; ++rx) {
                                    fullSamples[((static_cast<size_t>(y0 + ry) * ifd.width + (x0 + rx)) * effSpp) + pl] =
                                        blockBuf[static_cast<size_t>(ry) * ifd.tileWidth + rx];
                                }
                            }
                        }
                    }
                }
            }
        }

        // Canon .cr2 horizontal slice unslicing (tag 0xC640: [count, sliceWidth, lastSliceWidth])
        if (ifd.hasCr2Slices && (ifd.cr2Slices[0] > 0 || ifd.cr2Slices[2] > 0)) {
            uint32_t sCount = ifd.cr2Slices[0];
            uint32_t sWidth = ifd.cr2Slices[1];
            uint32_t sLast  = ifd.cr2Slices[2];
            uint32_t rowElems = ifd.width * effSpp;
            if (effSpp > 1 && sCount * sWidth + sLast == ifd.width) {
                sWidth *= effSpp;
                sLast  *= effSpp;
            }
            if ((sCount == 0 || sWidth > 0) && sLast > 0 && sCount * sWidth + sLast == rowElems) {
                std::vector<uint16_t> unsliced(fullSamples.size(), 0);
                size_t regularSamples = static_cast<size_t>(sCount) * ifd.height * sWidth;
                size_t sliceSamples   = static_cast<size_t>(ifd.height) * sWidth;
                for (size_t j = 0; j < fullSamples.size(); ++j) {
                    size_t dstY = 0, dstX = 0;
                    if (j < regularSamples && sliceSamples > 0) {
                        size_t sliceIdx = j / sliceSamples;
                        size_t rem = j % sliceSamples;
                        dstY = rem / sWidth;
                        dstX = sliceIdx * sWidth + (rem % sWidth);
                    } else {
                        size_t rem = j - regularSamples;
                        dstY = rem / sLast;
                        dstX = static_cast<size_t>(sCount) * sWidth + (rem % sLast);
                    }
                    if (dstY < ifd.height && dstX < rowElems) {
                        unsliced[dstY * rowElems + dstX] = fullSamples[j];
                    }
                }
                fullSamples = std::move(unsliced);
            }
        }

        DemosaicAndToneMapSensor(
            fullSamples, ifd.width, ifd.height, effSpp, ifd.photometric, detectedNominalBps,
            ifd.cfaRows, ifd.cfaCols, ifd.cfaPattern, ifd.blRows, ifd.blCols, ifd.blackLevels,
            ifd.whiteLevel, ifd.hasNeutral, ifd.neutral, ifd.hasActiveArea, ifd.activeArea,
            ifd.orientation, bestRawW, bestRawH, fmtLabel, outInfo, camToSrgbPtr);
        return true;
    };

    for (const auto& cand : candidates) {
        if (tryDecodeIfd(cand)) return true;
    }

    // Fallback scan for any embedded validated JPEG/PNG stream (e.g. Olympus MakerNote 0x2020 preview)
    if (FindAndDecodeBestEmbeddedRaster(data, size, fmtLabel, outInfo, 8)) {
        if (bestRawW > outInfo.origWidth) { outInfo.origWidth = bestRawW; outInfo.origHeight = bestRawH; }
        return true;
    }

    // Final WIC fallback (e.g. Microsoft Raw Image Extension for proprietary vendor compressed streams)
    if (DetailImg100::DecodeEmbeddedWicBuffer(data, size, fmtLabel, outInfo)) {
        return true;
    }

    return false;
}

// Canon CR3 recursive ISOBMFF box walker (moov/trak/mdia/minf/stbl chunk offsets + Canon uuid PRVW & THMB)
inline void WalkCr3IsobmffBoxes(
    const uint8_t* fileData, size_t fileSize,
    size_t startPos, size_t endPos, int depth,
    const std::wstring& label,
    ExtendedImageInfo& bestImg,
    uint32_t& metaOrigW, uint32_t& metaOrigH, uint16_t& metaOrient)
{
    if (depth > 10 || startPos + 8 > endPos || endPos > fileSize) return;

    static const uint8_t kUuidCanonMeta[16] = {
        0x85, 0xC0, 0xB6, 0x87, 0x82, 0x0F, 0x11, 0xE0,
        0x81, 0x11, 0xF4, 0xCE, 0x46, 0x2B, 0x6A, 0x48
    };
    static const uint8_t kUuidCanonPrvw[16] = {
        0xEA, 0xF4, 0x2B, 0x5E, 0x1C, 0x98, 0x4B, 0x88,
        0xB9, 0xFB, 0xB7, 0xDC, 0x40, 0x6E, 0x4D, 0x16
    };

    auto considerJpegSlice = [&](const uint8_t* jp, size_t jlen) {
        if (jlen < 32 || jp < fileData || static_cast<size_t>(jp - fileData) + jlen > fileSize) return;
        if (jp[0] != 0xFF || jp[1] != 0xD8 || jp[2] != 0xFF) return;
        ExtendedImageInfo cand;
        if (DetailImg100::DecodeEmbeddedWicBuffer(jp, jlen, label, cand) &&
            cand.valid && cand.width > 0 && cand.height > 0 && !cand.bgraPixels.empty())
        {
            uint64_t newArea = static_cast<uint64_t>(cand.width) * cand.height;
            uint64_t curArea = bestImg.valid ? (static_cast<uint64_t>(bestImg.width) * bestImg.height) : 0ull;
            if (newArea > curArea) {
                bestImg = std::move(cand);
            }
        }
    };

    // Track stbl chunk offset + sample size within a single stbl box
    std::vector<uint64_t> stblOffsets;
    std::vector<uint32_t> stblSizes;

    size_t pos = startPos;
    while (pos + 8 <= endPos) {
        uint64_t boxLen = DetailImg100::RdU32BE(fileData + pos);
        size_t hdrLen = 8;
        if (boxLen == 1) {
            if (pos + 16 > endPos) break;
            boxLen = (static_cast<uint64_t>(DetailImg100::RdU32BE(fileData + pos + 8)) << 32) |
                     DetailImg100::RdU32BE(fileData + pos + 12);
            hdrLen = 16;
        } else if (boxLen == 0) {
            boxLen = endPos - pos;
        }
        if (boxLen < hdrLen || pos + boxLen > endPos) break;

        const char* btype = reinterpret_cast<const char*>(fileData + pos + 4);
        size_t payloadStart = pos + hdrLen;
        size_t boxEnd = pos + static_cast<size_t>(boxLen);
        size_t payloadLen = boxEnd - payloadStart;

        if (std::memcmp(btype, "moov", 4) == 0 ||
            std::memcmp(btype, "trak", 4) == 0 ||
            std::memcmp(btype, "mdia", 4) == 0 ||
            std::memcmp(btype, "minf", 4) == 0 ||
            std::memcmp(btype, "stbl", 4) == 0 ||
            std::memcmp(btype, "dinf", 4) == 0)
        {
            WalkCr3IsobmffBoxes(fileData, fileSize, payloadStart, boxEnd, depth + 1, label, bestImg, metaOrigW, metaOrigH, metaOrient);
        } else if (std::memcmp(btype, "uuid", 4) == 0 && payloadLen >= 16) {
            const uint8_t* uuid = fileData + payloadStart;
            if (std::memcmp(uuid, kUuidCanonMeta, 16) == 0) {
                WalkCr3IsobmffBoxes(fileData, fileSize, payloadStart + 16, boxEnd, depth + 1, label, bestImg, metaOrigW, metaOrigH, metaOrient);
            } else if (std::memcmp(uuid, kUuidCanonPrvw, 16) == 0) {
                // Canon preview uuid box has 8-byte flags/padding after UUID, then child PRVW box
                if (payloadLen > 24) {
                    WalkCr3IsobmffBoxes(fileData, fileSize, payloadStart + 24, boxEnd, depth + 1, label, bestImg, metaOrigW, metaOrigH, metaOrient);
                }
                ExtendedImageInfo cand;
                if (FindAndDecodeBestEmbeddedRaster(fileData + payloadStart + 16, payloadLen - 16, label, cand)) {
                    if (!bestImg.valid || static_cast<uint64_t>(cand.width) * cand.height > static_cast<uint64_t>(bestImg.width) * bestImg.height) {
                        bestImg = std::move(cand);
                    }
                }
            } else {
                ExtendedImageInfo cand;
                if (FindAndDecodeBestEmbeddedRaster(fileData + payloadStart + 16, payloadLen - 16, label, cand)) {
                    if (!bestImg.valid || static_cast<uint64_t>(cand.width) * cand.height > static_cast<uint64_t>(bestImg.width) * bestImg.height) {
                        bestImg = std::move(cand);
                    }
                }
            }
        } else if (std::memcmp(btype, "PRVW", 4) == 0 && payloadLen >= 16) {
            // Canon PRVW: [reserved:6][w:2][h:2][unk:2][jpegSize:4][jpeg...]
            uint32_t jsz = DetailImg100::RdU32BE(fileData + payloadStart + 12);
            if (jsz > 32 && 16u + static_cast<size_t>(jsz) <= payloadLen) {
                considerJpegSlice(fileData + payloadStart + 16, jsz);
            }
            ExtendedImageInfo cand;
            if (FindAndDecodeBestEmbeddedRaster(fileData + payloadStart, payloadLen, label, cand)) {
                if (!bestImg.valid || static_cast<uint64_t>(cand.width) * cand.height > static_cast<uint64_t>(bestImg.width) * bestImg.height) {
                    bestImg = std::move(cand);
                }
            }
        } else if (std::memcmp(btype, "THMB", 4) == 0 && payloadLen >= 16) {
            // Canon THMB: [verFlags:4][w:2][h:2][jpegSize:4][unk:4][jpeg...] (or jpeg at +12)
            uint32_t jsz = DetailImg100::RdU32BE(fileData + payloadStart + 8);
            if (jsz > 32 && 16u + static_cast<size_t>(jsz) <= payloadLen) {
                considerJpegSlice(fileData + payloadStart + 16, jsz);
            } else if (jsz > 32 && 12u + static_cast<size_t>(jsz) <= payloadLen) {
                considerJpegSlice(fileData + payloadStart + 12, jsz);
            }
            ExtendedImageInfo cand;
            if (FindAndDecodeBestEmbeddedRaster(fileData + payloadStart, payloadLen, label, cand)) {
                if (!bestImg.valid || static_cast<uint64_t>(cand.width) * cand.height > static_cast<uint64_t>(bestImg.width) * bestImg.height) {
                    bestImg = std::move(cand);
                }
            }
        } else if (std::memcmp(btype, "CMT1", 4) == 0 && payloadLen >= 16) {
            // Canon IFD0 TIFF header inside CR3
            const uint8_t* tp = fileData + payloadStart;
            bool tbe = (tp[0] == 'M' && tp[1] == 'M');
            if ((tp[0] == 'I' && tp[1] == 'I') || tbe) {
                uint32_t ifd0 = Rd32(tp + 4, tbe);
                if (ifd0 >= 8 && ifd0 + 2 <= payloadLen) {
                    uint16_t ne = Rd16(tp + ifd0, tbe);
                    if (ifd0 + 2u + static_cast<size_t>(ne) * 12u <= payloadLen) {
                        std::vector<uint32_t> vals;
                        for (uint16_t e = 0; e < ne; ++e) {
                            const uint8_t* ent = tp + ifd0 + 2u + static_cast<size_t>(e) * 12u;
                            uint16_t tag = Rd16(ent, tbe);
                            if (tag == 0x0100 && ReadTiffU32Values(tp, payloadLen, ent, tbe, vals, 1)) metaOrigW = std::max(metaOrigW, vals[0]);
                            else if (tag == 0x0101 && ReadTiffU32Values(tp, payloadLen, ent, tbe, vals, 1)) metaOrigH = std::max(metaOrigH, vals[0]);
                            else if (tag == 0x0112 && ReadTiffU32Values(tp, payloadLen, ent, tbe, vals, 1) && vals[0] >= 1 && vals[0] <= 8) {
                                metaOrient = static_cast<uint16_t>(vals[0]);
                            }
                        }
                    }
                }
            }
        } else if (std::memcmp(btype, "stco", 4) == 0 && payloadLen >= 8) {
            uint32_t cnt = DetailImg100::RdU32BE(fileData + payloadStart + 4);
            cnt = std::min<uint32_t>(cnt, 64u);
            for (uint32_t k = 0; k < cnt && 8u + (k + 1u) * 4u <= payloadLen; ++k) {
                stblOffsets.push_back(DetailImg100::RdU32BE(fileData + payloadStart + 8u + k * 4u));
            }
        } else if (std::memcmp(btype, "co64", 4) == 0 && payloadLen >= 8) {
            uint32_t cnt = DetailImg100::RdU32BE(fileData + payloadStart + 4);
            cnt = std::min<uint32_t>(cnt, 64u);
            for (uint32_t k = 0; k < cnt && 8u + (k + 1u) * 8u <= payloadLen; ++k) {
                uint64_t off64 = (static_cast<uint64_t>(DetailImg100::RdU32BE(fileData + payloadStart + 8u + k * 8u)) << 32) |
                                 DetailImg100::RdU32BE(fileData + payloadStart + 12u + k * 8u);
                stblOffsets.push_back(off64);
            }
        } else if (std::memcmp(btype, "stsz", 4) == 0 && payloadLen >= 12) {
            uint32_t fixedSz = DetailImg100::RdU32BE(fileData + payloadStart + 4);
            uint32_t cnt     = DetailImg100::RdU32BE(fileData + payloadStart + 8);
            cnt = std::min<uint32_t>(cnt, 64u);
            if (fixedSz > 0) {
                for (uint32_t k = 0; k < std::max(1u, cnt); ++k) stblSizes.push_back(fixedSz);
            } else {
                for (uint32_t k = 0; k < cnt && 12u + (k + 1u) * 4u <= payloadLen; ++k) {
                    stblSizes.push_back(DetailImg100::RdU32BE(fileData + payloadStart + 12u + k * 4u));
                }
            }
        }

        pos = boxEnd;
    }

    size_t nChunks = std::min(stblOffsets.size(), stblSizes.size());
    for (size_t k = 0; k < nChunks; ++k) {
        if (stblOffsets[k] < fileSize && stblSizes[k] > 32 && stblOffsets[k] + stblSizes[k] <= fileSize) {
            considerJpegSlice(fileData + static_cast<size_t>(stblOffsets[k]), stblSizes[k]);
        }
    }
}

} // namespace DetailImg200

// ============================================================================
// #1–#9. Camera RAW Family (.cr2, .cr3, .nef, .arw, .dng, .raf, .orf, .rw2, .pef)
// ============================================================================
inline bool DecodeCameraRaw200(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    // #2. Canon CR3 — ISOBMFF ('ftypcrx ')
    if (extLower == L".cr3" || (size >= 16 && std::memcmp(data + 4, "ftypcrx ", 8) == 0)) {
        if (size < 16 || std::memcmp(data + 4, "ftyp", 4) != 0) return false;
        std::wstring label = L"Canon CR3 RAW (.cr3)";

        ExtendedImageInfo bestImg;
        uint32_t metaW = 0, metaH = 0;
        uint16_t metaOrient = 1;
        DetailImg200::WalkCr3IsobmffBoxes(data, size, 0, size, 0, label, bestImg, metaW, metaH, metaOrient);

        // Unpack Canon CR3 'CRAW' + 'CMP1' RAW sensor track directly when no larger embedded JPEG preview exists
        std::vector<DetailImg200::Cr3Cmp1TrackInfo> cmpTracks;
        DetailImg200::Cr3Cmp1TrackInfo dummyTr;
        DetailImg200::ScanCr3Cmp1InTrak(data, size, 0, size, 0, dummyTr, cmpTracks);
        for (const auto& ct : cmpTracks) {
            uint64_t ctArea = static_cast<uint64_t>(ct.width) * ct.height;
            uint64_t curArea = bestImg.valid ? (static_cast<uint64_t>(bestImg.width) * bestImg.height) : 0ull;
            if (ctArea <= curArea || ct.width < 2 || ct.height < 2) continue;
            if (ct.mdatOffset >= size || ct.mdatSize > size - static_cast<size_t>(ct.mdatOffset)) continue;

            const uint8_t* slice = data + static_cast<size_t>(ct.mdatOffset);
            size_t sliceLen = ct.mdatSize;
            std::vector<uint16_t> cfaSamples;
            uint16_t bps = ct.bitDepth;

            DetailImg200::LosslessJpegResult lj;
            if (DetailImg200::DecodeLosslessJpegSOF3(slice, sliceLen, lj)) {
                bps = lj.precision;
                size_t expectedPx = static_cast<size_t>(ct.width) * ct.height;
                if (lj.components == 4 && lj.width * 2u == ct.width && lj.height * 2u == ct.height) {
                    cfaSamples.assign(expectedPx, 0);
                    for (uint32_t qy = 0; qy < lj.height; ++qy) {
                        for (uint32_t qx = 0; qx < lj.width; ++qx) {
                            const uint16_t* q = &lj.samples[(static_cast<size_t>(qy) * lj.width + qx) * 4u];
                            cfaSamples[static_cast<size_t>(qy * 2u + 0u) * ct.width + (qx * 2u + 0u)] = q[0];
                            cfaSamples[static_cast<size_t>(qy * 2u + 0u) * ct.width + (qx * 2u + 1u)] = q[1];
                            cfaSamples[static_cast<size_t>(qy * 2u + 1u) * ct.width + (qx * 2u + 0u)] = q[2];
                            cfaSamples[static_cast<size_t>(qy * 2u + 1u) * ct.width + (qx * 2u + 1u)] = q[3];
                        }
                    }
                } else if (lj.samples.size() >= expectedPx) {
                    cfaSamples.assign(lj.samples.begin(), lj.samples.begin() + expectedPx);
                }
            } else {
                DetailImg200::UnpackRawSamplesFromBytes(slice, sliceLen, ct.width, ct.height, 1, bps, true, 1, cfaSamples);
            }

            if (cfaSamples.size() == static_cast<size_t>(ct.width) * ct.height) {
                std::vector<uint8_t> cfaPat = { 0, 1, 1, 2 };
                if (ct.cfaLayout == 1)      cfaPat = { 1, 0, 2, 1 };
                else if (ct.cfaLayout == 2) cfaPat = { 1, 2, 0, 1 };
                else if (ct.cfaLayout == 3) cfaPat = { 2, 1, 1, 0 };
                float neutral[3] = { 1.0f, 1.0f, 1.0f };
                uint32_t act[4] = {};
                ExtendedImageInfo rawCand;
                DetailImg200::DemosaicAndToneMapSensor(
                    cfaSamples, ct.width, ct.height, 1, 32803, bps,
                    2, 2, cfaPat, 0, 0, {}, static_cast<float>((1u << bps) - 1u),
                    false, neutral, false, act, 1, std::max(metaW, ct.width), std::max(metaH, ct.height),
                    label, rawCand);
                if (rawCand.valid) {
                    bestImg = std::move(rawCand);
                    break;
                }
            }
        }

        if (!bestImg.valid) {
            DetailImg200::FindAndDecodeBestEmbeddedRaster(data, size, label, bestImg, 16);
        }
        if (bestImg.valid) {
            if (metaOrient > 1 && metaOrient <= 8) {
                DetailImg200::ApplyExifOrientationBGRA(metaOrient, bestImg.width, bestImg.height, bestImg.bgraPixels);
                if (metaOrient >= 5) std::swap(metaW, metaH);
            }
            if (metaW > bestImg.origWidth) { bestImg.origWidth = metaW; bestImg.origHeight = metaH; }
            outInfo = std::move(bestImg);
            return true;
        }
        return DetailImg100::DecodeEmbeddedWicBuffer(data, size, label, outInfo);
    }

    // #6. Fujifilm RAF — "FUJIFILMCCD-RAW "
    if (extLower == L".raf" || (size >= 16 && std::memcmp(data, "FUJIFILMCCD-RAW", 15) == 0)) {
        if (size < 92 || std::memcmp(data, "FUJIFILMCCD-RAW", 15) != 0) return false;
        std::wstring label = L"Fujifilm RAF RAW (.raf)";

        // 1. Embedded JPEG offset & length at bytes 84..91 BE (0x54..0x5B)
        uint32_t jpgOff = DetailImg100::RdU32BE(data + 84);
        uint32_t jpgLen = DetailImg100::RdU32BE(data + 88);
        if (jpgOff > 0 && jpgLen > 32 && static_cast<uint64_t>(jpgOff) + jpgLen <= size) {
            if (DetailImg100::DecodeEmbeddedWicBuffer(data + jpgOff, jpgLen, label, outInfo)) {
                return true;
            }
        }

        // 2. Fujifilm RAF raw CFA header (0x5C..0x5F BE) + CFA sensor data (0x64..0x6B BE)
        if (size >= 108) {
            uint32_t cfaHdrOff  = DetailImg100::RdU32BE(data + 92);
            uint32_t cfaDataOff = DetailImg100::RdU32BE(data + 100);
            uint32_t cfaDataLen = DetailImg100::RdU32BE(data + 104);
            if (cfaDataOff > 0 && cfaDataLen >= 16 && static_cast<uint64_t>(cfaDataOff) + cfaDataLen <= size) {
                // Some Fujifilm RAF sensors store a TIFF/CFA container directly at cfaDataOff
                if (DetailImg200::ParseTiffRawIfds(data + cfaDataOff, cfaDataLen, label, outInfo)) {
                    return true;
                }
            }
            if (cfaHdrOff > 0 && static_cast<uint64_t>(cfaHdrOff) + 8 <= size &&
                cfaDataOff > 0 && cfaDataLen > 0 && static_cast<uint64_t>(cfaDataOff) + cfaDataLen <= size)
            {
                uint32_t numRecs = DetailImg100::RdU32BE(data + cfaHdrOff);
                size_t rp = static_cast<size_t>(cfaHdrOff) + 4u;
                uint32_t rawW = 0, rawH = 0;
                std::vector<uint8_t> cfaPat = { 0, 1, 1, 2 };
                uint16_t cfaRows = 2, cfaCols = 2;
                for (uint32_t r = 0; r < std::min<uint32_t>(numRecs, 128u) && rp + 4 <= size; ++r) {
                    uint16_t rTag = DetailImg100::RdU16BE(data + rp);
                    uint16_t rLen = DetailImg100::RdU16BE(data + rp + 2);
                    rp += 4;
                    if (rp + rLen > size) break;
                    if (rTag == 0x0100 && rLen >= 4) {
                        rawH = DetailImg100::RdU16BE(data + rp);
                        rawW = DetailImg100::RdU16BE(data + rp + 2);
                    } else if (rTag == 0x0131 && rLen == 36) {
                        // Fujifilm 6x6 X-Trans filter array (0=R, 1=G, 2=B)
                        cfaRows = 6; cfaCols = 6;
                        cfaPat.assign(data + rp, data + rp + 36);
                    }
                    rp += rLen;
                }
                if (rawW >= 2 && rawH >= 2 && rawW <= 16384 && rawH <= 16384) {
                    size_t pxCount = static_cast<size_t>(rawW) * rawH;
                    std::vector<uint16_t> samples;
                    uint16_t bps = 14;
                    if (cfaDataLen >= pxCount * 2u) {
                        // Detect endianness: Fuji raw uncompressed 16-bit is typically LE or BE with values < 16384
                        size_t leSmall = 0, beSmall = 0;
                        size_t probeN = std::min<size_t>(pxCount, 256u);
                        for (size_t k = 0; k < probeN; ++k) {
                            if (DetailImg100::RdU16LE(data + cfaDataOff + k * 2u) <= 16383u) ++leSmall;
                            if (DetailImg100::RdU16BE(data + cfaDataOff + k * 2u) <= 16383u) ++beSmall;
                        }
                        bool useBe = (beSmall > leSmall);
                        bps = 16;
                        DetailImg200::UnpackRawSamplesFromBytes(data + cfaDataOff, cfaDataLen, rawW, rawH, 1, 16, useBe, 1, samples);
                    } else if (cfaDataLen >= (pxCount * 14u + 7u) / 8u) {
                        bps = 14;
                        DetailImg200::UnpackRawSamplesFromBytes(data + cfaDataOff, cfaDataLen, rawW, rawH, 1, 14, true, 1, samples);
                    } else if (cfaDataLen >= (pxCount * 12u + 7u) / 8u) {
                        bps = 12;
                        DetailImg200::UnpackRawSamplesFromBytes(data + cfaDataOff, cfaDataLen, rawW, rawH, 1, 12, true, 1, samples);
                    }
                    if (samples.size() == pxCount) {
                        float neutral[3] = { 1.0f, 1.0f, 1.0f };
                        uint32_t act[4] = {};
                        DetailImg200::DemosaicAndToneMapSensor(
                            samples, rawW, rawH, 1, 32803, bps,
                            cfaRows, cfaCols, cfaPat, 0, 0, {}, 0.0f,
                            false, neutral, false, act, 1, rawW, rawH, label, outInfo);
                        return true;
                    }
                }
            }
        }

        if (DetailImg200::FindAndDecodeBestEmbeddedRaster(data, size, label, outInfo, 64)) return true;
        return DetailImg100::DecodeEmbeddedWicBuffer(data, size, label, outInfo);
    }

    // #1, #3, #4, #5, #7, #8, #9: TIFF-structured RAWs & DNG
    std::wstring label = L"Camera RAW Image";
    if (extLower == L".cr2")      label = L"Canon CR2 RAW (.cr2)";
    else if (extLower == L".nef") label = L"Nikon NEF RAW (.nef)";
    else if (extLower == L".arw") label = L"Sony Alpha ARW RAW (.arw)";
    else if (extLower == L".dng") label = L"Adobe Digital Negative (.dng)";
    else if (extLower == L".orf") label = L"Olympus ORF RAW (.orf)";
    else if (extLower == L".rw2") label = L"Panasonic Lumix RW2 RAW (.rw2)";
    else if (extLower == L".pef") label = L"Pentax PEF RAW (.pef)";
    else return false;

    return DetailImg200::ParseTiffRawIfds(data, size, label, outInfo);
}

inline bool DecodeCameraRawOrPreview(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    return DecodeCameraRaw200(data, size, extLower, outInfo);
}

inline bool DecodeEmfOrWmf(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo);

namespace DetailImg200 {

inline bool DecodeDibBufferToInfo(
    const uint8_t* dib, size_t dibLen,
    const std::wstring& label,
    ExtendedImageInfo& outInfo)
{
    if (dibLen < 40) return false;
    uint32_t biSize = DetailImg100::RdU32LE(dib + 0);
    if (biSize != 40 && biSize != 108 && biSize != 124) return false;
    if (biSize > dibLen) return false;

    int32_t  biWidth  = RdS32(dib + 4, false);
    int32_t  biHeight = RdS32(dib + 8, false);
    uint16_t biPlanes = DetailImg100::RdU16LE(dib + 12);
    uint16_t biBits   = DetailImg100::RdU16LE(dib + 14);
    uint32_t biComp   = DetailImg100::RdU32LE(dib + 16);
    uint32_t biClr    = DetailImg100::RdU32LE(dib + 32);

    if (biWidth <= 0 || biWidth > 16384 || biHeight == 0 || std::abs(biHeight) > 16384 || biPlanes != 1) return false;
    if (biBits != 1 && biBits != 4 && biBits != 8 && biBits != 16 && biBits != 24 && biBits != 32) return false;

    uint32_t maxPal = (biBits <= 8) ? (1u << biBits) : 0u;
    uint32_t palEntries = (biBits <= 8)
        ? ((biClr > 0 && biClr <= maxPal) ? biClr : maxPal)
        : ((biComp == 3u && biSize == 40u) ? 3u : 0u);
    uint32_t bfOffBits = 14u + biSize + palEntries * 4u;
    if (bfOffBits - 14u > dibLen) return false;

    // 1. Prepend valid 14-byte 'BM' BITMAPFILEHEADER and try WIC
    std::vector<uint8_t> bmp(14u + dibLen, 0);
    bmp[0] = 'B';
    bmp[1] = 'M';
    uint32_t fileSz = static_cast<uint32_t>(bmp.size());
    std::memcpy(bmp.data() + 2, &fileSz, 4);
    std::memcpy(bmp.data() + 10, &bfOffBits, 4);
    std::memcpy(bmp.data() + 14, dib, dibLen);
    if (DetailImg100::DecodeEmbeddedWicBuffer(bmp.data(), bmp.size(), label, outInfo) &&
        outInfo.valid && outInfo.width > 0 && outInfo.height > 0)
    {
        return true;
    }

    // 2. Direct pure-C++ DIB unpacker fallback (handles BI_RGB=0 & BI_BITFIELDS=3)
    if (biComp != 0u && biComp != 3u) return false;
    const bool topDown = (biHeight < 0);
    const uint32_t w = static_cast<uint32_t>(biWidth);
    const uint32_t h = static_cast<uint32_t>(std::abs(biHeight));
    const size_t stride = ((static_cast<size_t>(w) * biBits + 31u) / 32u) * 4u;
    const size_t pixOffset = biSize + palEntries * 4u;
    if (pixOffset + stride * h > dibLen) return false;

    const uint8_t* pal = dib + biSize;
    const uint8_t* pixBase = dib + pixOffset;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = label;
    outInfo.bgraPixels.resize(static_cast<size_t>(w) * h * 4u);

    for (uint32_t y = 0; y < h; ++y) {
        uint32_t sy = topDown ? y : (h - 1u - y);
        const uint8_t* row = pixBase + static_cast<size_t>(sy) * stride;
        uint8_t* dstRow = &outInfo.bgraPixels[static_cast<size_t>(y) * w * 4u];
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t r = 0, g = 0, b = 0, a = 255;
            if (biBits == 1) {
                uint8_t idx = (row[x >> 3] >> (7u - (x & 7u))) & 1u;
                if (idx < palEntries) { b = pal[idx * 4 + 0]; g = pal[idx * 4 + 1]; r = pal[idx * 4 + 2]; }
            } else if (biBits == 4) {
                uint8_t idx = (x & 1u) ? (row[x >> 1] & 0x0Fu) : ((row[x >> 1] >> 4) & 0x0Fu);
                if (idx < palEntries) { b = pal[idx * 4 + 0]; g = pal[idx * 4 + 1]; r = pal[idx * 4 + 2]; }
            } else if (biBits == 8) {
                uint8_t idx = row[x];
                if (idx < palEntries) { b = pal[idx * 4 + 0]; g = pal[idx * 4 + 1]; r = pal[idx * 4 + 2]; }
            } else if (biBits == 16) {
                uint16_t v = DetailImg100::RdU16LE(row + x * 2u);
                if (biComp == 3u && palEntries >= 3 && DetailImg100::RdU32LE(pal + 4) == 0x07E0u) {
                    r = static_cast<uint8_t>(((v >> 11) & 0x1Fu) * 255u / 31u);
                    g = static_cast<uint8_t>(((v >> 5)  & 0x3Fu) * 255u / 63u);
                    b = static_cast<uint8_t>((v & 0x1Fu) * 255u / 31u);
                } else {
                    r = static_cast<uint8_t>(((v >> 10) & 0x1Fu) * 255u / 31u);
                    g = static_cast<uint8_t>(((v >> 5)  & 0x1Fu) * 255u / 31u);
                    b = static_cast<uint8_t>((v & 0x1Fu) * 255u / 31u);
                }
            } else if (biBits == 24) {
                b = row[x * 3u + 0];
                g = row[x * 3u + 1];
                r = row[x * 3u + 2];
            } else {
                b = row[x * 4u + 0];
                g = row[x * 4u + 1];
                r = row[x * 4u + 2];
                a = 255;
            }
            dstRow[x * 4u + 0] = b;
            dstRow[x * 4u + 1] = g;
            dstRow[x * 4u + 2] = r;
            dstRow[x * 4u + 3] = a;
        }
    }
    outInfo.valid = true;
    return true;
}

inline bool TryDecodeBestZipThumbnail(
    const uint8_t* data, size_t size,
    const std::wstring& label,
    ExtendedImageInfo& outInfo)
{
    std::vector<std::string> names;
    CollectAllZipEntryNames(data, size, names);
    if (names.empty()) return false;

    struct ScoredEntry {
        int score = 0;
        std::string origName;
    };
    std::vector<ScoredEntry> matches;

    for (const auto& orig : names) {
        std::string norm = orig;
        for (char& ch : norm) {
            if (ch == '\\') ch = '/';
            else if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
        }
        while (!norm.empty() && norm[0] == '/') norm.erase(norm.begin());

        auto endsWith = [&](std::string_view sfx) -> bool {
            return norm.size() >= sfx.size() && norm.compare(norm.size() - sfx.size(), sfx.size(), sfx) == 0;
        };
        bool isImgExt = endsWith(".png") || endsWith(".bmp") || endsWith(".jpg") ||
                        endsWith(".jpeg") || endsWith(".tif") || endsWith(".tiff");
        if (!isImgExt) continue;

        int score = 10;
        if (norm == "metadata/thumbnails/thumbnail.bmp")      score = 100;
        else if (norm == "previews/thumbnail.png")            score = 99;
        else if (norm == "previews/page1.png")                score = 98;
        else if (norm == "metadata/thumbnails/thumbnail.png") score = 97;
        else if (norm == "thumbnail.png" || norm == "preview.png" || norm == "thumbnail.bmp") score = 95;
        else if (norm.find("previews/") != std::string::npos || norm.find("thumbnails/") != std::string::npos) score = 90;
        else if (norm.find("thumb") != std::string::npos || norm.find("preview") != std::string::npos) score = 80;
        matches.push_back({ score, orig });
    }

    std::sort(matches.begin(), matches.end(), [](const ScoredEntry& a, const ScoredEntry& b) {
        return a.score > b.score;
    });

    for (const auto& m : matches) {
        std::vector<uint8_t> thumb;
        if (ZipDeflate::ExtractZipEntryFromMemory(data, size, m.origName.c_str(), thumb) && !thumb.empty()) {
            if (DetailImg100::DecodeEmbeddedWicBuffer(thumb.data(), thumb.size(), label, outInfo) && outInfo.valid) {
                return true;
            }
            if (thumb.size() > 54 && thumb[0] == 'B' && thumb[1] == 'M') {
                if (DecodeDibBufferToInfo(thumb.data() + 14, thumb.size() - 14, label, outInfo)) return true;
            } else if (DecodeDibBufferToInfo(thumb.data(), thumb.size(), label, outInfo)) {
                return true;
            }
        }
    }
    return false;
}

// Locale-independent ASCII number helpers (immune to European ',' decimal locales)
inline bool ParseAsciiFloatToken(std::string_view tok, float& outVal) {
    if (tok.empty()) return false;
    size_t i = 0;
    double sign = 1.0;
    if (tok[i] == '-') { sign = -1.0; ++i; }
    else if (tok[i] == '+') { ++i; }
    if (i >= tok.size()) return false;

    double val = 0.0;
    bool hasDigits = false;
    while (i < tok.size() && tok[i] >= '0' && tok[i] <= '9') {
        val = val * 10.0 + static_cast<double>(tok[i] - '0');
        hasDigits = true;
        ++i;
    }
    if (i < tok.size() && tok[i] == '.') {
        ++i;
        double frac = 0.1;
        while (i < tok.size() && tok[i] >= '0' && tok[i] <= '9') {
            val += static_cast<double>(tok[i] - '0') * frac;
            frac *= 0.1;
            hasDigits = true;
            ++i;
        }
    }
    if (!hasDigits) return false;
    if (i < tok.size() && (tok[i] == 'e' || tok[i] == 'E')) {
        ++i;
        int expSign = 1;
        if (i < tok.size() && tok[i] == '-') { expSign = -1; ++i; }
        else if (i < tok.size() && tok[i] == '+') { ++i; }
        if (i >= tok.size() || tok[i] < '0' || tok[i] > '9') return false;
        int expVal = 0;
        while (i < tok.size() && tok[i] >= '0' && tok[i] <= '9') {
            if (expVal < 1000) expVal = expVal * 10 + (tok[i] - '0');
            ++i;
        }
        val *= std::pow(10.0, static_cast<double>(expSign * std::clamp(expVal, -300, 300)));
    }
    if (i != tok.size()) return false;
    outVal = static_cast<float>(sign * val);
    return true;
}

inline float ParseAsciiFloatOrZero(std::string_view tok) {
    float v = 0.0f;
    ParseAsciiFloatToken(tok, v);
    return v;
}

inline int32_t ParseAsciiIntOrZero(std::string_view tok) {
    size_t i = 0;
    while (i < tok.size() && (tok[i] == ' ' || tok[i] == '\t' || tok[i] == '\r' || tok[i] == '\n')) ++i;
    int64_t sign = 1;
    if (i < tok.size() && tok[i] == '-') { sign = -1; ++i; }
    else if (i < tok.size() && tok[i] == '+') { ++i; }
    int64_t val = 0;
    while (i < tok.size() && tok[i] >= '0' && tok[i] <= '9') {
        val = val * 10 + (tok[i] - '0');
        if (val > 0x7FFFFFFFll) break;
        ++i;
    }
    return static_cast<int32_t>(std::clamp<int64_t>(sign * val, -2147483647ll, 2147483647ll));
}

inline size_t ParseFloatsFromSv(std::string_view sv, float* outVals, size_t maxCount) {
    size_t n = 0;
    size_t i = 0;
    while (i < sv.size() && n < maxCount) {
        while (i < sv.size() && (sv[i] == ' ' || sv[i] == '\t' || sv[i] == '\r' || sv[i] == '\n')) ++i;
        size_t s = i;
        while (i < sv.size() && sv[i] != ' ' && sv[i] != '\t' && sv[i] != '\r' && sv[i] != '\n') ++i;
        if (i > s) {
            float v = 0.0f;
            if (!ParseAsciiFloatToken(sv.substr(s, i - s), v)) break;
            outVals[n++] = v;
        }
    }
    return n;
}

inline bool WalkRiffCdrForDisp(
    const uint8_t* fileData, size_t fileSize,
    size_t startPos, size_t endPos, int depth,
    const std::wstring& label,
    ExtendedImageInfo& outInfo)
{
    if (depth > 8 || startPos + 8 > endPos || endPos > fileSize) return false;
    size_t pos = startPos;
    while (pos + 8 <= endPos) {
        const uint8_t* hdr = fileData + pos;
        uint32_t chunkLen = DetailImg100::RdU32LE(hdr + 4);
        size_t payloadPos = pos + 8;
        if (chunkLen > endPos - payloadPos) break;

        if (std::memcmp(hdr, "DISP", 4) == 0 && chunkLen >= 40) {
            const uint8_t* cp = fileData + payloadPos;
            // Corel RIFF DISP typically has a 4-byte CF_DIB prefix (offset 4), or BITMAPINFOHEADER at offset 0/8/12
            static const size_t kProbeOffsets[] = { 4, 0, 8, 2, 6, 10, 12 };
            for (size_t off : kProbeOffsets) {
                if (off + 40 <= chunkLen) {
                    if (DecodeDibBufferToInfo(cp + off, chunkLen - off, label, outInfo)) {
                        return true;
                    }
                }
            }
        } else if ((std::memcmp(hdr, "LIST", 4) == 0 || std::memcmp(hdr, "RIFF", 4) == 0) && chunkLen >= 4) {
            if (WalkRiffCdrForDisp(fileData, fileSize, payloadPos + 4, payloadPos + chunkLen, depth + 1, label, outInfo)) {
                return true;
            }
        }

        size_t step = static_cast<size_t>(chunkLen) + (chunkLen & 1u);
        if (step > endPos - payloadPos) break;
        pos = payloadPos + step;
    }
    return false;
}

inline bool DecodeEpsiHexPreview(std::string_view sv, const std::wstring& label, ExtendedImageInfo& outInfo) {
    size_t bp = sv.find("%%BeginPreview:");
    if (bp == std::string_view::npos) return false;
    size_t lineEnd = sv.find_first_of("\r\n", bp);
    if (lineEnd == std::string_view::npos) return false;

    float hdrVals[4] = {};
    if (ParseFloatsFromSv(sv.substr(bp + 15, lineEnd - (bp + 15)), hdrVals, 4) < 3) return false;
    int pw = static_cast<int>(hdrVals[0]);
    int ph = static_cast<int>(hdrVals[1]);
    int pdepth = static_cast<int>(hdrVals[2]);
    if (pw <= 0 || ph <= 0 || pw > 4096 || ph > 4096) return false;
    if (pdepth != 1 && pdepth != 2 && pdepth != 4 && pdepth != 8) return false;

    size_t ep = sv.find("%%EndPreview", lineEnd);
    if (ep == std::string_view::npos) ep = std::min(sv.size(), lineEnd + static_cast<size_t>(pw) * ph * 4u + 4096u);

    const size_t rowBytes = (static_cast<size_t>(pw) * static_cast<size_t>(pdepth) + 7u) / 8u;
    const size_t totalBytes = rowBytes * static_cast<size_t>(ph);
    std::vector<uint8_t> raw;
    raw.reserve(totalBytes);

    int hiNib = -1;
    for (size_t i = lineEnd; i < ep && raw.size() < totalBytes; ++i) {
        char c = sv[i];
        int nib = -1;
        if (c >= '0' && c <= '9') nib = c - '0';
        else if (c >= 'a' && c <= 'f') nib = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') nib = c - 'A' + 10;
        if (nib >= 0) {
            if (hiNib < 0) hiNib = nib;
            else {
                raw.push_back(static_cast<uint8_t>((hiNib << 4) | nib));
                hiNib = -1;
            }
        }
    }
    if (raw.size() < totalBytes) return false;

    outInfo.width = static_cast<uint32_t>(pw);
    outInfo.height = static_cast<uint32_t>(ph);
    outInfo.origWidth = static_cast<uint32_t>(pw);
    outInfo.origHeight = static_cast<uint32_t>(ph);
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = label;
    outInfo.bgraPixels.resize(static_cast<size_t>(pw) * ph * 4u);

    const uint32_t maxVal = (1u << pdepth) - 1u;
    for (int y = 0; y < ph; ++y) {
        const uint8_t* rp = &raw[static_cast<size_t>(y) * rowBytes];
        for (int x = 0; x < pw; ++x) {
            uint32_t bitPos = static_cast<uint32_t>(x * pdepth);
            uint32_t byteIdx = bitPos >> 3;
            uint32_t shift = 8u - static_cast<uint32_t>(pdepth) - (bitPos & 7u);
            uint32_t v = (rp[byteIdx] >> shift) & maxVal;
            // Per Adobe EPSF 3.0 spec: 1-bit EPSI uses 0=white, 1=black; 2/4/8-bit uses 0=black, max=white
            uint8_t g = (pdepth == 1)
                ? static_cast<uint8_t>(v ? 0 : 255)
                : static_cast<uint8_t>((v * 255u) / maxVal);
            uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(y) * pw + x) * 4u];
            dst[0] = g; dst[1] = g; dst[2] = g; dst[3] = 255;
        }
    }
    outInfo.valid = true;
    return true;
}

inline bool ExtractPdfBox(std::string_view sv, const char* boxName, float outBox[4]) {
    size_t p = sv.find(boxName);
    if (p == std::string_view::npos) return false;
    size_t lb = sv.find('[', p);
    size_t rb = (lb != std::string_view::npos) ? sv.find(']', lb) : std::string_view::npos;
    if (lb == std::string_view::npos || rb == std::string_view::npos || rb - lb > 128) return false;
    float vals[4] = {};
    if (ParseFloatsFromSv(sv.substr(lb + 1, rb - lb - 1), vals, 4) == 4 && vals[2] > vals[0] && vals[3] > vals[1]) {
        outBox[0] = vals[0]; outBox[1] = vals[1]; outBox[2] = vals[2]; outBox[3] = vals[3];
        return true;
    }
    return false;
}

} // namespace DetailImg200

// ============================================================================
// #10 & #11. Encapsulated PostScript (.eps) & Adobe Illustrator (.ai)
// ============================================================================
inline bool DecodeEpsOrAi(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (size < 8) return false;
    std::wstring label = (extLower == L".ai") ? L"Adobe Illustrator Artwork (.ai)" : L"Encapsulated PostScript (.eps)";

    const uint8_t* workData = data;
    size_t workSize = size;

    // 1. DOS Binary EPS Header (C5 D0 D3 C6): [psStart:4][psLen:4][wmfStart:4][wmfLen:4][tiffStart:4][tiffLen:4]
    if (size >= 30 && data[0] == 0xC5 && data[1] == 0xD0 && data[2] == 0xD3 && data[3] == 0xC6) {
        uint32_t psStart   = DetailImg100::RdU32LE(data + 4);
        uint32_t psLen     = DetailImg100::RdU32LE(data + 8);
        uint32_t wmfStart  = DetailImg100::RdU32LE(data + 12);
        uint32_t wmfLen    = DetailImg100::RdU32LE(data + 16);
        uint32_t tiffStart = DetailImg100::RdU32LE(data + 20);
        uint32_t tiffLen   = DetailImg100::RdU32LE(data + 24);

        if (tiffLen > 16 && static_cast<uint64_t>(tiffStart) + tiffLen <= size) {
            if (DetailImg100::DecodeEmbeddedWicBuffer(data + tiffStart, tiffLen, label, outInfo)) return true;
            if (DetailImg200::ParseTiffRawIfds(data + tiffStart, tiffLen, label, outInfo)) return true;
        }
        if (wmfLen > 16 && static_cast<uint64_t>(wmfStart) + wmfLen <= size) {
            if (DecodeEmfOrWmf(data + wmfStart, wmfLen, L".wmf", outInfo)) {
                outInfo.formatName = label;
                return true;
            }
        }
        if (psLen > 0 && static_cast<uint64_t>(psStart) + psLen <= size) {
            workData = data + psStart;
            workSize = psLen;
        } else {
            return false;
        }
    }

    const bool isPdf = (workSize >= 5 && std::memcmp(workData, "%PDF-", 5) == 0);
    std::string inflatedPdfOps;
    ExtendedImageInfo pdfImageFallback;
    bool hasExplicitBox = false;
    float bx0 = 0.0f, by0 = 0.0f, bx1 = 400.0f, by1 = 300.0f;

    // 2. PDF-based .ai / EPS container: inflate /FlateDecode content streams & collect /Subtype /Image XObjects
    if (isPdf) {
        std::string_view fullPdfSv(reinterpret_cast<const char*>(workData), std::min<size_t>(workSize, 16u * 1024u * 1024u));
        float pdfBox[4] = {};
        if (DetailImg200::ExtractPdfBox(fullPdfSv, "/ArtBox", pdfBox) ||
            DetailImg200::ExtractPdfBox(fullPdfSv, "/CropBox", pdfBox) ||
            DetailImg200::ExtractPdfBox(fullPdfSv, "/MediaBox", pdfBox))
        {
            bx0 = pdfBox[0]; by0 = pdfBox[1]; bx1 = pdfBox[2]; by1 = pdfBox[3];
            hasExplicitBox = true;
        }

        size_t pos = 0;
        while (pos < fullPdfSv.size()) {
            size_t stPos = fullPdfSv.find("stream", pos);
            if (stPos == std::string_view::npos) break;
            char prevCh = (stPos > 0) ? fullPdfSv[stPos - 1] : ' ';
            if ((prevCh >= 'A' && prevCh <= 'Z') || (prevCh >= 'a' && prevCh <= 'z') || prevCh == '/') {
                pos = stPos + 6;
                continue;
            }
            size_t dataStart = stPos + 6;
            if (dataStart < fullPdfSv.size() && fullPdfSv[dataStart] == '\r') ++dataStart;
            if (dataStart < fullPdfSv.size() && fullPdfSv[dataStart] == '\n') ++dataStart;
            else if (dataStart == stPos + 6) { pos = stPos + 6; continue; }

            size_t dictStart = fullPdfSv.rfind("<<", stPos);
            std::string_view dictSv = (dictStart != std::string_view::npos && stPos - dictStart < 2048)
                ? fullPdfSv.substr(dictStart, stPos - dictStart)
                : std::string_view{};

            size_t endSt = fullPdfSv.find("endstream", dataStart);
            if (endSt == std::string_view::npos) break;
            size_t dataEnd = endSt;
            if (dataEnd > dataStart && fullPdfSv[dataEnd - 1] == '\n') --dataEnd;
            if (dataEnd > dataStart && fullPdfSv[dataEnd - 1] == '\r') --dataEnd;
            size_t rawLen = (dataEnd > dataStart) ? (dataEnd - dataStart) : 0;

            bool isFlate = (dictSv.find("/FlateDecode") != std::string_view::npos);
            bool isImageXObj = (dictSv.find("/Image") != std::string_view::npos && dictSv.find("/Subtype") != std::string_view::npos);
            bool isBinaryOther = (dictSv.find("/DCTDecode") != std::string_view::npos ||
                                  dictSv.find("/JPXDecode") != std::string_view::npos ||
                                  dictSv.find("/CCITTFaxDecode") != std::string_view::npos ||
                                  dictSv.find("/JBIG2Decode") != std::string_view::npos);

            if (rawLen > 0) {
                const uint8_t* sp = workData + dataStart;
                if (isImageXObj) {
                    if (!pdfImageFallback.valid && DetailImg100::DecodeEmbeddedWicBuffer(sp, rawLen, label, pdfImageFallback)) {
                        // Stored in pdfImageFallback if vector content streams produce no geometry
                    } else if (!pdfImageFallback.valid) {
                        std::vector<uint8_t> imgBytes;
                        const uint8_t* rawPix = sp;
                        size_t rawPixLen = rawLen;
                        if (isFlate && DetailImg200::InflateZlibOrRaw(sp, rawLen, imgBytes, 32u * 1024u * 1024u)) {
                            rawPix = imgBytes.data();
                            rawPixLen = imgBytes.size();
                        }
                        auto parseDictInt = [&](std::string_view key) -> int {
                            size_t kp = dictSv.find(key);
                            if (kp == std::string_view::npos) return 0;
                            kp += key.size();
                            return DetailImg200::ParseAsciiIntOrZero(dictSv.substr(kp));
                        };
                        int iw = parseDictInt("/Width");
                        int ih = parseDictInt("/Height");
                        int ibpc = parseDictInt("/BitsPerComponent");
                        if (ibpc == 0) ibpc = 8;
                        int ich = (dictSv.find("/DeviceGray") != std::string_view::npos) ? 1 :
                                  (dictSv.find("/DeviceCMYK") != std::string_view::npos) ? 4 : 3;
                        if (iw > 0 && ih > 0 && iw <= 4096 && ih <= 4096 && ibpc == 8 &&
                            rawPixLen >= static_cast<size_t>(iw) * ih * ich)
                        {
                            pdfImageFallback.width = static_cast<uint32_t>(iw);
                            pdfImageFallback.height = static_cast<uint32_t>(ih);
                            pdfImageFallback.origWidth = static_cast<uint32_t>(iw);
                            pdfImageFallback.origHeight = static_cast<uint32_t>(ih);
                            pdfImageFallback.channels = 4;
                            pdfImageFallback.bitsPerChannel = 8;
                            pdfImageFallback.formatName = label;
                            pdfImageFallback.bgraPixels.resize(static_cast<size_t>(iw) * ih * 4u);
                            for (size_t px = 0; px < static_cast<size_t>(iw) * ih; ++px) {
                                uint8_t r = 0, g = 0, b = 0;
                                if (ich == 1) { r = g = b = rawPix[px]; }
                                else if (ich == 3) { r = rawPix[px * 3 + 0]; g = rawPix[px * 3 + 1]; b = rawPix[px * 3 + 2]; }
                                else {
                                    uint32_t c = 255u - rawPix[px * 4 + 0], m = 255u - rawPix[px * 4 + 1];
                                    uint32_t yv = 255u - rawPix[px * 4 + 2], k = 255u - rawPix[px * 4 + 3];
                                    r = static_cast<uint8_t>((c * k) / 255u);
                                    g = static_cast<uint8_t>((m * k) / 255u);
                                    b = static_cast<uint8_t>((yv * k) / 255u);
                                }
                                pdfImageFallback.bgraPixels[px * 4 + 0] = b;
                                pdfImageFallback.bgraPixels[px * 4 + 1] = g;
                                pdfImageFallback.bgraPixels[px * 4 + 2] = r;
                                pdfImageFallback.bgraPixels[px * 4 + 3] = 255;
                            }
                            pdfImageFallback.valid = true;
                        }
                    }
                } else if (isFlate && inflatedPdfOps.size() < 8u * 1024u * 1024u) {
                    std::vector<uint8_t> dec;
                    if (DetailImg200::InflateZlibOrRaw(sp, rawLen, dec, 8u * 1024u * 1024u)) {
                        inflatedPdfOps.append(reinterpret_cast<const char*>(dec.data()), dec.size());
                        inflatedPdfOps.push_back('\n');
                    }
                } else if (!isBinaryOther && !isFlate && inflatedPdfOps.size() < 8u * 1024u * 1024u) {
                    inflatedPdfOps.append(reinterpret_cast<const char*>(sp), rawLen);
                    inflatedPdfOps.push_back('\n');
                }
            }
            pos = endSt + 9;
        }
    }

    std::string_view rawSv(reinterpret_cast<const char*>(workData), std::min<size_t>(workSize, 8u * 1024u * 1024u));
    if (!isPdf) {
        if (rawSv.find("%!PS") == std::string_view::npos &&
            rawSv.find("%%BoundingBox") == std::string_view::npos &&
            rawSv.find("%AI") == std::string_view::npos)
        {
            return false;
        }

        auto parseBBoxAt = [&](size_t pos, std::string_view tag) -> bool {
            if (pos == std::string_view::npos) return false;
            size_t start = pos + tag.size();
            size_t eol = rawSv.find_first_of("\r\n", start);
            float vals[4] = {};
            if (DetailImg200::ParseFloatsFromSv(rawSv.substr(start, (eol == std::string_view::npos ? rawSv.size() : eol) - start), vals, 4) == 4 &&
                vals[2] > vals[0] && vals[3] > vals[1])
            {
                bx0 = vals[0]; by0 = vals[1]; bx1 = vals[2]; by1 = vals[3];
                hasExplicitBox = true;
                return true;
            }
            return false;
        };
        if (!parseBBoxAt(rawSv.find("%%HiResBoundingBox:"), "%%HiResBoundingBox:")) {
            if (!parseBBoxAt(rawSv.find("%%BoundingBox:"), "%%BoundingBox:")) {
                parseBBoxAt(rawSv.rfind("%%BoundingBox:"), "%%BoundingBox:");
            }
        }
    }

    std::string_view interpSv = (isPdf && !inflatedPdfOps.empty())
        ? std::string_view(inflatedPdfOps)
        : rawSv;

    // 3. PostScript & PDF Vector Interpreter (CTM, q/Q/gsave/grestore, RGB/CMYK/Gray, Bezier c/v/y, re, scanline fill + stroke)
    struct Mat2D {
        float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, tx = 0.0f, ty = 0.0f;
        DetailImg200::Pt2f apply(float x, float y) const {
            return { a * x + c * y + tx, b * x + d * y + ty };
        }
    };
    auto mulMat = [](const Mat2D& m1, const Mat2D& m2) -> Mat2D {
        // Pre-multiply: P' = m2(m1(P))
        return {
            m1.a * m2.a + m1.b * m2.c,
            m1.a * m2.b + m1.b * m2.d,
            m1.c * m2.a + m1.d * m2.c,
            m1.c * m2.b + m1.d * m2.d,
            m1.tx * m2.a + m1.ty * m2.c + m2.tx,
            m1.tx * m2.b + m1.ty * m2.d + m2.ty
        };
    };

    struct GState {
        Mat2D ctm;
        uint8_t fillR = 30, fillG = 41, fillB = 59;
        uint8_t strokeR = 30, strokeG = 41, strokeB = 59;
        float lineWidth = 1.0f;
    };

    struct SubPath {
        std::vector<DetailImg200::Pt2f> pts;
        bool closed = false;
    };

    struct PaintedItem {
        bool isFill = false;
        bool evenOdd = false;
        uint8_t r = 0, g = 0, b = 0;
        float lineWidth = 1.0f;
        std::vector<SubPath> paths;
    };

    GState gs;
    std::vector<GState> gsStack;
    std::vector<SubPath> curPath;
    std::vector<PaintedItem> displayList;
    std::vector<float> stack;
    float curX = 0.0f, curY = 0.0f, startX = 0.0f, startY = 0.0f;
    bool hasCurPt = false;

    struct PsMacro {
        std::string name;
        std::vector<std::string> body;
    };
    std::vector<PsMacro> macros;
    std::string pendingDefName;
    std::vector<std::string> pendingProc;
    bool hasPendingProc = false;

    auto updatePathBounds = [&](float& minX, float& minY, float& maxX, float& maxY) {
        for (const auto& item : displayList) {
            for (const auto& sp : item.paths) {
                for (const auto& p : sp.pts) {
                    minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                    minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
                }
            }
        }
    };

    auto pop = [&]() -> float {
        if (stack.empty()) return 0.0f;
        float v = stack.back();
        stack.pop_back();
        return v;
    };

    auto ensureSubPath = [&]() {
        if (curPath.empty()) {
            SubPath sp;
            sp.pts.push_back(gs.ctm.apply(curX, curY));
            curPath.push_back(std::move(sp));
        }
    };

    auto addCubic = [&](float x1, float y1, float x2, float y2, float x3, float y3) {
        ensureSubPath();
        const int steps = 16;
        for (int s = 1; s <= steps; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(steps);
            float it = 1.0f - t;
            float bx = it * it * it * curX + 3.0f * it * it * t * x1 + 3.0f * it * t * t * x2 + t * t * t * x3;
            float by = it * it * it * curY + 3.0f * it * it * t * y1 + 3.0f * it * t * t * y2 + t * t * t * y3;
            curPath.back().pts.push_back(gs.ctm.apply(bx, by));
        }
        curX = x3; curY = y3; hasCurPt = true;
    };

    auto cmykToRgb = [](float c, float m, float y, float k, uint8_t& outR, uint8_t& outG, uint8_t& outB) {
        c = std::clamp(c, 0.0f, 1.0f);
        m = std::clamp(m, 0.0f, 1.0f);
        y = std::clamp(y, 0.0f, 1.0f);
        k = std::clamp(k, 0.0f, 1.0f);
        outR = static_cast<uint8_t>(std::round((1.0f - c) * (1.0f - k) * 255.0f));
        outG = static_cast<uint8_t>(std::round((1.0f - m) * (1.0f - k) * 255.0f));
        outB = static_cast<uint8_t>(std::round((1.0f - y) * (1.0f - k) * 255.0f));
    };

    auto emitPaint = [&](bool doClose, bool doFill, bool evenOdd, bool doStroke) {
        if (curPath.empty()) return;
        if (doClose && !curPath.empty()) curPath.back().closed = true;
        bool nonTrivial = false;
        for (const auto& sp : curPath) {
            if (sp.pts.size() >= 2) { nonTrivial = true; break; }
        }
        if (nonTrivial) {
            if (doFill) {
                PaintedItem item;
                item.isFill = true;
                item.evenOdd = evenOdd;
                item.r = gs.fillR; item.g = gs.fillG; item.b = gs.fillB;
                item.paths = curPath;
                displayList.push_back(std::move(item));
            }
            if (doStroke) {
                float scaleApprox = std::sqrt(std::max(0.01f, std::abs(gs.ctm.a * gs.ctm.d - gs.ctm.b * gs.ctm.c)));
                PaintedItem item;
                item.isFill = false;
                item.r = gs.strokeR; item.g = gs.strokeG; item.b = gs.strokeB;
                item.lineWidth = gs.lineWidth * scaleApprox;
                item.paths = curPath;
                displayList.push_back(std::move(item));
            }
        }
        curPath.clear();
        hasCurPt = false;
    };

    auto execToken = [&](auto& self, std::string_view tok, int macroDepth) -> void {
        if (tok.empty()) return;

        if (tok[0] == '/') {
            pendingDefName.assign(tok.substr(1));
            return;
        }

        float numVal = 0.0f;
        if (DetailImg200::ParseAsciiFloatToken(tok, numVal)) {
            if (stack.size() > 96) stack.erase(stack.begin(), stack.begin() + 32);
            stack.push_back(numVal);
            return;
        }

        if (tok == "bind") return;
        if (tok == "def") {
            if (!pendingDefName.empty() && hasPendingProc && macros.size() < 64) {
                macros.push_back({ pendingDefName, pendingProc });
            }
            pendingDefName.clear();
            pendingProc.clear();
            hasPendingProc = false;
            stack.clear();
            return;
        }

        if (tok == "q" || tok == "gsave") {
            if (gsStack.size() < 64) gsStack.push_back(gs);
        } else if (tok == "Q" || tok == "grestore") {
            if (!gsStack.empty()) { gs = gsStack.back(); gsStack.pop_back(); }
        } else if ((tok == "cm" || tok == "concat") && stack.size() >= 6) {
            float f = pop(), e = pop(), d = pop(), c = pop(), b = pop(), a = pop();
            gs.ctm = mulMat({ a, b, c, d, e, f }, gs.ctm);
        } else if (tok == "translate" && stack.size() >= 2) {
            float ty = pop(), tx = pop();
            gs.ctm = mulMat({ 1.0f, 0.0f, 0.0f, 1.0f, tx, ty }, gs.ctm);
        } else if (tok == "scale" && stack.size() >= 2) {
            float sy = pop(), sx = pop();
            gs.ctm = mulMat({ sx, 0.0f, 0.0f, sy, 0.0f, 0.0f }, gs.ctm);
        } else if (tok == "rotate" && !stack.empty()) {
            float rad = pop() * 3.1415926535f / 180.0f;
            float cs = std::cos(rad), sn = std::sin(rad);
            gs.ctm = mulMat({ cs, sn, -sn, cs, 0.0f, 0.0f }, gs.ctm);
        } else if ((tok == "m" || tok == "moveto") && stack.size() >= 2) {
            curY = pop(); curX = pop();
            startX = curX; startY = curY; hasCurPt = true;
            SubPath sp;
            sp.pts.push_back(gs.ctm.apply(curX, curY));
            curPath.push_back(std::move(sp));
        } else if ((tok == "rm" || tok == "rmoveto") && stack.size() >= 2) {
            float dy = pop(), dx = pop();
            curX = (hasCurPt ? curX : 0.0f) + dx;
            curY = (hasCurPt ? curY : 0.0f) + dy;
            startX = curX; startY = curY; hasCurPt = true;
            SubPath sp;
            sp.pts.push_back(gs.ctm.apply(curX, curY));
            curPath.push_back(std::move(sp));
        } else if ((tok == "l" || tok == "L" || tok == "lineto") && stack.size() >= 2) {
            float ny = pop(), nx = pop();
            ensureSubPath();
            curPath.back().pts.push_back(gs.ctm.apply(nx, ny));
            curX = nx; curY = ny; hasCurPt = true;
        } else if ((tok == "rl" || tok == "rlineto") && stack.size() >= 2) {
            float dy = pop(), dx = pop();
            float nx = curX + dx, ny = curY + dy;
            ensureSubPath();
            curPath.back().pts.push_back(gs.ctm.apply(nx, ny));
            curX = nx; curY = ny; hasCurPt = true;
        } else if ((tok == "c" || tok == "C" || tok == "curveto") && stack.size() >= 6) {
            float y3 = pop(), x3 = pop(), y2 = pop(), x2 = pop(), y1 = pop(), x1 = pop();
            addCubic(x1, y1, x2, y2, x3, y3);
        } else if (tok == "rcurveto" && stack.size() >= 6) {
            float dy3 = pop(), dx3 = pop(), dy2 = pop(), dx2 = pop(), dy1 = pop(), dx1 = pop();
            addCubic(curX + dx1, curY + dy1, curX + dx2, curY + dy2, curX + dx3, curY + dy3);
        } else if ((tok == "v" || tok == "V") && stack.size() >= 4) {
            float y3 = pop(), x3 = pop(), y2 = pop(), x2 = pop();
            addCubic(curX, curY, x2, y2, x3, y3);
        } else if ((tok == "y" || tok == "Y") && stack.size() >= 4) {
            float y3 = pop(), x3 = pop(), y1 = pop(), x1 = pop();
            addCubic(x1, y1, x3, y3, x3, y3);
        } else if (tok == "re" && stack.size() >= 4) {
            float rh = pop(), rw = pop(), ry = pop(), rx = pop();
            SubPath sp;
            sp.pts.push_back(gs.ctm.apply(rx, ry));
            sp.pts.push_back(gs.ctm.apply(rx + rw, ry));
            sp.pts.push_back(gs.ctm.apply(rx + rw, ry + rh));
            sp.pts.push_back(gs.ctm.apply(rx, ry + rh));
            sp.closed = true;
            curPath.push_back(std::move(sp));
            curX = rx; curY = ry; startX = rx; startY = ry; hasCurPt = true;
        } else if ((tok == "arc" || tok == "arcn") && stack.size() >= 5) {
            float a2 = pop(), a1 = pop(), rad = pop(), cy = pop(), cx = pop();
            bool ccw = (tok == "arc");
            if (ccw) { while (a2 < a1) a2 += 360.0f; }
            else     { while (a2 > a1) a2 -= 360.0f; }
            if (curPath.empty()) curPath.emplace_back();
            for (int s = 0; s <= 24; ++s) {
                float ang = (a1 + (a2 - a1) * (static_cast<float>(s) / 24.0f)) * 3.1415926535f / 180.0f;
                float ax = cx + rad * std::cos(ang);
                float ay = cy + rad * std::sin(ang);
                curPath.back().pts.push_back(gs.ctm.apply(ax, ay));
                curX = ax; curY = ay;
            }
            hasCurPt = true;
        } else if (tok == "h" || tok == "closepath") {
            if (!curPath.empty()) curPath.back().closed = true;
            curX = startX; curY = startY;
        } else if (tok == "rectfill" && stack.size() >= 4) {
            float rh = pop(), rw = pop(), ry = pop(), rx = pop();
            SubPath sp;
            sp.pts.push_back(gs.ctm.apply(rx, ry));
            sp.pts.push_back(gs.ctm.apply(rx + rw, ry));
            sp.pts.push_back(gs.ctm.apply(rx + rw, ry + rh));
            sp.pts.push_back(gs.ctm.apply(rx, ry + rh));
            sp.closed = true;
            PaintedItem item;
            item.isFill = true;
            item.r = gs.fillR; item.g = gs.fillG; item.b = gs.fillB;
            item.paths.push_back(std::move(sp));
            displayList.push_back(std::move(item));
        } else if (tok == "rectstroke" && stack.size() >= 4) {
            float rh = pop(), rw = pop(), ry = pop(), rx = pop();
            SubPath sp;
            sp.pts.push_back(gs.ctm.apply(rx, ry));
            sp.pts.push_back(gs.ctm.apply(rx + rw, ry));
            sp.pts.push_back(gs.ctm.apply(rx + rw, ry + rh));
            sp.pts.push_back(gs.ctm.apply(rx, ry + rh));
            sp.closed = true;
            PaintedItem item;
            item.isFill = false;
            item.r = gs.strokeR; item.g = gs.strokeG; item.b = gs.strokeB;
            item.lineWidth = gs.lineWidth;
            item.paths.push_back(std::move(sp));
            displayList.push_back(std::move(item));
        } else if (tok == "setrgbcolor" && stack.size() >= 3) {
            float b = pop(), g = pop(), r = pop();
            gs.fillR = gs.strokeR = static_cast<uint8_t>(std::clamp(std::round(r * 255.0f), 0.0f, 255.0f));
            gs.fillG = gs.strokeG = static_cast<uint8_t>(std::clamp(std::round(g * 255.0f), 0.0f, 255.0f));
            gs.fillB = gs.strokeB = static_cast<uint8_t>(std::clamp(std::round(b * 255.0f), 0.0f, 255.0f));
        } else if ((tok == "rg" || tok == "Xa") && stack.size() >= 3) {
            float b = pop(), g = pop(), r = pop();
            gs.fillR = static_cast<uint8_t>(std::clamp(std::round(r * 255.0f), 0.0f, 255.0f));
            gs.fillG = static_cast<uint8_t>(std::clamp(std::round(g * 255.0f), 0.0f, 255.0f));
            gs.fillB = static_cast<uint8_t>(std::clamp(std::round(b * 255.0f), 0.0f, 255.0f));
        } else if ((tok == "RG" || tok == "XA") && stack.size() >= 3) {
            float b = pop(), g = pop(), r = pop();
            gs.strokeR = static_cast<uint8_t>(std::clamp(std::round(r * 255.0f), 0.0f, 255.0f));
            gs.strokeG = static_cast<uint8_t>(std::clamp(std::round(g * 255.0f), 0.0f, 255.0f));
            gs.strokeB = static_cast<uint8_t>(std::clamp(std::round(b * 255.0f), 0.0f, 255.0f));
        } else if (tok == "setcmykcolor" && stack.size() >= 4) {
            float k = pop(), y = pop(), m = pop(), c = pop();
            cmykToRgb(c, m, y, k, gs.fillR, gs.fillG, gs.fillB);
            gs.strokeR = gs.fillR; gs.strokeG = gs.fillG; gs.strokeB = gs.fillB;
        } else if (tok == "k" && stack.size() >= 4) {
            float k = pop(), y = pop(), m = pop(), c = pop();
            cmykToRgb(c, m, y, k, gs.fillR, gs.fillG, gs.fillB);
        } else if (tok == "K" && stack.size() >= 4) {
            float k = pop(), y = pop(), m = pop(), c = pop();
            cmykToRgb(c, m, y, k, gs.strokeR, gs.strokeG, gs.strokeB);
        } else if (tok == "setgray" && !stack.empty()) {
            uint8_t v = static_cast<uint8_t>(std::clamp(std::round(pop() * 255.0f), 0.0f, 255.0f));
            gs.fillR = gs.fillG = gs.fillB = gs.strokeR = gs.strokeG = gs.strokeB = v;
        } else if (tok == "g" && !stack.empty()) {
            uint8_t v = static_cast<uint8_t>(std::clamp(std::round(pop() * 255.0f), 0.0f, 255.0f));
            gs.fillR = gs.fillG = gs.fillB = v;
        } else if (tok == "G" && !stack.empty()) {
            uint8_t v = static_cast<uint8_t>(std::clamp(std::round(pop() * 255.0f), 0.0f, 255.0f));
            gs.strokeR = gs.strokeG = gs.strokeB = v;
        } else if ((tok == "sc" || tok == "scn") && !stack.empty()) {
            if (stack.size() >= 4) { float k = pop(), y = pop(), m = pop(), c = pop(); cmykToRgb(c, m, y, k, gs.fillR, gs.fillG, gs.fillB); }
            else if (stack.size() >= 3) { float b = pop(), g = pop(), r = pop(); gs.fillR = static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f)); gs.fillG = static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f)); gs.fillB = static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f)); }
            else { uint8_t v = static_cast<uint8_t>(std::clamp(pop() * 255.0f, 0.0f, 255.0f)); gs.fillR = gs.fillG = gs.fillB = v; }
        } else if ((tok == "SC" || tok == "SCN") && !stack.empty()) {
            if (stack.size() >= 4) { float k = pop(), y = pop(), m = pop(), c = pop(); cmykToRgb(c, m, y, k, gs.strokeR, gs.strokeG, gs.strokeB); }
            else if (stack.size() >= 3) { float b = pop(), g = pop(), r = pop(); gs.strokeR = static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f)); gs.strokeG = static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f)); gs.strokeB = static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f)); }
            else { uint8_t v = static_cast<uint8_t>(std::clamp(pop() * 255.0f, 0.0f, 255.0f)); gs.strokeR = gs.strokeG = gs.strokeB = v; }
        } else if ((tok == "w" || tok == "setlinewidth") && !stack.empty()) {
            gs.lineWidth = std::max(0.1f, pop());
        } else if (tok == "f" || tok == "F" || tok == "fill") {
            emitPaint(true, true, false, false);
            stack.clear();
        } else if (tok == "f*" || tok == "eofill") {
            emitPaint(true, true, true, false);
            stack.clear();
        } else if (tok == "S" || tok == "stroke") {
            emitPaint(false, false, false, true);
            stack.clear();
        } else if (tok == "s") {
            emitPaint(true, false, false, true);
            stack.clear();
        } else if (tok == "B") {
            emitPaint(false, true, false, true);
            stack.clear();
        } else if (tok == "B*") {
            emitPaint(false, true, true, true);
            stack.clear();
        } else if (tok == "b") {
            emitPaint(true, true, false, true);
            stack.clear();
        } else if (tok == "b*") {
            emitPaint(true, true, true, true);
            stack.clear();
        } else if (tok == "n" || tok == "N" || tok == "newpath") {
            curPath.clear();
            hasCurPt = false;
            stack.clear();
        } else if (macroDepth < 4) {
            for (const auto& mac : macros) {
                if (mac.name == tok) {
                    for (const auto& subTok : mac.body) {
                        self(self, subTok, macroDepth + 1);
                    }
                    break;
                }
            }
        }
    };

    size_t i = 0;
    while (i < interpSv.size() && displayList.size() < 200000) {
        char ch = interpSv[i];
        if (ch == '%') {
            while (i < interpSv.size() && interpSv[i] != '\n') ++i;
            continue;
        }
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f') { ++i; continue; }
        if (ch == '(') {
            int depth = 1; ++i;
            while (i < interpSv.size() && depth > 0) {
                if (interpSv[i] == '\\') i += 2;
                else { if (interpSv[i] == '(') ++depth; else if (interpSv[i] == ')') --depth; ++i; }
            }
            continue;
        }
        if (ch == '<') {
            if (i + 1 < interpSv.size() && interpSv[i + 1] == '<') { i += 2; continue; }
            while (i < interpSv.size() && interpSv[i] != '>') ++i;
            if (i < interpSv.size()) ++i;
            continue;
        }
        if (ch == '>') { ++i; continue; }
        if (ch == '[' || ch == ']') { ++i; continue; }
        if (ch == '{') {
            int depth = 1; ++i;
            size_t bodyStart = i;
            while (i < interpSv.size() && depth > 0) {
                if (interpSv[i] == '{') ++depth;
                else if (interpSv[i] == '}') --depth;
                ++i;
            }
            size_t bodyEnd = (i > bodyStart) ? (i - 1) : bodyStart;
            pendingProc.clear();
            hasPendingProc = true;
            if (bodyEnd > bodyStart && bodyEnd - bodyStart < 512) {
                std::string_view bsv = interpSv.substr(bodyStart, bodyEnd - bodyStart);
                size_t bp = 0;
                while (bp < bsv.size() && pendingProc.size() < 32) {
                    while (bp < bsv.size() && (bsv[bp] == ' ' || bsv[bp] == '\t' || bsv[bp] == '\r' || bsv[bp] == '\n')) ++bp;
                    size_t ts = bp;
                    while (bp < bsv.size() && bsv[bp] != ' ' && bsv[bp] != '\t' && bsv[bp] != '\r' && bsv[bp] != '\n') ++bp;
                    if (bp > ts) pendingProc.emplace_back(bsv.substr(ts, bp - ts));
                }
            }
            continue;
        }

        size_t tokStart = i;
        if (ch == '/') ++i;
        while (i < interpSv.size() && interpSv[i] != ' ' && interpSv[i] != '\t' &&
               interpSv[i] != '\r' && interpSv[i] != '\n' && interpSv[i] != '\f' &&
               interpSv[i] != '%' && interpSv[i] != '(' && interpSv[i] != ')' &&
               interpSv[i] != '<' && interpSv[i] != '>' && interpSv[i] != '[' &&
               interpSv[i] != ']' && interpSv[i] != '{' && interpSv[i] != '}' &&
               interpSv[i] != '/')
        {
            ++i;
        }
        std::string_view tok = interpSv.substr(tokStart, i - tokStart);
        execToken(execToken, tok, 0);
    }

    if (displayList.empty()) {
        if (!isPdf && DetailImg200::DecodeEpsiHexPreview(rawSv, label, outInfo)) return true;
        if (DetailImg200::FindAndDecodeBestEmbeddedRaster(workData, workSize, label, outInfo)) return true;
        if (pdfImageFallback.valid) { outInfo = std::move(pdfImageFallback); return true; }
        return false;
    }

    if (!hasExplicitBox) {
        float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
        updatePathBounds(minX, minY, maxX, maxY);
        if (minX <= maxX && minY <= maxY) {
            float padX = std::max(4.0f, (maxX - minX) * 0.08f);
            float padY = std::max(4.0f, (maxY - minY) * 0.08f);
            bx0 = minX - padX; by0 = minY - padY;
            bx1 = maxX + padX; by1 = maxY + padY;
        }
    }

    float docW = std::max(1.0f, bx1 - bx0);
    float docH = std::max(1.0f, by1 - by0);
    uint32_t imgW = 512;
    uint32_t imgH = std::clamp(static_cast<uint32_t>(std::round(512.0f * (docH / docW))), 64u, 768u);
    if (docH > docW) {
        imgH = 512;
        imgW = std::clamp(static_cast<uint32_t>(std::round(512.0f * (docW / docH))), 64u, 768u);
    }

    std::vector<uint8_t> canvas(static_cast<size_t>(imgW) * imgH * 4u, 255); // Crisp white artboard
    auto mapXf = [&](float x) -> float { return ((x - bx0) / docW) * static_cast<float>(imgW - 1u); };
    auto mapYf = [&](float y) -> float { return (1.0f - ((y - by0) / docH)) * static_cast<float>(imgH - 1u); };

    bool drewAny = false;
    for (const auto& item : displayList) {
        if (item.isFill) {
            std::vector<std::vector<DetailImg200::Pt2f>> rings;
            rings.reserve(item.paths.size());
            for (const auto& sp : item.paths) {
                if (sp.pts.size() < 3) continue;
                std::vector<DetailImg200::Pt2f> r;
                r.reserve(sp.pts.size());
                for (const auto& p : sp.pts) r.push_back({ mapXf(p.x), mapYf(p.y) });
                rings.push_back(std::move(r));
            }
            if (DetailImg200::FillPolygonRingsBGRA(canvas, imgW, imgH, rings, item.evenOdd, item.r, item.g, item.b)) {
                drewAny = true;
            }
        } else {
            int thick = std::clamp(static_cast<int>(std::round((item.lineWidth / docW) * static_cast<float>(imgW))), 1, 12);
            for (const auto& sp : item.paths) {
                if (sp.pts.size() < 2) continue;
                for (size_t k = 0; k + 1 < sp.pts.size(); ++k) {
                    DetailImg200::DrawLineBGRA(
                        canvas, imgW, imgH,
                        static_cast<int>(std::round(mapXf(sp.pts[k].x))),
                        static_cast<int>(std::round(mapYf(sp.pts[k].y))),
                        static_cast<int>(std::round(mapXf(sp.pts[k + 1].x))),
                        static_cast<int>(std::round(mapYf(sp.pts[k + 1].y))),
                        item.r, item.g, item.b, thick);
                    drewAny = true;
                }
                if (sp.closed && sp.pts.size() >= 2) {
                    DetailImg200::DrawLineBGRA(
                        canvas, imgW, imgH,
                        static_cast<int>(std::round(mapXf(sp.pts.back().x))),
                        static_cast<int>(std::round(mapYf(sp.pts.back().y))),
                        static_cast<int>(std::round(mapXf(sp.pts.front().x))),
                        static_cast<int>(std::round(mapYf(sp.pts.front().y))),
                        item.r, item.g, item.b, thick);
                    drewAny = true;
                }
            }
        }
    }

    if (!drewAny) {
        if (!isPdf && DetailImg200::DecodeEpsiHexPreview(rawSv, label, outInfo)) return true;
        if (DetailImg200::FindAndDecodeBestEmbeddedRaster(workData, workSize, label, outInfo)) return true;
        if (pdfImageFallback.valid) { outInfo = std::move(pdfImageFallback); return true; }
        return false;
    }

    outInfo.width = imgW;
    outInfo.height = imgH;
    outInfo.origWidth = static_cast<uint32_t>(std::max(1.0f, std::round(docW)));
    outInfo.origHeight = static_cast<uint32_t>(std::max(1.0f, std::round(docH)));
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = label;
    outInfo.bgraPixels = std::move(canvas);
    outInfo.valid = true;
    return true;
}

// ============================================================================
// #12 & #13. CorelDRAW (.cdr) & Affinity Suite (.afphoto / .afdesign / .afpub)
// ============================================================================
inline bool DecodeCorelOrAffinity(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (size < 8) return false;

    if (extLower == L".cdr") {
        std::wstring label = L"CorelDRAW Artwork (.cdr)";

        // 1. Modern CorelDRAW X4+ ZIP container (metadata/thumbnails/thumbnail.bmp, previews/thumbnail.png, previews/page1.png, ...)
        if (data[0] == 'P' && data[1] == 'K' && (data[2] == 3 || data[2] == 5)) {
            return DetailImg200::TryDecodeBestZipThumbnail(data, size, label, outInfo);
        }

        // 2. Classic CorelDRAW v3–X3 RIFF container ('RIFF'....'CDR*' -> walk RIFF & LIST chunks for 'DISP' DIB)
        if (size >= 16 && std::memcmp(data, "RIFF", 4) == 0 &&
            (std::memcmp(data + 8, "CDR", 3) == 0 || std::memcmp(data + 8, "cdr", 3) == 0))
        {
            uint32_t riffLen = DetailImg100::RdU32LE(data + 4);
            size_t riffEnd = std::min<size_t>(size, 8u + static_cast<size_t>(riffLen));
            if (riffEnd < 16) riffEnd = size;
            if (DetailImg200::WalkRiffCdrForDisp(data, size, 12, riffEnd, 0, label, outInfo)) {
                return true;
            }
            if (DetailImg200::FindAndDecodeBestEmbeddedRaster(data, size, label, outInfo, 12)) {
                return true;
            }
        }
        return false;
    }

    // #13. Affinity Photo / Designer / Publisher (.afphoto, .afdesign, .afpub)
    const bool hasAffinityMagic = (data[0] == 0x00 && data[1] == 0xFF && data[2] == 0x4B && data[3] == 0x41);
    const bool hasZipMagic      = (data[0] == 'P' && data[1] == 'K' && (data[2] == 3 || data[2] == 5));
    if (extLower != L".afphoto" && extLower != L".afdesign" && extLower != L".afpub" && !hasAffinityMagic) {
        return false;
    }
    if (!hasAffinityMagic && !hasZipMagic) {
        return false;
    }

    std::wstring label = (extLower == L".afdesign") ? L"Affinity Designer (.afdesign)" :
                         (extLower == L".afpub")    ? L"Affinity Publisher (.afpub)" :
                                                      L"Affinity Photo (.afphoto)";

    if (hasZipMagic) {
        if (DetailImg200::TryDecodeBestZipThumbnail(data, size, label, outInfo)) return true;
        return false;
    }

    // 1. Scan raw embedded PNG (89 50 4E 47 ... IEND) or JPEG (FF D8 FF ... SOF ... FF D9)
    if (DetailImg200::FindAndDecodeBestEmbeddedRaster(data, size, label, outInfo, 4)) {
        return true;
    }

    // 2. Scan Zlib-compressed 'Thmb' / 'bmhT' blocks inside Affinity container
    for (size_t i = 4; i + 12 < size; ++i) {
        bool isThmbTag = (std::memcmp(data + i, "Thmb", 4) == 0 || std::memcmp(data + i, "bmhT", 4) == 0);
        if (!isThmbTag) continue;

        size_t probeLimit = std::min<size_t>(size, i + 64u);
        for (size_t z = i + 4; z + 6 < probeLimit; ++z) {
            uint8_t cmf = data[z], flg = data[z + 1];
            bool looksLikeZlib = ((cmf & 0x0Fu) == 8u && (cmf >> 4) <= 7u && ((static_cast<uint32_t>(cmf) * 256u + flg) % 31u) == 0u);
            if (!looksLikeZlib) continue;

            size_t maxComp = std::min<size_t>(size - z, 4u * 1024u * 1024u);
            std::vector<uint8_t> inflated;
            if (DetailImg200::CallInflateZlib(data + z, maxComp, inflated, 16u * 1024u * 1024u) && inflated.size() >= 32) {
                if (DetailImg100::DecodeEmbeddedWicBuffer(inflated.data(), inflated.size(), label, outInfo) && outInfo.valid) {
                    return true;
                }
                if (DetailImg200::FindAndDecodeBestEmbeddedRaster(inflated.data(), inflated.size(), label, outInfo, 0)) {
                    return true;
                }
            }
        }
    }

    return false;
}

// ============================================================================
// #14. Windows Enhanced Metafile (.emf) & Windows / Aldus Placeable Metafile (.wmf)
// ============================================================================
inline bool DecodeEmfOrWmf(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (size < 22) return false;
    HENHMETAFILE hEmf = nullptr;
    std::wstring label = L"Windows Enhanced Metafile (.emf)";

    // Check EMF signature: RecordType == 1 (EMR_HEADER) and dSignature == 0x464D4520 (" EMF") at offset 40
    if (size >= 88 && DetailImg100::RdU32LE(data) == 1 && DetailImg100::RdU32LE(data + 40) == 0x464D4520u) {
        hEmf = SetEnhMetaFileBits(static_cast<UINT>(size), data);
    } else if (extLower == L".wmf" || DetailImg100::RdU32LE(data) == 0x9AC6CDD7u ||
               (data[0] == 0x01 && data[1] == 0x00 && data[2] == 0x09 && data[3] == 0x00))
    {
        label = L"Windows Metafile (.wmf)";
        size_t wmfOff = 0;
        if (DetailImg100::RdU32LE(data) == 0x9AC6CDD7u) {
            if (size <= 22) return false;
            wmfOff = 22; // Skip 22-byte Aldus Placeable header
        }
        hEmf = SetWinMetaFileBits(static_cast<UINT>(size - wmfOff), data + wmfOff, nullptr, nullptr);
    } else if (extLower == L".emf") {
        hEmf = SetEnhMetaFileBits(static_cast<UINT>(size), data);
    }

    if (!hEmf) return false;

    ENHMETAHEADER emh = {};
    emh.nSize = sizeof(emh);
    UINT gotHdr = GetEnhMetaFileHeader(hEmf, sizeof(emh), &emh);
    int srcW = 512, srcH = 384;
    if (gotHdr > 0) {
        int bw = emh.rclBounds.right - emh.rclBounds.left;
        int bh = emh.rclBounds.bottom - emh.rclBounds.top;
        if (bw > 8 && bh > 8) { srcW = bw; srcH = bh; }
        else {
            int fw = (emh.rclFrame.right - emh.rclFrame.left) / 26;
            int fh = (emh.rclFrame.bottom - emh.rclFrame.top) / 26;
            if (fw > 8 && fh > 8) { srcW = fw; srcH = fh; }
        }
    }

    float aspect = static_cast<float>(srcW) / std::max(1, srcH);
    uint32_t outW = 512, outH = 512;
    if (aspect >= 1.0f) outH = std::clamp(static_cast<uint32_t>(512.0f / aspect), 64u, 512u);
    else                outW = std::clamp(static_cast<uint32_t>(512.0f * aspect), 64u, 512u);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(outW);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(outH); // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdcMem = CreateCompatibleDC(nullptr);
    void* dibBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
    bool rendered = false;
    if (hdcMem && hBmp && dibBits) {
        HGDIOBJ oldObj = SelectObject(hdcMem, hBmp);
        RECT rc = { 0, 0, static_cast<LONG>(outW), static_cast<LONG>(outH) };
        HBRUSH hWhite = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        FillRect(hdcMem, &rc, hWhite);
        PlayEnhMetaFile(hdcMem, hEmf, &rc);
        GdiFlush();

        outInfo.width = outW;
        outInfo.height = outH;
        outInfo.origWidth = static_cast<uint32_t>(std::max(1, srcW));
        outInfo.origHeight = static_cast<uint32_t>(std::max(1, srcH));
        outInfo.channels = 4;
        outInfo.bitsPerChannel = 8;
        outInfo.formatName = label;
        outInfo.bgraPixels.resize(static_cast<size_t>(outW) * outH * 4);
        std::memcpy(outInfo.bgraPixels.data(), dibBits, outInfo.bgraPixels.size());
        // GDI writes 0 in alpha byte; force opaque alpha = 255
        for (size_t p = 3; p < outInfo.bgraPixels.size(); p += 4) {
            outInfo.bgraPixels[p] = 255;
        }
        outInfo.valid = true;
        rendered = true;
        SelectObject(hdcMem, oldObj);
    }
    if (hBmp) DeleteObject(hBmp);
    if (hdcMem) DeleteDC(hdcMem);
    DeleteEnhMetaFile(hEmf);
    return rendered;
}

// ============================================================================
// #15. Computer Graphics Metafile (.cgm — ISO/IEC 8632 Clear-Text & Binary)
// ============================================================================
inline bool DecodeCgmVector(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 6) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 8u * 1024u * 1024u));
    const bool isBinaryCgm = ((DetailImg100::RdU16BE(data) & 0xFFE0u) == 0x0020u); // Class 0, ID 1 = BEGIN METAFILE

    // Case-insensitive Clear-Text CGM header probe ("BEGMF" / "BEG_MF")
    bool isTextCgm = false;
    if (!isBinaryCgm) {
        size_t probeLen = std::min<size_t>(sv.size(), 2048u);
        std::string headNorm;
        headNorm.reserve(probeLen);
        for (size_t i = 0; i < probeLen; ++i) {
            char c = sv[i];
            if (c == '_' || c == '$') continue;
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
            headNorm.push_back(c);
        }
        isTextCgm = (headNorm.find("BEGMF") != std::string::npos);
    }
    if (!isBinaryCgm && !isTextCgm) return false;

    uint8_t colTable[256][3] = {};
    // ISO 8632 default indexed palette
    static const uint8_t kDefaultCgmPal[8][3] = {
        {248, 250, 252}, {15,  23,  42},  {220, 38,  38},  {22,  163, 74},
        {37,  99,  235}, {234, 179, 8},   {192, 38,  211}, {8,   145, 178}
    };
    for (int i = 0; i < 8; ++i) {
        colTable[i][0] = kDefaultCgmPal[i][0];
        colTable[i][1] = kDefaultCgmPal[i][1];
        colTable[i][2] = kDefaultCgmPal[i][2];
    }
    for (int i = 8; i < 256; ++i) {
        colTable[i][0] = static_cast<uint8_t>((i * 53) & 0xFF);
        colTable[i][1] = static_cast<uint8_t>((i * 97) & 0xFF);
        colTable[i][2] = static_cast<uint8_t>((i * 193) & 0xFF);
    }

    uint8_t bgR = 248, bgG = 250, bgB = 252;
    uint8_t lineR = 15, lineG = 23, lineB = 42;
    uint8_t fillR = 59, fillG = 130, fillB = 246;
    uint8_t edgeR = 15, edgeG = 23, edgeB = 42;
    int interiorStyle = 1; // 0=hollow, 1=solid, 2=pattern, 3=hatch, 4=empty
    bool edgeVisible = false;
    float lineWidthVdc = 0.0f;

    bool hasExplicitExtent = false;
    float vx0 = 0.0f, vy0 = 0.0f, vx1 = 1000.0f, vy1 = 1000.0f;

    struct CgmCmd {
        int kind = 0; // 0=polyline, 1=disjoint, 2=polygonRings
        bool doFill = false;
        bool doOutline = false;
        uint8_t fR = 0, fG = 0, fB = 0;
        uint8_t sR = 0, sG = 0, sB = 0;
        float widthVdc = 0.0f;
        std::vector<std::vector<DetailImg200::Pt2f>> rings;
        std::vector<std::pair<DetailImg200::Pt2f, DetailImg200::Pt2f>> explicitEdges;
    };
    std::vector<CgmCmd> cmds;

    auto makeArc3Pt = [](float x1, float y1, float x2, float y2, float x3, float y3, int closeType, std::vector<DetailImg200::Pt2f>& outPts) {
        outPts.clear();
        float d = 2.0f * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2));
        if (std::abs(d) < 1e-4f) {
            outPts.push_back({ x1, y1 });
            outPts.push_back({ x2, y2 });
            outPts.push_back({ x3, y3 });
            return;
        }
        float s1 = x1 * x1 + y1 * y1;
        float s2 = x2 * x2 + y2 * y2;
        float s3 = x3 * x3 + y3 * y3;
        float ux = (s1 * (y2 - y3) + s2 * (y3 - y1) + s3 * (y1 - y2)) / d;
        float uy = (s1 * (x3 - x2) + s2 * (x1 - x3) + s3 * (x2 - x1)) / d;
        float rad = std::sqrt((x1 - ux) * (x1 - ux) + (y1 - uy) * (y1 - uy));

        const float twoPi = 6.283185307f;
        auto wrap2Pi = [&](float a) -> float {
            while (a < 0.0f) a += twoPi;
            while (a >= twoPi) a -= twoPi;
            return a;
        };
        float a1 = std::atan2(y1 - uy, x1 - ux);
        float a2 = std::atan2(y2 - uy, x2 - ux);
        float a3 = std::atan2(y3 - uy, x3 - ux);
        float d12 = wrap2Pi(a2 - a1);
        float d13 = wrap2Pi(a3 - a1);
        float sweep = (d12 <= d13) ? d13 : -(twoPi - d13);

        const int steps = 36;
        for (int s = 0; s <= steps; ++s) {
            float ang = a1 + sweep * (static_cast<float>(s) / static_cast<float>(steps));
            outPts.push_back({ ux + rad * std::cos(ang), uy + rad * std::sin(ang) });
        }
        if (closeType == 0) outPts.push_back({ ux, uy }); // Pie closure
    };

    auto makeArcCtr = [](float cx, float cy, float dx1, float dy1, float dx2, float dy2, float rad, int closeType, std::vector<DetailImg200::Pt2f>& outPts) {
        outPts.clear();
        if (rad <= 0.0f) return;
        const float twoPi = 6.283185307f;
        float a1 = std::atan2(dy1, dx1);
        float a2 = std::atan2(dy2, dx2);
        float sweep = a2 - a1;
        while (sweep <= 0.0f) sweep += twoPi;
        const int steps = 36;
        for (int s = 0; s <= steps; ++s) {
            float ang = a1 + sweep * (static_cast<float>(s) / static_cast<float>(steps));
            outPts.push_back({ cx + rad * std::cos(ang), cy + rad * std::sin(ang) });
        }
        if (closeType == 0) outPts.push_back({ cx, cy }); // Pie closure
    };

    auto pushClosedShape = [&](std::vector<DetailImg200::Pt2f>&& ring) {
        if (ring.size() < 3) return;
        CgmCmd cmd;
        cmd.kind = 2;
        cmd.doFill = (interiorStyle != 0 && interiorStyle != 4);
        cmd.doOutline = (interiorStyle == 0 || edgeVisible);
        cmd.fR = fillR; cmd.fG = fillG; cmd.fB = fillB;
        cmd.sR = (interiorStyle == 0) ? fillR : edgeR;
        cmd.sG = (interiorStyle == 0) ? fillG : edgeG;
        cmd.sB = (interiorStyle == 0) ? fillB : edgeB;
        cmd.widthVdc = lineWidthVdc;
        cmd.rings.push_back(std::move(ring));
        cmds.push_back(std::move(cmd));
    };

    if (isBinaryCgm) {
        int vdcType = 0;       // 0=integer, 1=real
        int intPrec = 16;      // 8, 16, 24, 32
        int vdcIntPrec = 16;   // 8, 16, 24, 32
        bool explicitVdcIntPrec = false;
        int vdcRealForm = 1;   // 0=IEEE float, 1=fixed-point (ISO default 16.16)
        int vdcRealExp  = 16;
        int vdcRealMant = 16;
        int colPrec = 8;       // 8 or 16
        int colIdxPrec = 8;    // 8, 16, 24, 32
        int colorMode = 0;     // 0=indexed, 1=direct RGB

        auto readSignedInt = [](const uint8_t* p, int bits) -> int32_t {
            if (bits == 8) return static_cast<int8_t>(p[0]);
            if (bits == 16) return static_cast<int16_t>(DetailImg100::RdU16BE(p));
            if (bits == 24) {
                int32_t v = (static_cast<int32_t>(p[0]) << 16) | (static_cast<int32_t>(p[1]) << 8) | p[2];
                if (v & 0x800000) v -= 0x1000000;
                return v;
            }
            return static_cast<int32_t>(DetailImg100::RdU32BE(p));
        };

        auto vdcScalarSize = [&]() -> size_t {
            if (vdcType == 0) return static_cast<size_t>(std::clamp(vdcIntPrec, 8, 32) / 8);
            if (vdcRealForm == 0 && vdcRealExp == 12 && vdcRealMant == 52) return 8u;
            if (vdcRealForm == 1 && vdcRealExp == 32 && vdcRealMant == 32) return 8u;
            return 4u;
        };

        auto readVdcScalar = [&](const uint8_t* p) -> float {
            if (vdcType == 0) {
                return static_cast<float>(readSignedInt(p, vdcIntPrec));
            }
            if (vdcRealForm == 0) {
                if (vdcRealExp == 12 && vdcRealMant == 52) {
                    uint64_t hi = DetailImg100::RdU32BE(p);
                    uint64_t lo = DetailImg100::RdU32BE(p + 4);
                    uint64_t u = (hi << 32) | lo;
                    double d = 0.0;
                    std::memcpy(&d, &u, 8);
                    return static_cast<float>(d);
                }
                uint32_t u = DetailImg100::RdU32BE(p);
                float f = 0.0f;
                std::memcpy(&f, &u, 4);
                return f;
            }
            if (vdcRealExp == 32 && vdcRealMant == 32) {
                int32_t whole = static_cast<int32_t>(DetailImg100::RdU32BE(p));
                uint32_t frac = DetailImg100::RdU32BE(p + 4);
                return static_cast<float>(whole) + static_cast<float>(frac) / 4294967296.0f;
            }
            // Fixed-point 16.16
            int32_t raw = static_cast<int32_t>(DetailImg100::RdU32BE(p));
            return static_cast<float>(raw) / 65536.0f;
        };

        auto readColor = [&](const uint8_t* p, size_t len, bool forceDirect, uint8_t& outR, uint8_t& outG, uint8_t& outB) {
            if (colorMode == 1 || forceDirect) {
                if (colPrec == 16 && len >= 6) {
                    outR = p[0]; outG = p[2]; outB = p[4];
                } else if (len >= 3) {
                    outR = p[0]; outG = p[1]; outB = p[2];
                }
            } else {
                uint32_t idx = 1;
                if (colIdxPrec == 8 && len >= 1) idx = p[0];
                else if (colIdxPrec == 16 && len >= 2) idx = DetailImg100::RdU16BE(p);
                else if (colIdxPrec == 24 && len >= 3) idx = (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
                else if (colIdxPrec == 32 && len >= 4) idx = DetailImg100::RdU32BE(p);
                else if (len >= 1) idx = p[0];
                idx &= 0xFFu;
                outR = colTable[idx][0];
                outG = colTable[idx][1];
                outB = colTable[idx][2];
            }
        };

        size_t p = 0;
        std::vector<uint8_t> partBuf;
        while (p + 2 <= size && cmds.size() < 200000) {
            uint16_t hdr = DetailImg100::RdU16BE(data + p);
            p += 2;
            int cls = (hdr >> 12) & 0x0F;
            int eid = (hdr >> 5) & 0x7F;
            uint32_t shortLen = hdr & 0x1Fu;

            const uint8_t* payload = nullptr;
            size_t plen = 0;
            if (shortLen < 31u) {
                if (p + shortLen > size) break;
                payload = data + p;
                plen = shortLen;
                p += static_cast<size_t>(shortLen) + (shortLen & 1u);
            } else {
                // Partitioned long-form command header (bit 15 = more partitions)
                partBuf.clear();
                bool ok = true;
                for (int parts = 0; parts < 2048; ++parts) {
                    if (p + 2 > size) { ok = false; break; }
                    uint16_t pw = DetailImg100::RdU16BE(data + p);
                    p += 2;
                    bool more = ((pw & 0x8000u) != 0);
                    uint32_t chunk = pw & 0x7FFFu;
                    if (p + chunk > size || partBuf.size() + chunk > 16u * 1024u * 1024u) { ok = false; break; }
                    partBuf.insert(partBuf.end(), data + p, data + p + chunk);
                    p += static_cast<size_t>(chunk) + (chunk & 1u);
                    if (!more) break;
                }
                if (!ok) break;
                payload = partBuf.data();
                plen = partBuf.size();
            }

            if (cls == 0 && eid == 2) break; // END METAFILE

            if (cls == 1) {
                int stepI = std::clamp(intPrec, 8, 32) / 8;
                if (eid == 3 && plen >= 2) { // VDC TYPE (0=integer, 1=real)
                    uint16_t vt = DetailImg100::RdU16BE(payload);
                    if (vt <= 1) vdcType = vt;
                } else if (eid == 4 && plen >= static_cast<size_t>(stepI)) { // INTEGER PRECISION
                    int v = readSignedInt(payload, intPrec);
                    if (v == 8 || v == 16 || v == 24 || v == 32) {
                        intPrec = v;
                        if (!explicitVdcIntPrec) vdcIntPrec = v;
                    }
                } else if (eid == 5 && plen >= static_cast<size_t>(stepI) * 3u) { // REAL PRECISION
                    int rf = readSignedInt(payload, intPrec);
                    int re = readSignedInt(payload + stepI, intPrec);
                    int rm = readSignedInt(payload + stepI * 2, intPrec);
                    if (rf == 0 || rf == 1) { vdcRealForm = rf; vdcRealExp = re; vdcRealMant = rm; }
                } else if (eid == 7 && plen >= static_cast<size_t>(stepI)) { // COLOUR PRECISION
                    int v = readSignedInt(payload, intPrec);
                    if (v == 8 || v == 16) colPrec = v;
                } else if (eid == 8 && plen >= static_cast<size_t>(stepI)) { // COLOUR INDEX PRECISION
                    int v = readSignedInt(payload, intPrec);
                    if (v == 8 || v == 16 || v == 24 || v == 32) colIdxPrec = v;
                }
            } else if (cls == 2) {
                if (eid == 2 && plen >= 2) { // COLOUR SELECTION MODE (0=indexed, 1=direct)
                    uint16_t cm = DetailImg100::RdU16BE(payload);
                    if (cm <= 1) colorMode = cm;
                } else if (eid == 6) { // VDC EXTENT
                    size_t vs = vdcScalarSize();
                    if (plen >= vs * 4u) {
                        vx0 = readVdcScalar(payload + 0 * vs);
                        vy0 = readVdcScalar(payload + 1 * vs);
                        vx1 = readVdcScalar(payload + 2 * vs);
                        vy1 = readVdcScalar(payload + 3 * vs);
                        hasExplicitExtent = true;
                    }
                } else if (eid == 7) { // BACKGROUND COLOUR (always direct RGB)
                    readColor(payload, plen, true, bgR, bgG, bgB);
                }
            } else if (cls == 3) {
                int stepI = std::clamp(intPrec, 8, 32) / 8;
                if (eid == 1 && plen >= 1) { // VDC INTEGER PRECISION (or 0/1 VDC TYPE alias)
                    int v = (plen == 2) ? static_cast<int16_t>(DetailImg100::RdU16BE(payload)) : readSignedInt(payload, intPrec);
                    if (v == 8 || v == 16 || v == 24 || v == 32) {
                        vdcIntPrec = v;
                        explicitVdcIntPrec = true;
                    } else if (v == 0 || v == 1) {
                        vdcType = v;
                    }
                } else if (eid == 2 && plen >= static_cast<size_t>(stepI) * 3u) { // VDC REAL PRECISION
                    int rf = readSignedInt(payload, intPrec);
                    int re = readSignedInt(payload + stepI, intPrec);
                    int rm = readSignedInt(payload + stepI * 2, intPrec);
                    if (rf == 0 || rf == 1) { vdcRealForm = rf; vdcRealExp = re; vdcRealMant = rm; }
                }
            } else if (cls == 5) {
                if (eid == 3) { // LINE WIDTH
                    size_t vs = vdcScalarSize();
                    if (plen >= vs) lineWidthVdc = std::abs(readVdcScalar(payload));
                } else if (eid == 4) { // LINE COLOUR
                    readColor(payload, plen, false, lineR, lineG, lineB);
                } else if (eid == 22 && plen >= 2) { // INTERIOR STYLE
                    interiorStyle = DetailImg100::RdU16BE(payload);
                } else if (eid == 23) { // FILL COLOUR
                    readColor(payload, plen, false, fillR, fillG, fillB);
                } else if (eid == 27 && plen >= 2) { // EDGE VISIBILITY
                    edgeVisible = (DetailImg100::RdU16BE(payload) != 0);
                } else if (eid == 29) { // EDGE COLOUR
                    readColor(payload, plen, false, edgeR, edgeG, edgeB);
                } else if (eid == 34) { // COLOUR TABLE
                    size_t idxBytes = static_cast<size_t>(std::clamp(colIdxPrec, 8, 32) / 8);
                    size_t compBytes = (colPrec == 16) ? 2u : 1u;
                    if (plen >= idxBytes + compBytes * 3u) {
                        uint32_t startIdx = (idxBytes == 1) ? payload[0] : DetailImg100::RdU16BE(payload);
                        size_t q = idxBytes;
                        while (q + compBytes * 3u <= plen && startIdx < 256u) {
                            colTable[startIdx][0] = payload[q];
                            colTable[startIdx][1] = payload[q + compBytes];
                            colTable[startIdx][2] = payload[q + compBytes * 2u];
                            ++startIdx;
                            q += compBytes * 3u;
                        }
                    }
                }
            } else if (cls == 4) {
                size_t vs = vdcScalarSize();
                size_t ptBytes = vs * 2u;
                if ((eid == 1 || eid == 2) && plen >= ptBytes * 2u) { // POLYLINE / DISJOINT POLYLINE
                    size_t nPts = plen / ptBytes;
                    std::vector<DetailImg200::Pt2f> pts(nPts);
                    for (size_t k = 0; k < nPts; ++k) {
                        pts[k] = { readVdcScalar(payload + k * ptBytes), readVdcScalar(payload + k * ptBytes + vs) };
                    }
                    CgmCmd cmd;
                    cmd.kind = (eid == 2) ? 1 : 0;
                    cmd.sR = lineR; cmd.sG = lineG; cmd.sB = lineB;
                    cmd.widthVdc = lineWidthVdc;
                    cmd.rings.push_back(std::move(pts));
                    cmds.push_back(std::move(cmd));
                } else if (eid == 7 && plen >= ptBytes * 3u) { // POLYGON (scanline filled)
                    size_t nPts = plen / ptBytes;
                    std::vector<DetailImg200::Pt2f> ring(nPts);
                    for (size_t k = 0; k < nPts; ++k) {
                        ring[k] = { readVdcScalar(payload + k * ptBytes), readVdcScalar(payload + k * ptBytes + vs) };
                    }
                    pushClosedShape(std::move(ring));
                } else if (eid == 8 && plen >= (ptBytes + 2u) * 3u) { // POLYGON SET
                    size_t tupleBytes = ptBytes + 2u;
                    size_t nTuples = plen / tupleBytes;
                    CgmCmd cmd;
                    cmd.kind = 2;
                    cmd.doFill = (interiorStyle != 0 && interiorStyle != 4);
                    cmd.fR = fillR; cmd.fG = fillG; cmd.fB = fillB;
                    cmd.sR = edgeR; cmd.sG = edgeG; cmd.sB = edgeB;
                    cmd.widthVdc = lineWidthVdc;

                    std::vector<DetailImg200::Pt2f> curRing;
                    std::vector<uint16_t> curFlags;
                    auto flushRing = [&]() {
                        if (curRing.size() >= 3) {
                            for (size_t k = 0; k < curRing.size(); ++k) {
                                uint16_t fl = curFlags[k];
                                if (fl == 1 || fl == 3 || (edgeVisible && (fl == 1 || fl == 3))) {
                                    cmd.explicitEdges.push_back({ curRing[k], curRing[(k + 1) % curRing.size()] });
                                }
                            }
                            cmd.rings.push_back(std::move(curRing));
                        }
                        curRing.clear();
                        curFlags.clear();
                    };
                    for (size_t k = 0; k < nTuples; ++k) {
                        const uint8_t* tp = payload + k * tupleBytes;
                        DetailImg200::Pt2f pt = { readVdcScalar(tp), readVdcScalar(tp + vs) };
                        uint16_t flag = DetailImg100::RdU16BE(tp + ptBytes);
                        curRing.push_back(pt);
                        curFlags.push_back(flag);
                        if (flag == 2 || flag == 3) flushRing();
                    }
                    flushRing();
                    if (!cmd.rings.empty()) cmds.push_back(std::move(cmd));
                } else if (eid == 11 && plen >= ptBytes * 2u) { // RECTANGLE
                    float x1 = readVdcScalar(payload + 0 * vs);
                    float y1 = readVdcScalar(payload + 1 * vs);
                    float x2 = readVdcScalar(payload + 2 * vs);
                    float y2 = readVdcScalar(payload + 3 * vs);
                    pushClosedShape({ {x1, y1}, {x2, y1}, {x2, y2}, {x1, y2} });
                } else if (eid == 12 && plen >= vs * 3u) { // CIRCLE
                    float cx = readVdcScalar(payload + 0 * vs);
                    float cy = readVdcScalar(payload + 1 * vs);
                    float rad = std::abs(readVdcScalar(payload + 2 * vs));
                    if (rad > 0.0f) {
                        std::vector<DetailImg200::Pt2f> ring;
                        ring.reserve(48);
                        for (int s = 0; s < 48; ++s) {
                            float a = static_cast<float>(s) * 6.283185307f / 48.0f;
                            ring.push_back({ cx + rad * std::cos(a), cy + rad * std::sin(a) });
                        }
                        pushClosedShape(std::move(ring));
                    }
                } else if ((eid == 13 || eid == 14) && plen >= vs * 6u) { // CIRCULAR ARC 3 POINT / CLOSE
                    float x1 = readVdcScalar(payload + 0 * vs), y1 = readVdcScalar(payload + 1 * vs);
                    float x2 = readVdcScalar(payload + 2 * vs), y2 = readVdcScalar(payload + 3 * vs);
                    float x3 = readVdcScalar(payload + 4 * vs), y3 = readVdcScalar(payload + 5 * vs);
                    int closeType = (eid == 14 && plen >= vs * 6u + 2u) ? DetailImg100::RdU16BE(payload + vs * 6u) : -1;
                    std::vector<DetailImg200::Pt2f> pts;
                    makeArc3Pt(x1, y1, x2, y2, x3, y3, closeType, pts);
                    if (eid == 14) {
                        pushClosedShape(std::move(pts));
                    } else if (pts.size() >= 2) {
                        CgmCmd cmd;
                        cmd.kind = 0;
                        cmd.sR = lineR; cmd.sG = lineG; cmd.sB = lineB;
                        cmd.widthVdc = lineWidthVdc;
                        cmd.rings.push_back(std::move(pts));
                        cmds.push_back(std::move(cmd));
                    }
                } else if ((eid == 15 || eid == 16) && plen >= vs * 7u) { // CIRCULAR ARC CENTRE / CLOSE
                    float cx  = readVdcScalar(payload + 0 * vs), cy  = readVdcScalar(payload + 1 * vs);
                    float dx1 = readVdcScalar(payload + 2 * vs), dy1 = readVdcScalar(payload + 3 * vs);
                    float dx2 = readVdcScalar(payload + 4 * vs), dy2 = readVdcScalar(payload + 5 * vs);
                    float rad = std::abs(readVdcScalar(payload + 6 * vs));
                    int closeType = (eid == 16 && plen >= vs * 7u + 2u) ? DetailImg100::RdU16BE(payload + vs * 7u) : -1;
                    std::vector<DetailImg200::Pt2f> pts;
                    makeArcCtr(cx, cy, dx1, dy1, dx2, dy2, rad, closeType, pts);
                    if (eid == 16) {
                        pushClosedShape(std::move(pts));
                    } else if (pts.size() >= 2) {
                        CgmCmd cmd;
                        cmd.kind = 0;
                        cmd.sR = lineR; cmd.sG = lineG; cmd.sB = lineB;
                        cmd.widthVdc = lineWidthVdc;
                        cmd.rings.push_back(std::move(pts));
                        cmds.push_back(std::move(cmd));
                    }
                } else if (eid == 17 && plen >= vs * 6u) { // ELLIPSE (center + 2 conjugate diameter endpoints)
                    float cx = readVdcScalar(payload + 0 * vs), cy = readVdcScalar(payload + 1 * vs);
                    float x1 = readVdcScalar(payload + 2 * vs), y1 = readVdcScalar(payload + 3 * vs);
                    float x2 = readVdcScalar(payload + 4 * vs), y2 = readVdcScalar(payload + 5 * vs);
                    float ux = x1 - cx, uy = y1 - cy;
                    float wx = x2 - cx, wy = y2 - cy;
                    std::vector<DetailImg200::Pt2f> ring;
                    ring.reserve(48);
                    for (int s = 0; s < 48; ++s) {
                        float a = static_cast<float>(s) * 6.283185307f / 48.0f;
                        float cs = std::cos(a), sn = std::sin(a);
                        ring.push_back({ cx + cs * ux + sn * wx, cy + cs * uy + sn * wy });
                    }
                    pushClosedShape(std::move(ring));
                }
            }
        }
    } else {
        // ISO/IEC 8632-4 Clear-Text Encoding parser (';' or '/' statement terminators, '_'/'$' null chars)
        int colorMode = 0; // 0=indexed, 1=direct

        auto parseTextColor = [&](const std::vector<std::string>& toks, size_t startIdx, bool forceDirect, uint8_t& outR, uint8_t& outG, uint8_t& outB) {
            if ((colorMode == 1 || forceDirect || toks.size() >= startIdx + 3) && toks.size() >= startIdx + 3) {
                outR = static_cast<uint8_t>(std::clamp<int32_t>(DetailImg200::ParseAsciiIntOrZero(toks[startIdx + 0]), 0, 255));
                outG = static_cast<uint8_t>(std::clamp<int32_t>(DetailImg200::ParseAsciiIntOrZero(toks[startIdx + 1]), 0, 255));
                outB = static_cast<uint8_t>(std::clamp<int32_t>(DetailImg200::ParseAsciiIntOrZero(toks[startIdx + 2]), 0, 255));
            } else if (toks.size() > startIdx) {
                uint32_t idx = static_cast<uint32_t>(std::clamp<int32_t>(DetailImg200::ParseAsciiIntOrZero(toks[startIdx]), 0, 255));
                outR = colTable[idx][0];
                outG = colTable[idx][1];
                outB = colTable[idx][2];
            }
        };

        size_t pos = 0;
        while (pos < sv.size() && cmds.size() < 200000) {
            std::string stmt;
            char inQuote = 0;
            bool inComment = false;
            while (pos < sv.size()) {
                char c = sv[pos++];
                if (inComment) {
                    if (c == '%') inComment = false;
                    continue;
                }
                if (inQuote) {
                    if (c == inQuote) inQuote = 0;
                    continue;
                }
                if (c == '%') { inComment = true; continue; }
                if (c == '\'' || c == '"') { inQuote = c; stmt.push_back(' '); continue; }
                if (c == ';' || c == '/') break;
                stmt.push_back(c);
            }

            std::vector<std::string> toks;
            size_t tp = 0;
            while (tp < stmt.size()) {
                while (tp < stmt.size() && (stmt[tp] == ' ' || stmt[tp] == '\t' || stmt[tp] == '\r' || stmt[tp] == '\n' ||
                                            stmt[tp] == ',' || stmt[tp] == '(' || stmt[tp] == ')')) {
                    ++tp;
                }
                size_t te = tp;
                while (te < stmt.size() && stmt[te] != ' ' && stmt[te] != '\t' && stmt[te] != '\r' && stmt[te] != '\n' &&
                       stmt[te] != ',' && stmt[te] != '(' && stmt[te] != ')') {
                    ++te;
                }
                if (te > tp) {
                    std::string tok;
                    tok.reserve(te - tp);
                    for (size_t k = tp; k < te; ++k) {
                        char ch = stmt[k];
                        if (ch == '_' || ch == '$') continue;
                        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - ('a' - 'A'));
                        tok.push_back(ch);
                    }
                    if (!tok.empty()) toks.push_back(std::move(tok));
                }
                tp = te;
            }
            if (toks.empty()) continue;

            const std::string& kw = toks[0];
            if (kw == "ENDMF") break;
            if (kw == "VDCEXT" && toks.size() >= 5) {
                vx0 = DetailImg200::ParseAsciiFloatOrZero(toks[1]);
                vy0 = DetailImg200::ParseAsciiFloatOrZero(toks[2]);
                vx1 = DetailImg200::ParseAsciiFloatOrZero(toks[3]);
                vy1 = DetailImg200::ParseAsciiFloatOrZero(toks[4]);
                hasExplicitExtent = true;
            } else if (kw == "COLRMODE" && toks.size() >= 2) {
                colorMode = (toks[1] == "DIRECT" || toks[1] == "1") ? 1 : 0;
            } else if (kw == "COLRTABLE" && toks.size() >= 5) {
                int startIdx = static_cast<int>(std::clamp<int32_t>(DetailImg200::ParseAsciiIntOrZero(toks[1]), 0, 255));
                for (size_t k = 2; k + 2 < toks.size() && startIdx < 256; k += 3, ++startIdx) {
                    colTable[startIdx][0] = static_cast<uint8_t>(std::clamp<int32_t>(DetailImg200::ParseAsciiIntOrZero(toks[k + 0]), 0, 255));
                    colTable[startIdx][1] = static_cast<uint8_t>(std::clamp<int32_t>(DetailImg200::ParseAsciiIntOrZero(toks[k + 1]), 0, 255));
                    colTable[startIdx][2] = static_cast<uint8_t>(std::clamp<int32_t>(DetailImg200::ParseAsciiIntOrZero(toks[k + 2]), 0, 255));
                }
            } else if (kw == "BACKCOLR" && toks.size() >= 4) {
                parseTextColor(toks, 1, true, bgR, bgG, bgB);
            } else if (kw == "LINECOLR" && toks.size() >= 2) {
                parseTextColor(toks, 1, false, lineR, lineG, lineB);
            } else if (kw == "FILLCOLR" && toks.size() >= 2) {
                parseTextColor(toks, 1, false, fillR, fillG, fillB);
            } else if (kw == "EDGECOLR" && toks.size() >= 2) {
                parseTextColor(toks, 1, false, edgeR, edgeG, edgeB);
            } else if (kw == "EDGEVIS" && toks.size() >= 2) {
                edgeVisible = (toks[1] == "ON" || toks[1] == "1");
            } else if (kw == "INTSTYLE" && toks.size() >= 2) {
                if (toks[1] == "HOLLOW" || toks[1] == "0") interiorStyle = 0;
                else if (toks[1] == "EMPTY" || toks[1] == "4") interiorStyle = 4;
                else interiorStyle = 1;
            } else if (kw == "LINEWIDTH" && toks.size() >= 2) {
                lineWidthVdc = std::abs(DetailImg200::ParseAsciiFloatOrZero(toks[1]));
            } else if ((kw == "LINE" || kw == "POLYLINE" || kw == "DISJTLINE") && toks.size() >= 5) {
                std::vector<DetailImg200::Pt2f> pts;
                for (size_t k = 1; k + 1 < toks.size(); k += 2) {
                    pts.push_back({ DetailImg200::ParseAsciiFloatOrZero(toks[k]), DetailImg200::ParseAsciiFloatOrZero(toks[k + 1]) });
                }
                if (pts.size() >= 2) {
                    CgmCmd cmd;
                    cmd.kind = (kw == "DISJTLINE") ? 1 : 0;
                    cmd.sR = lineR; cmd.sG = lineG; cmd.sB = lineB;
                    cmd.widthVdc = lineWidthVdc;
                    cmd.rings.push_back(std::move(pts));
                    cmds.push_back(std::move(cmd));
                }
            } else if (kw == "POLYGON" && toks.size() >= 7) {
                std::vector<DetailImg200::Pt2f> ring;
                for (size_t k = 1; k + 1 < toks.size(); k += 2) {
                    ring.push_back({ DetailImg200::ParseAsciiFloatOrZero(toks[k]), DetailImg200::ParseAsciiFloatOrZero(toks[k + 1]) });
                }
                pushClosedShape(std::move(ring));
            } else if (kw == "RECT" && toks.size() >= 5) {
                float x1 = DetailImg200::ParseAsciiFloatOrZero(toks[1]), y1 = DetailImg200::ParseAsciiFloatOrZero(toks[2]);
                float x2 = DetailImg200::ParseAsciiFloatOrZero(toks[3]), y2 = DetailImg200::ParseAsciiFloatOrZero(toks[4]);
                pushClosedShape({ {x1, y1}, {x2, y1}, {x2, y2}, {x1, y2} });
            } else if (kw == "CIRCLE" && toks.size() >= 4) {
                float cx = DetailImg200::ParseAsciiFloatOrZero(toks[1]), cy = DetailImg200::ParseAsciiFloatOrZero(toks[2]);
                float rad = std::abs(DetailImg200::ParseAsciiFloatOrZero(toks[3]));
                if (rad > 0.0f) {
                    std::vector<DetailImg200::Pt2f> ring;
                    ring.reserve(48);
                    for (int s = 0; s < 48; ++s) {
                        float a = static_cast<float>(s) * 6.283185307f / 48.0f;
                        ring.push_back({ cx + rad * std::cos(a), cy + rad * std::sin(a) });
                    }
                    pushClosedShape(std::move(ring));
                }
            } else if ((kw == "ARC3PT" || kw == "ARC3PTCLOSE") && toks.size() >= 7) {
                float x1 = DetailImg200::ParseAsciiFloatOrZero(toks[1]), y1 = DetailImg200::ParseAsciiFloatOrZero(toks[2]);
                float x2 = DetailImg200::ParseAsciiFloatOrZero(toks[3]), y2 = DetailImg200::ParseAsciiFloatOrZero(toks[4]);
                float x3 = DetailImg200::ParseAsciiFloatOrZero(toks[5]), y3 = DetailImg200::ParseAsciiFloatOrZero(toks[6]);
                int closeType = (kw == "ARC3PTCLOSE") ? ((toks.size() >= 8 && (toks[7] == "CHORD" || toks[7] == "1")) ? 1 : 0) : -1;
                std::vector<DetailImg200::Pt2f> pts;
                makeArc3Pt(x1, y1, x2, y2, x3, y3, closeType, pts);
                if (kw == "ARC3PTCLOSE") pushClosedShape(std::move(pts));
                else if (pts.size() >= 2) {
                    CgmCmd cmd; cmd.kind = 0; cmd.sR = lineR; cmd.sG = lineG; cmd.sB = lineB; cmd.widthVdc = lineWidthVdc;
                    cmd.rings.push_back(std::move(pts)); cmds.push_back(std::move(cmd));
                }
            } else if ((kw == "ARCCTR" || kw == "ARCCTRCLOSE") && toks.size() >= 8) {
                float cx  = DetailImg200::ParseAsciiFloatOrZero(toks[1]), cy  = DetailImg200::ParseAsciiFloatOrZero(toks[2]);
                float dx1 = DetailImg200::ParseAsciiFloatOrZero(toks[3]), dy1 = DetailImg200::ParseAsciiFloatOrZero(toks[4]);
                float dx2 = DetailImg200::ParseAsciiFloatOrZero(toks[5]), dy2 = DetailImg200::ParseAsciiFloatOrZero(toks[6]);
                float rad = std::abs(DetailImg200::ParseAsciiFloatOrZero(toks[7]));
                int closeType = (kw == "ARCCTRCLOSE") ? ((toks.size() >= 9 && (toks[8] == "CHORD" || toks[8] == "1")) ? 1 : 0) : -1;
                std::vector<DetailImg200::Pt2f> pts;
                makeArcCtr(cx, cy, dx1, dy1, dx2, dy2, rad, closeType, pts);
                if (kw == "ARCCTRCLOSE") pushClosedShape(std::move(pts));
                else if (pts.size() >= 2) {
                    CgmCmd cmd; cmd.kind = 0; cmd.sR = lineR; cmd.sG = lineG; cmd.sB = lineB; cmd.widthVdc = lineWidthVdc;
                    cmd.rings.push_back(std::move(pts)); cmds.push_back(std::move(cmd));
                }
            } else if (kw == "ELLIPSE" && toks.size() >= 7) {
                float cx = DetailImg200::ParseAsciiFloatOrZero(toks[1]), cy = DetailImg200::ParseAsciiFloatOrZero(toks[2]);
                float x1 = DetailImg200::ParseAsciiFloatOrZero(toks[3]), y1 = DetailImg200::ParseAsciiFloatOrZero(toks[4]);
                float x2 = DetailImg200::ParseAsciiFloatOrZero(toks[5]), y2 = DetailImg200::ParseAsciiFloatOrZero(toks[6]);
                float ux = x1 - cx, uy = y1 - cy;
                float wx = x2 - cx, wy = y2 - cy;
                std::vector<DetailImg200::Pt2f> ring;
                ring.reserve(48);
                for (int s = 0; s < 48; ++s) {
                    float a = static_cast<float>(s) * 6.283185307f / 48.0f;
                    float cs = std::cos(a), sn = std::sin(a);
                    ring.push_back({ cx + cs * ux + sn * wx, cy + cs * uy + sn * wy });
                }
                pushClosedShape(std::move(ring));
            }
        }
    }

    if (cmds.empty()) return false;

    if (!hasExplicitExtent || std::abs(vx1 - vx0) < 1e-6f || std::abs(vy1 - vy0) < 1e-6f) {
        float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
        for (const auto& cmd : cmds) {
            for (const auto& ring : cmd.rings) {
                for (const auto& pt : ring) {
                    minX = std::min(minX, pt.x); maxX = std::max(maxX, pt.x);
                    minY = std::min(minY, pt.y); maxY = std::max(maxY, pt.y);
                }
            }
        }
        if (minX > maxX || minY > maxY) return false;
        float spanX = std::max(1.0f, maxX - minX);
        float spanY = std::max(1.0f, maxY - minY);
        vx0 = minX - spanX * 0.06f; vx1 = maxX + spanX * 0.06f;
        vy0 = minY - spanY * 0.06f; vy1 = maxY + spanY * 0.06f;
    }

    float spanX = vx1 - vx0;
    float spanY = vy1 - vy0;
    float absW = std::max(1.0f, std::abs(spanX));
    float absH = std::max(1.0f, std::abs(spanY));
    uint32_t W = 512, H = 384;
    if (absW >= absH) {
        W = 512;
        H = std::clamp(static_cast<uint32_t>(std::round(512.0f * (absH / absW))), 128u, 512u);
    } else {
        H = 512;
        W = std::clamp(static_cast<uint32_t>(std::round(512.0f * (absW / absH))), 128u, 512u);
    }

    std::vector<uint8_t> canvas(static_cast<size_t>(W) * H * 4u);
    DetailImg200::FillRectBGRA(canvas, W, H, 0, 0, static_cast<int>(W), static_cast<int>(H), bgR, bgG, bgB);

    const float padPx = 16.0f;
    const float drawW = static_cast<float>(W) - padPx * 2.0f;
    const float drawH = static_cast<float>(H) - padPx * 2.0f;
    auto mapXf = [&](float x) -> float { return padPx + ((x - vx0) / spanX) * drawW; };
    auto mapYf = [&](float y) -> float { return static_cast<float>(H) - padPx - ((y - vy0) / spanY) * drawH; };

    int renderedPrims = 0;
    for (const auto& cmd : cmds) {
        int thick = (cmd.widthVdc > 0.0f)
            ? std::clamp(static_cast<int>(std::round((cmd.widthVdc / absW) * drawW)), 1, 10)
            : 2;

        if (cmd.kind == 0) { // POLYLINE
            for (const auto& ring : cmd.rings) {
                for (size_t k = 0; k + 1 < ring.size(); ++k) {
                    DetailImg200::DrawLineBGRA(
                        canvas, W, H,
                        static_cast<int>(std::round(mapXf(ring[k].x))),
                        static_cast<int>(std::round(mapYf(ring[k].y))),
                        static_cast<int>(std::round(mapXf(ring[k + 1].x))),
                        static_cast<int>(std::round(mapYf(ring[k + 1].y))),
                        cmd.sR, cmd.sG, cmd.sB, thick);
                    ++renderedPrims;
                }
            }
        } else if (cmd.kind == 1) { // DISJOINT POLYLINE
            for (const auto& ring : cmd.rings) {
                for (size_t k = 0; k + 1 < ring.size(); k += 2) {
                    DetailImg200::DrawLineBGRA(
                        canvas, W, H,
                        static_cast<int>(std::round(mapXf(ring[k].x))),
                        static_cast<int>(std::round(mapYf(ring[k].y))),
                        static_cast<int>(std::round(mapXf(ring[k + 1].x))),
                        static_cast<int>(std::round(mapYf(ring[k + 1].y))),
                        cmd.sR, cmd.sG, cmd.sB, thick);
                    ++renderedPrims;
                }
            }
        } else if (cmd.kind == 2) { // POLYGON / POLYGON SET / CLOSED SHAPE
            std::vector<std::vector<DetailImg200::Pt2f>> mappedRings;
            mappedRings.reserve(cmd.rings.size());
            for (const auto& ring : cmd.rings) {
                if (ring.size() < 3) continue;
                std::vector<DetailImg200::Pt2f> mr;
                mr.reserve(ring.size());
                for (const auto& pt : ring) mr.push_back({ mapXf(pt.x), mapYf(pt.y) });
                mappedRings.push_back(std::move(mr));
            }
            if (cmd.doFill && DetailImg200::FillPolygonRingsBGRA(canvas, W, H, mappedRings, true, cmd.fR, cmd.fG, cmd.fB)) {
                ++renderedPrims;
            }
            if (cmd.doOutline) {
                for (const auto& mr : mappedRings) {
                    for (size_t k = 0; k < mr.size(); ++k) {
                        const auto& a = mr[k];
                        const auto& b = mr[(k + 1) % mr.size()];
                        DetailImg200::DrawLineBGRA(
                            canvas, W, H,
                            static_cast<int>(std::round(a.x)), static_cast<int>(std::round(a.y)),
                            static_cast<int>(std::round(b.x)), static_cast<int>(std::round(b.y)),
                            cmd.sR, cmd.sG, cmd.sB, thick);
                        ++renderedPrims;
                    }
                }
            }
            for (const auto& ed : cmd.explicitEdges) {
                DetailImg200::DrawLineBGRA(
                    canvas, W, H,
                    static_cast<int>(std::round(mapXf(ed.first.x))),  static_cast<int>(std::round(mapYf(ed.first.y))),
                    static_cast<int>(std::round(mapXf(ed.second.x))), static_cast<int>(std::round(mapYf(ed.second.y))),
                    cmd.sR, cmd.sG, cmd.sB, thick);
                ++renderedPrims;
            }
        }
    }

    if (renderedPrims == 0) return false;

    outInfo.width = W;
    outInfo.height = H;
    outInfo.origWidth = W;
    outInfo.origHeight = H;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"ISO 8632 Computer Graphics Metafile (.cgm)";
    outInfo.bgraPixels = std::move(canvas);
    outInfo.valid = true;
    return true;
}

// ============================================================================
// #16 & #17. Photoshop Brush Set (.abr) & GIMP / Photoshop Pattern (.pat)
// ============================================================================
inline bool DecodeAbrOrPat(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    // #17. GIMP Pattern (.pat — magic "GPAT" at offset 20) or Photoshop Pattern ("8BPT")
    if (extLower == L".pat" || (size >= 24 && std::memcmp(data + 20, "GPAT", 4) == 0)) {
        if (size >= 24 && std::memcmp(data + 20, "GPAT", 4) == 0) {
            uint32_t hdrSize = DetailImg100::RdU32BE(data + 0);
            uint32_t pw      = DetailImg100::RdU32BE(data + 8);
            uint32_t ph      = DetailImg100::RdU32BE(data + 12);
            uint32_t bpp     = DetailImg100::RdU32BE(data + 16);
            if (pw == 0 || ph == 0 || pw > 4096 || ph > 4096 || bpp < 1 || bpp > 4) return false;
            if (hdrSize < 24 || hdrSize + static_cast<size_t>(pw) * ph * bpp > size) return false;

            // Tile 2x2 so seamless pattern tiling is immediately visible!
            uint32_t tileX = (pw <= 256) ? 2 : 1;
            uint32_t tileY = (ph <= 256) ? 2 : 1;
            uint32_t outW = pw * tileX, outH = ph * tileY;
            outInfo.width = outW; outInfo.height = outH;
            outInfo.origWidth = pw; outInfo.origHeight = ph;
            outInfo.channels = 4; outInfo.bitsPerChannel = 8;
            outInfo.formatName = L"GIMP Seamless Pattern (.pat)";
            outInfo.bgraPixels.resize(static_cast<size_t>(outW) * outH * 4);

            const uint8_t* src = data + hdrSize;
            for (uint32_t y = 0; y < outH; ++y) {
                uint32_t sy = y % ph;
                for (uint32_t x = 0; x < outW; ++x) {
                    uint32_t sx = x % pw;
                    const uint8_t* p = src + (static_cast<size_t>(sy) * pw + sx) * bpp;
                    uint8_t r = 0, g = 0, b = 0, a = 255;
                    if (bpp == 1) { r = g = b = p[0]; }
                    else if (bpp == 2) { r = g = b = p[0]; a = p[1]; }
                    else if (bpp == 3) { r = p[0]; g = p[1]; b = p[2]; }
                    else { r = p[0]; g = p[1]; b = p[2]; a = p[3]; }
                    uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(y) * outW + x) * 4];
                    dst[0] = static_cast<uint8_t>((b * a) / 255);
                    dst[1] = static_cast<uint8_t>((g * a) / 255);
                    dst[2] = static_cast<uint8_t>((r * a) / 255);
                    dst[3] = a;
                }
            }
            outInfo.valid = true;
            return true;
        }
        if (size >= 28 && std::memcmp(data, "8BPT", 4) == 0) {
            size_t pos = 0;
            uint32_t patCount = 0;
            if (DetailImg100::RdU16BE(data + 4) == 1) {
                patCount = DetailImg100::RdU32BE(data + 6);
                pos = 10;
            } else if (DetailImg100::RdU32BE(data + 4) == 1) {
                patCount = DetailImg100::RdU32BE(data + 8);
                pos = 12;
            } else {
                return false;
            }
            if (patCount == 0 || patCount > 4096 || pos + 16 > size) return false;

            uint32_t patVer    = DetailImg100::RdU32BE(data + pos + 0);
            uint32_t imageMode = DetailImg100::RdU32BE(data + pos + 4);
            uint16_t patH      = DetailImg100::RdU16BE(data + pos + 8);
            uint16_t patW      = DetailImg100::RdU16BE(data + pos + 10);
            uint32_t nameChars = DetailImg100::RdU32BE(data + pos + 12);
            pos += 16;
            if (patVer != 1 || patW == 0 || patH == 0 || patW > 4096 || patH > 4096 || nameChars > 2048) {
                return false;
            }
            if (pos + static_cast<size_t>(nameChars) * 2 + 1 > size) return false;

            std::wstring patName;
            for (uint32_t i = 0; i < nameChars; ++i) {
                wchar_t wc = static_cast<wchar_t>(DetailImg100::RdU16BE(data + pos + static_cast<size_t>(i) * 2));
                if (wc != 0) patName.push_back(wc);
            }
            pos += static_cast<size_t>(nameChars) * 2;

            uint8_t idLen = data[pos++];
            if (pos + idLen > size) return false;
            pos += idLen;

            uint8_t palRgb[768] = {};
            uint16_t transIdx = 0xFFFFu;
            if (imageMode == 2) {
                if (pos + 772 > size) return false;
                std::memcpy(palRgb, data + pos, 768);
                transIdx = DetailImg100::RdU16BE(data + pos + 768);
                pos += 772;
            }

            if (pos + 5 <= size && DetailImg100::RdU32BE(data + pos) != 3 &&
                data[pos] == 0 && DetailImg100::RdU32BE(data + pos + 1) == 3)
            {
                ++pos;
            }
            if (pos + 28 > size) return false;

            uint32_t vmaVer     = DetailImg100::RdU32BE(data + pos + 0);
            uint32_t vmaLen     = DetailImg100::RdU32BE(data + pos + 4);
            int32_t  vTop       = static_cast<int32_t>(DetailImg100::RdU32BE(data + pos + 8));
            int32_t  vLeft      = static_cast<int32_t>(DetailImg100::RdU32BE(data + pos + 12));
            int32_t  vBottom    = static_cast<int32_t>(DetailImg100::RdU32BE(data + pos + 16));
            int32_t  vRight     = static_cast<int32_t>(DetailImg100::RdU32BE(data + pos + 20));
            uint32_t maxChans   = DetailImg100::RdU32BE(data + pos + 24);
            (void)vmaLen;
            pos += 28;
            if (vmaVer != 3 || maxChans > 64) return false;

            uint32_t tileW = (vRight > vLeft && (vRight - vLeft) <= 4096) ? static_cast<uint32_t>(vRight - vLeft) : patW;
            uint32_t tileH = (vBottom > vTop && (vBottom - vTop) <= 4096) ? static_cast<uint32_t>(vBottom - vTop) : patH;
            size_t tilePx = static_cast<size_t>(tileW) * tileH;

            struct DecodedChan {
                uint32_t slot = 0;
                std::vector<uint8_t> plane;
            };
            std::vector<DecodedChan> chans;
            uint32_t totalSlots = maxChans + 2u;

            for (uint32_t slot = 0; slot < totalSlots && pos + 4 <= size; ++slot) {
                uint32_t isWritten = DetailImg100::RdU32BE(data + pos);
                pos += 4;
                if (isWritten == 0) continue;
                if (pos + 4 > size) return false;
                uint32_t chanLen = DetailImg100::RdU32BE(data + pos);
                pos += 4;
                if (chanLen == 0) continue;
                if (chanLen < 23 || pos + chanLen > size) return false;
                size_t chanEnd = pos + chanLen;

                uint32_t pixDepth = DetailImg100::RdU32BE(data + pos + 0);
                int32_t  cTop     = static_cast<int32_t>(DetailImg100::RdU32BE(data + pos + 4));
                int32_t  cLeft    = static_cast<int32_t>(DetailImg100::RdU32BE(data + pos + 8));
                int32_t  cBottom  = static_cast<int32_t>(DetailImg100::RdU32BE(data + pos + 12));
                int32_t  cRight   = static_cast<int32_t>(DetailImg100::RdU32BE(data + pos + 16));
                uint16_t depth16  = DetailImg100::RdU16BE(data + pos + 20);
                uint8_t  comp     = data[pos + 22];
                const uint8_t* csrc = data + pos + 23;
                size_t clen = chanLen - 23;

                uint32_t effDepth = (depth16 == 8 || depth16 == 16) ? depth16 : pixDepth;
                uint32_t chW = (cRight > cLeft) ? static_cast<uint32_t>(cRight - cLeft) : tileW;
                uint32_t chH = (cBottom > cTop) ? static_cast<uint32_t>(cBottom - cTop) : tileH;
                if (chW == 0 || chH == 0 || chW > 4096 || chH > 4096 || (effDepth != 8 && effDepth != 16)) {
                    return false;
                }

                size_t sampleBytes = (effDepth == 16) ? 2u : 1u;
                size_t rawRowBytes = static_cast<size_t>(chW) * sampleBytes;
                std::vector<uint8_t> rawPlane(rawRowBytes * chH, 0);

                if (comp == 0) {
                    if (clen < rawPlane.size()) return false;
                    std::memcpy(rawPlane.data(), csrc, rawPlane.size());
                } else if (comp == 1) {
                    size_t tblBytes = static_cast<size_t>(chH) * 2u;
                    if (clen < tblBytes) return false;
                    size_t rpos = tblBytes;
                    for (uint32_t ry = 0; ry < chH; ++ry) {
                        uint16_t rowCnt = DetailImg100::RdU16BE(csrc + static_cast<size_t>(ry) * 2u);
                        if (rpos + rowCnt > clen) return false;
                        const uint8_t* rp = csrc + rpos;
                        const uint8_t* rend = rp + rowCnt;
                        uint8_t* dstRow = rawPlane.data() + static_cast<size_t>(ry) * rawRowBytes;
                        size_t dIdx = 0;
                        while (rp < rend && dIdx < rawRowBytes) {
                            int8_t n = static_cast<int8_t>(*rp++);
                            if (n >= 0) {
                                size_t cnt = static_cast<size_t>(n) + 1u;
                                if (rp + cnt > rend || dIdx + cnt > rawRowBytes) return false;
                                std::memcpy(dstRow + dIdx, rp, cnt);
                                rp += cnt;
                                dIdx += cnt;
                            } else if (n != -128) {
                                size_t cnt = static_cast<size_t>(1 - static_cast<int>(n));
                                if (rp >= rend || dIdx + cnt > rawRowBytes) return false;
                                uint8_t val = *rp++;
                                std::memset(dstRow + dIdx, val, cnt);
                                dIdx += cnt;
                            }
                        }
                        if (dIdx < rawRowBytes) return false;
                        rpos += rowCnt;
                    }
                } else {
                    return false;
                }

                DecodedChan dc;
                dc.slot = slot;
                dc.plane.assign(tilePx, (slot >= 3) ? 255 : 0);
                int offX = cLeft - vLeft;
                int offY = cTop - vTop;
                for (uint32_t y = 0; y < chH; ++y) {
                    int dy = offY + static_cast<int>(y);
                    if (dy < 0 || dy >= static_cast<int>(tileH)) continue;
                    for (uint32_t x = 0; x < chW; ++x) {
                        int dx = offX + static_cast<int>(x);
                        if (dx < 0 || dx >= static_cast<int>(tileW)) continue;
                        uint8_t v8 = rawPlane[(static_cast<size_t>(y) * chW + x) * sampleBytes];
                        dc.plane[static_cast<size_t>(dy) * tileW + dx] = v8;
                    }
                }
                chans.push_back(std::move(dc));
                pos = chanEnd;
            }

            auto findSlot = [&](uint32_t s) -> const std::vector<uint8_t>* {
                for (const auto& c : chans) {
                    if (c.slot == s) return &c.plane;
                }
                return nullptr;
            };

            const std::vector<uint8_t>* ch0 = findSlot(0);
            const std::vector<uint8_t>* ch1 = findSlot(1);
            const std::vector<uint8_t>* ch2 = findSlot(2);
            const std::vector<uint8_t>* ch3 = findSlot(3);
            const std::vector<uint8_t>* alphaPlane = nullptr;
            for (const auto& c : chans) {
                if ((imageMode == 1 && c.slot >= 1) ||
                    (imageMode == 2 && c.slot >= 1) ||
                    (imageMode == 3 && c.slot >= 3) ||
                    (imageMode == 4 && c.slot >= 4))
                {
                    alphaPlane = &c.plane;
                    break;
                }
            }

            if (imageMode == 1 && !ch0) return false;
            if (imageMode == 2 && !ch0) return false;
            if (imageMode == 3 && (!ch0 || !ch1 || !ch2)) return false;
            if (imageMode == 4 && (!ch0 || !ch1 || !ch2 || !ch3)) return false;
            if (imageMode != 1 && imageMode != 2 && imageMode != 3 && imageMode != 4) return false;

            std::vector<uint8_t> singleTile(tilePx * 4u);
            for (size_t i = 0; i < tilePx; ++i) {
                uint8_t r = 0, g = 0, b = 0, a = alphaPlane ? (*alphaPlane)[i] : 255;
                if (imageMode == 1) {
                    r = g = b = (*ch0)[i];
                } else if (imageMode == 2) {
                    uint8_t idx = (*ch0)[i];
                    r = palRgb[static_cast<size_t>(idx) * 3u + 0];
                    g = palRgb[static_cast<size_t>(idx) * 3u + 1];
                    b = palRgb[static_cast<size_t>(idx) * 3u + 2];
                    if (idx == transIdx) a = 0;
                } else if (imageMode == 3) {
                    r = (*ch0)[i];
                    g = (*ch1)[i];
                    b = (*ch2)[i];
                } else {
                    uint32_t c = (*ch0)[i], m = (*ch1)[i], y = (*ch2)[i], k = (*ch3)[i];
                    r = static_cast<uint8_t>((c * k + 127u) / 255u);
                    g = static_cast<uint8_t>((m * k + 127u) / 255u);
                    b = static_cast<uint8_t>((y * k + 127u) / 255u);
                }
                WritePremulBGRA(&singleTile[i * 4u], r, g, b, a);
            }

            uint32_t tilesX = std::max(1u, (128u + tileW - 1u) / tileW);
            uint32_t tilesY = std::max(1u, (128u + tileH - 1u) / tileH);
            uint32_t outW = tileW * tilesX;
            uint32_t outH = tileH * tilesY;

            outInfo.width = outW;
            outInfo.height = outH;
            outInfo.origWidth = tileW;
            outInfo.origHeight = tileH;
            outInfo.channels = 4;
            outInfo.bitsPerChannel = 8;
            outInfo.formatName = L"Adobe Photoshop Pattern (.pat)";
            if (!patName.empty()) outInfo.layerNames.push_back(patName);
            outInfo.bgraPixels.resize(static_cast<size_t>(outW) * outH * 4u);

            for (uint32_t y = 0; y < outH; ++y) {
                uint32_t sy = y % tileH;
                for (uint32_t x = 0; x < outW; ++x) {
                    uint32_t sx = x % tileW;
                    std::memcpy(&outInfo.bgraPixels[(static_cast<size_t>(y) * outW + x) * 4u],
                                &singleTile[(static_cast<size_t>(sy) * tileW + sx) * 4u], 4u);
                }
            }
            outInfo.valid = true;
            return true;
        }
        return false;
    }

    // #16. Adobe Photoshop Brush Set (.abr) — real v1/v2 & v6/v7/v10 sampled brush decoder
    if (extLower != L".abr" || size < 10) return false;
    uint16_t ver = DetailImg100::RdU16BE(data + 0);
    if (ver != 1 && ver != 2 && ver != 6 && ver != 7 && ver != 10) return false;
    uint16_t subVer = 0;
    if (ver == 6 || ver == 7 || ver == 10) {
        if (size < 16) return false;
        subVer = DetailImg100::RdU16BE(data + 2);
        if (subVer != 1 && subVer != 2) return false;
    }

    struct BrushTip {
        uint32_t w = 0, h = 0;
        std::wstring name;
        std::vector<uint8_t> alpha;
    };
    std::vector<BrushTip> tips;

    auto decodeAbrSampleBitmap = [](const uint8_t* pix, size_t availBytes,
                                    uint32_t bw, uint32_t bh, uint16_t depth, uint8_t comp,
                                    bool strictTrailingBound, std::vector<uint8_t>& outAlpha) -> bool
    {
        if (bw == 0 || bh == 0 || bw > 8192 || bh > 8192) return false;
        if (depth != 8 && depth != 16) return false;
        if (comp > 1) return false;
        uint64_t pxCount64 = static_cast<uint64_t>(bw) * static_cast<uint64_t>(bh);
        if (pxCount64 == 0 || pxCount64 > 32ull * 1024ull * 1024ull) return false;

        size_t sampleBytes = (depth == 16) ? 2u : 1u;
        size_t rawRowBytes = static_cast<size_t>(bw) * sampleBytes;
        size_t totalRawBytes = static_cast<size_t>(pxCount64) * sampleBytes;
        std::vector<uint8_t> rawPlane;

        if (comp == 0) {
            if (availBytes < totalRawBytes) return false;
            if (strictTrailingBound && availBytes > totalRawBytes + 64u) return false;
            rawPlane.assign(pix, pix + totalRawBytes);
        } else {
            // comp == 1: height * uint16_t BE scanline byte counts followed by PackBits RLE rows
            size_t tblBytes = static_cast<size_t>(bh) * 2u;
            if (availBytes <= tblBytes) return false;
            rawPlane.assign(totalRawBytes, 0);

            size_t rpos = tblBytes;
            bool rowByRowOk = true;
            for (uint32_t ry = 0; ry < bh; ++ry) {
                uint16_t rowCnt = DetailImg100::RdU16BE(pix + static_cast<size_t>(ry) * 2u);
                if (rowCnt == 0 || rpos + rowCnt > availBytes) {
                    rowByRowOk = false;
                    break;
                }
                const uint8_t* rp = pix + rpos;
                const uint8_t* rend = rp + rowCnt;
                uint8_t* dstRow = rawPlane.data() + static_cast<size_t>(ry) * rawRowBytes;
                size_t dIdx = 0;
                while (rp < rend && dIdx < rawRowBytes) {
                    int8_t n = static_cast<int8_t>(*rp++);
                    if (n >= 0) {
                        size_t cnt = static_cast<size_t>(n) + 1u;
                        if (rp + cnt > rend || dIdx + cnt > rawRowBytes) { rowByRowOk = false; break; }
                        std::memcpy(dstRow + dIdx, rp, cnt);
                        rp += cnt;
                        dIdx += cnt;
                    } else if (n != -128) {
                        size_t cnt = static_cast<size_t>(1 - static_cast<int>(n));
                        if (rp >= rend || dIdx + cnt > rawRowBytes) { rowByRowOk = false; break; }
                        uint8_t val = *rp++;
                        std::memset(dstRow + dIdx, val, cnt);
                        dIdx += cnt;
                    }
                }
                if (!rowByRowOk || dIdx != rawRowBytes) {
                    rowByRowOk = false;
                    break;
                }
                rpos += rowCnt;
            }
            if (rowByRowOk && strictTrailingBound && availBytes > rpos + 64u) {
                rowByRowOk = false;
            }

            if (!rowByRowOk) {
                // Fallback for continuous PackBits stream after the bh*2 scanline count table
                const uint8_t* rle = pix + tblBytes;
                const uint8_t* rend = pix + availBytes;
                size_t dstIdx = 0;
                bool contOk = true;
                while (rle < rend && dstIdx < totalRawBytes) {
                    int8_t n = static_cast<int8_t>(*rle++);
                    if (n >= 0) {
                        size_t cnt = static_cast<size_t>(n) + 1u;
                        if (rle + cnt > rend || dstIdx + cnt > totalRawBytes) { contOk = false; break; }
                        std::memcpy(rawPlane.data() + dstIdx, rle, cnt);
                        rle += cnt;
                        dstIdx += cnt;
                    } else if (n != -128) {
                        size_t cnt = static_cast<size_t>(1 - static_cast<int>(n));
                        if (rle >= rend || dstIdx + cnt > totalRawBytes) { contOk = false; break; }
                        uint8_t val = *rle++;
                        std::memset(rawPlane.data() + dstIdx, val, cnt);
                        dstIdx += cnt;
                    }
                }
                size_t consumed = static_cast<size_t>(rle - pix);
                if (!contOk || dstIdx != totalRawBytes) return false;
                if (strictTrailingBound && availBytes > consumed + 64u) return false;
            }
        }

        size_t pxCount = static_cast<size_t>(pxCount64);
        outAlpha.resize(pxCount);
        if (depth == 8) {
            std::memcpy(outAlpha.data(), rawPlane.data(), pxCount);
        } else {
            uint16_t maxV16 = 0;
            for (size_t i = 0; i < pxCount; ++i) {
                uint16_t v16 = DetailImg100::RdU16BE(rawPlane.data() + i * 2u);
                if (v16 > maxV16) maxV16 = v16;
            }
            for (size_t i = 0; i < pxCount; ++i) {
                uint16_t v16 = DetailImg100::RdU16BE(rawPlane.data() + i * 2u);
                if (maxV16 > 32768u) {
                    outAlpha[i] = static_cast<uint8_t>((static_cast<uint32_t>(v16) * 255u + 32767u) / 65535u);
                } else if (maxV16 > 255u) {
                    outAlpha[i] = static_cast<uint8_t>(std::min<uint32_t>(255u, (static_cast<uint32_t>(v16) * 255u + 16384u) / 32768u));
                } else {
                    outAlpha[i] = static_cast<uint8_t>(v16);
                }
            }
        }

        bool hasInk = false;
        for (uint8_t a : outAlpha) {
            if (a > 0) { hasInk = true; break; }
        }
        return hasInk;
    };

    if (ver == 1 || ver == 2) {
        uint16_t count = DetailImg100::RdU16BE(data + 2);
        if (count == 0) return false;
        size_t pos = 4;
        for (uint16_t b = 0; b < count && pos + 6 <= size && tips.size() < 8; ++b) {
            uint16_t btype = DetailImg100::RdU16BE(data + pos);
            uint32_t bsize = DetailImg100::RdU32BE(data + pos + 2);
            pos += 6;
            if (bsize > size - pos) break;

            if (btype == 2 && bsize >= 34) { // Sampled brush record
                const uint8_t* recStart = data + pos;
                const uint8_t* recEnd   = recStart + bsize;

                auto tryParseV12At = [&](const uint8_t* afterName, const std::wstring& bName) -> bool {
                    if (afterName + 28 > recEnd) return false;
                    // afterName[0] = antialias (1 byte), afterName[1..8] = shortBounds[4]
                    int16_t sTop    = static_cast<int16_t>(DetailImg100::RdU16BE(afterName + 1));
                    int16_t sLeft   = static_cast<int16_t>(DetailImg100::RdU16BE(afterName + 3));
                    int16_t sBottom = static_cast<int16_t>(DetailImg100::RdU16BE(afterName + 5));
                    int16_t sRight  = static_cast<int16_t>(DetailImg100::RdU16BE(afterName + 7));

                    const uint8_t* bp = afterName + 9;
                    int32_t top    = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 0));
                    int32_t left   = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 4));
                    int32_t bottom = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 8));
                    int32_t right  = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 12));
                    uint16_t depth = DetailImg100::RdU16BE(bp + 16);
                    uint8_t comp   = bp[18];
                    const uint8_t* pix = bp + 19;

                    if (bottom <= top || right <= left) {
                        top = sTop; left = sLeft; bottom = sBottom; right = sRight;
                    }
                    int64_t bw64 = static_cast<int64_t>(right) - static_cast<int64_t>(left);
                    int64_t bh64 = static_cast<int64_t>(bottom) - static_cast<int64_t>(top);
                    if (bw64 <= 0 || bh64 <= 0 || bw64 > 8192 || bh64 > 8192) return false;

                    BrushTip tip;
                    tip.w = static_cast<uint32_t>(bw64);
                    tip.h = static_cast<uint32_t>(bh64);
                    tip.name = bName;
                    size_t avail = static_cast<size_t>(recEnd - pix);
                    if (!decodeAbrSampleBitmap(pix, avail, tip.w, tip.h, depth, comp, true, tip.alpha)) {
                        return false;
                    }
                    tips.push_back(std::move(tip));
                    return true;
                };

                // Skip misc (4 bytes) + spacing (2 bytes) = 6 bytes
                const uint8_t* pName = recStart + 6;
                if (ver == 1) {
                    tryParseV12At(pName, L"");
                } else {
                    bool parsed = false;
                    if (pName + 4 <= recEnd) {
                        uint32_t nameLen = DetailImg100::RdU32BE(pName);
                        if (nameLen <= 2048 && pName + 4 + static_cast<size_t>(nameLen) * 2u <= recEnd) {
                            std::wstring bName;
                            for (uint32_t ci = 0; ci < nameLen; ++ci) {
                                wchar_t wc = static_cast<wchar_t>(DetailImg100::RdU16BE(pName + 4 + static_cast<size_t>(ci) * 2u));
                                if (wc != 0) bName.push_back(wc);
                            }
                            parsed = tryParseV12At(pName + 4 + static_cast<size_t>(nameLen) * 2u, bName);
                        }
                    }
                    if (!parsed && pName + 1 <= recEnd) {
                        uint8_t pasLen = pName[0];
                        if (pName + 1 + pasLen <= recEnd) {
                            parsed = tryParseV12At(pName + 1 + pasLen, L"");
                        }
                    }
                }
            }
            pos += bsize;
        }
    } else {
        // ABR v6, v7, v10: walk top-level 8BIM blocks (with fallback scan for "8BIMsamp")
        auto parseSampSection = [&](size_t secStart, size_t secEnd) {
            size_t p = secStart;
            while (p + 4 <= secEnd && tips.size() < 8) {
                uint32_t bLen = DetailImg100::RdU32BE(data + p);
                p += 4;
                if (bLen == 0 || bLen > secEnd - p) break;

                const uint8_t* entryStart = data + p;
                uint8_t idLen = entryStart[0];
                size_t candidates[6] = {};
                size_t numCand = 0;
                auto pushCand = [&](size_t off) {
                    for (size_t i = 0; i < numCand; ++i) {
                        if (candidates[i] == off) return;
                    }
                    if (numCand < 6) candidates[numCand++] = off;
                };

                if (subVer == 1) {
                    pushCand(47u);
                    pushCand(static_cast<size_t>(1u + idLen + 10u));
                    pushCand(37u);
                    pushCand(static_cast<size_t>(1u + idLen));
                    pushCand(301u);
                    pushCand(264u);
                } else {
                    pushCand(301u);
                    pushCand(static_cast<size_t>(1u + idLen + 264u));
                    pushCand(264u);
                    pushCand(47u);
                    pushCand(37u);
                }

                for (size_t ci = 0; ci < numCand; ++ci) {
                    size_t hdrSkip = candidates[ci];
                    if (hdrSkip + 19u >= bLen) continue;
                    const uint8_t* bp = entryStart + hdrSkip;
                    int32_t top    = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 0));
                    int32_t left   = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 4));
                    int32_t bottom = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 8));
                    int32_t right  = static_cast<int32_t>(DetailImg100::RdU32BE(bp + 12));
                    uint16_t depth = DetailImg100::RdU16BE(bp + 16);
                    uint8_t comp   = bp[18];

                    int64_t bw64 = static_cast<int64_t>(right) - static_cast<int64_t>(left);
                    int64_t bh64 = static_cast<int64_t>(bottom) - static_cast<int64_t>(top);
                    if (bw64 <= 0 || bh64 <= 0 || bw64 > 8192 || bh64 > 8192) continue;

                    BrushTip tip;
                    tip.w = static_cast<uint32_t>(bw64);
                    tip.h = static_cast<uint32_t>(bh64);
                    size_t avail = static_cast<size_t>(bLen) - (hdrSkip + 19u);
                    if (decodeAbrSampleBitmap(bp + 19u, avail, tip.w, tip.h, depth, comp, true, tip.alpha)) {
                        tips.push_back(std::move(tip));
                        break;
                    }
                }

                size_t paddedLen = (static_cast<size_t>(bLen) + 3u) & ~3u;
                if (paddedLen <= secEnd - p) {
                    p += paddedLen;
                } else {
                    p = secEnd;
                }
            }
        };

        bool foundSampBlock = false;
        size_t pos = 4;
        while (pos + 12 <= size && std::memcmp(data + pos, "8BIM", 4) == 0) {
            const uint8_t* key = data + pos + 4;
            uint32_t secLen = DetailImg100::RdU32BE(data + pos + 8);
            size_t secStart = pos + 12;
            if (secLen > size - secStart) {
                secLen = static_cast<uint32_t>(size - secStart);
            }
            size_t secEnd = secStart + secLen;
            if (std::memcmp(key, "samp", 4) == 0) {
                foundSampBlock = true;
                parseSampSection(secStart, secEnd);
            }
            pos = secEnd;
            // Align to next 8BIM tag across 2-byte or 4-byte resource padding
            for (size_t pad = 0; pad < 4 && pos < size; ++pad) {
                if (pos + 4 <= size && std::memcmp(data + pos, "8BIM", 4) == 0) break;
                if (data[pos] != 0) break;
                ++pos;
            }
        }

        if (!foundSampBlock) {
            for (size_t i = 4; i + 12 <= size; ++i) {
                if (std::memcmp(data + i, "8BIMsamp", 8) == 0) {
                    uint32_t secLen = DetailImg100::RdU32BE(data + i + 8);
                    size_t secStart = i + 12;
                    size_t secEnd = (secLen <= size - secStart) ? (secStart + secLen) : size;
                    parseSampSection(secStart, secEnd);
                    break;
                }
            }
        }
    }

    // Strictly reject if zero real sampled brush bitmaps were decoded (no fake circle card!)
    if (tips.empty()) return false;

    for (const auto& t : tips) {
        if (!t.name.empty()) outInfo.layerNames.push_back(t.name);
    }

    if (tips.size() == 1) {
        const auto& tip = tips[0];
        uint32_t dstW = tip.w;
        uint32_t dstH = tip.h;
        if (dstW > 512u || dstH > 512u) {
            double scale = 512.0 / static_cast<double>(std::max(dstW, dstH));
            dstW = std::max(1u, static_cast<uint32_t>(std::round(static_cast<double>(tip.w) * scale)));
            dstH = std::max(1u, static_cast<uint32_t>(std::round(static_cast<double>(tip.h) * scale)));
        }

        outInfo.width = dstW;
        outInfo.height = dstH;
        outInfo.origWidth = tip.w;
        outInfo.origHeight = tip.h;
        outInfo.channels = 4;
        outInfo.bitsPerChannel = 8;
        outInfo.formatName = L"Adobe Photoshop Brush (.abr)";
        outInfo.bgraPixels.resize(static_cast<size_t>(dstW) * dstH * 4u);

        for (uint32_t dy = 0; dy < dstH; ++dy) {
            uint32_t sy = static_cast<uint32_t>((static_cast<uint64_t>(dy) * tip.h) / dstH);
            for (uint32_t dx = 0; dx < dstW; ++dx) {
                uint32_t sx = static_cast<uint32_t>((static_cast<uint64_t>(dx) * tip.w) / dstW);
                uint8_t a = tip.alpha[static_cast<size_t>(sy) * tip.w + sx];
                uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(dy) * dstW + dx) * 4u];
                dst[0] = static_cast<uint8_t>(28u + (224u * a + 127u) / 255u);
                dst[1] = static_cast<uint8_t>(26u + (224u * a + 127u) / 255u);
                dst[2] = static_cast<uint8_t>(24u + (226u * a + 127u) / 255u);
                dst[3] = 255u;
            }
        }
        outInfo.valid = true;
        return true;
    }

    // Multiple brush tips: composite up to 4 (2x2) or 6 (3x2) aspect-preserved cells
    uint32_t gridCols = (tips.size() <= 4) ? 2u : 3u;
    uint32_t gridRows = 2u;
    uint32_t cellSize = 128u;
    uint32_t pad = 12u;
    uint32_t W = gridCols * cellSize + (gridCols + 1u) * pad;
    uint32_t H = gridRows * cellSize + (gridRows + 1u) * pad;

    outInfo.width = W;
    outInfo.height = H;
    outInfo.origWidth = tips[0].w;
    outInfo.origHeight = tips[0].h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Adobe Photoshop Brush Set (.abr)";
    outInfo.bgraPixels.resize(static_cast<size_t>(W) * H * 4u);
    DetailImg200::FillRectBGRA(outInfo.bgraPixels, W, H, 0, 0, static_cast<int>(W), static_cast<int>(H), 20, 20, 24);

    size_t maxCells = static_cast<size_t>(gridCols) * gridRows;
    for (size_t t = 0; t < tips.size() && t < maxCells; ++t) {
        uint32_t gx = static_cast<uint32_t>(t) % gridCols;
        uint32_t gy = static_cast<uint32_t>(t) / gridCols;
        uint32_t cellX = pad + gx * (cellSize + pad);
        uint32_t cellY = pad + gy * (cellSize + pad);
        DetailImg200::FillRectBGRA(
            outInfo.bgraPixels, W, H,
            static_cast<int>(cellX), static_cast<int>(cellY),
            static_cast<int>(cellX + cellSize), static_cast<int>(cellY + cellSize),
            34, 34, 39);

        const auto& tip = tips[t];
        uint32_t innerMax = cellSize - 12u;
        double fitScale = std::min(1.0, static_cast<double>(innerMax) / static_cast<double>(std::max(tip.w, tip.h)));
        if (tip.w < innerMax / 2u && tip.h < innerMax / 2u) {
            fitScale = static_cast<double>(innerMax) / static_cast<double>(std::max(tip.w, tip.h));
        }
        uint32_t drawW = std::max(1u, static_cast<uint32_t>(std::round(static_cast<double>(tip.w) * fitScale)));
        uint32_t drawH = std::max(1u, static_cast<uint32_t>(std::round(static_cast<double>(tip.h) * fitScale)));
        uint32_t ox = cellX + (cellSize - drawW) / 2u;
        uint32_t oy = cellY + (cellSize - drawH) / 2u;

        for (uint32_t dy = 0; dy < drawH; ++dy) {
            uint32_t sy = static_cast<uint32_t>((static_cast<uint64_t>(dy) * tip.h) / drawH);
            for (uint32_t dx = 0; dx < drawW; ++dx) {
                uint32_t sx = static_cast<uint32_t>((static_cast<uint64_t>(dx) * tip.w) / drawW);
                uint8_t a = tip.alpha[static_cast<size_t>(sy) * tip.w + sx];
                uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(oy + dy) * W + (ox + dx)) * 4u];
                dst[0] = static_cast<uint8_t>(39u + (214u * a + 127u) / 255u);
                dst[1] = static_cast<uint8_t>(34u + (216u * a + 127u) / 255u);
                dst[2] = static_cast<uint8_t>(34u + (218u * a + 127u) / 255u);
                dst[3] = 255u;
            }
        }
    }
    outInfo.valid = true;
    return true;
}

// ============================================================================
// #18 & #19. Tajima (.dst) & Brother (.pes) Industrial Embroidery Stitch Renderers
// ============================================================================
inline bool DecodeEmbroideryDstOrPes(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    struct StitchPt { float x, y; int colorIdx; bool jump; };
    std::vector<StitchPt> pts;
    std::wstring label;

    static const uint8_t kThreadColors[12][3] = {
        {239, 68, 68},   {59, 130, 246},  {34, 197, 94},  {250, 204, 21},
        {168, 85, 247},  {249, 115, 22},  {20, 184, 166}, {236, 72, 153},
        {248, 250, 252}, {132, 204, 22},  {56, 189, 248}, {251, 191, 36}
    };

    if (extLower == L".dst" || (size >= 515 && std::memcmp(data, "LA:", 3) == 0)) {
        if (size < 515) return false;
        label = L"Tajima Embroidery Stitch Design (.dst)";
        float cx = 0.0f, cy = 0.0f;
        int col = 0;
        for (size_t p = 512; p + 3 <= size; p += 3) {
            uint8_t b0 = data[p + 0], b1 = data[p + 1], b2 = data[p + 2];
            if (b0 == 0x00 && b1 == 0x00 && b2 == 0xF3) break; // End of stitch stream
            int dx = 0, dy = 0;
            if (b0 & 0x01) dx += 1;  if (b0 & 0x02) dx -= 1;
            if (b0 & 0x04) dx += 9;  if (b0 & 0x08) dx -= 9;
            if (b0 & 0x80) dy += 1;  if (b0 & 0x40) dy -= 1;
            if (b0 & 0x20) dy += 9;  if (b0 & 0x10) dy -= 9;

            if (b1 & 0x01) dx += 3;  if (b1 & 0x02) dx -= 3;
            if (b1 & 0x04) dx += 27; if (b1 & 0x08) dx -= 27;
            if (b1 & 0x80) dy += 3;  if (b1 & 0x40) dy -= 3;
            if (b1 & 0x20) dy += 27; if (b1 & 0x10) dy -= 27;

            if (b2 & 0x04) dx += 81; if (b2 & 0x08) dx -= 81;
            if (b2 & 0x20) dy += 81; if (b2 & 0x10) dy -= 81;

            bool colorChange = ((b2 & 0xC0) == 0xC0);
            bool jump = ((b2 & 0x80) != 0) && !colorChange;
            cx += dx * 0.1f; // 0.1 mm units
            cy -= dy * 0.1f;
            if (colorChange) {
                col = (col + 1) % 12;
                pts.push_back({ cx, cy, col, true });
            } else {
                pts.push_back({ cx, cy, col, jump });
            }
        }
    } else if (extLower == L".pes" || (size >= 64 && std::memcmp(data, "#PES", 4) == 0)) {
        if (size < 64 || std::memcmp(data, "#PES", 4) != 0) return false;
        label = L"Brother Embroidery Stitch Design (.pes)";
        uint32_t pecOff = DetailImg100::RdU32LE(data + 8);
        if (pecOff == 0 || pecOff + 532 > size) return false;

        uint8_t numColors = data[pecOff + 48];
        std::vector<int> pecPalette;
        for (int i = 0; i <= numColors && pecOff + 49 + i < size; ++i) {
            pecPalette.push_back(data[pecOff + 49 + i] % 12);
        }
        if (pecPalette.empty()) pecPalette.push_back(0);

        size_t sp = pecOff + 532; // PEC stitch data starts right after 512-byte header + 20-byte graphic header
        float cx = 0.0f, cy = 0.0f;
        int colIdx = 0;
        while (sp < size && pts.size() < 200000) {
            uint8_t v1 = data[sp++];
            if (v1 == 0xFF && (sp >= size || data[sp] == 0x00)) break; // End of PEC
            if (v1 == 0xFE && sp + 1 < size && data[sp] == 0xB0) {
                sp += 2; // Color change
                colIdx = (colIdx + 1) % static_cast<int>(pecPalette.size());
                pts.push_back({ cx, cy, pecPalette[colIdx], true });
                continue;
            }
            bool jump = false;
            int dx = 0, dy = 0;
            if ((v1 & 0x80) == 0) {
                dx = (v1 >= 0x40) ? (static_cast<int>(v1) - 0x80) : static_cast<int>(v1);
            } else {
                if (sp >= size) break;
                uint8_t b2 = data[sp++];
                if (v1 & 0x20) jump = true;
                int d12 = ((v1 & 0x0F) << 8) | b2;
                if (d12 & 0x800) d12 -= 0x1000;
                dx = d12;
            }
            if (sp >= size) break;
            uint8_t v2 = data[sp++];
            if ((v2 & 0x80) == 0) {
                dy = (v2 >= 0x40) ? (static_cast<int>(v2) - 0x80) : static_cast<int>(v2);
            } else {
                if (sp >= size) break;
                uint8_t b2 = data[sp++];
                if (v2 & 0x20) jump = true;
                int d12 = ((v2 & 0x0F) << 8) | b2;
                if (d12 & 0x800) d12 -= 0x1000;
                dy = d12;
            }
            cx += dx * 0.1f;
            cy += dy * 0.1f;
            pts.push_back({ cx, cy, pecPalette[colIdx], jump });
        }
    } else {
        return false;
    }

    const uint32_t W = 440, H = 440;
    outInfo.width = W; outInfo.height = H;
    outInfo.origWidth = W; outInfo.origHeight = H;
    outInfo.channels = 4; outInfo.bitsPerChannel = 8;
    outInfo.formatName = label;
    outInfo.bgraPixels.resize(W * H * 4);

    // Woven dark linen hoop background
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            uint8_t weave = (((x + y) & 3) == 0) ? 34 : 26;
            uint8_t* dst = &outInfo.bgraPixels[(y * W + x) * 4];
            dst[0] = weave + 8; dst[1] = weave + 2; dst[2] = weave; dst[3] = 255;
        }
    }

    if (!pts.empty()) {
        float minX = pts[0].x, maxX = pts[0].x, minY = pts[0].y, maxY = pts[0].y;
        for (const auto& p : pts) {
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        }
        float span = std::max({ maxX - minX, maxY - minY, 1.0f });
        float scale = (W - 64.0f) / span;
        float midX = (minX + maxX) * 0.5f, midY = (minY + maxY) * 0.5f;

        for (size_t i = 1; i < pts.size(); ++i) {
            if (pts[i].jump) continue;
            int x0 = static_cast<int>(W * 0.5f + (pts[i - 1].x - midX) * scale);
            int y0 = static_cast<int>(H * 0.5f + (pts[i - 1].y - midY) * scale);
            int x1 = static_cast<int>(W * 0.5f + (pts[i].x - midX) * scale);
            int y1 = static_cast<int>(H * 0.5f + (pts[i].y - midY) * scale);
            int ci = pts[i].colorIdx % 12;
            DetailImg200::DrawLineBGRA(
                outInfo.bgraPixels, W, H, x0, y0, x1, y1,
                kThreadColors[ci][0], kThreadColors[ci][1], kThreadColors[ci][2], 2);
        }
    }
    outInfo.valid = true;
    return true;
}

// ============================================================================
// #20. ANSI Escape CP437 BBS Art (.ans) & DEC VT340 Sixel Graphics (.sixel)
// ============================================================================
namespace DetailAnsiCp437 {

// Complete 256-glyph IBM VGA 8x16 Code Page 437 ROM bitmap font (4,096 bytes).
// Bit 7 is x=0 (leftmost pixel), bit 0 is x=7 (rightmost pixel); 16 scanlines per cell.
// Single box-drawing centers on x=3 (0x10) and y=7; double box-drawing centers on x=2,5 (0x24) and y=6,9,
// guaranteeing 100% seamless cell-to-cell connectivity across all 40 box-drawing glyphs (0xB3..0xDA).
inline constexpr uint8_t kVgaCp437Font[256][16] = {
    // 0x00..0x0F: Control / dingbat glyphs (smileys, card suits, bullets, notes, sun)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x00
    {0x00,0x00,0x7E,0x81,0xA5,0x81,0x81,0xBD,0x99,0x81,0x81,0x7E,0x00,0x00,0x00,0x00}, // 0x01 ☺
    {0x00,0x00,0x7E,0xFF,0xDB,0xFF,0xFF,0xC3,0xE7,0xFF,0xFF,0x7E,0x00,0x00,0x00,0x00}, // 0x02 ☻
    {0x00,0x00,0x00,0x00,0x6C,0xFE,0xFE,0xFE,0xFE,0x7C,0x38,0x10,0x00,0x00,0x00,0x00}, // 0x03 ♥
    {0x00,0x00,0x00,0x00,0x10,0x38,0x7C,0xFE,0x7C,0x38,0x10,0x00,0x00,0x00,0x00,0x00}, // 0x04 ♦
    {0x00,0x00,0x00,0x18,0x3C,0x3C,0xE7,0xE7,0xE7,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, // 0x05 ♣
    {0x00,0x00,0x00,0x18,0x3C,0x7E,0xFF,0xFF,0x7E,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, // 0x06 ♠
    {0x00,0x00,0x00,0x00,0x00,0x18,0x3C,0x3C,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x07 •
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xE7,0xC3,0xC3,0xE7,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, // 0x08 ◘
    {0x00,0x00,0x00,0x00,0x3C,0x66,0x42,0x42,0x66,0x3C,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x09 ○
    {0xFF,0xFF,0xFF,0xFF,0xC3,0x99,0xBD,0xBD,0x99,0xC3,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, // 0x0A ◙
    {0x00,0x00,0x1E,0x0E,0x1A,0x32,0x78,0xCC,0xCC,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00}, // 0x0B ♂
    {0x00,0x00,0x3C,0x66,0x66,0x66,0x66,0x3C,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00}, // 0x0C ♀
    {0x00,0x00,0x3F,0x33,0x3F,0x30,0x30,0x30,0x30,0x70,0xF0,0xE0,0x00,0x00,0x00,0x00}, // 0x0D ♪
    {0x00,0x00,0x7F,0x63,0x7F,0x63,0x63,0x63,0x63,0x67,0xE7,0xE6,0xC0,0x00,0x00,0x00}, // 0x0E ♫
    {0x00,0x00,0x18,0xDB,0x3C,0xE7,0x7E,0x7E,0xE7,0x3C,0xDB,0x18,0x00,0x00,0x00,0x00}, // 0x0F ☼
    // 0x10..0x1F: Arrows & symbols
    {0x00,0x80,0xC0,0xE0,0xF0,0xF8,0xFE,0xF8,0xF0,0xE0,0xC0,0x80,0x00,0x00,0x00,0x00}, // 0x10 ►
    {0x00,0x02,0x06,0x0E,0x1E,0x3E,0xFE,0x3E,0x1E,0x0E,0x06,0x02,0x00,0x00,0x00,0x00}, // 0x11 ◄
    {0x00,0x18,0x3C,0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x3C,0x18,0x00,0x00,0x00,0x00}, // 0x12 ↕
    {0x00,0x00,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x66,0x66,0x00,0x00,0x00,0x00}, // 0x13 ‼
    {0x00,0x00,0x7F,0xDB,0xDB,0xDB,0x7B,0x1B,0x1B,0x1B,0x1B,0x1B,0x00,0x00,0x00,0x00}, // 0x14 ¶
    {0x00,0x7C,0xC6,0x60,0x38,0x6C,0xC6,0xC6,0x6C,0x38,0x0C,0xC6,0x7C,0x00,0x00,0x00}, // 0x15 §
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0xFE,0xFE,0xFE,0x00,0x00,0x00,0x00}, // 0x16 ▬
    {0x00,0x18,0x3C,0x7E,0x18,0x18,0x18,0x18,0x7E,0x3C,0x18,0xFF,0x00,0x00,0x00,0x00}, // 0x17 ↨
    {0x00,0x00,0x18,0x3C,0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00}, // 0x18 ↑
    {0x00,0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x3C,0x18,0x00,0x00,0x00,0x00}, // 0x19 ↓
    {0x00,0x00,0x00,0x00,0x00,0x18,0x0C,0xFE,0x0C,0x18,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x1A →
    {0x00,0x00,0x00,0x00,0x00,0x30,0x60,0xFE,0x60,0x30,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x1B ←
    {0x00,0x00,0x00,0x00,0x00,0x00,0xC0,0xC0,0xC0,0xFE,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x1C ∟
    {0x00,0x00,0x00,0x00,0x00,0x24,0x66,0xFF,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x1D ↔
    {0x00,0x00,0x00,0x00,0x10,0x38,0x38,0x7C,0x7C,0xFE,0xFE,0x00,0x00,0x00,0x00,0x00}, // 0x1E ▲
    {0x00,0x00,0x00,0x00,0xFE,0xFE,0x7C,0x7C,0x38,0x38,0x10,0x00,0x00,0x00,0x00,0x00}, // 0x1F ▼
    // 0x20..0x2F: ASCII punctuation & digits
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x20 ' '
    {0x00,0x00,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00}, // 0x21 !
    {0x00,0x66,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x22 "
    {0x00,0x00,0x00,0x6C,0x6C,0xFE,0x6C,0x6C,0x6C,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00}, // 0x23 #
    {0x18,0x18,0x7C,0xC6,0xC2,0xC0,0x7C,0x06,0x06,0x86,0xC6,0x7C,0x18,0x18,0x00,0x00}, // 0x24 $
    {0x00,0x00,0x00,0x00,0xC2,0xC6,0x0C,0x18,0x30,0x60,0xC6,0x86,0x00,0x00,0x00,0x00}, // 0x25 %
    {0x00,0x00,0x38,0x6C,0x6C,0x38,0x76,0xDC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, // 0x26 &
    {0x00,0x30,0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x27 '
    {0x00,0x00,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00,0x00}, // 0x28 (
    {0x00,0x00,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00,0x00}, // 0x29 )
    {0x00,0x00,0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x2A *
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x2B +
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x30,0x00,0x00,0x00}, // 0x2C ,
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x2D -
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00}, // 0x2E .
    {0x00,0x00,0x00,0x00,0x02,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00,0x00,0x00,0x00}, // 0x2F /
    // 0x30..0x3F
    {0x00,0x00,0x7C,0xC6,0xCE,0xDE,0xF6,0xE6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x30 0
    {0x00,0x00,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00}, // 0x31 1
    {0x00,0x00,0x7C,0xC6,0x06,0x0C,0x18,0x30,0x60,0xC0,0xC6,0xFE,0x00,0x00,0x00,0x00}, // 0x32 2
    {0x00,0x00,0x7C,0xC6,0x06,0x06,0x3C,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x33 3
    {0x00,0x00,0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x0C,0x1E,0x00,0x00,0x00,0x00}, // 0x34 4
    {0x00,0x00,0xFE,0xC0,0xC0,0xC0,0xFC,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x35 5
    {0x00,0x00,0x38,0x60,0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x36 6
    {0x00,0x00,0xFE,0xC6,0x06,0x06,0x0C,0x18,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00}, // 0x37 7
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x38 8
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7E,0x06,0x06,0x06,0x0C,0x78,0x00,0x00,0x00,0x00}, // 0x39 9
    {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00}, // 0x3A :
    {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00}, // 0x3B ;
    {0x00,0x00,0x00,0x06,0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x06,0x00,0x00,0x00,0x00}, // 0x3C <
    {0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x3D =
    {0x00,0x00,0x00,0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00}, // 0x3E >
    {0x00,0x00,0x7C,0xC6,0xC6,0x0C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00}, // 0x3F ?
    // 0x40..0x4F: @ A..O
    {0x00,0x00,0x7C,0xC6,0xC6,0xDE,0xDE,0xDE,0xDC,0xC0,0xC0,0x7C,0x00,0x00,0x00,0x00}, // 0x40 @
    {0x00,0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, // 0x41 A
    {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0xFC,0x00,0x00,0x00,0x00}, // 0x42 B
    {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x00,0x00,0x00,0x00}, // 0x43 C
    {0x00,0x00,0xF8,0x6C,0x66,0x66,0x66,0x66,0x66,0x66,0x6C,0xF8,0x00,0x00,0x00,0x00}, // 0x44 D
    {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00}, // 0x45 E
    {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00}, // 0x46 F
    {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xDE,0xC6,0xC6,0x66,0x3A,0x00,0x00,0x00,0x00}, // 0x47 G
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, // 0x48 H
    {0x00,0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, // 0x49 I
    {0x00,0x00,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00}, // 0x4A J
    {0x00,0x00,0xE6,0x66,0x66,0x6C,0x78,0x78,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00}, // 0x4B K
    {0x00,0x00,0xF0,0x60,0x60,0x60,0x60,0x60,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00}, // 0x4C L
    {0x00,0x00,0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, // 0x4D M
    {0x00,0x00,0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, // 0x4E N
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x4F O
    // 0x50..0x5F: P..Z [ \ ] ^ _
    {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00}, // 0x50 P
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x0E,0x00,0x00,0x00}, // 0x51 Q
    {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x6C,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00}, // 0x52 R
    {0x00,0x00,0x7C,0xC6,0xC6,0x60,0x38,0x0C,0x06,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x53 S
    {0x00,0x00,0x7E,0x7E,0x5A,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, // 0x54 T
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x55 U
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00}, // 0x56 V
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xD6,0xD6,0xFE,0xEE,0xC6,0xC6,0x00,0x00,0x00,0x00}, // 0x57 W
    {0x00,0x00,0xC6,0xC6,0x6C,0x7C,0x38,0x38,0x7C,0x6C,0xC6,0xC6,0x00,0x00,0x00,0x00}, // 0x58 X
    {0x00,0x00,0x66,0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, // 0x59 Y
    {0x00,0x00,0xFE,0xC6,0x86,0x0C,0x18,0x30,0x60,0xC2,0xC6,0xFE,0x00,0x00,0x00,0x00}, // 0x5A Z
    {0x00,0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00}, // 0x5B [
    {0x00,0x00,0x00,0x80,0xC0,0xE0,0x70,0x38,0x1C,0x0E,0x06,0x02,0x00,0x00,0x00,0x00}, // 0x5C backslash
    {0x00,0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00}, // 0x5D ]
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x5E ^
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00}, // 0x5F _
    // 0x60..0x6F: ` a..o
    {0x30,0x30,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x60 `
    {0x00,0x00,0x00,0x00,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, // 0x61 a
    {0x00,0x00,0xE0,0x60,0x60,0x78,0x6C,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00}, // 0x62 b
    {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x63 c
    {0x00,0x00,0x1C,0x0C,0x0C,0x3C,0x6C,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, // 0x64 d
    {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x65 e
    {0x00,0x00,0x38,0x6C,0x64,0x60,0xF0,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00}, // 0x66 f
    {0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0xCC,0x78,0x00,0x00}, // 0x67 g
    {0x00,0x00,0xE0,0x60,0x60,0x6C,0x76,0x66,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00}, // 0x68 h
    {0x00,0x00,0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, // 0x69 i
    {0x00,0x00,0x06,0x06,0x00,0x0E,0x06,0x06,0x06,0x06,0x06,0x66,0x66,0x3C,0x00,0x00}, // 0x6A j
    {0x00,0x00,0xE0,0x60,0x60,0x66,0x6C,0x78,0x78,0x6C,0x66,0xE6,0x00,0x00,0x00,0x00}, // 0x6B k
    {0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, // 0x6C l
    {0x00,0x00,0x00,0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0xD6,0xC6,0x00,0x00,0x00,0x00}, // 0x6D m
    {0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00}, // 0x6E n
    {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x6F o
    // 0x70..0x7F: p..z { | } ~ ⌂
    {0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00,0x00}, // 0x70 p
    {0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0x0C,0x1E,0x00,0x00}, // 0x71 q
    {0x00,0x00,0x00,0x00,0x00,0xDC,0x76,0x66,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00}, // 0x72 r
    {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x73 s
    {0x00,0x00,0x10,0x30,0x30,0xFC,0x30,0x30,0x30,0x30,0x36,0x1C,0x00,0x00,0x00,0x00}, // 0x74 t
    {0x00,0x00,0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, // 0x75 u
    {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00}, // 0x76 v
    {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xD6,0xD6,0xFE,0xEE,0x6C,0x00,0x00,0x00,0x00}, // 0x77 w
    {0x00,0x00,0x00,0x00,0x00,0xC6,0x6C,0x38,0x38,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00}, // 0x78 x
    {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0x7E,0x06,0x0C,0xF8,0x00,0x00}, // 0x79 y
    {0x00,0x00,0x00,0x00,0x00,0xFE,0xCC,0x18,0x30,0x60,0xC6,0xFE,0x00,0x00,0x00,0x00}, // 0x7A z
    {0x00,0x00,0x0E,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x18,0x0E,0x00,0x00,0x00,0x00}, // 0x7B {
    {0x00,0x00,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00}, // 0x7C |
    {0x00,0x00,0x70,0x18,0x18,0x18,0x0E,0x18,0x18,0x18,0x18,0x70,0x00,0x00,0x00,0x00}, // 0x7D }
    {0x00,0x00,0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x7E ~
    {0x00,0x00,0x00,0x00,0x10,0x38,0x6C,0xC6,0xC6,0xC6,0xFE,0x00,0x00,0x00,0x00,0x00}, // 0x7F ⌂
    // 0x80..0x9F: CP437 International Latin & currency
    {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x0C,0x06,0x7C,0x00,0x00}, // 0x80 Ç
    {0x00,0x00,0xCC,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, // 0x81 ü
    {0x00,0x0C,0x18,0x30,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x82 é
    {0x00,0x10,0x38,0x6C,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, // 0x83 â
    {0x00,0x00,0xCC,0x00,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, // 0x84 ä
    {0x00,0x60,0x30,0x18,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, // 0x85 à
    {0x00,0x38,0x6C,0x38,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, // 0x86 å
    {0x00,0x00,0x00,0x00,0x3C,0x66,0x60,0x60,0x66,0x3C,0x0C,0x06,0x3C,0x00,0x00,0x00}, // 0x87 ç
    {0x00,0x10,0x38,0x6C,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x88 ê
    {0x00,0x00,0xC6,0x00,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x89 ë
    {0x00,0x60,0x30,0x18,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x8A è
    {0x00,0x00,0x66,0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, // 0x8B ï
    {0x00,0x18,0x3C,0x66,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, // 0x8C î
    {0x00,0x60,0x30,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, // 0x8D ì
    {0x00,0xC6,0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, // 0x8E Ä
    {0x38,0x6C,0x38,0x00,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, // 0x8F Å
    {0x18,0x30,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x66,0xFE,0x00,0x00,0x00,0x00}, // 0x90 É
    {0x00,0x00,0x00,0x00,0x00,0xEC,0x36,0x7E,0xD8,0xD8,0xDC,0x6E,0x00,0x00,0x00,0x00}, // 0x91 æ
    {0x00,0x00,0x3E,0x6C,0xCC,0xCC,0xFE,0xCC,0xCC,0xCC,0xCC,0xCE,0x00,0x00,0x00,0x00}, // 0x92 Æ
    {0x00,0x10,0x38,0x6C,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x93 ô
    {0x00,0x00,0xC6,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x94 ö
    {0x00,0x60,0x30,0x18,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x95 ò
    {0x00,0x30,0x78,0xCC,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, // 0x96 û
    {0x00,0x60,0x30,0x18,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, // 0x97 ù
    {0x00,0x00,0xC6,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00,0x00}, // 0x98 ÿ
    {0x00,0xC6,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x99 Ö
    {0x00,0xC6,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0x9A Ü
    {0x00,0x18,0x18,0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x18,0x18,0x00,0x00,0x00,0x00}, // 0x9B ¢
    {0x00,0x38,0x6C,0x64,0x60,0xF0,0x60,0x60,0x60,0x60,0xE6,0xFC,0x00,0x00,0x00,0x00}, // 0x9C £
    {0x00,0x00,0x66,0x66,0x3C,0x18,0x7E,0x18,0x7E,0x18,0x18,0x18,0x00,0x00,0x00,0x00}, // 0x9D ¥
    {0x00,0xFC,0x66,0x66,0x7C,0x62,0x66,0x6F,0x66,0x66,0x66,0xF3,0x00,0x00,0x00,0x00}, // 0x9E ₧
    {0x00,0x0E,0x1B,0x18,0x18,0x18,0x7E,0x18,0x18,0x18,0x18,0xD8,0x70,0x00,0x00,0x00}, // 0x9F ƒ
    // 0xA0..0xAF: More CP437 Latin & guillemets
    {0x00,0x18,0x30,0x60,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, // 0xA0 á
    {0x00,0x0C,0x18,0x30,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, // 0xA1 í
    {0x00,0x18,0x30,0x60,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0xA2 ó
    {0x00,0x18,0x30,0x60,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, // 0xA3 ú
    {0x00,0x00,0x76,0xDC,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00}, // 0xA4 ñ
    {0x76,0xDC,0x00,0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, // 0xA5 Ñ
    {0x00,0x3C,0x6C,0x6C,0x3E,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xA6 ª
    {0x00,0x38,0x6C,0x6C,0x38,0x00,0x7C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xA7 º
    {0x00,0x00,0x30,0x30,0x00,0x30,0x30,0x60,0xC0,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0xA8 ¿
    {0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0xC0,0xC0,0xC0,0xC0,0x00,0x00,0x00,0x00,0x00}, // 0xA9 ⌐
    {0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x06,0x06,0x06,0x06,0x00,0x00,0x00,0x00,0x00}, // 0xAA ¬
    {0x00,0xC0,0xC0,0xC2,0xC6,0xCC,0x18,0x30,0x60,0xDC,0x86,0x0C,0x18,0x3E,0x00,0x00}, // 0xAB ½
    {0x00,0xC0,0xC0,0xC2,0xC6,0xCC,0x18,0x30,0x66,0xCE,0x9E,0x3E,0x06,0x06,0x00,0x00}, // 0xAC ¼
    {0x00,0x00,0x18,0x18,0x00,0x18,0x18,0x18,0x3C,0x3C,0x3C,0x18,0x00,0x00,0x00,0x00}, // 0xAD ¡
    {0x00,0x00,0x00,0x00,0x00,0x36,0x6C,0xD8,0x6C,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xAE «
    {0x00,0x00,0x00,0x00,0x00,0xD8,0x6C,0x36,0x6C,0xD8,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xAF »
    // 0xB0..0xB2: Shade blocks ░ ▒ ▓
    {0x22,0x88,0x22,0x88,0x22,0x88,0x22,0x88,0x22,0x88,0x22,0x88,0x22,0x88,0x22,0x88}, // 0xB0 ░
    {0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA}, // 0xB1 ▒
    {0xDD,0x77,0xDD,0x77,0xDD,0x77,0xDD,0x77,0xDD,0x77,0xDD,0x77,0xDD,0x77,0xDD,0x77}, // 0xB2 ▓
    // 0xB3..0xDA: Complete 40-glyph Single & Double Box-Drawing Set (seamless 8x16 cell edges!)
    {0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10}, // 0xB3 │
    {0x10,0x10,0x10,0x10,0x10,0x10,0x10,0xF0,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10}, // 0xB4 ┤
    {0x10,0x10,0x10,0x10,0x10,0x10,0xF0,0x10,0x10,0xF0,0x10,0x10,0x10,0x10,0x10,0x10}, // 0xB5 ╡
    {0x24,0x24,0x24,0x24,0x24,0x24,0x24,0xE4,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24}, // 0xB6 ╢
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFC,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24}, // 0xB7 ╖
    {0x00,0x00,0x00,0x00,0x00,0x00,0xF0,0x10,0x10,0xF0,0x10,0x10,0x10,0x10,0x10,0x10}, // 0xB8 ╕
    {0x24,0x24,0x24,0x24,0x24,0x24,0xE4,0x04,0x04,0xE4,0x24,0x24,0x24,0x24,0x24,0x24}, // 0xB9 ╣
    {0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24}, // 0xBA ║
    {0x00,0x00,0x00,0x00,0x00,0x00,0xFC,0x04,0x04,0xE4,0x24,0x24,0x24,0x24,0x24,0x24}, // 0xBB ╗
    {0x24,0x24,0x24,0x24,0x24,0x24,0xE4,0x04,0x04,0xFC,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xBC ╝
    {0x24,0x24,0x24,0x24,0x24,0x24,0x24,0xFC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xBD ╜
    {0x10,0x10,0x10,0x10,0x10,0x10,0xF0,0x10,0x10,0xF0,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xBE ╛
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF0,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10}, // 0xBF ┐
    {0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x1F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xC0 └
    {0x10,0x10,0x10,0x10,0x10,0x10,0x10,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xC1 ┴
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10}, // 0xC2 ┬
    {0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x1F,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10}, // 0xC3 ├
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xC4 ─
    {0x10,0x10,0x10,0x10,0x10,0x10,0x10,0xFF,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10}, // 0xC5 ┼
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F,0x10,0x10,0x1F,0x10,0x10,0x10,0x10,0x10,0x10}, // 0xC6 ╞
    {0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x27,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24}, // 0xC7 ╟
    {0x24,0x24,0x24,0x24,0x24,0x24,0x27,0x20,0x20,0x3F,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xC8 ╚
    {0x00,0x00,0x00,0x00,0x00,0x00,0x3F,0x20,0x20,0x27,0x24,0x24,0x24,0x24,0x24,0x24}, // 0xC9 ╔
    {0x24,0x24,0x24,0x24,0x24,0x24,0xE7,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xCA ╩
    {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0xE7,0x24,0x24,0x24,0x24,0x24,0x24}, // 0xCB ╦
    {0x24,0x24,0x24,0x24,0x24,0x24,0x27,0x20,0x20,0x27,0x24,0x24,0x24,0x24,0x24,0x24}, // 0xCC ╠
    {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xCD ═
    {0x24,0x24,0x24,0x24,0x24,0x24,0xE7,0x00,0x00,0xE7,0x24,0x24,0x24,0x24,0x24,0x24}, // 0xCE ╬
    {0x10,0x10,0x10,0x10,0x10,0x10,0xFF,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xCF ╧
    {0x24,0x24,0x24,0x24,0x24,0x24,0x24,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xD0 ╨
    {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0xFF,0x10,0x10,0x10,0x10,0x10,0x10}, // 0xD1 ╤
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24}, // 0xD2 ╥
    {0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x3F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xD3 ╙
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F,0x10,0x10,0x1F,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xD4 ╘
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x10,0x10,0x1F,0x10,0x10,0x10,0x10,0x10,0x10}, // 0xD5 ╒
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3F,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24}, // 0xD6 ╓
    {0x24,0x24,0x24,0x24,0x24,0x24,0x24,0xFF,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24}, // 0xD7 ╫
    {0x10,0x10,0x10,0x10,0x10,0x10,0xFF,0x10,0x10,0xFF,0x10,0x10,0x10,0x10,0x10,0x10}, // 0xD8 ╪
    {0x10,0x10,0x10,0x10,0x10,0x10,0x10,0xF0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xD9 ┘
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10}, // 0xDA ┌
    // 0xDB..0xDF: Solid & half blocks █ ▄ ▌ ▐ ▀
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, // 0xDB █
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, // 0xDC ▄
    {0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0}, // 0xDD ▌
    {0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F}, // 0xDE ▐
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xDF ▀
    // 0xE0..0xFF: Greek & mathematical symbols
    {0x00,0x00,0x00,0x00,0x00,0x76,0xDC,0xD8,0xD8,0xD8,0xDC,0x76,0x00,0x00,0x00,0x00}, // 0xE0 α
    {0x00,0x00,0x78,0xCC,0xCC,0xCC,0xD8,0xCC,0xC6,0xC6,0xCC,0xD8,0xC0,0xC0,0x00,0x00}, // 0xE1 ß
    {0x00,0x00,0xFE,0xC6,0xC6,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0x00,0x00,0x00,0x00}, // 0xE2 Γ
    {0x00,0x00,0x00,0x00,0xFE,0x6C,0x6C,0x6C,0x6C,0x6C,0x6C,0x6C,0x00,0x00,0x00,0x00}, // 0xE3 π
    {0x00,0x00,0xFE,0xC6,0x60,0x30,0x18,0x30,0x60,0xC6,0xFE,0x00,0x00,0x00,0x00,0x00}, // 0xE4 Σ
    {0x00,0x00,0x00,0x00,0x00,0x7E,0xD8,0xD8,0xD8,0xD8,0xD8,0x70,0x00,0x00,0x00,0x00}, // 0xE5 σ
    {0x00,0x00,0x00,0x00,0x66,0x66,0x66,0x66,0x66,0x7C,0x60,0x60,0xC0,0x00,0x00,0x00}, // 0xE6 µ
    {0x00,0x00,0x00,0x00,0x76,0xDC,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00}, // 0xE7 τ
    {0x00,0x00,0x38,0x18,0x7C,0xD6,0xD6,0xD6,0x7C,0x18,0x38,0x00,0x00,0x00,0x00,0x00}, // 0xE8 Φ
    {0x00,0x00,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0x6C,0x38,0x00,0x00,0x00,0x00,0x00}, // 0xE9 Θ
    {0x00,0x00,0x38,0x6C,0xC6,0xC6,0xC6,0x6C,0x6C,0x6C,0xEE,0x00,0x00,0x00,0x00,0x00}, // 0xEA Ω
    {0x00,0x00,0x1E,0x30,0x18,0x0C,0x3E,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00,0x00}, // 0xEB δ
    {0x00,0x00,0x00,0x00,0x00,0x7E,0xDB,0xDB,0xDB,0x7E,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xEC ∞
    {0x00,0x00,0x00,0x06,0x7C,0xCE,0xD6,0xD6,0xCE,0x7C,0xC0,0x80,0x00,0x00,0x00,0x00}, // 0xED φ
    {0x00,0x00,0x00,0x00,0x3C,0x60,0xC0,0xF8,0xC0,0x60,0x3C,0x00,0x00,0x00,0x00,0x00}, // 0xEE ε
    {0x00,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00}, // 0xEF ∩
    {0x00,0x00,0x00,0x00,0xFE,0x00,0x00,0xFE,0x00,0x00,0xFE,0x00,0x00,0x00,0x00,0x00}, // 0xF0 ≡
    {0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xF1 ±
    {0x00,0x00,0x00,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00,0x7E,0x00,0x00,0x00,0x00}, // 0xF2 ≥
    {0x00,0x00,0x00,0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00,0x7E,0x00,0x00,0x00,0x00}, // 0xF3 ≤
    {0x00,0x00,0x0E,0x1B,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18}, // 0xF4 ⌠
    {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0xD8,0x70,0x00,0x00}, // 0xF5 ⌡
    {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x7E,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00}, // 0xF6 ÷
    {0x00,0x00,0x00,0x00,0x76,0xDC,0x00,0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xF7 ≈
    {0x00,0x38,0x6C,0x6C,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xF8 °
    {0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x3C,0x3C,0x18,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xF9 ∙
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xFA ·
    {0x00,0x0F,0x0C,0x0C,0x0C,0x0C,0x0C,0xEC,0x6C,0x3C,0x1C,0x0C,0x00,0x00,0x00,0x00}, // 0xFB √
    {0x00,0xD8,0x6C,0x6C,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xFC ⁿ
    {0x00,0x70,0x98,0x30,0x60,0xF8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0xFD ²
    {0x00,0x00,0x00,0x00,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x00,0x00,0x00,0x00,0x00}, // 0xFE ■
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}  // 0xFF NBSP
};

inline void Vt340HlsToRgb(int hueDeg, int lightPct, int satPct, uint8_t outRgb[3]) {
    // DEC VT340 HLS wheel has Blue = 0°, Red = 120°, Green = 240°.
    // Shifting by +240° aligns Blue->240°, Red->0°, Green->120° (standard RGB HSL!).
    int normHue = ((hueDeg % 360) + 360 + 240) % 360;
    double l = std::clamp(lightPct, 0, 100) / 100.0;
    double s = std::clamp(satPct, 0, 100) / 100.0;
    double c = (1.0 - std::abs(2.0 * l - 1.0)) * s;
    double h6 = static_cast<double>(normHue) / 60.0;
    double x = c * (1.0 - std::abs(std::fmod(h6, 2.0) - 1.0));
    double m = l - c * 0.5;
    double r1 = 0.0, g1 = 0.0, b1 = 0.0;
    if (h6 < 1.0)      { r1 = c; g1 = x; b1 = 0; }
    else if (h6 < 2.0) { r1 = x; g1 = c; b1 = 0; }
    else if (h6 < 3.0) { r1 = 0; g1 = c; b1 = x; }
    else if (h6 < 4.0) { r1 = 0; g1 = x; b1 = c; }
    else if (h6 < 5.0) { r1 = x; g1 = 0; b1 = c; }
    else               { r1 = c; g1 = 0; b1 = x; }
    outRgb[0] = static_cast<uint8_t>(std::clamp((r1 + m) * 255.0 + 0.5, 0.0, 255.0));
    outRgb[1] = static_cast<uint8_t>(std::clamp((g1 + m) * 255.0 + 0.5, 0.0, 255.0));
    outRgb[2] = static_cast<uint8_t>(std::clamp((b1 + m) * 255.0 + 0.5, 0.0, 255.0));
}

} // namespace DetailAnsiCp437

inline bool DecodeAnsiOrSixelArt(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (!data || size == 0) return false;

    const size_t scanLen = std::min<size_t>(size, 4 * 1024 * 1024);
    std::string_view sv(reinterpret_cast<const char*>(data), scanLen);

    // Check DEC VT340 Sixel Graphics (ESC P [P1;P2;P3] q  or  DCS 0x90 [P1;P2;P3] q)
    auto findSixelDataStart = [&](size_t& outQPos, int& outP2BgMode) -> bool {
        outP2BgMode = 0;
        size_t introEnd = std::string_view::npos;
        size_t escP = sv.find("\x1BP");
        size_t dcsC1 = sv.find('\x90');
        if (escP != std::string_view::npos && (dcsC1 == std::string_view::npos || escP < dcsC1)) {
            introEnd = escP + 2;
        } else if (dcsC1 != std::string_view::npos) {
            introEnd = dcsC1 + 1;
        } else if (extLower == L".sixel") {
            introEnd = 0;
        } else {
            return false;
        }

        int params[3] = { 0, 0, 0 };
        int pIdx = 0;
        for (size_t k = introEnd; k < sv.size() && k < introEnd + 64; ++k) {
            uint8_t ch = static_cast<uint8_t>(sv[k]);
            if (ch == 'q') {
                outQPos = k;
                outP2BgMode = params[1];
                return true;
            }
            if (ch >= '0' && ch <= '9') {
                if (pIdx < 3) params[pIdx] = params[pIdx] * 10 + (ch - '0');
            } else if (ch == ';') {
                ++pIdx;
            } else if (ch != ' ' && ch != '\r' && ch != '\n') {
                break;
            }
        }
        return false;
    };

    size_t qPos = 0;
    int sixelBgMode = 0;
    bool hasSixelStream = findSixelDataStart(qPos, sixelBgMode);
    if (extLower == L".sixel" && !hasSixelStream) {
        return false;
    }

    if (hasSixelStream) {
        // Initialize standard DEC VT340 16-color default registers (0..15); registers 16..1023 default to {0,0,0}
        std::vector<std::array<uint8_t, 3>> pal(1024, { 0, 0, 0 });
        static constexpr uint8_t kVt340Default16[16][3] = {
            {   0,   0,   0 }, //  0: Black
            {  51,  51, 204 }, //  1: Blue
            { 204,  33,  33 }, //  2: Red
            {  51, 204,  51 }, //  3: Green
            { 204,  51, 204 }, //  4: Magenta
            {  51, 204, 204 }, //  5: Cyan
            { 204, 204,  51 }, //  6: Yellow
            { 135, 135, 135 }, //  7: Gray 50%
            {  66,  66,  66 }, //  8: Gray 25%
            {  84,  84, 249 }, //  9: Light Blue
            { 249,  66,  66 }, // 10: Light Red
            {  84, 249,  84 }, // 11: Light Green
            { 249,  84, 249 }, // 12: Light Magenta
            {  84, 249, 249 }, // 13: Light Cyan
            { 249, 249,  84 }, // 14: Light Yellow
            { 255, 255, 255 }  // 15: White
        };
        for (int r = 0; r < 16; ++r) {
            pal[r] = { kVt340Default16[r][0], kVt340Default16[r][1], kVt340Default16[r][2] };
        }

        constexpr uint32_t kMaxSixelDim = 4096;
        uint32_t capW = 64, capH = 64;
        uint32_t explicitW = 0, explicitH = 0;
        const uint8_t initAlpha = (sixelBgMode == 1) ? 0 : 255;
        std::vector<uint8_t> canvas(static_cast<size_t>(capW) * capH * 4, 0);
        if (initAlpha == 255) {
            for (size_t px = 0; px < static_cast<size_t>(capW) * capH; ++px) canvas[px * 4 + 3] = 255;
        }

        auto ensureCanvas = [&](uint32_t needW, uint32_t needH) {
            needW = std::min(needW, kMaxSixelDim);
            needH = std::min(needH, kMaxSixelDim);
            if (needW <= capW && needH <= capH) return;
            uint32_t newW = capW, newH = capH;
            while (newW < needW) newW = std::min(kMaxSixelDim, newW * 2);
            while (newH < needH) newH = std::min(kMaxSixelDim, newH * 2);
            std::vector<uint8_t> next(static_cast<size_t>(newW) * newH * 4, 0);
            if (initAlpha == 255) {
                for (size_t px = 0; px < static_cast<size_t>(newW) * newH; ++px) {
                    next[px * 4 + 0] = pal[0][2];
                    next[px * 4 + 1] = pal[0][1];
                    next[px * 4 + 2] = pal[0][0];
                    next[px * 4 + 3] = 255;
                }
            }
            for (uint32_t y = 0; y < capH; ++y) {
                std::memcpy(&next[static_cast<size_t>(y) * newW * 4],
                            &canvas[static_cast<size_t>(y) * capW * 4],
                            static_cast<size_t>(capW) * 4);
            }
            canvas.swap(next);
            capW = newW;
            capH = newH;
        };

        uint32_t curX = 0, bandY = 0, usedW = 0, usedH = 0;
        size_t renderedSixelCols = 0;
        uint32_t curCol = 1;
        size_t i = qPos + 1;

        while (i < sv.size()) {
            uint8_t c = static_cast<uint8_t>(sv[i]);
            if (c == 0x00) return false; // Reject binary NUL garbage inside Sixel stream
            if (c == 0x1B || c == 0x9C) break; // ESC \ or C1 ST terminator
            if (c == '\r' || c == '\n' || c == ' ' || c == '\t') { ++i; continue; }
            if (c == '$') { curX = 0; ++i; continue; }
            if (c == '-') { curX = 0; bandY += 6; ++i; continue; }

            if (c == '"') {
                // Raster attributes: "Pan;Pad;Ph;Pv
                ++i;
                uint32_t ras[4] = { 0, 0, 0, 0 };
                for (int k = 0; k < 4; ++k) {
                    while (i < sv.size() && sv[i] >= '0' && sv[i] <= '9') {
                        ras[k] = std::min<uint32_t>(65535u, ras[k] * 10u + static_cast<uint32_t>(sv[i++] - '0'));
                    }
                    if (k < 3 && i < sv.size() && sv[i] == ';') ++i;
                    else break;
                }
                if (ras[2] > 0) explicitW = std::min(ras[2], kMaxSixelDim);
                if (ras[3] > 0) explicitH = std::min(ras[3], kMaxSixelDim);
                if (explicitW > 0 || explicitH > 0) {
                    ensureCanvas(std::max(capW, explicitW), std::max(capH, explicitH));
                }
                continue;
            }

            if (c == '#') {
                // Color introducer: #Pc  or  #Pc;Pu;Px;Py;Pz
                ++i;
                uint32_t reg = 0;
                while (i < sv.size() && sv[i] >= '0' && sv[i] <= '9') {
                    reg = std::min<uint32_t>(65535u, reg * 10u + static_cast<uint32_t>(sv[i++] - '0'));
                }
                curCol = (reg < 1024u) ? reg : (reg & 1023u);
                if (i < sv.size() && sv[i] == ';') {
                    int vals[4] = { 0, 0, 0, 0 };
                    int parsed = 0;
                    for (int k = 0; k < 4 && i < sv.size() && sv[i] == ';'; ++k) {
                        ++i;
                        while (i < sv.size() && sv[i] >= '0' && sv[i] <= '9') {
                            vals[k] = std::min(10000, vals[k] * 10 + (sv[i++] - '0'));
                        }
                        ++parsed;
                    }
                    if (parsed >= 4) {
                        if (vals[0] == 2) {
                            // RGB percentages 0..100
                            pal[curCol][0] = static_cast<uint8_t>(std::clamp((vals[1] * 255 + 50) / 100, 0, 255));
                            pal[curCol][1] = static_cast<uint8_t>(std::clamp((vals[2] * 255 + 50) / 100, 0, 255));
                            pal[curCol][2] = static_cast<uint8_t>(std::clamp((vals[3] * 255 + 50) / 100, 0, 255));
                        } else if (vals[0] == 1) {
                            // DEC VT340 HLS (Blue=0°, Red=120°, Green=240°)
                            DetailAnsiCp437::Vt340HlsToRgb(vals[1], vals[2], vals[3], pal[curCol].data());
                        }
                    }
                }
                continue;
            }

            uint32_t repeat = 1;
            if (c == '!') {
                ++i;
                repeat = 0;
                while (i < sv.size() && sv[i] >= '0' && sv[i] <= '9') {
                    repeat = std::min<uint32_t>(kMaxSixelDim, repeat * 10u + static_cast<uint32_t>(sv[i++] - '0'));
                }
                if (repeat == 0) repeat = 1;
                if (i >= sv.size()) break;
                c = static_cast<uint8_t>(sv[i]);
            }

            if (c >= '?' && c <= '~') {
                uint8_t bits = static_cast<uint8_t>(c - '?');
                if (curX < kMaxSixelDim && bandY < kMaxSixelDim) {
                    uint32_t drawCnt = std::min<uint32_t>(repeat, kMaxSixelDim - curX);
                    ensureCanvas(curX + drawCnt, std::min(kMaxSixelDim, bandY + 6u));
                    const auto& rgb = pal[curCol];
                    for (uint32_t r = 0; r < drawCnt; ++r) {
                        uint32_t x = curX + r;
                        for (uint32_t b = 0; b < 6u; ++b) {
                            if ((bits & (1u << b)) && (bandY + b) < capH) {
                                uint8_t* dst = &canvas[(static_cast<size_t>(bandY + b) * capW + x) * 4u];
                                WritePremulBGRA(dst, rgb[0], rgb[1], rgb[2], 255);
                                usedH = std::max(usedH, bandY + b + 1u);
                            }
                        }
                    }
                    usedW = std::max(usedW, curX + drawCnt);
                    if (usedH < std::min(kMaxSixelDim, bandY + 6u) && bits == 0) {
                        usedH = std::max(usedH, std::min(kMaxSixelDim, bandY + 6u));
                    }
                    renderedSixelCols += drawCnt;
                }
                curX = std::min<uint32_t>(kMaxSixelDim, curX + repeat);
                ++i;
                continue;
            }

            // Reject unexpected high-bit binary bytes inside Sixel stream
            if (c < 0x20 || c > 0x7E) return false;
            ++i;
        }

        if (renderedSixelCols == 0) return false;

        uint32_t outW = (explicitW > 0) ? std::max(explicitW, usedW) : std::max(1u, usedW);
        uint32_t outH = (explicitH > 0) ? std::max(explicitH, usedH) : std::max(1u, usedH);
        outW = std::min(outW, capW);
        outH = std::min(outH, capH);

        outInfo.width = outW;
        outInfo.height = outH;
        outInfo.origWidth = outW;
        outInfo.origHeight = outH;
        outInfo.channels = 4;
        outInfo.bitsPerChannel = 8;
        outInfo.formatName = L"DEC VT340 Sixel Graphics (.sixel)";
        outInfo.bgraPixels.resize(static_cast<size_t>(outW) * outH * 4u);
        for (uint32_t y = 0; y < outH; ++y) {
            std::memcpy(&outInfo.bgraPixels[static_cast<size_t>(y) * outW * 4u],
                        &canvas[static_cast<size_t>(y) * capW * 4u],
                        static_cast<size_t>(outW) * 4u);
        }
        outInfo.valid = true;
        return true;
    }

    if (extLower != L".ans") return false;

    // Parse optional trailing 128-byte ACiD SAUCE00 record ("SAUCE00" at size - 128)
    size_t contentEnd = size;
    int cols = 80;
    bool hasSauce = false;
    bool iceColors = true;
    if (size >= 128 && std::memcmp(data + size - 128, "SAUCE00", 7) == 0) {
        const uint8_t* sauce = data + size - 128;
        hasSauce = true;
        contentEnd = size - 128;
        uint8_t dataType = sauce[94];
        uint8_t fileType = sauce[95];
        if (dataType == 1 && (fileType <= 2)) {
            uint16_t tInfo1 = DetailImg100::RdU16LE(sauce + 96);
            if (tInfo1 >= 1 && tInfo1 <= 320) cols = static_cast<int>(tInfo1);
            // ACiD SAUCE v00.5 places TFlags at offset 105; also check 106 when non-ASCII bitfield
            uint8_t tFlags = sauce[105];
            if (tFlags == 0 && sauce[106] > 0 && sauce[106] < 0x20) {
                tFlags = sauce[106];
            }
            iceColors = ((tFlags & 0x01u) != 0);
        }
        uint8_t commentLines = sauce[104];
        if (commentLines > 0) {
            size_t comntBytes = 5u + static_cast<size_t>(commentLines) * 64u;
            if (contentEnd >= comntBytes && std::memcmp(data + contentEnd - comntBytes, "COMNT", 5) == 0) {
                contentEnd -= comntBytes;
            }
        }
        if (contentEnd > 0 && data[contentEnd - 1] == 0x1A) {
            --contentEnd;
        }
    }

    // Scan up to 0x1A EOF and reject binary garbage (high NUL ratio or binary control noise with no ANSI escapes)
    size_t effectiveLen = 0;
    size_t nulCount = 0, escCount = 0, weirdCtrlCount = 0;
    while (effectiveLen < contentEnd && data[effectiveLen] != 0x1A) {
        uint8_t b = data[effectiveLen++];
        if (b == 0x00) ++nulCount;
        else if (b == 0x1B) ++escCount;
        else if (b < 0x20 && b != '\r' && b != '\n' && b != '\t' && b != '\b' && b != 0x0C) ++weirdCtrlCount;
    }
    if (effectiveLen == 0) return false;
    if (nulCount > 0 && (nulCount * 16 >= effectiveLen || (escCount == 0 && !hasSauce))) {
        return false;
    }
    if (escCount == 0 && !hasSauce && weirdCtrlCount * 8 >= effectiveLen) {
        return false;
    }

    static constexpr uint8_t kCga16[16][3] = {
        {   0,   0,   0 }, { 170,   0,   0 }, {   0, 170,   0 }, { 170,  85,   0 },
        {   0,   0, 170 }, { 170,   0, 170 }, {   0, 170, 170 }, { 170, 170, 170 },
        {  85,  85,  85 }, { 255,  85,  85 }, {  85, 255,  85 }, { 255, 255,  85 },
        {  85,  85, 255 }, { 255,  85, 255 }, {  85, 255, 255 }, { 255, 255, 255 }
    };

    auto xterm256ToRgb = [&](int idx, uint8_t out[3]) {
        idx = std::clamp(idx, 0, 255);
        if (idx < 16) {
            out[0] = kCga16[idx][0]; out[1] = kCga16[idx][1]; out[2] = kCga16[idx][2];
        } else if (idx < 232) {
            int c = idx - 16;
            int r = c / 36, g = (c / 6) % 6, b = c % 6;
            out[0] = static_cast<uint8_t>(r == 0 ? 0 : 55 + r * 40);
            out[1] = static_cast<uint8_t>(g == 0 ? 0 : 55 + g * 40);
            out[2] = static_cast<uint8_t>(b == 0 ? 0 : 55 + b * 40);
        } else {
            uint8_t v = static_cast<uint8_t>(8 + (idx - 232) * 10);
            out[0] = out[1] = out[2] = v;
        }
    };

    constexpr int kMaxAnsiRows = 512;
    struct AnsiCell {
        uint8_t ch = ' ';
        uint8_t fg[3] = { 170, 170, 170 };
        uint8_t bg[3] = { 0, 0, 0 };
    };

    int allocRows = 25;
    std::vector<AnsiCell> grid(static_cast<size_t>(cols) * allocRows);
    auto ensureRow = [&](int r) {
        if (r < allocRows) return;
        int nextRows = std::min(kMaxAnsiRows, std::max(r + 1, allocRows * 2));
        grid.resize(static_cast<size_t>(cols) * nextRows);
        allocRows = nextRows;
    };

    int cx = 0, cy = 0, savedX = 0, savedY = 0, maxUsedRow = 1;
    int fgIdx = 7, bgIdx = 0; // -1 when custom 256-color / 24-bit TrueColor is active
    uint8_t customFg[3] = { 170, 170, 170 };
    uint8_t customBg[3] = { 0, 0, 0 };
    bool bold = false, blink = false, inverse = false;
    size_t drawnCells = 0;

    auto resolveAttrColors = [&](uint8_t effFg[3], uint8_t effBg[3]) {
        uint8_t baseFg[3], baseBg[3];
        if (fgIdx >= 0) {
            int idx = (fgIdx < 8 && bold) ? (fgIdx + 8) : (fgIdx & 15);
            baseFg[0] = kCga16[idx][0]; baseFg[1] = kCga16[idx][1]; baseFg[2] = kCga16[idx][2];
        } else {
            baseFg[0] = customFg[0]; baseFg[1] = customFg[1]; baseFg[2] = customFg[2];
        }
        if (bgIdx >= 0) {
            int idx = (bgIdx < 8 && blink && iceColors) ? (bgIdx + 8) : (bgIdx & 15);
            baseBg[0] = kCga16[idx][0]; baseBg[1] = kCga16[idx][1]; baseBg[2] = kCga16[idx][2];
        } else {
            baseBg[0] = customBg[0]; baseBg[1] = customBg[1]; baseBg[2] = customBg[2];
        }
        if (inverse) {
            effFg[0] = baseBg[0]; effFg[1] = baseBg[1]; effFg[2] = baseBg[2];
            effBg[0] = baseFg[0]; effBg[1] = baseFg[1]; effBg[2] = baseFg[2];
        } else {
            effFg[0] = baseFg[0]; effFg[1] = baseFg[1]; effFg[2] = baseFg[2];
            effBg[0] = baseBg[0]; effBg[1] = baseBg[1]; effBg[2] = baseBg[2];
        }
    };

    size_t p = 0;
    while (p < effectiveLen) {
        uint8_t b = data[p++];
        if (b == 0x1B && p < effectiveLen && data[p] == '[') {
            ++p;
            uint8_t privPrefix = 0;
            if (p < effectiveLen && (data[p] == '?' || data[p] == '=' || data[p] == '>')) {
                privPrefix = data[p++];
            }
            std::vector<int> params;
            int cur = 0;
            bool hasNum = false;
            while (p < effectiveLen) {
                uint8_t c = data[p];
                if (c >= '0' && c <= '9') {
                    cur = std::min(100000, cur * 10 + (c - '0'));
                    hasNum = true;
                    ++p;
                } else if (c == ';' || c == ':') {
                    params.push_back(hasNum ? cur : 0);
                    cur = 0;
                    hasNum = false;
                    ++p;
                } else {
                    break;
                }
            }
            if (hasNum || !params.empty()) params.push_back(hasNum ? cur : 0);
            while (p < effectiveLen && data[p] >= 0x20 && data[p] <= 0x2F) ++p; // Intermediate bytes
            if (p >= effectiveLen) break;
            uint8_t cmd = data[p++];

            if (privPrefix != 0) {
                if (privPrefix == '?' && (cmd == 'h' || cmd == 'l') && !params.empty() && params[0] == 33) {
                    iceColors = (cmd == 'h'); // DEC/iCE private mode ?33h / ?33l
                }
                continue;
            }

            if (cmd == 'm') {
                if (params.empty()) params.push_back(0);
                for (size_t pi = 0; pi < params.size(); ++pi) {
                    int v = params[pi];
                    if (v == 0) { fgIdx = 7; bgIdx = 0; bold = false; blink = false; inverse = false; }
                    else if (v == 1) bold = true;
                    else if (v == 5 || v == 6) blink = true;
                    else if (v == 7) inverse = true;
                    else if (v == 22) bold = false;
                    else if (v == 25) blink = false;
                    else if (v == 27) inverse = false;
                    else if (v >= 30 && v <= 37) fgIdx = v - 30;
                    else if (v == 39) fgIdx = 7;
                    else if (v >= 40 && v <= 47) bgIdx = v - 40;
                    else if (v == 49) bgIdx = 0;
                    else if (v >= 90 && v <= 97) fgIdx = (v - 90) + 8;
                    else if (v >= 100 && v <= 107) bgIdx = (v - 100) + 8;
                    else if (v == 38 || v == 48) {
                        uint8_t* targetRgb = (v == 38) ? customFg : customBg;
                        int& targetIdx     = (v == 38) ? fgIdx : bgIdx;
                        if (pi + 1 < params.size() && params[pi + 1] == 5 && pi + 2 < params.size()) {
                            xterm256ToRgb(params[pi + 2], targetRgb);
                            targetIdx = -1;
                            pi += 2;
                        } else if (pi + 1 < params.size() && params[pi + 1] == 2 && pi + 4 < params.size()) {
                            targetRgb[0] = static_cast<uint8_t>(std::clamp(params[pi + 2], 0, 255));
                            targetRgb[1] = static_cast<uint8_t>(std::clamp(params[pi + 3], 0, 255));
                            targetRgb[2] = static_cast<uint8_t>(std::clamp(params[pi + 4], 0, 255));
                            targetIdx = -1;
                            pi += 4;
                        }
                    }
                }
            } else if (cmd == 'H' || cmd == 'f') {
                cy = std::clamp((params.size() >= 1 ? std::max(1, params[0]) : 1) - 1, 0, kMaxAnsiRows - 1);
                cx = std::clamp((params.size() >= 2 ? std::max(1, params[1]) : 1) - 1, 0, cols - 1);
                ensureRow(cy);
            } else if (cmd == 'A') {
                cy = std::max(0, cy - (params.empty() ? 1 : std::max(1, params[0])));
            } else if (cmd == 'B') {
                cy = std::min(kMaxAnsiRows - 1, cy + (params.empty() ? 1 : std::max(1, params[0])));
                ensureRow(cy);
            } else if (cmd == 'C') {
                cx = std::min(cols - 1, cx + (params.empty() ? 1 : std::max(1, params[0])));
            } else if (cmd == 'D') {
                cx = std::max(0, cx - (params.empty() ? 1 : std::max(1, params[0])));
            } else if (cmd == 'E') {
                cy = std::min(kMaxAnsiRows - 1, cy + (params.empty() ? 1 : std::max(1, params[0])));
                cx = 0;
                ensureRow(cy);
            } else if (cmd == 'F') {
                cy = std::max(0, cy - (params.empty() ? 1 : std::max(1, params[0])));
                cx = 0;
            } else if (cmd == 'G') {
                cx = std::clamp((params.empty() ? 1 : std::max(1, params[0])) - 1, 0, cols - 1);
            } else if (cmd == 'J') {
                int mode = params.empty() ? 0 : params[0];
                uint8_t eFg[3], eBg[3];
                resolveAttrColors(eFg, eBg);
                ensureRow(cy);
                AnsiCell blank{ ' ', { eFg[0], eFg[1], eFg[2] }, { eBg[0], eBg[1], eBg[2] } };
                if (mode == 2) {
                    std::fill(grid.begin(), grid.end(), blank);
                    cx = 0; cy = 0;
                } else if (mode == 0) {
                    size_t startIdx = static_cast<size_t>(cy) * cols + cx;
                    if (startIdx < grid.size()) std::fill(grid.begin() + startIdx, grid.end(), blank);
                } else if (mode == 1) {
                    size_t endIdx = std::min(grid.size(), static_cast<size_t>(cy) * cols + cx + 1);
                    std::fill(grid.begin(), grid.begin() + endIdx, blank);
                }
            } else if (cmd == 'K') {
                int mode = params.empty() ? 0 : params[0];
                uint8_t eFg[3], eBg[3];
                resolveAttrColors(eFg, eBg);
                ensureRow(cy);
                AnsiCell blank{ ' ', { eFg[0], eFg[1], eFg[2] }, { eBg[0], eBg[1], eBg[2] } };
                int c0 = (mode == 0) ? cx : 0;
                int c1 = (mode == 1) ? cx : (cols - 1);
                for (int c = c0; c <= c1 && c < cols; ++c) {
                    grid[static_cast<size_t>(cy) * cols + c] = blank;
                }
            } else if (cmd == 's') {
                savedX = cx; savedY = cy;
            } else if (cmd == 'u') {
                cx = savedX; cy = savedY;
                ensureRow(cy);
            }
            continue;
        }

        if (b == '\r') { cx = 0; continue; }
        if (b == '\n') {
            if (cy + 1 < kMaxAnsiRows) {
                ++cy;
                ensureRow(cy);
            }
            continue;
        }
        if (b == '\b') { cx = std::max(0, cx - 1); continue; }
        if (b == '\t') {
            int nextTab = std::min(cols - 1, (cx + 8) & ~7);
            cx = nextTab;
            continue;
        }
        if (b == 0x00) continue;

        if (cy < kMaxAnsiRows && cx < cols) {
            ensureRow(cy);
            AnsiCell& cell = grid[static_cast<size_t>(cy) * cols + cx];
            cell.ch = b;
            resolveAttrColors(cell.fg, cell.bg);
            ++drawnCells;
            maxUsedRow = std::max(maxUsedRow, cy + 1);
            if (++cx >= cols) {
                cx = 0;
                if (cy + 1 < kMaxAnsiRows) {
                    ++cy;
                    ensureRow(cy);
                }
            }
        }
    }

    if (drawnCells == 0 && escCount == 0) return false;

    int rows = std::clamp(maxUsedRow, 1, allocRows);
    uint32_t imgW = static_cast<uint32_t>(cols * 8);
    uint32_t imgH = static_cast<uint32_t>(rows * 16);
    outInfo.width = imgW;
    outInfo.height = imgH;
    outInfo.origWidth = imgW;
    outInfo.origHeight = imgH;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"ANSI Escape CP437 Art (.ans)";
    outInfo.bgraPixels.resize(static_cast<size_t>(imgW) * imgH * 4u);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const AnsiCell& cell = grid[static_cast<size_t>(r) * cols + c];
            const uint8_t* glyph = DetailAnsiCp437::kVgaCp437Font[cell.ch];
            const uint32_t px0 = static_cast<uint32_t>(c * 8);
            const uint32_t py0 = static_cast<uint32_t>(r * 16);
            for (uint32_t gy = 0; gy < 16u; ++gy) {
                uint8_t rowBits = glyph[gy];
                uint8_t* dstRow = &outInfo.bgraPixels[(static_cast<size_t>(py0 + gy) * imgW + px0) * 4u];
                for (uint32_t gx = 0; gx < 8u; ++gx) {
                    const uint8_t* col = ((rowBits & (0x80u >> gx)) != 0) ? cell.fg : cell.bg;
                    WritePremulBGRA(dstRow + gx * 4u, col[0], col[1], col[2], 255);
                }
            }
        }
    }
    outInfo.valid = true;
    return true;
}

inline bool DecodeExtendedImages200(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (extLower == L".cr2" || extLower == L".cr3" || extLower == L".nef" ||
        extLower == L".arw" || extLower == L".dng" || extLower == L".raf" ||
        extLower == L".orf" || extLower == L".rw2" || extLower == L".pef")
    {
        return DecodeCameraRaw200(data, size, extLower, outInfo);
    }
    if (extLower == L".eps" || extLower == L".ai") {
        return DecodeEpsOrAi(data, size, extLower, outInfo);
    }
    if (extLower == L".cdr" || extLower == L".afphoto" || extLower == L".afdesign" || extLower == L".afpub") {
        return DecodeCorelOrAffinity(data, size, extLower, outInfo);
    }
    if (extLower == L".emf" || extLower == L".wmf") {
        return DecodeEmfOrWmf(data, size, extLower, outInfo);
    }
    if (extLower == L".cgm") {
        return DecodeCgmVector(data, size, outInfo);
    }
    if (extLower == L".abr" || extLower == L".pat") {
        return DecodeAbrOrPat(data, size, extLower, outInfo);
    }
    if (extLower == L".dst" || extLower == L".pes") {
        return DecodeEmbroideryDstOrPes(data, size, extLower, outInfo);
    }
    if (extLower == L".ans" || extLower == L".sixel") {
        return DecodeAnsiOrSixelArt(data, size, extLower, outInfo);
    }
    return false;
}

} // namespace Formats
