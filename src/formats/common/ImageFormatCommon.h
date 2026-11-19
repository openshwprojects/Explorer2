#pragma once
// ImageFormatCommon.h — Shared non-format codecs, bit-readers, color/RAW/GPU/ISOBMFF helpers
// Included inside `namespace Explorer2` in `ImageFormatLoader.cpp` before `formats/*.h`.

namespace Formats {

namespace DetailImg100 {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdI16BE;
using ::Explorer2::ByteIO::RdU24LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU64BE;
using ::Explorer2::ByteIO::HalfToFloat;

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
#if !defined(_WIN32) && !defined(E2_WIN32_HAL)
    return false;   // the Linux build of the readers has no Windows Imaging Component (WIC)
#else
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
#endif
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

namespace DetailBlpJpeg {

struct RawJpegDecoded {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t components = 0;
    bool hasJfif = false;
    bool hasAdobe = false;
    uint8_t adobeTransform = 0;
    std::vector<uint8_t> pixels; // Interleaved [Y][X][components] raw IDCT+128 samples (before color transform)
};

inline bool DecodeBaselineJpegRawChannels(const uint8_t* data, size_t size, RawJpegDecoded& out) {
    out = RawJpegDecoded{};
    if (!data || size < 16 || data[0] != 0xFFu || data[1] != 0xD8u) return false;

    static constexpr uint8_t kZigZag8[64] = {
         0,  1,  8, 16,  9,  2,  3, 10,
        17, 24, 32, 25, 18, 11,  4,  5,
        12, 19, 26, 33, 40, 48, 41, 34,
        27, 20, 13,  6,  7, 14, 21, 28,
        35, 42, 49, 56, 57, 50, 43, 36,
        29, 22, 15, 23, 30, 37, 44, 51,
        58, 59, 52, 45, 38, 31, 39, 46,
        53, 60, 61, 54, 47, 55, 62, 63
    };

    uint16_t qt[4][64] = {};
    bool qtValid[4] = {};

    struct HuffTbl {
        bool valid = false;
        int32_t minCode[17] = {};
        int32_t maxCode[17] = {};
        int32_t valPtr[17]  = {};
        uint8_t syms[256]   = {};
    };
    HuffTbl dcHuff[4], acHuff[4];

    auto buildHuff = [](HuffTbl& ht, const uint8_t counts[16], const uint8_t* syms, uint32_t totalSyms) {
        ht = HuffTbl{};
        std::memcpy(ht.syms, syms, totalSyms);
        int32_t code = 0, symIdx = 0;
        for (int len = 1; len <= 16; ++len) {
            if (counts[len - 1] == 0) {
                ht.maxCode[len] = -1;
            } else {
                ht.valPtr[len]  = symIdx;
                ht.minCode[len] = code;
                code += static_cast<int32_t>(counts[len - 1]) - 1;
                ht.maxCode[len] = code;
                symIdx += counts[len - 1];
                ++code;
            }
            code <<= 1;
        }
        ht.valid = true;
    };

    // Preload ITU-T T.81 Annex K standard default Huffman tables in case DHT is omitted
    {
        static constexpr uint8_t kDcLumBits[16] = { 0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0 };
        static constexpr uint8_t kDcLumVal[12]  = { 0,1,2,3,4,5,6,7,8,9,10,11 };
        static constexpr uint8_t kDcChrBits[16] = { 0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0 };
        static constexpr uint8_t kDcChrVal[12]  = { 0,1,2,3,4,5,6,7,8,9,10,11 };
        static constexpr uint8_t kAcLumBits[16] = { 0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,125 };
        static constexpr uint8_t kAcLumVal[162] = {
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
        static constexpr uint8_t kAcChrBits[16] = { 0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,119 };
        static constexpr uint8_t kAcChrVal[162] = {
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
        buildHuff(dcHuff[0], kDcLumBits, kDcLumVal, 12);
        buildHuff(dcHuff[1], kDcChrBits, kDcChrVal, 12);
        buildHuff(acHuff[0], kAcLumBits, kAcLumVal, 162);
        buildHuff(acHuff[1], kAcChrBits, kAcChrVal, 162);
    }

    struct CompSpec {
        uint8_t id = 0, hSamp = 1, vSamp = 1, tq = 0, td = 0, ta = 0;
    };
    CompSpec comps[4] = {};
    bool hasSof = false;
    uint16_t restartInterval = 0;
    size_t scanPos = 0;

    size_t p = 2;
    while (p + 2 <= size) {
        if (data[p] != 0xFFu) { ++p; continue; }
        while (p < size && data[p] == 0xFFu) ++p;
        if (p >= size) break;
        uint8_t marker = data[p++];
        if (marker == 0xD8u || marker == 0x00u || (marker >= 0xD0u && marker <= 0xD7u)) continue;
        if (marker == 0xD9u) break;
        if (p + 2 > size) return false;
        uint16_t segLen = DetailImg100::RdU16BE(data + p);
        if (segLen < 2u || p + segLen > size) return false;
        const uint8_t* seg = data + p + 2u;
        size_t payLen = segLen - 2u;

        if (marker == 0xE0u && payLen >= 5u && std::memcmp(seg, "JFIF\0", 5) == 0) {
            out.hasJfif = true;
        } else if (marker == 0xEEu && payLen >= 12u && std::memcmp(seg, "Adobe", 5) == 0) {
            out.hasAdobe = true;
            out.adobeTransform = seg[11];
        } else if (marker == 0xDBu) { // DQT
            size_t q = 0;
            while (q < payLen) {
                uint8_t pqTq = seg[q++];
                uint8_t pq = (pqTq >> 4u) & 0x0Fu;
                uint8_t tq = pqTq & 0x0Fu;
                if (tq >= 4u || pq > 1u) return false;
                size_t need = (pq == 0u) ? 64u : 128u;
                if (q + need > payLen) return false;
                for (int i = 0; i < 64; ++i) {
                    qt[tq][kZigZag8[i]] = (pq == 0u) ? seg[q + i] : DetailImg100::RdU16BE(seg + q + i * 2);
                }
                qtValid[tq] = true;
                q += need;
            }
        } else if (marker == 0xC4u) { // DHT
            size_t q = 0;
            while (q + 17u <= payLen) {
                uint8_t tcTh = seg[q++];
                uint8_t tc = (tcTh >> 4u) & 0x0Fu;
                uint8_t th = tcTh & 0x0Fu;
                if (tc > 1u || th >= 4u) return false;
                uint32_t totalSyms = 0;
                uint8_t counts[16];
                for (int i = 0; i < 16; ++i) { counts[i] = seg[q + i]; totalSyms += counts[i]; }
                q += 16u;
                if (totalSyms > 256u || q + totalSyms > payLen) return false;
                buildHuff((tc == 0u) ? dcHuff[th] : acHuff[th], counts, seg + q, totalSyms);
                q += totalSyms;
            }
        } else if (marker == 0xDDu && payLen >= 2u) { // DRI
            restartInterval = DetailImg100::RdU16BE(seg);
        } else if (marker == 0xC0u || marker == 0xC1u) { // SOF0 / SOF1
            if (payLen < 6u || seg[0] != 8u) return false;
            uint16_t h = DetailImg100::RdU16BE(seg + 1);
            uint16_t w = DetailImg100::RdU16BE(seg + 3);
            uint8_t nf = seg[5];
            if (w == 0 || h == 0 || w > 16384 || h > 16384 || (nf != 1u && nf != 3u && nf != 4u)) return false;
            if (payLen < 6u + static_cast<size_t>(nf) * 3u) return false;
            out.width = w;
            out.height = h;
            out.components = nf;
            for (uint8_t c = 0; c < nf; ++c) {
                comps[c].id    = seg[6u + c * 3u + 0u];
                uint8_t hv     = seg[6u + c * 3u + 1u];
                comps[c].hSamp = static_cast<uint8_t>((hv >> 4u) & 0x0Fu);
                comps[c].vSamp = static_cast<uint8_t>(hv & 0x0Fu);
                comps[c].tq    = seg[6u + c * 3u + 2u];
                if (comps[c].hSamp == 0u || comps[c].hSamp > 4u || comps[c].vSamp == 0u || comps[c].vSamp > 4u || comps[c].tq >= 4u) return false;
            }
            hasSof = true;
        } else if (marker >= 0xC2u && marker <= 0xCFu && marker != 0xC4u && marker != 0xC8u && marker != 0xCCu) {
            return false; // Progressive/Arithmetic SOF -> let caller fall back to WIC if needed
        } else if (marker == 0xDAu) { // SOS
            if (!hasSof || payLen < 4u) return false;
            uint8_t ns = seg[0];
            if (ns != out.components || payLen < 1u + static_cast<size_t>(ns) * 2u + 3u) return false;
            for (uint8_t s = 0; s < ns; ++s) {
                uint8_t cs = seg[1u + s * 2u + 0u];
                uint8_t tdTa = seg[1u + s * 2u + 1u];
                uint8_t td = (tdTa >> 4u) & 0x0Fu;
                uint8_t ta = tdTa & 0x0Fu;
                if (td >= 4u || ta >= 4u) return false;
                int matchIdx = s;
                for (uint8_t k = 0; k < ns; ++k) { if (comps[k].id == cs) { matchIdx = k; break; } }
                comps[matchIdx].td = td;
                comps[matchIdx].ta = ta;
            }
            scanPos = p + segLen;
            break;
        }
        p += segLen;
    }

    if (!hasSof || scanPos == 0 || scanPos >= size) return false;
    for (uint32_t c = 0; c < out.components; ++c) {
        if (!qtValid[comps[c].tq] || !dcHuff[comps[c].td].valid || !acHuff[comps[c].ta].valid) return false;
    }

    uint8_t maxH = 1, maxV = 1;
    for (uint32_t c = 0; c < out.components; ++c) {
        maxH = std::max(maxH, comps[c].hSamp);
        maxV = std::max(maxV, comps[c].vSamp);
    }
    const uint32_t mcuW = maxH * 8u;
    const uint32_t mcuH = maxV * 8u;
    const uint32_t mcuCols = (out.width  + mcuW - 1u) / mcuW;
    const uint32_t mcuRows = (out.height + mcuH - 1u) / mcuH;

    std::vector<uint8_t> planes[4];
    uint32_t planeStride[4] = {};
    for (uint32_t c = 0; c < out.components; ++c) {
        planeStride[c] = mcuCols * comps[c].hSamp * 8u;
        planes[c].assign(static_cast<size_t>(planeStride[c]) * (mcuRows * comps[c].vSamp * 8u), 128u);
    }

    uint32_t bitBuf = 0;
    int bitCnt = 0;
    size_t sp = scanPos;
    bool hitMarker = false;

    auto refill = [&]() {
        while (bitCnt <= 24) {
            if (hitMarker || sp >= size) {
                bitBuf <<= 8u;
                bitCnt += 8;
                continue;
            }
            uint8_t b = data[sp++];
            if (b == 0xFFu) {
                while (sp < size && data[sp] == 0xFFu) ++sp;
                if (sp >= size) { hitMarker = true; continue; }
                uint8_t nx = data[sp++];
                if (nx != 0x00u) {
                    sp -= 2u; // Rewind onto 0xFF marker
                    hitMarker = true;
                    continue;
                }
            }
            bitBuf = (bitBuf << 8u) | b;
            bitCnt += 8;
        }
    };

    auto getBits = [&](int n) -> int32_t {
        if (n <= 0) return 0;
        if (bitCnt < n) refill();
        bitCnt -= n;
        return static_cast<int32_t>((bitBuf >> bitCnt) & ((1u << n) - 1u));
    };

    auto decodeHuff = [&](const HuffTbl& ht) -> int {
        int32_t code = 0;
        for (int len = 1; len <= 16; ++len) {
            if (bitCnt < 1) refill();
            --bitCnt;
            code = (code << 1) | static_cast<int32_t>((bitBuf >> bitCnt) & 1u);
            if (code <= ht.maxCode[len]) {
                int idx = ht.valPtr[len] + (code - ht.minCode[len]);
                return (idx >= 0 && idx < 256) ? ht.syms[idx] : -1;
            }
        }
        return -1;
    };

    auto extendDiff = [&](int ssss) -> int32_t {
        if (ssss == 0) return 0;
        int32_t v = getBits(ssss);
        if ((v & (1 << (ssss - 1))) == 0) v -= (1 << ssss) - 1;
        return v;
    };

    auto idct8x8 = [](const int32_t blk[64], uint8_t* dst, uint32_t stride) {
        static constexpr float kC1 = 0.98078528f, kS1 = 0.19509032f;
        static constexpr float kC2 = 0.92387953f, kS2 = 0.38268343f;
        static constexpr float kC3 = 0.83146961f, kS3 = 0.55557023f;
        static constexpr float kInvSqrt2 = 0.70710678f;

        float tmp[64];
        for (int r = 0; r < 8; ++r) {
            const int32_t* s = &blk[r * 8];
            float p0 = static_cast<float>(s[0]) * kInvSqrt2;
            float p4 = static_cast<float>(s[4]) * kInvSqrt2;
            float e0 = p0 + p4, e1 = p0 - p4;
            float e2 = static_cast<float>(s[2]) * kS2 - static_cast<float>(s[6]) * kC2;
            float e3 = static_cast<float>(s[2]) * kC2 + static_cast<float>(s[6]) * kS2;
            float a0 = e0 + e3, a1 = e1 + e2, a2 = e1 - e2, a3 = e0 - e3;

            float o0 = static_cast<float>(s[1]) * kC1 + static_cast<float>(s[7]) * kS1;
            float o3 = static_cast<float>(s[1]) * kS1 - static_cast<float>(s[7]) * kC1;
            float o1 = static_cast<float>(s[5]) * kS3 + static_cast<float>(s[3]) * kC3;
            float o2 = static_cast<float>(s[5]) * kC3 - static_cast<float>(s[3]) * kS3;
            float b0 = o0 + o1, b1 = o0 - o1, b2 = o3 + o2, b3 = o3 - o2;
            float d1 = (b3 - b1) * kInvSqrt2;
            float d2 = (b3 + b1) * kInvSqrt2;

            float* t = &tmp[r * 8];
            t[0] = a0 + b0; t[7] = a0 - b0;
            t[1] = a1 + d2; t[6] = a1 - d2;
            t[2] = a2 + d1; t[5] = a2 - d1;
            t[3] = a3 - b2; t[4] = a3 + b2;
        }
        for (int c = 0; c < 8; ++c) {
            float p0 = tmp[0 * 8 + c] * kInvSqrt2;
            float p4 = tmp[4 * 8 + c] * kInvSqrt2;
            float e0 = p0 + p4, e1 = p0 - p4;
            float e2 = tmp[2 * 8 + c] * kS2 - tmp[6 * 8 + c] * kC2;
            float e3 = tmp[2 * 8 + c] * kC2 + tmp[6 * 8 + c] * kS2;
            float a0 = e0 + e3, a1 = e1 + e2, a2 = e1 - e2, a3 = e0 - e3;

            float o0 = tmp[1 * 8 + c] * kC1 + tmp[7 * 8 + c] * kS1;
            float o3 = tmp[1 * 8 + c] * kS1 - tmp[7 * 8 + c] * kC1;
            float o1 = tmp[5 * 8 + c] * kS3 + tmp[3 * 8 + c] * kC3;
            float o2 = tmp[5 * 8 + c] * kC3 - tmp[3 * 8 + c] * kS3;
            float b0 = o0 + o1, b1 = o0 - o1, b2 = o3 + o2, b3 = o3 - o2;
            float d1 = (b3 - b1) * kInvSqrt2;
            float d2 = (b3 + b1) * kInvSqrt2;

            const float outCol[8] = {
                a0 + b0, a1 + d2, a2 + d1, a3 - b2,
                a3 + b2, a2 - d1, a1 - d2, a0 - b0
            };
            for (int r = 0; r < 8; ++r) {
                int v = static_cast<int>(std::lround(outCol[r] * 0.25f)) + 128;
                dst[r * stride + c] = static_cast<uint8_t>(std::clamp(v, 0, 255));
            }
        }
    };

    int32_t dcPred[4] = {};
    uint32_t mcusSinceRestart = 0;
    for (uint32_t my = 0; my < mcuRows; ++my) {
        for (uint32_t mx = 0; mx < mcuCols; ++mx) {
            if (restartInterval > 0u && mcusSinceRestart == restartInterval) {
                bitBuf = 0; bitCnt = 0; hitMarker = false;
                while (sp + 1u < size && !(data[sp] == 0xFFu && data[sp + 1u] >= 0xD0u && data[sp + 1u] <= 0xD7u)) {
                    if (data[sp] == 0xFFu && data[sp + 1u] == 0xD9u) break;
                    ++sp;
                }
                if (sp + 1u < size && data[sp] == 0xFFu && data[sp + 1u] >= 0xD0u && data[sp + 1u] <= 0xD7u) sp += 2u;
                std::memset(dcPred, 0, sizeof(dcPred));
                mcusSinceRestart = 0;
            }
            for (uint32_t c = 0; c < out.components; ++c) {
                const uint16_t* qtbl = qt[comps[c].tq];
                for (uint8_t vy = 0; vy < comps[c].vSamp; ++vy) {
                    for (uint8_t hx = 0; hx < comps[c].hSamp; ++hx) {
                        int32_t blk[64] = {};
                        int dcSym = decodeHuff(dcHuff[comps[c].td]);
                        if (dcSym < 0 || dcSym > 11) return false;
                        dcPred[c] += extendDiff(dcSym);
                        blk[0] = dcPred[c] * static_cast<int32_t>(qtbl[0]);
                        int k = 1;
                        while (k < 64) {
                            int rs = decodeHuff(acHuff[comps[c].ta]);
                            if (rs < 0) return false;
                            if (rs == 0x00) break; // EOB
                            if (rs == 0xF0) { k += 16; continue; } // ZRL
                            k += (rs >> 4) & 0x0F;
                            int ssss = rs & 0x0F;
                            if (k >= 64 || ssss == 0 || ssss > 10) return false;
                            int pos = kZigZag8[k];
                            blk[pos] = extendDiff(ssss) * static_cast<int32_t>(qtbl[pos]);
                            ++k;
                        }
                        uint32_t bx = (mx * comps[c].hSamp + hx) * 8u;
                        uint32_t by = (my * comps[c].vSamp + vy) * 8u;
                        idct8x8(blk, &planes[c][static_cast<size_t>(by) * planeStride[c] + bx], planeStride[c]);
                    }
                }
            }
            ++mcusSinceRestart;
        }
    }

    out.pixels.resize(static_cast<size_t>(out.width) * out.height * out.components);
    for (uint32_t y = 0; y < out.height; ++y) {
        for (uint32_t x = 0; x < out.width; ++x) {
            uint8_t* dst = &out.pixels[(static_cast<size_t>(y) * out.width + x) * out.components];
            for (uint32_t c = 0; c < out.components; ++c) {
                uint32_t sx = (x * comps[c].hSamp) / maxH;
                uint32_t sy = (y * comps[c].vSamp) / maxV;
                dst[c] = planes[c][static_cast<size_t>(sy) * planeStride[c] + sx];
            }
        }
    }
    return true;
}

} // namespace DetailBlpJpeg

namespace DetailDpxCin {

    struct DpxElemSpec {
        uint32_t dataSign = 0;
        uint8_t descriptor = 50; // 50 = RGB
        uint8_t transfer = 2;    // 1 = Printing Density (Cineon Log), 2 = Linear
        uint8_t bitSize = 10;
        uint16_t packing = 1;    // 0 = Packed, 1 = Method A, 2 = Method B
        uint16_t encoding = 0;   // 0 = None, 1 = RLE
        uint32_t elemOff = 0;
    };

inline bool RenderDpxOrCineonElements(
    const uint8_t* data, size_t size,
    bool isLittle, bool isCin,
    uint32_t w, uint32_t h, uint16_t orientation,
    const std::vector<DpxElemSpec>& elems,
    ExtendedImageInfo& outInfo)
{
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

} // namespace DetailDpxCin

namespace DetailArtFont {

inline std::string_view TrimAsciiView(std::string_view sv) { return ::Explorer2::PathUtils::TrimAscii(sv); }

inline std::string GetXmlAttr(std::string_view tag, std::string_view attrName) {
    std::string_view v;
    return ::Explorer2::XmlTagScan::FindAttr(tag, attrName, v, false) ? std::string(v) : std::string();
}

inline int ParseIntOr(const std::string& s, int defVal) {
    if (s.empty()) return defVal;
    char* endP = nullptr;
    long v = std::strtol(s.c_str(), &endP, 10);
    return (endP != s.c_str()) ? static_cast<int>(v) : defVal;
}

inline float ParseFloatOr(const std::string& s, float defVal) {
    if (s.empty()) return defVal;
    char* endP = nullptr;
    float v = std::strtof(s.c_str(), &endP);
    return (endP != s.c_str() && std::isfinite(v)) ? v : defVal;
}

enum class LayerBlendOp { SrcOver, Multiply, Screen, Plus, Lighten, Darken };

inline LayerBlendOp ParseBlendOp(std::string_view op) {
    if (op.find("multiply") != std::string_view::npos) return LayerBlendOp::Multiply;
    if (op.find("screen") != std::string_view::npos)   return LayerBlendOp::Screen;
    if (op.find("plus") != std::string_view::npos || op.find("add") != std::string_view::npos) return LayerBlendOp::Plus;
    if (op.find("lighten") != std::string_view::npos)  return LayerBlendOp::Lighten;
    if (op.find("darken") != std::string_view::npos)   return LayerBlendOp::Darken;
    return LayerBlendOp::SrcOver;
}

// Blend straight or premultiplied BGRA source pixel onto premultiplied BGRA canvas
inline void BlendPremulBgraPixel(
    uint8_t* dstBgra,
    uint8_t srcB_pm, uint8_t srcG_pm, uint8_t srcR_pm, uint8_t srcA,
    float opacity,
    LayerBlendOp op)
{
    if (srcA == 0 || opacity <= 0.0f) return;
    float sa = (static_cast<float>(srcA) / 255.0f) * std::clamp(opacity, 0.0f, 1.0f);
    if (sa <= 0.0f) return;

    float scaleSrc = std::clamp(opacity, 0.0f, 1.0f);
    float sr_pm = (static_cast<float>(srcR_pm) / 255.0f) * scaleSrc;
    float sg_pm = (static_cast<float>(srcG_pm) / 255.0f) * scaleSrc;
    float sb_pm = (static_cast<float>(srcB_pm) / 255.0f) * scaleSrc;

    float da = static_cast<float>(dstBgra[3]) / 255.0f;
    float dr_pm = static_cast<float>(dstBgra[2]) / 255.0f;
    float dg_pm = static_cast<float>(dstBgra[1]) / 255.0f;
    float db_pm = static_cast<float>(dstBgra[0]) / 255.0f;

    float outA = sa + da * (1.0f - sa);
    float outR_pm = 0.0f, outG_pm = 0.0f, outB_pm = 0.0f;

    if (op == LayerBlendOp::SrcOver || da <= 1e-5f) {
        outR_pm = sr_pm + dr_pm * (1.0f - sa);
        outG_pm = sg_pm + dg_pm * (1.0f - sa);
        outB_pm = sb_pm + db_pm * (1.0f - sa);
    } else if (op == LayerBlendOp::Plus) {
        outR_pm = std::min(1.0f, sr_pm + dr_pm);
        outG_pm = std::min(1.0f, sg_pm + dg_pm);
        outB_pm = std::min(1.0f, sb_pm + db_pm);
        outA = std::min(1.0f, sa + da);
    } else {
        float sr = std::clamp(sr_pm / sa, 0.0f, 1.0f);
        float sg = std::clamp(sg_pm / sa, 0.0f, 1.0f);
        float sb = std::clamp(sb_pm / sa, 0.0f, 1.0f);
        float dr = std::clamp(dr_pm / da, 0.0f, 1.0f);
        float dg = std::clamp(dg_pm / da, 0.0f, 1.0f);
        float db = std::clamp(db_pm / da, 0.0f, 1.0f);
        auto blendCh = [&](float s, float d) -> float {
            switch (op) {
                case LayerBlendOp::Multiply: return s * d;
                case LayerBlendOp::Screen:   return s + d - s * d;
                case LayerBlendOp::Lighten:  return std::max(s, d);
                case LayerBlendOp::Darken:   return std::min(s, d);
                default: return s;
            }
        };
        outR_pm = sr_pm * (1.0f - da) + dr_pm * (1.0f - sa) + sa * da * blendCh(sr, dr);
        outG_pm = sg_pm * (1.0f - da) + dg_pm * (1.0f - sa) + sa * da * blendCh(sg, dg);
        outB_pm = sb_pm * (1.0f - da) + db_pm * (1.0f - sa) + sa * da * blendCh(sb, db);
    }

    dstBgra[0] = static_cast<uint8_t>(std::clamp(outB_pm * 255.0f + 0.5f, 0.0f, 255.0f));
    dstBgra[1] = static_cast<uint8_t>(std::clamp(outG_pm * 255.0f + 0.5f, 0.0f, 255.0f));
    dstBgra[2] = static_cast<uint8_t>(std::clamp(outR_pm * 255.0f + 0.5f, 0.0f, 255.0f));
    dstBgra[3] = static_cast<uint8_t>(std::clamp(outA    * 255.0f + 0.5f, 0.0f, 255.0f));
}

inline const ZipDeflate::ArchiveItemMeta* FindZipEntryCaseInsensitive(
    const std::vector<ZipDeflate::ArchiveItemMeta>& entries,
    std::wstring relPath)
{
    for (wchar_t& ch : relPath) if (ch == L'\\') ch = L'/';
    while (!relPath.empty() && relPath.front() == L'/') relPath.erase(relPath.begin());
    std::wstring targetLower = PathUtils::ToLower(relPath);
    for (const auto& e : entries) {
        std::wstring nl = PathUtils::ToLower(e.pathInArchive);
        for (wchar_t& ch : nl) if (ch == L'\\') ch = L'/';
        while (!nl.empty() && nl.front() == L'/') nl.erase(nl.begin());
        if (nl == targetLower) return &e;
    }
    return nullptr;
}

inline bool DecodeZipArtworkFallback(
    const uint8_t* data, size_t size,
    const std::vector<ZipDeflate::ArchiveItemMeta>& entries,
    const std::wstring& label,
    ExtendedImageInfo& outInfo)
{
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

} // namespace DetailArtFont

namespace DetailPalette {

struct Rgb { uint8_t r, g, b; };

inline void ParseTextPaletteLines(std::string_view sv, std::vector<Rgb>& colors) {
        size_t pos = 0;
        while (pos < sv.size() && colors.size() < 256) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view ln = sv.substr(pos, eol - pos);
            pos = eol + 1;
            while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
            if (ln.empty() || ln[0] == '#' || ln.rfind("GIMP", 0) == 0 || ln.rfind("Name:", 0) == 0 ||
                ln.rfind("Columns:", 0) == 0 || ln.rfind("JASC", 0) == 0 || ln == "0100\r" || ln == "0100") {
                continue;
            }
            int r = -1, g = -1, b = -1;
            if (sscanf_s(std::string(ln).c_str(), "%d %d %d", &r, &g, &b) == 3) {
                colors.push_back({
                    static_cast<uint8_t>(std::clamp(r, 0, 255)),
                    static_cast<uint8_t>(std::clamp(g, 0, 255)),
                    static_cast<uint8_t>(std::clamp(b, 0, 255))
                });
            }
        }
}

inline bool RenderPaletteSwatchGrid(const std::vector<Rgb>& colors, ExtendedImageInfo& outInfo) {
    if (colors.empty()) return false;

    const uint32_t cols = 8, cell = 16;
    uint32_t rows = std::max(1u, static_cast<uint32_t>((colors.size() + cols - 1) / cols));
    uint32_t w = cols * cell, h = rows * cell;
    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Color Palette Swatch (" + std::to_wstring(colors.size()) + L" colors)";
    outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4, 255);

    for (size_t i = 0; i < colors.size(); ++i) {
        uint32_t cx = static_cast<uint32_t>(i % cols) * cell;
        uint32_t cy = static_cast<uint32_t>(i / cols) * cell;
        for (uint32_t py = 1; py + 1 < cell; ++py) {
            for (uint32_t px = 1; px + 1 < cell; ++px) {
                uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(cy + py) * w + (cx + px)) * 4];
                dst[0] = colors[i].b; dst[1] = colors[i].g; dst[2] = colors[i].r; dst[3] = 255;
            }
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace DetailPalette

namespace DetailColorLut {

    struct Rgb { float r, g, b; };

inline bool RenderColorLutTestCard(
    const std::vector<Rgb>& table,
    bool use3d, int size3d, bool use1d, int size1d, bool is3dl,
    const float dmin[3], const float dmax[3],
    const std::wstring& name,
    ExtendedImageInfo& outInfo)
{
    auto at3 = [&](int r, int g, int b) -> const Rgb& {
        size_t n = static_cast<size_t>(size3d);
        size_t idx = is3dl ? ((static_cast<size_t>(r) * n + g) * n + b)   // blue fastest
                           : ((static_cast<size_t>(b) * n + g) * n + r);  // red fastest
        return table[idx];
    };
    auto lerp = [](const Rgb& a, const Rgb& b, float t) {
        return Rgb{ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
    };
    // Input colour (0..1) -> output colour, trilinear (3D) or per-channel linear (1D)
    auto apply = [&](float r, float g, float b) -> Rgb {
        float in[3] = { r, g, b };
        float t[3];
        int n = use3d ? size3d : size1d;
        for (int k = 0; k < 3; ++k) {
            float span = (dmax[k] - dmin[k]);
            float u = span > 1e-12f ? (in[k] - dmin[k]) / span : in[k];
            t[k] = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(n - 1);
        }
        if (use1d) {
            Rgb outc{};
            float* dst[3] = { &outc.r, &outc.g, &outc.b };
            for (int k = 0; k < 3; ++k) {
                int i0 = std::min(static_cast<int>(t[k]), n - 2);
                float f = t[k] - static_cast<float>(i0);
                const Rgb& a = table[static_cast<size_t>(i0)];
                const Rgb& bb = table[static_cast<size_t>(i0 + 1)];
                float av = (k == 0) ? a.r : (k == 1) ? a.g : a.b;
                float bv = (k == 0) ? bb.r : (k == 1) ? bb.g : bb.b;
                *dst[k] = av + (bv - av) * f;
            }
            return outc;
        }
        int i[3];
        float f[3];
        for (int k = 0; k < 3; ++k) {
            i[k] = std::min(static_cast<int>(t[k]), n - 2);
            f[k] = t[k] - static_cast<float>(i[k]);
        }
        Rgb c00 = lerp(at3(i[0], i[1], i[2]), at3(i[0] + 1, i[1], i[2]), f[0]);
        Rgb c10 = lerp(at3(i[0], i[1] + 1, i[2]), at3(i[0] + 1, i[1] + 1, i[2]), f[0]);
        Rgb c01 = lerp(at3(i[0], i[1], i[2] + 1), at3(i[0] + 1, i[1], i[2] + 1), f[0]);
        Rgb c11 = lerp(at3(i[0], i[1] + 1, i[2] + 1), at3(i[0] + 1, i[1] + 1, i[2] + 1), f[0]);
        return lerp(lerp(c00, c10, f[1]), lerp(c01, c11, f[1]), f[2]);
    };

    // Before | after: the same test card (hue sweep, falling brightness, grey ramp at the bottom) on both halves
    const uint32_t w = 256, h = 160, half = w / 2;
    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = name;
    outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4, 0);
    for (uint32_t y = 0; y < h; ++y) {
        float v = static_cast<float>(y) / static_cast<float>(h - 1);
        for (uint32_t x = 0; x < w; ++x) {
            float u = static_cast<float>(x % half) / static_cast<float>(half - 1);
            float r, g, b;
            if (v < 0.8f) {
                // HSV with s = 1, value falling from 1 to 0.3 down the card
                float hh = u * 6.0f, val = 1.0f - (v / 0.8f) * 0.7f;
                float xx = 1.0f - std::fabs(std::fmod(hh, 2.0f) - 1.0f);
                float rr = 0, gg = 0, bb = 0;
                if (hh < 1) { rr = 1; gg = xx; } else if (hh < 2) { rr = xx; gg = 1; } else if (hh < 3) { gg = 1; bb = xx; }
                else if (hh < 4) { gg = xx; bb = 1; } else if (hh < 5) { rr = xx; bb = 1; } else { rr = 1; bb = xx; }
                r = rr * val; g = gg * val; b = bb * val;
            } else {
                r = g = b = u; // grey ramp
            }
            if (x >= half) {
                Rgb o = apply(r, g, b);
                r = o.r; g = o.g; b = o.b;
            }
            uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(y) * w + x) * 4];
            dst[0] = static_cast<uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f);
            dst[1] = static_cast<uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f);
            dst[2] = static_cast<uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f);
            dst[3] = 255;
        }
    }
    DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, w, h, 6, 6, "ORIGINAL", 1, 255, 255, 255);
    DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, w, h, half + 6, 6, "LUT APPLIED", 1, 255, 255, 120);
    outInfo.valid = true;
    return true;
}

} // namespace DetailColorLut

