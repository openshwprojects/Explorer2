#pragma once
// ExtendedImages100.h — 18 Popular Graphics, Texture, Design, Cinema & Font Specimen Decoders
// Covers: .exr, .ktx2, .basis, .kra, .ora, .clip, .procreate, .aseprite, .icns,
//         .jxl, .bpg, .blp, .utx, .dpx, .cin, .aco, .cube, .3dl, .ies, .ttf, .otf, .woff, .woff2

#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace Formats {

namespace DetailImg100 {

inline uint16_t RdU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint16_t RdU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
inline int16_t RdI16BE(const uint8_t* p) {
    return static_cast<int16_t>(RdU16BE(p));
}
inline uint32_t RdU24LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16);
}
inline uint32_t RdU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
inline uint32_t RdU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}
inline uint64_t RdU64LE(const uint8_t* p) {
    return static_cast<uint64_t>(RdU32LE(p)) | (static_cast<uint64_t>(RdU32LE(p + 4)) << 32);
}
inline uint64_t RdU64BE(const uint8_t* p) {
    return (static_cast<uint64_t>(RdU32BE(p)) << 32) | static_cast<uint64_t>(RdU32BE(p + 4));
}
inline float HalfToFloat(uint16_t h) {
    return GpuTex::HalfToFloat(h);
}

// Minimal 5x7 ASCII bitmap font for rendering technical badges
inline uint8_t GlyphRow5x7(char c, int row) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    switch (c) {
    case 'A': { const uint8_t g[7]={0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}; return g[row]; }
    case 'B': { const uint8_t g[7]={0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}; return g[row]; }
    case 'C': { const uint8_t g[7]={0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}; return g[row]; }
    case 'D': { const uint8_t g[7]={0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}; return g[row]; }
    case 'E': { const uint8_t g[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}; return g[row]; }
    case 'F': { const uint8_t g[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}; return g[row]; }
    case 'G': { const uint8_t g[7]={0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}; return g[row]; }
    case 'H': { const uint8_t g[7]={0x11,0x11,0x11,0x1F,0x11,0x11,0x11}; return g[row]; }
    case 'I': { const uint8_t g[7]={0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}; return g[row]; }
    case 'J': { const uint8_t g[7]={0x07,0x02,0x02,0x02,0x02,0x12,0x0C}; return g[row]; }
    case 'K': { const uint8_t g[7]={0x11,0x12,0x14,0x18,0x14,0x12,0x11}; return g[row]; }
    case 'L': { const uint8_t g[7]={0x10,0x10,0x10,0x10,0x10,0x10,0x1F}; return g[row]; }
    case 'M': { const uint8_t g[7]={0x11,0x1B,0x15,0x15,0x11,0x11,0x11}; return g[row]; }
    case 'N': { const uint8_t g[7]={0x11,0x19,0x15,0x13,0x11,0x11,0x11}; return g[row]; }
    case 'O': { const uint8_t g[7]={0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}; return g[row]; }
    case 'P': { const uint8_t g[7]={0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}; return g[row]; }
    case 'Q': { const uint8_t g[7]={0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}; return g[row]; }
    case 'R': { const uint8_t g[7]={0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}; return g[row]; }
    case 'S': { const uint8_t g[7]={0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}; return g[row]; }
    case 'T': { const uint8_t g[7]={0x1F,0x04,0x04,0x04,0x04,0x04,0x04}; return g[row]; }
    case 'U': { const uint8_t g[7]={0x11,0x11,0x11,0x11,0x11,0x11,0x0E}; return g[row]; }
    case 'V': { const uint8_t g[7]={0x11,0x11,0x11,0x11,0x11,0x0A,0x04}; return g[row]; }
    case 'W': { const uint8_t g[7]={0x11,0x11,0x11,0x15,0x15,0x1B,0x11}; return g[row]; }
    case 'X': { const uint8_t g[7]={0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}; return g[row]; }
    case 'Y': { const uint8_t g[7]={0x11,0x11,0x0A,0x04,0x04,0x04,0x04}; return g[row]; }
    case 'Z': { const uint8_t g[7]={0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}; return g[row]; }
    case '0': { const uint8_t g[7]={0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}; return g[row]; }
    case '1': { const uint8_t g[7]={0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}; return g[row]; }
    case '2': { const uint8_t g[7]={0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}; return g[row]; }
    case '3': { const uint8_t g[7]={0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}; return g[row]; }
    case '4': { const uint8_t g[7]={0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}; return g[row]; }
    case '5': { const uint8_t g[7]={0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}; return g[row]; }
    case '6': { const uint8_t g[7]={0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}; return g[row]; }
    case '7': { const uint8_t g[7]={0x1F,0x01,0x02,0x04,0x08,0x08,0x08}; return g[row]; }
    case '8': { const uint8_t g[7]={0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}; return g[row]; }
    case '9': { const uint8_t g[7]={0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}; return g[row]; }
    case ':': { const uint8_t g[7]={0x00,0x04,0x04,0x00,0x04,0x04,0x00}; return g[row]; }
    case '.': { const uint8_t g[7]={0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}; return g[row]; }
    case '-': { const uint8_t g[7]={0x00,0x00,0x00,0x1F,0x00,0x00,0x00}; return g[row]; }
    case '/': { const uint8_t g[7]={0x01,0x02,0x02,0x04,0x08,0x08,0x10}; return g[row]; }
    default:  return 0;
    }
}

inline void DrawBitmapText5x7(
    std::vector<uint8_t>& bgra, uint32_t imgW, uint32_t imgH,
    uint32_t x, uint32_t y, std::string_view text, int scale,
    uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t curX = x;
    for (char ch : text) {
        if (ch == ' ') { curX += static_cast<uint32_t>(6 * scale); continue; }
        for (int ry = 0; ry < 7; ++ry) {
            uint8_t bits = GlyphRow5x7(ch, ry);
            for (int rx = 0; rx < 5; ++rx) {
                if ((bits >> (4 - rx)) & 1) {
                    for (int sy = 0; sy < scale; ++sy) {
                        for (int sx = 0; sx < scale; ++sx) {
                            uint32_t px = curX + static_cast<uint32_t>(rx * scale + sx);
                            uint32_t py = y + static_cast<uint32_t>(ry * scale + sy);
                            if (px < imgW && py < imgH) {
                                uint8_t* dst = &bgra[(static_cast<size_t>(py) * imgW + px) * 4];
                                dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255;
                            }
                        }
                    }
                }
            }
        }
        curX += static_cast<uint32_t>(6 * scale);
    }
}

// Decode an embedded PNG, JPEG, JP2, or JXL buffer via Windows WIC into ExtendedImageInfo
inline bool DecodeEmbeddedWicBuffer(const uint8_t* bytes, size_t len, const std::wstring& fmtLabel, ExtendedImageInfo& outInfo) {
    if (!bytes || len < 16) return false;
    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return false;

    IWICStream* stream = nullptr;
    bool decoded = false;
    if (SUCCEEDED(factory->CreateStream(&stream)) && stream) {
        if (SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes), static_cast<DWORD>(len)))) {
            IWICBitmapDecoder* decoder = nullptr;
            if (SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) && decoder) {
                IWICBitmapFrameDecode* frame = nullptr;
                if (SUCCEEDED(decoder->GetFrame(0, &frame)) && frame) {
                    UINT w = 0, h = 0;
                    frame->GetSize(&w, &h);
                    if (w > 0 && h > 0 && w <= 16384 && h <= 16384) {
                        IWICFormatConverter* conv = nullptr;
                        if (SUCCEEDED(factory->CreateFormatConverter(&conv)) && conv) {
                            if (SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
                            {
                                outInfo.width = w;
                                outInfo.height = h;
                                outInfo.channels = 4;
                                outInfo.bitsPerChannel = 8;
                                outInfo.formatName = fmtLabel;
                                outInfo.bgraPixels.resize(static_cast<size_t>(w) * h * 4);
                                if (SUCCEEDED(conv->CopyPixels(nullptr, w * 4,
                                    static_cast<UINT>(outInfo.bgraPixels.size()), outInfo.bgraPixels.data())))
                                {
                                    outInfo.valid = true;
                                    decoded = true;
                                }
                            }
                            conv->Release();
                        }
                    }
                    frame->Release();
                }
                decoder->Release();
            }
        }
        stream->Release();
    }
    factory->Release();
    return decoded;
}

inline bool EqualsIgnoreCaseAscii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

} // namespace DetailImg100

#include "ExrUpgrade.h"

// ============================================================================
// 1. OpenEXR (.exr) — Magic 0x76, 0x2F, 0x31, 0x01
//    Delegates to ExrUpgrade.h (NO, RLE, ZIPS, ZIP, PIZ, PXR24, B44, B44A, DWAA, DWAB + Multi-Part v2)
// ============================================================================
inline bool DecodeOpenEXR(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return DecodeOpenEXRUpgraded(data, size, outInfo);
}

// ============================================================================
// 2. Khronos KTX 2.0 (.ktx2) & Basis Universal (.basis)
// ============================================================================
namespace DetailBasis {

inline uint16_t BasisCrc16(const void* r, size_t sz, uint16_t crc = 0) {
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
    crc = static_cast<uint16_t>(~crc);
    const uint8_t* p = static_cast<const uint8_t*>(r);
    for (size_t i = 0; i < sz; ++i) {
        crc = static_cast<uint16_t>((crc << 8) ^ kTable[((crc >> 8) ^ p[i]) & 0xFFu]);
    }
    return static_cast<uint16_t>(~crc);
}

inline bool DecodeUastcBlock4x4(const uint8_t blk[16], uint8_t outRgba[64]) {
    uint32_t bitPos = 0;
    auto rdBits = [&](uint32_t n) -> uint32_t {
        uint32_t v = 0;
        for (uint32_t i = 0; i < n; ++i) {
            if (bitPos >= 128u) return 0u;
            uint32_t b = (blk[bitPos >> 3] >> (bitPos & 7u)) & 1u;
            v |= (b << i);
            ++bitPos;
        }
        return v;
    };

    const uint8_t b0 = static_cast<uint8_t>(blk[0] & 0x7Fu);
    int mode = -1;
    uint32_t modeLen = 0;
    if ((b0 & 0x03u) == 0x00u)      { mode = 11; modeLen = 2; }
    else if ((b0 & 0x07u) == 0x02u) { mode = 10; modeLen = 3; }
    else if ((b0 & 0x07u) == 0x06u) { mode = 12; modeLen = 3; }
    else if ((b0 & 0x0Fu) == 0x01u) { mode = 0;  modeLen = 4; }
    else if ((b0 & 0x0Fu) == 0x09u) { mode = 18; modeLen = 4; }
    else if ((b0 & 0x1Fu) == 0x1Du) { mode = 2;  modeLen = 5; }
    else if ((b0 & 0x1Fu) == 0x03u) { mode = 3;  modeLen = 5; }
    else if ((b0 & 0x1Fu) == 0x13u) { mode = 4;  modeLen = 5; }
    else if ((b0 & 0x1Fu) == 0x0Bu) { mode = 5;  modeLen = 5; }
    else if ((b0 & 0x1Fu) == 0x1Bu) { mode = 6;  modeLen = 5; }
    else if ((b0 & 0x1Fu) == 0x07u) { mode = 7;  modeLen = 5; }
    else if ((b0 & 0x1Fu) == 0x17u) { mode = 8;  modeLen = 5; }
    else if ((b0 & 0x1Fu) == 0x0Fu) { mode = 9;  modeLen = 5; }
    else if ((b0 & 0x1Fu) == 0x1Fu) { mode = 13; modeLen = 5; }
    else if ((b0 & 0x1Fu) == 0x0Du) { mode = 14; modeLen = 5; }
    else if ((b0 & 0x3Fu) == 0x35u) { mode = 1;  modeLen = 6; }
    else if ((b0 & 0x3Fu) == 0x15u) { mode = 16; modeLen = 6; }
    else if ((b0 & 0x3Fu) == 0x25u) { mode = 17; modeLen = 6; }
    else if (b0 == 0x05u)           { mode = 15; modeLen = 7; }
    else                            { return false; } // Mode 19 (0x45) reserved

    bitPos = modeLen;

    // Mode 8: Void-Extent Solid Color RGBA32
    if (mode == 8) {
        uint8_t r = static_cast<uint8_t>(rdBits(8));
        uint8_t g = static_cast<uint8_t>(rdBits(8));
        uint8_t b = static_cast<uint8_t>(rdBits(8));
        uint8_t a = static_cast<uint8_t>(rdBits(8));
        for (int i = 0; i < 16; ++i) {
            outRgba[i * 4 + 0] = r;
            outRgba[i * 4 + 1] = g;
            outRgba[i * 4 + 2] = b;
            outRgba[i * 4 + 3] = a;
        }
        return true;
    }

    static constexpr uint8_t kWtRange[19]  = { 8, 2, 5, 2, 2, 5, 2, 2, 0, 2, 8, 2, 5, 0, 2, 8, 2, 2, 11 };
    static constexpr uint8_t kEpRange[19]  = { 19, 20, 8, 7, 12, 20, 18, 12, 0, 8, 13, 13, 19, 20, 20, 20, 20, 20, 11 };
    static constexpr uint8_t kSubsets[19]  = { 1, 1, 2, 3, 2, 1, 1, 2, 0, 2, 1, 1, 1, 1, 1, 1, 2, 1, 1 };
    static constexpr uint8_t kPlanes[19]   = { 1, 1, 1, 1, 1, 1, 2, 1, 0, 1, 1, 2, 1, 2, 1, 1, 1, 2, 1 };
    static constexpr uint8_t kComps[19]    = { 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 2, 2, 2, 3 };
    static constexpr uint8_t kBc1H1[19]    = { 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1 };
    static constexpr uint8_t kEtcBias[19]  = { 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1 };
    static constexpr uint8_t kHasAlpha[19] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0 };

    // Skip transcoding acceleration hint fields (BC1H0/1, ETC1F/D/I0/I1/BIAS, ETC2TM)
    uint32_t hintBits = 1u + kBc1H1[mode] + 1u + 1u + 3u + 3u + 5u * kEtcBias[mode] + 8u * kHasAlpha[mode];
    bitPos += hintBits;

    const int subsets = kSubsets[mode];
    const int planes  = kPlanes[mode];
    const int comps   = kComps[mode];
    const int epRange = kEpRange[mode];
    const int wtRange = kWtRange[mode];

    int ccs = 0;
    if (planes == 2) {
        if (mode == 17) {
            ccs = 3; // LA dual-plane fixes second weight plane to Alpha
        } else {
            ccs = static_cast<int>(rdBits(2));
            if (comps == 3 && ccs >= 3) return false;
        }
    }

    uint8_t subsetOf[16] = {};
    int anchors[3] = { 0, -1, -1 };
    if (subsets > 1) {
        static constexpr uint16_t kSeeds2[30] = {
            28, 20, 16, 29, 91, 9, 107, 72, 149, 204, 50, 114, 496, 17, 78,
            39, 252, 828, 43, 156, 116, 210, 476, 273, 684, 359, 246, 195, 694, 524
        };
        static constexpr uint16_t kSeeds3[11] = {
            260, 74, 32, 156, 183, 15, 745, 0, 335, 902, 254
        };
        static constexpr uint16_t kSeeds7[19] = {
            36, 48, 61, 137, 161, 183, 226, 281, 302, 307, 479, 495, 593, 594, 605, 799, 812, 988, 993
        };

        uint32_t pat = rdBits(subsets == 3 ? 4u : 5u);
        int astcSeed = 0;
        if (mode == 3) {
            if (pat >= 11u) return false;
            astcSeed = kSeeds3[pat];
        } else if (mode == 7) {
            if (pat >= 19u) return false;
            astcSeed = kSeeds7[pat];
        } else {
            if (pat >= 30u) return false;
            astcSeed = kSeeds2[pat];
        }

        bool seen[3] = { false, false, false };
        for (int py = 0; py < 4; ++py) {
            for (int px = 0; px < 4; ++px) {
                int idx = py * 4 + px;
                int s = DetailAstc::SelectPartition(astcSeed, px, py, subsets, true);
                if (s < 0 || s >= subsets) return false;
                subsetOf[idx] = static_cast<uint8_t>(s);
                if (!seen[s]) {
                    seen[s] = true;
                    anchors[s] = idx;
                }
            }
        }
    }

    // Unpack UASTC BISE endpoints: ETQ (all trits/quints contiguous) followed by EBITS (all low bits contiguous)
    const int epValCount = subsets * comps * 2;
    const DetailAstc::BiseDesc& ed = DetailAstc::kBiseRanges[epRange];
    uint8_t tqVals[24] = {};
    if (ed.trits) {
        static constexpr uint8_t kTritBits[5] = { 2, 2, 1, 2, 1 };
        static constexpr uint8_t kTritShift[5] = { 0, 2, 4, 5, 7 };
        for (int base = 0; base < epValCount; base += 5) {
            int groupCnt = std::min(5, epValCount - base);
            uint32_t T = 0;
            for (int i = 0; i < groupCnt; ++i) {
                T |= (rdBits(kTritBits[i]) << kTritShift[i]);
            }
            uint32_t C = 0;
            uint8_t t[5] = {};
            if ((T & 0x1Cu) == 0x1Cu) {
                C = ((T >> 5) << 2) | (T & 3u);
                t[4] = t[3] = 2;
            } else {
                C = T & 0x1Fu;
                if ((T & 0x60u) == 0x60u) {
                    t[4] = 2;
                    t[3] = static_cast<uint8_t>((T >> 7) & 1u);
                } else {
                    t[4] = static_cast<uint8_t>((T >> 7) & 1u);
                    t[3] = static_cast<uint8_t>((T >> 5) & 3u);
                }
            }
            if ((C & 3u) == 3u) {
                t[2] = 2;
                t[1] = static_cast<uint8_t>((C >> 4) & 1u);
                uint32_t c3 = (C >> 3) & 1u;
                uint32_t c2 = (C >> 2) & 1u;
                t[0] = static_cast<uint8_t>((c3 << 1) | (c2 & (1u - c3)));
            } else if (((C >> 2) & 3u) == 3u) {
                t[2] = 2;
                t[1] = 2;
                t[0] = static_cast<uint8_t>(C & 3u);
            } else {
                t[2] = static_cast<uint8_t>((C >> 4) & 1u);
                t[1] = static_cast<uint8_t>((C >> 2) & 3u);
                t[0] = static_cast<uint8_t>(C & 3u);
            }
            for (int i = 0; i < groupCnt; ++i) tqVals[base + i] = t[i];
        }
    } else if (ed.quints) {
        static constexpr uint8_t kQuintBits[3] = { 3, 2, 2 };
        static constexpr uint8_t kQuintShift[3] = { 0, 3, 5 };
        for (int base = 0; base < epValCount; base += 3) {
            int groupCnt = std::min(3, epValCount - base);
            uint32_t Q = 0;
            for (int i = 0; i < groupCnt; ++i) {
                Q |= (rdBits(kQuintBits[i]) << kQuintShift[i]);
            }
            uint32_t C = 0;
            uint8_t q[3] = {};
            if (((Q >> 1) & 3u) == 3u && ((Q >> 5) & 3u) == 0u) {
                uint32_t q4 = (Q >> 4) & 1u;
                uint32_t q3 = (Q >> 3) & 1u;
                q[2] = static_cast<uint8_t>(((Q & 1u) << 2) | ((q4 & (1u - q3)) << 1) | (q3 & (1u - q4)));
                q[1] = q[0] = 4;
            } else {
                if (((Q >> 1) & 3u) == 3u) {
                    q[2] = 4;
                    C = (((Q >> 3) & 3u) << 3) | ((~(Q >> 5) & 3u) << 1) | (Q & 1u);
                } else {
                    q[2] = static_cast<uint8_t>((Q >> 5) & 3u);
                    C = Q & 0x1Fu;
                }
                if ((C & 7u) == 5u) {
                    q[1] = 4;
                    q[0] = static_cast<uint8_t>((C >> 3) & 3u);
                } else {
                    q[1] = static_cast<uint8_t>((C >> 3) & 3u);
                    q[0] = static_cast<uint8_t>(C & 7u);
                }
            }
            for (int i = 0; i < groupCnt; ++i) tqVals[base + i] = q[i];
        }
    }

    uint8_t epUnquant[24] = {};
    for (int i = 0; i < epValCount; ++i) {
        uint32_t mBits = ed.bits ? rdBits(ed.bits) : 0u;
        uint8_t qv = static_cast<uint8_t>((static_cast<uint32_t>(tqVals[i]) << ed.bits) | mBits);
        epUnquant[i] = DetailAstc::UnquantizeEndpoint(epRange, qv);
    }

    uint8_t e0[3][4] = {}, e1[3][4] = {};
    for (int s = 0; s < subsets; ++s) {
        const uint8_t* sv = epUnquant + s * comps * 2;
        if (comps == 2) { // LA Direct
            e0[s][0] = e0[s][1] = e0[s][2] = sv[0];
            e1[s][0] = e1[s][1] = e1[s][2] = sv[1];
            e0[s][3] = sv[2];
            e1[s][3] = sv[3];
        } else if (comps == 3) { // RGB Direct
            e0[s][0] = sv[0]; e1[s][0] = sv[1];
            e0[s][1] = sv[2]; e1[s][1] = sv[3];
            e0[s][2] = sv[4]; e1[s][2] = sv[5];
            e0[s][3] = 255;   e1[s][3] = 255;
        } else { // RGBA Direct
            e0[s][0] = sv[0]; e1[s][0] = sv[1];
            e0[s][1] = sv[2]; e1[s][1] = sv[3];
            e0[s][2] = sv[4]; e1[s][2] = sv[5];
            e0[s][3] = sv[6]; e1[s][3] = sv[7];
        }
    }

    // Unpack LSB-first weights (anchors omit MSB)
    const uint32_t wtBits = DetailAstc::kBiseRanges[wtRange].bits;
    uint8_t wPlane0[16] = {}, wPlane1[16] = {};
    if (planes == 1) {
        for (int i = 0; i < 16; ++i) {
            bool isAnc = (i == anchors[0] || i == anchors[1] || i == anchors[2]);
            uint32_t nb = isAnc ? (wtBits - 1u) : wtBits;
            uint8_t qIdx = static_cast<uint8_t>(nb ? rdBits(nb) : 0u);
            wPlane0[i] = DetailAstc::UnquantizeWeight(wtRange, qIdx);
        }
    } else {
        for (int i = 0; i < 16; ++i) {
            uint32_t nb = (i == 0) ? (wtBits - 1u) : wtBits;
            uint8_t q0 = static_cast<uint8_t>(nb ? rdBits(nb) : 0u);
            uint8_t q1 = static_cast<uint8_t>(nb ? rdBits(nb) : 0u);
            wPlane0[i] = DetailAstc::UnquantizeWeight(wtRange, q0);
            wPlane1[i] = DetailAstc::UnquantizeWeight(wtRange, q1);
        }
    }

    if (bitPos > 128u) return false;

    for (int i = 0; i < 16; ++i) {
        int s = subsetOf[i];
        for (int c = 0; c < 4; ++c) {
            uint32_t w = (planes == 2 && c == ccs) ? wPlane1[i] : wPlane0[i];
            uint32_t c0 = e0[s][c];
            uint32_t c1 = e1[s][c];
            outRgba[i * 4 + c] = static_cast<uint8_t>(((64u - w) * c0 + w * c1 + 32u) >> 6);
        }
    }
    return true;
}

struct BasisBitReader {
    const uint8_t* data = nullptr;
    size_t totalBits = 0;
    size_t bitPos = 0;
    bool eof = false;

    BasisBitReader(const uint8_t* p, size_t byteLen) : data(p), totalBits(byteLen * 8u) {}

    uint32_t GetBits(uint32_t n) {
        if (n == 0) return 0u;
        if (bitPos + n > totalBits) {
            eof = true;
            bitPos = totalBits;
            return 0u;
        }
        uint32_t v = 0;
        for (uint32_t i = 0; i < n; ++i) {
            size_t bp = bitPos + i;
            v |= (static_cast<uint32_t>((data[bp >> 3] >> (bp & 7u)) & 1u) << i);
        }
        bitPos += n;
        return v;
    }

    uint32_t Peek16() const {
        uint32_t v = 0;
        size_t avail = (bitPos < totalBits) ? std::min<size_t>(16u, totalBits - bitPos) : 0u;
        for (size_t i = 0; i < avail; ++i) {
            size_t bp = bitPos + i;
            v |= (static_cast<uint32_t>((data[bp >> 3] >> (bp & 7u)) & 1u) << i);
        }
        return v;
    }

    bool Advance(uint32_t n) {
        if (bitPos + n > totalBits) {
            eof = true;
            return false;
        }
        bitPos += n;
        return true;
    }

    uint32_t DecodeVlc(uint32_t chunkBits) {
        const uint32_t chunkSize = 1u << chunkBits;
        const uint32_t chunkMask = chunkSize - 1u;
        uint32_t v = 0;
        uint32_t ofs = 0;
        for (;;) {
            uint32_t s = GetBits(chunkBits + 1u);
            if (eof) return 0u;
            v |= ((s & chunkMask) << ofs);
            ofs += chunkBits;
            if ((s & chunkSize) == 0u) break;
            if (ofs >= 32u) {
                eof = true;
                return 0u;
            }
        }
        return v;
    }
};

struct BasisHuffTable {
    static constexpr uint32_t kFastBits = 10;
    static constexpr uint32_t kFastSize = 1u << kFastBits;
    // Packed (len << 16) | sym; len == 0 means invalid or slow path
    std::array<uint32_t, kFastSize> fast{};
    struct LongSym { uint16_t codeRev; uint16_t mask; uint16_t sym; uint8_t len; };
    std::vector<LongSym> slow;
    bool valid = false;

    static uint16_t BitRev16(uint16_t v, uint32_t len) {
        uint16_t r = 0;
        for (uint32_t i = 0; i < len; ++i) {
            r = static_cast<uint16_t>((r << 1) | ((v >> i) & 1u));
        }
        return r;
    }

    bool BuildFromLengths(const uint8_t* codeSizes, uint32_t numSyms, uint32_t maxCodeLen = 16u) {
        fast.fill(0u);
        slow.clear();
        valid = false;
        if (numSyms == 0) return false;

        uint32_t blCount[17] = {};
        uint32_t usedCount = 0;
        for (uint32_t i = 0; i < numSyms; ++i) {
            uint8_t cl = codeSizes[i];
            if (cl > maxCodeLen || cl > 16u) return false;
            if (cl > 0) {
                ++blCount[cl];
                ++usedCount;
            }
        }
        if (usedCount == 0) return false;

        uint32_t nextCode[17] = {};
        uint32_t code = 0;
        for (uint32_t bits = 1; bits <= 16; ++bits) {
            code = (code + blCount[bits - 1]) << 1;
            if (code + blCount[bits] > (1u << bits)) return false; // Over-subscribed tree
            nextCode[bits] = code;
        }

        for (uint32_t sym = 0; sym < numSyms; ++sym) {
            uint8_t len = codeSizes[sym];
            if (len == 0) continue;
            uint16_t cw = static_cast<uint16_t>(nextCode[len]++);
            uint16_t rev = BitRev16(cw, len);
            if (len <= kFastBits) {
                uint32_t entry = (static_cast<uint32_t>(len) << 16) | sym;
                uint32_t step = 1u << len;
                for (uint32_t idx = rev; idx < kFastSize; idx += step) {
                    fast[idx] = entry;
                }
            } else {
                slow.push_back({ rev, static_cast<uint16_t>((1u << len) - 1u), static_cast<uint16_t>(sym), len });
            }
        }
        valid = true;
        return true;
    }

    bool Decode(BasisBitReader& br, uint32_t& outSym) const {
        if (!valid || br.eof) return false;
        uint32_t peek = br.Peek16();
        uint32_t f = fast[peek & (kFastSize - 1u)];
        if (f != 0u) {
            uint32_t len = f >> 16;
            if (!br.Advance(len)) return false;
            outSym = f & 0xFFFFu;
            return true;
        }
        for (const auto& ls : slow) {
            if ((peek & ls.mask) == ls.codeRev) {
                if (!br.Advance(ls.len)) return false;
                outSym = ls.sym;
                return true;
            }
        }
        return false;
    }
};

inline bool ReadBasisHuffTable(BasisBitReader& br, BasisHuffTable& outTable, bool allowEmpty = false) {
    outTable = BasisHuffTable{};
    uint32_t totalUsedSyms = br.GetBits(14);
    if (br.eof) return false;
    if (totalUsedSyms == 0) return allowEmpty;
    if (totalUsedSyms > 8192u) return false;

    uint32_t numClCodes = br.GetBits(5);
    if (br.eof || numClCodes == 0 || numClCodes > 21u) return false;

    static constexpr uint8_t kClOrder[21] = {
        17, 18, 19, 20, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15, 16
    };
    uint8_t clSizes[21] = {};
    for (uint32_t i = 0; i < numClCodes; ++i) {
        clSizes[kClOrder[i]] = static_cast<uint8_t>(br.GetBits(3));
    }
    if (br.eof) return false;

    BasisHuffTable clTable;
    if (!clTable.BuildFromLengths(clSizes, 21u, 7u)) return false;

    std::vector<uint8_t> codeSizes(totalUsedSyms, 0);
    uint32_t cur = 0;
    uint8_t prevCodeSize = 0;
    while (cur < totalUsedSyms) {
        uint32_t sym = 0;
        if (!clTable.Decode(br, sym)) return false;
        if (sym <= 16u) {
            codeSizes[cur++] = static_cast<uint8_t>(sym);
            if (sym != 0u) prevCodeSize = static_cast<uint8_t>(sym);
        } else if (sym == 17u) {
            uint32_t run = 3u + br.GetBits(3);
            if (br.eof || cur + run > totalUsedSyms) return false;
            cur += run;
        } else if (sym == 18u) {
            uint32_t run = 11u + br.GetBits(7);
            if (br.eof || cur + run > totalUsedSyms) return false;
            cur += run;
        } else if (sym == 19u || sym == 20u) {
            if (prevCodeSize == 0u) return false;
            uint32_t run = (sym == 19u) ? (3u + br.GetBits(2)) : (7u + br.GetBits(7));
            if (br.eof || cur + run > totalUsedSyms) return false;
            for (uint32_t k = 0; k < run; ++k) codeSizes[cur++] = prevCodeSize;
        } else {
            return false;
        }
    }

    return outTable.BuildFromLengths(codeSizes.data(), totalUsedSyms, 16u);
}

struct Etc1sEndpoint {
    uint8_t color5[3];
    uint8_t inten;
};

struct Etc1sSelector {
    uint8_t bytes[4]; // 4 rows, 2 bits per column (col 0 at bits 0..1)
};

struct ApproxMoveToFront {
    std::vector<uint32_t> values;
    uint32_t rover = 0;

    void Init(uint32_t n) {
        values.assign(n, 0u);
        rover = n / 2u;
    }

    void Add(uint32_t newVal) {
        if (values.empty()) return;
        values[rover++] = newVal;
        if (rover == values.size()) rover = static_cast<uint32_t>(values.size() / 2u);
    }

    void Use(uint32_t idx) {
        if (idx > 0 && idx < values.size()) {
            std::swap(values[idx / 2u], values[idx]);
        }
    }
};

inline bool DecodeEtc1sEndpoints(
    const uint8_t* data, size_t size, uint32_t numEndpoints, std::vector<Etc1sEndpoint>& outEndpoints)
{
    if (!data || size == 0 || numEndpoints == 0 || numEndpoints > 65536u) return false;
    BasisBitReader br(data, size);
    BasisHuffTable colorDelta0, colorDelta1, colorDelta2, intenDelta;
    if (!ReadBasisHuffTable(br, colorDelta0, true) ||
        !ReadBasisHuffTable(br, colorDelta1, true) ||
        !ReadBasisHuffTable(br, colorDelta2, true) ||
        !ReadBasisHuffTable(br, intenDelta, false)) {
        return false;
    }
    bool grayscale = br.GetBits(1) != 0u;
    if (br.eof) return false;

    outEndpoints.resize(numEndpoints);
    uint32_t prevColor5[3] = { 16u, 16u, 16u };
    uint32_t prevInten = 0u;

    for (uint32_t i = 0; i < numEndpoints; ++i) {
        uint32_t dInten = 0;
        if (!intenDelta.Decode(br, dInten)) return false;
        uint32_t inten = (prevInten + dInten) & 7u;
        prevInten = inten;

        const uint32_t chLimit = grayscale ? 1u : 3u;
        for (uint32_t c = 0; c < chLimit; ++c) {
            const BasisHuffTable* mdl = (prevColor5[c] <= 9u) ? &colorDelta0 :
                                        ((prevColor5[c] <= 21u) ? &colorDelta1 : &colorDelta2);
            uint32_t dCol = 0;
            if (!mdl->Decode(br, dCol)) return false;
            prevColor5[c] = (prevColor5[c] + dCol) & 31u;
        }
        if (grayscale) {
            prevColor5[1] = prevColor5[0];
            prevColor5[2] = prevColor5[0];
        }
        outEndpoints[i].color5[0] = static_cast<uint8_t>(prevColor5[0]);
        outEndpoints[i].color5[1] = static_cast<uint8_t>(prevColor5[1]);
        outEndpoints[i].color5[2] = static_cast<uint8_t>(prevColor5[2]);
        outEndpoints[i].inten = static_cast<uint8_t>(inten);
    }
    return true;
}

inline bool DecodeEtc1sSelectors(
    const uint8_t* data, size_t size, uint32_t numSelectors, std::vector<Etc1sSelector>& outSelectors)
{
    if (!data || size == 0 || numSelectors == 0 || numSelectors > 65536u) return false;
    BasisBitReader br(data, size);
    bool globalPal = br.GetBits(1) != 0u;
    bool hybridPal = br.GetBits(1) != 0u;
    bool rawPal    = br.GetBits(1) != 0u;
    if (br.eof || globalPal || hybridPal) return false;

    outSelectors.resize(numSelectors);
    if (rawPal) {
        for (uint32_t i = 0; i < numSelectors; ++i) {
            for (uint32_t j = 0; j < 4u; ++j) {
                outSelectors[i].bytes[j] = static_cast<uint8_t>(br.GetBits(8));
            }
            if (br.eof) return false;
        }
        return true;
    }

    BasisHuffTable deltaSelModel;
    if (numSelectors > 1u && !ReadBasisHuffTable(br, deltaSelModel, false)) {
        return false;
    }

    uint8_t prevBytes[4] = { 0, 0, 0, 0 };
    for (uint32_t i = 0; i < numSelectors; ++i) {
        if (i == 0) {
            for (uint32_t j = 0; j < 4u; ++j) {
                prevBytes[j] = static_cast<uint8_t>(br.GetBits(8));
                outSelectors[i].bytes[j] = prevBytes[j];
            }
            if (br.eof) return false;
        } else {
            for (uint32_t j = 0; j < 4u; ++j) {
                uint32_t dByte = 0;
                if (!deltaSelModel.Decode(br, dByte) || dByte > 255u) return false;
                prevBytes[j] = static_cast<uint8_t>(prevBytes[j] ^ static_cast<uint8_t>(dByte));
                outSelectors[i].bytes[j] = prevBytes[j];
            }
        }
    }
    return true;
}

struct Etc1sTables {
    BasisHuffTable endpointPred;
    BasisHuffTable deltaEndpoint;
    BasisHuffTable selector;
    BasisHuffTable selectorHistoryRle;
    uint32_t selectorHistoryBufSize = 0;
};

inline bool DecodeEtc1sTables(const uint8_t* data, size_t size, Etc1sTables& outTables) {
    if (!data || size == 0) return false;
    BasisBitReader br(data, size);
    if (!ReadBasisHuffTable(br, outTables.endpointPred, false) ||
        !ReadBasisHuffTable(br, outTables.deltaEndpoint, true) ||
        !ReadBasisHuffTable(br, outTables.selector, false) ||
        !ReadBasisHuffTable(br, outTables.selectorHistoryRle, true)) {
        return false;
    }
    outTables.selectorHistoryBufSize = br.GetBits(13);
    return !br.eof;
}

inline bool DecodeEtc1sSliceToRgba(
    const uint8_t* sliceData, size_t sliceSize,
    uint32_t blocksX, uint32_t blocksY,
    uint32_t origW, uint32_t origH,
    bool isVideo,
    const std::vector<Etc1sEndpoint>& endpoints,
    const std::vector<Etc1sSelector>& selectors,
    const Etc1sTables& tables,
    std::vector<uint8_t>& outRgba)
{
    if (!sliceData || sliceSize == 0 || endpoints.empty() || selectors.empty()) return false;
    BasisBitReader br(sliceData, sliceSize);

    const uint32_t numEndpoints = static_cast<uint32_t>(endpoints.size());
    const uint32_t numSelectors = static_cast<uint32_t>(selectors.size());
    const uint32_t histSize = tables.selectorHistoryBufSize;
    const uint32_t rleSymIndex = numSelectors + histSize;

    ApproxMoveToFront selHist;
    selHist.Init(histSize);

    struct BlockPredPair { uint8_t pred[2]; }; // [0] = current even row, [1] = next odd row
    std::vector<BlockPredPair> rowPreds(blocksX + 2u);
    std::vector<uint32_t> upperEndpoints(blocksX, 0u);
    std::vector<uint32_t> curRowEndpoints(blocksX, 0u);

    uint32_t predRepeatCount = 0;
    uint32_t prevPredSym = 0;
    uint32_t prevEndpointIdx = 0;
    uint32_t curSelectorRleCount = 0;

    static constexpr int kEtc1sMod[8][4] = {
        { -8,   -2,   2,   8 },
        { -17,  -5,   5,  17 },
        { -29,  -9,   9,  29 },
        { -42, -13,  13,  42 },
        { -60, -18,  18,  60 },
        { -80, -24,  24,  80 },
        { -106,-33,  33, 106 },
        { -183,-47,  47, 183 }
    };

    outRgba.assign(static_cast<size_t>(origW) * origH * 4u, 255u);

    for (uint32_t by = 0; by < blocksY; ++by) {
        uint32_t leftEndpointIdx = 0;
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
            if ((by & 1u) == 0u && (bx & 1u) == 0u) {
                uint32_t predBits = 0;
                if (predRepeatCount > 0u) {
                    --predRepeatCount;
                    predBits = prevPredSym;
                } else {
                    if (!tables.endpointPred.Decode(br, predBits)) return false;
                    if (predBits == 256u) {
                        predRepeatCount = br.DecodeVlc(4u) + 3u - 1u;
                        if (br.eof) return false;
                        predBits = prevPredSym;
                    } else if (predBits > 256u) {
                        return false;
                    } else {
                        prevPredSym = predBits;
                    }
                }
                rowPreds[bx].pred[0]      = static_cast<uint8_t>(predBits & 3u);
                rowPreds[bx + 1u].pred[0] = static_cast<uint8_t>((predBits >> 2) & 3u);
                rowPreds[bx].pred[1]      = static_cast<uint8_t>((predBits >> 4) & 3u);
                rowPreds[bx + 1u].pred[1] = static_cast<uint8_t>((predBits >> 6) & 3u);
            }

            const uint8_t pred = rowPreds[bx].pred[by & 1u];
            uint32_t epIdx = 0;
            bool isCrBlock = false;
            if (pred == 0u) {
                epIdx = (bx > 0u) ? leftEndpointIdx : 0u;
            } else if (pred == 1u) {
                epIdx = (by > 0u) ? upperEndpoints[bx] : 0u;
            } else if (pred == 2u) {
                if (isVideo) {
                    epIdx = 0u;
                    isCrBlock = true;
                } else {
                    epIdx = (bx > 0u && by > 0u) ? upperEndpoints[bx - 1u] : 0u;
                }
            } else {
                uint32_t deltaSym = 0;
                if (!tables.deltaEndpoint.Decode(br, deltaSym)) return false;
                epIdx = (deltaSym + prevEndpointIdx) % numEndpoints;
            }
            prevEndpointIdx = epIdx;
            leftEndpointIdx = epIdx;
            curRowEndpoints[bx] = epIdx;

            uint32_t selIdx = 0;
            if (!isCrBlock) {
                if (curSelectorRleCount > 0u) {
                    --curSelectorRleCount;
                    if (histSize == 0u) return false;
                    selIdx = selHist.values[0];
                } else {
                    uint32_t selSym = 0;
                    if (!tables.selector.Decode(br, selSym)) return false;
                    if (selSym == rleSymIndex) {
                        if (histSize == 0u) return false;
                        uint32_t runSym = 0;
                        if (!tables.selectorHistoryRle.Decode(br, runSym)) return false;
                        uint32_t runLen = (runSym == 63u) ? (br.DecodeVlc(7u) + 64u) : (runSym + 1u);
                        if (br.eof || runLen == 0u) return false;
                        selIdx = selHist.values[0];
                        curSelectorRleCount = runLen - 1u;
                    } else if (selSym < numSelectors) {
                        selIdx = selSym;
                        if (histSize > 0u) selHist.Add(selIdx);
                    } else if (selSym < rleSymIndex) {
                        uint32_t hIdx = selSym - numSelectors;
                        selIdx = selHist.values[hIdx];
                        selHist.Use(hIdx);
                    } else {
                        return false;
                    }
                }
            }

            if (epIdx >= numEndpoints || selIdx >= numSelectors) return false;
            const Etc1sEndpoint& ep = endpoints[epIdx];
            const Etc1sSelector& sel = selectors[selIdx];

            int baseR = (static_cast<int>(ep.color5[0]) << 3) | (static_cast<int>(ep.color5[0]) >> 2);
            int baseG = (static_cast<int>(ep.color5[1]) << 3) | (static_cast<int>(ep.color5[1]) >> 2);
            int baseB = (static_cast<int>(ep.color5[2]) << 3) | (static_cast<int>(ep.color5[2]) >> 2);
            const int* modRow = kEtc1sMod[ep.inten & 7u];

            for (uint32_t py = 0; py < 4u; ++py) {
                uint32_t dy = by * 4u + py;
                if (dy >= origH) continue;
                uint8_t sByte = sel.bytes[py];
                for (uint32_t px = 0; px < 4u; ++px) {
                    uint32_t dx = bx * 4u + px;
                    if (dx >= origW) continue;
                    int m = modRow[(sByte >> (px * 2u)) & 3u];
                    size_t dst = (static_cast<size_t>(dy) * origW + dx) * 4u;
                    outRgba[dst + 0] = static_cast<uint8_t>(std::clamp(baseR + m, 0, 255));
                    outRgba[dst + 1] = static_cast<uint8_t>(std::clamp(baseG + m, 0, 255));
                    outRgba[dst + 2] = static_cast<uint8_t>(std::clamp(baseB + m, 0, 255));
                    outRgba[dst + 3] = 255u;
                }
            }
        }
        upperEndpoints.swap(curRowEndpoints);
    }
    return true;
}

} // namespace DetailBasis

inline bool DecodeKTX2OrBasis(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    static const uint8_t kKtx2Sig[12] = { 0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
    bool isKtx2 = (size >= 104 && std::memcmp(data, kKtx2Sig, 12) == 0);
    bool isBasis = (size >= 100 && data[0] == 's' && data[1] == 'B');
    if (!isKtx2 && !isBasis) {
        return false;
    }

    if (isKtx2) {
        uint32_t vkFormat = DetailImg100::RdU32LE(data + 12);
        uint32_t w = DetailImg100::RdU32LE(data + 20);
        uint32_t h = DetailImg100::RdU32LE(data + 24);
        if (h == 0) h = 1; // 1D KTX2 texture
        uint32_t d = std::max(1u, DetailImg100::RdU32LE(data + 28));
        uint32_t layers = std::max(1u, DetailImg100::RdU32LE(data + 32));
        uint32_t faces = std::max(1u, DetailImg100::RdU32LE(data + 36));
        uint32_t levelCount = std::max(1u, DetailImg100::RdU32LE(data + 40));
        uint32_t supercomp = DetailImg100::RdU32LE(data + 44);
        uint32_t dfdByteOff = DetailImg100::RdU32LE(data + 48);
        uint32_t dfdByteLen = DetailImg100::RdU32LE(data + 52);
        uint64_t sgdByteOff = DetailImg100::RdU64LE(data + 64);
        uint64_t sgdByteLen = DetailImg100::RdU64LE(data + 72);
        if (w == 0 || w > 8192 || h > 8192 || levelCount > 32) return false;

        size_t levelIndexEnd = 80ull + static_cast<size_t>(levelCount) * 24ull;
        if (levelIndexEnd > size) return false;

        // KTX 2.0 Section 3.9.7: levels[0] at byte offset 80 is mip level 0 (largest resolution)
        uint64_t byteOff = DetailImg100::RdU64LE(data + 80);
        uint64_t byteLen = DetailImg100::RdU64LE(data + 88);
        uint64_t uncompLen = DetailImg100::RdU64LE(data + 96);
        if (byteLen == 0 || byteOff < levelIndexEnd || byteOff > size || byteLen > size - byteOff) {
            return false;
        }

        if (supercomp == 1) { // KTX_SS_BASIS_LZ (ETC1S supercompression, KTX 2.0 Section 3.10.2)
            if (vkFormat != 0) return false;
            if (dfdByteLen >= 28 && dfdByteOff <= size && dfdByteLen <= size - dfdByteOff) {
                uint8_t colorModel = data[dfdByteOff + 12];
                if (colorModel == 0u) colorModel = data[dfdByteOff + 16];
                if (colorModel != 163u) return false; // KHR_DF_MODEL_ETC1S == 163
            }
            uint64_t totalImgCount = 0;
            for (uint32_t lv = 0; lv < levelCount; ++lv) {
                uint32_t lvDepth = std::max(1u, d >> lv);
                totalImgCount += static_cast<uint64_t>(layers) * faces * lvDepth;
            }
            if (totalImgCount == 0 || totalImgCount > 65536ull) return false;
            uint64_t descsEnd = 20ull + totalImgCount * 20ull;
            if (sgdByteOff < levelIndexEnd || sgdByteOff > size || sgdByteLen > size - sgdByteOff || sgdByteLen < descsEnd) {
                return false;
            }
            const uint8_t* sgd = data + static_cast<size_t>(sgdByteOff);
            uint16_t endpointCount    = DetailImg100::RdU16LE(sgd + 0);
            uint16_t selectorCount    = DetailImg100::RdU16LE(sgd + 2);
            uint32_t endpointsByteLen = DetailImg100::RdU32LE(sgd + 4);
            uint32_t selectorsByteLen = DetailImg100::RdU32LE(sgd + 8);
            uint32_t tablesByteLen    = DetailImg100::RdU32LE(sgd + 12);
            uint32_t extendedByteLen  = DetailImg100::RdU32LE(sgd + 16);
            if (endpointCount == 0 || selectorCount == 0 ||
                endpointsByteLen == 0 || selectorsByteLen == 0 || tablesByteLen == 0) {
                return false;
            }
            uint64_t sgdRequired = descsEnd + endpointsByteLen + selectorsByteLen + tablesByteLen + extendedByteLen;
            if (sgdRequired > sgdByteLen) return false;

            // Mip 0, layer 0, face 0, slice 0 descriptor at sgd + 20
            uint32_t imageFlags    = DetailImg100::RdU32LE(sgd + 20);
            uint32_t rgbSliceOff   = DetailImg100::RdU32LE(sgd + 24);
            uint32_t rgbSliceLen   = DetailImg100::RdU32LE(sgd + 28);
            uint32_t alphaSliceOff = DetailImg100::RdU32LE(sgd + 32);
            uint32_t alphaSliceLen = DetailImg100::RdU32LE(sgd + 36);
            if (rgbSliceLen == 0 || rgbSliceOff > byteLen || rgbSliceLen > byteLen - rgbSliceOff) {
                return false;
            }

            const uint8_t* epPtr  = sgd + static_cast<size_t>(descsEnd);
            const uint8_t* selPtr = epPtr + endpointsByteLen;
            const uint8_t* tblPtr = selPtr + selectorsByteLen;

            std::vector<DetailBasis::Etc1sEndpoint> endpoints;
            std::vector<DetailBasis::Etc1sSelector> selectors;
            DetailBasis::Etc1sTables tables;
            if (!DetailBasis::DecodeEtc1sEndpoints(epPtr, endpointsByteLen, endpointCount, endpoints) ||
                !DetailBasis::DecodeEtc1sSelectors(selPtr, selectorsByteLen, selectorCount, selectors) ||
                !DetailBasis::DecodeEtc1sTables(tblPtr, tablesByteLen, tables)) {
                return false;
            }

            uint32_t bw4 = (w + 3u) / 4u;
            uint32_t bh4 = (h + 3u) / 4u;
            bool isVideo = (imageFlags & 0x01u) != 0u;
            const uint8_t* mipBase = data + static_cast<size_t>(byteOff);
            std::vector<uint8_t> rgbRgba;
            if (!DetailBasis::DecodeEtc1sSliceToRgba(
                    mipBase + rgbSliceOff, rgbSliceLen, bw4, bh4, w, h,
                    isVideo, endpoints, selectors, tables, rgbRgba)) {
                return false;
            }

            bool hasAlpha = false;
            if (alphaSliceLen > 0) {
                if (alphaSliceOff > byteLen || alphaSliceLen > byteLen - alphaSliceOff) return false;
                std::vector<uint8_t> alphaRgba;
                if (!DetailBasis::DecodeEtc1sSliceToRgba(
                        mipBase + alphaSliceOff, alphaSliceLen, bw4, bh4, w, h,
                        isVideo, endpoints, selectors, tables, alphaRgba)) {
                    return false;
                }
                size_t pxTotal = static_cast<size_t>(w) * h;
                for (size_t i = 0; i < pxTotal; ++i) {
                    rgbRgba[i * 4u + 3] = alphaRgba[i * 4u + 1];
                }
                hasAlpha = true;
            }

            size_t pxTotal = static_cast<size_t>(w) * h;
            outInfo.bgraPixels.resize(pxTotal * 4u);
            for (size_t i = 0; i < pxTotal; ++i) {
                WritePremulBGRA(
                    &outInfo.bgraPixels[i * 4u],
                    rgbRgba[i * 4u + 0],
                    rgbRgba[i * 4u + 1],
                    rgbRgba[i * 4u + 2],
                    rgbRgba[i * 4u + 3]);
            }
            outInfo.width = w;
            outInfo.height = h;
            outInfo.origWidth = w;
            outInfo.origHeight = h;
            outInfo.channels = hasAlpha ? 4u : 3u;
            outInfo.bitsPerChannel = 8;
            outInfo.formatName = L"Khronos KTX 2.0 BasisLZ ETC1S (.ktx2)";
            outInfo.valid = true;
            return true;
        }

        std::vector<uint8_t> inflatedMip;
        const uint8_t* mipPtr = nullptr;
        size_t mipLen = 0;
        if (supercomp == 0) {
            mipPtr = data + static_cast<size_t>(byteOff);
            mipLen = static_cast<size_t>(byteLen);
        } else if (supercomp == 2) { // KTX_SS_ZSTD (RFC 8878 Zstandard supercompression)
            if (uncompLen == 0 || uncompLen > 256ULL * 1024 * 1024) return false;
            if (!StreamCodecs::DecompressZstdFrame(data + static_cast<size_t>(byteOff), static_cast<size_t>(byteLen),
                                                   inflatedMip, static_cast<size_t>(uncompLen) + 64) ||
                inflatedMip.size() < static_cast<size_t>(uncompLen)) {
                return false;
            }
            mipPtr = inflatedMip.data();
            mipLen = static_cast<size_t>(uncompLen);
        } else if (supercomp == 3) { // KTX_SS_ZLIB
            if (uncompLen == 0 || uncompLen > 256ULL * 1024 * 1024) return false;
            if (!ZipDeflate::InflateZlib(data + static_cast<size_t>(byteOff), static_cast<size_t>(byteLen),
                                         inflatedMip, static_cast<size_t>(uncompLen) + 64) ||
                inflatedMip.size() < static_cast<size_t>(uncompLen)) {
                return false;
            }
            mipPtr = inflatedMip.data();
            mipLen = static_cast<size_t>(uncompLen);
        } else {
            return false;
        }

        uint64_t numFacesSlices = static_cast<uint64_t>(layers) * faces * d;
        if (numFacesSlices > 1 && mipLen >= numFacesSlices) {
            // Decode first layer/face/slice of mip0
            mipLen = static_cast<size_t>(mipLen / numFacesSlices);
        }

        size_t pxCount = static_cast<size_t>(w) * h;
        uint32_t bw4 = (w + 3u) / 4u;
        uint32_t bh4 = (h + 3u) / 4u;
        size_t blocks4x4 = static_cast<size_t>(bw4) * bh4;
        std::wstring fmtDesc = L"Khronos KTX 2.0 (.ktx2)";
        uint32_t outCh = 4;
        uint32_t outBits = 8;

        if (vkFormat == 0) { // VK_FORMAT_UNDEFINED -> check DFD colorModel == 166 (KHR_DF_MODEL_UASTC)
            if (dfdByteLen < 28 || dfdByteOff > size || dfdByteLen > size - dfdByteOff) return false;
            uint8_t colorModel = data[dfdByteOff + 12];
            if (colorModel == 0u) colorModel = data[dfdByteOff + 16];
            if (colorModel != 166u || mipLen < blocks4x4 * 16u) return false;
            outInfo.bgraPixels.assign(pxCount * 4u, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    if (!DetailBasis::DecodeUastcBlock4x4(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 16u, blk)) {
                        return false;
                    }
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 UASTC 4x4 (.ktx2)";
        } else if (vkFormat >= 9 && vkFormat <= 15) { // R8
            if (mipLen < pxCount) return false;
            outCh = 1;
            outInfo.bgraPixels.resize(pxCount * 4);
            bool snorm = (vkFormat == 10 || vkFormat == 12 || vkFormat == 14);
            for (size_t i = 0; i < pxCount; ++i) {
                uint8_t v = snorm ? static_cast<uint8_t>(static_cast<int>(static_cast<int8_t>(mipPtr[i])) + 128) : mipPtr[i];
                WritePremulBGRA(&outInfo.bgraPixels[i * 4], v, v, v, 255);
            }
            fmtDesc = L"Khronos KTX 2.0 R8 (.ktx2)";
        } else if (vkFormat >= 16 && vkFormat <= 22) { // RG8
            if (mipLen < pxCount * 2) return false;
            outCh = 2;
            outInfo.bgraPixels.resize(pxCount * 4);
            bool snorm = (vkFormat == 17 || vkFormat == 19 || vkFormat == 21);
            for (size_t i = 0; i < pxCount; ++i) {
                uint8_t r = snorm ? static_cast<uint8_t>(static_cast<int>(static_cast<int8_t>(mipPtr[i * 2 + 0])) + 128) : mipPtr[i * 2 + 0];
                uint8_t g = snorm ? static_cast<uint8_t>(static_cast<int>(static_cast<int8_t>(mipPtr[i * 2 + 1])) + 128) : mipPtr[i * 2 + 1];
                WritePremulBGRA(&outInfo.bgraPixels[i * 4], r, g, 0, 255);
            }
            fmtDesc = L"Khronos KTX 2.0 RG8 (.ktx2)";
        } else if (vkFormat >= 23 && vkFormat <= 29) { // RGB8
            if (mipLen < pxCount * 3) return false;
            outCh = 3;
            outInfo.bgraPixels.resize(pxCount * 4);
            for (size_t i = 0; i < pxCount; ++i) {
                WritePremulBGRA(&outInfo.bgraPixels[i * 4], mipPtr[i * 3 + 0], mipPtr[i * 3 + 1], mipPtr[i * 3 + 2], 255);
            }
            fmtDesc = L"Khronos KTX 2.0 RGB8 (.ktx2)";
        } else if (vkFormat >= 30 && vkFormat <= 36) { // BGR8
            if (mipLen < pxCount * 3) return false;
            outCh = 3;
            outInfo.bgraPixels.resize(pxCount * 4);
            for (size_t i = 0; i < pxCount; ++i) {
                WritePremulBGRA(&outInfo.bgraPixels[i * 4], mipPtr[i * 3 + 2], mipPtr[i * 3 + 1], mipPtr[i * 3 + 0], 255);
            }
            fmtDesc = L"Khronos KTX 2.0 BGR8 (.ktx2)";
        } else if (vkFormat >= 37 && vkFormat <= 43) { // RGBA8
            if (mipLen < pxCount * 4) return false;
            outInfo.bgraPixels.resize(pxCount * 4);
            for (size_t i = 0; i < pxCount; ++i) {
                WritePremulBGRA(&outInfo.bgraPixels[i * 4], mipPtr[i * 4 + 0], mipPtr[i * 4 + 1], mipPtr[i * 4 + 2], mipPtr[i * 4 + 3]);
            }
            fmtDesc = L"Khronos KTX 2.0 RGBA8 (.ktx2)";
        } else if (vkFormat >= 44 && vkFormat <= 50) { // BGRA8
            if (mipLen < pxCount * 4) return false;
            outInfo.bgraPixels.resize(pxCount * 4);
            for (size_t i = 0; i < pxCount; ++i) {
                WritePremulBGRA(&outInfo.bgraPixels[i * 4], mipPtr[i * 4 + 2], mipPtr[i * 4 + 1], mipPtr[i * 4 + 0], mipPtr[i * 4 + 3]);
            }
            fmtDesc = L"Khronos KTX 2.0 BGRA8 (.ktx2)";
        } else if (vkFormat == 76 || vkFormat == 83 || vkFormat == 90 || vkFormat == 97) { // R16F / RG16F / RGB16F / RGBA16F
            uint32_t ch = (vkFormat == 76) ? 1u : (vkFormat == 83) ? 2u : (vkFormat == 90) ? 3u : 4u;
            if (mipLen < pxCount * ch * 2) return false;
            outCh = ch;
            outBits = 16;
            std::vector<float> flt(pxCount * 4, 0.0f);
            for (size_t i = 0; i < pxCount; ++i) {
                const uint8_t* sp = mipPtr + i * ch * 2;
                float r = GpuTex::HalfToFloat(DetailImg100::RdU16LE(sp));
                float g = (ch >= 2) ? GpuTex::HalfToFloat(DetailImg100::RdU16LE(sp + 2)) : r;
                float b = (ch >= 3) ? GpuTex::HalfToFloat(DetailImg100::RdU16LE(sp + 4)) : (ch == 1 ? r : 0.0f);
                float a = (ch >= 4) ? GpuTex::HalfToFloat(DetailImg100::RdU16LE(sp + 6)) : 1.0f;
                flt[i * 4 + 0] = r; flt[i * 4 + 1] = g; flt[i * 4 + 2] = b; flt[i * 4 + 3] = a;
            }
            GpuTex::ConvertFloatRgbaToBgra(flt.data(), w, h, false, outInfo.bgraPixels);
            fmtDesc = L"Khronos KTX 2.0 RGBA16F (.ktx2)";
        } else if (vkFormat == 100 || vkFormat == 103 || vkFormat == 106 || vkFormat == 109) { // R32F / RG32F / RGB32F / RGBA32F
            uint32_t ch = (vkFormat == 100) ? 1u : (vkFormat == 103) ? 2u : (vkFormat == 106) ? 3u : 4u;
            if (mipLen < pxCount * ch * 4) return false;
            outCh = ch;
            outBits = 32;
            std::vector<float> flt(pxCount * 4, 0.0f);
            for (size_t i = 0; i < pxCount; ++i) {
                const uint8_t* sp = mipPtr + i * ch * 4;
                float r = GpuTex::ReadF32LE(sp);
                float g = (ch >= 2) ? GpuTex::ReadF32LE(sp + 4) : r;
                float b = (ch >= 3) ? GpuTex::ReadF32LE(sp + 8) : (ch == 1 ? r : 0.0f);
                float a = (ch >= 4) ? GpuTex::ReadF32LE(sp + 12) : 1.0f;
                flt[i * 4 + 0] = r; flt[i * 4 + 1] = g; flt[i * 4 + 2] = b; flt[i * 4 + 3] = a;
            }
            GpuTex::ConvertFloatRgbaToBgra(flt.data(), w, h, false, outInfo.bgraPixels);
            fmtDesc = L"Khronos KTX 2.0 RGBA32F (.ktx2)";
        } else if (vkFormat >= 131 && vkFormat <= 134) { // BC1
            if (mipLen < blocks4x4 * 8) return false;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            bool alpha1 = (vkFormat >= 133);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeBC1Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 8, blk, alpha1);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 BC1 (.ktx2)";
        } else if (vkFormat == 135 || vkFormat == 136) { // BC2
            if (mipLen < blocks4x4 * 16) return false;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeBC2Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 16, blk);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 BC2 (.ktx2)";
        } else if (vkFormat == 137 || vkFormat == 138) { // BC3
            if (mipLen < blocks4x4 * 16) return false;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeBC3Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 16, blk);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 BC3 (.ktx2)";
        } else if (vkFormat == 139 || vkFormat == 140) { // BC4
            if (mipLen < blocks4x4 * 8) return false;
            outCh = 1;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeBC4Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 8, blk, vkFormat == 140);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 BC4 (.ktx2)";
        } else if (vkFormat == 141 || vkFormat == 142) { // BC5
            if (mipLen < blocks4x4 * 16) return false;
            outCh = 2;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeBC5Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 16, blk, vkFormat == 142);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 BC5 (.ktx2)";
        } else if (vkFormat == 143 || vkFormat == 144) { // BC6H_UFLOAT / BC6H_SFLOAT
            if (mipLen < blocks4x4 * 16) return false;
            outCh = 3;
            outBits = 16;
            std::vector<float> flt(pxCount * 4, 0.0f);
            float blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeBC6HBlock(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 16, blk, vkFormat == 144);
                    for (uint32_t py = 0; py < 4; ++py) {
                        uint32_t dy = by * 4 + py;
                        if (dy >= h) continue;
                        for (uint32_t px = 0; px < 4; ++px) {
                            uint32_t dx = bx * 4 + px;
                            if (dx >= w) continue;
                            std::memcpy(&flt[(static_cast<size_t>(dy) * w + dx) * 4], &blk[(py * 4 + px) * 4], 16);
                        }
                    }
                }
            }
            GpuTex::ConvertFloatRgbaToBgra(flt.data(), w, h, true, outInfo.bgraPixels);
            fmtDesc = L"Khronos KTX 2.0 BC6H HDR (.ktx2)";
        } else if (vkFormat == 145 || vkFormat == 146) { // BC7
            if (mipLen < blocks4x4 * 16) return false;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeBC7Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 16, blk);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 BC7 (.ktx2)";
        } else if (vkFormat == 147 || vkFormat == 148) { // ETC2 RGB8
            if (mipLen < blocks4x4 * 8) return false;
            outCh = 3u;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeETC2RgbBlock(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 8, blk, true);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 ETC2 RGB8 (.ktx2)";
        } else if (vkFormat == 149 || vkFormat == 150) { // ETC2 RGB8A1 punchthrough alpha
            if (mipLen < blocks4x4 * 8) return false;
            outCh = 4u;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeETC2RgbA1Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 8, blk);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 ETC2 RGB8A1 (.ktx2)";
        } else if (vkFormat == 151 || vkFormat == 152) { // ETC2 RGBA8
            if (mipLen < blocks4x4 * 16) return false;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeETC2Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 16, blk, true);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 ETC2 RGBA8 (.ktx2)";
        } else if (vkFormat == 153 || vkFormat == 154) { // EAC R11 UNORM / SNORM
            if (mipLen < blocks4x4 * 8) return false;
            outCh = 1u;
            outBits = 11u;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeEacR11Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 8, vkFormat == 154, blk);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 EAC R11 (.ktx2)";
        } else if (vkFormat == 155 || vkFormat == 156) { // EAC RG11 UNORM / SNORM
            if (mipLen < blocks4x4 * 16) return false;
            outCh = 2u;
            outBits = 11u;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeEacRG11Block(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 16, vkFormat == 156, blk);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 EAC RG11 (.ktx2)";
        } else if (vkFormat >= 157 && vkFormat <= 184) { // ASTC 4x4 .. 12x12
            static const uint8_t kAstcDims[14][2] = {
                {4,4}, {5,4}, {5,5}, {6,5}, {6,6}, {8,5}, {8,6},
                {8,8}, {10,5}, {10,6}, {10,8}, {10,10}, {12,10}, {12,12}
            };
            uint32_t pairIdx = (vkFormat - 157u) / 2u;
            uint32_t bxDim = kAstcDims[pairIdx][0];
            uint32_t byDim = kAstcDims[pairIdx][1];
            if (!DecodeAstcSurface(mipPtr, mipLen, w, h, bxDim, byDim, outInfo.bgraPixels)) {
                return false;
            }
            fmtDesc = L"Khronos KTX 2.0 ASTC " + std::to_wstring(bxDim) + L"x" + std::to_wstring(byDim) + L" (.ktx2)";
        } else {
            return false;
        }

        outInfo.width = w;
        outInfo.height = h;
        outInfo.origWidth = w;
        outInfo.origHeight = h;
        outInfo.channels = outCh;
        outInfo.bitsPerChannel = outBits;
        outInfo.formatName = fmtDesc;
        outInfo.valid = true;
        return true;
    }

    // Basis Universal (.basis): magic 's','B' (0x4273 LE), 77-byte basis_file_header + 23-byte basis_slice_desc
    if (size < 100 || data[0] != 's' || data[1] != 'B') return false;
    uint16_t hdrVer        = DetailImg100::RdU16LE(data + 2);
    uint16_t hdrSize       = DetailImg100::RdU16LE(data + 4);
    uint16_t hdrCrc16      = DetailImg100::RdU16LE(data + 6);
    uint32_t dataSize      = DetailImg100::RdU32LE(data + 8);
    uint16_t dataCrc16     = DetailImg100::RdU16LE(data + 12);
    uint32_t totalSlices   = DetailImg100::RdU24LE(data + 14);
    uint32_t totalImages   = DetailImg100::RdU24LE(data + 17);
    uint8_t  texFormat     = data[20]; // 0 = cETC1S, 1 = cUASTC4x4
    uint16_t flags         = DetailImg100::RdU16LE(data + 21);
    uint8_t  texType       = data[23]; // 3 = cBASISTexTypeVideoFrames
    uint16_t numEndpoints  = DetailImg100::RdU16LE(data + 39);
    uint32_t epCbOfs       = DetailImg100::RdU32LE(data + 41);
    uint32_t epCbSize      = DetailImg100::RdU24LE(data + 45);
    uint16_t numSelectors  = DetailImg100::RdU16LE(data + 48);
    uint32_t selCbOfs      = DetailImg100::RdU32LE(data + 50);
    uint32_t selCbSize     = DetailImg100::RdU24LE(data + 54);
    uint32_t tablesOfs     = DetailImg100::RdU32LE(data + 57);
    uint32_t tablesSize    = DetailImg100::RdU32LE(data + 61);
    uint32_t sliceDescOfs  = DetailImg100::RdU32LE(data + 65);

    if (hdrVer == 0 || hdrSize < 77 || hdrSize > size || totalSlices == 0 || totalImages == 0 || dataSize == 0) {
        return false;
    }
    if (static_cast<uint64_t>(hdrSize) + dataSize > size || texFormat > 1u) {
        return false;
    }
    if (hdrCrc16 != 0 && DetailBasis::BasisCrc16(data + 8, hdrSize - 8u) != hdrCrc16) {
        return false;
    }
    if (dataCrc16 != 0 && DetailBasis::BasisCrc16(data + hdrSize, dataSize) != dataCrc16) {
        return false;
    }

    uint64_t sliceDescBytes = static_cast<uint64_t>(totalSlices) * 23ull;
    if (sliceDescOfs < hdrSize || sliceDescOfs > size || sliceDescBytes > size - sliceDescOfs) {
        return false;
    }

    // Pick first mip-0 RGB slice descriptor (23 bytes per basis_slice_desc)
    size_t chosenIdx = 0;
    for (size_t si = 0; si < totalSlices; ++si) {
        const uint8_t* sd = data + sliceDescOfs + si * 23u;
        if (sd[3] == 0u && (sd[4] & 0x01u) == 0u) { // m_level_index == 0 and not alpha slice
            chosenIdx = si;
            break;
        }
    }
    const uint8_t* chosenSlice = data + sliceDescOfs + chosenIdx * 23u;
    uint32_t chosenImgIdx = DetailImg100::RdU24LE(chosenSlice + 0);
    uint8_t  chosenLvlIdx = chosenSlice[3];
    uint32_t origW        = DetailImg100::RdU16LE(chosenSlice + 5);
    uint32_t origH        = DetailImg100::RdU16LE(chosenSlice + 7);
    uint32_t blocksX      = DetailImg100::RdU16LE(chosenSlice + 9);
    uint32_t blocksY      = DetailImg100::RdU16LE(chosenSlice + 11);
    uint32_t sliceOfs     = DetailImg100::RdU32LE(chosenSlice + 13);
    uint32_t sliceSize    = DetailImg100::RdU32LE(chosenSlice + 17);
    uint16_t sliceCrc16   = DetailImg100::RdU16LE(chosenSlice + 21);

    if (origW == 0 || origH == 0 || origW > 8192 || origH > 8192 || blocksX == 0 || blocksY == 0) {
        return false;
    }
    if (blocksX < (origW + 3u) / 4u || blocksY < (origH + 3u) / 4u) {
        return false;
    }
    if (sliceSize == 0 || sliceOfs < hdrSize || sliceOfs > size || sliceSize > size - sliceOfs) {
        return false;
    }
    if (sliceCrc16 != 0 && DetailBasis::BasisCrc16(data + sliceOfs, sliceSize) != sliceCrc16) {
        return false;
    }

    const uint8_t* sliceBytes = data + sliceOfs;
    size_t totalBlocks = static_cast<size_t>(blocksX) * blocksY;

    if (texFormat == 1u) { // cUASTC4x4 (16 bytes per 4x4 block)
        if (sliceSize < totalBlocks * 16u) return false;
        outInfo.bgraPixels.assign(static_cast<size_t>(origW) * origH * 4u, 0);
        uint8_t blkRgba[64];
        bool anyNonOpaque = false;
        for (uint32_t by = 0; by < blocksY; ++by) {
            for (uint32_t bx = 0; bx < blocksX; ++bx) {
                const uint8_t* blkPtr = sliceBytes + (static_cast<size_t>(by) * blocksX + bx) * 16u;
                if (!DetailBasis::DecodeUastcBlock4x4(blkPtr, blkRgba)) {
                    outInfo.bgraPixels.clear();
                    return false;
                }
                for (int t = 0; t < 16; ++t) {
                    if (blkRgba[t * 4 + 3] < 255u) anyNonOpaque = true;
                }
                GpuTex::BlitBlock4x4RgbaToPremulBgra(blkRgba, bx, by, origW, origH, outInfo.bgraPixels);
            }
        }
        outInfo.width = origW;
        outInfo.height = origH;
        outInfo.origWidth = origW;
        outInfo.origHeight = origH;
        outInfo.channels = (anyNonOpaque || (flags & 0x04u) != 0u) ? 4u : 3u;
        outInfo.bitsPerChannel = 8;
        outInfo.formatName = L"Basis Universal UASTC 4x4 (.basis)";
        outInfo.valid = true;
        return true;
    }

    // Mode 0: cETC1S — require valid endpoint codebook, selector codebook, and Huffman tables
    if (numEndpoints == 0 || epCbSize == 0 || epCbOfs < hdrSize || epCbOfs > size || epCbSize > size - epCbOfs) {
        return false;
    }
    if (numSelectors == 0 || selCbSize == 0 || selCbOfs < hdrSize || selCbOfs > size || selCbSize > size - selCbOfs) {
        return false;
    }
    if (tablesSize == 0 || tablesOfs < hdrSize || tablesOfs > size || tablesSize > size - tablesOfs) {
        return false;
    }

    std::vector<DetailBasis::Etc1sEndpoint> endpoints;
    std::vector<DetailBasis::Etc1sSelector> selectors;
    DetailBasis::Etc1sTables tables;
    if (!DetailBasis::DecodeEtc1sEndpoints(data + epCbOfs, epCbSize, numEndpoints, endpoints) ||
        !DetailBasis::DecodeEtc1sSelectors(data + selCbOfs, selCbSize, numSelectors, selectors) ||
        !DetailBasis::DecodeEtc1sTables(data + tablesOfs, tablesSize, tables)) {
        return false;
    }

    bool isVideo = (texType == 3u);
    std::vector<uint8_t> rgbRgba;
    if (!DetailBasis::DecodeEtc1sSliceToRgba(
            sliceBytes, sliceSize, blocksX, blocksY, origW, origH,
            isVideo, endpoints, selectors, tables, rgbRgba)) {
        return false;
    }

    // Optional paired alpha slice when cBASISHeaderFlagHasAlphaSlices (0x04) or next slice has cSliceDescFlagsHasAlpha (0x01)
    bool hasAlpha = false;
    if (chosenIdx + 1u < totalSlices) {
        const uint8_t* alphaDesc = data + sliceDescOfs + (chosenIdx + 1u) * 23u;
        uint32_t aImgIdx = DetailImg100::RdU24LE(alphaDesc + 0);
        uint8_t  aLvlIdx = alphaDesc[3];
        uint8_t  aFlags  = alphaDesc[4];
        if (aImgIdx == chosenImgIdx && aLvlIdx == chosenLvlIdx &&
            ((flags & 0x04u) != 0u || (flags & 0x02u) != 0u || (aFlags & 0x01u) != 0u)) {
            uint32_t aBlocksX = DetailImg100::RdU16LE(alphaDesc + 9);
            uint32_t aBlocksY = DetailImg100::RdU16LE(alphaDesc + 11);
            uint32_t aOfs     = DetailImg100::RdU32LE(alphaDesc + 13);
            uint32_t aSize    = DetailImg100::RdU32LE(alphaDesc + 17);
            uint16_t aCrc16   = DetailImg100::RdU16LE(alphaDesc + 21);
            if (aBlocksX != blocksX || aBlocksY != blocksY ||
                aSize == 0 || aOfs < hdrSize || aOfs > size || aSize > size - aOfs) {
                return false;
            }
            if (aCrc16 != 0 && DetailBasis::BasisCrc16(data + aOfs, aSize) != aCrc16) {
                return false;
            }
            std::vector<uint8_t> alphaRgba;
            if (!DetailBasis::DecodeEtc1sSliceToRgba(
                    data + aOfs, aSize, blocksX, blocksY, origW, origH,
                    isVideo, endpoints, selectors, tables, alphaRgba)) {
                return false;
            }
            size_t pxTotal = static_cast<size_t>(origW) * origH;
            for (size_t i = 0; i < pxTotal; ++i) {
                rgbRgba[i * 4u + 3] = alphaRgba[i * 4u + 1]; // ETC1S alpha slice stores alpha in Green
            }
            hasAlpha = true;
        }
    }

    size_t pxTotal = static_cast<size_t>(origW) * origH;
    outInfo.bgraPixels.resize(pxTotal * 4u);
    for (size_t i = 0; i < pxTotal; ++i) {
        WritePremulBGRA(
            &outInfo.bgraPixels[i * 4u],
            rgbRgba[i * 4u + 0],
            rgbRgba[i * 4u + 1],
            rgbRgba[i * 4u + 2],
            rgbRgba[i * 4u + 3]);
    }

    (void)extLower;
    outInfo.width = origW;
    outInfo.height = origH;
    outInfo.origWidth = origW;
    outInfo.origHeight = origH;
    outInfo.channels = hasAlpha ? 4u : 3u;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Basis Universal ETC1S (.basis)";
    outInfo.valid = true;
    return true;
}

// (Moved below DetailFont + ArtFontUpgrades.h so .ora/.kra/.procreate/.clip/.aseprite use full layer/tile compositors)

// ============================================================================
// 8. Apple Icon Image (.icns) — Magic "icns"
//    Supports WIC PNG/JP2 (ic07..ic14, icp4..icp6), classic 24-bit RGB RLE
//    (is32, il32, ih32, it32 with 4-byte zero header), 8-bit masks (s8mk, l8mk,
//    h8mk, t8mk), 1-bit masks/icons (ics#, ICN#, ich#), and 'ARGB' RLE chunks.
// ============================================================================
inline bool DecodeAppleICNS(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 16 || std::memcmp(data, "icns", 4) != 0) return false;
    uint32_t fileLen = DetailImg100::RdU32BE(data + 4);
    size_t parseLimit = (fileLen >= 16 && fileLen <= size) ? static_cast<size_t>(fileLen) : size;

    struct IcnsChunk {
        uint32_t tag = 0;
        const uint8_t* payload = nullptr;
        size_t len = 0;
    };
    auto makeTag = [](const char s[4]) -> uint32_t {
        return (static_cast<uint32_t>(static_cast<uint8_t>(s[0])) << 24) |
               (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 16) |
               (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 8) |
               static_cast<uint32_t>(static_cast<uint8_t>(s[3]));
    };

    std::vector<IcnsChunk> chunks;
    size_t pos = 8;
    while (pos + 8 <= parseLimit) {
        uint32_t tag = DetailImg100::RdU32BE(data + pos);
        uint32_t blkLen = DetailImg100::RdU32BE(data + pos + 4);
        if (blkLen < 8 || blkLen > parseLimit - pos) break;
        chunks.push_back({ tag, data + pos + 8, static_cast<size_t>(blkLen - 8) });
        pos += blkLen;
    }
    if (chunks.empty()) return false;

    // Collect 8-bit and 1-bit masks by icon dimension
    const IcnsChunk* mask8_16 = nullptr;
    const IcnsChunk* mask8_32 = nullptr;
    const IcnsChunk* mask8_48 = nullptr;
    const IcnsChunk* mask8_128 = nullptr;
    const IcnsChunk* mask1_16 = nullptr;
    const IcnsChunk* mask1_32 = nullptr;
    const IcnsChunk* mask1_48 = nullptr;

    for (const auto& ch : chunks) {
        if (ch.tag == makeTag("s8mk") && ch.len >= 16 * 16) mask8_16 = &ch;
        else if (ch.tag == makeTag("l8mk") && ch.len >= 32 * 32) mask8_32 = &ch;
        else if (ch.tag == makeTag("h8mk") && ch.len >= 48 * 48) mask8_48 = &ch;
        else if (ch.tag == makeTag("t8mk") && ch.len >= 128 * 128) mask8_128 = &ch;
        else if (ch.tag == makeTag("ics#") && ch.len >= 64) mask1_16 = &ch;
        else if (ch.tag == makeTag("ICN#") && ch.len >= 256) mask1_32 = &ch;
        else if (ch.tag == makeTag("ich#") && ch.len >= 576) mask1_48 = &ch;
    }

    // Apple ICNS PackBits variant: ctrl < 0x80 -> ctrl + 1 literals; ctrl >= 0x80 -> repeat next byte (ctrl - 125) times
    auto unpackIcnsRle = [](const uint8_t* src, size_t srcLen, size_t expectedOut, std::vector<uint8_t>& out) -> bool {
        out.clear();
        out.reserve(expectedOut);
        size_t sp = 0;
        while (sp < srcLen && out.size() < expectedOut) {
            uint8_t ctrl = src[sp++];
            if (ctrl < 0x80u) {
                size_t count = static_cast<size_t>(ctrl) + 1u;
                if (sp + count > srcLen || out.size() + count > expectedOut) return false;
                out.insert(out.end(), src + sp, src + sp + count);
                sp += count;
            } else {
                size_t count = static_cast<size_t>(ctrl) - 125u; // 3 .. 130
                if (sp >= srcLen || out.size() + count > expectedOut) return false;
                uint8_t val = src[sp++];
                out.insert(out.end(), count, val);
            }
        }
        return out.size() == expectedOut;
    };

    auto applyAlphaMask = [&](uint32_t dim, std::vector<uint8_t>& rgba) {
        size_t n = static_cast<size_t>(dim) * dim;
        const IcnsChunk* m8 = (dim == 16) ? mask8_16 : (dim == 32) ? mask8_32 : (dim == 48) ? mask8_48 : (dim == 128) ? mask8_128 : nullptr;
        const IcnsChunk* m1 = (dim == 16) ? mask1_16 : (dim == 32) ? mask1_32 : (dim == 48) ? mask1_48 : nullptr;
        if (m8) {
            for (size_t i = 0; i < n; ++i) rgba[i * 4 + 3] = m8->payload[i];
        } else if (m1) {
            const uint8_t* maskBits = m1->payload + (n / 8u);
            for (size_t i = 0; i < n; ++i) {
                uint8_t bit = (maskBits[i >> 3] >> (7u - (i & 7u))) & 1u;
                rgba[i * 4 + 3] = bit ? 255u : 0u;
            }
        } else {
            for (size_t i = 0; i < n; ++i) rgba[i * 4 + 3] = 255u;
        }
    };

    struct Candidate {
        uint32_t area = 0;
        ExtendedImageInfo info;
    };
    Candidate best;

    auto commitCandidate = [&](uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba, const std::wstring& label) {
        if (w * h <= best.area) return;
        ExtendedImageInfo cand;
        cand.width = w;
        cand.height = h;
        cand.origWidth = w;
        cand.origHeight = h;
        cand.channels = 4;
        cand.bitsPerChannel = 8;
        cand.formatName = label;
        cand.bgraPixels.resize(static_cast<size_t>(w) * h * 4);
        for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
            WritePremulBGRA(&cand.bgraPixels[i * 4], rgba[i * 4 + 0], rgba[i * 4 + 1], rgba[i * 4 + 2], rgba[i * 4 + 3]);
        }
        cand.valid = true;
        best.area = w * h;
        best.info = std::move(cand);
    };

    auto argbDimForTag = [&](uint32_t tag) -> uint32_t {
        if (tag == makeTag("ic04") || tag == makeTag("icp4")) return 16;
        if (tag == makeTag("icsb")) return 18;
        if (tag == makeTag("sb24")) return 24;
        if (tag == makeTag("ic05") || tag == makeTag("icp5") || tag == makeTag("ic11")) return 32;
        if (tag == makeTag("SB24")) return 48;
        if (tag == makeTag("icp6") || tag == makeTag("ic12")) return 64;
        if (tag == makeTag("ic07")) return 128;
        if (tag == makeTag("ic08") || tag == makeTag("ic13")) return 256;
        if (tag == makeTag("ic09") || tag == makeTag("ic14")) return 512;
        if (tag == makeTag("ic10")) return 1024;
        return 0;
    };

    for (const auto& ch : chunks) {
        if (ch.len < 4) continue;

        // 1. Embedded PNG / JPEG2000 / JPEG via WIC
        bool looksCompressedImg =
            (ch.len >= 8 && ch.payload[0] == 0x89 && ch.payload[1] == 'P' && ch.payload[2] == 'N' && ch.payload[3] == 'G') ||
            (ch.len >= 12 && ch.payload[0] == 0x00 && ch.payload[1] == 0x00 && ch.payload[2] == 0x00 && ch.payload[3] == 0x0C &&
             std::memcmp(ch.payload + 4, "jP  ", 4) == 0) ||
            (ch.len >= 4 && ch.payload[0] == 0xFF && ch.payload[1] == 0x4F && ch.payload[2] == 0xFF && ch.payload[3] == 0x51) ||
            (ch.len >= 4 && ch.payload[0] == 0xFF && ch.payload[1] == 0xD8 && ch.payload[2] == 0xFF);
        if (looksCompressedImg) {
            ExtendedImageInfo wicInfo;
            if (DetailImg100::DecodeEmbeddedWicBuffer(ch.payload, ch.len, L"Apple macOS Icon (.icns)", wicInfo)) {
                uint32_t area = wicInfo.width * wicInfo.height;
                if (area > best.area) {
                    best.area = area;
                    best.info = std::move(wicInfo);
                }
            }
            continue;
        }

        // 2. 'ARGB' 4-channel planar ICNS RLE chunks (ic04, ic05, icp4, icp5, etc.)
        if (ch.len > 4 && std::memcmp(ch.payload, "ARGB", 4) == 0) {
            uint32_t dim = argbDimForTag(ch.tag);
            if (dim > 0) {
                size_t n = static_cast<size_t>(dim) * dim;
                std::vector<uint8_t> planes;
                if (unpackIcnsRle(ch.payload + 4, ch.len - 4, n * 4, planes)) {
                    std::vector<uint8_t> rgba(n * 4);
                    for (size_t i = 0; i < n; ++i) {
                        rgba[i * 4 + 0] = planes[n + i];     // R
                        rgba[i * 4 + 1] = planes[2 * n + i]; // G
                        rgba[i * 4 + 2] = planes[3 * n + i]; // B
                        rgba[i * 4 + 3] = planes[i];         // A
                    }
                    commitCandidate(dim, dim, rgba, L"Apple macOS ARGB Icon (.icns)");
                }
            }
            continue;
        }

        // 3. Classic 24-bit RGB chunks: is32 (16), il32 (32), ih32 (48), it32 (128, 4-byte zero header prefix)
        uint32_t rgbDim = 0;
        bool isIt32 = false;
        if (ch.tag == makeTag("is32")) rgbDim = 16;
        else if (ch.tag == makeTag("il32")) rgbDim = 32;
        else if (ch.tag == makeTag("ih32")) rgbDim = 48;
        else if (ch.tag == makeTag("it32")) { rgbDim = 128; isIt32 = true; }

        if (rgbDim > 0 && rgbDim * rgbDim > best.area) {
            size_t n = static_cast<size_t>(rgbDim) * rgbDim;
            std::vector<uint8_t> planes;
            bool okRgb = false;
            std::vector<uint8_t> rgba(n * 4, 255);

            if (isIt32) {
                if (ch.len > 4 && unpackIcnsRle(ch.payload + 4, ch.len - 4, n * 3, planes)) {
                    okRgb = true;
                } else if (unpackIcnsRle(ch.payload, ch.len, n * 3, planes)) {
                    okRgb = true;
                } else if (ch.len == n * 4) {
                    for (size_t i = 0; i < n; ++i) {
                        rgba[i * 4 + 0] = ch.payload[i * 4 + 1];
                        rgba[i * 4 + 1] = ch.payload[i * 4 + 2];
                        rgba[i * 4 + 2] = ch.payload[i * 4 + 3];
                    }
                    okRgb = true;
                }
            } else {
                if (unpackIcnsRle(ch.payload, ch.len, n * 3, planes)) {
                    okRgb = true;
                } else if (ch.len == n * 4) { // Uncompressed 0RGB packed pixels
                    for (size_t i = 0; i < n; ++i) {
                        rgba[i * 4 + 0] = ch.payload[i * 4 + 1];
                        rgba[i * 4 + 1] = ch.payload[i * 4 + 2];
                        rgba[i * 4 + 2] = ch.payload[i * 4 + 3];
                    }
                    okRgb = true;
                } else if (ch.len == n * 3) {
                    planes.assign(ch.payload, ch.payload + n * 3);
                    okRgb = true;
                }
            }

            if (okRgb) {
                if (!planes.empty()) {
                    for (size_t i = 0; i < n; ++i) {
                        rgba[i * 4 + 0] = planes[i];
                        rgba[i * 4 + 1] = planes[n + i];
                        rgba[i * 4 + 2] = planes[2 * n + i];
                    }
                }
                applyAlphaMask(rgbDim, rgba);
                commitCandidate(rgbDim, rgbDim, rgba, L"Apple macOS Classic Icon (.icns)");
            }
            continue;
        }

        // 4. Standalone 1-bit monochrome icon + mask (ICN# 32x32, ics# 16x16, ich# 48x48)
        uint32_t monoDim = (ch.tag == makeTag("ics#") && ch.len >= 64) ? 16u :
                           (ch.tag == makeTag("ICN#") && ch.len >= 256) ? 32u :
                           (ch.tag == makeTag("ich#") && ch.len >= 576) ? 48u : 0u;
        if (monoDim > 0 && monoDim * monoDim > best.area) {
            size_t n = static_cast<size_t>(monoDim) * monoDim;
            const uint8_t* imgBits = ch.payload;
            const uint8_t* mskBits = ch.payload + (n / 8u);
            std::vector<uint8_t> rgba(n * 4);
            for (size_t i = 0; i < n; ++i) {
                uint8_t fg = (imgBits[i >> 3] >> (7u - (i & 7u))) & 1u;
                uint8_t mk = (mskBits[i >> 3] >> (7u - (i & 7u))) & 1u;
                uint8_t lum = fg ? 0u : 255u;
                rgba[i * 4 + 0] = lum;
                rgba[i * 4 + 1] = lum;
                rgba[i * 4 + 2] = lum;
                rgba[i * 4 + 3] = mk ? 255u : 0u;
            }
            commitCandidate(monoDim, monoDim, rgba, L"Apple macOS 1-bit Icon (.icns)");
        }
    }

    if (!best.info.valid || best.area == 0) {
        return false;
    }
    outInfo = std::move(best.info);
    return true;
}

// ============================================================================
// RFC 7932 Brotli Decompressor (used by JXL 'brob' boxes and WOFF 2.0 '.woff2')
// ============================================================================
namespace DetailBrotli {

struct BitReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t bitPos = 0;
    bool eof = false;

    BitReader(const uint8_t* d, size_t s) : data(d), size(s) {}

    uint32_t PeekBits(uint32_t n) const {
        if (n == 0u) return 0u;
        uint64_t acc = 0;
        size_t byteIdx = bitPos >> 3;
        uint32_t bitOff = static_cast<uint32_t>(bitPos & 7u);
        for (uint32_t k = 0; k < 5u; ++k) {
            uint64_t b = (byteIdx + k < size) ? data[byteIdx + k] : 0ull;
            acc |= (b << (k * 8u));
        }
        uint64_t mask = (n >= 32u) ? 0xFFFFFFFFull : ((1ull << n) - 1ull);
        return static_cast<uint32_t>((acc >> bitOff) & mask);
    }

    void DropBits(uint32_t n) {
        bitPos += n;
        if (bitPos > size * 8u) {
            eof = true;
        }
    }

    uint32_t ReadBits(uint32_t n) {
        if (n == 0u) return 0u;
        uint32_t v = PeekBits(n);
        DropBits(n);
        return v;
    }

    bool AlignToByteZero() {
        while ((bitPos & 7u) != 0u) {
            if (ReadBits(1u) != 0u) return false;
        }
        return !eof;
    }
};

inline uint32_t CeilLog2(uint32_t v) {
    if (v <= 1u) return 0u;
    uint32_t r = 0u;
    uint32_t x = v - 1u;
    while (x > 0u) {
        x >>= 1u;
        ++r;
    }
    return r;
}

inline uint32_t ReverseBits16(uint32_t v, uint32_t len) {
    uint32_t r = 0u;
    for (uint32_t i = 0; i < len; ++i) {
        r = (r << 1u) | ((v >> i) & 1u);
    }
    return r;
}

struct PrefixTree {
    uint32_t maxLen = 0;
    uint32_t singleSym = 0;
    bool isSingle = false;
    std::vector<uint32_t> table; // (len << 16) | sym, size = 1 << maxLen

    bool BuildFromLengths(const uint8_t* lengths, uint32_t alphabetSize) {
        uint32_t count[16] = {};
        uint32_t maxL = 0u;
        uint32_t nonZeroCount = 0u;
        uint32_t lastSym = 0u;
        for (uint32_t s = 0; s < alphabetSize; ++s) {
            uint8_t l = lengths[s];
            if (l > 15u) return false;
            if (l > 0u) {
                ++count[l];
                if (l > maxL) maxL = l;
                ++nonZeroCount;
                lastSym = s;
            }
        }
        if (nonZeroCount == 0u) return false;
        if (nonZeroCount == 1u) {
            isSingle = true;
            singleSym = lastSym;
            maxLen = 0u;
            table.clear();
            return true;
        }
        // Kraft sum check: sum(count[l] << (15 - l)) == 32768
        uint32_t kraft = 0u;
        for (uint32_t l = 1u; l <= 15u; ++l) {
            kraft += count[l] << (15u - l);
        }
        if (kraft != 32768u) return false;

        isSingle = false;
        maxLen = maxL;
        uint32_t nextCode[16] = {};
        uint32_t code = 0u;
        for (uint32_t l = 1u; l <= maxL; ++l) {
            code = (code + count[l - 1u]) << 1u;
            nextCode[l] = code;
        }
        const uint32_t tabSize = 1u << maxL;
        table.assign(tabSize, 0u);
        for (uint32_t s = 0; s < alphabetSize; ++s) {
            uint8_t l = lengths[s];
            if (l == 0u) continue;
            uint32_t c = nextCode[l]++;
            uint32_t rev = ReverseBits16(c, l);
            uint32_t entry = (static_cast<uint32_t>(l) << 16u) | s;
            uint32_t step = 1u << l;
            for (uint32_t idx = rev; idx < tabSize; idx += step) {
                table[idx] = entry;
            }
        }
        return true;
    }

    bool Decode(BitReader& br, uint32_t& outSym) const {
        if (isSingle) {
            outSym = singleSym;
            return !br.eof;
        }
        if (table.empty()) return false;
        uint32_t peek = br.PeekBits(maxLen);
        uint32_t entry = table[peek];
        uint32_t len = entry >> 16u;
        if (len == 0u) return false;
        br.DropBits(len);
        if (br.eof) return false;
        outSym = entry & 0xFFFFu;
        return true;
    }
};

inline bool ReadPrefixCode(BitReader& br, uint32_t alphabetSize, PrefixTree& outTree) {
    if (alphabetSize == 0u || alphabetSize > 4096u) return false;
    if (alphabetSize == 1u) {
        outTree.isSingle = true;
        outTree.singleSym = 0u;
        outTree.maxLen = 0u;
        outTree.table.clear();
        return true;
    }

    uint32_t hskip = br.ReadBits(2u);
    if (br.eof) return false;

    if (hskip == 1u) {
        // Simple prefix code (1..4 symbols)
        uint32_t nsym = br.ReadBits(2u) + 1u;
        uint32_t maxBits = CeilLog2(alphabetSize);
        uint32_t syms[4] = {};
        for (uint32_t i = 0; i < nsym; ++i) {
            syms[i] = br.ReadBits(maxBits);
            if (br.eof || syms[i] >= alphabetSize) return false;
            for (uint32_t j = 0; j < i; ++j) {
                if (syms[j] == syms[i]) return false;
            }
        }
        if (nsym == 1u) {
            outTree.isSingle = true;
            outTree.singleSym = syms[0];
            outTree.maxLen = 0u;
            outTree.table.clear();
            return true;
        }
        std::vector<uint8_t> lengths(alphabetSize, 0u);
        if (nsym == 2u) {
            lengths[syms[0]] = 1u;
            lengths[syms[1]] = 1u;
        } else if (nsym == 3u) {
            // sym[0] has code '0' (len 1), sym[1..2] have len 2 ('01','11' in LSB)
            outTree.isSingle = false;
            outTree.maxLen = 2u;
            outTree.table.resize(4u);
            if (syms[1] > syms[2]) std::swap(syms[1], syms[2]);
            outTree.table[0] = (1u << 16u) | syms[0];
            outTree.table[2] = (1u << 16u) | syms[0];
            outTree.table[1] = (2u << 16u) | syms[1];
            outTree.table[3] = (2u << 16u) | syms[2];
            return true;
        } else {
            uint32_t treeSelect = br.ReadBits(1u);
            if (br.eof) return false;
            if (treeSelect == 0u) {
                for (uint32_t i = 0; i < 4u; ++i) lengths[syms[i]] = 2u;
            } else {
                outTree.isSingle = false;
                outTree.maxLen = 3u;
                outTree.table.resize(8u);
                if (syms[2] > syms[3]) std::swap(syms[2], syms[3]);
                for (uint32_t k = 0; k < 8u; k += 2u) outTree.table[k] = (1u << 16u) | syms[0];
                outTree.table[1] = (2u << 16u) | syms[1];
                outTree.table[5] = (2u << 16u) | syms[1];
                outTree.table[3] = (3u << 16u) | syms[2];
                outTree.table[7] = (3u << 16u) | syms[3];
                return true;
            }
        }
        return outTree.BuildFromLengths(lengths.data(), alphabetSize);
    }

    // Complex prefix code (RFC 7932 Section 3.5 & ISO/IEC 18181-1)
    static constexpr uint8_t kCodeLenOrder[18] = {
        1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };
    uint8_t clLengths[18] = {};
    uint32_t space = 32u;
    uint32_t numCodes = 0u;
    for (uint32_t i = hskip; i < 18u && space > 0u; ++i) {
        uint32_t p4 = br.PeekBits(4u);
        uint32_t v = 0u, drop = 2u;
        if ((p4 & 3u) == 0u)      { v = 0u; drop = 2u; }
        else if ((p4 & 3u) == 2u) { v = 3u; drop = 2u; }
        else if ((p4 & 3u) == 1u) { v = 4u; drop = 2u; }
        else if ((p4 & 7u) == 3u) { v = 2u; drop = 3u; }
        else if (p4 == 7u)        { v = 1u; drop = 4u; }
        else                      { v = 5u; drop = 4u; }
        br.DropBits(drop);
        if (br.eof) return false;
        uint8_t sym = kCodeLenOrder[i];
        clLengths[sym] = static_cast<uint8_t>(v);
        if (v != 0u) {
            uint32_t cost = 32u >> v;
            if (cost > space) return false;
            space -= cost;
            ++numCodes;
        }
    }
    if (numCodes != 1u && space != 0u) return false;

    PrefixTree clTree;
    if (numCodes == 1u) {
        for (uint32_t s = 0; s < 18u; ++s) {
            if (clLengths[s] != 0u) {
                clTree.isSingle = true;
                clTree.singleSym = s;
                break;
            }
        }
    } else if (!clTree.BuildFromLengths(clLengths, 18u)) {
        return false;
    }

    std::vector<uint8_t> lengths(alphabetSize, 0u);
    uint32_t symIdx = 0u;
    uint8_t prevCodeLen = 8u;
    uint32_t repeat = 0u;
    uint8_t repeatLen = 0u;
    uint32_t mainSpace = 32768u;

    while (symIdx < alphabetSize && mainSpace > 0u) {
        uint32_t codeLenSym = 0u;
        if (!clTree.Decode(br, codeLenSym)) return false;
        if (codeLenSym < 16u) {
            repeat = 0u;
            lengths[symIdx++] = static_cast<uint8_t>(codeLenSym);
            if (codeLenSym != 0u) {
                prevCodeLen = static_cast<uint8_t>(codeLenSym);
                uint32_t cost = 32768u >> codeLenSym;
                if (cost > mainSpace) return false;
                mainSpace -= cost;
            }
        } else {
            uint32_t extraBits = (codeLenSym == 16u) ? 2u : 3u;
            uint8_t newLen = (codeLenSym == 16u) ? prevCodeLen : 0u;
            if (repeatLen != newLen) {
                repeat = 0u;
                repeatLen = newLen;
            }
            uint32_t oldRepeat = repeat;
            if (repeat > 0u) {
                repeat = (repeat - 2u) << extraBits;
            }
            repeat += br.ReadBits(extraBits) + 3u;
            if (br.eof || repeat <= oldRepeat) return false;
            uint32_t delta = repeat - oldRepeat;
            if (symIdx + delta > alphabetSize) return false;
            for (uint32_t k = 0; k < delta; ++k) {
                lengths[symIdx++] = repeatLen;
            }
            if (repeatLen != 0u) {
                uint64_t cost = static_cast<uint64_t>(delta) * (32768ull >> repeatLen);
                if (cost > mainSpace) return false;
                mainSpace -= static_cast<uint32_t>(cost);
            }
        }
    }
    if (mainSpace != 0u) return false;
    return outTree.BuildFromLengths(lengths.data(), alphabetSize);
}

inline uint32_t ReadVarLenUint8(BitReader& br) {
    if (br.ReadBits(1u) == 0u) return 0u;
    uint32_t n = br.ReadBits(3u);
    if (n == 0u) return 1u;
    return (1u << n) + br.ReadBits(n);
}

inline uint32_t DecodeBlockLength(BitReader& br, const PrefixTree& tree) {
    static constexpr uint32_t kBlockLenBase[26] = {
        1, 5, 9, 13, 17, 25, 33, 41, 49, 65, 81, 97, 113,
        145, 177, 209, 241, 305, 369, 497, 753, 1265, 2289, 4337, 8433, 16625
    };
    static constexpr uint8_t kBlockLenExtra[26] = {
        2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5,
        5, 5, 5, 6, 6, 7, 8, 9, 10, 11, 12, 13, 24
    };
    uint32_t code = 0u;
    if (!tree.Decode(br, code) || code >= 26u) return 0u;
    return kBlockLenBase[code] + br.ReadBits(kBlockLenExtra[code]);
}

inline bool ReadContextMap(BitReader& br, uint32_t mapSize, uint32_t& outNumTrees, std::vector<uint8_t>& outMap) {
    outNumTrees = ReadVarLenUint8(br) + 1u;
    if (br.eof || outNumTrees > 256u) return false;
    outMap.assign(mapSize, 0u);
    if (outNumTrees == 1u) return true;

    bool useRle = br.ReadBits(1u) != 0u;
    uint32_t rleMax = useRle ? (br.ReadBits(4u) + 1u) : 0u;
    if (br.eof) return false;

    PrefixTree cmTree;
    if (!ReadPrefixCode(br, outNumTrees + rleMax, cmTree)) return false;

    uint32_t i = 0u;
    while (i < mapSize) {
        uint32_t code = 0u;
        if (!cmTree.Decode(br, code)) return false;
        if (code == 0u) {
            outMap[i++] = 0u;
        } else if (code <= rleMax) {
            uint32_t reps = (1u << code) + br.ReadBits(code);
            if (br.eof || i + reps > mapSize) return false;
            for (uint32_t k = 0; k < reps; ++k) outMap[i++] = 0u;
        } else {
            outMap[i++] = static_cast<uint8_t>(code - rleMax);
        }
    }

    if (br.ReadBits(1u) != 0u) { // IMTF
        uint8_t mtf[256];
        for (uint32_t k = 0; k < 256u; ++k) mtf[k] = static_cast<uint8_t>(k);
        for (uint32_t k = 0; k < mapSize; ++k) {
            uint8_t idx = outMap[k];
            uint8_t val = mtf[idx];
            outMap[k] = val;
            for (uint8_t m = idx; m > 0u; --m) mtf[m] = mtf[m - 1u];
            mtf[0] = val;
        }
    }
    return !br.eof;
}

inline uint8_t LiteralContext(uint8_t p1, uint8_t p2, uint8_t mode) {
    if (mode == 0u) return p1 & 0x3Fu;         // LSB6
    if (mode == 1u) return p1 >> 2u;           // MSB6
    static constexpr uint8_t kLut0[256] = {
        0,0,0,0,0,0,0,0,0,4,4,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        8,12,16,12,12,20,12,16,24,28,12,12,32,12,36,12,44,44,44,44,44,44,44,44,44,44,32,32,24,40,28,12,
        12,48,52,52,52,48,52,52,52,48,52,52,52,52,52,48,52,52,52,52,52,48,52,52,52,52,52,24,12,28,12,12,
        12,56,60,60,60,56,60,60,60,56,60,60,60,60,60,56,60,60,60,60,60,56,60,60,60,60,60,24,12,28,12,0,
        0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,
        0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,
        2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,
        2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3
    };
    static constexpr uint8_t kLut1[256] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1,
        1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,
        1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,1,1,1,1,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2
    };
    static constexpr uint8_t kLut2[256] = {
        0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
        6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
    };
    if (mode == 2u) return static_cast<uint8_t>(kLut0[p1] | kLut1[p2]); // UTF8
    return static_cast<uint8_t>((kLut2[p1] << 3u) | kLut2[p2]);        // Signed
}

inline bool Decompress(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& out, size_t maxOutSize) {
    out.clear();
    if (!src || srcSize == 0u || maxOutSize == 0u) return false;
    BitReader br(src, srcSize);

    // 1. WBITS (RFC 7932 Section 9.1)
    uint32_t wbits = 16u;
    if (br.ReadBits(1u) != 0u) {
        uint32_t n = br.ReadBits(3u);
        if (n != 0u) {
            wbits = 17u + n;
        } else {
            n = br.ReadBits(3u);
            if (n == 1u) return false; // Reserved invalid WBITS pattern
            wbits = (n != 0u) ? (8u + n) : 17u;
        }
    }
    if (br.eof) return false;
    const size_t maxBackDist = (size_t(1u) << wbits) - 16u;

    int32_t distRing[4] = { 16, 15, 11, 4 };
    uint32_t distRingIdx = 0u;
    uint8_t prevByte1 = 0u, prevByte2 = 0u;

    static constexpr uint32_t kInsBase[24] = {
        0, 1, 2, 3, 4, 5, 6, 8, 10, 14, 18, 26, 34, 50, 66, 98,
        130, 194, 322, 578, 1090, 2114, 6210, 22594
    };
    static constexpr uint8_t kInsExtra[24] = {
        0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5,
        6, 7, 8, 9, 10, 12, 14, 24
    };
    static constexpr uint32_t kCopyBase[24] = {
        2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 18, 22, 30, 38, 54,
        70, 102, 134, 198, 326, 582, 1094, 2118
    };
    static constexpr uint8_t kCopyExtra[24] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4,
        5, 5, 6, 7, 8, 9, 10, 24
    };
    // RFC 7932 Section 5 Table of 11 ranges (each 64 codes, bits [5:3] = ins low 3, bits [2:0] = copy low 3):
    // Range 0 (0..63):     ins 0..7,   copy 0..7   (dist 0)
    // Range 1 (64..127):   ins 0..7,   copy 8..15  (dist 0)
    // Range 2 (128..191):  ins 0..7,   copy 0..7
    // Range 3 (192..255):  ins 0..7,   copy 8..15
    // Range 4 (256..319):  ins 8..15,  copy 0..7
    // Range 5 (320..383):  ins 8..15,  copy 8..15
    // Range 6 (384..447):  ins 0..7,   copy 16..23
    // Range 7 (448..511):  ins 16..23, copy 0..7
    // Range 8 (512..575):  ins 8..15,  copy 16..23
    // Range 9 (576..639):  ins 16..23, copy 8..15
    // Range 10 (640..703): ins 16..23, copy 16..23
    static constexpr uint8_t kRangeIns[11]  = { 0, 0, 0, 0, 8, 8, 0, 16, 8, 16, 16 };
    static constexpr uint8_t kRangeCopy[11] = { 0, 8, 0, 8, 0, 8, 16, 0, 16, 8, 16 };

    bool isLast = false;
    while (!isLast) {
        isLast = br.ReadBits(1u) != 0u;
        if (br.eof) return false;
        if (isLast) {
            bool isLastEmpty = br.ReadBits(1u) != 0u;
            if (br.eof) return false;
            if (isLastEmpty) break;
        }

        uint32_t mnibblesCode = br.ReadBits(2u);
        if (br.eof) return false;
        if (mnibblesCode == 3u) {
            if (isLast) return false;
            if (br.ReadBits(1u) != 0u) return false; // Reserved bit must be 0
            uint32_t mskipBytes = br.ReadBits(2u);
            uint32_t metaLen = 0u;
            if (mskipBytes > 0u) {
                for (uint32_t i = 0; i < mskipBytes; ++i) {
                    uint32_t b = br.ReadBits(8u);
                    if (i + 1u == mskipBytes && mskipBytes > 1u && b == 0u) return false;
                    metaLen |= (b << (i * 8u));
                }
                ++metaLen;
            }
            if (!br.AlignToByteZero()) return false;
            size_t curByte = br.bitPos >> 3;
            if (metaLen > srcSize - curByte) return false;
            br.bitPos += static_cast<size_t>(metaLen) * 8u;
            continue;
        }

        uint32_t mnibbles = mnibblesCode + 4u;
        uint32_t mlenMinus1 = 0u;
        for (uint32_t i = 0; i < mnibbles; ++i) {
            uint32_t nib = br.ReadBits(4u);
            if (i + 1u == mnibbles && mnibbles > 4u && nib == 0u) return false;
            mlenMinus1 |= (nib << (i * 4u));
        }
        if (br.eof) return false;
        size_t mlen = static_cast<size_t>(mlenMinus1) + 1u;
        if (mlen > maxOutSize - out.size()) return false;

        if (!isLast) {
            bool isUncompressed = br.ReadBits(1u) != 0u;
            if (br.eof) return false;
            if (isUncompressed) {
                if (!br.AlignToByteZero()) return false;
                size_t curByte = br.bitPos >> 3;
                if (mlen > srcSize - curByte) return false;
                out.insert(out.end(), src + curByte, src + curByte + mlen);
                if (mlen >= 1u) {
                    prevByte2 = (mlen >= 2u) ? src[curByte + mlen - 2u] : prevByte1;
                    prevByte1 = src[curByte + mlen - 1u];
                }
                br.bitPos += mlen * 8u;
                continue;
            }
        }

        // Compressed meta-block
        uint32_t nblTypes[3] = { 1u, 1u, 1u };
        PrefixTree blockTypeTrees[3];
        PrefixTree blockLenTrees[3];
        uint32_t blockType[3] = { 0u, 0u, 0u };
        uint32_t prevBlockType[3] = { 1u, 1u, 1u };
        uint32_t blockRem[3] = { 1u << 28u, 1u << 28u, 1u << 28u };

        for (int cat = 0; cat < 3; ++cat) {
            nblTypes[cat] = ReadVarLenUint8(br) + 1u;
            if (br.eof || nblTypes[cat] > 256u) return false;
            if (nblTypes[cat] >= 2u) {
                if (!ReadPrefixCode(br, nblTypes[cat] + 2u, blockTypeTrees[cat]) ||
                    !ReadPrefixCode(br, 26u, blockLenTrees[cat])) {
                    return false;
                }
                blockRem[cat] = DecodeBlockLength(br, blockLenTrees[cat]);
                if (br.eof || blockRem[cat] == 0u) return false;
            }
        }

        uint32_t npostfix = br.ReadBits(2u);
        uint32_t ndirect = br.ReadBits(4u) << npostfix;
        if (br.eof) return false;

        std::vector<uint8_t> ctxModes(nblTypes[0], 0u);
        for (uint32_t i = 0; i < nblTypes[0]; ++i) {
            ctxModes[i] = static_cast<uint8_t>(br.ReadBits(2u));
        }
        if (br.eof) return false;

        uint32_t numLitTrees = 1u, numDistTrees = 1u;
        std::vector<uint8_t> litCtxMap, distCtxMap;
        if (!ReadContextMap(br, nblTypes[0] * 64u, numLitTrees, litCtxMap) ||
            !ReadContextMap(br, nblTypes[2] * 4u, numDistTrees, distCtxMap)) {
            return false;
        }

        std::vector<PrefixTree> litTrees(numLitTrees);
        std::vector<PrefixTree> iacTrees(nblTypes[1]);
        uint32_t distAlpha = 16u + ndirect + (48u << npostfix);
        std::vector<PrefixTree> distTrees(numDistTrees);

        for (uint32_t i = 0; i < numLitTrees; ++i) {
            if (!ReadPrefixCode(br, 256u, litTrees[i])) return false;
        }
        for (uint32_t i = 0; i < nblTypes[1]; ++i) {
            if (!ReadPrefixCode(br, 704u, iacTrees[i])) return false;
        }
        for (uint32_t i = 0; i < numDistTrees; ++i) {
            if (!ReadPrefixCode(br, distAlpha, distTrees[i])) return false;
        }

        auto stepBlockSwitch = [&](int cat) -> bool {
            if (nblTypes[cat] < 2u) return true;
            uint32_t code = 0u;
            if (!blockTypeTrees[cat].Decode(br, code)) return false;
            uint32_t nextType = 0u;
            if (code == 0u) nextType = prevBlockType[cat];
            else if (code == 1u) nextType = (blockType[cat] + 1u) % nblTypes[cat];
            else nextType = code - 2u;
            if (nextType >= nblTypes[cat]) return false;
            prevBlockType[cat] = blockType[cat];
            blockType[cat] = nextType;
            blockRem[cat] = DecodeBlockLength(br, blockLenTrees[cat]);
            return !br.eof && blockRem[cat] > 0u;
        };

        out.reserve(out.size() + mlen);
        size_t produced = 0u;
        while (produced < mlen) {
            if (blockRem[1] == 0u && !stepBlockSwitch(1)) return false;
            --blockRem[1];

            uint32_t iacSym = 0u;
            if (!iacTrees[blockType[1]].Decode(br, iacSym) || iacSym >= 704u) return false;
            uint32_t rangeIdx = iacSym >> 6u;
            uint32_t insCode  = static_cast<uint32_t>(kRangeIns[rangeIdx]) + ((iacSym >> 3u) & 7u);
            uint32_t copyCode = static_cast<uint32_t>(kRangeCopy[rangeIdx]) + (iacSym & 7u);
            bool distZeroFlag = (rangeIdx < 2u);

            size_t insLen  = kInsBase[insCode]  + br.ReadBits(kInsExtra[insCode]);
            size_t copyLen = kCopyBase[copyCode] + br.ReadBits(kCopyExtra[copyCode]);
            if (br.eof || insLen > mlen - produced) return false;

            for (size_t k = 0; k < insLen; ++k) {
                if (blockRem[0] == 0u && !stepBlockSwitch(0)) return false;
                --blockRem[0];
                uint8_t cMode = ctxModes[blockType[0]];
                uint8_t lCtx = LiteralContext(prevByte1, prevByte2, cMode);
                uint8_t treeIdx = litCtxMap[blockType[0] * 64u + lCtx];
                uint32_t lit = 0u;
                if (treeIdx >= numLitTrees || !litTrees[treeIdx].Decode(br, lit) || lit > 255u) return false;
                uint8_t b = static_cast<uint8_t>(lit);
                out.push_back(b);
                prevByte2 = prevByte1;
                prevByte1 = b;
            }
            produced += insLen;
            if (produced == mlen) break;
            if (copyLen > mlen - produced) return false;

            int32_t dist = 0;
            bool recordInRing = false;
            if (distZeroFlag) {
                dist = distRing[(distRingIdx - 1u) & 3u];
            } else {
                if (blockRem[2] == 0u && !stepBlockSwitch(2)) return false;
                --blockRem[2];
                uint32_t dCtx = (copyLen > 4u) ? 3u : static_cast<uint32_t>(copyLen - 2u);
                uint8_t dTreeIdx = distCtxMap[blockType[2] * 4u + dCtx];
                uint32_t dSym = 0u;
                if (dTreeIdx >= numDistTrees || !distTrees[dTreeIdx].Decode(br, dSym)) return false;

                if (dSym < 16u) {
                    static constexpr uint8_t kRingBack[16]  = { 1, 2, 3, 4, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2 };
                    static constexpr int8_t  kRingDelta[16] = { 0, 0, 0, 0,-1, 1,-2, 2,-3, 3,-1, 1,-2, 2,-3, 3 };
                    dist = distRing[(distRingIdx - kRingBack[dSym]) & 3u] + kRingDelta[dSym];
                    recordInRing = (dSym > 0u);
                } else if (dSym < 16u + ndirect) {
                    dist = static_cast<int32_t>(dSym - 15u);
                    recordInRing = true;
                } else {
                    uint32_t dRem = dSym - 16u - ndirect;
                    uint32_t postfix = dRem & ((1u << npostfix) - 1u);
                    uint32_t hcode = dRem >> npostfix;
                    uint32_t ndistbits = 1u + (hcode >> 1u);
                    uint32_t offset = ((2u + (hcode & 1u)) << ndistbits) - 4u;
                    uint32_t dextra = br.ReadBits(ndistbits);
                    if (br.eof) return false;
                    uint64_t fullDist = ((static_cast<uint64_t>(offset) + dextra) << npostfix) + postfix + ndirect + 1ull;
                    if (fullDist > 0x7FFFFFFFull) return false;
                    dist = static_cast<int32_t>(fullDist);
                    recordInRing = true;
                }
            }
            if (dist <= 0) return false;
            size_t uDist = static_cast<size_t>(dist);
            size_t maxAllowed = std::min(out.size(), maxBackDist);
            if (uDist > maxAllowed) return false; // Out-of-window or unsupported static dict word
            if (recordInRing) {
                distRing[distRingIdx & 3u] = dist;
                ++distRingIdx;
            }

            for (size_t k = 0; k < copyLen; ++k) {
                uint8_t b = out[out.size() - uDist];
                out.push_back(b);
                prevByte2 = prevByte1;
                prevByte1 = b;
            }
            produced += copyLen;
        }
    }

    if (!br.AlignToByteZero()) return false;
    return !out.empty() || maxOutSize == 0u;
}

} // namespace DetailBrotli

// ============================================================================
// 9 & 10. JPEG XL (.jxl) & Better Portable Graphics (.bpg)
// ============================================================================
namespace DetailJxlBpg {

inline int32_t UnpackSigned(uint32_t u) {
    return static_cast<int32_t>((u >> 1u) ^ (~(u & 1u) + 1u));
}

#include "JxlUpgrade.h"

struct JxlHybridUintCfg {
    uint32_t splitExp = 0;
    uint32_t splitToken = 0;
    uint32_t msbInToken = 0;
    uint32_t lsbInToken = 0;
};

struct JxlHistogramSet {
    bool lz77Enabled = false;
    uint32_t lzMinSymbol = 224;
    uint32_t lzMinLength = 3;
    JxlHybridUintCfg lzLenCfg;
    std::vector<uint8_t> clusterMap;
    std::vector<JxlHybridUintCfg> uintCfgs;
    std::vector<DetailBrotli::PrefixTree> trees;

    // Pure-C++ rANS state & alias distributions (when use_prefix_code == 0)
    bool useAns = false;
    uint32_t ansLogAlphaSize = 8u;
    uint32_t ansLogBucketSize = 4u;
    std::vector<JxlAnsDist> ansDists;
    bool ansStateInitialized = false;
    uint32_t ansState = 0u;

    // Stateful LZ77 window across symbols within a ModularStream
    uint32_t lzNumToCopy = 0;
    uint32_t lzCopyDist = 1;
    std::vector<uint32_t> lzWindow;
    uint32_t lzWinPos = 0;
};

inline bool ReadJxlSymbol(DetailBrotli::BitReader& br, JxlHistogramSet& hs, uint32_t ctx, uint32_t& outVal);

inline bool ReadHybridUintConfig(DetailBrotli::BitReader& br, uint32_t logAlphaSize, JxlHybridUintCfg& cfg) {
    uint32_t splitBits = DetailBrotli::CeilLog2(logAlphaSize + 1u);
    cfg.splitExp = br.ReadBits(splitBits);
    if (br.eof || cfg.splitExp > logAlphaSize) return false;
    cfg.splitToken = 1u << cfg.splitExp;
    if (cfg.splitExp != logAlphaSize) {
        uint32_t msbBits = DetailBrotli::CeilLog2(cfg.splitExp + 1u);
        cfg.msbInToken = br.ReadBits(msbBits);
        if (br.eof || cfg.msbInToken > cfg.splitExp) return false;
        uint32_t lsbBits = DetailBrotli::CeilLog2(cfg.splitExp - cfg.msbInToken + 1u);
        cfg.lsbInToken = br.ReadBits(lsbBits);
        if (br.eof || cfg.msbInToken + cfg.lsbInToken > cfg.splitExp) return false;
    } else {
        cfg.msbInToken = 0u;
        cfg.lsbInToken = 0u;
    }
    return true;
}

inline uint32_t ReadHybridUintDirect(DetailBrotli::BitReader& br, uint32_t token, const JxlHybridUintCfg& cfg) {
    if (token < cfg.splitToken) return token;
    uint32_t n = cfg.splitExp - (cfg.msbInToken + cfg.lsbInToken) +
                 ((token - cfg.splitToken) >> (cfg.msbInToken + cfg.lsbInToken));
    if (n >= 30u) {
        br.eof = true;
        return 0u;
    }
    uint32_t low = token & ((1u << cfg.lsbInToken) - 1u);
    token >>= cfg.lsbInToken;
    token &= ((1u << cfg.msbInToken) - 1u);
    token |= (1u << cfg.msbInToken);
    uint64_t res = (((static_cast<uint64_t>(token) << n) | br.ReadBits(n)) << cfg.lsbInToken) | low;
    if (res > 0xFFFFFFFFull) {
        br.eof = true;
        return 0u;
    }
    return static_cast<uint32_t>(res);
}

inline bool DecodeJxlHistograms(DetailBrotli::BitReader& br, uint32_t numContexts, JxlHistogramSet& hs) {
    if (numContexts == 0u || numContexts > (1u << 16u)) return false;
    hs.lz77Enabled = br.ReadBits(1u) != 0u;
    if (br.eof) return false;
    auto rdU32 = [&](uint32_t c0, uint32_t u0, uint32_t c1, uint32_t u1,
                     uint32_t c2, uint32_t u2, uint32_t c3, uint32_t u3) -> uint32_t {
        uint32_t sel = br.ReadBits(2u);
        if (sel == 0u) return c0 + br.ReadBits(u0);
        if (sel == 1u) return c1 + br.ReadBits(u1);
        if (sel == 2u) return c2 + br.ReadBits(u2);
        return c3 + br.ReadBits(u3);
    };
    if (hs.lz77Enabled) {
        hs.lzMinSymbol = rdU32(224, 0, 512, 0, 4096, 0, 8, 15);
        hs.lzMinLength = rdU32(3, 0, 4, 0, 5, 2, 9, 8);
        if (br.eof || !ReadHybridUintConfig(br, 8u, hs.lzLenCfg)) return false;
        hs.lzWindow.assign(1u << 20u, 0u);
        hs.lzWinPos = 0u;
        hs.lzNumToCopy = 0u;
    }

    uint32_t totalContexts = numContexts + (hs.lz77Enabled ? 1u : 0u);
    hs.clusterMap.assign(totalContexts, 0u);
    uint32_t numClusters = 1u;
    if (totalContexts > 1u) {
        bool simpleCluster = br.ReadBits(1u) != 0u;
        if (br.eof) return false;
        if (simpleCluster) {
            uint32_t nbits = br.ReadBits(2u);
            numClusters = 0u;
            for (uint32_t i = 0; i < totalContexts; ++i) {
                uint8_t c = static_cast<uint8_t>(br.ReadBits(nbits));
                hs.clusterMap[i] = c;
                if (static_cast<uint32_t>(c) + 1u > numClusters) numClusters = static_cast<uint32_t>(c) + 1u;
            }
            if (br.eof) return false;
        } else {
            bool useMtf = br.ReadBits(1u) != 0u;
            JxlHistogramSet nested;
            if (!DecodeJxlHistograms(br, 1u, nested)) return false;
            uint8_t mtf[256];
            for (uint32_t k = 0; k < 256u; ++k) mtf[k] = static_cast<uint8_t>(k);
            numClusters = 0u;
            for (uint32_t i = 0; i < totalContexts; ++i) {
                uint32_t sym = 0u;
                if (!ReadJxlSymbol(br, nested, 0u, sym) || sym >= 256u) return false;
                uint8_t c = static_cast<uint8_t>(sym);
                if (useMtf) {
                    uint8_t val = mtf[c];
                    for (uint8_t m = c; m > 0u; --m) mtf[m] = mtf[m - 1u];
                    mtf[0] = val;
                    c = val;
                }
                hs.clusterMap[i] = c;
                if (static_cast<uint32_t>(c) + 1u > numClusters) numClusters = static_cast<uint32_t>(c) + 1u;
            }
            if (nested.useAns && nested.ansStateInitialized && nested.ansState != 0x00130000u) return false;
        }
    }
    if (numClusters == 0u || numClusters > 256u) return false;

    bool usePrefixCode = br.ReadBits(1u) != 0u;
    if (br.eof) return false;
    if (!usePrefixCode) {
        hs.useAns = true;
        hs.ansStateInitialized = false;
        hs.ansState = 0u;
        hs.ansLogAlphaSize = 5u + br.ReadBits(2u);
        hs.ansLogBucketSize = 12u - hs.ansLogAlphaSize;
        if (br.eof) return false;
        hs.uintCfgs.resize(numClusters);
        for (uint32_t i = 0; i < numClusters; ++i) {
            if (!ReadHybridUintConfig(br, hs.ansLogAlphaSize, hs.uintCfgs[i])) return false;
        }
        hs.ansDists.resize(numClusters);
        for (uint32_t i = 0; i < numClusters; ++i) {
            if (!ReadJxlAnsDistribution(br, hs.ansLogAlphaSize, hs.ansDists[i])) return false;
        }
        return !br.eof;
    }

    hs.useAns = false;
    const uint32_t logAlphaSize = 15u;
    hs.uintCfgs.resize(numClusters);
    for (uint32_t i = 0; i < numClusters; ++i) {
        if (!ReadHybridUintConfig(br, logAlphaSize, hs.uintCfgs[i])) return false;
    }
    std::vector<uint32_t> alphaSizes(numClusters, 1u);
    for (uint32_t i = 0; i < numClusters; ++i) {
        if (br.ReadBits(1u) != 0u) {
            uint32_t n = br.ReadBits(4u);
            alphaSizes[i] = 1u + (1u << n) + br.ReadBits(n);
            if (br.eof || alphaSizes[i] > (1u << 15u)) return false;
        }
    }
    hs.trees.resize(numClusters);
    for (uint32_t i = 0; i < numClusters; ++i) {
        if (!DetailBrotli::ReadPrefixCode(br, alphaSizes[i], hs.trees[i])) return false;
    }
    return !br.eof;
}

inline bool DecodeJxlRawToken(DetailBrotli::BitReader& br, JxlHistogramSet& hs, uint8_t cluster, uint32_t& outTok) {
    if (!hs.useAns) {
        if (cluster >= hs.trees.size()) return false;
        return hs.trees[cluster].Decode(br, outTok);
    }
    if (cluster >= hs.ansDists.size()) return false;
    if (!hs.ansStateInitialized) {
        uint32_t hi = br.ReadBits(16u);
        uint32_t lo = br.ReadBits(16u);
        if (br.eof) return false;
        hs.ansState = (hi << 16u) | lo;
        hs.ansStateInitialized = true;
    }
    const auto& dist = hs.ansDists[cluster];
    if (dist.singleSymbol >= 0) {
        outTok = static_cast<uint32_t>(dist.singleSymbol);
        return true;
    }
    uint32_t res = hs.ansState & 0xFFFu;
    uint32_t bIdx = res >> hs.ansLogBucketSize;
    uint32_t pos  = res & ((1u << hs.ansLogBucketSize) - 1u);
    const auto& b = dist.buckets[bIdx];
    uint32_t sym = 0u;
    uint32_t offset = 0u;
    if (pos < b.cutoff) {
        sym = bIdx;
        offset = pos;
    } else {
        sym = b.rightSym;
        offset = static_cast<uint32_t>(static_cast<int32_t>(pos) + b.offset);
    }
    uint32_t freq = dist.freqs[sym];
    if (freq == 0u) return false;
    hs.ansState = freq * (hs.ansState >> 12u) + offset;
    if (hs.ansState < (1u << 16u)) {
        uint32_t low16 = br.ReadBits(16u);
        if (br.eof) return false;
        hs.ansState = (hs.ansState << 16u) | low16;
    }
    outTok = sym;
    return true;
}

inline bool ReadJxlSymbol(DetailBrotli::BitReader& br, JxlHistogramSet& hs, uint32_t ctx, uint32_t& outVal) {
    if (hs.lz77Enabled && hs.lzNumToCopy > 0u) {
        --hs.lzNumToCopy;
        uint32_t v = hs.lzWindow[(hs.lzWinPos - hs.lzCopyDist) & 0xFFFFFu];
        hs.lzWindow[hs.lzWinPos & 0xFFFFFu] = v;
        ++hs.lzWinPos;
        outVal = v;
        return true;
    }
    if (ctx >= hs.clusterMap.size()) return false;
    uint8_t cluster = hs.clusterMap[ctx];
    uint32_t tok = 0u;
    if (!DecodeJxlRawToken(br, hs, cluster, tok)) return false;
    if (hs.lz77Enabled && tok >= hs.lzMinSymbol) {
        uint32_t lenTok = tok - hs.lzMinSymbol;
        uint32_t copyLen = hs.lzMinLength + ReadHybridUintDirect(br, lenTok, hs.lzLenCfg);
        if (br.eof || copyLen == 0u) return false;
        uint8_t distCluster = hs.clusterMap.back();
        uint32_t distTok = 0u;
        if (!DecodeJxlRawToken(br, hs, distCluster, distTok)) return false;
        uint32_t distSym = ReadHybridUintDirect(br, distTok, hs.uintCfgs[distCluster]);
        if (br.eof) return false;
        // ISO/IEC 18181-1 2D special distance table (first 120 codes, fallback linear for 1D/general):
        uint32_t dist = (distSym < 120u) ? (distSym + 1u) : (distSym - 119u);
        if (dist == 0u) dist = 1u;
        hs.lzCopyDist = std::min(dist, std::max(1u, hs.lzWinPos));
        hs.lzNumToCopy = copyLen - 1u;
        uint32_t v = hs.lzWindow[(hs.lzWinPos - hs.lzCopyDist) & 0xFFFFFu];
        hs.lzWindow[hs.lzWinPos & 0xFFFFFu] = v;
        ++hs.lzWinPos;
        outVal = v;
        return true;
    }
    uint32_t val = ReadHybridUintDirect(br, tok, hs.uintCfgs[cluster]);
    if (br.eof) return false;
    if (hs.lz77Enabled) {
        hs.lzWindow[hs.lzWinPos & 0xFFFFFu] = val;
        ++hs.lzWinPos;
    }
    outVal = val;
    return true;
}

struct JxlMaNode {
    int32_t prop = -1; // -1 = leaf
    int32_t splitVal = 0;
    uint32_t left = 0;
    uint32_t right = 0;
    uint32_t leafCtx = 0;
    uint8_t predictor = 0;
    int32_t offset = 0;
    uint32_t multiplier = 1;
};

struct JxlWpHeader {
    uint32_t p1c = 16, p2c = 10;
    uint32_t p3a = 7, p3b = 7, p3c = 7, p3d = 0, p3e = 0;
    uint32_t w[4] = { 13, 12, 12, 12 };
};

inline bool ReadWpHeader(DetailBrotli::BitReader& br, JxlWpHeader& wp) {
    bool allDef = br.ReadBits(1u) != 0u;
    if (br.eof) return false;
    if (!allDef) {
        wp.p1c = br.ReadBits(7u);
        wp.p2c = br.ReadBits(7u);
        wp.p3a = br.ReadBits(7u);
        wp.p3b = br.ReadBits(7u);
        wp.p3c = br.ReadBits(7u);
        wp.p3d = br.ReadBits(7u);
        wp.p3e = br.ReadBits(7u);
        wp.w[0] = br.ReadBits(4u);
        wp.w[1] = br.ReadBits(4u);
        wp.w[2] = br.ReadBits(4u);
        wp.w[3] = br.ReadBits(4u);
    }
    return !br.eof;
}

inline bool DecodeMaTree(DetailBrotli::BitReader& br, size_t maxNodes, std::vector<JxlMaNode>& nodes, uint32_t& outNumLeaves) {
    JxlHistogramSet treeHist;
    if (!DecodeJxlHistograms(br, 6u, treeHist)) return false;

    nodes.clear();
    nodes.push_back(JxlMaNode{});
    size_t cur = 0u;
    outNumLeaves = 0u;
    while (cur < nodes.size()) {
        if (nodes.size() > maxNodes) return false;
        uint32_t propSym = 0u;
        if (!ReadJxlSymbol(br, treeHist, 1u, propSym)) return false;
        if (propSym == 0u) {
            uint32_t pred = 0u, offU = 0u, mulLog = 0u, mulBits = 0u;
            if (!ReadJxlSymbol(br, treeHist, 2u, pred) || pred > 13u ||
                !ReadJxlSymbol(br, treeHist, 3u, offU) ||
                !ReadJxlSymbol(br, treeHist, 4u, mulLog) || mulLog > 30u ||
                !ReadJxlSymbol(br, treeHist, 5u, mulBits)) {
                return false;
            }
            nodes[cur].prop = -1;
            nodes[cur].leafCtx = outNumLeaves++;
            nodes[cur].predictor = static_cast<uint8_t>(pred);
            nodes[cur].offset = UnpackSigned(offU);
            nodes[cur].multiplier = (mulBits + 1u) << mulLog;
        } else {
            uint32_t splitU = 0u;
            if (!ReadJxlSymbol(br, treeHist, 0u, splitU)) return false;
            uint32_t leftIdx = static_cast<uint32_t>(nodes.size());
            nodes.push_back(JxlMaNode{});
            nodes.push_back(JxlMaNode{});
            nodes[cur].prop = static_cast<int32_t>(propSym - 1u);
            nodes[cur].splitVal = UnpackSigned(splitU);
            nodes[cur].left = leftIdx;
            nodes[cur].right = leftIdx + 1u;
        }
        ++cur;
    }
    if (treeHist.useAns && treeHist.ansStateInitialized && treeHist.ansState != 0x00130000u) return false;
    return outNumLeaves > 0u;
}

struct JxlChannel {
    uint32_t w = 0, h = 0;
    int32_t hshift = 0, vshift = 0;
    bool decoded = false;
    std::vector<int32_t> pixels;
};

struct JxlSqueezeStep {
    bool horizontal = false;
    bool inPlace = false;
    uint32_t beginC = 0;
    uint32_t numC = 0;
};

struct JxlTransform {
    uint32_t trId = 0; // 0=RCT, 1=Palette, 2=Squeeze
    uint32_t beginC = 0;
    uint32_t rctType = 6;
    uint32_t numC = 0;
    uint32_t nbColors = 0;
    uint32_t nbDeltas = 0;
    uint32_t dPred = 0;
    std::vector<JxlSqueezeStep> squeezes;
};

inline int64_t PredictModularSample(
    uint8_t pred, int64_t W, int64_t N, int64_t NW, int64_t NE, int64_t NN, int64_t WW, int64_t NEE, int64_t wpPred)
{
    switch (pred) {
    case 0:  return 0;
    case 1:  return W;
    case 2:  return N;
    case 3:  return (W + N) >> 1;
    case 4:  return (std::abs(N - NW) < std::abs(W - NW)) ? W : N;
    case 5:  return std::clamp<int64_t>(W + N - NW, std::min(W, N), std::max(W, N));
    case 6:  return wpPred;
    case 7:  return NE;
    case 8:  return NW;
    case 9:  return WW;
    case 10: return (W + NW) >> 1;
    case 11: return (N + NW) >> 1;
    case 12: return (N + NE) >> 1;
    case 13: return (6 * N - 2 * NN + 7 * W + WW + NEE + 3 * NE + 8) >> 4;
    default: return 0;
    }
}

inline bool ReadAndApplyTransformHeader(
    DetailBrotli::BitReader& br,
    std::vector<JxlChannel>& channels,
    uint32_t& nbMetaChannels,
    JxlTransform& tr)
{
    auto rdU32 = [&](uint32_t c0, uint32_t u0, uint32_t c1, uint32_t u1,
                     uint32_t c2, uint32_t u2, uint32_t c3, uint32_t u3) -> uint32_t {
        uint32_t sel = br.ReadBits(2u);
        if (sel == 0u) return c0 + br.ReadBits(u0);
        if (sel == 1u) return c1 + br.ReadBits(u1);
        if (sel == 2u) return c2 + br.ReadBits(u2);
        return c3 + br.ReadBits(u3);
    };
    tr.trId = br.ReadBits(2u);
    if (br.eof || tr.trId > 2u) return false;

    if (tr.trId == 0u) { // RCT
        tr.beginC = rdU32(0, 3, 8, 6, 72, 10, 1096, 13);
        tr.rctType = rdU32(6, 0, 0, 2, 2, 4, 10, 6);
        if (br.eof || tr.rctType >= 42u || tr.beginC + 3u > channels.size()) return false;
        for (uint32_t k = 1u; k < 3u; ++k) {
            if (channels[tr.beginC + k].w != channels[tr.beginC].w ||
                channels[tr.beginC + k].h != channels[tr.beginC].h) return false;
        }
        return true;
    }

    if (tr.trId == 1u) { // Palette
        tr.beginC   = rdU32(0, 3, 8, 6, 72, 10, 1096, 13);
        tr.numC     = rdU32(1, 0, 3, 0, 4, 0, 1, 13);
        tr.nbColors = rdU32(0, 8, 256, 10, 1280, 12, 5376, 16);
        tr.nbDeltas = rdU32(0, 0, 1, 8, 257, 10, 1281, 16);
        tr.dPred    = br.ReadBits(4u);
        if (br.eof || tr.dPred > 13u || tr.numC == 0u || tr.beginC + tr.numC > channels.size() ||
            tr.nbColors + tr.nbDeltas > 65536u) return false;
        uint32_t w0 = channels[tr.beginC].w;
        uint32_t h0 = channels[tr.beginC].h;
        for (uint32_t k = 1u; k < tr.numC; ++k) {
            if (channels[tr.beginC + k].w != w0 || channels[tr.beginC + k].h != h0) return false;
        }
        if (tr.beginC < nbMetaChannels) ++nbMetaChannels;
        else nbMetaChannels += 1u;
        // Erase beginC+1 .. beginC+numC-1, keep beginC as index channel, insert palette meta-channel at 0
        if (tr.numC > 1u) {
            channels.erase(channels.begin() + tr.beginC + 1, channels.begin() + tr.beginC + tr.numC);
        }
        JxlChannel palCh;
        palCh.w = tr.nbColors + tr.nbDeltas;
        palCh.h = tr.numC;
        palCh.hshift = -1;
        palCh.vshift = -1;
        channels.insert(channels.begin(), std::move(palCh));
        return true;
    }

    // Squeeze (trId == 2)
    uint32_t numSq = rdU32(0, 0, 1, 4, 9, 6, 41, 8);
    if (br.eof || numSq > 256u) return false;
    if (numSq == 0u) {
        // Default squeeze pyramid (ISO/IEC 18181-1 Clause H.6.1)
        uint32_t first = nbMetaChannels;
        uint32_t count = static_cast<uint32_t>(channels.size()) - first;
        if (count == 0u) return true;
        uint32_t w = channels[first].w;
        uint32_t h = channels[first].h;
        if (count > 2u && channels[first + 1u].w == w && channels[first + 1u].h == h) {
            // Squeeze chroma (channels first+1, first+2) once horizontally & vertically first
            tr.squeezes.push_back({ true,  false, first + 1u, 2u });
            tr.squeezes.push_back({ false, false, first + 1u, 2u });
        }
        bool horiz = (w >= h);
        while (w > 8u || h > 8u) {
            if (horiz) {
                if (w > 8u) {
                    tr.squeezes.push_back({ true, true, first, count });
                    w = (w + 1u) >> 1u;
                }
            } else {
                if (h > 8u) {
                    tr.squeezes.push_back({ false, true, first, count });
                    h = (h + 1u) >> 1u;
                }
            }
            horiz = !horiz;
        }
    } else {
        for (uint32_t i = 0; i < numSq; ++i) {
            JxlSqueezeStep st;
            st.horizontal = br.ReadBits(1u) != 0u;
            st.inPlace    = br.ReadBits(1u) != 0u;
            st.beginC     = rdU32(0, 3, 8, 6, 72, 10, 1096, 13);
            st.numC       = rdU32(1, 0, 2, 0, 3, 0, 4, 4);
            if (br.eof) return false;
            tr.squeezes.push_back(st);
        }
    }

    // Apply forward Squeeze channel splitting to metadata dimensions
    for (const auto& st : tr.squeezes) {
        if (st.beginC < nbMetaChannels || st.beginC + st.numC > channels.size()) return false;
        uint32_t resPos = st.inPlace ? (st.beginC + st.numC) : static_cast<uint32_t>(channels.size());
        std::vector<JxlChannel> resChs;
        resChs.reserve(st.numC);
        for (uint32_t k = 0; k < st.numC; ++k) {
            JxlChannel& src = channels[st.beginC + k];
            JxlChannel res;
            if (st.horizontal) {
                uint32_t origW = src.w;
                src.w = (origW + 1u) >> 1u;
                res.w = origW >> 1u;
                res.h = src.h;
                res.hshift = src.hshift + 1;
                res.vshift = src.vshift;
                src.hshift += 1;
            } else {
                uint32_t origH = src.h;
                src.h = (origH + 1u) >> 1u;
                res.w = src.w;
                res.h = origH >> 1u;
                res.hshift = src.hshift;
                res.vshift = src.vshift + 1;
                src.vshift += 1;
            }
            resChs.push_back(std::move(res));
        }
        channels.insert(channels.begin() + resPos, resChs.begin(), resChs.end());
    }
    return true;
}

inline int64_t SmoothTendency(int64_t B, int64_t a, int64_t N) {
    int64_t diff = 0;
    if (B >= a && a >= N) {
        diff = (4 * B - 3 * N - a + 6) / 12;
        if (diff - (diff & 1) > 2 * (B - a)) diff = 2 * (B - a) + 1;
        if (diff + (diff & 1) > 2 * (a - N)) diff = 2 * (a - N);
    } else if (B <= a && a <= N) {
        diff = (4 * B - 3 * N - a - 6) / 12;
        if (diff + (diff & 1) < 2 * (B - a)) diff = 2 * (B - a) - 1;
        if (diff - (diff & 1) < 2 * (a - N)) diff = 2 * (a - N);
    }
    return diff;
}

inline bool UndoModularTransform(
    std::vector<JxlChannel>& channels,
    const JxlTransform& tr,
    uint32_t bitDepth)
{
    if (tr.trId == 0u) { // Inverse RCT
        if (tr.beginC + 3u > channels.size()) return false;
        auto& c0 = channels[tr.beginC + 0u].pixels;
        auto& c1 = channels[tr.beginC + 1u].pixels;
        auto& c2 = channels[tr.beginC + 2u].pixels;
        size_t n = c0.size();
        if (c1.size() != n || c2.size() != n) return false;
        uint32_t op = tr.rctType % 7u;
        uint32_t perm = tr.rctType / 7u;
        uint32_t d0 = perm % 3u;
        uint32_t d1 = (perm + 1u + (perm / 3u)) % 3u;
        uint32_t d2 = (perm + 2u - (perm / 3u)) % 3u;
        std::vector<int32_t>* dst[3] = { &c0, &c1, &c2 };

        for (size_t i = 0; i < n; ++i) {
            int64_t v0 = c0[i], v1 = c1[i], v2 = c2[i];
            int64_t o0 = v0, o1 = v1, o2 = v2;
            if (op == 1u) { o2 = v2 + v0; }
            else if (op == 2u) { o1 = v1 + v0; }
            else if (op == 3u) { o1 = v1 + v0; o2 = v2 + v0; }
            else if (op == 4u) { o1 = v1 + ((v0 + v2) >> 1); }
            else if (op == 5u) { o2 = v2 + v0; o1 = v1 + ((v0 + o2) >> 1); }
            else if (op == 6u) { int64_t t = v0 - (v2 >> 1); o1 = v2 + t; o2 = t - (v1 >> 1); o0 = o2 + v1; }
            (*dst[d0])[i] = static_cast<int32_t>(o0);
            (*dst[d1])[i] = static_cast<int32_t>(o1);
            (*dst[d2])[i] = static_cast<int32_t>(o2);
        }
        return true;
    }

    if (tr.trId == 1u) { // Inverse Palette
        if (channels.size() < 2u || tr.beginC + 1u >= channels.size()) return false;
        JxlChannel pal = std::move(channels.front());
        channels.erase(channels.begin());
        JxlChannel idxCh = std::move(channels[tr.beginC]);
        const uint32_t w = idxCh.w, h = idxCh.h;
        std::vector<JxlChannel> restored(tr.numC);
        for (uint32_t c = 0; c < tr.numC; ++c) {
            restored[c].w = w;
            restored[c].h = h;
            restored[c].hshift = idxCh.hshift;
            restored[c].vshift = idxCh.vshift;
            restored[c].decoded = true;
            restored[c].pixels.assign(static_cast<size_t>(w) * h, 0);
        }

        static constexpr int16_t kCubeDeltas[72][3] = {
            {0,0,0},{4,4,4},{11,0,0},{0,0,-13},{0,-12,0},{-10,-10,-10},{-18,-18,-18},{-27,-27,-27},
            {-18,-18,0},{0,0,-32},{-32,0,0},{-37,-37,-37},{0,-32,-32},{24,24,45},{50,50,50},{-45,-24,-24},
            {-24,-45,-45},{0,-24,-24},{-34,-34,0},{-24,0,-24},{-45,-45,-24},{64,64,64},{-32,0,-32},{-24,-24,-45},
            {0,-32,0},{-12,0,0},{24,45,45},{-45,-45,-64},{0,-32,32},{-32,-32,0},{0,0,24},{0,24,24},
            {45,24,24},{0,32,0},{24,0,0},{0,0,45},{45,45,24},{32,32,0},{64,64,45},{0,45,45},
            {24,24,0},{32,0,32},{-64,-64,-64},{45,24,45},{-45,-24,-45},{0,-45,-45},{-24,-45,-24},{32,0,0},
            {0,32,32},{45,64,64},{0,0,32},{0,24,0},{37,37,37},{27,27,27},{18,18,18},{10,10,10},
            {0,12,0},{0,0,13},{-11,0,0},{-4,-4,-4},{45,45,64},{-24,0,0},{0,-24,0},{0,0,-24},
            {-45,-64,-64},{64,45,45},{-64,-45,-45},{24,45,24},{-24,-24,0},{0,45,0},{45,0,45},{-45,0,-45}
        };

        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                size_t p = static_cast<size_t>(y) * w + x;
                int32_t index = idxCh.pixels[p];
                for (uint32_t c = 0; c < tr.numC; ++c) {
                    int64_t val = 0;
                    if (index >= 0 && static_cast<uint32_t>(index) < pal.w) {
                        val = pal.pixels[static_cast<size_t>(c) * pal.w + static_cast<uint32_t>(index)];
                    } else if (index >= static_cast<int32_t>(pal.w)) {
                        uint32_t uIdx = static_cast<uint32_t>(index) - pal.w;
                        if (uIdx < 72u && c < 3u) {
                            val = (static_cast<int64_t>(kCubeDeltas[uIdx][c]) * ((1ll << bitDepth) - 1ll)) >> 7;
                        } else {
                            uint32_t qIdx = uIdx - 72u;
                            uint32_t step = (c == 0u) ? (qIdx % 5u) : ((c == 1u) ? ((qIdx / 5u) % 5u) : ((qIdx / 25u) % 5u));
                            val = (static_cast<int64_t>(step) * ((1ll << bitDepth) - 1ll) + 2ll) / 4ll;
                        }
                    } else {
                        uint32_t dIdx = static_cast<uint32_t>(-1 - index);
                        val = (c < 3u && dIdx < 72u) ? kCubeDeltas[dIdx][c] : 0;
                    }
                    if (index < static_cast<int32_t>(tr.nbDeltas)) {
                        auto& rpx = restored[c].pixels;
                        int64_t W  = (x > 0u) ? rpx[p - 1u] : (y > 0u ? rpx[p - w] : 0);
                        int64_t N  = (y > 0u) ? rpx[p - w] : W;
                        int64_t NW = (x > 0u && y > 0u) ? rpx[p - w - 1u] : W;
                        int64_t NE = (x + 1u < w && y > 0u) ? rpx[p - w + 1u] : N;
                        int64_t NN = (y > 1u) ? rpx[p - 2u * w] : N;
                        int64_t WW = (x > 1u) ? rpx[p - 2u] : W;
                        val += PredictModularSample(static_cast<uint8_t>(tr.dPred), W, N, NW, NE, NN, WW, NE, (W + N) >> 1);
                    }
                    restored[c].pixels[p] = static_cast<int32_t>(val);
                }
            }
        }
        channels.erase(channels.begin() + tr.beginC);
        channels.insert(channels.begin() + tr.beginC, restored.begin(), restored.end());
        return true;
    }

    // Inverse Squeeze (reverse order of squeeze steps)
    for (int si = static_cast<int>(tr.squeezes.size()) - 1; si >= 0; --si) {
        const auto& st = tr.squeezes[si];
        uint32_t resPos = st.inPlace ? (st.beginC + st.numC) : static_cast<uint32_t>(channels.size() - st.numC);
        if (st.beginC + st.numC > resPos || resPos + st.numC > channels.size()) return false;
        for (uint32_t k = 0; k < st.numC; ++k) {
            JxlChannel& avg = channels[st.beginC + k];
            JxlChannel& res = channels[resPos + k];
            if (st.horizontal) {
                uint32_t outW = avg.w + res.w;
                uint32_t outH = avg.h;
                if (res.h != outH) return false;
                std::vector<int32_t> dst(static_cast<size_t>(outW) * outH, 0);
                for (uint32_t y = 0; y < outH; ++y) {
                    for (uint32_t x = 0; x < res.w; ++x) {
                        int64_t a  = avg.pixels[static_cast<size_t>(y) * avg.w + x];
                        int64_t B  = (x > 0u) ? avg.pixels[static_cast<size_t>(y) * avg.w + x - 1u] : a;
                        int64_t Nv = (x + 1u < avg.w) ? avg.pixels[static_cast<size_t>(y) * avg.w + x + 1u] : a;
                        int64_t d  = res.pixels[static_cast<size_t>(y) * res.w + x] + SmoothTendency(B, a, Nv);
                        int64_t p1 = a - (d >> 1);
                        int64_t p0 = p1 + d;
                        dst[static_cast<size_t>(y) * outW + 2u * x]      = static_cast<int32_t>(p0);
                        dst[static_cast<size_t>(y) * outW + 2u * x + 1u] = static_cast<int32_t>(p1);
                    }
                    if ((outW & 1u) != 0u) {
                        dst[static_cast<size_t>(y) * outW + outW - 1u] = avg.pixels[static_cast<size_t>(y) * avg.w + avg.w - 1u];
                    }
                }
                avg.w = outW;
                avg.hshift -= 1;
                avg.pixels = std::move(dst);
            } else {
                uint32_t outW = avg.w;
                uint32_t outH = avg.h + res.h;
                if (res.w != outW) return false;
                std::vector<int32_t> dst(static_cast<size_t>(outW) * outH, 0);
                for (uint32_t y = 0; y < res.h; ++y) {
                    for (uint32_t x = 0; x < outW; ++x) {
                        int64_t a  = avg.pixels[static_cast<size_t>(y) * outW + x];
                        int64_t B  = (y > 0u) ? avg.pixels[static_cast<size_t>(y - 1u) * outW + x] : a;
                        int64_t Nv = (y + 1u < avg.h) ? avg.pixels[static_cast<size_t>(y + 1u) * outW + x] : a;
                        int64_t d  = res.pixels[static_cast<size_t>(y) * outW + x] + SmoothTendency(B, a, Nv);
                        int64_t p1 = a - (d >> 1);
                        int64_t p0 = p1 + d;
                        dst[static_cast<size_t>(2u * y) * outW + x]      = static_cast<int32_t>(p0);
                        dst[static_cast<size_t>(2u * y + 1u) * outW + x] = static_cast<int32_t>(p1);
                    }
                }
                if ((outH & 1u) != 0u) {
                    for (uint32_t x = 0; x < outW; ++x) {
                        dst[static_cast<size_t>(outH - 1u) * outW + x] = avg.pixels[static_cast<size_t>(avg.h - 1u) * outW + x];
                    }
                }
                avg.h = outH;
                avg.vshift -= 1;
                avg.pixels = std::move(dst);
            }
        }
        channels.erase(channels.begin() + resPos, channels.begin() + resPos + st.numC);
    }
    return true;
}

inline bool DecodeModularChannelsInStream(
    DetailBrotli::BitReader& br,
    std::vector<JxlChannel*>& streamChs,
    uint32_t streamId,
    bool hasGlobalTree,
    const std::vector<JxlMaNode>& globalTree,
    const JxlHistogramSet& globalHist,
    bool headerAlreadyRead,
    bool useGlobalTreeIn,
    const JxlWpHeader& wpIn)
{
    if (streamChs.empty()) return true;

    bool useGlobalTree = useGlobalTreeIn;
    JxlWpHeader wp = wpIn;
    std::vector<JxlTransform> localTransforms;
    uint32_t localMeta = 0u;
    std::vector<JxlChannel> workingChs;

    if (!headerAlreadyRead) {
        useGlobalTree = br.ReadBits(1u) != 0u;
        if (br.eof || !ReadWpHeader(br, wp)) return false;
        auto rdU32 = [&](uint32_t c0, uint32_t u0, uint32_t c1, uint32_t u1,
                         uint32_t c2, uint32_t u2, uint32_t c3, uint32_t u3) -> uint32_t {
            uint32_t sel = br.ReadBits(2u);
            if (sel == 0u) return c0 + br.ReadBits(u0);
            if (sel == 1u) return c1 + br.ReadBits(u1);
            if (sel == 2u) return c2 + br.ReadBits(u2);
            return c3 + br.ReadBits(u3);
        };
        uint32_t nbTr = rdU32(0, 0, 1, 0, 2, 4, 18, 8);
        if (br.eof || nbTr > 64u) return false;
        workingChs.resize(streamChs.size());
        for (size_t i = 0; i < streamChs.size(); ++i) {
            workingChs[i].w = streamChs[i]->w;
            workingChs[i].h = streamChs[i]->h;
            workingChs[i].hshift = streamChs[i]->hshift;
            workingChs[i].vshift = streamChs[i]->vshift;
        }
        localTransforms.resize(nbTr);
        for (uint32_t t = 0; t < nbTr; ++t) {
            if (!ReadAndApplyTransformHeader(br, workingChs, localMeta, localTransforms[t])) return false;
        }
    }

    std::vector<JxlMaNode> localTree;
    JxlHistogramSet localHist;
    const std::vector<JxlMaNode>* treePtr = nullptr;
    JxlHistogramSet activeHist;

    if (useGlobalTree) {
        if (!hasGlobalTree) return false;
        treePtr = &globalTree;
        activeHist = globalHist;
    } else {
        uint32_t numLeaves = 0u;
        if (!DecodeMaTree(br, 1u << 20u, localTree, numLeaves) ||
            !DecodeJxlHistograms(br, numLeaves, localHist)) {
            return false;
        }
        treePtr = &localTree;
        activeHist = std::move(localHist);
    }

    bool treeNeedsWp = false;
    for (const auto& nd : *treePtr) {
        if ((nd.prop == -1 && nd.predictor == 6u) || nd.prop == 15) {
            treeNeedsWp = true;
            break;
        }
    }

    size_t numTargets = headerAlreadyRead ? streamChs.size() : workingChs.size();
    std::vector<JxlChannel*> prevDecoded;
    prevDecoded.reserve(numTargets);

    for (size_t ci = 0; ci < numTargets; ++ci) {
        JxlChannel* ch = headerAlreadyRead ? streamChs[ci] : &workingChs[ci];
        const uint32_t cw = ch->w;
        const uint32_t chH = ch->h;
        ch->pixels.assign(static_cast<size_t>(cw) * chH, 0);
        if (cw == 0u || chH == 0u) {
            ch->decoded = true;
            continue;
        }

        // Collect earlier channels with matching dimensions/shifts for properties >= 16 (reverse order per spec)
        std::vector<const JxlChannel*> matchPrev;
        for (int pi = static_cast<int>(prevDecoded.size()) - 1; pi >= 0; --pi) {
            if (prevDecoded[pi]->w == cw && prevDecoded[pi]->h == chH &&
                prevDecoded[pi]->hshift == ch->hshift && prevDecoded[pi]->vshift == ch->vshift) {
                matchPrev.push_back(prevDecoded[pi]);
            }
        }

        // Optional WP error rows (5 sub-error buffers: true error + 4 sub-predictor absolute errors)
        std::vector<int64_t> wpTrueErrPrev(cw + 2u, 0), wpTrueErrCur(cw + 2u, 0);
        std::vector<uint64_t> wpSubErrPrev[4], wpSubErrCur[4];
        if (treeNeedsWp) {
            for (int k = 0; k < 4; ++k) {
                wpSubErrPrev[k].assign(cw + 2u, 0u);
                wpSubErrCur[k].assign(cw + 2u, 0u);
            }
        }

        static constexpr uint32_t kWpDivLut[64] = {
            65536, 32768, 21845, 16384, 13107, 10922, 9362, 8192,
            7281,  6553,  5957,  5461,  5041,  4681,  4369, 4096,
            3855,  3640,  3449,  3276,  3120,  2978,  2849, 2730,
            2621,  2520,  2427,  2340,  2259,  2184,  2114, 2048,
            1985,  1927,  1872,  1820,  1771,  1724,  1680, 1638,
            1598,  1560,  1524,  1489,  1456,  1424,  1394, 1365,
            1337,  1310,  1285,  1260,  1236,  1213,  1191, 1170,
            1149,  1129,  1110,  1092,  1074,  1057,  1040, 1024
        };

        std::vector<int32_t> props(16u + matchPrev.size() * 4u, 0);
        props[0] = static_cast<int32_t>(ci);
        props[1] = static_cast<int32_t>(streamId);

        for (uint32_t y = 0; y < chH; ++y) {
            props[2] = static_cast<int32_t>(y);
            int64_t prevGradResidualW = 0;
            for (uint32_t x = 0; x < cw; ++x) {
                props[3] = static_cast<int32_t>(x);
                size_t idx = static_cast<size_t>(y) * cw + x;
                int64_t W   = (x > 0u) ? ch->pixels[idx - 1u] : (y > 0u ? ch->pixels[idx - cw] : 0);
                int64_t N   = (y > 0u) ? ch->pixels[idx - cw] : W;
                int64_t NW  = (x > 0u && y > 0u) ? ch->pixels[idx - cw - 1u] : W;
                int64_t NE  = (x + 1u < cw && y > 0u) ? ch->pixels[idx - cw + 1u] : N;
                int64_t NN  = (y > 1u) ? ch->pixels[idx - 2u * cw] : N;
                int64_t WW  = (x > 1u) ? ch->pixels[idx - 2u] : W;
                int64_t NEE = (x + 2u < cw && y > 0u) ? ch->pixels[idx - cw + 2u] : NE;

                props[4]  = static_cast<int32_t>(std::abs(N));
                props[5]  = static_cast<int32_t>(std::abs(W));
                props[6]  = static_cast<int32_t>(N);
                props[7]  = static_cast<int32_t>(W);
                props[8]  = static_cast<int32_t>((x > 0u) ? prevGradResidualW : W);
                props[9]  = static_cast<int32_t>(W + N - NW);
                props[10] = static_cast<int32_t>(W - NW);
                props[11] = static_cast<int32_t>(NW - N);
                props[12] = static_cast<int32_t>(N - NE);
                props[13] = static_cast<int32_t>(N - NN);
                props[14] = static_cast<int32_t>(W - WW);

                int64_t wpPred = (W + N) >> 1;
                int64_t wpSubPreds[4] = {};
                if (treeNeedsWp) {
                    int64_t eN  = (y > 0u) ? wpTrueErrPrev[x + 1u] : 0;
                    int64_t eW  = (x > 0u) ? wpTrueErrCur[x] : 0;
                    int64_t eNW = (x > 0u && y > 0u) ? wpTrueErrPrev[x] : 0;
                    int64_t eNE = (x + 1u < cw && y > 0u) ? wpTrueErrPrev[x + 2u] : eN;
                    // Exact ISO 18181-1 WP fixed-point (3 fractional bits):
                    int64_t N3 = N << 3, W3 = W << 3, NW3 = NW << 3, NE3 = NE << 3, NN3 = NN << 3, WW3 = WW << 3;
                    wpSubPreds[0] = W3 + NE3 - N3;
                    wpSubPreds[1] = N3 - (((eW + eN + eNE) * static_cast<int64_t>(wp.p1c)) >> 5);
                    wpSubPreds[2] = W3 - (((eW + eN + eNW) * static_cast<int64_t>(wp.p2c)) >> 5);
                    wpSubPreds[3] = N3 - ((eNW * static_cast<int64_t>(wp.p3a) +
                                           eN  * static_cast<int64_t>(wp.p3b) +
                                           eNE * static_cast<int64_t>(wp.p3c) +
                                           (NN3 - N3) * static_cast<int64_t>(wp.p3d) +
                                           (NW3 - W3) * static_cast<int64_t>(wp.p3e)) >> 5);
                    (void)WW3;
                    uint64_t wts[4] = {};
                    uint64_t sumW = 0u;
                    for (int k = 0; k < 4; ++k) {
                        uint64_t errSum = wpSubErrCur[k][x] + wpSubErrPrev[k][x + 1u] + wpSubErrPrev[k][x] + wpSubErrPrev[k][x + 2u];
                        uint32_t shift = static_cast<uint32_t>(std::min<uint64_t>(31u, DetailBrotli::CeilLog2(static_cast<uint32_t>(std::min<uint64_t>(errSum >> 5u, 0x7FFFFFFFull)) + 1u)));
                        uint32_t idxDiv = static_cast<uint32_t>(std::min<uint64_t>(63u, (errSum >> shift) & 63u));
                        wts[k] = 1ull + (((static_cast<uint64_t>(wp.w[k]) + 1ull) * kWpDivLut[idxDiv]) >> (shift + 1u));
                        sumW += wts[k];
                    }
                    uint32_t logW = (sumW > 1u) ? (DetailBrotli::CeilLog2(static_cast<uint32_t>(std::min<uint64_t>(sumW, 0x7FFFFFFFull))) - 1u) : 0u;
                    if (logW > 4u) {
                        uint32_t rsh = logW - 4u;
                        sumW = 0u;
                        for (int k = 0; k < 4; ++k) { wts[k] = (wts[k] >> rsh) + 1ull; sumW += wts[k]; }
                    }
                    int64_t s = static_cast<int64_t>(sumW >> 1u) - 1;
                    for (int k = 0; k < 4; ++k) s += wpSubPreds[k] * static_cast<int64_t>(wts[k]);
                    int64_t predFp = (s * static_cast<int64_t>(kWpDivLut[std::min<uint64_t>(63u, sumW - 1u)])) >> 16;
                    if (((eN ^ eW) | (eN ^ eNW)) <= 0) {
                        int64_t lo = std::min({ W3, N3, NE3 });
                        int64_t hi = std::max({ W3, N3, NE3 });
                        predFp = std::clamp(predFp, lo, hi);
                    }
                    props[15] = static_cast<int32_t>(eW);
                    wpPred = (predFp + 3 + (predFp < 0 ? 1 : 0)) >> 3;
                }

                for (size_t pi = 0; pi < matchPrev.size(); ++pi) {
                    const auto& pp = matchPrev[pi]->pixels;
                    int64_t cVal = pp[idx];
                    int64_t cW  = (x > 0u) ? pp[idx - 1u] : (y > 0u ? pp[idx - cw] : 0);
                    int64_t cN  = (y > 0u) ? pp[idx - cw] : cW;
                    int64_t cNW = (x > 0u && y > 0u) ? pp[idx - cw - 1u] : cW;
                    int64_t cGrad = std::clamp<int64_t>(cW + cN - cNW, std::min(cW, cN), std::max(cW, cN));
                    size_t baseP = 16u + pi * 4u;
                    props[baseP + 0u] = static_cast<int32_t>(std::abs(cVal));
                    props[baseP + 1u] = static_cast<int32_t>(cVal);
                    props[baseP + 2u] = static_cast<int32_t>(std::abs(cVal - cGrad));
                    props[baseP + 3u] = static_cast<int32_t>(cVal - cGrad);
                }

                // Traverse MA tree
                uint32_t nodeIdx = 0u;
                while ((*treePtr)[nodeIdx].prop >= 0) {
                    const auto& nd = (*treePtr)[nodeIdx];
                    int32_t pv = (static_cast<size_t>(nd.prop) < props.size()) ? props[nd.prop] : 0;
                    nodeIdx = (pv > nd.splitVal) ? nd.left : nd.right;
                }
                const auto& leaf = (*treePtr)[nodeIdx];
                uint32_t sym = 0u;
                if (!ReadJxlSymbol(br, activeHist, leaf.leafCtx, sym)) return false;

                int64_t diff = static_cast<int64_t>(UnpackSigned(sym));
                int64_t predVal = PredictModularSample(leaf.predictor, W, N, NW, NE, NN, WW, NEE, wpPred);
                int64_t sample = diff * static_cast<int64_t>(leaf.multiplier) + static_cast<int64_t>(leaf.offset) + predVal;
                ch->pixels[idx] = static_cast<int32_t>(sample);
                prevGradResidualW = sample - (W + N - NW);

                if (treeNeedsWp) {
                    int64_t trueFp = sample << 3;
                    wpTrueErrCur[x + 1u] = (wpPred << 3) - trueFp;
                    for (int k = 0; k < 4; ++k) {
                        wpSubErrCur[k][x + 1u] = static_cast<uint64_t>(std::abs(wpSubPreds[k] - trueFp));
                    }
                }
            }
            if (treeNeedsWp) {
                wpTrueErrPrev.swap(wpTrueErrCur);
                std::fill(wpTrueErrCur.begin(), wpTrueErrCur.end(), 0);
                for (int k = 0; k < 4; ++k) {
                    wpSubErrPrev[k].swap(wpSubErrCur[k]);
                    std::fill(wpSubErrCur[k].begin(), wpSubErrCur[k].end(), 0u);
                }
            }
        }
        ch->decoded = true;
        prevDecoded.push_back(ch);
    }

    if (activeHist.useAns && activeHist.ansStateInitialized && activeHist.ansState != 0x00130000u) {
        return false;
    }

    if (!headerAlreadyRead) {
        for (int ti = static_cast<int>(localTransforms.size()) - 1; ti >= 0; --ti) {
            if (!UndoModularTransform(workingChs, localTransforms[ti], 8u)) return false;
        }
        if (workingChs.size() != streamChs.size()) return false;
        for (size_t i = 0; i < streamChs.size(); ++i) {
            streamChs[i]->pixels = std::move(workingChs[i].pixels);
            streamChs[i]->decoded = true;
        }
    }
    return true;
}

#include "JxlVarDct.h"

inline bool DecodeJxlCodestreamModular(const uint8_t* cs, size_t csLen, ExtendedImageInfo& outInfo) {
    if (!cs || csLen < 5u || cs[0] != 0xFFu || cs[1] != 0x0Au) return false;

    DetailBrotli::BitReader br(cs, csLen);
    br.DropBits(16u); // Skip 0xFF 0x0A

    auto rdU32 = [&](uint32_t c0, uint32_t u0, uint32_t c1, uint32_t u1,
                     uint32_t c2, uint32_t u2, uint32_t c3, uint32_t u3) -> uint32_t {
        uint32_t sel = br.ReadBits(2u);
        if (sel == 0u) return c0 + br.ReadBits(u0);
        if (sel == 1u) return c1 + br.ReadBits(u1);
        if (sel == 2u) return c2 + br.ReadBits(u2);
        return c3 + br.ReadBits(u3);
    };
    auto rdEnum = [&]() -> uint32_t {
        return rdU32(0, 0, 1, 0, 2, 4, 18, 6);
    };
    auto rdU64 = [&]() -> uint64_t {
        uint32_t sel = br.ReadBits(2u);
        if (sel == 0u) return 0ull;
        if (sel == 1u) return 1ull + br.ReadBits(4u);
        if (sel == 2u) return 17ull + br.ReadBits(8u);
        uint64_t val = br.ReadBits(12u);
        uint32_t shift = 12u;
        while (br.ReadBits(1u) == 1u && !br.eof) {
            if (shift == 60u) {
                val |= static_cast<uint64_t>(br.ReadBits(4u)) << 60u;
                break;
            }
            val |= static_cast<uint64_t>(br.ReadBits(8u)) << shift;
            shift += 8u;
        }
        return val;
    };
    auto parseSizeHeader = [&](uint32_t& outW, uint32_t& outH) -> bool {
        bool div8 = br.ReadBits(1u) != 0u;
        outH = div8 ? ((br.ReadBits(5u) + 1u) * 8u) : rdU32(1, 9, 1, 13, 1, 18, 1, 30);
        uint32_t ratio = br.ReadBits(3u);
        if (ratio == 0u)      outW = div8 ? ((br.ReadBits(5u) + 1u) * 8u) : rdU32(1, 9, 1, 13, 1, 18, 1, 30);
        else if (ratio == 1u) outW = outH;
        else if (ratio == 2u) outW = static_cast<uint32_t>((static_cast<uint64_t>(outH) * 12ull) / 10ull);
        else if (ratio == 3u) outW = static_cast<uint32_t>((static_cast<uint64_t>(outH) * 4ull) / 3ull);
        else if (ratio == 4u) outW = static_cast<uint32_t>((static_cast<uint64_t>(outH) * 3ull) / 2ull);
        else if (ratio == 5u) outW = static_cast<uint32_t>((static_cast<uint64_t>(outH) * 16ull) / 9ull);
        else if (ratio == 6u) outW = static_cast<uint32_t>((static_cast<uint64_t>(outH) * 5ull) / 4ull);
        else                  outW = outH * 2u;
        return !br.eof && outW >= 1u && outH >= 1u && outW <= 16384u && outH <= 16384u;
    };

    uint32_t xsize = 0u, ysize = 0u;
    if (!parseSizeHeader(xsize, ysize)) return false;

    // ImageMetadata
    uint32_t orientation = 1u;
    bool havePreview = false;
    bool haveAnimation = false;
    uint32_t bitsPerSample = 8u;
    bool fpSample = false;
    uint32_t numExtraChannels = 0u;
    struct EcMeta { uint32_t type = 0; uint32_t bits = 8; uint32_t dimShift = 0; bool alphaAssoc = false; };
    std::vector<EcMeta> ecInfos;
    bool xybEncoded = true;
    uint32_t colourSpace = 0u; // 0=RGB, 1=Grey, 2=XYB, 3=Unknown
    uint32_t transferFn = 13u; // 13=sRGB, 8=Linear, 16=PQ
    bool wantIcc = false;

    bool allDefaultMeta = br.ReadBits(1u) != 0u;
    if (br.eof) return false;
    if (!allDefaultMeta) {
        bool extraFields = br.ReadBits(1u) != 0u;
        if (extraFields) {
            orientation = 1u + br.ReadBits(3u);
            if (br.ReadBits(1u) != 0u) {
                uint32_t iw = 0u, ih = 0u;
                if (!parseSizeHeader(iw, ih)) return false;
            }
            havePreview = br.ReadBits(1u) != 0u;
            if (havePreview) {
                bool pdiv8 = br.ReadBits(1u) != 0u;
                (void)(pdiv8 ? ((br.ReadBits(5u) + 1u) * 8u) : rdU32(1, 6, 65, 8, 321, 10, 1345, 14));
                if (br.ReadBits(3u) == 0u) {
                    (void)(pdiv8 ? ((br.ReadBits(5u) + 1u) * 8u) : rdU32(1, 6, 65, 8, 321, 10, 1345, 14));
                }
            }
            haveAnimation = br.ReadBits(1u) != 0u;
            if (haveAnimation) {
                (void)rdU32(100, 0, 1000, 0, 1, 10, 1, 30);
                (void)rdU32(1, 0, 1001, 0, 1, 8, 1, 10);
                (void)rdU32(0, 0, 0, 3, 0, 16, 0, 32);
                (void)br.ReadBits(1u);
            }
        }
        fpSample = br.ReadBits(1u) != 0u;
        if (fpSample) return false; // Modular integer bitstreams expected in fallback
        bitsPerSample = rdU32(8, 0, 10, 0, 12, 0, 1, 6);
        if (bitsPerSample == 0u || bitsPerSample > 16u) return false;
        (void)br.ReadBits(1u); // modular_16_bit_buffer_sufficient

        numExtraChannels = rdU32(0, 0, 1, 0, 2, 4, 1, 12);
        if (br.eof || numExtraChannels > 16u) return false;
        ecInfos.resize(numExtraChannels);
        for (uint32_t ec = 0; ec < numExtraChannels; ++ec) {
            bool dAlpha = br.ReadBits(1u) != 0u;
            if (!dAlpha) {
                ecInfos[ec].type = rdEnum();
                bool ecFp = br.ReadBits(1u) != 0u;
                if (ecFp) return false;
                ecInfos[ec].bits = rdU32(8, 0, 10, 0, 12, 0, 1, 6);
                ecInfos[ec].dimShift = rdU32(0, 0, 3, 0, 4, 0, 1, 3);
                uint32_t nameLen = rdU32(0, 0, 0, 4, 16, 5, 48, 10);
                for (uint32_t ni = 0; ni < nameLen && !br.eof; ++ni) (void)br.ReadBits(8u);
                if (ecInfos[ec].type == 0u) {
                    ecInfos[ec].alphaAssoc = br.ReadBits(1u) != 0u;
                } else if (ecInfos[ec].type == 2u) {
                    for (int k = 0; k < 4; ++k) (void)br.ReadBits(16u);
                } else if (ecInfos[ec].type == 5u) {
                    (void)rdU32(1, 0, 0, 2, 3, 4, 19, 8);
                }
            }
            if (br.eof) return false;
        }

        xybEncoded = br.ReadBits(1u) != 0u;

        // ColourEncoding
        bool ceAllDefault = br.ReadBits(1u) != 0u;
        if (!ceAllDefault) {
            wantIcc = br.ReadBits(1u) != 0u;
            colourSpace = rdEnum();
            if (!wantIcc && colourSpace != 2u) {
                if (colourSpace != 1u) {
                    uint32_t wp = rdEnum();
                    if (wp == 2u) {
                        (void)rdU32(0, 19, 524288, 19, 1048576, 20, 2097152, 21);
                        (void)rdU32(0, 19, 524288, 19, 1048576, 20, 2097152, 21);
                    }
                    uint32_t prim = rdEnum();
                    if (prim == 2u) {
                        for (int k = 0; k < 6; ++k) (void)rdU32(0, 19, 524288, 19, 1048576, 20, 2097152, 21);
                    }
                }
                bool haveGamma = br.ReadBits(1u) != 0u;
                if (haveGamma) (void)br.ReadBits(24u);
                else transferFn = rdEnum();
                (void)rdEnum(); // rendering_intent
            }
        }
        if (extraFields) {
            bool tmDefault = br.ReadBits(1u) != 0u;
            if (!tmDefault) {
                (void)br.ReadBits(16u);
                (void)br.ReadBits(16u);
                (void)br.ReadBits(1u);
                (void)br.ReadBits(16u);
            }
        }
        uint64_t extMask = rdU64();
        if (extMask != 0ull) return false;
    }

    // TransformData (default_m + custom OpsinInverseMatrix)
    JxlOpsinParams opsinParams;
    bool defaultM = br.ReadBits(1u) != 0u;
    if (br.eof) return false;
    if (!defaultM) {
        if (xybEncoded) {
            for (int k = 0; k < 9; ++k) {
                if (!ReadJxlF16(br, opsinParams.invMat[k])) return false;
            }
            for (int k = 0; k < 3; ++k) {
                if (!ReadJxlF16(br, opsinParams.opsinBias[k])) return false;
                float b = opsinParams.opsinBias[k];
                opsinParams.cbrtBias[k] = (b >= 0.0f) ? std::cbrt(b) : -std::cbrt(-b);
            }
            for (int k = 0; k < 3; ++k) {
                if (!ReadJxlF16(br, opsinParams.quantBias[k])) return false;
            }
            if (!ReadJxlF16(br, opsinParams.quantBiasNumerator)) return false;
        }
        uint32_t cwMask = br.ReadBits(3u);
        if ((cwMask & 1u) != 0u) { for (int k = 0; k < 15; ++k)  (void)br.ReadBits(16u); }
        if ((cwMask & 2u) != 0u) { for (int k = 0; k < 55; ++k)  (void)br.ReadBits(16u); }
        if ((cwMask & 4u) != 0u) { for (int k = 0; k < 210; ++k) (void)br.ReadBits(16u); }
    }
    if (wantIcc) return false; // Compressed ICC stream handled by WIC fallback when present
    if (!br.AlignToByteZero() || havePreview) return false;

    // FrameHeader (ISO/IEC 18181-1 Clause 6.3)
    uint32_t frameType = 0u;
    uint32_t encoding  = 0u; // 0 = VarDCT, 1 = Modular
    uint64_t fhFlags   = 0ull;
    bool doYcbcr       = false;
    uint32_t jpegUps[3] = { 0u, 0u, 0u };
    uint32_t upsampling = 1u;
    uint32_t groupSizeShift = 1u;
    uint32_t groupDim = 256u;
    uint32_t xQmScale = xybEncoded ? 3u : 2u;
    uint32_t bQmScale = 2u;
    uint32_t numPasses = 1u;
    uint32_t frameW = xsize, frameH = ysize;
    bool gaborish = true;
    float gabWeights[6] = {
        0.115169525f, 0.061248592f,
        0.115169525f, 0.061248592f,
        0.115169525f, 0.061248592f
    };

    bool fhAllDefault = br.ReadBits(1u) != 0u;
    if (br.eof) return false;
    if (!fhAllDefault) {
        frameType = br.ReadBits(2u);
        encoding  = br.ReadBits(1u);
        if (br.eof || (frameType != 0u && frameType != 2u)) return false;
        fhFlags = rdU64();
        doYcbcr = (!xybEncoded) ? (br.ReadBits(1u) != 0u) : false;
        if (doYcbcr && (fhFlags & 0x20ull) == 0ull) {
            jpegUps[0] = br.ReadBits(2u);
            jpegUps[1] = br.ReadBits(2u);
            jpegUps[2] = br.ReadBits(2u);
        }
        upsampling = rdU32(1, 0, 2, 0, 4, 0, 8, 0);
        for (uint32_t ec = 0; ec < numExtraChannels; ++ec) {
            (void)rdU32(1, 0, 2, 0, 4, 0, 8, 0);
        }
        if (encoding == 1u) {
            groupSizeShift = br.ReadBits(2u);
            groupDim = 128u << groupSizeShift;
        } else {
            groupSizeShift = 1u;
            groupDim = 256u;
            if (xybEncoded) {
                xQmScale = br.ReadBits(3u);
                bQmScale = br.ReadBits(3u);
            } else {
                xQmScale = 2u;
                bQmScale = 2u;
            }
        }
        if (br.eof || upsampling != 1u) return false;

        if (frameType != 2u) {
            numPasses = rdU32(1, 0, 2, 0, 3, 0, 4, 3);
            if (numPasses != 1u) return false;
        }
        bool haveCrop = br.ReadBits(1u) != 0u;
        if (haveCrop) {
            (void)rdU32(0, 8, 256, 11, 2304, 14, 18688, 30);
            (void)rdU32(0, 8, 256, 11, 2304, 14, 18688, 30);
            frameW = rdU32(0, 8, 256, 11, 2304, 14, 18688, 30);
            frameH = rdU32(0, 8, 256, 11, 2304, 14, 18688, 30);
            if (frameW == 0u || frameH == 0u || frameW > 16384u || frameH > 16384u) return false;
        }

        bool isLastFrame = true;
        if (frameType == 0u) {
            uint32_t blendMode = rdU32(0, 0, 1, 0, 2, 0, 3, 2);
            if (numExtraChannels > 0u && (blendMode == 2u || blendMode == 3u)) {
                (void)rdU32(0, 0, 1, 0, 2, 0, 3, 3);
            }
            if (haveCrop || blendMode > 0u) (void)br.ReadBits(1u);
            for (uint32_t ec = 0; ec < numExtraChannels; ++ec) {
                uint32_t ebm = rdU32(0, 0, 1, 0, 2, 0, 3, 2);
                if (numExtraChannels > 0u && (ebm == 2u || ebm == 3u)) (void)rdU32(0, 0, 1, 0, 2, 0, 3, 3);
                if (haveCrop || ebm > 0u) (void)br.ReadBits(1u);
            }
            if (haveAnimation) {
                (void)rdU32(0, 0, 1, 0, 0, 8, 0, 32);
            }
            isLastFrame = br.ReadBits(1u) != 0u;
        }
        if (!isLastFrame) {
            uint32_t canRef = br.ReadBits(2u);
            if (canRef != 0u) (void)br.ReadBits(1u);
        } else if (frameType == 2u) {
            (void)br.ReadBits(2u);
            (void)br.ReadBits(1u);
        }

        uint32_t frameNameLen = rdU32(0, 0, 0, 4, 16, 5, 48, 10);
        for (uint32_t ni = 0; ni < frameNameLen && !br.eof; ++ni) (void)br.ReadBits(8u);

        // RestorationFilter
        bool rfAllDefault = br.ReadBits(1u) != 0u;
        if (!rfAllDefault) {
            gaborish = br.ReadBits(1u) != 0u;
            if (gaborish && br.ReadBits(1u) != 0u) {
                for (int k = 0; k < 6; ++k) {
                    if (!ReadJxlF16(br, gabWeights[k])) return false;
                }
            }
            uint32_t epfIters = br.ReadBits(2u);
            if (epfIters != 0u) {
                if (encoding == 0u && br.ReadBits(1u) != 0u) { for (int k = 0; k < 8; ++k) (void)br.ReadBits(16u); }
                if (br.ReadBits(1u) != 0u) {
                    (void)br.ReadBits(16u);
                    (void)br.ReadBits(16u);
                    (void)br.ReadBits(16u);
                }
                if (epfIters == 3u && br.ReadBits(1u) != 0u) { (void)br.ReadBits(16u); (void)br.ReadBits(16u); }
                if (encoding == 1u) (void)br.ReadBits(16u);
            }
            if (rdU64() != 0ull) return false;
        }
        if (rdU64() != 0ull || fhFlags != 0ull) return false;
    }
    if (!br.AlignToByteZero()) return false;

    // TOC (Table of Contents)
    uint32_t numGroupsX   = (frameW + groupDim - 1u) / groupDim;
    uint32_t numGroupsY   = (frameH + groupDim - 1u) / groupDim;
    uint32_t numGroups    = numGroupsX * numGroupsY;
    uint32_t lfGroupDim   = groupDim * 8u;
    uint32_t numLfGroupsX = (frameW + lfGroupDim - 1u) / lfGroupDim;
    uint32_t numLfGroupsY = (frameH + lfGroupDim - 1u) / lfGroupDim;
    uint32_t numLfGroups  = numLfGroupsX * numLfGroupsY;
    uint32_t numTocEntries = (numGroups == 1u && numPasses == 1u) ? 1u : (1u + numLfGroups + 1u + numGroups * numPasses);

    bool permutedToc = br.ReadBits(1u) != 0u;
    if (br.eof || permutedToc) return false;
    std::vector<uint32_t> tocSizes(numTocEntries, 0u);
    for (uint32_t i = 0; i < numTocEntries; ++i) {
        tocSizes[i] = rdU32(0, 10, 1024, 14, 17408, 22, 4211712, 30);
    }
    if (!br.AlignToByteZero()) return false;

    size_t sectionBaseOff = br.bitPos >> 3;
    std::vector<size_t> sectionOffsets(numTocEntries, 0u);
    size_t runOff = sectionBaseOff;
    for (uint32_t i = 0; i < numTocEntries; ++i) {
        if (tocSizes[i] > csLen - runOff) return false;
        sectionOffsets[i] = runOff;
        runOff += tocSizes[i];
    }

    if (encoding == 0u) {
        JxlVarDctFrameParams vfp;
        vfp.frameW = frameW;
        vfp.frameH = frameH;
        vfp.orientation = orientation;
        vfp.bitsPerSample = bitsPerSample;
        vfp.colourSpace = colourSpace;
        vfp.transferFn = transferFn;
        vfp.xybEncoded = xybEncoded;
        vfp.doYcbcr = doYcbcr;
        vfp.xQmScale = xQmScale;
        vfp.bQmScale = bQmScale;
        vfp.gaborish = gaborish;
        for (int k = 0; k < 6; ++k) vfp.gabWeights[k] = gabWeights[k];
        vfp.opsin = opsinParams;
        return DecodeJxlVarDctSections(cs, sectionOffsets, tocSizes, vfp, ecInfos, outInfo);
    }

    // Build initial full-frame channels
    uint32_t numColorCh = (colourSpace == 1u && !xybEncoded) ? 1u : 3u;
    std::vector<JxlChannel> channels(numColorCh + numExtraChannels);
    for (uint32_t c = 0; c < numColorCh; ++c) {
        uint32_t hs = (numColorCh == 3u && (jpegUps[c] == 1u || jpegUps[c] == 2u)) ? 1u : 0u;
        uint32_t vs = (numColorCh == 3u && (jpegUps[c] == 1u || jpegUps[c] == 3u)) ? 1u : 0u;
        channels[c].w = (frameW + (1u << hs) - 1u) >> hs;
        channels[c].h = (frameH + (1u << vs) - 1u) >> vs;
        channels[c].hshift = static_cast<int32_t>(hs);
        channels[c].vshift = static_cast<int32_t>(vs);
    }
    for (uint32_t ec = 0; ec < numExtraChannels; ++ec) {
        uint32_t ds = ecInfos[ec].dimShift;
        channels[numColorCh + ec].w = (frameW + (1u << ds) - 1u) >> ds;
        channels[numColorCh + ec].h = (frameH + (1u << ds) - 1u) >> ds;
        channels[numColorCh + ec].hshift = static_cast<int32_t>(ds);
        channels[numColorCh + ec].vshift = static_cast<int32_t>(ds);
    }

    // Section 0: LfGlobal
    DetailBrotli::BitReader lfBr(cs + sectionOffsets[0], tocSizes[0]);
    float modLfDequant[3] = { 1.0f / 4096.0f, 1.0f / 512.0f, 1.0f / 256.0f };
    bool lfDequantDefault = lfBr.ReadBits(1u) != 0u;
    if (!lfDequantDefault) {
        for (int k = 0; k < 3; ++k) {
            if (!ReadJxlF16(lfBr, modLfDequant[k])) return false;
        }
    }
    bool hasGlobalTree = lfBr.ReadBits(1u) != 0u;
    if (lfBr.eof) return false;

    std::vector<JxlMaNode> globalTree;
    JxlHistogramSet globalHist;
    if (hasGlobalTree) {
        uint32_t gLeaves = 0u;
        if (!DecodeMaTree(lfBr, 1u << 20u, globalTree, gLeaves) ||
            !DecodeJxlHistograms(lfBr, gLeaves, globalHist)) {
            return false;
        }
    }

    // Global Modular Stream Header
    bool gmUseGlobalTree = lfBr.ReadBits(1u) != 0u;
    JxlWpHeader gmWp;
    if (lfBr.eof || !ReadWpHeader(lfBr, gmWp)) return false;
    auto rdLfU32 = [&](uint32_t c0, uint32_t u0, uint32_t c1, uint32_t u1,
                       uint32_t c2, uint32_t u2, uint32_t c3, uint32_t u3) -> uint32_t {
        uint32_t sel = lfBr.ReadBits(2u);
        if (sel == 0u) return c0 + lfBr.ReadBits(u0);
        if (sel == 1u) return c1 + lfBr.ReadBits(u1);
        if (sel == 2u) return c2 + lfBr.ReadBits(u2);
        return c3 + lfBr.ReadBits(u3);
    };
    uint32_t nbGlobalTransforms = rdLfU32(0, 0, 1, 0, 2, 4, 18, 8);
    if (lfBr.eof || nbGlobalTransforms > 64u) return false;

    uint32_t nbMetaChannels = 0u;
    std::vector<JxlTransform> globalTransforms(nbGlobalTransforms);
    for (uint32_t t = 0; t < nbGlobalTransforms; ++t) {
        if (!ReadAndApplyTransformHeader(lfBr, channels, nbMetaChannels, globalTransforms[t])) return false;
    }

    std::vector<JxlChannel*> gmTargets;
    for (size_t i = 0; i < channels.size(); ++i) {
        if (i < nbMetaChannels || (channels[i].w <= groupDim && channels[i].h <= groupDim)) {
            gmTargets.push_back(&channels[i]);
        }
    }
    if (!gmTargets.empty()) {
        if (!DecodeModularChannelsInStream(
                lfBr, gmTargets, 0u, hasGlobalTree, globalTree, globalHist,
                true, gmUseGlobalTree, gmWp)) {
            return false;
        }
    } else if (!gmUseGlobalTree) {
        // Even with 0 global-decoded channels, local tree/histograms are still coded in GlobalModular when !gmUseGlobalTree
        std::vector<JxlMaNode> dummyTree;
        JxlHistogramSet dummyHist;
        uint32_t dummyLeaves = 0u;
        if (!DecodeMaTree(lfBr, 1u << 20u, dummyTree, dummyLeaves) ||
            !DecodeJxlHistograms(lfBr, dummyLeaves, dummyHist)) {
            return false;
        }
    }

    // Allocate pixel buffers for remaining large channels before group stitching
    for (auto& ch : channels) {
        if (!ch.decoded) {
            ch.pixels.assign(static_cast<size_t>(ch.w) * ch.h, 0);
        }
    }

    // Multi-group decoding when numTocEntries > 1
    if (numTocEntries > 1u) {
        // 1. LfGroups (channels with hshift >= 3 && vshift >= 3)
        for (uint32_t lfg = 0; lfg < numLfGroups; ++lfg) {
            uint32_t lgx = lfg % numLfGroupsX;
            uint32_t lgy = lfg / numLfGroupsX;
            std::vector<JxlChannel> tileChs;
            std::vector<size_t> parentIdxs;
            std::vector<uint32_t> tileX0, tileY0;
            for (size_t ci = nbMetaChannels; ci < channels.size(); ++ci) {
                auto& ch = channels[ci];
                if (ch.decoded || ch.hshift < 3 || ch.vshift < 3) continue;
                uint32_t tx = (lgx * lfGroupDim) >> ch.hshift;
                uint32_t ty = (lgy * lfGroupDim) >> ch.vshift;
                if (tx >= ch.w || ty >= ch.h) continue;
                uint32_t tw = std::min(ch.w - tx, lfGroupDim >> ch.hshift);
                uint32_t th = std::min(ch.h - ty, lfGroupDim >> ch.vshift);
                JxlChannel tc; tc.w = tw; tc.h = th; tc.hshift = ch.hshift; tc.vshift = ch.vshift;
                tileChs.push_back(std::move(tc));
                parentIdxs.push_back(ci);
                tileX0.push_back(tx);
                tileY0.push_back(ty);
            }
            if (!tileChs.empty()) {
                DetailBrotli::BitReader lfgBr(cs + sectionOffsets[1u + lfg], tocSizes[1u + lfg]);
                std::vector<JxlChannel*> ptrs;
                for (auto& tc : tileChs) ptrs.push_back(&tc);
                if (!DecodeModularChannelsInStream(
                        lfgBr, ptrs, 1u + lfg, hasGlobalTree, globalTree, globalHist,
                        false, false, JxlWpHeader{})) {
                    return false;
                }
                for (size_t k = 0; k < tileChs.size(); ++k) {
                    auto& dstCh = channels[parentIdxs[k]];
                    const auto& srcCh = tileChs[k];
                    for (uint32_t ry = 0; ry < srcCh.h; ++ry) {
                        std::memcpy(&dstCh.pixels[static_cast<size_t>(tileY0[k] + ry) * dstCh.w + tileX0[k]],
                                    &srcCh.pixels[static_cast<size_t>(ry) * srcCh.w],
                                    static_cast<size_t>(srcCh.w) * sizeof(int32_t));
                    }
                }
            }
        }
        for (size_t ci = nbMetaChannels; ci < channels.size(); ++ci) {
            if (!channels[ci].decoded && channels[ci].hshift >= 3 && channels[ci].vshift >= 3) {
                channels[ci].decoded = true;
            }
        }

        // 2. PassGroups (remaining channels with hshift < 3 || vshift < 3)
        for (uint32_t g = 0; g < numGroups; ++g) {
            uint32_t gx = g % numGroupsX;
            uint32_t gy = g / numGroupsX;
            std::vector<JxlChannel> tileChs;
            std::vector<size_t> parentIdxs;
            std::vector<uint32_t> tileX0, tileY0;
            for (size_t ci = nbMetaChannels; ci < channels.size(); ++ci) {
                auto& ch = channels[ci];
                if (ch.decoded) continue;
                uint32_t tx = (gx * groupDim) >> ch.hshift;
                uint32_t ty = (gy * groupDim) >> ch.vshift;
                if (tx >= ch.w || ty >= ch.h) continue;
                uint32_t tw = std::min(ch.w - tx, groupDim >> ch.hshift);
                uint32_t th = std::min(ch.h - ty, groupDim >> ch.vshift);
                JxlChannel tc; tc.w = tw; tc.h = th; tc.hshift = ch.hshift; tc.vshift = ch.vshift;
                tileChs.push_back(std::move(tc));
                parentIdxs.push_back(ci);
                tileX0.push_back(tx);
                tileY0.push_back(ty);
            }
            if (!tileChs.empty()) {
                uint32_t tocIdx = 2u + numLfGroups + g;
                DetailBrotli::BitReader pgBr(cs + sectionOffsets[tocIdx], tocSizes[tocIdx]);
                std::vector<JxlChannel*> ptrs;
                for (auto& tc : tileChs) ptrs.push_back(&tc);
                if (!DecodeModularChannelsInStream(
                        pgBr, ptrs, 1u + numLfGroups + g, hasGlobalTree, globalTree, globalHist,
                        false, false, JxlWpHeader{})) {
                    return false;
                }
                for (size_t k = 0; k < tileChs.size(); ++k) {
                    auto& dstCh = channels[parentIdxs[k]];
                    const auto& srcCh = tileChs[k];
                    for (uint32_t ry = 0; ry < srcCh.h; ++ry) {
                        std::memcpy(&dstCh.pixels[static_cast<size_t>(tileY0[k] + ry) * dstCh.w + tileX0[k]],
                                    &srcCh.pixels[static_cast<size_t>(ry) * srcCh.w],
                                    static_cast<size_t>(srcCh.w) * sizeof(int32_t));
                    }
                }
            }
        }
        for (size_t ci = nbMetaChannels; ci < channels.size(); ++ci) {
            channels[ci].decoded = true;
        }
    }

    // Undo global Modular transforms in reverse order
    for (int ti = static_cast<int>(globalTransforms.size()) - 1; ti >= 0; --ti) {
        if (!UndoModularTransform(channels, globalTransforms[ti], bitsPerSample)) {
            return false;
        }
    }
    if (channels.size() < numColorCh) return false;

    // Convert reconstructed channels to Premultiplied BGRA
    int alphaIdx = -1;
    bool alphaPremul = false;
    for (uint32_t ec = 0; ec < numExtraChannels; ++ec) {
        if (ecInfos[ec].type == 0u && numColorCh + ec < channels.size()) {
            alphaIdx = static_cast<int>(numColorCh + ec);
            alphaPremul = ecInfos[ec].alphaAssoc;
            break;
        }
    }

    const int64_t maxSample = (1ll << bitsPerSample) - 1ll;
    const int64_t maxAlpha  = (alphaIdx >= 0) ? ((1ll << ecInfos[alphaIdx - numColorCh].bits) - 1ll) : 255ll;

    auto scaleTo8 = [&](int64_t v, int64_t mx) -> uint8_t {
        v = std::clamp<int64_t>(v, 0ll, mx);
        if (mx == 255ll) return static_cast<uint8_t>(v);
        return static_cast<uint8_t>((v * 255ll + (mx >> 1)) / mx);
    };

    std::vector<uint8_t> rawBgra(static_cast<size_t>(frameW) * frameH * 4u, 255u);
    for (uint32_t y = 0; y < frameH; ++y) {
        for (uint32_t x = 0; x < frameW; ++x) {
            auto sampleCh = [&](size_t ci) -> int64_t {
                const auto& ch = channels[ci];
                uint32_t sx = std::min(ch.w - 1u, x >> ch.hshift);
                uint32_t sy = std::min(ch.h - 1u, y >> ch.vshift);
                return ch.pixels[static_cast<size_t>(sy) * ch.w + sx];
            };

            uint8_t r8 = 0u, g8 = 0u, b8 = 0u, a8 = 255u;
            if (numColorCh == 1u) {
                r8 = g8 = b8 = scaleTo8(sampleCh(0), maxSample);
            } else if (doYcbcr) {
                // JXL do_ycbcr channel order: [0] = Cb, [1] = Y, [2] = Cr (zero-centered signed offsets!)
                double half = static_cast<double>(1ll << (bitsPerSample - 1u));
                double sc = 255.0 / static_cast<double>(maxSample);
                double cb = static_cast<double>(sampleCh(0)) * sc;
                double yv = (static_cast<double>(sampleCh(1)) + half) * sc;
                double cr = static_cast<double>(sampleCh(2)) * sc;
                r8 = static_cast<uint8_t>(std::clamp(std::lround(yv + 1.402 * cr), 0l, 255l));
                g8 = static_cast<uint8_t>(std::clamp(std::lround(yv - 0.344136 * cb - 0.714136 * cr), 0l, 255l));
                b8 = static_cast<uint8_t>(std::clamp(std::lround(yv + 1.772 * cb), 0l, 255l));
            } else if (xybEncoded) {
                // Modular XYB channel order: [0] = Y, [1] = X, [2] = B - Y (ISO/IEC 18181-1 Clause 10.1.2)
                float yv = static_cast<float>(sampleCh(0)) * modLfDequant[1];
                float xv = static_cast<float>(sampleCh(1)) * modLfDequant[0];
                float bv = static_cast<float>(sampleCh(2)) * modLfDequant[2] + yv;
                JxlXybToSrgb8(xv, yv, bv, opsinParams, transferFn, r8, g8, b8);
                if (colourSpace == 1u) r8 = g8 = b8;
            } else {
                r8 = scaleTo8(sampleCh(0), maxSample);
                g8 = scaleTo8(sampleCh(1), maxSample);
                b8 = scaleTo8(sampleCh(2), maxSample);
            }

            if (alphaIdx >= 0) {
                a8 = scaleTo8(sampleCh(static_cast<size_t>(alphaIdx)), maxAlpha);
            }

            uint8_t* dst = &rawBgra[(static_cast<size_t>(y) * frameW + x) * 4u];
            if (alphaPremul) {
                dst[0] = std::min(b8, a8);
                dst[1] = std::min(g8, a8);
                dst[2] = std::min(r8, a8);
                dst[3] = a8;
            } else {
                WritePremulBGRA(dst, r8, g8, b8, a8);
            }
        }
    }

    // Apply EXIF/JXL orientation (1..8)
    uint32_t outW = (orientation >= 5u) ? frameH : frameW;
    uint32_t outH = (orientation >= 5u) ? frameW : frameH;
    outInfo.width = outW;
    outInfo.height = outH;
    outInfo.origWidth = outW;
    outInfo.origHeight = outH;
    outInfo.channels = numColorCh + (alphaIdx >= 0 ? 1u : 0u);
    outInfo.bitsPerChannel = bitsPerSample;
    outInfo.formatName = L"JPEG XL Modular (" + std::to_wstring(outW) + L"x" + std::to_wstring(outH) +
                         L", " + std::to_wstring(bitsPerSample) + L"-bit .jxl)";

    if (orientation == 1u) {
        outInfo.bgraPixels = std::move(rawBgra);
    } else {
        outInfo.bgraPixels.resize(static_cast<size_t>(outW) * outH * 4u);
        for (uint32_t sy = 0; sy < frameH; ++sy) {
            for (uint32_t sx = 0; sx < frameW; ++sx) {
                uint32_t dx = sx, dy = sy;
                switch (orientation) {
                case 2: dx = frameW - 1u - sx; dy = sy; break;
                case 3: dx = frameW - 1u - sx; dy = frameH - 1u - sy; break;
                case 4: dx = sx; dy = frameH - 1u - sy; break;
                case 5: dx = sy; dy = sx; break;
                case 6: dx = frameH - 1u - sy; dy = sx; break;
                case 7: dx = frameH - 1u - sy; dy = frameW - 1u - sx; break;
                case 8: dx = sy; dy = frameW - 1u - sx; break;
                default: break;
                }
                std::memcpy(&outInfo.bgraPixels[(static_cast<size_t>(dy) * outW + dx) * 4u],
                            &rawBgra[(static_cast<size_t>(sy) * frameW + sx) * 4u], 4u);
            }
        }
    }
    outInfo.valid = true;
    return true;
}

// ----------------------------------------------------------------------------
// BPG / ITU-T H.265 (HEVC) Helpers: MSPS, Annex-B Builder, MF MFT, Pure-C++ I_PCM
// ----------------------------------------------------------------------------
struct MsbBitReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t bitPos = 0;
    bool eof = false;

    MsbBitReader(const uint8_t* d, size_t s) : data(d), size(s) {}

    uint32_t ReadBit() {
        size_t byteIdx = bitPos >> 3;
        if (byteIdx >= size) { eof = true; return 0u; }
        uint32_t v = (data[byteIdx] >> (7u - (bitPos & 7u))) & 1u;
        ++bitPos;
        return v;
    }

    uint32_t ReadBits(uint32_t n) {
        uint32_t v = 0u;
        for (uint32_t i = 0; i < n; ++i) {
            v = (v << 1u) | ReadBit();
        }
        return v;
    }

    uint32_t ReadUe() {
        uint32_t lz = 0u;
        while (!eof && lz <= 31u && ReadBit() == 0u) ++lz;
        if (eof || lz > 31u) { eof = true; return 0u; }
        if (lz == 0u) return 0u;
        return ((1u << lz) - 1u) + ReadBits(lz);
    }

    int32_t ReadSe() {
        uint32_t u = ReadUe();
        if (u == 0u) return 0;
        int32_t mag = static_cast<int32_t>((u + 1u) >> 1u);
        return ((u & 1u) != 0u) ? mag : -mag;
    }
};

struct MsbBitWriter {
    std::vector<uint8_t> bytes;
    uint8_t cur = 0;
    uint32_t used = 0;

    void PutBit(uint32_t b) {
        cur = static_cast<uint8_t>((cur << 1u) | (b & 1u));
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

    void PutRbspTrailing() {
        PutBit(1u);
        while (used != 0u) PutBit(0u);
    }
};

inline bool UnescapeRbsp(const uint8_t* src, size_t len, std::vector<uint8_t>& outRbsp) {
    outRbsp.clear();
    outRbsp.reserve(len);
    uint32_t zeros = 0u;
    for (size_t i = 0; i < len; ++i) {
        uint8_t b = src[i];
        if (zeros >= 2u && b == 0x03u) {
            if (i + 1u < len && src[i + 1u] > 0x03u) return false;
            zeros = 0u;
            continue;
        }
        outRbsp.push_back(b);
        zeros = (b == 0u) ? (zeros + 1u) : 0u;
    }
    return true;
}

inline void AppendAnnexBNal(std::vector<uint8_t>& dst, uint8_t nalType, uint8_t layerId, const std::vector<uint8_t>& rbsp) {
    dst.push_back(0x00u);
    dst.push_back(0x00u);
    dst.push_back(0x00u);
    dst.push_back(0x01u);
    dst.push_back(static_cast<uint8_t>(((nalType & 0x3Fu) << 1u) | ((layerId >> 5u) & 1u)));
    dst.push_back(static_cast<uint8_t>(((layerId & 0x1Fu) << 3u) | 1u));
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

struct BpgMsps {
    uint32_t chromaFormatIdc = 1;
    bool separateColourPlane = false;
    uint32_t bitDepthLumaMinus8 = 0;
    uint32_t bitDepthChromaMinus8 = 0;
    uint32_t log2MaxPocLsbMinus4 = 0;
    uint32_t log2MinCbMinus3 = 0;
    uint32_t log2DiffMaxMinCb = 0;
    uint32_t log2MinTbMinus2 = 0;
    uint32_t log2DiffMaxMinTb = 0;
    uint32_t maxTrDepthIntra = 0;
    bool scalingListEnabled = false;
    bool ampEnabled = false;
    bool saoEnabled = false;
    bool pcmEnabled = false;
    uint32_t pcmBitDepthLumaMinus1 = 7;
    uint32_t pcmBitDepthChromaMinus1 = 7;
    uint32_t log2MinPcmCbMinus3 = 0;
    uint32_t log2DiffMaxMinPcmCb = 0;
    bool pcmLoopFilterDisabled = false;
    bool strongIntraSmoothing = false;
    bool spsExtensionPresent = false;
    uint32_t spsRangeExtFlags9 = 0;
    bool hasRangeExt = false;
};

inline bool ParseBpgMsps(const uint8_t* raw, size_t rawLen, BpgMsps& sps) {
    if (!raw || rawLen == 0u) return false;
    std::vector<uint8_t> rbsp;
    if (!UnescapeRbsp(raw, rawLen, rbsp) || rbsp.empty()) return false;
    MsbBitReader br(rbsp.data(), rbsp.size());

    sps.chromaFormatIdc = br.ReadUe();
    if (sps.chromaFormatIdc > 3u) return false;
    if (sps.chromaFormatIdc == 3u) {
        sps.separateColourPlane = br.ReadBit() != 0u;
        if (sps.separateColourPlane) return false;
    }
    sps.bitDepthLumaMinus8   = br.ReadUe();
    sps.bitDepthChromaMinus8 = br.ReadUe();
    sps.log2MaxPocLsbMinus4  = br.ReadUe();
    sps.log2MinCbMinus3      = br.ReadUe();
    sps.log2DiffMaxMinCb     = br.ReadUe();
    sps.log2MinTbMinus2      = br.ReadUe();
    sps.log2DiffMaxMinTb     = br.ReadUe();
    sps.maxTrDepthIntra      = br.ReadUe();
    if (br.eof || sps.bitDepthLumaMinus8 > 8u || sps.bitDepthChromaMinus8 > 8u ||
        sps.log2MaxPocLsbMinus4 > 12u || sps.log2MinCbMinus3 > 3u ||
        sps.log2MinCbMinus3 + sps.log2DiffMaxMinCb > 3u) {
        return false;
    }
    sps.scalingListEnabled = br.ReadBit() != 0u;
    if (sps.scalingListEnabled) return false;
    sps.ampEnabled = br.ReadBit() != 0u;
    sps.saoEnabled = br.ReadBit() != 0u;
    sps.pcmEnabled = br.ReadBit() != 0u;
    if (sps.pcmEnabled) {
        sps.pcmBitDepthLumaMinus1   = br.ReadBits(4u);
        sps.pcmBitDepthChromaMinus1 = br.ReadBits(4u);
        sps.log2MinPcmCbMinus3      = br.ReadUe();
        sps.log2DiffMaxMinPcmCb     = br.ReadUe();
        sps.pcmLoopFilterDisabled   = br.ReadBit() != 0u;
        if (sps.log2MinPcmCbMinus3 > 2u || sps.log2MinPcmCbMinus3 + sps.log2DiffMaxMinPcmCb > 2u) {
            return false;
        }
    }
    sps.strongIntraSmoothing = br.ReadBit() != 0u;
    sps.spsExtensionPresent  = br.ReadBit() != 0u;
    if (sps.spsExtensionPresent) {
        uint32_t extFlags = br.ReadBits(8u);
        if ((extFlags & 0x80u) != 0u) { // sps_range_extension_flag
            sps.hasRangeExt = true;
            sps.spsRangeExtFlags9 = br.ReadBits(9u);
        }
    }
    return !br.eof;
}

inline void BuildHevcAnnexBFromBpg(
    const BpgMsps& sps, uint32_t picW, uint32_t picH,
    const uint8_t* annexBNals, size_t annexBLen,
    std::vector<uint8_t>& outAnnexB, uint32_t& outAlignedW, uint32_t& outAlignedH)
{
    uint32_t minCbSizeY = 1u << (sps.log2MinCbMinus3 + 3u);
    outAlignedW = (picW + minCbSizeY - 1u) & ~(minCbSizeY - 1u);
    outAlignedH = (picH + minCbSizeY - 1u) & ~(minCbSizeY - 1u);

    uint32_t profileIdc = (sps.chromaFormatIdc == 1u && sps.bitDepthLumaMinus8 == 0u) ? 1u :
                          ((sps.chromaFormatIdc == 1u && sps.bitDepthLumaMinus8 == 2u) ? 2u : 4u);
    auto writePtl = [&](MsbBitWriter& w) {
        w.PutBits(0u, 2u); // general_profile_space
        w.PutBit(0u);      // general_tier_flag
        w.PutBits(profileIdc, 5u);
        uint32_t compat = 1u << (31u - profileIdc);
        w.PutBits(compat, 32u);
        w.PutBit(1u); // progressive
        w.PutBit(0u); // interlaced
        w.PutBit(1u); // non_packed
        w.PutBit(1u); // frame_only
        w.PutBits(0u, 32u);
        w.PutBits(0u, 12u);
        w.PutBits(120u, 8u); // general_level_idc = Level 4.0
    };

    // 1. VPS (NAL 32)
    MsbBitWriter vps;
    vps.PutBits(0u, 4u);   // vps_id = 0
    vps.PutBits(3u, 2u);   // base_layer_internal=1, available=1
    vps.PutBits(0u, 6u);   // vps_max_layers_minus1 = 0
    vps.PutBits(0u, 3u);   // vps_max_sub_layers_minus1 = 0
    vps.PutBit(1u);        // vps_temporal_id_nesting_flag = 1
    vps.PutBits(0xFFFFu, 16u);
    writePtl(vps);
    vps.PutBit(1u);        // vps_sub_layer_ordering_info_present_flag
    vps.PutUe(0u); vps.PutUe(0u); vps.PutUe(0u);
    vps.PutBits(0u, 6u);   // vps_max_layer_id = 0
    vps.PutUe(0u);         // vps_num_layer_sets_minus1 = 0
    vps.PutBit(0u);        // vps_timing_info_present_flag = 0
    vps.PutBit(0u);        // vps_extension_flag = 0
    vps.PutRbspTrailing();

    // 2. SPS (NAL 33)
    MsbBitWriter sw;
    sw.PutBits(0u, 4u);    // sps_video_parameter_set_id = 0
    sw.PutBits(0u, 3u);    // sps_max_sub_layers_minus1 = 0
    sw.PutBit(1u);         // sps_temporal_id_nesting_flag = 1
    writePtl(sw);
    sw.PutUe(0u);          // sps_seq_parameter_set_id = 0
    sw.PutUe(sps.chromaFormatIdc);
    if (sps.chromaFormatIdc == 3u) sw.PutBit(0u);
    sw.PutUe(outAlignedW);
    sw.PutUe(outAlignedH);
    uint32_t subW = (sps.chromaFormatIdc == 1u || sps.chromaFormatIdc == 2u) ? 2u : 1u;
    uint32_t subH = (sps.chromaFormatIdc == 1u) ? 2u : 1u;
    if (outAlignedW != picW || outAlignedH != picH) {
        sw.PutBit(1u);
        sw.PutUe(0u);
        sw.PutUe((outAlignedW - picW) / subW);
        sw.PutUe(0u);
        sw.PutUe((outAlignedH - picH) / subH);
    } else {
        sw.PutBit(0u);
    }
    sw.PutUe(sps.bitDepthLumaMinus8);
    sw.PutUe(sps.bitDepthChromaMinus8);
    sw.PutUe(sps.log2MaxPocLsbMinus4);
    sw.PutBit(1u);
    sw.PutUe(0u); sw.PutUe(0u); sw.PutUe(0u);
    sw.PutUe(sps.log2MinCbMinus3);
    sw.PutUe(sps.log2DiffMaxMinCb);
    sw.PutUe(sps.log2MinTbMinus2);
    sw.PutUe(sps.log2DiffMaxMinTb);
    sw.PutUe(0u); // max_transform_hierarchy_depth_inter = 0
    sw.PutUe(sps.maxTrDepthIntra);
    sw.PutBit(0u); // scaling_list_enabled_flag = 0
    sw.PutBit(sps.ampEnabled ? 1u : 0u);
    sw.PutBit(sps.saoEnabled ? 1u : 0u);
    sw.PutBit(sps.pcmEnabled ? 1u : 0u);
    if (sps.pcmEnabled) {
        sw.PutBits(sps.pcmBitDepthLumaMinus1, 4u);
        sw.PutBits(sps.pcmBitDepthChromaMinus1, 4u);
        sw.PutUe(sps.log2MinPcmCbMinus3);
        sw.PutUe(sps.log2DiffMaxMinPcmCb);
        sw.PutBit(sps.pcmLoopFilterDisabled ? 1u : 0u);
    }
    sw.PutUe(0u); // num_short_term_ref_pic_sets = 0
    sw.PutBit(0u); // long_term_ref_pics_present_flag = 0
    sw.PutBit(0u); // sps_temporal_mvp_enabled_flag = 0
    sw.PutBit(sps.strongIntraSmoothing ? 1u : 0u);
    sw.PutBit(0u); // vui_parameters_present_flag = 0
    if (sps.hasRangeExt) {
        sw.PutBit(1u);
        sw.PutBits(0x80u, 8u);
        sw.PutBits(sps.spsRangeExtFlags9, 9u);
    } else {
        sw.PutBit(0u);
    }
    sw.PutRbspTrailing();

    outAnnexB.clear();
    AppendAnnexBNal(outAnnexB, 32u, 0u, vps.bytes);
    AppendAnnexBNal(outAnnexB, 33u, 0u, sw.bytes);
    outAnnexB.insert(outAnnexB.end(), annexBNals, annexBNals + annexBLen);
    // Append End-Of-Sequence (NAL 36) so MFT flushes the single Intra picture immediately
    static const uint8_t kEosNal[6] = { 0x00, 0x00, 0x00, 0x01, 0x48, 0x01 };
    outAnnexB.insert(outAnnexB.end(), kEosNal, kEosNal + 6);
}

inline bool TryDecodeHevcViaMediaFoundation(
    const std::vector<uint8_t>& annexB,
    uint32_t alignedW, uint32_t alignedH,
    uint32_t cropW, uint32_t cropH,
    std::vector<uint8_t>& outBgra)
{
    if (annexB.empty() || alignedW == 0u || alignedH == 0u) return false;
    if (!::Explorer2::IsMediaFoundationAvailable()) return false;
    HRESULT hrMf = ::MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hrMf)) return false;

    bool ok = false;
    MFT_REGISTER_TYPE_INFO inInfo = { MFMediaType_Video, MFVideoFormat_HEVC };
    IMFActivate** activates = nullptr;
    UINT32 actCount = 0u;
    HRESULT hr = ::MFTEnumEx(
        MFT_CATEGORY_VIDEO_DECODER,
        MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER,
        &inInfo, nullptr, &activates, &actCount);

    if (SUCCEEDED(hr) && activates && actCount > 0u) {
        IMFTransform* mft = nullptr;
        if (SUCCEEDED(activates[0]->ActivateObject(IID_PPV_ARGS(&mft))) && mft) {
            IMFMediaType* inType = nullptr;
            if (SUCCEEDED(::MFCreateMediaType(&inType)) && inType) {
                inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_HEVC);
                ::MFSetAttributeSize(inType, MF_MT_FRAME_SIZE, alignedW, alignedH);
                inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
                if (SUCCEEDED(mft->SetInputType(0, inType, 0))) {
                    GUID chosenSub = GUID_NULL;
                    for (DWORD idx = 0; idx < 32u; ++idx) {
                        IMFMediaType* avail = nullptr;
                        if (FAILED(mft->GetOutputAvailableType(0, idx, &avail)) || !avail) break;
                        GUID sub = GUID_NULL;
                        avail->GetGUID(MF_MT_SUBTYPE, &sub);
                        if (sub == MFVideoFormat_RGB32 || sub == MFVideoFormat_ARGB32 ||
                            sub == MFVideoFormat_NV12  || sub == MFVideoFormat_YV12   ||
                            sub == MFVideoFormat_IYUV) {
                            if (SUCCEEDED(mft->SetOutputType(0, avail, 0))) {
                                chosenSub = sub;
                                avail->Release();
                                break;
                            }
                        }
                        avail->Release();
                    }

                    if (chosenSub != GUID_NULL) {
                        mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
                        mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

                        IMFSample* inSample = nullptr;
                        IMFMediaBuffer* inBuf = nullptr;
                        if (SUCCEEDED(::MFCreateMemoryBuffer(static_cast<DWORD>(annexB.size()), &inBuf)) && inBuf) {
                            BYTE* dstPtr = nullptr;
                            if (SUCCEEDED(inBuf->Lock(&dstPtr, nullptr, nullptr)) && dstPtr) {
                                std::memcpy(dstPtr, annexB.data(), annexB.size());
                                inBuf->Unlock();
                                inBuf->SetCurrentLength(static_cast<DWORD>(annexB.size()));
                                if (SUCCEEDED(::MFCreateSample(&inSample)) && inSample) {
                                    inSample->AddBuffer(inBuf);
                                    inSample->SetSampleTime(0);
                                    inSample->SetSampleDuration(333333);
                                    mft->ProcessInput(0, inSample, 0);
                                    inSample->Release();
                                }
                            }
                            inBuf->Release();
                        }

                        mft->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);

                        for (int attempt = 0; attempt < 4 && !ok; ++attempt) {
                            MFT_OUTPUT_STREAM_INFO osi = {};
                            mft->GetOutputStreamInfo(0, &osi);
                            bool mftAlloc = (osi.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0u;
                            IMFSample* outSample = nullptr;
                            IMFMediaBuffer* outAllocBuf = nullptr;
                            if (!mftAlloc) {
                                DWORD cbOut = std::max<DWORD>(osi.cbSize, alignedW * alignedH * 4u);
                                if (SUCCEEDED(::MFCreateMemoryBuffer(cbOut, &outAllocBuf)) && outAllocBuf &&
                                    SUCCEEDED(::MFCreateSample(&outSample)) && outSample) {
                                    outSample->AddBuffer(outAllocBuf);
                                }
                            }
                            MFT_OUTPUT_DATA_BUFFER odb = {};
                            odb.dwStreamID = 0;
                            odb.pSample = outSample;
                            DWORD status = 0;
                            HRESULT hrOut = mft->ProcessOutput(0, 1, &odb, &status);
                            if (odb.pEvents) odb.pEvents->Release();

                            if (hrOut == MF_E_TRANSFORM_STREAM_CHANGE) {
                                for (DWORD idx = 0; idx < 32u; ++idx) {
                                    IMFMediaType* avail = nullptr;
                                    if (FAILED(mft->GetOutputAvailableType(0, idx, &avail)) || !avail) break;
                                    GUID sub = GUID_NULL;
                                    avail->GetGUID(MF_MT_SUBTYPE, &sub);
                                    if (sub == MFVideoFormat_RGB32 || sub == MFVideoFormat_ARGB32 || sub == MFVideoFormat_NV12) {
                                        if (SUCCEEDED(mft->SetOutputType(0, avail, 0))) {
                                            chosenSub = sub;
                                            avail->Release();
                                            break;
                                        }
                                    }
                                    avail->Release();
                                }
                            } else if (SUCCEEDED(hrOut) && odb.pSample) {
                                IMFMediaType* curOut = nullptr;
                                UINT32 realW = alignedW, realH = alignedH;
                                INT32 stride = 0;
                                if (SUCCEEDED(mft->GetOutputCurrentType(0, &curOut)) && curOut) {
                                    ::MFGetAttributeSize(curOut, MF_MT_FRAME_SIZE, &realW, &realH);
                                    stride = static_cast<INT32>(::MFGetAttributeUINT32(curOut, MF_MT_DEFAULT_STRIDE, 0));
                                    curOut->Release();
                                }
                                IMFMediaBuffer* contig = nullptr;
                                if (SUCCEEDED(odb.pSample->ConvertToContiguousBuffer(&contig)) && contig) {
                                    BYTE* raw = nullptr;
                                    DWORD rawLen = 0;
                                    if (SUCCEEDED(contig->Lock(&raw, nullptr, &rawLen)) && raw) {
                                        outBgra.resize(static_cast<size_t>(cropW) * cropH * 4u);
                                        if (chosenSub == MFVideoFormat_RGB32 || chosenSub == MFVideoFormat_ARGB32) {
                                            size_t rowBytes = (stride > 0) ? static_cast<size_t>(stride) : (static_cast<size_t>(realW) * 4u);
                                            if (rawLen >= rowBytes * cropH) {
                                                for (uint32_t y = 0; y < cropH; ++y) {
                                                    const uint8_t* sp = raw + static_cast<size_t>(y) * rowBytes;
                                                    for (uint32_t x = 0; x < cropW; ++x) {
                                                        WritePremulBGRA(&outBgra[(static_cast<size_t>(y) * cropW + x) * 4u],
                                                                        sp[x * 4u + 2u], sp[x * 4u + 1u], sp[x * 4u + 0u], 255u);
                                                    }
                                                }
                                                ok = true;
                                            }
                                        } else if (chosenSub == MFVideoFormat_NV12) {
                                            size_t pitch = (stride > 0) ? static_cast<size_t>(stride) : static_cast<size_t>(realW);
                                            size_t uvBase = pitch * realH;
                                            if (rawLen >= uvBase + pitch * ((cropH + 1u) / 2u)) {
                                                for (uint32_t y = 0; y < cropH; ++y) {
                                                    for (uint32_t x = 0; x < cropW; ++x) {
                                                        int yv = static_cast<int>(raw[y * pitch + x]);
                                                        const uint8_t* uv = raw + uvBase + (y / 2u) * pitch + (x & ~1u);
                                                        int cb = static_cast<int>(uv[0]) - 128;
                                                        int cr = static_cast<int>(uv[1]) - 128;
                                                        uint8_t r8 = static_cast<uint8_t>(std::clamp((298 * (yv - 16) + 409 * cr + 128) >> 8, 0, 255));
                                                        uint8_t g8 = static_cast<uint8_t>(std::clamp((298 * (yv - 16) - 100 * cb - 208 * cr + 128) >> 8, 0, 255));
                                                        uint8_t b8 = static_cast<uint8_t>(std::clamp((298 * (yv - 16) + 516 * cb + 128) >> 8, 0, 255));
                                                        WritePremulBGRA(&outBgra[(static_cast<size_t>(y) * cropW + x) * 4u], r8, g8, b8, 255u);
                                                    }
                                                }
                                                ok = true;
                                            }
                                        }
                                        contig->Unlock();
                                    }
                                    contig->Release();
                                }
                            }
                            if (outAllocBuf) outAllocBuf->Release();
                            if (outSample) outSample->Release();
                            else if (mftAlloc && odb.pSample) odb.pSample->Release();
                            if (hrOut != MF_E_TRANSFORM_STREAM_CHANGE && FAILED(hrOut)) break;
                        }
                    }
                }
                inType->Release();
            }
            mft->Release();
        }
        for (UINT32 i = 0; i < actCount; ++i) {
            if (activates[i]) activates[i]->Release();
        }
        ::CoTaskMemFree(activates);
    }

    ::MFShutdown();
    return ok;
}

// Pure-C++ ITU-T H.265 Intra (I-slice + CABAC + Quadtree + I_PCM) Plane Decoder
struct HevcPps {
    bool dependentSliceEnabled = false;
    bool outputFlagPresent = false;
    uint32_t numExtraSliceHdrBits = 0;
    int32_t initQpMinus26 = 0;
    bool sliceChromaQpOffsetsPresent = false;
    bool transquantBypassEnabled = false;
    bool tilesEnabled = false;
    bool entropyCodingSyncEnabled = false;
    bool loopFilterAcrossSlicesEnabled = false;
    bool deblockingControlPresent = false;
    bool deblockingOverrideEnabled = false;
    bool deblockingDisabled = false;
    bool sliceHeaderExtPresent = false;
};

inline bool ParseHevcPps(const std::vector<uint8_t>& rbsp, HevcPps& pps) {
    if (rbsp.empty()) return false;
    MsbBitReader br(rbsp.data(), rbsp.size());
    (void)br.ReadUe(); // pps_id
    (void)br.ReadUe(); // sps_id
    pps.dependentSliceEnabled = br.ReadBit() != 0u;
    pps.outputFlagPresent     = br.ReadBit() != 0u;
    pps.numExtraSliceHdrBits  = br.ReadBits(3u);
    (void)br.ReadBit(); // sign_data_hiding
    (void)br.ReadBit(); // cabac_init_present
    (void)br.ReadUe();  // num_ref_l0
    (void)br.ReadUe();  // num_ref_l1
    pps.initQpMinus26 = br.ReadSe();
    if (pps.initQpMinus26 < -60 || pps.initQpMinus26 > 25) return false;
    (void)br.ReadBit(); // constrained_intra_pred
    (void)br.ReadBit(); // transform_skip_enabled
    if (br.ReadBit() != 0u) (void)br.ReadUe(); // cu_qp_delta
    (void)br.ReadSe(); // cb_qp_offset
    (void)br.ReadSe(); // cr_qp_offset
    pps.sliceChromaQpOffsetsPresent = br.ReadBit() != 0u;
    (void)br.ReadBit(); // weighted_pred
    (void)br.ReadBit(); // weighted_bipred
    pps.transquantBypassEnabled = br.ReadBit() != 0u;
    pps.tilesEnabled            = br.ReadBit() != 0u;
    pps.entropyCodingSyncEnabled = br.ReadBit() != 0u;
    if (pps.tilesEnabled) {
        uint32_t colsMinus1 = br.ReadUe();
        uint32_t rowsMinus1 = br.ReadUe();
        if (colsMinus1 > 64u || rowsMinus1 > 64u) return false;
        bool uniform = br.ReadBit() != 0u;
        if (!uniform) {
            for (uint32_t i = 0; i < colsMinus1; ++i) (void)br.ReadUe();
            for (uint32_t i = 0; i < rowsMinus1; ++i) (void)br.ReadUe();
        }
        (void)br.ReadBit(); // loop_filter_across_tiles
    }
    pps.loopFilterAcrossSlicesEnabled = br.ReadBit() != 0u;
    pps.deblockingControlPresent = br.ReadBit() != 0u;
    if (pps.deblockingControlPresent) {
        pps.deblockingOverrideEnabled = br.ReadBit() != 0u;
        pps.deblockingDisabled        = br.ReadBit() != 0u;
        if (!pps.deblockingDisabled) {
            (void)br.ReadSe();
            (void)br.ReadSe();
        }
    }
    if (br.ReadBit() != 0u) return false; // pps_scaling_list_data_present_flag
    (void)br.ReadBit(); // lists_modification_present
    (void)br.ReadUe();  // log2_parallel_merge_level_minus2
    pps.sliceHeaderExtPresent = br.ReadBit() != 0u;
    return !br.eof;
}

struct HevcCabacCtx {
    uint8_t state = 0;
    uint8_t mps = 0;

    void Init(uint8_t initVal, int sliceQp) {
        int slope = (initVal >> 4u) * 5 - 45;
        int inter = ((initVal & 15u) << 3) - 16;
        int pre = std::clamp(((slope * std::clamp(sliceQp, 0, 51)) >> 4) + inter, 1, 126);
        mps = (pre <= 63) ? 0u : 1u;
        state = static_cast<uint8_t>((mps != 0u) ? (pre - 64) : (63 - pre));
    }
};

struct HevcCabacDecoder {
    MsbBitReader& br;
    uint32_t range = 510;
    uint32_t offset = 0;
    bool error = false;

    explicit HevcCabacDecoder(MsbBitReader& r) : br(r) {}

    bool InitEngine() {
        range = 510u;
        offset = br.ReadBits(9u);
        if (br.eof || offset >= 510u) {
            error = true;
            return false;
        }
        return true;
    }

    void Renorm() {
        while (range < 256u && !br.eof) {
            range <<= 1u;
            offset = (offset << 1u) | br.ReadBit();
        }
        if (br.eof) error = true;
    }

    uint32_t DecodeDecision(HevcCabacCtx& ctx) {
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
        uint32_t lpsRange = kRangeTabLps[ctx.state][qIdx];
        range -= lpsRange;
        uint32_t bin = 0u;
        if (offset >= range) {
            bin = 1u - ctx.mps;
            offset -= range;
            range = lpsRange;
            if (ctx.state == 0u) ctx.mps = 1u - ctx.mps;
            ctx.state = kTransIdxLps[ctx.state];
        } else {
            bin = ctx.mps;
            if (ctx.state < 62u) ++ctx.state;
        }
        Renorm();
        return bin;
    }

    uint32_t DecodeBypass() {
        offset = (offset << 1u) | br.ReadBit();
        if (br.eof) { error = true; return 0u; }
        if (offset >= range) {
            offset -= range;
            return 1u;
        }
        return 0u;
    }

    uint32_t DecodeTerminate() {
        if (range < 2u) { error = true; return 1u; }
        range -= 2u;
        if (offset >= range) {
            return 1u;
        }
        Renorm();
        return 0u;
    }
};

struct HevcPlanes {
    uint32_t alignedW = 0, alignedH = 0;
    uint32_t subW = 1, subH = 1;
    std::vector<uint16_t> yPlane, cbPlane, crPlane;
};

inline bool DecodeHevcLayerPureCpp(
    const BpgMsps& sps,
    uint32_t picW, uint32_t picH,
    const std::vector<uint8_t>& ppsRbsp,
    const std::vector<uint8_t>& sliceRbsp,
    uint8_t sliceNalType,
    HevcPlanes& outPlanes)
{
    if (!sps.pcmEnabled) return false; // Pure-C++ fallback decodes I_PCM CUs deterministically
    HevcPps pps;
    if (!ParseHevcPps(ppsRbsp, pps) || sliceRbsp.empty()) return false;

    uint32_t minCbLog2 = sps.log2MinCbMinus3 + 3u;
    uint32_t ctbLog2   = minCbLog2 + sps.log2DiffMaxMinCb;
    uint32_t minCbSize = 1u << minCbLog2;
    uint32_t ctbSize   = 1u << ctbLog2;
    uint32_t alignedW  = (picW + minCbSize - 1u) & ~(minCbSize - 1u);
    uint32_t alignedH  = (picH + minCbSize - 1u) & ~(minCbSize - 1u);
    uint32_t ctbW      = (alignedW + ctbSize - 1u) / ctbSize;
    uint32_t ctbH      = (alignedH + ctbSize - 1u) / ctbSize;
    uint32_t numCtbs   = ctbW * ctbH;

    uint32_t minPcmLog2 = sps.log2MinPcmCbMinus3 + 3u;
    uint32_t maxPcmLog2 = minPcmLog2 + sps.log2DiffMaxMinPcmCb;
    uint32_t pcmBdY     = sps.pcmBitDepthLumaMinus1 + 1u;
    uint32_t pcmBdC     = sps.pcmBitDepthChromaMinus1 + 1u;
    uint32_t bdY        = sps.bitDepthLumaMinus8 + 8u;
    uint32_t bdC        = sps.bitDepthChromaMinus8 + 8u;
    if (pcmBdY > bdY || pcmBdC > bdC || pcmBdY == 0u || pcmBdC == 0u) return false;

    // Parse slice_segment_header (ITU-T H.265 7.3.6.1)
    MsbBitReader br(sliceRbsp.data(), sliceRbsp.size());
    bool firstSlice = br.ReadBit() != 0u;
    if (!firstSlice) return false;
    if (sliceNalType >= 16u && sliceNalType <= 23u) {
        (void)br.ReadBit(); // no_output_of_prior_pics_flag
    }
    (void)br.ReadUe(); // slice_pic_parameter_set_id
    for (uint32_t i = 0; i < pps.numExtraSliceHdrBits; ++i) (void)br.ReadBit();
    uint32_t sliceType = br.ReadUe();
    if (br.eof || sliceType != 2u) return false; // Must be I-slice (2)

    if (pps.outputFlagPresent) (void)br.ReadBit();
    if (sliceNalType != 19u && sliceNalType != 20u) {
        (void)br.ReadBits(sps.log2MaxPocLsbMinus4 + 4u);
        if (br.ReadBit() == 0u) return false; // Expect empty short_term_ref_pic_set for still picture
    }
    bool sliceSaoLuma = false, sliceSaoChroma = false;
    if (sps.saoEnabled) {
        sliceSaoLuma = br.ReadBit() != 0u;
        if (sps.chromaFormatIdc != 0u) sliceSaoChroma = br.ReadBit() != 0u;
    }
    if (sliceSaoLuma || sliceSaoChroma) return false;

    int32_t sliceQpDelta = br.ReadSe();
    int32_t sliceQpY = 26 + pps.initQpMinus26 + sliceQpDelta;
    if (br.eof || sliceQpY < -static_cast<int32_t>(6u * sps.bitDepthLumaMinus8) || sliceQpY > 51) return false;

    if (pps.sliceChromaQpOffsetsPresent) {
        (void)br.ReadSe();
        (void)br.ReadSe();
    }
    bool sliceDeblockDisabled = pps.deblockingDisabled;
    if (pps.deblockingOverrideEnabled) {
        if (br.ReadBit() != 0u) {
            sliceDeblockDisabled = br.ReadBit() != 0u;
            if (!sliceDeblockDisabled) { (void)br.ReadSe(); (void)br.ReadSe(); }
        }
    }
    if (pps.loopFilterAcrossSlicesEnabled && !sliceDeblockDisabled) {
        (void)br.ReadBit();
    }
    if (pps.tilesEnabled || pps.entropyCodingSyncEnabled) {
        uint32_t numEp = br.ReadUe();
        if (numEp > 0u) {
            uint32_t offLen = br.ReadUe() + 1u;
            for (uint32_t i = 0; i < numEp; ++i) (void)br.ReadBits(offLen);
        }
    }
    if (pps.sliceHeaderExtPresent) {
        uint32_t extLen = br.ReadUe();
        for (uint32_t i = 0; i < extLen; ++i) (void)br.ReadBits(8u);
    }

    // byte_alignment(): alignment_bit_equal_to_one == 1, followed by alignment_bit_equal_to_zero == 0
    if (br.ReadBit() != 1u) return false;
    while ((br.bitPos & 7u) != 0u) {
        if (br.ReadBit() != 0u) return false;
    }
    if (br.eof) return false;

    outPlanes.alignedW = alignedW;
    outPlanes.alignedH = alignedH;
    outPlanes.subW = (sps.chromaFormatIdc == 1u || sps.chromaFormatIdc == 2u) ? 2u : 1u;
    outPlanes.subH = (sps.chromaFormatIdc == 1u) ? 2u : 1u;
    outPlanes.yPlane.assign(static_cast<size_t>(alignedW) * alignedH, 0u);
    if (sps.chromaFormatIdc != 0u) {
        outPlanes.cbPlane.assign(static_cast<size_t>(alignedW / outPlanes.subW) * (alignedH / outPlanes.subH), 0u);
        outPlanes.crPlane.assign(static_cast<size_t>(alignedW / outPlanes.subW) * (alignedH / outPlanes.subH), 0u);
    }

    // Quadtree CtDepth grid in units of MinCbSizeY
    uint32_t gridW = alignedW >> minCbLog2;
    uint32_t gridH = alignedH >> minCbLog2;
    std::vector<uint8_t> ctDepth(static_cast<size_t>(gridW) * gridH, 0u);

    HevcCabacCtx ctxSplit[3];
    ctxSplit[0].Init(139u, sliceQpY);
    ctxSplit[1].Init(141u, sliceQpY);
    ctxSplit[2].Init(157u, sliceQpY);
    HevcCabacCtx ctxTqBypass;
    ctxTqBypass.Init(154u, sliceQpY);
    HevcCabacCtx ctxPartMode;
    ctxPartMode.Init(184u, sliceQpY);

    HevcCabacDecoder cabac(br);
    if (!cabac.InitEngine()) return false;

    auto decodeCu = [&](auto& self, uint32_t x0, uint32_t y0, uint32_t log2Cb, uint32_t depth) -> bool {
        uint32_t cbSize = 1u << log2Cb;
        bool split = false;
        if (x0 + cbSize <= alignedW && y0 + cbSize <= alignedH && log2Cb > minCbLog2) {
            uint32_t gx = x0 >> minCbLog2;
            uint32_t gy = y0 >> minCbLog2;
            uint32_t inc = ((gx > 0u && ctDepth[gy * gridW + (gx - 1u)] > depth) ? 1u : 0u) +
                           ((gy > 0u && ctDepth[(gy - 1u) * gridW + gx] > depth) ? 1u : 0u);
            split = cabac.DecodeDecision(ctxSplit[inc]) != 0u;
            if (cabac.error) return false;
        } else if ((x0 + cbSize > alignedW || y0 + cbSize > alignedH) && log2Cb > minCbLog2) {
            split = true;
        }

        if (split) {
            uint32_t half = cbSize >> 1u;
            if (!self(self, x0, y0, log2Cb - 1u, depth + 1u)) return false;
            if (x0 + half < alignedW && !self(self, x0 + half, y0, log2Cb - 1u, depth + 1u)) return false;
            if (y0 + half < alignedH && !self(self, x0, y0 + half, log2Cb - 1u, depth + 1u)) return false;
            if (x0 + half < alignedW && y0 + half < alignedH && !self(self, x0 + half, y0 + half, log2Cb - 1u, depth + 1u)) return false;
            return true;
        }

        uint32_t gx0 = x0 >> minCbLog2, gy0 = y0 >> minCbLog2;
        uint32_t gSpan = 1u << (log2Cb - minCbLog2);
        for (uint32_t dy = 0; dy < gSpan && gy0 + dy < gridH; ++dy) {
            for (uint32_t dx = 0; dx < gSpan && gx0 + dx < gridW; ++dx) {
                ctDepth[(gy0 + dy) * gridW + (gx0 + dx)] = static_cast<uint8_t>(depth);
            }
        }

        if (pps.transquantBypassEnabled) {
            (void)cabac.DecodeDecision(ctxTqBypass);
            if (cabac.error) return false;
        }
        bool part2Nx2N = true;
        if (log2Cb == minCbLog2) {
            part2Nx2N = cabac.DecodeDecision(ctxPartMode) != 0u;
            if (cabac.error) return false;
        }
        if (!part2Nx2N || log2Cb < minPcmLog2 || log2Cb > maxPcmLog2) {
            return false; // Non-PCM Intra TU requires hardware/OS HEVC MFT
        }
        uint32_t pcmFlag = cabac.DecodeTerminate();
        if (cabac.error || pcmFlag != 1u) return false;

        // pcm_alignment_zero_bit until byte-aligned
        while ((br.bitPos & 7u) != 0u) {
            if (br.ReadBit() != 0u) return false;
        }
        if (br.eof) return false;

        // Read pcm_sample luma + Cb + Cr
        uint32_t shiftY = bdY - pcmBdY;
        for (uint32_t py = 0; py < cbSize; ++py) {
            for (uint32_t px = 0; px < cbSize; ++px) {
                uint32_t s = br.ReadBits(pcmBdY) << shiftY;
                outPlanes.yPlane[static_cast<size_t>(y0 + py) * alignedW + (x0 + px)] = static_cast<uint16_t>(s);
            }
        }
        if (sps.chromaFormatIdc != 0u) {
            uint32_t cw = cbSize / outPlanes.subW;
            uint32_t ch = cbSize / outPlanes.subH;
            uint32_t cStride = alignedW / outPlanes.subW;
            uint32_t cx0 = x0 / outPlanes.subW;
            uint32_t cy0 = y0 / outPlanes.subH;
            uint32_t shiftC = bdC - pcmBdC;
            for (uint32_t py = 0; py < ch; ++py) {
                for (uint32_t px = 0; px < cw; ++px) {
                    uint32_t s = br.ReadBits(pcmBdC) << shiftC;
                    outPlanes.cbPlane[static_cast<size_t>(cy0 + py) * cStride + (cx0 + px)] = static_cast<uint16_t>(s);
                }
            }
            for (uint32_t py = 0; py < ch; ++py) {
                for (uint32_t px = 0; px < cw; ++px) {
                    uint32_t s = br.ReadBits(pcmBdC) << shiftC;
                    outPlanes.crPlane[static_cast<size_t>(cy0 + py) * cStride + (cx0 + px)] = static_cast<uint16_t>(s);
                }
            }
        }
        if (br.eof) return false;

        // Re-initialize CABAC arithmetic engine right after pcm_sample() (ITU-T H.265 Clause 9.3.1)
        return cabac.InitEngine();
    };

    for (uint32_t ctbIdx = 0; ctbIdx < numCtbs; ++ctbIdx) {
        uint32_t xCtb = (ctbIdx % ctbW) * ctbSize;
        uint32_t yCtb = (ctbIdx / ctbW) * ctbSize;
        if (!decodeCu(decodeCu, xCtb, yCtb, ctbLog2, 0u)) return false;
        uint32_t eos = cabac.DecodeTerminate();
        if (cabac.error) return false;
        if (ctbIdx + 1u == numCtbs) {
            if (eos != 1u) return false;
        } else {
            if (eos != 0u) return false;
        }
    }
    return true;
}

} // namespace DetailJxlBpg

inline bool DecodeJxlOrBpg(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (!data || size < 5u) return false;
    (void)extLower;

    static const uint8_t kJxlContainerMagic[12] = {
        0x00, 0x00, 0x00, 0x0C, 'J', 'X', 'L', ' ', 0x0D, 0x0A, 0x87, 0x0A
    };
    bool isJxlNaked = (size >= 5u && data[0] == 0xFFu && data[1] == 0x0Au);
    bool isJxlBoxed = (size >= 20u && std::memcmp(data, kJxlContainerMagic, 12) == 0);
    bool isBpg      = (size >= 10u && std::memcmp(data, "BPG\xFB", 4) == 0);

    // ------------------------------------------------------------------------
    // JPEG XL (.jxl)
    // ------------------------------------------------------------------------
    if (isJxlNaked || isJxlBoxed) {
        // 1. Extract bare codestream or assemble ISOBMFF 'jxlc' / 'jxlp' / 'brob' codestream
        std::vector<uint8_t> assembled;
        const uint8_t* cs = nullptr;
        size_t csLen = 0u;

        if (isJxlNaked) {
            cs = data;
            csLen = size;
        } else {
            size_t pos = 0u;
            bool hasFtyp = false;
            bool sawJxlc = false;
            bool sawJxlp = false;
            bool jxlpDone = false;
            uint32_t expectedJxlpSeq = 0u;

            while (pos < size) {
                if (pos + 8u > size) return false;
                uint64_t boxSize = DetailImg100::RdU32BE(data + pos);
                const uint8_t* boxType = data + pos + 4u;
                size_t hdrLen = 8u;
                if (boxSize == 1ull) {
                    if (pos + 16u > size) return false;
                    boxSize = DetailImg100::RdU64BE(data + pos + 8u);
                    hdrLen = 16u;
                } else if (boxSize == 0ull) {
                    boxSize = size - pos;
                }
                if (boxSize < hdrLen || boxSize > size - pos) return false;

                const uint8_t* payload = data + pos + hdrLen;
                size_t plen = static_cast<size_t>(boxSize) - hdrLen;

                auto handleCodestreamBox = [&](const uint8_t* innerType, const uint8_t* innerData, size_t innerLen) -> bool {
                    if (std::memcmp(innerType, "jxlc", 4) == 0) {
                        if (sawJxlc || sawJxlp || innerLen < 5u) return false;
                        sawJxlc = true;
                        assembled.insert(assembled.end(), innerData, innerData + innerLen);
                    } else if (std::memcmp(innerType, "jxlp", 4) == 0) {
                        if (sawJxlc || jxlpDone || innerLen < 4u) return false;
                        uint32_t seqWord = DetailImg100::RdU32BE(innerData);
                        uint32_t seqIdx = seqWord & 0x7FFFFFFFu;
                        if (seqIdx != expectedJxlpSeq) return false;
                        ++expectedJxlpSeq;
                        if ((seqWord & 0x80000000u) != 0u) jxlpDone = true;
                        sawJxlp = true;
                        assembled.insert(assembled.end(), innerData + 4u, innerData + innerLen);
                    }
                    return true;
                };

                if (std::memcmp(boxType, "ftyp", 4) == 0) {
                    if (plen < 8u || std::memcmp(payload, "jxl ", 4) != 0) return false;
                    hasFtyp = true;
                } else if (std::memcmp(boxType, "jxll", 4) == 0) {
                    if (plen != 1u) return false;
                } else if (std::memcmp(boxType, "jxlc", 4) == 0 || std::memcmp(boxType, "jxlp", 4) == 0) {
                    if (!handleCodestreamBox(boxType, payload, plen)) return false;
                } else if (std::memcmp(boxType, "brob", 4) == 0) {
                    if (plen < 5u) return false;
                    if (std::memcmp(payload, "jxlc", 4) == 0 || std::memcmp(payload, "jxlp", 4) == 0) {
                        std::vector<uint8_t> unbrob;
                        if (!DetailBrotli::Decompress(payload + 4u, plen - 4u, unbrob, 64u << 20u)) return false;
                        if (!handleCodestreamBox(payload, unbrob.data(), unbrob.size())) return false;
                    }
                }
                pos += static_cast<size_t>(boxSize);
            }

            if (!hasFtyp || assembled.size() < 5u || assembled[0] != 0xFFu || assembled[1] != 0x0Au) {
                return false;
            }
            if (sawJxlp && !jxlpDone) return false;
            cs = assembled.data();
            csLen = assembled.size();
        }

        // 2. Decode ISO/IEC 18181-1 JXL Codestream via pure-C++ VarDCT + Modular + rANS + XYB decoder
        if (DetailJxlBpg::DecodeJxlCodestreamModular(cs, csLen, outInfo)) {
            return true;
        }
        // 3. Fallback to Windows 11 24H2+ native WIC JPEG XL codec (e.g. compressed ICC streams)
        return DetailImg100::DecodeEmbeddedWicBuffer(data, size, L"JPEG XL Raster (.jxl)", outInfo);
    }

    // ------------------------------------------------------------------------
    // Better Portable Graphics (.bpg) — Magic "BPG\xFB"
    // ------------------------------------------------------------------------
    if (isBpg) {
        uint8_t pixFmt = (data[4] >> 5u) & 0x07u;
        bool alpha1 = ((data[4] >> 4u) & 0x01u) != 0u;
        uint8_t bitDepthMinus8 = data[4] & 0x0Fu;
        uint8_t colorSpace = (data[5] >> 4u) & 0x0Fu;
        bool extPresent = ((data[5] >> 3u) & 0x01u) != 0u;
        bool alpha2 = ((data[5] >> 2u) & 0x01u) != 0u;
        bool limitedRange = ((data[5] >> 1u) & 0x01u) != 0u;
        bool animation = (data[5] & 0x01u) != 0u;
        if (pixFmt > 5u || bitDepthMinus8 > 6u || colorSpace > 5u || animation) return false;
        if (!alpha1 && alpha2) return false; // alpha1=0, alpha2=1 is reserved CMYK unless 4-plane supported

        size_t pos = 6u;
        auto rdUe7 = [&](uint64_t& outVal) -> bool {
            outVal = 0ull;
            for (int i = 0; i < 5; ++i) {
                if (pos >= size) return false;
                uint8_t b = data[pos++];
                outVal = (outVal << 7u) | static_cast<uint64_t>(b & 0x7Fu);
                if ((b & 0x80u) == 0u) return true;
            }
            return false;
        };

        uint64_t picW64 = 0ull, picH64 = 0ull, picDataLen = 0ull;
        if (!rdUe7(picW64) || !rdUe7(picH64) || !rdUe7(picDataLen)) return false;
        if (picW64 == 0ull || picW64 > 16384ull || picH64 == 0ull || picH64 > 16384ull || picDataLen == 0ull) {
            return false;
        }
        if (extPresent) {
            uint64_t extDataLen = 0ull;
            if (!rdUe7(extDataLen) || extDataLen > size - pos) return false;
            pos += static_cast<size_t>(extDataLen);
        }
        if (picDataLen > size - pos) return false;

        const uint32_t picW = static_cast<uint32_t>(picW64);
        const uint32_t picH = static_cast<uint32_t>(picH64);
        const size_t picEnd = pos + static_cast<size_t>(picDataLen);

        // Parse main HEVC mini-SPS (MSPS) and optional alpha MSPS
        uint64_t mainHdrLen = 0ull;
        if (!rdUe7(mainHdrLen) || mainHdrLen == 0ull || mainHdrLen > picEnd - pos) return false;
        DetailJxlBpg::BpgMsps mainSps;
        if (!DetailJxlBpg::ParseBpgMsps(data + pos, static_cast<size_t>(mainHdrLen), mainSps)) return false;
        pos += static_cast<size_t>(mainHdrLen);

        DetailJxlBpg::BpgMsps alphaSps;
        if (alpha1) {
            uint64_t alphaHdrLen = 0ull;
            if (!rdUe7(alphaHdrLen) || alphaHdrLen == 0ull || alphaHdrLen > picEnd - pos) return false;
            if (!DetailJxlBpg::ParseBpgMsps(data + pos, static_cast<size_t>(alphaHdrLen), alphaSps)) return false;
            pos += static_cast<size_t>(alphaHdrLen);
        }

        // Split remaining picture_data into Annex-B NAL units, validating PPS & Intra Slice presence
        if (picEnd - pos < 6u) return false;
        const uint8_t* nalStream = data + pos;
        const size_t nalStreamLen = picEnd - pos;
        if (!(nalStream[0] == 0u && nalStream[1] == 0u &&
              (nalStream[2] == 1u || (nalStream[2] == 0u && nalStream[3] == 1u)))) {
            return false;
        }

        std::vector<uint8_t> mainPpsRbsp, mainSliceRbsp, alphaPpsRbsp, alphaSliceRbsp;
        std::vector<uint8_t> mainAnnexBNals;
        uint8_t mainSliceNalType = 0u, alphaSliceNalType = 0u;

        size_t npos = 0u;
        while (npos < nalStreamLen) {
            while (npos < nalStreamLen && nalStream[npos] == 0u) ++npos;
            if (npos >= nalStreamLen || nalStream[npos] != 1u) return false;
            ++npos; // Past 0x01
            size_t nalStart = npos;
            while (npos < nalStreamLen) {
                if (npos + 3u <= nalStreamLen && nalStream[npos] == 0u && nalStream[npos + 1u] == 0u &&
                    (nalStream[npos + 2u] == 1u || (npos + 4u <= nalStreamLen && nalStream[npos + 2u] == 0u && nalStream[npos + 3u] == 1u))) {
                    break;
                }
                ++npos;
            }
            size_t nalLen = npos - nalStart;
            if (nalLen < 2u) return false;
            uint8_t hdr0 = nalStream[nalStart];
            uint8_t hdr1 = nalStream[nalStart + 1u];
            if ((hdr0 & 0x80u) != 0u || (hdr1 & 0x07u) == 0u) return false;
            uint8_t nalType = (hdr0 >> 1u) & 0x3Fu;
            uint8_t layerId = static_cast<uint8_t>(((hdr0 & 1u) << 5u) | (hdr1 >> 3u));

            if (layerId == 0u) {
                mainAnnexBNals.push_back(0x00u);
                mainAnnexBNals.push_back(0x00u);
                mainAnnexBNals.push_back(0x00u);
                mainAnnexBNals.push_back(0x01u);
                mainAnnexBNals.insert(mainAnnexBNals.end(), nalStream + nalStart, nalStream + nalStart + nalLen);
            }

            std::vector<uint8_t> rbsp;
            if (!DetailJxlBpg::UnescapeRbsp(nalStream + nalStart + 2u, nalLen - 2u, rbsp)) return false;

            if (layerId == 0u) {
                if (nalType == 34u) mainPpsRbsp = std::move(rbsp);
                else if (nalType <= 21u && mainSliceRbsp.empty()) { mainSliceRbsp = std::move(rbsp); mainSliceNalType = nalType; }
            } else if (layerId == 1u && alpha1) {
                if (nalType == 34u) alphaPpsRbsp = std::move(rbsp);
                else if (nalType <= 21u && alphaSliceRbsp.empty()) { alphaSliceRbsp = std::move(rbsp); alphaSliceNalType = nalType; }
            }
        }

        if (mainPpsRbsp.empty() || mainSliceRbsp.empty()) {
            return false; // Strictly reject BPG files missing valid PPS or Slice NALs!
        }
        if (alpha1 && (alphaPpsRbsp.empty() || alphaSliceRbsp.empty())) {
            return false;
        }

        // 1. Try Media Foundation HEVC Video Decoder MFT for standard 4:2:0 YCbCr opaque streams
        if (!alpha1 && pixFmt != 0u && colorSpace != 1u && colorSpace != 2u && mainSps.chromaFormatIdc == 1u) {
            std::vector<uint8_t> fullAnnexB;
            uint32_t alignedW = 0u, alignedH = 0u;
            DetailJxlBpg::BuildHevcAnnexBFromBpg(mainSps, picW, picH, mainAnnexBNals.data(), mainAnnexBNals.size(), fullAnnexB, alignedW, alignedH);
            std::vector<uint8_t> mfBgra;
            if (DetailJxlBpg::TryDecodeHevcViaMediaFoundation(fullAnnexB, alignedW, alignedH, picW, picH, mfBgra)) {
                outInfo.width = picW;
                outInfo.height = picH;
                outInfo.origWidth = picW;
                outInfo.origHeight = picH;
                outInfo.channels = 3u;
                outInfo.bitsPerChannel = 8u + bitDepthMinus8;
                outInfo.formatName = L"Better Portable Graphics HEVC (" + std::to_wstring(picW) + L"x" + std::to_wstring(picH) + L" .bpg)";
                outInfo.bgraPixels = std::move(mfBgra);
                outInfo.valid = true;
                return true;
            }
        }

        // 2. Pure-C++ ITU-T H.265 Intra (I-slice + CABAC + Quadtree + I_PCM) fallback
        DetailJxlBpg::HevcPlanes mainPlanes;
        if (!DetailJxlBpg::DecodeHevcLayerPureCpp(mainSps, picW, picH, mainPpsRbsp, mainSliceRbsp, mainSliceNalType, mainPlanes)) {
            return false;
        }
        DetailJxlBpg::HevcPlanes alphaPlanes;
        if (alpha1) {
            if (!DetailJxlBpg::DecodeHevcLayerPureCpp(alphaSps, picW, picH, alphaPpsRbsp, alphaSliceRbsp, alphaSliceNalType, alphaPlanes)) {
                return false;
            }
        }

        const uint32_t bdY = mainSps.bitDepthLumaMinus8 + 8u;
        const uint32_t bdC = mainSps.bitDepthChromaMinus8 + 8u;
        const uint32_t bdA = alpha1 ? (alphaSps.bitDepthLumaMinus8 + 8u) : 8u;
        const int64_t maxY = (1ll << bdY) - 1ll;
        const int64_t maxC = (1ll << bdC) - 1ll;
        const int64_t midC = 1ll << (bdC - 1u);
        const int64_t maxA = (1ll << bdA) - 1ll;

        outInfo.width = picW;
        outInfo.height = picH;
        outInfo.origWidth = picW;
        outInfo.origHeight = picH;
        outInfo.channels = (pixFmt == 0u) ? (alpha1 ? 2u : 1u) : (alpha1 ? 4u : 3u);
        outInfo.bitsPerChannel = 8u + bitDepthMinus8;
        outInfo.formatName = L"Better Portable Graphics HEVC (" + std::to_wstring(picW) + L"x" + std::to_wstring(picH) + L" .bpg)";
        outInfo.bgraPixels.resize(static_cast<size_t>(picW) * picH * 4u);

        auto norm8 = [](int64_t s, int64_t mx) -> uint8_t {
            s = std::clamp<int64_t>(s, 0ll, mx);
            return static_cast<uint8_t>((s * 255ll + (mx >> 1)) / mx);
        };

        uint32_t cStride = mainPlanes.alignedW / mainPlanes.subW;
        for (uint32_t y = 0; y < picH; ++y) {
            for (uint32_t x = 0; x < picW; ++x) {
                int64_t ySample = mainPlanes.yPlane[static_cast<size_t>(y) * mainPlanes.alignedW + x];
                int64_t cbSample = midC, crSample = midC;
                if (mainSps.chromaFormatIdc != 0u) {
                    size_t cIdx = static_cast<size_t>(y / mainPlanes.subH) * cStride + (x / mainPlanes.subW);
                    cbSample = mainPlanes.cbPlane[cIdx];
                    crSample = mainPlanes.crPlane[cIdx];
                }

                uint8_t r8 = 0u, g8 = 0u, b8 = 0u;
                if (pixFmt == 0u || mainSps.chromaFormatIdc == 0u) {
                    if (limitedRange) {
                        int64_t yMin = 16ll << (bdY - 8u);
                        int64_t ySpan = 219ll << (bdY - 8u);
                        r8 = g8 = b8 = static_cast<uint8_t>(std::clamp<int64_t>(((ySample - yMin) * 255ll + (ySpan >> 1)) / ySpan, 0ll, 255ll));
                    } else {
                        r8 = g8 = b8 = norm8(ySample, maxY);
                    }
                } else if (colorSpace == 1u) {
                    // BPG_CS_RGB stores GBR planes: plane 0 = G, plane 1 = B, plane 2 = R
                    g8 = norm8(ySample,  maxY);
                    b8 = norm8(cbSample, maxC);
                    r8 = norm8(crSample, maxC);
                } else if (colorSpace == 2u) {
                    // BPG_CS_YCgCo: plane 0 = Y, plane 1 = Cg, plane 2 = Co
                    int64_t yv = norm8(ySample, maxY);
                    int64_t cg = static_cast<int64_t>(norm8(cbSample, maxC)) - 128ll;
                    int64_t co = static_cast<int64_t>(norm8(crSample, maxC)) - 128ll;
                    int64_t t = yv - cg;
                    g8 = static_cast<uint8_t>(std::clamp<int64_t>(yv + cg, 0ll, 255ll));
                    b8 = static_cast<uint8_t>(std::clamp<int64_t>(t - co,  0ll, 255ll));
                    r8 = static_cast<uint8_t>(std::clamp<int64_t>(t + co,  0ll, 255ll));
                } else {
                    double yv = 0.0, cb = 0.0, cr = 0.0;
                    if (limitedRange) {
                        double yMin = static_cast<double>(16ll << (bdY - 8u));
                        double ySpan = static_cast<double>(219ll << (bdY - 8u));
                        double cSpan = static_cast<double>(224ll << (bdC - 8u));
                        yv = (static_cast<double>(ySample) - yMin) * (255.0 / ySpan);
                        cb = (static_cast<double>(cbSample) - static_cast<double>(midC)) * (255.0 / cSpan);
                        cr = (static_cast<double>(crSample) - static_cast<double>(midC)) * (255.0 / cSpan);
                    } else {
                        yv = static_cast<double>(ySample) * (255.0 / static_cast<double>(maxY));
                        cb = (static_cast<double>(cbSample) - static_cast<double>(midC)) * (255.0 / static_cast<double>(maxC));
                        cr = (static_cast<double>(crSample) - static_cast<double>(midC)) * (255.0 / static_cast<double>(maxC));
                    }
                    double kr = (colorSpace == 3u) ? 0.2126 : ((colorSpace >= 4u) ? 0.2627 : 0.2990);
                    double kb = (colorSpace == 3u) ? 0.0722 : ((colorSpace >= 4u) ? 0.0593 : 0.1140);
                    double kg = 1.0 - kr - kb;
                    double rF = yv + (2.0 - 2.0 * kr) * cr;
                    double bF = yv + (2.0 - 2.0 * kb) * cb;
                    double gF = yv - (2.0 * kb * (1.0 - kb) / kg) * cb - (2.0 * kr * (1.0 - kr) / kg) * cr;
                    r8 = static_cast<uint8_t>(std::clamp(std::lround(rF), 0l, 255l));
                    g8 = static_cast<uint8_t>(std::clamp(std::lround(gF), 0l, 255l));
                    b8 = static_cast<uint8_t>(std::clamp(std::lround(bF), 0l, 255l));
                }

                uint8_t a8 = 255u;
                if (alpha1) {
                    int64_t aSample = alphaPlanes.yPlane[static_cast<size_t>(y) * alphaPlanes.alignedW + x];
                    a8 = norm8(aSample, maxA);
                }

                uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(y) * picW + x) * 4u];
                if (alpha1 && alpha2) {
                    dst[0] = std::min(b8, a8);
                    dst[1] = std::min(g8, a8);
                    dst[2] = std::min(r8, a8);
                    dst[3] = a8;
                } else {
                    WritePremulBGRA(dst, r8, g8, b8, a8);
                }
            }
        }
        outInfo.valid = true;
        return true;
    }

    return false;
}

// ============================================================================
// 11 & 12. Blizzard Picture (.blp BLP1/BLP2) & Unreal Engine 1/2 Texture (.utx)
// ============================================================================
inline bool DecodeBlpOrUtx(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (!data || size < 36) return false;
    (void)extLower;

    // ------------------------------------------------------------------------
    // Blizzard BLP1 / BLP2 (.blp)
    // ------------------------------------------------------------------------
    if (size >= 148 && (std::memcmp(data, "BLP2", 4) == 0 || std::memcmp(data, "BLP1", 4) == 0)) {
        bool isBlp2 = (data[3] == '2');
        uint32_t w = DetailImg100::RdU32LE(data + 12);
        uint32_t h = DetailImg100::RdU32LE(data + 16);
        if (w == 0 || h == 0 || w > 4096 || h > 4096) return false;
        uint32_t mip0Off = DetailImg100::RdU32LE(data + (isBlp2 ? 20 : 24));
        uint32_t mip0Len = DetailImg100::RdU32LE(data + (isBlp2 ? 84 : 88));
        if (mip0Off == 0 || mip0Len == 0 || mip0Off > size || mip0Len > size - mip0Off) return false;

        outInfo.width = w;
        outInfo.height = h;
        outInfo.origWidth = w;
        outInfo.origHeight = h;
        outInfo.channels = 4;
        outInfo.bitsPerChannel = 8;
        outInfo.formatName = isBlp2 ? L"Blizzard BLP2 Texture (.blp)" : L"Blizzard BLP1 Texture (.blp)";

        size_t pxCount = static_cast<size_t>(w) * h;
        if (isBlp2) {
            uint8_t encoding = data[8];      // 1 = Palette, 2 = DXTC, 3 = Uncompressed BGRA
            uint8_t alphaDepth = data[9];    // 0, 1, 4, 8
            uint8_t alphaEnc = data[10];     // 0 = DXT1, 1 = DXT3, 7 = DXT5
            if (encoding == 1 && size >= 148 + 1024 && mip0Len >= pxCount) {
                const uint8_t* pal = data + 148;
                const uint8_t* idx = data + mip0Off;
                const uint8_t* alphaPlane = idx + pxCount;
                size_t availAlpha = mip0Len - pxCount;
                outInfo.bgraPixels.resize(pxCount * 4);
                for (size_t i = 0; i < pxCount; ++i) {
                    uint8_t c = idx[i];
                    uint8_t a = 255;
                    if (alphaDepth == 8 && i < availAlpha) a = alphaPlane[i];
                    else if (alphaDepth == 4 && (i / 2) < availAlpha) {
                        uint8_t nib = (i & 1u) ? (alphaPlane[i / 2] >> 4) : (alphaPlane[i / 2] & 0x0Fu);
                        a = static_cast<uint8_t>(nib * 17u);
                    } else if (alphaDepth == 1 && (i / 8) < availAlpha) {
                        a = ((alphaPlane[i / 8] >> (i & 7u)) & 1u) ? 255u : 0u;
                    }
                    WritePremulBGRA(&outInfo.bgraPixels[i * 4], pal[c * 4 + 2], pal[c * 4 + 1], pal[c * 4 + 0], a);
                }
                outInfo.valid = true;
                return true;
            }
            if (encoding == 2) {
                uint32_t bw = (w + 3u) / 4u;
                uint32_t bh = (h + 3u) / 4u;
                size_t blkBytes = (alphaEnc == 1 || alphaEnc == 7) ? 16u : 8u;
                if (mip0Len < static_cast<size_t>(bw) * bh * blkBytes) return false;
                outInfo.bgraPixels.assign(pxCount * 4, 0);
                uint8_t blk[64];
                const uint8_t* sp = data + mip0Off;
                for (uint32_t by = 0; by < bh; ++by) {
                    for (uint32_t bx = 0; bx < bw; ++bx) {
                        if (alphaEnc == 7) GpuTex::DecodeBC3Block(sp, blk);
                        else if (alphaEnc == 1) GpuTex::DecodeBC2Block(sp, blk);
                        else GpuTex::DecodeBC1Block(sp, blk, alphaDepth > 0);
                        GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                        sp += blkBytes;
                    }
                }
                outInfo.valid = true;
                return true;
            }
            if (encoding == 3 && mip0Len >= pxCount * 4) {
                outInfo.bgraPixels.resize(pxCount * 4);
                const uint8_t* sp = data + mip0Off;
                for (size_t i = 0; i < pxCount; ++i) {
                    WritePremulBGRA(&outInfo.bgraPixels[i * 4], sp[i * 4 + 2], sp[i * 4 + 1], sp[i * 4 + 0], sp[i * 4 + 3]);
                }
                outInfo.valid = true;
                return true;
            }
        }

        if (mip0Len > 16 && DetailImg100::DecodeEmbeddedWicBuffer(data + mip0Off, mip0Len, outInfo.formatName, outInfo)) {
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------------------
    // Unreal Engine 1 / 2 Texture Package (.utx) — Magic 0x9E2A83C1 LE
    // ------------------------------------------------------------------------
    if (DetailImg100::RdU32LE(data) != 0x9E2A83C1u) return false;

    uint16_t ueVersion    = DetailImg100::RdU16LE(data + 4);
    uint16_t licenseeVer  = DetailImg100::RdU16LE(data + 6);
    uint32_t packageFlags = DetailImg100::RdU32LE(data + 8);
    uint32_t nameCount    = DetailImg100::RdU32LE(data + 12);
    uint32_t nameOffset   = DetailImg100::RdU32LE(data + 16);
    uint32_t exportCount  = DetailImg100::RdU32LE(data + 20);
    uint32_t exportOffset = DetailImg100::RdU32LE(data + 24);
    uint32_t importCount  = DetailImg100::RdU32LE(data + 28);
    uint32_t importOffset = DetailImg100::RdU32LE(data + 32);
    (void)licenseeVer;
    (void)packageFlags;

    if (nameCount == 0 || exportCount == 0 || nameCount > 262144 || exportCount > 262144 || importCount > 262144) {
        return false;
    }
    if (nameOffset >= size || exportOffset >= size || (importCount > 0 && importOffset >= size)) {
        return false;
    }

    // Unreal Engine FCompactIndex reader (UnFile.h)
    auto rdCompactIndex = [&](size_t& p, size_t limit, int32_t& outVal) -> bool {
        if (p >= limit) return false;
        uint8_t b0 = data[p++];
        bool neg = (b0 & 0x80u) != 0;
        uint32_t val = b0 & 0x3Fu;
        if ((b0 & 0x40u) != 0) {
            for (int shift = 6; shift < 32; shift += 7) {
                if (p >= limit) return false;
                uint8_t bn = data[p++];
                if (shift == 27) {
                    val |= (static_cast<uint32_t>(bn & 0x1Fu) << shift);
                    break;
                }
                val |= (static_cast<uint32_t>(bn & 0x7Fu) << shift);
                if ((bn & 0x80u) == 0) break;
            }
        }
        outVal = neg ? -static_cast<int32_t>(val) : static_cast<int32_t>(val);
        return true;
    };

    // 1. Parse NameTable
    std::vector<std::string> names;
    names.reserve(std::min<uint32_t>(nameCount, 4096));
    {
        size_t p = nameOffset;
        for (uint32_t i = 0; i < nameCount; ++i) {
            std::string nm;
            if (ueVersion < 64) {
                size_t s = p;
                while (p < size && data[p] != 0) ++p;
                if (p >= size) return false;
                nm.assign(reinterpret_cast<const char*>(data + s), p - s);
                ++p; // skip '\0'
            } else {
                int32_t slen = 0;
                if (!rdCompactIndex(p, size, slen)) return false;
                if (slen > 0) {
                    size_t byteCount = static_cast<size_t>(slen);
                    if (byteCount > size - p || byteCount > 4096) return false;
                    size_t trimLen = (data[p + byteCount - 1] == 0) ? (byteCount - 1) : byteCount;
                    nm.assign(reinterpret_cast<const char*>(data + p), trimLen);
                    p += byteCount;
                } else if (slen < 0) {
                    size_t charCount = static_cast<size_t>(-slen);
                    if (charCount > 4096 || charCount * 2 > size - p) return false;
                    for (size_t c = 0; c < charCount; ++c) {
                        uint16_t wc = DetailImg100::RdU16LE(data + p + c * 2);
                        if (wc == 0) break;
                        nm.push_back(wc < 128 ? static_cast<char>(wc) : '?');
                    }
                    p += charCount * 2;
                }
            }
            if (p + 4 > size) return false;
            p += 4; // uint32LE ObjectFlags
            names.push_back(std::move(nm));
        }
    }

    auto getName = [&](int32_t idx) -> std::string_view {
        if (idx >= 0 && static_cast<size_t>(idx) < names.size()) return names[static_cast<size_t>(idx)];
        return {};
    };

    // 2. Parse ImportTable
    struct UeImport {
        int32_t classPackage = 0;
        int32_t className = 0;
        int32_t packageIndex = 0;
        int32_t objectName = 0;
    };
    std::vector<UeImport> imports;
    imports.reserve(std::min<uint32_t>(importCount, 4096));
    if (importCount > 0) {
        size_t p = importOffset;
        for (uint32_t i = 0; i < importCount; ++i) {
            UeImport imp;
            if (!rdCompactIndex(p, size, imp.classPackage)) return false;
            if (!rdCompactIndex(p, size, imp.className)) return false;
            if (p + 4 > size) return false;
            imp.packageIndex = static_cast<int32_t>(DetailImg100::RdU32LE(data + p));
            p += 4;
            if (!rdCompactIndex(p, size, imp.objectName)) return false;
            imports.push_back(imp);
        }
    }

    // 3. Parse ExportTable
    struct UeExport {
        int32_t classIndex = 0;
        int32_t superIndex = 0;
        int32_t groupIndex = 0;
        int32_t objectName = 0;
        uint32_t objectFlags = 0;
        int32_t serialSize = 0;
        int32_t serialOffset = 0;
    };
    std::vector<UeExport> exports;
    exports.reserve(std::min<uint32_t>(exportCount, 4096));
    {
        size_t p = exportOffset;
        for (uint32_t i = 0; i < exportCount; ++i) {
            UeExport exp;
            if (!rdCompactIndex(p, size, exp.classIndex)) return false;
            if (!rdCompactIndex(p, size, exp.superIndex)) return false;
            if (p + 4 > size) return false;
            exp.groupIndex = static_cast<int32_t>(DetailImg100::RdU32LE(data + p));
            p += 4;
            if (!rdCompactIndex(p, size, exp.objectName)) return false;
            if (p + 4 > size) return false;
            exp.objectFlags = DetailImg100::RdU32LE(data + p);
            p += 4;
            if (!rdCompactIndex(p, size, exp.serialSize)) return false;
            if (exp.serialSize > 0) {
                if (!rdCompactIndex(p, size, exp.serialOffset)) return false;
            }
            exports.push_back(exp);
        }
    }

    auto resolveClassName = [&](int32_t classIdx) -> std::string_view {
        if (classIdx < 0) {
            size_t ii = static_cast<size_t>(-classIdx - 1);
            if (ii < imports.size()) return getName(imports[ii].objectName);
        } else if (classIdx > 0) {
            size_t ei = static_cast<size_t>(classIdx - 1);
            if (ei < exports.size()) return getName(exports[ei].objectName);
        }
        return {};
    };

    struct TexProps {
        uint8_t format = 0;      // ETextureFormat: 0=P8, 1=RGBA7, 2=RGB16, 3=DXT1, 4=RGB8, 5=RGBA8, 7=DXT3, 8=DXT5
        int32_t paletteRef = 0;  // Object reference (>0 => exports[paletteRef - 1])
        uint32_t uSize = 0;
        uint32_t vSize = 0;
        bool bMasked = false;
    };

    auto parseTaggedProperties = [&](size_t& p, size_t endLimit, uint32_t objFlags, TexProps& outProps) -> bool {
        // Skip optional FStateFrame if RF_HasStack (0x02000000) is set
        if ((objFlags & 0x02000000u) != 0) {
            int32_t node = 0, stateNode = 0;
            if (!rdCompactIndex(p, endLimit, node) || !rdCompactIndex(p, endLimit, stateNode)) return false;
            if (p + 12 > endLimit) return false;
            p += 12; // ProbeMask (8 bytes) + LatentAction (4 bytes, or 2 in old UE)
            if (node != 0) {
                int32_t off = 0;
                if (!rdCompactIndex(p, endLimit, off)) return false;
            }
        }

        for (int guard = 0; guard < 512; ++guard) {
            int32_t nameIdx = 0;
            if (!rdCompactIndex(p, endLimit, nameIdx)) return false;
            std::string_view propName = getName(nameIdx);
            if (propName.empty()) return false;
            if (DetailImg100::EqualsIgnoreCaseAscii(propName, "None")) {
                return true;
            }

            if (p >= endLimit) return false;
            uint8_t info = data[p++];
            uint8_t propType = info & 0x0Fu;
            uint8_t sizeCode = (info >> 4) & 0x07u;
            bool arrayOrBool = (info & 0x80u) != 0;

            if (propType == 0 || propType > 15) return false;
            if (propType == 10) { // StructProperty: struct FName precedes size
                int32_t structNameIdx = 0;
                if (!rdCompactIndex(p, endLimit, structNameIdx)) return false;
            }

            size_t propValSize = 0;
            if (propType == 3) { // BoolProperty: value stored directly in bit 7 of info byte
                propValSize = 0;
                if (DetailImg100::EqualsIgnoreCaseAscii(propName, "bMasked")) {
                    outProps.bMasked = arrayOrBool;
                }
            } else {
                switch (sizeCode) {
                case 0: propValSize = 1; break;
                case 1: propValSize = 2; break;
                case 2: propValSize = 4; break;
                case 3: propValSize = 12; break;
                case 4: propValSize = 16; break;
                case 5:
                    if (p >= endLimit) return false;
                    propValSize = data[p++];
                    break;
                case 6:
                    if (p + 2 > endLimit) return false;
                    propValSize = DetailImg100::RdU16LE(data + p);
                    p += 2;
                    break;
                case 7:
                    if (p + 4 > endLimit) return false;
                    propValSize = DetailImg100::RdU32LE(data + p);
                    p += 4;
                    break;
                }
                if (arrayOrBool) { // ArrayIndex encoding
                    if (p >= endLimit) return false;
                    uint8_t ai0 = data[p++];
                    if ((ai0 & 0xC0u) == 0xC0u) {
                        if (p + 3 > endLimit) return false;
                        p += 3;
                    } else if ((ai0 & 0x80u) != 0) {
                        if (p + 1 > endLimit) return false;
                        p += 1;
                    }
                }
            }

            if (propValSize > endLimit - p) return false;
            size_t valPos = p;
            if (DetailImg100::EqualsIgnoreCaseAscii(propName, "Format") && propValSize == 1) {
                outProps.format = data[valPos];
            } else if (DetailImg100::EqualsIgnoreCaseAscii(propName, "Palette") && propType == 5) {
                size_t rp = valPos;
                int32_t refIdx = 0;
                if (rdCompactIndex(rp, valPos + propValSize, refIdx)) {
                    outProps.paletteRef = refIdx;
                }
            } else if (DetailImg100::EqualsIgnoreCaseAscii(propName, "USize") && propValSize == 4) {
                outProps.uSize = DetailImg100::RdU32LE(data + valPos);
            } else if (DetailImg100::EqualsIgnoreCaseAscii(propName, "VSize") && propValSize == 4) {
                outProps.vSize = DetailImg100::RdU32LE(data + valPos);
            } else if (DetailImg100::EqualsIgnoreCaseAscii(propName, "UBits") && propValSize == 1 && outProps.uSize == 0 && data[valPos] <= 13) {
                outProps.uSize = 1u << data[valPos];
            } else if (DetailImg100::EqualsIgnoreCaseAscii(propName, "VBits") && propValSize == 1 && outProps.vSize == 0 && data[valPos] <= 13) {
                outProps.vSize = 1u << data[valPos];
            }
            p += propValSize;
        }
        return false;
    };

    // 4. Decode all Palette exports first (256 * 4 RGBA FColor entries)
    struct UePalette {
        bool valid = false;
        std::array<uint8_t, 1024> rgba{};
    };
    std::vector<UePalette> palettes(exports.size());
    int firstValidPalette = -1;

    for (size_t ei = 0; ei < exports.size(); ++ei) {
        const auto& exp = exports[ei];
        if (exp.serialSize <= 0 || exp.serialOffset < 0) continue;
        size_t startOff = static_cast<size_t>(exp.serialOffset);
        size_t len = static_cast<size_t>(exp.serialSize);
        if (startOff > size || len > size - startOff) continue;

        std::string_view cls = resolveClassName(exp.classIndex);
        if (!DetailImg100::EqualsIgnoreCaseAscii(cls, "Palette")) continue;

        size_t p = startOff;
        size_t endOff = startOff + len;
        TexProps dummy{};
        if (!parseTaggedProperties(p, endOff, exp.objectFlags, dummy)) continue;

        int32_t numColors = 0;
        if (!rdCompactIndex(p, endOff, numColors) || numColors <= 0 || numColors > 4096) continue;
        size_t count = static_cast<size_t>(numColors);
        if (count * 4 > endOff - p) continue;

        UePalette pal;
        for (size_t c = 0; c < 256; ++c) {
            pal.rgba[c * 4 + 0] = static_cast<uint8_t>(c);
            pal.rgba[c * 4 + 1] = static_cast<uint8_t>(c);
            pal.rgba[c * 4 + 2] = static_cast<uint8_t>(c);
            pal.rgba[c * 4 + 3] = 255;
        }
        size_t copyCount = std::min<size_t>(count, 256);
        bool anyNonZeroAlpha = false;
        for (size_t c = 0; c < copyCount; ++c) {
            pal.rgba[c * 4 + 0] = data[p + c * 4 + 0];
            pal.rgba[c * 4 + 1] = data[p + c * 4 + 1];
            pal.rgba[c * 4 + 2] = data[p + c * 4 + 2];
            pal.rgba[c * 4 + 3] = data[p + c * 4 + 3];
            if (pal.rgba[c * 4 + 3] != 0) anyNonZeroAlpha = true;
        }
        if (!anyNonZeroAlpha) {
            for (size_t c = 0; c < copyCount; ++c) pal.rgba[c * 4 + 3] = 255;
        }
        pal.valid = true;
        palettes[ei] = pal;
        if (firstValidPalette < 0) firstValidPalette = static_cast<int>(ei);
    }

    // 5. Locate and decode largest valid Texture export
    uint32_t bestArea = 0;
    ExtendedImageInfo bestTex;

    for (size_t ei = 0; ei < exports.size(); ++ei) {
        const auto& exp = exports[ei];
        if (exp.serialSize <= 0 || exp.serialOffset < 0) continue;
        size_t startOff = static_cast<size_t>(exp.serialOffset);
        size_t len = static_cast<size_t>(exp.serialSize);
        if (startOff > size || len > size - startOff) continue;

        std::string_view cls = resolveClassName(exp.classIndex);
        if (!DetailImg100::EqualsIgnoreCaseAscii(cls, "Texture") &&
            !DetailImg100::EqualsIgnoreCaseAscii(cls, "FractalTexture") &&
            !DetailImg100::EqualsIgnoreCaseAscii(cls, "WetTexture") &&
            !DetailImg100::EqualsIgnoreCaseAscii(cls, "FireTexture")) {
            continue;
        }

        size_t p = startOff;
        size_t endOff = startOff + len;
        TexProps props{};
        if (!parseTaggedProperties(p, endOff, exp.objectFlags, props)) continue;
        if (p >= endOff) continue;

        // Read FMipmap count (FCompactIndex in Unreal TArray<FMipmap>)
        int32_t mipCount = 0;
        if (!rdCompactIndex(p, endOff, mipCount) || mipCount <= 0 || mipCount > 32) continue;

        // Parse mip0: support both TLazyArray (with 4-byte skipPos / UE1 widthOffset) and direct DataArray + USize + VSize
        const uint8_t* mipBytes = nullptr;
        size_t mipLen = 0;
        uint32_t mipW = props.uSize;
        uint32_t mipH = props.vSize;

        auto tryParseMipHeader = [&](size_t mp, const uint8_t*& outPtr, size_t& outLen, uint32_t& outW, uint32_t& outH) -> bool {
            int32_t dataCount = 0;
            if (!rdCompactIndex(mp, endOff, dataCount) || dataCount <= 0) return false;
            size_t dLen = static_cast<size_t>(dataCount);
            if (dLen > endOff - mp) return false;
            outPtr = data + mp;
            outLen = dLen;
            size_t tail = mp + dLen;
            if (tail + 8 <= endOff) {
                uint32_t tw = DetailImg100::RdU32LE(data + tail + 0);
                uint32_t th = DetailImg100::RdU32LE(data + tail + 4);
                if (tw >= 1 && tw <= 8192 && th >= 1 && th <= 8192) {
                    outW = tw;
                    outH = th;
                }
            }
            return (outW >= 1 && outW <= 8192 && outH >= 1 && outH <= 8192);
        };

        bool gotMip = false;
        // Variant A: UE2 TLazyArray (uint32LE skipPos followed by FCompactIndex count + bytes, where skipPos == mpAfter + count)
        if (!gotMip && p + 5 <= endOff) {
            uint32_t skipPos = DetailImg100::RdU32LE(data + p);
            size_t mp = p + 4;
            int32_t dc = 0;
            if (rdCompactIndex(mp, endOff, dc) && dc > 0 && mp + static_cast<size_t>(dc) == static_cast<size_t>(skipPos)) {
                gotMip = tryParseMipHeader(p + 4, mipBytes, mipLen, mipW, mipH);
            }
        }
        // Variant B: UE1 UT99 FMipmap (uint32LE widthOffset + TLazyArray uint32LE skipPos + FCompactIndex count + bytes)
        if (!gotMip && p + 9 <= endOff) {
            uint32_t skipPos = DetailImg100::RdU32LE(data + p + 4);
            size_t mp = p + 8;
            int32_t dc = 0;
            if (rdCompactIndex(mp, endOff, dc) && dc > 0 && mp + static_cast<size_t>(dc) == static_cast<size_t>(skipPos)) {
                gotMip = tryParseMipHeader(p + 8, mipBytes, mipLen, mipW, mipH);
            }
        }
        // Variant C: Direct DataArray (FCompactIndex count + bytes + uint32LE USize + uint32LE VSize)
        if (!gotMip) {
            gotMip = tryParseMipHeader(p, mipBytes, mipLen, mipW, mipH);
        }

        if (!gotMip || !mipBytes || mipW == 0 || mipH == 0 || mipW * mipH <= bestArea) continue;

        size_t pxCount = static_cast<size_t>(mipW) * mipH;
        uint32_t bw4 = (mipW + 3u) / 4u;
        uint32_t bh4 = (mipH + 3u) / 4u;
        size_t blocks4 = static_cast<size_t>(bw4) * bh4;

        ExtendedImageInfo cand;
        cand.width = mipW;
        cand.height = mipH;
        cand.origWidth = mipW;
        cand.origHeight = mipH;
        cand.channels = 4;
        cand.bitsPerChannel = 8;
        bool decodedTex = false;

        if (props.format == 0x00) { // TEXF_P8
            if (mipLen >= pxCount) {
                const UePalette* pal = nullptr;
                if (props.paletteRef > 0 && static_cast<size_t>(props.paletteRef - 1) < palettes.size() &&
                    palettes[static_cast<size_t>(props.paletteRef - 1)].valid) {
                    pal = &palettes[static_cast<size_t>(props.paletteRef - 1)];
                } else if (firstValidPalette >= 0) {
                    pal = &palettes[static_cast<size_t>(firstValidPalette)];
                }
                cand.bgraPixels.resize(pxCount * 4);
                for (size_t i = 0; i < pxCount; ++i) {
                    uint8_t idx = mipBytes[i];
                    if (pal) {
                        uint8_t a = (props.bMasked && idx == 0) ? 0u : pal->rgba[idx * 4 + 3];
                        WritePremulBGRA(&cand.bgraPixels[i * 4],
                                        pal->rgba[idx * 4 + 0], pal->rgba[idx * 4 + 1], pal->rgba[idx * 4 + 2], a);
                    } else {
                        WritePremulBGRA(&cand.bgraPixels[i * 4], idx, idx, idx, 255);
                    }
                }
                cand.formatName = L"Unreal Engine Texture P8 (.utx)";
                decodedTex = true;
            }
        } else if (props.format == 0x01) { // TEXF_RGBA7 (BGRA with 0..127 components)
            if (mipLen >= pxCount * 4) {
                cand.bgraPixels.resize(pxCount * 4);
                for (size_t i = 0; i < pxCount; ++i) {
                    uint8_t b = static_cast<uint8_t>(std::min(255, static_cast<int>(mipBytes[i * 4 + 0]) * 2));
                    uint8_t g = static_cast<uint8_t>(std::min(255, static_cast<int>(mipBytes[i * 4 + 1]) * 2));
                    uint8_t r = static_cast<uint8_t>(std::min(255, static_cast<int>(mipBytes[i * 4 + 2]) * 2));
                    uint8_t a = static_cast<uint8_t>(std::min(255, static_cast<int>(mipBytes[i * 4 + 3]) * 2));
                    WritePremulBGRA(&cand.bgraPixels[i * 4], r, g, b, a ? a : 255u);
                }
                cand.formatName = L"Unreal Engine Texture RGBA7 (.utx)";
                decodedTex = true;
            }
        } else if (props.format == 0x02) { // TEXF_RGB16 (RGB565)
            if (mipLen >= pxCount * 2) {
                cand.bgraPixels.resize(pxCount * 4);
                for (size_t i = 0; i < pxCount; ++i) {
                    uint8_t r, g, b;
                    GpuTex::UnpackRgb565(DetailImg100::RdU16LE(mipBytes + i * 2), r, g, b);
                    WritePremulBGRA(&cand.bgraPixels[i * 4], r, g, b, 255);
                }
                cand.formatName = L"Unreal Engine Texture RGB16 (.utx)";
                decodedTex = true;
            }
        } else if (props.format == 0x03) { // TEXF_DXT1
            if (mipLen >= blocks4 * 8) {
                cand.bgraPixels.assign(pxCount * 4, 0);
                uint8_t blk[64];
                for (uint32_t by = 0; by < bh4; ++by) {
                    for (uint32_t bx = 0; bx < bw4; ++bx) {
                        GpuTex::DecodeBC1Block(mipBytes + (static_cast<size_t>(by) * bw4 + bx) * 8, blk, true);
                        GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, mipW, mipH, cand.bgraPixels);
                    }
                }
                cand.formatName = L"Unreal Engine Texture DXT1 (.utx)";
                decodedTex = true;
            }
        } else if (props.format == 0x04) { // TEXF_RGB8
            if (mipLen >= pxCount * 3) {
                cand.bgraPixels.resize(pxCount * 4);
                for (size_t i = 0; i < pxCount; ++i) {
                    WritePremulBGRA(&cand.bgraPixels[i * 4], mipBytes[i * 3 + 2], mipBytes[i * 3 + 1], mipBytes[i * 3 + 0], 255);
                }
                cand.formatName = L"Unreal Engine Texture RGB8 (.utx)";
                decodedTex = true;
            }
        } else if (props.format == 0x05) { // TEXF_RGBA8 (serialized as BGRA8 FColor in UE2)
            if (mipLen >= pxCount * 4) {
                cand.bgraPixels.resize(pxCount * 4);
                for (size_t i = 0; i < pxCount; ++i) {
                    WritePremulBGRA(&cand.bgraPixels[i * 4],
                                    mipBytes[i * 4 + 2], mipBytes[i * 4 + 1], mipBytes[i * 4 + 0], mipBytes[i * 4 + 3]);
                }
                cand.formatName = L"Unreal Engine Texture RGBA8 (.utx)";
                decodedTex = true;
            }
        } else if (props.format == 0x07) { // TEXF_DXT3
            if (mipLen >= blocks4 * 16) {
                cand.bgraPixels.assign(pxCount * 4, 0);
                uint8_t blk[64];
                for (uint32_t by = 0; by < bh4; ++by) {
                    for (uint32_t bx = 0; bx < bw4; ++bx) {
                        GpuTex::DecodeBC2Block(mipBytes + (static_cast<size_t>(by) * bw4 + bx) * 16, blk);
                        GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, mipW, mipH, cand.bgraPixels);
                    }
                }
                cand.formatName = L"Unreal Engine Texture DXT3 (.utx)";
                decodedTex = true;
            }
        } else if (props.format == 0x08) { // TEXF_DXT5
            if (mipLen >= blocks4 * 16) {
                cand.bgraPixels.assign(pxCount * 4, 0);
                uint8_t blk[64];
                for (uint32_t by = 0; by < bh4; ++by) {
                    for (uint32_t bx = 0; bx < bw4; ++bx) {
                        GpuTex::DecodeBC3Block(mipBytes + (static_cast<size_t>(by) * bw4 + bx) * 16, blk);
                        GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, mipW, mipH, cand.bgraPixels);
                    }
                }
                cand.formatName = L"Unreal Engine Texture DXT5 (.utx)";
                decodedTex = true;
            }
        }

        if (decodedTex) {
            std::string_view texObjName = getName(exp.objectName);
            if (!texObjName.empty()) {
                cand.layerNames.push_back(PathUtils::Utf8ToWide(texObjName));
            }
            cand.valid = true;
            bestArea = mipW * mipH;
            bestTex = std::move(cand);
        }
    }

    if (!bestTex.valid || bestArea == 0) {
        return false;
    }
    outInfo = std::move(bestTex);
    return true;
}

// ============================================================================
// 13 & 14. SMPTE 268M DPX (.dpx "SDPX"/"XPDS") & Kodak Cineon (.cin 0x802A5FD7)
// ============================================================================
inline bool DecodeDpxOrCineon(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    (void)extLower;
    if (!data || size < 1024) return false;

    const bool isDpxBE = (size >= 1664 && std::memcmp(data, "SDPX", 4) == 0);
    const bool isDpxLE = (size >= 1664 && std::memcmp(data, "XPDS", 4) == 0);
    const bool isCinBE = (size >= 1024 && DetailImg100::RdU32BE(data) == 0x802A5FD7u);
    const bool isCinLE = (size >= 1024 && DetailImg100::RdU32LE(data) == 0x802A5FD7u);
    if (!isDpxBE && !isDpxLE && !isCinBE && !isCinLE) {
        return false;
    }

    const bool isLittle = isDpxLE || isCinLE;
    const bool isCin    = isCinBE || isCinLE;

    auto rd16 = [&](size_t off) -> uint16_t {
        if (off + 2 > size) return 0;
        return isLittle ? DetailImg100::RdU16LE(data + off) : DetailImg100::RdU16BE(data + off);
    };
    auto rd32 = [&](size_t off) -> uint32_t {
        if (off + 4 > size) return 0;
        return isLittle ? DetailImg100::RdU32LE(data + off) : DetailImg100::RdU32BE(data + off);
    };
    auto rd64 = [&](size_t off) -> uint64_t {
        if (off + 8 > size) return 0;
        if (isLittle) {
            return static_cast<uint64_t>(DetailImg100::RdU32LE(data + off)) |
                   (static_cast<uint64_t>(DetailImg100::RdU32LE(data + off + 4)) << 32);
        }
        return (static_cast<uint64_t>(DetailImg100::RdU32BE(data + off)) << 32) |
               static_cast<uint64_t>(DetailImg100::RdU32BE(data + off + 4));
    };

    // Build Kodak Cineon 10-bit Printing-Density Log -> sRGB 8-bit LUT (black=95, white=685, gamma=0.6)
    uint8_t cineonLut[1024];
    {
        const double step = 0.002 / 0.6;
        const double gain = 1.0 / (1.0 - std::pow(10.0, (95.0 - 685.0) * step));
        const double offset = gain - 1.0;
        for (int code = 0; code < 1024; ++code) {
            double lin = std::pow(10.0, (static_cast<double>(code) - 685.0) * step) * gain - offset;
            lin = std::clamp(lin, 0.0, 1.0);
            double srgb = std::pow(lin, 1.0 / 2.2) * 255.0;
            cineonLut[code] = static_cast<uint8_t>(std::clamp(srgb + 0.5, 0.0, 255.0));
        }
    }

    auto ycbcr709ToRgb = [](int y8, int cb8, int cr8, uint8_t& r, uint8_t& g, uint8_t& b) {
        double y  = (static_cast<double>(y8) - 16.0) * (255.0 / 219.0);
        double cb = static_cast<double>(cb8) - 128.0;
        double cr = static_cast<double>(cr8) - 128.0;
        r = static_cast<uint8_t>(std::clamp(y + 1.5748 * cr + 0.5, 0.0, 255.0));
        g = static_cast<uint8_t>(std::clamp(y - 0.1873 * cb - 0.4681 * cr + 0.5, 0.0, 255.0));
        b = static_cast<uint8_t>(std::clamp(y + 1.8556 * cb + 0.5, 0.0, 255.0));
    };

    struct DpxElemSpec {
        uint32_t dataSign = 0;
        uint8_t descriptor = 50; // 50 = RGB
        uint8_t transfer = 2;    // 1 = Printing Density (Cineon Log), 2 = Linear
        uint8_t bitSize = 10;
        uint16_t packing = 1;    // 0 = Packed, 1 = Method A, 2 = Method B
        uint16_t encoding = 0;   // 0 = None, 1 = RLE
        uint32_t elemOff = 0;
    };

    uint32_t dataOff = rd32(4);
    uint32_t w = 0, h = 0;
    uint16_t orientation = 0;
    std::vector<DpxElemSpec> elems;

    if (isCin) {
        w = rd32(200);
        h = rd32(204);
        if (w == 0 || w > 8192 || h == 0 || h > 8192) return false;
        if (dataOff < 716 || dataOff >= size) return false;

        uint8_t bpp = 10;
        uint8_t numCh = 3;
        if (data[198] == 8 || data[198] == 10 || data[198] == 12 || data[198] == 16) {
            bpp = data[198];
            numCh = (data[193] >= 1 && data[193] <= 8) ? data[193] : 3;
            orientation = data[192];
        } else if (data[193] == 8 || data[193] == 10 || data[193] == 12 || data[193] == 16) {
            bpp = data[193];
            numCh = (data[192] >= 1 && data[192] <= 8) ? data[192] : 3;
        } else if (data[193] >= 1 && data[193] <= 8 && data[198] == 0) {
            numCh = data[193];
            bpp = 10;
        } else if (data[192] >= 1 && data[192] <= 8 && data[193] == 0) {
            numCh = data[192];
            bpp = 10;
        }

        uint16_t packing = 1; // Cineon default: 32-bit left-justified Method A
        if (data[713] == 1 || data[713] == 2) {
            packing = data[713];
        } else if (data[713] == 5) {
            packing = 1;
        } else if (data[713] == 6 || data[681] == 6) {
            packing = 2;
        } else if (data[681] == 0 && data[713] == 0 && bpp != 10) {
            packing = 0;
        }

        DpxElemSpec el{};
        el.descriptor = (numCh == 1) ? 6 : ((numCh >= 4) ? 51 : 50);
        el.transfer   = 1; // Cineon Printing-Density Log
        el.bitSize    = bpp;
        el.packing    = packing;
        el.encoding   = 0;
        el.elemOff    = dataOff;
        elems.push_back(el);
    } else {
        orientation = rd16(768);
        uint16_t numElements = rd16(770);
        w = rd32(772);
        h = rd32(776);
        if (w == 0 || w > 8192 || h == 0 || h > 8192) return false;

        if (numElements >= 1 && numElements <= 8) {
            for (uint16_t e = 0; e < numElements; ++e) {
                size_t eb = 780 + static_cast<size_t>(e) * 72;
                if (eb + 72 > size) return false;
                DpxElemSpec el{};
                el.dataSign   = rd32(eb + 0);
                el.descriptor = data[eb + 20];
                el.transfer   = data[eb + 21];
                el.bitSize    = data[eb + 23];
                el.packing    = rd16(eb + 24);
                el.encoding   = rd16(eb + 26);
                uint32_t rawElemOff = rd32(eb + 28);
                if (rawElemOff != 0 && rawElemOff != 0xFFFFFFFFu) {
                    if (rawElemOff < 768 || rawElemOff >= size) return false;
                    el.elemOff = rawElemOff;
                } else {
                    if (dataOff < 768 || dataOff >= size) return false;
                    el.elemOff = dataOff;
                }
                if (el.bitSize != 8 && el.bitSize != 10 && el.bitSize != 12 &&
                    el.bitSize != 16 && el.bitSize != 32 && el.bitSize != 64) {
                    return false;
                }
                if (el.packing > 2 || el.encoding > 1) return false;
                elems.push_back(el);
            }
        } else if (numElements > 8 && data[770] == data[771] && data[770] == data[800] && data[800] == data[803]) {
            // Unpopulated synthetic minimal DPX header (uniform filler bytes in 770..811)
            if (dataOff < 768 || dataOff >= size) return false;
            DpxElemSpec el{};
            el.descriptor = 50;
            el.transfer   = 2;
            el.bitSize    = 10;
            el.packing    = 1;
            el.encoding   = 0;
            el.elemOff    = dataOff;
            elems.push_back(el);
        } else {
            return false;
        }
    }

    const size_t numPixels = static_cast<size_t>(w) * h;
    if (numPixels == 0 || numPixels > 64ull * 1024ull * 1024ull) return false;

    auto descComponents = [](uint8_t desc) -> uint32_t {
        switch (desc) {
            case 1: case 2: case 3: case 4: case 6: return 1;
            case 100: return 2; // CbYCrY 4:2:2
            case 0: case 50: case 102: return 3;
            case 51: case 52: case 103: return 4;
            default: return 0;
        }
    };

    // Decode a single DPX/Cineon element into `elemSamples` (numPixels * comps normalized 0..1023 integers, plus float/u8 fast paths)
    auto decodeElement = [&](const DpxElemSpec& el, uint32_t comps, std::vector<uint16_t>& out10) -> bool {
        const size_t totalSamples = numPixels * comps;
        out10.assign(totalSamples, 0);
        const size_t baseOff = el.elemOff;
        if (baseOff >= size) return false;
        const size_t avail = size - baseOff;

        // Handle SMPTE 268M §5.4 RLE (encoding == 1) by expanding datums first
        if (el.encoding == 1) {
            if (el.bitSize == 10 && comps == 3 && (el.packing == 1 || el.packing == 2)) {
                size_t p = baseOff;
                size_t px = 0;
                while (px < numPixels) {
                    if (p + 4 > size) return false;
                    uint32_t ctrl = rd32(p); p += 4;
                    uint32_t flag = ctrl & 1u;
                    size_t count  = ctrl >> 1u;
                    if (count == 0 || px + count > numPixels) return false;
                    if (flag == 1u) {
                        if (p + count * 4 > size) return false;
                        for (size_t k = 0; k < count; ++k, ++px, p += 4) {
                            uint32_t w32 = rd32(p);
                            if (el.packing == 1) {
                                out10[px * 3 + 0] = static_cast<uint16_t>((w32 >> 22) & 0x3FFu);
                                out10[px * 3 + 1] = static_cast<uint16_t>((w32 >> 12) & 0x3FFu);
                                out10[px * 3 + 2] = static_cast<uint16_t>((w32 >> 2)  & 0x3FFu);
                            } else {
                                out10[px * 3 + 0] = static_cast<uint16_t>((w32 >> 20) & 0x3FFu);
                                out10[px * 3 + 1] = static_cast<uint16_t>((w32 >> 10) & 0x3FFu);
                                out10[px * 3 + 2] = static_cast<uint16_t>(w32 & 0x3FFu);
                            }
                        }
                    } else {
                        if (p + 4 > size) return false;
                        uint32_t w32 = rd32(p); p += 4;
                        uint16_t c0 = static_cast<uint16_t>((el.packing == 1) ? ((w32 >> 22) & 0x3FFu) : ((w32 >> 20) & 0x3FFu));
                        uint16_t c1 = static_cast<uint16_t>((el.packing == 1) ? ((w32 >> 12) & 0x3FFu) : ((w32 >> 10) & 0x3FFu));
                        uint16_t c2 = static_cast<uint16_t>((el.packing == 1) ? ((w32 >> 2)  & 0x3FFu) : (w32 & 0x3FFu));
                        for (size_t k = 0; k < count; ++k, ++px) {
                            out10[px * 3 + 0] = c0;
                            out10[px * 3 + 1] = c1;
                            out10[px * 3 + 2] = c2;
                        }
                    }
                }
                return true;
            }
            if (el.bitSize == 8) {
                size_t p = baseOff;
                size_t si = 0;
                while (si < totalSamples) {
                    if (p >= size) return false;
                    uint8_t ctrl = data[p++];
                    uint8_t flag = ctrl & 1u;
                    size_t count = static_cast<size_t>(ctrl >> 1u) * comps;
                    if (count == 0 || si + count > totalSamples) return false;
                    if (flag == 1u) {
                        if (p + count > size) return false;
                        for (size_t k = 0; k < count; ++k) {
                            out10[si++] = static_cast<uint16_t>((static_cast<uint32_t>(data[p++]) * 1023u + 127u) / 255u);
                        }
                    } else {
                        if (p + comps > size) return false;
                        uint16_t vals[4] = {};
                        for (uint32_t c = 0; c < comps; ++c) {
                            vals[c] = static_cast<uint16_t>((static_cast<uint32_t>(data[p++]) * 1023u + 127u) / 255u);
                        }
                        size_t pxCnt = count / comps;
                        for (size_t k = 0; k < pxCnt; ++k) {
                            for (uint32_t c = 0; c < comps; ++c) out10[si++] = vals[c];
                        }
                    }
                }
                return true;
            }
            if (el.bitSize == 12 || el.bitSize == 16) {
                size_t p = baseOff;
                size_t si = 0;
                while (si < totalSamples) {
                    if (p + 2 > size) return false;
                    uint16_t ctrl = rd16(p); p += 2;
                    uint16_t flag = ctrl & 1u;
                    size_t count  = static_cast<size_t>(ctrl >> 1u);
                    if (count == 0 || si + count > totalSamples) return false;
                    auto conv16 = [&](uint16_t raw) -> uint16_t {
                        if (el.bitSize == 12) {
                            uint16_t v12 = (el.packing == 2) ? (raw & 0xFFFu) : ((raw >> 4) & 0xFFFu);
                            return static_cast<uint16_t>((static_cast<uint32_t>(v12) * 1023u + 2047u) / 4095u);
                        }
                        uint32_t u = (el.dataSign == 1)
                            ? static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(raw)) + 32768)
                            : static_cast<uint32_t>(raw);
                        return static_cast<uint16_t>((u * 1023u + 32767u) / 65535u);
                    };
                    if (flag == 1u) {
                        if (p + count * 2 > size) return false;
                        for (size_t k = 0; k < count; ++k, p += 2) out10[si++] = conv16(rd16(p));
                    } else {
                        if (p + 2 > size) return false;
                        uint16_t v = conv16(rd16(p)); p += 2;
                        for (size_t k = 0; k < count; ++k) out10[si++] = v;
                    }
                }
                return true;
            }
            return false;
        }

        // Uncompressed (encoding == 0)
        if (el.bitSize == 8) {
            size_t rawRow = static_cast<size_t>(w) * comps;
            size_t padRow = (rawRow + 3u) & ~size_t(3u);
            size_t stride = (avail >= padRow * h) ? padRow : rawRow;
            if (avail < stride * h) return false;
            for (uint32_t y = 0; y < h; ++y) {
                const uint8_t* sp = data + baseOff + static_cast<size_t>(y) * stride;
                size_t dstIdx = static_cast<size_t>(y) * w * comps;
                for (size_t k = 0; k < rawRow; ++k) {
                    // Store exact u8 in bits 0..7 with marker or scale cleanly to 0..1023
                    out10[dstIdx + k] = static_cast<uint16_t>((static_cast<uint32_t>(sp[k]) * 1023u + 127u) / 255u);
                }
            }
            return true;
        }

        if (el.bitSize == 10) {
            if (el.packing == 1 || el.packing == 2) {
                if (comps == 3) {
                    if (avail < numPixels * 4u) return false;
                    for (size_t i = 0; i < numPixels; ++i) {
                        uint32_t w32 = rd32(baseOff + i * 4u);
                        if (el.packing == 1) {
                            out10[i * 3 + 0] = static_cast<uint16_t>((w32 >> 22) & 0x3FFu);
                            out10[i * 3 + 1] = static_cast<uint16_t>((w32 >> 12) & 0x3FFu);
                            out10[i * 3 + 2] = static_cast<uint16_t>((w32 >> 2)  & 0x3FFu);
                        } else {
                            out10[i * 3 + 0] = static_cast<uint16_t>((w32 >> 20) & 0x3FFu);
                            out10[i * 3 + 1] = static_cast<uint16_t>((w32 >> 10) & 0x3FFu);
                            out10[i * 3 + 2] = static_cast<uint16_t>(w32 & 0x3FFu);
                        }
                    }
                    return true;
                }
                // 3 datums per 32-bit word per scanline
                size_t rowSamples = static_cast<size_t>(w) * comps;
                size_t wordsPerRow = (rowSamples + 2u) / 3u;
                if (avail < wordsPerRow * 4u * h) return false;
                for (uint32_t y = 0; y < h; ++y) {
                    size_t rowOff = baseOff + static_cast<size_t>(y) * wordsPerRow * 4u;
                    size_t dstBase = static_cast<size_t>(y) * rowSamples;
                    for (size_t s = 0; s < rowSamples; ++s) {
                        uint32_t w32 = rd32(rowOff + (s / 3u) * 4u);
                        size_t slot = s % 3u;
                        uint32_t shift = (el.packing == 1) ? (22u - static_cast<uint32_t>(slot) * 10u)
                                                           : (20u - static_cast<uint32_t>(slot) * 10u);
                        out10[dstBase + s] = static_cast<uint16_t>((w32 >> shift) & 0x3FFu);
                    }
                }
                return true;
            }
            // packing == 0: tightly bit-packed 10-bit stream across 32-bit words per scanline
            size_t rowSamples = static_cast<size_t>(w) * comps;
            size_t wordsPerRow = (rowSamples * 10u + 31u) / 32u;
            if (avail < wordsPerRow * 4u * h) return false;
            for (uint32_t y = 0; y < h; ++y) {
                size_t rowOff = baseOff + static_cast<size_t>(y) * wordsPerRow * 4u;
                size_t dstBase = static_cast<size_t>(y) * rowSamples;
                for (size_t s = 0; s < rowSamples; ++s) {
                    size_t bitPos = s * 10u;
                    size_t wIdx = bitPos / 32u;
                    uint32_t bOff = static_cast<uint32_t>(bitPos % 32u);
                    uint32_t w0 = rd32(rowOff + wIdx * 4u);
                    uint32_t val = 0;
                    if (bOff <= 22u) {
                        val = (w0 >> bOff) & 0x3FFu;
                    } else {
                        uint32_t w1 = rd32(rowOff + (wIdx + 1u) * 4u);
                        uint32_t loBits = 32u - bOff;
                        val = ((w0 >> bOff) | (w1 << loBits)) & 0x3FFu;
                    }
                    out10[dstBase + s] = static_cast<uint16_t>(val);
                }
            }
            return true;
        }

        if (el.bitSize == 12) {
            size_t rowSamples = static_cast<size_t>(w) * comps;
            if (el.packing == 0) {
                size_t rawRow = (rowSamples * 12u + 7u) / 8u;
                size_t padRow = (rawRow + 3u) & ~size_t(3u);
                size_t stride = (avail >= padRow * h) ? padRow : rawRow;
                if (avail < stride * h) return false;
                for (uint32_t y = 0; y < h; ++y) {
                    const uint8_t* rp = data + baseOff + static_cast<size_t>(y) * stride;
                    size_t dstBase = static_cast<size_t>(y) * rowSamples;
                    for (size_t s = 0; s + 1 < rowSamples; s += 2) {
                        size_t bo = (s / 2u) * 3u;
                        uint16_t v0 = static_cast<uint16_t>((static_cast<uint16_t>(rp[bo]) << 4) | (rp[bo + 1] >> 4));
                        uint16_t v1 = static_cast<uint16_t>(((rp[bo + 1] & 0x0Fu) << 8) | rp[bo + 2]);
                        out10[dstBase + s]     = static_cast<uint16_t>((static_cast<uint32_t>(v0) * 1023u + 2047u) / 4095u);
                        out10[dstBase + s + 1] = static_cast<uint16_t>((static_cast<uint32_t>(v1) * 1023u + 2047u) / 4095u);
                    }
                    if (rowSamples & 1u) {
                        size_t bo = (rowSamples / 2u) * 3u;
                        uint16_t v0 = static_cast<uint16_t>((static_cast<uint16_t>(rp[bo]) << 4) | (rp[bo + 1] >> 4));
                        out10[dstBase + rowSamples - 1] = static_cast<uint16_t>((static_cast<uint32_t>(v0) * 1023u + 2047u) / 4095u);
                    }
                }
                return true;
            }
            size_t rawRow = rowSamples * 2u;
            size_t padRow = (rawRow + 3u) & ~size_t(3u);
            size_t stride = (avail >= padRow * h) ? padRow : rawRow;
            if (avail < stride * h) return false;
            for (uint32_t y = 0; y < h; ++y) {
                size_t rowOff = baseOff + static_cast<size_t>(y) * stride;
                size_t dstBase = static_cast<size_t>(y) * rowSamples;
                for (size_t s = 0; s < rowSamples; ++s) {
                    uint16_t w16 = rd16(rowOff + s * 2u);
                    uint16_t v12 = (el.packing == 2) ? (w16 & 0xFFFu) : ((w16 >> 4) & 0xFFFu);
                    out10[dstBase + s] = static_cast<uint16_t>((static_cast<uint32_t>(v12) * 1023u + 2047u) / 4095u);
                }
            }
            return true;
        }

        if (el.bitSize == 16) {
            size_t rowSamples = static_cast<size_t>(w) * comps;
            size_t rawRow = rowSamples * 2u;
            size_t padRow = (rawRow + 3u) & ~size_t(3u);
            size_t stride = (avail >= padRow * h) ? padRow : rawRow;
            if (avail < stride * h) return false;
            for (uint32_t y = 0; y < h; ++y) {
                size_t rowOff = baseOff + static_cast<size_t>(y) * stride;
                size_t dstBase = static_cast<size_t>(y) * rowSamples;
                for (size_t s = 0; s < rowSamples; ++s) {
                    uint16_t raw = rd16(rowOff + s * 2u);
                    uint32_t u = (el.dataSign == 1)
                        ? static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(raw)) + 32768)
                        : static_cast<uint32_t>(raw);
                    out10[dstBase + s] = static_cast<uint16_t>((u * 1023u + 32767u) / 65535u);
                }
            }
            return true;
        }

        if (el.bitSize == 32) {
            if (avail < totalSamples * 4u) return false;
            for (size_t s = 0; s < totalSamples; ++s) {
                uint32_t u32 = rd32(baseOff + s * 4u);
                float f = 0.0f;
                std::memcpy(&f, &u32, 4);
                if (!std::isfinite(f)) f = 0.0f;
                f = std::clamp(f, 0.0f, 1.0f);
                out10[s] = static_cast<uint16_t>(f * 1023.0f + 0.5f);
            }
            return true;
        }

        if (el.bitSize == 64) {
            if (avail < totalSamples * 8u) return false;
            for (size_t s = 0; s < totalSamples; ++s) {
                uint64_t u64 = rd64(baseOff + s * 8u);
                double d = 0.0;
                std::memcpy(&d, &u64, 8);
                if (!std::isfinite(d)) d = 0.0;
                d = std::clamp(d, 0.0, 1.0);
                out10[s] = static_cast<uint16_t>(d * 1023.0 + 0.5);
            }
            return true;
        }

        return false;
    };

    auto mapCode10ToU8 = [&](uint16_t c10, uint8_t transfer, bool isAlpha) -> uint8_t {
        c10 = std::min<uint16_t>(c10, 1023u);
        if (!isAlpha && (isCin || transfer == 1)) {
            return cineonLut[c10];
        }
        return static_cast<uint8_t>((static_cast<uint32_t>(c10) * 255u + 511u) / 1023u);
    };

    std::vector<uint8_t> rgba(numPixels * 4, 255);
    uint32_t outChannels = 3;

    // Multi-element planar RGB/RGBA DPX (e.g. elements 0,1,2 with descriptors 1=R, 2=G, 3=B, optional 4=A)
    if (elems.size() >= 3 && descComponents(elems[0].descriptor) == 1 &&
        descComponents(elems[1].descriptor) == 1 && descComponents(elems[2].descriptor) == 1)
    {
        size_t planeLimit = std::min<size_t>(elems.size(), 4u);
        std::vector<uint16_t> plane10;
        for (size_t e = 0; e < planeLimit; ++e) {
            if (!decodeElement(elems[e], 1, plane10)) return false;
            int chIdx = -1;
            if (elems[e].descriptor == 1) chIdx = 0;      // Red
            else if (elems[e].descriptor == 2) chIdx = 1; // Green
            else if (elems[e].descriptor == 3) chIdx = 2; // Blue
            else if (elems[e].descriptor == 4) { chIdx = 3; outChannels = 4; } // Alpha
            else chIdx = static_cast<int>(e);
            if (chIdx >= 0 && chIdx < 4) {
                for (size_t i = 0; i < numPixels; ++i) {
                    rgba[i * 4 + chIdx] = mapCode10ToU8(plane10[i], elems[e].transfer, chIdx == 3);
                }
            }
        }
    } else {
        const DpxElemSpec& el0 = elems[0];
        uint32_t comps = descComponents(el0.descriptor);
        if (comps == 0) return false;
        outChannels = (comps == 4 || el0.descriptor == 51 || el0.descriptor == 52 || el0.descriptor == 103) ? 4 : ((comps == 1) ? 1 : 3);

        // Exact 8-bit uncompressed fast path so 8-bit values are bit-exact without 10-bit intermediate rounding
        if (el0.bitSize == 8 && el0.encoding == 0 && !isCin && el0.transfer != 1) {
            size_t rawRow = static_cast<size_t>(w) * comps;
            size_t padRow = (rawRow + 3u) & ~size_t(3u);
            size_t avail = (el0.elemOff < size) ? (size - el0.elemOff) : 0;
            size_t stride = (avail >= padRow * h) ? padRow : rawRow;
            if (avail < stride * h) return false;

            for (uint32_t y = 0; y < h; ++y) {
                const uint8_t* sp = data + el0.elemOff + static_cast<size_t>(y) * stride;
                uint8_t* dstRow = rgba.data() + static_cast<size_t>(y) * w * 4u;
                if (el0.descriptor == 100) { // CbYCrY 4:2:2
                    for (uint32_t x = 0; x < w; x += 2) {
                        uint8_t cb = sp[x * 2 + 0];
                        uint8_t y0 = sp[x * 2 + 1];
                        uint8_t cr = (x + 1 < w) ? sp[x * 2 + 2] : 128;
                        uint8_t y1 = (x + 1 < w) ? sp[x * 2 + 3] : y0;
                        ycbcr709ToRgb(y0, cb, cr, dstRow[x * 4 + 0], dstRow[x * 4 + 1], dstRow[x * 4 + 2]);
                        dstRow[x * 4 + 3] = 255;
                        if (x + 1 < w) {
                            ycbcr709ToRgb(y1, cb, cr, dstRow[(x + 1) * 4 + 0], dstRow[(x + 1) * 4 + 1], dstRow[(x + 1) * 4 + 2]);
                            dstRow[(x + 1) * 4 + 3] = 255;
                        }
                    }
                } else {
                    for (uint32_t x = 0; x < w; ++x) {
                        const uint8_t* p = sp + static_cast<size_t>(x) * comps;
                        uint8_t* d = dstRow + static_cast<size_t>(x) * 4u;
                        if (comps == 1) {
                            d[0] = d[1] = d[2] = p[0]; d[3] = 255;
                        } else if (el0.descriptor == 102) {
                            ycbcr709ToRgb(p[1], p[0], p[2], d[0], d[1], d[2]); d[3] = 255;
                        } else if (el0.descriptor == 103) {
                            ycbcr709ToRgb(p[1], p[0], p[2], d[0], d[1], d[2]); d[3] = p[3];
                        } else if (el0.descriptor == 52) { // ABGR
                            d[3] = p[0]; d[2] = p[1]; d[1] = p[2]; d[0] = p[3];
                        } else if (comps == 4) { // RGBA
                            d[0] = p[0]; d[1] = p[1]; d[2] = p[2]; d[3] = p[3];
                        } else { // RGB
                            d[0] = p[0]; d[1] = p[1]; d[2] = p[2]; d[3] = 255;
                        }
                    }
                }
            }
        } else {
            std::vector<uint16_t> s10;
            if (!decodeElement(el0, comps, s10)) return false;

            if (el0.descriptor == 100) { // CbYCrY 4:2:2
                for (uint32_t y = 0; y < h; ++y) {
                    const uint16_t* sr = s10.data() + static_cast<size_t>(y) * w * 2u;
                    uint8_t* dr = rgba.data() + static_cast<size_t>(y) * w * 4u;
                    for (uint32_t x = 0; x < w; x += 2) {
                        uint8_t cb = mapCode10ToU8(sr[x * 2 + 0], 2, true);
                        uint8_t y0 = mapCode10ToU8(sr[x * 2 + 1], 2, true);
                        uint8_t cr = (x + 1 < w) ? mapCode10ToU8(sr[x * 2 + 2], 2, true) : 128;
                        uint8_t y1 = (x + 1 < w) ? mapCode10ToU8(sr[x * 2 + 3], 2, true) : y0;
                        ycbcr709ToRgb(y0, cb, cr, dr[x * 4 + 0], dr[x * 4 + 1], dr[x * 4 + 2]);
                        dr[x * 4 + 3] = 255;
                        if (x + 1 < w) {
                            ycbcr709ToRgb(y1, cb, cr, dr[(x + 1) * 4 + 0], dr[(x + 1) * 4 + 1], dr[(x + 1) * 4 + 2]);
                            dr[(x + 1) * 4 + 3] = 255;
                        }
                    }
                }
            } else {
                for (size_t i = 0; i < numPixels; ++i) {
                    const uint16_t* p = s10.data() + i * comps;
                    uint8_t* d = rgba.data() + i * 4u;
                    if (comps == 1) {
                        uint8_t v = mapCode10ToU8(p[0], el0.transfer, false);
                        d[0] = d[1] = d[2] = v; d[3] = 255;
                    } else if (el0.descriptor == 102 || el0.descriptor == 103) {
                        uint8_t cb = mapCode10ToU8(p[0], 2, true);
                        uint8_t yv = mapCode10ToU8(p[1], 2, true);
                        uint8_t cr = mapCode10ToU8(p[2], 2, true);
                        ycbcr709ToRgb(yv, cb, cr, d[0], d[1], d[2]);
                        d[3] = (el0.descriptor == 103) ? mapCode10ToU8(p[3], 2, true) : 255;
                    } else if (el0.descriptor == 52) { // ABGR
                        d[3] = mapCode10ToU8(p[0], el0.transfer, true);
                        d[2] = mapCode10ToU8(p[1], el0.transfer, false);
                        d[1] = mapCode10ToU8(p[2], el0.transfer, false);
                        d[0] = mapCode10ToU8(p[3], el0.transfer, false);
                    } else if (comps == 4) { // RGBA
                        d[0] = mapCode10ToU8(p[0], el0.transfer, false);
                        d[1] = mapCode10ToU8(p[1], el0.transfer, false);
                        d[2] = mapCode10ToU8(p[2], el0.transfer, false);
                        d[3] = mapCode10ToU8(p[3], el0.transfer, true);
                    } else { // RGB
                        d[0] = mapCode10ToU8(p[0], el0.transfer, false);
                        d[1] = mapCode10ToU8(p[1], el0.transfer, false);
                        d[2] = mapCode10ToU8(p[2], el0.transfer, false);
                        d[3] = 255;
                    }
                }
            }
        }
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = outChannels;
    outInfo.bitsPerChannel = elems[0].bitSize;
    outInfo.formatName = isCin
        ? L"Kodak Cineon Film Scan (.cin)"
        : L"SMPTE 268M DPX Cinema Frame (.dpx)";
    outInfo.bgraPixels.resize(numPixels * 4u);

    const bool flipX = (orientation == 1 || orientation == 3);
    const bool flipY = (orientation == 2 || orientation == 3);
    for (uint32_t y = 0; y < h; ++y) {
        uint32_t sy = flipY ? (h - 1u - y) : y;
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t sx = flipX ? (w - 1u - x) : x;
            const uint8_t* sp = rgba.data() + (static_cast<size_t>(sy) * w + sx) * 4u;
            uint8_t* dp = outInfo.bgraPixels.data() + (static_cast<size_t>(y) * w + x) * 4u;
            WritePremulBGRA(dp, sp[0], sp[1], sp[2], sp[3]);
        }
    }

    outInfo.valid = true;
    return true;
}

// ============================================================================
// 16. 3D LUT Color Grading Cube (.cube / .3dl) — Real-Time Color Test Card Renderer
// ============================================================================
inline bool DecodeColorLutCube(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (extLower != L".cube" && extLower != L".3dl") return false;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    int lutN = 0;
    struct Vec3 { float r, g, b; };
    std::vector<Vec3> table;

    size_t pos = 0;
    while (pos < sv.size() && table.size() < 64 * 64 * 64) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t' || ln.front() == '\r')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == ' ' || ln.back() == '\t' || ln.back() == '\r')) ln.remove_suffix(1);
        if (ln.empty() || ln[0] == '#') continue;
        if (ln.rfind("LUT_3D_SIZE", 0) == 0) {
            sscanf_s(std::string(ln.substr(11)).c_str(), "%d", &lutN);
            continue;
        }
        if ((ln[0] >= '0' && ln[0] <= '9') || ln[0] == '.' || ln[0] == '-') {
            float r = 0, g = 0, b = 0;
            if (sscanf_s(std::string(ln).c_str(), "%f %f %f", &r, &g, &b) == 3) {
                if (r > 1.5f || g > 1.5f || b > 1.5f) { r /= 1023.0f; g /= 1023.0f; b /= 1023.0f; }
                table.push_back({ std::clamp(r, 0.0f, 1.0f), std::clamp(g, 0.0f, 1.0f), std::clamp(b, 0.0f, 1.0f) });
            }
        }
    }
    if (table.empty()) return false;
    if (lutN <= 1) lutN = std::max(2, static_cast<int>(std::round(std::cbrt(static_cast<double>(table.size())))));

    const uint32_t w = 256, h = 160;
    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"3D Color Grading LUT (" + std::to_wstring(lutN) + L"\xB3 Cube)";
    outInfo.bgraPixels.resize(w * h * 4);

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            float sr = static_cast<float>(x) / (w - 1);
            float sg = static_cast<float>(y) / (h - 1);
            float sb = 0.5f + 0.5f * std::sin(sr * 6.28318f);
            float or_ = sr, og = sg, ob = sb;
            if (x >= w / 2 && static_cast<int>(table.size()) >= lutN * lutN * lutN) {
                int ir = std::clamp(static_cast<int>(sr * (lutN - 1)), 0, lutN - 1);
                int ig = std::clamp(static_cast<int>(sg * (lutN - 1)), 0, lutN - 1);
                int ib = std::clamp(static_cast<int>(sb * (lutN - 1)), 0, lutN - 1);
                const auto& c = table[static_cast<size_t>((ib * lutN + ig) * lutN + ir)];
                or_ = c.r; og = c.g; ob = c.b;
            }
            uint8_t* dst = &outInfo.bgraPixels[(y * w + x) * 4];
            dst[0] = static_cast<uint8_t>(ob * 255.0f);
            dst[1] = static_cast<uint8_t>(og * 255.0f);
            dst[2] = static_cast<uint8_t>(or_ * 255.0f);
            dst[3] = 255;
        }
    }
    DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, w, h, 12, 10, "ORIGINAL", 1, 255, 255, 255);
    DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, w, h, w / 2 + 12, 10, "3D LUT APPLIED", 1, 255, 255, 120);
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 17. IESNA LM-63 Photometric Light Profile (.ies)
//     Parses full IESNA:LM-63 header, vertical/horizontal angle arrays, and
//     2D candela web; renders both physically-sampled wall-wash cone AND
//     overlaid C0-C180 / C90-C270 polar candela distribution curves.
// ============================================================================
inline bool DecodePhotometricIES(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (!data || size < 24) return false;
    (void)extLower;
    std::string_view sv(reinterpret_cast<const char*>(data), size);

    // Walk header lines until TILT=NONE or TILT=INCLUDE (case-insensitive)
    std::string manufac;
    std::string lumcat;
    bool foundTilt = false;
    bool tiltInclude = false;
    size_t pos = 0;

    auto trimView = [](std::string_view s) -> std::string_view {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
        return s;
    };
    auto startsWithICase = [](std::string_view s, std::string_view pfx) -> bool {
        return s.size() >= pfx.size() && DetailImg100::EqualsIgnoreCaseAscii(s.substr(0, pfx.size()), pfx);
    };

    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = trimView(sv.substr(pos, eol - pos));
        pos = (eol < sv.size()) ? (eol + 1) : sv.size();

        if (startsWithICase(ln, "[MANUFAC]")) {
            manufac = std::string(trimView(ln.substr(9)));
        } else if (startsWithICase(ln, "[LUMCAT]")) {
            lumcat = std::string(trimView(ln.substr(8)));
        } else if (startsWithICase(ln, "TILT=")) {
            foundTilt = true;
            std::string_view mode = trimView(ln.substr(5));
            tiltInclude = startsWithICase(mode, "INCLUDE");
            break;
        }
    }
    if (!foundTilt) return false;

    // Fast locale-independent floating-point scanner
    auto nextFloat = [&](double& outVal) -> bool {
        while (pos < sv.size()) {
            char c = sv[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == ';') {
                ++pos;
                continue;
            }
            break;
        }
        if (pos >= sv.size()) return false;

        size_t start = pos;
        if (sv[pos] == '+' || sv[pos] == '-') ++pos;
        bool hasDigits = false;
        while (pos < sv.size() && sv[pos] >= '0' && sv[pos] <= '9') { ++pos; hasDigits = true; }
        if (pos < sv.size() && sv[pos] == '.') {
            ++pos;
            while (pos < sv.size() && sv[pos] >= '0' && sv[pos] <= '9') { ++pos; hasDigits = true; }
        }
        if (!hasDigits) return false;
        if (pos < sv.size() && (sv[pos] == 'e' || sv[pos] == 'E')) {
            size_t ePos = pos++;
            if (pos < sv.size() && (sv[pos] == '+' || sv[pos] == '-')) ++pos;
            bool expDigits = false;
            while (pos < sv.size() && sv[pos] >= '0' && sv[pos] <= '9') { ++pos; expDigits = true; }
            if (!expDigits) pos = ePos;
        }

        char buf[64];
        size_t tokLen = std::min<size_t>(pos - start, sizeof(buf) - 1);
        std::memcpy(buf, sv.data() + start, tokLen);
        buf[tokLen] = '\0';
        char* endPtr = nullptr;
        outVal = std::strtod(buf, &endPtr);
        return (endPtr != buf) && std::isfinite(outVal);
    };

    if (tiltInclude) {
        double lampGeom = 0.0, nTiltD = 0.0;
        if (!nextFloat(lampGeom) || !nextFloat(nTiltD)) return false;
        int nTilt = static_cast<int>(std::llround(nTiltD));
        if (nTilt < 0 || nTilt > 4096) return false;
        double dummy = 0.0;
        for (int i = 0; i < nTilt * 2; ++i) {
            if (!nextFloat(dummy)) return false;
        }
    }

    // Standard 13 numeric header parameters in IESNA LM-63:
    // 0:numLamps, 1:lumensPerLamp, 2:candelaMult, 3:nVertAngles, 4:nHorizAngles,
    // 5:photometricType, 6:unitsType, 7:width, 8:length, 9:height,
    // 10:ballastFactor, 11:futureUse, 12:inputWatts
    double hdr[13] = {};
    for (int i = 0; i < 13; ++i) {
        if (!nextFloat(hdr[i])) return false;
    }

    int nVert = static_cast<int>(std::llround(hdr[3]));
    int nHoriz = static_cast<int>(std::llround(hdr[4]));
    if (nVert < 2 || nVert > 4096 || nHoriz < 1 || nHoriz > 4096) return false;
    if (static_cast<size_t>(nVert) * static_cast<size_t>(nHoriz) > 2000000) return false;

    float candelaScale = static_cast<float>((hdr[2] > 0.0 ? hdr[2] : 1.0) * (hdr[10] > 0.0 ? hdr[10] : 1.0));
    float inputWatts = static_cast<float>(hdr[12]);

    std::vector<float> vertAngles(static_cast<size_t>(nVert));
    for (int v = 0; v < nVert; ++v) {
        double val = 0.0;
        if (!nextFloat(val)) return false;
        vertAngles[static_cast<size_t>(v)] = static_cast<float>(val);
        if (v > 0 && vertAngles[static_cast<size_t>(v)] < vertAngles[static_cast<size_t>(v - 1)] - 0.01f) {
            return false; // Vertical angles must be non-decreasing
        }
    }

    std::vector<float> horizAngles(static_cast<size_t>(nHoriz));
    for (int hIdx = 0; hIdx < nHoriz; ++hIdx) {
        double val = 0.0;
        if (!nextFloat(val)) return false;
        horizAngles[static_cast<size_t>(hIdx)] = static_cast<float>(val);
    }

    std::vector<float> candela(static_cast<size_t>(nHoriz) * static_cast<size_t>(nVert), 0.0f);
    float maxCandela = 0.0f;
    for (int hIdx = 0; hIdx < nHoriz; ++hIdx) {
        for (int v = 0; v < nVert; ++v) {
            double val = 0.0;
            if (!nextFloat(val)) return false;
            float cd = std::max(0.0f, static_cast<float>(val) * candelaScale);
            candela[static_cast<size_t>(hIdx) * static_cast<size_t>(nVert) + static_cast<size_t>(v)] = cd;
            if (cd > maxCandela) maxCandela = cd;
        }
    }
    if (!(maxCandela > 0.0f) || !std::isfinite(maxCandela)) return false;

    auto sampleCandela = [&](float thetaDeg, float phiDeg) -> float {
        if (thetaDeg < vertAngles.front() || thetaDeg > vertAngles.back()) {
            return 0.0f;
        }
        auto itV = std::lower_bound(vertAngles.begin(), vertAngles.end(), thetaDeg);
        size_t v1 = static_cast<size_t>(std::distance(vertAngles.begin(), itV));
        if (v1 >= vertAngles.size()) v1 = vertAngles.size() - 1;
        size_t v0 = (v1 > 0) ? (v1 - 1) : 0;
        float denomV = vertAngles[v1] - vertAngles[v0];
        float tv = (denomV > 1e-5f) ? std::clamp((thetaDeg - vertAngles[v0]) / denomV, 0.0f, 1.0f) : 0.0f;

        if (nHoriz == 1) {
            return candela[v0] * (1.0f - tv) + candela[v1] * tv;
        }

        float p = std::fmod(phiDeg, 360.0f);
        if (p < 0.0f) p += 360.0f;
        float hMax = horizAngles.back();
        if (hMax <= 90.05f) { // Quadrant symmetric
            if (p > 180.0f) p = 360.0f - p;
            if (p > 90.0f) p = 180.0f - p;
        } else if (hMax <= 180.05f) { // Bilaterally symmetric across C0-C180
            if (p > 180.0f) p = 360.0f - p;
        }
        p = std::clamp(p, horizAngles.front(), horizAngles.back());

        auto itH = std::lower_bound(horizAngles.begin(), horizAngles.end(), p);
        size_t h1 = static_cast<size_t>(std::distance(horizAngles.begin(), itH));
        if (h1 >= horizAngles.size()) h1 = horizAngles.size() - 1;
        size_t h0 = (h1 > 0) ? (h1 - 1) : 0;
        float denomH = horizAngles[h1] - horizAngles[h0];
        float th = (denomH > 1e-5f) ? std::clamp((p - horizAngles[h0]) / denomH, 0.0f, 1.0f) : 0.0f;

        float c00 = candela[h0 * static_cast<size_t>(nVert) + v0];
        float c01 = candela[h0 * static_cast<size_t>(nVert) + v1];
        float c10 = candela[h1 * static_cast<size_t>(nVert) + v0];
        float c11 = candela[h1 * static_cast<size_t>(nVert) + v1];
        float c0 = c00 * (1.0f - tv) + c01 * tv;
        float c1 = c10 * (1.0f - tv) + c11 * tv;
        return c0 * (1.0f - th) + c1 * th;
    };

    const uint32_t W = 320, H = 320;
    outInfo.width = W;
    outInfo.height = H;
    outInfo.origWidth = W;
    outInfo.origHeight = H;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    std::wstring desc = L"IESNA LM-63 Photometric Web (";
    if (!lumcat.empty()) desc += PathUtils::Utf8ToWide(lumcat) + L", ";
    else if (!manufac.empty()) desc += PathUtils::Utf8ToWide(manufac) + L", ";
    desc += std::to_wstring(static_cast<int>(std::round(maxCandela))) + L" cd max)";
    outInfo.formatName = desc;
    outInfo.bgraPixels.resize(static_cast<size_t>(W) * H * 4);

    bool hasUplight = (vertAngles.back() > 95.0f);
    const float cx = W * 0.5f;
    const float ly = hasUplight ? (H * 0.44f) : (H * 0.14f);
    const float zWall = H * 0.20f;
    const float polarMaxR = hasUplight ? (H * 0.38f) : (H * 0.72f);

    // Subtle color temperature shift from lamp wattage/lumens (warm 3000K -> neutral 4200K)
    float efficacy = (inputWatts > 1.0f && hdr[1] > 0.0) ? static_cast<float>((hdr[0] * hdr[1]) / inputWatts) : 75.0f;
    float coolMix = std::clamp((efficacy - 40.0f) / 100.0f, 0.0f, 1.0f);
    float tintR = 1.00f - 0.05f * coolMix;
    float tintG = 0.90f + 0.06f * coolMix;
    float tintB = 0.72f + 0.22f * coolMix;

    // Pass 1: Physically-sampled architectural wall-wash cone + subtle polar reference rings
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            float dx = static_cast<float>(x) - cx;
            float dy = static_cast<float>(y) - ly; // +dy = downward nadir (theta = 0)
            float r2d = std::sqrt(dx * dx + dy * dy);
            float r3d = std::sqrt(dx * dx + dy * dy + zWall * zWall);

            float cosTheta = std::clamp(dy / std::max(1e-4f, r3d), -1.0f, 1.0f);
            float thetaDeg = std::acos(cosTheta) * 57.2957795f;
            // Right half samples C0..C90, left half samples C180..C90 so asymmetric fixtures show true asymmetry
            float phiDeg = (dx >= 0.0f)
                ? (std::atan2(zWall * 0.35f, dx + 0.01f) * 57.2957795f)
                : (180.0f - std::atan2(zWall * 0.35f, -dx + 0.01f) * 57.2957795f);

            float normI = sampleCandela(thetaDeg, phiDeg) / maxCandela;
            float distNorm = r3d / static_cast<float>(H);
            float illuminance = normI * (zWall / r3d) / (0.14f + distNorm * distNorm * 2.2f);
            float mapped = 1.0f - std::exp(-illuminance * 2.35f);
            mapped = std::pow(std::clamp(mapped, 0.0f, 1.0f), 1.0f / 2.1f);

            // Polar grid rings at 25%, 50%, 75%, 100% of polarMaxR
            float ringPos = (r2d / polarMaxR) * 4.0f;
            bool onRing = (r2d <= polarMaxR + 1.0f) &&
                          (std::abs(ringPos - std::round(ringPos)) * (polarMaxR * 0.25f) < 0.65f) &&
                          (ringPos >= 0.5f);

            float bgR = 14.0f + (onRing ? 18.0f : 0.0f);
            float bgG = 17.0f + (onRing ? 24.0f : 0.0f);
            float bgB = 24.0f + (onRing ? 34.0f : 0.0f);

            uint8_t pr = static_cast<uint8_t>(std::clamp(bgR + mapped * 245.0f * tintR, 0.0f, 255.0f));
            uint8_t pg = static_cast<uint8_t>(std::clamp(bgG + mapped * 245.0f * tintG, 0.0f, 255.0f));
            uint8_t pb = static_cast<uint8_t>(std::clamp(bgB + mapped * 245.0f * tintB, 0.0f, 255.0f));
            WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(y) * W + x) * 4], pr, pg, pb, 255);
        }
    }

    // Pass 2: Overlaid polar candela distribution curves (C0-C180 gold & C90-C270 cyan)
    auto blendPixel = [&](int px, int py, uint8_t cr, uint8_t cg, uint8_t cb, float alpha) {
        if (px < 0 || py < 0 || px >= static_cast<int>(W) || py >= static_cast<int>(H)) return;
        uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(py) * W + static_cast<size_t>(px)) * 4];
        dst[0] = static_cast<uint8_t>(std::clamp(dst[0] * (1.0f - alpha) + cb * alpha, 0.0f, 255.0f));
        dst[1] = static_cast<uint8_t>(std::clamp(dst[1] * (1.0f - alpha) + cg * alpha, 0.0f, 255.0f));
        dst[2] = static_cast<uint8_t>(std::clamp(dst[2] * (1.0f - alpha) + cr * alpha, 0.0f, 255.0f));
        dst[3] = 255;
    };
    auto drawLineAA = [&](float x0, float y0, float x1, float y1, uint8_t cr, uint8_t cg, uint8_t cb, float alpha) {
        float dx = x1 - x0;
        float dy = y1 - y0;
        int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::abs(dx), std::abs(dy)) * 2.0f)));
        for (int s = 0; s <= steps; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(steps);
            float fx = x0 + dx * t;
            float fy = y0 + dy * t;
            int ix = static_cast<int>(std::floor(fx));
            int iy = static_cast<int>(std::floor(fy));
            blendPixel(ix, iy, cr, cg, cb, alpha);
            blendPixel(ix + 1, iy, cr, cg, cb, alpha * 0.45f);
            blendPixel(ix, iy + 1, cr, cg, cb, alpha * 0.45f);
        }
    };

    auto plotPolarPlane = [&](float phiRight, float phiLeft, uint8_t cr, uint8_t cg, uint8_t cb) {
        const int N = 360;
        for (int side = 0; side < 2; ++side) {
            float phi = (side == 0) ? phiRight : phiLeft;
            float signX = (side == 0) ? 1.0f : -1.0f;
            float prevX = cx, prevY = ly;
            bool hasPrev = false;
            for (int i = 0; i <= N; ++i) {
                float theta = (static_cast<float>(i) / static_cast<float>(N)) * 180.0f;
                float rad = (sampleCandela(theta, phi) / maxCandela) * polarMaxR;
                float radAngle = theta * 0.0174532925f;
                float px = cx + signX * std::sin(radAngle) * rad;
                float py = ly + std::cos(radAngle) * rad;
                if (hasPrev) {
                    drawLineAA(prevX, prevY, px, py, cr, cg, cb, 0.85f);
                }
                prevX = px;
                prevY = py;
                hasPrev = true;
            }
        }
    };

    plotPolarPlane(90.0f, 270.0f, 75, 215, 255);  // C90 - C270 plane (cyan)
    plotPolarPlane(0.0f,  180.0f, 255, 215, 65);  // C0 - C180 plane (amber/gold)

    outInfo.valid = true;
    return true;
}

// ============================================================================
// 18. TrueType / OpenType / WebFont Specimen (.ttf / .otf / .woff / .woff2)
//     - Reconstructs valid uncompressed SFNT from .woff (wOFF + InflateZlib)
//     - Parses SFNT tables (name, head, maxp, hhea, hmtx, cmap, loca, glyf)
//     - Renders real font specimen via Win32 GDI (AddFontMemResourceEx + DIB)
//     - Includes a pure-C++ TrueType glyf quadratic-Bezier scanline rasterizer
//       so synthetic/minimal TTFs & glyph showcases always render real contours!
// ============================================================================
namespace DetailFont {

struct SfntTableSlice {
    uint32_t offset = 0;
    uint32_t length = 0;
    bool valid = false;
};

struct ParsedSfnt {
    const uint8_t* data = nullptr;
    size_t size = 0;
    uint32_t flavor = 0;
    uint16_t numTables = 0;
    uint16_t numGlyphs = 0;
    uint16_t unitsPerEm = 1000;
    int16_t indexToLocFormat = 0;
    int16_t ascent = 800;
    int16_t descent = -200;
    uint16_t numHMetrics = 0;
    std::wstring familyName;
    std::wstring subfamilyName;

    SfntTableSlice tblName;
    SfntTableSlice tblHead;
    SfntTableSlice tblMaxp;
    SfntTableSlice tblHhea;
    SfntTableSlice tblHmtx;
    SfntTableSlice tblCmap;
    SfntTableSlice tblLoca;
    SfntTableSlice tblGlyf;
    SfntTableSlice tblCff;
};

inline uint32_t MakeSfntTag(const char t[4]) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(t[0])) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(t[1])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(t[2])) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(t[3]));
}

inline uint32_t CalcSfntChecksum(const uint8_t* p, size_t len) {
    uint32_t sum = 0;
    size_t nLongs = len / 4;
    for (size_t i = 0; i < nLongs; ++i) {
        sum += DetailImg100::RdU32BE(p + i * 4);
    }
    size_t rem = len & 3u;
    if (rem > 0) {
        uint8_t tail[4] = { 0, 0, 0, 0 };
        for (size_t i = 0; i < rem; ++i) tail[i] = p[nLongs * 4 + i];
        sum += DetailImg100::RdU32BE(tail);
    }
    return sum;
}

inline void WrU16BE(uint8_t* dst, uint16_t v) {
    dst[0] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    dst[1] = static_cast<uint8_t>(v & 0xFFu);
}

inline void WrU32BE(uint8_t* dst, uint32_t v) {
    dst[0] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    dst[1] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    dst[2] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    dst[3] = static_cast<uint8_t>(v & 0xFFu);
}

// Reconstruct an uncompressed 4-byte-aligned SFNT .ttf/.otf buffer from a WOFF 1.0 file
inline bool ReconstructSfntFromWoff(const uint8_t* data, size_t size, std::vector<uint8_t>& outSfnt) {
    if (!data || size < 44 || std::memcmp(data, "wOFF", 4) != 0) return false;
    uint32_t flavor = DetailImg100::RdU32BE(data + 4);
    uint16_t numTables = DetailImg100::RdU16BE(data + 12);
    if (numTables == 0 || numTables > 256) return false;
    if (44ull + static_cast<size_t>(numTables) * 20ull > size) return false;

    struct WoffRec {
        uint32_t tag = 0;
        uint32_t woffOffset = 0;
        uint32_t compLength = 0;
        uint32_t origLength = 0;
        uint32_t origChecksum = 0;
        std::vector<uint8_t> rawBytes;
    };
    std::vector<WoffRec> recs(numTables);
    size_t totalDataAligned = 0;

    for (uint16_t i = 0; i < numTables; ++i) {
        const uint8_t* ent = data + 44 + static_cast<size_t>(i) * 20;
        WoffRec& r = recs[i];
        r.tag = DetailImg100::RdU32BE(ent + 0);
        r.woffOffset = DetailImg100::RdU32BE(ent + 4);
        r.compLength = DetailImg100::RdU32BE(ent + 8);
        r.origLength = DetailImg100::RdU32BE(ent + 12);
        r.origChecksum = DetailImg100::RdU32BE(ent + 16);

        if (r.woffOffset > size || r.compLength > size - r.woffOffset) return false;
        if (r.compLength > r.origLength || r.origLength > 64u * 1024u * 1024u) return false;

        if (r.origLength == 0) {
            r.rawBytes.clear();
        } else if (r.compLength < r.origLength) {
            if (!ZipDeflate::InflateZlib(data + r.woffOffset, r.compLength, r.rawBytes, static_cast<size_t>(r.origLength) + 64) ||
                r.rawBytes.size() != r.origLength) {
                return false;
            }
        } else {
            r.rawBytes.assign(data + r.woffOffset, data + r.woffOffset + r.origLength);
        }
        if (r.origChecksum == 0 && !r.rawBytes.empty()) {
            r.origChecksum = CalcSfntChecksum(r.rawBytes.data(), r.rawBytes.size());
        }
        totalDataAligned += (static_cast<size_t>(r.origLength) + 3u) & ~size_t(3u);
    }

    // OpenType/GDI requires table directory sorted ascending by 4-byte tag
    std::sort(recs.begin(), recs.end(), [](const WoffRec& a, const WoffRec& b) {
        return a.tag < b.tag;
    });

    uint16_t entrySelector = 0;
    uint16_t maxPow2 = 1;
    while (static_cast<uint32_t>(maxPow2) * 2u <= numTables) {
        maxPow2 = static_cast<uint16_t>(maxPow2 * 2u);
        ++entrySelector;
    }
    uint16_t searchRange = static_cast<uint16_t>(maxPow2 * 16u);
    uint16_t rangeShift = static_cast<uint16_t>(numTables * 16u - searchRange);

    size_t dirBytes = 12u + static_cast<size_t>(numTables) * 16u;
    outSfnt.assign(dirBytes + totalDataAligned, 0);

    WrU32BE(outSfnt.data() + 0, flavor);
    WrU16BE(outSfnt.data() + 4, numTables);
    WrU16BE(outSfnt.data() + 6, searchRange);
    WrU16BE(outSfnt.data() + 8, entrySelector);
    WrU16BE(outSfnt.data() + 10, rangeShift);

    size_t curOff = dirBytes;
    for (uint16_t i = 0; i < numTables; ++i) {
        uint8_t* recPtr = outSfnt.data() + 12 + static_cast<size_t>(i) * 16;
        WrU32BE(recPtr + 0, recs[i].tag);
        WrU32BE(recPtr + 4, recs[i].origChecksum);
        WrU32BE(recPtr + 8, static_cast<uint32_t>(curOff));
        WrU32BE(recPtr + 12, recs[i].origLength);
        if (!recs[i].rawBytes.empty()) {
            std::memcpy(outSfnt.data() + curOff, recs[i].rawBytes.data(), recs[i].rawBytes.size());
        }
        curOff += (static_cast<size_t>(recs[i].origLength) + 3u) & ~size_t(3u);
    }
    return true;
}

inline bool ParseSfntDirectory(const uint8_t* data, size_t size, ParsedSfnt& out) {
    if (!data || size < 28) return false;
    // Handle TrueType Collection ('ttcf') by jumping to first font offset
    size_t hdrOff = 0;
    if (std::memcmp(data, "ttcf", 4) == 0) {
        if (size < 16) return false;
        uint32_t numFonts = DetailImg100::RdU32BE(data + 8);
        if (numFonts == 0) return false;
        hdrOff = DetailImg100::RdU32BE(data + 12);
        if (hdrOff + 28 > size) return false;
    }

    uint32_t sfntVer = DetailImg100::RdU32BE(data + hdrOff);
    bool validFlavor = (sfntVer == 0x00010000u) ||
                       (sfntVer == MakeSfntTag("OTTO")) ||
                       (sfntVer == MakeSfntTag("true")) ||
                       (sfntVer == MakeSfntTag("typ1"));
    if (!validFlavor) return false;

    uint16_t numTables = DetailImg100::RdU16BE(data + hdrOff + 4);
    if (numTables == 0 || numTables > 256) return false;
    if (hdrOff + 12ull + static_cast<size_t>(numTables) * 16ull > size) return false;

    out.data = data;
    out.size = size;
    out.flavor = sfntVer;
    out.numTables = numTables;

    int validCoreTables = 0;
    for (uint16_t i = 0; i < numTables; ++i) {
        const uint8_t* rec = data + hdrOff + 12 + static_cast<size_t>(i) * 16;
        uint32_t tag = DetailImg100::RdU32BE(rec + 0);
        uint32_t off = DetailImg100::RdU32BE(rec + 8);
        uint32_t len = DetailImg100::RdU32BE(rec + 12);
        if (off > size || len > size - off) return false; // Corrupt/truncated table bounds!

        SfntTableSlice sl{ off, len, true };
        if (tag == MakeSfntTag("name")) { out.tblName = sl; ++validCoreTables; }
        else if (tag == MakeSfntTag("head")) { out.tblHead = sl; ++validCoreTables; }
        else if (tag == MakeSfntTag("maxp")) { out.tblMaxp = sl; ++validCoreTables; }
        else if (tag == MakeSfntTag("hhea")) { out.tblHhea = sl; }
        else if (tag == MakeSfntTag("hmtx")) { out.tblHmtx = sl; }
        else if (tag == MakeSfntTag("cmap")) { out.tblCmap = sl; ++validCoreTables; }
        else if (tag == MakeSfntTag("loca")) { out.tblLoca = sl; }
        else if (tag == MakeSfntTag("glyf")) { out.tblGlyf = sl; ++validCoreTables; }
        else if (tag == MakeSfntTag("CFF ") || tag == MakeSfntTag("CFF2")) { out.tblCff = sl; ++validCoreTables; }
    }
    if (validCoreTables == 0) return false;

    if (out.tblHead.valid && out.tblHead.length >= 54) {
        const uint8_t* h = data + out.tblHead.offset;
        uint16_t upem = DetailImg100::RdU16BE(h + 18);
        if (upem >= 16 && upem <= 16384) out.unitsPerEm = upem;
        out.indexToLocFormat = DetailImg100::RdI16BE(h + 50);
    }
    if (out.tblMaxp.valid && out.tblMaxp.length >= 6) {
        out.numGlyphs = DetailImg100::RdU16BE(data + out.tblMaxp.offset + 4);
    }
    if (out.tblHhea.valid && out.tblHhea.length >= 36) {
        const uint8_t* hh = data + out.tblHhea.offset;
        int16_t asc = DetailImg100::RdI16BE(hh + 4);
        int16_t dsc = DetailImg100::RdI16BE(hh + 6);
        if (asc > 0) out.ascent = asc;
        if (dsc < 0) out.descent = dsc;
        out.numHMetrics = DetailImg100::RdU16BE(hh + 34);
    } else {
        out.ascent = static_cast<int16_t>((out.unitsPerEm * 4) / 5);
        out.descent = -static_cast<int16_t>(out.unitsPerEm / 5);
    }

    // Parse 'name' table for Family (nameID 1 / 16) and Subfamily (nameID 2 / 17)
    if (out.tblName.valid && out.tblName.length >= 6) {
        const uint8_t* nm = data + out.tblName.offset;
        size_t nmLen = out.tblName.length;
        uint16_t count = DetailImg100::RdU16BE(nm + 2);
        uint16_t strStorageOff = DetailImg100::RdU16BE(nm + 4);
        int bestFamPrio = 0, bestSubPrio = 0;

        for (uint16_t i = 0; i < count && 6u + static_cast<size_t>(i + 1) * 12u <= nmLen; ++i) {
            const uint8_t* nr = nm + 6 + static_cast<size_t>(i) * 12;
            uint16_t platId = DetailImg100::RdU16BE(nr + 0);
            uint16_t encId  = DetailImg100::RdU16BE(nr + 2);
            uint16_t langId = DetailImg100::RdU16BE(nr + 4);
            uint16_t nameId = DetailImg100::RdU16BE(nr + 6);
            uint16_t sLen   = DetailImg100::RdU16BE(nr + 8);
            uint16_t sOff   = DetailImg100::RdU16BE(nr + 10);
            if (nameId != 1 && nameId != 2 && nameId != 16 && nameId != 17) continue;
            size_t absOff = static_cast<size_t>(strStorageOff) + sOff;
            if (sLen == 0 || absOff > nmLen || sLen > nmLen - absOff) continue;

            std::wstring decoded;
            int prio = 0;
            if (platId == 3 || platId == 0) { // Windows / Unicode UTF-16BE
                if ((sLen & 1u) != 0) continue;
                for (uint16_t k = 0; k < sLen; k += 2) {
                    wchar_t wc = static_cast<wchar_t>(DetailImg100::RdU16BE(nm + absOff + k));
                    if (wc != 0) decoded.push_back(wc);
                }
                prio = (platId == 3 && (langId == 0x0409 || langId == 0)) ? 4 : 3;
                (void)encId;
            } else if (platId == 1) { // Macintosh Roman 8-bit
                for (uint16_t k = 0; k < sLen; ++k) {
                    uint8_t ch = nm[absOff + k];
                    if (ch >= 32 && ch < 127) decoded.push_back(static_cast<wchar_t>(ch));
                }
                prio = (langId == 0) ? 2 : 1;
            }
            if (decoded.empty()) continue;

            if (nameId == 1) {
                if (prio > bestFamPrio) { bestFamPrio = prio; out.familyName = std::move(decoded); }
            } else if (nameId == 16 && bestFamPrio == 0) {
                out.familyName = std::move(decoded);
            } else if (nameId == 2) {
                if (prio > bestSubPrio) { bestSubPrio = prio; out.subfamilyName = std::move(decoded); }
            } else if (nameId == 17 && bestSubPrio == 0) {
                out.subfamilyName = std::move(decoded);
            }
        }
    }
    return true;
}

inline uint16_t LookupCmapGlyph(const ParsedSfnt& sfnt, uint32_t codepoint) {
    if (!sfnt.tblCmap.valid || sfnt.tblCmap.length < 4) return 0;
    const uint8_t* cm = sfnt.data + sfnt.tblCmap.offset;
    size_t cmLen = sfnt.tblCmap.length;
    uint16_t numSub = DetailImg100::RdU16BE(cm + 2);

    uint32_t subOff12 = 0, subOff4 = 0, subOff6 = 0, subOff0 = 0;
    for (uint16_t i = 0; i < numSub && 4u + static_cast<size_t>(i + 1) * 8u <= cmLen; ++i) {
        uint32_t off = DetailImg100::RdU32BE(cm + 4 + static_cast<size_t>(i) * 8 + 4);
        if (off + 4 > cmLen) continue;
        uint16_t fmt = DetailImg100::RdU16BE(cm + off);
        if (fmt == 12 && subOff12 == 0) subOff12 = off;
        else if (fmt == 4 && subOff4 == 0) subOff4 = off;
        else if (fmt == 6 && subOff6 == 0) subOff6 = off;
        else if (fmt == 0 && subOff0 == 0) subOff0 = off;
    }

    // Format 12: Segmented coverage (32-bit)
    if (subOff12 != 0 && subOff12 + 16 <= cmLen) {
        uint32_t nGroups = DetailImg100::RdU32BE(cm + subOff12 + 12);
        size_t gBase = subOff12 + 16;
        size_t maxGroups = std::min<size_t>(nGroups, (cmLen - gBase) / 12u);
        for (size_t g = 0; g < maxGroups; ++g) {
            const uint8_t* gp = cm + gBase + g * 12u;
            uint32_t startCp = DetailImg100::RdU32BE(gp + 0);
            uint32_t endCp   = DetailImg100::RdU32BE(gp + 4);
            uint32_t startGl = DetailImg100::RdU32BE(gp + 8);
            if (codepoint < startCp) break;
            if (codepoint <= endCp) {
                return static_cast<uint16_t>((startGl + (codepoint - startCp)) & 0xFFFFu);
            }
        }
    }

    // Format 4: Segment mapping to delta values (16-bit BMP)
    if (subOff4 != 0 && codepoint <= 0xFFFFu && subOff4 + 14 <= cmLen) {
        uint16_t subLen = DetailImg100::RdU16BE(cm + subOff4 + 2);
        size_t lim = std::min<size_t>(cmLen, subOff4 + (subLen > 14 ? subLen : (cmLen - subOff4)));
        uint16_t segCountX2 = DetailImg100::RdU16BE(cm + subOff4 + 6);
        uint16_t segCount = segCountX2 / 2u;
        size_t endCodeOff   = subOff4 + 14;
        size_t startCodeOff = endCodeOff + static_cast<size_t>(segCountX2) + 2u;
        size_t idDeltaOff   = startCodeOff + static_cast<size_t>(segCountX2);
        size_t idRangeOff   = idDeltaOff + static_cast<size_t>(segCountX2);
        if (idRangeOff + segCountX2 <= lim) {
            uint16_t cp16 = static_cast<uint16_t>(codepoint);
            for (uint16_t s = 0; s < segCount; ++s) {
                uint16_t endCode = DetailImg100::RdU16BE(cm + endCodeOff + s * 2u);
                if (cp16 > endCode) continue;
                uint16_t startCode = DetailImg100::RdU16BE(cm + startCodeOff + s * 2u);
                if (cp16 < startCode) break;
                uint16_t idDelta = DetailImg100::RdU16BE(cm + idDeltaOff + s * 2u);
                size_t roPos = idRangeOff + s * 2u;
                uint16_t idRange = DetailImg100::RdU16BE(cm + roPos);
                if (idRange == 0) {
                    return static_cast<uint16_t>((cp16 + idDelta) & 0xFFFFu);
                }
                size_t glyphIdxPos = roPos + idRange + static_cast<size_t>(cp16 - startCode) * 2u;
                if (glyphIdxPos + 2 <= lim) {
                    uint16_t g = DetailImg100::RdU16BE(cm + glyphIdxPos);
                    if (g != 0) return static_cast<uint16_t>((g + idDelta) & 0xFFFFu);
                }
                return 0;
            }
        }
    }

    // Format 6: Trimmed table mapping
    if (subOff6 != 0 && codepoint <= 0xFFFFu && subOff6 + 10 <= cmLen) {
        uint16_t firstCode = DetailImg100::RdU16BE(cm + subOff6 + 6);
        uint16_t entryCount = DetailImg100::RdU16BE(cm + subOff6 + 8);
        if (codepoint >= firstCode && codepoint < static_cast<uint32_t>(firstCode) + entryCount) {
            size_t idxPos = subOff6 + 10 + static_cast<size_t>(codepoint - firstCode) * 2u;
            if (idxPos + 2 <= cmLen) return DetailImg100::RdU16BE(cm + idxPos);
        }
    }

    // Format 0: Byte encoding table
    if (subOff0 != 0 && codepoint < 256 && subOff0 + 6 + 256 <= cmLen) {
        return cm[subOff0 + 6 + codepoint];
    }
    return 0;
}

inline uint16_t GetGlyphAdvance(const ParsedSfnt& sfnt, uint16_t glyphId) {
    if (sfnt.tblHmtx.valid && sfnt.numHMetrics > 0) {
        const uint8_t* hm = sfnt.data + sfnt.tblHmtx.offset;
        size_t hmLen = sfnt.tblHmtx.length;
        uint16_t idx = (glyphId < sfnt.numHMetrics) ? glyphId : static_cast<uint16_t>(sfnt.numHMetrics - 1);
        if (static_cast<size_t>(idx) * 4u + 2u <= hmLen) {
            uint16_t adv = DetailImg100::RdU16BE(hm + static_cast<size_t>(idx) * 4u);
            if (adv > 0) return adv;
        }
    }
    return static_cast<uint16_t>((sfnt.unitsPerEm * 3u) / 5u);
}

struct EdgeSeg {
    float x0, y0, x1, y1;
};

inline bool ExtractGlyphEdges(
    const ParsedSfnt& sfnt,
    uint16_t glyphId,
    std::vector<EdgeSeg>& outEdges,
    int16_t& outXMin,
    int16_t& outXMax,
    int depth = 0)
{
    if (depth > 4 || !sfnt.tblLoca.valid || !sfnt.tblGlyf.valid || sfnt.numGlyphs == 0 || glyphId >= sfnt.numGlyphs) {
        return false;
    }
    const uint8_t* loca = sfnt.data + sfnt.tblLoca.offset;
    size_t locaLen = sfnt.tblLoca.length;
    uint32_t gOff = 0, gEnd = 0;
    if (sfnt.indexToLocFormat == 0) {
        if (static_cast<size_t>(glyphId + 1) * 2u + 2u > locaLen) return false;
        gOff = static_cast<uint32_t>(DetailImg100::RdU16BE(loca + static_cast<size_t>(glyphId) * 2u)) * 2u;
        gEnd = static_cast<uint32_t>(DetailImg100::RdU16BE(loca + static_cast<size_t>(glyphId + 1) * 2u)) * 2u;
    } else {
        if (static_cast<size_t>(glyphId + 1) * 4u + 4u > locaLen) return false;
        gOff = DetailImg100::RdU32BE(loca + static_cast<size_t>(glyphId) * 4u);
        gEnd = DetailImg100::RdU32BE(loca + static_cast<size_t>(glyphId + 1) * 4u);
    }
    if (gOff >= gEnd) return true; // Whitespace / empty glyph
    if (gEnd > sfnt.tblGlyf.length || gEnd - gOff < 10) return false;

    const uint8_t* g = sfnt.data + sfnt.tblGlyf.offset + gOff;
    size_t gLen = gEnd - gOff;
    int16_t numContours = DetailImg100::RdI16BE(g + 0);
    if (depth == 0) {
        outXMin = DetailImg100::RdI16BE(g + 2);
        outXMax = DetailImg100::RdI16BE(g + 6);
    }

    if (numContours == 0) return true;

    // Composite glyph (numContours < 0)
    if (numContours < 0) {
        size_t cp = 10;
        uint16_t compFlags = 0x0020u; // MORE_COMPONENTS
        while ((compFlags & 0x0020u) != 0 && cp + 4 <= gLen) {
            compFlags = DetailImg100::RdU16BE(g + cp);
            uint16_t childGlyph = DetailImg100::RdU16BE(g + cp + 2);
            cp += 4;

            float dx = 0.0f, dy = 0.0f;
            if ((compFlags & 0x0001u) != 0) { // ARG_1_AND_2_ARE_WORDS
                if (cp + 4 > gLen) break;
                if ((compFlags & 0x0002u) != 0) {
                    dx = static_cast<float>(DetailImg100::RdI16BE(g + cp));
                    dy = static_cast<float>(DetailImg100::RdI16BE(g + cp + 2));
                }
                cp += 4;
            } else {
                if (cp + 2 > gLen) break;
                if ((compFlags & 0x0002u) != 0) {
                    dx = static_cast<float>(static_cast<int8_t>(g[cp]));
                    dy = static_cast<float>(static_cast<int8_t>(g[cp + 1]));
                }
                cp += 2;
            }

            float m00 = 1.0f, m01 = 0.0f, m10 = 0.0f, m11 = 1.0f;
            if ((compFlags & 0x0008u) != 0) { // WE_HAVE_A_SCALE
                if (cp + 2 > gLen) break;
                m00 = m11 = static_cast<float>(DetailImg100::RdI16BE(g + cp)) / 16384.0f;
                cp += 2;
            } else if ((compFlags & 0x0040u) != 0) { // WE_HAVE_AN_X_AND_Y_SCALE
                if (cp + 4 > gLen) break;
                m00 = static_cast<float>(DetailImg100::RdI16BE(g + cp)) / 16384.0f;
                m11 = static_cast<float>(DetailImg100::RdI16BE(g + cp + 2)) / 16384.0f;
                cp += 4;
            } else if ((compFlags & 0x0080u) != 0) { // WE_HAVE_A_TWO_BY_TWO
                if (cp + 8 > gLen) break;
                m00 = static_cast<float>(DetailImg100::RdI16BE(g + cp + 0)) / 16384.0f;
                m01 = static_cast<float>(DetailImg100::RdI16BE(g + cp + 2)) / 16384.0f;
                m10 = static_cast<float>(DetailImg100::RdI16BE(g + cp + 4)) / 16384.0f;
                m11 = static_cast<float>(DetailImg100::RdI16BE(g + cp + 6)) / 16384.0f;
                cp += 8;
            }

            std::vector<EdgeSeg> childEdges;
            int16_t cxMin = 0, cxMax = 0;
            if (ExtractGlyphEdges(sfnt, childGlyph, childEdges, cxMin, cxMax, depth + 1)) {
                for (const auto& ce : childEdges) {
                    EdgeSeg te;
                    te.x0 = ce.x0 * m00 + ce.y0 * m10 + dx;
                    te.y0 = ce.x0 * m01 + ce.y0 * m11 + dy;
                    te.x1 = ce.x1 * m00 + ce.y1 * m10 + dx;
                    te.y1 = ce.x1 * m01 + ce.y1 * m11 + dy;
                    outEdges.push_back(te);
                }
            }
        }
        return true;
    }

    // Simple glyph (numContours > 0)
    size_t nc = static_cast<size_t>(numContours);
    if (10u + nc * 2u + 2u > gLen) return false;
    std::vector<uint16_t> endPts(nc);
    for (size_t c = 0; c < nc; ++c) {
        endPts[c] = DetailImg100::RdU16BE(g + 10 + c * 2u);
        if (c > 0 && endPts[c] <= endPts[c - 1]) return false;
    }
    size_t numPts = static_cast<size_t>(endPts.back()) + 1u;
    if (numPts > 8192) return false;

    uint16_t instrLen = DetailImg100::RdU16BE(g + 10 + nc * 2u);
    size_t fp = 10u + nc * 2u + 2u + instrLen;
    if (fp > gLen) return false;

    std::vector<uint8_t> flags(numPts);
    for (size_t i = 0; i < numPts; ) {
        if (fp >= gLen) return false;
        uint8_t fl = g[fp++];
        flags[i++] = fl;
        if ((fl & 0x08u) != 0) { // REPEAT_FLAG
            if (fp >= gLen) return false;
            uint8_t rep = g[fp++];
            for (uint8_t r = 0; r < rep && i < numPts; ++r) flags[i++] = fl;
        }
    }

    std::vector<float> px(numPts), py(numPts);
    int32_t curX = 0;
    for (size_t i = 0; i < numPts; ++i) {
        uint8_t fl = flags[i];
        if ((fl & 0x02u) != 0) { // X_SHORT_VECTOR
            if (fp >= gLen) return false;
            int32_t d = g[fp++];
            curX += ((fl & 0x10u) != 0) ? d : -d;
        } else if ((fl & 0x10u) == 0) {
            if (fp + 2 > gLen) return false;
            curX += DetailImg100::RdI16BE(g + fp);
            fp += 2;
        }
        px[i] = static_cast<float>(curX);
    }

    int32_t curY = 0;
    for (size_t i = 0; i < numPts; ++i) {
        uint8_t fl = flags[i];
        if ((fl & 0x04u) != 0) { // Y_SHORT_VECTOR
            if (fp >= gLen) return false;
            int32_t d = g[fp++];
            curY += ((fl & 0x20u) != 0) ? d : -d;
        } else if ((fl & 0x20u) == 0) {
            if (fp + 2 > gLen) return false;
            curY += DetailImg100::RdI16BE(g + fp);
            fp += 2;
        }
        py[i] = static_cast<float>(curY);
    }

    // Flatten TrueType contours (handling on-curve & off-curve quadratic Beziers with implied midpoints)
    size_t startIdx = 0;
    for (size_t c = 0; c < nc; ++c) {
        size_t endIdx = endPts[c];
        size_t cnt = endIdx - startIdx + 1u;
        if (cnt >= 2) {
            auto ptX = [&](size_t k) { return px[startIdx + (k % cnt)]; };
            auto ptY = [&](size_t k) { return py[startIdx + (k % cnt)]; };
            auto isOn = [&](size_t k) { return (flags[startIdx + (k % cnt)] & 0x01u) != 0; };

            float sx = 0.0f, sy = 0.0f;
            size_t kStart = 0;
            if (isOn(0)) {
                sx = ptX(0); sy = ptY(0);
                kStart = 1;
            } else if (isOn(cnt - 1)) {
                sx = ptX(cnt - 1); sy = ptY(cnt - 1);
                kStart = 0;
            } else {
                sx = 0.5f * (ptX(cnt - 1) + ptX(0));
                sy = 0.5f * (ptY(cnt - 1) + ptY(0));
                kStart = 0;
            }

            float curPx = sx, curPy = sy;
            auto emitQuad = [&](float cx0, float cy0, float qx, float qy, float ex, float ey) {
                float lx = cx0, ly = cy0;
                constexpr int kSteps = 8;
                for (int s = 1; s <= kSteps; ++s) {
                    float t = static_cast<float>(s) / static_cast<float>(kSteps);
                    float omt = 1.0f - t;
                    float nx = omt * omt * cx0 + 2.0f * omt * t * qx + t * t * ex;
                    float ny = omt * omt * cy0 + 2.0f * omt * t * qy + t * t * ey;
                    outEdges.push_back({ lx, ly, nx, ny });
                    lx = nx; ly = ny;
                }
            };

            size_t k = kStart;
            while (k < cnt) {
                if (isOn(k)) {
                    float nx = ptX(k), ny = ptY(k);
                    outEdges.push_back({ curPx, curPy, nx, ny });
                    curPx = nx; curPy = ny;
                    ++k;
                } else {
                    float ctrlX = ptX(k), ctrlY = ptY(k);
                    if (k + 1 < cnt) {
                        if (isOn(k + 1)) {
                            float nx = ptX(k + 1), ny = ptY(k + 1);
                            emitQuad(curPx, curPy, ctrlX, ctrlY, nx, ny);
                            curPx = nx; curPy = ny;
                            k += 2;
                        } else {
                            float midX = 0.5f * (ctrlX + ptX(k + 1));
                            float midY = 0.5f * (ctrlY + ptY(k + 1));
                            emitQuad(curPx, curPy, ctrlX, ctrlY, midX, midY);
                            curPx = midX; curPy = midY;
                            ++k;
                        }
                    } else {
                        emitQuad(curPx, curPy, ctrlX, ctrlY, sx, sy);
                        curPx = sx; curPy = sy;
                        ++k;
                    }
                }
            }
            if (std::abs(curPx - sx) > 1e-3f || std::abs(curPy - sy) > 1e-3f) {
                outEdges.push_back({ curPx, curPy, sx, sy });
            }
        }
        startIdx = endIdx + 1u;
    }
    return true;
}

// 4x4 supersampled non-zero winding scanline rasterizer for TrueType contours
inline bool RasterizeGlyphContours(
    const ParsedSfnt& sfnt,
    uint16_t glyphId,
    float originX,
    float baselineY,
    float pixelHeight,
    uint8_t inkR,
    uint8_t inkG,
    uint8_t inkB,
    std::vector<uint8_t>& bgra,
    uint32_t imgW,
    uint32_t imgH,
    float& outAdvancePx)
{
    float scale = pixelHeight / static_cast<float>(std::max<uint16_t>(16, sfnt.unitsPerEm));
    outAdvancePx = static_cast<float>(GetGlyphAdvance(sfnt, glyphId)) * scale;

    std::vector<EdgeSeg> rawEdges;
    int16_t gxMin = 0, gxMax = 0;
    if (!ExtractGlyphEdges(sfnt, glyphId, rawEdges, gxMin, gxMax, 0)) {
        return false;
    }
    if (rawEdges.empty()) {
        return true;
    }
    if (outAdvancePx <= 1.0f && gxMax > gxMin) {
        outAdvancePx = static_cast<float>(gxMax - gxMin) * scale + pixelHeight * 0.12f;
    }

    std::vector<EdgeSeg> scrEdges;
    scrEdges.reserve(rawEdges.size());
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (const auto& e : rawEdges) {
        EdgeSeg se;
        se.x0 = originX + e.x0 * scale;
        se.y0 = baselineY - e.y0 * scale;
        se.x1 = originX + e.x1 * scale;
        se.y1 = baselineY - e.y1 * scale;
        if (std::abs(se.y1 - se.y0) < 1e-5f) continue;
        minX = std::min({ minX, se.x0, se.x1 });
        maxX = std::max({ maxX, se.x0, se.x1 });
        minY = std::min({ minY, se.y0, se.y1 });
        maxY = std::max({ maxY, se.y0, se.y1 });
        scrEdges.push_back(se);
    }
    if (scrEdges.empty()) return true;

    int x0 = std::max(0, static_cast<int>(std::floor(minX)) - 1);
    int x1 = std::min(static_cast<int>(imgW) - 1, static_cast<int>(std::ceil(maxX)) + 1);
    int y0 = std::max(0, static_cast<int>(std::floor(minY)) - 1);
    int y1 = std::min(static_cast<int>(imgH) - 1, static_cast<int>(std::ceil(maxY)) + 1);
    if (x0 > x1 || y0 > y1) return true;

    struct Hit {
        float x;
        int dir;
    };
    std::vector<Hit> hits;
    std::vector<uint8_t> rowCov(static_cast<size_t>(x1 - x0 + 1), 0);

    for (int py = y0; py <= y1; ++py) {
        std::fill(rowCov.begin(), rowCov.end(), uint8_t(0));
        for (int subY = 0; subY < 4; ++subY) {
            float sy = static_cast<float>(py) + (static_cast<float>(subY) + 0.5f) * 0.25f;
            hits.clear();
            for (const auto& e : scrEdges) {
                if ((e.y0 <= sy && e.y1 > sy) || (e.y1 <= sy && e.y0 > sy)) {
                    int dir = (e.y1 > e.y0) ? 1 : -1;
                    float xHit = e.x0 + (sy - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0);
                    hits.push_back({ xHit, dir });
                }
            }
            if (hits.size() < 2) continue;
            std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.x < b.x; });

            int winding = 0;
            for (size_t hi = 0; hi + 1 < hits.size(); ++hi) {
                winding += hits[hi].dir;
                if (winding != 0) {
                    float segL = hits[hi].x;
                    float segR = hits[hi + 1].x;
                    int pxStart = std::max(x0, static_cast<int>(std::floor(segL)));
                    int pxEnd   = std::min(x1, static_cast<int>(std::ceil(segR)));
                    for (int px = pxStart; px <= pxEnd; ++px) {
                        for (int subX = 0; subX < 4; ++subX) {
                            float sx = static_cast<float>(px) + (static_cast<float>(subX) + 0.5f) * 0.25f;
                            if (sx >= segL && sx < segR) {
                                ++rowCov[static_cast<size_t>(px - x0)];
                            }
                        }
                    }
                }
            }
        }

        for (int px = x0; px <= x1; ++px) {
            uint8_t c = rowCov[static_cast<size_t>(px - x0)];
            if (c == 0) continue;
            float a = std::clamp(static_cast<float>(c) / 16.0f, 0.0f, 1.0f);
            uint8_t* dst = &bgra[(static_cast<size_t>(py) * imgW + static_cast<size_t>(px)) * 4];
            dst[0] = static_cast<uint8_t>(dst[0] * (1.0f - a) + inkB * a);
            dst[1] = static_cast<uint8_t>(dst[1] * (1.0f - a) + inkG * a);
            dst[2] = static_cast<uint8_t>(dst[2] * (1.0f - a) + inkR * a);
            dst[3] = 255;
        }
    }
    return true;
}

inline size_t RasterizeTextLineGlyf(
    const ParsedSfnt& sfnt,
    std::wstring_view text,
    float x,
    float baselineY,
    float pixelHeight,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    std::vector<uint8_t>& bgra,
    uint32_t imgW,
    uint32_t imgH)
{
    float curX = x;
    size_t drawnGlyphs = 0;
    for (wchar_t wc : text) {
        if (wc == L' ') {
            uint16_t spGid = LookupCmapGlyph(sfnt, ' ');
            float spAdv = (spGid != 0)
                ? (static_cast<float>(GetGlyphAdvance(sfnt, spGid)) * pixelHeight / std::max<uint16_t>(16, sfnt.unitsPerEm))
                : (pixelHeight * 0.32f);
            curX += std::max(pixelHeight * 0.22f, spAdv);
            continue;
        }
        uint16_t gid = LookupCmapGlyph(sfnt, static_cast<uint32_t>(wc));
        if (gid == 0 && wc >= L'a' && wc <= L'z') {
            gid = LookupCmapGlyph(sfnt, static_cast<uint32_t>(wc - L'a' + L'A'));
        }
        if (gid == 0) {
            curX += pixelHeight * 0.35f;
            continue;
        }
        float adv = 0.0f;
        if (RasterizeGlyphContours(sfnt, gid, curX, baselineY, pixelHeight, r, g, b, bgra, imgW, imgH, adv)) {
            ++drawnGlyphs;
        }
        curX += std::max(pixelHeight * 0.20f, adv);
        if (curX >= static_cast<float>(imgW) - 12.0f) break;
    }
    return drawnGlyphs;
}

inline bool ReconstructWoff2TransformedGlyfLoca(
    const uint8_t* xfGlyf, size_t xfLen,
    std::vector<uint8_t>& outGlyf, std::vector<uint8_t>& outLoca, uint16_t& outIndexFormat)
{
    if (!xfGlyf || xfLen < 36u) return false;
    uint16_t numGlyphs   = DetailImg100::RdU16BE(xfGlyf + 4u);
    uint16_t indexFormat = DetailImg100::RdU16BE(xfGlyf + 6u);
    uint32_t nContourSz  = DetailImg100::RdU32BE(xfGlyf + 8u);
    uint32_t nPointsSz   = DetailImg100::RdU32BE(xfGlyf + 12u);
    uint32_t flagSz      = DetailImg100::RdU32BE(xfGlyf + 16u);
    uint32_t glyphSz     = DetailImg100::RdU32BE(xfGlyf + 20u);
    uint32_t compSz      = DetailImg100::RdU32BE(xfGlyf + 24u);
    uint32_t bboxTotalSz = DetailImg100::RdU32BE(xfGlyf + 28u);
    uint32_t instrSz     = DetailImg100::RdU32BE(xfGlyf + 32u);
    if (numGlyphs == 0u || indexFormat > 1u) return false;

    uint64_t totalSub = 36ull + nContourSz + nPointsSz + flagSz + glyphSz + compSz + bboxTotalSz + instrSz;
    if (totalSub > xfLen || nContourSz < static_cast<uint32_t>(numGlyphs) * 2u) return false;

    uint32_t bboxBitmapBytes = ((static_cast<uint32_t>(numGlyphs) + 31u) >> 5u) * 4u;
    if (bboxTotalSz < bboxBitmapBytes) return false;

    size_t offContour = 36u;
    size_t offPoints  = offContour + nContourSz;
    size_t offFlags   = offPoints  + nPointsSz;
    size_t offGlyph   = offFlags   + flagSz;
    size_t offComp    = offGlyph   + glyphSz;
    size_t offBboxBmp = offComp    + compSz;
    size_t offBboxStr = offBboxBmp + bboxBitmapBytes;
    size_t offInstr   = offBboxBmp + bboxTotalSz;

    size_t pPoints = offPoints, endPoints = offFlags;
    size_t pFlags  = offFlags,  endFlags  = offGlyph;
    size_t pGlyph  = offGlyph,  endGlyph  = offComp;
    size_t pComp   = offComp,   endComp   = offBboxBmp;
    size_t pBbox   = offBboxStr, endBbox  = offInstr;
    size_t pInstr  = offInstr,  endInstr  = offInstr + instrSz;

    auto rd255UShort = [&](size_t& p, size_t lim, uint16_t& outU) -> bool {
        if (p >= lim) return false;
        uint8_t code = xfGlyf[p++];
        if (code == 253u) { if (p + 2u > lim) return false; outU = DetailImg100::RdU16BE(xfGlyf + p); p += 2u; return true; }
        if (code == 255u) { if (p >= lim) return false; outU = static_cast<uint16_t>(253u + xfGlyf[p++]); return true; }
        if (code == 254u) { if (p >= lim) return false; outU = static_cast<uint16_t>(506u + xfGlyf[p++]); return true; }
        outU = code; return true;
    };

    outIndexFormat = indexFormat;
    outGlyf.clear();
    std::vector<uint32_t> glyphOffsets(static_cast<size_t>(numGlyphs) + 1u, 0u);

    for (uint16_t gid = 0; gid < numGlyphs; ++gid) {
        glyphOffsets[gid] = static_cast<uint32_t>(outGlyf.size());
        int16_t nContours = DetailImg100::RdI16BE(xfGlyf + offContour + static_cast<size_t>(gid) * 2u);
        bool hasExplicitBbox = ((xfGlyf[offBboxBmp + (gid >> 3u)] >> (7u - (gid & 7u))) & 1u) != 0u;

        if (nContours == 0) {
            if (hasExplicitBbox) return false; // Empty glyphs cannot have explicit bbox per WOFF2 spec
            continue;
        }

        if (nContours < 0) {
            // Composite glyph
            if (!hasExplicitBbox || pBbox + 8u > endBbox) return false;
            size_t compStart = pComp;
            bool haveInstr = false;
            uint16_t cFlags = 0x0020u;
            while ((cFlags & 0x0020u) != 0u) {
                if (pComp + 4u > endComp) return false;
                cFlags = DetailImg100::RdU16BE(xfGlyf + pComp);
                if ((cFlags & 0x0100u) != 0u) haveInstr = true;
                pComp += 4u;
                pComp += ((cFlags & 0x0001u) != 0u) ? 4u : 2u;
                if ((cFlags & 0x0008u) != 0u) pComp += 2u;
                else if ((cFlags & 0x0040u) != 0u) pComp += 4u;
                else if ((cFlags & 0x0080u) != 0u) pComp += 8u;
                if (pComp > endComp) return false;
            }
            uint16_t instrLen = 0u;
            if (haveInstr) {
                if (!rd255UShort(pGlyph, endGlyph, instrLen) || pInstr + instrLen > endInstr) return false;
            }
            size_t recStart = outGlyf.size();
            outGlyf.resize(recStart + 10u);
            WrU16BE(outGlyf.data() + recStart, static_cast<uint16_t>(nContours));
            std::memcpy(outGlyf.data() + recStart + 2u, xfGlyf + pBbox, 8u);
            pBbox += 8u;
            outGlyf.insert(outGlyf.end(), xfGlyf + compStart, xfGlyf + pComp);
            if (haveInstr) {
                uint8_t il[2]; WrU16BE(il, instrLen);
                outGlyf.insert(outGlyf.end(), il, il + 2u);
                outGlyf.insert(outGlyf.end(), xfGlyf + pInstr, xfGlyf + pInstr + instrLen);
                pInstr += instrLen;
            }
        } else {
            // Simple glyph: decode points, triplets, instructions, and serialize TrueType glyf record
            uint16_t nc = static_cast<uint16_t>(nContours);
            std::vector<uint16_t> endPts(nc, 0u);
            uint32_t totalPts = 0u;
            for (uint16_t c = 0; c < nc; ++c) {
                uint16_t np = 0u;
                if (!rd255UShort(pPoints, endPoints, np) || np == 0u) return false;
                totalPts += np;
                if (totalPts > 65535u) return false;
                endPts[c] = static_cast<uint16_t>(totalPts - 1u);
            }

            std::vector<int16_t> dxArr(totalPts, 0), dyArr(totalPts, 0);
            std::vector<uint8_t> onCurveArr(totalPts, 0u);
            int32_t curX = 0, curY = 0;
            int32_t xMin = 32767, yMin = 32767, xMax = -32768, yMax = -32768;

            for (uint32_t pi = 0; pi < totalPts; ++pi) {
                if (pFlags >= endFlags) return false;
                uint8_t fl = xfGlyf[pFlags++];
                onCurveArr[pi] = ((fl & 0x80u) == 0u) ? 1u : 0u;
                uint8_t f = fl & 0x7Fu;
                int32_t dx = 0, dy = 0;
                if (f < 10u) {
                    if (pGlyph >= endGlyph) return false;
                    int32_t mag = (static_cast<int32_t>(f & 14u) << 7) + xfGlyf[pGlyph++];
                    dy = ((f & 1u) != 0u) ? mag : -mag;
                } else if (f < 20u) {
                    if (pGlyph >= endGlyph) return false;
                    uint8_t fi = static_cast<uint8_t>(f - 10u);
                    int32_t mag = (static_cast<int32_t>(fi & 14u) << 7) + xfGlyf[pGlyph++];
                    dx = ((fi & 1u) != 0u) ? mag : -mag;
                } else if (f < 84u) {
                    if (pGlyph >= endGlyph) return false;
                    uint8_t b0 = xfGlyf[pGlyph++];
                    uint8_t fi = static_cast<uint8_t>(f - 20u);
                    int32_t mx = 1 + static_cast<int32_t>(fi & 0x30u) + (b0 >> 4u);
                    int32_t my = 1 + (static_cast<int32_t>(fi & 0x0Cu) << 2) + (b0 & 0x0Fu);
                    dx = ((fi & 1u) != 0u) ? mx : -mx;
                    dy = ((fi & 2u) != 0u) ? my : -my;
                } else if (f < 120u) {
                    if (pGlyph + 2u > endGlyph) return false;
                    uint8_t b0 = xfGlyf[pGlyph++], b1 = xfGlyf[pGlyph++];
                    uint8_t fi = static_cast<uint8_t>(f - 84u);
                    int32_t mx = 1 + ((static_cast<int32_t>(fi / 12u) << 8) + b0);
                    int32_t my = 1 + (((static_cast<int32_t>(fi % 12u) >> 2) << 8) + b1);
                    dx = ((fi & 1u) != 0u) ? mx : -mx;
                    dy = ((fi & 2u) != 0u) ? my : -my;
                } else if (f < 124u) {
                    if (pGlyph + 3u > endGlyph) return false;
                    uint8_t b0 = xfGlyf[pGlyph++], b1 = xfGlyf[pGlyph++], b2 = xfGlyf[pGlyph++];
                    int32_t mx = (static_cast<int32_t>(b0) << 4) + (b1 >> 4u);
                    int32_t my = (static_cast<int32_t>(b1 & 0x0Fu) << 8) + b2;
                    dx = ((f & 1u) != 0u) ? mx : -mx;
                    dy = ((f & 2u) != 0u) ? my : -my;
                } else {
                    if (pGlyph + 4u > endGlyph) return false;
                    uint8_t b0 = xfGlyf[pGlyph++], b1 = xfGlyf[pGlyph++], b2 = xfGlyf[pGlyph++], b3 = xfGlyf[pGlyph++];
                    int32_t mx = (static_cast<int32_t>(b0) << 8) + b1;
                    int32_t my = (static_cast<int32_t>(b2) << 8) + b3;
                    dx = ((f & 1u) != 0u) ? mx : -mx;
                    dy = ((f & 2u) != 0u) ? my : -my;
                }
                dxArr[pi] = static_cast<int16_t>(dx);
                dyArr[pi] = static_cast<int16_t>(dy);
                curX += dx;
                curY += dy;
                xMin = std::min(xMin, curX); xMax = std::max(xMax, curX);
                yMin = std::min(yMin, curY); yMax = std::max(yMax, curY);
            }

            uint16_t instrLen = 0u;
            if (!rd255UShort(pGlyph, endGlyph, instrLen) || pInstr + instrLen > endInstr) return false;

            size_t recStart = outGlyf.size();
            outGlyf.resize(recStart + 10u + static_cast<size_t>(nc) * 2u + 2u + instrLen);
            WrU16BE(outGlyf.data() + recStart + 0u, nc);
            if (hasExplicitBbox) {
                if (pBbox + 8u > endBbox) return false;
                std::memcpy(outGlyf.data() + recStart + 2u, xfGlyf + pBbox, 8u);
                pBbox += 8u;
            } else {
                WrU16BE(outGlyf.data() + recStart + 2u, static_cast<uint16_t>(static_cast<int16_t>(xMin)));
                WrU16BE(outGlyf.data() + recStart + 4u, static_cast<uint16_t>(static_cast<int16_t>(yMin)));
                WrU16BE(outGlyf.data() + recStart + 6u, static_cast<uint16_t>(static_cast<int16_t>(xMax)));
                WrU16BE(outGlyf.data() + recStart + 8u, static_cast<uint16_t>(static_cast<int16_t>(yMax)));
            }
            for (uint16_t c = 0; c < nc; ++c) {
                WrU16BE(outGlyf.data() + recStart + 10u + static_cast<size_t>(c) * 2u, endPts[c]);
            }
            size_t ilOff = recStart + 10u + static_cast<size_t>(nc) * 2u;
            WrU16BE(outGlyf.data() + ilOff, instrLen);
            if (instrLen > 0u) {
                std::memcpy(outGlyf.data() + ilOff + 2u, xfGlyf + pInstr, instrLen);
                pInstr += instrLen;
            }

            // Encode TrueType flags + X deltas + Y deltas
            std::vector<uint8_t> ttFlags(totalPts, 0u), xBytes, yBytes;
            for (uint32_t pi = 0; pi < totalPts; ++pi) {
                uint8_t tf = onCurveArr[pi];
                int16_t dx = dxArr[pi], dy = dyArr[pi];
                if (dx == 0) {
                    tf |= 0x10u;
                } else if (dx >= -255 && dx <= 255) {
                    tf |= 0x02u | ((dx > 0) ? 0x10u : 0x00u);
                    xBytes.push_back(static_cast<uint8_t>(std::abs(dx)));
                } else {
                    xBytes.push_back(static_cast<uint8_t>(static_cast<uint16_t>(dx) >> 8u));
                    xBytes.push_back(static_cast<uint8_t>(static_cast<uint16_t>(dx) & 0xFFu));
                }
                if (dy == 0) {
                    tf |= 0x20u;
                } else if (dy >= -255 && dy <= 255) {
                    tf |= 0x04u | ((dy > 0) ? 0x20u : 0x00u);
                    yBytes.push_back(static_cast<uint8_t>(std::abs(dy)));
                } else {
                    yBytes.push_back(static_cast<uint8_t>(static_cast<uint16_t>(dy) >> 8u));
                    yBytes.push_back(static_cast<uint8_t>(static_cast<uint16_t>(dy) & 0xFFu));
                }
                ttFlags[pi] = tf;
            }
            outGlyf.insert(outGlyf.end(), ttFlags.begin(), ttFlags.end());
            outGlyf.insert(outGlyf.end(), xBytes.begin(), xBytes.end());
            outGlyf.insert(outGlyf.end(), yBytes.begin(), yBytes.end());
        }

        // Pad glyph record to 4-byte boundary (required for short loca alignment too)
        while ((outGlyf.size() & 3u) != 0u) outGlyf.push_back(0u);
    }
    glyphOffsets[numGlyphs] = static_cast<uint32_t>(outGlyf.size());
    if (indexFormat == 0u && (outGlyf.size() / 2u) > 0xFFFFu) {
        outIndexFormat = 1u;
    }
    if (outIndexFormat == 0u) {
        outLoca.resize((static_cast<size_t>(numGlyphs) + 1u) * 2u);
        for (size_t i = 0; i <= numGlyphs; ++i) {
            WrU16BE(outLoca.data() + i * 2u, static_cast<uint16_t>(glyphOffsets[i] >> 1u));
        }
    } else {
        outLoca.resize((static_cast<size_t>(numGlyphs) + 1u) * 4u);
        for (size_t i = 0; i <= numGlyphs; ++i) {
            WrU32BE(outLoca.data() + i * 4u, glyphOffsets[i]);
        }
    }
    return true;
}

inline bool ReconstructSfntFromWoff2(const uint8_t* data, size_t size, std::vector<uint8_t>& outSfnt) {
    outSfnt.clear();
    if (!data || size < 48u || std::memcmp(data, "wOF2", 4) != 0) return false;

    uint32_t flavor        = DetailImg100::RdU32BE(data + 4u);
    uint32_t woff2Length   = DetailImg100::RdU32BE(data + 8u);
    uint16_t numTables     = DetailImg100::RdU16BE(data + 12u);
    uint16_t reserved      = DetailImg100::RdU16BE(data + 14u);
    uint32_t totalSfntSize = DetailImg100::RdU32BE(data + 16u);
    uint32_t totalCompSize = DetailImg100::RdU32BE(data + 20u);

    bool validFlavor = (flavor == 0x00010000u) ||
                       (flavor == MakeSfntTag("OTTO")) ||
                       (flavor == MakeSfntTag("true")) ||
                       (flavor == MakeSfntTag("ttcf"));
    if (!validFlavor || reserved != 0u || woff2Length < 48u || woff2Length > size ||
        numTables == 0u || numTables > 256u || totalCompSize == 0u ||
        totalSfntSize == 0u || totalSfntSize > (64u << 20u)) {
        return false;
    }

    // Parse WOFF2 table directory & validate stream bounds before unpacking
    static const char kWoff2KnownTags[63][5] = {
        "cmap","head","hhea","hmtx","maxp","name","OS/2","post","cvt ","fpgm","glyf","loca","prep","CFF ","VORG","EBDT",
        "EBLC","gasp","hdmx","kern","LTSH","PCLT","VDMX","vhea","vmtx","BASE","GDEF","GPOS","GSUB","EBSC","JSTF","MATH",
        "CBDT","CBLC","COLR","CPAL","SVG ","sbix","acnt","avar","bdat","bloc","bsln","cvar","fdsc","feat","fmtx","fvar",
        "gvar","hsty","just","lcar","mort","morx","opbd","prop","trak","Zapf","Silf","Glat","Gloc","Feat","Sill"
    };

    size_t pos = 48u;
    auto rdUIntBase128 = [&](uint32_t& outVal) -> bool {
        uint32_t accum = 0u;
        for (int i = 0; i < 5; ++i) {
            if (pos >= size) return false;
            uint8_t b = data[pos++];
            if (i == 0 && b == 0x80u) return false;
            if ((accum & 0xFE000000u) != 0u) return false;
            accum = (accum << 7u) | static_cast<uint32_t>(b & 0x7Fu);
            if ((b & 0x80u) == 0u) { outVal = accum; return true; }
        }
        return false;
    };

    struct Woff2Entry {
        uint32_t tag = 0;
        uint8_t xformVer = 0;
        uint32_t origLen = 0;
        uint32_t xformLen = 0;
        std::vector<uint8_t> rawBytes;
    };
    std::vector<Woff2Entry> entries;
    entries.reserve(numTables);
    uint64_t totalUnpackedSum = 0ull;

    for (uint16_t i = 0; i < numTables; ++i) {
        if (pos >= size) return false;
        uint8_t flags = data[pos++];
        uint8_t tagIdx = flags & 0x3Fu;
        uint8_t xformVer = (flags >> 6u) & 0x03u;
        Woff2Entry e{};
        e.xformVer = xformVer;
        if (tagIdx == 63u) {
            if (pos + 4u > size) return false;
            e.tag = DetailImg100::RdU32BE(data + pos);
            pos += 4u;
        } else {
            e.tag = MakeSfntTag(kWoff2KnownTags[tagIdx]);
        }
        if (!rdUIntBase128(e.origLen)) return false;
        bool isGlyfOrLoca = (e.tag == MakeSfntTag("glyf") || e.tag == MakeSfntTag("loca"));
        bool hasXformLen = isGlyfOrLoca ? (xformVer == 0u) : (xformVer != 0u);
        if (hasXformLen) {
            if (!rdUIntBase128(e.xformLen)) return false;
        } else {
            e.xformLen = e.origLen;
        }
        if (e.tag == MakeSfntTag("loca") && xformVer == 0u && e.xformLen != 0u) return false;
        totalUnpackedSum += e.xformLen;
        if (totalUnpackedSum > (64ull << 20u)) return false;
        entries.push_back(std::move(e));
    }

    if (flavor == MakeSfntTag("ttcf")) {
        if (pos + 4u > size) return false;
        pos += 4u;
        auto rd255UShort = [&](uint16_t& outU) -> bool {
            if (pos >= size) return false;
            uint8_t code = data[pos++];
            if (code == 253u) { if (pos + 2u > size) return false; outU = DetailImg100::RdU16BE(data + pos); pos += 2u; return true; }
            if (code == 255u) { if (pos >= size) return false; outU = static_cast<uint16_t>(253u + data[pos++]); return true; }
            if (code == 254u) { if (pos >= size) return false; outU = static_cast<uint16_t>(506u + data[pos++]); return true; }
            outU = code; return true;
        };
        uint16_t nFonts = 0u;
        if (!rd255UShort(nFonts) || nFonts == 0u) return false;
        for (uint16_t f = 0; f < nFonts; ++f) {
            uint16_t nt = 0u;
            if (!rd255UShort(nt) || pos + 4u > size) return false;
            pos += 4u;
            for (uint16_t k = 0; k < nt; ++k) {
                uint16_t idx = 0u;
                if (!rd255UShort(idx)) return false;
            }
        }
    }

    if (totalCompSize > size - pos) return false;
    const uint8_t* compStream = data + pos;

    // 1. Primary Unpacker: DirectWrite IDWriteFactory5::UnpackFontFile(DWRITE_CONTAINER_TYPE_WOFF2, ...)
    {
        IDWriteFactory5* dwrite5 = nullptr;
        HRESULT hr = ::DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory5),
            reinterpret_cast<IUnknown**>(&dwrite5));
        if (SUCCEEDED(hr) && dwrite5) {
            IDWriteFontFileStream* fontStream = nullptr;
            if (SUCCEEDED(dwrite5->UnpackFontFile(
                    DWRITE_CONTAINER_TYPE_WOFF2,
                    data,
                    static_cast<UINT32>(size),
                    &fontStream)) && fontStream)
            {
                UINT64 sfntSize = 0;
                if (SUCCEEDED(fontStream->GetFileSize(&sfntSize)) && sfntSize >= 28u && sfntSize <= (64ull << 20u)) {
                    const void* fragPtr = nullptr;
                    void* fragCtx = nullptr;
                    if (SUCCEEDED(fontStream->ReadFileFragment(&fragPtr, 0, sfntSize, &fragCtx)) && fragPtr) {
                        const uint8_t* bytes = static_cast<const uint8_t*>(fragPtr);
                        outSfnt.assign(bytes, bytes + static_cast<size_t>(sfntSize));
                        fontStream->ReleaseFileFragment(fragCtx);
                    }
                }
                fontStream->Release();
            }
            dwrite5->Release();
        }
        if (!outSfnt.empty()) {
            return true;
        }
    }

    // 2. Pure-C++ Fallback Unpacker: RFC 7932 Brotli + SFNT / glyf / loca reconstruction
    if (flavor == MakeSfntTag("ttcf")) return false;
    std::vector<uint8_t> unpacked;
    if (!DetailBrotli::Decompress(compStream, totalCompSize, unpacked, static_cast<size_t>(totalUnpackedSum)) ||
        unpacked.size() != static_cast<size_t>(totalUnpackedSum)) {
        return false;
    }

    int idxGlyf = -1, idxLoca = -1, idxHead = -1;
    size_t uOff = 0u;
    for (size_t i = 0; i < entries.size(); ++i) {
        auto& e = entries[i];
        if (uOff + e.xformLen > unpacked.size()) return false;
        const uint8_t* slice = unpacked.data() + uOff;
        uOff += e.xformLen;

        if (e.tag == MakeSfntTag("glyf")) idxGlyf = static_cast<int>(i);
        else if (e.tag == MakeSfntTag("loca")) idxLoca = static_cast<int>(i);
        else if (e.tag == MakeSfntTag("head")) idxHead = static_cast<int>(i);

        bool isGlyfOrLoca = (e.tag == MakeSfntTag("glyf") || e.tag == MakeSfntTag("loca"));
        bool untransformed = isGlyfOrLoca ? (e.xformVer == 3u) : (e.xformVer == 0u);
        if (untransformed) {
            e.rawBytes.assign(slice, slice + e.xformLen);
        } else if (e.tag == MakeSfntTag("glyf") && e.xformVer == 0u) {
            e.rawBytes.assign(slice, slice + e.xformLen); // Reconstructed below with loca
        } else if (e.tag == MakeSfntTag("loca") && e.xformVer == 0u) {
            // Empty in stream; reconstructed from glyf
        } else {
            return false; // Unsupported custom table transform
        }
    }

    if (idxGlyf >= 0 && entries[idxGlyf].xformVer == 0u) {
        if (idxLoca < 0) return false;
        std::vector<uint8_t> recGlyf, recLoca;
        uint16_t recIdxFmt = 0u;
        if (!ReconstructWoff2TransformedGlyfLoca(
                entries[idxGlyf].rawBytes.data(), entries[idxGlyf].rawBytes.size(),
                recGlyf, recLoca, recIdxFmt)) {
            return false;
        }
        entries[idxGlyf].rawBytes = std::move(recGlyf);
        entries[idxGlyf].origLen  = static_cast<uint32_t>(entries[idxGlyf].rawBytes.size());
        entries[idxLoca].rawBytes = std::move(recLoca);
        entries[idxLoca].origLen  = static_cast<uint32_t>(entries[idxLoca].rawBytes.size());
        if (idxHead >= 0 && entries[idxHead].rawBytes.size() >= 54u) {
            WrU16BE(entries[idxHead].rawBytes.data() + 50u, recIdxFmt);
        }
    }

    if (idxHead >= 0 && entries[idxHead].rawBytes.size() >= 12u) {
        WrU32BE(entries[idxHead].rawBytes.data() + 8u, 0u); // Zero checkSumAdjustment before computing checksums
    }

    std::sort(entries.begin(), entries.end(), [](const Woff2Entry& a, const Woff2Entry& b) {
        return a.tag < b.tag;
    });

    size_t totalDataAligned = 0u;
    for (const auto& e : entries) {
        totalDataAligned += (e.rawBytes.size() + 3u) & ~size_t(3u);
    }

    uint16_t entrySelector = 0u;
    uint16_t maxPow2 = 1u;
    while (static_cast<uint32_t>(maxPow2) * 2u <= numTables) {
        maxPow2 = static_cast<uint16_t>(maxPow2 * 2u);
        ++entrySelector;
    }
    uint16_t searchRange = static_cast<uint16_t>(maxPow2 * 16u);
    uint16_t rangeShift  = static_cast<uint16_t>(numTables * 16u - searchRange);

    size_t dirBytes = 12u + static_cast<size_t>(numTables) * 16u;
    outSfnt.assign(dirBytes + totalDataAligned, 0u);
    WrU32BE(outSfnt.data() + 0u, flavor);
    WrU16BE(outSfnt.data() + 4u, numTables);
    WrU16BE(outSfnt.data() + 6u, searchRange);
    WrU16BE(outSfnt.data() + 8u, entrySelector);
    WrU16BE(outSfnt.data() + 10u, rangeShift);

    size_t curOff = dirBytes;
    size_t headTableOff = 0u;
    for (uint16_t i = 0; i < numTables; ++i) {
        const auto& e = entries[i];
        uint32_t csum = e.rawBytes.empty() ? 0u : CalcSfntChecksum(e.rawBytes.data(), e.rawBytes.size());
        uint8_t* rec = outSfnt.data() + 12u + static_cast<size_t>(i) * 16u;
        WrU32BE(rec + 0u, e.tag);
        WrU32BE(rec + 4u, csum);
        WrU32BE(rec + 8u, static_cast<uint32_t>(curOff));
        WrU32BE(rec + 12u, static_cast<uint32_t>(e.rawBytes.size()));
        if (e.tag == MakeSfntTag("head") && e.rawBytes.size() >= 12u) {
            headTableOff = curOff;
        }
        if (!e.rawBytes.empty()) {
            std::memcpy(outSfnt.data() + curOff, e.rawBytes.data(), e.rawBytes.size());
        }
        curOff += (e.rawBytes.size() + 3u) & ~size_t(3u);
    }

    if (headTableOff != 0u) {
        uint32_t fontCsum = CalcSfntChecksum(outSfnt.data(), outSfnt.size());
        WrU32BE(outSfnt.data() + headTableOff + 8u, 0xB1B0AFBAu - fontCsum);
    }
    return true;
}

} // namespace DetailFont

} // namespace Formats
#include "ArtFontUpgrades.h"
namespace Formats {

inline bool DecodeZipArtworkContainer(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (!data || size < 30 || data[0] != 'P' || data[1] != 'K') return false;
    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries) || entries.empty()) return false;

    std::wstring label = (extLower == L".kra") ? L"Krita Document (.kra)" :
                         (extLower == L".ora") ? L"OpenRaster Document (.ora)" :
                         (extLower == L".procreate") ? L"Procreate Artwork (.procreate)" : L"Layered Artwork Container";

    // 1. Native layer/tile compositors (.ora stack.xml, .kra VERSION 2 tile store, .procreate .chunk tiles)
    const ZipDeflate::ArchiveItemMeta* stackEnt = DetailArtFont::FindZipEntryCaseInsensitive(entries, L"stack.xml");
    if (stackEnt && (extLower == L".ora" || extLower.empty())) {
        std::vector<uint8_t> xmlBytes;
        if (ZipDeflate::ExtractZipEntryFromMemory(data, size, *stackEnt, xmlBytes) && !xmlBytes.empty()) {
            std::string_view sv(reinterpret_cast<const char*>(xmlBytes.data()), xmlBytes.size());
            if (DetailArtFont::CompositeOraFromStackXml(data, size, entries, sv, outInfo)) {
                return true;
            }
        }
    }

    const ZipDeflate::ArchiveItemMeta* mainDocEnt = DetailArtFont::FindZipEntryCaseInsensitive(entries, L"maindoc.xml");
    if (mainDocEnt && (extLower == L".kra" || extLower.empty())) {
        std::vector<uint8_t> xmlBytes;
        if (ZipDeflate::ExtractZipEntryFromMemory(data, size, *mainDocEnt, xmlBytes) && !xmlBytes.empty()) {
            std::string_view sv(reinterpret_cast<const char*>(xmlBytes.data()), xmlBytes.size());
            if (DetailArtFont::CompositeKritaFromMainDoc(data, size, entries, sv, outInfo)) {
                return true;
            }
        }
    }

    if (extLower == L".procreate") {
        if (DetailArtFont::CompositeProcreateChunks(data, size, entries, outInfo)) {
            return true;
        }
    }

    // 2. Fallback: extract layer names if not yet populated & decode mergedimage.png / preview PNG
    if (outInfo.layerNames.empty()) {
        for (const auto& e : entries) {
            std::wstring nl = PathUtils::ToLower(e.pathInArchive);
            if (nl == L"stack.xml" || nl == L"maindoc.xml") {
                std::vector<uint8_t> xmlBytes;
                if (ZipDeflate::ExtractZipEntryFromMemory(data, size, e, xmlBytes) && !xmlBytes.empty()) {
                    std::string_view sv(reinterpret_cast<const char*>(xmlBytes.data()), xmlBytes.size());
                    size_t pos = 0;
                    while ((pos = sv.find("name=\"", pos)) != std::string_view::npos && outInfo.layerNames.size() < 32) {
                        pos += 6;
                        size_t endQ = sv.find('"', pos);
                        if (endQ == std::string_view::npos) break;
                        outInfo.layerNames.push_back(PathUtils::Utf8ToWide(sv.substr(pos, endQ - pos)));
                        pos = endQ + 1;
                    }
                }
            }
        }
    }

    const ZipDeflate::ArchiveItemMeta* bestPng = nullptr;
    for (const auto& e : entries) {
        std::wstring nl = PathUtils::ToLower(e.pathInArchive);
        if (nl == L"mergedimage.png" || nl == L"thumbnails/thumbnail.png" ||
            nl == L"quicklook/thumbnail.png" || nl == L"preview.png") {
            bestPng = &e;
            break;
        }
        if (!bestPng && nl.size() > 4 && nl.substr(nl.size() - 4) == L".png") {
            bestPng = &e;
        }
    }
    if (bestPng) {
        std::vector<uint8_t> pngBytes;
        if (ZipDeflate::ExtractZipEntryFromMemory(data, size, *bestPng, pngBytes) && !pngBytes.empty()) {
            std::vector<std::wstring> savedLayers = std::move(outInfo.layerNames);
            if (DetailImg100::DecodeEmbeddedWicBuffer(pngBytes.data(), pngBytes.size(), label, outInfo)) {
                if (outInfo.layerNames.empty()) outInfo.layerNames = std::move(savedLayers);
                return true;
            }
        }
    }
    return false;
}

inline bool DecodeClipStudio(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return DetailArtFont::DecodeClipStudioUpgraded(data, size, outInfo);
}

inline bool DecodeAseprite(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return DetailArtFont::DecodeAsepriteUpgraded(data, size, outInfo);
}

inline bool DecodeFontSpecimen(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (!data || size < 28) return false;
    (void)extLower;

    // ------------------------------------------------------------------------
    // WOFF 2.0 ('wOF2') & WOFF 1.0 ('wOFF') -> Unpack SFNT buffer
    // TTF / OTF / TTC                       -> Direct SFNT buffer
    // ------------------------------------------------------------------------
    std::vector<uint8_t> reconstructedSfnt;
    const uint8_t* sfntPtr = data;
    size_t sfntLen = size;
    bool fromWoff = false;
    bool fromWoff2 = false;

    if (size >= 48 && std::memcmp(data, "wOF2", 4) == 0) {
        if (!DetailFont::ReconstructSfntFromWoff2(data, size, reconstructedSfnt)) {
            return false;
        }
        sfntPtr = reconstructedSfnt.data();
        sfntLen = reconstructedSfnt.size();
        fromWoff2 = true;
    } else if (size >= 44 && std::memcmp(data, "wOFF", 4) == 0) {
        if (!DetailFont::ReconstructSfntFromWoff(data, size, reconstructedSfnt)) {
            return false;
        }
        sfntPtr = reconstructedSfnt.data();
        sfntLen = reconstructedSfnt.size();
        fromWoff = true;
    }

    DetailFont::ParsedSfnt sfnt;
    if (!DetailFont::ParseSfntDirectory(sfntPtr, sfntLen, sfnt)) {
        return false;
    }

    const uint32_t W = 640, H = 400;
    outInfo.width = W;
    outInfo.height = H;
    outInfo.origWidth = W;
    outInfo.origHeight = H;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;

    std::wstring dispFamily = sfnt.familyName.empty() ? L"Unnamed Font" : sfnt.familyName;
    std::wstring dispSub    = sfnt.subfamilyName.empty() ? L"Regular" : sfnt.subfamilyName;
    std::wstring kindTag    = fromWoff2 ? L"WOFF2 WebFont" :
                              (fromWoff ? L"WOFF WebFont" :
                              (sfnt.flavor == DetailFont::MakeSfntTag("OTTO") ? L"OpenType CFF" : L"TrueType"));
    outInfo.formatName = dispFamily + L" " + dispSub + L" — " + kindTag +
                         (sfnt.numGlyphs > 0 ? (L" (" + std::to_wstring(sfnt.numGlyphs) + L" glyphs)") : L"");

    // Initialize warm parchment-white specimen background + top dark header bar
    outInfo.bgraPixels.resize(static_cast<size_t>(W) * H * 4);
    for (uint32_t y = 0; y < H; ++y) {
        bool inBanner = (y < 42);
        for (uint32_t x = 0; x < W; ++x) {
            uint8_t r = inBanner ? 26 : 250;
            uint8_t g = inBanner ? 34 : 250;
            uint8_t b = inBanner ? 48 : 247;
            WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(y) * W + x) * 4], r, g, b, 255);
        }
    }

    // Prefer native color / CFF2 renderer first when font carries COLR/CPAL, CBDT, sbix, or CFF2 tables
    // (Win32 GDI DrawTextW is monochrome and does not render COLR/CBDT/sbix colors or CFF2 variable outlines!)
    {
        size_t dirOff = (sfntLen >= 16 && std::memcmp(sfntPtr, "ttcf", 4) == 0) ? DetailImg100::RdU32BE(sfntPtr + 12) : 0u;
        bool hasColorOrCff2 = false;
        if (dirOff + 12u <= sfntLen) {
            uint16_t nt = DetailImg100::RdU16BE(sfntPtr + dirOff + 4);
            for (uint16_t i = 0; i < nt && dirOff + 12u + static_cast<size_t>(i + 1) * 16u <= sfntLen; ++i) {
                uint32_t t = DetailImg100::RdU32BE(sfntPtr + dirOff + 12u + static_cast<size_t>(i) * 16u);
                if (t == DetailFont::MakeSfntTag("COLR") || t == DetailFont::MakeSfntTag("CBDT") ||
                    t == DetailFont::MakeSfntTag("sbix") || t == DetailFont::MakeSfntTag("CFF2")) {
                    hasColorOrCff2 = true;
                    break;
                }
            }
        }
        if (hasColorOrCff2 && DetailArtFont::RenderCffOrColorFontSpecimen(sfntPtr, sfntLen, sfnt, outInfo)) {
            outInfo.valid = true;
            return true;
        }
    }

    // Attempt Win32 GDI rendering via AddFontMemResourceEx + DIBSection
    bool gdiRenderedRealFont = false;
    if (!sfnt.familyName.empty()) {
        DWORD numAdded = 0;
        HANDLE hFontRes = AddFontMemResourceEx(
            const_cast<uint8_t*>(sfntPtr),
            static_cast<DWORD>(sfntLen),
            nullptr,
            &numAdded);

        if (hFontRes && numAdded > 0) {
            HDC hdc = CreateCompatibleDC(nullptr);
            if (hdc) {
                BITMAPINFO bmi = {};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = static_cast<LONG>(W);
                bmi.bmiHeader.biHeight = -static_cast<LONG>(H); // top-down DIB
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;

                void* dibBits = nullptr;
                HBITMAP hDib = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
                if (hDib && dibBits) {
                    std::memcpy(dibBits, outInfo.bgraPixels.data(), outInfo.bgraPixels.size());
                    HGDIOBJ oldBmp = SelectObject(hdc, hDib);
                    SetBkMode(hdc, TRANSPARENT);

                    bool isItalic = (dispSub.find(L"Italic") != std::wstring::npos ||
                                     dispSub.find(L"Oblique") != std::wstring::npos);
                    int weight = (dispSub.find(L"Bold") != std::wstring::npos) ? FW_BOLD : FW_NORMAL;

                    auto makeFont = [&](int pxHeight) -> HFONT {
                        return CreateFontW(
                            -pxHeight, 0, 0, 0, weight, isItalic ? TRUE : FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                            sfnt.familyName.c_str());
                    };

                    HFONT hTestFont = makeFont(28);
                    if (hTestFont) {
                        HGDIOBJ oldFnt = SelectObject(hdc, hTestFont);
                        wchar_t actualFace[128] = {};
                        GetTextFaceW(hdc, 128, actualFace);
                        // Confirm GDI loaded our font face rather than substituting system Arial/MS Sans Serif
                        if (_wcsicmp(actualFace, sfnt.familyName.c_str()) == 0) {
                            gdiRenderedRealFont = true;
                        }
                        SelectObject(hdc, oldFnt);
                        DeleteObject(hTestFont);
                    }

                    if (gdiRenderedRealFont) {
                        auto drawLineGdi = [&](int x, int y, int pxSize, COLORREF col, const std::wstring& txt) {
                            HFONT hf = makeFont(pxSize);
                            if (!hf) return;
                            HGDIOBJ prev = SelectObject(hdc, hf);
                            SetTextColor(hdc, col);
                            RECT rc = { x, y, static_cast<LONG>(W) - 12, static_cast<LONG>(H) - 4 };
                            DrawTextW(hdc, txt.c_str(), static_cast<int>(txt.size()), &rc, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE);
                            SelectObject(hdc, prev);
                            DeleteObject(hf);
                        };

                        std::wstring headerTitle = dispFamily + L"  (" + dispSub + L")";
                        drawLineGdi(14, 8,  22, RGB(245, 248, 255), headerTitle);
                        drawLineGdi(16, 50, 30, RGB(20, 26, 36),    L"ABCDEFGHIJKLM   NOPQRSTUVWXYZ");
                        drawLineGdi(16, 88, 28, RGB(20, 26, 36),    L"abcdefghijklm   nopqrstuvwxyz");
                        drawLineGdi(16, 124, 24, RGB(45, 78, 138),  L"0123456789   !@#$%^&*()_+-=[]{};':\",./<>?");
                        drawLineGdi(16, 162, 16, RGB(55, 62, 74),   L"12pt: The quick brown fox jumps over the lazy dog. 0123456789");
                        drawLineGdi(16, 186, 22, RGB(35, 42, 54),   L"18pt: The quick brown fox jumps over the lazy dog.");
                        drawLineGdi(16, 218, 30, RGB(22, 28, 38),   L"26pt: Pack my box with five dozen liquor jugs.");
                        drawLineGdi(16, 260, 42, RGB(15, 20, 30),   L"36pt: Sphinx of black quartz!");
                        drawLineGdi(16, 318, 56, RGB(28, 56, 108),  L"Aa Bb Cc Gg Qq Rr & 0123");

                        GdiFlush();
                        std::memcpy(outInfo.bgraPixels.data(), dibBits, outInfo.bgraPixels.size());
                        // Restore opaque alpha = 255 (Win32 GDI clears alpha channel on touched text pixels)
                        for (size_t i = 0; i < static_cast<size_t>(W) * H; ++i) {
                            outInfo.bgraPixels[i * 4 + 3] = 255;
                        }
                    }

                    SelectObject(hdc, oldBmp);
                    DeleteObject(hDib);
                }
                DeleteDC(hdc);
            }
            RemoveFontMemResourceEx(hFontRes);
        }
    }

    // Pure-C++ TrueType 'glyf' / OpenType 'CFF ' / 'CFF2' / Color font rasterizer fallback
    if (!gdiRenderedRealFont) {
        if (!sfnt.tblGlyf.valid || !sfnt.tblLoca.valid || sfnt.numGlyphs == 0) {
            if (DetailArtFont::RenderCffOrColorFontSpecimen(sfntPtr, sfntLen, sfnt, outInfo)) {
                outInfo.valid = true;
                return true;
            }
            return false; // Neither GDI accepted the font nor TrueType/CFF/Color glyphs exist
        }

        std::string asciiHeader;
        for (wchar_t wc : dispFamily) {
            if (wc >= 32 && wc < 127) asciiHeader.push_back(static_cast<char>(wc));
        }
        if (asciiHeader.empty()) asciiHeader = "TRUETYPE SPECIMEN";
        DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, W, H, 14, 13, asciiHeader, 2, 240, 246, 255);

        size_t totalDrawn = 0;
        totalDrawn += DetailFont::RasterizeTextLineGlyf(sfnt, L"ABCDEFGHIJKLM",          18.0f, 82.0f,  34.0f, 20, 26, 36,  outInfo.bgraPixels, W, H);
        totalDrawn += DetailFont::RasterizeTextLineGlyf(sfnt, L"NOPQRSTUVWXYZ",          18.0f, 122.0f, 34.0f, 20, 26, 36,  outInfo.bgraPixels, W, H);
        totalDrawn += DetailFont::RasterizeTextLineGlyf(sfnt, L"abcdefghijklm",          18.0f, 160.0f, 30.0f, 28, 36, 48,  outInfo.bgraPixels, W, H);
        totalDrawn += DetailFont::RasterizeTextLineGlyf(sfnt, L"nopqrstuvwxyz",          18.0f, 196.0f, 30.0f, 28, 36, 48,  outInfo.bgraPixels, W, H);
        totalDrawn += DetailFont::RasterizeTextLineGlyf(sfnt, L"0123456789 !@#$%^&*()",  18.0f, 232.0f, 28.0f, 45, 78, 138, outInfo.bgraPixels, W, H);
        totalDrawn += DetailFont::RasterizeTextLineGlyf(sfnt, L"The quick brown fox jumps over the lazy dog.", 18.0f, 266.0f, 22.0f, 55, 62, 74, outInfo.bgraPixels, W, H);
        totalDrawn += DetailFont::RasterizeTextLineGlyf(sfnt, L"Aa Bb Cc 123",           18.0f, 322.0f, 46.0f, 24, 52, 102, outInfo.bgraPixels, W, H);
        totalDrawn += DetailFont::RasterizeTextLineGlyf(sfnt, L"WATERFALL 64",           290.0f, 380.0f, 60.0f, 18, 24, 34, outInfo.bgraPixels, W, H);

        // If cmap didn't map standard ASCII (e.g. symbol/icon font or minimal glyph table), rasterize available glyph IDs directly!
        if (totalDrawn == 0) {
            float curX = 18.0f;
            float curY = 110.0f;
            for (uint16_t gid = 0; gid < sfnt.numGlyphs && curY < static_cast<float>(H) - 20.0f; ++gid) {
                std::vector<DetailFont::EdgeSeg> testEdges;
                int16_t xmn = 0, xmx = 0;
                if (!DetailFont::ExtractGlyphEdges(sfnt, gid, testEdges, xmn, xmx, 0) || testEdges.empty()) continue;
                float adv = 0.0f;
                if (DetailFont::RasterizeGlyphContours(sfnt, gid, curX, curY, 54.0f, 22, 30, 44, outInfo.bgraPixels, W, H, adv)) {
                    ++totalDrawn;
                    curX += std::max(36.0f, adv + 10.0f);
                    if (curX > static_cast<float>(W) - 60.0f) {
                        curX = 18.0f;
                        curY += 72.0f;
                    }
                }
            }
        }

        if (totalDrawn == 0) {
            return false; // Zero renderable glyphs in font!
        }
    }

    outInfo.valid = true;
    return true;
}

} // namespace Formats
