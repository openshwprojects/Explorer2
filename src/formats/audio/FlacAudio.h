#pragma once
// FlacAudio.h — Free Lossless Audio Codec (.flac) Metadata Block Inspector

namespace Formats {

inline bool ParseFlacAudioDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using namespace DetailAudio;
    const uint8_t* d = raw.data();
    const size_t n = raw.size();
    const uint64_t fileSize = std::max<uint64_t>(out.sourceFileSize, n); // the reader may hold only the file's start
    if (n < 12) return false;

    if (std::memcmp(d, "fLaC", 4) != 0) return false;
    SpreadsheetSheet sheet;
    sheet.name = L"Audio_Properties";
    sheet.rows.push_back({ L"Property / Chunk", L"Value / Details" });
    auto add = [&](const std::wstring& k, const std::wstring& v) { if (sheet.rows.size() < 2000) sheet.rows.push_back({ k, v }); };
    size_t chunks = 0;
    constexpr size_t kMaxChunks = 512;

        out.formatTitle = L"Free Lossless Audio Codec (.flac)";
        add(L"Container Format", out.formatTitle);
        add(L"File Size", std::to_wstring(fileSize) + L" B");
        SourceBytes src(raw, out);
        std::vector<uint8_t> headBuf, bodyBuf;
        uint64_t pos = 4;
        bool haveInfo = false, last = false, sawLast = false;
        while (!last && pos + 4 <= src.Size() && chunks < kMaxChunks) {
            ++chunks;
            const uint8_t* hp = src.Read(pos, 4, headBuf);
            if (!hp) break;
            last = (hp[0] & 0x80) != 0;
            int type = hp[0] & 0x7F;
            size_t len = (static_cast<size_t>(hp[1]) << 16) | (hp[2] << 8) | hp[3];
            const uint64_t body = pos + 4;
            if (len > src.Size() - body) break;
            // only the bodies shown are read: STREAMINFO, the comments, a picture's MIME type
            const size_t want = (type == 0 || type == 4) ? len : (type == 6) ? std::min<size_t>(len, 8 + 256) : 0;
            const uint8_t* b = want ? src.Read(body, want, bodyBuf) : nullptr;
            if (want && !b) break;
            if (type == 0 && len >= 34) {
                haveInfo = true;
                uint32_t sr = (static_cast<uint32_t>(b[10]) << 12) | (b[11] << 4) | (b[12] >> 4);
                int ch = ((b[12] >> 1) & 0x07) + 1;
                int bps = (((b[12] & 0x01) << 4) | (b[13] >> 4)) + 1;
                uint64_t total = (static_cast<uint64_t>(b[13] & 0x0F) << 32) | Be32(b + 14);
                add(L"Sample Rate", std::to_wstring(sr) + L" Hz");
                add(L"Channels", std::to_wstring(ch));
                add(L"Bits Per Sample", std::to_wstring(bps) + L" bit");
                add(L"Block size (samples)", std::to_wstring(Be16(b)) + L" .. " + std::to_wstring(Be16(b + 2)));
                if (total > 0 && sr > 0) {
                    add(L"Total samples", std::to_wstring(total));
                    add(L"Duration", Duration(static_cast<double>(total) / sr));
                    double seconds = static_cast<double>(total) / sr;
                    if (seconds > 0.0) add(L"Average bitrate", std::to_wstring(static_cast<long long>(fileSize * 8 / seconds / 1000.0)) + L" kbit/s");
                }
                std::wstring md5;
                for (int i = 0; i < 16; ++i) { wchar_t hx[4]; swprintf_s(hx, L"%02x", b[18 + i]); md5 += hx; }
                add(L"Audio MD5", md5);
            } else if (type == 4 && len >= 8) {
                // VORBIS_COMMENT: little-endian lengths, vendor string, then "KEY=value" fields
                size_t q = 0;
                uint32_t vl = Le32(b);
                q = 4;
                if (vl <= len - q) {
                    add(L"Encoder", TextField(b + q, vl));
                    q += vl;
                    if (q + 4 <= len) {
                        uint32_t count = Le32(b + q);
                        q += 4;
                        for (uint32_t i = 0; i < count && i < 256 && q + 4 <= len; ++i) {
                            uint32_t cl = Le32(b + q);
                            q += 4;
                            if (cl > len - q) break;
                            std::string field(reinterpret_cast<const char*>(b + q), cl);
                            q += cl;
                            size_t eq = field.find('=');
                            if (eq == std::string::npos) continue;
                            std::string key = field.substr(0, eq);
                            for (char& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                            add(L"Tag " + PathUtils::Utf8ToWide(key), PathUtils::Utf8ToWide(field.substr(eq + 1)));
                        }
                    }
                }
            } else if (type == 6 && len >= 32) {
                uint32_t mimeLen = Be32(b + 4);
                std::wstring mime = (mimeLen <= want - 8) ? TextField(b + 8, mimeLen) : L"";
                add(L"Picture", mime + L" (" + std::to_wstring(len) + L" B)");
            } else {
                static const wchar_t* kNames[] = { L"STREAMINFO", L"PADDING", L"APPLICATION", L"SEEKTABLE", L"VORBIS_COMMENT", L"CUESHEET", L"PICTURE" };
                std::wstring nm = (type < 7) ? kNames[type] : (L"Block type " + std::to_wstring(type));
                add(L"Metadata [" + nm + L"]", std::to_wstring(len) + L" B");
            }
            pos = body + len;
            if (last) sawLast = true;
        }
        if (!haveInfo) return false;
        if (sawLast) out.sourceReadFully = true; // every metadata block was read; what follows is audio frames

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    sheet.maxCols = 2;
    out.sheets.push_back(std::move(sheet));
    out.valid = true;
    return true;
}


} // namespace Formats