inline bool DecodeWindowsWmf(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo);

namespace DetailImg200 {

inline uint16_t Rd16(const uint8_t* p, bool be) { return ::Explorer2::ByteIO::RdU16(p, be); }

inline uint32_t Rd32(const uint8_t* p, bool be) { return ::Explorer2::ByteIO::RdU32(p, be); }

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


// ============================================================================
// 1. DNG / Camera Color Science: ColorMatrix1 (XYZ D50 -> Cam) -> Linear sRGB
// ============================================================================
inline bool BuildCameraToSrgbMatrix(
    const float colorMatrix1[9],
    bool hasCamCal, const float camCal1[9],
    const float wb[3],
    float outCamToSrgb[9])
{
    // Bradford-adapted sRGB (D65) -> CIE XYZ (D50) matrix (Adobe DNG 1.7 Spec)
    static constexpr double kSrgbToXyzD50[9] = {
        0.4360747, 0.3850649, 0.1430804,
        0.2225045, 0.7168786, 0.0606169,
        0.0139322, 0.0971045, 0.7141733
    };

    // cm = (hasCamCal ? camCal1 * colorMatrix1 : colorMatrix1)
    double cm[9];
    if (hasCamCal) {
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                cm[r * 3 + c] = static_cast<double>(camCal1[r * 3 + 0]) * colorMatrix1[0 * 3 + c] +
                                static_cast<double>(camCal1[r * 3 + 1]) * colorMatrix1[1 * 3 + c] +
                                static_cast<double>(camCal1[r * 3 + 2]) * colorMatrix1[2 * 3 + c];
            }
        }
    } else {
        for (int i = 0; i < 9; ++i) cm[i] = colorMatrix1[i];
    }

    // srgbToCam = cm * kSrgbToXyzD50 (maps linear sRGB -> un-white-balanced Camera RGB)
    // Since input to our matrix is ALREADY white-balanced by wb[0..2] (or we normalize each row so sRGB [1,1,1] -> [1,1,1]!),
    // normalizing each row of srgbToCam to sum to 1.0 ensures neutral gray [v, v, v] after WB stays exact neutral [v, v, v]!
    double srgbToCam[9];
    for (int r = 0; r < 3; ++r) {
        double rowSum = 0.0;
        for (int c = 0; c < 3; ++c) {
            srgbToCam[r * 3 + c] = cm[r * 3 + 0] * kSrgbToXyzD50[0 * 3 + c] +
                                   cm[r * 3 + 1] * kSrgbToXyzD50[1 * 3 + c] +
                                   cm[r * 3 + 2] * kSrgbToXyzD50[2 * 3 + c];
            rowSum += srgbToCam[r * 3 + c];
        }
        if (std::abs(rowSum) < 1e-7) return false;
        for (int c = 0; c < 3; ++c) srgbToCam[r * 3 + c] /= rowSum;
    }
    (void)wb;

    double det =
        srgbToCam[0] * (srgbToCam[4] * srgbToCam[8] - srgbToCam[5] * srgbToCam[7]) -
        srgbToCam[1] * (srgbToCam[3] * srgbToCam[8] - srgbToCam[5] * srgbToCam[6]) +
        srgbToCam[2] * (srgbToCam[3] * srgbToCam[7] - srgbToCam[4] * srgbToCam[6]);
    if (std::abs(det) < 1e-7) return false;

    double invDet = 1.0 / det;
    outCamToSrgb[0] = static_cast<float>( (srgbToCam[4] * srgbToCam[8] - srgbToCam[5] * srgbToCam[7]) * invDet);
    outCamToSrgb[1] = static_cast<float>(-(srgbToCam[1] * srgbToCam[8] - srgbToCam[2] * srgbToCam[7]) * invDet);
    outCamToSrgb[2] = static_cast<float>( (srgbToCam[1] * srgbToCam[5] - srgbToCam[2] * srgbToCam[4]) * invDet);
    outCamToSrgb[3] = static_cast<float>(-(srgbToCam[3] * srgbToCam[8] - srgbToCam[5] * srgbToCam[6]) * invDet);
    outCamToSrgb[4] = static_cast<float>( (srgbToCam[0] * srgbToCam[8] - srgbToCam[2] * srgbToCam[6]) * invDet);
    outCamToSrgb[5] = static_cast<float>(-(srgbToCam[0] * srgbToCam[5] - srgbToCam[2] * srgbToCam[3]) * invDet);
    outCamToSrgb[6] = static_cast<float>( (srgbToCam[3] * srgbToCam[7] - srgbToCam[4] * srgbToCam[6]) * invDet);
    outCamToSrgb[7] = static_cast<float>(-(srgbToCam[0] * srgbToCam[7] - srgbToCam[1] * srgbToCam[6]) * invDet);
    outCamToSrgb[8] = static_cast<float>( (srgbToCam[0] * srgbToCam[4] - srgbToCam[1] * srgbToCam[3]) * invDet);
    return true;
}

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

