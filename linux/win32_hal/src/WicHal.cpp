// linux/win32_hal/src/WicHal.cpp
// Pure-C++ Windows Imaging Component (WIC) HAL for Linux (32-bit & 64-bit, zero external codec deps).
// Supports:
//   - Decoders: PNG (1/2/4/8/16-bit, all color types, Adam7), JPEG (SOF0/SOF1/SOF2 progressive, 4:4:4/4:2:2/4:2:0, Gray/YCbCr/RGB, EXIF APP1),
//               BMP/DIB (1/4/8/16/24/32-bpp), GIF (87a/89a LZW), ICO/CUR (PNG & DIB frames), plus ImageFormatLoader fallback (TIFF/WebP/etc.)
//   - Encoders: PNG, JPEG (Baseline DCT with quality scaling via IPropertyBag2 "ImageQuality"), BMP
//   - Pipeline: IWICStream, IWICFormatConverter (BGRA/PBGRA/RGBA/BGR/RGB/Gray), IWICBitmapScaler, IWICBitmapFlipRotator, IWICBitmap, IWICMetadataQueryReader

#include <wincodec.h>
#include "ZipDeflate.h"
#include "Crc32.h"
#include "ImageFormatLoader.h"
#include "PathUtils.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Forward declaration from Win32HalCore.cpp
HRESULT CreateWicStreamInstance(IWICStream** ppStream);

namespace Explorer2::Hal {

namespace {

inline uint16_t Rd16BE(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
inline uint32_t Rd32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  | static_cast<uint32_t>(p[3]);
}
inline uint16_t Rd16LE(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t Rd32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline void Wr32BE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
}
inline void Wr16LE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}
inline void Wr32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
}

struct ExifMetaMap {
    uint16_t orientation = 1;
    bool hasOrientation = false;
    std::map<std::wstring, std::string> strProps;
    std::map<std::wstring, uint32_t> u32Props;
    std::map<std::wstring, uint64_t> u64RatProps; // (den << 32) | num, matching WIC VT_UI8 rational layout
};

struct DecodedImage {
    uint32_t w = 0;
    uint32_t h = 0;
    GUID container = GUID_ContainerFormatPng;
    std::vector<uint8_t> bgra; // Straight-alpha BGRA8
    ExifMetaMap exif;
};

// ==============================================================================================================
// EXIF APP1 TIFF IFD Parser (for ImageCache orientation & camera metadata)
// ==============================================================================================================
void ParseTiffIfd(const uint8_t* tiff, size_t tiffLen, size_t ifdOff, bool le, bool isExifSub, ExifMetaMap& out, int depth = 0) {
    if (depth > 3 || ifdOff + 2 > tiffLen) return;
    auto r16 = [&](size_t o) -> uint16_t { return le ? Rd16LE(tiff + o) : Rd16BE(tiff + o); };
    auto r32 = [&](size_t o) -> uint32_t { return le ? Rd32LE(tiff + o) : Rd32BE(tiff + o); };

    const uint16_t count = r16(ifdOff);
    size_t pos = ifdOff + 2;
    for (uint16_t i = 0; i < count && pos + 12 <= tiffLen; ++i, pos += 12) {
        const uint16_t tag = r16(pos);
        const uint16_t type = r16(pos + 2);
        const uint32_t num = r32(pos + 4);
        const uint32_t valOff = r32(pos + 8);

        if (!isExifSub && tag == 34665u && (type == 4u || type == 3u)) { // ExifIFDPointer
            ParseTiffIfd(tiff, tiffLen, valOff, le, true, out, depth + 1);
            continue;
        }
        const std::wstring prefix = isExifSub ? L"/app1/ifd/exif/{ushort=" : L"/app1/ifd/{ushort=";
        const std::wstring key = prefix + std::to_wstring(tag) + L"}";

        if (tag == 274u && !isExifSub && (type == 3u || type == 4u) && num >= 1) {
            uint16_t ori = (type == 3u) ? r16(pos + 8) : static_cast<uint16_t>(valOff);
            if (ori >= 1 && ori <= 8) {
                out.orientation = ori;
                out.hasOrientation = true;
            }
        } else if (type == 2u && num > 0 && num < 4096) { // ASCII
            size_t sOff = (num <= 4) ? (pos + 8) : valOff;
            if (sOff + num <= tiffLen) {
                std::string s(reinterpret_cast<const char*>(tiff + sOff), num);
                while (!s.empty() && s.back() == '\0') s.pop_back();
                out.strProps[key] = std::move(s);
            }
        } else if ((type == 3u || type == 4u) && num >= 1) {
            uint32_t v = (type == 3u) ? r16(pos + 8) : valOff;
            out.u32Props[key] = v;
        } else if (type == 5u && num >= 1 && valOff + 8 <= tiffLen) { // RATIONAL (num/den)
            uint32_t rn = r32(valOff);
            uint32_t rd = r32(valOff + 4);
            out.u64RatProps[key] = (static_cast<uint64_t>(rd) << 32) | rn;
        }
    }
}

void ParseJpegApp1Exif(const uint8_t* seg, size_t segLen, ExifMetaMap& out) {
    if (segLen >= 14 && std::memcmp(seg, "Exif\0\0", 6) == 0) {
        const uint8_t* tiff = seg + 6;
        const size_t tiffLen = segLen - 6;
        bool le = false;
        if (tiff[0] == 'I' && tiff[1] == 'I' && Rd16LE(tiff + 2) == 42) le = true;
        else if (tiff[0] == 'M' && tiff[1] == 'M' && Rd16BE(tiff + 2) == 42) le = false;
        else return;
        const uint32_t ifd0 = le ? Rd32LE(tiff + 4) : Rd32BE(tiff + 4);
        ParseTiffIfd(tiff, tiffLen, ifd0, le, false, out, 0);
    }
}

// ==============================================================================================================
// 1. Pure-C++ PNG Decoder & Encoder
// ==============================================================================================================
inline uint8_t PaethPred(int a, int b, int c) {
    int p = a + b - c;
    int pa = std::abs(p - a);
    int pb = std::abs(p - b);
    int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<uint8_t>(a);
    return (pb <= pc) ? static_cast<uint8_t>(b) : static_cast<uint8_t>(c);
}

bool DecodePngPure(const uint8_t* data, size_t size, DecodedImage& out) {
    static const uint8_t kPngSig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (!data || size < 33 || std::memcmp(data, kPngSig, 8) != 0) return false;

    uint32_t w = 0, h = 0;
    uint8_t bitDepth = 0, colorType = 0, comp = 0, filter = 0, interlace = 0;
    bool gotIhdr = false, gotIend = false;
    std::vector<uint8_t> palRgb;
    std::vector<uint8_t> trns;
    std::vector<uint8_t> idat;

    size_t pos = 8;
    while (pos + 12 <= size) {
        uint32_t cLen = Rd32BE(data + pos);
        const uint8_t* cType = data + pos + 4;
        if (cLen > size - (pos + 12)) return false;
        const uint8_t* cData = data + pos + 8;
        pos += 12 + cLen;

        if (std::memcmp(cType, "IHDR", 4) == 0) {
            if (cLen != 13 || gotIhdr) return false;
            w = Rd32BE(cData);
            h = Rd32BE(cData + 4);
            bitDepth  = cData[8];
            colorType = cData[9];
            comp      = cData[10];
            filter    = cData[11];
            interlace = cData[12];
            if (w == 0 || h == 0 || w > 16384 || h > 16384 || comp != 0 || filter != 0 || interlace > 1) return false;
            gotIhdr = true;
        } else if (std::memcmp(cType, "PLTE", 4) == 0) {
            if (cLen % 3 != 0 || cLen > 768) return false;
            palRgb.assign(cData, cData + cLen);
        } else if (std::memcmp(cType, "tRNS", 4) == 0) {
            trns.assign(cData, cData + cLen);
        } else if (std::memcmp(cType, "eXIf", 4) == 0 && cLen >= 8) {
            bool le = (cData[0] == 'I' && cData[1] == 'I');
            if (le || (cData[0] == 'M' && cData[1] == 'M')) {
                uint32_t ifd0 = le ? Rd32LE(cData + 4) : Rd32BE(cData + 4);
                ParseTiffIfd(cData, cLen, ifd0, le, false, out.exif, 0);
            }
        } else if (std::memcmp(cType, "IDAT", 4) == 0) {
            idat.insert(idat.end(), cData, cData + cLen);
        } else if (std::memcmp(cType, "IEND", 4) == 0) {
            gotIend = true;
            break;
        }
    }
    if (!gotIhdr || !gotIend || idat.empty()) return false;

    int channels = 0;
    switch (colorType) {
    case 0: channels = 1; if (bitDepth != 1 && bitDepth != 2 && bitDepth != 4 && bitDepth != 8 && bitDepth != 16) return false; break;
    case 2: channels = 3; if (bitDepth != 8 && bitDepth != 16) return false; break;
    case 3: channels = 1; if ((bitDepth != 1 && bitDepth != 2 && bitDepth != 4 && bitDepth != 8) || palRgb.empty()) return false; break;
    case 4: channels = 2; if (bitDepth != 8 && bitDepth != 16) return false; break;
    case 6: channels = 4; if (bitDepth != 8 && bitDepth != 16) return false; break;
    default: return false;
    }

    const size_t bitsPerPixel = static_cast<size_t>(channels) * bitDepth;
    const size_t bppFilter = (bitsPerPixel + 7u) / 8u;

    auto rowStrideForW = [&](uint32_t pw) -> size_t {
        return (static_cast<size_t>(pw) * bitsPerPixel + 7u) / 8u;
    };

    size_t expectedRaw = 0;
    static const int kA7X0[7] = {0,4,0,2,0,1,0};
    static const int kA7Y0[7] = {0,0,4,0,2,0,1};
    static const int kA7DX[7] = {8,8,4,4,2,2,1};
    static const int kA7DY[7] = {8,8,8,4,4,2,2};

    if (interlace == 0) {
        expectedRaw = static_cast<size_t>(h) * (1u + rowStrideForW(w));
    } else {
        for (int p = 0; p < 7; ++p) {
            uint32_t pw = (w > static_cast<uint32_t>(kA7X0[p])) ? ((w - kA7X0[p] + kA7DX[p] - 1) / kA7DX[p]) : 0;
            uint32_t ph = (h > static_cast<uint32_t>(kA7Y0[p])) ? ((h - kA7Y0[p] + kA7DY[p] - 1) / kA7DY[p]) : 0;
            if (pw > 0 && ph > 0) expectedRaw += static_cast<size_t>(ph) * (1u + rowStrideForW(pw));
        }
    }
    if (expectedRaw == 0 || expectedRaw > 512ULL * 1024 * 1024) return false;

    std::vector<uint8_t> raw;
    if (!ZipDeflate::InflateZlib(idat.data(), idat.size(), raw, expectedRaw + 4096) || raw.size() < expectedRaw) {
        return false;
    }

    out.w = w;
    out.h = h;
    out.container = GUID_ContainerFormatPng;
    out.bgra.assign(static_cast<size_t>(w) * h * 4, 255);

    auto writePixel = [&](uint32_t px, uint32_t py, const uint8_t* row, uint32_t rowX) {
        uint8_t r = 0, g = 0, b = 0, a = 255;
        if (bitDepth < 8) {
            const int perByte = 8 / bitDepth;
            const uint8_t mask = static_cast<uint8_t>((1u << bitDepth) - 1u);
            const int shift = (perByte - 1 - static_cast<int>(rowX % perByte)) * bitDepth;
            const uint8_t idx = (row[rowX / perByte] >> shift) & mask;
            if (colorType == 3) {
                if (static_cast<size_t>(idx) * 3 + 2 < palRgb.size()) {
                    r = palRgb[idx * 3]; g = palRgb[idx * 3 + 1]; b = palRgb[idx * 3 + 2];
                }
                if (idx < trns.size()) a = trns[idx];
            } else {
                uint8_t v = static_cast<uint8_t>((idx * 255u + mask / 2u) / mask);
                r = g = b = v;
                if (trns.size() >= 2 && idx == Rd16BE(trns.data())) a = 0;
            }
        } else if (bitDepth == 8) {
            const uint8_t* s = row + static_cast<size_t>(rowX) * channels;
            if (colorType == 0) {
                r = g = b = s[0];
                if (trns.size() >= 2 && s[0] == trns[1]) a = 0;
            } else if (colorType == 2) {
                r = s[0]; g = s[1]; b = s[2];
                if (trns.size() >= 6 && s[0] == trns[1] && s[1] == trns[3] && s[2] == trns[5]) a = 0;
            } else if (colorType == 3) {
                uint8_t idx = s[0];
                if (static_cast<size_t>(idx) * 3 + 2 < palRgb.size()) {
                    r = palRgb[idx * 3]; g = palRgb[idx * 3 + 1]; b = palRgb[idx * 3 + 2];
                }
                if (idx < trns.size()) a = trns[idx];
            } else if (colorType == 4) {
                r = g = b = s[0]; a = s[1];
            } else if (colorType == 6) {
                r = s[0]; g = s[1]; b = s[2]; a = s[3];
            }
        } else { // 16-bit
            const uint8_t* s = row + static_cast<size_t>(rowX) * channels * 2;
            if (colorType == 0) {
                r = g = b = s[0];
                if (trns.size() >= 2 && Rd16BE(s) == Rd16BE(trns.data())) a = 0;
            } else if (colorType == 2) {
                r = s[0]; g = s[2]; b = s[4];
                if (trns.size() >= 6 && Rd16BE(s) == Rd16BE(trns.data()) && Rd16BE(s+2) == Rd16BE(trns.data()+2) && Rd16BE(s+4) == Rd16BE(trns.data()+4)) a = 0;
            } else if (colorType == 4) {
                r = g = b = s[0]; a = s[2];
            } else if (colorType == 6) {
                r = s[0]; g = s[2]; b = s[4]; a = s[6];
            }
        }
        uint8_t* d = &out.bgra[(static_cast<size_t>(py) * w + px) * 4];
        d[0] = b; d[1] = g; d[2] = r; d[3] = a;
    };

    size_t rPos = 0;
    auto unfilterPass = [&](uint32_t pw, uint32_t ph, int x0, int y0, int dx, int dy) -> bool {
        if (pw == 0 || ph == 0) return true;
        const size_t stride = rowStrideForW(pw);
        std::vector<uint8_t> prev(stride, 0), cur(stride, 0);
        for (uint32_t y = 0; y < ph; ++y) {
            if (rPos + 1 + stride > raw.size()) return false;
            uint8_t fType = raw[rPos++];
            const uint8_t* src = raw.data() + rPos;
            rPos += stride;
            for (size_t i = 0; i < stride; ++i) {
                uint8_t a = (i >= bppFilter) ? cur[i - bppFilter] : 0;
                uint8_t b = prev[i];
                uint8_t c = (i >= bppFilter) ? prev[i - bppFilter] : 0;
                switch (fType) {
                case 0: cur[i] = src[i]; break;
                case 1: cur[i] = static_cast<uint8_t>(src[i] + a); break;
                case 2: cur[i] = static_cast<uint8_t>(src[i] + b); break;
                case 3: cur[i] = static_cast<uint8_t>(src[i] + ((static_cast<int>(a) + b) >> 1)); break;
                case 4: cur[i] = static_cast<uint8_t>(src[i] + PaethPred(a, b, c)); break;
                default: return false;
                }
            }
            for (uint32_t x = 0; x < pw; ++x) {
                writePixel(static_cast<uint32_t>(x0 + x * dx), static_cast<uint32_t>(y0 + y * dy), cur.data(), x);
            }
            prev.swap(cur);
        }
        return true;
    };

    if (interlace == 0) {
        return unfilterPass(w, h, 0, 0, 1, 1);
    }
    for (int p = 0; p < 7; ++p) {
        uint32_t pw = (w > static_cast<uint32_t>(kA7X0[p])) ? ((w - kA7X0[p] + kA7DX[p] - 1) / kA7DX[p]) : 0;
        uint32_t ph = (h > static_cast<uint32_t>(kA7Y0[p])) ? ((h - kA7Y0[p] + kA7DY[p] - 1) / kA7DY[p]) : 0;
        if (!unfilterPass(pw, ph, kA7X0[p], kA7Y0[p], kA7DX[p], kA7DY[p])) return false;
    }
    return true;
}

