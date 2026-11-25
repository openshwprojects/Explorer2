// FormatTestsImagesStudioRaw.cpp: Stage 15 self-tests for studio layered/HDR/RAW/palette/typography
// decoders and workstation & retro computer raster formats.
#include "SelfTestCommon.h"
#include "Types.h"
#include "ImageFormatLoader.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "BrotliDecode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace Explorer2::SelfTestSuite {

namespace {

uint64_t HashBgraBytes(const std::vector<uint8_t>& bytes) {
    uint64_t h = 14695981039346656037ull;
    for (uint8_t b : bytes) {
        h ^= static_cast<uint64_t>(b);
        h *= 1099511628211ull;
    }
    return h;
}

void PushU16LE(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}

void PushU16BE(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    b.push_back(static_cast<uint8_t>(v & 0xFFu));
}

void PushU32LE(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
}

void PushU32BE(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    b.push_back(static_cast<uint8_t>(v & 0xFFu));
}

uint32_t ComputeAdler32(const uint8_t* data, size_t len) {
    uint32_t s1 = 1u, s2 = 0u;
    for (size_t i = 0; i < len; ++i) {
        s1 = (s1 + data[i]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    return (s2 << 16) | s1;
}

uint32_t ComputeCrc32Ieee(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc & 1u) ? ((crc >> 1u) ^ 0xEDB88320u) : (crc >> 1u);
        }
    }
    return ~crc;
}

std::vector<uint8_t> CompressRawDeflateFixed(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out;
    uint32_t bitBuf = 0u;
    int bitCnt = 0;
    auto putBitsLSB = [&](uint32_t val, int n) {
        for (int i = 0; i < n; ++i) {
            bitBuf |= (((val >> i) & 1u) << bitCnt);
            if (++bitCnt == 8) {
                out.push_back(static_cast<uint8_t>(bitBuf));
                bitBuf = 0u;
                bitCnt = 0;
            }
        }
    };
    auto putHuffMSB = [&](uint32_t code, int n) {
        for (int i = n - 1; i >= 0; --i) putBitsLSB((code >> i) & 1u, 1);
    };

    putBitsLSB(1u, 1); // BFINAL = 1
    putBitsLSB(1u, 2); // BTYPE  = 01 (Fixed Huffman)
    size_t i = 0;
    while (i < raw.size()) {
        if (i > 0) {
            size_t run = 0;
            while (i + run < raw.size() && run < 10u && raw[i + run] == raw[i - 1u]) ++run;
            if (run >= 3u) {
                uint32_t sym = static_cast<uint32_t>(254u + run);
                putHuffMSB(sym - 256u, 7);
                putHuffMSB(0u, 5);
                i += run;
                continue;
            }
        }
        uint8_t lit = raw[i++];
        if (lit <= 143u) putHuffMSB(0x30u + lit, 8);
        else putHuffMSB(0x190u + (lit - 144u), 9);
    }
    putHuffMSB(0u, 7);
    if (bitCnt > 0) out.push_back(static_cast<uint8_t>(bitBuf));
    return out;
}

std::vector<uint8_t> CompressZlibFixed(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out = { 0x78u, 0x01u };
    std::vector<uint8_t> def = CompressRawDeflateFixed(raw);
    out.insert(out.end(), def.begin(), def.end());
    PushU32BE(out, ComputeAdler32(raw.data(), raw.size()));
    return out;
}

std::vector<uint8_t> BuildMultiFileStoredZip(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
    struct CdItem {
        std::string name;
        uint32_t crc = 0;
        uint32_t sz = 0;
        uint32_t localOff = 0;
    };
    std::vector<uint8_t> z;
    std::vector<CdItem> cd;
    for (const auto& kv : files) {
        CdItem c;
        c.name = kv.first;
        c.crc = ComputeCrc32Ieee(kv.second.data(), kv.second.size());
        c.sz = static_cast<uint32_t>(kv.second.size());
        c.localOff = static_cast<uint32_t>(z.size());
        PushU32LE(z, 0x04034B50u);
        PushU16LE(z, 20u); PushU16LE(z, 0u); PushU16LE(z, 0u);
        PushU16LE(z, 0u);  PushU16LE(z, 0u);
        PushU32LE(z, c.crc); PushU32LE(z, c.sz); PushU32LE(z, c.sz);
        PushU16LE(z, static_cast<uint16_t>(c.name.size())); PushU16LE(z, 0u);
        z.insert(z.end(), c.name.begin(), c.name.end());
        z.insert(z.end(), kv.second.begin(), kv.second.end());
        cd.push_back(std::move(c));
    }
    uint32_t cdOff = static_cast<uint32_t>(z.size());
    for (const auto& c : cd) {
        PushU32LE(z, 0x02014B50u);
        PushU16LE(z, 20u); PushU16LE(z, 20u); PushU16LE(z, 0u); PushU16LE(z, 0u);
        PushU16LE(z, 0u);  PushU16LE(z, 0u);
        PushU32LE(z, c.crc); PushU32LE(z, c.sz); PushU32LE(z, c.sz);
        PushU16LE(z, static_cast<uint16_t>(c.name.size()));
        PushU16LE(z, 0u); PushU16LE(z, 0u); PushU16LE(z, 0u); PushU16LE(z, 0u);
        PushU32LE(z, 0u); PushU32LE(z, c.localOff);
        z.insert(z.end(), c.name.begin(), c.name.end());
    }
    uint32_t cdSize = static_cast<uint32_t>(z.size()) - cdOff;
    PushU32LE(z, 0x06054B50u);
    PushU16LE(z, 0u); PushU16LE(z, 0u);
    PushU16LE(z, static_cast<uint16_t>(cd.size()));
    PushU16LE(z, static_cast<uint16_t>(cd.size()));
    PushU32LE(z, cdSize); PushU32LE(z, cdOff); PushU16LE(z, 0u);
    return z;
}

std::vector<uint8_t> BuildSolidPngRgba(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::vector<uint8_t> png = { 0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au, 0x0Au };
    auto appendPngChunk = [&](const char tag[4], const std::vector<uint8_t>& pay) {
        PushU32BE(png, static_cast<uint32_t>(pay.size()));
        size_t crcBase = png.size();
        png.insert(png.end(), tag, tag + 4);
        png.insert(png.end(), pay.begin(), pay.end());
        PushU32BE(png, ComputeCrc32Ieee(png.data() + crcBase, 4u + pay.size()));
    };
    std::vector<uint8_t> ihdr;
    PushU32BE(ihdr, w);
    PushU32BE(ihdr, h);
    ihdr.insert(ihdr.end(), { 8u, 6u, 0u, 0u, 0u }); // 8-bit RGBA
    appendPngChunk("IHDR", ihdr);

    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(h) * (1u + static_cast<size_t>(w) * 4u));
    for (uint32_t y = 0; y < h; ++y) {
        raw.push_back(0u); // Filter 0 (None)
        for (uint32_t x = 0; x < w; ++x) {
            raw.push_back(r);
            raw.push_back(g);
            raw.push_back(b);
            raw.push_back(a);
        }
    }
    appendPngChunk("IDAT", CompressZlibFixed(raw));
    appendPngChunk("IEND", {});
    return png;
}

struct SfntTableEntry {
    char tag[4];
    std::vector<uint8_t> bytes;
};

std::vector<uint8_t> AssembleSfntTables(uint32_t flavor, std::vector<SfntTableEntry> tables) {
    std::sort(tables.begin(), tables.end(), [](const SfntTableEntry& a, const SfntTableEntry& b) {
        return std::memcmp(a.tag, b.tag, 4) < 0;
    });
    std::vector<uint8_t> sfnt;
    PushU32BE(sfnt, flavor);
    PushU16BE(sfnt, static_cast<uint16_t>(tables.size()));
    PushU16BE(sfnt, 128u);
    PushU16BE(sfnt, 3u);
    PushU16BE(sfnt, 0u);

    size_t dirEnd = 12u + tables.size() * 16u;
    sfnt.resize(dirEnd, 0u);
    for (size_t i = 0; i < tables.size(); ++i) {
        uint32_t off = static_cast<uint32_t>(sfnt.size());
        uint32_t len = static_cast<uint32_t>(tables[i].bytes.size());
        uint32_t csum = 0u;
        for (size_t k = 0; k + 4u <= tables[i].bytes.size(); k += 4u) {
            csum += (static_cast<uint32_t>(tables[i].bytes[k]) << 24) |
                    (static_cast<uint32_t>(tables[i].bytes[k + 1u]) << 16) |
                    (static_cast<uint32_t>(tables[i].bytes[k + 2u]) << 8) |
                    static_cast<uint32_t>(tables[i].bytes[k + 3u]);
        }
        size_t rec = 12u + i * 16u;
        std::memcpy(sfnt.data() + rec, tables[i].tag, 4);
        sfnt[rec + 4u]  = static_cast<uint8_t>((csum >> 24) & 0xFFu);
        sfnt[rec + 5u]  = static_cast<uint8_t>((csum >> 16) & 0xFFu);
        sfnt[rec + 6u]  = static_cast<uint8_t>((csum >> 8) & 0xFFu);
        sfnt[rec + 7u]  = static_cast<uint8_t>(csum & 0xFFu);
        sfnt[rec + 8u]  = static_cast<uint8_t>((off >> 24) & 0xFFu);
        sfnt[rec + 9u]  = static_cast<uint8_t>((off >> 16) & 0xFFu);
        sfnt[rec + 10u] = static_cast<uint8_t>((off >> 8) & 0xFFu);
        sfnt[rec + 11u] = static_cast<uint8_t>(off & 0xFFu);
        sfnt[rec + 12u] = static_cast<uint8_t>((len >> 24) & 0xFFu);
        sfnt[rec + 13u] = static_cast<uint8_t>((len >> 16) & 0xFFu);
        sfnt[rec + 14u] = static_cast<uint8_t>((len >> 8) & 0xFFu);
        sfnt[rec + 15u] = static_cast<uint8_t>(len & 0xFFu);

        sfnt.insert(sfnt.end(), tables[i].bytes.begin(), tables[i].bytes.end());
        while ((sfnt.size() & 3u) != 0u) sfnt.push_back(0u);
    }
    return sfnt;
}

std::vector<SfntTableEntry> BuildBaseSfntTables(
    const std::string& familyName,
    int shapeVariant,
    const std::string& macAsciiTag = "")
{
    std::vector<uint8_t> glyf;
    std::vector<uint32_t> locaOffsets;
    auto appendSimpleGlyph = [&](const std::vector<std::pair<int16_t, int16_t>>& pts, const std::vector<bool>& onCurve) {
        locaOffsets.push_back(static_cast<uint32_t>(glyf.size()));
        if (pts.empty()) return;
        PushU16BE(glyf, 1u);
        PushU16BE(glyf, 0u); PushU16BE(glyf, 0u); PushU16BE(glyf, 700u); PushU16BE(glyf, 800u);
        PushU16BE(glyf, static_cast<uint16_t>(pts.size() - 1u));
        PushU16BE(glyf, 0u);
        for (size_t i = 0; i < pts.size(); ++i) glyf.push_back(onCurve[i] ? 0x01u : 0x00u);
        int16_t prevX = 0;
        for (const auto& p : pts) {
            int16_t dx = static_cast<int16_t>(p.first - prevX);
            prevX = p.first;
            PushU16BE(glyf, static_cast<uint16_t>(dx));
        }
        int16_t prevY = 0;
        for (const auto& p : pts) {
            int16_t dy = static_cast<int16_t>(p.second - prevY);
            prevY = p.second;
            PushU16BE(glyf, static_cast<uint16_t>(dy));
        }
        while ((glyf.size() & 3u) != 0u) glyf.push_back(0u);
    };

    appendSimpleGlyph({}, {});
    if (shapeVariant == 0) {
        appendSimpleGlyph({ {60, 0}, {340, 780}, {620, 0}, {460, 0}, {340, 420}, {220, 0} },
                          { true, true, true, true, true, true });
        appendSimpleGlyph({ {340, 20}, {640, 400}, {340, 780}, {40, 400} },
                          { true, true, true, true });
    } else {
        appendSimpleGlyph({ {40, 80}, {640, 80}, {640, 680}, {340, 820}, {40, 680} },
                          { true, true, true, false, true });
        appendSimpleGlyph({ {80, 0}, {600, 0}, {600, 240}, {260, 240}, {260, 760}, {80, 760} },
                          { true, true, true, true, true, true });
    }
    locaOffsets.push_back(static_cast<uint32_t>(glyf.size()));

    std::vector<uint8_t> loca;
    for (uint32_t off : locaOffsets) PushU32BE(loca, off);

    std::vector<uint8_t> head(54, 0u);
    head[1] = 1u;
    head[12] = 0x5Fu; head[13] = 0x0Fu; head[14] = 0x3Cu; head[15] = 0xF5u;
    head[17] = 0x0Bu;
    head[18] = (1000 >> 8) & 0xFF; head[19] = 1000 & 0xFF;
    head[41] = (700 >> 8) & 0xFF;  head[42] = 700 & 0xFF;
    head[50] = 0u; head[51] = 1u;

    std::vector<uint8_t> maxp = { 0x00u, 0x00u, 0x50u, 0x00u, 0x00u, 0x03u };

    std::vector<uint8_t> hhea(36, 0u);
    hhea[1] = 1u;
    hhea[4] = (800 >> 8) & 0xFF; hhea[5] = 800 & 0xFF;
    hhea[6] = 0xFFu; hhea[7] = 0x38u;
    hhea[34] = 0u; hhea[35] = 3u;

    std::vector<uint8_t> hmtx;
    for (int g = 0; g < 3; ++g) { PushU16BE(hmtx, 700u); PushU16BE(hmtx, 40u); }

    std::vector<uint8_t> cmap;
    PushU16BE(cmap, 0u); PushU16BE(cmap, 1u);
    PushU16BE(cmap, 3u); PushU16BE(cmap, 1u); PushU32BE(cmap, 12u);
    std::vector<uint8_t> sub4;
    PushU16BE(sub4, 4u); PushU16BE(sub4, 0u); PushU16BE(sub4, 0u);
    PushU16BE(sub4, 6u); PushU16BE(sub4, 4u); PushU16BE(sub4, 1u); PushU16BE(sub4, 2u);
    PushU16BE(sub4, 'M'); PushU16BE(sub4, 'z'); PushU16BE(sub4, 0xFFFFu);
    PushU16BE(sub4, 0u);
    PushU16BE(sub4, '0'); PushU16BE(sub4, 'N'); PushU16BE(sub4, 0xFFFFu);
    std::vector<uint16_t> glyphArr;
    for (uint16_t c = '0'; c <= 'M'; ++c) glyphArr.push_back(1u);
    size_t seg1Start = glyphArr.size();
    for (uint16_t c = 'N'; c <= 'z'; ++c) glyphArr.push_back(2u);
    PushU16BE(sub4, 0u); PushU16BE(sub4, 0u); PushU16BE(sub4, 1u);
    PushU16BE(sub4, 6u);
    PushU16BE(sub4, static_cast<uint16_t>(4u + seg1Start * 2u));
    PushU16BE(sub4, 0u);
    for (uint16_t g : glyphArr) PushU16BE(sub4, g);
    sub4[2] = static_cast<uint8_t>((sub4.size() >> 8) & 0xFFu);
    sub4[3] = static_cast<uint8_t>(sub4.size() & 0xFFu);
    cmap.insert(cmap.end(), sub4.begin(), sub4.end());

    std::vector<uint8_t> nameStr;
    uint16_t macBytes = 0u;
    if (!macAsciiTag.empty()) {
        nameStr.insert(nameStr.end(), macAsciiTag.begin(), macAsciiTag.end());
        macBytes = static_cast<uint16_t>(nameStr.size());
    }
    uint16_t famOff = static_cast<uint16_t>(nameStr.size());
    for (char c : familyName) PushU16BE(nameStr, static_cast<uint8_t>(c));
    uint16_t famBytes = static_cast<uint16_t>(nameStr.size() - famOff);
    uint16_t subOff = static_cast<uint16_t>(nameStr.size());
    const char* subFam = "Regular";
    for (size_t i = 0; subFam[i]; ++i) PushU16BE(nameStr, static_cast<uint8_t>(subFam[i]));
    uint16_t subBytes = static_cast<uint16_t>(nameStr.size() - subOff);

    const uint16_t recCount = macAsciiTag.empty() ? 2u : 3u;
    std::vector<uint8_t> nameTbl;
    PushU16BE(nameTbl, 0u);
    PushU16BE(nameTbl, recCount);
    PushU16BE(nameTbl, static_cast<uint16_t>(6u + recCount * 12u));
    if (!macAsciiTag.empty()) {
        PushU16BE(nameTbl, 1u); PushU16BE(nameTbl, 0u); PushU16BE(nameTbl, 0u);
        PushU16BE(nameTbl, 4u); PushU16BE(nameTbl, macBytes); PushU16BE(nameTbl, 0u);
    }
    PushU16BE(nameTbl, 3u); PushU16BE(nameTbl, 1u); PushU16BE(nameTbl, 0x0409u);
    PushU16BE(nameTbl, 1u); PushU16BE(nameTbl, famBytes); PushU16BE(nameTbl, famOff);
    PushU16BE(nameTbl, 3u); PushU16BE(nameTbl, 1u); PushU16BE(nameTbl, 0x0409u);
    PushU16BE(nameTbl, 2u); PushU16BE(nameTbl, subBytes); PushU16BE(nameTbl, subOff);
    nameTbl.insert(nameTbl.end(), nameStr.begin(), nameStr.end());

    return {
        { {'c','m','a','p'}, std::move(cmap) },
        { {'g','l','y','f'}, std::move(glyf) },
        { {'h','e','a','d'}, std::move(head) },
        { {'h','h','e','a'}, std::move(hhea) },
        { {'h','m','t','x'}, std::move(hmtx) },
        { {'l','o','c','a'}, std::move(loca) },
        { {'m','a','x','p'}, std::move(maxp) },
        { {'n','a','m','e'}, std::move(nameTbl) },
    };
}