// Vendor-specific Camera RAW MakerNote / sensor unpacker forward declarations
// (implementations live in formats/SonyArw.h, NikonNef.h, PanasonicRw2.h, PentaxPef.h, OlympusOrf.h, CanonCr2.h)
struct NikonMetaParams {
    bool valid = false;
    uint16_t nefComp = 3; // 1=Lossy1, 3=Lossless, 4=Lossy2
    uint16_t vpred[2][2] = { { 512, 512 }, { 512, 512 } };
    std::vector<uint16_t> curveLut;
};

struct PentaxMetaParams {
    bool hasCustomTree = false;
    uint8_t counts[16] = { 0, 2, 3, 1, 1, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0 };
    uint8_t syms[16]   = { 3, 4, 2, 5, 1, 6, 0, 7, 8, 9, 10, 11, 12, 0, 0, 0 };
    uint32_t symCount  = 13;
};

inline bool UnpackSonyArw2Sensor(
    const uint8_t* src, size_t srcLen, uint32_t width, uint32_t height,
    const uint16_t* optCurveLut4096,
    std::vector<uint16_t>& outSamples);

inline void ParseNikonMakerNote(const uint8_t* mn, size_t mnLen, uint16_t bps, NikonMetaParams& out);

inline bool UnpackNikonNefHuffmanSensor(
    const uint8_t* src, size_t srcLen,
    uint32_t width, uint32_t height, uint16_t bps,
    const NikonMetaParams& nm,
    std::vector<uint16_t>& outSamples);

inline bool UnpackPanasonicRw2Sensor(
    const uint8_t* src, size_t srcLen, uint32_t width, uint32_t height,
    std::vector<uint16_t>& outSamples);

inline void ParsePentaxMakerNote(
    const uint8_t* tiffData, size_t tiffSize,
    const uint8_t* mn, size_t mnLen,
    bool parentBe, PentaxMetaParams& out);

inline bool UnpackPentaxPefHuffmanSensor(
    const uint8_t* src, size_t srcLen,
    uint32_t width, uint32_t height, uint16_t bps,
    const PentaxMetaParams& pm,
    std::vector<uint16_t>& outSamples);

inline bool UnpackOlympusOrfSensor(
    const uint8_t* src, size_t srcLen,
    uint32_t width, uint32_t height, uint16_t bps,
    std::vector<uint16_t>& outSamples);

