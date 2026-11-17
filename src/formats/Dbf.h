#pragma once
// Dbf.h — dBASE III/IV/5, FoxPro and Visual FoxPro tables (.dbf): text decoded with the code page named by the
// header's language driver byte (DOS 437 / 850 / 852 / 866, Windows 1250 / 1251 / 1252, ...), typed fields
// shown as values (C, N, F, D, L, M, and the binary Visual FoxPro I, B, Y, T), deleted records skipped and
// counted, and a summary of the table header.

namespace DetailDbf {

// Language driver ID (header byte 29) -> Windows code page
inline unsigned CodePageFromLdid(uint8_t ldid) {
    switch (ldid) {
        case 0x01: return 437;  case 0x02: return 850;  case 0x03: return 1252; case 0x04: return 10000;
        case 0x08: return 865;  case 0x09: return 437;  case 0x0A: return 850;  case 0x0B: return 437;
        case 0x0D: return 437;  case 0x0E: return 850;  case 0x0F: return 437;  case 0x10: return 850;
        case 0x11: return 437;  case 0x12: return 850;  case 0x13: return 932;  case 0x14: return 850;
        case 0x15: return 437;  case 0x16: return 850;  case 0x17: return 865;  case 0x18: return 437;
        case 0x19: return 437;  case 0x1A: return 850;  case 0x1B: return 437;  case 0x1C: return 863;
        case 0x1D: return 850;  case 0x1F: return 852;  case 0x22: return 852;  case 0x23: return 852;
        case 0x24: return 860;  case 0x25: return 850;  case 0x26: return 866;  case 0x37: return 850;
        case 0x40: return 852;  case 0x4D: return 936;  case 0x4E: return 949;  case 0x4F: return 950;
        case 0x50: return 874;  case 0x57: return 1252; case 0x58: return 1252; case 0x59: return 1252;
        case 0x64: return 852;  case 0x65: return 866;  case 0x66: return 865;  case 0x67: return 861;
        case 0x6A: return 737;  case 0x6B: return 857;  case 0x78: return 950;  case 0x79: return 949;
        case 0x7A: return 936;  case 0x7B: return 932;  case 0x7C: return 874;  case 0x86: return 737;
        case 0x87: return 852;  case 0x88: return 857;  case 0xC8: return 1250; case 0xC9: return 1251;
        case 0xCA: return 1254; case 0xCB: return 1253; case 0xCC: return 1257;
        default: return 0;
    }
}

inline std::wstring Decode(const uint8_t* p, size_t n, unsigned cp) {
    if (n == 0) return {};
    if (cp != 0) {
        int wn = MultiByteToWideChar(cp, 0, reinterpret_cast<const char*>(p), static_cast<int>(n), nullptr, 0);
        if (wn > 0) {
            std::wstring w(static_cast<size_t>(wn), L'\0');
            MultiByteToWideChar(cp, 0, reinterpret_cast<const char*>(p), static_cast<int>(n), w.data(), wn);
            return w;
        }
    }
    return PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), n));
}

inline std::wstring Trim(std::wstring s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    size_t b = s.find_last_not_of(L" \t\r\n");
    return (a == std::wstring::npos) ? std::wstring() : s.substr(a, b - a + 1);
}

} // namespace DetailDbf

