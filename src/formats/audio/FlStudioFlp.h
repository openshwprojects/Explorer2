#pragma once
// FlStudioFlp.h — FL Studio Project (.flp) Reader

namespace Formats {
namespace DetailMediaAndScoreDoc {

inline bool ParseFlStudioFlp(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 22 || std::memcmp(raw.data(), "FLhd", 4) != 0 ||
        DetailBinarySysDoc::RdU32LE(raw.data() + 4) != 6 || std::memcmp(raw.data() + 14, "FLdt", 4) != 0)
    {
        return false;
    }

    int16_t flpFormat = static_cast<int16_t>(DetailBinarySysDoc::RdU16LE(raw.data() + 8));
    uint16_t numChannels = DetailBinarySysDoc::RdU16LE(raw.data() + 10);
    uint16_t ppq = DetailBinarySysDoc::RdU16LE(raw.data() + 12);
    uint32_t dataLen = DetailBinarySysDoc::RdU32LE(raw.data() + 18);

    size_t pos = 22;
    size_t end = std::min<size_t>(raw.size(), 22ULL + dataLen);
    bool isUnicode = false;
    std::wstring flVersion;
    std::wstring projTitle;
    std::wstring projAuthor;
    std::wstring projGenre;
    std::wstring projComment;
    double tempoBpm = 0.0;
    size_t eventCount = 0;

    std::vector<std::pair<std::wstring, std::wstring>> items;

    auto decodeFlpText = [&](const uint8_t* p, size_t len, bool forceAscii) -> std::wstring {
        if (len == 0) return {};
        if (!forceAscii && (isUnicode || (len >= 4 && (len % 2) == 0 && p[1] == 0 && p[len - 1] == 0))) {
            return Utf16BytesToWide(p, len, false);
        }
        size_t n = 0;
        while (n < len && p[n] != 0) ++n;
        return PathUtils::Trim(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), n)));
    };

    while (pos < end) {
        uint8_t ev = raw[pos++];
        ++eventCount;
        if (ev < 64) {
            if (pos + 1 > end) break;
            pos += 1;
        } else if (ev < 128) {
            if (pos + 2 > end) break;
            uint16_t w = DetailBinarySysDoc::RdU16LE(raw.data() + pos);
            pos += 2;
            if (ev == 66 && tempoBpm == 0.0) tempoBpm = static_cast<double>(w);
        } else if (ev < 192) {
            if (pos + 4 > end) break;
            uint32_t dw = DetailBinarySysDoc::RdU32LE(raw.data() + pos);
            pos += 4;
            if (ev == 156) tempoBpm = static_cast<double>(dw) / 1000.0;
        } else {
            uint64_t vLen = 0;
            int shift = 0;
            while (pos < end && shift < 35) {
                uint8_t b = raw[pos++];
                vLen |= static_cast<uint64_t>(b & 0x7Fu) << shift;
                shift += 7;
                if ((b & 0x80u) == 0) break;
            }
            if (vLen > end - pos) break;
            const uint8_t* p = raw.data() + pos;
            size_t len = static_cast<size_t>(vLen);
            pos += len;

            if (ev == 199) {
                flVersion = decodeFlpText(p, len, true);
                int maj = std::wcstol(flVersion.c_str(), nullptr, 10);
                if (maj >= 12 || flVersion.find(L"11.5") == 0) isUnicode = true;
            } else if (ev == 194) {
                projTitle = decodeFlpText(p, len, false);
            } else if (ev == 195) {
                projComment = decodeFlpText(p, len, false);
            } else if (ev == 206) {
                projGenre = decodeFlpText(p, len, false);
            } else if (ev == 207) {
                projAuthor = decodeFlpText(p, len, false);
            } else if (ev == 192 || ev == 203) {
                std::wstring nm = decodeFlpText(p, len, false);
                if (!nm.empty() && items.size() < 512) items.push_back({ L"Channel", nm });
            } else if (ev == 193) {
                std::wstring nm = decodeFlpText(p, len, false);
                if (!nm.empty() && items.size() < 512) items.push_back({ L"Pattern", nm });
            } else if (ev == 196) {
                std::wstring nm = decodeFlpText(p, len, false);
                if (!nm.empty() && items.size() < 512) items.push_back({ L"Sample File", nm });
            } else if (ev == 201) {
                std::wstring nm = decodeFlpText(p, len, false);
                if (!nm.empty() && items.size() < 512) items.push_back({ L"Plugin", nm });
            }
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"FL Studio Project (.flp)";

    SpreadsheetSheet hdr;
    hdr.name = L"FLP_Project";
    hdr.rows.push_back({ L"Property", L"Value" });
    if (!flVersion.empty()) hdr.rows.push_back({ L"FL Studio Version", flVersion });
    if (!projTitle.empty()) hdr.rows.push_back({ L"Title", projTitle });
    if (!projAuthor.empty()) hdr.rows.push_back({ L"Author", projAuthor });
    if (!projGenre.empty()) hdr.rows.push_back({ L"Genre", projGenre });
    if (!projComment.empty()) hdr.rows.push_back({ L"Comment", projComment });
    hdr.rows.push_back({ L"Format / Channels / PPQ",
        std::to_wstring(flpFormat) + L" / " + std::to_wstring(numChannels) + L" / " + std::to_wstring(ppq) });
    if (tempoBpm > 0.0) {
        wchar_t tbuf[64] = {};
        swprintf_s(tbuf, L"%.3f BPM", tempoBpm);
        hdr.rows.push_back({ L"Tempo", tbuf });
    }
    hdr.rows.push_back({ L"Decoded TLV Events", std::to_wstring(eventCount) });
    FinalizeSheetCols(hdr);
    out.sheets.push_back(std::move(hdr));

    if (!items.empty()) {
        SpreadsheetSheet chSh;
        chSh.name = L"Channels_&_Plugins";
        chSh.rows.push_back({ L"Type", L"Name / Path" });
        for (const auto& it : items) chSh.rows.push_back({ it.first, it.second });
        FinalizeSheetCols(chSh);
        out.sheets.push_back(std::move(chSh));
    }

    out.valid = true;
    return true;
}

} // namespace DetailMediaAndScoreDoc

inline bool ParseFlStudioFlpDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMediaAndScoreDoc::ParseFlStudioFlp(raw, out); }

} // namespace Formats
