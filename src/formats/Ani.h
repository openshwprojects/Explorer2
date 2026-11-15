#pragma once
// Ani.h — Windows Animated Cursor RIFF Container (.ani) Decoder

namespace Formats {

namespace DetailAni {

inline uint16_t RdU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

inline uint32_t RdU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline void StorePremulBgra(uint8_t* dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (a == 255u) {
        dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255u;
    } else if (a == 0u) {
        dst[0] = 0u; dst[1] = 0u; dst[2] = 0u; dst[3] = 0u;
    } else {
        dst[0] = static_cast<uint8_t>((static_cast<uint32_t>(b) * a + 127u) / 255u);
        dst[1] = static_cast<uint8_t>((static_cast<uint32_t>(g) * a + 127u) / 255u);
        dst[2] = static_cast<uint8_t>((static_cast<uint32_t>(r) * a + 127u) / 255u);
        dst[3] = a;
    }
}

inline uint8_t ExtractBitfield8(uint32_t px, uint32_t mask) {
    if (mask == 0u) return 0u;
    uint32_t shift = 0u;
    while (((mask >> shift) & 1u) == 0u && shift < 32u) ++shift;
    uint32_t m = mask >> shift;
    uint32_t maxVal = 0u;
    uint32_t bits = 0u;
    while (((m >> bits) & 1u) != 0u && bits < 32u) ++bits;
    if (bits == 0u) return 0u;
    maxVal = (bits >= 32u) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    uint32_t v = (px >> shift) & maxVal;
    if (bits >= 8u) return static_cast<uint8_t>(v >> (bits - 8u));
    return static_cast<uint8_t>((v * 255u + (maxVal / 2u)) / maxVal);
}

inline bool DecodePngViaWic(const uint8_t* pngData, size_t pngSize, ExtendedImageInfo& outInfo) {
    if (!pngData || pngSize < 8 || pngSize > 0x7FFFFFFFu) return false;
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool needUninit = (hrCo == S_OK || hrCo == S_FALSE);

    bool ok = false;
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr) && factory) {
        hr = factory->CreateStream(&stream);
        if (SUCCEEDED(hr) && stream) {
            hr = stream->InitializeFromMemory(const_cast<BYTE*>(pngData), static_cast<DWORD>(pngSize));
        }
        if (SUCCEEDED(hr)) {
            hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
        }
        if (SUCCEEDED(hr) && decoder) {
            hr = decoder->GetFrame(0, &frame);
        }
        UINT w = 0, h = 0;
        if (SUCCEEDED(hr) && frame) {
            hr = frame->GetSize(&w, &h);
            if (SUCCEEDED(hr) && (w == 0 || h == 0 || w > 1024u || h > 1024u)) {
                hr = E_FAIL;
            }
        }
        if (SUCCEEDED(hr) && frame) {
            hr = factory->CreateFormatConverter(&converter);
        }
        if (SUCCEEDED(hr) && converter) {
            hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                       WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        }
        if (SUCCEEDED(hr) && converter) {
            size_t totalBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
            outInfo.bgraPixels.resize(totalBytes);
            hr = converter->CopyPixels(nullptr, w * 4u, static_cast<UINT>(totalBytes), outInfo.bgraPixels.data());
            if (SUCCEEDED(hr)) {
                outInfo.width = w;
                outInfo.height = h;
                outInfo.channels = 4;
                outInfo.bitsPerChannel = 8;
                outInfo.formatName = L"Windows Animated Cursor (.ani)";
                outInfo.valid = true;
                ok = true;
            }
        }
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    if (needUninit) CoUninitialize();
    return ok;
}

inline bool DecodeIcoDibEntry(const uint8_t* sub, size_t subLen, uint8_t dirH, ExtendedImageInfo& outInfo) {
    if (subLen < 40) return false;
    uint32_t biSize = RdU32LE(sub);
    if (biSize < 40u || biSize > 256u || biSize > subLen) return false;

    int32_t biWidth = static_cast<int32_t>(RdU32LE(sub + 4));
    int32_t biHeightRaw = static_cast<int32_t>(RdU32LE(sub + 8));
    uint16_t biBitCount = RdU16LE(sub + 14);
    uint32_t biCompression = RdU32LE(sub + 16);
    uint32_t biClrUsed = RdU32LE(sub + 32);

    if (biWidth <= 0 || biWidth > 1024 || biHeightRaw == 0) return false;
    int64_t absH64 = (biHeightRaw < 0) ? -static_cast<int64_t>(biHeightRaw) : static_cast<int64_t>(biHeightRaw);
    uint32_t width = static_cast<uint32_t>(biWidth);
    // In ICO/CUR DIBs biHeight is 2 * height (XOR + AND mask); tolerate non-doubled synthetic headers matching dirH
    uint32_t height = (dirH != 0u && absH64 == static_cast<int64_t>(dirH))
                      ? static_cast<uint32_t>(absH64)
                      : static_cast<uint32_t>(absH64 / 2);
    if (height == 0u || height > 1024u) return false;

    if (biBitCount != 1u && biBitCount != 4u && biBitCount != 8u &&
        biBitCount != 16u && biBitCount != 24u && biBitCount != 32u) {
        return false;
    }
    if (biCompression != 0u && !(biCompression == 3u && (biBitCount == 16u || biBitCount == 32u))) {
        return false;
    }

    size_t palOff = biSize;
    uint32_t rMask = 0x00FF0000u;
    uint32_t gMask = 0x0000FF00u;
    uint32_t bMask = 0x000000FFu;
    uint32_t aMask = 0xFF000000u;

    if (biBitCount == 16u && biCompression == 0u) {
        rMask = 0x7C00u;
        gMask = 0x03E0u;
        bMask = 0x001Fu;
        aMask = 0u;
    } else if (biCompression == 3u) {
        if (biSize == 40u) {
            if (palOff + 12u > subLen) return false;
            rMask = RdU32LE(sub + 40);
            gMask = RdU32LE(sub + 44);
            bMask = RdU32LE(sub + 48);
            aMask = 0u;
            palOff += 12u;
        } else if (biSize >= 52u) {
            rMask = RdU32LE(sub + 40);
            gMask = RdU32LE(sub + 44);
            bMask = RdU32LE(sub + 48);
            aMask = (biSize >= 56u) ? RdU32LE(sub + 52) : 0u;
        }
    }

    uint32_t clrCount = 0u;
    if (biBitCount <= 8u) {
        uint32_t maxPal = 1u << biBitCount;
        clrCount = (biClrUsed > 0u && biClrUsed <= maxPal) ? biClrUsed : maxPal;
    } else if (biClrUsed <= 256u) {
        clrCount = biClrUsed;
    }

    size_t palBytes = static_cast<size_t>(clrCount) * 4u;
    if (palOff + palBytes > subLen) return false;
    const uint8_t* palette = sub + palOff;
    size_t xorOff = palOff + palBytes;

    size_t xorStride = ((static_cast<size_t>(width) * biBitCount + 31u) / 32u) * 4u;
    size_t xorBytes = xorStride * height;
    if (xorBytes > subLen - xorOff) return false;

    size_t andOff = xorOff + xorBytes;
    size_t andStride = ((static_cast<size_t>(width) + 31u) / 32u) * 4u;
    size_t andBytes = andStride * height;
    bool hasAndMask = (andBytes <= subLen - andOff);

    bool hasPerPixelAlpha = false;
    if (biBitCount == 32u) {
        for (uint32_t y = 0; y < height && !hasPerPixelAlpha; ++y) {
            const uint8_t* row = sub + xorOff + static_cast<size_t>(y) * xorStride;
            for (uint32_t x = 0; x < width; ++x) {
                uint8_t a = (biCompression == 3u && aMask != 0u)
                            ? ExtractBitfield8(RdU32LE(row + x * 4u), aMask)
                            : row[x * 4u + 3u];
                if (a != 0u) {
                    hasPerPixelAlpha = true;
                    break;
                }
            }
        }
    }

    outInfo.width = width;
    outInfo.height = height;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Windows Animated Cursor (.ani)";
    outInfo.bgraPixels.resize(static_cast<size_t>(width) * height * 4u);

    const bool bottomUp = (biHeightRaw > 0);
    for (uint32_t y = 0; y < height; ++y) {
        size_t srcY = bottomUp ? static_cast<size_t>(height - 1u - y) : static_cast<size_t>(y);
        const uint8_t* xorRow = sub + xorOff + srcY * xorStride;
        const uint8_t* andRow = hasAndMask ? (sub + andOff + srcY * andStride) : nullptr;
        uint8_t* dstRow = &outInfo.bgraPixels[static_cast<size_t>(y) * width * 4u];

        for (uint32_t x = 0; x < width; ++x) {
            uint8_t r = 0u, g = 0u, b = 0u, a = 255u;
            if (biBitCount == 1u) {
                uint32_t idx = (xorRow[x >> 3] >> (7u - (x & 7u))) & 1u;
                if (idx < clrCount) {
                    b = palette[idx * 4u + 0u];
                    g = palette[idx * 4u + 1u];
                    r = palette[idx * 4u + 2u];
                }
            } else if (biBitCount == 4u) {
                uint8_t byte = xorRow[x >> 1];
                uint32_t idx = ((x & 1u) == 0u) ? (byte >> 4) : (byte & 0x0Fu);
                if (idx < clrCount) {
                    b = palette[idx * 4u + 0u];
                    g = palette[idx * 4u + 1u];
                    r = palette[idx * 4u + 2u];
                }
            } else if (biBitCount == 8u) {
                uint32_t idx = xorRow[x];
                if (idx < clrCount) {
                    b = palette[idx * 4u + 0u];
                    g = palette[idx * 4u + 1u];
                    r = palette[idx * 4u + 2u];
                }
            } else if (biBitCount == 16u) {
                uint32_t px = RdU16LE(xorRow + x * 2u);
                r = ExtractBitfield8(px, rMask);
                g = ExtractBitfield8(px, gMask);
                b = ExtractBitfield8(px, bMask);
            } else if (biBitCount == 24u) {
                b = xorRow[x * 3u + 0u];
                g = xorRow[x * 3u + 1u];
                r = xorRow[x * 3u + 2u];
            } else { // 32bpp
                if (biCompression == 3u) {
                    uint32_t px = RdU32LE(xorRow + x * 4u);
                    r = ExtractBitfield8(px, rMask);
                    g = ExtractBitfield8(px, gMask);
                    b = ExtractBitfield8(px, bMask);
                    a = (aMask != 0u) ? ExtractBitfield8(px, aMask) : 0u;
                } else {
                    b = xorRow[x * 4u + 0u];
                    g = xorRow[x * 4u + 1u];
                    r = xorRow[x * 4u + 2u];
                    a = xorRow[x * 4u + 3u];
                }
            }

            if (biBitCount < 32u || !hasPerPixelAlpha) {
                if (andRow) {
                    uint8_t maskBit = (andRow[x >> 3] >> (7u - (x & 7u))) & 1u;
                    a = (maskBit != 0u) ? 0u : 255u;
                } else {
                    a = 255u;
                }
            }

            StorePremulBgra(dstRow + x * 4u, r, g, b, a);
        }
    }

    outInfo.valid = true;
    return true;
}

inline bool DecodeAniIconPayload(const uint8_t* ico, size_t icoLen, ExtendedImageInfo& outInfo) {
    static const uint8_t kPngMagic[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    if (icoLen >= 8 && std::memcmp(ico, kPngMagic, 8) == 0) {
        return DecodePngViaWic(ico, icoLen, outInfo);
    }

    if (icoLen < 22) return false;
    uint16_t reserved = RdU16LE(ico);
    uint16_t icoType = RdU16LE(ico + 2);
    uint16_t imgCount = RdU16LE(ico + 4);
    if (reserved != 0u || (icoType != 1u && icoType != 2u) || imgCount == 0u) {
        return false;
    }
    if (6ull + static_cast<uint64_t>(imgCount) * 16ull > icoLen) {
        return false;
    }

    struct CandEntry {
        uint16_t index;
        uint64_t score;
    };
    std::vector<CandEntry> cands;
    cands.reserve(imgCount);
    for (uint16_t i = 0; i < imgCount; ++i) {
        const uint8_t* ent = ico + 6u + static_cast<size_t>(i) * 16u;
        uint32_t ew = ent[0] ? ent[0] : 256u;
        uint32_t eh = ent[1] ? ent[1] : 256u;
        uint32_t bpp = (icoType == 1u) ? RdU16LE(ent + 6) : 0u;
        uint32_t imgOff = RdU32LE(ent + 12);
        if (imgOff >= icoLen) continue;
        cands.push_back({ i, static_cast<uint64_t>(ew) * eh * 64ull + bpp });
    }
    std::sort(cands.begin(), cands.end(), [](const CandEntry& a, const CandEntry& b) {
        return a.score > b.score;
    });

    for (const auto& c : cands) {
        const uint8_t* ent = ico + 6u + static_cast<size_t>(c.index) * 16u;
        uint8_t dirH = ent[1];
        uint32_t bytesInRes = RdU32LE(ent + 8);
        uint32_t imgOff = RdU32LE(ent + 12);
        if (imgOff >= icoLen) continue;

        size_t maxAvail = icoLen - imgOff;
        if (bytesInRes > 0u && bytesInRes > maxAvail) {
            continue; // Truncated resource
        }
        size_t entryLen = (bytesInRes > 0u) ? static_cast<size_t>(bytesInRes) : maxAvail;
        const uint8_t* sub = ico + imgOff;

        if (entryLen >= 8 && std::memcmp(sub, kPngMagic, 8) == 0) {
            if (DecodePngViaWic(sub, entryLen, outInfo)) return true;
        } else {
            if (DecodeIcoDibEntry(sub, entryLen, dirH, outInfo)) return true;
        }
    }
    return false;
}

struct AniChunkSpan {
    const uint8_t* ptr = nullptr;
    size_t len = 0;
};

inline void WalkAniRiffChunks(const uint8_t* data, size_t startPos, size_t endPos, int depth,
                              std::vector<uint32_t>& seqIndices,
                              std::vector<AniChunkSpan>& frames)
{
    if (depth > 4) return;
    size_t pos = startPos;
    while (pos + 8 <= endPos) {
        const uint8_t* ck = data + pos;
        uint32_t ckLen = RdU32LE(ck + 4);
        pos += 8;
        if (ckLen > endPos - pos) break; // Strictly reject truncated RIFF chunk

        if (std::memcmp(ck, "LIST", 4) == 0) {
            if (ckLen >= 4) {
                WalkAniRiffChunks(data, pos + 4, pos + ckLen, depth + 1, seqIndices, frames);
            }
        } else if (std::memcmp(ck, "anih", 4) == 0) {
            // 36-byte ANIHEADER: cbSize(0), cFrames(4), cSteps(8), cx(12), cy(16), cBitCount(20), cPlanes(24), jiffRate(28), fl(32)
        } else if (std::memcmp(ck, "seq ", 4) == 0 && ckLen >= 4) {
            size_t count = ckLen / 4u;
            for (size_t i = 0; i < count; ++i) {
                seqIndices.push_back(RdU32LE(data + pos + i * 4u));
            }
        } else if (std::memcmp(ck, "icon", 4) == 0 && ckLen >= 8) {
            frames.push_back({ data + pos, static_cast<size_t>(ckLen) });
        }

        size_t adv = static_cast<size_t>(ckLen) + (ckLen & 1u);
        if (adv > endPos - pos) pos = endPos;
        else pos += adv;
    }
}

} // namespace DetailAni

inline bool DecodeANI(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    using namespace DetailAni;
    if (!data || size < 36 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "ACON", 4) != 0) {
        return false;
    }

    uint32_t riffLen = RdU32LE(data + 4);
    size_t riffEnd = size;
    if (riffLen >= 4u && 8ull + static_cast<uint64_t>(riffLen) <= size) {
        riffEnd = static_cast<size_t>(8ull + static_cast<uint64_t>(riffLen));
    }

    std::vector<uint32_t> seqIndices;
    std::vector<AniChunkSpan> frames;
    WalkAniRiffChunks(data, 12, riffEnd, 0, seqIndices, frames);

    if (frames.empty()) {
        return false; // Zero fake solid-gray fallback!
    }

    size_t preferredIdx = (!seqIndices.empty() && seqIndices[0] < frames.size())
                          ? static_cast<size_t>(seqIndices[0])
                          : 0u;

    if (DecodeAniIconPayload(frames[preferredIdx].ptr, frames[preferredIdx].len, outInfo)) {
        return true;
    }
    for (size_t i = 0; i < frames.size(); ++i) {
        if (i == preferredIdx) continue;
        if (DecodeAniIconPayload(frames[i].ptr, frames[i].len, outInfo)) {
            return true;
        }
    }
    return false;
}

} // namespace Formats
