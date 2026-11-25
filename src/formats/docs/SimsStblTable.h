#pragma once
// SimsStblTable.h — Maxis The Sims 3 (v2 UTF-16LE) & The Sims 4 (v5 UTF-8) String Table (.stbl)
// Parses `"STBL"` header + FNV-32 / FNV-64 hashed localized string entries into a spreadsheet view.

namespace Formats {

inline bool ParseSimsStblTable(const std::vector<uint8_t>& raw, ParsedOfficeDocument& outDoc) {
    const size_t size = raw.size();
    const uint8_t* data = raw.data();
    if (!data || size < 18u || size > (64u << 20)) return false;
    if (std::memcmp(data, "STBL", 4) != 0) return false;

    const uint8_t version = data[4u];
    if (version != 2u && version != 5u) return false;

    SpreadsheetSheet sheet;
    sheet.name = L"STBL";
    sheet.rows.push_back({ L"KeyHash", L"Flags", L"Text" });

    size_t pos = 0u;
    uint64_t numEntries = 0ULL;

    if (version == 5u) {
        // Sims 4 STBL v5 (21-byte header): [4]ver=5, [5]isCompressed=0, [6..14]u64 count, [14..16]pad, [16..20]u32 mnStringLen, [20]pad
        if (size < 21u || data[5u] != 0u) return false;
        numEntries = ByteIO::RdU64LE(data + 6u);
        if (numEntries == 0ULL || numEntries > 500000ULL) return false;
        pos = 21u;

        for (uint64_t i = 0ULL; i < numEntries; ++i) {
            if (size - pos < 7u) return false;
            const uint32_t keyHash = ByteIO::RdU32LE(data + pos + 0u);
            const uint8_t  flags   = data[pos + 4u];
            const uint16_t strLen  = ByteIO::RdU16LE(data + pos + 5u);
            pos += 7u;
            if (strLen > size - pos) return false;

            std::string_view sv(reinterpret_cast<const char*>(data + pos), strLen);
            pos += strLen;

            if (sheet.rows.size() < 16385u) {
                wchar_t hBuf[16] = {};
                swprintf_s(hBuf, L"0x%08X", keyHash);
                sheet.rows.push_back({ hBuf, std::to_wstring(flags), PathUtils::Utf8ToWide(sv) });
            }
        }
    } else {
        // Sims 3 STBL v2 (17-byte header): [4]ver=2, [5..7]pad, [7..15]u64 count, [15..17]pad
        numEntries = ByteIO::RdU64LE(data + 7u);
        if (numEntries == 0ULL || numEntries > 500000ULL) return false;
        pos = 17u;

        for (uint64_t i = 0ULL; i < numEntries; ++i) {
            if (size - pos < 12u) return false;
            const uint64_t keyHash   = ByteIO::RdU64LE(data + pos + 0u);
            const uint32_t charCount = ByteIO::RdU32LE(data + pos + 8u);
            pos += 12u;
            if (charCount > (1u << 20) || static_cast<uint64_t>(charCount) * 2ULL > size - pos) return false;

            std::wstring txt = ReaderText::DecodeUtf16Span(data + pos, charCount, false);
            pos += static_cast<size_t>(charCount) * 2u;

            if (sheet.rows.size() < 16385u) {
                wchar_t hBuf[24] = {};
                swprintf_s(hBuf, L"0x%016llX", static_cast<unsigned long long>(keyHash));
                sheet.rows.push_back({ hBuf, L"0", std::move(txt) });
            }
        }
    }

    if (pos != size) return false;

    wchar_t title[160] = {};
    swprintf_s(title, L"Maxis The Sims %d String Table (.stbl v%u, %llu strings)",
               version == 5u ? 4 : 3, version, static_cast<unsigned long long>(numEntries));
    return BioWareCommon::EmitSpreadsheetDocument(outDoc, title, std::move(sheet));
}

} // namespace Formats