std::vector<uint8_t> EncodePngPure(uint32_t w, uint32_t h, const uint8_t* bgra, size_t stride) {
    std::vector<uint8_t> out = { 137, 80, 78, 71, 13, 10, 26, 10 };
    auto writeChunk = [&](const char type[4], const uint8_t* payload, size_t len) {
        Wr32BE(out, static_cast<uint32_t>(len));
        const size_t typeStart = out.size();
        out.insert(out.end(), type, type + 4);
        if (len > 0) out.insert(out.end(), payload, payload + len);
        uint32_t crc = Checksum::Crc32(out.data() + typeStart, 4 + len);
        Wr32BE(out, crc);
    };

    uint8_t ihdr[13] = {};
    ihdr[0] = uint8_t(w >> 24); ihdr[1] = uint8_t(w >> 16); ihdr[2] = uint8_t(w >> 8); ihdr[3] = uint8_t(w);
    ihdr[4] = uint8_t(h >> 24); ihdr[5] = uint8_t(h >> 16); ihdr[6] = uint8_t(h >> 8); ihdr[7] = uint8_t(h);
    ihdr[8] = 8; ihdr[9] = 6; // 8-bit RGBA
    writeChunk("IHDR", ihdr, 13);

    // Raw scanlines (filter 0 + RGBA)
    const size_t rowLen = 1 + static_cast<size_t>(w) * 4;
    std::vector<uint8_t> raw(rowLen * h);
    for (uint32_t y = 0; y < h; ++y) {
        uint8_t* dst = raw.data() + y * rowLen;
        const uint8_t* src = bgra + static_cast<size_t>(y) * stride;
        dst[0] = 0;
        for (uint32_t x = 0; x < w; ++x) {
            dst[1 + x * 4 + 0] = src[x * 4 + 2];
            dst[1 + x * 4 + 1] = src[x * 4 + 1];
            dst[1 + x * 4 + 2] = src[x * 4 + 0];
            dst[1 + x * 4 + 3] = src[x * 4 + 3];
        }
    }

    // Zlib stored stream (RFC 1950 + RFC 1951 stored blocks + Adler32)
    std::vector<uint8_t> z;
    z.reserve(raw.size() + (raw.size() / 65535 + 2) * 5 + 6);
    z.push_back(0x78); z.push_back(0x01);
    uint32_t s1 = 1, s2 = 0;
    for (uint8_t b : raw) {
        s1 = (s1 + b) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t blk = std::min<size_t>(65535, raw.size() - pos);
        bool last = (pos + blk == raw.size());
        z.push_back(last ? 0x01 : 0x00);
        uint16_t l16 = static_cast<uint16_t>(blk);
        uint16_t nl16 = static_cast<uint16_t>(~l16);
        Wr16LE(z, l16);
        Wr16LE(z, nl16);
        z.insert(z.end(), raw.data() + pos, raw.data() + pos + blk);
        pos += blk;
    }
    Wr32BE(z, (s2 << 16) | s1);
    writeChunk("IDAT", z.data(), z.size());
    writeChunk("IEND", nullptr, 0);
    return out;
}

// ==============================================================================================================
// 2. Pure-C++ Baseline & Progressive JPEG Decoder + Baseline JFIF Encoder
// ==============================================================================================================
static const uint8_t kZigZag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

struct JpgHuff {
    int maxCode[18] = {};
    int valOffset[18] = {};
    uint8_t vals[256] = {};
    bool valid = false;

    void Build(const uint8_t bits[16], const uint8_t* syms, size_t nSyms) {
        std::memcpy(vals, syms, std::min<size_t>(256, nSyms));
        int code = 0;
        int k = 0;
        for (int i = 1; i <= 16; ++i) {
            if (bits[i - 1] == 0) {
                maxCode[i] = -1;
            } else {
                valOffset[i] = k - code;
                k += bits[i - 1];
                code += bits[i - 1];
                maxCode[i] = code - 1;
            }
            code <<= 1;
        }
        valid = true;
    }
};

struct JpgBitReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    uint32_t bitBuf = 0;
    int bitCnt = 0;
    bool hitMarker = false;

    void ResetBits() { bitBuf = 0; bitCnt = 0; hitMarker = false; }

    int GetBit() {
        if (bitCnt == 0) {
            if (hitMarker || pos >= size) return 0;
            uint8_t b = data[pos++];
            if (b == 0xFFu) {
                while (pos < size && data[pos] == 0xFFu) ++pos;
                if (pos >= size) { hitMarker = true; return 0; }
                uint8_t m = data[pos++];
                if (m != 0x00u) {
                    pos -= 2; // rewind to marker
                    hitMarker = true;
                    return 0;
                }
            }
            bitBuf = b;
            bitCnt = 8;
        }
        --bitCnt;
        return (bitBuf >> bitCnt) & 1;
    }

    int ReadBits(int n) {
        int v = 0;
        for (int i = 0; i < n; ++i) v = (v << 1) | GetBit();
        return v;
    }

    int ReceiveExtend(int s) {
        if (s == 0) return 0;
        int v = ReadBits(s);
        if (v < (1 << (s - 1))) v += (-1 << s) + 1;
        return v;
    }

    int DecodeHuff(const JpgHuff& h) {
        int code = 0;
        for (int len = 1; len <= 16; ++len) {
            code = (code << 1) | GetBit();
            if (code <= h.maxCode[len]) {
                int idx = code + h.valOffset[len];
                return (idx >= 0 && idx < 256) ? h.vals[idx] : 0;
            }
        }
        return -1;
    }
};

void Idct8x8(const int16_t coeff[64], const uint16_t qt[64], uint8_t out[64]) {
    float tmp[64];
    static const float kC[8] = {
        0.35355339f, 0.49039264f, 0.46193977f, 0.41573481f,
        0.35355339f, 0.27778512f, 0.19134172f, 0.09754516f
    };
    static float s_cos[8][8];
    static bool s_init = []() {
        for (int x = 0; x < 8; ++x)
            for (int u = 0; u < 8; ++u)
                s_cos[x][u] = kC[u] * std::cos((2.0f * x + 1.0f) * u * 3.1415926535f / 16.0f);
        return true;
    }();
    (void)s_init;

    float block[64];
    for (int i = 0; i < 64; ++i) block[i] = static_cast<float>(coeff[i]) * static_cast<float>(qt[i]);

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            float sum = 0.0f;
            for (int u = 0; u < 8; ++u) sum += block[y * 8 + u] * s_cos[x][u];
            tmp[y * 8 + x] = sum;
        }
    }
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            float sum = 0.0f;
            for (int v = 0; v < 8; ++v) sum += tmp[v * 8 + x] * s_cos[y][v];
            int px = static_cast<int>(std::lround(sum + 128.0f));
            out[y * 8 + x] = static_cast<uint8_t>(std::clamp(px, 0, 255));
        }
    }
}

