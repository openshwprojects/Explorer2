#pragma once
// BioMedicalEdf.h — European Data Format (.edf, EDF and EDF+ / BDF-style headers): the 256-byte header
// (patient, recording, start, data records, record duration, EDF+C / EDF+D), the per-signal header
// (label, transducer, physical dimension and range, digital range, prefiltering, samples per record ->
// sampling rate) and a preview of the first data record converted to physical units.

namespace Formats {

inline bool ParseBioMedicalEdfDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 256 || std::memcmp(raw.data(), "0       ", 8) != 0) return false;
    const uint8_t* d = raw.data();

    auto field = [&](size_t off, size_t len) -> std::string {
        std::string s(reinterpret_cast<const char*>(d + off), len);
        size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
        return (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
    };
    auto w = [](const std::string& s) { return PathUtils::Utf8ToWide(s); };
    auto num = [](const std::string& s, double& v) {
        if (s.empty()) return false;
        const char* b = s.data();
        if (*b == '+') ++b;
        auto res = std::from_chars(b, s.data() + s.size(), v);
        return res.ec == std::errc{} && res.ptr == s.data() + s.size() && std::isfinite(v);
    };

    double headerBytes = 0, nRecords = 0, recDuration = 0, nsD = 0;
    if (!num(field(252, 4), nsD)) return false;
    num(field(184, 8), headerBytes); // when unreadable, the data starts right after the signal headers
    num(field(236, 8), nRecords);
    num(field(244, 8), recDuration);
    int ns = static_cast<int>(nsD);
    if (ns <= 0 || ns > 4096 || static_cast<size_t>(256 + ns * 256) > raw.size()) return false;
    std::string reserved = field(192, 44);
    bool edfPlus = reserved.rfind("EDF+", 0) == 0;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = edfPlus ? L"European Data Format+ Biomedical Signal (.edf)" : L"European Data Format Biomedical Signal (.edf)";

    SpreadsheetSheet sheet;
    sheet.name = L"EDF_Biosignals";
    sheet.rows.push_back({ L"Field / Signal #", L"Label / Patient", L"Transducer / Recording", L"Physical Dim / Date",
                           L"Range / Samples", L"Sampling Rate", L"Prefiltering" });
    std::string start = field(168, 8) + " " + field(176, 8);
    std::wstring total;
    if (nRecords > 0 && recDuration > 0) {
        wchar_t buf[64];
        double secs = nRecords * recDuration;
        swprintf_s(buf, L"%.0f records x %g s = %.1f s", nRecords, recDuration, secs);
        total = buf;
    } else {
        total = w(field(236, 8)) + L" records";
    }
    sheet.rows.push_back({ L"EDF Header", w(field(8, 80)), w(field(88, 80)), w(start), total + L" (" + std::to_wstring(ns) + L" signals)",
                           edfPlus ? w(reserved.substr(0, 5)) : std::wstring(), L"" });

    struct Sig { std::string label; double pmin, pmax, dmin, dmax; int samples; };
    std::vector<Sig> sigs;
    const size_t base = 256;
    auto sigField = [&](size_t blockOff, size_t width, int i) { return field(base + blockOff * static_cast<size_t>(ns) + static_cast<size_t>(i) * width, width); };
    for (int i = 0; i < ns; ++i) {
        Sig s{};
        s.label = sigField(0, 16, i);
        double samples = 0;
        num(sigField(104, 8, i), s.pmin);
        num(sigField(112, 8, i), s.pmax);
        num(sigField(120, 8, i), s.dmin);
        num(sigField(128, 8, i), s.dmax);
        num(sigField(216, 8, i), samples);
        s.samples = static_cast<int>(samples);
        sigs.push_back(s);
        std::wstring rate;
        if (recDuration > 0 && s.samples > 0) {
            wchar_t buf[32];
            swprintf_s(buf, L"%g Hz", s.samples / recDuration);
            rate = buf;
        }
        if (sheet.rows.size() <= 1000) {
            sheet.rows.push_back({ L"Signal #" + std::to_wstring(i + 1), w(s.label), w(sigField(16, 80, i)), w(sigField(96, 8, i)),
                                   L"[" + w(sigField(104, 8, i)) + L" .. " + w(sigField(112, 8, i)) + L"], " + std::to_wstring(s.samples) + L" smp/rec",
                                   rate, w(sigField(136, 80, i)) });
        }
    }
    sheet.maxCols = 7;
    out.sheets.push_back(std::move(sheet));

    // First data record: 16-bit little-endian samples, signal after signal, scaled to physical units
    size_t dataStart = static_cast<size_t>(std::max(headerBytes, 256.0 + ns * 256.0));
    size_t recordSamples = 0;
    for (const auto& s : sigs) recordSamples += static_cast<size_t>(std::max(0, s.samples));
    if (recordSamples > 0 && dataStart + recordSamples * 2 <= raw.size()) {
        SpreadsheetSheet preview;
        preview.name = L"EDF_First_Record";
        std::vector<std::wstring> hdr = { L"Sample #" };
        size_t shown = 0;
        for (const auto& s : sigs) {
            if (shown++ >= 32) break;
            hdr.push_back(w(s.label));
        }
        preview.rows.push_back(hdr);
        int maxRows = 0;
        for (const auto& s : sigs) maxRows = std::max(maxRows, s.samples);
        maxRows = std::min(maxRows, 200);
        std::vector<size_t> offsets;
        size_t off = dataStart;
        for (const auto& s : sigs) { offsets.push_back(off); off += static_cast<size_t>(std::max(0, s.samples)) * 2; }
        for (int r = 0; r < maxRows; ++r) {
            std::vector<std::wstring> row = { std::to_wstring(r + 1) };
            for (size_t k = 0; k < sigs.size() && k < 32; ++k) {
                const Sig& s = sigs[k];
                if (r >= s.samples) { row.push_back(L""); continue; }
                const uint8_t* p = d + offsets[k] + static_cast<size_t>(r) * 2;
                int16_t digital = static_cast<int16_t>(p[0] | (p[1] << 8));
                double span = s.dmax - s.dmin;
                double phys = (span != 0.0) ? s.pmin + (digital - s.dmin) * (s.pmax - s.pmin) / span : digital;
                wchar_t buf[32];
                swprintf_s(buf, L"%.6g", phys);
                row.push_back(buf);
            }
            preview.rows.push_back(std::move(row));
        }
        preview.maxCols = hdr.size();
        out.sheets.push_back(std::move(preview));
    }
    out.valid = true;
    return true;
}

} // namespace Formats
