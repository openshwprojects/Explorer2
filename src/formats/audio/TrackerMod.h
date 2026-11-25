#pragma once
// TrackerMod.h — ProTracker / Amiga Module (.mod) Inspector

namespace Formats {
namespace DetailMediaAndScoreDoc {

// ============================================================================
// 4. Demoscene Tracker Modules (.mod / .xm / .s3m / .it)
// ============================================================================
inline bool ParseTrackerMod(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 1084) return false;
    std::string sig(reinterpret_cast<const char*>(raw.data() + 1080), 4);
    int channels = 0;
    if (sig == "M.K." || sig == "M!K!" || sig == "M&K!" || sig == "FLT4") channels = 4;
    else if (sig == "FLT8" || sig == "OCTA" || sig == "CD81" || sig == "OKTA") channels = 8;
    else if (sig.size() == 4 && sig.substr(1, 3) == "CHN" && sig[0] >= '1' && sig[0] <= '9') channels = sig[0] - '0';
    else if (sig.size() == 4 && sig.substr(2, 2) == "CH" && sig[0] >= '0' && sig[0] <= '9' && sig[1] >= '0' && sig[1] <= '9')
        channels = (sig[0] - '0') * 10 + (sig[1] - '0');
    else if (sig.size() == 4 && sig.substr(0, 3) == "TDZ" && sig[3] >= '1' && sig[3] <= '3') channels = sig[3] - '0';
    if (channels <= 0 || channels > 64) return false;

    std::wstring songTitle = FixedAsciiField(raw.data(), 20);
    uint8_t songLength = raw[950];
    uint8_t restartPos = raw[951];
    uint8_t maxPat = 0;
    std::wstring orderPreview;
    for (int i = 0; i < 128; ++i) {
        if (raw[952 + i] > maxPat) maxPat = raw[952 + i];
        if (i < static_cast<int>(songLength) && i < 24) {
            if (!orderPreview.empty()) orderPreview += L", ";
            orderPreview += std::to_wstring(raw[952 + i]);
        }
    }
    int numPatterns = static_cast<int>(maxPat) + 1;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"ProTracker / Amiga Module (.mod)";

    SpreadsheetSheet hdr;
    hdr.name = L"MOD_Header";
    hdr.rows.push_back({ L"Property", L"Value" });
    hdr.rows.push_back({ L"Song Title", songTitle.empty() ? L"(Untitled)" : songTitle });
    hdr.rows.push_back({ L"Format Tag", PathUtils::Utf8ToWide(sig) });
    hdr.rows.push_back({ L"Channels", std::to_wstring(channels) });
    hdr.rows.push_back({ L"Song Length (Orders)", std::to_wstring(songLength) });
    hdr.rows.push_back({ L"Restart Position", std::to_wstring(restartPos) });
    hdr.rows.push_back({ L"Pattern Count", std::to_wstring(numPatterns) });
    hdr.rows.push_back({ L"Order Table", orderPreview });
    FinalizeSheetCols(hdr);

    SpreadsheetSheet smp;
    smp.name = L"Samples";
    smp.rows.push_back({ L"#", L"Sample Name", L"Length (Bytes)", L"Finetune", L"Volume", L"Loop Start", L"Loop Length" });
    for (int i = 0; i < 31; ++i) {
        const uint8_t* sp = raw.data() + 20 + i * 30;
        std::wstring sName = FixedAsciiField(sp, 22);
        uint32_t lenBytes = static_cast<uint32_t>(DetailBinarySysDoc::RdU16BE(sp + 22)) * 2u;
        int ft = sp[24] & 0x0F;
        if (ft >= 8) ft -= 16;
        uint8_t vol = sp[25];
        uint32_t loopStart = static_cast<uint32_t>(DetailBinarySysDoc::RdU16BE(sp + 26)) * 2u;
        uint32_t loopLen   = static_cast<uint32_t>(DetailBinarySysDoc::RdU16BE(sp + 28)) * 2u;
        if (!sName.empty() || lenBytes > 0) {
            smp.rows.push_back({
                std::to_wstring(i + 1), sName, std::to_wstring(lenBytes),
                std::to_wstring(ft), std::to_wstring(vol),
                std::to_wstring(loopStart), std::to_wstring(loopLen)
            });
        }
    }
    FinalizeSheetCols(smp);

    out.sheets.push_back(std::move(hdr));
    out.sheets.push_back(std::move(smp));
    out.valid = true;
    return true;
}

} // namespace DetailMediaAndScoreDoc

inline bool ParseTrackerModDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMediaAndScoreDoc::ParseTrackerMod(raw, out); }

} // namespace Formats
