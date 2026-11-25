#pragma once
// AdobeDng.h — Adobe Digital Negative Stripped/Tiled/LJPEG/Deflate CFA & LinearRaw Decoder (.dng)

namespace Formats {

inline bool DecodeAdobeDng(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return DetailVectorAndRawImg::ParseTiffRawIfds(data, size, L"Adobe Digital Negative (.dng)", outInfo);
}

} // namespace Formats
