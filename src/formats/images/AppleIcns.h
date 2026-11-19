#pragma once
// AppleIcns.h — Apple Icon Image Decoder (.icns)

namespace Formats {

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


} // namespace Formats