bool DecodeJpegPure(const uint8_t* data, size_t size, DecodedImage& out) {
    if (!data || size < 4 || data[0] != 0xFF || data[1] != 0xD8) return false;

    uint16_t qt[4][64] = {};
    bool hasQt[4] = {};
    JpgHuff dcHuff[4], acHuff[4];

    struct Comp {
        uint8_t id = 0;
        int hSamp = 1, vSamp = 1, qtId = 0;
        int dcTd = 0, acTa = 0;
        int prevDc = 0;
        int blocksX = 0, blocksY = 0;
        std::vector<std::array<int16_t, 64>> coeffs;
    };

    uint32_t width = 0, height = 0;
    int numComp = 0;
    Comp comps[4];
    bool progressive = false;
    bool gotSof = false, decodedAnyScan = false;
    uint16_t restartInterval = 0;
    bool adobeRgb = false;
    bool hasJfif = false;

    // Install standard Annex K default Huffman tables (used by MJPEG streams without DHT)
    static const uint8_t kStdDcLumBits[16] = {0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
    static const uint8_t kStdDcLumVals[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
    static const uint8_t kStdDcChrBits[16] = {0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
    static const uint8_t kStdDcChrVals[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
    static const uint8_t kStdAcLumBits[16] = {0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
    static const uint8_t kStdAcLumVals[162] = {
        0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,
        0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
        0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
        0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
        0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
        0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
        0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,
        0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
        0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
        0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
    };
    static const uint8_t kStdAcChrBits[16] = {0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
    static const uint8_t kStdAcChrVals[162] = {
        0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,
        0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
        0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
        0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
        0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,
        0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
        0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,
        0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
        0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
        0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
    };
    dcHuff[0].Build(kStdDcLumBits, kStdDcLumVals, 12);
    dcHuff[1].Build(kStdDcChrBits, kStdDcChrVals, 12);
    acHuff[0].Build(kStdAcLumBits, kStdAcLumVals, 162);
    acHuff[1].Build(kStdAcChrBits, kStdAcChrVals, 162);

    int maxH = 1, maxV = 1;
    int mcuCountX = 0, mcuCountY = 0;

    size_t pos = 2;
    while (pos + 2 <= size) {
        if (data[pos] != 0xFFu) { ++pos; continue; }
        while (pos < size && data[pos] == 0xFFu) ++pos;
        if (pos >= size) break;
        uint8_t marker = data[pos++];
        if (marker == 0xD9u) break; // EOI
        if (marker == 0x00u || (marker >= 0xD0u && marker <= 0xD8u)) continue;
        if (pos + 2 > size) return false;
        uint16_t segLen = Rd16BE(data + pos);
        if (segLen < 2 || pos + segLen > size) return false;
        const uint8_t* seg = data + pos + 2;
        const size_t bodyLen = segLen - 2;
        pos += segLen;

        if (marker == 0xE0u && bodyLen >= 5 && std::memcmp(seg, "JFIF\0", 5) == 0) {
            hasJfif = true;
        } else if (marker == 0xE1u) {
            ParseJpegApp1Exif(seg, bodyLen, out.exif);
        } else if (marker == 0xEEu && bodyLen >= 12 && std::memcmp(seg, "Adobe", 5) == 0) {
            adobeRgb = (seg[11] == 0);
        } else if (marker == 0xDBu) { // DQT
            size_t p = 0;
            while (p < bodyLen) {
                uint8_t pqTq = seg[p++];
                int prec = (pqTq >> 4) & 0x0F;
                int tq = pqTq & 0x0F;
                if (tq >= 4 || (prec != 0 && prec != 1)) return false;
                size_t need = prec ? 128 : 64;
                if (p + need > bodyLen) return false;
                for (int i = 0; i < 64; ++i) {
                    qt[tq][kZigZag[i]] = prec ? Rd16BE(seg + p + i * 2) : seg[p + i];
                }
                hasQt[tq] = true;
                p += need;
            }
        } else if (marker == 0xC4u) { // DHT
            size_t p = 0;
            while (p + 17 <= bodyLen) {
                uint8_t tcTh = seg[p++];
                int tc = (tcTh >> 4) & 0x0F;
                int th = tcTh & 0x0F;
                if (th >= 4 || tc > 1) return false;
                const uint8_t* bits = seg + p;
                p += 16;
                size_t total = 0;
                for (int i = 0; i < 16; ++i) total += bits[i];
                if (total > 256 || p + total > bodyLen) return false;
                if (tc == 0) dcHuff[th].Build(bits, seg + p, total);
                else         acHuff[th].Build(bits, seg + p, total);
                p += total;
            }
        } else if (marker == 0xDDu && bodyLen >= 2) { // DRI
            restartInterval = Rd16BE(seg);
        } else if (marker == 0xC0u || marker == 0xC1u || marker == 0xC2u) { // SOF0 / SOF1 / SOF2
            if (bodyLen < 6 || gotSof) return false;
            progressive = (marker == 0xC2u);
            uint8_t prec = seg[0];
            height = Rd16BE(seg + 1);
            width  = Rd16BE(seg + 3);
            numComp = seg[5];
            if (prec != 8 || width == 0 || height == 0 || width > 16384 || height > 16384) return false;
            if ((numComp != 1 && numComp != 3 && numComp != 4) || bodyLen < static_cast<size_t>(6 + numComp * 3)) return false;

            maxH = 1; maxV = 1;
            for (int i = 0; i < numComp; ++i) {
                comps[i].id = seg[6 + i * 3];
                uint8_t hv = seg[6 + i * 3 + 1];
                comps[i].hSamp = (hv >> 4) & 0x0F;
                comps[i].vSamp = hv & 0x0F;
                comps[i].qtId  = seg[6 + i * 3 + 2];
                if (comps[i].hSamp < 1 || comps[i].hSamp > 4 || comps[i].vSamp < 1 || comps[i].vSamp > 4 || comps[i].qtId >= 4) return false;
                maxH = std::max(maxH, comps[i].hSamp);
                maxV = std::max(maxV, comps[i].vSamp);
            }
            mcuCountX = static_cast<int>((width  + maxH * 8 - 1) / (maxH * 8));
            mcuCountY = static_cast<int>((height + maxV * 8 - 1) / (maxV * 8));
            if (static_cast<uint64_t>(mcuCountX) * mcuCountY > 4ULL * 1024 * 1024) return false;
            for (int i = 0; i < numComp; ++i) {
                comps[i].blocksX = mcuCountX * comps[i].hSamp;
                comps[i].blocksY = mcuCountY * comps[i].vSamp;
                std::array<int16_t, 64> zeroBlk{};
                comps[i].coeffs.assign(static_cast<size_t>(comps[i].blocksX) * comps[i].blocksY, zeroBlk);
            }
            gotSof = true;
        } else if (marker == 0xDAu) { // SOS
            if (!gotSof || bodyLen < 4) return false;
            int ns = seg[0];
            if (ns < 1 || ns > numComp || bodyLen < static_cast<size_t>(1 + ns * 2 + 3)) return false;
            int scanCompIdx[4] = {};
            for (int i = 0; i < ns; ++i) {
                uint8_t cid = seg[1 + i * 2];
                uint8_t tdta = seg[1 + i * 2 + 1];
                int found = -1;
                for (int c = 0; c < numComp; ++c) if (comps[c].id == cid) { found = c; break; }
                if (found < 0) return false;
                comps[found].dcTd = (tdta >> 4) & 0x0F;
                comps[found].acTa = tdta & 0x0F;
                if (comps[found].dcTd >= 4 || comps[found].acTa >= 4) return false;
                scanCompIdx[i] = found;
            }
            int Ss = seg[1 + ns * 2];
            int Se = seg[1 + ns * 2 + 1];
            uint8_t AhAl = seg[1 + ns * 2 + 2];
            int Ah = (AhAl >> 4) & 0x0F;
            int Al = AhAl & 0x0F;
            if (Ss < 0 || Se > 63 || Ss > Se) return false;

            for (int c = 0; c < numComp; ++c) comps[c].prevDc = 0;
            int eobRun = 0;
            int mcusSinceRst = 0;

            JpgBitReader br{ data, size, pos, 0, 0, false };
            auto handleRestart = [&]() {
                if (restartInterval > 0 && ++mcusSinceRst == restartInterval) {
                    mcusSinceRst = 0;
                    br.ResetBits();
                    if (br.pos + 2 <= br.size && br.data[br.pos] == 0xFFu &&
                        br.data[br.pos + 1] >= 0xD0u && br.data[br.pos + 1] <= 0xD7u)
                    {
                        br.pos += 2;
                    }
                    for (int c = 0; c < numComp; ++c) comps[c].prevDc = 0;
                    eobRun = 0;
                }
            };

            auto decodeBaselineBlock = [&](Comp& cp, int16_t* blk) -> bool {
                int s = br.DecodeHuff(dcHuff[cp.dcTd]);
                if (s < 0) return false;
                int diff = br.ReceiveExtend(s);
                cp.prevDc += diff;
                blk[0] = static_cast<int16_t>(cp.prevDc);
                int k = 1;
                while (k < 64) {
                    int rs = br.DecodeHuff(acHuff[cp.acTa]);
                    if (rs < 0) return false;
                    int r = (rs >> 4) & 0x0F;
                    int s2 = rs & 0x0F;
                    if (s2 == 0) {
                        if (r == 15) { k += 16; continue; }
                        break;
                    }
                    k += r;
                    if (k >= 64) break;
                    blk[kZigZag[k]] = static_cast<int16_t>(br.ReceiveExtend(s2));
                    ++k;
                }
                return true;
            };

            auto decodeProgBlock = [&](Comp& cp, int16_t* blk) -> bool {
                if (Ss == 0) { // DC scan
                    if (Ah == 0) {
                        int s = br.DecodeHuff(dcHuff[cp.dcTd]);
                        if (s < 0) return false;
                        cp.prevDc += br.ReceiveExtend(s);
                        blk[0] = static_cast<int16_t>(cp.prevDc << Al);
                    } else {
                        if (br.GetBit()) blk[0] |= static_cast<int16_t>(1 << Al);
                    }
                    return true;
                }
                // AC scan (ns == 1)
                if (Ah == 0) {
                    if (eobRun > 0) { --eobRun; return true; }
                    for (int k = Ss; k <= Se; ++k) {
                        int rs = br.DecodeHuff(acHuff[cp.acTa]);
                        if (rs < 0) return false;
                        int r = (rs >> 4) & 0x0F;
                        int s2 = rs & 0x0F;
                        if (s2 == 0) {
                            if (r < 15) {
                                eobRun = (1 << r) + (r ? br.ReadBits(r) : 0) - 1;
                                break;
                            }
                            k += 15;
                        } else {
                            k += r;
                            if (k > Se) break;
                            blk[kZigZag[k]] = static_cast<int16_t>(br.ReceiveExtend(s2) << Al);
                        }
                    }
                } else {
                    const int16_t p1 = static_cast<int16_t>(1 << Al);
                    const int16_t m1 = static_cast<int16_t>((-1) << Al);
                    int k = Ss;
                    if (eobRun == 0) {
                        for (; k <= Se; ++k) {
                            int rs = br.DecodeHuff(acHuff[cp.acTa]);
                            if (rs < 0) return false;
                            int r = (rs >> 4) & 0x0F;
                            int s2 = rs & 0x0F;
                            int16_t newVal = 0;
                            if (s2 != 0) {
                                newVal = br.GetBit() ? p1 : m1;
                            } else if (r != 15) {
                                eobRun = (1 << r) + (r ? br.ReadBits(r) : 0);
                                break;
                            }
                            do {
                                int16_t& coef = blk[kZigZag[k]];
                                if (coef != 0) {
                                    if (br.GetBit() && (coef & p1) == 0) coef += (coef > 0) ? p1 : m1;
                                } else if (--r < 0) {
                                    break;
                                }
                                ++k;
                            } while (k <= Se);
                            if (newVal != 0 && k <= Se) blk[kZigZag[k]] = newVal;
                        }
                    }
                    if (eobRun > 0) {
                        for (; k <= Se; ++k) {
                            int16_t& coef = blk[kZigZag[k]];
                            if (coef != 0 && br.GetBit() && (coef & p1) == 0) coef += (coef > 0) ? p1 : m1;
                        }
                        --eobRun;
                    }
                }
                return true;
            };

            if (ns == 1 && (progressive || numComp > 1)) {
                Comp& cp = comps[scanCompIdx[0]];
                int visBx = static_cast<int>((width * cp.hSamp + maxH * 8 - 1) / (maxH * 8));
                int visBy = static_cast<int>((height * cp.vSamp + maxV * 8 - 1) / (maxV * 8));
                for (int by = 0; by < visBy; ++by) {
                    for (int bx = 0; bx < visBx; ++bx) {
                        int16_t* blk = cp.coeffs[static_cast<size_t>(by) * cp.blocksX + bx].data();
                        if (progressive ? !decodeProgBlock(cp, blk) : !decodeBaselineBlock(cp, blk)) return false;
                        if (! (by + 1 == visBy && bx + 1 == visBx)) handleRestart();
                    }
                }
            } else {
                for (int my = 0; my < mcuCountY; ++my) {
                    for (int mx = 0; mx < mcuCountX; ++mx) {
                        for (int si = 0; si < ns; ++si) {
                            Comp& cp = comps[scanCompIdx[si]];
                            for (int vy = 0; vy < cp.vSamp; ++vy) {
                                for (int vx = 0; vx < cp.hSamp; ++vx) {
                                    size_t bIdx = static_cast<size_t>(my * cp.vSamp + vy) * cp.blocksX + (mx * cp.hSamp + vx);
                                    int16_t* blk = cp.coeffs[bIdx].data();
                                    if (progressive ? !decodeProgBlock(cp, blk) : !decodeBaselineBlock(cp, blk)) return false;
                                }
                            }
                        }
                        if (!(my + 1 == mcuCountY && mx + 1 == mcuCountX)) handleRestart();
                    }
                }
            }
            decodedAnyScan = true;
            pos = br.pos;
        }
    }

    if (!gotSof || !decodedAnyScan) return false;
    for (int c = 0; c < numComp; ++c) if (!hasQt[comps[c].qtId]) return false;

    // Reconstruct spatial planes via 8x8 IDCT
    std::vector<uint8_t> planes[4];
    for (int c = 0; c < numComp; ++c) {
        const Comp& cp = comps[c];
        planes[c].resize(static_cast<size_t>(cp.blocksX * 8) * (cp.blocksY * 8));
        uint8_t spatial[64];
        for (int by = 0; by < cp.blocksY; ++by) {
            for (int bx = 0; bx < cp.blocksX; ++bx) {
                Idct8x8(cp.coeffs[static_cast<size_t>(by) * cp.blocksX + bx].data(), qt[cp.qtId], spatial);
                for (int r = 0; r < 8; ++r) {
                    std::memcpy(&planes[c][(static_cast<size_t>(by * 8 + r) * (cp.blocksX * 8)) + bx * 8], &spatial[r * 8], 8);
                }
            }
        }
    }

    const bool isRgbIds = (numComp == 3 && !hasJfif && (adobeRgb || (comps[0].id == 'R' && comps[1].id == 'G' && comps[2].id == 'B')));
    out.w = width;
    out.h = height;
    out.container = GUID_ContainerFormatJpeg;
    out.bgra.resize(static_cast<size_t>(width) * height * 4);

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            auto sampleComp = [&](int c) -> uint8_t {
                const Comp& cp = comps[c];
                uint32_t cx = (x * cp.hSamp) / maxH;
                uint32_t cy = (y * cp.vSamp) / maxV;
                return planes[c][static_cast<size_t>(cy) * (cp.blocksX * 8) + cx];
            };
            uint8_t r = 0, g = 0, b = 0;
            if (numComp == 1) {
                r = g = b = sampleComp(0);
            } else if (numComp == 3 && isRgbIds) {
                r = sampleComp(0); g = sampleComp(1); b = sampleComp(2);
            } else if (numComp == 3) {
                float Y  = static_cast<float>(sampleComp(0));
                float Cb = static_cast<float>(sampleComp(1)) - 128.0f;
                float Cr = static_cast<float>(sampleComp(2)) - 128.0f;
                r = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(Y + 1.40200f * Cr)), 0, 255));
                g = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(Y - 0.34414f * Cb - 0.71414f * Cr)), 0, 255));
                b = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(Y + 1.77200f * Cb)), 0, 255));
            } else { // 4-channel CMYK / YCCK
                float c0 = sampleComp(0), c1 = sampleComp(1), c2 = sampleComp(2), k = sampleComp(3) / 255.0f;
                r = static_cast<uint8_t>(std::clamp(static_cast<int>(c0 * k), 0, 255));
                g = static_cast<uint8_t>(std::clamp(static_cast<int>(c1 * k), 0, 255));
                b = static_cast<uint8_t>(std::clamp(static_cast<int>(c2 * k), 0, 255));
            }
            uint8_t* dst = &out.bgra[(static_cast<size_t>(y) * width + x) * 4];
            dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255;
        }
    }
    return true;
}