std::vector<uint8_t> BuildTrueTypeCollection(
    const std::vector<uint8_t>& face0,
    const std::vector<uint8_t>& face1)
{
    std::vector<uint8_t> ttc = { 't', 't', 'c', 'f' };
    PushU16BE(ttc, 1u);
    PushU16BE(ttc, 0u);
    PushU32BE(ttc, 2u); // 2 faces
    const uint32_t off0 = 20u;
    const uint32_t off1 = 20u + static_cast<uint32_t>(face0.size());
    PushU32BE(ttc, off0);
    PushU32BE(ttc, off1);

    auto appendShiftedSfnt = [&](const std::vector<uint8_t>& sfnt, uint32_t baseShift) {
        std::vector<uint8_t> shifted = sfnt;
        uint16_t numTables = static_cast<uint16_t>((shifted[4] << 8) | shifted[5]);
        for (uint16_t i = 0; i < numTables; ++i) {
            size_t rec = 12u + static_cast<size_t>(i) * 16u;
            uint32_t origOff = (static_cast<uint32_t>(shifted[rec + 8u]) << 24) |
                               (static_cast<uint32_t>(shifted[rec + 9u]) << 16) |
                               (static_cast<uint32_t>(shifted[rec + 10u]) << 8) |
                               static_cast<uint32_t>(shifted[rec + 11u]);
            uint32_t newOff = origOff + baseShift;
            shifted[rec + 8u]  = static_cast<uint8_t>((newOff >> 24) & 0xFFu);
            shifted[rec + 9u]  = static_cast<uint8_t>((newOff >> 16) & 0xFFu);
            shifted[rec + 10u] = static_cast<uint8_t>((newOff >> 8) & 0xFFu);
            shifted[rec + 11u] = static_cast<uint8_t>(newOff & 0xFFu);
        }
        ttc.insert(ttc.end(), shifted.begin(), shifted.end());
    };
    appendShiftedSfnt(face0, off0);
    appendShiftedSfnt(face1, off1);
    return ttc;
}

struct LsbBitWriter {
    std::vector<uint8_t> bytes;
    uint32_t bitBuf = 0u;
    uint32_t bitCnt = 0u;

    void PutBits(uint32_t v, uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) {
            bitBuf |= (((v >> i) & 1u) << bitCnt);
            if (++bitCnt == 8u) {
                bytes.push_back(static_cast<uint8_t>(bitBuf));
                bitBuf = 0u;
                bitCnt = 0u;
            }
        }
    }

    void AlignZero() {
        if (bitCnt > 0u) {
            bytes.push_back(static_cast<uint8_t>(bitBuf));
            bitBuf = 0u;
            bitCnt = 0u;
        }
    }
};

uint32_t BitRev(uint32_t v, uint32_t n) {
    uint32_t r = 0u;
    for (uint32_t i = 0; i < n; ++i) r = (r << 1u) | ((v >> i) & 1u);
    return r;
}

// Encodes `raw` into a single-metablock RFC 7932 Brotli bitstream that replaces the 4-byte word at
// `dictMatchPos` with an explicit RFC 7932 static dictionary reference (`uDist = dictMatchPos + 1 + dictWordIdx`).
std::vector<uint8_t> CompressBrotliWithStaticDict4(
    const std::vector<uint8_t>& raw,
    size_t dictMatchPos,
    uint32_t dictWordIdx)
{
    static constexpr uint32_t kInsBase[24] = {
        0, 1, 2, 3, 4, 5, 6, 8, 10, 14, 18, 26, 34, 50, 66, 98,
        130, 194, 322, 578, 1090, 2114, 6210, 22594
    };
    static constexpr uint8_t kInsExtra[24] = {
        0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5,
        6, 7, 8, 9, 10, 12, 14, 24
    };
    auto findInsCode = [&](uint32_t len) {
        uint32_t c = 0u;
        for (uint32_t i = 0; i < 24u; ++i) {
            if (len >= kInsBase[i]) c = i;
        }
        return c;
    };
    // Build IAC symbol with explicit distance (rangeIdx >= 2) and copyCode == 2 (copyLen == 4, 0 extra bits)
    auto makeIacSymCopy4 = [](uint32_t insCode) -> uint32_t {
        if (insCode < 8u)  return (2u << 6u) | (insCode << 3u) | 2u;            // range 2: ins 0..7, copy 0..7
        if (insCode < 16u) return (4u << 6u) | ((insCode - 8u) << 3u) | 2u;     // range 4: ins 8..15, copy 0..7
        return (7u << 6u) | ((insCode - 16u) << 3u) | 2u;                       // range 7: ins 16..23, copy 0..7
    };

    const uint32_t mlen = static_cast<uint32_t>(raw.size());
    const uint32_t ins0 = static_cast<uint32_t>(dictMatchPos);
    const uint32_t ins1 = mlen - static_cast<uint32_t>(dictMatchPos + 4u);
    const uint32_t ic0  = findInsCode(ins0);
    const uint32_t ic1  = findInsCode(ins1);
    const uint32_t sym0 = makeIacSymCopy4(ic0);
    const uint32_t sym1 = makeIacSymCopy4(ic1);

    // Distance encoding with NPOSTFIX=0, NDIRECT=0:
    // fullDist = offset + dextra + 1, where offset = ((2 + (hcode & 1)) << ndistbits) - 4, ndistbits = 1 + (hcode >> 1)
    const uint32_t targetDist = static_cast<uint32_t>(dictMatchPos) + 1u + dictWordIdx;
    const uint32_t distMinus1 = targetDist - 1u; // >= 0
    uint32_t hcode = 0u, ndistbits = 1u, dextra = 0u;
    for (uint32_t hc = 0u; hc < 48u; ++hc) {
        uint32_t nb = 1u + (hc >> 1u);
        uint32_t off = ((2u + (hc & 1u)) << nb) - 4u;
        if (distMinus1 >= off && (distMinus1 - off) < (1u << nb)) {
            hcode = hc;
            ndistbits = nb;
            dextra = distMinus1 - off;
            break;
        }
    }
    const uint32_t dSym = 16u + hcode;

    LsbBitWriter bw;
    bw.PutBits(0u, 1u); // WBITS = 16
    bw.PutBits(1u, 1u); // ISLAST = 1
    bw.PutBits(0u, 1u); // ISLASTEMPTY = 0
    bw.PutBits(0u, 2u); // MNIBBLES = 4
    bw.PutBits(mlen - 1u, 16u);
    bw.PutBits(0u, 3u); // NBLTYPES L/I/D = 1
    bw.PutBits(0u, 2u); // NPOSTFIX = 0
    bw.PutBits(0u, 4u); // NDIRECT = 0
    bw.PutBits(0u, 2u); // ctxModes[0] = 0
    bw.PutBits(0u, 1u); // NTREESL = 1
    bw.PutBits(0u, 1u); // NTREESD = 1

    // 256 literals all of Huffman length 8
    bw.PutBits(0u, 2u);
    for (int i = 0; i < 8; ++i) bw.PutBits(0u, 2u);
    bw.PutBits(7u, 4u);
    bw.PutBits(0u, 2u);
    bw.PutBits(7u, 4u);
    for (int s = 0; s < 256; ++s) bw.PutBits(0u, 1u);

    // InsertAndCopy prefix code: 1-symbol or 2-symbol simple tree
    if (sym0 == sym1) {
        bw.PutBits(1u, 2u); bw.PutBits(0u, 2u); bw.PutBits(sym0, 10u);
    } else {
        uint32_t lo = std::min(sym0, sym1), hi = std::max(sym0, sym1);
        bw.PutBits(1u, 2u); bw.PutBits(1u, 2u);
        bw.PutBits(lo, 10u); bw.PutBits(hi, 10u);
    }

    // Distance prefix code: 1-symbol simple tree (alphabetSize = 64 -> 6 bits)
    bw.PutBits(1u, 2u); bw.PutBits(0u, 2u); bw.PutBits(dSym, 6u);

    // Command 0: insert `ins0` literals, then static-dictionary copy of 4 bytes!
    if (sym0 != sym1) bw.PutBits(sym0 < sym1 ? 0u : 1u, 1u);
    bw.PutBits(ins0 - kInsBase[ic0], kInsExtra[ic0]);
    for (size_t i = 0; i < dictMatchPos; ++i) bw.PutBits(BitRev(raw[i], 8u), 8u);
    bw.PutBits(dextra, ndistbits);

    // Command 1: insert remaining `ins1` literals (decoder hits produced == mlen and exits cleanly)
    if (ins1 > 0u) {
        if (sym0 != sym1) bw.PutBits(sym1 < sym0 ? 0u : 1u, 1u);
        bw.PutBits(ins1 - kInsBase[ic1], kInsExtra[ic1]);
        for (size_t i = dictMatchPos + 4u; i < raw.size(); ++i) bw.PutBits(BitRev(raw[i], 8u), 8u);
    }

    bw.AlignZero();
    return bw.bytes;
}

void PushUIntBase128(std::vector<uint8_t>& out, uint32_t v) {
    uint8_t buf[5] = {};
    int n = 0;
    do {
        buf[n++] = static_cast<uint8_t>(v & 0x7Fu);
        v >>= 7u;
    } while (v != 0u);
    for (int i = n - 1; i >= 0; --i) {
        out.push_back(static_cast<uint8_t>(buf[i] | (i > 0 ? 0x80u : 0x00u)));
    }
}

std::vector<uint8_t> WrapSfntIntoWoff2CustomBrotli(
    const std::vector<uint8_t>& sfnt,
    std::vector<uint8_t>& outRawConcat,
    size_t dictMatchOffsetInConcat,
    uint32_t dictWordIdx)
{
    static const char kWoff2KnownTags[63][5] = {
        "cmap","head","hhea","hmtx","maxp","name","OS/2","post","cvt ","fpgm","glyf","loca","prep","CFF ","VORG","EBDT",
        "EBLC","gasp","hdmx","kern","LTSH","PCLT","VDMX","vhea","vmtx","BASE","GDEF","GPOS","GSUB","EBSC","JSTF","MATH",
        "CBDT","CBLC","COLR","CPAL","SVG ","sbix","acnt","avar","bdat","bloc","bsln","cvar","fdsc","feat","fmtx","fvar",
        "gvar","hsty","just","lcar","mort","morx","opbd","prop","trak","Zapf","Silf","Glat","Gloc","Feat","Sill"
    };
    uint16_t numTables = static_cast<uint16_t>((sfnt[4] << 8) | sfnt[5]);
    std::vector<uint8_t> dirBytes;
    outRawConcat.clear();
    for (uint16_t i = 0; i < numTables; ++i) {
        const uint8_t* srec = sfnt.data() + 12u + static_cast<size_t>(i) * 16u;
        uint32_t off = (static_cast<uint32_t>(srec[8]) << 24)  | (static_cast<uint32_t>(srec[9]) << 16) |
                       (static_cast<uint32_t>(srec[10]) << 8)  | static_cast<uint32_t>(srec[11]);
        uint32_t len = (static_cast<uint32_t>(srec[12]) << 24) | (static_cast<uint32_t>(srec[13]) << 16) |
                       (static_cast<uint32_t>(srec[14]) << 8)  | static_cast<uint32_t>(srec[15]);
        uint8_t tagIdx = 63u;
        for (uint8_t k = 0; k < 63u; ++k) {
            if (std::memcmp(srec, kWoff2KnownTags[k], 4) == 0) { tagIdx = k; break; }
        }
        bool isGlyfOrLoca = (std::memcmp(srec, "glyf", 4) == 0 || std::memcmp(srec, "loca", 4) == 0);
        uint8_t flags = static_cast<uint8_t>((isGlyfOrLoca ? (3u << 6u) : 0u) | tagIdx);
        dirBytes.push_back(flags);
        if (tagIdx == 63u) dirBytes.insert(dirBytes.end(), srec, srec + 4);
        PushUIntBase128(dirBytes, len);
        outRawConcat.insert(outRawConcat.end(), sfnt.begin() + off, sfnt.begin() + off + len);
    }

    std::vector<uint8_t> comp = CompressBrotliWithStaticDict4(outRawConcat, dictMatchOffsetInConcat, dictWordIdx);

    std::vector<uint8_t> f(48, 0u);
    std::memcpy(f.data(), "wOF2", 4);
    std::memcpy(f.data() + 4, sfnt.data(), 4);
    f[12] = sfnt[4]; f[13] = sfnt[5];
    uint32_t totalSfnt = static_cast<uint32_t>(sfnt.size());
    f[16] = static_cast<uint8_t>((totalSfnt >> 24) & 0xFFu);
    f[17] = static_cast<uint8_t>((totalSfnt >> 16) & 0xFFu);
    f[18] = static_cast<uint8_t>((totalSfnt >> 8) & 0xFFu);
    f[19] = static_cast<uint8_t>(totalSfnt & 0xFFu);
    uint32_t compSize = static_cast<uint32_t>(comp.size());
    f[20] = static_cast<uint8_t>((compSize >> 24) & 0xFFu);
    f[21] = static_cast<uint8_t>((compSize >> 16) & 0xFFu);
    f[22] = static_cast<uint8_t>((compSize >> 8) & 0xFFu);
    f[23] = static_cast<uint8_t>(compSize & 0xFFu);
    f.insert(f.end(), dirBytes.begin(), dirBytes.end());
    f.insert(f.end(), comp.begin(), comp.end());
    while ((f.size() & 3u) != 0u) f.push_back(0u);
    uint32_t totalLen = static_cast<uint32_t>(f.size());
    f[8]  = static_cast<uint8_t>((totalLen >> 24) & 0xFFu);
    f[9]  = static_cast<uint8_t>((totalLen >> 16) & 0xFFu);
    f[10] = static_cast<uint8_t>((totalLen >> 8) & 0xFFu);
    f[11] = static_cast<uint8_t>(totalLen & 0xFFu);
    return f;
}

void AssertImgRejected(TestCounter& tc, const wchar_t* testName, const std::vector<uint8_t>& badBytes, const wchar_t* ext) {
    ExtendedImageInfo badInfo;
    bool ok = ImageFormatLoader::DecodeImageMemory(badBytes.data(), badBytes.size(), ext, 0, badInfo);
    tc.Record(!ok && !badInfo.valid && badInfo.bgraPixels.empty(), testName,
              ok ? L"unexpectedly decoded corrupt input" : L"rejected cleanly");
}

} // namespace

