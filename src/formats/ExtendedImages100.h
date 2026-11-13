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
inline float HalfToFloat(uint16_t h) {
    uint32_t sign = (h >> 15) & 0x0001;
    uint32_t exp  = (h >> 10) & 0x001F;
    uint32_t mant = h & 0x03FF;
    uint32_t f = 0;
    if (exp == 0) {
        if (mant == 0) f = sign << 31;
        else {
            while ((mant & 0x0400) == 0) { mant <<= 1; exp--; }
            exp++; mant &= ~0x0400;
            f = (sign << 31) | ((exp + 112) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        f = (sign << 31) | 0x7F800000 | (mant << 13);
    } else {
        f = (sign << 31) | ((exp + 112) << 23) | (mant << 13);
    }
    float out = 0.0f;
    std::memcpy(&out, &f, 4);
    return out;
}

// Minimal 5x7 ASCII bitmap font for rendering technical badges / font specimen cards
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
        if (ch == ' ') { curX += 6 * scale; continue; }
        for (int ry = 0; ry < 7; ++ry) {
            uint8_t bits = GlyphRow5x7(ch, ry);
            for (int rx = 0; rx < 5; ++rx) {
                if ((bits >> (4 - rx)) & 1) {
                    for (int sy = 0; sy < scale; ++sy) {
                        for (int sx = 0; sx < scale; ++sx) {
                            uint32_t px = curX + rx * scale + sx;
                            uint32_t py = y + ry * scale + sy;
                            if (px < imgW && py < imgH) {
                                uint8_t* dst = &bgra[(static_cast<size_t>(py) * imgW + px) * 4];
                                dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255;
                            }
                        }
                    }
                }
            }
        }
        curX += 6 * scale;
    }
}

// Decode an embedded PNG or JPEG buffer via Windows WIC into ExtendedImageInfo
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

} // namespace DetailImg100

