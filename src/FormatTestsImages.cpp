// FormatTestsImages.cpp: self-tests of the image decoders (ImageFormatLoader, formats/), stage 15 ("15.images" runs these alone)
#include "SelfTestCommon.h"
#include "Types.h"
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

uint16_t ComputeBasisCrc16(const uint8_t* ptr, size_t len) {
    static constexpr std::array<uint16_t, 256> kTable = []() constexpr {
        std::array<uint16_t, 256> t{};
        for (uint32_t i = 0; i < 256u; ++i) {
            uint16_t v = static_cast<uint16_t>(i << 8);
            for (int b = 0; b < 8; ++b) {
                v = (v & 0x8000u) ? static_cast<uint16_t>((v << 1) ^ 0x1021u) : static_cast<uint16_t>(v << 1);
            }
            t[i] = v;
        }
        return t;
    }();
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc = static_cast<uint16_t>((crc << 8) ^ kTable[((crc >> 8) ^ ptr[i]) & 0xFFu]);
    }
    return static_cast<uint16_t>(~crc);
}

struct TestLsbBitWriter {
    std::vector<uint8_t> bytes;
    uint32_t cur = 0;
    uint32_t used = 0;

    void PutBits(uint32_t val, uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) {
            cur |= (((val >> i) & 1u) << used);
            if (++used == 8u) {
                bytes.push_back(static_cast<uint8_t>(cur));
                cur = 0u;
                used = 0u;
            }
        }
    }

    void AlignToByteZero() {
        if (used != 0u) {
            bytes.push_back(static_cast<uint8_t>(cur));
            cur = 0u;
            used = 0u;
        }
    }
};

// Build a genuine 8x8 (2x2 blocks) Khronos Basis Universal cUASTC4x4 (.basis) texture
// exercising Mode 8 (solid RGBA32), Mode 5 (RGB8 interpolated), Mode 14 (RGBA8 interpolated), and Mode 15 (LA8).
std::vector<uint8_t> BuildBasisFixture(bool variantB) {
    auto finishBlock16 = [](TestLsbBitWriter& bw) -> std::vector<uint8_t> {
        bw.AlignToByteZero();
        bw.bytes.resize(16, 0u);
        return bw.bytes;
    };

    std::vector<uint8_t> sliceData;
    sliceData.reserve(64);

    // Block (0,0): Mode 8 (0x17, 5 bits) Void-Extent Solid RGBA32
    {
        TestLsbBitWriter bw;
        bw.PutBits(0x17u, 5u);
        if (!variantB) {
            bw.PutBits(230u, 8u); bw.PutBits(40u, 8u); bw.PutBits(25u, 8u); bw.PutBits(255u, 8u);
        } else {
            bw.PutBits(20u, 8u); bw.PutBits(210u, 8u); bw.PutBits(140u, 8u); bw.PutBits(255u, 8u);
        }
        auto blk = finishBlock16(bw);
        sliceData.insert(sliceData.end(), blk.begin(), blk.end());
    }

    // Block (1,0): Mode 5 (0x0B, 5 bits + 15 hint bits + 6x8-bit RGB endpoints + 3-bit weights)
    {
        TestLsbBitWriter bw;
        bw.PutBits(0x0Bu, 5u);
        bw.PutBits(0u, 15u);
        uint8_t ep[6] = { 10, 250, 80, 220, 240, 30 };
        if (variantB) {
            uint8_t epB[6] = { 220, 40, 30, 240, 190, 250 };
            std::memcpy(ep, epB, 6);
        }
        for (int i = 0; i < 6; ++i) bw.PutBits(ep[i], 8u);
        bw.PutBits(0u, 2u); // anchor 0 -> weight 0 (exact e0 at (4,0))
        for (uint32_t i = 1; i < 15u; ++i) bw.PutBits(i & 7u, 3u);
        bw.PutBits(7u, 3u); // texel 15 -> weight 64 (exact e1 at (7,3))
        auto blk = finishBlock16(bw);
        sliceData.insert(sliceData.end(), blk.begin(), blk.end());
    }

    // Block (0,1): Mode 14 (0x0D, 5 bits + 23 hint bits + 8x8-bit RGBA endpoints + 2-bit weights)
    {
        TestLsbBitWriter bw;
        bw.PutBits(0x0Du, 5u);
        bw.PutBits(0u, 23u);
        uint8_t ep[8] = { 200, 40, 100, 200, 40, 100, 128, 255 };
        if (variantB) {
            uint8_t epB[8] = { 60, 220, 140, 180, 220, 50, 128, 255 };
            std::memcpy(ep, epB, 8);
        }
        for (int i = 0; i < 8; ++i) bw.PutBits(ep[i], 8u);
        bw.PutBits(0u, 1u); // anchor 0 -> weight 0 (exact e0 with alpha=128 at (0,4))
        for (uint32_t i = 1; i < 15u; ++i) bw.PutBits(i & 3u, 2u);
        bw.PutBits(3u, 2u); // texel 15 -> weight 64
        auto blk = finishBlock16(bw);
        sliceData.insert(sliceData.end(), blk.begin(), blk.end());
    }

    // Block (1,1): Mode 15 (0x05, 7 bits + 23 hint bits + 4x8-bit LA endpoints + 4-bit weights)
    {
        TestLsbBitWriter bw;
        bw.PutBits(0x05u, 7u);
        bw.PutBits(0u, 23u);
        uint8_t ep[4] = { static_cast<uint8_t>(variantB ? 210 : 30),
                          static_cast<uint8_t>(variantB ? 45  : 230), 255, 255 };
        for (int i = 0; i < 4; ++i) bw.PutBits(ep[i], 8u);
        bw.PutBits(0u, 3u); // anchor 0 -> weight 0
        for (uint32_t i = 1; i < 15u; ++i) bw.PutBits(i & 15u, 4u);
        bw.PutBits(15u, 4u); // texel 15 -> weight 64 (exact e1_L at (7,7))
        auto blk = finishBlock16(bw);
        sliceData.insert(sliceData.end(), blk.begin(), blk.end());
    }

    uint16_t sliceCrc = ComputeBasisCrc16(sliceData.data(), sliceData.size());
    std::vector<uint8_t> sd;
    PushU24LE(sd, 0);   // m_image_index
    sd.push_back(0);    // m_level_index
    sd.push_back(0);    // m_flags
    PushU16LE(sd, 8);   // m_orig_width
    PushU16LE(sd, 8);   // m_orig_height
    PushU16LE(sd, 2);   // m_num_blocks_x
    PushU16LE(sd, 2);   // m_num_blocks_y
    PushU32LE(sd, 100); // m_file_ofs = 77 + 23
    PushU32LE(sd, static_cast<uint32_t>(sliceData.size()));
    PushU16LE(sd, sliceCrc);

    std::vector<uint8_t> f(77, 0);
    f[0] = 's'; f[1] = 'B';
    f[2] = 0x13; f[3] = 0x00; // m_version
    f[4] = 77;   f[5] = 0;    // m_header_size = 77
    uint32_t dataSize = static_cast<uint32_t>(sd.size() + sliceData.size());
    f[8]  = static_cast<uint8_t>(dataSize & 0xFFu);
    f[9]  = static_cast<uint8_t>((dataSize >> 8) & 0xFFu);
    f[10] = static_cast<uint8_t>((dataSize >> 16) & 0xFFu);
    f[11] = static_cast<uint8_t>((dataSize >> 24) & 0xFFu);
    f[14] = 1;  // m_total_slices = 1
    f[17] = 1;  // m_total_images = 1
    f[20] = 1;  // m_tex_format = cUASTC4x4
    f[65] = 77; // m_slice_desc_file_ofs = 77

    f.insert(f.end(), sd.begin(), sd.end());
    f.insert(f.end(), sliceData.begin(), sliceData.end());

    uint16_t dataCrc = ComputeBasisCrc16(f.data() + 77, dataSize);
    f[12] = static_cast<uint8_t>(dataCrc & 0xFFu);
    f[13] = static_cast<uint8_t>((dataCrc >> 8) & 0xFFu);
    uint16_t hdrCrc = ComputeBasisCrc16(f.data() + 8, 77 - 8);
    f[6] = static_cast<uint8_t>(hdrCrc & 0xFFu);
    f[7] = static_cast<uint8_t>((hdrCrc >> 8) & 0xFFu);
    return f;
}

// Build a genuine 8x8 (2x2 blocks) Basis Universal cETC1S (.basis) texture with canonical Huffman codebooks & slice stream
std::vector<uint8_t> BuildBasisEtc1sFixture() {
    auto writeHuff1Bit = [](TestLsbBitWriter& bw, const std::vector<uint8_t>& codeSizes) {
        if (codeSizes.empty()) {
            bw.PutBits(0u, 14u);
            return;
        }
        bw.PutBits(static_cast<uint32_t>(codeSizes.size()), 14u);
        bw.PutBits(19u, 5u); // kClOrder[4]=0 -> len 1 ('0'), kClOrder[18]=1 -> len 1 ('1')
        for (uint32_t i = 0; i < 19u; ++i) {
            bw.PutBits((i == 4u || i == 18u) ? 1u : 0u, 3u);
        }
        for (uint8_t cs : codeSizes) {
            bw.PutBits(cs ? 1u : 0u, 1u);
        }
    };

    // 1. Endpoint codebook (numEndpoints = 2: Gold {28,28,4} inten 0, Cyan-Blue {2,20,29} inten 1)
    TestLsbBitWriter epBw;
    {
        std::vector<uint8_t> cd0(26, 0), cd1(21, 0), cd2(25, 0), idl(2, 1);
        cd0[25] = 1;               // single sym 25 -> '0'
        cd1[12] = 1; cd1[20] = 1;  // sym 12 -> '0', sym 20 -> '1'
        cd2[6]  = 1; cd2[24] = 1;  // sym 6  -> '0', sym 24 -> '1'
        writeHuff1Bit(epBw, cd0);
        writeHuff1Bit(epBw, cd1);
        writeHuff1Bit(epBw, cd2);
        writeHuff1Bit(epBw, idl);
        epBw.PutBits(0u, 1u); // grayscale = 0
        // Endpoint 0 from {16,16,16}, prevInten=0: dInten=0 ('0'), dR=12 ('0'->28), dG=12 ('0'->28), dB=20 ('1'->4)
        epBw.PutBits(0u, 1u); epBw.PutBits(0u, 1u); epBw.PutBits(0u, 1u); epBw.PutBits(1u, 1u);
        // Endpoint 1 from {28,28,4}, prevInten=0: dInten=1 ('1'), dR=6 ('0'->2), dG=24 ('1'->20), dB=25 ('0'->29)
        epBw.PutBits(1u, 1u); epBw.PutBits(0u, 1u); epBw.PutBits(1u, 1u); epBw.PutBits(0u, 1u);
        epBw.PutBits(0u, 16u);
        epBw.AlignToByteZero();
    }

    // 2. Selector codebook (numSelectors = 2, rawPal = 1)
    TestLsbBitWriter selBw;
    {
        selBw.PutBits(0u, 1u); // globalPal = 0
        selBw.PutBits(0u, 1u); // hybridPal = 0
        selBw.PutBits(1u, 1u); // rawPal = 1
        for (int j = 0; j < 4; ++j) selBw.PutBits(0xAAu, 8u); // Selector 0: idx 2 (+2 for inten 0)
        for (int j = 0; j < 4; ++j) selBw.PutBits(0xFFu, 8u); // Selector 1: idx 3 (+17 for inten 1)
        selBw.AlignToByteZero();
    }

    // 3. Huffman tables
    TestLsbBitWriter tblBw;
    {
        std::vector<uint8_t> epPred(96, 0), dEp(2, 1), selMdl(2, 1), emptyTbl;
        epPred[95] = 1; // 95 = 3 | (3<<2) | (1<<4) | (1<<6): row 0 delta, row 1 upper-endpoint prediction!
        writeHuff1Bit(tblBw, epPred);
        writeHuff1Bit(tblBw, dEp);
        writeHuff1Bit(tblBw, selMdl);
        writeHuff1Bit(tblBw, emptyTbl);
        tblBw.PutBits(0u, 13u); // selectorHistoryBufSize = 0
        tblBw.PutBits(0u, 16u);
        tblBw.AlignToByteZero();
    }

    // 4. Slice bitstream (2x2 blocks = 8x8)
    TestLsbBitWriter slBw;
    {
        slBw.PutBits(0u, 1u); // predBits = 95
        slBw.PutBits(0u, 1u); slBw.PutBits(0u, 1u); // (0,0): delta=0 -> ep0, sel=0
        slBw.PutBits(1u, 1u); slBw.PutBits(1u, 1u); // (1,0): delta=1 -> ep1, sel=1
        slBw.PutBits(0u, 1u);                       // (0,1): pred=1 (upper=ep0), sel=0
        slBw.PutBits(1u, 1u);                       // (1,1): pred=1 (upper=ep1), sel=1
        slBw.PutBits(0u, 24u);
        slBw.AlignToByteZero();
    }

    uint32_t sliceDescOfs = 77u;
    uint32_t epOfs   = sliceDescOfs + 23u;
    uint32_t selOfs  = epOfs  + static_cast<uint32_t>(epBw.bytes.size());
    uint32_t tblOfs  = selOfs + static_cast<uint32_t>(selBw.bytes.size());
    uint32_t slOfs   = tblOfs + static_cast<uint32_t>(tblBw.bytes.size());

    uint16_t sliceCrc = ComputeBasisCrc16(slBw.bytes.data(), slBw.bytes.size());
    std::vector<uint8_t> sd;
    PushU24LE(sd, 0);
    sd.push_back(0);
    sd.push_back(0);
    PushU16LE(sd, 8);
    PushU16LE(sd, 8);
    PushU16LE(sd, 2);
    PushU16LE(sd, 2);
    PushU32LE(sd, slOfs);
    PushU32LE(sd, static_cast<uint32_t>(slBw.bytes.size()));
    PushU16LE(sd, sliceCrc);

    std::vector<uint8_t> f(77, 0);
    f[0] = 's'; f[1] = 'B';
    f[2] = 0x13; f[3] = 0x00;
    f[4] = 77;   f[5] = 0;
    f[14] = 1;
    f[17] = 1;
    f[20] = 0; // cETC1S
    auto wr16 = [&](size_t o, uint16_t v) { f[o] = static_cast<uint8_t>(v & 0xFFu); f[o + 1] = static_cast<uint8_t>(v >> 8); };
    auto wr24 = [&](size_t o, uint32_t v) { f[o] = static_cast<uint8_t>(v & 0xFFu); f[o + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu); f[o + 2] = static_cast<uint8_t>((v >> 16) & 0xFFu); };
    auto wr32 = [&](size_t o, uint32_t v) { f[o] = static_cast<uint8_t>(v & 0xFFu); f[o + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu); f[o + 2] = static_cast<uint8_t>((v >> 16) & 0xFFu); f[o + 3] = static_cast<uint8_t>((v >> 24) & 0xFFu); };

    wr16(39, 2u); // numEndpoints
    wr32(41, epOfs);
    wr24(45, static_cast<uint32_t>(epBw.bytes.size()));
    wr16(48, 2u); // numSelectors
    wr32(50, selOfs);
    wr24(54, static_cast<uint32_t>(selBw.bytes.size()));
    wr32(57, tblOfs);
    wr32(61, static_cast<uint32_t>(tblBw.bytes.size()));
    wr32(65, sliceDescOfs);

    f.insert(f.end(), sd.begin(), sd.end());
    f.insert(f.end(), epBw.bytes.begin(), epBw.bytes.end());
    f.insert(f.end(), selBw.bytes.begin(), selBw.bytes.end());
    f.insert(f.end(), tblBw.bytes.begin(), tblBw.bytes.end());
    f.insert(f.end(), slBw.bytes.begin(), slBw.bytes.end());

    uint32_t dataSize = static_cast<uint32_t>(f.size() - 77u);
    wr32(8, dataSize);
    wr16(12, ComputeBasisCrc16(f.data() + 77u, dataSize));
    wr16(6,  ComputeBasisCrc16(f.data() + 8u, 77u - 8u));
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

uint32_t BitRevN(uint32_t v, uint32_t n) {
    uint32_t r = 0u;
    for (uint32_t i = 0; i < n; ++i) {
        r = (r << 1u) | ((v >> i) & 1u);
    }
    return r;
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

// RFC 7932 Brotli encoder using uncompressed meta-blocks + final empty meta-block
std::vector<uint8_t> CompressBrotliUncompressed(const std::vector<uint8_t>& raw) {
    TestLsbBitWriter bw;
    bw.PutBits(0u, 1u); // WBITS = 16
    size_t pos = 0u;
    while (pos < raw.size()) {
        size_t chunk = std::min<size_t>(raw.size() - pos, 65536u);
        bw.PutBits(0u, 1u); // ISLAST = 0
        bw.PutBits(0u, 2u); // MNIBBLES = 4 (16-bit mlen - 1)
        bw.PutBits(static_cast<uint32_t>(chunk - 1u), 16u);
        bw.PutBits(1u, 1u); // ISUNCOMPRESSED = 1
        bw.AlignToByteZero();
        bw.bytes.insert(bw.bytes.end(), raw.begin() + pos, raw.begin() + pos + chunk);
        pos += chunk;
    }
    bw.PutBits(1u, 1u); // ISLAST = 1
    bw.PutBits(1u, 1u); // ISLASTEMPTY = 1
    bw.AlignToByteZero();
    return bw.bytes;
}

// RFC 7932 Brotli encoder using a genuine canonical-Huffman compressed meta-block
std::vector<uint8_t> CompressBrotliHuffman(const std::vector<uint8_t>& raw) {
    if (raw.empty() || raw.size() > 65536u) {
        return CompressBrotliUncompressed(raw);
    }
    static constexpr uint32_t kInsBase[24] = {
        0, 1, 2, 3, 4, 5, 6, 8, 10, 14, 18, 26, 34, 50, 66, 98,
        130, 194, 322, 578, 1090, 2114, 6210, 22594
    };
    static constexpr uint8_t kInsExtra[24] = {
        0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5,
        6, 7, 8, 9, 10, 12, 14, 24
    };
    const uint32_t mlen = static_cast<uint32_t>(raw.size());
    uint32_t insCode = 0u;
    for (uint32_t i = 0; i < 24u; ++i) {
        if (mlen >= kInsBase[i]) insCode = i;
    }
    uint32_t iacSym = (insCode < 8u)  ? (128u + (insCode << 3u)) :
                      ((insCode < 16u) ? (256u + ((insCode - 8u) << 3u)) :
                                         (448u + ((insCode - 16u) << 3u)));

    TestLsbBitWriter bw;
    bw.PutBits(0u, 1u); // WBITS = 16
    bw.PutBits(1u, 1u); // ISLAST = 1
    bw.PutBits(0u, 1u); // ISLASTEMPTY = 0
    bw.PutBits(0u, 2u); // MNIBBLES = 4
    bw.PutBits(mlen - 1u, 16u);
    bw.PutBits(0u, 3u); // NBLTYPESL/I/D = 1
    bw.PutBits(0u, 2u); // NPOSTFIX = 0
    bw.PutBits(0u, 4u); // NDIRECT = 0
    bw.PutBits(0u, 2u); // ctxModes[0] = 0
    bw.PutBits(0u, 1u); // NTREESL = 1
    bw.PutBits(0u, 1u); // NTREESD = 1

    // Complex prefix code for 256 literals all of length 8 (clLengths[16]=1, clLengths[8]=1 -> Kraft space=0)
    bw.PutBits(0u, 2u); // hskip = 0
    for (int i = 0; i < 8; ++i) bw.PutBits(0u, 2u); // syms 1,2,3,4,0,5,17,6 -> len 0
    bw.PutBits(7u, 4u); // sym 16 -> len 1
    bw.PutBits(0u, 2u); // sym 7  -> len 0
    bw.PutBits(7u, 4u); // sym 8  -> len 1 (Kraft sum = 16+16 = 32)
    for (int s = 0; s < 256; ++s) bw.PutBits(0u, 1u); // '0' -> codeLenSym 8 for all 256 literals

    // Simple 1-symbol prefix codes for InsertAndCopy (iacSym) and Distance (0)
    bw.PutBits(1u, 2u); bw.PutBits(0u, 2u); bw.PutBits(iacSym, 10u);
    bw.PutBits(1u, 2u); bw.PutBits(0u, 2u); bw.PutBits(0u, 6u);

    // Emit insert extra bits (copyCode == 0 has 0 extra bits) followed by canonical 8-bit literals
    bw.PutBits(mlen - kInsBase[insCode], kInsExtra[insCode]);
    for (uint8_t b : raw) {
        bw.PutBits(BitRevN(b, 8u), 8u);
    }
    bw.AlignToByteZero();
    return bw.bytes;
}

// Wrap a valid SFNT .ttf into a spec-compliant WOFF 2.0 ('wOF2') container
std::vector<uint8_t> BuildWoff2FromSfnt(const std::vector<uint8_t>& sfnt, bool useHuffmanBrotli) {
    static const char kWoff2KnownTags[63][5] = {
        "cmap","head","hhea","hmtx","maxp","name","OS/2","post","cvt ","fpgm","glyf","loca","prep","CFF ","VORG","EBDT",
        "EBLC","gasp","hdmx","kern","LTSH","PCLT","VDMX","vhea","vmtx","BASE","GDEF","GPOS","GSUB","EBSC","JSTF","MATH",
        "CBDT","CBLC","COLR","CPAL","SVG ","sbix","acnt","avar","bdat","bloc","bsln","cvar","fdsc","feat","fmtx","fvar",
        "gvar","hsty","just","lcar","mort","morx","opbd","prop","trak","Zapf","Silf","Glat","Gloc","Feat","Sill"
    };

    uint16_t numTables = static_cast<uint16_t>((sfnt[4] << 8) | sfnt[5]);
    std::vector<uint8_t> dirBytes;
    std::vector<uint8_t> rawConcat;

    for (uint16_t i = 0; i < numTables; ++i) {
        const uint8_t* srec = sfnt.data() + 12u + static_cast<size_t>(i) * 16u;
        uint32_t off = (static_cast<uint32_t>(srec[8]) << 24)  | (static_cast<uint32_t>(srec[9]) << 16) |
                       (static_cast<uint32_t>(srec[10]) << 8)  | static_cast<uint32_t>(srec[11]);
        uint32_t len = (static_cast<uint32_t>(srec[12]) << 24) | (static_cast<uint32_t>(srec[13]) << 16) |
                       (static_cast<uint32_t>(srec[14]) << 8)  | static_cast<uint32_t>(srec[15]);

        uint8_t tagIdx = 63u;
        for (uint8_t k = 0; k < 63u; ++k) {
            if (std::memcmp(srec, kWoff2KnownTags[k], 4) == 0) {
                tagIdx = k;
                break;
            }
        }
        bool isGlyfOrLoca = (std::memcmp(srec, "glyf", 4) == 0 || std::memcmp(srec, "loca", 4) == 0);
        uint8_t flags = static_cast<uint8_t>((isGlyfOrLoca ? (3u << 6u) : 0u) | tagIdx);
        dirBytes.push_back(flags);
        if (tagIdx == 63u) {
            dirBytes.insert(dirBytes.end(), srec, srec + 4);
        }
        PushUIntBase128(dirBytes, len);
        rawConcat.insert(rawConcat.end(), sfnt.begin() + off, sfnt.begin() + off + len);
    }

    std::vector<uint8_t> comp = useHuffmanBrotli
        ? CompressBrotliHuffman(rawConcat)
        : CompressBrotliUncompressed(rawConcat);

    std::vector<uint8_t> f(48, 0);
    std::memcpy(f.data(), "wOF2", 4);
    std::memcpy(f.data() + 4, sfnt.data(), 4); // flavor
    f[12] = sfnt[4];
    f[13] = sfnt[5]; // numTables
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

// Build a genuine ISO/IEC 18181-1 JPEG XL Modular (encoding == 1) lossless codestream
std::vector<uint8_t> BuildJxlModularCodestream(
    uint32_t w, uint32_t h, bool hasAlpha, bool useRctYCoCg, bool useGradPred)
{
    const uint32_t numCh = hasAlpha ? 4u : 3u;
    std::vector<std::vector<int32_t>> ch(numCh, std::vector<int32_t>(static_cast<size_t>(w) * h, 0));

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            int32_t r = 0, g = 0, b = 0, a = 255;
            if (!hasAlpha) {
                bool right = (x >= w / 2u);
                bool bot   = (y >= h / 2u);
                if (!right && !bot)      { r = 220; g = 40;  b = 30;  }
                else if (right && !bot)  { r = 30;  g = 195; b = 75;  }
                else if (!right && bot)  { r = 35;  g = 95;  b = 235; }
                else                     { r = 240; g = 205; b = 45;  }
            } else {
                if (y < h / 2u) { r = 20;  g = 180; b = 220; a = 255; }
                else            { r = 200; g = 60;  b = 120; a = 128; }
            }
            if (useRctYCoCg) {
                int32_t co = r - b;
                int32_t t  = b + (co >> 1);
                int32_t cg = g - t;
                int32_t yv = t + (cg >> 1);
                ch[0][idx] = yv;
                ch[1][idx] = co;
                ch[2][idx] = cg;
            } else {
                ch[0][idx] = r;
                ch[1][idx] = g;
                ch[2][idx] = b;
            }
            if (hasAlpha) ch[3][idx] = a;
        }
    }

    // Build LfGlobal Modular section bitstream
    TestLsbBitWriter lf;
    lf.PutBits(1u, 1u); // lf_dequant_default = 1
    lf.PutBits(0u, 1u); // has_global_tree = 0
    lf.PutBits(0u, 1u); // gm_use_global_tree = 0
    lf.PutBits(1u, 1u); // wp_all_default = 1
    if (useRctYCoCg) {
        lf.PutBits(1u, 2u); // nb_transforms = 1 (sel=1)
        lf.PutBits(0u, 2u); // trId = 0 (RCT)
        lf.PutBits(0u, 2u); lf.PutBits(0u, 3u); // beginC = 0
        lf.PutBits(0u, 2u); // rctType = 6 (YCoCg, sel=0)
    } else {
        lf.PutBits(0u, 2u); // nb_transforms = 0 (sel=0)
    }

    // Local MA tree (1 leaf: predictor = 0 Zero or 5 Gradient)
    lf.PutBits(0u, 1u); // lz77 = 0
    lf.PutBits(1u, 1u); // simpleCluster = 1
    lf.PutBits(0u, 2u); // nbits = 0 -> 1 cluster across all 6 contexts
    lf.PutBits(1u, 1u); // usePrefixCode = 1
    lf.PutBits(4u, 4u); lf.PutBits(0u, 3u); lf.PutBits(0u, 3u); // splitExp = 4, msb = 0, lsb = 0
    if (!useGradPred) {
        lf.PutBits(0u, 1u); // alphaSize = 1 -> 0-bit single-symbol tree (all 5 leaf fields = 0)
    } else {
        lf.PutBits(1u, 1u); lf.PutBits(2u, 4u); lf.PutBits(1u, 2u); // alphaSize = 6
        lf.PutBits(1u, 2u); lf.PutBits(1u, 2u); // simple 2-symbol prefix code
        lf.PutBits(0u, 3u); lf.PutBits(5u, 3u); // sym0 = 0 ('0'), sym1 = 5 ('1')
        // Leaf fields: prop=0 ('0'), pred=5 ('1'), offset=0 ('0'), mulLog=0 ('0'), mulBits=0 ('0')
        lf.PutBits(0u, 1u); lf.PutBits(1u, 1u); lf.PutBits(0u, 1u); lf.PutBits(0u, 1u); lf.PutBits(0u, 1u);
    }

    // Local Histograms (1 context, splitExp = 0, 16 tokens all of Huffman length 4)
    lf.PutBits(0u, 1u); // lz77 = 0
    lf.PutBits(1u, 1u); // usePrefixCode = 1
    lf.PutBits(0u, 4u); // splitExp = 0 (msbBits=0, lsbBits=0)
    lf.PutBits(1u, 1u); lf.PutBits(3u, 4u); lf.PutBits(7u, 3u); // alphaSize = 16
    lf.PutBits(0u, 2u); // complex prefix code (hskip = 0)
    lf.PutBits(0u, 2u); lf.PutBits(0u, 2u); lf.PutBits(0u, 2u); // syms 1, 2, 3 -> len 0
    lf.PutBits(7u, 4u); // sym 4 -> len 1
    lf.PutBits(7u, 4u); // sym 0 -> len 1 (Kraft sum = 32)
    lf.PutBits(0xFFFFu, 16u); // 16 x '1' -> lengths[0..15] = 4

    for (uint32_t ci = 0; ci < numCh; ++ci) {
        const auto& px = ch[ci];
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                size_t idx = static_cast<size_t>(y) * w + x;
                int64_t predVal = 0;
                if (useGradPred) {
                    int64_t W  = (x > 0u) ? px[idx - 1u] : (y > 0u ? px[idx - w] : 0);
                    int64_t N  = (y > 0u) ? px[idx - w] : W;
                    int64_t NW = (x > 0u && y > 0u) ? px[idx - w - 1u] : W;
                    predVal = std::clamp<int64_t>(W + N - NW, std::min(W, N), std::max(W, N));
                }
                int32_t r = px[idx] - static_cast<int32_t>(predVal);
                uint32_t u = (r < 0) ? static_cast<uint32_t>(-2 * r - 1) : static_cast<uint32_t>(2 * r);
                if (u == 0u) {
                    lf.PutBits(0u, 4u);
                } else {
                    uint32_t k = 0u;
                    while ((1u << (k + 1u)) <= u) ++k;
                    uint32_t tok = 1u + k;
                    lf.PutBits(BitRevN(tok, 4u), 4u);
                    lf.PutBits(u & ((1u << k) - 1u), k);
                }
            }
        }
    }
    lf.AlignToByteZero();

    // Assemble main codestream header + FrameHeader + TOC + LfGlobal bytes
    TestLsbBitWriter cs;
    cs.PutBits(0xFFu, 8u);
    cs.PutBits(0x0Au, 8u);

    // SizeHeader (div8 = 0, explicit 9-bit h-1 and w-1)
    cs.PutBits(0u, 1u);
    cs.PutBits(0u, 2u); cs.PutBits(h - 1u, 9u);
    cs.PutBits(0u, 3u); // ratio = 0
    cs.PutBits(0u, 2u); cs.PutBits(w - 1u, 9u);

    // ImageMetadata (8-bit integer, xybEncoded = 0, sRGB ceAllDefault = 1, default_m = 1)
    cs.PutBits(0u, 1u); // all_default = 0
    cs.PutBits(0u, 1u); // extra_fields = 0
    cs.PutBits(0u, 1u); // bit_depth.fp = 0
    cs.PutBits(0u, 2u); // bits_per_sample = 8 (sel=0)
    cs.PutBits(1u, 1u); // modular_16_bit_buffer_sufficient = 1
    if (hasAlpha) {
        cs.PutBits(1u, 2u); // num_extra_channels = 1 (sel=1)
        cs.PutBits(1u, 1u); // d_alpha = 1 (default 8-bit straight alpha)
    } else {
        cs.PutBits(0u, 2u); // num_extra_channels = 0
    }
    cs.PutBits(0u, 1u); // xyb_encoded = 0
    cs.PutBits(1u, 1u); // ColourEncoding.all_default = 1 (sRGB)
    cs.PutBits(0u, 2u); // extensions = 0 (U64)
    cs.PutBits(1u, 1u); // default_m = 1
    cs.AlignToByteZero();

    // FrameHeader (RegularFrame, encoding = 1 kModular, 256x256 single group)
    cs.PutBits(0u, 1u); // all_default = 0
    cs.PutBits(0u, 2u); // frame_type = 0 (RegularFrame)
    cs.PutBits(1u, 1u); // encoding = 1 (kModular)
    cs.PutBits(0u, 2u); // flags = 0 (U64)
    cs.PutBits(0u, 1u); // do_ycbcr = 0
    cs.PutBits(0u, 2u); // upsampling = 1
    if (hasAlpha) cs.PutBits(0u, 2u); // ec_upsampling = 1
    cs.PutBits(1u, 2u); // group_size_shift = 1 (256x256)
    cs.PutBits(0u, 2u); // num_passes = 1
    cs.PutBits(0u, 1u); // have_crop = 0
    cs.PutBits(0u, 2u); // blend_mode = 0
    if (hasAlpha) cs.PutBits(0u, 2u); // ec_blend_mode = 0
    cs.PutBits(1u, 1u); // is_last = 1
    cs.PutBits(0u, 2u); // name_len = 0
    cs.PutBits(1u, 1u); // restoration_filter.all_default = 1
    cs.PutBits(0u, 2u); // extensions = 0 (U64)
    cs.AlignToByteZero();

    // TOC (1 entry)
    cs.PutBits(0u, 1u); // permuted = 0
    uint32_t tocLen = static_cast<uint32_t>(lf.bytes.size());
    if (tocLen < 1024u) {
        cs.PutBits(0u, 2u);
        cs.PutBits(tocLen, 10u);
    } else {
        cs.PutBits(1u, 2u);
        cs.PutBits(tocLen - 1024u, 14u);
    }
    cs.AlignToByteZero();
    cs.bytes.insert(cs.bytes.end(), lf.bytes.begin(), lf.bytes.end());
    return cs.bytes;
}

