#pragma once
// Y4m.h — YUV4MPEG2 Uncompressed Video Stream (.y4m) First Frame Decoder

namespace Formats {

inline bool DecodeY4M(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 24 || std::memcmp(data, "YUV4MPEG2", 9) != 0) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 4096));
    size_t eol = sv.find('\n');
    size_t frameTag = (eol != std::string_view::npos) ? sv.find("FRAME", eol) : std::string_view::npos;
    if (frameTag == std::string_view::npos) return false;
    size_t frameEol = sv.find('\n', frameTag);
    if (frameEol == std::string_view::npos) return false;

    std::string_view hdr = sv.substr(0, eol);
    uint32_t w = 0, h = 0;
    size_t wPos = hdr.find(" W"), hPos = hdr.find(" H");
    if (wPos != std::string_view::npos) w = static_cast<uint32_t>(std::strtoul(std::string(hdr.substr(wPos + 2)).c_str(), nullptr, 10));
    if (hPos != std::string_view::npos) h = static_cast<uint32_t>(std::strtoul(std::string(hdr.substr(hPos + 2)).c_str(), nullptr, 10));
    if (w == 0 || h == 0 || w > 4096 || h > 4096) return false;

    size_t yOff = frameEol + 1;
    size_t lumaBytes = static_cast<size_t>(w) * h;
    size_t chromaW = (w + 1) / 2, chromaH = (h + 1) / 2;
    size_t chromaBytes = chromaW * chromaH;
    if (yOff + lumaBytes > size) return false;
    bool hasChroma = (yOff + lumaBytes + chromaBytes * 2 <= size);

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 3;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"YUV4MPEG2 Video Frame (.y4m)";
    outInfo.bgraPixels.resize(lumaBytes * 4);

    const uint8_t* yPlane = data + yOff;
    const uint8_t* uPlane = hasChroma ? (yPlane + lumaBytes) : nullptr;
    const uint8_t* vPlane = hasChroma ? (uPlane + chromaBytes) : nullptr;

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            int Y = yPlane[static_cast<size_t>(y) * w + x];
            int U = hasChroma ? (static_cast<int>(uPlane[(y / 2) * chromaW + (x / 2)]) - 128) : 0;
            int V = hasChroma ? (static_cast<int>(vPlane[(y / 2) * chromaW + (x / 2)]) - 128) : 0;
            uint8_t r = static_cast<uint8_t>(std::clamp(Y + (1402 * V) / 1000, 0, 255));
            uint8_t g = static_cast<uint8_t>(std::clamp(Y - (344 * U + 714 * V) / 1000, 0, 255));
            uint8_t b = static_cast<uint8_t>(std::clamp(Y + (1772 * U) / 1000, 0, 255));
            uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(y) * w + x) * 4];
            dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255;
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