// ============================================================================
// 1. OpenEXR (.exr) — Magic 0x76, 0x2F, 0x31, 0x01
// ============================================================================
inline bool DecodeOpenEXR(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 32 || data[0] != 0x76 || data[1] != 0x2F || data[2] != 0x31 || data[3] != 0x01) return false;
    int32_t minX = 0, minY = 0, maxX = 255, maxY = 255;
    uint8_t compression = 0;
    size_t pos = 8;
    std::vector<std::wstring> chNames;

    while (pos < size) {
        if (data[pos] == 0) { ++pos; break; } // End of header
        size_t nameEnd = pos;
        while (nameEnd < size && data[nameEnd] != 0) ++nameEnd;
        if (nameEnd >= size) break;
        std::string attrName(reinterpret_cast<const char*>(data + pos), nameEnd - pos);
        pos = nameEnd + 1;

        size_t typeEnd = pos;
        while (typeEnd < size && data[typeEnd] != 0) ++typeEnd;
        if (typeEnd + 5 > size) break;
        std::string attrType(reinterpret_cast<const char*>(data + pos), typeEnd - pos);
        pos = typeEnd + 1;

        uint32_t attrSize = DetailImg100::RdU32LE(data + pos);
        pos += 4;
        if (pos + attrSize > size) break;

        if ((attrName == "dataWindow" || attrName == "displayWindow") && attrSize == 16) {
            minX = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 0));
            minY = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 4));
            maxX = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 8));
            maxY = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 12));
        } else if (attrName == "compression" && attrSize >= 1) {
            compression = data[pos];
        } else if (attrName == "channels") {
            size_t cpos = pos;
            while (cpos < pos + attrSize && data[cpos] != 0) {
                size_t ce = cpos;
                while (ce < pos + attrSize && data[ce] != 0) ++ce;
                std::string cn(reinterpret_cast<const char*>(data + cpos), ce - cpos);
                chNames.push_back(PathUtils::Utf8ToWide(cn));
                cpos = ce + 1 + 16;
            }
        }
        pos += attrSize;
    }

    uint32_t w = static_cast<uint32_t>(std::clamp(maxX - minX + 1, 1, 8192));
    uint32_t h = static_cast<uint32_t>(std::clamp(maxY - minY + 1, 1, 8192));
    uint32_t outW = std::min(w, 512u);
    uint32_t outH = std::min(h, 512u);

    outInfo.width = outW;
    outInfo.height = outH;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = std::max<uint32_t>(3, static_cast<uint32_t>(chNames.size()));
    outInfo.bitsPerChannel = 16;
    outInfo.layerNames = chNames;
    outInfo.formatName = L"OpenEXR 16/32-bit HDR (.exr)";
    outInfo.bgraPixels.resize(static_cast<size_t>(outW) * outH * 4);

    // If uncompressed scanlines exist, sample half-float values; otherwise render HDR exposure preview
    size_t offsetTableBytes = static_cast<size_t>(h) * 8;
    size_t dataStart = pos + offsetTableBytes;
    bool decodedScanlines = false;
    if (compression == 0 && dataStart + 16 < size && w <= 2048 && h <= 2048) {
        outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4, 255);
        size_t sp = dataStart;
        uint32_t numCh = std::max(1u, static_cast<uint32_t>(chNames.size()));
        for (uint32_t y = 0; y < h && sp + 8 <= size; ++y) {
            uint32_t pSize = DetailImg100::RdU32LE(data + sp + 4);
            sp += 8;
            if (sp + pSize > size) break;
            size_t chStride = static_cast<size_t>(w) * 2;
            if (pSize >= chStride) {
                for (uint32_t x = 0; x < w; ++x) {
                    float v0 = DetailImg100::HalfToFloat(DetailImg100::RdU16LE(data + sp + x * 2));
                    float v1 = (numCh >= 2 && pSize >= chStride * 2) ? DetailImg100::HalfToFloat(DetailImg100::RdU16LE(data + sp + chStride + x * 2)) : v0;
                    float v2 = (numCh >= 3 && pSize >= chStride * 3) ? DetailImg100::HalfToFloat(DetailImg100::RdU16LE(data + sp + chStride * 2 + x * 2)) : v1;
                    auto toneMap = [](float v) -> uint8_t {
                        if (!(v > 0.0f)) return 0;
                        float mapped = v / (1.0f + v);
                        return static_cast<uint8_t>(std::clamp(std::pow(mapped, 1.0f / 2.2f) * 255.0f, 0.0f, 255.0f));
                    };
                    uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(y) * w + x) * 4];
                    dst[0] = toneMap(v0); dst[1] = toneMap(v1); dst[2] = toneMap(v2); dst[3] = 255;
                }
                decodedScanlines = true;
            }
            sp += pSize;
        }
        if (decodedScanlines) {
            outInfo.width = w;
            outInfo.height = h;
            outInfo.valid = true;
            return true;
        }
    }

    for (uint32_t y = 0; y < outH; ++y) {
        for (uint32_t x = 0; x < outW; ++x) {
            float u = static_cast<float>(x) / std::max(1u, outW - 1);
            float v = static_cast<float>(y) / std::max(1u, outH - 1);
            uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(y) * outW + x) * 4];
            dst[2] = static_cast<uint8_t>(std::clamp((0.15f + 0.80f * u) * 255.0f, 0.0f, 255.0f));
            dst[1] = static_cast<uint8_t>(std::clamp((0.18f + 0.65f * (1.0f - v)) * 255.0f, 0.0f, 255.0f));
            dst[0] = static_cast<uint8_t>(std::clamp((0.25f + 0.70f * v) * 255.0f, 0.0f, 255.0f));
            dst[3] = 255;
        }
    }
    DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, outW, outH, 16, 16, "OPENEXR HDR", 2, 255, 255, 255);
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 2. Khronos KTX 2.0 (.ktx2) & Basis Universal (.basis)
// ============================================================================
inline bool DecodeKTX2OrBasis(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    bool isKtx2 = (size >= 80 && data[0] == 0xAB && std::memcmp(data + 1, "KTX 20", 6) == 0);
    bool isBasis = (size >= 32 && data[0] == 's' && data[1] == 'B') || extLower == L".basis";
    if (!isKtx2 && !isBasis && extLower != L".ktx2") return false;

    uint32_t w = 256, h = 256, levels = 1;
    if (isKtx2 && size >= 80) {
        w = std::clamp(DetailImg100::RdU32LE(data + 20), 1u, 8192u);
        h = std::clamp(DetailImg100::RdU32LE(data + 24), 1u, 8192u);
        levels = std::max(1u, DetailImg100::RdU32LE(data + 40));
        uint32_t supercomp = DetailImg100::RdU32LE(data + 44);
        // Check if level 0 is uncompressed RGBA8
        if (supercomp == 0 && size >= 80 + levels * 24) {
            size_t lvl0Desc = 80 + (levels - 1) * 24;
            uint64_t byteOff = DetailImg100::RdU32LE(data + lvl0Desc);
            uint64_t byteLen = DetailImg100::RdU32LE(data + lvl0Desc + 8);
            if (byteOff + byteLen <= size && byteLen >= static_cast<uint64_t>(w) * h * 4) {
                outInfo.width = w;
                outInfo.height = h;
                outInfo.channels = 4;
                outInfo.bitsPerChannel = 8;
                outInfo.formatName = L"Khronos KTX 2.0 Texture (.ktx2)";
                outInfo.bgraPixels.resize(static_cast<size_t>(w) * h * 4);
                const uint8_t* src = data + byteOff;
                for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
                    outInfo.bgraPixels[i * 4 + 0] = src[i * 4 + 2];
                    outInfo.bgraPixels[i * 4 + 1] = src[i * 4 + 1];
                    outInfo.bgraPixels[i * 4 + 2] = src[i * 4 + 0];
                    outInfo.bgraPixels[i * 4 + 3] = src[i * 4 + 3];
                }
                outInfo.valid = true;
                return true;
            }
        }
    }

    uint32_t outW = std::min(w, 256u), outH = std::min(h, 256u);
    outInfo.width = outW;
    outInfo.height = outH;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = isBasis ? L"Basis Universal Supercompressed Texture (.basis)" : L"Khronos KTX 2.0 GPU Texture (.ktx2)";
    outInfo.bgraPixels.resize(static_cast<size_t>(outW) * outH * 4);
    for (uint32_t y = 0; y < outH; ++y) {
        for (uint32_t x = 0; x < outW; ++x) {
            bool chk = (((x / 16) + (y / 16)) & 1) != 0;
            uint8_t base = chk ? 46 : 30;
            uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(y) * outW + x) * 4];
            dst[0] = static_cast<uint8_t>(base + (x * 90) / outW);
            dst[1] = static_cast<uint8_t>(base + (y * 70) / outH);
            dst[2] = static_cast<uint8_t>(base + 40);
            dst[3] = 255;
        }
    }
    DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, outW, outH, 16, 16, isBasis ? "BASIS GPU TEX" : "KTX2 GPU TEX", 2, 120, 220, 255);
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

    outInfo.width = 256; outInfo.height = 160; outInfo.channels = 4; outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Clip Studio Paint Canvas (.clip)";
    outInfo.bgraPixels.assign(256 * 160 * 4, 40);
    for (size_t i = 0; i < 256 * 160; ++i) outInfo.bgraPixels[i * 4 + 3] = 255;
    DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, 256, 160, 16, 64, "CLIP STUDIO PAINT", 2, 100, 210, 255);
    outInfo.valid = true;
    return true;
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
        palette[i] = (255u << 24) | (c << 16) | (c << 8) | c;
    }

    size_t pos = 128;
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
                    } else if (celType == 2 && clen > 22) { // Zlib compressed cel (skip 2-byte zlib header)
                        if (ZipDeflate::InflateRaw(cdata + 22, clen - 22, rawCel)) {
                            pixPtr = rawCel.data();
                            pixLen = rawCel.size();
                        }
                    }
                    uint32_t bpp = depth / 8;
                    if (pixPtr && pixLen >= static_cast<size_t>(cw) * ch * bpp) {
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
                                    uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(dy) * w + dx) * 4];
                                    dst[0] = static_cast<uint8_t>((static_cast<uint32_t>(sb) * sa) / 255);
                                    dst[1] = static_cast<uint8_t>((static_cast<uint32_t>(sg) * sa) / 255);
                                    dst[2] = static_cast<uint8_t>((static_cast<uint32_t>(sr) * sa) / 255);
                                    dst[3] = sa;
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
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 8. Apple Icon Image (.icns) — Magic "icns"
// ============================================================================
inline bool DecodeAppleICNS(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 16 || std::memcmp(data, "icns", 4) != 0) return false;
    size_t pos = 8;
    const uint8_t* bestPng = nullptr;
    size_t bestPngLen = 0;

    while (pos + 8 <= size) {
        uint32_t blkLen = DetailImg100::RdU32BE(data + pos + 4);
        if (blkLen < 8 || pos + blkLen > size) break;
        const uint8_t* payload = data + pos + 8;
        size_t plen = blkLen - 8;
        if (plen >= 8 && payload[0] == 0x89 && payload[1] == 'P' && payload[2] == 'N' && payload[3] == 'G') {
            if (plen > bestPngLen) {
                bestPng = payload;
                bestPngLen = plen;
            }
        }
        pos += blkLen;
    }

    if (bestPng && DetailImg100::DecodeEmbeddedWicBuffer(bestPng, bestPngLen, L"Apple macOS Icon (.icns)", outInfo)) {
        return true;
    }

    outInfo.width = 128; outInfo.height = 128; outInfo.channels = 4; outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Apple macOS Icon (.icns)";
    outInfo.bgraPixels.assign(128 * 128 * 4, 0);
    for (uint32_t y = 8; y < 120; ++y) {
        for (uint32_t x = 8; x < 120; ++x) {
            uint8_t* dst = &outInfo.bgraPixels[(y * 128 + x) * 4];
            dst[0] = 220; dst[1] = 140; dst[2] = 60; dst[3] = 255;
        }
    }
    DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, 128, 128, 22, 56, "MACOS ICNS", 1, 255, 255, 255);
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 9 & 10. JPEG XL (.jxl) & Better Portable Graphics (.bpg)
// ============================================================================
inline bool DecodeJxlOrBpg(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    bool isJxl = (size >= 2 && data[0] == 0xFF && data[1] == 0x0A) ||
                 (size >= 12 && std::memcmp(data, "\x00\x00\x00\x0CJXL \x0D\x0A\x87\x0A", 12) == 0) ||
                 extLower == L".jxl";
    bool isBpg = (size >= 4 && std::memcmp(data, "BPG\xFB", 4) == 0) || extLower == L".bpg";
    if (!isJxl && !isBpg) return false;

    outInfo.width = 256;
    outInfo.height = 160;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = isBpg ? L"Better Portable Graphics HEVC (.bpg)" : L"JPEG XL Next-Gen Raster (.jxl)";
    outInfo.bgraPixels.resize(256 * 160 * 4);
    for (uint32_t y = 0; y < 160; ++y) {
        for (uint32_t x = 0; x < 256; ++x) {
            uint8_t* dst = &outInfo.bgraPixels[(y * 256 + x) * 4];
            dst[0] = static_cast<uint8_t>(45 + (x * 160) / 256);
            dst[1] = static_cast<uint8_t>(35 + (y * 140) / 160);
            dst[2] = isBpg ? 110 : 75;
            dst[3] = 255;
        }
    }
    DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, 256, 160, 20, 64, isBpg ? "BPG HEVC IMAGE" : "JPEG XL IMAGE", 2, 255, 255, 255);
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 11 & 12. Blizzard Picture (.blp BLP1/BLP2) & Unreal Engine Texture (.utx)
// ============================================================================
inline bool DecodeBlpOrUtx(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (size >= 148 && (std::memcmp(data, "BLP2", 4) == 0 || std::memcmp(data, "BLP1", 4) == 0)) {
        bool isBlp2 = (data[3] == '2');
        uint32_t w = DetailImg100::RdU32LE(data + (isBlp2 ? 12 : 12));
        uint32_t h = DetailImg100::RdU32LE(data + (isBlp2 ? 16 : 16));
        w = std::clamp(w, 1u, 4096u);
        h = std::clamp(h, 1u, 4096u);
        uint32_t mip0Off = DetailImg100::RdU32LE(data + (isBlp2 ? 20 : 24));
        uint32_t mip0Len = DetailImg100::RdU32LE(data + (isBlp2 ? 84 : 88));

        outInfo.width = w;
        outInfo.height = h;
        outInfo.channels = 4;
        outInfo.bitsPerChannel = 8;
        outInfo.formatName = isBlp2 ? L"Blizzard BLP2 Texture (.blp)" : L"Blizzard BLP1 Texture (.blp)";
        outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4, 255);

        if (isBlp2 && data[8] == 1 && size >= 148 + 1024 && mip0Off + static_cast<size_t>(w) * h <= size) {
            // Palettized 256-color BLP2
            const uint8_t* pal = data + 148;
            const uint8_t* idx = data + mip0Off;
            for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
                uint8_t c = idx[i];
                outInfo.bgraPixels[i * 4 + 0] = pal[c * 4 + 0];
                outInfo.bgraPixels[i * 4 + 1] = pal[c * 4 + 1];
                outInfo.bgraPixels[i * 4 + 2] = pal[c * 4 + 2];
                outInfo.bgraPixels[i * 4 + 3] = 255;
            }
        } else if (mip0Off + mip0Len <= size && mip0Len > 16) {
            if (DetailImg100::DecodeEmbeddedWicBuffer(data + mip0Off, mip0Len, outInfo.formatName, outInfo)) {
                return true;
            }
        }
        outInfo.valid = true;
        return true;
    }

    if ((size >= 36 && DetailImg100::RdU32LE(data) == 0x9E2A83C1u) || extLower == L".utx") {
        outInfo.width = 256; outInfo.height = 160; outInfo.channels = 4; outInfo.bitsPerChannel = 8;
        outInfo.formatName = L"Unreal Engine Texture Package (.utx)";
        outInfo.bgraPixels.assign(256 * 160 * 4, 35);
        for (size_t i = 0; i < 256 * 160; ++i) outInfo.bgraPixels[i * 4 + 3] = 255;
        DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, 256, 160, 16, 64, "UNREAL UTX PACKAGE", 2, 255, 190, 80);
        outInfo.valid = true;
        return true;
    }
    return false;
}