std::vector<uint8_t> WrapJxlIsobmff(const std::vector<uint8_t>& cs) {
    std::vector<uint8_t> box = {
        0x00, 0x00, 0x00, 0x0C, 'J', 'X', 'L', ' ', 0x0D, 0x0A, 0x87, 0x0A,
        0x00, 0x00, 0x00, 0x14, 'f', 't', 'y', 'p', 'j', 'x', 'l', ' ', 0, 0, 0, 0, 'j', 'x', 'l', ' '
    };
    PushU32BE(box, static_cast<uint32_t>(8u + cs.size()));
    box.insert(box.end(), { 'j', 'x', 'l', 'c' });
    box.insert(box.end(), cs.begin(), cs.end());
    return box;
}

std::vector<uint8_t> WrapJxlPartialBoxes(const std::vector<uint8_t>& cs, bool markLast) {
    std::vector<uint8_t> box = {
        0x00, 0x00, 0x00, 0x0C, 'J', 'X', 'L', ' ', 0x0D, 0x0A, 0x87, 0x0A,
        0x00, 0x00, 0x00, 0x14, 'f', 't', 'y', 'p', 'j', 'x', 'l', ' ', 0, 0, 0, 0, 'j', 'x', 'l', ' '
    };
    size_t p1 = cs.size() / 3u;
    size_t p2 = (cs.size() * 2u) / 3u;
    const size_t bounds[4] = { 0u, p1, p2, cs.size() };
    for (uint32_t seq = 0; seq < 3u; ++seq) {
        size_t segLen = bounds[seq + 1u] - bounds[seq];
        PushU32BE(box, static_cast<uint32_t>(12u + segLen));
        box.insert(box.end(), { 'j', 'x', 'l', 'p' });
        uint32_t seqWord = seq | ((seq == 2u && markLast) ? 0x80000000u : 0u);
        PushU32BE(box, seqWord);
        box.insert(box.end(), cs.begin() + bounds[seq], cs.begin() + bounds[seq + 1u]);
    }
    return box;
}

std::vector<uint8_t> WrapJxlBrob(const std::vector<uint8_t>& cs) {
    std::vector<uint8_t> box = {
        0x00, 0x00, 0x00, 0x0C, 'J', 'X', 'L', ' ', 0x0D, 0x0A, 0x87, 0x0A,
        0x00, 0x00, 0x00, 0x14, 'f', 't', 'y', 'p', 'j', 'x', 'l', ' ', 0, 0, 0, 0, 'j', 'x', 'l', ' '
    };
    std::vector<uint8_t> brCs = CompressBrotliHuffman(cs);
    PushU32BE(box, static_cast<uint32_t>(12u + brCs.size()));
    box.insert(box.end(), { 'b', 'r', 'o', 'b', 'j', 'x', 'l', 'c' });
    box.insert(box.end(), brCs.begin(), brCs.end());
    return box;
}

struct TestMsbBitWriter {
    std::vector<uint8_t> bytes;
    uint8_t cur = 0;
    uint32_t used = 0;

    void PutBit(uint32_t bit) {
        cur = static_cast<uint8_t>((cur << 1u) | (bit & 1u));
        if (++used == 8u) {
            bytes.push_back(cur);
            cur = 0u;
            used = 0u;
        }
    }

    void PutBits(uint32_t v, uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) {
            PutBit((v >> (n - 1u - i)) & 1u);
        }
    }

    void PutUe(uint32_t v) {
        uint32_t codeNum = v + 1u;
        uint32_t bits = 0u;
        uint32_t tmp = codeNum;
        while (tmp > 1u) { tmp >>= 1u; ++bits; }
        for (uint32_t i = 0; i < bits; ++i) PutBit(0u);
        PutBits(codeNum, bits + 1u);
    }

    void AlignZero() {
        while (used != 0u) PutBit(0u);
    }

    void PutRbspTrailing() {
        PutBit(1u);
        AlignZero();
    }
};

struct TestHevcCabacCtx {
    uint8_t state = 0;
    uint8_t mps = 1;
};

struct TestHevcCabacSegment {
    uint64_t low = 0;
    uint32_t range = 510;
    uint32_t numShifts = 0;

    void Renorm() {
        while (range < 256u) {
            range <<= 1u;
            low <<= 1u;
            ++numShifts;
        }
    }

    void EncodeDecision(TestHevcCabacCtx& ctx, uint32_t bin) {
        static constexpr uint8_t kRangeTabLps[64][4] = {
            {128,176,208,240},{128,167,197,227},{128,158,187,216},{123,150,178,205},
            {116,142,169,195},{111,135,160,185},{105,128,152,175},{100,122,144,166},
            { 95,116,137,158},{ 90,110,130,150},{ 85,104,123,142},{ 81, 99,117,135},
            { 77, 94,111,128},{ 73, 89,105,122},{ 69, 85,100,116},{ 66, 80, 95,110},
            { 62, 76, 90,104},{ 59, 72, 86, 99},{ 56, 69, 81, 94},{ 53, 65, 77, 89},
            { 51, 62, 73, 85},{ 48, 59, 69, 80},{ 46, 56, 66, 76},{ 43, 53, 63, 72},
            { 41, 50, 59, 69},{ 39, 48, 56, 65},{ 37, 45, 54, 62},{ 35, 43, 51, 59},
            { 33, 41, 48, 56},{ 32, 39, 46, 53},{ 30, 37, 43, 50},{ 29, 35, 41, 48},
            { 27, 33, 39, 45},{ 26, 31, 37, 43},{ 24, 30, 35, 41},{ 23, 28, 33, 39},
            { 22, 27, 32, 37},{ 21, 26, 30, 35},{ 20, 24, 29, 33},{ 19, 23, 27, 31},
            { 18, 22, 26, 30},{ 17, 21, 25, 28},{ 16, 20, 23, 27},{ 15, 19, 22, 25},
            { 14, 18, 21, 24},{ 14, 17, 20, 23},{ 13, 16, 19, 22},{ 12, 15, 18, 21},
            { 12, 14, 17, 20},{ 11, 14, 16, 19},{ 11, 13, 15, 18},{ 10, 12, 15, 17},
            { 10, 12, 14, 16},{  9, 11, 13, 15},{  9, 11, 12, 14},{  8, 10, 12, 14},
            {  8,  9, 11, 13},{  7,  9, 11, 12},{  7,  9, 10, 12},{  7,  8, 10, 11},
            {  6,  8,  9, 11},{  6,  7,  9, 10},{  6,  7,  8,  9},{  2,  2,  2,  2}
        };
        static constexpr uint8_t kTransIdxLps[64] = {
             0, 0, 1, 2, 2, 4, 4, 5, 6, 7, 8, 9, 9,11,11,12,
            13,13,15,15,16,16,18,18,19,19,21,21,22,22,23,24,
            24,25,26,26,27,27,28,29,29,30,30,30,31,32,32,33,
            33,33,34,34,35,35,35,36,36,36,37,37,37,38,38,63
        };
        uint32_t qIdx = (range >> 6u) & 3u;
        uint32_t lps = kRangeTabLps[ctx.state][qIdx];
        range -= lps;
        if (bin != ctx.mps) {
            low += range;
            range = lps;
            if (ctx.state == 0u) ctx.mps = 1u - ctx.mps;
            ctx.state = kTransIdxLps[ctx.state];
        } else {
            if (ctx.state < 62u) ++ctx.state;
        }
        Renorm();
    }

    void EncodeTerminate(uint32_t bin) {
        range -= 2u;
        if (bin != 0u) {
            low += range;
            range = 2u;
        }
        Renorm();
    }

    void FlushToWriter(TestMsbBitWriter& w, bool rbspStopAfter) const {
        uint64_t val = (low + 128ull) >> 7u;
        uint32_t totalBits = numShifts + 2u;
        for (uint32_t i = 0; i < totalBits; ++i) {
            w.PutBit(static_cast<uint32_t>((val >> (totalBits - 1u - i)) & 1ull));
        }
        if (rbspStopAfter) {
            w.PutRbspTrailing();
        } else {
            w.AlignZero(); // pcm_alignment_zero_bit
        }
    }
};

void AppendTestAnnexBNal(std::vector<uint8_t>& dst, uint8_t nalType, const std::vector<uint8_t>& rbsp) {
    dst.push_back(0x00u);
    dst.push_back(0x00u);
    dst.push_back(0x00u);
    dst.push_back(0x01u);
    dst.push_back(static_cast<uint8_t>((nalType & 0x3Fu) << 1u));
    dst.push_back(0x01u);
    uint32_t zeros = 0u;
    for (uint8_t b : rbsp) {
        if (zeros >= 2u && b <= 0x03u) {
            dst.push_back(0x03u);
            zeros = 0u;
        }
        dst.push_back(b);
        zeros = (b == 0u) ? (zeros + 1u) : 0u;
    }
}

