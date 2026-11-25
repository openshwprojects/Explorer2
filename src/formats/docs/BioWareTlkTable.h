#pragma once
// BioWareTlkTable.h — BioWare Dialog Talk Table (.tlk — "TLK V3.0", "TLK V4.0", & Infinity "TLK V1  ")
// Used across Baldur's Gate I & II, Planescape: Torment, Neverwinter Nights 1 & 2, KotOR I & II, and Jade Empire.

namespace Formats {

inline bool ParseBioWareTlkTable(const std::vector<uint8_t>& raw, ParsedOfficeDocument& outDoc) {
    if (raw.size() < 28u || raw.size() > (128u << 20)) return false;

    const uint8_t* data = raw.data();
    const size_t   size = raw.size();

    // 1. Aurora / Odyssey "TLK V3.0" (20B header + 40B entries)
    if (std::memcmp(data, "TLK V3.0", 8) == 0) {
        const uint32_t langId  = ByteIO::RdU32LE(data + 0x08);
        const uint32_t strCnt  = ByteIO::RdU32LE(data + 0x0C);
        const uint32_t poolOff = ByteIO::RdU32LE(data + 0x10);
        if (strCnt == 0u || strCnt > 2000000u) return false;
        const uint64_t tblEnd = 20ULL + static_cast<uint64_t>(strCnt) * 40ULL;
        if (tblEnd > size || poolOff < tblEnd || poolOff > size) return false;

        SpreadsheetSheet sheet;
        sheet.name = L"TLK_V30";
        sheet.rows.push_back({ L"StrRef", L"SoundResRef", L"SoundLen_s", L"Text" });

        const uint32_t emitCap = std::min<uint32_t>(strCnt, 16384u);
        for (uint32_t i = 0; i < strCnt; ++i) {
            const uint8_t* ent      = data + 20u + static_cast<size_t>(i) * 40u;
            const uint32_t flags    = ByteIO::RdU32LE(ent + 0x00);
            const char*    sndRaw   = reinterpret_cast<const char*>(ent + 0x04);
            const uint32_t relOff   = ByteIO::RdU32LE(ent + 0x1C);
            const uint32_t strLen   = ByteIO::RdU32LE(ent + 0x20);
            uint32_t sndBits        = ByteIO::RdU32LE(ent + 0x24);
            float sndLen = 0.0f;
            std::memcpy(&sndLen, &sndBits, 4);

            std::string_view txtSv;
            if ((flags & 0x01u) != 0u && strLen > 0u) {
                if (static_cast<uint64_t>(poolOff) + relOff + strLen > size) return false;
                txtSv = std::string_view(reinterpret_cast<const char*>(data + poolOff + relOff), strLen);
            } else if (strLen > 0u && static_cast<uint64_t>(poolOff) + relOff + strLen <= size && flags == 0u) {
                // Some community TLK editors leave flags=0 when stringSize > 0
                txtSv = std::string_view(reinterpret_cast<const char*>(data + poolOff + relOff), strLen);
            }

            if (i < emitCap) {
                const size_t sl = strnlen(sndRaw, 16u);
                std::wstring sndW = BioWareCommon::DecodeBioWareString(std::string_view(sndRaw, sl));
                wchar_t durBuf[32] = {};
                if ((flags & 0x04u) != 0u && std::isfinite(sndLen) && sndLen > 0.0f) {
                    swprintf_s(durBuf, L"%.2f", sndLen);
                }
                sheet.rows.push_back({
                    std::to_wstring(i),
                    std::move(sndW),
                    durBuf,
                    BioWareCommon::DecodeBioWareString(txtSv)
                });
            }
        }

        wchar_t title[180] = {};
        swprintf_s(title, L"BioWare Talk Table (TLK V3.0, %ls, %u strings)",
                   BioWareCommon::LanguageIdName(langId), strCnt);
        return BioWareCommon::EmitSpreadsheetDocument(outDoc, title, std::move(sheet));
    }

    // 2. Jade Empire "TLK V4.0" (20B header + 10B entries)
    if (std::memcmp(data, "TLK V4.0", 8) == 0) {
        const uint32_t langId  = ByteIO::RdU32LE(data + 0x08);
        const uint32_t strCnt  = ByteIO::RdU32LE(data + 0x0C);
        const uint32_t poolOff = ByteIO::RdU32LE(data + 0x10);
        if (strCnt == 0u || strCnt > 2000000u) return false;
        const uint64_t tblEnd = 20ULL + static_cast<uint64_t>(strCnt) * 10ULL;
        if (tblEnd > size || poolOff < tblEnd || poolOff > size) return false;

        SpreadsheetSheet sheet;
        sheet.name = L"TLK_V40";
        sheet.rows.push_back({ L"StrRef", L"SoundID", L"Text" });

        const uint32_t emitCap = std::min<uint32_t>(strCnt, 16384u);
        for (uint32_t i = 0; i < strCnt; ++i) {
            const uint8_t* ent    = data + 20u + static_cast<size_t>(i) * 10u;
            const uint32_t sndId  = ByteIO::RdU32LE(ent + 0u);
            const uint32_t rawOff = ByteIO::RdU32LE(ent + 4u);
            const uint16_t strLen = ByteIO::RdU16LE(ent + 8u);

            std::string_view txtSv;
            if (strLen > 0u) {
                uint64_t absOff = rawOff;
                if (absOff < poolOff) absOff = static_cast<uint64_t>(poolOff) + rawOff;
                if (absOff + strLen > size) return false;
                txtSv = std::string_view(reinterpret_cast<const char*>(data + static_cast<size_t>(absOff)), strLen);
            }
            if (i < emitCap) {
                sheet.rows.push_back({
                    std::to_wstring(i),
                    std::to_wstring(sndId),
                    BioWareCommon::DecodeBioWareString(txtSv)
                });
            }
        }

        wchar_t title[180] = {};
        swprintf_s(title, L"BioWare Jade Empire Talk Table (TLK V4.0, %ls, %u strings)",
                   BioWareCommon::LanguageIdName(langId), strCnt);
        return BioWareCommon::EmitSpreadsheetDocument(outDoc, title, std::move(sheet));
    }

    // 3. Infinity Engine "TLK V1  " (18B header + 26B entries — Baldur's Gate / Planescape)
    if (std::memcmp(data, "TLK V1  ", 8) == 0) {
        const uint16_t langId  = ByteIO::RdU16LE(data + 0x08);
        const uint32_t strCnt  = ByteIO::RdU32LE(data + 0x0A);
        const uint32_t poolOff = ByteIO::RdU32LE(data + 0x0E);
        if (strCnt == 0u || strCnt > 2000000u) return false;
        const uint64_t tblEnd = 18ULL + static_cast<uint64_t>(strCnt) * 26ULL;
        if (tblEnd > size || poolOff < tblEnd || poolOff > size) return false;

        SpreadsheetSheet sheet;
        sheet.name = L"TLK_V1";
        sheet.rows.push_back({ L"StrRef", L"SoundResRef", L"Text" });

        const uint32_t emitCap = std::min<uint32_t>(strCnt, 16384u);
        for (uint32_t i = 0; i < strCnt; ++i) {
            const uint8_t* ent    = data + 18u + static_cast<size_t>(i) * 26u;
            const char*    sndRaw = reinterpret_cast<const char*>(ent + 0x02);
            const uint32_t relOff = ByteIO::RdU32LE(ent + 18u);
            const uint32_t strLen = ByteIO::RdU32LE(ent + 22u);

            std::string_view txtSv;
            if (strLen > 0u) {
                if (static_cast<uint64_t>(poolOff) + relOff + strLen > size) return false;
                txtSv = std::string_view(reinterpret_cast<const char*>(data + poolOff + relOff), strLen);
            }
            if (i < emitCap) {
                const size_t sl = strnlen(sndRaw, 8u);
                sheet.rows.push_back({
                    std::to_wstring(i),
                    BioWareCommon::DecodeBioWareString(std::string_view(sndRaw, sl)),
                    BioWareCommon::DecodeBioWareString(txtSv)
                });
            }
        }

        wchar_t title[180] = {};
        swprintf_s(title, L"BioWare Infinity Engine Talk Table (TLK V1, %ls, %u strings)",
                   BioWareCommon::LanguageIdName(langId), strCnt);
        return BioWareCommon::EmitSpreadsheetDocument(outDoc, title, std::move(sheet));
    }

    return false;
}

} // namespace Formats
