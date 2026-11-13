#pragma once
// Pkm.h — Ericsson Texture Compression (.pkm, PKM 10 / PKM 20) Decoder

namespace Formats {

inline bool DecodePKM(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 16 || std::memcmp(data, "PKM ", 4) != 0) return false;
    auto rdBE16 = [](const uint8_t* p) -> uint16_t { return static_cast<uint16_t>((p[0] << 8) | p[1]); };
    uint16_t extW  = rdBE16(data + 8);
    uint16_t extH  = rdBE16(data + 10);
    uint16_t origW = rdBE16(data + 12);
    uint16_t origH = rdBE16(data + 14);
    uint32_t w = (origW > 0) ? origW : extW;
    uint32_t h = (origH > 0) ? origH : extH;
    if (w == 0 || h == 0 || w > 8192 || h > 8192) return false;
    uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
    if (16 + static_cast<size_t>(bw) * bh * 8 > size) return false;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 3;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"ETC1/ETC2 Texture (.pkm)";
    outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4, 255);

    static const int kEtcMod[8][4] = {
        { 2, 8, -2, -8 }, { 5, 17, -5, -17 }, { 9, 29, -9, -29 }, { 13, 42, -13, -42 },
        { 18, 60, -18, -60 }, { 24, 80, -24, -80 }, { 33, 106, -33, -106 }, { 47, 183, -47, -183 }
    };

    const uint8_t* ptr = data + 16;
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            bool diffBit = (ptr[3] & 2) != 0;
            bool flipBit = (ptr[3] & 1) != 0;
            int r1, g1, b1, r2, g2, b2;
            if (diffBit) {
                auto s3 = [](int v) { return (v >= 4) ? (v - 8) : v; };
                int br1 = (ptr[0] >> 3) & 31, bg1 = (ptr[1] >> 3) & 31, bb1 = (ptr[2] >> 3) & 31;
                int br2 = std::clamp(br1 + s3(ptr[0] & 7), 0, 31);
                int bg2 = std::clamp(bg1 + s3(ptr[1] & 7), 0, 31);
                int bb2 = std::clamp(bb1 + s3(ptr[2] & 7), 0, 31);
                r1 = (br1 << 3) | (br1 >> 2); g1 = (bg1 << 3) | (bg1 >> 2); b1 = (bb1 << 3) | (bb1 >> 2);
                r2 = (br2 << 3) | (br2 >> 2); g2 = (bg2 << 3) | (bg2 >> 2); b2 = (bb2 << 3) | (bb2 >> 2);
            } else {
                r1 = ((ptr[0] & 0xF0) | (ptr[0] >> 4)); r2 = ((ptr[0] & 0x0F) | ((ptr[0] & 0x0F) << 4));
                g1 = ((ptr[1] & 0xF0) | (ptr[1] >> 4)); g2 = ((ptr[1] & 0x0F) | ((ptr[1] & 0x0F) << 4));
                b1 = ((ptr[2] & 0xF0) | (ptr[2] >> 4)); b2 = ((ptr[2] & 0x0F) | ((ptr[2] & 0x0F) << 4));
            }
            int cw1 = (ptr[3] >> 5) & 7, cw2 = (ptr[3] >> 2) & 7;
            uint16_t msb = rdBE16(ptr + 4), lsb = rdBE16(ptr + 6);
            for (uint32_t py = 0; py < 4; ++py) {
                for (uint32_t px = 0; px < 4; ++px) {
                    uint32_t x = bx * 4 + px, y = by * 4 + py;
                    if (x >= w || y >= h) continue;
                    int bitIdx = static_cast<int>(px * 4 + py);
                    int idx = (((msb >> bitIdx) & 1) << 1) | ((lsb >> bitIdx) & 1);
                    bool sub2 = flipBit ? (py >= 2) : (px >= 2);
                    int mod = kEtcMod[sub2 ? cw2 : cw1][idx];
                    uint8_t r = static_cast<uint8_t>(std::clamp((sub2 ? r2 : r1) + mod, 0, 255));
                    uint8_t g = static_cast<uint8_t>(std::clamp((sub2 ? g2 : g1) + mod, 0, 255));
                    uint8_t b = static_cast<uint8_t>(std::clamp((sub2 ? b2 : b1) + mod, 0, 255));
                    uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(y) * w + x) * 4];
                    dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255;
                }
            }
            ptr += 8;
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
