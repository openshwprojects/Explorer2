#pragma once
// UnrealLocresDoc.h — Unreal Engine 4 / 5 Binary Localization Resource (.locres)
// Parses Compact (v1), Optimized_CRC32 (v2), and Optimized_CityHash64_UTF16 (v3) `0x7574140E` GUID
// headers, localized `FString` pools (UTF-8 & UTF-16LE), and Namespace/Key/Hash translation tables.

namespace Formats {

namespace DetailUnrealLocres {

static constexpr uint8_t kLocResMagic[16] = {
    0x0E, 0x14, 0x74, 0x75, 0x67, 0x4A, 0x03, 0xFC,
    0x4A, 0x15, 0x90, 0x9D, 0xC3, 0x37, 0x7F, 0x1B
};

inline bool ReadUeFString(const uint8_t* data, size_t size, size_t& pos, std::wstring& outStr) {
    outStr.clear();
    if (size - pos < 4u) return false;
    const int32_t len = ByteIO::RdI32LE(data + pos);
    pos += 4u;
    if (len == 0) return true;

    if (len > 0) {
        const size_t nBytes = static_cast<size_t>(len);
        if (nBytes > (4u << 20) || nBytes > size - pos) return false;
        if (data[pos + nBytes - 1u] != 0u) return false;
        std::string_view sv(reinterpret_cast<const char*>(data + pos), nBytes - 1u);
        outStr = PathUtils::Utf8ToWide(sv);
        pos += nBytes;
        return true;
    }

    // Negative length: `-len` UTF-16LE code units (including trailing NUL uint16)
    if (len == INT32_MIN) return false;
    const size_t charCount = static_cast<size_t>(-len);
    if (charCount > (2u << 20) || charCount * 2u > size - pos) return false;
    if (ByteIO::RdU16LE(data + pos + (charCount - 1u) * 2u) != 0u) return false;
    outStr = ReaderText::DecodeUtf16Span(data + pos, charCount - 1u, false);
    pos += charCount * 2u;
    return true;
}

} // namespace DetailUnrealLocres

inline bool ParseUnrealLocresDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& outDoc) {
    const size_t size = raw.size();
    const uint8_t* data = raw.data();
    if (!data || size < 29u || size > (64u << 20)) return false;
    if (std::memcmp(data, DetailUnrealLocres::kLocResMagic, 16u) != 0) return false;

    const uint8_t version = data[16u];
    if (version < 1u || version > 3u) return false;

    const int64_t strArrayOff = ByteIO::RdI64LE(data + 17u);
    if (strArrayOff < 25 || static_cast<uint64_t>(strArrayOff) >= size) return false;

    // 1. Read localized string pool at `strArrayOff`
    size_t poolPos = static_cast<size_t>(strArrayOff);
    if (size - poolPos < 4u) return false;
    const uint32_t poolCount = ByteIO::RdU32LE(data + poolPos);
    poolPos += 4u;
    if (poolCount > 500000u) return false;

    std::vector<std::wstring> strPool(poolCount);
    for (uint32_t i = 0u; i < poolCount; ++i) {
        if (!DetailUnrealLocres::ReadUeFString(data, size, poolPos, strPool[i])) return false;
        if (version >= 2u) {
            if (size - poolPos < 4u) return false;
            poolPos += 4u; // int32 refCount
        }
    }

    // 2. Read Namespace -> Key -> LocalizedStringIndex hierarchy starting at offset 25
    size_t pos = 25u;
    const size_t nsLimit = static_cast<size_t>(strArrayOff);
    if (version >= 2u) {
        if (nsLimit - pos < 4u) return false;
        pos += 4u; // uint32 entriesCount
    }
    if (nsLimit - pos < 4u) return false;
    const uint32_t nsCount = ByteIO::RdU32LE(data + pos);
    pos += 4u;
    if (nsCount > 65536u) return false;

    SpreadsheetSheet sheet;
    sheet.name = L"LocRes";
    sheet.rows.push_back({ L"Namespace", L"Key", L"SourceHash", L"Translation" });

    size_t totalEntries = 0u;
    for (uint32_t ni = 0u; ni < nsCount; ++ni) {
        if (version >= 2u) {
            if (nsLimit - pos < 4u) return false;
            pos += 4u; // namespace hash (uint32 StrHash)
        }
        std::wstring nsName;
        if (!DetailUnrealLocres::ReadUeFString(data, nsLimit, pos, nsName)) return false;
        if (nsLimit - pos < 4u) return false;
        const uint32_t keyCount = ByteIO::RdU32LE(data + pos);
        pos += 4u;
        if (keyCount > 200000u) return false;

        for (uint32_t ki = 0u; ki < keyCount; ++ki) {
            if (version >= 2u) {
                if (nsLimit - pos < 4u) return false;
                pos += 4u; // key hash
            }
            std::wstring keyName;
            if (!DetailUnrealLocres::ReadUeFString(data, nsLimit, pos, keyName)) return false;
            if (nsLimit - pos < 8u) return false;
            const uint32_t srcHash = ByteIO::RdU32LE(data + pos);
            const int32_t  strIdx  = ByteIO::RdI32LE(data + pos + 4u);
            pos += 8u;
            if (strIdx < 0 || static_cast<uint32_t>(strIdx) >= poolCount) return false;

            if (sheet.rows.size() < 16385u) {
                wchar_t hashHex[16] = {};
                swprintf_s(hashHex, L"0x%08X", srcHash);
                sheet.rows.push_back({
                    nsName.empty() ? L"(Global)" : nsName,
                    std::move(keyName),
                    hashHex,
                    strPool[static_cast<size_t>(strIdx)]
                });
            }
            ++totalEntries;
        }
    }

    if (totalEntries == 0u) return false;

    wchar_t title[160] = {};
    swprintf_s(title, L"Unreal Engine Localization Resource (.locres v%u, %u namespaces, %zu entries)",
               version, nsCount, totalEntries);
    return BioWareCommon::EmitSpreadsheetDocument(outDoc, title, std::move(sheet));
}

} // namespace Formats