// Baseline JFIF JPEG Encoder (4:4:4 YCbCr with quality scaling in [0.01..1.0])
std::vector<uint8_t> EncodeJpegPure(uint32_t w, uint32_t h, const uint8_t* bgra, size_t stride, float quality01) {
    static const uint8_t kBaseLumQt[64] = {
        16,11,10,16,24,40,51,61, 12,12,14,19,26,58,60,55, 14,13,16,24,40,57,69,56, 14,17,22,29,51,87,80,62,
        18,22,37,56,68,109,103,77, 24,35,55,64,81,104,113,92, 49,64,78,87,103,121,120,101, 72,92,95,98,112,100,103,99
    };
    static const uint8_t kBaseChrQt[64] = {
        17,18,24,47,99,99,99,99, 18,21,26,66,99,99,99,99, 24,26,56,99,99,99,99,99, 47,66,99,99,99,99,99,99,
        99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99
    };
    static const uint8_t kDcLumBits[16] = {0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
    static const uint8_t kDcLumVals[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
    static const uint8_t kDcChrBits[16] = {0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
    static const uint8_t kDcChrVals[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
    static const uint8_t kAcLumBits[16] = {0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
    static const uint8_t kAcLumVals[162] = {
        0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,
        0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
        0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
        0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
        0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
        0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
        0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,
        0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
        0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
        0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
    };
    static const uint8_t kAcChrBits[16] = {0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
    static const uint8_t kAcChrVals[162] = {
        0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,
        0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
        0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
        0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
        0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,
        0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
        0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,
        0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
        0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
        0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
    };

    int q = std::clamp(static_cast<int>(std::lround(quality01 * 100.0f)), 1, 100);
    int scale = (q < 50) ? (5000 / q) : (200 - q * 2);
    uint8_t lumQt[64], chrQt[64], lumZig[64], chrZig[64];
    for (int i = 0; i < 64; ++i) {
        lumQt[i] = static_cast<uint8_t>(std::clamp((kBaseLumQt[i] * scale + 50) / 100, 1, 255));
        chrQt[i] = static_cast<uint8_t>(std::clamp((kBaseChrQt[i] * scale + 50) / 100, 1, 255));
    }
    for (int i = 0; i < 64; ++i) {
        lumZig[i] = lumQt[kZigZag[i]];
        chrZig[i] = chrQt[kZigZag[i]];
    }

    struct EncHuff {
        uint16_t code[256] = {};
        uint8_t  len[256]  = {};
        void Init(const uint8_t bits[16], const uint8_t* vals, int n) {
            int c = 0, k = 0;
            for (int b = 1; b <= 16; ++b) {
                for (int i = 0; i < bits[b - 1] && k < n; ++i, ++k) {
                    code[vals[k]] = static_cast<uint16_t>(c++);
                    len[vals[k]]  = static_cast<uint8_t>(b);
                }
                c <<= 1;
            }
        }
    };
    EncHuff dcL, dcC, acL, acC;
    dcL.Init(kDcLumBits, kDcLumVals, 12);
    dcC.Init(kDcChrBits, kDcChrVals, 12);
    acL.Init(kAcLumBits, kAcLumVals, 162);
    acC.Init(kAcChrBits, kAcChrVals, 162);

    std::vector<uint8_t> out = {
        0xFF,0xD8,
        0xFF,0xE0, 0x00,0x10, 'J','F','I','F',0x00, 0x01,0x01, 0x01, 0x00,0x60, 0x00,0x60, 0x00,0x00
    };
    auto pushDqt = [&](uint8_t id, const uint8_t zq[64]) {
        out.insert(out.end(), { 0xFF, 0xDB, 0x00, 67, id });
        out.insert(out.end(), zq, zq + 64);
    };
    pushDqt(0, lumZig);
    pushDqt(1, chrZig);

    // SOF0 4:4:4
    out.insert(out.end(), {
        0xFF,0xC0, 0x00,17, 8,
        uint8_t(h >> 8), uint8_t(h), uint8_t(w >> 8), uint8_t(w),
        3, 1,0x11,0, 2,0x11,1, 3,0x11,1
    });
    auto pushDht = [&](uint8_t clsId, const uint8_t bits[16], const uint8_t* vals, size_t nv) {
        uint16_t sl = static_cast<uint16_t>(19 + nv);
        out.insert(out.end(), { 0xFF, 0xC4, uint8_t(sl >> 8), uint8_t(sl), clsId });
        out.insert(out.end(), bits, bits + 16);
        out.insert(out.end(), vals, vals + nv);
    };
    pushDht(0x00, kDcLumBits, kDcLumVals, 12);
    pushDht(0x10, kAcLumBits, kAcLumVals, 162);
    pushDht(0x01, kDcChrBits, kDcChrVals, 12);
    pushDht(0x11, kAcChrBits, kAcChrVals, 162);

    // SOS
    out.insert(out.end(), { 0xFF,0xDA, 0x00,12, 3, 1,0x00, 2,0x11, 3,0x11, 0,63,0 });

    uint32_t bitBuf = 0;
    int bitCnt = 0;
    auto putBits = [&](uint16_t bits, int n) {
        for (int i = n - 1; i >= 0; --i) {
            bitBuf = (bitBuf << 1) | ((bits >> i) & 1u);
            if (++bitCnt == 8) {
                uint8_t b = static_cast<uint8_t>(bitBuf & 0xFFu);
                out.push_back(b);
                if (b == 0xFFu) out.push_back(0x00u);
                bitCnt = 0;
            }
        }
    };

    static float s_fcos[8][8];
    static bool s_finit = []() {
        static const float kC[8] = {
            0.35355339f, 0.49039264f, 0.46193977f, 0.41573481f,
            0.35355339f, 0.27778512f, 0.19134172f, 0.09754516f
        };
        for (int u = 0; u < 8; ++u)
            for (int x = 0; x < 8; ++x)
                s_fcos[u][x] = kC[u] * std::cos((2.0f * x + 1.0f) * u * 3.1415926535f / 16.0f);
        return true;
    }();
    (void)s_finit;

    auto encodeBlock = [&](const float blk[64], const uint8_t qtbl[64], int& prevDc, const EncHuff& dcH, const EncHuff& acH) {
        float tmp[64], dct[64];
        for (int y = 0; y < 8; ++y)
            for (int u = 0; u < 8; ++u) {
                float s = 0;
                for (int x = 0; x < 8; ++x) s += blk[y * 8 + x] * s_fcos[u][x];
                tmp[y * 8 + u] = s;
            }
        for (int v = 0; v < 8; ++v)
            for (int u = 0; u < 8; ++u) {
                float s = 0;
                for (int y = 0; y < 8; ++y) s += tmp[y * 8 + u] * s_fcos[v][y];
                dct[v * 8 + u] = s;
            }
        int zz[64];
        for (int i = 0; i < 64; ++i) {
            int idx = kZigZag[i];
            zz[i] = static_cast<int>(std::lround(dct[idx] / static_cast<float>(qtbl[idx])));
        }
        auto bitMag = [](int val, int& nbits, uint16_t& mag) {
            int a = std::abs(val);
            nbits = 0;
            while (a > 0) { ++nbits; a >>= 1; }
            mag = static_cast<uint16_t>((val >= 0) ? val : (val + (1 << nbits) - 1));
        };
        int diff = zz[0] - prevDc;
        prevDc = zz[0];
        int nb = 0; uint16_t mg = 0;
        bitMag(diff, nb, mg);
        putBits(dcH.code[nb], dcH.len[nb]);
        if (nb > 0) putBits(mg, nb);

        int zeroRun = 0;
        for (int i = 1; i < 64; ++i) {
            if (zz[i] == 0) {
                ++zeroRun;
            } else {
                while (zeroRun >= 16) {
                    putBits(acH.code[0xF0], acH.len[0xF0]);
                    zeroRun -= 16;
                }
                bitMag(zz[i], nb, mg);
                uint8_t sym = static_cast<uint8_t>((zeroRun << 4) | nb);
                putBits(acH.code[sym], acH.len[sym]);
                putBits(mg, nb);
                zeroRun = 0;
            }
        }
        if (zeroRun > 0) putBits(acH.code[0x00], acH.len[0x00]);
    };

    int prevY = 0, prevCb = 0, prevCr = 0;
    for (uint32_t by = 0; by < h; by += 8) {
        for (uint32_t bx = 0; bx < w; bx += 8) {
            float bY[64], bCb[64], bCr[64];
            for (uint32_t r = 0; r < 8; ++r) {
                uint32_t sy = std::min(by + r, h - 1);
                const uint8_t* row = bgra + static_cast<size_t>(sy) * stride;
                for (uint32_t c = 0; c < 8; ++c) {
                    uint32_t sx = std::min(bx + c, w - 1);
                    float B = row[sx * 4 + 0], G = row[sx * 4 + 1], R = row[sx * 4 + 2];
                    bY[r * 8 + c]  = (0.2990f * R + 0.5870f * G + 0.1140f * B) - 128.0f;
                    bCb[r * 8 + c] = (-0.168736f * R - 0.331264f * G + 0.5000f * B);
                    bCr[r * 8 + c] = (0.5000f * R - 0.418688f * G - 0.081312f * B);
                }
            }
            encodeBlock(bY,  lumQt, prevY,  dcL, acL);
            encodeBlock(bCb, chrQt, prevCb, dcC, acC);
            encodeBlock(bCr, chrQt, prevCr, dcC, acC);
        }
    }
    if (bitCnt > 0) putBits(static_cast<uint16_t>((1u << (8 - bitCnt)) - 1u), 8 - bitCnt);
    out.push_back(0xFF); out.push_back(0xD9);
    return out;
}

// ==============================================================================================================
// 3. Pure-C++ BMP/DIB, ICO/CUR & GIF Decoders + BMP Encoder
// ==============================================================================================================
bool DecodeBmpPure(const uint8_t* data, size_t size, DecodedImage& out) {
    if (!data || size < 54 || data[0] != 'B' || data[1] != 'M') return false;
    uint32_t pixOff = Rd32LE(data + 10);
    uint32_t hdrSz  = Rd32LE(data + 14);
    if (hdrSz < 40 || 14 + hdrSz > size || pixOff >= size) return false;

    int32_t bw = static_cast<int32_t>(Rd32LE(data + 18));
    int32_t bh = static_cast<int32_t>(Rd32LE(data + 22));
    uint16_t bpp = Rd16LE(data + 28);
    uint32_t comp = Rd32LE(data + 30);
    if (bw <= 0 || bh == 0 || bw > 16384 || std::abs(bh) > 16384) return false;
    if (comp != 0 && comp != 3) return false;

    bool topDown = (bh < 0);
    uint32_t w = static_cast<uint32_t>(bw);
    uint32_t h = static_cast<uint32_t>(std::abs(bh));

    const uint8_t* pal = data + 14 + hdrSz;
    size_t palEntries = (pixOff > 14 + hdrSz) ? ((pixOff - (14 + hdrSz)) / 4) : 0;

    size_t rowStride = ((static_cast<size_t>(w) * bpp + 31u) / 32u) * 4u;
    if (pixOff + rowStride * h > size) return false;

    out.w = w; out.h = h;
    out.container = GUID_ContainerFormatBmp;
    out.bgra.resize(static_cast<size_t>(w) * h * 4, 255);

    for (uint32_t y = 0; y < h; ++y) {
        uint32_t srcY = topDown ? y : (h - 1 - y);
        const uint8_t* sr = data + pixOff + static_cast<size_t>(srcY) * rowStride;
        uint8_t* dr = &out.bgra[static_cast<size_t>(y) * w * 4];
        for (uint32_t x = 0; x < w; ++x) {
            if (bpp == 32) {
                dr[x*4+0] = sr[x*4+0]; dr[x*4+1] = sr[x*4+1]; dr[x*4+2] = sr[x*4+2]; dr[x*4+3] = sr[x*4+3] ? sr[x*4+3] : 255;
            } else if (bpp == 24) {
                dr[x*4+0] = sr[x*3+0]; dr[x*4+1] = sr[x*3+1]; dr[x*4+2] = sr[x*3+2]; dr[x*4+3] = 255;
            } else if (bpp == 16) {
                uint16_t px = Rd16LE(sr + x * 2);
                dr[x*4+0] = static_cast<uint8_t>(((px & 0x1Fu) * 255u) / 31u);
                dr[x*4+1] = static_cast<uint8_t>((((px >> 5) & 0x1Fu) * 255u) / 31u);
                dr[x*4+2] = static_cast<uint8_t>((((px >> 10) & 0x1Fu) * 255u) / 31u);
                dr[x*4+3] = 255;
            } else if (bpp == 8 || bpp == 4 || bpp == 1) {
                uint32_t idx = 0;
                if (bpp == 8) idx = sr[x];
                else if (bpp == 4) idx = (x & 1u) ? (sr[x / 2] & 0x0Fu) : (sr[x / 2] >> 4);
                else idx = (sr[x / 8] >> (7 - (x & 7u))) & 1u;
                if (idx < palEntries) {
                    dr[x*4+0] = pal[idx*4+0]; dr[x*4+1] = pal[idx*4+1]; dr[x*4+2] = pal[idx*4+2]; dr[x*4+3] = 255;
                }
            } else {
                return false;
            }
        }
    }
    return true;
}

std::vector<uint8_t> EncodeBmpPure(uint32_t w, uint32_t h, const uint8_t* bgra, size_t stride) {
    const size_t imgSz = static_cast<size_t>(w) * h * 4;
    std::vector<uint8_t> out;
    out.reserve(54 + imgSz);
    out.push_back('B'); out.push_back('M');
    Wr32LE(out, static_cast<uint32_t>(54 + imgSz));
    Wr32LE(out, 0);
    Wr32LE(out, 54);
    Wr32LE(out, 40);
    Wr32LE(out, w);
    Wr32LE(out, h); // bottom-up
    Wr16LE(out, 1);
    Wr16LE(out, 32);
    Wr32LE(out, 0);
    Wr32LE(out, static_cast<uint32_t>(imgSz));
    Wr32LE(out, 3780); Wr32LE(out, 3780); Wr32LE(out, 0); Wr32LE(out, 0);
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* row = bgra + static_cast<size_t>(h - 1 - y) * stride;
        out.insert(out.end(), row, row + static_cast<size_t>(w) * 4);
    }
    return out;
}

bool DecodeIcoPure(const uint8_t* data, size_t size, DecodedImage& out) {
    if (!data || size < 22 || Rd16LE(data) != 0) return false;
    uint16_t type = Rd16LE(data + 2);
    uint16_t count = Rd16LE(data + 4);
    if ((type != 1 && type != 2) || count == 0 || 6 + static_cast<size_t>(count) * 16 > size) return false;

    size_t bestIdx = 0;
    uint32_t bestArea = 0;
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t* e = data + 6 + static_cast<size_t>(i) * 16;
        uint32_t ew = e[0] ? e[0] : 256u;
        uint32_t eh = e[1] ? e[1] : 256u;
        if (ew * eh >= bestArea) { bestArea = ew * eh; bestIdx = i; }
    }
    const uint8_t* e = data + 6 + bestIdx * 16;
    uint32_t subSz  = Rd32LE(e + 8);
    uint32_t subOff = Rd32LE(e + 12);
    if (subSz < 40 || subOff + subSz > size) return false;
    const uint8_t* sub = data + subOff;
    if (subSz >= 8 && std::memcmp(sub, "\x89PNG\r\n\x1a\n", 8) == 0) {
        if (!DecodePngPure(sub, subSz, out)) return false;
        out.container = GUID_ContainerFormatIco;
        return true;
    }
    uint32_t biSz = Rd32LE(sub);
    int32_t bw = static_cast<int32_t>(Rd32LE(sub + 4));
    int32_t bh = static_cast<int32_t>(Rd32LE(sub + 8)) / 2;
    uint16_t bpp = Rd16LE(sub + 14);
    if (biSz < 40 || bw <= 0 || bh <= 0 || bw > 1024 || bh > 1024) return false;
    uint32_t w = static_cast<uint32_t>(bw), h = static_cast<uint32_t>(bh);
    size_t palCount = (bpp <= 8) ? (1u << bpp) : 0;
    size_t xorOff = biSz + palCount * 4;
    size_t xorStride = ((static_cast<size_t>(w) * bpp + 31u) / 32u) * 4u;
    size_t andOff = xorOff + xorStride * h;
    size_t andStride = ((static_cast<size_t>(w) + 31u) / 32u) * 4u;
    if (xorOff + xorStride * h > subSz) return false;

    out.w = w; out.h = h;
    out.container = GUID_ContainerFormatIco;
    out.bgra.resize(static_cast<size_t>(w) * h * 4, 255);
    const uint8_t* pal = sub + biSz;
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* sr = sub + xorOff + static_cast<size_t>(h - 1 - y) * xorStride;
        const uint8_t* ar = (andOff + andStride * h <= subSz) ? (sub + andOff + static_cast<size_t>(h - 1 - y) * andStride) : nullptr;
        uint8_t* dr = &out.bgra[static_cast<size_t>(y) * w * 4];
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t maskBit = ar ? ((ar[x / 8] >> (7 - (x & 7u))) & 1u) : 0u;
            if (bpp == 32) {
                dr[x*4+0] = sr[x*4+0]; dr[x*4+1] = sr[x*4+1]; dr[x*4+2] = sr[x*4+2]; dr[x*4+3] = sr[x*4+3];
            } else if (bpp == 24) {
                dr[x*4+0] = sr[x*3+0]; dr[x*4+1] = sr[x*3+1]; dr[x*4+2] = sr[x*3+2]; dr[x*4+3] = maskBit ? 0 : 255;
            } else if (bpp == 8 || bpp == 4 || bpp == 1) {
                uint32_t idx = (bpp == 8) ? sr[x] : (bpp == 4) ? ((x & 1u) ? (sr[x/2] & 0x0Fu) : (sr[x/2] >> 4)) : ((sr[x/8] >> (7 - (x & 7u))) & 1u);
                if (idx < palCount) {
                    dr[x*4+0] = pal[idx*4+0]; dr[x*4+1] = pal[idx*4+1]; dr[x*4+2] = pal[idx*4+2]; dr[x*4+3] = maskBit ? 0 : 255;
                }
            }
        }
    }
    return true;
}

bool DecodeGifPure(const uint8_t* data, size_t size, DecodedImage& out) {
    if (!data || size < 14 || (std::memcmp(data, "GIF87a", 6) != 0 && std::memcmp(data, "GIF89a", 6) != 0)) return false;
    uint16_t w = Rd16LE(data + 6);
    uint16_t h = Rd16LE(data + 8);
    if (w == 0 || h == 0 || w > 8192 || h > 8192) return false;
    uint8_t packed = data[10];
    size_t pos = 13;
    std::vector<uint8_t> gct;
    if (packed & 0x80u) {
        size_t gctBytes = 3u * (1u << ((packed & 7u) + 1u));
        if (pos + gctBytes > size) return false;
        gct.assign(data + pos, data + pos + gctBytes);
        pos += gctBytes;
    }
    int transIdx = -1;
    while (pos < size) {
        uint8_t intro = data[pos++];
        if (intro == 0x3Bu) break;
        if (intro == 0x21u) {
            if (pos >= size) return false;
            uint8_t label = data[pos++];
            if (label == 0xF9u && pos + 6 <= size && data[pos] == 4) {
                if (data[pos + 1] & 1u) transIdx = data[pos + 4];
            }
            while (pos < size) {
                uint8_t subLen = data[pos++];
                if (subLen == 0) break;
                if (pos + subLen > size) return false;
                pos += subLen;
            }
        } else if (intro == 0x2Cu) {
            if (pos + 9 > size) return false;
            uint16_t ix = Rd16LE(data + pos), iy = Rd16LE(data + pos + 2);
            uint16_t iw = Rd16LE(data + pos + 4), ih = Rd16LE(data + pos + 6);
            uint8_t ipack = data[pos + 8];
            pos += 9;
            std::vector<uint8_t> lct;
            if (ipack & 0x80u) {
                size_t lctBytes = 3u * (1u << ((ipack & 7u) + 1u));
                if (pos + lctBytes > size) return false;
                lct.assign(data + pos, data + pos + lctBytes);
                pos += lctBytes;
            }
            const auto& pal = lct.empty() ? gct : lct;
            if (pal.empty() || pos >= size) return false;
            int minCodeSize = data[pos++];
            if (minCodeSize < 2 || minCodeSize > 8) return false;
            std::vector<uint8_t> lzw;
            while (pos < size) {
                uint8_t subLen = data[pos++];
                if (subLen == 0) break;
                if (pos + subLen > size) return false;
                lzw.insert(lzw.end(), data + pos, data + pos + subLen);
                pos += subLen;
            }
            const int clearCode = 1 << minCodeSize;
            const int endCode = clearCode + 1;
            int codeSize = minCodeSize + 1;
            int nextCode = endCode + 1;
            int prevCode = -1;
            std::array<int, 4096> prefix{};
            std::array<uint8_t, 4096> suffix{}, firstCh{};
            for (int i = 0; i < clearCode; ++i) { prefix[i] = -1; suffix[i] = firstCh[i] = static_cast<uint8_t>(i); }

            std::vector<uint8_t> indices;
            indices.reserve(static_cast<size_t>(iw) * ih);
            size_t bitPos = 0;
            while (bitPos + codeSize <= lzw.size() * 8 && indices.size() < static_cast<size_t>(iw) * ih) {
                int code = 0;
                for (int b = 0; b < codeSize; ++b) {
                    size_t bp = bitPos + b;
                    code |= ((lzw[bp >> 3] >> (bp & 7u)) & 1) << b;
                }
                bitPos += codeSize;
                if (code == clearCode) {
                    codeSize = minCodeSize + 1;
                    nextCode = endCode + 1;
                    prevCode = -1;
                    continue;
                }
                if (code == endCode) break;
                int cur = code;
                std::vector<uint8_t> stack;
                if (code >= nextCode) {
                    if (prevCode < 0 || code > nextCode) break;
                    stack.push_back(firstCh[prevCode]);
                    cur = prevCode;
                }
                while (cur >= 0 && cur < 4096) {
                    stack.push_back(suffix[cur]);
                    cur = prefix[cur];
                }
                uint8_t fc = stack.back();
                for (auto it = stack.rbegin(); it != stack.rend() && indices.size() < static_cast<size_t>(iw) * ih; ++it) {
                    indices.push_back(*it);
                }
                if (prevCode >= 0 && nextCode < 4096) {
                    prefix[nextCode] = prevCode;
                    suffix[nextCode] = fc;
                    firstCh[nextCode] = firstCh[prevCode];
                    ++nextCode;
                    if (nextCode == (1 << codeSize) && codeSize < 12) ++codeSize;
                }
                prevCode = code;
            }
            out.w = w; out.h = h;
            out.container = GUID_ContainerFormatGif;
            out.bgra.assign(static_cast<size_t>(w) * h * 4, 0);
            const bool interlaced = (ipack & 0x40u) != 0;
            size_t srcIdx = 0;
            auto putRow = [&](uint16_t ry) {
                for (uint16_t rx = 0; rx < iw && srcIdx < indices.size(); ++rx, ++srcIdx) {
                    uint8_t ci = indices[srcIdx];
                    if (ix + rx >= w || iy + ry >= h) continue;
                    uint8_t* d = &out.bgra[(static_cast<size_t>(iy + ry) * w + (ix + rx)) * 4];
                    if (static_cast<int>(ci) == transIdx) {
                        d[0] = d[1] = d[2] = d[3] = 0;
                    } else if (static_cast<size_t>(ci) * 3 + 2 < pal.size()) {
                        d[2] = pal[ci*3]; d[1] = pal[ci*3+1]; d[0] = pal[ci*3+2]; d[3] = 255;
                    }
                }
            };
            if (!interlaced) {
                for (uint16_t ry = 0; ry < ih; ++ry) putRow(ry);
            } else {
                static const int kStart[4] = {0,4,2,1}, kStep[4] = {8,8,4,2};
                for (int p = 0; p < 4; ++p)
                    for (uint16_t ry = static_cast<uint16_t>(kStart[p]); ry < ih; ry += static_cast<uint16_t>(kStep[p]))
                        putRow(ry);
            }
            return true;
        } else {
            break;
        }
    }
    return false;
}

bool DecodeAnyImageMemory(const uint8_t* data, size_t size, DecodedImage& out) {
    if (!data || size < 4) return false;
    if (size >= 8 && std::memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) return DecodePngPure(data, size, out);
    if (data[0] == 0xFF && data[1] == 0xD8) return DecodeJpegPure(data, size, out);
    if (data[0] == 'B' && data[1] == 'M') return DecodeBmpPure(data, size, out);
    if (size >= 6 && (std::memcmp(data, "GIF87a", 6) == 0 || std::memcmp(data, "GIF89a", 6) == 0)) return DecodeGifPure(data, size, out);
    if (size >= 6 && Rd16LE(data) == 0 && (Rd16LE(data + 2) == 1 || Rd16LE(data + 2) == 2)) return DecodeIcoPure(data, size, out);

    // Fallback to Explorer2's native ImageFormatLoader (TIFF, WebP, JXL, JP2, TGA, QOI, PSD, etc.), guarded against re-entry
    thread_local int s_wicDepth = 0;
    if (s_wicDepth == 0) {
        struct DepthGuard { int& d; DepthGuard(int& v) : d(v) { ++d; } ~DepthGuard() { --d; } } guard(s_wicDepth);
        ExtendedImageInfo ext;
        if (ImageFormatLoader::DecodeImageMemory(data, size, L"", 16384, ext) && ext.valid && ext.width > 0 && ext.height > 0 &&
            ext.bgraPixels.size() == static_cast<size_t>(ext.width) * ext.height * 4)
        {
            out.w = ext.width;
            out.h = ext.height;
            out.container = GUID_ContainerFormatTiff;
            out.bgra = std::move(ext.bgraPixels);
            return true;
        }
    }
    return false;
}

// ==============================================================================================================
// 4. WIC COM Object Implementations
// ==============================================================================================================
class WicMetadataReaderImpl final : public IWICMetadataQueryReader {
public:
    explicit WicMetadataReaderImpl(ExifMetaMap meta) : m_meta(std::move(meta)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IWICMetadataQueryReader)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE GetMetadataByName(LPCWSTR wzName, PROPVARIANT* pvarValue) override {
        if (!wzName || !pvarValue) return E_INVALIDARG;
        PropVariantInit(pvarValue);
        std::wstring key(wzName);
        if ((key == L"/app1/ifd/{ushort=274}" || key == L"/ifd/{ushort=274}") && m_meta.hasOrientation) {
            pvarValue->vt = VT_UI2;
            pvarValue->uiVal = m_meta.orientation;
            return S_OK;
        }
        auto sIt = m_meta.strProps.find(key);
        if (sIt != m_meta.strProps.end()) {
            pvarValue->vt = VT_LPSTR;
            char* dup = static_cast<char*>(CoTaskMemAlloc(sIt->second.size() + 1));
            if (!dup) return E_OUTOFMEMORY;
            std::memcpy(dup, sIt->second.c_str(), sIt->second.size() + 1);
            pvarValue->pszVal = dup;
            return S_OK;
        }
        auto uIt = m_meta.u32Props.find(key);
        if (uIt != m_meta.u32Props.end()) {
            pvarValue->vt = VT_UI4;
            pvarValue->ulVal = uIt->second;
            return S_OK;
        }
        auto rIt = m_meta.u64RatProps.find(key);
        if (rIt != m_meta.u64RatProps.end()) {
            pvarValue->vt = VT_UI8;
            pvarValue->uhVal.QuadPart = rIt->second;
            return S_OK;
        }
        return E_FAIL;
    }
private:
    std::atomic<ULONG> m_ref{ 1 };
    ExifMetaMap m_meta;
};

HRESULT CopyPixelBufferConverted(
    const std::vector<uint8_t>& srcBgra, uint32_t w, uint32_t h,
    const GUID& dstFmt,
    const WICRect* prc, UINT cbStride, UINT cbBufferSize, BYTE* pbBuffer)
{
    if (!pbBuffer || w == 0 || h == 0) return E_INVALIDARG;
    INT rx = prc ? prc->X : 0;
    INT ry = prc ? prc->Y : 0;
    INT rw = prc ? prc->Width : static_cast<INT>(w);
    INT rh = prc ? prc->Height : static_cast<INT>(h);
    if (rx < 0 || ry < 0 || rw <= 0 || rh <= 0 || rx + rw > static_cast<INT>(w) || ry + rh > static_cast<INT>(h)) return E_INVALIDARG;

    UINT bpp = 4;
    if (dstFmt == GUID_WICPixelFormat24bppBGR || dstFmt == GUID_WICPixelFormat24bppRGB) bpp = 3;
    else if (dstFmt == GUID_WICPixelFormat8bppGray) bpp = 1;

    if (cbStride < static_cast<UINT>(rw) * bpp) return E_INVALIDARG;
    const size_t minBuf = static_cast<size_t>(rh - 1) * cbStride + static_cast<size_t>(rw) * bpp;
    if (cbBufferSize < minBuf) return E_INVALIDARG;

    for (INT y = 0; y < rh; ++y) {
        const uint8_t* sr = &srcBgra[(static_cast<size_t>(ry + y) * w + rx) * 4];
        uint8_t* dr = pbBuffer + static_cast<size_t>(y) * cbStride;
        for (INT x = 0; x < rw; ++x) {
            uint8_t b = sr[x*4+0], g = sr[x*4+1], r = sr[x*4+2], a = sr[x*4+3];
            if (dstFmt == GUID_WICPixelFormat32bppBGRA || dstFmt == GUID_WICPixelFormat32bppBGR) {
                dr[x*4+0] = b; dr[x*4+1] = g; dr[x*4+2] = r; dr[x*4+3] = (dstFmt == GUID_WICPixelFormat32bppBGR) ? 255 : a;
            } else if (dstFmt == GUID_WICPixelFormat32bppPBGRA) {
                dr[x*4+0] = static_cast<uint8_t>((b * a + 127) / 255);
                dr[x*4+1] = static_cast<uint8_t>((g * a + 127) / 255);
                dr[x*4+2] = static_cast<uint8_t>((r * a + 127) / 255);
                dr[x*4+3] = a;
            } else if (dstFmt == GUID_WICPixelFormat32bppRGBA) {
                dr[x*4+0] = r; dr[x*4+1] = g; dr[x*4+2] = b; dr[x*4+3] = a;
            } else if (dstFmt == GUID_WICPixelFormat24bppBGR) {
                dr[x*3+0] = b; dr[x*3+1] = g; dr[x*3+2] = r;
            } else if (dstFmt == GUID_WICPixelFormat24bppRGB) {
                dr[x*3+0] = r; dr[x*3+1] = g; dr[x*3+2] = b;
            } else if (dstFmt == GUID_WICPixelFormat8bppGray) {
                dr[x] = static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
            } else {
                dr[x*4+0] = b; dr[x*4+1] = g; dr[x*4+2] = r; dr[x*4+3] = a;
            }
        }
    }
    return S_OK;
}

bool PullSourceToStraightBgra(IWICBitmapSource* src, uint32_t& outW, uint32_t& outH, std::vector<uint8_t>& outBgra) {
    if (!src) return false;
    UINT w = 0, h = 0;
    if (FAILED(src->GetSize(&w, &h)) || w == 0 || h == 0) return false;
    GUID fmt = GUID_WICPixelFormat32bppBGRA;
    src->GetPixelFormat(&fmt);
    outW = w; outH = h;
    outBgra.resize(static_cast<size_t>(w) * h * 4, 255);

    if (fmt == GUID_WICPixelFormat24bppBGR || fmt == GUID_WICPixelFormat24bppRGB) {
        std::vector<uint8_t> tmp(static_cast<size_t>(w) * h * 3);
        if (FAILED(src->CopyPixels(nullptr, w * 3, static_cast<UINT>(tmp.size()), tmp.data()))) return false;
        const bool isRgb = (fmt == GUID_WICPixelFormat24bppRGB);
        for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
            outBgra[i*4+0] = tmp[i*3 + (isRgb ? 2 : 0)];
            outBgra[i*4+1] = tmp[i*3 + 1];
            outBgra[i*4+2] = tmp[i*3 + (isRgb ? 0 : 2)];
            outBgra[i*4+3] = 255;
        }
        return true;
    }
    if (fmt == GUID_WICPixelFormat8bppGray) {
        std::vector<uint8_t> tmp(static_cast<size_t>(w) * h);
        if (FAILED(src->CopyPixels(nullptr, w, static_cast<UINT>(tmp.size()), tmp.data()))) return false;
        for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
            outBgra[i*4+0] = outBgra[i*4+1] = outBgra[i*4+2] = tmp[i];
            outBgra[i*4+3] = 255;
        }
        return true;
    }

    if (FAILED(src->CopyPixels(nullptr, w * 4, static_cast<UINT>(outBgra.size()), outBgra.data()))) return false;
    if (fmt == GUID_WICPixelFormat32bppRGBA) {
        for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) std::swap(outBgra[i*4+0], outBgra[i*4+2]);
    } else if (fmt == GUID_WICPixelFormat32bppPBGRA) {
        for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
            uint8_t a = outBgra[i*4+3];
            if (a > 0 && a < 255) {
                outBgra[i*4+0] = static_cast<uint8_t>(std::min(255, (outBgra[i*4+0] * 255 + a / 2) / a));
                outBgra[i*4+1] = static_cast<uint8_t>(std::min(255, (outBgra[i*4+1] * 255 + a / 2) / a));
                outBgra[i*4+2] = static_cast<uint8_t>(std::min(255, (outBgra[i*4+2] * 255 + a / 2) / a));
            }
        }
    }
    return true;
}

