#pragma once
// AppleNumbers.h — Apple Numbers Spreadsheet Package (.numbers) Reader

namespace Formats {

inline bool ParseAppleNumbersDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    return DetailDocs200::ParseIWorkZipPackage(raw, L"Apple Numbers Spreadsheet Package (.numbers)", out);
}

} // namespace Formats
