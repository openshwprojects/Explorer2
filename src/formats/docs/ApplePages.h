#pragma once
// ApplePages.h — Apple Pages Document Package (.pages) Reader

namespace Formats {

inline bool ParseApplePagesDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    return DetailDocs200::ParseIWorkZipPackage(raw, L"Apple Pages Document Package (.pages)", out);
}

} // namespace Formats