inline void UnsliceCanonCr2Sensor(
    uint32_t width, uint32_t height, uint16_t effSpp,
    const uint16_t cr2Slices[3],
    std::vector<uint16_t>& fullSamples);

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
    PentaxMetaParams pentaxMeta;
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
        bool sawPhotometric = false;
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
                    if (ReadTiffU32Values(data, size, ent, be, tmpU32, 1)) {
                        d.photometric = static_cast<uint16_t>(tmpU32[0]);
                        sawPhotometric = true;
                    }
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
                case 0x927C: { // MakerNote (Nikon NEF / Pentax PEF Huffman & Linearization tables)
                    uint16_t pt = 0; uint32_t pc = 0;
                    const uint8_t* pp = GetTiffTagPayload(data, size, ent, be, pt, pc);
                    if (pp && pc >= 10) {
                        if (pc >= 18) ParseNikonMakerNote(pp, pc, d.bps, nikonMeta);
                        ParsePentaxMakerNote(data, size, pp, pc, be, pentaxMeta);
                    }
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

        if (!sawPhotometric && (magic == 0x4F52 || magic == 0x5352) && d.spp == 1 && d.bps >= 10) {
            d.photometric = 32803; // Olympus ORF sensor IFDs may omit explicit CFA PhotometricInterpretation
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
            ifd.compression != 34316 && ifd.compression != 34826 &&
            ifd.compression != 65535 && ifd.compression != 32768)
        {
            return false;
        }

        const bool isOlympusOrf = (magic == 0x4F52 || magic == 0x5352 || fmtLabel.find(L"Olympus") != std::wstring::npos);
        const uint16_t effSpp = std::clamp<uint16_t>(ifd.spp, 1, 4);
        const bool planar = (ifd.planarConfig == 2 && effSpp > 1);
        const uint16_t blockSpp = planar ? 1 : effSpp;
        uint16_t detectedNominalBps = ifd.bps;

        std::vector<uint16_t> fullSamples(static_cast<size_t>(ifd.width) * ifd.height * effSpp, 0);

        auto decodeBlock = [&](const uint8_t* cPtr, size_t cLen, uint32_t bw, uint32_t bh, std::vector<uint16_t>& blockOut) -> bool {
            if (cLen == 0) return false;
            if (ifd.compression == 65535 && blockSpp == 1) { // Pentax PEF Lossless Huffman (or custom ORF)
                uint16_t effBps = (ifd.bps >= 8 && ifd.bps <= 16) ? ifd.bps : 12u;
                detectedNominalBps = effBps;
                if (isOlympusOrf && UnpackOlympusOrfSensor(cPtr, cLen, bw, bh, effBps, blockOut)) return true;
                return UnpackPentaxPefHuffmanSensor(cPtr, cLen, bw, bh, effBps, pentaxMeta, blockOut);
            }
            if (ifd.compression == 32768 && blockSpp == 1 && isOlympusOrf) {
                uint16_t effBps = (ifd.bps == 14u) ? 14u : 12u;
                detectedNominalBps = effBps;
                return UnpackOlympusOrfSensor(cPtr, cLen, bw, bh, effBps, blockOut);
            }
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
                // Olympus ORF stores 12/14-bit adaptive Golomb-Rice lossless strips tagged as Compression == 1!
                if (isOlympusOrf && blockSpp == 1 && ifd.bps >= 12u) {
                    uint16_t orfBps = (ifd.bps == 14u) ? 14u : 12u;
                    size_t rawPackedMin = (static_cast<size_t>(bw) * bh * orfBps + 7u) / 8u;
                    if (cLen < rawPackedMin) {
                        detectedNominalBps = orfBps;
                        return UnpackOlympusOrfSensor(cPtr, cLen, bw, bh, orfBps, blockOut);
                    }
                }
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
            UnsliceCanonCr2Sensor(ifd.width, ifd.height, effSpp, ifd.cr2Slices, fullSamples);
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


inline bool DecodePsOrPdfArtwork(const uint8_t* data, size_t size, const std::wstring& label, ExtendedImageInfo& outInfo) {
    if (size < 8) return false;
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
            if (DecodeWindowsWmf(data + wmfStart, wmfLen, outInfo)) {
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

struct StitchPt { float x, y; int colorIdx; bool jump; };

inline bool RenderEmbroideryStitches(const std::vector<StitchPt>& pts, const std::wstring& label, ExtendedImageInfo& outInfo) {
    static const uint8_t kThreadColors[12][3] = {
        {239, 68, 68},   {59, 130, 246},  {34, 197, 94},  {250, 204, 21},
        {168, 85, 247},  {249, 115, 22},  {20, 184, 166}, {236, 72, 153},
        {248, 250, 252}, {132, 204, 22},  {56, 189, 248}, {251, 191, 36}
    };

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

} // namespace DetailImg200

namespace DetailEmfWmf {

struct GdiPen {
    uint32_t style = 0; // 0=SOLID, 1=DASH, 2=DOT, 3=DASHDOT, 4=DASHDOTDOT, 5=NULL, 6=INSIDEFRAME
    float width = 1.0f;
    uint8_t r = 0, g = 0, b = 0;
};

struct GdiBrush {
    uint32_t style = 0; // 0=SOLID, 1=NULL/HOLLOW, 2=HATCHED
    uint8_t r = 255, g = 255, b = 255;
    uint32_t hatch = 0;
};

struct GdiFont {
    float height = 14.0f;
    float escapementDeg = 0.0f;
};

struct GdiObjectSlot {
    enum Kind : uint8_t { Empty = 0, Pen, Brush, Font, Other } kind = Empty;
    GdiPen pen{};
    GdiBrush brush{};
    GdiFont font{};
};

struct XForm2D {
    float m11 = 1.0f, m12 = 0.0f;
    float m21 = 0.0f, m22 = 1.0f;
    float dx  = 0.0f, dy  = 0.0f;

    static XForm2D Multiply(const XForm2D& a, const XForm2D& b) {
        return {
            a.m11 * b.m11 + a.m12 * b.m21,
            a.m11 * b.m12 + a.m12 * b.m22,
            a.m21 * b.m11 + a.m22 * b.m21,
            a.m21 * b.m12 + a.m22 * b.m22,
            a.dx  * b.m11 + a.dy  * b.m21 + b.dx,
            a.dx  * b.m12 + a.dy  * b.m22 + b.dy
        };
    }
};

struct GdiDcState {
    uint32_t mapMode = 1; // MM_TEXT
    float winOrgX = 0.0f, winOrgY = 0.0f;
    float winExtX = 1.0f, winExtY = 1.0f;
    float vpOrgX  = 0.0f, vpOrgY  = 0.0f;
    float vpExtX  = 1.0f, vpExtY  = 1.0f;
    bool hasWinExt = false;
    bool hasVpExt  = false;
    XForm2D world{};
    float curX = 0.0f, curY = 0.0f;
    uint32_t polyFillMode = 1; // 1=ALTERNATE (even-odd), 2=WINDING
    uint32_t bkMode = 2;       // 1=TRANSPARENT, 2=OPAQUE
    uint8_t bkR = 255, bkG = 255, bkB = 255;
    uint8_t textR = 0, textG = 0, textB = 0;
    uint32_t textAlign = 0;
    uint8_t dcPenR = 0, dcPenG = 0, dcPenB = 0;
    uint8_t dcBrushR = 255, dcBrushG = 255, dcBrushB = 255;
    bool useDcPen = false;
    bool useDcBrush = false;
    GdiPen pen{ 0u, 1.0f, 0u, 0u, 0u };
    GdiBrush brush{ 0u, 255u, 255u, 255u, 0u };
    GdiFont font{};
};

struct PathSubfigure {
    std::vector<DetailImg200::Pt2f> pts; // Already transformed to canvas pixel space
    bool closed = false;
};

inline void UnpackColorRef(uint32_t cr, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = static_cast<uint8_t>(cr & 0xFFu);
    g = static_cast<uint8_t>((cr >> 8u) & 0xFFu);
    b = static_cast<uint8_t>((cr >> 16u) & 0xFFu);
}

inline void BlendPixelBgra(uint8_t* dst, uint8_t r, uint8_t g, uint8_t b, float alpha01) {
    int a = std::clamp(static_cast<int>(std::lround(alpha01 * 255.0f)), 0, 255);
    if (a <= 0) return;
    if (a >= 255) {
        dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255u;
        return;
    }
    int inv = 255 - a;
    dst[0] = static_cast<uint8_t>((b * a + dst[0] * inv + 127) / 255);
    dst[1] = static_cast<uint8_t>((g * a + dst[1] * inv + 127) / 255);
    dst[2] = static_cast<uint8_t>((r * a + dst[2] * inv + 127) / 255);
    dst[3] = 255u;
}

// 4x4 supersampled scanline anti-aliased polygon rasterizer (ALTERNATE even-odd & WINDING fill rules)
inline bool FillPolygons4x4AA(
    std::vector<uint8_t>& bgra, uint32_t w, uint32_t h,
    const std::vector<std::vector<DetailImg200::Pt2f>>& rings,
    bool evenOdd, const GdiBrush& brush,
    uint32_t bkMode, uint8_t bkR, uint8_t bkG, uint8_t bkB)
{
    if ((brush.style & 0xFFu) == 1u) return false; // BS_NULL / BS_HOLLOW

    float minYf = 1e30f, maxYf = -1e30f;
    size_t validRings = 0;
    for (const auto& ring : rings) {
        if (ring.size() < 3u) continue;
        ++validRings;
        for (const auto& p : ring) {
            minYf = std::min(minYf, p.y);
            maxYf = std::max(maxYf, p.y);
        }
    }
    if (validRings == 0u || minYf > maxYf) return false;

    const int yStart = std::clamp(static_cast<int>(std::floor(minYf)), 0, static_cast<int>(h) - 1);
    const int yEnd   = std::clamp(static_cast<int>(std::ceil(maxYf)),  0, static_cast<int>(h) - 1);

    struct EdgeHit {
        float x = 0.0f;
        int dir = 0;
    };
    std::vector<EdgeHit> hits;
    std::vector<float> rowCov(w, 0.0f);
    bool painted = false;

    auto addSpan = [&](float sx0, float sx1) {
        if (sx0 > sx1) std::swap(sx0, sx1);
        int ix0 = std::max(0, static_cast<int>(std::floor(sx0)));
        int ix1 = std::min(static_cast<int>(w) - 1, static_cast<int>(std::floor(sx1)));
        for (int x = ix0; x <= ix1; ++x) {
            float left  = std::max(sx0, static_cast<float>(x));
            float right = std::min(sx1, static_cast<float>(x + 1));
            if (right > left) rowCov[static_cast<size_t>(x)] += (right - left);
        }
    };

    for (int y = yStart; y <= yEnd; ++y) {
        std::fill(rowCov.begin(), rowCov.end(), 0.0f);
        for (int sub = 0; sub < 4; ++sub) {
            const float sy = static_cast<float>(y) + (static_cast<float>(sub) + 0.5f) * 0.25f;
            hits.clear();
            for (const auto& ring : rings) {
                if (ring.size() < 3u) continue;
                const size_t n = ring.size();
                for (size_t i = 0; i < n; ++i) {
                    const auto& a = ring[i];
                    const auto& c = ring[(i + 1u) % n];
                    if ((a.y <= sy && c.y > sy) || (c.y <= sy && a.y > sy)) {
                        float dy = c.y - a.y;
                        if (std::abs(dy) < 1e-9f) continue;
                        float ix = a.x + ((sy - a.y) / dy) * (c.x - a.x);
                        hits.push_back({ ix, (c.y > a.y) ? 1 : -1 });
                    }
                }
            }
            if (hits.size() < 2u) continue;
            std::sort(hits.begin(), hits.end(), [](const EdgeHit& a, const EdgeHit& b) { return a.x < b.x; });

            if (evenOdd) {
                for (size_t i = 0; i + 1u < hits.size(); i += 2u) {
                    addSpan(hits[i].x, hits[i + 1u].x);
                }
            } else {
                int winding = 0;
                float spanStart = 0.0f;
                for (const auto& hit : hits) {
                    int prev = winding;
                    winding += hit.dir;
                    if (prev == 0 && winding != 0) spanStart = hit.x;
                    else if (prev != 0 && winding == 0) addSpan(spanStart, hit.x);
                }
            }
        }

        uint8_t* dstRow = &bgra[static_cast<size_t>(y) * w * 4u];
        for (uint32_t x = 0; x < w; ++x) {
            float cov = rowCov[x] * 0.25f;
            if (cov <= 0.001f) continue;
            cov = std::min(1.0f, cov);
            if ((brush.style & 0xFFu) == 2u) { // BS_HATCHED
                bool onHatch = false;
                switch (brush.hatch) {
                case 0: onHatch = ((y & 7) == 0); break;                         // HS_HORIZONTAL
                case 1: onHatch = ((x & 7) == 0); break;                         // HS_VERTICAL
                case 2: onHatch = (((static_cast<int>(x) - y) & 7) == 0); break; // HS_FDIAGONAL
                case 3: onHatch = (((static_cast<int>(x) + y) & 7) == 0); break; // HS_BDIAGONAL
                case 4: onHatch = ((x & 7) == 0) || ((y & 7) == 0); break;       // HS_CROSS
                default: onHatch = (((static_cast<int>(x) - y) & 7) == 0) || (((static_cast<int>(x) + y) & 7) == 0); break;
                }
                if (onHatch) {
                    BlendPixelBgra(dstRow + x * 4u, brush.r, brush.g, brush.b, cov);
                    painted = true;
                } else if (bkMode == 2u) {
                    BlendPixelBgra(dstRow + x * 4u, bkR, bkG, bkB, cov);
                    painted = true;
                }
            } else {
                BlendPixelBgra(dstRow + x * 4u, brush.r, brush.g, brush.b, cov);
                painted = true;
            }
        }
    }
    return painted;
}

// Anti-aliased capsule stroke rasterizer with dash support
inline bool StrokePolylineAA(
    std::vector<uint8_t>& bgra, uint32_t w, uint32_t h,
    const std::vector<DetailImg200::Pt2f>& pts, bool closed,
    const GdiPen& pen, float devWidth)
{
    uint32_t styleType = pen.style & 0x0Fu;
    if (styleType == 5u || pts.size() < 2u) return false; // PS_NULL

    const float strokeW = std::clamp(devWidth, 1.0f, 128.0f);
    const float halfW = strokeW * 0.5f;
    const size_t segCount = closed ? pts.size() : (pts.size() - 1u);
    float arcDist = 0.0f;
    bool painted = false;

    for (size_t i = 0; i < segCount; ++i) {
        const auto& p0 = pts[i];
        const auto& p1 = pts[(i + 1u) % pts.size()];
        float dx = p1.x - p0.x;
        float dy = p1.y - p0.y;
        float segLen2 = dx * dx + dy * dy;
        float segLen  = std::sqrt(segLen2);

        int x0 = std::max(0, static_cast<int>(std::floor(std::min(p0.x, p1.x) - halfW - 1.0f)));
        int x1 = std::min(static_cast<int>(w) - 1, static_cast<int>(std::ceil(std::max(p0.x, p1.x) + halfW + 1.0f)));
        int y0 = std::max(0, static_cast<int>(std::floor(std::min(p0.y, p1.y) - halfW - 1.0f)));
        int y1 = std::min(static_cast<int>(h) - 1, static_cast<int>(std::ceil(std::max(p0.y, p1.y) + halfW + 1.0f)));

        for (int py = y0; py <= y1; ++py) {
            float cy = static_cast<float>(py) + 0.5f;
            uint8_t* dstRow = &bgra[static_cast<size_t>(py) * w * 4u];
            for (int px = x0; px <= x1; ++px) {
                float cx = static_cast<float>(px) + 0.5f;
                float t = 0.0f;
                if (segLen2 > 1e-8f) {
                    t = std::clamp(((cx - p0.x) * dx + (cy - p0.y) * dy) / segLen2, 0.0f, 1.0f);
                }
                if (styleType >= 1u && styleType <= 4u) {
                    float dAlong = arcDist + t * segLen;
                    float period = std::max(6.0f, strokeW * 6.0f);
                    float phase  = std::fmod(dAlong, period) / period;
                    bool gap = false;
                    if (styleType == 1u)      gap = (phase > 0.65f); // DASH
                    else if (styleType == 2u) gap = (phase > 0.25f && phase < 0.50f) || (phase > 0.75f); // DOT
                    else                      gap = (phase > 0.50f && phase < 0.68f) || (phase > 0.82f); // DASHDOT
                    if (gap) continue;
                }
                float nx = p0.x + t * dx;
                float ny = p0.y + t * dy;
                float dist = std::sqrt((cx - nx) * (cx - nx) + (cy - ny) * (cy - ny));
                float cov = std::clamp(halfW + 0.5f - dist, 0.0f, 1.0f);
                if (cov > 0.001f) {
                    BlendPixelBgra(dstRow + static_cast<size_t>(px) * 4u, pen.r, pen.g, pen.b, cov);
                    painted = true;
                }
            }
        }
        arcDist += segLen;
    }
    return painted;
}

inline void AppendCubicBezier(
    std::vector<DetailImg200::Pt2f>& poly,
    DetailImg200::Pt2f p0, DetailImg200::Pt2f p1,
    DetailImg200::Pt2f p2, DetailImg200::Pt2f p3)
{
    float chord = std::hypot(p3.x - p0.x, p3.y - p0.y) +
                  std::hypot(p1.x - p0.x, p1.y - p0.y) +
                  std::hypot(p2.x - p1.x, p2.y - p1.y) +
                  std::hypot(p3.x - p2.x, p3.y - p2.y);
    int steps = std::clamp(static_cast<int>(std::ceil(chord * 0.25f)), 8, 64);
    for (int s = 1; s <= steps; ++s) {
        float t = static_cast<float>(s) / static_cast<float>(steps);
        float u = 1.0f - t;
        float b0 = u * u * u;
        float b1 = 3.0f * u * u * t;
        float b2 = 3.0f * u * t * t;
        float b3 = t * t * t;
        poly.push_back({
            b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x,
            b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y
        });
    }
}

inline void BuildEllipseRing(
    float x0, float y0, float x1, float y1,
    std::vector<DetailImg200::Pt2f>& outPts, int segs = 48)
{
    outPts.clear();
    float cx = (x0 + x1) * 0.5f;
    float cy = (y0 + y1) * 0.5f;
    float rx = std::abs(x1 - x0) * 0.5f;
    float ry = std::abs(y1 - y0) * 0.5f;
    constexpr float kTwoPi = 6.283185307f;
    for (int i = 0; i < segs; ++i) {
        float a = kTwoPi * static_cast<float>(i) / static_cast<float>(segs);
        outPts.push_back({ cx + rx * std::cos(a), cy + ry * std::sin(a) });
    }
}

inline void BuildRoundRectRing(
    float x0, float y0, float x1, float y1, float cw, float ch,
    std::vector<DetailImg200::Pt2f>& outPts)
{
    outPts.clear();
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    float rx = std::clamp(std::abs(cw) * 0.5f, 0.0f, (x1 - x0) * 0.5f);
    float ry = std::clamp(std::abs(ch) * 0.5f, 0.0f, (y1 - y0) * 0.5f);
    if (rx < 0.5f || ry < 0.5f) {
        outPts = { {x0, y0}, {x1, y0}, {x1, y1}, {x0, y1} };
        return;
    }
    constexpr float kHalfPi = 1.570796327f;
    const float corners[4][3] = {
        { x1 - rx, y0 + ry, -kHalfPi },
        { x1 - rx, y1 - ry, 0.0f },
        { x0 + rx, y1 - ry, kHalfPi },
        { x0 + rx, y0 + ry, 2.0f * kHalfPi }
    };
    for (int c = 0; c < 4; ++c) {
        for (int s = 0; s <= 8; ++s) {
            float a = corners[c][2] + kHalfPi * (static_cast<float>(s) / 8.0f);
            outPts.push_back({ corners[c][0] + rx * std::cos(a), corners[c][1] + ry * std::sin(a) });
        }
    }
}

inline void BuildEllipticalArcPoints(
    float left, float top, float right, float bottom,
    float startX, float startY, float endX, float endY,
    bool counterClockwise,
    std::vector<DetailImg200::Pt2f>& outPts)
{
    outPts.clear();
    float cx = (left + right) * 0.5f;
    float cy = (top + bottom) * 0.5f;
    float rx = std::abs(right - left) * 0.5f;
    float ry = std::abs(bottom - top) * 0.5f;
    if (rx < 0.5f || ry < 0.5f) return;

    constexpr float kTwoPi = 6.283185307f;
    float a0 = std::atan2((startY - cy) / ry, (startX - cx) / rx);
    float a1 = std::atan2((endY   - cy) / ry, (endX   - cx) / rx);
    // In GDI MM_TEXT (y-down), counter-clockwise on screen means DECREASING angle!
    float sweep = a1 - a0;
    if (counterClockwise) {
        while (sweep >= 0.0f) sweep -= kTwoPi;
        if (std::abs(sweep) < 1e-4f) sweep = -kTwoPi;
    } else {
        while (sweep <= 0.0f) sweep += kTwoPi;
        if (std::abs(sweep) < 1e-4f) sweep = kTwoPi;
    }
    int steps = std::clamp(static_cast<int>(std::ceil(std::abs(sweep) * 12.0f)), 12, 64);
    for (int s = 0; s <= steps; ++s) {
        float a = a0 + sweep * (static_cast<float>(s) / static_cast<float>(steps));
        outPts.push_back({ cx + rx * std::cos(a), cy + ry * std::sin(a) });
    }
}

inline void ApplyStockObject(uint32_t stockId, GdiDcState& dc) {
    switch (stockId & 0x7FFFFFFFu) {
    case 0: dc.brush = { 0u, 255u, 255u, 255u, 0u }; dc.useDcBrush = false; break; // WHITE_BRUSH
    case 1: dc.brush = { 0u, 192u, 192u, 192u, 0u }; dc.useDcBrush = false; break; // LTGRAY_BRUSH
    case 2: dc.brush = { 0u, 128u, 128u, 128u, 0u }; dc.useDcBrush = false; break; // GRAY_BRUSH
    case 3: dc.brush = { 0u,  64u,  64u,  64u, 0u }; dc.useDcBrush = false; break; // DKGRAY_BRUSH
    case 4: dc.brush = { 0u,   0u,   0u,   0u, 0u }; dc.useDcBrush = false; break; // BLACK_BRUSH
    case 5: dc.brush = { 1u,   0u,   0u,   0u, 0u }; dc.useDcBrush = false; break; // NULL_BRUSH
    case 6: dc.pen   = { 0u, 1.0f, 255u, 255u, 255u }; dc.useDcPen = false; break; // WHITE_PEN
    case 7: dc.pen   = { 0u, 1.0f,   0u,   0u,   0u }; dc.useDcPen = false; break; // BLACK_PEN
    case 8: dc.pen   = { 5u, 0.0f,   0u,   0u,   0u }; dc.useDcPen = false; break; // NULL_PEN
    case 18: dc.useDcBrush = true; dc.brush = { 0u, dc.dcBrushR, dc.dcBrushG, dc.dcBrushB, 0u }; break; // DC_BRUSH
    case 19: dc.useDcPen   = true; dc.pen   = { 0u, 1.0f, dc.dcPenR, dc.dcPenG, dc.dcPenB };    break; // DC_PEN
    default: break;
    }
}

inline bool BlitSplitOrPackedDib(
    std::vector<uint8_t>& canvas, uint32_t outW, uint32_t outH,
    float dstX0, float dstY0, float dstX1, float dstY1,
    int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH,
    const uint8_t* bmiPtr, size_t bmiLen,
    const uint8_t* bitsPtr, size_t bitsLen)
{
    if (!bmiPtr || bmiLen < 40) return false;
    std::vector<uint8_t> dib;
    if (bitsPtr && bitsLen > 0) {
        dib.reserve(bmiLen + bitsLen);
        dib.insert(dib.end(), bmiPtr, bmiPtr + bmiLen);
        dib.insert(dib.end(), bitsPtr, bitsPtr + bitsLen);
    } else {
        dib.assign(bmiPtr, bmiPtr + bmiLen);
    }

    ExtendedImageInfo srcImg;
    if (!DetailImg200::DecodeDibBufferToInfo(dib.data(), dib.size(), L"DIB", srcImg) ||
        !srcImg.valid || srcImg.width == 0 || srcImg.height == 0)
    {
        return false;
    }

    if (dstX0 > dstX1) std::swap(dstX0, dstX1);
    if (dstY0 > dstY1) std::swap(dstY0, dstY1);
    int px0 = std::clamp(static_cast<int>(std::floor(dstX0)), 0, static_cast<int>(outW));
    int px1 = std::clamp(static_cast<int>(std::ceil(dstX1)),  0, static_cast<int>(outW));
    int py0 = std::clamp(static_cast<int>(std::floor(dstY0)), 0, static_cast<int>(outH));
    int py1 = std::clamp(static_cast<int>(std::ceil(dstY1)),  0, static_cast<int>(outH));
    if (px1 <= px0 || py1 <= py0) return false;

    int sx0 = (srcW > 0) ? std::clamp<int>(srcX, 0, static_cast<int>(srcImg.width) - 1) : 0;
    int sy0 = (srcH > 0) ? std::clamp<int>(srcY, 0, static_cast<int>(srcImg.height) - 1) : 0;
    int sw  = (srcW > 0) ? std::clamp<int>(srcW, 1, static_cast<int>(srcImg.width) - sx0) : static_cast<int>(srcImg.width);
    int sh  = (srcH > 0) ? std::clamp<int>(srcH, 1, static_cast<int>(srcImg.height) - sy0) : static_cast<int>(srcImg.height);

    for (int y = py0; y < py1; ++y) {
        float v = (static_cast<float>(y) + 0.5f - dstY0) / std::max(1.0f, dstY1 - dstY0);
        int sy = std::clamp(sy0 + static_cast<int>(v * static_cast<float>(sh)), 0, static_cast<int>(srcImg.height) - 1);
        for (int x = px0; x < px1; ++x) {
            float u = (static_cast<float>(x) + 0.5f - dstX0) / std::max(1.0f, dstX1 - dstX0);
            int sx = std::clamp(sx0 + static_cast<int>(u * static_cast<float>(sw)), 0, static_cast<int>(srcImg.width) - 1);
            const uint8_t* sp = &srcImg.bgraPixels[(static_cast<size_t>(sy) * srcImg.width + static_cast<size_t>(sx)) * 4u];
            uint8_t* dp = &canvas[(static_cast<size_t>(y) * outW + static_cast<size_t>(x)) * 4u];
            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = 255u;
        }
    }
    return true;
}


#if defined(_WIN32) || defined(E2_WIN32_HAL)
inline bool RenderEnhMetaFileViaGdi(HENHMETAFILE hEmf, const std::wstring& label, ExtendedImageInfo& outInfo) {
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
    bmi.bmiHeader.biHeight = -static_cast<LONG>(outH);
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

        // Reject empty comment-only metafiles that left the canvas solid white
        const uint8_t* pxBytes = static_cast<const uint8_t*>(dibBits);
        bool anyNonWhite = false;
        for (size_t i = 0; i < static_cast<size_t>(outW) * outH * 4u; i += 4u) {
            if (pxBytes[i + 0] != 255u || pxBytes[i + 1] != 255u || pxBytes[i + 2] != 255u) {
                anyNonWhite = true;
                break;
            }
        }
        if (anyNonWhite) {
            outInfo.width = outW;
            outInfo.height = outH;
            outInfo.origWidth = static_cast<uint32_t>(std::max(1, srcW));
            outInfo.origHeight = static_cast<uint32_t>(std::max(1, srcH));
            outInfo.channels = 4;
            outInfo.bitsPerChannel = 8;
            outInfo.formatName = label;
            outInfo.bgraPixels.resize(static_cast<size_t>(outW) * outH * 4);
            std::memcpy(outInfo.bgraPixels.data(), dibBits, outInfo.bgraPixels.size());
            for (size_t p = 3; p < outInfo.bgraPixels.size(); p += 4) {
                outInfo.bgraPixels[p] = 255;
            }
            outInfo.valid = true;
            rendered = true;
        }
        SelectObject(hdcMem, oldObj);
    }
    if (hBmp) DeleteObject(hBmp);
    if (hdcMem) DeleteDC(hdcMem);
    DeleteEnhMetaFile(hEmf);
    return rendered;
}
#endif

} // namespace DetailEmfWmf

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

namespace ConsoleTexDetail {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdI32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU32BE;

enum class GpuSurfaceCodec {
    None,
    RGBA8,
    BGRA8,
    ARGB8,
    ABGR8,
    RGB565,
    R8,
    RGBA16F,
    BC1,
    BC2,
    BC3,
    BC4_U,
    BC4_S,
    BC5_U,
    BC5_S,
    BC6H_U,
    BC6H_S,
    BC7,
    ETC2_RGB,
    ETC2_RGBA,
    ASTC_4x4
};

inline bool GetCodecBlockLayout(GpuSurfaceCodec c, uint32_t& outBlockW, uint32_t& outBlockH, uint32_t& outBytesPerBlock) {
    outBlockW = 1u; outBlockH = 1u; outBytesPerBlock = 4u;
    switch (c) {
        case GpuSurfaceCodec::RGBA8:
        case GpuSurfaceCodec::BGRA8:
        case GpuSurfaceCodec::ARGB8:
        case GpuSurfaceCodec::ABGR8:
            outBlockW = 1u; outBlockH = 1u; outBytesPerBlock = 4u; return true;
        case GpuSurfaceCodec::RGB565:
            outBlockW = 1u; outBlockH = 1u; outBytesPerBlock = 2u; return true;
        case GpuSurfaceCodec::R8:
            outBlockW = 1u; outBlockH = 1u; outBytesPerBlock = 1u; return true;
        case GpuSurfaceCodec::RGBA16F:
            outBlockW = 1u; outBlockH = 1u; outBytesPerBlock = 8u; return true;
        case GpuSurfaceCodec::BC1:
        case GpuSurfaceCodec::BC4_U:
        case GpuSurfaceCodec::BC4_S:
        case GpuSurfaceCodec::ETC2_RGB:
            outBlockW = 4u; outBlockH = 4u; outBytesPerBlock = 8u; return true;
        case GpuSurfaceCodec::BC2:
        case GpuSurfaceCodec::BC3:
        case GpuSurfaceCodec::BC5_U:
        case GpuSurfaceCodec::BC5_S:
        case GpuSurfaceCodec::BC6H_U:
        case GpuSurfaceCodec::BC6H_S:
        case GpuSurfaceCodec::BC7:
        case GpuSurfaceCodec::ETC2_RGBA:
        case GpuSurfaceCodec::ASTC_4x4:
            outBlockW = 4u; outBlockH = 4u; outBytesPerBlock = 16u; return true;
        default:
            return false;
    }
}

// Decode linear surface of blocks/texels into premultiplied BGRA8
inline bool DecodeLinearGpuSurface(
    const uint8_t* src,
    size_t srcLen,
    uint32_t w,
    uint32_t h,
    GpuSurfaceCodec codec,
    std::vector<uint8_t>& outBgra)
{
    uint32_t blkW = 1, blkH = 1, bpb = 4;
    if (!GetCodecBlockLayout(codec, blkW, blkH, bpb) || w == 0 || h == 0 || w > 8192 || h > 8192) return false;
    uint32_t gridW = (w + blkW - 1u) / blkW;
    uint32_t gridH = (h + blkH - 1u) / blkH;
    uint64_t needBytes = static_cast<uint64_t>(gridW) * gridH * bpb;
    if (needBytes > srcLen) return false;

    if (codec == GpuSurfaceCodec::ASTC_4x4) {
        return DecodeAstcSurface(src, srcLen, w, h, 4, 4, outBgra);
    }

    size_t pxTotal = static_cast<size_t>(w) * h;
    if (codec == GpuSurfaceCodec::RGBA16F) {
        std::vector<float> flt(pxTotal * 4u, 0.0f);
        for (size_t i = 0; i < pxTotal; ++i) {
            const uint8_t* sp = src + i * 8u;
            flt[i * 4 + 0] = GpuTex::HalfToFloat(RdU16LE(sp + 0));
            flt[i * 4 + 1] = GpuTex::HalfToFloat(RdU16LE(sp + 2));
            flt[i * 4 + 2] = GpuTex::HalfToFloat(RdU16LE(sp + 4));
            flt[i * 4 + 3] = GpuTex::HalfToFloat(RdU16LE(sp + 6));
        }
        GpuTex::ConvertFloatRgbaToBgra(flt.data(), w, h, false, outBgra);
        return true;
    }
    if (codec == GpuSurfaceCodec::BC6H_U || codec == GpuSurfaceCodec::BC6H_S) {
        std::vector<float> flt(pxTotal * 4u, 0.0f);
        float blkF[64];
        for (uint32_t by = 0; by < gridH; ++by) {
            for (uint32_t bx = 0; bx < gridW; ++bx) {
                GpuTex::DecodeBC6HBlock(src + (static_cast<size_t>(by) * gridW + bx) * 16u, blkF, codec == GpuSurfaceCodec::BC6H_S);
                for (uint32_t py = 0; py < 4u; ++py) {
                    uint32_t dy = by * 4u + py;
                    if (dy >= h) continue;
                    for (uint32_t px = 0; px < 4u; ++px) {
                        uint32_t dx = bx * 4u + px;
                        if (dx >= w) continue;
                        std::memcpy(&flt[(static_cast<size_t>(dy) * w + dx) * 4u], &blkF[(py * 4u + px) * 4u], 16u);
                    }
                }
            }
        }
        GpuTex::ConvertFloatRgbaToBgra(flt.data(), w, h, true, outBgra);
        return true;
    }

    outBgra.assign(pxTotal * 4u, 0u);
    if (blkW == 1u && blkH == 1u) {
        for (size_t i = 0; i < pxTotal; ++i) {
            const uint8_t* sp = src + i * bpb;
            uint8_t r = 0, g = 0, b = 0, a = 255u;
            switch (codec) {
                case GpuSurfaceCodec::RGBA8: r = sp[0]; g = sp[1]; b = sp[2]; a = sp[3]; break;
                case GpuSurfaceCodec::BGRA8: b = sp[0]; g = sp[1]; r = sp[2]; a = sp[3]; break;
                case GpuSurfaceCodec::ARGB8: a = sp[0]; r = sp[1]; g = sp[2]; b = sp[3]; break;
                case GpuSurfaceCodec::ABGR8: a = sp[0]; b = sp[1]; g = sp[2]; r = sp[3]; break;
                case GpuSurfaceCodec::RGB565: {
                    uint16_t v = RdU16LE(sp);
                    r = static_cast<uint8_t>(((v >> 11) & 31u) * 255u / 31u);
                    g = static_cast<uint8_t>(((v >> 5)  & 63u) * 255u / 63u);
                    b = static_cast<uint8_t>((v & 31u) * 255u / 31u);
                    break;
                }
                case GpuSurfaceCodec::R8: r = g = b = sp[0]; break;
                default: return false;
            }
            WritePremulBGRA(&outBgra[i * 4u], r, g, b, a);
        }
        return true;
    }

    uint8_t blkRgba[64];
    for (uint32_t by = 0; by < gridH; ++by) {
        for (uint32_t bx = 0; bx < gridW; ++bx) {
            const uint8_t* bp = src + (static_cast<size_t>(by) * gridW + bx) * bpb;
            switch (codec) {
                case GpuSurfaceCodec::BC1:       GpuTex::DecodeBC1Block(bp, blkRgba, true); break;
                case GpuSurfaceCodec::BC2:       GpuTex::DecodeBC2Block(bp, blkRgba); break;
                case GpuSurfaceCodec::BC3:       GpuTex::DecodeBC3Block(bp, blkRgba); break;
                case GpuSurfaceCodec::BC4_U:     GpuTex::DecodeBC4Block(bp, blkRgba, false); break;
                case GpuSurfaceCodec::BC4_S:     GpuTex::DecodeBC4Block(bp, blkRgba, true); break;
                case GpuSurfaceCodec::BC5_U:     GpuTex::DecodeBC5Block(bp, blkRgba, false); break;
                case GpuSurfaceCodec::BC5_S:     GpuTex::DecodeBC5Block(bp, blkRgba, true); break;
                case GpuSurfaceCodec::BC7:       GpuTex::DecodeBC7Block(bp, blkRgba); break;
                case GpuSurfaceCodec::ETC2_RGB:  GpuTex::DecodeETC2RgbBlock(bp, blkRgba, true); break;
                case GpuSurfaceCodec::ETC2_RGBA: GpuTex::DecodeETC2Block(bp, blkRgba, true); break;
                default: return false;
            }
            GpuTex::BlitBlock4x4RgbaToPremulBgra(blkRgba, bx, by, w, h, outBgra);
        }
    }
    return true;
}

// NVIDIA Tegra X1 Block-Linear GOB (64B x 8 rows = 512B) deswizzler for Nintendo Switch .bntx
inline bool DeswizzleTegraBlockLinear(
    const uint8_t* swizzled,
    size_t swizzledLen,
    uint32_t gridW,
    uint32_t gridH,
    uint32_t bpb,
    uint32_t blockHeightLog2,
    std::vector<uint8_t>& outLinear)
{
    uint64_t linearBytes = static_cast<uint64_t>(gridW) * gridH * bpb;
    if (linearBytes == 0 || linearBytes > 256ULL * 1024 * 1024) return false;
    outLinear.assign(static_cast<size_t>(linearBytes), 0u);

    uint32_t bhGobs = 1u << std::min<uint32_t>(blockHeightLog2, 5u);
    while (bhGobs > 1u && gridH <= (bhGobs >> 1u) * 8u) {
        bhGobs >>= 1u;
    }

    const uint32_t rowBytes = gridW * bpb;
    const uint32_t widthInGobs = (rowBytes + 63u) / 64u;
    const uint32_t blockRows = bhGobs * 8u;

    for (uint32_t by = 0; by < gridH; ++by) {
        uint32_t gobBlockY = by / blockRows;
        uint32_t remY      = by % blockRows;
        uint32_t gobInBlkY = remY / 8u;
        uint32_t yInGob    = remY & 7u;

        for (uint32_t bx = 0; bx < gridW; ++bx) {
            uint32_t byteX  = bx * bpb;
            uint32_t gobX   = byteX / 64u;
            uint32_t xInGob = byteX & 63u;

            // Intra-GOB 9-bit Tegra X1 Sector-Z swizzle
            uint32_t intraGob = ((xInGob & 32u) << 3) |
                                ((yInGob & 6u)  << 5) |
                                ((xInGob & 16u) << 1) |
                                ((yInGob & 1u)  << 4) |
                                 (xInGob & 15u);

            uint64_t gobIndex = (static_cast<uint64_t>(gobBlockY) * widthInGobs + gobX) * bhGobs + gobInBlkY;
            uint64_t srcOff   = gobIndex * 512ull + intraGob;
            if (srcOff + bpb > swizzledLen) return false;

            size_t dstOff = (static_cast<size_t>(by) * gridW + bx) * bpb;
            std::memcpy(&outLinear[dstOff], swizzled + static_cast<size_t>(srcOff), bpb);
        }
    }
    return true;
}

// General 2D Morton (Z-order) index for power-of-2 & rectangular grids (PS Vita SceGxm / PS3 CellGcm SZ)
inline uint32_t CalcMorton2D(uint32_t x, uint32_t y, uint32_t gridW, uint32_t gridH, bool vitaOrder) {
    // Vita SceGxm interleaves Y in bit 0, X in bit 1: (x0<<1)|y0!
    // PS3 CellGcm SZ interleaves X in bit 0, Y in bit 1: (y0<<1)|x0!
    uint32_t minDim = std::min(gridW, gridH);
    uint32_t logMin = 0;
    while ((1u << (logMin + 1u)) <= minDim) ++logMin;
    if ((1u << logMin) < minDim) ++logMin; // round up if non-pow2

    uint32_t idx = 0;
    uint32_t bitOut = 0;
    for (uint32_t b = 0; b < logMin; ++b) {
        uint32_t xb = (x >> b) & 1u;
        uint32_t yb = (y >> b) & 1u;
        if (vitaOrder) {
            idx |= (yb << bitOut);
            idx |= (xb << (bitOut + 1u));
        } else {
            idx |= (xb << bitOut);
            idx |= (yb << (bitOut + 1u));
        }
        bitOut += 2u;
    }
    if (gridW >= gridH) {
        idx |= ((x >> logMin) << bitOut);
    } else {
        idx |= ((y >> logMin) << bitOut);
    }
    return idx;
}

inline bool DeswizzleMortonSurface(
    const uint8_t* swizzled,
    size_t swizzledLen,
    uint32_t gridW,
    uint32_t gridH,
    uint32_t bpb,
    bool vitaOrder,
    std::vector<uint8_t>& outLinear)
{
    uint64_t totalBytes = static_cast<uint64_t>(gridW) * gridH * bpb;
    if (totalBytes == 0 || totalBytes > 256ULL * 1024 * 1024) return false;
    outLinear.assign(static_cast<size_t>(totalBytes), 0u);

    for (uint32_t by = 0; by < gridH; ++by) {
        for (uint32_t bx = 0; bx < gridW; ++bx) {
            uint32_t mIdx = CalcMorton2D(bx, by, gridW, gridH, vitaOrder);
            uint64_t srcOff = static_cast<uint64_t>(mIdx) * bpb;
            if (srcOff + bpb > swizzledLen) return false;
            std::memcpy(&outLinear[(static_cast<size_t>(by) * gridW + bx) * bpb],
                        swizzled + static_cast<size_t>(srcOff), bpb);
        }
    }
    return true;
}

inline GpuSurfaceCodec MapDxgiToSurfaceCodec(uint32_t dxgi) {
    switch (dxgi) {
        case 9: case 10:               return GpuSurfaceCodec::RGBA16F;
        case 27: case 28: case 29:     return GpuSurfaceCodec::RGBA8;
        case 60: case 61:              return GpuSurfaceCodec::R8;
        case 70: case 71: case 72:     return GpuSurfaceCodec::BC1;
        case 73: case 74: case 75:     return GpuSurfaceCodec::BC2;
        case 76: case 77: case 78:     return GpuSurfaceCodec::BC3;
        case 79: case 80:              return GpuSurfaceCodec::BC4_U;
        case 81:                       return GpuSurfaceCodec::BC4_S;
        case 82: case 83:              return GpuSurfaceCodec::BC5_U;
        case 84:                       return GpuSurfaceCodec::BC5_S;
        case 85:                       return GpuSurfaceCodec::RGB565;
        case 87: case 88: case 90: case 91: return GpuSurfaceCodec::BGRA8;
        case 94: case 95:              return GpuSurfaceCodec::BC6H_U;
        case 96:                       return GpuSurfaceCodec::BC6H_S;
        case 97: case 98: case 99:     return GpuSurfaceCodec::BC7;
        default:                       return GpuSurfaceCodec::None;
    }
}

} // namespace ConsoleTexDetail

namespace AvifHeifDetail {

// big-endian loads (ByteIO.h)
inline uint16_t RdBE16(const uint8_t* p) { return ::Explorer2::ByteIO::RdU16BE(p); }
inline uint32_t RdBE32(const uint8_t* p) { return ::Explorer2::ByteIO::RdU32BE(p); }
inline uint64_t RdBE64(const uint8_t* p) { return ::Explorer2::ByteIO::RdU64BE(p); }

inline uint64_t RdVarBE(const uint8_t* p, uint8_t nBytes) {
    uint64_t v = 0;
    for (uint8_t i = 0; i < nBytes; ++i) v = (v << 8) | p[i];
    return v;
}

struct IsoBox {
    char type[5] = {};
    size_t start = 0;      // box header start
    size_t bodyStart = 0;  // payload start (after 8B or 16B size/type)
    size_t end = 0;        // box end
};

inline bool NextIsoBox(const uint8_t* data, size_t limit, size_t& cursor, IsoBox& out) {
    if (cursor + 8 > limit) return false;
    uint64_t sz = RdBE32(data + cursor);
    std::memcpy(out.type, data + cursor + 4, 4);
    out.type[4] = '\0';
    out.start = cursor;
    size_t hdr = 8;
    if (sz == 1) {
        if (cursor + 16 > limit) return false;
        sz = RdBE64(data + cursor + 8);
        hdr = 16;
    } else if (sz == 0) {
        sz = limit - cursor;
    }
    if (sz < hdr || sz > limit - cursor) return false;
    out.bodyStart = cursor + hdr;
    out.end = cursor + static_cast<size_t>(sz);
    cursor = out.end;
    return true;
}

// Bitstream reader with optional H.264/H.265 RBSP emulation-prevention removal (00 00 03 -> 00 00)
struct BitReader {
    const uint8_t* buf = nullptr;
    size_t len = 0;
    size_t bytePos = 0;
    int bitPos = 0; // 0..7 MSB first

    BitReader(const uint8_t* b, size_t n) : buf(b), len(n) {}

    bool Eof() const { return bytePos >= len; }

    void AlignToByte() {
        if (bitPos != 0) { bitPos = 0; ++bytePos; }
    }

    uint32_t ReadBit() {
        if (bytePos >= len) return 0;
        uint32_t b = (buf[bytePos] >> (7 - bitPos)) & 1u;
        if (++bitPos == 8) { bitPos = 0; ++bytePos; }
        return b;
    }

    uint32_t ReadBits(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) v = (v << 1) | ReadBit();
        return v;
    }

    // Exp-Golomb unsigned ue(v)
    uint32_t ReadUE() {
        int zeros = 0;
        while (!Eof() && zeros < 31 && ReadBit() == 0) ++zeros;
        if (zeros == 0) return 0;
        return ((1u << zeros) - 1u) + ReadBits(zeros);
    }

    // Exp-Golomb signed se(v)
    int32_t ReadSE() {
        uint32_t u = ReadUE();
        return ((u & 1u) != 0) ? static_cast<int32_t>((u + 1u) >> 1) : -static_cast<int32_t>(u >> 1);
    }
};

inline std::vector<uint8_t> StripRbspEmulationBytes(const uint8_t* nal, size_t nalLen, size_t hdrBytes) {
    std::vector<uint8_t> rbsp;
    if (nalLen <= hdrBytes) return rbsp;
    rbsp.reserve(nalLen - hdrBytes);
    int zeroCnt = 0;
    for (size_t i = hdrBytes; i < nalLen; ++i) {
        uint8_t b = nal[i];
        if (zeroCnt >= 2 && b == 0x03u && i + 1 < nalLen && (nal[i + 1] <= 0x03u)) {
            zeroCnt = 0;
            continue;
        }
        rbsp.push_back(b);
        if (b == 0) ++zeroCnt;
        else zeroCnt = 0;
    }
    return rbsp;
}

inline void YCbCrToRgb8(int y, int cb, int cr, bool fullRange, uint16_t matrixCoeffs, uint8_t& r, uint8_t& g, uint8_t& b) {
    // matrixCoeffs == 0: Identity (GBR!)
    if (matrixCoeffs == 0) {
        g = static_cast<uint8_t>(std::clamp(y, 0, 255));
        b = static_cast<uint8_t>(std::clamp(cb, 0, 255));
        r = static_cast<uint8_t>(std::clamp(cr, 0, 255));
        return;
    }
    double yf = fullRange ? static_cast<double>(y) : ((static_cast<double>(y) - 16.0) * (255.0 / 219.0));
    double uf = fullRange ? (static_cast<double>(cb) - 128.0) : ((static_cast<double>(cb) - 128.0) * (255.0 / 224.0));
    double vf = fullRange ? (static_cast<double>(cr) - 128.0) : ((static_cast<double>(cr) - 128.0) * (255.0 / 224.0));

    // BT.709 (1) vs BT.601 (5, 6, default) vs BT.2020 (9)
    double kr = 0.299, kb = 0.114;
    if (matrixCoeffs == 1)      { kr = 0.2126; kb = 0.0722; }
    else if (matrixCoeffs == 9) { kr = 0.2627; kb = 0.0593; }
    double kg = 1.0 - kr - kb;

    double rf = yf + (2.0 - 2.0 * kr) * vf;
    double bf = yf + (2.0 - 2.0 * kb) * uf;
    double gf = yf - ((kb * (2.0 - 2.0 * kb)) / kg) * uf - ((kr * (2.0 - 2.0 * kr)) / kg) * vf;

    r = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(rf)), 0, 255));
    g = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(gf)), 0, 255));
    b = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(bf)), 0, 255));
}