class WicFrameDecodeImpl final : public IWICBitmapFrameDecode {
public:
    explicit WicFrameDecodeImpl(std::shared_ptr<DecodedImage> img) : m_img(std::move(img)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IWICBitmapSource) || riid == __uuidof(IWICBitmapFrameDecode)) {
            *ppv = static_cast<IWICBitmapFrameDecode*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE GetSize(UINT* pw, UINT* ph) override {
        if (!pw || !ph) return E_POINTER;
        *pw = m_img->w; *ph = m_img->h; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* pf) override {
        if (!pf) return E_POINTER;
        *pf = GUID_WICPixelFormat32bppBGRA; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* prc, UINT cbStride, UINT cbBufferSize, BYTE* pbBuffer) override {
        return CopyPixelBufferConverted(m_img->bgra, m_img->w, m_img->h, GUID_WICPixelFormat32bppBGRA, prc, cbStride, cbBufferSize, pbBuffer);
    }
    HRESULT STDMETHODCALLTYPE GetMetadataQueryReader(IWICMetadataQueryReader** ppReader) override {
        if (!ppReader) return E_POINTER;
        *ppReader = new WicMetadataReaderImpl(m_img->exif);
        return S_OK;
    }
private:
    std::atomic<ULONG> m_ref{ 1 };
    std::shared_ptr<DecodedImage> m_img;
};