// Build a genuine BPG (Fabrice Bellard HEVC Intra I_PCM + CABAC + PPS + MSPS) image
std::vector<uint8_t> BuildBpgFixture(uint32_t width, uint32_t height, bool useYcbcr) {
    // 1. BPG HEVC mini-SPS (chroma_format_idc = 3 4:4:4, 8x8 CTBs, pcm_enabled_flag = 1)
    TestMsbBitWriter msps;
    msps.PutUe(3u);  // chroma_format_idc = 3 (4:4:4)
    msps.PutBit(0u); // separate_colour_plane_flag = 0
    for (int i = 0; i < 8; ++i) msps.PutUe(0u); // bdLuma=0, bdChroma=0, poc=0, minCb=0 (8), diffCb=0, minTb=0, diffTb=0, trDepth=0
    msps.PutBit(0u); // scaling_list = 0
    msps.PutBit(0u); // amp = 0
    msps.PutBit(0u); // sao = 0
    msps.PutBit(1u); // pcm_enabled_flag = 1
    msps.PutBits(7u, 4u); // pcm_bd_luma_minus1 = 7
    msps.PutBits(7u, 4u); // pcm_bd_chroma_minus1 = 7
    msps.PutUe(0u);       // log2_min_pcm_cb_minus3 = 0 (8)
    msps.PutUe(0u);       // log2_diff_max_min_pcm_cb = 0
    msps.PutBit(1u);      // pcm_loop_filter_disabled = 1
    msps.PutBit(0u);      // strong_intra_smoothing = 0
    msps.PutBit(0u);      // sps_extension_present = 0
    msps.PutRbspTrailing();

    // 2. HEVC PPS (NAL 34, SliceQpY = 26)
    TestMsbBitWriter pps;
    pps.PutUe(0u); pps.PutUe(0u); // pps_id=0, sps_id=0
    pps.PutBit(0u); pps.PutBit(0u); pps.PutBits(0u, 3u); // dep_slice=0, output_flag=0, extra_bits=0
    pps.PutBit(0u); pps.PutBit(0u); // sign_hiding=0, cabac_init=0
    pps.PutUe(0u); pps.PutUe(0u);   // ref_l0=0, ref_l1=0
    pps.PutUe(0u);                  // init_qp_minus26 = 0 (se(v)=0)
    pps.PutBit(0u); pps.PutBit(0u); pps.PutBit(0u); // constrained=0, tr_skip=0, cu_qp_delta=0
    pps.PutUe(0u); pps.PutUe(0u);   // cb/cr_qp_offset = 0
    for (int i = 0; i < 10; ++i) pps.PutBit(0u); // chroma_qp..lists_mod = 0
    pps.PutUe(0u);  // parallel_merge_minus2 = 0
    pps.PutBit(0u); // slice_hdr_ext = 0
    pps.PutRbspTrailing();

    // 3. HEVC IDR_W_RADL Slice (NAL 19, slice_type = 2 Intra, CABAC + 8x8 I_PCM CUs)
    TestMsbBitWriter slice;
    slice.PutBit(1u); // first_slice_segment_in_pic_flag = 1
    slice.PutBit(0u); // no_output_of_prior_pics_flag = 0
    slice.PutUe(0u);  // slice_pic_parameter_set_id = 0
    slice.PutUe(2u);  // slice_type = 2 (I-slice)
    slice.PutUe(0u);  // slice_qp_delta = 0 (se(v)=0 -> SliceQpY = 26)
    slice.PutRbspTrailing(); // byte_alignment()

    const uint32_t ctbW = width / 8u;
    const uint32_t ctbH = height / 8u;
    const uint32_t numCtbs = ctbW * ctbH;
    TestHevcCabacCtx ctxPartMode{ 0u, 1u }; // initVal=184 at SliceQpY=26 -> pre=64 -> mps=1, state=0

    for (uint32_t ctbIdx = 0; ctbIdx < numCtbs; ++ctbIdx) {
        TestHevcCabacSegment seg;
        if (ctbIdx > 0u) {
            seg.EncodeTerminate(0u); // end_of_slice_segment_flag = 0 for previous CTU
        }
        seg.EncodeDecision(ctxPartMode, 1u); // part_mode = PART_2Nx2N (1)
        seg.EncodeTerminate(1u);             // pcm_flag = 1
        seg.FlushToWriter(slice, false);

        uint32_t x0 = (ctbIdx % ctbW) * 8u;
        uint32_t y0 = (ctbIdx / ctbW) * 8u;
        uint8_t p0[64], p1[64], p2[64];
        for (uint32_t py = 0; py < 8u; ++py) {
            for (uint32_t px = 0; px < 8u; ++px) {
                uint32_t x = x0 + px, y = y0 + py;
                size_t k = py * 8u + px;
                if (!useYcbcr) {
                    // BPG_CS_RGB stores GBR planes (plane0=G, plane1=B, plane2=R); quadrant (1,0) is pure 0,0,0 to exercise 00 00 03 RBSP escaping!
                    uint8_t r = 0, g = 0, b = 0;
                    if (x < 8u && y < 8u)        { r = 235; g = 45;  b = 25;  }
                    else if (x >= 8u && y < 8u)  { r = 0;   g = 0;   b = 0;   }
                    else if (x < 8u && y >= 8u)  { r = 35;  g = 210; b = 95;  }
                    else                         { r = 45;  g = 110; b = 245; }
                    p0[k] = g; p1[k] = b; p2[k] = r;
                } else {
                    if (x < width / 2u) { p0[k] = 180; p1[k] = 60;  p2[k] = 200; }
                    else                { p0[k] = 110; p1[k] = 200; p2[k] = 70;  }
                }
            }
        }
        slice.bytes.insert(slice.bytes.end(), p0, p0 + 64);
        slice.bytes.insert(slice.bytes.end(), p1, p1 + 64);
        slice.bytes.insert(slice.bytes.end(), p2, p2 + 64);
    }

    // Final CABAC segment after last CTU's pcm_sample(): end_of_slice_segment_flag = 1 + RBSP stop bit
    {
        TestHevcCabacSegment eosSeg;
        eosSeg.EncodeTerminate(1u);
        eosSeg.FlushToWriter(slice, true);
    }

    std::vector<uint8_t> picData;
    PushUIntBase128(picData, static_cast<uint32_t>(msps.bytes.size()));
    picData.insert(picData.end(), msps.bytes.begin(), msps.bytes.end());
    AppendTestAnnexBNal(picData, 34u, pps.bytes);
    AppendTestAnnexBNal(picData, 19u, slice.bytes);

    std::vector<uint8_t> b = {
        'B', 'P', 'G', 0xFB,
        0x60u, // pixFmt=3 (4:4:4), alpha1=0, bitDepthMinus8=0
        static_cast<uint8_t>(useYcbcr ? 0x00u : 0x10u) // colorSpace: 0=YCbCr BT.601 full-range, 1=RGB (GBR)
    };
    PushUIntBase128(b, width);
    PushUIntBase128(b, height);
    PushUIntBase128(b, static_cast<uint32_t>(picData.size()));
    b.insert(b.end(), picData.begin(), picData.end());
    return b;
}

void AppendDcmShortTag(std::vector<uint8_t>& d, uint16_t grp, uint16_t elem, const char vr[2], const void* pay, uint16_t len) {
    PushU16LE(d, grp);
    PushU16LE(d, elem);
    d.push_back(static_cast<uint8_t>(vr[0]));
    d.push_back(static_cast<uint8_t>(vr[1]));
    uint16_t padded = static_cast<uint16_t>(len + (len & 1u));
    PushU16LE(d, padded);
    const auto* u = static_cast<const uint8_t*>(pay);
    if (len > 0) d.insert(d.end(), u, u + len);
    if (len & 1u) d.push_back((vr[0] == 'U' && vr[1] == 'I') ? 0u : static_cast<uint8_t>(' '));
}

void AppendDcmUS(std::vector<uint8_t>& d, uint16_t grp, uint16_t elem, uint16_t val) {
    uint8_t b[2] = { static_cast<uint8_t>(val & 0xFFu), static_cast<uint8_t>(val >> 8) };
    AppendDcmShortTag(d, grp, elem, "US", b, 2);
}

void AppendDcmStr(std::vector<uint8_t>& d, uint16_t grp, uint16_t elem, const char vr[2], const char* s) {
    AppendDcmShortTag(d, grp, elem, vr, s, static_cast<uint16_t>(std::strlen(s)));
}

void AppendDcmLongTag(std::vector<uint8_t>& d, uint16_t grp, uint16_t elem, const char vr[2], uint32_t len) {
    PushU16LE(d, grp);
    PushU16LE(d, elem);
    d.push_back(static_cast<uint8_t>(vr[0]));
    d.push_back(static_cast<uint8_t>(vr[1]));
    PushU16LE(d, 0);
    PushU32LE(d, len);
}

// PackBits RLE encoder for DICOM RLE Lossless byte planes
std::vector<uint8_t> EncodeDicomPackBits(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out;
    size_t i = 0;
    while (i < raw.size()) {
        size_t run = 1;
        while (i + run < raw.size() && run < 128 && raw[i + run] == raw[i]) ++run;
        if (run >= 3) {
            out.push_back(static_cast<uint8_t>(static_cast<int8_t>(1 - static_cast<int>(run))));
            out.push_back(raw[i]);
            i += run;
        } else {
            size_t start = i;
            size_t lit = 0;
            while (i < raw.size() && lit < 128) {
                if (i + 2 < raw.size() && raw[i] == raw[i + 1] && raw[i] == raw[i + 2]) break;
                ++i;
                ++lit;
            }
            out.push_back(static_cast<uint8_t>(lit - 1));
            out.insert(out.end(), raw.begin() + start, raw.begin() + start + lit);
        }
    }
    if (out.size() & 1u) out.push_back(0x80u); // Even byte length pad (PackBits noop)
    return out;
}

// Clinical CT slice with Undefined-Length SQ before (0028,*), signed 12-bit PixelRepresentation=1, RescaleIntercept=-1024, Window=40/400
std::vector<uint8_t> BuildDicomCtSqFixture() {
    std::vector<uint8_t> f(128, 0);
    f.insert(f.end(), { 'D', 'I', 'C', 'M' });

    AppendDcmStr(f, 0x0002, 0x0010, "UI", "1.2.840.10008.1.2.1"); // Explicit VR LE

    // Undefined-Length ReferencedImageSequence (0008,1140) with Undefined-Length Item
    AppendDcmLongTag(f, 0x0008, 0x1140, "SQ", 0xFFFFFFFFu);
    PushU16LE(f, 0xFFFEu); PushU16LE(f, 0xE000u); PushU32LE(f, 0xFFFFFFFFu); // Item start
    AppendDcmStr(f, 0x0008, 0x1150, "UI", "1.2.840.10008.5.1.4.1.1.2");
    AppendDcmStr(f, 0x0008, 0x1155, "UI", "1.2.840.113619.2.55.3.99");
    PushU16LE(f, 0xFFFEu); PushU16LE(f, 0xE00Du); PushU32LE(f, 0u);          // Item Delimitation
    PushU16LE(f, 0xFFFEu); PushU16LE(f, 0xE0DDu); PushU32LE(f, 0u);          // Sequence Delimitation

    AppendDcmUS(f, 0x0028, 0x0002, 1);   // SamplesPerPixel = 1
    AppendDcmStr(f, 0x0028, 0x0004, "CS", "MONOCHROME2");
    AppendDcmUS(f, 0x0028, 0x0010, 8);   // Rows = 8
    AppendDcmUS(f, 0x0028, 0x0011, 8);   // Columns = 8
    AppendDcmUS(f, 0x0028, 0x0100, 16);  // BitsAllocated = 16
    AppendDcmUS(f, 0x0028, 0x0101, 12);  // BitsStored = 12
    AppendDcmUS(f, 0x0028, 0x0102, 11);  // HighBit = 11
    AppendDcmUS(f, 0x0028, 0x0103, 1);   // PixelRepresentation = 1 (signed two's complement)
    AppendDcmStr(f, 0x0028, 0x1050, "DS", "40");
    AppendDcmStr(f, 0x0028, 0x1051, "DS", "400");
    AppendDcmStr(f, 0x0028, 0x1052, "DS", "-1024");
    AppendDcmStr(f, 0x0028, 0x1053, "DS", "1");

    std::vector<int16_t> samples(64, 1024); // 1024 - 1024 = 0 HU
    samples[0] = -1000; // Negative 12-bit signed (-1000 -> -2024 HU -> dark 0, NOT bright white!)
    samples[1] = 24;    // 24 - 1024 = -1000 HU (CT air -> dark 0!)
    samples[4 * 8 + 4] = 1064; // 1064 - 1024 = +40 HU (exact WindowCenter -> mid-gray ~128)
    samples[7 * 8 + 7] = 1524; // 1524 - 1024 = +500 HU (cortical bone -> bright white 255)
    for (int i = 2; i < 64; ++i) {
        if (i != (4 * 8 + 4) && i != (7 * 8 + 7)) {
            samples[static_cast<size_t>(i)] = static_cast<int16_t>(900 + i * 5);
        }
    }

    AppendDcmLongTag(f, 0x7FE0, 0x0010, "OW", 128u);
    for (int16_t s : samples) {
        PushU16LE(f, static_cast<uint16_t>(s));
    }
    return f;
}

// Encapsulated RLE Lossless (1.2.840.10008.1.2.5) 16-bit 8x8 DICOM image
std::vector<uint8_t> BuildDicomRleLosslessFixture() {
    std::vector<uint8_t> f(128, 0);
    f.insert(f.end(), { 'D', 'I', 'C', 'M' });

    AppendDcmStr(f, 0x0002, 0x0010, "UI", "1.2.840.10008.1.2.5"); // RLE Lossless
    AppendDcmUS(f, 0x0028, 0x0002, 1);
    AppendDcmStr(f, 0x0028, 0x0004, "CS", "MONOCHROME2");
    AppendDcmUS(f, 0x0028, 0x0010, 8);
    AppendDcmUS(f, 0x0028, 0x0011, 8);
    AppendDcmUS(f, 0x0028, 0x0100, 16);
    AppendDcmUS(f, 0x0028, 0x0101, 16);
    AppendDcmUS(f, 0x0028, 0x0102, 15);
    AppendDcmUS(f, 0x0028, 0x0103, 0);
    AppendDcmStr(f, 0x0028, 0x1050, "DS", "2000");
    AppendDcmStr(f, 0x0028, 0x1051, "DS", "2000");

    std::vector<uint8_t> msbPlane(64);
    std::vector<uint8_t> lsbPlane(64);
    for (size_t i = 0; i < 64; ++i) {
        uint16_t v = (i < 16) ? 800u : static_cast<uint16_t>(1100u + i * 32u);
        msbPlane[i] = static_cast<uint8_t>(v >> 8);
        lsbPlane[i] = static_cast<uint8_t>(v & 0xFFu);
    }
    std::vector<uint8_t> rleSeg0 = EncodeDicomPackBits(msbPlane);
    std::vector<uint8_t> rleSeg1 = EncodeDicomPackBits(lsbPlane);

    std::vector<uint8_t> rleFrag(64, 0);
    PushU32LE(rleFrag, 0); rleFrag.resize(64, 0);
    // 64-byte RLE Header: numSegments=2, offset0=64, offset1=64+rleSeg0.size()
    rleFrag[0] = 2;
    uint32_t off0 = 64u;
    uint32_t off1 = 64u + static_cast<uint32_t>(rleSeg0.size());
    std::memcpy(rleFrag.data() + 4, &off0, 4);
    std::memcpy(rleFrag.data() + 8, &off1, 4);
    rleFrag.insert(rleFrag.end(), rleSeg0.begin(), rleSeg0.end());
    rleFrag.insert(rleFrag.end(), rleSeg1.begin(), rleSeg1.end());

    // Encapsulated PixelData (7FE0,0010) OB with Undefined Length
    AppendDcmLongTag(f, 0x7FE0, 0x0010, "OB", 0xFFFFFFFFu);
    PushU16LE(f, 0xFFFEu); PushU16LE(f, 0xE000u); PushU32LE(f, 0u); // Empty BOT
    PushU16LE(f, 0xFFFEu); PushU16LE(f, 0xE000u); PushU32LE(f, static_cast<uint32_t>(rleFrag.size()));
    f.insert(f.end(), rleFrag.begin(), rleFrag.end());
    PushU16LE(f, 0xFFFEu); PushU16LE(f, 0xE0DDu); PushU32LE(f, 0u); // Sequence Delimitation
    return f;
}

// Build a 16x16 4bpp paletted CUR icon payload with 1bpp AND transparency mask
std::vector<uint8_t> BuildAniCur4BppPayload(uint8_t rCol, uint8_t gCol, uint8_t bCol, bool transparentCorners) {
    const uint32_t w = 16, h = 16;
    std::vector<uint8_t> dib;
    PushU32LE(dib, 40u);        // biSize
    PushU32LE(dib, w);          // biWidth
    PushU32LE(dib, h * 2u);     // biHeight = 2 * h (XOR + AND)
    PushU16LE(dib, 1u);         // biPlanes
    PushU16LE(dib, 4u);         // biBitCount = 4bpp
    PushU32LE(dib, 0u);         // BI_RGB
    PushU32LE(dib, 0u);         // biSizeImage
    PushU32LE(dib, 0u); PushU32LE(dib, 0u);
    PushU32LE(dib, 16u);        // biClrUsed = 16
    PushU32LE(dib, 0u);

    // 16-entry RGBQUAD palette: idx 0 = black, idx 1 = primary color, idx 2 = secondary accent
    for (int i = 0; i < 16; ++i) {
        if (i == 1) {
            dib.insert(dib.end(), { bCol, gCol, rCol, 0u });
        } else if (i == 2) {
            dib.insert(dib.end(), { static_cast<uint8_t>(255u - bCol), static_cast<uint8_t>(gCol / 2u), rCol, 0u });
        } else {
            dib.insert(dib.end(), { 0u, 0u, 0u, 0u });
        }
    }

    // Bottom-up 4bpp XOR rows (stride = ((16 * 4 + 31)/32)*4 = 8 bytes/row)
    for (uint32_t row = 0; row < h; ++row) {
        for (uint32_t bx = 0; bx < 8; ++bx) {
            uint8_t idx0 = ((row + bx) & 1u) ? 1u : 2u;
            uint8_t idx1 = 1u;
            if (row == 7u && bx == 4u) { idx0 = 1u; idx1 = 1u; } // Ensure center (8,8) uses palette idx 1
            dib.push_back(static_cast<uint8_t>((idx0 << 4) | idx1));
        }
    }

    // Bottom-up 1bpp AND mask rows (stride = ((16 + 31)/32)*4 = 4 bytes/row)
    for (uint32_t row = 0; row < h; ++row) {
        uint32_t topY = h - 1u - row;
        uint8_t b0 = 0u, b1 = 0u;
        for (uint32_t x = 0; x < 16; ++x) {
            bool transp = transparentCorners && ((x < 2u || x >= 14u) && (topY < 2u || topY >= 14u));
            if (transp) {
                if (x < 8u) b0 |= static_cast<uint8_t>(1u << (7u - x));
                else b1 |= static_cast<uint8_t>(1u << (15u - x));
            }
        }
        dib.insert(dib.end(), { b0, b1, 0u, 0u });
    }

    std::vector<uint8_t> cur;
    PushU16LE(cur, 0u); // reserved
    PushU16LE(cur, 2u); // type = 2 (CUR)
    PushU16LE(cur, 1u); // count = 1
    cur.push_back(static_cast<uint8_t>(w));
    cur.push_back(static_cast<uint8_t>(h));
    cur.push_back(16u);
    cur.push_back(0u);
    PushU16LE(cur, 8u); // hotspotX
    PushU16LE(cur, 8u); // hotspotY
    PushU32LE(cur, static_cast<uint32_t>(dib.size()));
    PushU32LE(cur, 22u);
    cur.insert(cur.end(), dib.begin(), dib.end());
    return cur;
}

// RIFF ACON with LIST 'fram' -> 2 x 4bpp CUR frames + 'seq ' selecting Frame 1
std::vector<uint8_t> BuildAniSeqFixture() {
    auto frame0Red   = BuildAniCur4BppPayload(250u, 15u, 15u, false);
    auto frame1Green = BuildAniCur4BppPayload(10u, 230u, 20u, true);

    auto appendChunk = [](std::vector<uint8_t>& dst, const char id[4], const std::vector<uint8_t>& pay) {
        dst.insert(dst.end(), id, id + 4);
        PushU32LE(dst, static_cast<uint32_t>(pay.size()));
        dst.insert(dst.end(), pay.begin(), pay.end());
        if (pay.size() & 1u) dst.push_back(0u);
    };

    std::vector<uint8_t> anih;
    PushU32LE(anih, 36u); // cbSize
    PushU32LE(anih, 2u);  // cFrames = 2
    PushU32LE(anih, 2u);  // cSteps = 2
    PushU32LE(anih, 16u); // cx = 16
    PushU32LE(anih, 16u); // cy = 16
    PushU32LE(anih, 4u);  // cBitCount
    PushU32LE(anih, 1u);  // cPlanes
    PushU32LE(anih, 6u);  // jiffRate
    PushU32LE(anih, 3u);  // AF_ICON | AF_SEQUENCE

    std::vector<uint8_t> seq;
    PushU32LE(seq, 1u); // Step 0 -> Frame 1 (green + transparent corners!)
    PushU32LE(seq, 0u); // Step 1 -> Frame 0 (red)

    std::vector<uint8_t> framBody = { 'f', 'r', 'a', 'm' };
    appendChunk(framBody, "icon", frame0Red);
    appendChunk(framBody, "icon", frame1Green);

    std::vector<uint8_t> body = { 'A', 'C', 'O', 'N' };
    appendChunk(body, "anih", anih);
    appendChunk(body, "seq ", seq);
    appendChunk(body, "LIST", framBody);

    std::vector<uint8_t> riff = { 'R', 'I', 'F', 'F' };
    PushU32LE(riff, static_cast<uint32_t>(body.size()));
    riff.insert(riff.end(), body.begin(), body.end());
    return riff;
}