struct DecodedRgbaTile {
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> rgba; // non-premultiplied RGBA8
};

// ============================================================================
// Backend 1: WIC Stream Decoder (handles JPEG items & OS-supported HEIF/AVIF)
// ============================================================================
inline bool TryDecodeViaWicMemory(const uint8_t* bytes, size_t len, DecodedRgbaTile& out) {
#if !defined(_WIN32) && !defined(E2_WIN32_HAL)
    return false;   // the Linux build of the readers has no Windows Imaging Component (WIC)
#else
    if (!bytes || len < 16) return false;
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) || !factory) {
        return false;
    }
    Microsoft::WRL::ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)) || !stream) return false;
    if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes), static_cast<DWORD>(len)))) return false;

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) || !decoder) {
        return false;
    }
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)) || !frame) return false;

    UINT w = 0, h = 0;
    if (FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0 || w > 16384 || h > 16384) return false;

    Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
    if (FAILED(factory->CreateFormatConverter(&conv)) || !conv) return false;
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        return false;
    }
    out.w = w; out.h = h;
    out.rgba.resize(static_cast<size_t>(w) * h * 4);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(out.rgba.size()), out.rgba.data()))) {
        out.rgba.clear();
        return false;
    }
    return true;
#endif
}

// ============================================================================
// Backend 2: Pure-C++ ISO/IEC 23001-17 Uncompressed ('unci' + 'uncC'/'cmpd')
// ============================================================================
struct UncConfig {
    bool present = false;
    uint32_t profile = 0;
    uint8_t interleaveType = 1; // 0=component(planar), 1=pixel(interleaved), 5=multi-Y
    std::vector<uint16_t> compTypes; // from cmpd: 0=mono, 1=Y, 2=Cb, 3=Cr, 4=R, 5=G, 6=B, 7=A
    std::vector<uint8_t> compBitDepths;
};

