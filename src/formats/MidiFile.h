#pragma once
// MidiFile.h — Standard MIDI File (.mid / .midi) MThd/MTrk State Machine, Tempo Map & Note-Pair Inspector

namespace Formats {

namespace MidiDetail {

inline const wchar_t* GmProgramName(uint8_t prog) {
    static const wchar_t* const kGmNames[128] = {
        L"Acoustic Grand Piano", L"Bright Acoustic Piano", L"Electric Grand Piano", L"Honky-tonk Piano",
        L"Electric Piano 1", L"Electric Piano 2", L"Harpsichord", L"Clavinet",
        L"Celesta", L"Glockenspiel", L"Music Box", L"Vibraphone",
        L"Marimba", L"Xylophone", L"Tubular Bells", L"Dulcimer",
        L"Drawbar Organ", L"Percussive Organ", L"Rock Organ", L"Church Organ",
        L"Reed Organ", L"Accordion", L"Harmonica", L"Tango Accordion",
        L"Acoustic Guitar (nylon)", L"Acoustic Guitar (steel)", L"Electric Guitar (jazz)", L"Electric Guitar (clean)",
        L"Electric Guitar (muted)", L"Overdriven Guitar", L"Distortion Guitar", L"Guitar Harmonics",
        L"Acoustic Bass", L"Electric Bass (finger)", L"Electric Bass (pick)", L"Fretless Bass",
        L"Slap Bass 1", L"Slap Bass 2", L"Synth Bass 1", L"Synth Bass 2",
        L"Violin", L"Viola", L"Cello", L"Contrabass",
        L"Tremolo Strings", L"Pizzicato Strings", L"Orchestral Harp", L"Timpani",
        L"String Ensemble 1", L"String Ensemble 2", L"SynthStrings 1", L"SynthStrings 2",
        L"Choir Aahs", L"Voice Oohs", L"Synth Voice", L"Orchestra Hit",
        L"Trumpet", L"Trombone", L"Tuba", L"Muted Trumpet",
        L"French Horn", L"Brass Section", L"SynthBrass 1", L"SynthBrass 2",
        L"Soprano Sax", L"Alto Sax", L"Tenor Sax", L"Baritone Sax",
        L"Oboe", L"English Horn", L"Bassoon", L"Clarinet",
        L"Piccolo", L"Flute", L"Recorder", L"Pan Flute",
        L"Blown Bottle", L"Shakuhachi", L"Whistle", L"Ocarina",
        L"Lead 1 (square)", L"Lead 2 (sawtooth)", L"Lead 3 (calliope)", L"Lead 4 (chiff)",
        L"Lead 5 (charang)", L"Lead 6 (voice)", L"Lead 7 (fifths)", L"Lead 8 (bass + lead)",
        L"Pad 1 (new age)", L"Pad 2 (warm)", L"Pad 3 (polysynth)", L"Pad 4 (choir)",
        L"Pad 5 (bowed)", L"Pad 6 (metallic)", L"Pad 7 (halo)", L"Pad 8 (sweep)",
        L"FX 1 (rain)", L"FX 2 (soundtrack)", L"FX 3 (crystal)", L"FX 4 (atmosphere)",
        L"FX 5 (brightness)", L"FX 6 (goblins)", L"FX 7 (echoes)", L"FX 8 (sci-fi)",
        L"Sitar", L"Banjo", L"Shamisen", L"Koto",
        L"Kalimba", L"Bag pipe", L"Fiddle", L"Shanai",
        L"Tinkle Bell", L"Agogo", L"Steel Drums", L"Woodblock",
        L"Taiko Drum", L"Melodic Tom", L"Synth Drum", L"Reverse Cymbal",
        L"Guitar Fret Noise", L"Breath Noise", L"Seashore", L"Bird Tweet",
        L"Telephone Ring", L"Helicopter", L"Applause", L"Gunshot"
    };
    return kGmNames[prog & 0x7Fu];
}

inline std::wstring PitchToNoteName(uint8_t note) {
    static const wchar_t* const kNames[12] = {
        L"C", L"C#", L"D", L"D#", L"E", L"F", L"F#", L"G", L"G#", L"A", L"A#", L"B"
    };
    const int octave = (static_cast<int>(note) / 12) - 1;
    return std::wstring(kNames[note % 12]) + std::to_wstring(octave) + L" (" + std::to_wstring(note) + L")";
}

inline std::wstring KeySignatureName(int8_t sf, uint8_t mi) {
    static const wchar_t* const kMajor[15] = {
        L"Cb Major", L"Gb Major", L"Db Major", L"Ab Major", L"Eb Major", L"Bb Major", L"F Major",
        L"C Major",
        L"G Major", L"D Major", L"A Major", L"E Major", L"B Major", L"F# Major", L"C# Major"
    };
    static const wchar_t* const kMinor[15] = {
        L"Ab Minor", L"Eb Minor", L"Bb Minor", L"F Minor", L"C Minor", L"G Minor", L"D Minor",
        L"A Minor",
        L"E Minor", L"B Minor", L"F# Minor", L"C# Minor", L"G# Minor", L"D# Minor", L"A# Minor"
    };
    const int idx = static_cast<int>(sf) + 7;
    if (idx < 0 || idx >= 15) return L"Custom Key";
    return (mi == 0) ? kMajor[idx] : kMinor[idx];
}

inline bool ReadMidiVlq(const uint8_t* data, size_t end, size_t& pos, uint32_t& outVal) {
    outVal = 0;
    for (int i = 0; i < 4; ++i) {
        if (pos >= end) return false;
        const uint8_t b = data[pos++];
        outVal = (outVal << 7) | static_cast<uint32_t>(b & 0x7Fu);
        if ((b & 0x80u) == 0) return true;
    }
    return false; // > 4-byte VLQ is invalid per SMF 1.0 specification
}

struct TempoPoint {
    uint64_t tick = 0;
    uint32_t usPerQuarter = 500000; // default 120 BPM
    double   timeSecAtTick = 0.0;
};

struct RawMidiNote {
    int      trackIdx = 0;
    uint64_t startTick = 0;
    uint64_t endTick = 0;
    uint8_t  channel = 0;
    uint8_t  pitch = 0;
    uint8_t  velocity = 0;
    uint8_t  program = 0;
};

struct TrackStats {
    int          trackIdx = 0;
    size_t       chunkOffset = 0;
    uint32_t     chunkSize = 0;
    std::wstring trackName;
    std::wstring instrumentSummary;
    std::wstring timeSig;
    std::wstring keySig;
    std::wstring firstTempoBpm;
    uint64_t     endTick = 0;
    uint32_t     noteCount = 0;
    uint32_t     metaCount = 0;
    uint32_t     sysexCount = 0;
};

} // namespace MidiDetail

inline bool ParseMidiFileDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 14 || std::memcmp(raw.data(), "MThd", 4) != 0) return false;

    auto rdU16BE = [&](size_t off) -> uint16_t {
        return static_cast<uint16_t>((static_cast<uint16_t>(raw[off]) << 8) | raw[off + 1]);
    };
    auto rdU32BE = [&](size_t off) -> uint32_t {
        return (static_cast<uint32_t>(raw[off]) << 24) |
               (static_cast<uint32_t>(raw[off + 1]) << 16) |
               (static_cast<uint32_t>(raw[off + 2]) << 8) |
               static_cast<uint32_t>(raw[off + 3]);
    };

    const uint32_t hdrLen = rdU32BE(4);
    if (hdrLen < 6 || 8ULL + hdrLen > raw.size()) return false;

    const uint16_t fmt = rdU16BE(8);
    const uint16_t ntrks = rdU16BE(10);
    const uint16_t division = rdU16BE(12);
    if (fmt > 2 || ntrks == 0 || division == 0) return false;

    const bool isSmpte = (division & 0x8000u) != 0;
    double smpteSecPerTick = 0.0;
    uint16_t ticksPerQuarter = 480;
    std::wstring divisionDesc;

    if (!isSmpte) {
        ticksPerQuarter = division;
        divisionDesc = std::to_wstring(ticksPerQuarter) + L" ticks/quarter-note";
    } else {
        const int fpsNeg = -static_cast<int>(static_cast<int8_t>((division >> 8) & 0xFFu));
        const uint32_t ticksPerFrame = division & 0xFFu;
        if (fpsNeg <= 0 || ticksPerFrame == 0) return false;
        const double fps = (fpsNeg == 29) ? 29.97 : static_cast<double>(fpsNeg);
        smpteSecPerTick = 1.0 / (fps * static_cast<double>(ticksPerFrame));
        wchar_t dbuf[96] = {};
        swprintf_s(dbuf, L"SMPTE %.2g fps × %u ticks/frame", fps, ticksPerFrame);
        divisionDesc = dbuf;
    }

    std::vector<MidiDetail::TempoPoint> tempoChanges;
    std::vector<MidiDetail::RawMidiNote> allNotes;
    std::vector<MidiDetail::TrackStats> tracks;

    struct MetaRow {
        int          trackIdx = 0;
        uint64_t     tick = 0;
        std::wstring typeLabel;
        std::wstring detail;
    };
    std::vector<MetaRow> metaEvents;

    size_t pos = 8 + static_cast<size_t>(hdrLen);
    int trkIdx = 1;
    while (pos + 8 <= raw.size() && trkIdx <= 512) {
        const char* cid = reinterpret_cast<const char*>(raw.data() + pos);
        const uint32_t chunkLen = rdU32BE(pos + 4);
        const size_t dataStart = pos + 8;
        if (dataStart + static_cast<size_t>(chunkLen) > raw.size()) return false;
        const size_t dataEnd = dataStart + static_cast<size_t>(chunkLen);

        if (std::memcmp(cid, "MTrk", 4) != 0) {
            // Allow standard printable non-MTrk chunks, reject binary garbage
            for (int c = 0; c < 4; ++c) {
                if (static_cast<unsigned char>(cid[c]) < 0x20u || static_cast<unsigned char>(cid[c]) > 0x7Eu) {
                    return false;
                }
            }
            pos = dataEnd;
            continue;
        }

        MidiDetail::TrackStats ts;
        ts.trackIdx = trkIdx;
        ts.chunkOffset = pos;
        ts.chunkSize = chunkLen;

        uint8_t runningStatus = 0;
        uint8_t chProgram[16] = {};
        std::set<std::wstring> usedInstruments;
        // Per-(channel, pitch) FIFO of open note indices in allNotes
        std::vector<size_t> openNotes[16][128];

        uint64_t absTick = 0;
        size_t cur = dataStart;
        while (cur < dataEnd) {
            uint32_t delta = 0;
            if (!MidiDetail::ReadMidiVlq(raw.data(), dataEnd, cur, delta)) return false;
            absTick += delta;

            if (cur >= dataEnd) return false;
            const uint8_t firstByte = raw[cur++];

            if (firstByte == 0xFFu) {
                // Meta event
                if (cur >= dataEnd) return false;
                const uint8_t metaType = raw[cur++];
                uint32_t metaLen = 0;
                if (!MidiDetail::ReadMidiVlq(raw.data(), dataEnd, cur, metaLen)) return false;
                if (cur + static_cast<size_t>(metaLen) > dataEnd) return false;
                const uint8_t* mp = raw.data() + cur;
                cur += static_cast<size_t>(metaLen);
                ++ts.metaCount;

                if (metaType == 0x03u && metaLen > 0) {
                    std::wstring nm = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(mp), metaLen));
                    if (ts.trackName.empty()) ts.trackName = nm;
                    if (metaEvents.size() < 512) metaEvents.push_back({ trkIdx, absTick, L"Track Name", nm });
                } else if (metaType == 0x04u && metaLen > 0) {
                    std::wstring inm = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(mp), metaLen));
                    usedInstruments.insert(inm);
                } else if ((metaType == 0x01u || metaType == 0x02u || metaType == 0x05u) && metaLen > 0) {
                    std::wstring txt = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(mp), std::min<uint32_t>(metaLen, 120)));
                    const wchar_t* lbl = (metaType == 0x05u) ? L"Lyric" : (metaType == 0x02u ? L"Copyright" : L"Text");
                    if (metaEvents.size() < 512) metaEvents.push_back({ trkIdx, absTick, lbl, txt });
                } else if (metaType == 0x51u && metaLen == 3) {
                    const uint32_t usPerQn = (static_cast<uint32_t>(mp[0]) << 16) |
                                             (static_cast<uint32_t>(mp[1]) << 8) |
                                             static_cast<uint32_t>(mp[2]);
                    if (usPerQn > 0) {
                        tempoChanges.push_back({ absTick, usPerQn, 0.0 });
                        const double bpm = 60000000.0 / static_cast<double>(usPerQn);
                        wchar_t bpmBuf[64] = {};
                        swprintf_s(bpmBuf, L"%.2f BPM (%u us/qn)", bpm, usPerQn);
                        if (ts.firstTempoBpm.empty()) ts.firstTempoBpm = bpmBuf;
                        if (metaEvents.size() < 512) metaEvents.push_back({ trkIdx, absTick, L"Set Tempo", bpmBuf });
                    }
                } else if (metaType == 0x58u && metaLen >= 2 && mp[1] <= 10) {
                    const uint32_t num = mp[0];
                    const uint32_t den = 1u << mp[1];
                    std::wstring sig = std::to_wstring(num) + L"/" + std::to_wstring(den);
                    if (ts.timeSig.empty()) ts.timeSig = sig;
                    if (metaEvents.size() < 512) metaEvents.push_back({ trkIdx, absTick, L"Time Signature", sig });
                } else if (metaType == 0x59u && metaLen >= 2) {
                    std::wstring ksig = MidiDetail::KeySignatureName(static_cast<int8_t>(mp[0]), mp[1]);
                    if (ts.keySig.empty()) ts.keySig = ksig;
                    if (metaEvents.size() < 512) metaEvents.push_back({ trkIdx, absTick, L"Key Signature", ksig });
                } else if (metaType == 0x2Fu) {
                    break; // End of Track
                }
            } else if (firstByte == 0xF0u || firstByte == 0xF7u) {
                // SysEx event clears Running Status per MIDI 1.0 specification
                runningStatus = 0;
                uint32_t sysexLen = 0;
                if (!MidiDetail::ReadMidiVlq(raw.data(), dataEnd, cur, sysexLen)) return false;
                if (cur + static_cast<size_t>(sysexLen) > dataEnd) return false;
                cur += static_cast<size_t>(sysexLen);
                ++ts.sysexCount;
            } else if (firstByte >= 0xF1u) {
                return false; // Undefined system common / real-time byte inside SMF stream
            } else {
                uint8_t status = 0;
                uint8_t d1 = 0;
                if ((firstByte & 0x80u) != 0) {
                    status = firstByte;
                    runningStatus = status;
                    if (cur >= dataEnd) return false;
                    d1 = raw[cur++];
                    if ((d1 & 0x80u) != 0) return false;
                } else {
                    if (runningStatus == 0) return false; // Data byte without active Running Status
                    status = runningStatus;
                    d1 = firstByte;
                }

                const uint8_t hi = status & 0xF0u;
                const uint8_t ch = status & 0x0Fu;
                uint8_t d2 = 0;
                if (hi != 0xC0u && hi != 0xD0u) {
                    if (cur >= dataEnd) return false;
                    d2 = raw[cur++];
                    if ((d2 & 0x80u) != 0) return false;
                }

                if (hi == 0xC0u) {
                    chProgram[ch] = d1 & 0x7Fu;
                    if (ch == 9) {
                        usedInstruments.insert(L"Channel 10 Percussion");
                    } else {
                        usedInstruments.insert(MidiDetail::GmProgramName(chProgram[ch]));
                    }
                } else if (hi == 0x90u && d2 > 0) {
                    ++ts.noteCount;
                    if (allNotes.size() < 20000) {
                        const size_t noteIdx = allNotes.size();
                        allNotes.push_back({
                            trkIdx, absTick, absTick, ch, static_cast<uint8_t>(d1 & 0x7Fu),
                            static_cast<uint8_t>(d2 & 0x7Fu), chProgram[ch]
                        });
                        openNotes[ch][d1 & 0x7Fu].push_back(noteIdx);
                    }
                } else if (hi == 0x80u || (hi == 0x90u && d2 == 0)) {
                    auto& q = openNotes[ch][d1 & 0x7Fu];
                    if (!q.empty()) {
                        const size_t openIdx = q.front();
                        q.erase(q.begin());
                        allNotes[openIdx].endTick = absTick;
                    }
                }
            }
        }

        // Close any notes left open at end-of-track
        for (int ch = 0; ch < 16; ++ch) {
            for (int p = 0; p < 128; ++p) {
                for (size_t openIdx : openNotes[ch][p]) {
                    allNotes[openIdx].endTick = absTick;
                }
            }
        }

        for (const auto& ins : usedInstruments) {
            if (!ts.instrumentSummary.empty()) ts.instrumentSummary += L", ";
            ts.instrumentSummary += ins;
        }
        ts.endTick = absTick;
        tracks.push_back(std::move(ts));

        pos = dataEnd;
        ++trkIdx;
    }

    if (tracks.empty()) return false;

    // Build global sorted tempo map for exact tick -> seconds conversion
    std::stable_sort(tempoChanges.begin(), tempoChanges.end(),
        [](const MidiDetail::TempoPoint& a, const MidiDetail::TempoPoint& b) { return a.tick < b.tick; });
    if (tempoChanges.empty() || tempoChanges.front().tick > 0) {
        tempoChanges.insert(tempoChanges.begin(), MidiDetail::TempoPoint{ 0, 500000, 0.0 });
    }
    for (size_t i = 1; i < tempoChanges.size(); ++i) {
        const uint64_t dt = tempoChanges[i].tick - tempoChanges[i - 1].tick;
        const double secPerTick = (static_cast<double>(tempoChanges[i - 1].usPerQuarter) * 1e-6) /
                                  static_cast<double>(ticksPerQuarter);
        tempoChanges[i].timeSecAtTick = tempoChanges[i - 1].timeSecAtTick + static_cast<double>(dt) * secPerTick;
    }

    auto tickToSeconds = [&](uint64_t tick) -> double {
        if (isSmpte) return static_cast<double>(tick) * smpteSecPerTick;
        size_t lo = 0, hi = tempoChanges.size();
        while (lo + 1 < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            if (tempoChanges[mid].tick <= tick) lo = mid;
            else hi = mid;
        }
        const uint64_t dt = tick - tempoChanges[lo].tick;
        const double secPerTick = (static_cast<double>(tempoChanges[lo].usPerQuarter) * 1e-6) /
                                  static_cast<double>(ticksPerQuarter);
        return tempoChanges[lo].timeSecAtTick + static_cast<double>(dt) * secPerTick;
    };

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Standard MIDI Music Sequence (.mid)";

    // Sheet 1: Track Summary
    SpreadsheetSheet summary;
    summary.name = L"Track Summary";
    summary.maxCols = 8;
    summary.rows.push_back({
        L"Track", L"Offset", L"Size (B)", L"Track Name",
        L"Instruments (GM)", L"Tempo / Meter / Key", L"Note Count", L"Duration (s)"
    });
    summary.rows.push_back({
        L"MThd", L"0x000000", std::to_wstring(hdrLen),
        L"Format " + std::to_wstring(fmt) + L" (" + std::to_wstring(tracks.size()) + L"/" + std::to_wstring(ntrks) + L" tracks)",
        divisionDesc,
        tempoChanges.empty() ? L"120.00 BPM" : (std::to_wstring(static_cast<int>(std::round(60000000.0 / tempoChanges[0].usPerQuarter))) + L" BPM initial"),
        std::to_wstring(allNotes.size()),
        L"-"
    });

    for (const auto& t : tracks) {
        wchar_t offBuf[32] = {};
        swprintf_s(offBuf, L"0x%06zX", t.chunkOffset);
        wchar_t durBuf[32] = {};
        swprintf_s(durBuf, L"%.3f", tickToSeconds(t.endTick));

        std::wstring sigInfo;
        if (!t.firstTempoBpm.empty()) sigInfo += t.firstTempoBpm;
        if (!t.timeSig.empty()) { if (!sigInfo.empty()) sigInfo += L" | "; sigInfo += t.timeSig; }
        if (!t.keySig.empty())  { if (!sigInfo.empty()) sigInfo += L" | "; sigInfo += t.keySig; }

        summary.rows.push_back({
            L"MTrk #" + std::to_wstring(t.trackIdx),
            offBuf,
            std::to_wstring(t.chunkSize),
            t.trackName.empty() ? L"(Untitled)" : t.trackName,
            t.instrumentSummary.empty() ? L"-" : t.instrumentSummary,
            sigInfo.empty() ? L"-" : sigInfo,
            std::to_wstring(t.noteCount),
            durBuf
        });
    }
    out.sheets.push_back(std::move(summary));

    // Sheet 2: Events / Notes
    SpreadsheetSheet notesSheet;
    notesSheet.name = L"Events & Notes";
    notesSheet.maxCols = 8;
    notesSheet.rows.push_back({
        L"Track", L"Time (s)", L"Tick", L"Channel",
        L"Event / Pitch", L"Velocity", L"Duration (s / ticks)", L"Instrument / Detail"
    });

    for (const auto& me : metaEvents) {
        if (notesSheet.rows.size() >= 5000) break;
        wchar_t tBuf[32] = {};
        swprintf_s(tBuf, L"%.4f", tickToSeconds(me.tick));
        notesSheet.rows.push_back({
            L"#" + std::to_wstring(me.trackIdx),
            tBuf,
            std::to_wstring(me.tick),
            L"Meta",
            me.typeLabel,
            L"-",
            L"-",
            me.detail
        });
    }

    for (const auto& n : allNotes) {
        if (notesSheet.rows.size() >= 5000) break;
        const double startSec = tickToSeconds(n.startTick);
        const double endSec = tickToSeconds(std::max(n.startTick, n.endTick));
        const uint64_t durTicks = (n.endTick >= n.startTick) ? (n.endTick - n.startTick) : 0;
        wchar_t tBuf[32] = {};
        swprintf_s(tBuf, L"%.4f", startSec);
        wchar_t dBuf[64] = {};
        swprintf_s(dBuf, L"%.4f s (%llu ticks)", std::max(0.0, endSec - startSec), static_cast<unsigned long long>(durTicks));

        const std::wstring inst = (n.channel == 9)
            ? L"Percussion"
            : std::wstring(MidiDetail::GmProgramName(n.program));

        notesSheet.rows.push_back({
            L"#" + std::to_wstring(n.trackIdx),
            tBuf,
            std::to_wstring(n.startTick),
            L"Ch " + std::to_wstring(n.channel + 1),
            MidiDetail::PitchToNoteName(n.pitch),
            std::to_wstring(n.velocity),
            dBuf,
            inst
        });
    }
    out.sheets.push_back(std::move(notesSheet));

    out.valid = true;
    return true;
}

} // namespace Formats
