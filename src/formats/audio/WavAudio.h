#pragma once
// WavAudio.h — RIFF / RF64 WAVE Audio Container (.wav) Inspector

namespace Formats {

inline bool ParseWavAudioDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using namespace DetailAudio;
    const uint8_t* d = raw.data();
    const size_t n = raw.size();
    const uint64_t fileSize = std::max<uint64_t>(out.sourceFileSize, n); // the reader may hold only the file's start
    if (n < 12) return false;

    if ((std::memcmp(d, "RIFF", 4) != 0 && std::memcmp(d, "RF64", 4) != 0) || std::memcmp(d + 8, "WAVE", 4) != 0) return false;
    SpreadsheetSheet sheet;
    sheet.name = L"Audio_Properties";
    sheet.rows.push_back({ L"Property / Chunk", L"Value / Details" });
    auto add = [&](const std::wstring& k, const std::wstring& v) { if (sheet.rows.size() < 2000) sheet.rows.push_back({ k, v }); };
    size_t chunks = 0;
    constexpr size_t kMaxChunks = 512;

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

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    sheet.maxCols = 2;
    out.sheets.push_back(std::move(sheet));
    out.valid = true;
    return true;
}


} // namespace Formats
