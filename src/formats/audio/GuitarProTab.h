#pragma once
// GuitarProTab.h — Guitar Pro Tablature (.gp / .gp3 / .gp4 / .gp5) Reader

namespace Formats {
namespace DetailMedia100 {

inline bool ParseGuitarPro(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    // 1. Guitar Pro 7/8 (.gp ZIP with Content/score.gpif)
    if (raw.size() >= 4 && std::memcmp(raw.data(), "PK\x03\x04", 4) == 0) {
        std::vector<uint8_t> gpif;
        if (!ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), ".gpif", gpif) || gpif.empty()) return false;
        std::string_view xml(reinterpret_cast<const char*>(gpif.data()), gpif.size());
        if (xml.find("<GPIF") == std::string_view::npos) return false;

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Guitar Pro 7/8 Tablature (.gp)";

        SpreadsheetSheet info;
        info.name = L"Score_Info";
        info.rows.push_back({ L"Field", L"Value" });
        auto addTag = [&](const char* tag, const wchar_t* label) {
            std::wstring v = ExtractFirstXmlTagText(xml, tag);
            if (!v.empty()) {
                // Strip <![CDATA[ ... ]]>
                if (v.size() >= 12 && v.substr(0, 9) == L"<![CDATA[" && v.substr(v.size() - 3) == L"]]>") {
                    v = v.substr(9, v.size() - 12);
                }
                if (!v.empty()) info.rows.push_back({ label, v });
            }
        };
        addTag("GPVersion", L"GP Version");
        addTag("Title", L"Title");
        addTag("SubTitle", L"Subtitle");
        addTag("Artist", L"Artist");
        addTag("Album", L"Album");
        addTag("Words", L"Lyrics / Words");
        addTag("Music", L"Composer");
        addTag("Tabber", L"Tabber");

        size_t mbCount = 0;
        size_t p = 0;
        while ((p = xml.find("<MasterBar>", p)) != std::string_view::npos) { ++mbCount; p += 11; }
        p = 0;
        while ((p = xml.find("<MasterBar ", p)) != std::string_view::npos) { ++mbCount; p += 11; }
        info.rows.push_back({ L"Master Bars", std::to_wstring(mbCount) });

        SpreadsheetSheet trks;
        trks.name = L"Tracks";
        trks.rows.push_back({ L"Track ID", L"Name", L"Tuning Pitches" });
        p = 0;
        while ((p = xml.find("<Track ", p)) != std::string_view::npos) {
            size_t gt = xml.find('>', p);
            size_t cl = (gt != std::string_view::npos) ? xml.find("</Track>", gt) : std::string_view::npos;
            if (gt == std::string_view::npos || cl == std::string_view::npos) break;
            std::string tid = ExtractXmlAttr(xml.substr(p, gt - p), "id");
            std::string_view blk = xml.substr(gt + 1, cl - gt - 1);
            std::wstring nm = ExtractFirstXmlTagText(blk, "Name");
            if (nm.size() >= 12 && nm.substr(0, 9) == L"<![CDATA[" && nm.substr(nm.size() - 3) == L"]]>") {
                nm = nm.substr(9, nm.size() - 12);
            }
            std::wstring pitches = ExtractFirstXmlTagText(blk, "Pitches");
            trks.rows.push_back({ PathUtils::Utf8ToWide(tid), nm.empty() ? L"(Track)" : nm, pitches });
            p = cl + 8;
        }
        FinalizeSheetCols(info);
        out.sheets.push_back(std::move(info));
        if (trks.rows.size() > 1) { FinalizeSheetCols(trks); out.sheets.push_back(std::move(trks)); }
        out.valid = true;
        return true;
    }

    // 2. Legacy Guitar Pro v3 / v4 / v5 Binary ("FICHIER GUITAR PRO vX.XX")
    if (raw.size() < 35 || raw[0] < 18 || raw[0] > 30) return false;
    std::string verBanner(reinterpret_cast<const char*>(raw.data() + 1), raw[0]);
    if (verBanner.find("FICHIER GUITAR PRO") == std::string::npos) return false;

    int gpMaj = 5;
    int gpMin = 0;
    size_t vPos = verBanner.find('v');
    if (vPos != std::string::npos && vPos + 1 < verBanner.size()) {
        gpMaj = verBanner[vPos + 1] - '0';
        if (vPos + 3 < verBanner.size() && verBanner[vPos + 2] == '.') {
            gpMin = std::atoi(verBanner.c_str() + vPos + 3);
        }
    }

    size_t pos = 31;
    auto readIntByteStr = [&](std::wstring& outStr) -> bool {
        if (pos + 4 > raw.size()) return false;
        uint32_t blkLen = DetailSys100::RdU32LE(raw.data() + pos);
        pos += 4;
        if (blkLen == 0) {
            if (pos < raw.size() && raw[pos] == 0) ++pos;
            outStr.clear();
            return true;
        }
        if (blkLen > raw.size() - pos || blkLen > 65536) return false;
        uint8_t sLen = raw[pos];
        size_t take = std::min<size_t>(sLen, blkLen - 1);
        outStr = Latin1OrCp1252ToWide(raw.data() + pos + 1, take);
        pos += blkLen;
        return true;
    };

