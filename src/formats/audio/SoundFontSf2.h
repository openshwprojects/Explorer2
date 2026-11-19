#pragma once
// SoundFontSf2.h — SoundFont 2 RIFF sfbk (.sf2) Inspector

namespace Formats {
namespace DetailMedia100 {

// ============================================================================
// 5. SoundFont 2 (.sf2) & SFZ (.sfz)
// ============================================================================
inline bool ParseSoundFontSf2(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 12 || std::memcmp(raw.data(), "RIFF", 4) != 0 || std::memcmp(raw.data() + 8, "sfbk", 4) != 0) return false;

    SpreadsheetSheet info;
    info.name = L"SF2_Info";
    info.rows.push_back({ L"Chunk / Field", L"Value" });
    info.rows.push_back({ L"File Size", PathUtils::FormatByteSize(std::max<uint64_t>(out.sourceFileSize, raw.size())) });

    SpreadsheetSheet presets;
    presets.name = L"Presets";
    presets.rows.push_back({ L"Bank", L"Preset #", L"Preset Name" });

    SpreadsheetSheet insts;
    insts.name = L"Instruments";
    insts.rows.push_back({ L"#", L"Instrument Name", L"Bag Index" });

    SpreadsheetSheet samples;
    samples.name = L"Samples";
    samples.rows.push_back({ L"Sample Name", L"Start", L"End", L"Sample Rate (Hz)", L"Root Pitch" });

    size_t pos = 12;
    while (pos + 8 <= raw.size()) {
        std::string cid(reinterpret_cast<const char*>(raw.data() + pos), 4);
        uint32_t csz = DetailSys100::RdU32LE(raw.data() + pos + 4);
        size_t bodyOff = pos + 8;
        size_t bodyEnd = std::min<size_t>(raw.size(), bodyOff + csz);

        if (cid == "LIST" && bodyOff + 4 <= bodyEnd) {
            std::string listType(reinterpret_cast<const char*>(raw.data() + bodyOff), 4);
            size_t sp = bodyOff + 4;
            while (sp + 8 <= bodyEnd) {
                std::string sid(reinterpret_cast<const char*>(raw.data() + sp), 4);
                uint32_t ssz = DetailSys100::RdU32LE(raw.data() + sp + 4);
                size_t sData = sp + 8;
                size_t sEnd = std::min<size_t>(bodyEnd, sData + ssz);
                const uint8_t* p = raw.data() + sData;
                size_t avail = sEnd - sData;

                if (listType == "INFO") {
                    if ((sid == "ifil" || sid == "iver") && avail >= 4) {
                        info.rows.push_back({ PathUtils::Utf8ToWide(sid),
                            std::to_wstring(DetailSys100::RdU16LE(p)) + L"." + std::to_wstring(DetailSys100::RdU16LE(p + 2)) });
                    } else if (avail > 0) {
                        std::wstring val = FixedAsciiField(p, avail);
                        if (!val.empty()) info.rows.push_back({ PathUtils::Utf8ToWide(sid), val });
                    }
                } else if (listType == "sdta" && sid == "smpl") {
                    info.rows.push_back({ L"PCM Sample Pool (smpl)", PathUtils::FormatByteSize(ssz) });
                } else if (listType == "pdta") {
                    if (sid == "phdr") {
                        size_t n = avail / 38;
                        for (size_t i = 0; i + 1 < n && presets.rows.size() < 1024; ++i) {
                            const uint8_t* r = p + i * 38;
                            presets.rows.push_back({
                                std::to_wstring(DetailSys100::RdU16LE(r + 22)),
                                std::to_wstring(DetailSys100::RdU16LE(r + 20)),
                                FixedAsciiField(r, 20)
                            });
                        }
                    } else if (sid == "inst") {
                        size_t n = avail / 22;
                        for (size_t i = 0; i + 1 < n && insts.rows.size() < 1024; ++i) {
                            const uint8_t* r = p + i * 22;
                            insts.rows.push_back({
                                std::to_wstring(i),
                                FixedAsciiField(r, 20),
                                std::to_wstring(DetailSys100::RdU16LE(r + 20))
                            });
                        }
                    } else if (sid == "shdr") {
                        size_t n = avail / 46;
                        for (size_t i = 0; i + 1 < n && samples.rows.size() < 2048; ++i) {
                            const uint8_t* r = p + i * 46;
                            samples.rows.push_back({
                                FixedAsciiField(r, 20),
                                std::to_wstring(DetailSys100::RdU32LE(r + 20)),
                                std::to_wstring(DetailSys100::RdU32LE(r + 24)),
                                std::to_wstring(DetailSys100::RdU32LE(r + 36)),
                                std::to_wstring(r[40])
                            });
                        }
                    }
                }
                sp = sData + ssz + (ssz & 1u);
            }
        }
        pos = bodyOff + csz + (csz & 1u);
    }

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

} // namespace DetailMedia100

inline bool ParseSoundFontSf2Doc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMedia100::ParseSoundFontSf2(raw, out); }

} // namespace Formats