inline bool DecodeUncompressedHeifItem(
    const uint8_t* payload, size_t payLen,
    uint32_t w, uint32_t h,
    const UncConfig& unc,
    bool fullRange, uint16_t matrixCoeffs,
    DecodedRgbaTile& out)
{
    if (!payload || w == 0 || h == 0 || w > 16384 || h > 16384 || !unc.present || unc.compBitDepths.empty()) return false;
    size_t nComp = unc.compBitDepths.size();
    if (nComp != 1 && nComp != 3 && nComp != 4) return false;

    uint8_t bd = unc.compBitDepths[0];
    if (bd != 8 && bd != 10 && bd != 12 && bd != 16) return false;
    size_t bps = (bd > 8) ? 2u : 1u;
    uint32_t maxVal = (1u << bd) - 1u;

    uint64_t need = static_cast<uint64_t>(w) * h * nComp * bps;
    if (payLen < need) return false;

    bool isYuv = (nComp >= 3 && unc.compTypes.size() >= 3 && unc.compTypes[0] == 1 && unc.compTypes[1] == 2 && unc.compTypes[2] == 3);
    auto rdSample8 = [&](size_t sampleIdx) -> uint8_t {
        if (bps == 1) return payload[sampleIdx];
        uint16_t v = RdBE16(payload + sampleIdx * 2u);
        return static_cast<uint8_t>((static_cast<uint32_t>(std::min<uint32_t>(v, maxVal)) * 255u + maxVal / 2u) / maxVal);
    };

    out.w = w; out.h = h;
    out.rgba.resize(static_cast<size_t>(w) * h * 4);
    size_t pixCnt = static_cast<size_t>(w) * h;

    for (size_t p = 0; p < pixCnt; ++p) {
        uint8_t c0 = 0, c1 = 0, c2 = 0, c3 = 255;
        if (unc.interleaveType == 0) { // Planar
            c0 = rdSample8(0 * pixCnt + p);
            if (nComp >= 3) { c1 = rdSample8(1 * pixCnt + p); c2 = rdSample8(2 * pixCnt + p); }
            if (nComp == 4) c3 = rdSample8(3 * pixCnt + p);
        } else { // Pixel-interleaved
            c0 = rdSample8(p * nComp + 0);
            if (nComp >= 3) { c1 = rdSample8(p * nComp + 1); c2 = rdSample8(p * nComp + 2); }
            if (nComp == 4) c3 = rdSample8(p * nComp + 3);
        }

        uint8_t r = c0, g = c0, b = c0;
        if (nComp >= 3) {
            if (isYuv) YCbCrToRgb8(c0, c1, c2, fullRange, matrixCoeffs, r, g, b);
            else { r = c0; g = c1; b = c2; }
        }
        out.rgba[p * 4 + 0] = r;
        out.rgba[p * 4 + 1] = g;
        out.rgba[p * 4 + 2] = b;
        out.rgba[p * 4 + 3] = c3;
    }
    return true;
}