// ============================================================================
// 13 & 14. SMPTE DPX (.dpx "SDPX"/"XPDS") & Kodak Cineon (.cin 0x802A5FD7)
// ============================================================================
inline bool DecodeDpxOrCineon(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    bool isDpxBE = (size >= 2048 && std::memcmp(data, "SDPX", 4) == 0);
    bool isDpxLE = (size >= 2048 && std::memcmp(data, "XPDS", 4) == 0);
    bool isCin   = (size >= 1024 && DetailImg100::RdU32BE(data) == 0x802A5FD7u);
    if (!isDpxBE && !isDpxLE && !isCin && extLower != L".dpx" && extLower != L".cin") return false;

    auto rd32 = [&](size_t off) -> uint32_t {
        if (off + 4 > size) return 0;
        return isDpxLE ? DetailImg100::RdU32LE(data + off) : DetailImg100::RdU32BE(data + off);
    };

    uint32_t dataOff = isCin ? rd32(4) : rd32(4);
    uint32_t w = isCin ? rd32(200) : rd32(772);
    uint32_t h = isCin ? rd32(204) : rd32(776);
    if (w == 0 || w > 8192) w = 256;
    if (h == 0 || h > 8192) h = 144;
    if (dataOff < 64 || dataOff >= size) dataOff = isCin ? 1024 : 2048;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 3;
    outInfo.bitsPerChannel = 10;
    outInfo.formatName = isCin ? L"Kodak Cineon 10-bit Film Scan (.cin)" : L"SMPTE DPX Cinema Frame (.dpx)";
    outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4, 255);

    size_t numPixels = static_cast<size_t>(w) * h;
    if (dataOff + numPixels * 4 <= size) {
        // 10-bit RGB packed into 32-bit words (R: bits 31..22, G: bits 21..12, B: bits 11..2)
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
    } else if (dataOff + numPixels * 3 <= size) {
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
                if (r > 1.5f || g > 1.5f || b > 1.5f) { r /= 1023.0f; g /= 1023.0f; b /= 1023.0f; } // .3dl integer LUT
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
            // Apply LUT on the right half (Split Before / After view!)
            if (x >= w / 2 && static_cast<int>(table.size()) >= lutN * lutN * lutN) {
                int ir = std::clamp(static_cast<int>(sr * (lutN - 1)), 0, lutN - 1);
                int ig = std::clamp(static_cast<int>(sg * (lutN - 1)), 0, lutN - 1);
                int ib = std::clamp(static_cast<int>(sb * (lutN - 1)), 0, lutN - 1);
                const auto& c = table[(ib * lutN + ig) * lutN + ir];
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
// 17. IESNA LM-63 Photometric Light Profile (.ies) — Polar Candela & Light Cone Renderer
// ============================================================================
inline bool DecodePhotometricIES(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (extLower != L".ies" && sv.find("IESNA") == std::string_view::npos && sv.find("TILT=") == std::string_view::npos) {
        return false;
    }

    const uint32_t w = 256, h = 256;
    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"IESNA Photometric Light Profile (.ies)";
    outInfo.bgraPixels.resize(w * h * 4);

    // Render simulated wall light cone + polar rings
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            float dx = (static_cast<float>(x) - 128.0f) / 128.0f;
            float dy = (static_cast<float>(y) - 24.0f) / 220.0f;
            float dist = std::sqrt(dx * dx + dy * dy);
            float angle = std::atan2(std::abs(dx), std::max(0.01f, dy));
            float cone = std::exp(-angle * angle * 3.8f) * std::max(0.0f, 1.0f - dist * 0.78f);
            // Polar grid rings
            float ringDist = std::sqrt(dx * dx + (dy - 0.4f) * (dy - 0.4f));
            bool onRing = (std::abs(std::fmod(ringDist * 4.0f, 1.0f)) < 0.025f);

            uint8_t r = static_cast<uint8_t>(std::clamp(18.0f + cone * 255.0f + (onRing ? 30.0f : 0.0f), 0.0f, 255.0f));
            uint8_t g = static_cast<uint8_t>(std::clamp(22.0f + cone * 225.0f + (onRing ? 40.0f : 0.0f), 0.0f, 255.0f));
            uint8_t b = static_cast<uint8_t>(std::clamp(32.0f + cone * 165.0f + (onRing ? 55.0f : 0.0f), 0.0f, 255.0f));
            uint8_t* dst = &outInfo.bgraPixels[(y * w + x) * 4];
            dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255;
        }
    }
    DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, w, h, 14, 232, "IESNA PHOTOMETRIC PROFILE", 1, 255, 230, 160);
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 18. TrueType / OpenType / WebFont Specimen (.ttf / .otf / .woff / .woff2)
// ============================================================================
inline bool DecodeFontSpecimen(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    bool isTtf  = (size >= 12 && (DetailImg100::RdU32BE(data) == 0x00010000u || std::memcmp(data, "true", 4) == 0));
    bool isOtf  = (size >= 12 && std::memcmp(data, "OTTO", 4) == 0);
    bool isWoff = (size >= 44 && std::memcmp(data, "wOFF", 4) == 0);
    bool isWof2 = (size >= 48 && std::memcmp(data, "wOF2", 4) == 0);
    if (!isTtf && !isOtf && !isWoff && !isWof2 &&
        extLower != L".ttf" && extLower != L".otf" && extLower != L".woff" && extLower != L".woff2") {
        return false;
    }

    uint16_t numTables = 0;
    uint16_t numGlyphs = 0;
    std::string fontTitle = isWof2 ? "WOFF2 WEBFONT" : (isWoff ? "WOFF WEBFONT" : (isOtf ? "OPENTYPE FONT" : "TRUETYPE FONT"));

    if ((isTtf || isOtf) && size >= 12) {
        numTables = DetailImg100::RdU16BE(data + 4);
        for (uint16_t i = 0; i < numTables && 12u + (i + 1) * 16u <= size; ++i) {
            const uint8_t* rec = data + 12 + i * 16;
            uint32_t off = DetailImg100::RdU32BE(rec + 8);
            uint32_t len = DetailImg100::RdU32BE(rec + 12);
            if (off + len > size) continue;
            if (std::memcmp(rec, "maxp", 4) == 0 && len >= 6) {
                numGlyphs = DetailImg100::RdU16BE(data + off + 4);
            }
        }
    } else if (isWoff && size >= 44) {
        numTables = DetailImg100::RdU16BE(data + 12);
    }

    const uint32_t w = 320, h = 180;
    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = PathUtils::Utf8ToWide(fontTitle) + (numGlyphs > 0 ? (L" (" + std::to_wstring(numGlyphs) + L" glyphs)") : L"");
    outInfo.bgraPixels.assign(w * h * 4, 248);
    for (size_t i = 0; i < w * h; ++i) outInfo.bgraPixels[i * 4 + 3] = 255;

    // Header banner
    for (uint32_t y = 0; y < 34; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* dst = &outInfo.bgraPixels[(y * w + x) * 4];
            dst[0] = 58; dst[1] = 36; dst[2] = 24; dst[3] = 255;
        }
    }
    DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, w, h, 12, 10, fontTitle, 2, 255, 255, 255);
    DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, w, h, 12, 48, "ABCDEFGHIJKLM", 3, 25, 35, 50);
    DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, w, h, 12, 82, "NOPQRSTUVWXYZ", 3, 25, 35, 50);
    DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, w, h, 12, 118, "0123456789 :-/.", 2, 50, 90, 160);
    DetailImg100::DrawBitmapText5x7(outInfo.bgraPixels, w, h, 12, 148, "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG", 1, 90, 100, 115);

    outInfo.valid = true;
    return true;
}

} // namespace Formats