    std::wstring title, subtitle, artist, album, words, music, copyright, tabber, instructions;
    if (!readIntByteStr(title)) return false;
    readIntByteStr(subtitle);
    readIntByteStr(artist);
    readIntByteStr(album);
    readIntByteStr(words);
    if (gpMaj >= 5) readIntByteStr(music);
    readIntByteStr(copyright);
    readIntByteStr(tabber);
    readIntByteStr(instructions);

    std::vector<std::wstring> notices;
    if (pos + 4 <= raw.size()) {
        uint32_t nCount = DetailSys100::RdU32LE(raw.data() + pos);
        pos += 4;
        for (uint32_t i = 0; i < nCount && i < 64; ++i) {
            std::wstring nl;
            if (!readIntByteStr(nl)) break;
            if (!nl.empty()) notices.push_back(nl);
        }
    }

    if (gpMaj <= 4 && pos < raw.size()) {
        pos += 1; // tripletFeel
    }
    if (gpMaj >= 4 && pos + 4 <= raw.size()) {
        pos += 4; // lyricTrack
        for (int i = 0; i < 5 && pos + 8 <= raw.size(); ++i) {
            pos += 4; // startBar
            uint32_t lLen = DetailSys100::RdU32LE(raw.data() + pos);
            pos += 4;
            if (lLen > raw.size() - pos) { pos = raw.size(); break; }
            pos += lLen;
        }
    }
    if (gpMaj >= 5) {
        if (gpMin >= 10 && pos + 19 <= raw.size()) pos += 19; // RSE master effect
        if (pos + 30 <= raw.size()) {
            pos += 30; // PageSetup dimensions & flags
            for (int i = 0; i < 10; ++i) {
                std::wstring dummy;
                if (!readIntByteStr(dummy)) break;
            }
        }
        std::wstring tempoName;
        readIntByteStr(tempoName);
    }

    uint32_t tempo = 0;
    uint32_t measureCount = 0;
    uint32_t trackCount = 0;
    if (pos + 4 <= raw.size()) {
        tempo = DetailSys100::RdU32LE(raw.data() + pos);
        pos += 4;
        if (gpMaj >= 5 && gpMin >= 10 && pos < raw.size()) pos += 1; // hideTempo
        if (gpMaj >= 4) pos += 5; // key(1) + octave(4)
        else pos += 4;            // key(4)
        if (pos + 768 <= raw.size()) {
            pos += 768; // 64 MIDI channel ports (64 * 12B)
            if (gpMaj >= 5 && pos + 46 <= raw.size()) pos += 46; // musical directions(42) + master reverb(4)
            if (pos + 8 <= raw.size()) {
                measureCount = DetailSys100::RdU32LE(raw.data() + pos);
                trackCount   = DetailSys100::RdU32LE(raw.data() + pos + 4);
            }
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Guitar Pro Tablature (" + extLower + L")";

    SpreadsheetSheet info;
    info.name = L"GP_Header";
    info.rows.push_back({ L"Property", L"Value" });
    info.rows.push_back({ L"Version", PathUtils::Utf8ToWide(verBanner) });
    if (!title.empty())        info.rows.push_back({ L"Title", title });
    if (!subtitle.empty())     info.rows.push_back({ L"Subtitle", subtitle });
    if (!artist.empty())       info.rows.push_back({ L"Artist", artist });
    if (!album.empty())        info.rows.push_back({ L"Album", album });
    if (!words.empty())        info.rows.push_back({ L"Words", words });
    if (!music.empty())        info.rows.push_back({ L"Music", music });
    if (!copyright.empty())    info.rows.push_back({ L"Copyright", copyright });
    if (!tabber.empty())       info.rows.push_back({ L"Tabber", tabber });
    if (!instructions.empty()) info.rows.push_back({ L"Instructions", instructions });
    if (tempo > 0 && tempo < 1000) info.rows.push_back({ L"Tempo", std::to_wstring(tempo) + L" BPM" });
    if (measureCount > 0 && measureCount < 100000) info.rows.push_back({ L"Measures", std::to_wstring(measureCount) });
    if (trackCount > 0 && trackCount < 256) info.rows.push_back({ L"Tracks", std::to_wstring(trackCount) });
    for (size_t i = 0; i < notices.size(); ++i) {
        info.rows.push_back({ L"Notice " + std::to_wstring(i + 1), notices[i] });
    }
    FinalizeSheetCols(info);
    out.sheets.push_back(std::move(info));
    out.valid = true;
    return true;
}

} // namespace DetailMedia100

inline bool ParseGuitarProTabDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) { return DetailMedia100::ParseGuitarPro(raw, extLower, out); }

} // namespace Formats
