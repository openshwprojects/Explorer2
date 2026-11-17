#pragma once
// AudioContainer.h — audio file properties for WAV (RIFF / RF64 WAVE), FLAC and AIFF / AIFC:
//   WAV:  fmt (PCM, IEEE float, A-law, µ-law, WAVE_FORMAT_EXTENSIBLE with its sub-format), data size -> duration,
//         LIST/INFO tags, other chunks listed
//   FLAC: every metadata block: STREAMINFO (rate, channels, bits, total samples -> duration, MD5), VORBIS_COMMENT
//         tags, PICTURE, SEEKTABLE, CUESHEET, APPLICATION, PADDING
//   AIFF: COMM (channels, frames, bits, 80-bit extended sample rate -> duration, AIFC compression), NAME / AUTH /
//         (c) / ANNO text, other chunks listed
// Readers get only a file's first 32 MB: chunks and metadata blocks past that (tags written after the audio) are
// read from the file itself, the audio never

namespace Formats {

namespace DetailAudio {

inline uint32_t Le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
inline uint16_t Le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t Be32(const uint8_t* p) { return (static_cast<uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
inline uint16_t Be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

// IEEE 754 80-bit extended (AIFF sample rate)
inline double Extended80(const uint8_t* p) {
    int exp = ((p[0] & 0x7F) << 8) | p[1];
    uint64_t mant = 0;
    for (int i = 0; i < 8; ++i) mant = (mant << 8) | p[2 + i];
    if (exp == 0 && mant == 0) return 0.0;
    double v = std::ldexp(static_cast<double>(mant), exp - 16383 - 63);
    return (p[0] & 0x80) ? -v : v;
}

inline std::wstring Duration(double seconds) {
    if (!(seconds >= 0.0) || !std::isfinite(seconds)) return L"";
    long long ms = static_cast<long long>(seconds * 1000.0 + 0.5);
    wchar_t buf[64];
    if (ms >= 3600000) swprintf_s(buf, L"%lld:%02lld:%02lld.%03lld", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000);
    else swprintf_s(buf, L"%lld:%02lld.%03lld", ms / 60000, (ms / 1000) % 60, ms % 1000);
    return buf;
}

inline std::wstring FourCC(const uint8_t* p) {
    std::wstring s;
    for (int i = 0; i < 4; ++i) s.push_back((p[i] >= 32 && p[i] < 127) ? static_cast<wchar_t>(p[i]) : L'?');
    return s;
}

inline std::wstring TextField(const uint8_t* p, size_t n) {
    std::string s(reinterpret_cast<const char*>(p), n);
    while (!s.empty() && (s.back() == '\0' || s.back() == ' ')) s.pop_back();
    return PathUtils::Utf8ToWide(s);
}

} // namespace DetailAudio

inline bool ParseAudioContainerDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using namespace DetailAudio;
    const uint8_t* d = raw.data();
    const size_t n = raw.size();
    const uint64_t fileSize = std::max<uint64_t>(out.sourceFileSize, n); // the reader may hold only the file's start
    if (n < 12) return false;
    bool isWav = (std::memcmp(d, "RIFF", 4) == 0 || std::memcmp(d, "RF64", 4) == 0) && std::memcmp(d + 8, "WAVE", 4) == 0;
    bool isFlac = std::memcmp(d, "fLaC", 4) == 0;
    bool isAiff = std::memcmp(d, "FORM", 4) == 0 && (std::memcmp(d + 8, "AIFF", 4) == 0 || std::memcmp(d + 8, "AIFC", 4) == 0);
    if (!isWav && !isFlac && !isAiff) return false;

    SpreadsheetSheet sheet;
    sheet.name = L"Audio_Properties";
    sheet.rows.push_back({ L"Property / Chunk", L"Value / Details" });
    auto add = [&](const std::wstring& k, const std::wstring& v) { if (sheet.rows.size() < 2000) sheet.rows.push_back({ k, v }); };
    size_t chunks = 0;
    constexpr size_t kMaxChunks = 512;

    if (isWav) {
        out.formatTitle = std::memcmp(d, "RF64", 4) == 0 ? L"RF64 WAVE Audio (.wav)" : L"RIFF WAVE Audio Container (.wav)";
        add(L"Container Format", out.formatTitle);
        add(L"File Size", std::to_wstring(fileSize) + L" B");
        uint16_t fmtTag = 0, channels = 0, bits = 0, blockAlign = 0;
        uint32_t rate = 0, byteRate = 0;
        uint64_t dataBytes = 0, ds64Data = 0;
        bool haveFmt = false;
        SourceBytes src(raw, out);
        std::vector<uint8_t> headBuf, bodyBuf;
        uint64_t pos = 12;
        bool walkedAll = false; // every chunk up to the end of the file was seen
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
            uint64_t len = Le32(c + 4);
            const uint64_t body = pos + 8;
            // only the bodies shown are read (fmt, ds64, LIST), never the audio
            size_t avail = 0;
            const uint8_t* b = nullptr;
            if (std::memcmp(c, "ds64", 4) == 0 || std::memcmp(c, "fmt ", 4) == 0 || std::memcmp(c, "LIST", 4) == 0) {
                b = src.ReadSome(body, len, 1u << 20, bodyBuf, avail);
            }
            if (std::memcmp(c, "ds64", 4) == 0 && len >= 16 && avail >= 16) {
                ds64Data = static_cast<uint64_t>(Le32(b + 8)) | (static_cast<uint64_t>(Le32(b + 12)) << 32);
            } else if (std::memcmp(c, "fmt ", 4) == 0 && len >= 16 && avail >= 16) {
                haveFmt = true;
                fmtTag = Le16(b);
                channels = Le16(b + 2);
                rate = Le32(b + 4);
                byteRate = Le32(b + 8);
                blockAlign = Le16(b + 12);
                bits = Le16(b + 14);
                std::wstring enc;
                uint16_t effective = fmtTag;
                if (fmtTag == 0xFFFE && len >= 40 && avail >= 40) {
                    effective = Le16(b + 24); // SubFormat GUID's first two bytes
                    add(L"Valid bits per sample", std::to_wstring(Le16(b + 18)));
                    wchar_t mask[24];
                    swprintf_s(mask, L"0x%08X", Le32(b + 20));
                    add(L"Channel mask", mask);
                }
                switch (effective) {
                    case 1: enc = L"PCM integer"; break;
                    case 3: enc = L"IEEE float"; break;
                    case 6: enc = L"A-law"; break;
                    case 7: enc = L"µ-law"; break;
                    case 2: enc = L"Microsoft ADPCM"; break;
                    case 0x11: enc = L"IMA ADPCM"; break;
                    case 0x55: enc = L"MPEG Layer 3"; break;
                    case 0x2000: enc = L"Dolby AC-3"; break;
                    default: enc = L"Format tag " + std::to_wstring(effective); break;
                }
                if (fmtTag == 0xFFFE) enc += L" (WAVE_FORMAT_EXTENSIBLE)";
                add(L"Audio Encoding", enc);
                add(L"Channels", std::to_wstring(channels));
                add(L"Sample Rate", std::to_wstring(rate) + L" Hz");
                add(L"Bit Depth", std::to_wstring(bits) + L" bit");
                add(L"Byte Rate", std::to_wstring(byteRate) + L" B/s");
                add(L"Block Align", std::to_wstring(blockAlign) + L" B");
            } else if (std::memcmp(c, "data", 4) == 0) {
                // 0xFFFFFFFF: the size is in ds64 (RF64), or unknown with the data running to the end of the file
                dataBytes = (len == 0xFFFFFFFFu) ? (ds64Data ? ds64Data : (fileSize > body ? fileSize - body : 0)) : len;
                add(L"Chunk [data]", std::to_wstring(dataBytes) + L" B");
                if (len == 0xFFFFFFFFu && !ds64Data) {
                    walkedAll = true; // the rest of the file is audio
                    break;
                }
                len = dataBytes;
            } else if (std::memcmp(c, "LIST", 4) == 0 && len >= 4 && avail >= 4 && std::memcmp(b, "INFO", 4) == 0) {
                static const struct { const char* id; const wchar_t* name; } kInfo[] = {
                    { "INAM", L"Title" }, { "IART", L"Artist" }, { "IPRD", L"Album" }, { "ICRD", L"Date" }, { "IGNR", L"Genre" },
                    { "ICMT", L"Comment" }, { "ICOP", L"Copyright" }, { "ISFT", L"Software" }, { "ITRK", L"Track" }, { "IENG", L"Engineer" },
                };
                size_t q = 4;
                while (q + 8 <= avail) {
                    uint32_t sl = Le32(b + q + 4);
                    if (sl > avail - q - 8) break;
                    std::wstring label = L"INFO " + FourCC(b + q);
                    for (const auto& k : kInfo) if (std::memcmp(b + q, k.id, 4) == 0) { label = k.name; break; }
                    add(label, TextField(b + q + 8, sl));
                    q += 8 + sl + (sl & 1);
                }
            } else {
                add(L"Chunk [" + FourCC(c) + L"]", std::to_wstring(len) + L" B");
            }
            const uint64_t next = body + len + (len & 1);
            if (next <= pos) break;
            if (next >= src.Size()) {
                walkedAll = body + len <= src.Size(); // (a missing final pad byte is common)
                break;
            }
            pos = next;
        }
        if (walkedAll && !src.ReadFailed()) out.sourceReadFully = true;
        if (!haveFmt) return false;
        if (byteRate > 0 && dataBytes > 0) {
            add(L"Duration", Duration(static_cast<double>(dataBytes) / byteRate));
            if (blockAlign > 0) add(L"Sample frames", std::to_wstring(dataBytes / blockAlign));
        }
    } else if (isFlac) {
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
    } else {
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
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    sheet.maxCols = 2;
    out.sheets.push_back(std::move(sheet));
    out.valid = true;
    return true;
}

} // namespace Formats