static bool ParseDbfDatabase(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using namespace DetailDbf;
    if (raw.size() < 64) return false;
    const uint8_t version = raw[0];
    uint32_t numRecords = static_cast<uint32_t>(raw[4]) | (static_cast<uint32_t>(raw[5]) << 8) |
                          (static_cast<uint32_t>(raw[6]) << 16) | (static_cast<uint32_t>(raw[7]) << 24);
    uint16_t headerBytes = static_cast<uint16_t>(raw[8] | (raw[9] << 8));
    uint16_t recordBytes = static_cast<uint16_t>(raw[10] | (raw[11] << 8));
    if (headerBytes < 33 || headerBytes > raw.size() || recordBytes < 2) return false;
    const unsigned cp = CodePageFromLdid(raw[29]);

    struct Field { std::wstring name; char type = 'C'; uint8_t length = 0; uint8_t decimals = 0; };
    std::vector<Field> fields;
    for (size_t pos = 32; pos + 32 <= headerBytes && raw[pos] != 0x0D && fields.size() < 2048; pos += 32) {
        Field f;
        size_t nl = 0;
        while (nl < 11 && raw[pos + nl] != 0) ++nl;
        f.name = Decode(&raw[pos], nl, cp);
        f.type = static_cast<char>(raw[pos + 11]);
        f.length = raw[pos + 16];
        f.decimals = raw[pos + 17];
        if (f.name.empty() || f.length == 0) continue;
        fields.push_back(std::move(f));
    }
    if (fields.empty()) return false;

    out.kind = OfficeFileKind::DbfTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"dBASE / FoxPro Database Table (.dbf)";

    SpreadsheetSheet sheet;
    sheet.name = L"DBF_Table";
    sheet.maxCols = fields.size();
    std::vector<std::wstring> hdrRow;
    for (const auto& f : fields) hdrRow.push_back(f.name);
    sheet.rows.push_back(std::move(hdrRow));

    auto fmtDouble = [](double v, int decimals) {
        wchar_t buf[64];
        swprintf_s(buf, L"%.*f", std::clamp(decimals, 0, 15), v);
        return std::wstring(buf);
    };
    size_t deleted = 0, shown = 0;
    size_t recOffset = headerBytes;
    for (uint32_t r = 0; r < numRecords && recOffset + recordBytes <= raw.size(); ++r, recOffset += recordBytes) {
        if (raw[recOffset] == 0x1A) break; // end-of-file marker
        if (raw[recOffset] == '*') { ++deleted; continue; }
        if (shown >= 20000) continue;
        ++shown;
        size_t fOff = recOffset + 1;
        std::vector<std::wstring> row;
        row.reserve(fields.size());
        for (const auto& f : fields) {
            if (fOff + f.length > recOffset + recordBytes) break;
            const uint8_t* v = &raw[fOff];
            std::wstring cell;
            switch (f.type) {
                case 'D': { // YYYYMMDD
                    std::wstring t = Trim(Decode(v, f.length, 0));
                    cell = (t.size() == 8) ? t.substr(0, 4) + L"-" + t.substr(4, 2) + L"-" + t.substr(6, 2) : t;
                    break;
                }
                case 'L': {
                    char c = static_cast<char>(v[0]);
                    cell = (c == 'T' || c == 't' || c == 'Y' || c == 'y') ? L"true" : (c == 'F' || c == 'f' || c == 'N' || c == 'n') ? L"false" : L"";
                    break;
                }
                case 'I': // Visual FoxPro 32-bit integer
                    if (f.length >= 4) {
                        int32_t iv = static_cast<int32_t>(v[0] | (v[1] << 8) | (v[2] << 16) | (static_cast<uint32_t>(v[3]) << 24));
                        cell = std::to_wstring(iv);
                    }
                    break;
                case 'B': // Visual FoxPro double
                    if (f.length >= 8) { double dv = 0; std::memcpy(&dv, v, 8); cell = fmtDouble(dv, f.decimals); }
                    break;
                case 'Y': // currency: int64 / 10000
                    if (f.length >= 8) { int64_t cv = 0; std::memcpy(&cv, v, 8); cell = fmtDouble(static_cast<double>(cv) / 10000.0, 4); }
                    break;
                case 'T': // Julian day + milliseconds
                    if (f.length >= 8) {
                        int32_t jd = 0, ms = 0;
                        std::memcpy(&jd, v, 4);
                        std::memcpy(&ms, v + 4, 4);
                        if (jd > 0) {
                            // Julian day number -> civil date (Fliegel & Van Flandern)
                            int64_t l = static_cast<int64_t>(jd) + 68569, n4 = 4 * l / 146097;
                            l -= (146097 * n4 + 3) / 4;
                            int64_t i = 4000 * (l + 1) / 1461001;
                            l = l - 1461 * i / 4 + 31;
                            int64_t j = 80 * l / 2447, day = l - 2447 * j / 80;
                            l = j / 11;
                            int64_t month = j + 2 - 12 * l, year = 100 * (n4 - 49) + i + l;
                            wchar_t buf[48];
                            swprintf_s(buf, L"%04lld-%02lld-%02lld %02d:%02d:%02d", year, month, day,
                                       ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60);
                            cell = buf;
                        }
                    }
                    break;
                case 'M': case 'G': case 'P': { // memo / general / picture: block number in the .dbt / .fpt file
                    std::wstring t = Trim(Decode(v, f.length, 0));
                    if (f.length == 4) {
                        uint32_t blk = v[0] | (v[1] << 8) | (v[2] << 16) | (static_cast<uint32_t>(v[3]) << 24);
                        t = blk ? std::to_wstring(blk) : L"";
                    }
                    cell = t.empty() ? L"" : L"[memo block " + t + L"]";
                    break;
                }
                default: // C, N, F and anything else: text in the table's code page
                    cell = Trim(Decode(v, f.length, cp));
                    break;
            }
            row.push_back(std::move(cell));
            fOff += f.length;
        }
        sheet.rows.push_back(std::move(row));
    }
    out.sheets.push_back(std::move(sheet));

    SpreadsheetSheet info;
    info.name = L"DBF_Header";
    info.rows.push_back({ L"Property", L"Value" });
    const wchar_t* kind = (version == 0x03 || version == 0x83) ? L"dBASE III" : (version == 0x04 || version == 0x8B) ? L"dBASE IV"
                        : (version == 0x05) ? L"dBASE 5" : (version == 0x30 || version == 0x31 || version == 0x32) ? L"Visual FoxPro"
                        : (version == 0xF5) ? L"FoxPro 2 with memo" : L"dBASE / FoxPro";
    wchar_t vb[16];
    swprintf_s(vb, L"0x%02X", version);
    info.rows.push_back({ L"Version", std::wstring(kind) + L" (" + vb + L")" });
    info.rows.push_back({ L"Last update", std::to_wstring(1900 + raw[1]) + L"-" + std::to_wstring(raw[2]) + L"-" + std::to_wstring(raw[3]) });
    info.rows.push_back({ L"Records", std::to_wstring(numRecords) + (deleted ? L" (" + std::to_wstring(deleted) + L" deleted)" : L"") });
    info.rows.push_back({ L"Fields", std::to_wstring(fields.size()) });
    info.rows.push_back({ L"Record size", std::to_wstring(recordBytes) + L" B" });
    info.rows.push_back({ L"Code page", cp ? std::to_wstring(cp) : std::wstring(L"not given (UTF-8 / system ANSI)") });
    info.maxCols = 2;
    out.sheets.push_back(std::move(info));
    out.valid = true;
    return true;
}
