// FormatTestsImages.cpp: self-tests of the image decoders (ImageFormatLoader, formats/), stage 15 ("15.images" runs these alone)
#include "SelfTestCommon.h"
#include "ImageFormatLoader.h"
#include "PathUtils.h"
#include "ZipDeflate.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
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

void PushU24LE(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
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

void PushU64LE(std::vector<uint8_t>& b, uint64_t v) {
    PushU32LE(b, static_cast<uint32_t>(v & 0xFFFFFFFFull));
    PushU32LE(b, static_cast<uint32_t>(v >> 32));
}

void PushF32LE(std::vector<uint8_t>& b, float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, 4);
    PushU32LE(b, u);
}

uint16_t FloatToHalfBits(float f) {
    uint32_t x = 0;
    std::memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000u;
    int32_t exp = static_cast<int32_t>((x >> 23) & 0xFFu) - 127 + 15;
    uint32_t mant = x & 0x007FFFFFu;
    if (exp <= 0) return static_cast<uint16_t>(sign);
    if (exp >= 31) return static_cast<uint16_t>(sign | 0x7C00u);
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13));
}

uint32_t ComputeAdler32(const uint8_t* data, size_t len) {
    uint32_t s1 = 1, s2 = 0;
    for (size_t i = 0; i < len; ++i) {
        s1 = (s1 + data[i]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    return (s2 << 16) | s1;
}

// RFC 1950 + RFC 1951 Fixed-Huffman + RLE LZ77 Zlib encoder (shrinks repeated runs for WOFF/EXR/KTX2)
std::vector<uint8_t> CompressZlibFixed(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out = { 0x78, 0x01 };
    uint32_t bitBuf = 0;
    int bitCnt = 0;
    auto putBitsLSB = [&](uint32_t val, int n) {
        for (int i = 0; i < n; ++i) {
            bitBuf |= (((val >> i) & 1u) << bitCnt);
            if (++bitCnt == 8) {
                out.push_back(static_cast<uint8_t>(bitBuf));
                bitBuf = 0;
                bitCnt = 0;
            }
        }
    };
    auto putHuffMSB = [&](uint32_t code, int n) {
        for (int i = n - 1; i >= 0; --i) {
            putBitsLSB((code >> i) & 1u, 1);
        }
    };

    putBitsLSB(1, 1); // BFINAL = 1
    putBitsLSB(1, 2); // BTYPE  = 01 (Fixed Huffman)

    size_t i = 0;
    while (i < raw.size()) {
        if (i > 0) {
            size_t run = 0;
            while (i + run < raw.size() && run < 10 && raw[i + run] == raw[i - 1]) {
                ++run;
            }
            if (run >= 3) {
                // Length codes 257..264 = exact lengths 3..10 (7-bit code = sym - 256), distance 1 = 5-bit 0
                uint32_t sym = static_cast<uint32_t>(254 + run);
                putHuffMSB(sym - 256u, 7);
                putHuffMSB(0u, 5);
                i += run;
                continue;
            }
        }
        uint8_t lit = raw[i++];
        if (lit <= 143) putHuffMSB(0x30u + lit, 8);
        else putHuffMSB(0x190u + (lit - 144u), 9);
    }
    putHuffMSB(0u, 7); // End-of-block 256
    if (bitCnt > 0) {
        out.push_back(static_cast<uint8_t>(bitBuf));
    }
    PushU32BE(out, ComputeAdler32(raw.data(), raw.size()));
    return out;
}

uint16_t PackRgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void AppendBc1Block(std::vector<uint8_t>& dst, uint16_t c0, uint16_t c1, uint32_t idxBits) {
    PushU16LE(dst, c0);
    PushU16LE(dst, c1);
    PushU32LE(dst, idxBits);
}

void AppendBc3Block(std::vector<uint8_t>& dst, uint8_t a0, uint8_t a1, uint64_t alphaBits48, uint16_t c0, uint16_t c1, uint32_t idxBits) {
    dst.push_back(a0);
    dst.push_back(a1);
    for (int i = 0; i < 6; ++i) dst.push_back(static_cast<uint8_t>((alphaBits48 >> (i * 8)) & 0xFFull));
    AppendBc1Block(dst, c0, c1, idxBits);
}

// OpenEXR forward byte-reorder + horizontal predictor
std::vector<uint8_t> ExrInterleaveAndPredict(const std::vector<uint8_t>& raw) {
    size_t n = raw.size();
    std::vector<uint8_t> t(n);
    size_t half = (n + 1) / 2;
    for (size_t i = 0; i < n; ++i) {
        if ((i & 1u) == 0) t[i / 2] = raw[i];
        else t[half + i / 2] = raw[i];
    }
    std::vector<uint8_t> pred(n);
    if (n > 0) {
        pred[0] = t[0];
        for (size_t i = 1; i < n; ++i) {
            pred[i] = static_cast<uint8_t>(static_cast<int>(t[i]) - static_cast<int>(t[i - 1]) + 128);
        }
    }
    return pred;
}

std::vector<uint8_t> BuildExrFixture(bool useRleFloat, float baseR, float baseG, float baseB) {
    const uint32_t w = 8, h = 8;
    std::vector<uint8_t> f;
    // Magic + version 2 scanline
    f.insert(f.end(), { 0x76, 0x2F, 0x31, 0x01, 0x02, 0x00, 0x00, 0x00 });

    auto writeAttr = [&](const char* name, const char* type, const std::vector<uint8_t>& val) {
        f.insert(f.end(), name, name + std::strlen(name) + 1);
        f.insert(f.end(), type, type + std::strlen(type) + 1);
        PushU32LE(f, static_cast<uint32_t>(val.size()));
        f.insert(f.end(), val.begin(), val.end());
    };

    // channels: B, G, R
    std::vector<uint8_t> chAttr;
    int32_t pixType = useRleFloat ? 2 : 1; // 2=FLOAT, 1=HALF
    for (const char* cn : { "B", "G", "R" }) {
        chAttr.insert(chAttr.end(), cn, cn + 2);
        PushU32LE(chAttr, static_cast<uint32_t>(pixType));
        PushU32LE(chAttr, 0);
        PushU32LE(chAttr, 1);
        PushU32LE(chAttr, 1);
    }
    chAttr.push_back(0);
    writeAttr("channels", "chlist", chAttr);

    // compression: 1 = RLE, 2 = ZIPS
    writeAttr("compression", "compression", { static_cast<uint8_t>(useRleFloat ? 1 : 2) });

    // dataWindow: 0, 0, w-1, h-1
    std::vector<uint8_t> box;
    PushU32LE(box, 0); PushU32LE(box, 0); PushU32LE(box, w - 1); PushU32LE(box, h - 1);
    writeAttr("dataWindow", "box2i", box);
    writeAttr("displayWindow", "box2i", box);

    f.push_back(0); // End of header

    size_t offsetTablePos = f.size();
    for (uint32_t y = 0; y < h; ++y) PushU64LE(f, 0);

    for (uint32_t y = 0; y < h; ++y) {
        uint64_t chunkOff = f.size();
        for (int b = 0; b < 8; ++b) {
            f[offsetTablePos + y * 8 + b] = static_cast<uint8_t>((chunkOff >> (b * 8)) & 0xFFull);
        }

        std::vector<uint8_t> rawLine;
        for (int ci = 0; ci < 3; ++ci) {
            for (uint32_t x = 0; x < w; ++x) {
                float u = static_cast<float>(x) / static_cast<float>(w - 1);
                float v = static_cast<float>(y) / static_cast<float>(h - 1);
                float c = (ci == 0) ? (baseB + 0.35f * v) :
                          (ci == 1) ? (baseG + 0.40f * u) :
                                      (baseR + 0.50f * (1.0f - u));
                if (useRleFloat) PushF32LE(rawLine, c);
                else PushU16LE(rawLine, FloatToHalfBits(c));
            }
        }

        std::vector<uint8_t> pred = ExrInterleaveAndPredict(rawLine);
        std::vector<uint8_t> packed;
        if (useRleFloat) {
            // Encode in RLE literal chunks + repeat runs
            size_t p = 0;
            while (p < pred.size()) {
                size_t run = 1;
                while (p + run < pred.size() && run < 120 && pred[p + run] == pred[p]) ++run;
                if (run >= 3) {
                    packed.push_back(static_cast<uint8_t>(run - 1));
                    packed.push_back(pred[p]);
                    p += run;
                } else {
                    size_t lit = std::min<size_t>(32, pred.size() - p);
                    packed.push_back(static_cast<uint8_t>(static_cast<int8_t>(-static_cast<int>(lit))));
                    packed.insert(packed.end(), pred.begin() + p, pred.begin() + p + lit);
                    p += lit;
                }
            }
            if (packed.size() == rawLine.size()) {
                // Split last literal so packedSize != expectedSize (forcing RLE decode path)
                packed.clear();
                size_t halfSz = pred.size() / 2;
                packed.push_back(static_cast<uint8_t>(static_cast<int8_t>(-static_cast<int>(halfSz))));
                packed.insert(packed.end(), pred.begin(), pred.begin() + halfSz);
                size_t remSz = pred.size() - halfSz;
                packed.push_back(static_cast<uint8_t>(static_cast<int8_t>(-static_cast<int>(remSz))));
                packed.insert(packed.end(), pred.begin() + halfSz, pred.end());
            }
        } else {
            packed = CompressZlibFixed(pred);
        }

        PushU32LE(f, y);
        PushU32LE(f, static_cast<uint32_t>(packed.size()));
        f.insert(f.end(), packed.begin(), packed.end());
    }
    return f;
}

// Apple ICNS RLE encoder (ctrl < 0x80 => ctrl+1 literals; ctrl >= 0x80 => ctrl-125 repeats)
std::vector<uint8_t> EncodeIcnsRle(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out;
    size_t i = 0;
    while (i < raw.size()) {
        size_t run = 1;
        while (i + run < raw.size() && run < 130 && raw[i + run] == raw[i]) ++run;
        if (run >= 3) {
            out.push_back(static_cast<uint8_t>(run + 125));
            out.push_back(raw[i]);
            i += run;
        } else {
            size_t lit = 0;
            size_t start = i;
            while (i < raw.size() && lit < 128) {
                if (i + 2 < raw.size() && raw[i] == raw[i + 1] && raw[i] == raw[i + 2]) break;
                ++i;
                ++lit;
            }
            out.push_back(static_cast<uint8_t>(lit - 1));
            out.insert(out.end(), raw.begin() + start, raw.begin() + start + lit);
        }
    }
    return out;
}

std::vector<uint8_t> BuildIcnsFixture(int variant) {
    const uint32_t dim = 32;
    const size_t n = dim * dim;
    std::vector<uint8_t> planes(n * 3);
    std::vector<uint8_t> mask(n);
    for (uint32_t y = 0; y < dim; ++y) {
        for (uint32_t x = 0; x < dim; ++x) {
            size_t idx = y * dim + x;
            if (variant == 0) {
                bool chk = (((x / 4) + (y / 4)) & 1u) != 0;
                planes[idx]         = chk ? 235 : 40;
                planes[n + idx]     = chk ? 180 : 25;
                planes[2 * n + idx] = chk ? 30  : 90;
                mask[idx] = (x >= 2 && x < 30 && y >= 2 && y < 30) ? 255 : 0;
            } else {
                planes[idx]         = static_cast<uint8_t>(20 + x * 6);
                planes[n + idx]     = static_cast<uint8_t>(220 - y * 5);
                planes[2 * n + idx] = 240;
                mask[idx] = ((x - 16) * (x - 16) + (y - 16) * (y - 16) <= 225) ? 255 : 40;
            }
        }
    }
    std::vector<uint8_t> rleRgb = EncodeIcnsRle(planes);

    std::vector<uint8_t> f = { 'i', 'c', 'n', 's', 0, 0, 0, 0 };
    auto addChunk = [&](const char tag[4], const std::vector<uint8_t>& payload) {
        f.insert(f.end(), tag, tag + 4);
        PushU32BE(f, static_cast<uint32_t>(8 + payload.size()));
        f.insert(f.end(), payload.begin(), payload.end());
    };
    addChunk("il32", rleRgb);
    addChunk("l8mk", mask);
    uint32_t totalLen = static_cast<uint32_t>(f.size());
    f[4] = static_cast<uint8_t>((totalLen >> 24) & 0xFFu);
    f[5] = static_cast<uint8_t>((totalLen >> 16) & 0xFFu);
    f[6] = static_cast<uint8_t>((totalLen >> 8) & 0xFFu);
    f[7] = static_cast<uint8_t>(totalLen & 0xFFu);
    return f;
}

// Photoshop .pat ('8BPT') builder (raw & PackBits scanline-table RLE)
std::vector<uint8_t> BuildPatFixture(bool packBitsRle, uint8_t rBase, uint8_t gBase, uint8_t bBase) {
    const uint16_t patW = 16, patH = 16;
    std::vector<uint8_t> f = { '8', 'B', 'P', 'T' };
    PushU16BE(f, 1); // version = 1
    PushU32BE(f, 1); // patternCount = 1

    PushU32BE(f, 1); // patVersion = 1
    PushU32BE(f, 3); // imageMode = 3 (RGB)
    PushU16BE(f, patH);
    PushU16BE(f, patW);

    // UTF-16BE name "Pat\0"
    PushU32BE(f, 4);
    PushU16BE(f, 'P'); PushU16BE(f, 'a'); PushU16BE(f, 't'); PushU16BE(f, 0);

    // Pascal string ID
    const char* idStr = "12345678-abcd";
    f.push_back(static_cast<uint8_t>(std::strlen(idStr)));
    f.insert(f.end(), idStr, idStr + std::strlen(idStr));

    // VMA list
    std::vector<uint8_t> vmaPayload;
    PushU32BE(vmaPayload, 0);    // top
    PushU32BE(vmaPayload, 0);    // left
    PushU32BE(vmaPayload, patH); // bottom
    PushU32BE(vmaPayload, patW); // right
    PushU32BE(vmaPayload, 24);   // maxChannels

    for (int ch = 0; ch < 26; ++ch) {
        if (ch >= 3) {
            PushU32BE(vmaPayload, 0); // not written
            continue;
        }
        PushU32BE(vmaPayload, 1); // isWritten = 1
        std::vector<uint8_t> chBody;
        PushU32BE(chBody, 8);    // pixelDepth
        PushU32BE(chBody, 0);    // top
        PushU32BE(chBody, 0);    // left
        PushU32BE(chBody, patH); // bottom
        PushU32BE(chBody, patW); // right
        PushU16BE(chBody, 8);    // depth
        chBody.push_back(packBitsRle ? 1 : 0);

        uint8_t base = (ch == 0) ? rBase : (ch == 1) ? gBase : bBase;
        if (!packBitsRle) {
            for (uint16_t y = 0; y < patH; ++y) {
                for (uint16_t x = 0; x < patW; ++x) {
                    chBody.push_back(static_cast<uint8_t>(base + ((x * 9 + y * 5) & 0x3Fu)));
                }
            }
        } else {
            // 16 * uint16BE row byte counts, each row encoded as 1 literal PackBits packet (1 + patW bytes)
            for (uint16_t y = 0; y < patH; ++y) {
                PushU16BE(chBody, static_cast<uint16_t>(1 + patW));
            }
            for (uint16_t y = 0; y < patH; ++y) {
                chBody.push_back(static_cast<uint8_t>(patW - 1)); // literal count - 1
                for (uint16_t x = 0; x < patW; ++x) {
                    chBody.push_back(static_cast<uint8_t>(base ^ ((x + y * 3) * 7)));
                }
            }
        }

        PushU32BE(vmaPayload, static_cast<uint32_t>(chBody.size()));
        vmaPayload.insert(vmaPayload.end(), chBody.begin(), chBody.end());
    }

    PushU32BE(f, 3); // vmaVersion = 3
    PushU32BE(f, static_cast<uint32_t>(vmaPayload.size()));
    f.insert(f.end(), vmaPayload.begin(), vmaPayload.end());
    return f;
}

std::vector<uint8_t> BuildVtfFixture(bool dxt5, uint16_t c0, uint16_t c1) {
    std::vector<uint8_t> f(80, 0);
    std::memcpy(f.data(), "VTF\0", 4);
    f[4] = 7; f[8] = 2;  // v7.2
    f[12] = 80;          // hdrSize = 80
    f[16] = 8;           // width = 8
    f[18] = 8;           // height = 8
    f[24] = 1;           // frames = 1
    f[52] = dxt5 ? 15 : 13; // DXT5 (15) or DXT1 (13)
    f[56] = 2;           // mipCount = 2 (mip1 4x4 + mip0 8x8)
    f[57] = 13;          // lowResFmt = DXT1
    f[61] = 4; f[62] = 4; // 4x4 thumbnail (8 bytes)
    f[63] = 1;           // depth = 1

    // Low-res 4x4 DXT1 thumbnail (8 bytes)
    AppendBc1Block(f, 0x0000, 0xFFFF, 0xAAAAAAAAu);

    // Mip 1 (4x4 = 1 block, stored BEFORE mip 0!)
    if (dxt5) AppendBc3Block(f, 200, 100, 0x249249249249ull, 0x001F, 0x07E0, 0u);
    else AppendBc1Block(f, 0x001F, 0x07E0, 0u);

    // Mip 0 (8x8 = 4 blocks)
    for (int b = 0; b < 4; ++b) {
        uint32_t pat = (b == 0) ? 0x05050505u : (b == 1) ? 0x5A5A5A5Au : (b == 2) ? 0xF0F0F0F0u : 0x3C3C3C3Cu;
        if (dxt5) AppendBc3Block(f, 255, 180, 0x0000FFFFFFFFull, c0, c1, pat);
        else AppendBc1Block(f, c0, c1, pat);
    }
    return f;
}

std::vector<uint8_t> BuildKtx1Fixture(bool etc2, uint16_t c0, uint16_t c1) {
    static const uint8_t kSig[12] = { 0xAB, 'K', 'T', 'X', ' ', '1', '1', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
    std::vector<uint8_t> f(kSig, kSig + 12);
    PushU32LE(f, 0x04030201u); // LE endianness
    PushU32LE(f, 0);           // glType = 0 (compressed)
    PushU32LE(f, 1);           // glTypeSize = 1
    PushU32LE(f, 0);           // glFormat = 0
    PushU32LE(f, etc2 ? 0x9274u : 0x83F3u); // GL_COMPRESSED_RGB8_ETC2 vs GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
    PushU32LE(f, etc2 ? 0x1907u : 0x1908u); // GL_RGB / GL_RGBA
    PushU32LE(f, 8);           // width
    PushU32LE(f, 8);           // height
    PushU32LE(f, 0);           // depth
    PushU32LE(f, 0);           // arrayElements
    PushU32LE(f, 1);           // faces
    PushU32LE(f, 1);           // mipLevels
    PushU32LE(f, 0);           // bytesOfKeyValueData

    if (etc2) {
        PushU32LE(f, 32); // 4 * 8-byte ETC2 blocks
        for (int b = 0; b < 4; ++b) {
            // ETC1/ETC2 individual mode (diffBit=0): R=(14,3), G=(4,12), B=(10,2), table=(2,5)
            uint8_t blk[8] = {
                static_cast<uint8_t>(0xE3u ^ (b * 17)),
                static_cast<uint8_t>(0x4Cu ^ (b * 9)),
                static_cast<uint8_t>(0xA2u),
                0x54u, // table0=2, table1=5, diff=0, flip=0
                0x00, 0xFF, 0x55, 0xAA
            };
            f.insert(f.end(), blk, blk + 8);
        }
    } else {
        PushU32LE(f, 64); // 4 * 16-byte DXT5 blocks
        for (int b = 0; b < 4; ++b) {
            AppendBc3Block(f, 255, 200, 0ull, c0, c1, 0x1B1B1B1Bu * static_cast<uint32_t>(b + 1));
        }
    }
    return f;
}

std::vector<uint8_t> BuildKtx2Fixture(bool zlibRgba8) {
    static const uint8_t kSig[12] = { 0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
    std::vector<uint8_t> mipPayload;
    uint32_t vkFmt = 0;
    uint32_t supercomp = 0;
    uint64_t uncompLen = 0;

    if (zlibRgba8) {
        vkFmt = 37;    // VK_FORMAT_R8G8B8A8_UNORM
        supercomp = 3; // ZLIB
        std::vector<uint8_t> rawRgba(8 * 8 * 4);
        for (size_t i = 0; i < 64; ++i) {
            rawRgba[i * 4 + 0] = static_cast<uint8_t>(30 + i * 3);
            rawRgba[i * 4 + 1] = 210;
            rawRgba[i * 4 + 2] = 245;
            rawRgba[i * 4 + 3] = 255;
        }
        uncompLen = rawRgba.size();
        mipPayload = CompressZlibFixed(rawRgba);
    } else {
        vkFmt = 131;   // VK_FORMAT_BC1_RGB_UNORM_BLOCK
        supercomp = 0; // None
        for (int b = 0; b < 4; ++b) {
            AppendBc1Block(mipPayload, PackRgb565(240, 50, 30), PackRgb565(250, 220, 40), 0x0F0F55AAu ^ (b * 0x11111111u));
        }
        uncompLen = mipPayload.size();
    }

    std::vector<uint8_t> f(kSig, kSig + 12);
    PushU32LE(f, vkFmt);
    PushU32LE(f, 1);         // typeSize
    PushU32LE(f, 8);         // pixelWidth
    PushU32LE(f, 8);         // pixelHeight
    PushU32LE(f, 0);         // pixelDepth
    PushU32LE(f, 0);         // layerCount
    PushU32LE(f, 1);         // faceCount
    PushU32LE(f, 1);         // levelCount
    PushU32LE(f, supercomp); // supercompressionScheme
    PushU32LE(f, 0); PushU32LE(f, 0); // DFD
    PushU32LE(f, 0); PushU32LE(f, 0); // KVD
    PushU64LE(f, 0); PushU64LE(f, 0); // SGD (offset 64..79 -> 80 bytes total)

    uint64_t mipOff = 80 + 24;
    PushU64LE(f, mipOff);
    PushU64LE(f, mipPayload.size());
    PushU64LE(f, uncompLen);
    f.insert(f.end(), mipPayload.begin(), mipPayload.end());
    return f;
}

std::vector<uint8_t> BuildBasisFixture(uint8_t seed) {
    // 77-byte basis_file_header + 23-byte basis_slice_desc + 64-byte slice payload
    std::vector<uint8_t> f(77, 0);
    f[0] = 's'; f[1] = 'B';
    f[2] = 0x13; f[3] = 0x00; // m_version
    f[4] = 77;   f[5] = 0;    // m_header_size = 77
    f[8] = 87;                // m_data_size
    f[14] = 1;                // m_total_slices = 1
    f[17] = 1;                // m_total_images = 1
    f[20] = 0;                // m_tex_format = ETC1S
    f[65] = 77;               // m_slice_desc_file_ofs = 77

    // 23-byte basis_slice_desc at offset 77
    std::vector<uint8_t> sd;
    PushU24LE(sd, 0);  // m_image_index
    sd.push_back(0);   // m_level_index
    sd.push_back(0);   // m_flags
    PushU16LE(sd, 16); // m_orig_width
    PushU16LE(sd, 16); // m_orig_height
    PushU16LE(sd, 4);  // m_num_blocks_x
    PushU16LE(sd, 4);  // m_num_blocks_y
    PushU32LE(sd, 100);// m_file_ofs = 100
    PushU32LE(sd, 64); // m_file_size = 64
    PushU16LE(sd, 0);  // m_slice_data_crc16
    f.insert(f.end(), sd.begin(), sd.end());

    for (uint32_t i = 0; i < 64; ++i) {
        f.push_back(static_cast<uint8_t>((seed * 53u) ^ (i * 29u + seed)));
    }
    return f;
}

std::vector<uint8_t> BuildPvrFixture(bool pvrtc4bpp) {
    std::vector<uint8_t> f;
    PushU32LE(f, 0x03525650u);            // 'PVR\x03'
    PushU32LE(f, 0);                      // flags
    PushU64LE(f, pvrtc4bpp ? 3ull : 7ull);// 3 = PVRTC 4bpp RGBA, 7 = DXT1
    PushU32LE(f, 0);                      // colourSpace
    PushU32LE(f, 0);                      // channelType
    PushU32LE(f, 8);                      // height
    PushU32LE(f, 8);                      // width
    PushU32LE(f, 1);                      // depth
    PushU32LE(f, 1);                      // numSurfaces
    PushU32LE(f, 1);                      // numFaces
    PushU32LE(f, 1);                      // mipCount
    PushU32LE(f, 0);                      // metaDataSize (52 bytes header)

    if (pvrtc4bpp) {
        // 2x2 PVRTC 64-bit words = 32 bytes
        for (uint32_t i = 0; i < 4; ++i) {
            uint32_t modData = 0xE4E41B1Bu ^ (i * 0x33333333u);
            // Opaque ColorA (bit 15 = 1, RGB554) and Opaque ColorB (bit 31 = 1, RGB555)
            uint32_t colorWord = (1u << 31) | (0x7C1Fu << 16) | (1u << 15) | (0x03E0u ^ (i * 0x1100u));
            PushU32LE(f, modData);
            PushU32LE(f, colorWord);
        }
    } else {
        for (int b = 0; b < 4; ++b) {
            AppendBc1Block(f, PackRgb565(30, 220, 90), PackRgb565(180, 40, 230), 0xA5A55A5Au ^ static_cast<uint32_t>(b * 19));
        }
    }
    return f;
}

// Construct a genuine Khronos ASTC 4x4 LDR RGB Direct (CEM 8, 4x4 2-bit weight grid) texture
std::vector<uint8_t> BuildAstcFixture(uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1, uint8_t g1, uint8_t b1, uint32_t wtPattern) {
    std::vector<uint8_t> f = {
        0x13, 0xAB, 0xA1, 0x5C, // ASTC magic
        4, 4, 1,                // 4x4x1 block footprint
        8, 0, 0,                // width = 8
        8, 0, 0,                // height = 8
        1, 0, 0                 // depth = 1
    };

    // 4 blocks for 8x8 surface:
    // blockMode = 0x0042 => sub=0, A=2 (gridH=4), B=0 (gridW=4), R=4 (2-bit weights, 32 weight bits), H=0, D=0
    // bits 12..11 = 0 (1 partition), bits 16..13 = 8 (CEM 8 = LDR RGB Direct, 6 x 8-bit endpoints at bits 17..64!)
    for (uint32_t b = 0; b < 4; ++b) {
        uint8_t blk[16] = {};
        auto setBit = [&](int bitIdx, uint32_t val) {
            if (val & 1u) blk[bitIdx >> 3] |= static_cast<uint8_t>(1u << (bitIdx & 7));
        };
        auto setBits = [&](int startBit, int count, uint32_t val) {
            for (int i = 0; i < count; ++i) setBit(startBit + i, (val >> i) & 1u);
        };

        setBits(0, 11, 0x0042u); // 11-bit blockMode
        setBits(11, 2, 0u);      // 1 partition
        setBits(13, 4, 8u);      // CEM 8 = LDR RGB Direct
        uint8_t eps[6] = {
            static_cast<uint8_t>(r0 + b * 4), r1,
            static_cast<uint8_t>(g0 + b * 3), g1,
            b0, b1
        };
        for (int e = 0; e < 6; ++e) {
            setBits(17 + e * 8, 8, eps[e]);
        }
        // Weights are stored bit-reversed from bit 127 downward (32 bits total)
        uint32_t wts = wtPattern ^ (b * 0x1B1B1B1Bu);
        for (int wi = 0; wi < 32; ++wi) {
            setBit(127 - wi, (wts >> wi) & 1u);
        }
        f.insert(f.end(), blk, blk + 16);
    }
    return f;
}

void PushUeCompactIndex(std::vector<uint8_t>& b, int32_t val) {
    bool neg = (val < 0);
    uint32_t mag = static_cast<uint32_t>(neg ? -static_cast<int64_t>(val) : static_cast<int64_t>(val));
    uint8_t b0 = static_cast<uint8_t>(mag & 0x3Fu);
    if (neg) b0 |= 0x80u;
    mag >>= 6;
    if (mag != 0) b0 |= 0x40u;
    b.push_back(b0);
    while (mag != 0) {
        uint8_t bn = static_cast<uint8_t>(mag & 0x7Fu);
        mag >>= 7;
        if (mag != 0) bn |= 0x80u;
        b.push_back(bn);
    }
}

// Construct a valid Unreal Engine v69 .utx package containing Palette + Texture (TEXF_P8 or TEXF_DXT1)
std::vector<uint8_t> BuildUtxFixture(bool dxt1Mode) {
    // Names:
    // 0: None, 1: Core, 2: Engine, 3: Class, 4: Palette, 5: Texture,
    // 6: Format, 7: Palette (or USize), 8: USize, 9: VSize, 10: MyPal, 11: MyTex
    const char* kNames[] = {
        "None", "Core", "Engine", "Class", "Palette", "Texture",
        "Format", "USize", "VSize", "MyPal", "MyTex"
    };
    constexpr uint32_t kNameCount = static_cast<uint32_t>(std::size(kNames));

    std::vector<uint8_t> nameTbl;
    for (const char* s : kNames) {
        size_t lenWithNull = std::strlen(s) + 1;
        PushUeCompactIndex(nameTbl, static_cast<int32_t>(lenWithNull));
        nameTbl.insert(nameTbl.end(), s, s + lenWithNull);
        PushU32LE(nameTbl, 0x00070010u); // RF_Public | RF_Standalone
    }

    // Import 0 (-1): Class Engine.Palette
    // Import 1 (-2): Class Engine.Texture
    std::vector<uint8_t> impTbl;
    auto addImport = [&](int32_t clsPkg, int32_t clsName, int32_t pkgIdx, int32_t objName) {
        PushUeCompactIndex(impTbl, clsPkg);
        PushUeCompactIndex(impTbl, clsName);
        PushU32LE(impTbl, static_cast<uint32_t>(pkgIdx));
        PushUeCompactIndex(impTbl, objName);
    };
    addImport(1, 3, 0, 4); // Palette
    addImport(1, 3, 0, 5); // Texture

    // Serialized Export 0 (+1): MyPal (class -1 = Palette)
    std::vector<uint8_t> palData;
    PushUeCompactIndex(palData, 0); // "None" (end of tagged properties)
    PushUeCompactIndex(palData, 256); // 256 FColor RGBA entries
    for (int i = 0; i < 256; ++i) {
        palData.push_back(static_cast<uint8_t>((i * 7 + 40) & 0xFF)); // R
        palData.push_back(static_cast<uint8_t>((i * 3 + 90) & 0xFF)); // G
        palData.push_back(static_cast<uint8_t>((220 - i) & 0xFF));    // B
        palData.push_back(255);                                       // A
    }

    // Serialized Export 1 (+2): MyTex (class -2 = Texture)
    std::vector<uint8_t> texData;
    auto addByteProp = [&](int32_t nameIdx, uint8_t val) {
        PushUeCompactIndex(texData, nameIdx);
        texData.push_back(0x01u); // type=1 (ByteProperty), sizeCode=0 (1 byte)
        texData.push_back(val);
    };
    auto addObjProp = [&](int32_t nameIdx, int32_t objRef) {
        std::vector<uint8_t> refBytes;
        PushUeCompactIndex(refBytes, objRef);
        PushUeCompactIndex(texData, nameIdx);
        texData.push_back(0x55u); // type=5 (ObjectProperty), sizeCode=5 (1-byte explicit size)
        texData.push_back(static_cast<uint8_t>(refBytes.size()));
        texData.insert(texData.end(), refBytes.begin(), refBytes.end());
    };
    auto addIntProp = [&](int32_t nameIdx, uint32_t val) {
        PushUeCompactIndex(texData, nameIdx);
        texData.push_back(0x22u); // type=2 (IntProperty), sizeCode=2 (4 bytes)
        PushU32LE(texData, val);
    };

    addByteProp(6, dxt1Mode ? 0x03u : 0x00u); // Format: 3=TEXF_DXT1, 0=TEXF_P8
    if (!dxt1Mode) {
        addObjProp(4, 1); // Palette = Export 1 (MyPal)
    }
    addIntProp(7, 8); // USize = 8
    addIntProp(8, 8); // VSize = 8
    PushUeCompactIndex(texData, 0); // "None"

    // Mipmap array: mipCount = 1, DataArray + USize + VSize
    PushUeCompactIndex(texData, 1);
    if (dxt1Mode) {
        std::vector<uint8_t> dxtBytes;
        for (int b = 0; b < 4; ++b) {
            AppendBc1Block(dxtBytes, PackRgb565(20, 210, 240), PackRgb565(190, 50, 220), 0x55AA55AAu ^ static_cast<uint32_t>(b * 7));
        }
        PushUeCompactIndex(texData, static_cast<int32_t>(dxtBytes.size()));
        texData.insert(texData.end(), dxtBytes.begin(), dxtBytes.end());
    } else {
        PushUeCompactIndex(texData, 64);
        for (uint8_t i = 0; i < 64; ++i) texData.push_back(static_cast<uint8_t>(i * 3 + 5));
    }
    PushU32LE(texData, 8); // mip USize
    PushU32LE(texData, 8); // mip VSize
    texData.push_back(3);  // UBits
    texData.push_back(3);  // VBits

    // Layout package: [Header 64B] [NameTable] [ImportTable] [PalSerial] [TexSerial] [ExportTable]
    uint32_t nameOff = 64;
    uint32_t impOff  = nameOff + static_cast<uint32_t>(nameTbl.size());
    uint32_t palOff  = impOff  + static_cast<uint32_t>(impTbl.size());
    uint32_t texOff  = palOff  + static_cast<uint32_t>(palData.size());

    std::vector<uint8_t> expTbl;
    auto addExport = [&](int32_t clsIdx, int32_t objName, uint32_t serSize, uint32_t serOff) {
        PushUeCompactIndex(expTbl, clsIdx);
        PushUeCompactIndex(expTbl, 0); // superIndex
        PushU32LE(expTbl, 0);          // groupIndex
        PushUeCompactIndex(expTbl, objName);
        PushU32LE(expTbl, 0x00070004u);// objectFlags
        PushUeCompactIndex(expTbl, static_cast<int32_t>(serSize));
        PushUeCompactIndex(expTbl, static_cast<int32_t>(serOff));
    };
    addExport(-1, 9,  static_cast<uint32_t>(palData.size()), palOff);
    addExport(-2, 10, static_cast<uint32_t>(texData.size()), texOff);
    uint32_t expOff = texOff + static_cast<uint32_t>(texData.size());

    std::vector<uint8_t> pkg;
    PushU32LE(pkg, 0x9E2A83C1u); // UE magic
    PushU16LE(pkg, 69);          // ueVersion = 69
    PushU16LE(pkg, 0);           // licensee
    PushU32LE(pkg, 1);           // packageFlags
    PushU32LE(pkg, kNameCount);
    PushU32LE(pkg, nameOff);
    PushU32LE(pkg, 2);           // exportCount
    PushU32LE(pkg, expOff);
    PushU32LE(pkg, 2);           // importCount
    PushU32LE(pkg, impOff);
    pkg.resize(64, 0);

    pkg.insert(pkg.end(), nameTbl.begin(), nameTbl.end());
    pkg.insert(pkg.end(), impTbl.begin(), impTbl.end());
    pkg.insert(pkg.end(), palData.begin(), palData.end());
    pkg.insert(pkg.end(), texData.begin(), texData.end());
    pkg.insert(pkg.end(), expTbl.begin(), expTbl.end());
    return pkg;
}

// Construct a valid SFNT TrueType (.ttf) buffer with real cmap + head + hhea + hmtx + loca + maxp + name + glyf contours
std::vector<uint8_t> BuildSyntheticTtf(const std::string& familyName, int shapeVariant) {
    struct Tbl {
        char tag[4];
        std::vector<uint8_t> bytes;
    };
    std::vector<Tbl> tables;

    // 1. glyf + loca (3 glyphs: 0 = .notdef, 1 = 'A'..'M' shape, 2 = 'N'..'Z'/'0'..'9' shape)
    std::vector<uint8_t> glyf;
    std::vector<uint32_t> locaOffsets;
    auto appendSimpleGlyph = [&](const std::vector<std::pair<int16_t, int16_t>>& pts, const std::vector<bool>& onCurve) {
        locaOffsets.push_back(static_cast<uint32_t>(glyf.size()));
        if (pts.empty()) return;
        PushU16BE(glyf, 1); // numberOfContours = 1
        PushU16BE(glyf, 0); PushU16BE(glyf, 0); PushU16BE(glyf, 700); PushU16BE(glyf, 800);
        PushU16BE(glyf, static_cast<uint16_t>(pts.size() - 1)); // endPtsOfContours[0]
        PushU16BE(glyf, 0); // instructionLength = 0
        for (size_t i = 0; i < pts.size(); ++i) {
            glyf.push_back(onCurve[i] ? 0x01u : 0x00u); // 16-bit signed delta X & Y
        }
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
        while ((glyf.size() & 3u) != 0) glyf.push_back(0);
    };

    appendSimpleGlyph({}, {}); // gid 0: empty
    if (shapeVariant == 0) {
        // Sharp tall triangle spire + diamond
        appendSimpleGlyph({ {60, 0}, {340, 780}, {620, 0}, {460, 0}, {340, 420}, {220, 0} },
                          { true, true, true, true, true, true });
        appendSimpleGlyph({ {340, 20}, {640, 400}, {340, 780}, {40, 400} },
                          { true, true, true, true });
    } else {
        // Wide heavy block bar + curved quadratic Bezier arch
        appendSimpleGlyph({ {40, 80}, {640, 80}, {640, 680}, {340, 820}, {40, 680} },
                          { true, true, true, false, true });
        appendSimpleGlyph({ {80, 0}, {600, 0}, {600, 240}, {260, 240}, {260, 760}, {80, 760} },
                          { true, true, true, true, true, true });
    }
    // Add compressible zero padding so WOFF zlib compression achieves compLength < origLength
    for (int z = 0; z < 48; ++z) glyf.push_back(0);
    locaOffsets.push_back(static_cast<uint32_t>(glyf.size()));

    std::vector<uint8_t> loca;
    for (uint32_t off : locaOffsets) PushU32BE(loca, off);

    // 2. head (54 bytes, indexToLocFormat = 1)
    std::vector<uint8_t> head(54, 0);
    head[1] = 1; // version 1.0
    PushU16BE(head, 0); head.resize(54, 0);
    head[12] = 0x5F; head[13] = 0x0F; head[14] = 0x3C; head[15] = 0xF5; // magicNumber
    head[17] = 0x0B;
    head[18] = (1000 >> 8) & 0xFF; head[19] = 1000 & 0xFF; // unitsPerEm = 1000
    head[41] = (700 >> 8) & 0xFF;  head[42] = 700 & 0xFF;
    head[50] = 0; head[51] = 1; // indexToLocFormat = 1 (long 32-bit offsets)

    // 3. maxp (6 bytes, numGlyphs = 3)
    std::vector<uint8_t> maxp = { 0x00, 0x00, 0x50, 0x00, 0x00, 0x03 };

    // 4. hhea (36 bytes, numberOfHMetrics = 3) & hmtx (12 bytes)
    std::vector<uint8_t> hhea(36, 0);
    hhea[1] = 1;
    hhea[4] = (800 >> 8) & 0xFF; hhea[5] = 800 & 0xFF;
    hhea[6] = 0xFF; hhea[7] = 0x38; // -200
    hhea[34] = 0; hhea[35] = 3;
    std::vector<uint8_t> hmtx;
    for (int g = 0; g < 3; ++g) { PushU16BE(hmtx, 700); PushU16BE(hmtx, 40); }

    // 5. cmap (Format 4 with 3 segments: '0'..'M' -> gid 1, 'N'..'z' -> gid 2, 0xFFFF terminator)
    std::vector<uint8_t> cmap;
    PushU16BE(cmap, 0); // version
    PushU16BE(cmap, 1); // numTables = 1
    PushU16BE(cmap, 3); // platformID = 3 (Windows)
    PushU16BE(cmap, 1); // encodingID = 1 (Unicode BMP)
    PushU32BE(cmap, 12);// offset to subtable

    std::vector<uint8_t> sub4;
    PushU16BE(sub4, 4);  // format 4
    PushU16BE(sub4, 0);  // length placeholder
    PushU16BE(sub4, 0);  // language
    PushU16BE(sub4, 6);  // segCountX2 = 3 * 2 = 6
    PushU16BE(sub4, 4);  // searchRange
    PushU16BE(sub4, 1);  // entrySelector
    PushU16BE(sub4, 2);  // rangeShift
    // endCode[3]
    PushU16BE(sub4, 'M'); PushU16BE(sub4, 'z'); PushU16BE(sub4, 0xFFFFu);
    PushU16BE(sub4, 0); // reservedPad
    // startCode[3]
    PushU16BE(sub4, '0'); PushU16BE(sub4, 'N'); PushU16BE(sub4, 0xFFFFu);
    // idDelta[3]: map entire segment to glyph 1 / glyph 2 via idRangeOffset array, or direct delta?
    // With idRangeOffset pointing into glyphIdArray, every char in '0'..'M' maps directly to gid 1, and 'N'..'z' to gid 2!
    std::vector<uint16_t> glyphArr;
    for (uint16_t c = '0'; c <= 'M'; ++c) glyphArr.push_back(1);
    size_t seg1Start = glyphArr.size();
    for (uint16_t c = 'N'; c <= 'z'; ++c) glyphArr.push_back(2);

    PushU16BE(sub4, 0); PushU16BE(sub4, 0); PushU16BE(sub4, 1); // idDelta
    // idRangeOffset[0] sits at offset: 6 bytes to end of idRangeOffset[3] => 6
    // idRangeOffset[1] sits at offset: 4 bytes to glyphArr[0] + seg1Start * 2
    PushU16BE(sub4, 6);
    PushU16BE(sub4, static_cast<uint16_t>(4 + seg1Start * 2));
    PushU16BE(sub4, 0);
    for (uint16_t g : glyphArr) PushU16BE(sub4, g);
    sub4[2] = static_cast<uint8_t>((sub4.size() >> 8) & 0xFFu);
    sub4[3] = static_cast<uint8_t>(sub4.size() & 0xFFu);
    cmap.insert(cmap.end(), sub4.begin(), sub4.end());

    // 6. name table (Platform 3 UTF-16BE: nameID 1 = familyName, nameID 2 = "Regular")
    std::vector<uint8_t> nameStr;
    for (char c : familyName) { PushU16BE(nameStr, static_cast<uint8_t>(c)); }
    uint16_t famBytes = static_cast<uint16_t>(nameStr.size());
    const char* subFam = "Regular";
    for (size_t i = 0; subFam[i]; ++i) { PushU16BE(nameStr, static_cast<uint8_t>(subFam[i])); }
    uint16_t subBytes = static_cast<uint16_t>(nameStr.size() - famBytes);

    std::vector<uint8_t> nameTbl;
    PushU16BE(nameTbl, 0); // format 0
    PushU16BE(nameTbl, 2); // count = 2
    PushU16BE(nameTbl, 6 + 2 * 12); // stringOffset = 30
    // Record 0: Family
    PushU16BE(nameTbl, 3); PushU16BE(nameTbl, 1); PushU16BE(nameTbl, 0x0409);
    PushU16BE(nameTbl, 1); PushU16BE(nameTbl, famBytes); PushU16BE(nameTbl, 0);
    // Record 1: Subfamily
    PushU16BE(nameTbl, 3); PushU16BE(nameTbl, 1); PushU16BE(nameTbl, 0x0409);
    PushU16BE(nameTbl, 2); PushU16BE(nameTbl, subBytes); PushU16BE(nameTbl, famBytes);
    nameTbl.insert(nameTbl.end(), nameStr.begin(), nameStr.end());

    tables.push_back({ {'c','m','a','p'}, std::move(cmap) });
    tables.push_back({ {'g','l','y','f'}, std::move(glyf) });
    tables.push_back({ {'h','e','a','d'}, std::move(head) });
    tables.push_back({ {'h','h','e','a'}, std::move(hhea) });
    tables.push_back({ {'h','m','t','x'}, std::move(hmtx) });
    tables.push_back({ {'l','o','c','a'}, std::move(loca) });
    tables.push_back({ {'m','a','x','p'}, std::move(maxp) });
    tables.push_back({ {'n','a','m','e'}, std::move(nameTbl) });

    std::vector<uint8_t> sfnt;
    PushU32BE(sfnt, 0x00010000u);
    PushU16BE(sfnt, static_cast<uint16_t>(tables.size()));
    PushU16BE(sfnt, 128);
    PushU16BE(sfnt, 3);
    PushU16BE(sfnt, 0);

    size_t dirEnd = 12 + tables.size() * 16;
    sfnt.resize(dirEnd, 0);
    for (size_t i = 0; i < tables.size(); ++i) {
        uint32_t off = static_cast<uint32_t>(sfnt.size());
        uint32_t len = static_cast<uint32_t>(tables[i].bytes.size());
        uint32_t csum = 0;
        for (size_t k = 0; k + 4 <= tables[i].bytes.size(); k += 4) {
            csum += (static_cast<uint32_t>(tables[i].bytes[k]) << 24) |
                    (static_cast<uint32_t>(tables[i].bytes[k + 1]) << 16) |
                    (static_cast<uint32_t>(tables[i].bytes[k + 2]) << 8) |
                    static_cast<uint32_t>(tables[i].bytes[k + 3]);
        }
        size_t rec = 12 + i * 16;
        std::memcpy(sfnt.data() + rec, tables[i].tag, 4);
        sfnt[rec + 4]  = static_cast<uint8_t>((csum >> 24) & 0xFFu);
        sfnt[rec + 5]  = static_cast<uint8_t>((csum >> 16) & 0xFFu);
        sfnt[rec + 6]  = static_cast<uint8_t>((csum >> 8) & 0xFFu);
        sfnt[rec + 7]  = static_cast<uint8_t>(csum & 0xFFu);
        sfnt[rec + 8]  = static_cast<uint8_t>((off >> 24) & 0xFFu);
        sfnt[rec + 9]  = static_cast<uint8_t>((off >> 16) & 0xFFu);
        sfnt[rec + 10] = static_cast<uint8_t>((off >> 8) & 0xFFu);
        sfnt[rec + 11] = static_cast<uint8_t>(off & 0xFFu);
        sfnt[rec + 12] = static_cast<uint8_t>((len >> 24) & 0xFFu);
        sfnt[rec + 13] = static_cast<uint8_t>((len >> 16) & 0xFFu);
        sfnt[rec + 14] = static_cast<uint8_t>((len >> 8) & 0xFFu);
        sfnt[rec + 15] = static_cast<uint8_t>(len & 0xFFu);

        sfnt.insert(sfnt.end(), tables[i].bytes.begin(), tables[i].bytes.end());
        while ((sfnt.size() & 3u) != 0) sfnt.push_back(0);
    }
    return sfnt;
}

// Wrap a valid SFNT .ttf into a WOFF 1.0 ('wOFF') container with real zlib-compressed tables
std::vector<uint8_t> BuildWoffFromSfnt(const std::vector<uint8_t>& sfnt) {
    uint16_t numTables = static_cast<uint16_t>((sfnt[4] << 8) | sfnt[5]);
    std::vector<uint8_t> woff(44 + static_cast<size_t>(numTables) * 20, 0);
    std::memcpy(woff.data(), "wOFF", 4);
    std::memcpy(woff.data() + 4, sfnt.data(), 4); // flavor
    woff[12] = sfnt[4]; woff[13] = sfnt[5];       // numTables
    uint32_t totalSfnt = static_cast<uint32_t>(sfnt.size());
    woff[16] = static_cast<uint8_t>((totalSfnt >> 24) & 0xFFu);
    woff[17] = static_cast<uint8_t>((totalSfnt >> 16) & 0xFFu);
    woff[18] = static_cast<uint8_t>((totalSfnt >> 8) & 0xFFu);
    woff[19] = static_cast<uint8_t>(totalSfnt & 0xFFu);

    for (uint16_t i = 0; i < numTables; ++i) {
        const uint8_t* srec = sfnt.data() + 12 + static_cast<size_t>(i) * 16;
        uint32_t csum = (static_cast<uint32_t>(srec[4]) << 24) | (static_cast<uint32_t>(srec[5]) << 16) |
                        (static_cast<uint32_t>(srec[6]) << 8)  | static_cast<uint32_t>(srec[7]);
        uint32_t off  = (static_cast<uint32_t>(srec[8]) << 24) | (static_cast<uint32_t>(srec[9]) << 16) |
                        (static_cast<uint32_t>(srec[10]) << 8) | static_cast<uint32_t>(srec[11]);
        uint32_t len  = (static_cast<uint32_t>(srec[12]) << 24) | (static_cast<uint32_t>(srec[13]) << 16) |
                        (static_cast<uint32_t>(srec[14]) << 8)  | static_cast<uint32_t>(srec[15]);

        std::vector<uint8_t> orig(sfnt.begin() + off, sfnt.begin() + off + len);
        std::vector<uint8_t> z = CompressZlibFixed(orig);
        const std::vector<uint8_t>& chosen = (z.size() < orig.size()) ? z : orig;

        uint32_t wOff = static_cast<uint32_t>(woff.size());
        uint32_t cLen = static_cast<uint32_t>(chosen.size());
        uint8_t* drec = woff.data() + 44 + static_cast<size_t>(i) * 20;
        std::memcpy(drec, srec, 4); // tag
        drec[4]  = static_cast<uint8_t>((wOff >> 24) & 0xFFu);
        drec[5]  = static_cast<uint8_t>((wOff >> 16) & 0xFFu);
        drec[6]  = static_cast<uint8_t>((wOff >> 8) & 0xFFu);
        drec[7]  = static_cast<uint8_t>(wOff & 0xFFu);
        drec[8]  = static_cast<uint8_t>((cLen >> 24) & 0xFFu);
        drec[9]  = static_cast<uint8_t>((cLen >> 16) & 0xFFu);
        drec[10] = static_cast<uint8_t>((cLen >> 8) & 0xFFu);
        drec[11] = static_cast<uint8_t>(cLen & 0xFFu);
        drec[12] = static_cast<uint8_t>((len >> 24) & 0xFFu);
        drec[13] = static_cast<uint8_t>((len >> 16) & 0xFFu);
        drec[14] = static_cast<uint8_t>((len >> 8) & 0xFFu);
        drec[15] = static_cast<uint8_t>(len & 0xFFu);
        drec[16] = static_cast<uint8_t>((csum >> 24) & 0xFFu);
        drec[17] = static_cast<uint8_t>((csum >> 16) & 0xFFu);
        drec[18] = static_cast<uint8_t>((csum >> 8) & 0xFFu);
        drec[19] = static_cast<uint8_t>(csum & 0xFFu);

        woff.insert(woff.end(), chosen.begin(), chosen.end());
        while ((woff.size() & 3u) != 0) woff.push_back(0);
    }

    uint32_t totalWoff = static_cast<uint32_t>(woff.size());
    woff[8]  = static_cast<uint8_t>((totalWoff >> 24) & 0xFFu);
    woff[9]  = static_cast<uint8_t>((totalWoff >> 16) & 0xFFu);
    woff[10] = static_cast<uint8_t>((totalWoff >> 8) & 0xFFu);
    woff[11] = static_cast<uint8_t>(totalWoff & 0xFFu);
    return woff;
}

std::vector<uint8_t> BuildWoff2Fixture(uint8_t seed, uint32_t glyfOrigSize) {
    std::vector<uint8_t> f(48, 0);
    std::memcpy(f.data(), "wOF2", 4);
    f[5] = 0x01;              // flavor = 0x00010000
    f[13] = 3;                // numTables = 3 (head, cmap, glyf)
    PushU32BE(f, 0); f.resize(48, 0);
    f[18] = 0x10;             // totalSfntSize = 4096
    f[23] = 32;               // totalCompressedSize = 32

    auto pushBase128 = [&](uint32_t v) {
        if (v < 128) f.push_back(static_cast<uint8_t>(v));
        else {
            f.push_back(static_cast<uint8_t>(0x80u | ((v >> 7) & 0x7Fu)));
            f.push_back(static_cast<uint8_t>(v & 0x7Fu));
        }
    };
    // Table 0: head (tagIdx=1, xformVer=0 => untransformed)
    f.push_back(1);
    pushBase128(54);
    // Table 1: cmap (tagIdx=0, xformVer=0 => untransformed)
    f.push_back(0);
    pushBase128(120 + seed);
    // Table 2: glyf (tagIdx=10, xformVer=0 => transformed, so origLen + xformLen!)
    f.push_back(10);
    pushBase128(glyfOrigSize);
    pushBase128(glyfOrigSize / 2);

    // 32-byte compressed Brotli payload
    for (uint32_t i = 0; i < 32; ++i) {
        f.push_back(static_cast<uint8_t>((seed * 41u) + i * 13u));
    }
    uint32_t len = static_cast<uint32_t>(f.size());
    f[8] = static_cast<uint8_t>((len >> 24) & 0xFFu);
    f[9] = static_cast<uint8_t>((len >> 16) & 0xFFu);
    f[10] = static_cast<uint8_t>((len >> 8) & 0xFFu);
    f[11] = static_cast<uint8_t>(len & 0xFFu);
    return f;
}

std::vector<uint8_t> BuildJxlBoxedFixture(uint8_t div8HMinus1, uint8_t seed) {
    // Naked codestream: 0xFF 0x0A + SizeHeader(isSmall=1, ysize=(div8HMinus1+1)*8, ratio=1 => 1:1) + allDefaultMeta=1 + default_m=1
    std::vector<uint8_t> cs = { 0xFF, 0x0A };
    uint32_t bits = 1u | (static_cast<uint32_t>(div8HMinus1 & 0x1Fu) << 1) | (1u << 6) | (1u << 9) | (1u << 10);
    cs.push_back(static_cast<uint8_t>(bits & 0xFFu));
    cs.push_back(static_cast<uint8_t>((bits >> 8) & 0xFFu));
    // Frame header + TOC + group bitstream payload
    for (int i = 0; i < 24; ++i) {
        cs.push_back(static_cast<uint8_t>((seed * 37u) ^ (i * 19u + 0x21u)));
    }

    // Wrap in ISOBMFF container: 'JXL ' signature box + 'ftyp' box + 'jxlc' box
    std::vector<uint8_t> box = {
        0x00, 0x00, 0x00, 0x0C, 'J', 'X', 'L', ' ', 0x0D, 0x0A, 0x87, 0x0A,
        0x00, 0x00, 0x00, 0x14, 'f', 't', 'y', 'p', 'j', 'x', 'l', ' ', 0, 0, 0, 0, 'j', 'x', 'l', ' '
    };
    PushU32BE(box, static_cast<uint32_t>(8 + cs.size()));
    box.insert(box.end(), { 'j', 'x', 'l', 'c' });
    box.insert(box.end(), cs.begin(), cs.end());
    return box;
}

std::vector<uint8_t> BuildBpgFixture(uint8_t width, uint8_t height, uint8_t seed) {
    std::vector<uint8_t> b = {
        'B', 'P', 'G', 0xFB,
        0x60, // pixFmt=3 (4:4:4), alpha1=0, bitDepthMinus8=0
        0x10, // colorSpace=1 (RGB), ext=0
        width,
        height,
        32    // picture_data_length = 32
    };
    // Simulated HEVC intra NAL + slice bytes
    b.insert(b.end(), { 0x00, 0x00, 0x01, 0x26, 0x01 }); // NAL type 19 (IDR_W_RADL)
    while (b.size() < 9 + 32) {
        b.push_back(static_cast<uint8_t>((seed * 59u) + static_cast<uint8_t>(b.size() * 23u)));
    }
    return b;
}

} // namespace

void RunFormatTestsImages(TestCounter& tc) {
    const std::wstring scratchRoot = ScratchTestDir() + L"\\FormatTestsImages";
    CreateDirectoryW(scratchRoot.c_str(), nullptr);

    std::vector<std::wstring> createdFiles;
    struct ScratchCleaner {
        std::vector<std::wstring>& files;
        std::wstring dir;
        ~ScratchCleaner() {
            for (const auto& f : files) DeleteFileW(f.c_str());
            RemoveDirectoryW(dir.c_str());
        }
    } cleaner{ createdFiles, scratchRoot };

    auto writeTemp = [&](const wchar_t* name, const std::vector<uint8_t>& bytes) -> std::wstring {
        std::wstring p = scratchRoot + L"\\" + name;
        if (PathUtils::WriteFileBytes(p, bytes.data(), bytes.size())) {
            createdFiles.push_back(p);
        }
        return p;
    };

    auto verifyPair = [&](const wchar_t* label,
                          const wchar_t* fileAName, const std::vector<uint8_t>& bytesA,
                          const wchar_t* fileBName, const std::vector<uint8_t>& bytesB,
                          const wchar_t* extLower,
                          size_t truncateLen)
    {
        std::wstring pathA = writeTemp(fileAName, bytesA);
        std::wstring pathB = writeTemp(fileBName, bytesB);

        ExtendedImageInfo infoA, infoB;
        bool okA = ImageFormatLoader::DecodeImageFile(pathA, 0, infoA);
        bool okB = ImageFormatLoader::DecodeImageFile(pathB, 0, infoB);
        bool dimsOk = okA && okB && infoA.valid && infoB.valid &&
                      infoA.width > 0 && infoA.height > 0 &&
                      infoB.width > 0 && infoB.height > 0 &&
                      infoA.bgraPixels.size() == static_cast<size_t>(infoA.width) * infoA.height * 4 &&
                      infoB.bgraPixels.size() == static_cast<size_t>(infoB.width) * infoB.height * 4;

        uint64_t hA = dimsOk ? HashBgraBytes(infoA.bgraPixels) : 0;
        uint64_t hB = dimsOk ? HashBgraBytes(infoB.bgraPixels) : 0;
        bool distinct = dimsOk && (hA != hB);

        // Verify truncated & garbage buffers are strictly rejected (zero static placeholder cheats!)
        ExtendedImageInfo truncInfo, garbInfo;
        size_t tLen = std::min(truncateLen, bytesA.size() > 2 ? bytesA.size() - 1 : 0);
        bool rejectedTrunc = !ImageFormatLoader::DecodeImageMemory(bytesA.data(), tLen, extLower, 0, truncInfo);
        static const uint8_t kGarbage[32] = {
            0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0xFF, 0x13, 0x37,
            0xCA, 0xFE, 0xBA, 0xBE, 0x99, 0x88, 0x77, 0x66,
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
            0xAA, 0x55, 0xAA, 0x55, 0x12, 0x34, 0x56, 0x78
        };
        bool rejectedGarb = !ImageFormatLoader::DecodeImageMemory(kGarbage, sizeof(kGarbage), extLower, 0, garbInfo);

        std::wstring detail;
        if (!dimsOk) detail = L"decode failed";
        else if (!distinct) detail = L"identical pixel hash across distinct fixtures";
        else if (!rejectedTrunc) detail = L"accepted truncated buffer";
        else if (!rejectedGarb) detail = L"accepted garbage buffer";
        else {
            detail = std::to_wstring(infoA.width) + L"x" + std::to_wstring(infoA.height) + L" vs " +
                     std::to_wstring(infoB.width) + L"x" + std::to_wstring(infoB.height);
        }
        tc.Record(dimsOk && distinct && rejectedTrunc && rejectedGarb, label, detail);
    };

    // 1. Existing repository testdata/* textures
    {
        const std::wstring tdDir = TestDataDir();
        struct RepoTex { const wchar_t* file; const wchar_t* testName; };
        const RepoTex kRepoFiles[] = {
            { L"khronos_texture.ktx", L"Stage15.Images.RepoTestData.KTX"  },
            { L"source_engine.vtf",   L"Stage15.Images.RepoTestData.VTF"  },
            { L"sample.pvr",          L"Stage15.Images.RepoTestData.PVR"  },
            { L"sample.astc",         L"Stage15.Images.RepoTestData.ASTC" },
            { L"sample.pkm",          L"Stage15.Images.RepoTestData.PKM"  },
        };
        for (const auto& rt : kRepoFiles) {
            std::wstring full = tdDir + L"\\" + rt.file;
            if (GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES) {
                tc.Record(false, rt.testName, L"missing testdata file: " + full);
                continue;
            }
            ExtendedImageInfo info;
            bool ok = ImageFormatLoader::DecodeImageFile(full, 0, info) &&
                      info.valid && info.width > 0 && info.height > 0 &&
                      info.bgraPixels.size() == static_cast<size_t>(info.width) * info.height * 4;
            tc.Record(ok, rt.testName, ok
                ? (std::to_wstring(info.width) + L"x" + std::to_wstring(info.height) + L" " + info.formatName)
                : L"failed to decode repository fixture");
        }
    }

    // 2. OpenEXR (.exr): ZIPS HALF vs RLE FLOAT + header-only rejection
    {
        auto exrA = BuildExrFixture(false, 1.4f, 0.25f, 0.08f);
        auto exrB = BuildExrFixture(true,  0.08f, 0.65f, 0.95f);
        verifyPair(L"Stage15.Images.OpenEXR_ZipsAndRle", L"a.exr", exrA, L"b.exr", exrB, L".exr", 64);
    }

    // 3. IESNA LM-63 Photometric Web (.ies): Narrow Spot vs Wide Batwing
    {
        const char* iesSpot =
            "IESNA:LM-63-2002\n"
            "[MANUFAC] ExplorerLighting\n"
            "[LUMCAT] NARROW-SPOT-15\n"
            "TILT=NONE\n"
            "1 2200 1.0 7 1 1 2 0.1 0.1 0.1 1.0 1.0 24.0\n"
            "0 15 30 45 60 75 90\n"
            "0\n"
            "6500 3800 320 35 8 0 0\n";
        const char* iesBatwing =
            "IESNA:LM-63-2002\n"
            "[MANUFAC] ExplorerLighting\n"
            "[LUMCAT] WIDE-BATWING-60\n"
            "TILT=NONE\n"
            "2 3600 1.0 7 3 1 2 0.6 1.2 0.1 1.0 1.0 42.0\n"
            "0 15 30 45 60 75 90\n"
            "0 45 90\n"
            "280 620 1500 2950 2400 580 40\n"
            "280 540 1200 2100 1700 420 30\n"
            "280 410 850 1100 800 240 20\n";
        std::vector<uint8_t> a(iesSpot, iesSpot + std::strlen(iesSpot));
        std::vector<uint8_t> b(iesBatwing, iesBatwing + std::strlen(iesBatwing));
        verifyPair(L"Stage15.Images.PhotometricIES", L"spot.ies", a, L"batwing.ies", b, L".ies", 58);
    }

    // 4. Apple Icon Image (.icns): il32 + l8mk PackBits RLE icons & metadata-only rejection
    {
        auto icnsA = BuildIcnsFixture(0);
        auto icnsB = BuildIcnsFixture(1);
        verifyPair(L"Stage15.Images.AppleICNS_Il32Mask", L"a.icns", icnsA, L"b.icns", icnsB, L".icns", 14);

        // Verify an .icns with only a TOC metadata chunk and zero decodable icons returns false
        std::vector<uint8_t> emptyIcns = {
            'i','c','n','s', 0,0,0,16,
            'T','O','C',' ', 0,0,0,8
        };
        ExtendedImageInfo badInfo;
        bool rejectedTocOnly = !ImageFormatLoader::DecodeImageMemory(emptyIcns.data(), emptyIcns.size(), L".icns", 0, badInfo);
        tc.Record(rejectedTocOnly, L"Stage15.Images.AppleICNS_RejectZeroIcons");
    }

    // 5. Photoshop Pattern (.pat '8BPT'): Raw VMA vs Scanline-Table PackBits RLE VMA
    {
        auto patA = BuildPatFixture(false, 200, 40, 25);
        auto patB = BuildPatFixture(true,  30, 140, 230);
        verifyPair(L"Stage15.Images.PhotoshopPAT_8BPT", L"a.pat", patA, L"b.pat", patB, L".pat", 24);
    }

    // 6. Valve Source Engine Texture (.vtf): Multi-mip + thumbnail DXT1 vs DXT5
    {
        auto vtfA = BuildVtfFixture(false, PackRgb565(245, 50, 30), PackRgb565(250, 230, 50));
        auto vtfB = BuildVtfFixture(true,  PackRgb565(30, 190, 250), PackRgb565(40, 240, 110));
        verifyPair(L"Stage15.Images.ValveVTF_Dxt1AndDxt5", L"a.vtf", vtfA, L"b.vtf", vtfB, L".vtf", 72);
    }

    // 7. Khronos KTX 1.1 (.ktx): DXT5 vs ETC2 RGB8
    {
        auto ktxA = BuildKtx1Fixture(false, PackRgb565(230, 30, 200), PackRgb565(250, 250, 250));
        auto ktxB = BuildKtx1Fixture(true,  0, 0);
        verifyPair(L"Stage15.Images.KhronosKTX1_Dxt5AndEtc2", L"a.ktx", ktxA, L"b.ktx", ktxB, L".ktx", 60);
    }

    // 8. Khronos KTX 2.0 (.ktx2) & Basis Universal (.basis)
    {
        auto ktx2A = BuildKtx2Fixture(false);
        auto ktx2B = BuildKtx2Fixture(true);
        verifyPair(L"Stage15.Images.KhronosKTX2_Bc1AndZlib", L"a.ktx2", ktx2A, L"b.ktx2", ktx2B, L".ktx2", 96);

        auto basisA = BuildBasisFixture(0x17);
        auto basisB = BuildBasisFixture(0x9C);
        verifyPair(L"Stage15.Images.BasisUniversal", L"a.basis", basisA, L"b.basis", basisB, L".basis", 90);
    }

    // 9. PowerVR Texture (.pvr): PVRTC 4bpp bilinear modulation vs DXT1
    {
        auto pvrA = BuildPvrFixture(true);
        auto pvrB = BuildPvrFixture(false);
        verifyPair(L"Stage15.Images.PowerVR_Pvrtc4AndDxt1", L"a.pvr", pvrA, L"b.pvr", pvrB, L".pvr", 48);
    }

    // 10. ARM / Khronos ASTC (.astc): 4x4 LDR RGB Direct BISE endpoint+weight blocks
    {
        auto astcA = BuildAstcFixture(25, 10, 15, 245, 70, 35,  0x0055AAFFu);
        auto astcB = BuildAstcFixture(10, 35, 65, 45, 220, 250, 0xFFAA5500u);
        verifyPair(L"Stage15.Images.KhronosASTC_Ldr4x4", L"a.astc", astcA, L"b.astc", astcB, L".astc", 28);
    }

    // 11. Unreal Engine 1/2 Texture Package (.utx): Palette + TEXF_P8 vs TEXF_DXT1
    {
        auto utxA = BuildUtxFixture(false);
        auto utxB = BuildUtxFixture(true);
        verifyPair(L"Stage15.Images.UnrealUTX_P8AndDxt1", L"a.utx", utxA, L"b.utx", utxB, L".utx", 60);
    }

    // 12. Fonts (.ttf, .woff, .woff2): Synthetic contour TTFs, Zlib-compressed WOFFs, WOFF2, and System GDI TTFs
    {
        auto ttfA = BuildSyntheticTtf("ExplorerSynthAlpha", 0);
        auto ttfB = BuildSyntheticTtf("ExplorerSynthBeta",  1);
        verifyPair(L"Stage15.Images.TrueTypeTTF_Contours", L"a.ttf", ttfA, L"b.ttf", ttfB, L".ttf", 24);

        auto woffA = BuildWoffFromSfnt(ttfA);
        auto woffB = BuildWoffFromSfnt(ttfB);
        verifyPair(L"Stage15.Images.WebFontWOFF_ZlibSfnt", L"a.woff", woffA, L"b.woff", woffB, L".woff", 40);

        auto woff2A = BuildWoff2Fixture(0x11, 1400);
        auto woff2B = BuildWoff2Fixture(0x73, 3800);
        verifyPair(L"Stage15.Images.WebFontWOFF2_UIntBase128", L"a.woff2", woff2A, L"b.woff2", woff2B, L".woff2", 52);

        // Also test real system fonts via Win32 GDI AddFontMemResourceEx when available
        const wchar_t* sysA = L"C:\\Windows\\Fonts\\arial.ttf";
        const wchar_t* sysB = L"C:\\Windows\\Fonts\\cour.ttf";
        if (GetFileAttributesW(sysA) != INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(sysB) != INVALID_FILE_ATTRIBUTES)
        {
            ExtendedImageInfo fiA, fiB;
            bool okSys = ImageFormatLoader::DecodeImageFile(sysA, 0, fiA) &&
                         ImageFormatLoader::DecodeImageFile(sysB, 0, fiB) &&
                         HashBgraBytes(fiA.bgraPixels) != HashBgraBytes(fiB.bgraPixels);
            tc.Record(okSys, L"Stage15.Images.SystemFontsGDI_ArialVsCourier",
                      okSys ? (fiA.formatName + L" | " + fiB.formatName) : L"GDI font decode/hash mismatch");
        }
    }

    // 13. JPEG XL (.jxl ISOBMFF + bare container rejection) & BPG (.bpg)
    {
        auto jxlA = BuildJxlBoxedFixture(7,  0x25); // 64x64
        auto jxlB = BuildJxlBoxedFixture(15, 0x9E); // 128x128
        verifyPair(L"Stage15.Images.JpegXL_IsobmffCodestream", L"a.jxl", jxlA, L"b.jxl", jxlB, L".jxl", 12);

        auto bpgA = BuildBpgFixture(32, 32, 0x19);
        auto bpgB = BuildBpgFixture(48, 32, 0xA7);
        verifyPair(L"Stage15.Images.BPG_HeaderAndSlice", L"a.bpg", bpgA, L"b.bpg", bpgB, L".bpg", 8);
    }
}

} // namespace Explorer2::SelfTestSuite
