#pragma once
// PostScriptEps.h — Encapsulated PostScript DOS-Binary / PostScript / EPSI Vector Interpreter (.eps)

namespace Formats {

inline bool DecodePostScriptEps(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return DetailVectorAndRawImg::DecodePsOrPdfArtwork(data, size, L"Encapsulated PostScript (.eps)", outInfo);
}

} // namespace Formats