void RunFormatTestsImagesStudioRaw(TestCounter& tc) {
    const std::wstring imagesDir   = TestDataDir() + L"\\images";
    const std::wstring rawDir      = TestDataDir() + L"\\raw";
    const std::wstring archivesDir = TestDataDir() + L"\\archives";
    const std::wstring models3dDir = TestDataDir() + L"\\3d";
    const std::wstring gisDir      = TestDataDir() + L"\\gis";

    // ========================================================================
    // 1. RenderWare Texture Dictionary (.txd — D3D8/D3D9 DXT1/3/5 & PAL8)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".txd") && ImageFormatLoader::IsExtendedImageExtension(L".txd"),
                  L"Stage15.Images.RenderWareTXD_ExtensionRegistered");

        ExtendedImageInfo txdInfo;
        bool okTxd = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\san_andreas_neon.txd", 0, txdInfo);
        bool pxOk = okTxd && txdInfo.valid && txdInfo.width == 8 && txdInfo.height == 8 &&
                    txdInfo.layerNames.size() == 2 &&
                    txdInfo.layerNames[0] == L"neon_sign_dxt1" &&
                    txdInfo.layerNames[1] == L"road_trim_pal8";
        if (pxOk) {
            const uint8_t* pMag  = &txdInfo.bgraPixels[(1 * 8 + 1) * 4];
            const uint8_t* pCyan = &txdInfo.bgraPixels[(1 * 8 + 5) * 4];
            pxOk = (pMag[2] > 240 && pMag[1] < 15 && pMag[0] > 240 && pMag[3] == 255) &&
                   (pCyan[2] < 15 && pCyan[1] > 240 && pCyan[0] > 240 && pCyan[3] == 255);
        }
        tc.Record(pxOk, L"Stage15.Images.RenderWareTXD_SampleDXT1AndMultiTexLayers");

        std::vector<uint8_t> palTxd;
        auto appendRw = [](std::vector<uint8_t>& dst, uint32_t tp, const std::vector<uint8_t>& pay) {
            PushU32LE(dst, tp);
            PushU32LE(dst, static_cast<uint32_t>(pay.size()));
            PushU32LE(dst, 0x1003FFFFu);
            dst.insert(dst.end(), pay.begin(), pay.end());
        };
        std::vector<uint8_t> tnSt(88, 0);
        uint32_t plat8 = 8, rFmt = 0x2500u;
        uint16_t tw = 4, th = 4;
        std::memcpy(tnSt.data() + 0, &plat8, 4);
        std::memcpy(tnSt.data() + 8, "vc_palm_leaf", 12);
        std::memcpy(tnSt.data() + 72, &rFmt, 4);
        std::memcpy(tnSt.data() + 80, &tw, 2);
        std::memcpy(tnSt.data() + 82, &th, 2);
        tnSt[84] = 8; tnSt[85] = 1; tnSt[86] = 4;
        std::vector<uint8_t> pal(1024, 0);
        pal[0] = 30; pal[1] = 220; pal[2] = 60; pal[3] = 255;
        pal[4] = 240; pal[5] = 90; pal[6] = 20; pal[7] = 255;
        tnSt.insert(tnSt.end(), pal.begin(), pal.end());
        PushU32LE(tnSt, 16);
        for (int i = 0; i < 16; ++i) tnSt.push_back(static_cast<uint8_t>(i & 1));
        std::vector<uint8_t> tnCh, tnWrap, dictSt = { 1, 0, 2, 0 }, dictBody;
        appendRw(tnCh, 0x01u, tnSt);
        appendRw(tnWrap, 0x15u, tnCh);
        appendRw(dictBody, 0x01u, dictSt);
        dictBody.insert(dictBody.end(), tnWrap.begin(), tnWrap.end());
        appendRw(palTxd, 0x16u, dictBody);

        ExtendedImageInfo palInfo;
        bool okPal = ImageFormatLoader::DecodeImageMemory(palTxd.data(), palTxd.size(), L".txd", 0, palInfo);
        tc.Record(okPal && palInfo.valid && palInfo.width == 4 && palInfo.height == 4 &&
                  HashBgraBytes(palInfo.bgraPixels) != HashBgraBytes(txdInfo.bgraPixels),
                  L"Stage15.Images.RenderWareTXD_Pal8DistinctHash");

        std::vector<uint8_t> wrongRoot(48, 0);
        PushU32LE(wrongRoot, 0x10u);
        AssertImgRejected(tc, L"Stage15.Images.RenderWareTXD_RejectNonDictChunk", wrongRoot, L".txd");

        std::vector<uint8_t> zeroTexDict, zBody, zSt = { 0, 0, 2, 0 };
        appendRw(zBody, 0x01u, zSt);
        appendRw(zeroTexDict, 0x16u, zBody);
        AssertImgRejected(tc, L"Stage15.Images.RenderWareTXD_RejectZeroTextures", zeroTexDict, L".txd");
    }

    // ========================================================================
    // 2. Unreal Engine 4/5 Direct UTexture2D (.uasset + .uexp + .ubulk)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".uasset") && ImageFormatLoader::IsExtendedImageExtension(L".uasset"),
                  L"Stage15.Images.UnrealTexture2D_ExtensionRegistered");

        ExtendedImageInfo ueTex;
        bool okUeTex = ImageFormatLoader::DecodeImageFile(models3dDir + L"\\ue4_scifi_panel.uasset", 0, ueTex);
        bool pxOk = okUeTex && ueTex.valid && ueTex.width == 8 && ueTex.height == 8;
        if (pxOk) {
            const uint8_t* pYel  = &ueTex.bgraPixels[(1 * 8 + 1) * 4];
            const uint8_t* pBlue = &ueTex.bgraPixels[(1 * 8 + 5) * 4];
            pxOk = (pYel[2] > 240 && pYel[1] > 240 && pYel[0] < 15 && pYel[3] == 255) &&
                   (pBlue[2] < 15 && pBlue[1] < 15 && pBlue[0] > 240 && pBlue[3] == 255);
        }
        tc.Record(pxOk, L"Stage15.Images.UnrealTexture2D_SampleDXT1WithUbulk");

        ExtendedImageInfo meshAsImg;
        bool meshDecodedAsImg = ImageFormatLoader::DecodeImageFile(models3dDir + L"\\ue5_nanite_pillar.uasset", 0, meshAsImg);
        tc.Record(!meshDecodedAsImg && !meshAsImg.valid, L"Stage15.Images.UnrealTexture2D_RejectStaticMeshUasset");
        AssertImgRejected(tc, L"Stage15.Images.UnrealTexture2D_RejectCorruptUasset", std::vector<uint8_t>(80, 0), L".uasset");
    }

    // ========================================================================
    // 3. AVIF & HEIF / HEIC / AVCI Still Images (.avif, .heic, .heif, .avci)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".avif") && IsImageExtension(L".heic") &&
                  IsImageExtension(L".heif") && IsImageExtension(L".avci") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".avif") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".heic") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".heif") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".avci"),
                  L"Stage15.Images.AvifHeif_ExtensionsRegistered");

        ExtendedImageInfo avifInfo, heicInfo, heifInfo, avciInfo;
        bool okAvif = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\hdr_sunset_alpha.avif", 0, avifInfo);
        bool okHeic = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\iphone_rotated.heic", 0, heicInfo);
        bool okHeif = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\tiled_panorama.heif", 0, heifInfo);
        bool okAvci = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\broadcast_still.avci", 0, avciInfo);

        bool avifPxOk = okAvif && avifInfo.valid && avifInfo.width == 8 && avifInfo.height == 8;
        if (avifPxOk) {
            const uint8_t* pL = &avifInfo.bgraPixels[(2 * 8 + 1) * 4];
            const uint8_t* pR = &avifInfo.bgraPixels[(2 * 8 + 6) * 4];
            avifPxOk = (pL[2] > 235 && pL[1] > 105 && pL[0] < 35 && pL[3] == 255) &&
                       (pR[3] >= 126 && pR[3] <= 130 && pR[0] > 95 && pR[1] > 85 && pR[2] < 25);
        }
        tc.Record(avifPxOk, L"Stage15.Images.AvifHeif_SampleAlphaAVIF");

        bool heicPxOk = okHeic && heicInfo.valid && heicInfo.width == 4 && heicInfo.height == 8;
        if (heicPxOk) {
            const uint8_t* p0 = &heicInfo.bgraPixels[0];
            heicPxOk = (p0[2] > 225 && p0[1] < 40 && p0[0] < 45 && p0[3] == 255);
        }
        tc.Record(heicPxOk && HashBgraBytes(heicInfo.bgraPixels) != HashBgraBytes(avifInfo.bgraPixels),
                  L"Stage15.Images.AvifHeif_SampleRotatedHvc1HEIC");

        bool heifPxOk = okHeif && heifInfo.valid && heifInfo.width == 8 && heifInfo.height == 4;
        if (heifPxOk) {
            const uint8_t* pG = &heifInfo.bgraPixels[(1 * 8 + 1) * 4];
            const uint8_t* pP = &heifInfo.bgraPixels[(1 * 8 + 6) * 4];
            heifPxOk = (pG[1] > 200 && pG[2] < 40 && pG[0] < 60) &&
                       (pP[2] > 165 && pP[0] > 220 && pP[1] < 60);
        }
        tc.Record(heifPxOk, L"Stage15.Images.AvifHeif_SampleMultiTileGridHEIF");

        bool avciPxOk = okAvci && avciInfo.valid && avciInfo.width == 8 && avciInfo.height == 8;
        if (avciPxOk) {
            const uint8_t* pGold = &avciInfo.bgraPixels[(3 * 8 + 3) * 4];
            avciPxOk = (pGold[2] > 235 && pGold[1] > 185 && pGold[0] < 50 && pGold[3] == 255);
        }
        tc.Record(avciPxOk && HashBgraBytes(avciInfo.bgraPixels) != HashBgraBytes(avifInfo.bgraPixels),
                  L"Stage15.Images.AvifHeif_SampleH264AVCI");

        std::vector<uint8_t> noMeta = { 0,0,0,16, 'f','t','y','p', 'a','v','i','f', 0,0,0,0 };
        AssertImgRejected(tc, L"Stage15.Images.AvifHeif_RejectMissingMetaBox", noMeta, L".avif");
        AssertImgRejected(tc, L"Stage15.Images.AvifHeif_RejectGarbageHEIC", std::vector<uint8_t>(64, 0xAA), L".heic");
    }

    // ========================================================================
    // 4. DirectDraw Surface (.dds) BC4 / BC5 / BC6H / BC7 & DX10 Float HDR
    // ========================================================================
    {
        ExtendedImageInfo bc7Info, bc6hInfo, bc5Info, bc4Info, f16Info, r11Info;
        bool okBc7  = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\dds_bc7_ui_badge.dds", 0, bc7Info);
        bool okBc6h = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\dds_bc6h_hdr_probe.dds", 0, bc6hInfo);
        bool okBc5  = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\dds_bc5_normal_map.dds", 0, bc5Info);
        bool okBc4  = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\dds_bc4_roughness.dds", 0, bc4Info);
        bool okF16  = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\dds_rgba16f_hdr_sky.dds", 0, f16Info);
        bool okR11  = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\dds_r11g11b10f_bloom.dds", 0, r11Info);

        bool bc7PxOk = okBc7 && bc7Info.valid && bc7Info.width == 8 && bc7Info.height == 8;
        if (bc7PxOk) {
            const uint8_t* pL = &bc7Info.bgraPixels[(1 * 8 + 1) * 4];
            const uint8_t* pR = &bc7Info.bgraPixels[(1 * 8 + 6) * 4];
            bc7PxOk = (pL[1] > 220 && pL[2] < 30 && pL[3] == 255) &&
                      (pR[3] >= 126 && pR[3] <= 130 && pR[2] > 110 && pR[1] < 55);
        }
        tc.Record(bc7PxOk, L"Stage15.Images.DdsUpgrade_SampleBC7Mode6Alpha");

        bool bc6hPxOk = okBc6h && bc6hInfo.valid && bc6hInfo.width == 8 && bc6hInfo.height == 4 && bc6hInfo.bitsPerChannel == 16;
        if (bc6hPxOk) {
            const uint8_t* pSun = &bc6hInfo.bgraPixels[(1 * 8 + 1) * 4];
            const uint8_t* pSky = &bc6hInfo.bgraPixels[(1 * 8 + 6) * 4];
            bc6hPxOk = (pSun[2] > 210 && pSun[1] > 150 && pSun[0] == 0) &&
                       (pSky[0] > 205 && pSky[1] > 140 && pSky[2] == 0);
        }
        tc.Record(bc6hPxOk && HashBgraBytes(bc6hInfo.bgraPixels) != HashBgraBytes(bc7Info.bgraPixels),
                  L"Stage15.Images.DdsUpgrade_SampleBC6HUf16Hdr");

        bool bc5PxOk = okBc5 && bc5Info.valid && bc5Info.width == 8 && bc5Info.height == 4;
        if (bc5PxOk) {
            const uint8_t* pFlat = &bc5Info.bgraPixels[(1 * 8 + 1) * 4];
            const uint8_t* pTilt = &bc5Info.bgraPixels[(1 * 8 + 6) * 4];
            bc5PxOk = (pFlat[0] >= 250 && pFlat[2] >= 126 && pFlat[2] <= 130) &&
                      (pTilt[2] >= 225 && pTilt[0] >= 195 && pTilt[0] <= 215);
        }
        tc.Record(bc5PxOk, L"Stage15.Images.DdsUpgrade_SampleBC5NormalZReconstruct");

        bool bc4PxOk = okBc4 && bc4Info.valid && bc4Info.bgraPixels[0] < 30 && bc4Info.bgraPixels[(0 * 8 + 6) * 4] > 230;
        bool f16PxOk = okF16 && f16Info.valid && f16Info.bgraPixels[2] > 220 && f16Info.bgraPixels[0] > 180 && f16Info.bgraPixels[1] < 140;
        bool r11PxOk = okR11 && r11Info.valid && r11Info.bgraPixels[1] > 210 && r11Info.bgraPixels[2] < 120;
        tc.Record(bc4PxOk && f16PxOk && r11PxOk &&
                  HashBgraBytes(f16Info.bgraPixels) != HashBgraBytes(r11Info.bgraPixels),
                  L"Stage15.Images.DdsUpgrade_SampleBC4AndDx10FloatHdr");

        std::vector<uint8_t> badDds(148, 0);
        std::memcpy(badDds.data(), "DDS ", 4);
        badDds[4] = 124; badDds[12] = 8; badDds[16] = 8; badDds[76] = 32; badDds[80] = 4;
        std::memcpy(badDds.data() + 84, "DX10", 4);
        badDds[128] = 98;
        AssertImgRejected(tc, L"Stage15.Images.DdsUpgrade_RejectTruncatedDx10Blocks", badDds, L".dds");
    }

    // ========================================================================
    // 5. OpenEXR (.exr) PIZ Wavelet, PXR24, B44 / B44A, DWAA / DWAB & Multi-Part v2
    // ========================================================================
    {
        ExtendedImageInfo pizInfo, pxrInfo, b44Info, dwaInfo, mpInfo;
        bool okPiz = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\exr_piz_wavelet_hdr.exr", 0, pizInfo);
        bool okPxr = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\exr_pxr24_float_hdr.exr", 0, pxrInfo);
        bool okB44 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\exr_b44a_envmap.exr", 0, b44Info);
        bool okDwa = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\exr_dwaa_studio.exr", 0, dwaInfo);
        bool okMp  = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\exr_multipart_aov.exr", 0, mpInfo);

        bool pizPxOk = okPiz && pizInfo.valid && pizInfo.width == 8 && pizInfo.height == 4;
        if (pizPxOk) {
            const uint8_t* pL = &pizInfo.bgraPixels[(1 * 8 + 1) * 4];
            const uint8_t* pR = &pizInfo.bgraPixels[(1 * 8 + 6) * 4];
            pizPxOk = (pL[2] > 210 && pL[1] < 95 && pL[0] < 120) &&
                      (pR[1] > 205 && pR[0] > 160 && pR[2] < 95);
        }
        tc.Record(pizPxOk, L"Stage15.Images.ExrUpgrade_SamplePizWaveletHuffman");

        bool pxrPxOk = okPxr && pxrInfo.valid && pxrInfo.width == 8 && pxrInfo.height == 4 && pxrInfo.bitsPerChannel == 32;
        if (pxrPxOk) {
            const uint8_t* p = &pxrInfo.bgraPixels[0];
            pxrPxOk = (p[2] > 215 && p[1] > 170 && p[0] < 80 && p[3] == 255);
        }
        tc.Record(pxrPxOk && HashBgraBytes(pxrInfo.bgraPixels) != HashBgraBytes(pizInfo.bgraPixels),
                  L"Stage15.Images.ExrUpgrade_SamplePxr24FloatHdr");

        bool b44PxOk = okB44 && b44Info.valid && b44Info.width == 8 && b44Info.height == 4;
        if (b44PxOk) {
            const uint8_t* pL = &b44Info.bgraPixels[(2 * 8 + 1) * 4];
            const uint8_t* pR = &b44Info.bgraPixels[(2 * 8 + 6) * 4];
            b44PxOk = (pL[0] > 210 && pL[2] < 95) && (pR[2] > 205 && pR[0] < 95);
        }
        tc.Record(b44PxOk, L"Stage15.Images.ExrUpgrade_SampleB44AndB44AFlat");

        bool dwaPxOk = okDwa && dwaInfo.valid && dwaInfo.width == 8 && dwaInfo.height == 8;
        if (dwaPxOk) {
            const uint8_t* p = &dwaInfo.bgraPixels[(3 * 8 + 3) * 4];
            dwaPxOk = (p[3] >= 125 && p[3] <= 131 && p[1] > 95 && p[0] > 85 && p[2] < 55);
        }
        tc.Record(dwaPxOk, L"Stage15.Images.ExrUpgrade_SampleDwaaDct709Alpha");

        bool mpPxOk = okMp && mpInfo.valid && mpInfo.width == 4 && mpInfo.height == 4 &&
                      mpInfo.layerNames.size() == 4;
        if (mpPxOk) {
            const uint8_t* p = &mpInfo.bgraPixels[0];
            mpPxOk = (p[0] > 210 && p[2] > 195 && p[1] < 95);
        }
        tc.Record(mpPxOk, L"Stage15.Images.ExrUpgrade_SampleMultiPartV2Aov");

        std::vector<uint8_t> badExr = { 0x76, 0x2F, 0x31, 0x01, 2, 0, 0, 0, 0 };
        AssertImgRejected(tc, L"Stage15.Images.ExrUpgrade_RejectIncompleteHeader", badExr, L".exr");
    }

    // ========================================================================
    // 6. Photoshop & PSB (.psd, .psb) Layer Compositor, ZIP Prediction, 32-bit HDR & CIE Lab
    // ========================================================================
    {
        ExtendedImageInfo lyPsd, psb16, hdr32, labPsd;
        bool okLy  = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\psd_layers_nocompat.psd", 0, lyPsd);
        bool okPsb = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\psb_zip_pred16_layers.psb", 0, psb16);
        bool okH32 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\psd_32bit_hdr_zippred.psd", 0, hdr32);
        bool okLab = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\psd_cielab_print.psd", 0, labPsd);

        bool lyOk = okLy && lyPsd.valid && lyPsd.width == 8 && lyPsd.height == 8 && lyPsd.layerNames.size() == 3 &&
                    lyPsd.layerNames[0] == L"BackdropSky" && lyPsd.layerNames[1] == L"MultiplySun";
        if (lyOk) {
            const uint8_t* pL = &lyPsd.bgraPixels[(2 * 8 + 1) * 4];
            const uint8_t* pR = &lyPsd.bgraPixels[(2 * 8 + 6) * 4];
            lyOk = (pL[0] > 225 && pL[1] > 185 && pL[2] < 55) &&
                   (pR[1] > 185 && pR[0] < 65 && pR[2] < 55);
        }
        tc.Record(lyOk, L"Stage15.Images.PsdUpgrade_SampleLayerCompositorMultiplyAndHidden");

        bool psbOk = okPsb && psb16.valid && psb16.width == 8 && psb16.height == 4 && psb16.bitsPerChannel == 16;
        if (psbOk) {
            const uint8_t* pL = &psb16.bgraPixels[(1 * 8 + 1) * 4];
            const uint8_t* pR = &psb16.bgraPixels[(1 * 8 + 6) * 4];
            psbOk = (pL[0] > 220 && pL[2] < 35) && (pR[2] > 220 && pR[1] > 125 && pR[0] < 35);
        }
        tc.Record(psbOk && HashBgraBytes(psb16.bgraPixels) != HashBgraBytes(lyPsd.bgraPixels),
                  L"Stage15.Images.PsdUpgrade_SamplePsb16ZipPredictedLayerOnly");

        bool h32Ok = okH32 && hdr32.valid && hdr32.width == 8 && hdr32.height == 4 && hdr32.bitsPerChannel == 32;
        if (h32Ok) {
            const uint8_t* p = &hdr32.bgraPixels[0];
            h32Ok = (p[2] > 215 && p[1] > 155 && p[0] < 80);
        }
        tc.Record(h32Ok, L"Stage15.Images.PsdUpgrade_Sample32BitFloatZipPredictedHdr");

        bool labOk = okLab && labPsd.valid && labPsd.width == 8 && labPsd.height == 4;
        if (labOk) {
            const uint8_t* p = &labPsd.bgraPixels[0];
            labOk = (p[2] > 210 && p[1] < 110 && p[0] < 95);
        }
        tc.Record(labOk, L"Stage15.Images.PsdUpgrade_SampleCieLabColorMode");

        std::vector<uint8_t> badPsd(30, 0);
        std::memcpy(badPsd.data(), "8BPS\x00\x01", 6);
        badPsd[13] = 3; badPsd[17] = 8; badPsd[21] = 8; badPsd[23] = 32; badPsd[25] = 3;
        AssertImgRejected(tc, L"Stage15.Images.PsdUpgrade_RejectTruncated32BitPsd", badPsd, L".psd");
    }

    // ========================================================================
    // 7. JPEG 2000 Pure-C++ Codestream & JP2 Box Decoder (.jp2, .j2k, .jpf, .jpx, .j2c)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".jp2") && IsImageExtension(L".j2k") &&
                  IsImageExtension(L".jpf") && IsImageExtension(L".jpx") && IsImageExtension(L".j2c") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".jp2") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".j2k") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".jpf") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".jpx") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".j2c"),
                  L"Stage15.Images.Jpeg2000_ExtensionsRegistered");

        ExtendedImageInfo jp2Info, j2kInfo, jpfInfo, jpxInfo, j2cInfo;
        bool okJp2 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\jp2_lossless_rct_alpha.jp2", 0, jp2Info);
        bool okJ2k = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\j2k_lossy_97_ict.j2k", 0, j2kInfo);
        bool okJpf = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\jpf_paletted_map.jpf", 0, jpfInfo);
        bool okJpx = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\jpx_16bit_medical.jpx", 0, jpxInfo);
        bool okJ2c = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\j2c_multitile_codestream.j2c", 0, j2cInfo);

        bool jp2Ok = okJp2 && jp2Info.valid && jp2Info.width == 8 && jp2Info.height == 8;
        if (jp2Ok) {
            const uint8_t* pL = &jp2Info.bgraPixels[(2 * 8 + 1) * 4];
            const uint8_t* pR = &jp2Info.bgraPixels[(2 * 8 + 6) * 4];
            jp2Ok = (pL[2] == 30 && pL[1] == 220 && pL[0] == 80 && pL[3] == 255) &&
                    (pR[3] == 128 && pR[2] >= 118 && pR[2] <= 122 && pR[1] >= 93 && pR[1] <= 97 && pR[0] < 15);
        }
        tc.Record(jp2Ok, L"Stage15.Images.Jpeg2000_SampleLossless53RctAlphaJP2");

        bool j2kOk = okJ2k && j2kInfo.valid && j2kInfo.width == 8 && j2kInfo.height == 8;
        if (j2kOk) {
            const uint8_t* pT = &j2kInfo.bgraPixels[(1 * 8 + 4) * 4];
            const uint8_t* pB = &j2kInfo.bgraPixels[(6 * 8 + 4) * 4];
            j2kOk = (pT[0] > 230 && pT[1] > 115 && pT[2] < 35) &&
                    (pB[2] > 230 && pB[1] > 75 && pB[0] < 65);
        }
        tc.Record(j2kOk && HashBgraBytes(j2kInfo.bgraPixels) != HashBgraBytes(jp2Info.bgraPixels),
                  L"Stage15.Images.Jpeg2000_SampleLossy97IctJ2K");

        bool jpfOk = okJpf && jpfInfo.valid &&
                     jpfInfo.bgraPixels[(1 * 8 + 1) * 4 + 1] == 235 &&
                     jpfInfo.bgraPixels[(1 * 8 + 6) * 4 + 2] == 235 &&
                     jpfInfo.bgraPixels[(1 * 8 + 6) * 4 + 0] == 220;
        bool jpxOk = okJpx && jpxInfo.valid && jpxInfo.bitsPerChannel == 16 &&
                     jpxInfo.bgraPixels[(1 * 8 + 1) * 4] < 25 &&
                     jpxInfo.bgraPixels[(1 * 8 + 6) * 4] > 225;
        bool j2cOk = okJ2c && j2cInfo.valid &&
                     j2cInfo.bgraPixels[(1 * 8 + 1) * 4 + 2] == 235 &&
                     j2cInfo.bgraPixels[(1 * 8 + 6) * 4 + 0] == 235;
        tc.Record(jpfOk && jpxOk && j2cOk, L"Stage15.Images.Jpeg2000_SamplePaletted16BitAndMultiTile");

        std::vector<uint8_t> noJp2c = { 0,0,0,12, 'j','P',' ',' ', 0x0D,0x0A,0x87,0x0A, 0,0,0,8, 'f','r','e','e' };
        std::vector<uint8_t> badJ2k = { 0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x29, 0x00, 0x00 };
        AssertImgRejected(tc, L"Stage15.Images.Jpeg2000_RejectMissingCodestreamBox", noJp2c, L".jp2");
        AssertImgRejected(tc, L"Stage15.Images.Jpeg2000_RejectTruncatedCodestream", badJ2k, L".j2k");
    }

    // ========================================================================
    // 8. JPEG XL (.jxl) Pure-C++ rANS Entropy, VarDCT Lossy & XYB Color
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".jxl") && ImageFormatLoader::IsExtendedImageExtension(L".jxl"),
                  L"Stage15.Images.Jxl_ExtensionRegistered");

        ExtendedImageInfo vdctInfo, modAnsInfo, boxedInfo, modXybInfo;
        bool okVdct   = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\jxl_vardct_xyb_photo.jxl", 0, vdctInfo);
        bool okModAns = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\jxl_modular_rans_alpha.jxl", 0, modAnsInfo);
        bool okBoxed  = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\jxl_vardct_boxed_alpha.jxl", 0, boxedInfo);
        bool okModXyb = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\jxl_modular_xyb_16bit.jxl", 0, modXybInfo);

        bool vdctOk = okVdct && vdctInfo.valid && vdctInfo.width == 16 && vdctInfo.height == 16;
        if (vdctOk) {
            const uint8_t* pTL = &vdctInfo.bgraPixels[(3 * 16 + 3) * 4];
            const uint8_t* pTR = &vdctInfo.bgraPixels[(3 * 16 + 12) * 4];
            const uint8_t* pBL = &vdctInfo.bgraPixels[(12 * 16 + 3) * 4];
            const uint8_t* pBR = &vdctInfo.bgraPixels[(12 * 16 + 12) * 4];
            vdctOk = (pTL[0] > 210 && pTL[1] > 110 && pTL[2] < 65) &&
                     (pTR[1] > 180 && pTR[2] < 70  && pTR[0] < 110) &&
                     (pBL[2] > 200 && pBL[1] < 95  && pBL[0] < 80) &&
                     (pBR[2] > 210 && pBR[1] > 170 && pBR[0] < 75);
        }
        tc.Record(vdctOk, L"Stage15.Images.Jxl_SampleVarDctXybPhoto");

        bool modAnsOk = okModAns && modAnsInfo.valid && modAnsInfo.width == 8 && modAnsInfo.height == 8;
        if (modAnsOk) {
            const uint8_t* pL = &modAnsInfo.bgraPixels[(2 * 8 + 1) * 4];
            const uint8_t* pR = &modAnsInfo.bgraPixels[(2 * 8 + 6) * 4];
            modAnsOk = (pL[2] == 160 && pL[1] == 40 && pL[0] == 235 && pL[3] == 255) &&
                       (pR[3] == 128 && pR[2] <= 15 && pR[1] >= 108 && pR[1] <= 112 && pR[0] >= 118 && pR[0] <= 122);
        }
        tc.Record(modAnsOk && HashBgraBytes(modAnsInfo.bgraPixels) != HashBgraBytes(vdctInfo.bgraPixels),
                  L"Stage15.Images.Jxl_SampleModularRansAlpha");

        bool boxedOk = okBoxed && boxedInfo.valid && boxedInfo.width == 16 && boxedInfo.height == 8;
        if (boxedOk) {
            const uint8_t* pL = &boxedInfo.bgraPixels[(4 * 16 + 3) * 4];
            const uint8_t* pR = &boxedInfo.bgraPixels[(4 * 16 + 12) * 4];
            boxedOk = (pL[3] == 128 && pL[1] > 95 && pL[2] < 45) &&
                      (pR[3] == 255 && pR[2] > 205 && pR[1] < 65);
        }
        bool modXybOk = okModXyb && modXybInfo.valid && modXybInfo.width == 8 && modXybInfo.height == 8;
        if (modXybOk) {
            const uint8_t* pT = &modXybInfo.bgraPixels[(1 * 8 + 4) * 4];
            const uint8_t* pB = &modXybInfo.bgraPixels[(6 * 8 + 4) * 4];
            modXybOk = (pT[2] > 210 && pT[1] < 110) && (pB[1] > 200 && pB[2] < 85);
        }
        tc.Record(boxedOk && modXybOk, L"Stage15.Images.Jxl_SampleBoxedOrientedAndModularXyb");

        std::vector<uint8_t> badJxl = { 0xFF, 0x0A, 0x39, 0x09 };
        std::vector<uint8_t> badJxlBox = { 0,0,0,12, 'J','X','L',' ', 0x0D,0x0A,0x87,0x0A, 0,0,0,8, 'j','x','l','c' };
        AssertImgRejected(tc, L"Stage15.Images.Jxl_RejectTruncatedCodestream", badJxl, L".jxl");
        AssertImgRejected(tc, L"Stage15.Images.Jxl_RejectMissingFtypContainer", badJxlBox, L".jxl");
    }

    // ========================================================================
    // 9. Pure-C++ TIFF / BigTIFF / GeoTIFF DEM (.tif, .btf, .tf8) & WebP (.webp)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".tif") && IsImageExtension(L".tiff") &&
                  IsImageExtension(L".btf") && IsImageExtension(L".tf8") && IsImageExtension(L".webp") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".tif") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".btf") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".tf8") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".webp"),
                  L"Stage15.Images.TiffWebp_ExtensionsRegistered");

        ExtendedImageInfo tifInfo, btfInfo, tf8Info, demInfo, wpL1Info, wpPalInfo, wpLossyInfo;
        bool okTif     = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\tiff_lzw_pred2_16bit.tif", 0, tifInfo);
        bool okBtf     = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\bigtiff_deflate_tiles_hdr.btf", 0, btfInfo);
        bool okTf8     = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\bigtiff_packbits_cmyk.tf8", 0, tf8Info);
        bool okDem     = ImageFormatLoader::DecodeImageFile(gisDir + L"\\alpine_dem.tif", 0, demInfo);
        bool okWpL1    = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\webp_vp8l_transforms_alpha.webp", 0, wpL1Info);
        bool okWpPal   = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\webp_vp8l_palette_bundled.webp", 0, wpPalInfo);
        bool okWpLossy = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\webp_vp8_lossy_alph.webp", 0, wpLossyInfo);

        bool tifOk = okTif && tifInfo.valid && tifInfo.width == 8 && tifInfo.height == 8 && tifInfo.bitsPerChannel == 16 &&
                     okDem && demInfo.valid && demInfo.width == 4 && demInfo.height == 4 && demInfo.bitsPerChannel == 32 &&
                     demInfo.bgraPixels[0] == 232u && demInfo.bgraPixels[(1 * 4 + 1) * 4] == 246u;
        if (tifOk) {
            const uint8_t* pL = &tifInfo.bgraPixels[(2 * 8 + 1) * 4];
            const uint8_t* pR = &tifInfo.bgraPixels[(2 * 8 + 6) * 4];
            tifOk = (pL[2] == 30 && pL[1] == 220 && pL[0] == 80 && pL[3] == 255) &&
                    (pR[3] == 128 && pR[2] >= 118 && pR[2] <= 122 && pR[1] >= 93 && pR[1] <= 97 && pR[0] <= 12);
        }
        tc.Record(tifOk, L"Stage15.Images.TiffWebp_SampleTiffLzwPred2Rgba16AndAlpineDem");

        bool btfOk = okBtf && btfInfo.valid && btfInfo.width == 8 && btfInfo.height == 8 && btfInfo.bitsPerChannel == 32;
        if (btfOk) {
            const uint8_t* pT = &btfInfo.bgraPixels[(1 * 8 + 2) * 4];
            const uint8_t* pB = &btfInfo.bgraPixels[(6 * 8 + 5) * 4];
            btfOk = (pT[0] > 180 && pT[2] < 35) && (pB[2] > 190 && pB[0] < 30);
        }
        bool tf8Ok = okTf8 && tf8Info.valid && tf8Info.width == 8 && tf8Info.height == 8 && tf8Info.layerNames.size() == 2;
        if (tf8Ok) {
            const uint8_t* pL = &tf8Info.bgraPixels[(2 * 8 + 1) * 4];
            const uint8_t* pR = &tf8Info.bgraPixels[(2 * 8 + 6) * 4];
            tf8Ok = (pL[1] > 220 && pL[0] > 225 && pL[2] < 40) &&
                    (pR[2] > 225 && pR[0] > 220 && pR[1] < 35);
        }
        tc.Record(btfOk && tf8Ok && HashBgraBytes(btfInfo.bgraPixels) != HashBgraBytes(tf8Info.bgraPixels),
                  L"Stage15.Images.TiffWebp_SampleBigTiffHdrTilesAndCmykPages");

        bool wpL1Ok = okWpL1 && wpL1Info.valid && wpL1Info.width == 8 && wpL1Info.height == 8;
        if (wpL1Ok) {
            const uint8_t* pTL = &wpL1Info.bgraPixels[(1 * 8 + 1) * 4];
            const uint8_t* pTR = &wpL1Info.bgraPixels[(1 * 8 + 6) * 4];
            const uint8_t* pBL = &wpL1Info.bgraPixels[(6 * 8 + 2) * 4];
            wpL1Ok = (pTL[2] == 235 && pTL[1] == 30  && pTL[0] == 45  && pTL[3] == 255) &&
                     (pTR[2] == 40  && pTR[1] == 225 && pTR[0] == 60  && pTR[3] == 255) &&
                     (pBL[3] == 128 && pBL[0] >= 118 && pBL[0] <= 122 && pBL[2] <= 22);
        }
        bool wpPalOk = okWpPal && wpPalInfo.valid && wpPalInfo.width == 8 && wpPalInfo.height == 8 &&
                       wpPalInfo.bgraPixels[0 * 4 + 2] == 245 && wpPalInfo.bgraPixels[1 * 4 + 0] == 205;
        bool wpLossyOk = okWpLossy && wpLossyInfo.valid && wpLossyInfo.width == 16 && wpLossyInfo.height == 16 &&
                         wpLossyInfo.bgraPixels[(4 * 16 + 2) * 4 + 3] == 255 &&
                         wpLossyInfo.bgraPixels[(4 * 16 + 2) * 4 + 2] > 175 &&
                         wpLossyInfo.bgraPixels[(4 * 16 + 12) * 4 + 3] == 128;
        tc.Record(wpL1Ok && wpPalOk && wpLossyOk && HashBgraBytes(wpL1Info.bgraPixels) != HashBgraBytes(wpPalInfo.bgraPixels),
                  L"Stage15.Images.TiffWebp_SampleWebpVp8lPaletteAndVp8Alph");

        std::vector<uint8_t> badBtf = { 'I','I', 43,0, 8,0, 0,0, 0xFF,0xFF,0,0,0,0,0,0 };
        std::vector<uint8_t> badWebp = { 'R','I','F','F', 16,0,0,0, 'W','E','B','P', 'V','P','8','L', 4,0,0,0, 0x2F,0,0,0 };
        AssertImgRejected(tc, L"Stage15.Images.TiffWebp_RejectCorruptBigTiff", badBtf, L".btf");
        AssertImgRejected(tc, L"Stage15.Images.TiffWebp_RejectTruncatedWebp", badWebp, L".webp");
    }

    // ========================================================================
    // 10. Camera RAW Sensor Unpackers & Color Matrices (.cr3, .arw, .nef, .rw2, .dng)
    // ========================================================================
    {
        ExtendedImageInfo cr3Info, arwInfo, nefInfo, rw2Info, dngInfo;
        bool okCr3 = ImageFormatLoader::DecodeImageFile(rawDir + L"\\canon_craw_cmp1_sensor.cr3", 0, cr3Info);
        bool okArw = ImageFormatLoader::DecodeImageFile(rawDir + L"\\sony_arw2_delta_cfa.arw", 0, arwInfo);
        bool okNef = ImageFormatLoader::DecodeImageFile(rawDir + L"\\nikon_lossless_huffman.nef", 0, nefInfo);
        bool okRw2 = ImageFormatLoader::DecodeImageFile(rawDir + L"\\panasonic_12bit_blocks.rw2", 0, rw2Info);
        bool okDng = ImageFormatLoader::DecodeImageFile(rawDir + L"\\adobe_colormatrix_quad.dng", 0, dngInfo);

        bool cr3Ok = okCr3 && cr3Info.valid && cr3Info.width == 16 && cr3Info.height == 16 &&
                     cr3Info.bgraPixels[(4 * 16 + 3) * 4 + 2] > 210 &&
                     cr3Info.bgraPixels[(4 * 16 + 3) * 4 + 1] < 80 &&
                     cr3Info.bgraPixels[(4 * 16 + 12) * 4 + 1] > 210 &&
                     cr3Info.bgraPixels[(4 * 16 + 12) * 4 + 2] < 85;
        tc.Record(cr3Ok, L"Stage15.Images.CameraRaw_SampleCanonCr3Cmp1Sensor");

        bool arwOk = okArw && arwInfo.valid && arwInfo.width == 32 && arwInfo.height == 16 &&
                     arwInfo.bgraPixels[(4 * 32 + 4) * 4 + 0] > 215 &&
                     arwInfo.bgraPixels[(4 * 32 + 4) * 4 + 2] < 85 &&
                     arwInfo.bgraPixels[(4 * 32 + 24) * 4 + 2] > 215 &&
                     arwInfo.bgraPixels[(4 * 32 + 24) * 4 + 0] < 85;
        bool nefOk = okNef && nefInfo.valid && nefInfo.width == 16 && nefInfo.height == 16 &&
                     nefInfo.bgraPixels[(3 * 16 + 8) * 4 + 2] > 210 &&
                     nefInfo.bgraPixels[(3 * 16 + 8) * 4 + 0] > 205 &&
                     nefInfo.bgraPixels[(3 * 16 + 8) * 4 + 1] < 85 &&
                     nefInfo.bgraPixels[(12 * 16 + 8) * 4 + 1] > 210 &&
                     nefInfo.bgraPixels[(12 * 16 + 8) * 4 + 2] < 95;
        tc.Record(arwOk && nefOk && HashBgraBytes(arwInfo.bgraPixels) != HashBgraBytes(nefInfo.bgraPixels),
                  L"Stage15.Images.CameraRaw_SampleSonyArw2AndNikonHuffman");

        bool rw2Ok = okRw2 && rw2Info.valid && rw2Info.width == 28 && rw2Info.height == 16 &&
                     rw2Info.bgraPixels[(4 * 28 + 4) * 4 + 0] > 210 &&
                     rw2Info.bgraPixels[(4 * 28 + 4) * 4 + 1] > 200 &&
                     rw2Info.bgraPixels[(4 * 28 + 4) * 4 + 2] < 65 &&
                     rw2Info.bgraPixels[(4 * 28 + 21) * 4 + 2] > 215 &&
                     rw2Info.bgraPixels[(4 * 28 + 21) * 4 + 0] < 120;
        bool dngOk = okDng && dngInfo.valid && dngInfo.width == 16 && dngInfo.height == 16 &&
                     dngInfo.bgraPixels[(4 * 16 + 3) * 4 + 2] > 220 &&
                     dngInfo.bgraPixels[(4 * 16 + 3) * 4 + 0] < 75 &&
                     dngInfo.bgraPixels[(4 * 16 + 12) * 4 + 0] > 220 &&
                     dngInfo.bgraPixels[(4 * 16 + 12) * 4 + 2] < 75;
        tc.Record(rw2Ok && dngOk && HashBgraBytes(rw2Info.bgraPixels) != HashBgraBytes(dngInfo.bgraPixels),
                  L"Stage15.Images.CameraRaw_SamplePanasonicRw2AndDngColorMatrix");

        std::vector<uint8_t> badCr3 = { 0,0,0,24, 'f','t','y','p','c','r','x',' ', 0,0,0,1, 'c','r','x',' ','i','s','o','m' };
        std::vector<uint8_t> badRw2 = { 'I','I', 0x55,0x00, 8,0,0,0, 1,0, 0x18,0x01, 4,0, 1,0,0,0, 0xFF,0,0,0, 0,0,0,0 };
        AssertImgRejected(tc, L"Stage15.Images.CameraRaw_RejectEmptyCr3", badCr3, L".cr3");
        AssertImgRejected(tc, L"Stage15.Images.CameraRaw_RejectTruncatedRw2", badRw2, L".rw2");
    }

    // ========================================================================
    // 11. KTX 2.0 Zstd/BasisLZ (.ktx2), PKM v2 ETC2/EAC (.pkm), & Amiga ILBM HAM8/24-bit (.iff, .ilbm)
    // ========================================================================
    {
        ExtendedImageInfo ktxZstd, ktxBasis, pkmModes, pkmRgba, pkmRg11, ilbmHam8, ilbm24;
        bool okKZ = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\ktx2_uastc_zstd.ktx2", 0, ktxZstd);
        bool okKB = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\ktx2_basislz_etc1s.ktx2", 0, ktxBasis);
        bool okPM = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\pkm20_etc2_modes.pkm", 0, pkmModes);
        bool okPA = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\pkm20_etc2_rgba_eac.pkm", 0, pkmRgba);
        bool okPR = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\pkm20_eac_rg11.pkm", 0, pkmRg11);
        bool okH8 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\amiga_ham8_camg.ilbm", 0, ilbmHam8);
        bool ok24 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\amiga_24bit_mask.iff", 0, ilbm24);

        bool kzOk = okKZ && ktxZstd.valid && ktxZstd.width == 8 && ktxZstd.height == 8 &&
                    ktxZstd.bgraPixels[(1 * 8 + 1) * 4 + 2] == 230 &&
                    ktxZstd.bgraPixels[(1 * 8 + 5) * 4 + 1] == 215 &&
                    ktxZstd.bgraPixels[(5 * 8 + 1) * 4 + 0] == 240 &&
                    ktxZstd.bgraPixels[(5 * 8 + 5) * 4 + 3] == 128;
        bool kbOk = okKB && ktxBasis.valid && ktxBasis.width == 8 && ktxBasis.height == 8 && ktxBasis.channels == 4 &&
                    ktxBasis.bgraPixels[(1 * 8 + 1) * 4 + 2] > 235 &&
                    ktxBasis.bgraPixels[(1 * 8 + 5) * 4 + 1] > 235 &&
                    ktxBasis.bgraPixels[(5 * 8 + 1) * 4 + 0] > 235 &&
                    ktxBasis.bgraPixels[(5 * 8 + 5) * 4 + 3] < 60;
        tc.Record(kzOk && kbOk && HashBgraBytes(ktxZstd.bgraPixels) != HashBgraBytes(ktxBasis.bgraPixels),
                  L"Stage15.Images.Ktx2PkmIlbm_SampleKtx2ZstdAndBasisLz");

        bool pmOk = okPM && pkmModes.valid && pkmModes.width == 8 && pkmModes.height == 8 &&
                    pkmModes.bgraPixels[(1 * 8 + 1) * 4 + 2] == 255 &&
                    pkmModes.bgraPixels[(1 * 8 + 5) * 4 + 1] == 255 &&
                    pkmModes.bgraPixels[(5 * 8 + 1) * 4 + 0] > 180 &&
                    pkmModes.bgraPixels[(5 * 8 + 5) * 4 + 2] > 230 &&
                    pkmModes.bgraPixels[(5 * 8 + 5) * 4 + 1] > 210;
        bool paOk = okPA && pkmRgba.valid && pkmRgba.channels == 4 &&
                    pkmRgba.bgraPixels[(1 * 8 + 1) * 4 + 3] == 255 &&
                    pkmRgba.bgraPixels[(5 * 8 + 1) * 4 + 3] == 128 &&
                    pkmRgba.bgraPixels[(5 * 8 + 5) * 4 + 3] == 64;
        bool prOk = okPR && pkmRg11.valid && pkmRg11.channels == 2 && pkmRg11.bitsPerChannel == 11 &&
                    pkmRg11.bgraPixels[(1 * 8 + 1) * 4 + 2] > 225 &&
                    pkmRg11.bgraPixels[(1 * 8 + 1) * 4 + 1] < 45 &&
                    pkmRg11.bgraPixels[(1 * 8 + 5) * 4 + 1] > 225 &&
                    pkmRg11.bgraPixels[(1 * 8 + 5) * 4 + 2] < 50;
        tc.Record(pmOk && paOk && prOk && HashBgraBytes(pkmModes.bgraPixels) != HashBgraBytes(pkmRg11.bgraPixels),
                  L"Stage15.Images.Ktx2PkmIlbm_SamplePkm20Etc2ModesAndEac");

        bool h8Ok = okH8 && ilbmHam8.valid && ilbmHam8.width == 16 && ilbmHam8.height == 16 &&
                    ilbmHam8.bgraPixels[(0 * 16 + 0) * 4 + 2] == 20 &&
                    ilbmHam8.bgraPixels[(10 * 16 + 0) * 4 + 2] == 220 &&
                    ilbmHam8.bgraPixels[(12 * 16 + 13) * 4 + 2] > 195 &&
                    ilbmHam8.bgraPixels[(14 * 16 + 14) * 4 + 1] > 220;
        bool d24Ok = ok24 && ilbm24.valid && ilbm24.width == 16 && ilbm24.height == 16 && ilbm24.channels == 4 &&
                     ilbm24.bgraPixels[(2 * 16 + 10) * 4 + 2] == (10 * 16 + 7) &&
                     ilbm24.bgraPixels[(2 * 16 + 10) * 4 + 1] == (2 * 16 + 3) &&
                     ilbm24.bgraPixels[(2 * 16 + 10) * 4 + 3] == 255 &&
                     ilbm24.bgraPixels[(14 * 16 + 14) * 4 + 3] == 0;
        tc.Record(h8Ok && d24Ok && HashBgraBytes(ilbmHam8.bgraPixels) != HashBgraBytes(ilbm24.bgraPixels),
                  L"Stage15.Images.Ktx2PkmIlbm_SampleAmigaHam8And24BitMask");

        std::vector<uint8_t> badPkm = { 'P','K','M',' ','9','9', 0,99, 0,8, 0,8, 0,8, 0,8, 0,0,0,0,0,0,0,0 };
        std::vector<uint8_t> badIlbm = {
            'F','O','R','M', 0,0,0,44, 'I','L','B','M',
            'B','M','H','D', 0,0,0,20, 0,16, 0,16, 0,0, 0,0, 8,0,0,0, 0,0, 1,1, 0,16, 0,16,
            'B','O','D','Y', 0,0,0,4,  1,2,3,4
        };
        AssertImgRejected(tc, L"Stage15.Images.Ktx2PkmIlbm_RejectInvalidPkmVersion", badPkm, L".pkm");
        AssertImgRejected(tc, L"Stage15.Images.Ktx2PkmIlbm_RejectTruncatedIlbmBody", badIlbm, L".ilbm");
    }

    // ========================================================================
    // 12. Digital Art Layer/Tile Compositors (.ora, .kra, .procreate, .clip),
    //     Adobe Swatches (.ase, .aco), Aseprite v1.3, & OpenType/WOFF2/TTC Fonts
    // ========================================================================
    {
        ExtendedImageInfo oraInfo, kraInfo, proInfo, clipInfo, aseSwInfo, aseTileInfo, otfColrInfo, otfCff2Info, curInfo;
        bool okOra     = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\ora_multilayer_blend.ora", 0, oraInfo);
        bool okKra     = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\krita_lzf_tiles.kra", 0, kraInfo);
        bool okPro     = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\procreate_bplist_lzfse.procreate", 0, proInfo);
        bool okClip    = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\clip_studio_sqlite_preview.clip", 0, clipInfo);
        bool okAseSw   = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\adobe_swatch_exchange.ase", 0, aseSwInfo);
        bool okAseTile = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\aseprite_v13_tilemap.aseprite", 0, aseTileInfo);
        bool okColr    = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\opentype_cff_colr.otf", 0, otfColrInfo);
        bool okCff2    = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\opentype_cff2_cubic.otf", 0, otfCff2Info);
        bool okCur     = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\cursor_hotspot_xor_and.cur", 0, curInfo);

        bool oraOk = okOra && oraInfo.valid && oraInfo.width == 16 && oraInfo.height == 16 && oraInfo.layerNames.size() >= 2 &&
                     oraInfo.bgraPixels[(4 * 16 + 3) * 4 + 0] == 230 &&
                     oraInfo.bgraPixels[(4 * 16 + 3) * 4 + 2] == 20 &&
                     oraInfo.bgraPixels[(4 * 16 + 12) * 4 + 2] == 235 &&
                     oraInfo.bgraPixels[(4 * 16 + 12) * 4 + 1] == 35;
        bool kraOk = okKra && kraInfo.valid && kraInfo.width == 16 && kraInfo.height == 16 && kraInfo.layerNames.size() >= 2 &&
                     kraInfo.bgraPixels[(3 * 16 + 4) * 4 + 0] == 200 &&
                     kraInfo.bgraPixels[(3 * 16 + 4) * 4 + 1] == 210 &&
                     kraInfo.bgraPixels[(12 * 16 + 4) * 4 + 2] == 245 &&
                     kraInfo.bgraPixels[(12 * 16 + 4) * 4 + 1] == 195;
        tc.Record(oraOk && kraOk && HashBgraBytes(oraInfo.bgraPixels) != HashBgraBytes(kraInfo.bgraPixels),
                  L"Stage15.Images.ArtFonts_SampleOraStackAndKritaLzfTiles");

        // Procreate NSKeyedArchiver bplist00 + LZFSE (bvx2/bvxn) SilicaTile artwork
        bool proOk = okPro && proInfo.valid && proInfo.width == 16 && proInfo.height == 16 &&
                     proInfo.layerNames.size() == 2 &&
                     proInfo.layerNames[0] == L"Paint Layer" &&
                     proInfo.layerNames[1] == L"Hidden Draft" &&
                     proInfo.formatName.find(L"Procreate Silica Tile Artwork") != std::wstring::npos &&
                     proInfo.bgraPixels[(0 * 16 + 0) * 4 + 0] == 64 &&
                     proInfo.bgraPixels[(0 * 16 + 0) * 4 + 1] == 38 &&
                     proInfo.bgraPixels[(0 * 16 + 0) * 4 + 2] == 26 &&
                     proInfo.bgraPixels[(8 * 16 + 8) * 4 + 0] == 120 &&
                     proInfo.bgraPixels[(8 * 16 + 8) * 4 + 1] == 220 &&
                     proInfo.bgraPixels[(8 * 16 + 8) * 4 + 2] == 40;

        // Clip Studio Paint CSFCHUNK + SQLite3 CHNKSQLi CanvasPreview + Layer catalog
        bool clipOk = okClip && clipInfo.valid && clipInfo.width == 24 && clipInfo.height == 24 &&
                      clipInfo.layerNames.size() == 2 &&
                      clipInfo.layerNames[0] == L"Ink Linework" &&
                      clipInfo.layerNames[1] == L"Color Wash" &&
                      clipInfo.formatName == L"Clip Studio Paint Canvas (.clip)" &&
                      clipInfo.bgraPixels[(0 * 24 + 0) * 4 + 2] == 90 &&
                      clipInfo.bgraPixels[(0 * 24 + 0) * 4 + 1] == 182 &&
                      clipInfo.bgraPixels[(6 * 24 + 6) * 4 + 1] == 234 &&
                      clipInfo.bgraPixels[(6 * 24 + 6) * 4 + 0] == 215 &&
                      clipInfo.bgraPixels[(18 * 24 + 18) * 4 + 2] == 250 &&
                      clipInfo.bgraPixels[(18 * 24 + 18) * 4 + 0] == 143;
        tc.Record(proOk && clipOk && HashBgraBytes(proInfo.bgraPixels) != HashBgraBytes(clipInfo.bgraPixels),
                  L"Stage15.Images.ArtFonts_SampleProcreateBplistLzfseAndClipSqlite");

        // Fake CSFCHUNK + CHNKExta without CHNKSQLi SQLite3 database must be strictly rejected!
        std::vector<uint8_t> fakeClipExta(64, 0u);
        std::memcpy(fakeClipExta.data(), "CSFCHUNK", 8);
        fakeClipExta[15] = 64u;
        std::memcpy(fakeClipExta.data() + 24u, "CHNKHead", 8);
        fakeClipExta[39] = 8u;
        std::memcpy(fakeClipExta.data() + 48u, "CHNKExta", 8);
        AssertImgRejected(tc, L"Stage15.Images.ClipStudio_RejectFakeChnkExtaWithoutSqlite", fakeClipExta, L".clip");

        std::vector<uint8_t> nonProcreateZip = BuildMultiFileStoredZip({ { "Document.archive", { 'b','p','l','i','s','t','0','0', 0, 0 } } });
        AssertImgRejected(tc, L"Stage15.Images.Procreate_RejectCorruptBplistArchive", nonProcreateZip, L".procreate");

        // Adobe Swatch Exchange (.ase) RGB / CMYK / CIE LAB / Grayscale + Photoshop Swatch (.aco) v1+v2
        bool aseSwOk = okAseSw && aseSwInfo.valid && aseSwInfo.width == 128 && aseSwInfo.height == 16 &&
                       aseSwInfo.layerNames.size() == 4 &&
                       aseSwInfo.layerNames[0] == L"Crimson RGB" &&
                       aseSwInfo.layerNames[1] == L"Process Cyan CMYK" &&
                       aseSwInfo.layerNames[2] == L"Ochre CIE LAB" &&
                       aseSwInfo.layerNames[3] == L"Neutral Slate Gray" &&
                       aseSwInfo.formatName == L"Adobe Swatch Exchange (.ase, 4 colors)";
        if (aseSwOk) {
            // RenderPaletteSwatchGrid uses 8 columns of 16x16 cells (1px white border -> centers at x = 8, 24, 40, 56)
            const uint8_t* c0 = &aseSwInfo.bgraPixels[(8 * 128 + 8) * 4];  // Crimson RGB
            const uint8_t* c1 = &aseSwInfo.bgraPixels[(8 * 128 + 24) * 4]; // Process Cyan CMYK
            const uint8_t* c2 = &aseSwInfo.bgraPixels[(8 * 128 + 40) * 4]; // Ochre CIE LAB
            const uint8_t* c3 = &aseSwInfo.bgraPixels[(8 * 128 + 56) * 4]; // Neutral Slate Gray
            aseSwOk = (c0[2] > 210 && c0[1] < 60 && c0[0] < 80) &&
                      (c1[0] > 180 && c1[1] > 140 && c1[2] < 60) &&
                      (c2[2] > 160 && c2[1] > 110 && c2[0] < 80) &&
                      (c3[0] == c3[1] && c3[1] == c3[2] && c3[0] >= 70 && c3[0] <= 150);
        }

        // Build spec-valid .aco v1 + v2 with RGB(0), HSB(1), CMYK(2), Lab(7), and Grayscale(8)
        std::vector<uint8_t> acoV1V2;
        auto appendAcoEntries = [&](uint16_t ver) {
            PushU16BE(acoV1V2, ver);
            PushU16BE(acoV1V2, 5u);
            struct Sw { uint16_t cs, w0, w1, w2, w3; const char* nm; };
            const Sw sws[5] = {
                { 0u, 60000u,  5000u,  8000u,     0u, "Ruby"   }, // RGB
                { 1u, 21845u, 65535u, 58000u,     0u, "Jade"   }, // HSB (~120 deg green)
                { 2u,     0u, 30000u, 65535u, 55000u, "Cobalt" }, // CMYK (0=100% cyan, 65535=0% yellow)
                { 7u,  7000u,  3500u,  5500u,     0u, "Amber"  }, // Lab (L=70, a=+35, b=+55)
                { 8u,  5000u,     0u,     0u,     0u, "MidGray"}, // Grayscale 50%
            };
            for (const auto& s : sws) {
                PushU16BE(acoV1V2, s.cs);
                PushU16BE(acoV1V2, s.w0); PushU16BE(acoV1V2, s.w1);
                PushU16BE(acoV1V2, s.w2); PushU16BE(acoV1V2, s.w3);
                if (ver == 2u) {
                    uint32_t nl = static_cast<uint32_t>(std::strlen(s.nm)) + 1u;
                    PushU32BE(acoV1V2, nl);
                    for (size_t k = 0; s.nm[k]; ++k) PushU16BE(acoV1V2, static_cast<uint8_t>(s.nm[k]));
                    PushU16BE(acoV1V2, 0u);
                }
            }
        };
        appendAcoEntries(1u);
        appendAcoEntries(2u);
        ExtendedImageInfo acoInfo;
        bool acoOk = ImageFormatLoader::DecodeImageMemory(acoV1V2.data(), acoV1V2.size(), L".aco", 0, acoInfo) &&
                     acoInfo.valid && acoInfo.layerNames.size() == 5 &&
                     acoInfo.layerNames[0] == L"Ruby" && acoInfo.layerNames[4] == L"MidGray" &&
                     HashBgraBytes(acoInfo.bgraPixels) != HashBgraBytes(aseSwInfo.bgraPixels);
        tc.Record(aseSwOk && acoOk, L"Stage15.Images.AdobeSwatches_SampleAseCmykLabAndAcoV1V2");

        std::vector<uint8_t> badAse = { 'A','S','E','F', 0,1, 0,0, 0,0,0,0 };
        AssertImgRejected(tc, L"Stage15.Images.AdobeSwatches_RejectEmptyAse", badAse, L".ase");
        AssertImgRejected(tc, L"Stage15.Images.AdobeSwatches_RejectAseHeaderInAco", badAse, L".aco");

        // Aseprite v1.3 Tilemap + OpenType CFF/COLR + CFF2 + Windows .cur
        bool aseTileOk = okAseTile && aseTileInfo.valid && aseTileInfo.width == 16 && aseTileInfo.height == 16 &&
                         aseTileInfo.bgraPixels[(2 * 16 + 2) * 4 + 1] == 215 &&
                         aseTileInfo.bgraPixels[(2 * 16 + 9) * 4 + 2] == 235 &&
                         aseTileInfo.bgraPixels[(2 * 16 + 14) * 4 + 2] == 245 &&
                         aseTileInfo.bgraPixels[(12 * 16 + 2) * 4 + 3] == 0 &&
                         aseTileInfo.bgraPixels[(12 * 16 + 9) * 4 + 2] == 245 &&
                         aseTileInfo.bgraPixels[(12 * 16 + 14) * 4 + 2] == 235;
        bool sawCrimsonGlyph = false, sawGoldGlyph = false, sawCff2Ink = false;
        if (okColr && otfColrInfo.valid && otfColrInfo.width == 640 && otfColrInfo.height == 400) {
            for (size_t i = 640u * 48u; i < 640u * 400u; ++i) {
                uint8_t b = otfColrInfo.bgraPixels[i * 4 + 0];
                uint8_t g = otfColrInfo.bgraPixels[i * 4 + 1];
                uint8_t r = otfColrInfo.bgraPixels[i * 4 + 2];
                if (r > 200 && g < 80 && b < 80) sawCrimsonGlyph = true;
                if (r > 220 && g > 190 && b < 80) sawGoldGlyph = true;
            }
        }
        if (okCff2 && otfCff2Info.valid && otfCff2Info.width == 640 && otfCff2Info.height == 400) {
            for (size_t i = 640u * 48u; i < 640u * 400u; ++i) {
                if (otfCff2Info.bgraPixels[i * 4 + 2] < 80 && otfCff2Info.bgraPixels[i * 4 + 1] < 80) {
                    sawCff2Ink = true;
                    break;
                }
            }
        }
        bool curOk = okCur && curInfo.valid && curInfo.width == 16 && curInfo.height == 16 &&
                     curInfo.formatName.find(L"3,5") != std::wstring::npos &&
                     curInfo.bgraPixels[(0 * 16 + 0) * 4 + 3] == 0 &&
                     curInfo.bgraPixels[(5 * 16 + 3) * 4 + 2] == 250 &&
                     curInfo.bgraPixels[(10 * 16 + 4) * 4 + 0] == 245 &&
                     curInfo.bgraPixels[(10 * 16 + 4) * 4 + 3] == 255;
        tc.Record(aseTileOk && sawCrimsonGlyph && sawGoldGlyph && sawCff2Ink && curOk &&
                  HashBgraBytes(otfColrInfo.bgraPixels) != HashBgraBytes(otfCff2Info.bgraPixels),
                  L"Stage15.Images.ArtFonts_SampleAsepriteTilemapCffColrAndCur");

        std::vector<uint8_t> badCur = { 0,0, 1,0, 1,0, 16,16,0,0, 0,0, 0,0, 40,0,0,0, 22,0,0,0 };
        std::vector<uint8_t> badOtf = { 'O','T','T','O', 0,1, 0,16,0,0,0,0, 'C','F','F','2', 0,0,0,0, 0,0,0,28, 0,0,0,4, 2,0,5,0 };
        AssertImgRejected(tc, L"Stage15.Images.ArtFonts_RejectWrongCurType", badCur, L".cur");
        AssertImgRejected(tc, L"Stage15.Images.ArtFonts_RejectCorruptCff2Otf", badOtf, L".otf");

        // TrueType Collection (.ttc — 'ttcf' v1.0 multi-face directory)
        std::vector<uint8_t> faceA = AssembleSfntTables(0x00010000u, BuildBaseSfntTables("StudioSerif", 0));
        std::vector<uint8_t> faceB = AssembleSfntTables(0x00010000u, BuildBaseSfntTables("StudioSlab", 1));
        std::vector<uint8_t> ttcBytes = BuildTrueTypeCollection(faceA, faceB);
        ExtendedImageInfo ttcInfo;
        bool ttcOk = IsImageExtension(L".ttc") && ImageFormatLoader::IsExtendedImageExtension(L".ttc") &&
                     ImageFormatLoader::DecodeImageMemory(ttcBytes.data(), ttcBytes.size(), L".ttc", 0, ttcInfo) &&
                     ttcInfo.valid && ttcInfo.width == 640 && ttcInfo.height == 400 &&
                     ttcInfo.layerNames.size() == 2 &&
                     ttcInfo.layerNames[0] == L"StudioSerif Regular" &&
                     ttcInfo.layerNames[1] == L"StudioSlab Regular" &&
                     ttcInfo.formatName.find(L"TrueType Collection (.ttc, 2 faces)") != std::wstring::npos;
        tc.Record(ttcOk, L"Stage15.Images.FontCollectionTTC_MultiFaceSpecimen");
        AssertImgRejected(tc, L"Stage15.Images.FontCollectionTTC_RejectZeroFaces",
                          { 't','t','c','f', 0,1,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 }, L".ttc");

        // WOFF2 + RFC 7932 Brotli Static Dictionary + Word Transformations (direct + WOFF2 container)
        uint8_t dictWord0[64] = {}, dictWordUpper[64] = {};
        size_t dictLen0 = 0u, dictLenUpper = 0u;
        bool dictApiOk = Brotli::ResolveStaticDictionaryWord(4u, 0u, dictWord0, dictLen0) &&
                         Brotli::ResolveStaticDictionaryWord(4u, 9u * 1024u + 0u, dictWordUpper, dictLenUpper) &&
                         dictLen0 == 4u && dictLenUpper == 4u &&
                         dictWordUpper[0] == static_cast<uint8_t>(dictWord0[0] - ('a' - 'A'));
        std::string dictTag(reinterpret_cast<const char*>(dictWord0), 4u);
        std::vector<uint8_t> sfntDict = AssembleSfntTables(0x00010000u, BuildBaseSfntTables("DictWoff", 0, dictTag + "Font"));
        std::vector<uint8_t> rawWoffConcat;
        // Locate exact offset of the 4-byte RFC 7932 dictionary word inside the concatenated table payload
        (void)WrapSfntIntoWoff2CustomBrotli(sfntDict, rawWoffConcat, 0u, 0u);
        size_t matchPos = 0u;
        bool foundMatch = false;
        for (size_t i = 0; i + 4u <= rawWoffConcat.size(); ++i) {
            if (std::memcmp(rawWoffConcat.data() + i, dictWord0, 4u) == 0) {
                matchPos = i;
                foundMatch = true;
                break;
            }
        }
        bool brotliDictOk = false, woff2DictOk = false;
        if (dictApiOk && foundMatch) {
            std::vector<uint8_t> brStream = CompressBrotliWithStaticDict4(rawWoffConcat, matchPos, 0u);
            std::vector<uint8_t> brUnpacked;
            brotliDictOk = Brotli::Decompress(brStream.data(), brStream.size(), brUnpacked) &&
                           brUnpacked == rawWoffConcat;

            std::vector<uint8_t> woff2Bytes = WrapSfntIntoWoff2CustomBrotli(sfntDict, rawWoffConcat, matchPos, 0u);
            ExtendedImageInfo woff2Info;
            woff2DictOk = ImageFormatLoader::DecodeImageMemory(woff2Bytes.data(), woff2Bytes.size(), L".woff2", 0, woff2Info) &&
                          woff2Info.valid && woff2Info.width == 640 && woff2Info.height == 400;
        }
        tc.Record(dictApiOk && brotliDictOk && woff2DictOk,
                  L"Stage15.Images.Woff2Brotli_Rfc7932StaticDictionaryAndTransforms");

        // SFNT CBLC + CBDT Color Bitmap Emoji Font (Format 17 PNG glyph strikes)
        {
            std::vector<uint8_t> pngGreen = BuildSolidPngRgba(12u, 12u, 20u, 225u, 85u, 255u);
            std::vector<uint8_t> pngCoral = BuildSolidPngRgba(12u, 12u, 240u, 65u, 55u, 255u);
            std::vector<uint8_t> cbdt = { 0u, 3u, 0u, 0u }; // CBDT v3.0
            const uint32_t g1Rel = 0u;
            cbdt.insert(cbdt.end(), { 12u, 12u, 0u, 12u, 14u }); // SmallGlyphMetrics
            PushU32BE(cbdt, static_cast<uint32_t>(pngGreen.size()));
            cbdt.insert(cbdt.end(), pngGreen.begin(), pngGreen.end());
            const uint32_t g2Rel = static_cast<uint32_t>(cbdt.size() - 4u);
            cbdt.insert(cbdt.end(), { 12u, 12u, 0u, 12u, 14u });
            PushU32BE(cbdt, static_cast<uint32_t>(pngCoral.size()));
            cbdt.insert(cbdt.end(), pngCoral.begin(), pngCoral.end());
            const uint32_t gEndRel = static_cast<uint32_t>(cbdt.size() - 4u);

            std::vector<uint8_t> cblc = { 0u, 3u, 0u, 0u, 0u, 0u, 0u, 1u }; // CBLC v3.0, 1 BitmapSize
            std::vector<uint8_t> bs(48u, 0u);
            bs[3] = 56u; // subTabArrOff = 56
            bs[7] = 28u; // indexTablesSize = 28
            bs[11] = 1u; // numSubTabs = 1
            bs[41] = 1u; bs[43] = 2u; // startGid=1, endGid=2
            bs[44] = 32u; bs[45] = 32u; bs[46] = 32u; bs[47] = 1u;
            cblc.insert(cblc.end(), bs.begin(), bs.end());
            PushU16BE(cblc, 1u); PushU16BE(cblc, 2u); PushU32BE(cblc, 8u); // IndexSubTableArray
            PushU16BE(cblc, 1u); PushU16BE(cblc, 17u); PushU32BE(cblc, 4u); // IndexSubTable1 (fmt 17, imgDataOff=4)
            PushU32BE(cblc, g1Rel); PushU32BE(cblc, g2Rel); PushU32BE(cblc, gEndRel);

            auto tbls = BuildBaseSfntTables("ColorCbdt", 0);
            tbls.push_back({ {'C','B','L','C'}, std::move(cblc) });
            tbls.push_back({ {'C','B','D','T'}, std::move(cbdt) });
            std::vector<uint8_t> cbdtFont = AssembleSfntTables(0x00010000u, std::move(tbls));
            ExtendedImageInfo cbdtInfo;
            bool okCbdt = ImageFormatLoader::DecodeImageMemory(cbdtFont.data(), cbdtFont.size(), L".ttf", 0, cbdtInfo) &&
                          cbdtInfo.valid && cbdtInfo.formatName.find(L"[CBDT Color Bitmap]") != std::wstring::npos;
            bool sawGreen = false, sawCoral = false;
            if (okCbdt) {
                for (size_t i = 640u * 56u; i < 640u * 200u; ++i) {
                    uint8_t b = cbdtInfo.bgraPixels[i * 4 + 0];
                    uint8_t g = cbdtInfo.bgraPixels[i * 4 + 1];
                    uint8_t r = cbdtInfo.bgraPixels[i * 4 + 2];
                    if (g > 210 && r < 40 && b < 200) sawGreen = true;
                    if (r > 220 && g < 80 && b < 120) sawCoral = true;
                }
            }
            tc.Record(okCbdt && sawGreen && sawCoral, L"Stage15.Images.ColorBitmapFont_CblcCbdtPngStrikes");
        }

        // SFNT Apple 'sbix' Extended Bitmap Graphics ('png ' + 'dupe' alias glyph record)
        {
            std::vector<uint8_t> pngAzure = BuildSolidPngRgba(12u, 12u, 25u, 135u, 245u, 255u);
            std::vector<uint8_t> sbix;
            PushU16BE(sbix, 1u); PushU16BE(sbix, 1u); PushU32BE(sbix, 1u); PushU32BE(sbix, 12u); // 1 strike @ 12
            // Strike @ 12: ppem=32, ppi=72, 4 glyphDataOffsets (numGlyphs=3) -> firstDataOff = 20
            const uint32_t d0 = 20u;
            const uint32_t d1 = 20u; // gid 0 empty
            const uint32_t d2 = d1 + 8u + static_cast<uint32_t>(pngAzure.size()); // gid 1 = 'png '
            const uint32_t d3 = d2 + 10u; // gid 2 = 'dupe' -> gid 1
            PushU16BE(sbix, 32u); PushU16BE(sbix, 72u);
            PushU32BE(sbix, d0); PushU32BE(sbix, d1); PushU32BE(sbix, d2); PushU32BE(sbix, d3);
            PushU16BE(sbix, 0u); PushU16BE(sbix, 0u);
            sbix.insert(sbix.end(), { 'p', 'n', 'g', ' ' });
            sbix.insert(sbix.end(), pngAzure.begin(), pngAzure.end());
            PushU16BE(sbix, 0u); PushU16BE(sbix, 0u);
            sbix.insert(sbix.end(), { 'd', 'u', 'p', 'e' });
            PushU16BE(sbix, 1u); // targetGid = 1

            auto tbls = BuildBaseSfntTables("ColorSbix", 0);
            tbls.push_back({ {'s','b','i','x'}, std::move(sbix) });
            std::vector<uint8_t> sbixFont = AssembleSfntTables(0x00010000u, std::move(tbls));
            ExtendedImageInfo sbixInfo;
            bool okSbix = ImageFormatLoader::DecodeImageMemory(sbixFont.data(), sbixFont.size(), L".ttf", 0, sbixInfo) &&
                          sbixInfo.valid && sbixInfo.formatName.find(L"[sbix Color Bitmap]") != std::wstring::npos;
            size_t azurePxCount = 0u;
            if (okSbix) {
                for (size_t i = 640u * 56u; i < 640u * 200u; ++i) {
                    if (sbixInfo.bgraPixels[i * 4 + 0] > 220 && sbixInfo.bgraPixels[i * 4 + 2] < 50) ++azurePxCount;
                }
            }
            tc.Record(okSbix && azurePxCount >= 200u, L"Stage15.Images.ColorBitmapFont_SbixPngAndDupeGlyph");
        }
    }

    // ========================================================================
    // 13. Classic Quake / Half-Life / Build Textures, Sprites & WAD2/WAD3
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".wal") && IsImageExtension(L".wad") &&
                  IsImageExtension(L".spr") && IsImageExtension(L".lmp") && IsImageExtension(L".art") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".wal") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".spr") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".art"),
                  L"Stage15.Images.RetroGameTex_ExtensionsRegistered");

        ExtendedImageInfo walInfo, wad3Info, wad2Info, sprInfo, lmpInfo, artInfo;
        bool okWal  = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\quake2_lava_trim.wal", 0, walInfo);
        bool okWad3 = ImageFormatLoader::DecodeImageFile(archivesDir + L"\\halflife_cstrike.wad", 0, wad3Info);
        bool okWad2 = ImageFormatLoader::DecodeImageFile(archivesDir + L"\\quake1_gfx.wad", 0, wad2Info);
        bool okSpr  = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\halflife_explosion.spr", 0, sprInfo);
        bool okLmp  = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\quake_hud_shield.lmp", 0, lmpInfo);
        bool okArt  = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\duke3d_tiles000.art", 0, artInfo);

        bool walOk = okWal && walInfo.valid && walInfo.width == 16 && walInfo.height == 16 &&
                     walInfo.bgraPixels[(2 * 16 + 4) * 4 + 2] > 210 &&
                     walInfo.bgraPixels[(12 * 16 + 2) * 4 + 0] > 230 &&
                     walInfo.bgraPixels[(12 * 16 + 12) * 4 + 3] == 0;
        bool wadOk = okWad3 && wad3Info.valid && wad3Info.width == 36 && wad3Info.height == 16 && wad3Info.layerNames.size() == 2 &&
                     wad3Info.bgraPixels[(2 * 36 + 2) * 4 + 2] == 240 &&
                     wad3Info.bgraPixels[(2 * 36 + 10) * 4 + 1] == 220 &&
                     wad3Info.bgraPixels[(2 * 36 + 22) * 4 + 0] == 245 &&
                     okWad2 && wad2Info.valid && wad2Info.width == 8 && wad2Info.height == 8 &&
                     wad2Info.bgraPixels[(1 * 8 + 2) * 4 + 2] == 250 &&
                     wad2Info.bgraPixels[(6 * 8 + 2) * 4 + 0] == 240;
        tc.Record(walOk && wadOk && HashBgraBytes(wad3Info.bgraPixels) != HashBgraBytes(wad2Info.bgraPixels),
                  L"Stage15.Images.RetroGameTex_SampleQuake2WalAndWad2Wad3");

        bool sprOk = okSpr && sprInfo.valid && sprInfo.width == 16 && sprInfo.height == 16 &&
                     sprInfo.bgraPixels[(0 * 16 + 0) * 4 + 3] == 0 &&
                     sprInfo.bgraPixels[(8 * 16 + 8) * 4 + 2] == 255 &&
                     sprInfo.bgraPixels[(8 * 16 + 8) * 4 + 1] == 140;
        bool lmpOk = okLmp && lmpInfo.valid && lmpInfo.width == 16 && lmpInfo.height == 16 &&
                     lmpInfo.bgraPixels[(0 * 16 + 0) * 4 + 3] == 0 &&
                     lmpInfo.bgraPixels[(8 * 16 + 4) * 4 + 2] > 220 &&
                     lmpInfo.bgraPixels[(8 * 16 + 12) * 4 + 1] < 70;
        bool artOk = okArt && artInfo.valid && artInfo.width == 8 && artInfo.height == 8 &&
                     artInfo.bgraPixels[(1 * 8 + 6) * 4 + 2] > 215 &&
                     artInfo.bgraPixels[(1 * 8 + 6) * 4 + 0] < 60 &&
                     artInfo.bgraPixels[(6 * 8 + 1) * 4 + 0] > 215 &&
                     artInfo.bgraPixels[(7 * 8 + 7) * 4 + 3] == 0;
        tc.Record(sprOk && lmpOk && artOk && HashBgraBytes(sprInfo.bgraPixels) != HashBgraBytes(artInfo.bgraPixels),
                  L"Stage15.Images.RetroGameTex_SampleHlSprQuakeLmpAndBuildArt");

        std::vector<uint8_t> badLmp = { 64,0,0,0, 64,0,0,0, 1,2,3,4,5,6,7,8 };
        std::vector<uint8_t> badArt = { 1,0,0,0, 1,0,0,0, 0,0,0,0, 0,0,0,0, 16,0, 16,0, 0,0,0,0, 1,2,3,4 };
        AssertImgRejected(tc, L"Stage15.Images.RetroGameTex_RejectTruncatedQpicLmp", badLmp, L".lmp");
        AssertImgRejected(tc, L"Stage15.Images.RetroGameTex_RejectTruncatedBuildArt", badArt, L".art");
    }

    // ========================================================================
    // 14. Valve Source VTF BC4/BC5 (ATI1N/ATI2N), Float32 HDR & v7.6 Deflate AUX
    // ========================================================================
    {
        ExtendedImageInfo ati2nInfo, hdr32Info, v76Info;
        bool okAti = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\source_ati2n_normal.vtf", 0, ati2nInfo);
        bool okHdr = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\source_hdr_rgba32f.vtf", 0, hdr32Info);
        bool okV76 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\strata_v76_deflate_aux.vtf", 0, v76Info);

        bool atiOk = okAti && ati2nInfo.valid && ati2nInfo.width == 8 && ati2nInfo.height == 8 && ati2nInfo.channels == 2 &&
                     ati2nInfo.bgraPixels[(1 * 8 + 1) * 4 + 0] > 245 &&
                     ati2nInfo.bgraPixels[(1 * 8 + 5) * 4 + 2] == 245 &&
                     ati2nInfo.bgraPixels[(5 * 8 + 1) * 4 + 1] == 245;
        bool hdrOk = okHdr && hdr32Info.valid && hdr32Info.width == 8 && hdr32Info.height == 8 && hdr32Info.bitsPerChannel == 32 &&
                     hdr32Info.bgraPixels[(2 * 8 + 1) * 4 + 2] > hdr32Info.bgraPixels[(2 * 8 + 1) * 4 + 0] + 120 &&
                     hdr32Info.bgraPixels[(2 * 8 + 6) * 4 + 3] >= 126 && hdr32Info.bgraPixels[(2 * 8 + 6) * 4 + 3] <= 129 &&
                     hdr32Info.bgraPixels[(2 * 8 + 6) * 4 + 0] > hdr32Info.bgraPixels[(2 * 8 + 6) * 4 + 2] + 80;
        bool v76Ok = okV76 && v76Info.valid && v76Info.width == 8 && v76Info.height == 8 &&
                     v76Info.formatName.find(L"v7.6") != std::wstring::npos &&
                     v76Info.bgraPixels[(1 * 8 + 2) * 4 + 1] == 230 &&
                     v76Info.bgraPixels[(1 * 8 + 2) * 4 + 2] == 35 &&
                     v76Info.bgraPixels[(6 * 8 + 2) * 4 + 3] == 200;

        tc.Record(atiOk && hdrOk && v76Ok &&
                  HashBgraBytes(ati2nInfo.bgraPixels) != HashBgraBytes(v76Info.bgraPixels),
                  L"Stage15.Images.ValveVtf_SampleAti2nFloat32HdrAndV76DeflateAux");

        std::vector<uint8_t> badVtf76(120, 0);
        std::memcpy(badVtf76.data(), "VTF\0", 4);
        badVtf76[4] = 7; badVtf76[8] = 6; badVtf76[12] = 96;
        badVtf76[16] = 8; badVtf76[18] = 8; badVtf76[24] = 1; badVtf76[56] = 1; badVtf76[68] = 2;
        badVtf76[80] = 'A'; badVtf76[81] = 'X'; badVtf76[82] = 'C'; badVtf76[84] = 96;
        badVtf76[88] = 0x30; badVtf76[92] = 108;
        badVtf76[96] = 12; badVtf76[100] = 6; badVtf76[104] = 8;
        badVtf76[108] = 0xDE; badVtf76[109] = 0xAD;
        AssertImgRejected(tc, L"Stage15.Images.ValveVtf_RejectCorruptV76DeflateAux", badVtf76, L".vtf");
    }

    // ========================================================================
    // 15. Fuzz-Hardening Regression Checks (RAW IFD wrap, EXR DWA 64-bit bomb, AVCI fast reject, TIFF fast reject)
    // ========================================================================
    {
        std::vector<uint8_t> rawIfdWrap = {
            'I', 'I', 42, 0,
            0xFF, 0xFF, 0xFF, 0xFF,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };
        AssertImgRejected(tc, L"Stage15.Images.FuzzReg_DngIfdOffsetWrap", rawIfdWrap, L".dng");
        AssertImgRejected(tc, L"Stage15.Images.FuzzReg_NefIfdOffsetWrap", rawIfdWrap, L".nef");
        AssertImgRejected(tc, L"Stage15.Images.FuzzReg_ArwIfdOffsetWrap", rawIfdWrap, L".arw");

        std::vector<uint8_t> exrBomb;
        if (PathUtils::ReadFileBytes(imagesDir + L"\\exr_dwaa_studio.exr", 1024 * 1024, exrBomb) && exrBomb.size() > 292) {
            exrBomb[286] = 0xFF; exrBomb[287] = 0xFF; exrBomb[288] = 0xFF; exrBomb[289] = 0x7F;
            AssertImgRejected(tc, L"Stage15.Images.FuzzReg_OpenExrDwa64BitCountBomb", exrBomb, L".exr");
        }

        std::vector<uint8_t> avciBomb;
        if (PathUtils::ReadFileBytes(imagesDir + L"\\broadcast_still.avci", 1024 * 1024, avciBomb) && avciBomb.size() >= 1044) {
            avciBomb[1042] = 0x01; avciBomb[1043] = 0x00;
            const ULONGLONG t0 = GetTickCount64();
            AssertImgRejected(tc, L"Stage15.Images.FuzzReg_AvciTruncatedIlocFastReject", avciBomb, L".avci");
            tc.Record((GetTickCount64() - t0) < 1000ULL, L"Stage15.Images.FuzzReg_AvciNoHangUnder1s");
        }

        std::vector<uint8_t> truncTif = {
            'I', 'I', 42, 0, 8, 0, 0, 0,
            10, 0, 0, 1, 3, 0, 1, 0
        };
        const ULONGLONG tifT0 = GetTickCount64();
        AssertImgRejected(tc, L"Stage15.Images.FuzzReg_TruncatedTiffFastReject", truncTif, L".tif");
        tc.Record((GetTickCount64() - tifT0) < 500ULL, L"Stage15.Images.FuzzReg_TruncatedTiffNoSlowWic");
    }

    // ========================================================================
    // 16. Workstation #1: Alias|Wavefront PIX & Matte (.pix, .als)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".pix") && !IsImageExtension(L".als") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".pix") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".als"),
                  L"Stage15.Images.AliasPix_ExtensionsAndAbletonDisambiguation");

        ExtendedImageInfo pix24, matte8;
        bool okPix   = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\alias_wavefront.pix", 0, pix24);
        bool okMatte = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\alias_matte.als", 0, matte8);
        bool pixOk = okPix && pix24.valid && pix24.width == 16 && pix24.height == 16 && pix24.channels == 3 &&
                     pix24.formatName == L"Alias|Wavefront PIX Raster (24-bit .pix)" &&
                     pix24.bgraPixels[0] == 32u && pix24.bgraPixels[1] == 64u && pix24.bgraPixels[2] == 220u &&
                     pix24.bgraPixels[(4 * 16 + 4) * 4 + 0] == 230u &&
                     pix24.bgraPixels[(4 * 16 + 4) * 4 + 1] == 110u &&
                     pix24.bgraPixels[(4 * 16 + 4) * 4 + 2] == 30u;
        bool matteOk = okMatte && matte8.valid && matte8.width == 16 && matte8.height == 16 && matte8.channels == 1 &&
                       matte8.formatName == L"Alias|Wavefront Matte PIX (8-bit .pix)" &&
                       matte8.bgraPixels[0] == 0u &&
                       matte8.bgraPixels[(4 * 16 + 4) * 4] == 68u &&
                       matte8.bgraPixels[(15 * 16 + 15) * 4] == 255u;
        tc.Record(pixOk && matteOk && HashBgraBytes(pix24.bgraPixels) != HashBgraBytes(matte8.bgraPixels),
                  L"Stage15.Images.AliasPix_Sample24BitPixAnd8BitMatteAls");

        // Reject Ableton Live GZip/XML .als and illegal zero-run PIX packets
        std::vector<uint8_t> abletonGz = { 0x1Fu, 0x8Bu, 0x08u, 0x00u, 0,0,0,0, 0,3, 0,0,0,0 };
        std::vector<uint8_t> zeroRunPix = { 0,4, 0,4, 0,0, 0,0, 0,24,  0, 100, 150, 200 };
        AssertImgRejected(tc, L"Stage15.Images.AliasPix_RejectAbletonGzipAls", abletonGz, L".als");
        AssertImgRejected(tc, L"Stage15.Images.AliasPix_RejectZeroRunPacket", zeroRunPix, L".pix");
    }

    // ========================================================================
    // 17. Workstation #2: Utah Raster Toolkit RLE (.rle — 0xCC52)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".rle") && ImageFormatLoader::IsExtendedImageExtension(L".rle"),
                  L"Stage15.Images.UtahRle_ExtensionRegistered");

        ExtendedImageInfo rle1;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\utah_toolkit.rle", 0, rle1);
        tc.Record(ok1 && rle1.valid && rle1.width == 16 && rle1.height == 16 && rle1.channels == 4 &&
                  rle1.formatName == L"Utah Raster Toolkit RLE (.rle)" &&
                  rle1.bgraPixels[0] == 80u && rle1.bgraPixels[1] == 0u && rle1.bgraPixels[2] == 255u &&
                  rle1.bgraPixels[(15 * 16 + 15) * 4 + 0] == 210u &&
                  rle1.bgraPixels[(15 * 16 + 15) * 4 + 1] == 240u &&
                  rle1.bgraPixels[(15 * 16 + 15) * 4 + 2] == 30u,
                  L"Stage15.Images.UtahRle_SampleRgbaBottomUpDecoded");

        // Second variant: 4x4 1-channel Utah RLE with 3-channel 16-bit LE colormap (ncmap=3, cmaplen=1 -> 2 entries)
        std::vector<uint8_t> rleCmap;
        PushU16LE(rleCmap, 0xCC52u);
        PushU16LE(rleCmap, 0u); PushU16LE(rleCmap, 0u);
        PushU16LE(rleCmap, 4u); PushU16LE(rleCmap, 4u);
        rleCmap.insert(rleCmap.end(), { 0x02u, 1u, 8u, 3u, 1u, 0u }); // flags=NO_BG, ncolors=1, pixelbits=8, ncmap=3, cmaplen=1
        // R[0..1], G[0..1], B[0..1] (high byte of u16LE is 8-bit color; index 1 = (40, 210, 90))
        PushU16LE(rleCmap, 0x1000u); PushU16LE(rleCmap, 0x2800u);
        PushU16LE(rleCmap, 0x1000u); PushU16LE(rleCmap, 0xD200u);
        PushU16LE(rleCmap, 0x1000u); PushU16LE(rleCmap, 0x5A00u);
        for (int row = 0; row < 4; ++row) {
            if (row > 0) rleCmap.insert(rleCmap.end(), { 1u, 1u }); // SkipLinesOp +1
            rleCmap.insert(rleCmap.end(), { 2u, 0u, 6u, 3u, 1u, 0u }); // SetColor(0), RunData(count=4, val=1)
        }
        rleCmap.insert(rleCmap.end(), { 7u, 0u }); // EOFOp
        ExtendedImageInfo rle2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(rleCmap.data(), rleCmap.size(), L".rle", 0, rle2);
        tc.Record(ok2 && rle2.valid && rle2.width == 4 && rle2.height == 4 &&
                  rle2.bgraPixels[0] == 90u && rle2.bgraPixels[1] == 210u && rle2.bgraPixels[2] == 40u &&
                  HashBgraBytes(rle2.bgraPixels) != HashBgraBytes(rle1.bgraPixels),
                  L"Stage15.Images.UtahRle_ColormappedOpcodesAndDistinctHash");

        std::vector<uint8_t> badOpRle = rleCmap;
        badOpRle[28u] = 63u; // illegal opcode 63 at first bytecode instruction (16B hdr + 12B cmap)
        AssertImgRejected(tc, L"Stage15.Images.UtahRle_RejectUnknownOpcode", badOpRle, L".rle");
        AssertImgRejected(tc, L"Stage15.Images.UtahRle_RejectTruncatedByteData",
                          { 0x52u, 0xCCu, 0,0, 0,0, 4,0, 4,0, 2,1,8,0,0,0, 5u, 20u, 1u, 2u }, L".rle");
    }

    // ========================================================================
    // 18. Workstation #3: ImageMagick Machine Independent File Format (.miff)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".miff") && ImageFormatLoader::IsExtendedImageExtension(L".miff"),
                  L"Stage15.Images.MagickMiff_ExtensionRegistered");

        ExtendedImageInfo miff1;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\imagemagick_zip.miff", 0, miff1);
        tc.Record(ok1 && miff1.valid && miff1.width == 16 && miff1.height == 16 && miff1.channels == 4 &&
                  miff1.formatName == L"ImageMagick MIFF Raster (.miff)" &&
                  miff1.bgraPixels[(4 * 16 + 12) * 4 + 2] == 204u &&
                  miff1.bgraPixels[(4 * 16 + 12) * 4 + 1] == 68u &&
                  miff1.bgraPixels[(15 * 16 + 15) * 4 + 2] == 255u &&
                  miff1.bgraPixels[(15 * 16 + 15) * 4 + 0] == 0u,
                  L"Stage15.Images.MagickMiff_SampleZipRgbaDecoded");

        // Second variant: PseudoClass + RunlengthEncoded 4x4 MIFF
        const std::string hdr2 =
            "id=ImageMagick version=1.0\n"
            "class=PseudoClass colors=2 columns=4 rows=4 depth=8 compression=RunlengthEncoded\n:\x1A";
        std::vector<uint8_t> miff2Bytes(hdr2.begin(), hdr2.end());
        // 2-entry RGB palette: [0]=(20,30,40), [1]=(250,140,35)
        miff2Bytes.insert(miff2Bytes.end(), { 20u, 30u, 40u,  250u, 140u, 35u });
        // RLE packets: [idx, length-1]: 8 pixels of idx 1 + 8 pixels of idx 0 = 16 pixels
        miff2Bytes.insert(miff2Bytes.end(), { 1u, 7u,  0u, 7u });
        ExtendedImageInfo miff2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(miff2Bytes.data(), miff2Bytes.size(), L".miff", 0, miff2);
        tc.Record(ok2 && miff2.valid && miff2.width == 4 && miff2.height == 4 &&
                  miff2.bgraPixels[2] == 250u && miff2.bgraPixels[1] == 140u &&
                  HashBgraBytes(miff2.bgraPixels) != HashBgraBytes(miff1.bgraPixels),
                  L"Stage15.Images.MagickMiff_PseudoClassRleAndDistinctHash");

        const std::string noTerm = "id=ImageMagick columns=8 rows=8 depth=8 compression=None\n";
        AssertImgRejected(tc, L"Stage15.Images.MagickMiff_RejectMissingHeaderTerminator",
                          std::vector<uint8_t>(noTerm.begin(), noTerm.end()), L".miff");
        const std::string badZipHdr = "id=ImageMagick columns=4 rows=4 depth=8 compression=Zip\n:\x1A\xDE\xAD\xBE\xEF";
        AssertImgRejected(tc, L"Stage15.Images.MagickMiff_RejectCorruptZipStream",
                          std::vector<uint8_t>(badZipHdr.begin(), badZipHdr.end()), L".miff");
    }

    // ========================================================================
    // 19. Workstation #4: Khoros Visualization Image File Format (.viff, .xv)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".viff") && IsImageExtension(L".xv") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".viff") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".xv"),
                  L"Stage15.Images.KhorosViff_ExtensionsRegistered");

        ExtendedImageInfo viffSci, viffMap;
        bool okSci = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\khoros_scientific.viff", 0, viffSci);
        bool okMap = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\khoros_colormapped.xv", 0, viffMap);
        bool sciOk = okSci && viffSci.valid && viffSci.width == 16 && viffSci.height == 16 &&
                     viffSci.bitsPerChannel == 32 &&
                     viffSci.formatName == L"Khoros VIFF Raster (.viff)" &&
                     viffSci.bgraPixels[0] == 128u && viffSci.bgraPixels[2] == 0u &&
                     viffSci.bgraPixels[(15 * 16 + 15) * 4 + 2] == 255u;
        bool mapOk = okMap && viffMap.valid && viffMap.width == 16 && viffMap.height == 16 &&
                     viffMap.formatName == L"Khoros VIFF Colormapped Image (.viff)" &&
                     viffMap.bgraPixels[1] == 255u && viffMap.bgraPixels[0] == 40u &&
                     viffMap.bgraPixels[(15 * 16 + 15) * 4 + 2] == 238u &&
                     viffMap.bgraPixels[(15 * 16 + 15) * 4 + 0] == 166u;
        tc.Record(sciOk && mapOk && HashBgraBytes(viffSci.bgraPixels) != HashBgraBytes(viffMap.bgraPixels),
                  L"Stage15.Images.KhorosViff_SampleFloat32AndColormappedXv");

        std::vector<uint8_t> shortViff(512, 0u);
        shortViff[0] = 0xABu; shortViff[1] = 0x01u;
        AssertImgRejected(tc, L"Stage15.Images.KhorosViff_RejectShortHeader", shortViff, L".viff");

        std::vector<uint8_t> truncBands(1024u + 16u, 0u);
        truncBands[0] = 0xABu; truncBands[1] = 0x01u; truncBands[4] = 0x08u; // LE
        truncBands[520] = 16u; truncBands[524] = 16u; truncBands[560] = 3u; truncBands[564] = 5u; // 16x16x3 float32 > 16B!
        AssertImgRejected(tc, L"Stage15.Images.KhorosViff_RejectTruncatedFloatBands", truncBands, L".viff");
    }

    // ========================================================================
    // 20. Retro #5: Amiga Workbench Icon (.info — OS3.5+ GlowIcons & Classic Planar)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".info") && ImageFormatLoader::IsExtendedImageExtension(L".info"),
                  L"Stage15.Images.AmigaIconInfo_ExtensionRegistered");

        ExtendedImageInfo glowInfo;
        bool okGlow = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\amiga_glowicon.info", 0, glowInfo);
        tc.Record(okGlow && glowInfo.valid && glowInfo.width == 16 && glowInfo.height == 16 &&
                  glowInfo.formatName == L"AmigaOS 3.5+ GlowIcon (.info)" &&
                  glowInfo.bgraPixels[3] == 0u &&
                  glowInfo.bgraPixels[(1 * 16 + 1) * 4 + 0] == 240u &&
                  glowInfo.bgraPixels[(1 * 16 + 1) * 4 + 2] == 40u &&
                  glowInfo.bgraPixels[(4 * 16 + 4) * 4 + 2] == 255u &&
                  glowInfo.bgraPixels[(4 * 16 + 4) * 4 + 1] == 190u,
                  L"Stage15.Images.AmigaIconInfo_SampleGlowIconDecoded");

        // Second variant: Classic Amiga Workbench 1.3 DiskObject (0xE310) with 16x8 2-plane GadgetRender
        std::vector<uint8_t> classicWb(78u + 20u + 2u * 8u * 2u, 0u);
        classicWb[0] = 0xE3u; classicWb[1] = 0x10u; classicWb[3] = 0x01u; // WB_DISKMAGIC v1
        classicWb[4 + 21] = 1u; // gadgetRender != 0
        // Planar Image header @ 78: w=16, h=8, depth=2, idPtr=1, planePick=0x03
        classicWb[78 + 5] = 16u; classicWb[78 + 7] = 8u; classicWb[78 + 9] = 2u;
        classicWb[78 + 13] = 1u; classicWb[78 + 14] = 0x03u;
        // Plane 0 & Plane 1 all 1s -> color index 3 = Workbench 1.3 Orange (0xFF, 0x88, 0x00)
        std::fill(classicWb.begin() + 98u, classicWb.end(), 0xFFu);
        ExtendedImageInfo wbInfo;
        bool okWb = ImageFormatLoader::DecodeImageMemory(classicWb.data(), classicWb.size(), L".info", 0, wbInfo);
        tc.Record(okWb && wbInfo.valid && wbInfo.width == 16 && wbInfo.height == 8 &&
                  wbInfo.formatName == L"Amiga Workbench Planar Icon (.info)" &&
                  wbInfo.bgraPixels[2] == 0xFFu && wbInfo.bgraPixels[1] == 0x88u && wbInfo.bgraPixels[0] == 0x00u &&
                  HashBgraBytes(wbInfo.bgraPixels) != HashBgraBytes(glowInfo.bgraPixels),
                  L"Stage15.Images.AmigaIconInfo_ClassicPlanarWb13AndDistinctHash");

        const std::string gnuTexinfo = "This is gcc.info, produced by makeinfo version 6.8 from gcc.texi.\n";
        AssertImgRejected(tc, L"Stage15.Images.AmigaIconInfo_RejectGnuTexinfoText",
                          std::vector<uint8_t>(gnuTexinfo.begin(), gnuTexinfo.end()), L".info");
        std::vector<uint8_t> truncWb(classicWb.begin(), classicWb.begin() + 104u);
        AssertImgRejected(tc, L"Stage15.Images.AmigaIconInfo_RejectTruncatedBitplanes", truncWb, L".info");
    }

    // ========================================================================
    // 21. Retro #6: Atari ST DEGAS (.pi1..3) & DEGAS Elite RLE (.pc1..3)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".pi1") && IsImageExtension(L".pi2") && IsImageExtension(L".pi3") &&
                  IsImageExtension(L".pc1") && IsImageExtension(L".pc2") && IsImageExtension(L".pc3") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".pi1") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".pc1"),
                  L"Stage15.Images.AtariDegas_ExtensionsRegistered");

        ExtendedImageInfo pi1, pc1;
        bool okPi1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\atari_degas_low.pi1", 0, pi1);
        bool okPc1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\atari_degas_elite.pc1", 0, pc1);
        tc.Record(okPi1 && okPc1 && pi1.valid && pc1.valid &&
                  pi1.width == 320 && pi1.height == 200 &&
                  pc1.width == 320 && pc1.height == 200 &&
                  pi1.bgraPixels == pc1.bgraPixels &&
                  pi1.bgraPixels[(50 * 320 + 80) * 4 + 0] == 255u &&
                  pi1.bgraPixels[(50 * 320 + 80) * 4 + 1] == 255u &&
                  pi1.bgraPixels[(50 * 320 + 240) * 4 + 2] == 255u,
                  L"Stage15.Images.AtariDegas_SamplePi1AndPc1ExactMatch");

        // Second variant: SM124 High-Res Monochrome .pi3 (res=2 -> 640x400, 32,034B)
        std::vector<uint8_t> pi3Bytes(32034u, 0u);
        pi3Bytes[1] = 2u; // res = 2 (640x400 mono)
        pi3Bytes[3] = 1u; // rawPal[0] bit 0 = 1 -> standard white paper / black ink
        std::fill(pi3Bytes.begin() + 34u, pi3Bytes.begin() + 34u + 8000u, 0xAAu);
        ExtendedImageInfo pi3;
        bool okPi3 = ImageFormatLoader::DecodeImageMemory(pi3Bytes.data(), pi3Bytes.size(), L".pi3", 0, pi3);
        tc.Record(okPi3 && pi3.valid && pi3.width == 640 && pi3.height == 400 &&
                  HashBgraBytes(pi3.bgraPixels) != HashBgraBytes(pi1.bgraPixels),
                  L"Stage15.Images.AtariDegas_Sm124HighResPi3AndDistinctHash");

        std::vector<uint8_t> badRes = pi3Bytes;
        badRes[1] = 5u; // invalid resolution > 2
        AssertImgRejected(tc, L"Stage15.Images.AtariDegas_RejectInvalidResolution", badRes, L".pi1");
        AssertImgRejected(tc, L"Stage15.Images.AtariDegas_RejectTruncatedUncompressedPi1",
                          std::vector<uint8_t>(1024u, 0u), L".pi1");
        std::vector<uint8_t> truncPc1(48u, 0u);
        truncPc1[0] = 0x80u;
        AssertImgRejected(tc, L"Stage15.Images.AtariDegas_RejectTruncatedPackBitsPc1", truncPc1, L".pc1");
    }

    // ========================================================================
    // 22. Retro #7: Commodore 64 KoalaPainter Multicolor (.koa, .kla)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".koa") && IsImageExtension(L".kla") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".koa") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".kla"),
                  L"Stage15.Images.CommodoreKoala_ExtensionsRegistered");

        ExtendedImageInfo koa1;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\c64_koala_multicolor.koa", 0, koa1);
        tc.Record(ok1 && koa1.valid && koa1.width == 320 && koa1.height == 200 &&
                  koa1.formatName == L"Commodore 64 KoalaPainter Multicolor (.koa)" &&
                  koa1.bgraPixels[0] == 155u && koa1.bgraPixels[1] == 44u && koa1.bgraPixels[2] == 46u &&
                  koa1.bgraPixels[(50 * 320 + 80) * 4 + 1] == 255u &&
                  koa1.bgraPixels[(199 * 320 + 319) * 4 + 1] == 172u,
                  L"Stage15.Images.CommodoreKoala_SampleColodoreVicIIDecoded");

        // Second variant: $4000 load-address .kla with colorRAM %11 Yellow (index 7 = 237, 241, 113)
        std::vector<uint8_t> kla2(10003u, 0u);
        kla2[0] = 0x00u; kla2[1] = 0x40u; // $4000 load addr
        std::fill(kla2.begin() + 2u, kla2.begin() + 8002u, 0xFFu);    // all crumbs = %11
        std::fill(kla2.begin() + 9002u, kla2.begin() + 10002u, 0x07u); // VIC-II Yellow
        ExtendedImageInfo koa2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(kla2.data(), kla2.size(), L".kla", 0, koa2);
        tc.Record(ok2 && koa2.valid && koa2.width == 320 && koa2.height == 200 &&
                  koa2.bgraPixels[2] == 237u && koa2.bgraPixels[1] == 241u && koa2.bgraPixels[0] == 113u &&
                  HashBgraBytes(koa2.bgraPixels) != HashBgraBytes(koa1.bgraPixels),
                  L"Stage15.Images.CommodoreKoala_4000AddrKlaAndDistinctHash");

        std::vector<uint8_t> basicPrg(10003u, 0u);
        basicPrg[0] = 0x01u; basicPrg[1] = 0x08u; // $0801 C64 BASIC program -> reject!
        AssertImgRejected(tc, L"Stage15.Images.CommodoreKoala_RejectBasicPrgLoadAddr", basicPrg, L".koa");
        AssertImgRejected(tc, L"Stage15.Images.CommodoreKoala_RejectWrongFileSize",
                          std::vector<uint8_t>(10000u, 0x60u), L".koa");
    }

    // ========================================================================
    // 23. Retro #8: Sinclair ZX Spectrum Screen Dump (.scr — 6,912B ULA VRAM)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".scr") && ImageFormatLoader::IsExtendedImageExtension(L".scr"),
                  L"Stage15.Images.ZxSpectrumScr_ExtensionRegistered");

        ExtendedImageInfo zxScrSample;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\zx_spectrum_loading.scr", 0, zxScrSample);
        tc.Record(ok1 && zxScrSample.valid && zxScrSample.width == 256 && zxScrSample.height == 192 &&
                  zxScrSample.formatName == L"Sinclair ZX Spectrum Screen (.scr)" &&
                  zxScrSample.bgraPixels[0] == 215u && zxScrSample.bgraPixels[2] == 0u &&
                  zxScrSample.bgraPixels[(48 * 256 + 64) * 4 + 2] == 215u &&
                  zxScrSample.bgraPixels[(48 * 256 + 192) * 4 + 1] == 215u &&
                  zxScrSample.bgraPixels[(144 * 256 + 64) * 4 + 2] == 215u &&
                  zxScrSample.bgraPixels[(144 * 256 + 64) * 4 + 1] == 215u,
                  L"Stage15.Images.ZxSpectrumScr_SampleNonLinearUlaDecoded");

        // Second variant: BRIGHT=1 (0x40) + INK=2 (Pure Red 255) across all 6,144 bitmap bytes
        std::vector<uint8_t> scrBright(6912u, 0xFFu);
        std::fill(scrBright.begin() + 6144u, scrBright.end(), static_cast<uint8_t>(0x40u | 0x02u));
        ExtendedImageInfo zxScrBrightInfo;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(scrBright.data(), scrBright.size(), L".scr", 0, zxScrBrightInfo);
        tc.Record(ok2 && zxScrBrightInfo.valid && zxScrBrightInfo.width == 256 && zxScrBrightInfo.height == 192 &&
                  zxScrBrightInfo.bgraPixels[2] == 255u && zxScrBrightInfo.bgraPixels[1] == 0u && zxScrBrightInfo.bgraPixels[0] == 0u &&
                  HashBgraBytes(zxScrBrightInfo.bgraPixels) != HashBgraBytes(zxScrSample.bgraPixels),
                  L"Stage15.Images.ZxSpectrumScr_BrightAttributeAndDistinctHash");

        // Reject 6,912-byte Windows PE screensaver ("MZ") and wrong-length buffers
        std::vector<uint8_t> winPeScreensaver(6912u, 0u);
        winPeScreensaver[0] = 'M'; winPeScreensaver[1] = 'Z';
        AssertImgRejected(tc, L"Stage15.Images.ZxSpectrumScr_RejectWindowsPeScreensaver", winPeScreensaver, L".scr");
        AssertImgRejected(tc, L"Stage15.Images.ZxSpectrumScr_RejectWrongSize",
                          std::vector<uint8_t>(4096u, 0xAAu), L".scr");
    }
}

} // namespace Explorer2::SelfTestSuite
