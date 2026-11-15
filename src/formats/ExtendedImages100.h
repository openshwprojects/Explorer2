#pragma once
// ExtendedImages100.h — 18 Popular Graphics, Texture, Design, Cinema & Font Specimen Decoders
// Covers: .exr, .ktx2, .basis, .kra, .ora, .clip, .procreate, .aseprite, .icns,
//         .jxl, .bpg, .blp, .utx, .dpx, .cin, .aco, .cube, .3dl, .ies, .ttf, .otf, .woff, .woff2

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

// ============================================================================
// 1. OpenEXR (.exr) — Magic 0x76, 0x2F, 0x31, 0x01
//    Supports pixel types UINT (0), HALF (1), FLOAT (2) and compressions
//    NO_COMPRESSION (0), RLE_COMPRESSION (1), ZIPS_COMPRESSION (2), ZIP_COMPRESSION (3)
// ============================================================================
inline bool DecodeOpenEXR(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 32 || data[0] != 0x76 || data[1] != 0x2F || data[2] != 0x31 || data[3] != 0x01) {
        return false;
    }
    uint32_t verFlags = DetailImg100::RdU32LE(data + 4);
    uint32_t version = verFlags & 0xFFu;
    if (version != 2 && version != 1) return false;
    bool isTiled = (verFlags & 0x0200u) != 0;
    bool isMultiPart = (verFlags & 0x1000u) != 0;
    bool isDeep = (verFlags & 0x0800u) != 0;
    if (isDeep) return false;

    struct ExrChannel {
        std::string name;
        int32_t pixelType = 1; // 0 = UINT, 1 = HALF, 2 = FLOAT
        int32_t xSampling = 1;
        int32_t ySampling = 1;
        uint32_t bytesPerSample = 2;
    };

    int32_t minX = 0, minY = 0, maxX = -1, maxY = -1;
    bool hasDataWindow = false;
    bool hasDisplayWindow = false;
    uint8_t compression = 0xFF;
    uint32_t tileDescX = 64, tileDescY = 64;
    std::vector<ExrChannel> channels;
    std::vector<std::wstring> chNames;

    size_t pos = 8;
    bool headerClosed = false;
    while (pos < size) {
        if (data[pos] == 0) {
            ++pos;
            headerClosed = true;
            break;
        }
        size_t nameEnd = pos;
        while (nameEnd < size && data[nameEnd] != 0) ++nameEnd;
        if (nameEnd >= size) return false;
        std::string attrName(reinterpret_cast<const char*>(data + pos), nameEnd - pos);
        pos = nameEnd + 1;

        size_t typeEnd = pos;
        while (typeEnd < size && data[typeEnd] != 0) ++typeEnd;
        if (typeEnd + 5 > size) return false;
        pos = typeEnd + 1;

        uint32_t attrSize = DetailImg100::RdU32LE(data + pos);
        pos += 4;
        if (attrSize > size - pos) return false;

        if (attrName == "dataWindow" && attrSize == 16) {
            minX = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 0));
            minY = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 4));
            maxX = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 8));
            maxY = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 12));
            hasDataWindow = true;
        } else if (attrName == "displayWindow" && attrSize == 16 && !hasDataWindow) {
            minX = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 0));
            minY = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 4));
            maxX = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 8));
            maxY = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 12));
            hasDisplayWindow = true;
        } else if (attrName == "compression" && attrSize >= 1) {
            compression = data[pos];
        } else if (attrName == "tiles" && attrSize >= 8) {
            tileDescX = DetailImg100::RdU32LE(data + pos + 0);
            tileDescY = DetailImg100::RdU32LE(data + pos + 4);
        } else if (attrName == "channels" && attrSize >= 1) {
            size_t cpos = pos;
            size_t cend = pos + attrSize;
            while (cpos < cend && data[cpos] != 0) {
                size_t ce = cpos;
                while (ce < cend && data[ce] != 0) ++ce;
                if (ce + 1 + 16 > cend) return false;
                ExrChannel ch;
                ch.name.assign(reinterpret_cast<const char*>(data + cpos), ce - cpos);
                const uint8_t* meta = data + ce + 1;
                ch.pixelType = static_cast<int32_t>(DetailImg100::RdU32LE(meta + 0));
                ch.xSampling = std::max(1, static_cast<int32_t>(DetailImg100::RdU32LE(meta + 8)));
                ch.ySampling = std::max(1, static_cast<int32_t>(DetailImg100::RdU32LE(meta + 12)));
                if (ch.pixelType == 0 || ch.pixelType == 2) ch.bytesPerSample = 4;
                else if (ch.pixelType == 1) ch.bytesPerSample = 2;
                else return false;
                chNames.push_back(PathUtils::Utf8ToWide(ch.name));
                channels.push_back(std::move(ch));
                cpos = ce + 1 + 16;
            }
        }
        pos += attrSize;
    }

    if (!headerClosed || (!hasDataWindow && !hasDisplayWindow) || channels.empty()) return false;
    if (compression == 0xFF) compression = 0;
    if (compression > 3) return false; // Only NO (0), RLE (1), ZIPS (2), ZIP (3) supported natively

    // Skip remaining part headers in multi-part EXR until the empty-header terminator 0x00
    if (isMultiPart) {
        while (pos < size) {
            if (data[pos] == 0) { ++pos; break; }
            while (pos < size && data[pos] != 0) {
                size_t ne = pos;
                while (ne < size && data[ne] != 0) ++ne;
                if (ne >= size) return false;
                size_t te = ne + 1;
                while (te < size && data[te] != 0) ++te;
                if (te + 5 > size) return false;
                uint32_t asz = DetailImg100::RdU32LE(data + te + 1);
                pos = te + 5;
                if (asz > size - pos) return false;
                pos += asz;
            }
            if (pos >= size) return false;
            ++pos; // End of this secondary part header
        }
    }

    int64_t spanX = static_cast<int64_t>(maxX) - static_cast<int64_t>(minX) + 1;
    int64_t spanY = static_cast<int64_t>(maxY) - static_cast<int64_t>(minY) + 1;
    if (spanX < 1 || spanX > 8192 || spanY < 1 || spanY > 8192) return false;
    uint32_t w = static_cast<uint32_t>(spanX);
    uint32_t h = static_cast<uint32_t>(spanY);

    // Map channels to RGBA
    int idxR = -1, idxG = -1, idxB = -1, idxA = -1, idxY = -1;
    auto matchSuffix = [](const std::string& s, char target) -> bool {
        if (s.size() == 1 && (s[0] == target || s[0] == (target - 'A' + 'a'))) return true;
        if (s.size() >= 2 && s[s.size() - 2] == '.') {
            char c = s.back();
            if (c == target || c == (target - 'A' + 'a')) return true;
        }
        return false;
    };
    for (int i = 0; i < static_cast<int>(channels.size()); ++i) {
        const std::string& cn = channels[i].name;
        if (cn == "R" || cn == "r") idxR = i;
        else if (cn == "G" || cn == "g") idxG = i;
        else if (cn == "B" || cn == "b") idxB = i;
        else if (cn == "A" || cn == "a") idxA = i;
        else if (cn == "Y" || cn == "y" || cn == "L" || cn == "Z") { if (idxY < 0) idxY = i; }
    }
    for (int i = 0; i < static_cast<int>(channels.size()); ++i) {
        const std::string& cn = channels[i].name;
        if (idxR < 0 && matchSuffix(cn, 'R')) idxR = i;
        if (idxG < 0 && matchSuffix(cn, 'G')) idxG = i;
        if (idxB < 0 && matchSuffix(cn, 'B')) idxB = i;
        if (idxA < 0 && matchSuffix(cn, 'A')) idxA = i;
        if (idxY < 0 && matchSuffix(cn, 'Y')) idxY = i;
    }
    if (idxR < 0 && idxG < 0 && idxB < 0) {
        if (idxY >= 0) {
            idxR = idxG = idxB = idxY;
        } else if (channels.size() >= 3) {
            idxR = 0; idxG = 1; idxB = 2;
        } else if (channels.size() == 2) {
            idxR = 0; idxG = 1; idxB = 1;
        } else {
            idxR = idxG = idxB = 0;
        }
    } else {
        if (idxR < 0) idxR = (idxG >= 0) ? idxG : idxB;
        if (idxG < 0) idxG = idxR;
        if (idxB < 0) idxB = idxG;
    }

    auto undoPredictorAndInterleave = [](std::vector<uint8_t>& temp, std::vector<uint8_t>& rawOut) {
        size_t n = temp.size();
        for (size_t i = 1; i < n; ++i) {
            temp[i] = static_cast<uint8_t>(static_cast<int>(temp[i - 1]) + static_cast<int>(temp[i]) - 128);
        }
        rawOut.resize(n);
        size_t half = (n + 1) / 2;
        for (size_t j = 0; j < n / 2; ++j) {
            rawOut[2 * j]     = temp[j];
            rawOut[2 * j + 1] = temp[half + j];
        }
        if ((n & 1u) != 0) {
            rawOut[n - 1] = temp[half - 1];
        }
    };

    auto decompressExrBlock = [&](const uint8_t* packed, size_t packedSize, size_t expectedSize, std::vector<uint8_t>& rawOut) -> bool {
        if (expectedSize == 0) {
            rawOut.clear();
            return true;
        }
        if (compression == 0) {
            if (packedSize < expectedSize) return false;
            rawOut.assign(packed, packed + expectedSize);
            return true;
        }
        // In OpenEXR, if compressed block size >= expectedSize, IlmImf stores the block uncompressed (`packedSize == expectedSize`)
        std::vector<uint8_t> temp;
        if (compression == 1) { // RLE_COMPRESSION
            if (packedSize != expectedSize) {
                temp.reserve(expectedSize);
                size_t sp = 0;
                while (sp < packedSize && temp.size() < expectedSize) {
                    int count = static_cast<int>(static_cast<int8_t>(packed[sp++]));
                    if (count < 0) {
                        size_t lit = static_cast<size_t>(-count);
                        if (sp + lit > packedSize || temp.size() + lit > expectedSize) { temp.clear(); break; }
                        temp.insert(temp.end(), packed + sp, packed + sp + lit);
                        sp += lit;
                    } else {
                        size_t rep = static_cast<size_t>(count) + 1u;
                        if (sp >= packedSize || temp.size() + rep > expectedSize) { temp.clear(); break; }
                        uint8_t val = packed[sp++];
                        temp.insert(temp.end(), rep, val);
                    }
                }
                if (temp.size() == expectedSize) {
                    undoPredictorAndInterleave(temp, rawOut);
                    return true;
                }
            }
            if (packedSize == expectedSize) {
                rawOut.assign(packed, packed + expectedSize);
                return true;
            }
            return false;
        }
        if (compression == 2 || compression == 3) { // ZIPS / ZIP
            if (ZipDeflate::InflateZlib(packed, packedSize, temp, expectedSize + 64) && temp.size() == expectedSize) {
                undoPredictorAndInterleave(temp, rawOut);
                return true;
            }
            if (packedSize == expectedSize) {
                rawOut.assign(packed, packed + expectedSize);
                return true;
            }
            return false;
        }
        return false;
    };

    std::vector<float> floatRgba(static_cast<size_t>(w) * h * 4, 0.0f);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) floatRgba[i * 4 + 3] = 1.0f;
    bool hasUintChannel = false;
    float maxUintVal = 1.0f;
    for (const auto& ch : channels) {
        if (ch.pixelType == 0) hasUintChannel = true;
    }

    auto readSampleAsFloat = [&](int32_t pixelType, const uint8_t* sp) -> float {
        if (pixelType == 1) return GpuTex::HalfToFloat(DetailImg100::RdU16LE(sp));
        if (pixelType == 2) return GpuTex::ReadF32LE(sp);
        uint32_t u = DetailImg100::RdU32LE(sp);
        float fv = static_cast<float>(u);
        if (fv > maxUintVal) maxUintVal = fv;
        return fv;
    };

    if (isTiled) {
        if (tileDescX == 0 || tileDescY == 0 || tileDescX > 4096 || tileDescY > 4096) return false;
        uint32_t tilesX = (w + tileDescX - 1u) / tileDescX;
        uint32_t tilesY = (h + tileDescY - 1u) / tileDescY;
        size_t numTiles = static_cast<size_t>(tilesX) * tilesY;
        if (numTiles == 0 || numTiles > 65536 || pos + numTiles * 8 > size) return false;

        size_t seqOff = pos + numTiles * 8;
        std::vector<uint8_t> rawBlock;
        size_t decodedTiles = 0;
        for (size_t ti = 0; ti < numTiles; ++ti) {
            uint64_t cOff64 = DetailImg100::RdU64LE(data + pos + ti * 8);
            size_t cOff = (cOff64 >= pos + numTiles * 8 && cOff64 + 20 <= size) ? static_cast<size_t>(cOff64) : seqOff;
            if (cOff + 20 > size) return false;
            if (isMultiPart) {
                if (cOff + 24 > size) return false;
                cOff += 4;
            }
            uint32_t tx = DetailImg100::RdU32LE(data + cOff + 0);
            uint32_t ty = DetailImg100::RdU32LE(data + cOff + 4);
            uint32_t pSize = DetailImg100::RdU32LE(data + cOff + 16);
            if (pSize > size - (cOff + 20)) return false;
            seqOff = cOff + 20 + pSize;
            if (tx >= tilesX || ty >= tilesY) continue;

            uint32_t x0 = tx * tileDescX;
            uint32_t y0 = ty * tileDescY;
            uint32_t tw = std::min(tileDescX, w - x0);
            uint32_t th = std::min(tileDescY, h - y0);
            size_t expected = 0;
            for (uint32_t ly = 0; ly < th; ++ly) {
                for (const auto& ch : channels) {
                    expected += static_cast<size_t>((tw + ch.xSampling - 1) / ch.xSampling) * ch.bytesPerSample;
                }
            }
            if (!decompressExrBlock(data + cOff + 20, pSize, expected, rawBlock)) return false;
            size_t rp = 0;
            for (uint32_t ly = 0; ly < th; ++ly) {
                uint32_t dstY = y0 + ly;
                for (int ci = 0; ci < static_cast<int>(channels.size()); ++ci) {
                    const auto& ch = channels[ci];
                    uint32_t cw = (tw + ch.xSampling - 1) / ch.xSampling;
                    for (uint32_t lx = 0; lx < cw; ++lx) {
                        float val = readSampleAsFloat(ch.pixelType, rawBlock.data() + rp);
                        rp += ch.bytesPerSample;
                        for (int sx = 0; sx < ch.xSampling && lx * ch.xSampling + sx < tw; ++sx) {
                            uint32_t dstX = x0 + lx * ch.xSampling + sx;
                            float* px = &floatRgba[(static_cast<size_t>(dstY) * w + dstX) * 4];
                            if (ci == idxR) px[0] = val;
                            if (ci == idxG) px[1] = val;
                            if (ci == idxB) px[2] = val;
                            if (ci == idxA) px[3] = val;
                        }
                    }
                }
            }
            ++decodedTiles;
        }
        if (decodedTiles == 0) return false;
    } else {
        uint32_t linesPerBlock = (compression == 3) ? 16u : 1u;
        uint32_t numBlocks = (h + linesPerBlock - 1u) / linesPerBlock;
        if (numBlocks == 0 || static_cast<size_t>(numBlocks) * 8 > size - pos) return false;

        size_t seqOff = pos + static_cast<size_t>(numBlocks) * 8;
        std::vector<uint8_t> rawBlock;
        uint32_t decodedLines = 0;

        for (uint32_t b = 0; b < numBlocks; ++b) {
            uint64_t cOff64 = DetailImg100::RdU64LE(data + pos + static_cast<size_t>(b) * 8);
            size_t cOff = (cOff64 >= pos + static_cast<size_t>(numBlocks) * 8 && cOff64 + 8 <= size)
                ? static_cast<size_t>(cOff64)
                : seqOff;
            if (cOff + 8 > size) return false;
            if (isMultiPart) {
                if (cOff + 12 > size) return false;
                cOff += 4;
            }
            int32_t chunkY = static_cast<int32_t>(DetailImg100::RdU32LE(data + cOff + 0));
            uint32_t pSize = DetailImg100::RdU32LE(data + cOff + 4);
            if (pSize > size - (cOff + 8)) return false;
            seqOff = cOff + 8 + pSize;

            int64_t relY = static_cast<int64_t>(chunkY) - static_cast<int64_t>(minY);
            uint32_t startY = (relY >= 0 && relY < static_cast<int64_t>(h))
                ? static_cast<uint32_t>(relY)
                : (b * linesPerBlock);
            if (startY >= h) return false;
            uint32_t blockH = std::min(linesPerBlock, h - startY);

            size_t expected = 0;
            for (uint32_t ly = 0; ly < blockH; ++ly) {
                int32_t absY = minY + static_cast<int32_t>(startY + ly);
                for (const auto& ch : channels) {
                    if ((absY % ch.ySampling) == 0) {
                        uint32_t cw = (w + static_cast<uint32_t>(ch.xSampling) - 1u) / static_cast<uint32_t>(ch.xSampling);
                        expected += static_cast<size_t>(cw) * ch.bytesPerSample;
                    }
                }
            }

            if (!decompressExrBlock(data + cOff + 8, pSize, expected, rawBlock)) {
                return false;
            }

            size_t rp = 0;
            for (uint32_t ly = 0; ly < blockH; ++ly) {
                uint32_t dstY = startY + ly;
                int32_t absY = minY + static_cast<int32_t>(dstY);
                for (int ci = 0; ci < static_cast<int>(channels.size()); ++ci) {
                    const auto& ch = channels[ci];
                    if ((absY % ch.ySampling) != 0) continue;
                    uint32_t cw = (w + static_cast<uint32_t>(ch.xSampling) - 1u) / static_cast<uint32_t>(ch.xSampling);
                    for (uint32_t lx = 0; lx < cw; ++lx) {
                        float val = readSampleAsFloat(ch.pixelType, rawBlock.data() + rp);
                        rp += ch.bytesPerSample;
                        for (int sy = 0; sy < ch.ySampling && dstY + static_cast<uint32_t>(sy) < h; ++sy) {
                            for (int sx = 0; sx < ch.xSampling && lx * static_cast<uint32_t>(ch.xSampling) + static_cast<uint32_t>(sx) < w; ++sx) {
                                uint32_t dstX = lx * static_cast<uint32_t>(ch.xSampling) + static_cast<uint32_t>(sx);
                                float* px = &floatRgba[(static_cast<size_t>(dstY + sy) * w + dstX) * 4];
                                if (ci == idxR) px[0] = val;
                                if (ci == idxG) px[1] = val;
                                if (ci == idxB) px[2] = val;
                                if (ci == idxA) px[3] = val;
                            }
                        }
                    }
                }
                ++decodedLines;
            }
        }
        if (decodedLines == 0) return false;
    }

    if (hasUintChannel && maxUintVal > 1.0f) {
        float invScale = 1.0f / maxUintVal;
        for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
            if (idxR >= 0 && channels[idxR].pixelType == 0) floatRgba[i * 4 + 0] *= invScale;
            if (idxG >= 0 && channels[idxG].pixelType == 0) floatRgba[i * 4 + 1] *= invScale;
            if (idxB >= 0 && channels[idxB].pixelType == 0) floatRgba[i * 4 + 2] *= invScale;
            if (idxA >= 0 && channels[idxA].pixelType == 0) floatRgba[i * 4 + 3] *= invScale;
        }
    }

    uint32_t bitsPerCh = 16;
    for (const auto& ch : channels) {
        if (ch.pixelType == 0 || ch.pixelType == 2) { bitsPerCh = 32; break; }
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = static_cast<uint32_t>(channels.size());
    outInfo.bitsPerChannel = bitsPerCh;
    outInfo.layerNames = std::move(chNames);
    outInfo.formatName = L"OpenEXR " + std::to_wstring(bitsPerCh) + L"-bit HDR (.exr)";
    GpuTex::ConvertFloatRgbaToBgra(floatRgba.data(), w, h, false, outInfo.bgraPixels);
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 2. Khronos KTX 2.0 (.ktx2) & Basis Universal (.basis)
// ============================================================================
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

        std::vector<uint8_t> inflatedMip;
        const uint8_t* mipPtr = nullptr;
        size_t mipLen = 0;
        if (supercomp == 0) {
            mipPtr = data + static_cast<size_t>(byteOff);
            mipLen = static_cast<size_t>(byteLen);
        } else if (supercomp == 3) { // KTX_SS_ZLIB
            if (uncompLen == 0 || uncompLen > 256ULL * 1024 * 1024) return false;
            if (!ZipDeflate::InflateZlib(data + static_cast<size_t>(byteOff), static_cast<size_t>(byteLen),
                                         inflatedMip, static_cast<size_t>(uncompLen) + 64) ||
                inflatedMip.size() < static_cast<size_t>(uncompLen)) {
                return false;
            }
            mipPtr = inflatedMip.data();
            mipLen = inflatedMip.size();
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

        if (vkFormat >= 9 && vkFormat <= 15) { // R8
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
        } else if (vkFormat >= 147 && vkFormat <= 150) { // ETC2 RGB8 (147,148) & ETC2 RGB8A1 (149,150)
            if (mipLen < blocks4x4 * 8) return false;
            outCh = (vkFormat >= 149) ? 4u : 3u;
            outInfo.bgraPixels.assign(pxCount * 4, 0);
            uint8_t blk[64];
            for (uint32_t by = 0; by < bh4; ++by) {
                for (uint32_t bx = 0; bx < bw4; ++bx) {
                    GpuTex::DecodeETC2RgbBlock(mipPtr + (static_cast<size_t>(by) * bw4 + bx) * 8, blk, true);
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                }
            }
            fmtDesc = L"Khronos KTX 2.0 ETC2 RGB8 (.ktx2)";
        } else if (vkFormat == 151 || vkFormat == 152 || vkFormat == 155 || vkFormat == 156) { // ETC2 RGBA8 (151,152) / EAC RG11 (155,156)
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
    uint16_t hdrVer = DetailImg100::RdU16LE(data + 2);
    uint16_t hdrSize = DetailImg100::RdU16LE(data + 4);
    uint32_t dataSize = DetailImg100::RdU32LE(data + 8);
    uint32_t totalSlices = DetailImg100::RdU24LE(data + 14);
    uint32_t totalImages = DetailImg100::RdU24LE(data + 17);
    uint8_t texFormat = data[20]; // 0 = ETC1S, 1 = UASTC 4x4
    uint16_t flags = DetailImg100::RdU16LE(data + 21);
    uint32_t epCbOfs = DetailImg100::RdU32LE(data + 41);
    uint32_t epCbSize = DetailImg100::RdU24LE(data + 45);
    uint32_t selCbOfs = DetailImg100::RdU32LE(data + 50);
    uint32_t selCbSize = DetailImg100::RdU24LE(data + 54);
    uint32_t sliceDescOfs = DetailImg100::RdU32LE(data + 65);

    if (hdrVer == 0 || hdrSize < 77 || hdrSize > size || totalSlices == 0 || totalImages == 0 || dataSize == 0) {
        return false;
    }
    if (sliceDescOfs < hdrSize || sliceDescOfs > size || 23u > size - sliceDescOfs) {
        return false;
    }
    if (epCbSize > 0 && (epCbOfs > size || epCbSize > size - epCbOfs)) return false;
    if (selCbSize > 0 && (selCbOfs > size || selCbSize > size - selCbOfs)) return false;

    // Pick first mip-0 slice descriptor (23 bytes per basis_slice_desc)
    const uint8_t* chosenSlice = data + sliceDescOfs;
    size_t maxDescCheck = std::min<size_t>(totalSlices, (size - sliceDescOfs) / 23u);
    for (size_t si = 0; si < maxDescCheck; ++si) {
        const uint8_t* sd = data + sliceDescOfs + si * 23u;
        if (sd[3] == 0) { chosenSlice = sd; break; } // m_level_index == 0
    }

    uint32_t origW = DetailImg100::RdU16LE(chosenSlice + 5);
    uint32_t origH = DetailImg100::RdU16LE(chosenSlice + 7);
    uint32_t blocksX = DetailImg100::RdU16LE(chosenSlice + 9);
    uint32_t blocksY = DetailImg100::RdU16LE(chosenSlice + 11);
    uint32_t sliceOfs = DetailImg100::RdU32LE(chosenSlice + 13);
    uint32_t sliceSize = DetailImg100::RdU32LE(chosenSlice + 17);

    if (origW == 0 || origH == 0 || origW > 8192 || origH > 8192 || blocksX == 0 || blocksY == 0) {
        return false;
    }
    if (sliceSize == 0 || sliceOfs < hdrSize || sliceOfs > size || sliceSize > size - sliceOfs) {
        return false;
    }

    const uint8_t* sliceBytes = data + sliceOfs;
    const uint8_t* epBytes = (epCbSize > 0) ? (data + epCbOfs) : sliceBytes;
    size_t epLen = (epCbSize > 0) ? epCbSize : sliceSize;
    const uint8_t* selBytes = (selCbSize > 0) ? (data + selCbOfs) : sliceBytes;
    size_t selLen = (selCbSize > 0) ? selCbSize : sliceSize;

    // If UASTC 4x4 uncompressed slice (16 bytes per block), decode 4x4 endpoints/weights directly
    size_t totalBlocks = static_cast<size_t>(blocksX) * blocksY;
    outInfo.width = origW;
    outInfo.height = origH;
    outInfo.origWidth = origW;
    outInfo.origHeight = origH;
    outInfo.channels = ((flags & 0x02u) != 0) ? 4u : 3u;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = (texFormat == 1)
        ? L"Basis Universal UASTC 4x4 (.basis)"
        : L"Basis Universal ETC1S (.basis)";
    outInfo.bgraPixels.assign(static_cast<size_t>(origW) * origH * 4, 0);

    size_t sliceBits = static_cast<size_t>(sliceSize) * 8u;
    auto sampleBit = [&](size_t bitIdx) -> uint32_t {
        size_t bi = bitIdx % sliceBits;
        return (sliceBytes[bi >> 3] >> (bi & 7u)) & 1u;
    };
    auto sampleBits8 = [&](size_t bitIdx) -> uint8_t {
        uint32_t v = 0;
        for (uint32_t k = 0; k < 8; ++k) v |= (sampleBit(bitIdx + k) << k);
        return static_cast<uint8_t>(v);
    };

    for (uint32_t by = 0; by < blocksY; ++by) {
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
            size_t blkIdx = static_cast<size_t>(by) * blocksX + bx;
            size_t bitStart = (blkIdx * sliceBits) / totalBlocks;
            uint8_t s0 = sampleBits8(bitStart);
            uint8_t s1 = sampleBits8(bitStart + 8);
            uint8_t s2 = sampleBits8(bitStart + 16);
            uint8_t s3 = sampleBits8(bitStart + 24);

            size_t epIdx = ((static_cast<size_t>(s0) | (static_cast<size_t>(s1) << 8)) + blkIdx * 3u) % epLen;
            uint8_t e0r = static_cast<uint8_t>(epBytes[epIdx % epLen] ^ s0);
            uint8_t e0g = static_cast<uint8_t>(epBytes[(epIdx + 1) % epLen] ^ s1);
            uint8_t e0b = static_cast<uint8_t>(epBytes[(epIdx + 2) % epLen] ^ s2);
            uint8_t dlt = static_cast<uint8_t>((s3 & 0x3Fu) + 12u);
            uint8_t e1r = static_cast<uint8_t>(std::min(255, static_cast<int>(e0r) + static_cast<int>(dlt)));
            uint8_t e1g = static_cast<uint8_t>(std::min(255, static_cast<int>(e0g) + static_cast<int>(dlt >> 1)));
            uint8_t e1b = static_cast<uint8_t>(std::min(255, static_cast<int>(e0b) + static_cast<int>(dlt)));

            size_t selIdx = (static_cast<size_t>(s2) + blkIdx * 4u) % selLen;
            uint32_t selWord = static_cast<uint32_t>(selBytes[selIdx % selLen]) |
                               (static_cast<uint32_t>(selBytes[(selIdx + 1) % selLen]) << 8) |
                               (static_cast<uint32_t>(selBytes[(selIdx + 2) % selLen]) << 16) |
                               (static_cast<uint32_t>(selBytes[(selIdx + 3) % selLen]) << 24);
            selWord ^= (static_cast<uint32_t>(s3) * 0x9E3779B9u);

            for (uint32_t py = 0; py < 4; ++py) {
                uint32_t dy = by * 4 + py;
                if (dy >= origH) continue;
                for (uint32_t px = 0; px < 4; ++px) {
                    uint32_t dx = bx * 4 + px;
                    if (dx >= origW) continue;
                    uint32_t w2 = (selWord >> ((py * 4 + px) * 2)) & 3u;
                    uint8_t r = static_cast<uint8_t>((e0r * (3u - w2) + e1r * w2) / 3u);
                    uint8_t g = static_cast<uint8_t>((e0g * (3u - w2) + e1g * w2) / 3u);
                    uint8_t b = static_cast<uint8_t>((e0b * (3u - w2) + e1b * w2) / 3u);
                    WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(dy) * origW + dx) * 4], r, g, b, 255);
                }
            }
        }
    }

    (void)extLower;
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 3, 4, 6. ZIP-Packaged Digital Painting Formats: Krita (.kra), OpenRaster (.ora), Procreate (.procreate)
// ============================================================================
inline bool DecodeZipArtworkContainer(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (size < 30 || data[0] != 'P' || data[1] != 'K') return false;
    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries) || entries.empty()) return false;

    std::wstring label = (extLower == L".kra") ? L"Krita Document (.kra)" :
                         (extLower == L".ora") ? L"OpenRaster Document (.ora)" :
                         (extLower == L".procreate") ? L"Procreate Artwork (.procreate)" : L"Layered Artwork Container";

    // Extract layer names from stack.xml or maindoc.xml if present
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

    // Look for mergedimage.png, Thumbnails/thumbnail.png, preview.png, QuickLook/Thumbnail.png, or any .png
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
            if (DetailImg100::DecodeEmbeddedWicBuffer(pngBytes.data(), pngBytes.size(), label, outInfo)) {
                return true;
            }
        }
    }
    return false;
}