class WicDecoderImpl final : public IWICBitmapDecoder {
public:
    explicit WicDecoderImpl(std::shared_ptr<DecodedImage> img) : m_img(std::move(img)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IWICBitmapDecoder)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE GetContainerFormat(GUID* pGuid) override {
        if (!pGuid) return E_POINTER;
        *pGuid = m_img->container; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFrameCount(UINT* pCount) override {
        if (!pCount) return E_POINTER;
        *pCount = 1; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFrame(UINT index, IWICBitmapFrameDecode** ppFrame) override {
        if (!ppFrame) return E_POINTER;
        if (index != 0) { *ppFrame = nullptr; return E_INVALIDARG; }
        *ppFrame = new WicFrameDecodeImpl(m_img);
        return S_OK;
    }
private:
    std::atomic<ULONG> m_ref{ 1 };
    std::shared_ptr<DecodedImage> m_img;
};

class WicFormatConverterImpl final : public IWICFormatConverter {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IWICBitmapSource) || riid == __uuidof(IWICFormatConverter)) {
            *ppv = static_cast<IWICFormatConverter*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE Initialize(IWICBitmapSource* pISource, REFWICPixelFormatGUID dstFormat, WICBitmapDitherType, IWICPalette*, double, WICBitmapPaletteType) override {
        if (!PullSourceToStraightBgra(pISource, m_w, m_h, m_bgra)) return E_FAIL;
        m_fmt = dstFormat;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSize(UINT* pw, UINT* ph) override {
        if (!pw || !ph) return E_POINTER;
        *pw = m_w; *ph = m_h; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* pf) override {
        if (!pf) return E_POINTER;
        *pf = m_fmt; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* prc, UINT cbStride, UINT cbBufferSize, BYTE* pbBuffer) override {
        return CopyPixelBufferConverted(m_bgra, m_w, m_h, m_fmt, prc, cbStride, cbBufferSize, pbBuffer);
    }
private:
    std::atomic<ULONG> m_ref{ 1 };
    uint32_t m_w = 0, m_h = 0;
    GUID m_fmt = GUID_WICPixelFormat32bppBGRA;
    std::vector<uint8_t> m_bgra;
};

class WicScalerImpl final : public IWICBitmapScaler {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IWICBitmapSource) || riid == __uuidof(IWICBitmapScaler)) {
            *ppv = static_cast<IWICBitmapScaler*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE Initialize(IWICBitmapSource* pISource, UINT uiWidth, UINT uiHeight, WICBitmapInterpolationMode mode) override {
        if (uiWidth == 0 || uiHeight == 0 || uiWidth > 16384 || uiHeight > 16384) return E_INVALIDARG;
        uint32_t sw = 0, sh = 0;
        std::vector<uint8_t> sBgra;
        if (!PullSourceToStraightBgra(pISource, sw, sh, sBgra)) return E_FAIL;
        m_w = uiWidth; m_h = uiHeight;
        m_bgra.resize(static_cast<size_t>(m_w) * m_h * 4);

        for (uint32_t y = 0; y < m_h; ++y) {
            float fy = ((y + 0.5f) * sh / static_cast<float>(m_h)) - 0.5f;
            int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, static_cast<int>(sh - 1));
            int y1 = std::min(y0 + 1, static_cast<int>(sh - 1));
            float wy = std::clamp(fy - y0, 0.0f, 1.0f);
            for (uint32_t x = 0; x < m_w; ++x) {
                uint8_t* dst = &m_bgra[(static_cast<size_t>(y) * m_w + x) * 4];
                if (mode == WICBitmapInterpolationModeNearestNeighbor) {
                    uint32_t nx = std::min(sw - 1, (x * sw) / m_w);
                    uint32_t ny = std::min(sh - 1, (y * sh) / m_h);
                    std::memcpy(dst, &sBgra[(static_cast<size_t>(ny) * sw + nx) * 4], 4);
                } else {
                    float fx = ((x + 0.5f) * sw / static_cast<float>(m_w)) - 0.5f;
                    int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, static_cast<int>(sw - 1));
                    int x1 = std::min(x0 + 1, static_cast<int>(sw - 1));
                    float wx = std::clamp(fx - x0, 0.0f, 1.0f);
                    const uint8_t* p00 = &sBgra[(static_cast<size_t>(y0) * sw + x0) * 4];
                    const uint8_t* p10 = &sBgra[(static_cast<size_t>(y0) * sw + x1) * 4];
                    const uint8_t* p01 = &sBgra[(static_cast<size_t>(y1) * sw + x0) * 4];
                    const uint8_t* p11 = &sBgra[(static_cast<size_t>(y1) * sw + x1) * 4];
                    for (int c = 0; c < 4; ++c) {
                        float v0 = p00[c] * (1.0f - wx) + p10[c] * wx;
                        float v1 = p01[c] * (1.0f - wx) + p11[c] * wx;
                        dst[c] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(v0 * (1.0f - wy) + v1 * wy)), 0, 255));
                    }
                }
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSize(UINT* pw, UINT* ph) override { if (!pw || !ph) return E_POINTER; *pw = m_w; *ph = m_h; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* pf) override { if (!pf) return E_POINTER; *pf = GUID_WICPixelFormat32bppBGRA; return S_OK; }
    HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* prc, UINT cbStride, UINT cbBufferSize, BYTE* pbBuffer) override {
        return CopyPixelBufferConverted(m_bgra, m_w, m_h, GUID_WICPixelFormat32bppBGRA, prc, cbStride, cbBufferSize, pbBuffer);
    }
