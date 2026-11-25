#pragma once
// FormatPrologueCommon.h — Shared DRY text-buffer prologue (bounds, binary-NUL guard, UTF-8 BOM strip)
// and bounded printable ASCII C-string extraction used across 3D, EDA, Image, Archive, and Doc readers.

namespace Formats::FormatPrologue {

inline bool PrepareUtf8TextNoNul(
    const uint8_t* data,
    size_t size,
    std::string_view& outSv,
    size_t minBytes = 12u,
    size_t maxBytes = (32u << 20),
    size_t nulScanCap = 4096u)
{
    if (!data || size < minBytes || size > maxBytes) return false;
    const size_t scanLen = std::min<size_t>(size, nulScanCap);
    for (size_t i = 0u; i < scanLen; ++i) {
        if (data[i] == 0u) return false;
    }
    outSv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    return !outSv.empty();
}

inline bool ReadPrintableFixedAscii(
    const uint8_t* field,
    size_t fieldCap,
    std::string& outStr,
    bool requireNulWithinField = false)
{
    outStr.clear();
    if (!field || fieldCap == 0u) return false;
    size_t len = 0u;
    while (len < fieldCap && field[len] != 0u) {
        const uint8_t ch = field[len];
        if (ch < 0x20u || ch > 0x7Eu) return false;
        ++len;
    }
    if (requireNulWithinField && len >= fieldCap) return false;
    outStr.assign(reinterpret_cast<const char*>(field), len);
    return true;
}

} // namespace Formats::FormatPrologue
