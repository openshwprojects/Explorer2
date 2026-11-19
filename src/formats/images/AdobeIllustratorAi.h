#pragma once
// AdobeIllustratorAi.h — Adobe Illustrator PostScript & PDF Content-Stream Vector Interpreter (.ai)

namespace Formats {

inline bool DecodeAdobeIllustratorAi(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return DetailImg200::DecodePsOrPdfArtwork(data, size, L"Adobe Illustrator Artwork (.ai)", outInfo);
}

} // namespace Formats
