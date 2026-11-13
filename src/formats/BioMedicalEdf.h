#pragma once
// BioMedicalEdf.h — European Data Format (.edf) EEG / Polysomnography Biosignal Header & Channel Parser

namespace Formats {

inline bool ParseBioMedicalEdfDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 256 || std::memcmp(raw.data(), "0       ", 8) != 0) return false;

    auto trimAscii = [](const uint8_t* p, size_t n) -> std::wstring {
        std::string_view sv(reinterpret_cast<const char*>(p), n);
        while (!sv.empty() && (sv.front() == ' ')) sv.remove_prefix(1);
        while (!sv.empty() && (sv.back() == ' ')) sv.remove_suffix(1);
        return PathUtils::Utf8ToWide(sv);
    };

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"European Data Format Biomedical Signal (.edf)";

    SpreadsheetSheet sheet;
    sheet.name = L"EDF_Biosignals";
    sheet.rows.push_back({ L"Field / Signal #", L"Label / Patient", L"Transducer / Recording", L"Physical Dim / Date", L"Range / Samples" });

    std::wstring patient = trimAscii(raw.data() + 8, 80);
    std::wstring record  = trimAscii(raw.data() + 88, 80);
    std::wstring sdate   = trimAscii(raw.data() + 168, 8) + L" " + trimAscii(raw.data() + 176, 8);
    std::wstring nrecs   = trimAscii(raw.data() + 236, 8);
    int ns = std::atoi(std::string(reinterpret_cast<const char*>(raw.data() + 252), 4).c_str());

    sheet.rows.push_back({ L"EDF Header", patient, record, sdate, nrecs + L" records (" + std::to_wstring(ns) + L" signals)" });

    if (ns > 0 && ns <= 256 && raw.size() >= static_cast<size_t>(256 + ns * 256)) {
        const uint8_t* sigBase = raw.data() + 256;
        for (int i = 0; i < ns; ++i) {
            std::wstring lbl   = trimAscii(sigBase + i * 16, 16);
            std::wstring trans = trimAscii(sigBase + ns * 16 + i * 80, 80);
            std::wstring dim   = trimAscii(sigBase + ns * 96 + i * 8, 8);
            std::wstring pmin  = trimAscii(sigBase + ns * 104 + i * 8, 8);
            std::wstring pmax  = trimAscii(sigBase + ns * 112 + i * 8, 8);
            std::wstring samps = trimAscii(sigBase + ns * 216 + i * 8, 8);
            sheet.rows.push_back({
                L"Signal #" + std::to_wstring(i + 1),
                lbl, trans, dim,
                L"[" + pmin + L" .. " + pmax + L"], " + samps + L" smp/rec"
            });
        }
    }

    sheet.maxCols = 5;
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