// RIFF ACON with 24bpp BGR ICO frame inside LIST 'fram'
std::vector<uint8_t> BuildAni24BppFixture() {
    const uint32_t w = 16, h = 16;
    std::vector<uint8_t> dib;
    PushU32LE(dib, 40u);
    PushU32LE(dib, w);
    PushU32LE(dib, h * 2u);
    PushU16LE(dib, 1u);
    PushU16LE(dib, 24u); // 24bpp BGR
    PushU32LE(dib, 0u);
    PushU32LE(dib, 0u);
    PushU32LE(dib, 0u); PushU32LE(dib, 0u);
    PushU32LE(dib, 0u); PushU32LE(dib, 0u);

    // 24bpp row stride = ((16 * 24 + 31)/32)*4 = 48 bytes
    for (uint32_t row = 0; row < h; ++row) {
        for (uint32_t x = 0; x < w; ++x) {
            dib.push_back(static_cast<uint8_t>(220u - x * 8u)); // B
            dib.push_back(static_cast<uint8_t>(40u + row * 10u)); // G
            dib.push_back(static_cast<uint8_t>(30u + x * 12u)); // R
        }
    }
    // Opaque 1bpp AND mask (4 bytes/row)
    dib.insert(dib.end(), h * 4u, 0u);

    std::vector<uint8_t> ico;
    PushU16LE(ico, 0u);
    PushU16LE(ico, 1u); // type = 1 (ICO)
    PushU16LE(ico, 1u);
    ico.push_back(16u); ico.push_back(16u); ico.push_back(0u); ico.push_back(0u);
    PushU16LE(ico, 1u); PushU16LE(ico, 24u);
    PushU32LE(ico, static_cast<uint32_t>(dib.size()));
    PushU32LE(ico, 22u);
    ico.insert(ico.end(), dib.begin(), dib.end());

    std::vector<uint8_t> anih(36, 0);
    anih[0] = 36; anih[4] = 1; anih[8] = 1; anih[12] = 16; anih[16] = 16; anih[20] = 24; anih[24] = 1; anih[28] = 6; anih[32] = 1;

    std::vector<uint8_t> fram = { 'f', 'r', 'a', 'm', 'i', 'c', 'o', 'n' };
    PushU32LE(fram, static_cast<uint32_t>(ico.size()));
    fram.insert(fram.end(), ico.begin(), ico.end());
    if (ico.size() & 1u) fram.push_back(0u);

    std::vector<uint8_t> body = { 'A', 'C', 'O', 'N', 'a', 'n', 'i', 'h' };
    PushU32LE(body, 36u);
    body.insert(body.end(), anih.begin(), anih.end());
    body.insert(body.end(), { 'L', 'I', 'S', 'T' });
    PushU32LE(body, static_cast<uint32_t>(fram.size()));
    body.insert(body.end(), fram.begin(), fram.end());

    std::vector<uint8_t> riff = { 'R', 'I', 'F', 'F' };
    PushU32LE(riff, static_cast<uint32_t>(body.size()));
    riff.insert(riff.end(), body.begin(), body.end());
    return riff;
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

    // Colour LUT card: the right half is the left half put through the LUT (same input colours).
    // Identity 3D cube -> halves equal; inverting .cube -> halves complementary; 1D LUT; 12-bit .3dl.
    {
        auto halvesMatch = [](const ExtendedImageInfo& im, bool inverted) {
            if (!im.valid || im.width != 256 || im.height != 160 || im.bgraPixels.size() != 256u * 160u * 4u) return false;
            int worst = 0;
            for (uint32_t y = 20; y < 160; ++y) {        // below the labels
                for (uint32_t x = 0; x < 128; ++x) {
                    const uint8_t* a = &im.bgraPixels[(static_cast<size_t>(y) * 256 + x) * 4];
                    const uint8_t* b = &im.bgraPixels[(static_cast<size_t>(y) * 256 + x + 128) * 4];
                    for (int c = 0; c < 3; ++c) {
                        int want = inverted ? 255 - a[c] : a[c];
                        worst = std::max(worst, std::abs(want - static_cast<int>(b[c])));
                    }
                }
            }
            return worst <= 2;
        };
        const char* identity = "TITLE \"Id\"\nLUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
        const char* invert = "LUT_3D_SIZE 2\n1 1 1\n0 1 1\n1 0 1\n0 0 1\n1 1 0\n0 1 0\n1 0 0\n0 0 0\n";
        const char* invert1d = "LUT_1D_SIZE 3\n1 1 1\n0.5 0.5 0.5\n0 0 0\n";
        // .3dl: mesh line, 12-bit values, blue fastest; this table inverts colours
        std::string lut3dl = "0 4095\n";
        for (int r = 0; r < 2; ++r) for (int g = 0; g < 2; ++g) for (int b = 0; b < 2; ++b)
            lut3dl += std::to_string((1 - r) * 4095) + " " + std::to_string((1 - g) * 4095) + " " + std::to_string((1 - b) * 4095) + "\n";
        ExtendedImageInfo iId, iInv, i1d, i3dl;
        bool okId = ImageFormatLoader::DecodeImageMemory(reinterpret_cast<const uint8_t*>(identity), std::strlen(identity), L".cube", 0, iId) && halvesMatch(iId, false);
        bool okInv = ImageFormatLoader::DecodeImageMemory(reinterpret_cast<const uint8_t*>(invert), std::strlen(invert), L".cube", 0, iInv) && halvesMatch(iInv, true);
        bool ok1d = ImageFormatLoader::DecodeImageMemory(reinterpret_cast<const uint8_t*>(invert1d), std::strlen(invert1d), L".cube", 0, i1d) && halvesMatch(i1d, true);
        bool ok3dl = ImageFormatLoader::DecodeImageMemory(reinterpret_cast<const uint8_t*>(lut3dl.data()), lut3dl.size(), L".3dl", 0, i3dl) && halvesMatch(i3dl, true);
        tc.Record(okId && okInv && ok1d && ok3dl, L"Images.ColorLut.SameColoursBeforeAfter",
                  L"id=" + std::to_wstring(okId) + L" inv=" + std::to_wstring(okInv) + L" 1d=" + std::to_wstring(ok1d) + L" 3dl=" + std::to_wstring(ok3dl));
    }

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

    // 8. Khronos KTX 2.0 (.ktx2) & Basis Universal (.basis UASTC 4x4 + ETC1S + negative CRC/mode checks)
    {
        auto ktx2A = BuildKtx2Fixture(false);
        auto ktx2B = BuildKtx2Fixture(true);
        verifyPair(L"Stage15.Images.KhronosKTX2_Bc1AndZlib", L"a.ktx2", ktx2A, L"b.ktx2", ktx2B, L".ktx2", 96);

        auto basisUastcA = BuildBasisFixture(false);
        auto basisUastcB = BuildBasisFixture(true);
        verifyPair(L"Stage15.Images.BasisUniversal_Uastc4x4", L"a.basis", basisUastcA, L"b.basis", basisUastcB, L".basis", 90);

        auto basisEtc1s = BuildBasisEtc1sFixture();
        ExtendedImageInfo uInfo, eInfo;
        bool uOk = ImageFormatLoader::DecodeImageMemory(basisUastcA.data(), basisUastcA.size(), L".basis", 0, uInfo) &&
                   uInfo.valid && uInfo.width == 8 && uInfo.height == 8 && uInfo.bgraPixels.size() == 256;
        bool eOk = ImageFormatLoader::DecodeImageMemory(basisEtc1s.data(), basisEtc1s.size(), L".basis", 0, eInfo) &&
                   eInfo.valid && eInfo.width == 8 && eInfo.height == 8 && eInfo.bgraPixels.size() == 256;

        bool pxExact = false;
        if (uOk && eOk) {
            auto chkPx = [](const std::vector<uint8_t>& px, uint32_t x, uint32_t y, int expR, int expG, int expB, int expA, int tol = 0) {
                size_t o = (static_cast<size_t>(y) * 8u + x) * 4u;
                return std::abs(static_cast<int>(px[o + 0]) - expB) <= tol &&
                       std::abs(static_cast<int>(px[o + 1]) - expG) <= tol &&
                       std::abs(static_cast<int>(px[o + 2]) - expR) <= tol &&
                       std::abs(static_cast<int>(px[o + 3]) - expA) <= tol;
            };
            // UASTC: (0,0)=Mode 8 RGBA(230,40,25,255), (4,0)=Mode 5 e0 RGB(10,80,240), (7,3)=Mode 5 e1 RGB(250,220,30),
            //        (0,4)=Mode 14 premul RGBA(200,100,40,128)->(100,50,20,128), (7,7)=Mode 15 LA(230,255)
            bool uPx = chkPx(uInfo.bgraPixels, 0, 0, 230, 40,  25,  255) &&
                       chkPx(uInfo.bgraPixels, 4, 0, 10,  80,  240, 255) &&
                       chkPx(uInfo.bgraPixels, 7, 3, 250, 220, 30,  255) &&
                       chkPx(uInfo.bgraPixels, 0, 4, 100, 50,  20,  128, 1) &&
                       chkPx(uInfo.bgraPixels, 7, 7, 230, 230, 230, 255);
            // ETC1S: (0,0) & (0,4) upper-pred -> Gold RGB(233,233,35); (4,0) & (4,4) upper-pred -> Cyan RGB(33,182,255)
            bool ePx = chkPx(eInfo.bgraPixels, 0, 0, 233, 233, 35,  255) &&
                       chkPx(eInfo.bgraPixels, 0, 4, 233, 233, 35,  255) &&
                       chkPx(eInfo.bgraPixels, 4, 0, 33,  182, 255, 255) &&
                       chkPx(eInfo.bgraPixels, 4, 4, 33,  182, 255, 255);
            pxExact = uPx && ePx;
        }

        // Negative checks: corrupt slice CRC16, reserved UASTC Mode 19 (0x45), and codebook-less ETC1S must fail
        auto badCrcBasis = basisUastcA;
        badCrcBasis.back() ^= 0x55u; // Invalidates sliceCrc16 & dataCrc16
        auto badMode19Basis = basisUastcA;
        badMode19Basis[100] = 0x45u; // Reserved UASTC Mode 19
        badMode19Basis[6] = badMode19Basis[7] = 0u;
        badMode19Basis[12] = badMode19Basis[13] = 0u;
        badMode19Basis[77 + 21] = badMode19Basis[77 + 22] = 0u;

        std::vector<uint8_t> fakeEtc1s = basisUastcA;
        fakeEtc1s[20] = 0u; // cETC1S with zero endpoint/selector codebooks (old cheat format!)
        fakeEtc1s[6] = fakeEtc1s[7] = fakeEtc1s[12] = fakeEtc1s[13] = 0u;

        ExtendedImageInfo rejInfo;
        bool rejOk = !ImageFormatLoader::DecodeImageMemory(badCrcBasis.data(), badCrcBasis.size(), L".basis", 0, rejInfo) &&
                     !ImageFormatLoader::DecodeImageMemory(badMode19Basis.data(), badMode19Basis.size(), L".basis", 0, rejInfo) &&
                     !ImageFormatLoader::DecodeImageMemory(fakeEtc1s.data(), fakeEtc1s.size(), L".basis", 0, rejInfo);
        tc.Record(uOk && eOk && pxExact && rejOk, L"Stage15.Images.BasisUniversal_Etc1sAndRejectCorrupt",
                  L"ETC1S 8x8 + UASTC exact texels + CRC16/Mode19/codebook-less reject");
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

        auto woff2A = BuildWoff2FromSfnt(ttfA, false); // Uncompressed RFC 7932 Brotli meta-block
        auto woff2B = BuildWoff2FromSfnt(ttfB, true);  // Canonical-Huffman RFC 7932 Brotli meta-block
        verifyPair(L"Stage15.Images.WebFontWOFF2_UIntBase128", L"a.woff2", woff2A, L"b.woff2", woff2B, L".woff2", 52);

        // Verify unpacked WOFF2 renders real 640x400 glyph specimen ink (NOT old 520x300 bar chart) & rejects corrupt Brotli
        ExtendedImageInfo wf2Info;
        bool wf2Ok = ImageFormatLoader::DecodeImageMemory(woff2B.data(), woff2B.size(), L".woff2", 0, wf2Info) &&
                     wf2Info.valid && wf2Info.width == 640 && wf2Info.height == 400 &&
                     wf2Info.bgraPixels.size() == 640u * 400u * 4u;
        size_t glyphInkPixels = 0u;
        if (wf2Ok) {
            for (uint32_t y = 50; y < 390; ++y) {
                for (uint32_t x = 10; x < 630; ++x) {
                    const uint8_t* p = &wf2Info.bgraPixels[(static_cast<size_t>(y) * 640u + x) * 4u];
                    if (p[2] < 100u && p[1] < 110u) ++glyphInkPixels; // Dark glyph ink over parchment RGB(250,250,247)
                }
            }
        }
        auto badWoff2 = woff2B;
        uint32_t wf2CompLen = (static_cast<uint32_t>(badWoff2[20]) << 24) |
                              (static_cast<uint32_t>(badWoff2[21]) << 16) |
                              (static_cast<uint32_t>(badWoff2[22]) << 8)  |
                               static_cast<uint32_t>(badWoff2[23]);
        // Locate start of Brotli stream (8 tables x 2-3 bytes dir = 65 bytes -> offset 65) and inject reserved invalid WBITS (0x11)
        size_t brotliStart = 48u;
        for (int ti = 0; ti < 8; ++ti) {
            ++brotliStart; // flags
            while (brotliStart < badWoff2.size() && (badWoff2[brotliStart++] & 0x80u) != 0u) {}
        }
        if (brotliStart + 4u <= badWoff2.size() && wf2CompLen >= 4u) {
            badWoff2[brotliStart + 0u] = 0x11u; // Reserved invalid RFC 7932 WBITS pattern
            badWoff2[brotliStart + 1u] = 0xFFu;
            badWoff2[brotliStart + 2u] = 0xFFu;
            badWoff2[brotliStart + 3u] = 0xFFu;
        }
        ExtendedImageInfo badWf2Info;
        bool wf2Rej = !ImageFormatLoader::DecodeImageMemory(badWoff2.data(), badWoff2.size(), L".woff2", 0, badWf2Info);
        tc.Record(wf2Ok && glyphInkPixels > 200u && wf2Rej, L"Stage15.Images.WebFontWOFF2_GlyphsAndRejectCorrupt");

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

    // 13. JPEG XL (.jxl Modular bare + ISOBMFF jxlc + jxlp + brob + VarDCT/container rejection) & BPG (.bpg HEVC I_PCM + rejection)
    {
        auto jxlCsRgb  = BuildJxlModularCodestream(16, 16, false, true,  true);  // 16x16 RGB + YCoCg RCT + Gradient predictor
        auto jxlCsRgba = BuildJxlModularCodestream(24, 16, true,  false, false); // 24x16 RGBA (alpha=128 bottom half)
        auto jxlA      = WrapJxlIsobmff(jxlCsRgb);
        verifyPair(L"Stage15.Images.JpegXL_IsobmffAndBareModular", L"a.jxl", jxlA, L"b.jxl", jxlCsRgba, L".jxl", 36);

        auto jxlPartial = WrapJxlPartialBoxes(jxlCsRgb, true);
        auto jxlBrob    = WrapJxlBrob(jxlCsRgb);
        ExtendedImageInfo infoJxlA, infoJxlP, infoJxlBrob, infoJxlRgba;
        bool jxlAllOk =
            ImageFormatLoader::DecodeImageMemory(jxlA.data(), jxlA.size(), L".jxl", 0, infoJxlA) &&
            ImageFormatLoader::DecodeImageMemory(jxlPartial.data(), jxlPartial.size(), L".jxl", 0, infoJxlP) &&
            ImageFormatLoader::DecodeImageMemory(jxlBrob.data(), jxlBrob.size(), L".jxl", 0, infoJxlBrob) &&
            ImageFormatLoader::DecodeImageMemory(jxlCsRgba.data(), jxlCsRgba.size(), L".jxl", 0, infoJxlRgba) &&
            infoJxlA.width == 16 && infoJxlA.height == 16 &&
            infoJxlRgba.width == 24 && infoJxlRgba.height == 16 &&
            infoJxlA.bgraPixels == infoJxlP.bgraPixels &&
            infoJxlA.bgraPixels == infoJxlBrob.bgraPixels;

        bool jxlPxExact = false;
        if (jxlAllOk) {
            auto chkJxl = [](const ExtendedImageInfo& inf, uint32_t x, uint32_t y, int r, int g, int b, int a, int tol = 0) {
                size_t o = (static_cast<size_t>(y) * inf.width + x) * 4u;
                return std::abs(static_cast<int>(inf.bgraPixels[o + 0]) - b) <= tol &&
                       std::abs(static_cast<int>(inf.bgraPixels[o + 1]) - g) <= tol &&
                       std::abs(static_cast<int>(inf.bgraPixels[o + 2]) - r) <= tol &&
                       std::abs(static_cast<int>(inf.bgraPixels[o + 3]) - a) <= tol;
            };
            jxlPxExact = chkJxl(infoJxlA,    2,  2,  220, 40,  30,  255) &&
                         chkJxl(infoJxlA,    12, 2,  30,  195, 75,  255) &&
                         chkJxl(infoJxlA,    2,  12, 35,  95,  235, 255) &&
                         chkJxl(infoJxlA,    12, 12, 240, 205, 45,  255) &&
                         chkJxl(infoJxlRgba, 4,  2,  20,  180, 220, 255) &&
                         chkJxl(infoJxlRgba, 4,  12, 100, 30,  60,  128, 1);
        }

        // Negative JXL checks: bare container without jxlc/jxlp, unfinished jxlp sequence, and fake VarDCT garbage
        std::vector<uint8_t> bareJxlBox = {
            0x00, 0x00, 0x00, 0x0C, 'J', 'X', 'L', ' ', 0x0D, 0x0A, 0x87, 0x0A,
            0x00, 0x00, 0x00, 0x14, 'f', 't', 'y', 'p', 'j', 'x', 'l', ' ', 0, 0, 0, 0, 'j', 'x', 'l', ' '
        };
        auto unfinishedJxlp = WrapJxlPartialBoxes(jxlCsRgb, false);
        std::vector<uint8_t> fakeVarDct = { 0xFF, 0x0A, 0x4F, 0x06, 0x01, 0x99, 0xAA, 0x55, 0xDE, 0xAD }; // fhAllDefault=1 + garbage
        ExtendedImageInfo badJxl;
        bool jxlRej = !ImageFormatLoader::DecodeImageMemory(bareJxlBox.data(), bareJxlBox.size(), L".jxl", 0, badJxl) &&
                      !ImageFormatLoader::DecodeImageMemory(unfinishedJxlp.data(), unfinishedJxlp.size(), L".jxl", 0, badJxl) &&
                      !ImageFormatLoader::DecodeImageMemory(fakeVarDct.data(), fakeVarDct.size(), L".jxl", 0, badJxl);
        tc.Record(jxlAllOk && jxlPxExact && jxlRej, L"Stage15.Images.JpegXL_JxlpBrobAlphaAndRejectFake");

        auto bpgRgb   = BuildBpgFixture(16, 16, false); // 16x16 BPG_CS_RGB (GBR I_PCM + 00 00 03 RBSP escaping)
        auto bpgYcbcr = BuildBpgFixture(24, 16, true);  // 24x16 BPG_CS_YCbCr full-range I_PCM
        verifyPair(L"Stage15.Images.BPG_HevcIntraPcmRgbAndYCbCr", L"a.bpg", bpgRgb, L"b.bpg", bpgYcbcr, L".bpg", 16);

        ExtendedImageInfo bpgInfo;
        bool bpgOk = ImageFormatLoader::DecodeImageMemory(bpgRgb.data(), bpgRgb.size(), L".bpg", 0, bpgInfo) &&
                     bpgInfo.valid && bpgInfo.width == 16 && bpgInfo.height == 16 && bpgInfo.bgraPixels.size() == 1024;
        bool bpgPxExact = false;
        if (bpgOk) {
            auto chkBpg = [&](uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
                size_t o = (static_cast<size_t>(y) * 16u + x) * 4u;
                return bpgInfo.bgraPixels[o + 0] == b && bpgInfo.bgraPixels[o + 1] == g &&
                       bpgInfo.bgraPixels[o + 2] == r && bpgInfo.bgraPixels[o + 3] == 255u;
            };
            bpgPxExact = chkBpg(2,  2,  235, 45,  25) &&
                         chkBpg(12, 2,  0,   0,   0)  && // Confirms 00 00 03 emulation-prevention unescaping!
                         chkBpg(2,  12, 35,  210, 95) &&
                         chkBpg(12, 12, 45,  110, 245);
        }

        // Negative BPG checks: missing PPS (old fake NAL-19-only cheat) and corrupt CABAC ivlOffset >= 510
        std::vector<uint8_t> fakeNoPpsBpg = {
            'B', 'P', 'G', 0xFB, 0x60, 0x10, 16, 16, 16,
            0x04, 0x30, 0x00, 0x00, 0x80, // MSPS
            0x00, 0x00, 0x01, 0x26, 0x01, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x11
        };
        auto badCabacBpg = bpgRgb;
        badCabacBpg[badCabacBpg.size() - 2u] = 0xFFu; // ivlOffset = 511 -> invalid CABAC engine init!
        badCabacBpg[badCabacBpg.size() - 1u] = 0xFFu;
        ExtendedImageInfo badBpgInfo;
        bool bpgRej = !ImageFormatLoader::DecodeImageMemory(fakeNoPpsBpg.data(), fakeNoPpsBpg.size(), L".bpg", 0, badBpgInfo) &&
                      !ImageFormatLoader::DecodeImageMemory(badCabacBpg.data(), badCabacBpg.size(), L".bpg", 0, badBpgInfo);
        tc.Record(bpgOk && bpgPxExact && bpgRej, L"Stage15.Images.BPG_ExactPixelsAndRejectCorrupt");
    }

    // 14. DICOM PS3.5 / PS3.10 (.dcm): Undefined-Length SQ + Signed 12-bit CT HU VOI Window vs RLE Lossless
    {
        auto dcmCt  = BuildDicomCtSqFixture();
        auto dcmRle = BuildDicomRleLosslessFixture();
        verifyPair(L"Stage15.Images.DICOM_CtSqVoiAndRleLossless", L"ct_sq.dcm", dcmCt, L"rle.dcm", dcmRle, L".dcm", 220);

        ExtendedImageInfo ctInfo;
        bool decOk = ImageFormatLoader::DecodeImageMemory(dcmCt.data(), dcmCt.size(), L".dcm", 0, ctInfo) &&
                     ctInfo.valid && ctInfo.width == 8 && ctInfo.height == 8 && ctInfo.bgraPixels.size() == 256;
        bool voiAccurate = false;
        if (decOk) {
            uint8_t airNeg = ctInfo.bgraPixels[0 * 4];              // stored -1000 -> dark 0 (NOT white 255!)
            uint8_t airHu  = ctInfo.bgraPixels[1 * 4];              // stored 24 (-1000 HU) -> dark 0
            uint8_t midTis = ctInfo.bgraPixels[(4 * 8 + 4) * 4];    // +40 HU at WindowCenter=40 -> mid-gray ~128
            uint8_t bone   = ctInfo.bgraPixels[(7 * 8 + 7) * 4];    // +500 HU > 240 -> white 255
            voiAccurate = (airNeg == 0u) && (airHu == 0u) && (midTis >= 120u && midTis <= 136u) && (bone == 255u);
        }

        // Header-only DICOM with rows/cols but missing (7FE0,0010) PixelData must fail
        std::vector<uint8_t> hdrOnlyDcm(128, 0);
        hdrOnlyDcm.insert(hdrOnlyDcm.end(), { 'D', 'I', 'C', 'M' });
        AppendDcmUS(hdrOnlyDcm, 0x0028, 0x0010, 8);
        AppendDcmUS(hdrOnlyDcm, 0x0028, 0x0011, 8);
        ExtendedImageInfo badDcm;
        bool rejectedHdrOnly = !ImageFormatLoader::DecodeImageMemory(hdrOnlyDcm.data(), hdrOnlyDcm.size(), L".dcm", 0, badDcm);
        tc.Record(decOk && voiAccurate && rejectedHdrOnly, L"Stage15.Images.DICOM_SignedCtAirAndHeaderReject");

        std::wstring repoDcm = TestDataDir() + L"\\sample.dcm";
        if (GetFileAttributesW(repoDcm.c_str()) != INVALID_FILE_ATTRIBUTES) {
            ExtendedImageInfo sampleDcm;
            bool okRepoDcm = ImageFormatLoader::DecodeImageFile(repoDcm, 0, sampleDcm) &&
                             sampleDcm.valid && sampleDcm.width > 0 && sampleDcm.height > 0;
            tc.Record(okRepoDcm, L"Stage15.Images.RepoTestData.DICOM");
        }
    }

    // 15. Windows Animated Cursor (.ani): LIST 'fram' + 'seq ' 4bpp CUR + AND Mask vs 24bpp ICO & Zero-Frame Rejection
    {
        auto aniSeq = BuildAniSeqFixture();
        auto ani24  = BuildAni24BppFixture();
        verifyPair(L"Stage15.Images.WindowsANI_Seq4BppAnd24Bpp", L"seq4.ani", aniSeq, L"ico24.ani", ani24, L".ani", 68);

        ExtendedImageInfo seqInfo;
        bool seqOk = ImageFormatLoader::DecodeImageMemory(aniSeq.data(), aniSeq.size(), L".ani", 0, seqInfo) &&
                     seqInfo.valid && seqInfo.width == 16 && seqInfo.height == 16 && seqInfo.bgraPixels.size() == 1024;
        bool frameAndMaskOk = false;
        if (seqOk) {
            // Corner (0,0) must be transparent (alpha == 0) via 1bpp AND mask
            uint8_t cornerAlpha = seqInfo.bgraPixels[3];
            // Center (8,8) must match Frame 1 Green (B=20, G=230, R=10, A=255), NOT Frame 0 Red and NOT solid gray 240!
            size_t cOff = (8u * 16u + 8u) * 4u;
            frameAndMaskOk = (cornerAlpha == 0u) &&
                             (seqInfo.bgraPixels[cOff + 0] == 20u) &&
                             (seqInfo.bgraPixels[cOff + 1] == 230u) &&
                             (seqInfo.bgraPixels[cOff + 2] == 10u) &&
                             (seqInfo.bgraPixels[cOff + 3] == 255u);
        }

        // anih-only .ani with 0 icon frames must strictly return false (proving solid-gray 240 fallback is gone!)
        std::vector<uint8_t> anihOnly = { 'R','I','F','F', 48,0,0,0, 'A','C','O','N', 'a','n','i','h', 36,0,0,0 };
        anihOnly.resize(12 + 8 + 36, 0);
        anihOnly[20] = 36; anihOnly[24] = 1; anihOnly[28] = 1; anihOnly[32] = 32; anihOnly[36] = 32;
        ExtendedImageInfo emptyAniInfo;
        bool rejectedZeroFrames = !ImageFormatLoader::DecodeImageMemory(anihOnly.data(), anihOnly.size(), L".ani", 0, emptyAniInfo);
        tc.Record(seqOk && frameAndMaskOk && rejectedZeroFrames, L"Stage15.Images.WindowsANI_SeqMaskAndRejectEmpty");

        std::wstring repoAni = TestDataDir() + L"\\sample.ani";
        if (GetFileAttributesW(repoAni.c_str()) != INVALID_FILE_ATTRIBUTES) {
            ExtendedImageInfo sampleAni;
            bool okRepoAni = ImageFormatLoader::DecodeImageFile(repoAni, 0, sampleAni) &&
                             sampleAni.valid && sampleAni.width > 0 && sampleAni.height > 0;
            tc.Record(okRepoAni, L"Stage15.Images.RepoTestData.ANI");
        }
    }

    // 16. Camera RAW & Adobe DNG (#4): SubIFDs 12-bit Packed CFA, Zlib-Tiled DNG, Lossless JPEG SOF3 CR2 & Fujifilm RAF CFA
    {
        auto appendIfdEntryLE = [](std::vector<uint8_t>& buf, uint16_t tag, uint16_t type, uint32_t count, uint32_t valOrOff) {
            PushU16LE(buf, tag);
            PushU16LE(buf, type);
            PushU32LE(buf, count);
            PushU32LE(buf, valOrOff);
        };

        // A. DNG with IFD0 -> SubIFDs (0x014A) -> 16x16 12-bit MSB-packed RGGB CFA (saturated Red)
        std::vector<uint8_t> dngSubIfd;
        {
            const uint32_t w = 16, h = 16;
            std::vector<uint16_t> samples(w * h);
            for (uint32_t y = 0; y < h; ++y) {
                for (uint32_t x = 0; x < w; ++x) {
                    bool isR = ((y & 1u) == 0u) && ((x & 1u) == 0u);
                    samples[y * w + x] = isR ? 3900u : 128u;
                }
            }
            // Pack 12-bit MSB-first samples (2 pixels -> 3 bytes)
            std::vector<uint8_t> packed12;
            for (size_t i = 0; i < samples.size(); i += 2) {
                uint16_t s0 = samples[i] & 0x0FFFu;
                uint16_t s1 = samples[i + 1] & 0x0FFFu;
                packed12.push_back(static_cast<uint8_t>(s0 >> 4));
                packed12.push_back(static_cast<uint8_t>(((s0 & 0x0Fu) << 4) | (s1 >> 8)));
                packed12.push_back(static_cast<uint8_t>(s1 & 0xFFu));
            }

            dngSubIfd = { 'I', 'I', 42, 0, 8, 0, 0, 0 }; // IFD0 at 8
            // IFD0: 2 entries (NewSubfileType=1 thumbnail placeholder without strips, SubIFDs=0x014A -> offset 38)
            PushU16LE(dngSubIfd, 2);
            appendIfdEntryLE(dngSubIfd, 0x00FE, 4, 1, 1u);
            appendIfdEntryLE(dngSubIfd, 0x014A, 4, 1, 38u);
            PushU32LE(dngSubIfd, 0); // nextIFD = 0 (offset 38 next)

            // SubIFD at offset 38: 12 entries (sorted ascending by tag) -> 2 + 12*12 + 4 = 150 bytes -> ends at 188
            uint32_t stripOff = 188u;
            uint32_t stripLen = static_cast<uint32_t>(packed12.size());
            PushU16LE(dngSubIfd, 12);
            appendIfdEntryLE(dngSubIfd, 0x00FE, 4, 1, 0u);          // NewSubfileType = 0 (Full-res RAW)
            appendIfdEntryLE(dngSubIfd, 0x0100, 4, 1, w);           // ImageWidth = 16
            appendIfdEntryLE(dngSubIfd, 0x0101, 4, 1, h);           // ImageLength = 16
            appendIfdEntryLE(dngSubIfd, 0x0102, 3, 1, 12u);         // BitsPerSample = 12
            appendIfdEntryLE(dngSubIfd, 0x0103, 3, 1, 1u);          // Compression = 1 (Uncompressed)
            appendIfdEntryLE(dngSubIfd, 0x0106, 3, 1, 32803u);      // Photometric = CFA
            appendIfdEntryLE(dngSubIfd, 0x0111, 4, 1, stripOff);    // StripOffsets
            appendIfdEntryLE(dngSubIfd, 0x0115, 3, 1, 1u);          // SamplesPerPixel = 1
            appendIfdEntryLE(dngSubIfd, 0x0117, 4, 1, stripLen);    // StripByteCounts
            appendIfdEntryLE(dngSubIfd, 0x828D, 3, 2, 0x00020002u); // CFARepeatPatternDim = 2x2
            appendIfdEntryLE(dngSubIfd, 0x828E, 1, 4, 0x02010100u); // CFAPattern = {0=R, 1=G, 1=G, 2=B}
            appendIfdEntryLE(dngSubIfd, 0xC61D, 4, 1, 4095u);       // WhiteLevel = 4095
            PushU32LE(dngSubIfd, 0);
            dngSubIfd.insert(dngSubIfd.end(), packed12.begin(), packed12.end());
        }

        // B. Tiled (8x8 tiles, 2x2 grid) Zlib-compressed (Compression=8) 16-bit RGGB CFA DNG (saturated Blue)
        std::vector<uint8_t> dngTiledZlib;
        {
            const uint32_t w = 16, h = 16, tw = 8, th = 8;
            std::vector<std::vector<uint8_t>> zTiles(4);
            for (uint32_t ty = 0; ty < 2; ++ty) {
                for (uint32_t tx = 0; tx < 2; ++tx) {
                    std::vector<uint8_t> rawTile;
                    for (uint32_t y = 0; y < th; ++y) {
                        for (uint32_t x = 0; x < tw; ++x) {
                            bool isB = ((y & 1u) == 1u) && ((x & 1u) == 1u);
                            PushU16LE(rawTile, isB ? 58000u : 1200u);
                        }
                    }
                    zTiles[ty * 2 + tx] = CompressZlibFixed(rawTile);
                }
            }

            dngTiledZlib = { 'I', 'I', 42, 0, 8, 0, 0, 0 };
            // IFD0 at 8: 12 entries -> 2 + 12*12 + 4 = 150 bytes -> offset 158
            // TileOffsets array (4*4=16B) at 158..173, TileByteCounts array (4*4=16B) at 174..189, tiles start at 190
            uint32_t tileOffTbl = 158u;
            uint32_t tileCntTbl = 174u;
            uint32_t curOff = 190u;
            uint32_t tOffs[4] = {}, tLens[4] = {};
            for (int i = 0; i < 4; ++i) {
                tOffs[i] = curOff;
                tLens[i] = static_cast<uint32_t>(zTiles[i].size());
                curOff += tLens[i];
            }
            PushU16LE(dngTiledZlib, 12);
            appendIfdEntryLE(dngTiledZlib, 0x00FE, 4, 1, 0u);
            appendIfdEntryLE(dngTiledZlib, 0x0100, 4, 1, w);
            appendIfdEntryLE(dngTiledZlib, 0x0101, 4, 1, h);
            appendIfdEntryLE(dngTiledZlib, 0x0102, 3, 1, 16u);         // 16-bit
            appendIfdEntryLE(dngTiledZlib, 0x0103, 3, 1, 8u);          // Compression = 8 (Adobe Deflate/Zlib)
            appendIfdEntryLE(dngTiledZlib, 0x0106, 3, 1, 32803u);      // CFA
            appendIfdEntryLE(dngTiledZlib, 0x0115, 3, 1, 1u);
            appendIfdEntryLE(dngTiledZlib, 0x0142, 4, 1, tw);          // TileWidth = 8
            appendIfdEntryLE(dngTiledZlib, 0x0143, 4, 1, th);          // TileLength = 8
            appendIfdEntryLE(dngTiledZlib, 0x0144, 4, 4, tileOffTbl);  // TileOffsets[4]
            appendIfdEntryLE(dngTiledZlib, 0x0145, 4, 4, tileCntTbl);  // TileByteCounts[4]
            appendIfdEntryLE(dngTiledZlib, 0x828E, 1, 4, 0x02010100u); // RGGB
            PushU32LE(dngTiledZlib, 0);
            for (int i = 0; i < 4; ++i) PushU32LE(dngTiledZlib, tOffs[i]);
            for (int i = 0; i < 4; ++i) PushU32LE(dngTiledZlib, tLens[i]);
            for (int i = 0; i < 4; ++i) dngTiledZlib.insert(dngTiledZlib.end(), zTiles[i].begin(), zTiles[i].end());
        }

        verifyPair(L"Stage15.Images.CameraRAW_DngSubIfd12BitAndTiledZlib",
                   L"subifd12.dng", dngSubIfd, L"tiled_zlib.dng", dngTiledZlib, L".dng", 64);

        // C. ITU-T T.81 Lossless JPEG (SOF3 0xFFC3, 4-component 12-bit Bayer quad) CR2/DNG + Fujifilm RAF Raw CFA
        std::vector<uint8_t> cr2Ljpeg;
        {
            // Build 4-component 8x8 quad (= 16x16 CFA) Lossless JPEG SOF3 stream (Green-dominant: R=200, G=3800, B=200)
            std::vector<uint8_t> lj = { 0xFF, 0xD8 };
            // DHT (0xFFC4): Th=0, counts[0]=1 (ssss=0 -> '0'), counts[1]=1 (ssss=11 -> '10')
            lj.insert(lj.end(), { 0xFF, 0xC4, 0x00, 21, 0x00 });
            lj.push_back(1); lj.push_back(1);
            for (int i = 2; i < 16; ++i) lj.push_back(0);
            lj.push_back(0);  // code '0'  -> ssss = 0 (diff = 0)
            lj.push_back(11); // code '10' -> ssss = 11 (11-bit diff)

            // SOF3 (0xFFC3): P=12, Y=8, X=8, Nf=4 (2x2 Bayer quad components 1..4)
            lj.insert(lj.end(), {
                0xFF, 0xC3, 0x00, 20, 12, 0x00, 8, 0x00, 8, 4,
                1, 0x11, 0,  2, 0x11, 0,  3, 0x11, 0,  4, 0x11, 0
            });
            // SOS (0xFFDA): Ns=4, Predictor Ss=1, Se=0, Pt=0
            lj.insert(lj.end(), {
                0xFF, 0xDA, 0x00, 14, 4,
                1, 0x00,  2, 0x00,  3, 0x00,  4, 0x00,
                1, 0, 0
            });

            // Entropy bitstream (MSB-first with 0xFF00 byte-stuffing):
            // First MCU (0,0) initPred=2048:
            //   Comp0 (R=200):  diff=-1848 -> ssss=11 ('10'), v = -1848 + 2047 = 199 (11 bits)
            //   Comp1 (G1=3800):diff=+1752 -> ssss=11 ('10'), v = 1752 (11 bits)
            //   Comp2 (G2=3800):diff=+1752 -> ssss=11 ('10'), v = 1752 (11 bits)
            //   Comp3 (B=200):  diff=-1848 -> ssss=11 ('10'), v = 199  (11 bits)
            // Remaining 63 MCUs * 4 comps = 252 samples: all diff=0 -> '0'
            uint32_t bitAcc = 0; int bitUsed = 0;
            auto emitMsb = [&](uint32_t val, int n) {
                for (int b = n - 1; b >= 0; --b) {
                    bitAcc = (bitAcc << 1) | ((val >> b) & 1u);
                    if (++bitUsed == 8) {
                        lj.push_back(static_cast<uint8_t>(bitAcc));
                        if ((bitAcc & 0xFFu) == 0xFFu) lj.push_back(0x00u);
                        bitAcc = 0; bitUsed = 0;
                    }
                }
            };
            const uint32_t mcu0Vals[4] = { 199u, 1752u, 1752u, 199u };
            for (int c = 0; c < 4; ++c) {
                emitMsb(0b10u, 2);
                emitMsb(mcu0Vals[c], 11);
            }
            for (int rem = 0; rem < 63 * 4; ++rem) emitMsb(0u, 1);
            if (bitUsed > 0) emitMsb((1u << (8 - bitUsed)) - 1u, 8 - bitUsed);
            lj.insert(lj.end(), { 0xFF, 0xD9 });

            cr2Ljpeg = { 'I', 'I', 42, 0, 16, 0, 0, 0, 'C', 'R', 2, 0, 16, 0, 0, 0 }; // Canon CR2 header, IFD0 at 16
            // 11 entries -> 2 + 11*12 + 4 = 138 bytes -> strip at 154
            uint32_t sOff = 154u, sLen = static_cast<uint32_t>(lj.size());
            PushU16LE(cr2Ljpeg, 11);
            appendIfdEntryLE(cr2Ljpeg, 0x00FE, 4, 1, 0u);
            appendIfdEntryLE(cr2Ljpeg, 0x0100, 4, 1, 16u);
            appendIfdEntryLE(cr2Ljpeg, 0x0101, 4, 1, 16u);
            appendIfdEntryLE(cr2Ljpeg, 0x0102, 3, 1, 12u);
            appendIfdEntryLE(cr2Ljpeg, 0x0103, 3, 1, 7u);           // Compression = 7 (Lossless JPEG)
            appendIfdEntryLE(cr2Ljpeg, 0x0106, 3, 1, 32803u);       // CFA
            appendIfdEntryLE(cr2Ljpeg, 0x0111, 4, 1, sOff);
            appendIfdEntryLE(cr2Ljpeg, 0x0115, 3, 1, 1u);
            appendIfdEntryLE(cr2Ljpeg, 0x0117, 4, 1, sLen);
            appendIfdEntryLE(cr2Ljpeg, 0x828D, 3, 2, 0x00020002u);
            appendIfdEntryLE(cr2Ljpeg, 0x828E, 1, 4, 0x02010100u);
            PushU32LE(cr2Ljpeg, 0);
            cr2Ljpeg.insert(cr2Ljpeg.end(), lj.begin(), lj.end());
        }

        // Fujifilm .raf with raw CFA directory (0x5C->108) + 16x16 16-bit BE RGGB CFA at 120 (no embedded JPEG)
        std::vector<uint8_t> rafCfa(108, 0);
        std::memcpy(rafCfa.data(), "FUJIFILMCCD-RAW 0201", 20);
        rafCfa[95]  = 108u; // cfaHdrOff = 108 BE
        rafCfa[103] = 120u; // cfaDataOff = 120 BE
        rafCfa[106] = 0x02; rafCfa[107] = 0x00; // cfaDataLen = 512 BE
        PushU32BE(rafCfa, 1u);      // numRecs = 1
        PushU16BE(rafCfa, 0x0100u); // tag 0x0100 = raw dimensions
        PushU16BE(rafCfa, 4u);      // len = 4
        PushU16BE(rafCfa, 16u);     // rawH = 16
        PushU16BE(rafCfa, 16u);     // rawW = 16
        for (uint32_t y = 0; y < 16; ++y) {
            for (uint32_t x = 0; x < 16; ++x) {
                bool isR = ((y & 1u) == 0u) && ((x & 1u) == 0u);
                PushU16BE(rafCfa, isR ? 0x0E55u : 0x0155u); // Low byte 0x55 ensures BE < 16384 < LE
            }
        }

        ExtendedImageInfo iSubRed, iTileBlue, iCr2Grn, iRafRed;
        bool rawPosOk =
            ImageFormatLoader::DecodeImageMemory(dngSubIfd.data(), dngSubIfd.size(), L".dng", 0, iSubRed) &&
            ImageFormatLoader::DecodeImageMemory(dngTiledZlib.data(), dngTiledZlib.size(), L".dng", 0, iTileBlue) &&
            ImageFormatLoader::DecodeImageMemory(cr2Ljpeg.data(), cr2Ljpeg.size(), L".cr2", 0, iCr2Grn) &&
            ImageFormatLoader::DecodeImageMemory(rafCfa.data(), rafCfa.size(), L".raf", 0, iRafRed) &&
            iSubRed.width == 16 && iTileBlue.width == 16 && iCr2Grn.width == 16 && iRafRed.width == 16;

        bool cfaColorOk = false;
        if (rawPosOk) {
            size_t cOff = (8u * 16u + 8u) * 4u; // Center pixel (8,8) BGRA
            bool redDom  = iSubRed.bgraPixels[cOff + 2] > iSubRed.bgraPixels[cOff + 0] + 80u;
            bool blueDom = iTileBlue.bgraPixels[cOff + 0] > iTileBlue.bgraPixels[cOff + 2] + 80u;
            bool grnDom  = iCr2Grn.bgraPixels[cOff + 1] > iCr2Grn.bgraPixels[cOff + 2] + 80u &&
                           iCr2Grn.bgraPixels[cOff + 1] > iCr2Grn.bgraPixels[cOff + 0] + 80u;
            bool rafRed  = iRafRed.bgraPixels[cOff + 2] > iRafRed.bgraPixels[cOff + 0] + 80u;
            cfaColorOk = redDom && blueDom && grnDom && rafRed;
        }

        // Negative RAW checks: 0-entry TIFF IFD, empty-mdat CR3, and offset-zero RAF must strictly fail (no fake card!)
        std::vector<uint8_t> emptyTiffRaw = { 'I', 'I', 42, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        std::vector<uint8_t> emptyCr3 = {
            0,0,0,24, 'f','t','y','p', 'c','r','x',' ', 0,0,0,1, 'c','r','x',' ', 'i','s','o','m',
            0,0,0,16, 'm','d','a','t', 0,0,0,0, 0,0,0,0
        };
        std::vector<uint8_t> emptyRaf(128, 0);
        std::memcpy(emptyRaf.data(), "FUJIFILMCCD-RAW 0201", 20);
        ExtendedImageInfo badRaw;
        bool rawRej = !ImageFormatLoader::DecodeImageMemory(emptyTiffRaw.data(), emptyTiffRaw.size(), L".dng", 0, badRaw) &&
                      !ImageFormatLoader::DecodeImageMemory(emptyTiffRaw.data(), emptyTiffRaw.size(), L".nef", 0, badRaw) &&
                      !ImageFormatLoader::DecodeImageMemory(emptyCr3.data(), emptyCr3.size(), L".cr3", 0, badRaw) &&
                      !ImageFormatLoader::DecodeImageMemory(emptyRaf.data(), emptyRaf.size(), L".raf", 0, badRaw);
        tc.Record(rawPosOk && cfaColorOk && rawRej, L"Stage15.Images.CameraRAW_LosslessJpegRafAndRejectFakeCard");
    }

    // 17. Vector PostScript / Illustrator (#5a — .eps DOS Binary & Bézier PS, .ai /FlateDecode PDF & Badge Rejection)
    {
        std::string psBody =
            "%!PS-Adobe-3.0 EPSF-3.0\n"
            "%%BoundingBox: 0 0 200 200\n"
            "0.90 0.12 0.15 setrgbcolor\n"
            "newpath 20 20 moveto 180 20 lineto 180 180 20 180 20 100 curveto closepath fill\n"
            "0.10 0.25 0.85 setrgbcolor 6 setlinewidth 30 30 140 140 rectstroke\n"
            "%%EOF\n";
        // DOS Binary EPS header (C5 D0 D3 C6 + PS offset=30 + PS length)
        std::vector<uint8_t> dosEps(30, 0);
        dosEps[0] = 0xC5; dosEps[1] = 0xD0; dosEps[2] = 0xD3; dosEps[3] = 0xC6;
        uint32_t psOff = 30u, psLen = static_cast<uint32_t>(psBody.size());
        std::memcpy(dosEps.data() + 4, &psOff, 4);
        std::memcpy(dosEps.data() + 8, &psLen, 4);
        dosEps.insert(dosEps.end(), psBody.begin(), psBody.end());

        // PDF-based Illustrator .ai with Zlib /FlateDecode content stream
        std::string pdfCmds =
            "0.12 0.78 0.35 rg\n"
            "20 20 160 160 re f\n"
            "0.85 0.20 0.60 RG 5 w\n"
            "30 100 m 80 180 120 20 170 100 c S\n";
        std::vector<uint8_t> pdfRaw(pdfCmds.begin(), pdfCmds.end());
        std::vector<uint8_t> pdfZlib = CompressZlibFixed(pdfRaw);
        std::string aiHdr =
            "%PDF-1.5\n"
            "1 0 obj << /Type /Page /MediaBox [0 0 200 200] /Contents 2 0 R >> endobj\n"
            "2 0 obj << /Filter /FlateDecode /Length " + std::to_string(pdfZlib.size()) + " >>\nstream\n";
        std::string aiTail = "\nendstream\nendobj\n%%EOF\n";
        std::vector<uint8_t> aiPdf(aiHdr.begin(), aiHdr.end());
        aiPdf.insert(aiPdf.end(), pdfZlib.begin(), pdfZlib.end());
        aiPdf.insert(aiPdf.end(), aiTail.begin(), aiTail.end());

        verifyPair(L"Stage15.Images.VectorEpsDosAndAiFlatePdf",
                   L"dos.eps", dosEps, L"flate.ai", aiPdf, L".ai", 24);

        const char* emptyEps = "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 100 100\n%%EOF\n";
        const char* emptyAi  = "%PDF-1.5\n1 0 obj << /Type /Catalog >> endobj\n%%EOF\n";
        ExtendedImageInfo badVec;
        bool vecRej = !ImageFormatLoader::DecodeImageMemory(reinterpret_cast<const uint8_t*>(emptyEps), std::strlen(emptyEps), L".eps", 0, badVec) &&
                      !ImageFormatLoader::DecodeImageMemory(reinterpret_cast<const uint8_t*>(emptyAi), std::strlen(emptyAi), L".ai", 0, badVec);
        tc.Record(vecRej, L"Stage15.Images.VectorEpsAi_RejectEmptyNoBadgeCard");
    }

    // 18. CorelDRAW & Affinity Suite (#5b — .cdr RIFF 'DISP' DIB vs ZIP, .afphoto Zlib 'Thmb' & Badge Rejection)
    {
        auto buildDib24 = [](uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b) {
            std::vector<uint8_t> dib;
            uint32_t stride = ((w * 24u + 31u) / 32u) * 4u;
            PushU32LE(dib, 40u);
            PushU32LE(dib, w);
            PushU32LE(dib, h);
            PushU16LE(dib, 1u);
            PushU16LE(dib, 24u);
            PushU32LE(dib, 0u);
            PushU32LE(dib, stride * h);
            PushU32LE(dib, 2835u); PushU32LE(dib, 2835u);
            PushU32LE(dib, 0u); PushU32LE(dib, 0u);
            for (uint32_t y = 0; y < h; ++y) {
                size_t rowStart = dib.size();
                for (uint32_t x = 0; x < w; ++x) {
                    dib.push_back(static_cast<uint8_t>(b ^ (x * 4u)));
                    dib.push_back(g);
                    dib.push_back(r);
                }
                while (dib.size() - rowStart < stride) dib.push_back(0u);
            }
            return dib;
        };

        // Classic RIFF 'CDRv' with 'DISP' chunk ([CF_DIB=8:4B] + 16x16 24bpp DIB)
        std::vector<uint8_t> dibRed = buildDib24(16, 16, 230, 45, 35);
        std::vector<uint8_t> cdrRiff = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'C', 'D', 'R', 'v', 'D', 'I', 'S', 'P' };
        PushU32LE(cdrRiff, static_cast<uint32_t>(4u + dibRed.size()));
        PushU32LE(cdrRiff, 8u); // CF_DIB
        cdrRiff.insert(cdrRiff.end(), dibRed.begin(), dibRed.end());
        uint32_t riffBodySz = static_cast<uint32_t>(cdrRiff.size() - 8u);
        std::memcpy(cdrRiff.data() + 4, &riffBodySz, 4);

        // Modern ZIP .cdr containing stored metadata/thumbnails/thumbnail.bmp
        std::vector<uint8_t> dibBlue = buildDib24(16, 16, 30, 95, 235);
        std::vector<uint8_t> bmpBlue(14u + dibBlue.size(), 0);
        bmpBlue[0] = 'B'; bmpBlue[1] = 'M';
        uint32_t bmpSz = static_cast<uint32_t>(bmpBlue.size()), bmpOff = 54u;
        std::memcpy(bmpBlue.data() + 2, &bmpSz, 4);
        std::memcpy(bmpBlue.data() + 10, &bmpOff, 4);
        std::memcpy(bmpBlue.data() + 14, dibBlue.data(), dibBlue.size());

        auto wrapSingleStoredZip = [](const char* entryName, const std::vector<uint8_t>& payload) {
            uint16_t fnLen = static_cast<uint16_t>(std::strlen(entryName));
            uint32_t sz = static_cast<uint32_t>(payload.size());
            uint32_t crc = 0xFFFFFFFFu;
            for (uint8_t b : payload) {
                crc ^= b;
                for (int k = 0; k < 8; ++k) crc = (crc & 1u) ? ((crc >> 1u) ^ 0xEDB88320u) : (crc >> 1u);
            }
            crc = ~crc;
            std::vector<uint8_t> z;
            PushU32LE(z, 0x04034B50u); PushU16LE(z, 20u); PushU16LE(z, 0u); PushU16LE(z, 0u);
            PushU16LE(z, 0u); PushU16LE(z, 0u); PushU32LE(z, crc); PushU32LE(z, sz); PushU32LE(z, sz);
            PushU16LE(z, fnLen); PushU16LE(z, 0u);
            z.insert(z.end(), entryName, entryName + fnLen);
            z.insert(z.end(), payload.begin(), payload.end());
            uint32_t cdOff = static_cast<uint32_t>(z.size());
            PushU32LE(z, 0x02014B50u); PushU16LE(z, 20u); PushU16LE(z, 20u); PushU16LE(z, 0u); PushU16LE(z, 0u);
            PushU16LE(z, 0u); PushU16LE(z, 0u); PushU32LE(z, crc); PushU32LE(z, sz); PushU32LE(z, sz);
            PushU16LE(z, fnLen); PushU16LE(z, 0u); PushU16LE(z, 0u); PushU16LE(z, 0u); PushU16LE(z, 0u);
            PushU32LE(z, 0u); PushU32LE(z, 0u);
            z.insert(z.end(), entryName, entryName + fnLen);
            uint32_t cdSize = static_cast<uint32_t>(z.size()) - cdOff;
            PushU32LE(z, 0x06054B50u); PushU16LE(z, 0u); PushU16LE(z, 0u);
            PushU16LE(z, 1u); PushU16LE(z, 1u); PushU32LE(z, cdSize); PushU32LE(z, cdOff); PushU16LE(z, 0u);
            return z;
        };
        std::vector<uint8_t> cdrZip = wrapSingleStoredZip("metadata/thumbnails/thumbnail.bmp", bmpBlue);
        verifyPair(L"Stage15.Images.CorelCdr_RiffDispAndZipThumb",
                   L"riff.cdr", cdrRiff, L"zip.cdr", cdrZip, L".cdr", 20);

        // Affinity .afphoto with 00 FF 4B 41 magic + Zlib-compressed 'Thmb' BMP stream
        std::vector<uint8_t> zThumbBmp = CompressZlibFixed(bmpBlue);
        std::vector<uint8_t> afThmb = { 0x00, 0xFF, 0x4B, 0x41, 0x08, 0x00, 0x00, 0x00, 'T', 'h', 'm', 'b', 0, 0, 0, 0 };
        afThmb.insert(afThmb.end(), zThumbBmp.begin(), zThumbBmp.end());

        ExtendedImageInfo afInfo, badCdrAff;
        bool afOk = ImageFormatLoader::DecodeImageMemory(afThmb.data(), afThmb.size(), L".afphoto", 0, afInfo) &&
                    afInfo.valid && afInfo.width == 16 && afInfo.height == 16;
        std::vector<uint8_t> emptyRiffCdr = { 'R', 'I', 'F', 'F', 12, 0, 0, 0, 'C', 'D', 'R', 'v', 'J', 'U', 'N', 'K', 0, 0, 0, 0 };
        std::vector<uint8_t> emptyAfPhoto = { 0x00, 0xFF, 0x4B, 0x41, 0x01, 0x02, 0x03, 0x04, 'N', 'O', 'N', 'E', 0, 0, 0, 0 };
        bool cdrAffRej = !ImageFormatLoader::DecodeImageMemory(emptyRiffCdr.data(), emptyRiffCdr.size(), L".cdr", 0, badCdrAff) &&
                         !ImageFormatLoader::DecodeImageMemory(emptyAfPhoto.data(), emptyAfPhoto.size(), L".afphoto", 0, badCdrAff);
        tc.Record(afOk && cdrAffRej, L"Stage15.Images.AffinityZlibThmbAndRejectFakeCard");
    }

    // 19. ISO/IEC 8632 Computer Graphics Metafile (#5c — Part 3 Binary vs Part 4 Clear-Text & Empty Rejection)
    {
        auto appendCgmCmd = [](std::vector<uint8_t>& out, uint16_t cls, uint16_t eid, const std::vector<uint8_t>& pay) {
            uint16_t hdr = static_cast<uint16_t>((cls << 12) | (eid << 5) | (pay.size() & 0x1Fu));
            PushU16BE(out, hdr);
            out.insert(out.end(), pay.begin(), pay.end());
            if (pay.size() & 1u) out.push_back(0u);
        };
        std::vector<uint8_t> cgmBin;
        appendCgmCmd(cgmBin, 0, 1, { 3, 'B', 'I', 'N' });                  // BEGIN METAFILE
        appendCgmCmd(cgmBin, 0, 3, {});                                    // BEGIN PICTURE
        appendCgmCmd(cgmBin, 2, 2, { 0x00, 0x01 });                        // COLOUR SELECTION MODE = direct RGB
        appendCgmCmd(cgmBin, 2, 6, { 0, 0, 0, 0, 0x03, 0xE8, 0x03, 0xE8 });// VDC EXTENT (0,0)-(1000,1000)
        appendCgmCmd(cgmBin, 0, 4, {});                                    // BEGIN PICTURE BODY
        appendCgmCmd(cgmBin, 5, 22, { 0x00, 0x01 });                       // INTERIOR STYLE = SOLID
        appendCgmCmd(cgmBin, 5, 23, { 220, 30, 40 });                      // FILL COLOUR = Crimson Red
        // POLYGON (cls=4, eid=7): rectangle (100,100)-(900,900) covering center
        appendCgmCmd(cgmBin, 4, 7, {
            0x00, 100, 0x00, 100,
            0x03, 0x84, 0x00, 100,
            0x03, 0x84, 0x03, 0x84,
            0x00, 100, 0x03, 0x84
        });
        appendCgmCmd(cgmBin, 0, 5, {});                                    // END PICTURE
        appendCgmCmd(cgmBin, 0, 2, {});                                    // END METAFILE

        std::string cgmTxtStr =
            "BEGMF 'CLEAR_TEXT_CGM';\n"
            "BEGPIC 'P1';\n"
            "COLRMODE DIRECT;\n"
            "VDCEXT (0,0) (1000,1000);\n"
            "BEGPICBODY;\n"
            "INTSTYLE SOLID;\n"
            "FILLCOLR 25 195 75;\n"
            "CIRCLE (500,500) 380;\n"
            "ENDPIC;\n"
            "ENDMF;\n";
        std::vector<uint8_t> cgmTxt(cgmTxtStr.begin(), cgmTxtStr.end());

        verifyPair(L"Stage15.Images.Cgm_BinaryAndClearText",
                   L"bin.cgm", cgmBin, L"txt.cgm", cgmTxt, L".cgm", 6);

        ExtendedImageInfo iBin, iTxt, badCgm;
        bool cgmColorsOk =
            ImageFormatLoader::DecodeImageMemory(cgmBin.data(), cgmBin.size(), L".cgm", 0, iBin) &&
            ImageFormatLoader::DecodeImageMemory(cgmTxt.data(), cgmTxt.size(), L".cgm", 0, iTxt);
        if (cgmColorsOk) {
            size_t cBin = (static_cast<size_t>(iBin.height / 2u) * iBin.width + (iBin.width / 2u)) * 4u;
            size_t cTxt = (static_cast<size_t>(iTxt.height / 2u) * iTxt.width + (iTxt.width / 2u)) * 4u;
            cgmColorsOk = (iBin.bgraPixels[cBin + 2] == 220u && iBin.bgraPixels[cBin + 1] == 30u) &&
                          (iTxt.bgraPixels[cTxt + 1] == 195u && iTxt.bgraPixels[cTxt + 2] == 25u);
        }
        std::vector<uint8_t> emptyBinCgm = { 0x00, 0x20, 0x00, 0x40, 0x00, 0x00 };
        const char* emptyTxtCgm = "BEGMF 'EMPTY'; BEGPIC 'P'; BEGPICBODY; ENDPIC; ENDMF;";
        bool cgmRej = !ImageFormatLoader::DecodeImageMemory(emptyBinCgm.data(), emptyBinCgm.size(), L".cgm", 0, badCgm) &&
                      !ImageFormatLoader::DecodeImageMemory(reinterpret_cast<const uint8_t*>(emptyTxtCgm), std::strlen(emptyTxtCgm), L".cgm", 0, badCgm);
        tc.Record(cgmColorsOk && cgmRej, L"Stage15.Images.Cgm_CenterColorsAndRejectEmpty");
    }

    // 20. Adobe Photoshop Brush Set (#5d — .abr v1 Raw Sampled vs v6.2 8BIMsamp PackBits RLE & No 3-Circle Fallback)
    {
        // ABR v1 sampled brush (16x16 raw 8-bit cross pattern)
        std::vector<uint8_t> abrV1;
        PushU16BE(abrV1, 1u); // version = 1
        PushU16BE(abrV1, 1u); // count = 1
        PushU16BE(abrV1, 2u); // type = 2 (Sampled brush)
        PushU32BE(abrV1, 6u + 9u + 19u + 256u); // bsize = 290
        abrV1.insert(abrV1.end(), 6, 0u); // misc(4) + spacing(2)
        abrV1.push_back(1u); // antialias
        PushU16BE(abrV1, 0u); PushU16BE(abrV1, 0u); PushU16BE(abrV1, 16u); PushU16BE(abrV1, 16u); // shortBounds
        PushU32BE(abrV1, 0u); PushU32BE(abrV1, 0u); PushU32BE(abrV1, 16u); PushU32BE(abrV1, 16u); // longBounds
        PushU16BE(abrV1, 8u); // depth = 8
        abrV1.push_back(0u);  // comp = 0 (raw)
        for (uint32_t y = 0; y < 16; ++y) {
            for (uint32_t x = 0; x < 16; ++x) {
                bool onCross = (x >= 6 && x <= 9) || (y >= 6 && y <= 9);
                abrV1.push_back(onCross ? 255u : 0u);
            }
        }

        // ABR v6.2 '8BIMsamp' brush (301-byte header + 16*2B row byte counts + PackBits RLE rows)
        std::vector<uint8_t> sampRec(301, 0);
        sampRec[0] = 36u; // Pascal UUID length
        PushU32BE(sampRec, 0u);  // top = 0
        PushU32BE(sampRec, 0u);  // left = 0
        PushU32BE(sampRec, 16u); // bottom = 16
        PushU32BE(sampRec, 16u); // right = 16
        PushU16BE(sampRec, 8u);  // depth = 8
        sampRec.push_back(1u);   // comp = 1 (PackBits RLE)
        std::vector<std::vector<uint8_t>> rleRows(16);
        for (uint32_t y = 0; y < 16; ++y) {
            if (y < 4 || y >= 12) {
                // 16 zeros -> PackBits repeat run: n = 1 - 16 = -15 (0xF1), val = 0x00
                rleRows[y] = { 0xF1u, 0x00u };
            } else {
                // 4 zeros (-3=0xFD, 0x00) + 8 opaque (-7=0xF9, 0xDC) + 4 zeros (0xFD, 0x00)
                rleRows[y] = { 0xFDu, 0x00u, 0xF9u, 0xDCu, 0xFDu, 0x00u };
            }
            PushU16BE(sampRec, static_cast<uint16_t>(rleRows[y].size()));
        }
        for (const auto& rr : rleRows) sampRec.insert(sampRec.end(), rr.begin(), rr.end());

        std::vector<uint8_t> abrV62;
        PushU16BE(abrV62, 6u); // version = 6
        PushU16BE(abrV62, 2u); // subVersion = 2
        abrV62.insert(abrV62.end(), { '8', 'B', 'I', 'M', 's', 'a', 'm', 'p' });
        uint32_t sampPayloadLen = static_cast<uint32_t>(4u + ((sampRec.size() + 3u) & ~3u));
        PushU32BE(abrV62, sampPayloadLen);
        PushU32BE(abrV62, static_cast<uint32_t>(sampRec.size()));
        abrV62.insert(abrV62.end(), sampRec.begin(), sampRec.end());
        while ((abrV62.size() - 16u) < sampPayloadLen) abrV62.push_back(0u);

        verifyPair(L"Stage15.Images.PhotoshopAbr_V1RawAndV62PackBits",
                   L"v1.abr", abrV1, L"v62.abr", abrV62, L".abr", 20);

        // Computed-only v1 (type=1, 0 sampled bitmaps) and invalid version (ver=99) must strictly fail!
        std::vector<uint8_t> computedOnlyAbr = {
            0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0E,
            0,0,0,0, 0,25, 0,32, 0,100, 0,0, 0,1
        };
        std::vector<uint8_t> badVerAbr = { 0x00, 99, 0x00, 0x02, '8', 'B', 'I', 'M', 's', 'a', 'm', 'p', 0, 0, 0, 0 };
        ExtendedImageInfo badAbr;
        bool abrRej = !ImageFormatLoader::DecodeImageMemory(computedOnlyAbr.data(), computedOnlyAbr.size(), L".abr", 0, badAbr) &&
                      !ImageFormatLoader::DecodeImageMemory(badVerAbr.data(), badVerAbr.size(), L".abr", 0, badAbr);
        tc.Record(abrRej, L"Stage15.Images.PhotoshopAbr_RejectComputedOnlyNoCircles");
    }

    // 21. GIMP XCF (.xcf) — v011 64-bit Zlib vs RLE, PROP_COLORMAP Indexed, 16-bit Precision + Mask + Group, Corrupt Reject
    {
        auto pushU64BE = [](std::vector<uint8_t>& b, uint64_t v) {
            PushU32BE(b, static_cast<uint32_t>(v >> 32));
            PushU32BE(b, static_cast<uint32_t>(v & 0xFFFFFFFFull));
        };
        auto encodeXcfRleTile = [](const std::vector<uint8_t>& interleaved, size_t numPixels, uint32_t bpp) {
            std::vector<uint8_t> out;
            for (uint32_t ch = 0; ch < bpp; ++ch) {
                // Emit literal chunks of up to 64 bytes (opcode = 256 - len, 192..255)
                size_t px = 0;
                while (px < numPixels) {
                    size_t chunk = std::min<size_t>(64u, numPixels - px);
                    out.push_back(static_cast<uint8_t>(256u - chunk));
                    for (size_t k = 0; k < chunk; ++k) {
                        out.push_back(interleaved[(px + k) * bpp + ch]);
                    }
                    px += chunk;
                }
            }
            return out;
        };

        auto buildSingleLayerXcf = [&](bool v11Zlib, int patternVariant) -> std::vector<uint8_t> {
            const uint32_t w = 8, h = 8, bpp = 4;
            std::vector<uint8_t> rawTile(w * h * bpp);
            for (uint32_t y = 0; y < h; ++y) {
                for (uint32_t x = 0; x < w; ++x) {
                    size_t idx = (y * w + x) * 4u;
                    if (patternVariant == 0) {
                        rawTile[idx + 0] = static_cast<uint8_t>(30 + x * 28);
                        rawTile[idx + 1] = static_cast<uint8_t>(200 - y * 20);
                        rawTile[idx + 2] = static_cast<uint8_t>((x ^ y) * 30 + 15);
                        rawTile[idx + 3] = 255u;
                    } else {
                        rawTile[idx + 0] = static_cast<uint8_t>(240 - x * 25);
                        rawTile[idx + 1] = static_cast<uint8_t>(40 + y * 25);
                        rawTile[idx + 2] = 180u;
                        rawTile[idx + 3] = 255u;
                    }
                }
            }

            std::vector<uint8_t> compTile = v11Zlib
                ? CompressZlibFixed(rawTile)
                : encodeXcfRleTile(rawTile, w * h, bpp);

            std::vector<uint8_t> xcf;
            if (v11Zlib) {
                const char hdr[14] = { 'g','i','m','p',' ','x','c','f',' ','v','0','1','1','\0' };
                xcf.insert(xcf.end(), hdr, hdr + 14);
                PushU32BE(xcf, w);
                PushU32BE(xcf, h);
                PushU32BE(xcf, 0u);   // baseType = RGB
                PushU32BE(xcf, 150u); // precision = 8-bit sRGB
                PushU32BE(xcf, 17u); PushU32BE(xcf, 1u); xcf.push_back(2u); // PROP_COMPRESSION = 2 (Zlib)
                PushU32BE(xcf, 0u);  PushU32BE(xcf, 0u);                    // PROP_END

                size_t layerPtrPos = xcf.size();
                pushU64BE(xcf, 0ull); // layer[0] offset placeholder
                pushU64BE(xcf, 0ull); // end of layers

                uint64_t layerOff = xcf.size();
                PushU32BE(xcf, w); PushU32BE(xcf, h);
                PushU32BE(xcf, 1u); // RGBA
                PushU32BE(xcf, 5u); xcf.insert(xcf.end(), { 'Z','l','i','b','\0' });
                PushU32BE(xcf, 0u); PushU32BE(xcf, 0u); // layer PROP_END

                size_t hierPtrPos = xcf.size();
                pushU64BE(xcf, 0ull); // hierPtr placeholder
                pushU64BE(xcf, 0ull); // maskPtr = 0

                uint64_t hierOff = xcf.size();
                PushU32BE(xcf, w); PushU32BE(xcf, h); PushU32BE(xcf, bpp);
                size_t lvlPtrPos = xcf.size();
                pushU64BE(xcf, 0ull); // level0 placeholder
                pushU64BE(xcf, 0ull); // end levels

                uint64_t lvlOff = xcf.size();
                PushU32BE(xcf, w); PushU32BE(xcf, h);
                size_t tilePtrPos = xcf.size();
                pushU64BE(xcf, 0ull); // tile0 placeholder
                pushU64BE(xcf, 0ull); // end tiles

                uint64_t tileOff = xcf.size();
                xcf.insert(xcf.end(), compTile.begin(), compTile.end());

                auto patch64 = [&](size_t at, uint64_t val) {
                    for (int b = 0; b < 8; ++b) xcf[at + b] = static_cast<uint8_t>((val >> ((7 - b) * 8)) & 0xFFull);
                };
                patch64(layerPtrPos, layerOff);
                patch64(hierPtrPos,  hierOff);
                patch64(lvlPtrPos,   lvlOff);
                patch64(tilePtrPos,  tileOff);
            } else {
                const char hdr[14] = { 'g','i','m','p',' ','x','c','f',' ','v','0','0','3','\0' };
                xcf.insert(xcf.end(), hdr, hdr + 14);
                PushU32BE(xcf, w);
                PushU32BE(xcf, h);
                PushU32BE(xcf, 0u);
                PushU32BE(xcf, 17u); PushU32BE(xcf, 1u); xcf.push_back(1u); // PROP_COMPRESSION = 1 (RLE)
                PushU32BE(xcf, 0u);  PushU32BE(xcf, 0u);

                size_t layerPtrPos = xcf.size();
                PushU32BE(xcf, 0u);
                PushU32BE(xcf, 0u);

                uint32_t layerOff = static_cast<uint32_t>(xcf.size());
                PushU32BE(xcf, w); PushU32BE(xcf, h);
                PushU32BE(xcf, 1u);
                PushU32BE(xcf, 4u); xcf.insert(xcf.end(), { 'R','L','E','\0' });
                PushU32BE(xcf, 0u); PushU32BE(xcf, 0u);

                size_t hierPtrPos = xcf.size();
                PushU32BE(xcf, 0u);
                PushU32BE(xcf, 0u);

                uint32_t hierOff = static_cast<uint32_t>(xcf.size());
                PushU32BE(xcf, w); PushU32BE(xcf, h); PushU32BE(xcf, bpp);
                size_t lvlPtrPos = xcf.size();
                PushU32BE(xcf, 0u);
                PushU32BE(xcf, 0u);

                uint32_t lvlOff = static_cast<uint32_t>(xcf.size());
                PushU32BE(xcf, w); PushU32BE(xcf, h);
                size_t tilePtrPos = xcf.size();
                PushU32BE(xcf, 0u);
                PushU32BE(xcf, 0u);

                uint32_t tileOff = static_cast<uint32_t>(xcf.size());
                xcf.insert(xcf.end(), compTile.begin(), compTile.end());

                auto patch32 = [&](size_t at, uint32_t val) {
                    xcf[at + 0] = static_cast<uint8_t>((val >> 24) & 0xFFu);
                    xcf[at + 1] = static_cast<uint8_t>((val >> 16) & 0xFFu);
                    xcf[at + 2] = static_cast<uint8_t>((val >> 8)  & 0xFFu);
                    xcf[at + 3] = static_cast<uint8_t>(val & 0xFFu);
                };
                patch32(layerPtrPos, layerOff);
                patch32(hierPtrPos,  hierOff);
                patch32(lvlPtrPos,   lvlOff);
                patch32(tilePtrPos,  tileOff);
            }
            return xcf;
        };

        std::vector<uint8_t> xcfZlibA = buildSingleLayerXcf(true, 0);
        std::vector<uint8_t> xcfRleA  = buildSingleLayerXcf(false, 0);
        std::vector<uint8_t> xcfZlibB = buildSingleLayerXcf(true, 1);

        verifyPair(L"Stage15.Images.GimpXcf_V11Zlib64BitPair",
                   L"zlib_a.xcf", xcfZlibA, L"zlib_b.xcf", xcfZlibB, L".xcf", 40);

        ExtendedImageInfo zInfo, rInfo;
        bool zrOk = ImageFormatLoader::DecodeImageMemory(xcfZlibA.data(), xcfZlibA.size(), L".xcf", 0, zInfo) &&
                    ImageFormatLoader::DecodeImageMemory(xcfRleA.data(),  xcfRleA.size(),  L".xcf", 0, rInfo) &&
                    zInfo.width == 8 && rInfo.width == 8 &&
                    zInfo.bgraPixels == rInfo.bgraPixels &&
                    zInfo.bgraPixels[2] == 30 && zInfo.bgraPixels[1] == 200 && zInfo.bgraPixels[0] == 15;
        tc.Record(zrOk, L"Stage15.Images.GimpXcf_V11Zlib64BitMatchesRleExact");

        // Indexed XCF with PROP_COLORMAP (4 entries) + 16-bit Precision=250 XCF with Layer Mask & Group Folder Skip
        std::vector<uint8_t> xcfIdx;
        {
            const char hdr[14] = { 'g','i','m','p',' ','x','c','f',' ','v','0','0','3','\0' };
            xcfIdx.insert(xcfIdx.end(), hdr, hdr + 14);
            PushU32BE(xcfIdx, 4u); PushU32BE(xcfIdx, 4u); PushU32BE(xcfIdx, 2u); // baseType = 2 (Indexed)
            // PROP_COLORMAP (1): 4 colors
            PushU32BE(xcfIdx, 1u); PushU32BE(xcfIdx, 4u + 12u);
            PushU32BE(xcfIdx, 4u);
            xcfIdx.insert(xcfIdx.end(), {
                240, 20, 30,   // idx 0: Red
                20, 220, 50,   // idx 1: Green
                30, 60, 250,   // idx 2: Blue
                250, 210, 10   // idx 3: Gold
            });
            PushU32BE(xcfIdx, 17u); PushU32BE(xcfIdx, 1u); xcfIdx.push_back(0u); // COMPRESSION_NONE
            PushU32BE(xcfIdx, 0u);  PushU32BE(xcfIdx, 0u);

            size_t lpPos = xcfIdx.size();
            PushU32BE(xcfIdx, 0u); PushU32BE(xcfIdx, 0u);
            uint32_t lOff = static_cast<uint32_t>(xcfIdx.size());
            PushU32BE(xcfIdx, 4u); PushU32BE(xcfIdx, 4u);
            PushU32BE(xcfIdx, 4u); // ltype = 4 (Indexed)
            PushU32BE(xcfIdx, 4u); xcfIdx.insert(xcfIdx.end(), { 'P','a','l','\0' });
            PushU32BE(xcfIdx, 0u); PushU32BE(xcfIdx, 0u);
            size_t hpPos = xcfIdx.size();
            PushU32BE(xcfIdx, 0u); PushU32BE(xcfIdx, 0u);
            uint32_t hOff = static_cast<uint32_t>(xcfIdx.size());
            PushU32BE(xcfIdx, 4u); PushU32BE(xcfIdx, 4u); PushU32BE(xcfIdx, 1u); // bpp = 1
            size_t lvpPos = xcfIdx.size();
            PushU32BE(xcfIdx, 0u); PushU32BE(xcfIdx, 0u);
            uint32_t lvOff = static_cast<uint32_t>(xcfIdx.size());
            PushU32BE(xcfIdx, 4u); PushU32BE(xcfIdx, 4u);
            size_t tpPos = xcfIdx.size();
            PushU32BE(xcfIdx, 0u); PushU32BE(xcfIdx, 0u);
            uint32_t tOff = static_cast<uint32_t>(xcfIdx.size());
            for (int i = 0; i < 16; ++i) xcfIdx.push_back(static_cast<uint8_t>(i & 3));

            auto patch32 = [&](size_t at, uint32_t val) {
                xcfIdx[at + 0] = static_cast<uint8_t>((val >> 24) & 0xFFu);
                xcfIdx[at + 1] = static_cast<uint8_t>((val >> 16) & 0xFFu);
                xcfIdx[at + 2] = static_cast<uint8_t>((val >> 8)  & 0xFFu);
                xcfIdx[at + 3] = static_cast<uint8_t>(val & 0xFFu);
            };
            patch32(lpPos, lOff); patch32(hpPos, hOff); patch32(lvpPos, lvOff); patch32(tpPos, tOff);
        }

        // 16-bit Precision=250 v011 XCF with a Group Folder layer (PROP_GROUP_ITEM=29, must be skipped)
        // + a 16-bit RGBA leaf layer (bpp=8) with an active 16-bit Layer Mask (left=0xFFFF, right=0x0000)
        std::vector<uint8_t> xcf16;
        {
            const char hdr[14] = { 'g','i','m','p',' ','x','c','f',' ','v','0','1','1','\0' };
            xcf16.insert(xcf16.end(), hdr, hdr + 14);
            PushU32BE(xcf16, 4u); PushU32BE(xcf16, 4u);
            PushU32BE(xcf16, 0u);   // RGB
            PushU32BE(xcf16, 250u); // Precision = 16-bit non-linear integer (2 bytes/comp -> RGBA bpp=8)
            PushU32BE(xcf16, 17u); PushU32BE(xcf16, 1u); xcf16.push_back(2u); // Zlib
            PushU32BE(xcf16, 0u);  PushU32BE(xcf16, 0u);

            auto patch64 = [&](size_t at, uint64_t val) {
                for (int b = 0; b < 8; ++b) xcf16[at + b] = static_cast<uint8_t>((val >> ((7 - b) * 8)) & 0xFFull);
            };

            size_t l0PtrPos = xcf16.size(); pushU64BE(xcf16, 0ull); // Group folder layer (top)
            size_t l1PtrPos = xcf16.size(); pushU64BE(xcf16, 0ull); // Leaf 16-bit layer (bottom)
            pushU64BE(xcf16, 0ull);

            // Group folder layer filled with pure green (if not skipped, would overwrite leaf with green!)
            uint64_t grpOff = xcf16.size();
            PushU32BE(xcf16, 4u); PushU32BE(xcf16, 4u); PushU32BE(xcf16, 1u);
            PushU32BE(xcf16, 4u); xcf16.insert(xcf16.end(), { 'G','r','p','\0' });
            PushU32BE(xcf16, 29u); PushU32BE(xcf16, 0u); // PROP_GROUP_ITEM = 29
            PushU32BE(xcf16, 0u);  PushU32BE(xcf16, 0u);
            size_t grpHierPtr = xcf16.size(); pushU64BE(xcf16, 0ull); pushU64BE(xcf16, 0ull);
            uint64_t grpHierOff = xcf16.size();
            PushU32BE(xcf16, 4u); PushU32BE(xcf16, 4u); PushU32BE(xcf16, 8u);
            size_t grpLvlPtr = xcf16.size(); pushU64BE(xcf16, 0ull); pushU64BE(xcf16, 0ull);
            uint64_t grpLvlOff = xcf16.size();
            PushU32BE(xcf16, 4u); PushU32BE(xcf16, 4u);
            size_t grpTilePtr = xcf16.size(); pushU64BE(xcf16, 0ull); pushU64BE(xcf16, 0ull);
            std::vector<uint8_t> grpRaw;
            for (int i = 0; i < 16; ++i) {
                PushU16BE(grpRaw, 0u); PushU16BE(grpRaw, 65535u); PushU16BE(grpRaw, 0u); PushU16BE(grpRaw, 65535u);
            }
            std::vector<uint8_t> grpZ = CompressZlibFixed(grpRaw);
            uint64_t grpTileOff = xcf16.size();
            xcf16.insert(xcf16.end(), grpZ.begin(), grpZ.end());
            patch64(grpHierPtr, grpHierOff); patch64(grpLvlPtr, grpLvlOff); patch64(grpTilePtr, grpTileOff);

            // Leaf 16-bit RGBA layer (R=0xC8C8=200, G=0x3232=50, B=0xFAFA=250, A=0xFFFF=255) + 16-bit Mask
            uint64_t leafOff = xcf16.size();
            PushU32BE(xcf16, 4u); PushU32BE(xcf16, 4u); PushU32BE(xcf16, 1u);
            PushU32BE(xcf16, 5u); xcf16.insert(xcf16.end(), { 'L','e','a','f','\0' });
            PushU32BE(xcf16, 7u); PushU32BE(xcf16, 4u); PushU32BE(xcf16, 1u); // PROP_APPLY_MASK = 1
            PushU32BE(xcf16, 0u); PushU32BE(xcf16, 0u);
            size_t leafHierPtr = xcf16.size(); pushU64BE(xcf16, 0ull);
            size_t leafMaskPtr = xcf16.size(); pushU64BE(xcf16, 0ull);

            uint64_t leafHierOff = xcf16.size();
            PushU32BE(xcf16, 4u); PushU32BE(xcf16, 4u); PushU32BE(xcf16, 8u); // bpp = 8 (4 * u16 BE)
            size_t leafLvlPtr = xcf16.size(); pushU64BE(xcf16, 0ull); pushU64BE(xcf16, 0ull);
            uint64_t leafLvlOff = xcf16.size();
            PushU32BE(xcf16, 4u); PushU32BE(xcf16, 4u);
            size_t leafTilePtr = xcf16.size(); pushU64BE(xcf16, 0ull); pushU64BE(xcf16, 0ull);
            std::vector<uint8_t> leafRaw;
            for (int i = 0; i < 16; ++i) {
                PushU16BE(leafRaw, 0xC8C8u); PushU16BE(leafRaw, 0x3232u); PushU16BE(leafRaw, 0xFAFAu); PushU16BE(leafRaw, 0xFFFFu);
            }
            std::vector<uint8_t> leafZ = CompressZlibFixed(leafRaw);
            uint64_t leafTileOff = xcf16.size();
            xcf16.insert(xcf16.end(), leafZ.begin(), leafZ.end());

            // Mask channel (w=4, h=4, name="M\0", PROP_END, maskHierPtr -> bpp=2: left 2 cols=0xFFFF, right 2 cols=0x0000)
            uint64_t maskOff = xcf16.size();
            PushU32BE(xcf16, 4u); PushU32BE(xcf16, 4u);
            PushU32BE(xcf16, 2u); xcf16.insert(xcf16.end(), { 'M','\0' });
            PushU32BE(xcf16, 0u); PushU32BE(xcf16, 0u);
            size_t mHierPtr = xcf16.size(); pushU64BE(xcf16, 0ull);
            uint64_t mHierOff = xcf16.size();
            PushU32BE(xcf16, 4u); PushU32BE(xcf16, 4u); PushU32BE(xcf16, 2u); // bpp = 2 (1 * u16 BE)
            size_t mLvlPtr = xcf16.size(); pushU64BE(xcf16, 0ull); pushU64BE(xcf16, 0ull);
            uint64_t mLvlOff = xcf16.size();
            PushU32BE(xcf16, 4u); PushU32BE(xcf16, 4u);
            size_t mTilePtr = xcf16.size(); pushU64BE(xcf16, 0ull); pushU64BE(xcf16, 0ull);
            std::vector<uint8_t> mRaw;
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) PushU16BE(mRaw, (x < 2) ? 0xFFFFu : 0x0000u);
            }
            std::vector<uint8_t> mZ = CompressZlibFixed(mRaw);
            uint64_t mTileOff = xcf16.size();
            xcf16.insert(xcf16.end(), mZ.begin(), mZ.end());

            patch64(l0PtrPos, grpOff);
            patch64(l1PtrPos, leafOff);
            patch64(leafHierPtr, leafHierOff); patch64(leafLvlPtr, leafLvlOff); patch64(leafTilePtr, leafTileOff);
            patch64(leafMaskPtr, maskOff);
            patch64(mHierPtr, mHierOff); patch64(mLvlPtr, mLvlOff); patch64(mTilePtr, mTileOff);
        }

        ExtendedImageInfo idxInfo, hi16Info;
        bool idxAnd16Ok =
            ImageFormatLoader::DecodeImageMemory(xcfIdx.data(), xcfIdx.size(), L".xcf", 0, idxInfo) &&
            ImageFormatLoader::DecodeImageMemory(xcf16.data(),  xcf16.size(),  L".xcf", 0, hi16Info) &&
            idxInfo.bgraPixels.size() == 64 &&
            // Pixel 0 -> idx 0 (240, 20, 30), Pixel 1 -> idx 1 (20, 220, 50), Pixel 2 -> idx 2 (30, 60, 250)
            idxInfo.bgraPixels[2] == 240 && idxInfo.bgraPixels[1] == 20 && idxInfo.bgraPixels[0] == 30 &&
            idxInfo.bgraPixels[6] == 20  && idxInfo.bgraPixels[5] == 220 && idxInfo.bgraPixels[4] == 50 &&
            idxInfo.bgraPixels[10] == 30 && idxInfo.bgraPixels[9] == 60 && idxInfo.bgraPixels[8] == 250 &&
            hi16Info.bitsPerChannel == 16 &&
            // Left half (x=0,y=0): opaque (200, 50, 250, 255) — NOT overwritten by green group projection!
            hi16Info.bgraPixels[2] == 200 && hi16Info.bgraPixels[1] == 50 && hi16Info.bgraPixels[0] == 250 && hi16Info.bgraPixels[3] == 255 &&
            // Right half (x=3,y=0): masked out to alpha == 0!
            hi16Info.bgraPixels[3 * 4 + 3] == 0;
        tc.Record(idxAnd16Ok, L"Stage15.Images.GimpXcf_IndexedColormapAnd16BitMaskGroup");

        // Corrupt Zlib tile & truncated RLE tile MUST return false (never a blank canvas!)
        std::vector<uint8_t> corruptZlibXcf = xcfZlibA;
        corruptZlibXcf[corruptZlibXcf.size() - 6] ^= 0xFFu;
        corruptZlibXcf[corruptZlibXcf.size() - 5] ^= 0xAAu;
        std::vector<uint8_t> corruptRleXcf(xcfRleA.begin(), xcfRleA.end() - 20);
        ExtendedImageInfo badXcf1, badXcf2;
        bool xcfRejOk = !ImageFormatLoader::DecodeImageMemory(corruptZlibXcf.data(), corruptZlibXcf.size(), L".xcf", 0, badXcf1) &&
                        !badXcf1.valid &&
                        !ImageFormatLoader::DecodeImageMemory(corruptRleXcf.data(), corruptRleXcf.size(), L".xcf", 0, badXcf2) &&
                        !badXcf2.valid;
        tc.Record(xcfRejOk, L"Stage15.Images.GimpXcf_RejectCorruptTilesNoBlankCanvas");
    }

    // 22. SMPTE 268M DPX (.dpx) & Kodak Cineon (.cin) — 8-bit RGBA, 10-bit Method A vs B, 16-bit BE, Cineon Log, Truncated Reject
    {
        auto buildDpxHeader = [&](bool littleEndian, uint32_t w, uint32_t h, uint8_t desc, uint8_t transfer,
                                  uint8_t bitSize, uint16_t packing, uint16_t encoding) -> std::vector<uint8_t>
        {
            std::vector<uint8_t> d(2048, 0);
            std::memcpy(d.data(), littleEndian ? "XPDS" : "SDPX", 4);
            auto wr16 = [&](size_t off, uint16_t v) {
                if (littleEndian) { d[off] = v & 0xFFu; d[off + 1] = (v >> 8) & 0xFFu; }
                else              { d[off] = (v >> 8) & 0xFFu; d[off + 1] = v & 0xFFu; }
            };
            auto wr32 = [&](size_t off, uint32_t v) {
                if (littleEndian) {
                    d[off] = v & 0xFFu; d[off + 1] = (v >> 8) & 0xFFu;
                    d[off + 2] = (v >> 16) & 0xFFu; d[off + 3] = (v >> 24) & 0xFFu;
                } else {
                    d[off] = (v >> 24) & 0xFFu; d[off + 1] = (v >> 16) & 0xFFu;
                    d[off + 2] = (v >> 8) & 0xFFu;  d[off + 3] = v & 0xFFu;
                }
            };
            wr32(4, 2048u);    // dataOff = 2048
            wr16(768, 0u);     // orientation = 0
            wr16(770, 1u);     // numElements = 1
            wr32(772, w);
            wr32(776, h);
            wr32(780, 0u);     // dataSign = 0 (unsigned)
            d[800] = desc;
            d[801] = transfer;
            d[803] = bitSize;
            wr16(804, packing);
            wr16(806, encoding);
            wr32(808, 2048u);  // elemDataOff = 2048
            return d;
        };

        // 8-bit RGBA DPX (descriptor=51, bitSize=8) vs 16-bit BE RGB DPX (descriptor=50, bitSize=16)
        std::vector<uint8_t> dpx8Rgba = buildDpxHeader(false, 4, 4, 51, 2, 8, 0, 0);
        for (int i = 0; i < 16; ++i) {
            dpx8Rgba.push_back(static_cast<uint8_t>(200 - i * 5)); // R
            dpx8Rgba.push_back(static_cast<uint8_t>(40 + i * 10)); // G
            dpx8Rgba.push_back(90u);                               // B
            dpx8Rgba.push_back((i == 0) ? 255u : 128u);            // A
        }

        std::vector<uint8_t> dpx16Be = buildDpxHeader(false, 4, 4, 50, 2, 16, 0, 0);
        for (int i = 0; i < 16; ++i) {
            PushU16BE(dpx16Be, 0xF0F0u); // R ~ 240
            PushU16BE(dpx16Be, 0x5050u); // G ~ 80
            PushU16BE(dpx16Be, 0x2020u); // B ~ 32
        }

        verifyPair(L"Stage15.Images.SmpteDpx_8BitRgbaAnd16BitBE",
                   L"rgba8.dpx", dpx8Rgba, L"rgb16be.dpx", dpx16Be, L".dpx", 2052);

        // 10-bit Method A (packing=1: [R:31..22][G:21..12][B:11..2]) vs Method B (packing=2: [R:29..20][G:19..10][B:9..0])
        std::vector<uint8_t> dpx10A = buildDpxHeader(false, 4, 4, 50, 2, 10, 1, 0);
        std::vector<uint8_t> dpx10B = buildDpxHeader(false, 4, 4, 50, 2, 10, 2, 0);
        const uint32_t r10 = 1023u, g10 = 512u, b10 = 128u;
        for (int i = 0; i < 16; ++i) {
            PushU32BE(dpx10A, (r10 << 22) | (g10 << 12) | (b10 << 2));
            PushU32BE(dpx10B, (r10 << 20) | (g10 << 10) | b10);
        }
        // Also test SMPTE 268M RLE (encoding=1) on 10-bit Method A: 1 repeat run of 16 pixels (count=16 -> (16<<1)|0 = 32)
        std::vector<uint8_t> dpx10Rle = buildDpxHeader(false, 4, 4, 50, 2, 10, 1, 1);
        PushU32BE(dpx10Rle, (16u << 1) | 0u);
        PushU32BE(dpx10Rle, (r10 << 22) | (g10 << 12) | (b10 << 2));

        ExtendedImageInfo i8, i16, i10A, i10B, i10Rle;
        bool dpxModesOk =
            ImageFormatLoader::DecodeImageMemory(dpx8Rgba.data(), dpx8Rgba.size(), L".dpx", 0, i8) &&
            ImageFormatLoader::DecodeImageMemory(dpx16Be.data(),  dpx16Be.size(),  L".dpx", 0, i16) &&
            ImageFormatLoader::DecodeImageMemory(dpx10A.data(),   dpx10A.size(),   L".dpx", 0, i10A) &&
            ImageFormatLoader::DecodeImageMemory(dpx10B.data(),   dpx10B.size(),   L".dpx", 0, i10B) &&
            ImageFormatLoader::DecodeImageMemory(dpx10Rle.data(), dpx10Rle.size(), L".dpx", 0, i10Rle) &&
            i8.bgraPixels[2] == 200 && i8.bgraPixels[1] == 40 && i8.bgraPixels[0] == 90 && i8.bgraPixels[3] == 255 &&
            std::abs(static_cast<int>(i16.bgraPixels[2]) - 240) <= 1 &&
            std::abs(static_cast<int>(i16.bgraPixels[1]) - 80)  <= 1 &&
            std::abs(static_cast<int>(i16.bgraPixels[0]) - 32)  <= 1 &&
            i10A.bgraPixels == i10B.bgraPixels &&
            i10A.bgraPixels == i10Rle.bgraPixels &&
            i10A.bgraPixels[2] == 255 &&
            std::abs(static_cast<int>(i10A.bgraPixels[1]) - 128) <= 1 &&
            std::abs(static_cast<int>(i10A.bgraPixels[0]) - 32)  <= 1;
        tc.Record(dpxModesOk, L"Stage15.Images.SmpteDpx_10BitMethodAVsMethodBAndRleExact");

        // Kodak Cineon (.cin) 10-bit Printing-Density Log (code 95 black -> 0, code 685 white -> 255, code 470 midtone)
        auto buildCineon = [&](uint32_t c0, uint32_t c1, uint32_t c2) -> std::vector<uint8_t> {
            std::vector<uint8_t> cin(1024, 0);
            auto wr32 = [&](size_t off, uint32_t v) {
                cin[off + 0] = static_cast<uint8_t>((v >> 24) & 0xFFu);
                cin[off + 1] = static_cast<uint8_t>((v >> 16) & 0xFFu);
                cin[off + 2] = static_cast<uint8_t>((v >> 8)  & 0xFFu);
                cin[off + 3] = static_cast<uint8_t>(v & 0xFFu);
            };
            wr32(0, 0x802A5FD7u); // Kodak Cineon BE magic
            wr32(4, 1024u);       // dataOff = 1024
            cin[192] = 0u;        // orientation
            cin[193] = 3u;        // 3 channels
            cin[198] = 10u;       // 10 bits/pixel
            wr32(200, 4u);        // pixelsPerLine = 4
            wr32(204, 4u);        // linesPerImage = 4
            cin[681] = 5u;        // 32-bit left-justified Method A
            for (int i = 0; i < 16; ++i) {
                uint32_t code = (i == 0) ? c0 : ((i == 1) ? c1 : c2);
                PushU32BE(cin, (code << 22) | (code << 12) | (code << 2));
            }
            return cin;
        };

        std::vector<uint8_t> cinA = buildCineon(95u, 685u, 470u);
        std::vector<uint8_t> cinB = buildCineon(685u, 300u, 150u);
        verifyPair(L"Stage15.Images.KodakCineon_10BitLogPair",
                   L"scan_a.cin", cinA, L"scan_b.cin", cinB, L".cin", 1040);

        ExtendedImageInfo cinInfo, truncDpxInfo, truncCinInfo;
        bool cinLogAndTruncOk =
            ImageFormatLoader::DecodeImageMemory(cinA.data(), cinA.size(), L".cin", 0, cinInfo) &&
            cinInfo.bgraPixels[2] <= 2 &&                  // code 95 film black -> ~0
            cinInfo.bgraPixels[4 + 2] >= 253 &&            // code 685 film white -> ~255
            cinInfo.bgraPixels[8 + 2] >= 80 && cinInfo.bgraPixels[8 + 2] <= 140 && // code 470 midtone
            !ImageFormatLoader::DecodeImageMemory(dpx10A.data(), 2048 + 32, L".dpx", 0, truncDpxInfo) &&
            !ImageFormatLoader::DecodeImageMemory(cinA.data(),   1024 + 32, L".cin", 0, truncCinInfo);
        tc.Record(cinLogAndTruncOk, L"Stage15.Images.KodakCineon_PrintingDensityLogAndTruncatedReject");
    }

    // 23. ANSI CP437 Art (.ans) & DEC VT340 Sixel (.sixel) — Box-Drawing, 24-bit TrueColor, SAUCE, VT340 HLS/RGB, Garbage Reject
    {
        // Build .ans with 24-bit TrueColor fg (240,120,30) + bg (10,20,40), CP437 box-drawing ┌─┐ (0xDA,0xC4,0xBF) + │ (0xB3),
        // iCE bright bg via SGR 5, and trailing 128-byte SAUCE00 specifying 40 columns (width = 320) & TFlags = 1
        std::string ansText =
            "\x1B[38;2;240;120;30;48;2;10;20;40m\xDA\xC4\xBF\r\n"
            "\xB3\x1B[5;30;44m*\x1B[25;38;2;240;120;30;48;2;10;20;40m\xB3\r\n"
            "\xC0\xC4\xD9";
        std::vector<uint8_t> ansA(ansText.begin(), ansText.end());
        ansA.push_back(0x1Au); // DOS EOF before SAUCE
        std::vector<uint8_t> sauce(128, 0);
        std::memcpy(sauce.data(), "SAUCE00", 7);
        sauce[94] = 1u; // DataType = Character
        sauce[95] = 1u; // FileType = ANSi
        sauce[96] = 40u; sauce[97] = 0u; // TInfo1 = 40 columns -> 320 px width
        sauce[105] = 1u; // TFlags bit 0 = iCE Colors
        sauce[106] = 1u;
        ansA.insert(ansA.end(), sauce.begin(), sauce.end());

        std::string ansTextB = "\x1B[38;5;196;48;5;21m\xDB\xB2\xB1\xB0 ANSI 256 \xDF\xDC\r\n";
        std::vector<uint8_t> ansB(ansTextB.begin(), ansTextB.end());

        verifyPair(L"Stage15.Images.AnsiArt_Cp437Pair",
                   L"box_sauce.ans", ansA, L"xterm256.ans", ansB, L".ans", 0);

        ExtendedImageInfo ansInfo;
        bool ansExactOk = ImageFormatLoader::DecodeImageMemory(ansA.data(), ansA.size(), L".ans", 0, ansInfo) &&
                          ansInfo.width == 320 && ansInfo.height == 48;
        if (ansExactOk) {
            auto pxMatch = [&](uint32_t x, uint32_t y, uint8_t er, uint8_t eg, uint8_t eb) {
                const uint8_t* p = &ansInfo.bgraPixels[(static_cast<size_t>(y) * ansInfo.width + x) * 4u];
                return p[2] == er && p[1] == eg && p[0] == eb && p[3] == 255;
            };
            // Cell (0,0) = 0xDA '┌': center (3,7) and right boundary (7,7) and bottom boundary (3,15) are fg (240,120,30); (0,0) is bg (10,20,40)
            // Cell (1,0) = 0xC4 '─': left boundary (8,7) and right boundary (15,7) connect seamlessly in fg (240,120,30)!
            // Cell (1,1) has SGR 5 + 44m (blue bg=4 -> iCE bright blue kCga16[12] = {85, 85, 255}) at background pixel (8, 16)!
            ansExactOk = pxMatch(0,  0,  10,  20,  40) &&
                         pxMatch(3,  7, 240, 120,  30) &&
                         pxMatch(7,  7, 240, 120,  30) &&
                         pxMatch(3, 15, 240, 120,  30) &&
                         pxMatch(8,  7, 240, 120,  30) &&
                         pxMatch(15, 7, 240, 120,  30) &&
                         pxMatch(8, 16,  85,  85, 255);
        }
        tc.Record(ansExactOk, L"Stage15.Images.AnsiArt_Cp437BoxDrawingTrueColorAndSauceExact");

        // VT340 Sixel with HLS (Blue=0°, Red=120°, Green=240°), RGB (100%, 50%, 0%), and unwritten register #500 (must be {0,0,0}, no hash!)
        std::string sixA_str =
            "\x1BP0;0;0q\"1;1;20;6"
            "#10;1;0;50;100!4~"
            "#11;1;120;50;100!4~"
            "#12;1;240;50;100!4~"
            "#20;2;100;50;0!4~"
            "#500!4~"
            "\x1B\\";
        std::string sixB_str = "\x1BPq\"1;1;12;12#4;2;10;80;90!12~-#5;2;90;20;70!12~\x1B\\";
        std::vector<uint8_t> sixA(sixA_str.begin(), sixA_str.end());
        std::vector<uint8_t> sixB(sixB_str.begin(), sixB_str.end());

        verifyPair(L"Stage15.Images.Vt340Sixel_HlsAndRgbPair",
                   L"hls.sixel", sixA, L"rgb.sixel", sixB, L".sixel", 6);

        ExtendedImageInfo sixInfo;
        bool sixExactOk = ImageFormatLoader::DecodeImageMemory(sixA.data(), sixA.size(), L".sixel", 0, sixInfo) &&
                          sixInfo.width == 20 && sixInfo.height == 6;
        if (sixExactOk) {
            auto sixPx = [&](uint32_t x, uint8_t er, uint8_t eg, uint8_t eb) {
                const uint8_t* p = &sixInfo.bgraPixels[(2u * sixInfo.width + x) * 4u];
                return std::abs(static_cast<int>(p[2]) - er) <= 2 &&
                       std::abs(static_cast<int>(p[1]) - eg) <= 2 &&
                       std::abs(static_cast<int>(p[0]) - eb) <= 2;
            };
            sixExactOk = sixPx(1,    0,   0, 255) && // #10 HLS 0°   -> Pure Blue
                         sixPx(5,  255,   0,   0) && // #11 HLS 120° -> Pure Red
                         sixPx(9,    0, 255,   0) && // #12 HLS 240° -> Pure Green
                         sixPx(13, 255, 128,   0) && // #20 RGB 100%,50%,0% -> Orange
                         sixPx(17,   0,   0,   0);   // #500 unwritten -> Black (NO fake hash color!)
        }
        tc.Record(sixExactOk, L"Stage15.Images.Vt340Sixel_HlsAndRgbRegistersExactColors");

        // Reject binary NUL garbage & zero-pixel Sixel streams
        std::vector<uint8_t> nulBin = { 0x00, 0xFF, 0x01, 0x02, 0x00, 0x89, 0x50, 0x4E, 0x00, 0x03, 0x04, 0x00 };
        std::string emptySixStr = "\x1BPq#1;2;100;0;0\x1B\\";
        std::vector<uint8_t> emptySix(emptySixStr.begin(), emptySixStr.end());
        ExtendedImageInfo badAns, badSix1, badSix2;
        bool rejAnsSix = !ImageFormatLoader::DecodeImageMemory(nulBin.data(), nulBin.size(), L".ans", 0, badAns) &&
                         !ImageFormatLoader::DecodeImageMemory(nulBin.data(), nulBin.size(), L".sixel", 0, badSix1) &&
                         !ImageFormatLoader::DecodeImageMemory(emptySix.data(), emptySix.size(), L".sixel", 0, badSix2);
        tc.Record(rejAnsSix, L"Stage15.Images.AnsiAndSixel_RejectBinaryGarbageAndEmptySixel");
    }

    // 24. W3C SVG (.svg) — Compact Elliptical Arc Flags & Rotation, <g transform> Stack Pop, <defs> Skip & <linearGradient>/<radialGradient>
    {
        auto strToBytes = [](const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); };

        // Minified compact arc flags: "a35,35 0 1050,0" (largeArc=1, sweep=0, dx=50, dy=0)
        // vs "a35,35 0 0150,0" (largeArc=0, sweep=1, dx=50, dy=0)
        std::vector<uint8_t> svgArcLarge = strToBytes(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 128 128\">"
            "<path d=\"M39,64a35,35 0 1050,0Z\" fill=\"#ef4444\"/></svg>");
        std::vector<uint8_t> svgArcSmall = strToBytes(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 128 128\">"
            "<path d=\"M39,64a35,35 0 0150,0Z\" fill=\"#ef4444\"/></svg>");
        std::vector<uint8_t> svgArcRot0 = strToBytes(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 128 128\">"
            "<path d=\"M24,64 A48,16 0 1,0 104,64 Z\" fill=\"#3b82f6\"/></svg>");
        std::vector<uint8_t> svgArcRot55 = strToBytes(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 128 128\">"
            "<path d=\"M24,64 A48,16 55 1,0 104,64 Z\" fill=\"#3b82f6\"/></svg>");

        verifyPair(L"Stage15.Images.Svg_CompactEllipticalArcPair",
                   L"arc_large.svg", svgArcLarge, L"arc_small.svg", svgArcSmall, L".svg", 8);

        ExtendedImageInfo arcLInfo, arcSInfo, arcR0Info, arcR55Info;
        bool arcOk =
            ImageFormatLoader::DecodeImageMemory(svgArcLarge.data(), svgArcLarge.size(), L".svg", 128, arcLInfo) &&
            ImageFormatLoader::DecodeImageMemory(svgArcSmall.data(), svgArcSmall.size(), L".svg", 128, arcSInfo) &&
            ImageFormatLoader::DecodeImageMemory(svgArcRot0.data(),  svgArcRot0.size(),  L".svg", 128, arcR0Info) &&
            ImageFormatLoader::DecodeImageMemory(svgArcRot55.data(), svgArcRot55.size(), L".svg", 128, arcR55Info);
        size_t litLarge = 0, litSmall = 0;
        if (arcOk) {
            for (size_t i = 3; i < arcLInfo.bgraPixels.size(); i += 4) if (arcLInfo.bgraPixels[i] > 0) ++litLarge;
            for (size_t i = 3; i < arcSInfo.bgraPixels.size(); i += 4) if (arcSInfo.bgraPixels[i] > 0) ++litSmall;
            arcOk = (litLarge > litSmall * 2) &&
                    (HashBgraBytes(arcR0Info.bgraPixels) != HashBgraBytes(arcR55Info.bgraPixels));
        }
        tc.Record(arcOk, L"Stage15.Images.Svg_CompactArcFlagsLargeVsSmallAndRotation");

        // <defs> non-rendering shape skip + <linearGradient> & <radialGradient> url(#id) + nested <g transform> stack pop
        std::vector<uint8_t> svgGradGroup = strToBytes(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 128 128\" fill=\"#102030\">"
            "  <defs>"
            "    <linearGradient id=\"linG\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"0%\">"
            "      <stop offset=\"0%\" stop-color=\"#ff0000\"/>"
            "      <stop offset=\"100%\" stop-color=\"#0000ff\"/>"
            "    </linearGradient>"
            "    <radialGradient id=\"radG\" cx=\"50%\" cy=\"50%\" r=\"50%\">"
            "      <stop offset=\"0%\" stop-color=\"#00ff00\"/>"
            "      <stop offset=\"100%\" stop-color=\"#000000\"/>"
            "    </radialGradient>"
            "    <rect x=\"0\" y=\"0\" width=\"128\" height=\"128\" fill=\"#00ff00\"/>"
            "  </defs>"
            "  <rect x=\"0\" y=\"0\" width=\"128\" height=\"32\" fill=\"url(#linG)\"/>"
            "  <circle cx=\"32\" cy=\"64\" r=\"20\" fill=\"url(#radG)\"/>"
            "  <g transform=\"translate(96, 64) rotate(90)\" fill=\"#facc15\">"
            "    <rect x=\"-12\" y=\"-6\" width=\"24\" height=\"12\"/>"
            "  </g>"
            "  <rect x=\"8\" y=\"96\" width=\"24\" height=\"24\"/>"
            "</svg>");

        ExtendedImageInfo ggInfo;
        bool ggOk = ImageFormatLoader::DecodeImageMemory(svgGradGroup.data(), svgGradGroup.size(), L".svg", 128, ggInfo) &&
                    ggInfo.width == 128 && ggInfo.height == 128;
        if (ggOk) {
            auto pxAt = [&](uint32_t x, uint32_t y) -> const uint8_t* {
                return &ggInfo.bgraPixels[(static_cast<size_t>(y) * 128u + x) * 4u];
            };
            const uint8_t* leftG  = pxAt(2, 16);   // Start of linearGradient -> Red (#ff0000)
            const uint8_t* midG   = pxAt(64, 16);  // Midpoint of linearGradient -> Purple (~128, 0, ~128)
            const uint8_t* rightG = pxAt(125, 16); // End of linearGradient -> Blue (#0000ff)
            const uint8_t* radCtr = pxAt(32, 64);  // Center of radialGradient -> Green (#00ff00)
            const uint8_t* grpPx  = pxAt(96, 72);  // Rotated 90° rect inside <g>: x=[-12..12] became y=[52..76] around (96,64)!
            const uint8_t* popPx  = pxAt(16, 104); // Sibling rect after </g>: untransformed + root fill #102030!
            const uint8_t* bgPx   = pxAt(120, 120);// Untouched corner: MUST be alpha=0 (proving <defs> rect was NOT drawn!)

            ggOk = (leftG[2] > 235 && leftG[0] < 20 && leftG[3] == 255) &&
                   (midG[2] > 100 && midG[2] < 156 && midG[0] > 100 && midG[0] < 156 && midG[1] == 0) &&
                   (rightG[0] > 235 && rightG[2] < 20 && rightG[3] == 255) &&
                   (radCtr[1] > 230 && radCtr[2] < 25 && radCtr[0] < 25) &&
                   (grpPx[2] == 0xFA && grpPx[1] == 0xCC && grpPx[0] == 0x15 && grpPx[3] == 255) &&
                   (popPx[2] == 0x10 && popPx[1] == 0x20 && popPx[0] == 0x30 && popPx[3] == 255) &&
                   (bgPx[3] == 0);
        }
        tc.Record(ggOk, L"Stage15.Images.Svg_GroupTransformStackDefsSkipAndGradients");
    }

    RunFormatTestsImagesBatch70(tc);
}

} // namespace Explorer2::SelfTestSuite