private:
    std::atomic<ULONG> m_ref{ 1 };
    uint32_t m_w = 0, m_h = 0;
    std::vector<uint8_t> m_bgra;
};

class WicFlipRotatorImpl final : public IWICBitmapFlipRotator {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IWICBitmapSource) || riid == __uuidof(IWICBitmapFlipRotator)) {
            *ppv = static_cast<IWICBitmapFlipRotator*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE Initialize(IWICBitmapSource* pISource, WICBitmapTransformOptions options) override {
        uint32_t sw = 0, sh = 0;
        std::vector<uint8_t> sBgra;
        if (!PullSourceToStraightBgra(pISource, sw, sh, sBgra)) return E_FAIL;

        const int rot = static_cast<int>(options) & 0x3;
        const bool flipH = (options & WICBitmapTransformFlipHorizontal) != 0;
        const bool flipV = (options & WICBitmapTransformFlipVertical) != 0;

        // Apply flip first on (sw, sh), or WIC convention: Rotate then Flip!
        // Note: WIC applies rotation first (CW), then horizontal/vertical flip!
        uint32_t rw = (rot == WICBitmapTransformRotate90 || rot == WICBitmapTransformRotate270) ? sh : sw;
        uint32_t rh = (rot == WICBitmapTransformRotate90 || rot == WICBitmapTransformRotate270) ? sw : sh;
        std::vector<uint8_t> rotBuf(static_cast<size_t>(rw) * rh * 4);
        for (uint32_t y = 0; y < sh; ++y) {
            for (uint32_t x = 0; x < sw; ++x) {
                uint32_t dx = x, dy = y;
                if (rot == WICBitmapTransformRotate90)       { dx = sh - 1 - y; dy = x; }
                else if (rot == WICBitmapTransformRotate180) { dx = sw - 1 - x; dy = sh - 1 - y; }
                else if (rot == WICBitmapTransformRotate270) { dx = y; dy = sw - 1 - x; }
                if (flipH) dx = rw - 1 - dx;
                if (flipV) dy = rh - 1 - dy;
                std::memcpy(&rotBuf[(static_cast<size_t>(dy) * rw + dx) * 4], &sBgra[(static_cast<size_t>(y) * sw + x) * 4], 4);
            }
        }
        m_w = rw; m_h = rh;
        m_bgra = std::move(rotBuf);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSize(UINT* pw, UINT* ph) override { if (!pw || !ph) return E_POINTER; *pw = m_w; *ph = m_h; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* pf) override { if (!pf) return E_POINTER; *pf = GUID_WICPixelFormat32bppBGRA; return S_OK; }
    HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* prc, UINT cbStride, UINT cbBufferSize, BYTE* pbBuffer) override {
        return CopyPixelBufferConverted(m_bgra, m_w, m_h, GUID_WICPixelFormat32bppBGRA, prc, cbStride, cbBufferSize, pbBuffer);
    }
private:
    std::atomic<ULONG> m_ref{ 1 };
    uint32_t m_w = 0, m_h = 0;
    std::vector<uint8_t> m_bgra;
};

