#pragma once
// EmailEml.h — RFC 5322 Email Message (.eml) Reader

namespace Formats {

inline bool ParseEmailEmlDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 8) return false;
    size_t checkLen = std::min<size_t>(raw.size(), 4096);
    size_t nulCount = 0;
    for (size_t i = 0; i < checkLen; ++i) if (raw[i] == 0) ++nulCount;
    if (nulCount * 50 > checkLen) return false;

    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());

    out.kind = OfficeFileKind::WordDocx;
    out.isSpreadsheet = false;
    out.formatTitle = L"RFC 5322 Email Message (.eml)";
    if (!DetailMedia100::ParseSingleEmailMessage(sv, 0, out) || out.paragraphs.empty()) return false;
    DetailMedia100::ComputeDocCounts(out);
    out.valid = true;
    return true;
}

} // namespace Formats
