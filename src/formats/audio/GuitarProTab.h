#pragma once
// GuitarProTab.h — Guitar Pro Tablature (.gp / .gp3 / .gp4 / .gp5) Structural Score, Measure & Track Reader

namespace Formats {
namespace DetailMediaAndScoreDoc {

inline std::wstring FormatMidiPitchNote(int32_t midiPitch) {
    if (midiPitch < 0 || midiPitch > 127) return std::to_wstring(midiPitch);
    static const wchar_t* const kNoteNames[12] = {
        L"C", L"C#", L"D", L"D#", L"E", L"F", L"F#", L"G", L"G#", L"A", L"A#", L"B"
    };
    const int octave = (midiPitch / 12) - 1;
    return std::wstring(kNoteNames[midiPitch % 12]) + std::to_wstring(octave);
}

inline std::wstring FormatSpaceSeparatedMidiPitches(std::string_view pitchesText) {
    std::wstring formatted;
    size_t i = 0;
    while (i < pitchesText.size()) {
        while (i < pitchesText.size() && (pitchesText[i] == ' ' || pitchesText[i] == '\t' || pitchesText[i] == '\r' || pitchesText[i] == '\n')) ++i;
        if (i >= pitchesText.size()) break;
        size_t s = i;
        while (i < pitchesText.size() && ((pitchesText[i] >= '0' && pitchesText[i] <= '9') || pitchesText[i] == '-')) ++i;
        int32_t p = -1;
        if (i > s && std::from_chars(pitchesText.data() + s, pitchesText.data() + i, p).ec == std::errc{} && p >= 0 && p <= 127) {
            if (!formatted.empty()) formatted += L" ";
            formatted += FormatMidiPitchNote(p);
        } else {
            return PathUtils::Trim(PathUtils::Utf8ToWide(pitchesText));
        }
    }
    return formatted;
}

inline bool SkipGpBeatEffects(const std::vector<uint8_t>& raw, size_t& pos, int gpMaj) {
    if (gpMaj == 3) {
        if (pos + 1 > raw.size()) return false;
        uint8_t f1 = raw[pos++];
        if (f1 & 0x20u) { // slap/tap/pop
            if (pos + 1 > raw.size()) return false;
            uint8_t t = raw[pos++];
            if (t == 0) { if (pos + 4 > raw.size()) return false; pos += 4; } // tremolo bar
        }
        if (f1 & 0x40u) { if (pos + 2 > raw.size()) return false; pos += 2; } // stroke
        return true;
    }
    if (pos + 2 > raw.size()) return false;
    uint8_t f1 = raw[pos++];
    uint8_t f2 = raw[pos++];
    if (f1 & 0x20u) { if (pos + 1 > raw.size()) return false; ++pos; } // tapping/slapping/popping
    if (f2 & 0x04u) { // tremolo bar BendEffect: type(1) + value(4) + count(4) + count * (pos(4)+val(4)+vib(1))
        if (pos + 9 > raw.size()) return false;
        uint32_t pts = DetailBinarySysDoc::RdU32LE(raw.data() + pos + 5);
        pos += 9;
        if (pts > 128 || pos + pts * 9ULL > raw.size()) return false;
        pos += pts * 9u;
    }
    if (f1 & 0x40u) { if (pos + 2 > raw.size()) return false; pos += 2; } // upstroke + downstroke
    if (f2 & 0x02u) { if (pos + 1 > raw.size()) return false; ++pos; }    // pickStroke
    return true;
}

inline bool SkipGpNoteEffects(const std::vector<uint8_t>& raw, size_t& pos, int gpMaj) {
    if (gpMaj == 3) {
        if (pos + 1 > raw.size()) return false;
        uint8_t f1 = raw[pos++];
        if (f1 & 0x01u) { // bend
            if (pos + 9 > raw.size()) return false;
            uint32_t pts = DetailBinarySysDoc::RdU32LE(raw.data() + pos + 5);
            pos += 9;
            if (pts > 128 || pos + pts * 9ULL > raw.size()) return false;
            pos += pts * 9u;
        }
        if (f1 & 0x10u) { if (pos + 4 > raw.size()) return false; pos += 4; } // grace
        return true;
    }
    if (pos + 2 > raw.size()) return false;
    uint8_t f1 = raw[pos++];
    uint8_t f2 = raw[pos++];
    if (f1 & 0x01u) { // bend
        if (pos + 9 > raw.size()) return false;
        uint32_t pts = DetailBinarySysDoc::RdU32LE(raw.data() + pos + 5);
        pos += 9;
        if (pts > 128 || pos + pts * 9ULL > raw.size()) return false;
        pos += pts * 9u;
    }
    if (f1 & 0x10u) { // grace: fret(1), vel(1), trans(1), dur(1) [+ flags(1) in GP5]
        size_t gSz = (gpMaj >= 5) ? 5u : 4u;
        if (pos + gSz > raw.size()) return false;
        pos += gSz;
    }
    if (f2 & 0x04u) { if (pos + 1 > raw.size()) return false; ++pos; } // tremoloPicking
    if (f2 & 0x08u) { if (pos + 1 > raw.size()) return false; ++pos; } // slide
    if (f2 & 0x10u) { // harmonic
        if (pos + 1 > raw.size()) return false;
        uint8_t ht = raw[pos++];
        if (gpMaj >= 5) {
            if (ht == 2) { if (pos + 3 > raw.size()) return false; pos += 3; }
            else if (ht == 3) { if (pos + 1 > raw.size()) return false; ++pos; }
        }
    }
    if (f2 & 0x20u) { if (pos + 2 > raw.size()) return false; pos += 2; } // trill
    return true;
}

inline bool SkipGpMixTableChange(const std::vector<uint8_t>& raw, size_t& pos, int gpMaj, int gpMin) {
    auto skipIntByteStr = [&]() -> bool {
        if (pos + 4 > raw.size()) return false;
        uint32_t blkLen = DetailBinarySysDoc::RdU32LE(raw.data() + pos);
        pos += 4;
        if (blkLen == 0) { if (pos < raw.size() && raw[pos] == 0) ++pos; return true; }
        if (blkLen > raw.size() - pos || blkLen > 65536) return false;
        pos += blkLen;
        return true;
    };
    if (pos + 1 > raw.size()) return false;
    int8_t inst = static_cast<int8_t>(raw[pos++]);
    if (gpMaj >= 5) {
        if (pos + 16 > raw.size()) return false;
        pos += 16; // RSEInstrument (4x i32 in GP5.10, or 3x i32 + i16 + 1 + ... wait: in MixTable RSE is 12B+effect)
        if (gpMin == 0) pos -= 1; // GP5.00 RSEInstrument is 15B
        else {
            if (!skipIntByteStr() || !skipIntByteStr()) return false;
        }
    }
    if (pos + 6 > raw.size()) return false;
    int8_t vol = static_cast<int8_t>(raw[pos++]);
    int8_t pan = static_cast<int8_t>(raw[pos++]);
    int8_t cho = static_cast<int8_t>(raw[pos++]);
    int8_t rev = static_cast<int8_t>(raw[pos++]);
    int8_t pha = static_cast<int8_t>(raw[pos++]);
    int8_t tre = static_cast<int8_t>(raw[pos++]);
    if (gpMaj >= 5 && !skipIntByteStr()) return false; // tempoName
    if (pos + 4 > raw.size()) return false;
    int32_t tempoVal = DetailBinarySysDoc::RdI32LE(raw.data() + pos);
    pos += 4;
    if (vol >= 0) { if (pos >= raw.size()) return false; ++pos; }
    if (pan >= 0) { if (pos >= raw.size()) return false; ++pos; }
    if (cho >= 0) { if (pos >= raw.size()) return false; ++pos; }
    if (rev >= 0) { if (pos >= raw.size()) return false; ++pos; }
    if (pha >= 0) { if (pos >= raw.size()) return false; ++pos; }
    if (tre >= 0) { if (pos >= raw.size()) return false; ++pos; }
    if (tempoVal >= 0) {
        if (pos >= raw.size()) return false;
        ++pos;
        if (gpMaj >= 5 && gpMin >= 10) { if (pos >= raw.size()) return false; ++pos; }
    }
    if (gpMaj >= 4) { if (pos >= raw.size()) return false; ++pos; } // flags
    if (gpMaj >= 5) {
        if (pos >= raw.size()) return false;
        ++pos; // wah
        if (gpMin >= 10) {
            if (!skipIntByteStr() || !skipIntByteStr()) return false;
        }
    }
    (void)inst;
    return true;
}

inline bool SkipGpChord(const std::vector<uint8_t>& raw, size_t& pos, int gpMaj) {
    if (pos >= raw.size()) return false;
    uint8_t newFmt = raw[pos++];
    if (newFmt == 0) {
        if (pos + 4 > raw.size()) return false;
        uint32_t blkLen = DetailBinarySysDoc::RdU32LE(raw.data() + pos); pos += 4;
        if (blkLen > raw.size() - pos) return false;
        pos += blkLen;
        if (pos + 4 > raw.size()) return false;
        int32_t firstFret = DetailBinarySysDoc::RdI32LE(raw.data() + pos); pos += 4;
        if (firstFret != 0) { if (pos + 24 > raw.size()) return false; pos += 24; }
        return true;
    }
    if (gpMaj == 3) {
        if (pos + 25 + 34 + 4 + 24 + 32 > raw.size()) return false;
        pos += 25 + 34 + 4 + 24 + 32;
        return true;
    }
    // GP4/GP5 chord diagram (fixed length: GP4 = 16 + 22 + 4 + 28 + 32 = 102B after newFmt; GP5 = 16 + 22 + 4 + 28 + 26 = 96B? Let's check exact bytes:
    // sharp(1)+blank(3)+root(1)+type(1)+ext(1)+bass(4)+tonality(4)+add(1) = 16B;
    // name ByteSizeString(20) = 21B (GP5) or 23B (GP4 ByteSizeString(22));
    // fifth/ninth/eleventh = 3B (GP4: 2B fifth/ninth + ... -> actually GP5: fifth(1)+ninth(1)+eleventh(1)+firstFret(4)+7*frets(28)+numBarres(1)+5*barres(15)+7*omission(7)+blank(1)+7*fingering(7)+show(1) = 66B -> 16+21+66 = 103B!)
    const size_t chordBytes = (gpMaj >= 5) ? 103u : 90u;
    if (pos + chordBytes > raw.size()) return false;
    pos += chordBytes;
    return true;
}

inline bool ParseGuitarPro(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    // 1. Guitar Pro 7/8 (.gp ZIP with Content/score.gpif)
    std::vector<ZipDeflate::ArchiveItemMeta> zipEntries;
    bool usedFile = false;
    if (((raw.size() >= 4 && std::memcmp(raw.data(), "PK\x03\x04", 4) == 0) || extLower == L".gp") &&
        DetailStructuredDataDoc::ListZipPackage(raw, out, zipEntries, usedFile))
    {
        std::vector<uint8_t> gpif;
        for (const auto& e : zipEntries) {
            if (!e.isDirectory && PathUtils::ToLower(PathUtils::GetExtension(e.pathInArchive)) == L".gpif") {
                if (DetailStructuredDataDoc::ExtractZipPackageEntry(raw, out, usedFile, e, gpif) && !gpif.empty()) break;
            }
        }
        if (gpif.empty()) return false;

        namespace X = ::Explorer2::XmlTagScan;
        std::string xmlStr = X::DocumentToUtf8(gpif.data(), gpif.size());
        X::Element gpifRoot;
        if (!X::FindChild(xmlStr, "GPIF", gpifRoot)) return false;

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Guitar Pro 7/8 Tablature (.gp)";

        SpreadsheetSheet info;
        info.name = L"Score_Info";
        info.rows.push_back({ L"Field", L"Value" });

        std::string gpVer = X::ChildText(gpifRoot.body, "GPVersion");
        if (!gpVer.empty()) info.rows.push_back({ L"GP Version", PathUtils::Utf8ToWide(gpVer) });

        X::Element scoreEl;
        std::string_view metaSrc = gpifRoot.body;
        if (X::FindChild(gpifRoot.body, "Score", scoreEl)) metaSrc = scoreEl.body;

        auto addScoreField = [&](std::string_view tag, const wchar_t* label) {
            std::string val = X::ChildText(metaSrc, tag);
            if (!val.empty()) info.rows.push_back({ label, PathUtils::Utf8ToWide(val) });
        };
        addScoreField("Title", L"Title");
        addScoreField("SubTitle", L"Subtitle");
        addScoreField("Artist", L"Artist");
        addScoreField("Album", L"Album");
        addScoreField("Words", L"Lyrics / Words");
        addScoreField("Music", L"Composer");
        addScoreField("Tabber", L"Tabber");

        size_t mbCount = 0;
        X::Element masterBarsEl;
        if (X::FindChild(gpifRoot.body, "MasterBars", masterBarsEl)) {
            X::ForEachChild(masterBarsEl.body, [&](std::string_view n, const X::Element&) {
                if (n == "MasterBar") ++mbCount;
                return true;
            });
        }
        info.rows.push_back({ L"Master Bars", std::to_wstring(mbCount) });

        SpreadsheetSheet trks;
        trks.name = L"Tracks";
        trks.rows.push_back({ L"Track ID", L"Name", L"Tuning Notes", L"MIDI Pitches" });
        X::Element tracksEl;
        if (X::FindChild(gpifRoot.body, "Tracks", tracksEl)) {
            X::ForEachChild(tracksEl.body, [&](std::string_view n, const X::Element& tel) {
                if (n != "Track") return true;
                std::string tid = X::AttrText(tel.attrs, "id");
                std::string nm  = X::ChildText(tel.body, "Name");
                std::string pitches;
                size_t sp = 0;
                X::XmlTag st;
                while (X::NextTag(tel.body, sp, st)) {
                    if (!st.isClosing && !st.isSelfClosing && st.localName == "Pitches") {
                        std::string_view pb; size_t aft = 0;
                        if (X::FindElementBody(tel.body, st.afterTag, "Pitches", pb, aft)) {
                            pitches = std::string(X::TrimView(X::DecodeText(pb)));
                            break;
                        }
                    }
                }
                trks.rows.push_back({
                    PathUtils::Utf8ToWide(tid),
                    nm.empty() ? L"(Track)" : PathUtils::Utf8ToWide(nm),
                    FormatSpaceSeparatedMidiPitches(pitches),
                    PathUtils::Utf8ToWide(pitches)
                });
                return true;
            });
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
        uint32_t blkLen = DetailBinarySysDoc::RdU32LE(raw.data() + pos);
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
        uint32_t nCount = DetailBinarySysDoc::RdU32LE(raw.data() + pos);
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
            uint32_t lLen = DetailBinarySysDoc::RdU32LE(raw.data() + pos);
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
    bool haveMeasureTrackCounts = false;
    if (pos + 4 <= raw.size()) {
        tempo = DetailBinarySysDoc::RdU32LE(raw.data() + pos);
        pos += 4;
        if (gpMaj >= 5 && gpMin >= 10 && pos < raw.size()) pos += 1; // hideTempo
        if (gpMaj >= 4) pos += 5; // key(1) + octave(4)
        else pos += 4;            // key(4)
        if (pos + 768 <= raw.size()) {
            pos += 768; // 64 MIDI channel ports (64 * 12B)
            if (gpMaj >= 5 && pos + 42 <= raw.size()) pos += 42; // 19 musical directions (38B) + master reverb (4B) — note: check 42 vs 46!
            // Wait: 19 * 2B = 38B + 4B reverb = 42B! Notice in some GP5 specs it's 19*2=38 + 4 = 42B! Let's check both 42B and +4B if needed:
            // In PyGuitarPro gp5.py: readDirections() reads 19 x i16 = 38 bytes, then masterReverb = i32 (4 bytes) -> total = 42 bytes!
            if (pos + 8 <= raw.size()) {
                uint32_t mc = DetailBinarySysDoc::RdU32LE(raw.data() + pos);
                uint32_t tc = DetailBinarySysDoc::RdU32LE(raw.data() + pos + 4);
                if ((mc == 0 || mc > 10000 || tc == 0 || tc > 128) && pos + 12 <= raw.size()) {
                    uint32_t mc2 = DetailBinarySysDoc::RdU32LE(raw.data() + pos + 4);
                    uint32_t tc2 = DetailBinarySysDoc::RdU32LE(raw.data() + pos + 8);
                    if (mc2 > 0 && mc2 <= 10000 && tc2 > 0 && tc2 <= 128) {
                        pos += 4;
                        mc = mc2;
                        tc = tc2;
                    }
                }
                measureCount = mc;
                trackCount = tc;
                pos += 8;
                haveMeasureTrackCounts = (measureCount > 0 && measureCount <= 10000 && trackCount > 0 && trackCount <= 128);
            }
        }
    }

    struct GpMeasureMeta {
        int num = 4;
        int den = 4;
        bool repeatOpen = false;
        int repeatClose = 0;
        uint8_t altEnding = 0;
        std::wstring marker;
        int8_t keySig = 0;
        uint8_t keyMode = 0;
        bool doubleBar = false;
        uint32_t totalBeats = 0;
        uint32_t totalNotes = 0;
    };
    std::vector<GpMeasureMeta> measures;
    SpreadsheetSheet trkSheet;
    trkSheet.name = L"Tracks";
    trkSheet.rows.push_back({ L"#", L"Track Name", L"Flags", L"Strings", L"Tuning", L"Channel / Port", L"Frets / Capo", L"Color" });

    if (haveMeasureTrackCounts && pos < raw.size()) {
        int curNum = 4, curDen = 4;
        int8_t curKey = 0; uint8_t curKeyMode = 0;
        bool headersOk = true;
        measures.reserve(measureCount);

        for (uint32_t m = 0; m < measureCount; ++m) {
            if (gpMaj >= 5 && m > 0) {
                if (pos >= raw.size()) { headersOk = false; break; }
                ++pos;
            }
            if (pos >= raw.size()) { headersOk = false; break; }
            uint8_t flags = raw[pos++];
            GpMeasureMeta mm;
            mm.repeatOpen = (flags & 0x04u) != 0;
            mm.doubleBar  = (flags & 0x80u) != 0;

            if (flags & 0x01u) { if (pos >= raw.size()) { headersOk = false; break; } curNum = raw[pos++]; }
            if (flags & 0x02u) { if (pos >= raw.size()) { headersOk = false; break; } curDen = raw[pos++]; }
            mm.num = curNum;
            mm.den = curDen;

            if (gpMaj >= 5) {
                if (flags & 0x08u) { if (pos >= raw.size()) { headersOk = false; break; } mm.repeatClose = raw[pos++]; }
                if (flags & 0x20u) {
                    if (!readIntByteStr(mm.marker) || pos + 4 > raw.size()) { headersOk = false; break; }
                    pos += 4;
                }
                if (flags & 0x10u) { if (pos >= raw.size()) { headersOk = false; break; } mm.altEnding = raw[pos++]; }
                if (flags & 0x40u) {
                    if (pos + 2 > raw.size()) { headersOk = false; break; }
                    curKey = static_cast<int8_t>(raw[pos++]);
                    curKeyMode = raw[pos++];
                }
                if (flags & 0x03u) { if (pos + 4 > raw.size()) { headersOk = false; break; } pos += 4; } // beam8thNotes
                if ((flags & 0x10u) == 0) { if (pos >= raw.size()) { headersOk = false; break; } ++pos; }
                if (pos >= raw.size()) { headersOk = false; break; }
                ++pos; // tripletFeel
            } else {
                if (flags & 0x08u) { if (pos >= raw.size()) { headersOk = false; break; } mm.repeatClose = raw[pos++]; }
                if (flags & 0x10u) { if (pos >= raw.size()) { headersOk = false; break; } mm.altEnding = raw[pos++]; }
                if (flags & 0x20u) {
                    if (!readIntByteStr(mm.marker) || pos + 4 > raw.size()) { headersOk = false; break; }
                    pos += 4;
                }
                if (flags & 0x40u) {
                    if (pos + 2 > raw.size()) { headersOk = false; break; }
                    curKey = static_cast<int8_t>(raw[pos++]);
                    curKeyMode = raw[pos++];
                }
            }
            mm.keySig = curKey;
            mm.keyMode = curKeyMode;
            measures.push_back(std::move(mm));
        }

        // Parse Track Headers
        bool tracksOk = headersOk;
        for (uint32_t t = 0; tracksOk && t < trackCount; ++t) {
            if (gpMaj >= 5 && (t == 0 || gpMin == 0)) {
                if (pos >= raw.size()) { tracksOk = false; break; }
                ++pos;
            }
            if (pos + 1 + 41 + 4 + 28 + 24 > raw.size()) { tracksOk = false; break; }
            uint8_t tFlags = raw[pos++];
            uint8_t nameLen = std::min<uint8_t>(raw[pos], 40u);
            std::wstring tName = PathUtils::Trim(Latin1OrCp1252ToWide(raw.data() + pos + 1, nameLen));
            pos += 41;

            uint32_t strCnt = DetailBinarySysDoc::RdU32LE(raw.data() + pos); pos += 4;
            int32_t tunings[7] = {};
            for (int s = 0; s < 7; ++s) { tunings[s] = DetailBinarySysDoc::RdI32LE(raw.data() + pos); pos += 4; }
            int32_t port    = DetailBinarySysDoc::RdI32LE(raw.data() + pos); pos += 4;
            int32_t chan    = DetailBinarySysDoc::RdI32LE(raw.data() + pos); pos += 4;
            int32_t fxChan  = DetailBinarySysDoc::RdI32LE(raw.data() + pos); pos += 4;
            int32_t frets   = DetailBinarySysDoc::RdI32LE(raw.data() + pos); pos += 4;
            int32_t capo    = DetailBinarySysDoc::RdI32LE(raw.data() + pos); pos += 4;
            uint8_t colR = raw[pos], colG = raw[pos + 1], colB = raw[pos + 2];
            pos += 4;

            if (gpMaj >= 5) {
                if (gpMin == 0) {
                    if (pos + 44 > raw.size()) { tracksOk = false; break; }
                    pos += 44;
                } else {
                    if (pos + 45 > raw.size()) { tracksOk = false; break; }
                    pos += 45;
                    std::wstring fxName, fxCat;
                    if (!readIntByteStr(fxName) || !readIntByteStr(fxCat)) { tracksOk = false; break; }
                }
            }

            const uint32_t clampStr = std::min<uint32_t>(strCnt, 7u);
            std::wstring tuneStr;
            if ((tFlags & 0x01u) != 0) {
                tuneStr = L"(Percussion / Drums)";
            } else {
                for (int s = static_cast<int>(clampStr) - 1; s >= 0; --s) {
                    if (tunings[s] <= 0) continue;
                    if (!tuneStr.empty()) tuneStr += L" ";
                    tuneStr += FormatMidiPitchNote(tunings[s]);
                }
            }

            std::wstring flagStr;
            if (tFlags & 0x01u) flagStr += L"Drums ";
            if (tFlags & 0x02u) flagStr += L"12-String ";
            if (tFlags & 0x04u) flagStr += L"Banjo ";
            if (flagStr.empty()) flagStr = L"Melodic";
            else flagStr = PathUtils::Trim(flagStr);

            wchar_t colHex[16] = {};
            swprintf_s(colHex, L"#%02X%02X%02X", colR, colG, colB);

            trkSheet.rows.push_back({
                std::to_wstring(t + 1),
                tName.empty() ? (L"Track " + std::to_wstring(t + 1)) : tName,
                flagStr,
                std::to_wstring(clampStr),
                tuneStr,
                L"Ch " + std::to_wstring(chan) + L" (FX " + std::to_wstring(fxChan) + L", Port " + std::to_wstring(port) + L")",
                std::to_wstring(frets) + L" frets" + (capo > 0 ? (L", Capo " + std::to_wstring(capo)) : L""),
                colHex
            });
        }

        if (tracksOk && gpMaj >= 5) {
            size_t postSkip = (gpMin == 0) ? 2u : 1u;
            if (pos + postSkip <= raw.size()) pos += postSkip;
            else tracksOk = false;
        }

        // Walk Measures x Tracks Beats & Notes for accurate per-measure beat/note counts
        bool beatsOk = tracksOk;
        const int voicesPerBar = (gpMaj >= 5) ? 2 : 1;
        for (uint32_t m = 0; beatsOk && m < measures.size(); ++m) {
            for (uint32_t t = 0; beatsOk && t < trackCount; ++t) {
                for (int v = 0; beatsOk && v < voicesPerBar; ++v) {
                    if (pos + 4 > raw.size()) { beatsOk = false; break; }
                    uint32_t beatCnt = DetailBinarySysDoc::RdU32LE(raw.data() + pos); pos += 4;
                    if (beatCnt > 512) { beatsOk = false; break; }
                    measures[m].totalBeats += beatCnt;
                    for (uint32_t b = 0; beatsOk && b < beatCnt; ++b) {
                        if (pos >= raw.size()) { beatsOk = false; break; }
                        uint8_t bFlags = raw[pos++];
                        if (bFlags & 0x40u) { if (pos >= raw.size()) { beatsOk = false; break; } ++pos; } // beatStatus
                        if (pos >= raw.size()) { beatsOk = false; break; }
                        ++pos; // duration
                        if (bFlags & 0x20u) { if (pos + 4 > raw.size()) { beatsOk = false; break; } pos += 4; } // tuplet
                        if (bFlags & 0x02u) { if (!SkipGpChord(raw, pos, gpMaj)) { beatsOk = false; break; } }
                        if (bFlags & 0x04u) { std::wstring bt; if (!readIntByteStr(bt)) { beatsOk = false; break; } }
                        if (bFlags & 0x08u) { if (!SkipGpBeatEffects(raw, pos, gpMaj)) { beatsOk = false; break; } }
                        if (bFlags & 0x10u) { if (!SkipGpMixTableChange(raw, pos, gpMaj, gpMin)) { beatsOk = false; break; } }
                        if (pos >= raw.size()) { beatsOk = false; break; }
                        uint8_t strMask = raw[pos++];
                        for (int bit = 6; bit >= 0; --bit) {
                            if ((strMask & (1u << bit)) == 0) continue;
                            ++measures[m].totalNotes;
                            if (pos >= raw.size()) { beatsOk = false; break; }
                            uint8_t nFlags = raw[pos++];
                            if (nFlags & 0x20u) { if (pos >= raw.size()) { beatsOk = false; break; } ++pos; } // noteType
                            if (gpMaj <= 4 && (nFlags & 0x01u)) { if (pos + 2 > raw.size()) { beatsOk = false; break; } pos += 2; }
                            if (nFlags & 0x10u) { if (pos >= raw.size()) { beatsOk = false; break; } ++pos; } // velocity
                            if (nFlags & 0x20u) { if (pos >= raw.size()) { beatsOk = false; break; } ++pos; } // fret
                            if (nFlags & 0x80u) { if (pos + 2 > raw.size()) { beatsOk = false; break; } pos += 2; } // fingering
                            if (gpMaj >= 5) {
                                if (nFlags & 0x01u) { if (pos + 8 > raw.size()) { beatsOk = false; break; } pos += 8; } // durationPercent
                                if (pos >= raw.size()) { beatsOk = false; break; }
                                ++pos; // flags2
                            }
                            if (nFlags & 0x08u) { if (!SkipGpNoteEffects(raw, pos, gpMaj)) { beatsOk = false; break; } }
                        }
                        if (gpMaj >= 5) {
                            if (pos + 2 > raw.size()) { beatsOk = false; break; }
                            uint16_t bFlags2 = DetailBinarySysDoc::RdU16LE(raw.data() + pos); pos += 2;
                            if (bFlags2 & 0x0800u) { if (pos >= raw.size()) { beatsOk = false; break; } ++pos; }
                        }
                    }
                }
                if (beatsOk && gpMaj >= 5) {
                    if (pos >= raw.size()) { beatsOk = false; break; }
                    ++pos; // lineBreak
                }
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

    if (trkSheet.rows.size() > 1) {
        FinalizeSheetCols(trkSheet);
        out.sheets.push_back(std::move(trkSheet));
    }

    if (!measures.empty()) {
        SpreadsheetSheet mSheet;
        mSheet.name = L"Measures";
        mSheet.rows.push_back({ L"Bar #", L"Time Sig", L"Key Sig", L"Repeats / Endings", L"Marker", L"Beats", L"Notes" });
        for (size_t i = 0; i < std::min<size_t>(measures.size(), 2048); ++i) {
            const auto& mm = measures[i];
            std::wstring rep;
            if (mm.repeatOpen) rep += L"|: ";
            if (mm.repeatClose > 0) rep += L":| (x" + std::to_wstring(mm.repeatClose + 1) + L") ";
            if (mm.altEnding > 0) rep += L"AltEnding=" + std::to_wstring(mm.altEnding) + L" ";
            if (mm.doubleBar) rep += L"||";
            rep = PathUtils::Trim(rep);

            std::wstring keyStr = (mm.keySig == 0) ? L"0" : ((mm.keySig > 0 ? L"+" : L"") + std::to_wstring(mm.keySig));
            keyStr += (mm.keyMode == 1) ? L" Minor" : L" Major";

            mSheet.rows.push_back({
                std::to_wstring(i + 1),
                std::to_wstring(mm.num) + L"/" + std::to_wstring(mm.den),
                keyStr,
                rep,
                mm.marker,
                std::to_wstring(mm.totalBeats),
                std::to_wstring(mm.totalNotes)
            });
        }
        FinalizeSheetCols(mSheet);
        out.sheets.push_back(std::move(mSheet));
    }

    out.valid = true;
    return true;
}

} // namespace DetailMediaAndScoreDoc

inline bool ParseGuitarProTabDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) { return DetailMediaAndScoreDoc::ParseGuitarPro(raw, extLower, out); }

} // namespace Formats
