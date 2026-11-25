#pragma once
// SoundFontSf2.h — SoundFont 2 RIFF sfbk (.sf2) Inspector (SourceBytes >32 MB Safe)

namespace Formats {
namespace DetailMediaAndScoreDoc {

inline bool ParseSoundFontSf2(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    SourceBytes src(raw, out);
    uint8_t riffHdr[12] = {};
    if (src.Size() < 12 || !DetailBinarySysDoc::ReadSourceExact(src, 0, riffHdr, 12) ||
        std::memcmp(riffHdr, "RIFF", 4) != 0 || std::memcmp(riffHdr + 8, "sfbk", 4) != 0)
    {
        return false;
    }

    const uint64_t fileSize = src.Size();

    SpreadsheetSheet info;
    info.name = L"SF2_Info";
    info.rows.push_back({ L"Chunk / Field", L"Value" });
    info.rows.push_back({ L"File Size", PathUtils::FormatByteSize(fileSize) });

    SpreadsheetSheet presets;
    presets.name = L"Presets";
    presets.rows.push_back({ L"Bank", L"Preset #", L"Preset Name" });

    SpreadsheetSheet insts;
    insts.name = L"Instruments";
    insts.rows.push_back({ L"#", L"Instrument Name", L"Bag Index" });

    SpreadsheetSheet samples;
    samples.name = L"Samples";
    samples.rows.push_back({ L"Sample Name", L"Start", L"End", L"Sample Rate (Hz)", L"Root Pitch" });

    uint64_t pos = 12;
    while (pos + 8 <= fileSize) {
        uint8_t chdr[8] = {};
        if (!DetailBinarySysDoc::ReadSourceExact(src, pos, chdr, 8)) break;
        std::string cid(reinterpret_cast<const char*>(chdr), 4);
        uint64_t csz = DetailBinarySysDoc::RdU32LE(chdr + 4);
        uint64_t bodyOff = pos + 8;
        uint64_t bodyEnd = std::min<uint64_t>(fileSize, bodyOff + csz);

        if (cid == "LIST" && bodyOff + 4 <= bodyEnd) {
            uint8_t ltBuf[4] = {};
            if (!DetailBinarySysDoc::ReadSourceExact(src, bodyOff, ltBuf, 4)) break;
            std::string listType(reinterpret_cast<const char*>(ltBuf), 4);
            uint64_t sp = bodyOff + 4;
            while (sp + 8 <= bodyEnd) {
                uint8_t shdr[8] = {};
                if (!DetailBinarySysDoc::ReadSourceExact(src, sp, shdr, 8)) break;
                std::string sid(reinterpret_cast<const char*>(shdr), 4);
                uint64_t ssz = DetailBinarySysDoc::RdU32LE(shdr + 4);
                uint64_t sData = sp + 8;
                uint64_t sEnd = std::min<uint64_t>(bodyEnd, sData + ssz);
                uint64_t avail = sEnd - sData;

                if (listType == "INFO") {
                    const size_t rdLen = static_cast<size_t>(std::min<uint64_t>(avail, 4096));
                    std::vector<uint8_t> buf;
                    if (rdLen > 0 && DetailBinarySysDoc::ReadSourceVec(src, sData, rdLen, buf)) {
                        if ((sid == "ifil" || sid == "iver") && buf.size() >= 4) {
                            info.rows.push_back({ PathUtils::Utf8ToWide(sid),
                                std::to_wstring(DetailBinarySysDoc::RdU16LE(buf.data())) + L"." +
                                std::to_wstring(DetailBinarySysDoc::RdU16LE(buf.data() + 2)) });
                        } else {
                            std::wstring val = FixedAsciiField(buf.data(), buf.size());
                            if (!val.empty()) info.rows.push_back({ PathUtils::Utf8ToWide(sid), std::move(val) });
                        }
                    }
                } else if (listType == "sdta" && sid == "smpl") {
                    info.rows.push_back({ L"PCM Sample Pool (smpl)", PathUtils::FormatByteSize(ssz) });
                } else if (listType == "sdta" && sid == "sm24") {
                    info.rows.push_back({ L"24-bit SampleLSB Pool (sm24)", PathUtils::FormatByteSize(ssz) });
                } else if (listType == "pdta") {
                    if (sid == "phdr" && avail >= 38) {
                        const size_t n = static_cast<size_t>(std::min<uint64_t>(avail / 38u, 1025u));
                        std::vector<uint8_t> buf;
                        if (DetailBinarySysDoc::ReadSourceVec(src, sData, n * 38u, buf)) {
                            for (size_t i = 0; i + 1 < n && presets.rows.size() < 1024; ++i) {
                                const uint8_t* r = buf.data() + i * 38u;
                                presets.rows.push_back({
                                    std::to_wstring(DetailBinarySysDoc::RdU16LE(r + 22)),
                                    std::to_wstring(DetailBinarySysDoc::RdU16LE(r + 20)),
                                    FixedAsciiField(r, 20)
                                });
                            }
                        }
                    } else if (sid == "inst" && avail >= 22) {
                        const size_t n = static_cast<size_t>(std::min<uint64_t>(avail / 22u, 1025u));
                        std::vector<uint8_t> buf;
                        if (DetailBinarySysDoc::ReadSourceVec(src, sData, n * 22u, buf)) {
                            for (size_t i = 0; i + 1 < n && insts.rows.size() < 1024; ++i) {
                                const uint8_t* r = buf.data() + i * 22u;
                                insts.rows.push_back({
                                    std::to_wstring(i),
                                    FixedAsciiField(r, 20),
                                    std::to_wstring(DetailBinarySysDoc::RdU16LE(r + 20))
                                });
                            }
                        }
                    } else if (sid == "shdr" && avail >= 46) {
                        const size_t n = static_cast<size_t>(std::min<uint64_t>(avail / 46u, 2049u));
                        std::vector<uint8_t> buf;
                        if (DetailBinarySysDoc::ReadSourceVec(src, sData, n * 46u, buf)) {
                            for (size_t i = 0; i + 1 < n && samples.rows.size() < 2048; ++i) {
                                const uint8_t* r = buf.data() + i * 46u;
                                samples.rows.push_back({
                                    FixedAsciiField(r, 20),
                                    std::to_wstring(DetailBinarySysDoc::RdU32LE(r + 20)),
                                    std::to_wstring(DetailBinarySysDoc::RdU32LE(r + 24)),
                                    std::to_wstring(DetailBinarySysDoc::RdU32LE(r + 36)),
                                    std::to_wstring(r[40])
                                });
                            }
                        }
                    }
                }
                sp = sData + ssz + (ssz & 1ULL);
            }
        }
        pos = bodyOff + csz + (csz & 1ULL);
    }

    if (!src.ReadFailed()) out.sourceReadFully = true;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"SoundFont 2 Sample Bank (.sf2)";
    FinalizeSheetCols(info);
    out.sheets.push_back(std::move(info));
    if (presets.rows.size() > 1) { FinalizeSheetCols(presets); out.sheets.push_back(std::move(presets)); }
    if (insts.rows.size() > 1)   { FinalizeSheetCols(insts);   out.sheets.push_back(std::move(insts)); }
    if (samples.rows.size() > 1) { FinalizeSheetCols(samples); out.sheets.push_back(std::move(samples)); }
    out.valid = true;
    return true;
}

} // namespace DetailMediaAndScoreDoc

inline bool ParseSoundFontSf2Doc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMediaAndScoreDoc::ParseSoundFontSf2(raw, out); }

} // namespace Formats