inline bool DecodeAvc1StillItem(const uint8_t* itemData, size_t itemLen, const std::vector<uint8_t>& avcC, bool fullRange, uint16_t matrixCoeffs, DecodedRgbaTile& out);
inline bool DecodeHvc1StillItem(const uint8_t* itemData, size_t itemLen, const std::vector<uint8_t>& hvcC, bool fullRange, uint16_t matrixCoeffs, DecodedRgbaTile& out);
inline bool DecodeAv01StillItem(const uint8_t* itemData, size_t itemLen, const std::vector<uint8_t>& av1C, bool defaultFullRange, uint16_t defaultMatrix, DecodedRgbaTile& out);

inline void ApplyMirrorRgba(DecodedRgbaTile& img, uint8_t axis) {
    // ISO 23008-12 imir: axis == 0 -> top-to-bottom (vertical flip); axis == 1 -> left-to-right (horizontal flip)
    if (img.w == 0 || img.h == 0) return;
    if (axis == 0) {
        for (uint32_t y = 0; y < img.h / 2; ++y) {
            uint32_t oy = img.h - 1 - y;
            for (uint32_t x = 0; x < img.w; ++x) {
                for (int c = 0; c < 4; ++c) {
                    std::swap(img.rgba[(static_cast<size_t>(y) * img.w + x) * 4 + c],
                              img.rgba[(static_cast<size_t>(oy) * img.w + x) * 4 + c]);
                }
            }
        }
    } else {
        for (uint32_t y = 0; y < img.h; ++y) {
            for (uint32_t x = 0; x < img.w / 2; ++x) {
                uint32_t ox = img.w - 1 - x;
                for (int c = 0; c < 4; ++c) {
                    std::swap(img.rgba[(static_cast<size_t>(y) * img.w + x) * 4 + c],
                              img.rgba[(static_cast<size_t>(y) * img.w + ox) * 4 + c]);
                }
            }
        }
    }
}

inline void ApplyRotateCcwRgba(DecodedRgbaTile& img, uint8_t angleCode) {
    // ISO 23008-12 irot: angle * 90 degrees counter-clockwise (1=90 CCW, 2=180, 3=270 CCW)
    angleCode &= 3u;
    if (angleCode == 0 || img.w == 0 || img.h == 0) return;
    uint32_t nw = (angleCode & 1u) ? img.h : img.w;
    uint32_t nh = (angleCode & 1u) ? img.w : img.h;
    std::vector<uint8_t> dst(static_cast<size_t>(nw) * nh * 4);
    for (uint32_t y = 0; y < img.h; ++y) {
        for (uint32_t x = 0; x < img.w; ++x) {
            uint32_t dx = x, dy = y;
            if (angleCode == 1)      { dx = y;              dy = img.w - 1 - x; }
            else if (angleCode == 2) { dx = img.w - 1 - x;  dy = img.h - 1 - y; }
            else if (angleCode == 3) { dx = img.h - 1 - y;  dy = x; }
            std::memcpy(&dst[(static_cast<size_t>(dy) * nw + dx) * 4],
                        &img.rgba[(static_cast<size_t>(y) * img.w + x) * 4], 4);
        }
    }
    img.w = nw; img.h = nh;
    img.rgba = std::move(dst);
}

inline bool IsSupportedHeifBrand(const char b[4]) {
    return std::memcmp(b, "avif", 4) == 0 || std::memcmp(b, "avis", 4) == 0 ||
           std::memcmp(b, "heic", 4) == 0 || std::memcmp(b, "heix", 4) == 0 ||
           std::memcmp(b, "hevc", 4) == 0 || std::memcmp(b, "heim", 4) == 0 ||
           std::memcmp(b, "heis", 4) == 0 || std::memcmp(b, "mif1", 4) == 0 ||
           std::memcmp(b, "msf1", 4) == 0 || std::memcmp(b, "miaf", 4) == 0 ||
           std::memcmp(b, "avci", 4) == 0 || std::memcmp(b, "avcs", 4) == 0;
}


inline bool IsSupportedAvifBrand(const char b[4]) {
    return std::memcmp(b, "avif", 4) == 0 || std::memcmp(b, "avis", 4) == 0;
}

} // namespace AvifHeifDetail

inline bool IsLikelyAvifOrHeifSignature(const uint8_t* data, size_t size) {
    using namespace AvifHeifDetail;
    if (!data || size < 16) return false;
    size_t cur = 0;
    IsoBox b;
    if (!NextIsoBox(data, size, cur, b) || std::strcmp(b.type, "ftyp") != 0) return false;
    if (b.end - b.bodyStart < 8) return false;
    if (IsSupportedHeifBrand(reinterpret_cast<const char*>(data + b.bodyStart))) return true;
    for (size_t p = b.bodyStart + 8; p + 4 <= b.end; p += 4) {
        if (IsSupportedHeifBrand(reinterpret_cast<const char*>(data + p))) return true;
    }
    return false;
}

