#pragma once
// GettextMoCatalog.h — GNU Gettext Compiled Machine Object Localization Catalog (.mo)
// Supports both little-endian (0x950412DE) and big-endian (0xDE120495) v0.x/v1.x catalogs,
// msgctxt (\x04 EOT) disambiguation, NUL-separated plural forms, and catalog header metadata.

namespace Formats {

inline bool ParseGettextMoCatalog(const std::vector<uint8_t>& raw, ParsedOfficeDocument& outDoc) {
    if (raw.size() < 28u || raw.size() > (64u << 20)) return false;

    const uint8_t* data = raw.data();
    const size_t   size = raw.size();

    bool bigEndian = false;
    if (ByteIO::RdU32LE(data) == 0x950412DEu) {
        bigEndian = false;
    } else if (ByteIO::RdU32BE(data) == 0x950412DEu) {
        bigEndian = true;
    } else {
        return false;
    }

    auto rd32 = [&](size_t off) -> uint32_t {
        return bigEndian ? ByteIO::RdU32BE(data + off) : ByteIO::RdU32LE(data + off);
    };

    const uint32_t revision      = rd32(0x04u);
    const uint32_t revMajor      = revision >> 16;
    const uint32_t numStrings    = rd32(0x08u);
    const uint32_t origTableOff  = rd32(0x0Cu);
    const uint32_t transTableOff = rd32(0x10u);
    if (revMajor > 1u || numStrings == 0u || numStrings > 1000000u) return false;
    if (origTableOff < 28u || transTableOff < 28u) return false;

    const uint64_t tableBytes = static_cast<uint64_t>(numStrings) * 8ULL;
    if (static_cast<uint64_t>(origTableOff)  + tableBytes > size ||
        static_cast<uint64_t>(transTableOff) + tableBytes > size) {
        return false;
    }

    GettextCommon::CatalogHeaderMeta meta{};
    std::vector<GettextCommon::CatalogEntry> entries;
    entries.reserve(std::min<uint32_t>(numStrings, 16384u));

    for (uint32_t i = 0; i < numStrings; ++i) {
        const uint32_t oLen = rd32(origTableOff  + static_cast<size_t>(i) * 8u + 0u);
        const uint32_t oOff = rd32(origTableOff  + static_cast<size_t>(i) * 8u + 4u);
        const uint32_t tLen = rd32(transTableOff + static_cast<size_t>(i) * 8u + 0u);
        const uint32_t tOff = rd32(transTableOff + static_cast<size_t>(i) * 8u + 4u);

        if (static_cast<uint64_t>(oOff) + oLen >= size || data[oOff + oLen] != 0u) return false;
        if (static_cast<uint64_t>(tOff) + tLen >= size || data[tOff + tLen] != 0u) return false;

        std::string_view origSv(reinterpret_cast<const char*>(data + oOff), oLen);
        std::string_view transSv(reinterpret_cast<const char*>(data + tOff), tLen);

        if (oLen == 0u) {
            GettextCommon::ParseHeaderMetadataBlock(transSv, meta);
            continue;
        }
        if (entries.size() >= 16384u) continue;

        GettextCommon::CatalogEntry ent{};
        // Extract optional msgctxt separated by EOT (0x04)
        const size_t eot = origSv.find('\x04');
        if (eot != std::string_view::npos) {
            ent.context = PathUtils::Utf8ToWide(origSv.substr(0, eot));
            origSv.remove_prefix(eot + 1u);
        }
        const bool isPlural = (origSv.find('\0') != std::string_view::npos);
        ent.msgid  = GettextCommon::JoinNulSeparatedForms(origSv);
        ent.msgstr = GettextCommon::JoinNulSeparatedForms(transSv);
        if (isPlural) ent.notes = L"plural";
        entries.push_back(std::move(ent));
    }

    return GettextCommon::EmitCatalogDocument(
        outDoc,
        bigEndian ? L"GNU Gettext MO Catalog (BE)" : L"GNU Gettext MO Catalog (LE)",
        meta,
        entries);
}

} // namespace Formats
