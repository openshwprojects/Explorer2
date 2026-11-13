#pragma once
// ExtendedMediaDoc100.h — 14 Audio Production, Tracker, DAW, NLE, Email, E-Book & Help Formats (87–100)
// Covers: .ogg/.opus/.spx, .mp3(ID3v2), .mod/.xm/.it/.s3m, .sf2/.sfz, .gp/.gp5/.mscz, .als/.flp/.rpp,
//         .edl/.fcpxml/.otio, .sub/.idx/.sup, .eml/.mbox, .msg, .mobi/.azw3, .djvu, .chm

namespace Formats {

inline bool ParseExtendedMediaDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());

    // 95. RFC 5322 Email Message (.eml / .mbox)
    if (extLower == L".eml" || extLower == L".mbox") {
        out.kind = OfficeFileKind::WordDocx;
        out.isSpreadsheet = false;
        out.formatTitle = (extLower == L".mbox") ? L"Unix Mailbox Archive (.mbox)" : L"RFC 5322 Email Message (.eml)";

        size_t hdrEnd = sv.find("\r\n\r\n");
        size_t sepLen = 4;
        if (hdrEnd == std::string_view::npos) { hdrEnd = sv.find("\n\n"); sepLen = 2; }
        std::string_view headers = (hdrEnd != std::string_view::npos) ? sv.substr(0, hdrEnd) : sv;
        std::string_view body    = (hdrEnd != std::string_view::npos) ? sv.substr(hdrEnd + sepLen) : std::string_view{};

        auto extractHdr = [&](const char* prefix) {
            size_t p = headers.find(prefix);
            if (p == std::string_view::npos) return;
            size_t eol = headers.find('\n', p);
            if (eol == std::string_view::npos) eol = headers.size();
            out.paragraphs.push_back({ PathUtils::Trim(PathUtils::Utf8ToWide(headers.substr(p, eol - p))), 2, false, false });
        };
        extractHdr("Subject:");
        extractHdr("From:");
        extractHdr("To:");
        extractHdr("Date:");

        if (!body.empty()) {
            out.paragraphs.push_back({ PathUtils::Trim(PathUtils::Utf8ToWide(body.substr(0, std::min<size_t>(body.size(), 8192)))), 0, false, false });
        }
        out.wordCount = raw.size() / 6;
        out.charCount = raw.size();
        out.valid = !out.paragraphs.empty();
        return out.valid;
    }

    // 87–94, 96–100: Ogg/Opus, MP3 ID3v2, Tracker Modules, SoundFont, GuitarPro, DAW, NLE, Subtitles, MSG, MOBI/AZW3, DjVu, CHM
    if (extLower == L".ogg" || extLower == L".opus" || extLower == L".spx" ||
        extLower == L".mp3" || extLower == L".mod"  || extLower == L".xm"  || extLower == L".it" || extLower == L".s3m" ||
        extLower == L".sf2" || extLower == L".sfz"  || extLower == L".gp"  || extLower == L".gp5"|| extLower == L".mscz"||
        extLower == L".als" || extLower == L".flp"  || extLower == L".rpp" || extLower == L".edl"|| extLower == L".fcpxml" ||
        extLower == L".otio"|| extLower == L".sub"  || extLower == L".idx" || extLower == L".sup"|| extLower == L".msg" ||
        extLower == L".mobi"|| extLower == L".azw3" || extLower == L".djvu"|| extLower == L".chm")
    {
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;

        std::wstring title = L"Media / Document Inspector (" + extLower + L")";
        if (extLower == L".ogg" || extLower == L".opus" || extLower == L".spx") title = L"Xiph Ogg / Opus Audio Container (" + extLower + L")";
        else if (extLower == L".mp3") title = L"MPEG-1 Layer III / ID3v2 Tag Inspector (.mp3)";
        else if (extLower == L".mod" || extLower == L".xm" || extLower == L".it" || extLower == L".s3m") title = L"Demoscene Tracker Music Module (" + extLower + L")";
        else if (extLower == L".sf2" || extLower == L".sfz") title = L"SoundFont Synthesizer Sample Bank (" + extLower + L")";
        else if (extLower == L".gp" || extLower == L".gp5" || extLower == L".mscz") title = L"Guitar Pro / MuseScore Tablature (" + extLower + L")";
        else if (extLower == L".als" || extLower == L".flp" || extLower == L".rpp") title = L"DAW Audio Production Project (" + extLower + L")";
        else if (extLower == L".edl" || extLower == L".fcpxml" || extLower == L".otio") title = L"NLE Video Timeline Decision List (" + extLower + L")";
        else if (extLower == L".sub" || extLower == L".idx" || extLower == L".sup") title = L"DVD VobSub / Blu-ray PGS Subtitle Stream (" + extLower + L")";
        else if (extLower == L".msg") title = L"Microsoft Outlook MAPI Message (.msg)";
        else if (extLower == L".mobi" || extLower == L".azw3") title = L"Amazon Kindle / Mobipocket E-Book (" + extLower + L")";
        else if (extLower == L".djvu") title = L"AT&T DjVu Scanned Document (.djvu)";
        else if (extLower == L".chm") title = L"Microsoft Compiled HTML Help (ITSF .chm)";
        out.formatTitle = title;

        SpreadsheetSheet sh;
        sh.name = L"Metadata_And_Structure";
        sh.maxCols = 2;
        sh.rows.push_back({ L"Field / Tag", L"Value" });
        sh.rows.push_back({ L"Container Format", title });
        sh.rows.push_back({ L"File Size", PathUtils::FormatByteSize(raw.size()) });

        // Format-specific binary header decoding
        if (raw.size() >= 10 && std::memcmp(raw.data(), "ID3", 3) == 0) {
            sh.rows.push_back({ L"ID3v2 Version", L"ID3v2." + std::to_wstring(raw[3]) + L"." + std::to_wstring(raw[4]) });
        } else if (raw.size() >= 28 && std::memcmp(raw.data(), "OggS", 4) == 0) {
            sh.rows.push_back({ L"Ogg Bitstream Serial", DetailSys100::Hex32(DetailSys100::RdU32LE(raw.data() + 14)) });
        } else if (raw.size() >= 68 && std::memcmp(raw.data() + 60, "BOOKMOBI", 8) == 0) {
            std::string pdbName(reinterpret_cast<const char*>(raw.data()), strnlen(reinterpret_cast<const char*>(raw.data()), 32));
            sh.rows.push_back({ L"PalmDOC / Kindle Title", PathUtils::Utf8ToWide(pdbName) });
        } else if (raw.size() >= 16 && std::memcmp(raw.data(), "AT&TFORM", 8) == 0) {
            sh.rows.push_back({ L"DjVu IFF Form Type", PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + 12), 4)) });
        } else if (raw.size() >= 24 && std::memcmp(raw.data(), "ITSF", 4) == 0) {
            sh.rows.push_back({ L"CHM ITSF Version", std::to_wstring(DetailSys100::RdU32LE(raw.data() + 4)) });
        }

        // Extract printable strings/tags
        std::string cur;
        for (size_t i = 0; i < raw.size() && sh.rows.size() < 40; ++i) {
            if (raw[i] >= 32 && raw[i] <= 126) cur.push_back(static_cast<char>(raw[i]));
            else {
                if (cur.size() >= 4 && cur.size() <= 80) {
                    sh.rows.push_back({ L"Embedded Text / Tag", PathUtils::Utf8ToWide(cur) });
                }
                cur.clear();
            }
        }

        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    return false;
}

} // namespace Formats
