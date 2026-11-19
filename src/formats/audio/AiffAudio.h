#pragma once
// AiffAudio.h — Audio IFF / AIFC Container (.aiff / .aif / .aifc) Inspector

namespace Formats {

inline bool ParseAiffAudioDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using namespace DetailAudio;
    const uint8_t* d = raw.data();
    const size_t n = raw.size();
    const uint64_t fileSize = std::max<uint64_t>(out.sourceFileSize, n); // the reader may hold only the file's start
    if (n < 12) return false;

    if (std::memcmp(d, "FORM", 4) != 0 || (std::memcmp(d + 8, "AIFF", 4) != 0 && std::memcmp(d + 8, "AIFC", 4) != 0)) return false;
    SpreadsheetSheet sheet;
    sheet.name = L"Audio_Properties";
    sheet.rows.push_back({ L"Property / Chunk", L"Value / Details" });
    auto add = [&](const std::wstring& k, const std::wstring& v) { if (sheet.rows.size() < 2000) sheet.rows.push_back({ k, v }); };
    size_t chunks = 0;
    constexpr size_t kMaxChunks = 512;

        bool aifc = std::memcmp(d + 8, "AIFC", 4) == 0;
        out.formatTitle = aifc ? L"Audio IFF-C Container (.aifc)" : L"Audio IFF Container (.aiff)";
        add(L"Container Format", out.formatTitle);
        add(L"File Size", std::to_wstring(fileSize) + L" B");
        bool haveComm = false;
        SourceBytes src(raw, out);
        std::vector<uint8_t> headBuf, bodyBuf;
        uint64_t pos = 12;
        bool walkedAll = false;
        while (chunks < kMaxChunks) {
            if (pos + 8 > src.Size()) {
                walkedAll = true;
                break;
            }
            ++chunks;
            const uint8_t* hp = src.Read(pos, 8, headBuf);
            if (!hp) break;
            uint8_t c[8];
            std::memcpy(c, hp, 8);
            const uint32_t len = Be32(c + 4);
            const uint64_t body = pos + 8;
            const bool isText = std::memcmp(c, "NAME", 4) == 0 || std::memcmp(c, "AUTH", 4) == 0 || std::memcmp(c, "(c) ", 4) == 0 ||
                                std::memcmp(c, "ANNO", 4) == 0;
            size_t avail = 0;
            const uint8_t* b = nullptr;
            if (std::memcmp(c, "COMM", 4) == 0 || isText) b = src.ReadSome(body, len, 1u << 20, bodyBuf, avail);
            if (std::memcmp(c, "COMM", 4) == 0 && len >= 18 && avail >= 18) {
                haveComm = true;
                uint16_t ch = Be16(b);
                uint32_t frames = Be32(b + 2);
                uint16_t bits = Be16(b + 6);
                double rate = Extended80(b + 8);
                add(L"Channels", std::to_wstring(ch));
                add(L"Sample frames", std::to_wstring(frames));
                add(L"Bit Depth", std::to_wstring(bits) + L" bit");
                if (rate > 0.0 && rate < 1e7) {
                    add(L"Sample Rate", std::to_wstring(static_cast<long long>(rate + 0.5)) + L" Hz");
                    add(L"Duration", Duration(frames / rate));
                }
                if (aifc && len >= 22 && avail >= 22) {
                    std::wstring comp = FourCC(b + 18);
                    if (len >= 23 && avail >= 23) {
                        size_t nl = b[22];
                        if (nl <= len - 23 && nl <= avail - 23) comp += L" — " + TextField(b + 23, nl);
                    }
                    add(L"Compression", comp);
                }
            } else if (isText && len <= avail) {
                const wchar_t* label = std::memcmp(c, "NAME", 4) == 0 ? L"Name" : std::memcmp(c, "AUTH", 4) == 0 ? L"Author"
                                     : std::memcmp(c, "ANNO", 4) == 0 ? L"Annotation" : L"Copyright";
                add(label, TextField(b, len));
            } else {
                add(L"Chunk [" + FourCC(c) + L"]", std::to_wstring(len) + L" B");
            }
            const uint64_t next = body + len + (len & 1);
            if (next <= pos) break;
            if (next >= src.Size()) {
                walkedAll = body + len <= src.Size();
                break;
            }
            pos = next;
        }
        if (walkedAll && !src.ReadFailed()) out.sourceReadFully = true;
        if (!haveComm) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    sheet.maxCols = 2;
    out.sheets.push_back(std::move(sheet));
    out.valid = true;
    return true;
}


} // namespace Formats
