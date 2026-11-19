#pragma once
// Fits.h — NASA Flexible Image Transport System (.fits / .fit / .fts) Decoder

namespace Formats {

inline bool DecodeFITS(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 2880 || std::memcmp(data, "SIMPLE  =", 9) != 0) return false;
    int bitpix = 0, naxis = 0, w = 0, h = 0, planes = 1;
    size_t hdrEnd = 0;

    for (size_t off = 0; off + 80 <= size; off += 80) {
        std::string_view card(reinterpret_cast<const char*>(data + off), 80);
        if (card.rfind("END     ", 0) == 0) {
            hdrEnd = ((off + 80 + 2879) / 2880) * 2880;
            break;
        }
        size_t eq = card.find('=');
        if (eq == std::string_view::npos) continue;
        std::string_view key = card.substr(0, eq);
        while (!key.empty() && key.back() == ' ') key.remove_suffix(1);
        std::string valStr(card.substr(eq + 1));
        int val = std::atoi(valStr.c_str());
        if (key == "BITPIX") bitpix = val;
        else if (key == "NAXIS") naxis = val;
        else if (key == "NAXIS1") w = val;
        else if (key == "NAXIS2") h = val;
        else if (key == "NAXIS3") planes = std::max(1, val);
    }
    if (hdrEnd == 0 || hdrEnd >= size || naxis < 2 || w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;
    int bytesPerElem = std::abs(bitpix) / 8;
    if (bytesPerElem != 1 && bytesPerElem != 2 && bytesPerElem != 4) return false;
    size_t planeElems = static_cast<size_t>(w) * h;
    if (hdrEnd + planeElems * bytesPerElem > size) return false;

    const uint8_t* payload = data + hdrEnd;
    auto sampleVal = [&](size_t idx) -> float {
        const uint8_t* p = payload + idx * bytesPerElem;
        if (bitpix == 8) return static_cast<float>(p[0]);
        if (bitpix == 16) return static_cast<float>(static_cast<int16_t>((p[0] << 8) | p[1]));
        if (bitpix == 32) {
            int32_t v = (static_cast<int32_t>(p[0]) << 24) | (static_cast<int32_t>(p[1]) << 16) |
                        (static_cast<int32_t>(p[2]) << 8)  |  static_cast<int32_t>(p[3]);
            return static_cast<float>(v);
        }
        if (bitpix == -32) {
            uint32_t u = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                         (static_cast<uint32_t>(p[2]) << 8)  |  static_cast<uint32_t>(p[3]);
            float f = 0.0f;
            std::memcpy(&f, &u, 4);
            return std::isfinite(f) ? f : 0.0f;
        }
        return 0.0f;
    };

    float minV = 1e30f, maxV = -1e30f;
    for (size_t i = 0; i < planeElems; ++i) {
        float v = sampleVal(i);
        minV = std::min(minV, v);
        maxV = std::max(maxV, v);
    }
    float span = (maxV > minV) ? (maxV - minV) : 1.0f;

    outInfo.width = static_cast<uint32_t>(w);
    outInfo.height = static_cast<uint32_t>(h);
    outInfo.channels = (planes >= 3) ? 3 : 1;
    outInfo.bitsPerChannel = static_cast<uint32_t>(std::abs(bitpix));
    outInfo.formatName = L"NASA FITS Astronomical Image (.fits)";
    outInfo.bgraPixels.resize(planeElems * 4);

    bool hasRGB = (planes >= 3 && hdrEnd + planeElems * 3 * bytesPerElem <= size);
    for (int y = 0; y < h; ++y) {
        int dstY = h - 1 - y;
        for (int x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            auto norm8 = [&](size_t sIdx) -> uint8_t {
                float t = (sampleVal(sIdx) - minV) / span;
                return static_cast<uint8_t>(std::clamp(t * 255.0f + 0.5f, 0.0f, 255.0f));
            };
            uint8_t r = norm8(idx);
            uint8_t g = hasRGB ? norm8(planeElems + idx) : r;
            uint8_t b = hasRGB ? norm8(planeElems * 2 + idx) : r;
            uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(dstY) * w + x) * 4];
            dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255;
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
