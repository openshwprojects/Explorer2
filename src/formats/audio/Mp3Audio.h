#pragma once
// Mp3Audio.h — MPEG Audio + ID3v1/v2 + Xing/VBRI (.mp3) Inspector

namespace Formats {
namespace DetailMedia100 {

// ============================================================================
// 2. MPEG Audio + ID3v2.2 / v2.3 / v2.4 + ID3v1 + Xing/VBRI (.mp3)
// ============================================================================
inline std::wstring DecodeId3String(uint8_t enc, const uint8_t* p, size_t len) {
    if (len == 0) return {};
    if (enc == 0) {
        return Latin1OrCp1252ToWide(p, len);
    }
    if (enc == 1) {
        bool be = false;
        size_t off = 0;
        if (len >= 2) {
            if (p[0] == 0xFE && p[1] == 0xFF) { be = true; off = 2; }
            else if (p[0] == 0xFF && p[1] == 0xFE) { be = false; off = 2; }
        }
        return Utf16BytesToWide(p + off, len - off, be);
    }
    if (enc == 2) {
        return Utf16BytesToWide(p, len, true);
    }
    // enc == 3 (UTF-8) or fallback
    size_t actual = 0;
    while (actual < len && p[actual] != 0) ++actual;
    return PathUtils::Trim(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), actual)));
}

inline size_t FindId3Terminator(uint8_t enc, const uint8_t* p, size_t len, size_t& outTermBytes) {
    if (enc == 1 || enc == 2) {
        outTermBytes = 2;
        for (size_t i = 0; i + 2 <= len; i += 2) {
            if (p[i] == 0 && p[i + 1] == 0) return i;
        }
        outTermBytes = 0;
        return len;
    }
    outTermBytes = 1;
    for (size_t i = 0; i < len; ++i) {
        if (p[i] == 0) return i;
    }
    outTermBytes = 0;
    return len;
}

