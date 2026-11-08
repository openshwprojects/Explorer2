#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace Explorer2 {

struct ExtendedImageInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t origWidth = 0;
    uint32_t origHeight = 0;
    uint32_t channels = 0;
    uint32_t bitsPerChannel = 8;
    std::wstring formatName;
    std::vector<std::wstring> layerNames; // Populated for PSD / XCF files
    std::vector<uint8_t> bgraPixels;      // 32-bit PBGRA (B, G, R, A) row-major
    bool valid = false;
};

class ImageFormatLoader {
public:
    // Returns true for raster formats decoded natively by ImageFormatLoader
    // (.tga, .psd, .psb, .xcf, .hdr, .pic, .ppm, .pgm, .pbm, .pnm, .pcx, .qoi)
    static bool IsExtendedImageExtension(const std::wstring& extLower);

    // Decodes an image from memory into 32-bit PBGRA pixels (optionally box/bilinear downscaled to maxDimension).
    static bool DecodeImageMemory(
        const uint8_t* data,
        size_t size,
        const std::wstring& extLower,
        uint32_t maxDimension,
        ExtendedImageInfo& outInfo);

    // Convenience helper that reads a file with full sharing (non-locking) and decodes it.
    static bool DecodeImageFile(
        const std::wstring& fullPath,
        uint32_t maxDimension,
        ExtendedImageInfo& outInfo);
};

} // namespace Explorer2
