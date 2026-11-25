#pragma once
// AppleKeynote.h — Apple Keynote Presentation Package (.key) Reader

namespace Formats {

inline bool ParseAppleKeynoteDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    return DetailStructuredDataDoc::ParseIWorkZipPackage(raw, L"Apple Keynote Presentation Package (.key)", out);
}

} // namespace Formats