inline bool DecodeAvifOrHeifImage(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ExtendedImageInfo& outInfo)
{
    using namespace AvifHeifDetail;
    if (!IsLikelyAvifOrHeifSignature(data, size)) return false;

    char majorBrand[5] = {};
    std::memcpy(majorBrand, data + 8, 4);

    // 1. Parse top-level ISO-BMFF boxes to locate 'meta'
    size_t cur = 0;
    IsoBox topBox;
    size_t metaBody = 0, metaEnd = 0;
    while (NextIsoBox(data, size, cur, topBox)) {
        if (std::strcmp(topBox.type, "meta") == 0 && topBox.end - topBox.bodyStart >= 4) {
            metaBody = topBox.bodyStart + 4; // FullBox version(1) + flags(3)
            metaEnd  = topBox.end;
            break;
        }
    }
    if (metaBody == 0 || metaBody >= metaEnd) return false;

    // 2. Parse 'meta' child boxes: hdlr, pitm, iinf, iprp (ipco + ipma), iref, idat, iloc
    bool hasPictHdlr = false;
    uint32_t primaryItemId = 0;
    bool hasPrimaryId = false;
    std::unordered_map<uint32_t, std::string> itemTypes;

    struct PropRecord {
        std::string type;
        std::vector<uint8_t> payload;
    };
    std::vector<PropRecord> props; // 1-based in ipma!
    std::unordered_map<uint32_t, std::vector<uint16_t>> itemProps;

    std::unordered_map<uint32_t, std::vector<uint32_t>> dimgRefs; // gridItem -> tileItems
    std::unordered_map<uint32_t, uint32_t> alphaItemForPrimary;   // primaryItem -> auxlAlphaItem

    const uint8_t* idatPtr = nullptr;
    size_t idatLen = 0;

    struct ItemExtent { uint64_t offset = 0, length = 0; };
    struct ItemLoc {
        uint8_t constructionMethod = 0; // 0=file, 1=idat
        uint64_t baseOffset = 0;
        std::vector<ItemExtent> extents;
    };
    std::unordered_map<uint32_t, ItemLoc> itemLocs;

    size_t mc = metaBody;
    IsoBox mb;
    while (NextIsoBox(data, metaEnd, mc, mb)) {
        size_t bs = mb.bodyStart, be = mb.end;
        if (std::strcmp(mb.type, "hdlr") == 0 && be - bs >= 12) {
            if (std::memcmp(data + bs + 8, "pict", 4) == 0) hasPictHdlr = true;
        } else if (std::strcmp(mb.type, "pitm") == 0 && be - bs >= 6) {
            uint8_t ver = data[bs];
            if (ver == 0) { primaryItemId = RdBE16(data + bs + 4); hasPrimaryId = true; }
            else if (be - bs >= 8) { primaryItemId = RdBE32(data + bs + 4); hasPrimaryId = true; }
        } else if (std::strcmp(mb.type, "idat") == 0) {
            idatPtr = data + bs;
            idatLen = be - bs;
        } else if (std::strcmp(mb.type, "iinf") == 0 && be - bs >= 6) {
            uint8_t ver = data[bs];
            size_t ic = bs + 4;
            uint32_t cnt = (ver == 0) ? RdBE16(data + ic) : ((be - ic >= 4) ? RdBE32(data + ic) : 0);
            ic += (ver == 0) ? 2 : 4;
            IsoBox infe;
            for (uint32_t i = 0; i < cnt && NextIsoBox(data, be, ic, infe); ++i) {
                if (std::strcmp(infe.type, "infe") != 0 || infe.end - infe.bodyStart < 8) continue;
                uint8_t iv = data[infe.bodyStart];
                if (iv == 2 && infe.end - infe.bodyStart >= 12) {
                    uint32_t id = RdBE16(data + infe.bodyStart + 4);
                    itemTypes[id] = std::string(reinterpret_cast<const char*>(data + infe.bodyStart + 8), 4);
                } else if (iv >= 3 && infe.end - infe.bodyStart >= 14) {
                    uint32_t id = RdBE32(data + infe.bodyStart + 4);
                    itemTypes[id] = std::string(reinterpret_cast<const char*>(data + infe.bodyStart + 10), 4);
                }
            }
        } else if (std::strcmp(mb.type, "iprp") == 0) {
            size_t pc = bs;
            IsoBox pb;
            while (NextIsoBox(data, be, pc, pb)) {
                if (std::strcmp(pb.type, "ipco") == 0) {
                    size_t cc = pb.bodyStart;
                    IsoBox cb;
                    while (NextIsoBox(data, pb.end, cc, cb)) {
                        props.push_back({ cb.type, std::vector<uint8_t>(data + cb.bodyStart, data + cb.end) });
                    }
                } else if (std::strcmp(pb.type, "ipma") == 0 && pb.end - pb.bodyStart >= 8) {
                    uint8_t ver = data[pb.bodyStart];
                    uint32_t flags = RdBE32(data + pb.bodyStart) & 0x00FFFFFFu;
                    size_t p = pb.bodyStart + 4;
                    uint32_t ecnt = RdBE32(data + p); p += 4;
                    for (uint32_t i = 0; i < ecnt && i < 65536; ++i) {
                        size_t idSz = (ver < 1) ? 2u : 4u;
                        if (p + idSz + 1 > pb.end) break;
                        uint32_t iid = (ver < 1) ? RdBE16(data + p) : RdBE32(data + p); p += idSz;
                        uint8_t assocCnt = data[p++];
                        size_t entryBytes = (flags & 1u) ? 2u : 1u;
                        if (p + assocCnt * entryBytes > pb.end) break;
                        for (uint8_t a = 0; a < assocCnt; ++a) {
                            uint16_t pidx = (flags & 1u) ? (RdBE16(data + p) & 0x7FFFu) : (data[p] & 0x7Fu);
                            p += entryBytes;
                            if (pidx > 0) itemProps[iid].push_back(pidx);
                        }
                    }
                }
            }
        } else if (std::strcmp(mb.type, "iref") == 0 && be - bs >= 4) {
            uint8_t ver = data[bs];
            size_t rc = bs + 4;
            IsoBox rb;
            while (NextIsoBox(data, be, rc, rb)) {
                size_t idSz = (ver == 0) ? 2u : 4u;
                size_t rp = rb.bodyStart;
                if (rp + idSz + 2 > rb.end) continue;
                uint32_t fromId = (ver == 0) ? RdBE16(data + rp) : RdBE32(data + rp); rp += idSz;
                uint16_t refCnt = RdBE16(data + rp); rp += 2;
                if (rp + refCnt * idSz > rb.end) continue;
                for (uint16_t k = 0; k < refCnt; ++k) {
                    uint32_t toId = (ver == 0) ? RdBE16(data + rp + k * idSz) : RdBE32(data + rp + k * idSz);
                    if (std::strcmp(rb.type, "dimg") == 0) dimgRefs[fromId].push_back(toId);
                    else if (std::strcmp(rb.type, "auxl") == 0) alphaItemForPrimary[toId] = fromId;
                }
            }
        } else if (std::strcmp(mb.type, "iloc") == 0 && be - bs >= 8) {
            uint8_t ver = data[bs];
            size_t lp = bs + 4;
            uint8_t offSz  = (data[lp] >> 4) & 0x0Fu;
            uint8_t lenSz  = data[lp] & 0x0Fu; ++lp;
            uint8_t baseSz = (data[lp] >> 4) & 0x0Fu;
            uint8_t idxSz  = (ver == 1 || ver == 2) ? (data[lp] & 0x0Fu) : 0u; ++lp;
            if (offSz > 8 || lenSz > 8 || baseSz > 8 || idxSz > 8) return false;
            uint32_t itemCnt = 0;
            if (ver < 2) { if (lp + 2 > be) return false; itemCnt = RdBE16(data + lp); lp += 2; }
            else         { if (lp + 4 > be) return false; itemCnt = RdBE32(data + lp); lp += 4; }
            for (uint32_t i = 0; i < itemCnt && i < 65536; ++i) {
                size_t idBytes = (ver < 2) ? 2u : 4u;
                if (lp + idBytes > be) break;
                uint32_t iid = (ver < 2) ? RdBE16(data + lp) : RdBE32(data + lp); lp += idBytes;
                uint8_t cMethod = 0;
                if (ver == 1 || ver == 2) {
                    if (lp + 2 > be) break;
                    cMethod = data[lp + 1] & 0x0Fu;
                    lp += 2;
                }
                if (lp + 2 + baseSz + 2 > be) break;
                lp += 2; // data_reference_index
                uint64_t baseOff = RdVarBE(data + lp, baseSz); lp += baseSz;
                uint16_t extCnt  = RdBE16(data + lp); lp += 2;
                size_t extStride = idxSz + offSz + lenSz;
                if (lp + static_cast<size_t>(extCnt) * extStride > be) break;
                ItemLoc loc;
                loc.constructionMethod = cMethod;
                loc.baseOffset = baseOff;
                for (uint16_t e = 0; e < extCnt; ++e) {
                    lp += idxSz;
                    uint64_t eo = RdVarBE(data + lp, offSz); lp += offSz;
                    uint64_t el = RdVarBE(data + lp, lenSz); lp += lenSz;
                    loc.extents.push_back({ eo, el });
                }
                itemLocs[iid] = std::move(loc);
            }
        }
    }

    if (!hasPictHdlr) return false;
    if (!hasPrimaryId && !itemTypes.empty()) primaryItemId = itemTypes.begin()->first;
    if (itemTypes.empty() && itemLocs.empty()) return false;

    // Helper to extract contiguous item bytes from iloc
    auto extractItemBytes = [&](uint32_t iid, std::vector<uint8_t>& outBytes) -> bool {
        outBytes.clear();
        auto it = itemLocs.find(iid);
        if (it == itemLocs.end() || it->second.extents.empty()) return false;
        const ItemLoc& loc = it->second;
        for (const auto& ex : loc.extents) {
            uint64_t absOff = loc.baseOffset + ex.offset;
            if (ex.length > 128 * 1024 * 1024) return false;
            if (loc.constructionMethod == 0) {
                if (absOff > size || ex.length > size - absOff) return false;
                outBytes.insert(outBytes.end(), data + absOff, data + absOff + static_cast<size_t>(ex.length));
            } else if (loc.constructionMethod == 1) {
                if (!idatPtr || absOff > idatLen || ex.length > idatLen - absOff) return false;
                outBytes.insert(outBytes.end(), idatPtr + absOff, idatPtr + absOff + static_cast<size_t>(ex.length));
            } else {
                return false;
            }
        }
        return !outBytes.empty();
    };

    // Decode via pure-C++ container + item decoders first; fall back to Windows WIC for complex OS-codec frames
    DecodedRgbaTile finalImg;
    {
        // Gather item properties helper
        struct ResolvedProps {
            uint32_t ispeW = 0, ispeH = 0;
            bool fullRange = true;
            uint16_t matrixCoeffs = 6; // BT.601 default
            std::vector<uint8_t> av1C, hvcC, avcC;
            UncConfig unc;
            std::vector<std::pair<char, uint8_t>> xforms; // ('m', axis) or ('r', angle)
        };

        auto getPropsFor = [&](uint32_t iid) -> ResolvedProps {
            ResolvedProps rp;
            auto it = itemProps.find(iid);
            if (it == itemProps.end()) return rp;
            for (uint16_t pidx : it->second) {
                if (pidx == 0 || pidx > props.size()) continue;
                const auto& pr = props[pidx - 1];
                const auto& pl = pr.payload;
                if (pr.type == "ispe" && pl.size() >= 12) {
                    rp.ispeW = RdBE32(pl.data() + 4);
                    rp.ispeH = RdBE32(pl.data() + 8);
                } else if (pr.type == "colr" && pl.size() >= 11 && std::memcmp(pl.data(), "nclx", 4) == 0) {
                    rp.matrixCoeffs = RdBE16(pl.data() + 8);
                    rp.fullRange    = (pl[10] & 0x80u) != 0;
                } else if (pr.type == "av1C") rp.av1C = pl;
                else if (pr.type == "hvcC")   rp.hvcC = pl;
                else if (pr.type == "avcC")   rp.avcC = pl;
                else if (pr.type == "cmpd" && pl.size() >= 4) {
                    uint32_t n = RdBE32(pl.data());
                    size_t p = 4;
                    for (uint32_t c = 0; c < n && p + 2 <= pl.size(); ++c) {
                        uint16_t ct = RdBE16(pl.data() + p); p += 2;
                        if (ct >= 0x8000u) { while (p < pl.size() && pl[p] != 0) ++p; if (p < pl.size()) ++p; }
                        rp.unc.compTypes.push_back(ct);
                    }
                } else if (pr.type == "uncC" && pl.size() >= 8) {
                    uint8_t ver = pl[0];
                    rp.unc.present = true;
                    rp.unc.profile = RdBE32(pl.data() + 4);
                    if (ver == 1) {
                        // ISO 23001-17 uncC v1 profile shorthand ('rgb3', 'rgba', 'yuv2')
                        if (std::memcmp(pl.data() + 4, "rgb3", 4) == 0) {
                            rp.unc.compTypes = { 4, 5, 6 }; rp.unc.compBitDepths = { 8, 8, 8 }; rp.unc.interleaveType = 1;
                        } else if (std::memcmp(pl.data() + 4, "rgba", 4) == 0) {
                            rp.unc.compTypes = { 4, 5, 6, 7 }; rp.unc.compBitDepths = { 8, 8, 8, 8 }; rp.unc.interleaveType = 1;
                        }
                    } else if (pl.size() >= 12) {
                        uint32_t nComp = RdBE32(pl.data() + 8);
                        size_t p = 12;
                        for (uint32_t c = 0; c < nComp && p + 4 <= pl.size(); ++c) {
                            uint8_t bdm1 = pl[p + 2];
                            rp.unc.compBitDepths.push_back(static_cast<uint8_t>(bdm1 + 1u));
                            p += 4;
                        }
                        if (p + 2 <= pl.size()) {
                            rp.unc.interleaveType = pl[p + 1];
                        }
                    }
                } else if (pr.type == "imir" && !pl.empty()) {
                    rp.xforms.push_back({ 'm', static_cast<uint8_t>(pl[0] & 1u) });
                } else if (pr.type == "irot" && !pl.empty()) {
                    rp.xforms.push_back({ 'r', static_cast<uint8_t>(pl[0] & 3u) });
                }
            }
            return rp;
        };

        std::function<bool(uint32_t, DecodedRgbaTile&, int)> decodeItemRecursive =
            [&](uint32_t iid, DecodedRgbaTile& dst, int depth) -> bool
        {
            if (depth > 4) return false;
            std::string itype = itemTypes[iid];
            ResolvedProps rp = getPropsFor(iid);

            bool ok = false;
            if (itype == "grid") {
                std::vector<uint8_t> gBytes;
                if (!extractItemBytes(iid, gBytes) || gBytes.size() < 8) return false;
                uint8_t flags = gBytes[1];
                uint32_t rows = static_cast<uint32_t>(gBytes[2]) + 1u;
                uint32_t cols = static_cast<uint32_t>(gBytes[3]) + 1u;
                uint32_t outW = 0, outH = 0;
                if ((flags & 1u) != 0) {
                    if (gBytes.size() < 12) return false;
                    outW = RdBE32(gBytes.data() + 4);
                    outH = RdBE32(gBytes.data() + 8);
                } else {
                    outW = RdBE16(gBytes.data() + 4);
                    outH = RdBE16(gBytes.data() + 6);
                }
                const auto& tiles = dimgRefs[iid];
                if (outW == 0 || outH == 0 || outW > 16384 || outH > 16384 || tiles.size() < static_cast<size_t>(rows) * cols) {
                    return false;
                }
                DecodedRgbaTile t0;
                if (!decodeItemRecursive(tiles[0], t0, depth + 1) || t0.w == 0 || t0.h == 0) return false;
                dst.w = outW; dst.h = outH;
                dst.rgba.assign(static_cast<size_t>(outW) * outH * 4, 0);

                for (uint32_t r = 0; r < rows; ++r) {
                    for (uint32_t c = 0; c < cols; ++c) {
                        DecodedRgbaTile curTile;
                        if (r == 0 && c == 0) curTile = std::move(t0);
                        else if (!decodeItemRecursive(tiles[r * cols + c], curTile, depth + 1)) return false;

                        uint32_t x0 = c * curTile.w, y0 = r * curTile.h;
                        for (uint32_t ty = 0; ty < curTile.h && y0 + ty < outH; ++ty) {
                            uint32_t copyW = std::min(curTile.w, outW - std::min(outW, x0));
                            if (copyW > 0) {
                                std::memcpy(&dst.rgba[(static_cast<size_t>(y0 + ty) * outW + x0) * 4],
                                            &curTile.rgba[static_cast<size_t>(ty) * curTile.w * 4],
                                            static_cast<size_t>(copyW) * 4);
                            }
                        }
                    }
                }
                ok = true;
            } else if (itype == "iden" && !dimgRefs[iid].empty()) {
                ok = decodeItemRecursive(dimgRefs[iid][0], dst, depth + 1);
            } else {
                std::vector<uint8_t> pay;
                if (!extractItemBytes(iid, pay)) return false;

                if (itype == "unci" || rp.unc.present) {
                    ok = DecodeUncompressedHeifItem(pay.data(), pay.size(), rp.ispeW, rp.ispeH, rp.unc, rp.fullRange, rp.matrixCoeffs, dst);
                } else if (itype == "jpeg") {
                    ok = TryDecodeViaWicMemory(pay.data(), pay.size(), dst);
                } else if (itype == "avc1") {
                    ok = DecodeAvc1StillItem(pay.data(), pay.size(), rp.avcC, rp.fullRange, rp.matrixCoeffs, dst);
                } else if (itype == "hvc1") {
                    ok = DecodeHvc1StillItem(pay.data(), pay.size(), rp.hvcC, rp.fullRange, rp.matrixCoeffs, dst);
                } else if (itype == "av01") {
                    ok = DecodeAv01StillItem(pay.data(), pay.size(), rp.av1C, rp.fullRange, rp.matrixCoeffs, dst);
                }
            }
            if (!ok) return false;

            // Crop to ispe if coded macroblock/CTU padded slightly larger than ispe
            if (rp.ispeW > 0 && rp.ispeH > 0 && rp.ispeW <= dst.w && rp.ispeH <= dst.h &&
                (rp.ispeW != dst.w || rp.ispeH != dst.h))
            {
                std::vector<uint8_t> cropped(static_cast<size_t>(rp.ispeW) * rp.ispeH * 4);
                for (uint32_t y = 0; y < rp.ispeH; ++y) {
                    std::memcpy(&cropped[static_cast<size_t>(y) * rp.ispeW * 4],
                                &dst.rgba[static_cast<size_t>(y) * dst.w * 4],
                                static_cast<size_t>(rp.ispeW) * 4);
                }
                dst.w = rp.ispeW; dst.h = rp.ispeH;
                dst.rgba = std::move(cropped);
            }

            // Apply transformative properties (imir, irot) in exact ipma association order!
            for (const auto& xf : rp.xforms) {
                if (xf.first == 'm') ApplyMirrorRgba(dst, xf.second);
                else if (xf.first == 'r') ApplyRotateCcwRgba(dst, xf.second);
            }
            return true;
        };

        if (decodeItemRecursive(primaryItemId, finalImg, 0)) {
            // Composite auxiliary alpha item ('auxl') if present
            auto aIt = alphaItemForPrimary.find(primaryItemId);
            if (aIt != alphaItemForPrimary.end()) {
                DecodedRgbaTile alphaTile;
                if (decodeItemRecursive(aIt->second, alphaTile, 0) &&
                    alphaTile.w == finalImg.w && alphaTile.h == finalImg.h)
                {
                    size_t nPx = static_cast<size_t>(finalImg.w) * finalImg.h;
                    for (size_t i = 0; i < nPx; ++i) {
                        finalImg.rgba[i * 4 + 3] = alphaTile.rgba[i * 4 + 0];
                    }
                }
            }
        } else if (!TryDecodeViaWicMemory(data, size, finalImg)) {
            return false;
        }
    }

    if (finalImg.w == 0 || finalImg.h == 0 || finalImg.rgba.size() < static_cast<size_t>(finalImg.w) * finalImg.h * 4) {
        return false;
    }

    // Convert RGBA8 -> premultiplied BGRA8 for Direct2D
    size_t nPx = static_cast<size_t>(finalImg.w) * finalImg.h;
    outInfo.width = finalImg.w;
    outInfo.height = finalImg.h;
    outInfo.origWidth = finalImg.w;
    outInfo.origHeight = finalImg.h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.bgraPixels.resize(nPx * 4);

    for (size_t i = 0; i < nPx; ++i) {
        uint8_t r = finalImg.rgba[i * 4 + 0];
        uint8_t g = finalImg.rgba[i * 4 + 1];
        uint8_t b = finalImg.rgba[i * 4 + 2];
        uint8_t a = finalImg.rgba[i * 4 + 3];
        WritePremulBGRA(&outInfo.bgraPixels[i * 4], r, g, b, a);
    }

    if (extLower == L".avif" || std::memcmp(majorBrand, "avif", 4) == 0 || std::memcmp(majorBrand, "avis", 4) == 0) {
        outInfo.formatName = L"AV1 Image File Format (.avif)";
    } else if (extLower == L".avci" || std::memcmp(majorBrand, "avci", 4) == 0) {
        outInfo.formatName = L"AVC in HEIF Still Image (.avci)";
    } else if (extLower == L".heic" || std::memcmp(majorBrand, "heic", 4) == 0 || std::memcmp(majorBrand, "heix", 4) == 0) {
        outInfo.formatName = L"HEVC High Efficiency Image (.heic)";
    } else {
        outInfo.formatName = L"ISO High Efficiency Image Container (.heif)";
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
