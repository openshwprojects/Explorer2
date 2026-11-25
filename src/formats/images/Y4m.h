#pragma once
// Y4m.h — YUV4MPEG2 Uncompressed Video Stream (.y4m) C420/C422/C444/Cmono & 8/10/12/16-bit Decoder

namespace Formats {

inline bool DecodeY4M(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 18 || std::memcmp(data, "YUV4MPEG2", 9) != 0 ||
        (data[9] != ' ' && data[9] != '\n'))
    {
        return false;
    }

    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 8192u));
    const size_t eol = sv.find('\n');
    if (eol == std::string_view::npos || eol + 6u >= size) return false;

    // Require FRAME header immediately after stream header line
    size_t frameTag = eol + 1u;
    if (frameTag + 5u >= sv.size() || sv.substr(frameTag, 5u) != "FRAME" ||
        (sv[frameTag + 5u] != '\n' && sv[frameTag + 5u] != ' ' && sv[frameTag + 5u] != '\r'))
    {
        return false;
    }
    const size_t frameEol = sv.find('\n', frameTag);
    if (frameEol == std::string_view::npos) return false;

    std::string_view hdr = sv.substr(9u, eol - 9u);
    uint32_t w = 0, h = 0;
    std::string chromaTag = "420";

    size_t p = 0;
    while (p < hdr.size()) {
        while (p < hdr.size() && (hdr[p] == ' ' || hdr[p] == '\t' || hdr[p] == '\r')) ++p;
        if (p >= hdr.size()) break;
        size_t e = p;
        while (e < hdr.size() && hdr[e] != ' ' && hdr[e] != '\t' && hdr[e] != '\r') ++e;
        std::string_view tok = hdr.substr(p, e - p);
        if (tok.size() >= 2u) {
            if (tok[0] == 'W') w = static_cast<uint32_t>(std::strtoul(std::string(tok.substr(1)).c_str(), nullptr, 10));
            else if (tok[0] == 'H') h = static_cast<uint32_t>(std::strtoul(std::string(tok.substr(1)).c_str(), nullptr, 10));
            else if (tok[0] == 'C') chromaTag.assign(tok.substr(1));
        }
        p = e;
    }
    if (w == 0 || h == 0 || w > 8192u || h > 8192u) return false;

    bool isMono = false;
    bool hasAlpha = false;
    uint32_t subX = 2u, subY = 2u;
    uint32_t bitDepth = 8u;

    for (char& ch : chromaTag) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    if (chromaTag.rfind("mono", 0) == 0) {
        isMono = true;
        subX = subY = 1u;
        if (chromaTag == "mono10") bitDepth = 10u;
        else if (chromaTag == "mono12") bitDepth = 12u;
        else if (chromaTag == "mono16") bitDepth = 16u;
    } else if (chromaTag.rfind("444", 0) == 0) {
        subX = 1u; subY = 1u;
        if (chromaTag == "444alpha") hasAlpha = true;
        else if (chromaTag.find("p10") != std::string::npos) bitDepth = 10u;
        else if (chromaTag.find("p12") != std::string::npos) bitDepth = 12u;
        else if (chromaTag.find("p16") != std::string::npos) bitDepth = 16u;
    } else if (chromaTag.rfind("422", 0) == 0) {
        subX = 2u; subY = 1u;
        if (chromaTag.find("p10") != std::string::npos) bitDepth = 10u;
        else if (chromaTag.find("p12") != std::string::npos) bitDepth = 12u;
        else if (chromaTag.find("p16") != std::string::npos) bitDepth = 16u;
    } else if (chromaTag.rfind("411", 0) == 0) {
        subX = 4u; subY = 1u;
    } else if (chromaTag.rfind("420", 0) == 0) {
        subX = 2u; subY = 2u;
        if (chromaTag.find("p10") != std::string::npos) bitDepth = 10u;
        else if (chromaTag.find("p12") != std::string::npos) bitDepth = 12u;
        else if (chromaTag.find("p16") != std::string::npos) bitDepth = 16u;
    } else {
        return false;
    }

    const size_t bps = (bitDepth > 8u) ? 2u : 1u;
    const size_t yOff = frameEol + 1u;
    const size_t lumaSamples = static_cast<size_t>(w) * h;
    const size_t lumaBytes = lumaSamples * bps;
    const size_t chromaW = isMono ? 0u : ((w + subX - 1u) / subX);
    const size_t chromaH = isMono ? 0u : ((h + subY - 1u) / subY);
    const size_t chromaBytes = chromaW * chromaH * bps;
    const size_t alphaBytes = hasAlpha ? lumaBytes : 0u;
    const size_t needBytes = lumaBytes + (isMono ? 0u : (chromaBytes * 2u)) + alphaBytes;
    if (yOff > size || needBytes > size - yOff) return false;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = isMono ? 1u : (hasAlpha ? 4u : 3u);
    outInfo.bitsPerChannel = bitDepth;
    outInfo.formatName = L"YUV4MPEG2 Video Frame (.y4m)";
    outInfo.bgraPixels.resize(lumaSamples * 4u);

    const uint8_t* yPlane = data + yOff;
    const uint8_t* uPlane = isMono ? nullptr : (yPlane + lumaBytes);
    const uint8_t* vPlane = isMono ? nullptr : (uPlane + chromaBytes);
    const uint8_t* aPlane = hasAlpha ? (vPlane + chromaBytes) : nullptr;
    const uint32_t shiftTo8 = (bitDepth > 8u) ? (bitDepth - 8u) : 0u;
    const uint32_t maxSample = (1u << bitDepth) - 1u;

    auto rd8 = [&](const uint8_t* plane, size_t idx) -> int {
        if (bps == 1u) return plane[idx];
        uint32_t v16 = std::min<uint32_t>(ReadU16LE(plane + idx * 2u), maxSample);
        return static_cast<int>(v16 >> shiftTo8);
    };

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const size_t li = static_cast<size_t>(y) * w + x;
            const int Y = rd8(yPlane, li);
            uint8_t r = static_cast<uint8_t>(Y), g = static_cast<uint8_t>(Y), b = static_cast<uint8_t>(Y);
            if (!isMono) {
                const size_t ci = static_cast<size_t>(y / subY) * chromaW + (x / subX);
                const int U = rd8(uPlane, ci) - 128;
                const int V = rd8(vPlane, ci) - 128;
                r = static_cast<uint8_t>(std::clamp(Y + (1402 * V) / 1000, 0, 255));
                g = static_cast<uint8_t>(std::clamp(Y - (344 * U + 714 * V) / 1000, 0, 255));
                b = static_cast<uint8_t>(std::clamp(Y + (1772 * U) / 1000, 0, 255));
            }
            const uint8_t a = aPlane ? static_cast<uint8_t>(rd8(aPlane, li)) : 255u;
            WritePremulBGRA(&outInfo.bgraPixels[li * 4u], r, g, b, a);
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
