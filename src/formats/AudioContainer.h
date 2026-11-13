#pragma once
// AudioContainer.h — Digital Audio Container Inspector (.wav / .flac / .aiff) Stream & Chunk Metadata Parser

namespace Formats {

inline bool ParseAudioContainerDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 12) return false;
    bool isWav  = (std::memcmp(raw.data(), "RIFF", 4) == 0 && std::memcmp(raw.data() + 8, "WAVE", 4) == 0);
    bool isFlac = (std::memcmp(raw.data(), "fLaC", 4) == 0);
    bool isAiff = (std::memcmp(raw.data(), "FORM", 4) == 0 && (std::memcmp(raw.data() + 8, "AIFF", 4) == 0 || std::memcmp(raw.data() + 8, "AIFC", 4) == 0));
    if (!isWav && !isFlac && !isAiff) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = isWav ? L"RIFF WAVE Audio Container (.wav)"
                    : (isFlac ? L"Free Lossless Audio Codec (.flac)" : L"Audio IFF Container (.aiff)");

    SpreadsheetSheet sheet;
    sheet.name = L"Audio_Properties";
    sheet.rows.push_back({ L"Property / Chunk", L"Value / Details" });
    sheet.rows.push_back({ L"Container Format", out.formatTitle });
    sheet.rows.push_back({ L"File Size", std::to_wstring(raw.size()) + L" B" });

    if (isWav) {
        size_t pos = 12;
        while (pos + 8 <= raw.size()) {
            char cid[5] = {};
            std::memcpy(cid, raw.data() + pos, 4);
            uint32_t clen = 0;
            std::memcpy(&clen, raw.data() + pos + 4, 4);
            if (std::memcmp(cid, "fmt ", 4) == 0 && pos + 24 <= raw.size()) {
                uint16_t audioFmt = 0, numCh = 0, bitsPerSample = 0;
                uint32_t sampleRate = 0, byteRate = 0;
                std::memcpy(&audioFmt, raw.data() + pos + 8, 2);
                std::memcpy(&numCh, raw.data() + pos + 10, 2);
                std::memcpy(&sampleRate, raw.data() + pos + 12, 4);
                std::memcpy(&byteRate, raw.data() + pos + 16, 4);
                std::memcpy(&bitsPerSample, raw.data() + pos + 22, 2);
                sheet.rows.push_back({ L"Audio Encoding", (audioFmt == 1) ? L"PCM Uncompressed (1)" : ((audioFmt == 3) ? L"IEEE Float (3)" : std::to_wstring(audioFmt)) });
                sheet.rows.push_back({ L"Channels", std::to_wstring(numCh) });
                sheet.rows.push_back({ L"Sample Rate", std::to_wstring(sampleRate) + L" Hz" });
                sheet.rows.push_back({ L"Bit Depth", std::to_wstring(bitsPerSample) + L" bit" });
                sheet.rows.push_back({ L"Byte Rate", std::to_wstring(byteRate) + L" B/s" });
            } else {
                sheet.rows.push_back({ L"Chunk [" + PathUtils::Utf8ToWide(cid) + L"]", std::to_wstring(clen) + L" B" });
            }
            pos += 8 + ((clen + 1) & ~1u);
        }
    } else if (isFlac && raw.size() >= 42) {
        // STREAMINFO metadata block at offset 8 (34 bytes)
        const uint8_t* si = raw.data() + 8;
        uint32_t sr = (static_cast<uint32_t>(si[10]) << 12) | (static_cast<uint32_t>(si[11]) << 4) | (si[12] >> 4);
        int ch = ((si[12] >> 1) & 0x07) + 1;
        int bps = (((si[12] & 0x01) << 4) | (si[13] >> 4)) + 1;
        sheet.rows.push_back({ L"Sample Rate", std::to_wstring(sr) + L" Hz" });
        sheet.rows.push_back({ L"Channels", std::to_wstring(ch) });
        sheet.rows.push_back({ L"Bits Per Sample", std::to_wstring(bps) + L" bit" });
    } else if (isAiff) {
        size_t pos = 12;
        while (pos + 8 <= raw.size()) {
            char cid[5] = {};
            std::memcpy(cid, raw.data() + pos, 4);
            uint32_t clen = _byteswap_ulong(*reinterpret_cast<const uint32_t*>(raw.data() + pos + 4));
            sheet.rows.push_back({ L"AIFF Chunk [" + PathUtils::Utf8ToWide(cid) + L"]", std::to_wstring(clen) + L" B" });
            pos += 8 + ((clen + 1) & ~1u);
        }
    }

    sheet.maxCols = 2;
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
