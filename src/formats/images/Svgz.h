#pragma once
// Svgz.h — Zero-dependency C++20 Compressed Scalable Vector Graphics (.svgz) reader.

namespace Formats {

inline bool DecodeSVGZ(const uint8_t* data, size_t size, uint32_t maxDimension, ExtendedImageInfo& outInfo) {
    if (!data || size < 10) return false;
    static constexpr size_t kMaxSvgzInflated = 32u * 1024u * 1024u; // 32 MB safety cap

    if (data[0] == 0x1Fu && data[1] == 0x8Bu) {
        std::vector<uint8_t> xmlBytes;
        if (!ZipDeflate::InflateGzip(data, size, xmlBytes, kMaxSvgzInflated) || xmlBytes.size() < 5) {
            return false;
        }
        if (!DecodeSVG(xmlBytes.data(), xmlBytes.size(), maxDimension, outInfo) || !outInfo.valid) {
            return false;
        }
        outInfo.formatName = L"Compressed SVG Vector (.svgz)";
        return true;
    }

    // Also accept plain XML SVG if someone misnamed an uncompressed SVG as .svgz
    size_t ws = 0;
    if (size >= 3 && data[0] == 0xEFu && data[1] == 0xBBu && data[2] == 0xBFu) ws = 3;
    while (ws < size && (data[ws] == ' ' || data[ws] == '\t' || data[ws] == '\r' || data[ws] == '\n')) ++ws;
    if (ws < size && data[ws] == '<') {
        if (DecodeSVG(data, size, maxDimension, outInfo) && outInfo.valid) {
            outInfo.formatName = L"Compressed SVG Vector (.svgz)";
            return true;
        }
    }
    return false;
}

} // namespace Formats