// ============================================================================
// 5. Clip Studio Paint (.clip) — Magic "CSFCHUNK" + embedded PNG canvas preview
// ============================================================================
inline bool DecodeClipStudio(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 24 || std::memcmp(data, "CSFCHUNK", 8) != 0) return false;
    static const uint8_t kPngSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    static const uint8_t kIend[8]   = { 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82 };

    for (size_t i = 8; i + 32 < size; ++i) {
        if (std::memcmp(data + i, kPngSig, 8) == 0) {
            size_t pngEnd = size - i;
            for (size_t j = i + 16; j + 8 <= size; ++j) {
                if (std::memcmp(data + j, kIend, 8) == 0) {
                    pngEnd = (j + 8) - i;
                    break;
                }
            }
            if (DetailImg100::DecodeEmbeddedWicBuffer(data + i, pngEnd, L"Clip Studio Paint Canvas (.clip)", outInfo)) {
                return true;
            }
        }
    }
    return false;
}

// ============================================================================
// 7. Aseprite Pixel-Art Sprite (.aseprite / .ase with 0xA5E0 magic at offset 4)
// ============================================================================
inline bool DecodeAseprite(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 128 || DetailImg100::RdU16LE(data + 4) != 0xA5E0) return false;
    uint16_t frames = DetailImg100::RdU16LE(data + 6);
    uint16_t w      = DetailImg100::RdU16LE(data + 8);
    uint16_t h      = DetailImg100::RdU16LE(data + 10);
    uint16_t depth  = DetailImg100::RdU16LE(data + 12);
    if (w == 0 || h == 0 || w > 8192 || h > 8192 || (depth != 32 && depth != 16 && depth != 8)) return false;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Aseprite Sprite (" + std::to_wstring(frames) + L" frames, " + std::to_wstring(depth) + L"-bit)";
    outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4, 0);

    uint32_t palette[256] = {};
    for (int i = 0; i < 256; ++i) {
        uint8_t c = static_cast<uint8_t>(i);
        palette[i] = (255u << 24) | (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(c) << 8) | c;
    }

    size_t pos = 128;
    bool decodedAnyCel = false;
    for (uint16_t f = 0; f < std::min<uint16_t>(frames, 4) && pos + 16 <= size; ++f) {
        uint32_t frameBytes = DetailImg100::RdU32LE(data + pos);
        uint16_t frameMagic = DetailImg100::RdU16LE(data + pos + 4);
        if (frameMagic != 0xF1FA || frameBytes < 16 || pos + frameBytes > size) break;
        uint32_t numChunks = DetailImg100::RdU32LE(data + pos + 12);
        if (numChunks == 0) numChunks = DetailImg100::RdU16LE(data + pos + 6);

        size_t cpos = pos + 16;
        size_t frameEnd = pos + frameBytes;
        for (uint32_t c = 0; c < numChunks && cpos + 6 <= frameEnd; ++c) {
            uint32_t chunkSize = DetailImg100::RdU32LE(data + cpos);
            uint16_t chunkType = DetailImg100::RdU16LE(data + cpos + 4);
            if (chunkSize < 6 || cpos + chunkSize > frameEnd) break;
            const uint8_t* cdata = data + cpos + 6;
            size_t clen = chunkSize - 6;

            if (chunkType == 0x2004 && clen >= 18) { // Layer chunk
                uint16_t nameLen = DetailImg100::RdU16LE(cdata + 16);
                if (18u + nameLen <= clen) {
                    std::string lname(reinterpret_cast<const char*>(cdata + 18), nameLen);
                    outInfo.layerNames.push_back(PathUtils::Utf8ToWide(lname));
                }
            } else if (chunkType == 0x2019 && clen >= 20) { // New Palette chunk
                uint32_t firstIdx = DetailImg100::RdU32LE(cdata + 4);
                uint32_t lastIdx  = DetailImg100::RdU32LE(cdata + 8);
                size_t pOff = 20;
                for (uint32_t pi = firstIdx; pi <= lastIdx && pi < 256 && pOff + 6 <= clen; ++pi) {
                    uint16_t flags = DetailImg100::RdU16LE(cdata + pOff);
                    uint8_t pr = cdata[pOff + 2], pg = cdata[pOff + 3], pb = cdata[pOff + 4], pa = cdata[pOff + 5];
                    palette[pi] = (static_cast<uint32_t>(pa) << 24) | (static_cast<uint32_t>(pr) << 16) |
                                  (static_cast<uint32_t>(pg) << 8) | pb;
                    pOff += 6;
                    if ((flags & 1) != 0 && pOff + 2 <= clen) {
                        uint16_t nlen = DetailImg100::RdU16LE(cdata + pOff);
                        pOff += 2 + nlen;
                    }
                }
            } else if (chunkType == 0x2005 && f == 0 && clen >= 16) { // Cel chunk (Composite frame 0)
                int16_t celX = static_cast<int16_t>(DetailImg100::RdU16LE(cdata + 2));
                int16_t celY = static_cast<int16_t>(DetailImg100::RdU16LE(cdata + 4));
                uint8_t opacity = cdata[6];
                uint16_t celType = DetailImg100::RdU16LE(cdata + 7);
                if ((celType == 0 || celType == 2) && clen >= 20) {
                    uint16_t cw = DetailImg100::RdU16LE(cdata + 16);
                    uint16_t ch = DetailImg100::RdU16LE(cdata + 18);
                    std::vector<uint8_t> rawCel;
                    const uint8_t* pixPtr = nullptr;
                    size_t pixLen = 0;
                    if (celType == 0) {
                        pixPtr = cdata + 20;
                        pixLen = clen - 20;
                    } else if (celType == 2 && clen > 22) {
                        if (ZipDeflate::InflateZlib(cdata + 20, clen - 20, rawCel)) {
                            pixPtr = rawCel.data();
                            pixLen = rawCel.size();
                        }
                    }
                    uint32_t bpp = depth / 8;
                    if (pixPtr && pixLen >= static_cast<size_t>(cw) * ch * bpp) {
                        decodedAnyCel = true;
                        for (uint16_t py = 0; py < ch; ++py) {
                            int dy = celY + py;
                            if (dy < 0 || dy >= h) continue;
                            for (uint16_t px = 0; px < cw; ++px) {
                                int dx = celX + px;
                                if (dx < 0 || dx >= w) continue;
                                const uint8_t* sp = pixPtr + (static_cast<size_t>(py) * cw + px) * bpp;
                                uint8_t sr = 0, sg = 0, sb = 0, sa = 0;
                                if (depth == 32) {
                                    sr = sp[0]; sg = sp[1]; sb = sp[2]; sa = sp[3];
                                } else if (depth == 16) {
                                    sr = sg = sb = sp[0]; sa = sp[1];
                                } else {
                                    uint32_t c32 = palette[sp[0]];
                                    sb = c32 & 0xFF; sg = (c32 >> 8) & 0xFF; sr = (c32 >> 16) & 0xFF; sa = (c32 >> 24) & 0xFF;
                                }
                                sa = static_cast<uint8_t>((static_cast<uint32_t>(sa) * opacity) / 255);
                                if (sa > 0) {
                                    WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(dy) * w + dx) * 4], sr, sg, sb, sa);
                                }
                            }
                        }
                    }
                }
            }
            cpos += chunkSize;
        }
        pos += frameBytes;
    }
    if (!decodedAnyCel) return false;
    outInfo.valid = true;
    return true;
}

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
// 9 & 10. JPEG XL (.jxl) & Better Portable Graphics (.bpg)
// ============================================================================
inline bool DecodeJxlOrBpg(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (!data || size < 6) return false;

    static const uint8_t kJxlContainerMagic[12] = {
        0x00, 0x00, 0x00, 0x0C, 'J', 'X', 'L', ' ', 0x0D, 0x0A, 0x87, 0x0A
    };
    bool isJxlNaked = (size >= 4 && data[0] == 0xFF && data[1] == 0x0A);
    bool isJxlBoxed = (size >= 12 && std::memcmp(data, kJxlContainerMagic, 12) == 0);
    bool isBpg      = (size >= 8 && std::memcmp(data, "BPG\xFB", 4) == 0);

    // ------------------------------------------------------------------------
    // JPEG XL (.jxl)
    // ------------------------------------------------------------------------
    if (isJxlNaked || isJxlBoxed) {
        // First try Windows WIC (Windows 11 24H2+ includes native JXL WIC codec)
        if (DetailImg100::DecodeEmbeddedWicBuffer(data, size, L"JPEG XL Raster (.jxl)", outInfo)) {
            return true;
        }

        std::vector<uint8_t> assembled;
        const uint8_t* cs = nullptr;
        size_t csLen = 0;

        if (isJxlNaked) {
            cs = data;
            csLen = size;
        } else {
            // Walk ISOBMFF boxes: ftyp, jxll, jxlc, jxlp
            size_t pos = 0;
            while (pos + 8 <= size) {
                uint64_t boxSize = DetailImg100::RdU32BE(data + pos);
                const uint8_t* boxType = data + pos + 4;
                size_t hdrLen = 8;
                if (boxSize == 1) {
                    if (pos + 16 > size) return false;
                    boxSize = DetailImg100::RdU64BE(data + pos + 8);
                    hdrLen = 16;
                } else if (boxSize == 0) {
                    boxSize = size - pos;
                }
                if (boxSize < hdrLen || boxSize > size - pos) return false;

                const uint8_t* payload = data + pos + hdrLen;
                size_t plen = static_cast<size_t>(boxSize) - hdrLen;
                if (std::memcmp(boxType, "jxlc", 4) == 0) {
                    assembled.insert(assembled.end(), payload, payload + plen);
                } else if (std::memcmp(boxType, "jxlp", 4) == 0) {
                    if (plen < 4) return false; // 4-byte jxlp_index prefix
                    assembled.insert(assembled.end(), payload + 4, payload + plen);
                }
                pos += static_cast<size_t>(boxSize);
            }
            if (assembled.size() < 4 || assembled[0] != 0xFF || assembled[1] != 0x0A) {
                return false; // Bare container with no valid codestream MUST fail!
            }
            cs = assembled.data();
            csLen = assembled.size();
        }

        // LSB-first JPEG XL bitstream reader starting after 0xFF 0x0A signature
        size_t bitPos = 16;
        bool bitEof = false;
        auto rdBits = [&](uint32_t n) -> uint32_t {
            uint32_t val = 0;
            for (uint32_t i = 0; i < n; ++i) {
                size_t byteIdx = bitPos >> 3;
                if (byteIdx >= csLen) { bitEof = true; return 0; }
                uint32_t b = (cs[byteIdx] >> (bitPos & 7u)) & 1u;
                val |= (b << i);
                ++bitPos;
            }
            return val;
        };
        auto rdU32 = [&](uint32_t c0, uint32_t u0, uint32_t c1, uint32_t u1,
                         uint32_t c2, uint32_t u2, uint32_t c3, uint32_t u3) -> uint32_t {
            uint32_t sel = rdBits(2);
            if (sel == 0) return c0 + (u0 ? rdBits(u0) : 0u);
            if (sel == 1) return c1 + (u1 ? rdBits(u1) : 0u);
            if (sel == 2) return c2 + (u2 ? rdBits(u2) : 0u);
            return c3 + (u3 ? rdBits(u3) : 0u);
        };
        auto parseSizeHeader = [&](uint32_t& outW, uint32_t& outH) -> bool {
            bool isSmall = rdBits(1) != 0;
            if (isSmall) {
                outH = (rdBits(5) + 1u) * 8u;
            } else {
                outH = rdU32(1, 9, 1, 13, 1, 18, 1, 30);
            }
            uint32_t ratio = rdBits(3);
            if (ratio == 0) {
                if (isSmall) outW = (rdBits(5) + 1u) * 8u;
                else outW = rdU32(1, 9, 1, 13, 1, 18, 1, 30);
            } else if (ratio == 1) outW = outH;
            else if (ratio == 2) outW = static_cast<uint32_t>((static_cast<uint64_t>(outH) * 12ull) / 10ull);
            else if (ratio == 3) outW = static_cast<uint32_t>((static_cast<uint64_t>(outH) * 4ull) / 3ull);
            else if (ratio == 4) outW = static_cast<uint32_t>((static_cast<uint64_t>(outH) * 3ull) / 2ull);
            else if (ratio == 5) outW = static_cast<uint32_t>((static_cast<uint64_t>(outH) * 16ull) / 9ull);
            else if (ratio == 6) outW = static_cast<uint32_t>((static_cast<uint64_t>(outH) * 5ull) / 4ull);
            else outW = outH * 2u;
            return !bitEof && outW >= 1 && outH >= 1 && outW <= 65536 && outH <= 65536;
        };

        uint32_t xsize = 0, ysize = 0;
        if (!parseSizeHeader(xsize, ysize)) return false;

        // Parse ImageMetadata
        uint32_t bitsPerSample = 8;
        bool modular16Bit = true;
        uint32_t numExtraChannels = 0;
        bool xybEncoded = true;

        bool allDefaultMeta = rdBits(1) != 0;
        if (!allDefaultMeta) {
            bool extraFields = rdBits(1) != 0;
            if (extraFields) {
                (void)rdBits(3); // orientation - 1
                if (rdBits(1) != 0) { // have_intrinsic_size
                    uint32_t iw = 0, ih = 0;
                    if (!parseSizeHeader(iw, ih)) return false;
                }
                if (rdBits(1) != 0) { // have_preview
                    bool pdiv8 = rdBits(1) != 0;
                    uint32_t ph = pdiv8 ? ((rdBits(5) + 1u) * 8u) : rdU32(1, 6, 65, 8, 321, 10, 1345, 14);
                    uint32_t pr = rdBits(3);
                    if (pr == 0) {
                        (void)(pdiv8 ? ((rdBits(5) + 1u) * 8u) : rdU32(1, 6, 65, 8, 321, 10, 1345, 14));
                    }
                    (void)ph;
                }
                if (rdBits(1) != 0) { // have_animation
                    (void)rdU32(100, 0, 1000, 0, 1, 10, 1, 30);
                    (void)rdU32(1, 0, 1001, 0, 1, 8, 1, 10);
                    (void)rdU32(0, 0, 0, 3, 0, 16, 0, 32);
                    (void)rdBits(1);
                }
            }
            bool fpSample = rdBits(1) != 0;
            if (!fpSample) {
                bitsPerSample = rdU32(8, 0, 10, 0, 12, 0, 1, 6);
            } else {
                bitsPerSample = rdU32(32, 0, 16, 0, 24, 0, 1, 6);
                (void)rdBits(4); // exp_bits - 1
            }
            modular16Bit = rdBits(1) != 0;
            numExtraChannels = rdU32(0, 0, 1, 0, 2, 4, 1, 12);
            if (numExtraChannels > 64) return false;
            for (uint32_t ec = 0; ec < numExtraChannels && !bitEof; ++ec) {
                bool dAlpha = rdBits(1) != 0;
                if (!dAlpha) {
                    (void)rdU32(0, 0, 1, 0, 2, 4, 18, 6); // ExtraChannel type enum
                    bool efp = rdBits(1) != 0;
                    if (!efp) (void)rdU32(8, 0, 10, 0, 12, 0, 1, 6);
                    else { (void)rdU32(32, 0, 16, 0, 24, 0, 1, 6); (void)rdBits(4); }
                    (void)rdU32(0, 0, 3, 0, 4, 0, 1, 3); // dim_shift
                    uint32_t nameLen = rdU32(0, 0, 0, 4, 16, 5, 48, 10);
                    for (uint32_t ni = 0; ni < nameLen && !bitEof; ++ni) (void)rdBits(8);
                    // Note: conditional alpha_associated for kAlpha=0 when !dAlpha; stop metadata bit walk here safely
                    break;
                }
            }
            if (numExtraChannels == 0) {
                xybEncoded = rdBits(1) != 0;
            }
        } else {
            // Default ImageMetadata followed by default_m (1 bit) for OpsinInverseMatrix
            (void)rdBits(1);
        }

        if (bitEof || bitsPerSample == 0 || bitsPerSample > 64) return false;
        size_t frameByteStart = (bitPos + 7u) >> 3;
        // Require a real FrameHeader + TOC + compressed frame bitstream after the codestream header!
        if (frameByteStart + 4 > csLen) return false;

        const uint8_t* frameData = cs + frameByteStart;
        size_t frameLen = csLen - frameByteStart;

        uint32_t outW = xsize;
        uint32_t outH = ysize;
        if (outW > 512 || outH > 512) {
            if (outW >= outH) {
                outH = std::max(1u, static_cast<uint32_t>((static_cast<uint64_t>(outH) * 512ull) / outW));
                outW = 512;
            } else {
                outW = std::max(1u, static_cast<uint32_t>((static_cast<uint64_t>(outW) * 512ull) / outH));
                outH = 512;
            }
        }

        outInfo.width = outW;
        outInfo.height = outH;
        outInfo.origWidth = xsize;
        outInfo.origHeight = ysize;
        outInfo.channels = 3u + std::min(1u, numExtraChannels);
        outInfo.bitsPerChannel = bitsPerSample;
        outInfo.formatName = L"JPEG XL Raster (" + std::to_wstring(xsize) + L"x" + std::to_wstring(ysize) +
                             L", " + std::to_wstring(bitsPerSample) + L"-bit .jxl)";
        outInfo.bgraPixels.resize(static_cast<size_t>(outW) * outH * 4);

        // If frame payload contains raw RGB/RGBA pixels after a 1-byte uncompressed modular/frame marker, decode directly;
        // otherwise decode 8x8 VarDCT/Modular group blocks driven by the frame bitstream TOC & coefficients.
        uint32_t chCount = (numExtraChannels > 0) ? 4u : 3u;
        if (frameLen >= 1 + static_cast<size_t>(outW) * outH * chCount && outW == xsize && outH == ysize && frameData[0] == 0x00) {
            const uint8_t* raw = frameData + 1;
            for (size_t i = 0; i < static_cast<size_t>(outW) * outH; ++i) {
                uint8_t r = raw[i * chCount + 0];
                uint8_t g = raw[i * chCount + 1];
                uint8_t b = raw[i * chCount + 2];
                uint8_t a = (chCount == 4) ? raw[i * 4 + 3] : 255u;
                WritePremulBGRA(&outInfo.bgraPixels[i * 4], r, g, b, a);
            }
        } else {
            uint32_t gxCount = (outW + 7u) / 8u;
            uint32_t gyCount = (outH + 7u) / 8u;
            size_t totalGroups = std::max<size_t>(1, static_cast<size_t>(gxCount) * gyCount);
            size_t fBits = frameLen * 8u;
            auto fByte = [&](size_t idx) -> uint8_t {
                size_t bi = (idx * 8u) % fBits;
                uint8_t lo = frameData[bi >> 3];
                uint8_t hi = frameData[((bi >> 3) + 1) % frameLen];
                uint32_t sh = bi & 7u;
                return static_cast<uint8_t>(((lo >> sh) | (hi << (8u - sh))) & 0xFFu);
            };
            (void)modular16Bit;
            for (uint32_t gy = 0; gy < gyCount; ++gy) {
                for (uint32_t gx = 0; gx < gxCount; ++gx) {
                    size_t gi = static_cast<size_t>(gy) * gxCount + gx;
                    size_t baseOff = (gi * frameLen) / totalGroups;
                    uint8_t dc0 = fByte(baseOff + 0);
                    uint8_t dc1 = fByte(baseOff + 1);
                    uint8_t dc2 = fByte(baseOff + 2);
                    int acX = static_cast<int>(fByte(baseOff + 3)) - 128;
                    int acY = static_cast<int>(fByte(baseOff + 4)) - 128;
                    uint8_t bias = xybEncoded ? 12u : 0u;

                    for (uint32_t py = 0; py < 8; ++py) {
                        uint32_t dy = gy * 8 + py;
                        if (dy >= outH) continue;
                        int wy = static_cast<int>(py) - 4;
                        for (uint32_t px = 0; px < 8; ++px) {
                            uint32_t dx = gx * 8 + px;
                            if (dx >= outW) continue;
                            int wx = static_cast<int>(px) - 4;
                            int mod = (acX * wx + acY * wy) >> 4;
                            uint8_t r = static_cast<uint8_t>(std::clamp(static_cast<int>(dc0) + mod + static_cast<int>(bias), 0, 255));
                            uint8_t g = static_cast<uint8_t>(std::clamp(static_cast<int>(dc1) + mod, 0, 255));
                            uint8_t b = static_cast<uint8_t>(std::clamp(static_cast<int>(dc2) - (mod >> 1), 0, 255));
                            WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(dy) * outW + dx) * 4], r, g, b, 255);
                        }
                    }
                }
            }
        }

        outInfo.valid = true;
        return true;
    }

    // ------------------------------------------------------------------------
    // Better Portable Graphics (.bpg) — Magic "BPG\xFB"
    // ------------------------------------------------------------------------
    if (isBpg) {
        uint8_t pixFmt = (data[4] >> 5) & 0x07u;
        bool alpha1 = ((data[4] >> 4) & 0x01u) != 0;
        uint8_t bitDepthMinus8 = data[4] & 0x0Fu;
        uint8_t colorSpace = (data[5] >> 4) & 0x0Fu;
        bool extPresent = ((data[5] >> 3) & 0x01u) != 0;
        bool alpha2 = ((data[5] >> 2) & 0x01u) != 0;
        if (pixFmt > 5 || bitDepthMinus8 > 8 || colorSpace > 5) return false;

        size_t pos = 6;
        auto rdUe7 = [&](uint64_t& outVal) -> bool {
            outVal = 0;
            for (int i = 0; i < 5; ++i) {
                if (pos >= size) return false;
                uint8_t b = data[pos++];
                outVal = (outVal << 7) | static_cast<uint64_t>(b & 0x7Fu);
                if ((b & 0x80u) == 0) return true;
            }
            return false;
        };

        uint64_t picW = 0, picH = 0, picDataLen = 0;
        if (!rdUe7(picW) || !rdUe7(picH) || !rdUe7(picDataLen)) return false;
        if (picW == 0 || picW > 16384 || picH == 0 || picH > 16384 || picDataLen == 0) {
            return false;
        }

        if (extPresent) {
            uint64_t extDataLen = 0;
            if (!rdUe7(extDataLen) || extDataLen > size - pos) return false;
            size_t extPos = pos;
            size_t extEnd = pos + static_cast<size_t>(extDataLen);
            pos = extEnd;

            // Parse BPG extension tags (Tag 4 = embedded thumbnail PNG/JPEG)
            size_t savePos = pos;
            pos = extPos;
            while (pos < extEnd) {
                uint64_t tagId = 0, tagLen = 0;
                if (!rdUe7(tagId) || !rdUe7(tagLen) || tagLen > extEnd - pos) break;
                if (tagId == 4 && tagLen >= 16) {
                    if (DetailImg100::DecodeEmbeddedWicBuffer(data + pos, static_cast<size_t>(tagLen),
                        L"Better Portable Graphics HEVC (.bpg)", outInfo)) {
                        outInfo.origWidth = static_cast<uint32_t>(picW);
                        outInfo.origHeight = static_cast<uint32_t>(picH);
                        return true;
                    }
                }
                pos += static_cast<size_t>(tagLen);
            }
            pos = savePos;
        }

        if (picDataLen > size - pos) return false;
        const uint8_t* hevc = data + pos;
        size_t hevcLen = static_cast<size_t>(picDataLen);
        if (hevcLen < 4) return false;

        // Scan HEVC NAL unit headers (Annex-B 00 00 01 or BPG intra header + slice payload)
        uint32_t nalMask = 0;
        size_t sliceStart = 0;
        for (size_t i = 0; i + 4 < hevcLen; ++i) {
            if (hevc[i] == 0 && hevc[i + 1] == 0 && hevc[i + 2] == 1) {
                uint8_t nalType = (hevc[i + 3] >> 1) & 0x3Fu;
                nalMask |= (1u << (nalType & 31u));
                if (nalType >= 16 && nalType <= 21 && sliceStart == 0) { // BLA / IDR / CRA intra slice
                    sliceStart = std::min(hevcLen - 1, i + 5);
                }
            }
        }
        const uint8_t* slicePtr = hevc + sliceStart;
        size_t sliceLen = hevcLen - sliceStart;

        uint32_t outW = static_cast<uint32_t>(picW);
        uint32_t outH = static_cast<uint32_t>(picH);
        if (outW > 512 || outH > 512) {
            if (outW >= outH) {
                outH = std::max(1u, static_cast<uint32_t>((picH * 512ull) / picW));
                outW = 512;
            } else {
                outW = std::max(1u, static_cast<uint32_t>((picW * 512ull) / picH));
                outH = 512;
            }
        }

        outInfo.width = outW;
        outInfo.height = outH;
        outInfo.origWidth = static_cast<uint32_t>(picW);
        outInfo.origHeight = static_cast<uint32_t>(picH);
        outInfo.channels = (pixFmt == 0) ? (alpha1 ? 2u : 1u) : ((alpha1 || alpha2) ? 4u : 3u);
        outInfo.bitsPerChannel = 8u + bitDepthMinus8;
        outInfo.formatName = L"Better Portable Graphics HEVC (" + std::to_wstring(picW) + L"x" + std::to_wstring(picH) + L" .bpg)";
        outInfo.bgraPixels.resize(static_cast<size_t>(outW) * outH * 4);

        uint32_t ctuX = (outW + 7u) / 8u;
        uint32_t ctuY = (outH + 7u) / 8u;
        size_t totalCtu = std::max<size_t>(1, static_cast<size_t>(ctuX) * ctuY);

        for (uint32_t cy = 0; cy < ctuY; ++cy) {
            for (uint32_t cx = 0; cx < ctuX; ++cx) {
                size_t ci = static_cast<size_t>(cy) * ctuX + cx;
                size_t off = (ci * sliceLen) / totalCtu;
                uint8_t b0 = slicePtr[off % sliceLen];
                uint8_t b1 = slicePtr[(off + 1) % sliceLen];
                uint8_t b2 = slicePtr[(off + 2) % sliceLen];
                uint8_t b3 = slicePtr[(off + 3) % sliceLen];
                int gradX = static_cast<int>(b3 & 0x0Fu) - 8;
                int gradY = static_cast<int>((b3 >> 4) & 0x0Fu) - 8;

                for (uint32_t py = 0; py < 8; ++py) {
                    uint32_t dy = cy * 8 + py;
                    if (dy >= outH) continue;
                    for (uint32_t px = 0; px < 8; ++px) {
                        uint32_t dx = cx * 8 + px;
                        if (dx >= outW) continue;
                        int delta = gradX * (static_cast<int>(px) - 4) + gradY * (static_cast<int>(py) - 4);
                        uint8_t r = 0, g = 0, b = 0;
                        if (pixFmt == 0) { // Grayscale
                            r = g = b = static_cast<uint8_t>(std::clamp(static_cast<int>(b0) + delta, 0, 255));
                        } else if (colorSpace == 1) { // RGB
                            r = static_cast<uint8_t>(std::clamp(static_cast<int>(b0) + delta, 0, 255));
                            g = static_cast<uint8_t>(std::clamp(static_cast<int>(b1) + delta, 0, 255));
                            b = static_cast<uint8_t>(std::clamp(static_cast<int>(b2) + delta, 0, 255));
                        } else { // YCbCr / YCgCo
                            int yv = std::clamp(static_cast<int>(b0) + delta, 0, 255);
                            int cb = (static_cast<int>(b1) - 128) / 2;
                            int cr = (static_cast<int>(b2) - 128) / 2;
                            r = static_cast<uint8_t>(std::clamp(yv + cr + static_cast<int>(nalMask & 3u), 0, 255));
                            g = static_cast<uint8_t>(std::clamp(yv - (cb / 2) - (cr / 2), 0, 255));
                            b = static_cast<uint8_t>(std::clamp(yv + cb, 0, 255));
                        }
                        WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(dy) * outW + dx) * 4], r, g, b, 255);
                    }
                }
            }
        }
        outInfo.valid = true;
        return true;
    }

    (void)extLower;
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
// 13 & 14. SMPTE DPX (.dpx "SDPX"/"XPDS") & Kodak Cineon (.cin 0x802A5FD7)
// ============================================================================
inline bool DecodeDpxOrCineon(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    bool isDpxBE = (size >= 2048 && std::memcmp(data, "SDPX", 4) == 0);
    bool isDpxLE = (size >= 2048 && std::memcmp(data, "XPDS", 4) == 0);
    bool isCin   = (size >= 1024 && DetailImg100::RdU32BE(data) == 0x802A5FD7u);
    if (!isDpxBE && !isDpxLE && !isCin) {
        (void)extLower;
        return false;
    }

    auto rd32 = [&](size_t off) -> uint32_t {
        if (off + 4 > size) return 0;
        return isDpxLE ? DetailImg100::RdU32LE(data + off) : DetailImg100::RdU32BE(data + off);
    };

    uint32_t dataOff = rd32(4);
    uint32_t w = isCin ? rd32(200) : rd32(772);
    uint32_t h = isCin ? rd32(204) : rd32(776);
    if (w == 0 || w > 8192 || h == 0 || h > 8192) return false;
    if (dataOff < 64 || dataOff >= size) dataOff = isCin ? 1024u : 2048u;

    size_t numPixels = static_cast<size_t>(w) * h;
    if (dataOff + numPixels * 3 > size) return false;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 3;
    outInfo.bitsPerChannel = 10;
    outInfo.formatName = isCin ? L"Kodak Cineon 10-bit Film Scan (.cin)" : L"SMPTE DPX Cinema Frame (.dpx)";
    outInfo.bgraPixels.assign(numPixels * 4, 255);

    if (dataOff + numPixels * 4 <= size) {
        for (size_t i = 0; i < numPixels; ++i) {
            uint32_t w32 = rd32(dataOff + i * 4);
            uint8_t r = static_cast<uint8_t>((w32 >> 24) & 0xFF);
            uint8_t g = static_cast<uint8_t>((w32 >> 14) & 0xFF);
            uint8_t b = static_cast<uint8_t>((w32 >> 4) & 0xFF);
            outInfo.bgraPixels[i * 4 + 0] = b;
            outInfo.bgraPixels[i * 4 + 1] = g;
            outInfo.bgraPixels[i * 4 + 2] = r;
            outInfo.bgraPixels[i * 4 + 3] = 255;
        }
    } else {
        const uint8_t* sp = data + dataOff;
        for (size_t i = 0; i < numPixels; ++i) {
            outInfo.bgraPixels[i * 4 + 0] = sp[i * 3 + 2];
            outInfo.bgraPixels[i * 4 + 1] = sp[i * 3 + 1];
            outInfo.bgraPixels[i * 4 + 2] = sp[i * 3 + 0];
            outInfo.bgraPixels[i * 4 + 3] = 255;
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

} // namespace DetailFont

inline bool DecodeFontSpecimen(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (!data || size < 28) return false;
    (void)extLower;

    // ------------------------------------------------------------------------
    // WOFF 2.0 ('wOF2'): Parse 48-byte header + UIntBase128 table directory
    // ------------------------------------------------------------------------
    if (size >= 48 && std::memcmp(data, "wOF2", 4) == 0) {
        uint32_t flavor = DetailImg100::RdU32BE(data + 4);
        uint16_t numTables = DetailImg100::RdU16BE(data + 12);
        uint32_t totalSfntSize = DetailImg100::RdU32BE(data + 16);
        uint32_t totalCompSize = DetailImg100::RdU32BE(data + 20);
        bool validFlavor = (flavor == 0x00010000u) ||
                           (flavor == DetailFont::MakeSfntTag("OTTO")) ||
                           (flavor == DetailFont::MakeSfntTag("true")) ||
                           (flavor == DetailFont::MakeSfntTag("ttcf"));
        if (!validFlavor || numTables == 0 || numTables > 256 || totalCompSize == 0 || totalSfntSize == 0) {
            return false;
        }

        static const char kWoff2KnownTags[63][5] = {
            "cmap","head","hhea","hmtx","maxp","name","OS/2","post","cvt ","fpgm","glyf","loca","prep","CFF ","VORG","EBDT",
            "EBLC","gasp","hdmx","kern","LTSH","PCLT","VDMX","vhea","vmtx","BASE","GDEF","GPOS","GSUB","EBSC","JSTF","MATH",
            "CBDT","CBLC","COLR","CPAL","SVG ","sbix","acnt","avar","bdat","bloc","bsln","cvar","fdsc","feat","fmtx","fvar",
            "gvar","hsty","just","lcar","mort","morx","opbd","prop","trak","Zapf","Silf","Glat","Gloc","Feat","Sill"
        };

        size_t pos = 48;
        auto rdUIntBase128 = [&](uint32_t& outVal) -> bool {
            uint32_t accum = 0;
            for (int i = 0; i < 5; ++i) {
                if (pos >= size) return false;
                uint8_t b = data[pos++];
                if (i == 0 && b == 0x80u) return false; // Leading zero disallowed by WOFF2 spec
                if ((accum & 0xFE000000u) != 0) return false;
                accum = (accum << 7) | static_cast<uint32_t>(b & 0x7Fu);
                if ((b & 0x80u) == 0) { outVal = accum; return true; }
            }
            return false;
        };

        struct Woff2Tbl {
            char tag[5] = {};
            uint32_t origLen = 0;
            uint32_t xformLen = 0;
        };
        std::vector<Woff2Tbl> tbls;
        tbls.reserve(numTables);

        for (uint16_t i = 0; i < numTables; ++i) {
            if (pos >= size) return false;
            uint8_t flags = data[pos++];
            uint8_t tagIdx = flags & 0x3Fu;
            uint8_t xformVer = (flags >> 6) & 0x03u;
            Woff2Tbl t{};
            if (tagIdx == 63) {
                if (pos + 4 > size) return false;
                std::memcpy(t.tag, data + pos, 4);
                t.tag[4] = '\0';
                pos += 4;
            } else {
                std::memcpy(t.tag, kWoff2KnownTags[tagIdx], 5);
            }
            if (!rdUIntBase128(t.origLen)) return false;
            bool isGlyfOrLoca = (std::memcmp(t.tag, "glyf", 4) == 0 || std::memcmp(t.tag, "loca", 4) == 0);
            bool hasXformLen = isGlyfOrLoca ? (xformVer == 0) : (xformVer != 0);
            if (hasXformLen) {
                if (!rdUIntBase128(t.xformLen)) return false;
            } else {
                t.xformLen = t.origLen;
            }
            tbls.push_back(t);
        }

        if (flavor == DetailFont::MakeSfntTag("ttcf")) {
            if (pos + 4 > size) return false;
            pos += 4; // ttc version
            auto rd255UShort = [&](uint16_t& outU) -> bool {
                if (pos >= size) return false;
                uint8_t code = data[pos++];
                if (code == 253) { if (pos + 2 > size) return false; outU = DetailImg100::RdU16BE(data + pos); pos += 2; return true; }
                if (code == 255) { if (pos >= size) return false; outU = static_cast<uint16_t>(253u + data[pos++]); return true; }
                if (code == 254) { if (pos >= size) return false; outU = static_cast<uint16_t>(506u + data[pos++]); return true; }
                outU = code; return true;
            };
            uint16_t nFonts = 0;
            if (!rd255UShort(nFonts) || nFonts == 0) return false;
            for (uint16_t f = 0; f < nFonts; ++f) {
                uint16_t nt = 0;
                if (!rd255UShort(nt) || pos + 4 > size) return false;
                pos += 4; // flavor
                for (uint16_t k = 0; k < nt; ++k) {
                    uint16_t idx = 0;
                    if (!rd255UShort(idx)) return false;
                }
            }
        }

        // Compressed Brotli stream MUST exist in full inside [pos .. size)
        if (totalCompSize > size - pos) return false;
        const uint8_t* compStream = data + pos;

        const uint32_t W = 520, H = 300;
        outInfo.width = W;
        outInfo.height = H;
        outInfo.origWidth = W;
        outInfo.origHeight = H;
        outInfo.channels = 4;
        outInfo.bitsPerChannel = 8;
        outInfo.formatName = L"WOFF2 WebFont (" + std::to_wstring(numTables) + L" tables, " +
                             std::to_wstring(totalSfntSize) + L" B SFNT)";
        outInfo.bgraPixels.assign(static_cast<size_t>(W) * H * 4, 248);
        for (size_t i = 0; i < static_cast<size_t>(W) * H; ++i) outInfo.bgraPixels[i * 4 + 3] = 255;

        // Top header bar
        for (uint32_t y = 0; y < 38; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(y) * W + x) * 4], 28, 42, 68, 255);
            }
        }
        DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, W, H, 14, 12, "WOFF2 BROTLI WEBFONT", 2, 240, 246, 255);

        // Visualize each parsed SFNT table proportional bar + Brotli stream fingerprint glyphs
        uint32_t maxTblLen = 1;
        for (const auto& t : tbls) maxTblLen = std::max(maxTblLen, t.origLen);

        uint32_t rowY = 52;
        for (size_t ti = 0; ti < std::min<size_t>(tbls.size(), 10) && rowY + 20 < H; ++ti) {
            const auto& t = tbls[ti];
            DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, W, H, 16, rowY + 3, std::string_view(t.tag, 4), 2, 35, 50, 75);
            uint32_t barMaxW = W - 110;
            uint32_t origBarW = std::max(4u, static_cast<uint32_t>((static_cast<uint64_t>(t.origLen) * barMaxW) / maxTblLen));
            uint32_t xfBarW   = std::max(2u, static_cast<uint32_t>((static_cast<uint64_t>(t.xformLen) * barMaxW) / maxTblLen));
            for (uint32_t py = rowY; py < rowY + 14; ++py) {
                for (uint32_t px = 90; px < 90 + std::min(barMaxW, origBarW); ++px) {
                    uint8_t sByte = compStream[((ti * 37u + (px - 90)) % totalCompSize)];
                    bool inXf = (px < 90 + std::min(barMaxW, xfBarW));
                    uint8_t r = inXf ? static_cast<uint8_t>(45 + (sByte & 0x3Fu)) : 195;
                    uint8_t g = inXf ? static_cast<uint8_t>(115 + ((sByte >> 2) & 0x3Fu)) : 208;
                    uint8_t b = inXf ? static_cast<uint8_t>(195 + ((sByte >> 4) & 0x37u)) : 225;
                    WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(py) * W + px) * 4], r, g, b, 255);
                }
            }
            rowY += 22;
        }
        outInfo.valid = true;
        return true;
    }

    // ------------------------------------------------------------------------
    // WOFF 1.0 ('wOFF') -> Reconstruct uncompressed SFNT buffer
    // TTF / OTF / TTC   -> Direct SFNT buffer
    // ------------------------------------------------------------------------
    std::vector<uint8_t> reconstructedSfnt;
    const uint8_t* sfntPtr = data;
    size_t sfntLen = size;
    bool fromWoff = false;

    if (size >= 44 && std::memcmp(data, "wOFF", 4) == 0) {
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
    std::wstring kindTag    = fromWoff ? L"WOFF WebFont" :
                              (sfnt.flavor == DetailFont::MakeSfntTag("OTTO") ? L"OpenType CFF" : L"TrueType");
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

    // Pure-C++ TrueType 'glyf' scanline contour rasterizer fallback (and direct contour verification!)
    if (!gdiRenderedRealFont) {
        if (!sfnt.tblGlyf.valid || !sfnt.tblLoca.valid || sfnt.numGlyphs == 0) {
            return false; // Neither GDI accepted the font nor TrueType glyf contours exist
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
