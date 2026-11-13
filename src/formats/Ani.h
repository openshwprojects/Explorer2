#pragma once
// Ani.h — Windows Animated Cursor RIFF Container (.ani) Decoder

namespace Formats {

inline bool DecodeANI(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 36 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "ACON", 4) != 0) return false;
    uint32_t w = 32, h = 32;
    size_t pos = 12;
    while (pos + 8 <= size) {
        const uint8_t* ck = data + pos;
        uint32_t ckLen = 0;
        std::memcpy(&ckLen, ck + 4, 4);
        if (std::memcmp(ck, "LIST", 4) == 0 && pos + 12 <= size) {
            pos += 12;
            continue;
        }
        pos += 8;
        if (pos + ckLen > size) break;
        if (std::memcmp(ck, "anih", 4) == 0 && ckLen >= 36) {
            uint32_t cw = 0, ch = 0;
            std::memcpy(&cw, data + pos + 8, 4);
            std::memcpy(&ch, data + pos + 12, 4);
            if (cw > 0 && cw <= 256) w = cw;
            if (ch > 0 && ch <= 256) h = ch;
        } else if (std::memcmp(ck, "icon", 4) == 0 && ckLen >= 22) {
            const uint8_t* ico = data + pos;
            uint8_t iw = ico[6] ? ico[6] : 32;
            uint8_t ih = ico[7] ? ico[7] : 32;
            uint32_t imgOff = 0;
            std::memcpy(&imgOff, ico + 18, 4);
            // 64-bit on purpose: with a 32-bit sum an offset near 4 GB wraps around and passes this test
            const uint64_t dibStart = static_cast<uint64_t>(imgOff) + 40ULL;
            if (dibStart <= ckLen) {
                w = iw; h = ih;
                const uint8_t* dib = ico + dibStart;
                size_t rem = static_cast<size_t>(ckLen - dibStart);
                if (rem >= static_cast<size_t>(w) * h * 4) {
                    outInfo.width = w; outInfo.height = h; outInfo.channels = 4; outInfo.bitsPerChannel = 8;
                    outInfo.formatName = L"Windows Animated Cursor (.ani)";
                    outInfo.bgraPixels.resize(static_cast<size_t>(w) * h * 4);
                    for (uint32_t y = 0; y < h; ++y) {
                        std::memcpy(&outInfo.bgraPixels[static_cast<size_t>(h - 1 - y) * w * 4],
                                    dib + static_cast<size_t>(y) * w * 4, w * 4);
                    }
                    outInfo.valid = true;
                    return true;
                }
            }
        }
        pos += ckLen + (ckLen & 1);
    }

    outInfo.width = w; outInfo.height = h; outInfo.channels = 4; outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Windows Animated Cursor (.ani)";
    outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4, 240);
    outInfo.valid = true;
    return true;
}

} // namespace Formats