inline bool ParseMp3Audio(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 4) return false;

    bool hasId3v2 = false;
    uint8_t id3Major = 0;
    uint8_t id3Minor = 0;
    uint8_t id3Flags = 0;
    uint32_t id3Size = 0;
    size_t audioStart = 0;

    if (raw.size() >= 10 && std::memcmp(raw.data(), "ID3", 3) == 0 &&
        raw[3] >= 2 && raw[3] <= 4 && raw[4] < 0xFF &&
        (raw[6] & 0x80) == 0 && (raw[7] & 0x80) == 0 && (raw[8] & 0x80) == 0 && (raw[9] & 0x80) == 0)
    {
        hasId3v2 = true;
        id3Major = raw[3];
        id3Minor = raw[4];
        id3Flags = raw[5];
        id3Size = (static_cast<uint32_t>(raw[6] & 0x7F) << 21) |
                  (static_cast<uint32_t>(raw[7] & 0x7F) << 14) |
                  (static_cast<uint32_t>(raw[8] & 0x7F) << 7)  |
                  static_cast<uint32_t>(raw[9] & 0x7F);
        audioStart = std::min<size_t>(raw.size(), 10ULL + id3Size);
    }

    struct Id3TagRow { std::wstring id; std::wstring label; std::wstring value; };
    std::vector<Id3TagRow> tagRows;

    if (hasId3v2) {
        size_t pos = 10;
        const size_t tagEnd = std::min<size_t>(raw.size(), 10ULL + id3Size);
        if ((id3Flags & 0x40) != 0 && pos + 4 <= tagEnd) {
            uint32_t extSz = (id3Major == 4)
                ? ((static_cast<uint32_t>(raw[pos] & 0x7F) << 21) |
                   (static_cast<uint32_t>(raw[pos + 1] & 0x7F) << 14) |
                   (static_cast<uint32_t>(raw[pos + 2] & 0x7F) << 7) |
                   static_cast<uint32_t>(raw[pos + 3] & 0x7F))
                : DetailSys100::RdU32BE(raw.data() + pos);
            pos += (id3Major == 4) ? extSz : (4ULL + extSz);
        }

        const size_t hdrLen = (id3Major == 2) ? 6 : 10;
        while (pos + hdrLen <= tagEnd && tagRows.size() < 256) {
            const uint8_t* fp = raw.data() + pos;
            if (fp[0] == 0) break; // Padding

            size_t idLen = (id3Major == 2) ? 3 : 4;
            bool validId = true;
            for (size_t k = 0; k < idLen; ++k) {
                if (!((fp[k] >= 'A' && fp[k] <= 'Z') || (fp[k] >= '0' && fp[k] <= '9'))) { validId = false; break; }
            }
            if (!validId) break;

            std::string fid(reinterpret_cast<const char*>(fp), idLen);
            uint32_t fSize = 0;
            uint16_t fFlags = 0;
            if (id3Major == 2) {
                fSize = (static_cast<uint32_t>(fp[3]) << 16) | (static_cast<uint32_t>(fp[4]) << 8) | fp[5];
            } else if (id3Major == 4) {
                if ((fp[4] & 0x80) == 0 && (fp[5] & 0x80) == 0 && (fp[6] & 0x80) == 0 && (fp[7] & 0x80) == 0) {
                    fSize = (static_cast<uint32_t>(fp[4] & 0x7F) << 21) |
                            (static_cast<uint32_t>(fp[5] & 0x7F) << 14) |
                            (static_cast<uint32_t>(fp[6] & 0x7F) << 7)  |
                            static_cast<uint32_t>(fp[7] & 0x7F);
                } else {
                    fSize = DetailSys100::RdU32BE(fp + 4);
                }
                fFlags = DetailSys100::RdU16BE(fp + 8);
            } else {
                fSize = DetailSys100::RdU32BE(fp + 4);
                fFlags = DetailSys100::RdU16BE(fp + 8);
            }
            pos += hdrLen;
            if (fSize == 0 || pos + fSize > tagEnd) break;

            const uint8_t* payload = raw.data() + pos;
            size_t pLen = fSize;
            pos += fSize;

            // Skip ID3v2.4 data length indicator if set
            if (id3Major == 4 && (fFlags & 0x0001u) != 0 && pLen >= 4) {
                payload += 4;
                pLen -= 4;
            }
            if (pLen == 0) continue;

            auto mapFrameName = [](const std::string& id) -> std::wstring {
                if (id == "TIT2" || id == "TT2") return L"Title";
                if (id == "TPE1" || id == "TP1") return L"Artist";
                if (id == "TPE2" || id == "TP2") return L"Album Artist";
                if (id == "TALB" || id == "TAL") return L"Album";
                if (id == "TYER" || id == "TYE" || id == "TDRC") return L"Year / Date";
                if (id == "TCON" || id == "TCO") return L"Genre";
                if (id == "TRCK" || id == "TRK") return L"Track Number";
                if (id == "TPOS" || id == "TPA") return L"Disc Number";
                if (id == "TCOM" || id == "TCM") return L"Composer";
                if (id == "TBPM" || id == "TBP") return L"BPM";
                if (id == "TPUB" || id == "TPB") return L"Publisher";
                if (id == "TSSE" || id == "TSS") return L"Encoder";
                if (id == "COMM" || id == "COM") return L"Comment";
                if (id == "TXXX" || id == "TXX") return L"User Text";
                if (id == "APIC" || id == "PIC") return L"Cover Art";
                return PathUtils::Utf8ToWide(id);
            };

            std::wstring wId = PathUtils::Utf8ToWide(fid);
            std::wstring label = mapFrameName(fid);

            if (fid == "TXXX" || fid == "TXX") {
                uint8_t enc = payload[0];
                size_t termBytes = 0;
                size_t descLen = FindId3Terminator(enc, payload + 1, pLen - 1, termBytes);
                std::wstring desc = DecodeId3String(enc, payload + 1, descLen);
                size_t valOff = 1 + descLen + termBytes;
                std::wstring val = (valOff < pLen) ? DecodeId3String(enc, payload + valOff, pLen - valOff) : L"";
                tagRows.push_back({ wId, desc.empty() ? label : (L"TXXX:" + desc), val });
            } else if (fid == "COMM" || fid == "COM") {
                if (pLen >= 4) {
                    uint8_t enc = payload[0];
                    std::wstring lang = FixedAsciiField(payload + 1, 3);
                    size_t termBytes = 0;
                    size_t descLen = FindId3Terminator(enc, payload + 4, pLen - 4, termBytes);
                    std::wstring desc = DecodeId3String(enc, payload + 4, descLen);
                    size_t txtOff = 4 + descLen + termBytes;
                    std::wstring txt = (txtOff < pLen) ? DecodeId3String(enc, payload + txtOff, pLen - txtOff) : desc;
                    std::wstring lbl = label;
                    if (!lang.empty()) lbl += L" [" + lang + L"]";
                    if (!desc.empty() && txt != desc) lbl += L" (" + desc + L")";
                    tagRows.push_back({ wId, lbl, txt });
                }
            } else if (fid == "APIC" || fid == "PIC") {
                uint8_t enc = payload[0];
                std::wstring mime;
                size_t cursor = 1;
                if (fid == "PIC") {
                    if (pLen < 6) continue;
                    mime = L"image/" + PathUtils::ToLower(FixedAsciiField(payload + 1, 3));
                    cursor = 4;
                } else {
                    size_t nul = 0;
                    while (cursor + nul < pLen && payload[cursor + nul] != 0) ++nul;
                    mime = FixedAsciiField(payload + cursor, nul);
                    cursor += nul + ((cursor + nul < pLen) ? 1 : 0);
                }
                uint8_t picType = (cursor < pLen) ? payload[cursor++] : 0;
                size_t termBytes = 0;
                size_t descLen = (cursor < pLen) ? FindId3Terminator(enc, payload + cursor, pLen - cursor, termBytes) : 0;
                size_t imgOff = cursor + descLen + termBytes;
                size_t imgBytes = (imgOff <= pLen) ? (pLen - imgOff) : 0;
                tagRows.push_back({ wId, label,
                    (mime.empty() ? L"image/binary" : mime) + L", type=" + std::to_wstring(picType) + L", " + PathUtils::FormatByteSize(imgBytes) });
            } else if (fid[0] == 'T') {
                uint8_t enc = payload[0];
                std::wstring val = DecodeId3String(enc, payload + 1, pLen - 1);
                if (!val.empty()) tagRows.push_back({ wId, label, val });
            } else {
                tagRows.push_back({ wId, label, PathUtils::FormatByteSize(pLen) });
            }
        }
    }

    // Trailing 128-byte ID3v1 tag (in the file's tail when the reader was handed only its start)
    const std::vector<uint8_t>& tailBytes = out.sourceTail.empty() ? raw : out.sourceTail;
    const uint64_t fileSize = std::max<uint64_t>(out.sourceFileSize, raw.size());
    bool hasId3v1 = false;
    if (tailBytes.size() >= 128 && std::memcmp(tailBytes.data() + tailBytes.size() - 128, "TAG", 3) == 0) {
        hasId3v1 = true;
        const uint8_t* t1 = tailBytes.data() + tailBytes.size() - 128;
        std::wstring title1   = Latin1OrCp1252ToWide(t1 + 3, 30);
        std::wstring artist1  = Latin1OrCp1252ToWide(t1 + 33, 30);
        std::wstring album1   = Latin1OrCp1252ToWide(t1 + 63, 30);
        std::wstring year1    = Latin1OrCp1252ToWide(t1 + 93, 4);
        std::wstring comment1 = Latin1OrCp1252ToWide(t1 + 97, (t1[125] == 0 && t1[126] != 0) ? 28 : 30);
        if (!hasId3v2 || tagRows.empty()) {
            if (!title1.empty())   tagRows.push_back({ L"ID3v1", L"Title", title1 });
            if (!artist1.empty())  tagRows.push_back({ L"ID3v1", L"Artist", artist1 });
            if (!album1.empty())   tagRows.push_back({ L"ID3v1", L"Album", album1 });
            if (!year1.empty())    tagRows.push_back({ L"ID3v1", L"Year", year1 });
            if (!comment1.empty()) tagRows.push_back({ L"ID3v1", L"Comment", comment1 });
            if (t1[125] == 0 && t1[126] != 0) {
                tagRows.push_back({ L"ID3v1.1", L"Track Number", std::to_wstring(t1[126]) });
            }
            if (t1[127] < 192) {
                tagRows.push_back({ L"ID3v1", L"Genre ID", std::to_wstring(t1[127]) });
            }
        }
    }

    // Scan for MPEG Audio Frame Sync
    static const int kBitratesMpeg1[3][16] = {
        { 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0 }, // Layer I
        { 0, 32, 48, 56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384, 0 }, // Layer II
        { 0, 32, 40, 48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 0 }  // Layer III
    };
    static const int kBitratesMpeg2[3][16] = {
        { 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0 },    // Layer I
        { 0,  8, 16, 24, 32, 40, 48,  56,  64,  80,  96, 112, 128, 144, 160, 0 },    // Layer II
        { 0,  8, 16, 24, 32, 40, 48,  56,  64,  80,  96, 112, 128, 144, 160, 0 }     // Layer III
    };
    static const int kSampleRatesMpeg1[4] = { 44100, 48000, 32000, 0 };

    auto decodeFrameAt = [&](size_t off, int& outVerBits, int& outLayerNum, int& outKbps, int& outHz, int& outChanMode, size_t& outFrameLen) -> bool {
        if (off + 4 > raw.size()) return false;
        uint32_t h = DetailSys100::RdU32BE(raw.data() + off);
        if ((h & 0xFFE00000u) != 0xFFE00000u) return false;
        int verBits = static_cast<int>((h >> 19) & 3u);
        int layBits = static_cast<int>((h >> 17) & 3u);
        int brIdx   = static_cast<int>((h >> 12) & 0xFu);
        int srIdx   = static_cast<int>((h >> 10) & 3u);
        int pad     = static_cast<int>((h >> 9) & 1u);
        int chMode  = static_cast<int>((h >> 6) & 3u);
        if (verBits == 1 || layBits == 0 || brIdx == 0 || brIdx == 15 || srIdx == 3) return false;

        int layerNum = (layBits == 3) ? 1 : ((layBits == 2) ? 2 : 3);
        int kbps = (verBits == 3) ? kBitratesMpeg1[layerNum - 1][brIdx] : kBitratesMpeg2[layerNum - 1][brIdx];
        int hz = kSampleRatesMpeg1[srIdx];
        if (verBits == 2) hz /= 2;
        else if (verBits == 0) hz /= 4;
        if (kbps <= 0 || hz <= 0) return false;

        size_t fLen = 0;
        if (layerNum == 1) {
            fLen = (static_cast<size_t>(12 * kbps * 1000 / hz) + static_cast<size_t>(pad)) * 4;
        } else {
            int coef = (layerNum == 3 && verBits != 3) ? 72 : 144;
            fLen = static_cast<size_t>(coef * kbps * 1000 / hz) + static_cast<size_t>(pad);
        }
        if (fLen < 24) return false;

        outVerBits = verBits;
        outLayerNum = layerNum;
        outKbps = kbps;
        outHz = hz;
        outChanMode = chMode;
        outFrameLen = fLen;
        return true;
    };

    bool foundFrame = false;
    size_t frameOff = 0;
    int mpegVerBits = 3, layerNum = 3, bitrateKbps = 0, sampleRateHz = 0, chanMode = 0;
    size_t firstFrameLen = 0;

    // Frames are looked for in the bytes at hand; the audio itself runs to the end of the file (less an ID3v1 tag)
    const size_t audioLimit = (hasId3v1 && out.sourceTail.empty() && raw.size() >= 128) ? (raw.size() - 128) : raw.size();
    const uint64_t audioEnd = fileSize - (hasId3v1 ? 128u : 0u);
    const size_t scanMax = std::min<size_t>(audioLimit, audioStart + 131072);
    size_t fallbackOff = 0;
    bool hasFallback = false;

    for (size_t off = audioStart; off + 4 <= scanMax; ++off) {
        if (raw[off] != 0xFF || (raw[off + 1] & 0xE0) != 0xE0) continue;
        int vb = 0, ln = 0, kb = 0, hz = 0, cm = 0;
        size_t fl = 0;
        if (!decodeFrameAt(off, vb, ln, kb, hz, cm, fl)) continue;
        if (!hasFallback) {
            hasFallback = true;
            fallbackOff = off;
        }
        // Confirm next frame sync if file has room; otherwise accept at EOF or when ID3 tag already verified MP3
        if (off + fl + 4 <= audioLimit) {
            int vb2 = 0, ln2 = 0, kb2 = 0, hz2 = 0, cm2 = 0;
            size_t fl2 = 0;
            if (decodeFrameAt(off + fl, vb2, ln2, kb2, hz2, cm2, fl2) && vb2 == vb && ln2 == ln && hz2 == hz) {
                foundFrame = true;
                frameOff = off;
                mpegVerBits = vb; layerNum = ln; bitrateKbps = kb; sampleRateHz = hz; chanMode = cm; firstFrameLen = fl;
                break;
            }
        } else {
            foundFrame = true;
            frameOff = off;
            mpegVerBits = vb; layerNum = ln; bitrateKbps = kb; sampleRateHz = hz; chanMode = cm; firstFrameLen = fl;
            break;
        }
    }
    if (!foundFrame && hasFallback && (hasId3v2 || hasId3v1)) {
        foundFrame = decodeFrameAt(fallbackOff, mpegVerBits, layerNum, bitrateKbps, sampleRateHz, chanMode, firstFrameLen);
        frameOff = fallbackOff;
    }

    if (!hasId3v2 && !foundFrame) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"MPEG Audio / ID3 Inspector (.mp3)";

    SpreadsheetSheet info;
    info.name = L"Audio_&_Tags";
    info.rows.push_back({ L"Property", L"Value" });
    info.rows.push_back({ L"File Size", PathUtils::FormatByteSize(fileSize) });

    if (hasId3v2) {
        info.rows.push_back({ L"ID3v2 Tag", L"ID3v2." + std::to_wstring(id3Major) + L"." + std::to_wstring(id3Minor) +
            L" (" + PathUtils::FormatByteSize(id3Size) + L")" });
    }
    if (hasId3v1) {
        info.rows.push_back({ L"ID3v1 Footer", L"Present (128 B)" });
    }

    if (foundFrame) {
        std::wstring verStr = (mpegVerBits == 3) ? L"MPEG-1" : ((mpegVerBits == 2) ? L"MPEG-2" : L"MPEG-2.5");
        std::wstring layStr = (layerNum == 1) ? L"Layer I" : ((layerNum == 2) ? L"Layer II" : L"Layer III");
        std::wstring chStr  = (chanMode == 0) ? L"Stereo" : ((chanMode == 1) ? L"Joint Stereo" : ((chanMode == 2) ? L"Dual Channel" : L"Mono"));

        // Check Xing / Info / VBRI VBR header
        uint32_t vbrFrames = 0;
        uint32_t vbrBytes = 0;
        std::wstring rateMode = L"CBR";
        const size_t fEnd = std::min<size_t>(raw.size(), frameOff + firstFrameLen);
        for (size_t p = frameOff + 4; p + 16 <= fEnd && p <= frameOff + 64; ++p) {
            if (std::memcmp(raw.data() + p, "Xing", 4) == 0 || std::memcmp(raw.data() + p, "Info", 4) == 0) {
                rateMode = (std::memcmp(raw.data() + p, "Xing", 4) == 0) ? L"VBR (Xing)" : L"CBR (Info)";
                uint32_t xFlags = DetailSys100::RdU32BE(raw.data() + p + 4);
                size_t xp = p + 8;
                if ((xFlags & 1u) != 0 && xp + 4 <= fEnd) { vbrFrames = DetailSys100::RdU32BE(raw.data() + xp); xp += 4; }
                if ((xFlags & 2u) != 0 && xp + 4 <= fEnd) { vbrBytes = DetailSys100::RdU32BE(raw.data() + xp); }
                break;
            }
            if (std::memcmp(raw.data() + p, "VBRI", 4) == 0 && p + 18 <= fEnd) {
                rateMode = L"VBR (Fraunhofer VBRI)";
                vbrBytes  = DetailSys100::RdU32BE(raw.data() + p + 10);
                vbrFrames = DetailSys100::RdU32BE(raw.data() + p + 14);
                break;
            }
        }

        double durationSec = 0.0;
        int samplesPerFrame = (layerNum == 1) ? 384 : ((layerNum == 3 && mpegVerBits != 3) ? 576 : 1152);
        if (vbrFrames > 0 && sampleRateHz > 0) {
            durationSec = (static_cast<double>(vbrFrames) * samplesPerFrame) / static_cast<double>(sampleRateHz);
            if (durationSec > 0.0) {
                uint64_t streamBytes = (vbrBytes > 0) ? vbrBytes : (audioEnd > frameOff ? (audioEnd - frameOff) : 0);
                bitrateKbps = static_cast<int>((static_cast<double>(streamBytes) * 8.0) / (durationSec * 1000.0) + 0.5);
            }
        } else if (bitrateKbps > 0 && audioEnd > frameOff) {
            durationSec = (static_cast<double>(audioEnd - frameOff) * 8.0) / (static_cast<double>(bitrateKbps) * 1000.0);
        }

        info.rows.push_back({ L"Codec Profile", verStr + L" " + layStr });
        info.rows.push_back({ L"Bitrate", std::to_wstring(bitrateKbps) + L" kbps (" + rateMode + L")" });
        info.rows.push_back({ L"Sample Rate", std::to_wstring(sampleRateHz) + L" Hz" });
        info.rows.push_back({ L"Channel Mode", chStr });
        info.rows.push_back({ L"Duration", FormatDurationSec(durationSec) });
        info.rows.push_back({ L"First Frame Offset", std::to_wstring(frameOff) + L" B (len " + std::to_wstring(firstFrameLen) + L" B)" });
        if (vbrFrames > 0) {
            info.rows.push_back({ L"VBR Frame Count", std::to_wstring(vbrFrames) });
        }
    }

    for (const auto& tr : tagRows) {
        info.rows.push_back({ tr.label + L" [" + tr.id + L"]", tr.value });
    }
    FinalizeSheetCols(info);
    out.sheets.push_back(std::move(info));

    if (!tagRows.empty()) {
        SpreadsheetSheet frames;
        frames.name = L"ID3_Frames";
        frames.rows.push_back({ L"Frame ID", L"Field", L"Value" });
        for (const auto& tr : tagRows) {
            frames.rows.push_back({ tr.id, tr.label, tr.value });
        }
        FinalizeSheetCols(frames);
        out.sheets.push_back(std::move(frames));
    }

    out.valid = true;
    return true;
}

} // namespace DetailMedia100

inline bool ParseMp3AudioDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMedia100::ParseMp3Audio(raw, out); }

} // namespace Formats