class WicBitmapLockImpl final : public IWICBitmapLock {
public:
    WicBitmapLockImpl(IUnknown* owner, BYTE* ptr, UINT w, UINT h, UINT stride, const GUID& fmt)
        : m_owner(owner), m_ptr(ptr), m_w(w), m_h(h), m_stride(stride), m_fmt(fmt)
    {
        if (m_owner) m_owner->AddRef();
    }
    ~WicBitmapLockImpl() override { if (m_owner) m_owner->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IWICBitmapLock)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE GetSize(UINT* pw, UINT* ph) override { if (!pw || !ph) return E_POINTER; *pw = m_w; *ph = m_h; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetStride(UINT* ps) override { if (!ps) return E_POINTER; *ps = m_stride; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDataPointer(UINT* pcb, BYTE** ppb) override {
        if (!pcb || !ppb) return E_POINTER;
        *pcb = m_stride * m_h;
        *ppb = m_ptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* pf) override { if (!pf) return E_POINTER; *pf = m_fmt; return S_OK; }
private:
    std::atomic<ULONG> m_ref{ 1 };
    IUnknown* m_owner = nullptr;
    BYTE* m_ptr = nullptr;
    UINT m_w = 0, m_h = 0, m_stride = 0;
    GUID m_fmt{};
};

class WicBitmapImpl final : public IWICBitmap {
public:
    WicBitmapImpl(UINT w, UINT h, const GUID& fmt, const BYTE* initBuf = nullptr, UINT initStride = 0)
        : m_w(w), m_h(h), m_fmt(fmt)
    {
        UINT bpp = (fmt == GUID_WICPixelFormat24bppBGR || fmt == GUID_WICPixelFormat24bppRGB) ? 3u
                 : (fmt == GUID_WICPixelFormat8bppGray) ? 1u : 4u;
        m_stride = w * bpp;
        m_buf.resize(static_cast<size_t>(m_stride) * h, 0);
        if (initBuf && initStride >= m_stride) {
            for (UINT y = 0; y < h; ++y) {
                std::memcpy(m_buf.data() + static_cast<size_t>(y) * m_stride, initBuf + static_cast<size_t>(y) * initStride, m_stride);
            }
        }
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IWICBitmapSource) || riid == __uuidof(IWICBitmap)) {
            *ppv = static_cast<IWICBitmap*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE GetSize(UINT* pw, UINT* ph) override { if (!pw || !ph) return E_POINTER; *pw = m_w; *ph = m_h; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* pf) override { if (!pf) return E_POINTER; *pf = m_fmt; return S_OK; }
    HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* prc, UINT cbStride, UINT cbBufferSize, BYTE* pbBuffer) override {
        if (!pbBuffer) return E_POINTER;
        UINT bpp = m_stride / std::max(1u, m_w);
        INT rx = prc ? prc->X : 0, ry = prc ? prc->Y : 0;
        INT rw = prc ? prc->Width : static_cast<INT>(m_w), rh = prc ? prc->Height : static_cast<INT>(m_h);
        if (rx < 0 || ry < 0 || rw <= 0 || rh <= 0 || rx + rw > static_cast<INT>(m_w) || ry + rh > static_cast<INT>(m_h)) return E_INVALIDARG;
        if (cbStride < static_cast<UINT>(rw) * bpp) return E_INVALIDARG;
        if (cbBufferSize < static_cast<size_t>(rh - 1) * cbStride + static_cast<size_t>(rw) * bpp) return E_INVALIDARG;
        for (INT y = 0; y < rh; ++y) {
            std::memcpy(pbBuffer + static_cast<size_t>(y) * cbStride,
                        m_buf.data() + static_cast<size_t>(ry + y) * m_stride + static_cast<size_t>(rx) * bpp,
                        static_cast<size_t>(rw) * bpp);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Lock(const WICRect* prc, DWORD, IWICBitmapLock** ppILock) override {
        if (!ppILock) return E_POINTER;
        UINT bpp = m_stride / std::max(1u, m_w);
        INT rx = prc ? prc->X : 0, ry = prc ? prc->Y : 0;
        INT rw = prc ? prc->Width : static_cast<INT>(m_w), rh = prc ? prc->Height : static_cast<INT>(m_h);
        BYTE* ptr = m_buf.data() + static_cast<size_t>(ry) * m_stride + static_cast<size_t>(rx) * bpp;
        *ppILock = new WicBitmapLockImpl(this, ptr, static_cast<UINT>(rw), static_cast<UINT>(rh), m_stride, m_fmt);
        return S_OK;
    }
private:
    std::atomic<ULONG> m_ref{ 1 };
    UINT m_w = 0, m_h = 0, m_stride = 0;
    GUID m_fmt{};
    std::vector<uint8_t> m_buf;
};

class WicPropertyBagImpl final : public IPropertyBag2 {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IPropertyBag2)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE Read(ULONG, PROPBAG2*, IErrorLog*, VARIANT*, HRESULT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Write(ULONG cProps, PROPBAG2* pPropBag, VARIANT* pvarValue) override {
        if (!pPropBag || !pvarValue) return E_INVALIDARG;
        for (ULONG i = 0; i < cProps; ++i) {
            if (pPropBag[i].pstrName && _wcsicmp(pPropBag[i].pstrName, L"ImageQuality") == 0) {
                if (pvarValue[i].vt == VT_R4) m_quality = std::clamp(pvarValue[i].fltVal, 0.01f, 1.0f);
                else if (pvarValue[i].vt == VT_R8) m_quality = std::clamp(static_cast<float>(pvarValue[i].dblVal), 0.01f, 1.0f);
            }
        }
        return S_OK;
    }
    float Quality() const { return m_quality; }
private:
    std::atomic<ULONG> m_ref{ 1 };
    float m_quality = 0.90f;
};

class WicFrameEncodeImpl final : public IWICBitmapFrameEncode {
public:
    WicFrameEncodeImpl(const GUID& container, IStream* stream, WicPropertyBagImpl* bag)
        : m_container(container), m_stream(stream), m_bag(bag)
    {
        if (m_stream) m_stream->AddRef();
        if (m_bag) m_bag->AddRef();
    }
    ~WicFrameEncodeImpl() override {
        if (m_stream) m_stream->Release();
        if (m_bag) m_bag->Release();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IWICBitmapFrameEncode)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE Initialize(IPropertyBag2*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetSize(UINT w, UINT h) override { m_w = w; m_h = h; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetPixelFormat(WICPixelFormatGUID* pf) override {
        if (pf) *pf = GUID_WICPixelFormat32bppBGRA;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE WritePixels(UINT lineCount, UINT cbStride, UINT, BYTE* pbPixels) override {
        if (!pbPixels || m_w == 0) return E_INVALIDARG;
        if (m_bgra.empty()) m_bgra.resize(static_cast<size_t>(m_w) * m_h * 4, 255);
        for (UINT y = 0; y < lineCount && m_curLine < m_h; ++y, ++m_curLine) {
            std::memcpy(&m_bgra[static_cast<size_t>(m_curLine) * m_w * 4], pbPixels + static_cast<size_t>(y) * cbStride, static_cast<size_t>(m_w) * 4);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE WriteSource(IWICBitmapSource* pISource, WICRect*) override {
        return PullSourceToStraightBgra(pISource, m_w, m_h, m_bgra) ? S_OK : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE Commit() override {
        if (!m_stream || m_w == 0 || m_h == 0 || m_bgra.empty()) return E_FAIL;
        std::vector<uint8_t> encoded;
        if (m_container == GUID_ContainerFormatJpeg) {
            float q = m_bag ? m_bag->Quality() : 0.90f;
            encoded = EncodeJpegPure(m_w, m_h, m_bgra.data(), static_cast<size_t>(m_w) * 4, q);
        } else if (m_container == GUID_ContainerFormatBmp) {
            encoded = EncodeBmpPure(m_w, m_h, m_bgra.data(), static_cast<size_t>(m_w) * 4);
        } else {
            encoded = EncodePngPure(m_w, m_h, m_bgra.data(), static_cast<size_t>(m_w) * 4);
        }
        ULONG written = 0;
        return m_stream->Write(encoded.data(), static_cast<ULONG>(encoded.size()), &written);
    }
private:
    std::atomic<ULONG> m_ref{ 1 };
    GUID m_container{};
    IStream* m_stream = nullptr;
    WicPropertyBagImpl* m_bag = nullptr;
    uint32_t m_w = 0, m_h = 0, m_curLine = 0;
    std::vector<uint8_t> m_bgra;
};

class WicEncoderImpl final : public IWICBitmapEncoder {
public:
    explicit WicEncoderImpl(const GUID& container) : m_container(container) {}
    ~WicEncoderImpl() override { if (m_stream) m_stream->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IWICBitmapEncoder)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE Initialize(IStream* pIStream, WICBitmapEncoderCacheOption) override {
        if (!pIStream) return E_INVALIDARG;
        if (m_stream) m_stream->Release();
        m_stream = pIStream;
        m_stream->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateNewFrame(IWICBitmapFrameEncode** ppFrame, IPropertyBag2** ppBag) override {
        if (!ppFrame || !m_stream) return E_INVALIDARG;
        auto* bag = new WicPropertyBagImpl();
        *ppFrame = new WicFrameEncodeImpl(m_container, m_stream, bag);
        if (ppBag) { *ppBag = bag; }
        else { bag->Release(); }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Commit() override {
        return m_stream ? m_stream->Commit(0) : E_FAIL;
    }
private:
    std::atomic<ULONG> m_ref{ 1 };
    GUID m_container{};
    IStream* m_stream = nullptr;
};

class WicImagingFactoryImpl final : public IWICImagingFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IWICImagingFactory)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE CreateDecoderFromFilename(LPCWSTR wzFilename, const GUID*, DWORD, WICDecodeOptions, IWICBitmapDecoder** ppIDecoder) override {
        if (!wzFilename || !ppIDecoder) return E_INVALIDARG;
        *ppIDecoder = nullptr;
        std::vector<uint8_t> bytes;
        if (!PathUtils::ReadFileBytes(wzFilename, 256ULL * 1024 * 1024, bytes) || bytes.empty()) return E_FAIL;
        auto img = std::make_shared<DecodedImage>();
        if (!DecodeAnyImageMemory(bytes.data(), bytes.size(), *img)) return E_FAIL;
        *ppIDecoder = new WicDecoderImpl(std::move(img));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateDecoderFromStream(IStream* pIStream, const GUID*, WICDecodeOptions, IWICBitmapDecoder** ppIDecoder) override {
        if (!pIStream || !ppIDecoder) return E_INVALIDARG;
        *ppIDecoder = nullptr;
        LARGE_INTEGER z{};
        pIStream->Seek(z, STREAM_SEEK_SET, nullptr);
        std::vector<uint8_t> bytes;
        uint8_t buf[32768];
        while (true) {
            ULONG got = 0;
            HRESULT hr = pIStream->Read(buf, sizeof(buf), &got);
            if (got > 0) bytes.insert(bytes.end(), buf, buf + got);
            if (FAILED(hr) || got < sizeof(buf)) break;
        }
        if (bytes.empty()) return E_FAIL;
        auto img = std::make_shared<DecodedImage>();
        if (!DecodeAnyImageMemory(bytes.data(), bytes.size(), *img)) return E_FAIL;
        *ppIDecoder = new WicDecoderImpl(std::move(img));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateFormatConverter(IWICFormatConverter** ppConv) override {
        if (!ppConv) return E_POINTER;
        *ppConv = new WicFormatConverterImpl();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBitmapScaler(IWICBitmapScaler** ppScaler) override {
        if (!ppScaler) return E_POINTER;
        *ppScaler = new WicScalerImpl();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBitmapFlipRotator(IWICBitmapFlipRotator** ppRot) override {
        if (!ppRot) return E_POINTER;
        *ppRot = new WicFlipRotatorImpl();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateStream(IWICStream** ppStream) override {
        return CreateWicStreamInstance(ppStream);
    }
    HRESULT STDMETHODCALLTYPE CreateEncoder(REFGUID guidContainerFormat, const GUID*, IWICBitmapEncoder** ppEnc) override {
        if (!ppEnc) return E_POINTER;
        *ppEnc = new WicEncoderImpl(guidContainerFormat);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBitmap(UINT w, UINT h, REFWICPixelFormatGUID fmt, WICBitmapCreateCacheOption, IWICBitmap** ppBmp) override {
        if (!ppBmp || w == 0 || h == 0) return E_INVALIDARG;
        *ppBmp = new WicBitmapImpl(w, h, fmt);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBitmapFromSource(IWICBitmapSource* pSrc, WICBitmapCreateCacheOption, IWICBitmap** ppBmp) override {
        if (!pSrc || !ppBmp) return E_INVALIDARG;
        uint32_t w = 0, h = 0;
        std::vector<uint8_t> bgra;
        if (!PullSourceToStraightBgra(pSrc, w, h, bgra)) return E_FAIL;
        *ppBmp = new WicBitmapImpl(w, h, GUID_WICPixelFormat32bppBGRA, bgra.data(), w * 4);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBitmapFromMemory(UINT w, UINT h, REFWICPixelFormatGUID fmt, UINT cbStride, UINT cbBuf, BYTE* pbBuf, IWICBitmap** ppBmp) override {
        if (!ppBmp || !pbBuf || w == 0 || h == 0 || cbBuf < static_cast<size_t>(h - 1) * cbStride + w) return E_INVALIDARG;
        *ppBmp = new WicBitmapImpl(w, h, fmt, pbBuf, cbStride);
        return S_OK;
    }

private:
    std::atomic<ULONG> m_ref{ 1 };
};

} // namespace

HRESULT CreateWicImagingFactory(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    auto* f = new WicImagingFactoryImpl();
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

} // namespace Explorer2::Hal
