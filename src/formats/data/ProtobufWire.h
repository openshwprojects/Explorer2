#pragma once
// ProtobufWire.h — Schema-less Protocol Buffers Wire-Format Inspector (.pb / .pbf)

namespace Formats {

namespace DetailStructuredDataDoc {

inline bool WalkProtobuf(const uint8_t* data, size_t size, const std::wstring& prefix, int depth,
                         SpreadsheetSheet* outSheet, size_t& ioRows) {
    if (size == 0 || depth > 4) return false;
    size_t pos = 0;
    size_t fields = 0;
    while (pos < size) {
        uint64_t tag = 0;
        if (!ReadVarint64(data, size, pos, tag) || tag == 0) return false;
        uint64_t fieldNum = tag >> 3;
        uint8_t wireType = static_cast<uint8_t>(tag & 0x07u);
        if (fieldNum == 0 || fieldNum > 536870911ULL) return false;

        std::wstring fpath = prefix.empty() ? (L"#" + std::to_wstring(fieldNum))
                                            : (prefix + L"." + std::to_wstring(fieldNum));
        if (wireType == 0) { // Varint
            uint64_t v = 0;
            if (!ReadVarint64(data, size, pos, v)) return false;
            int64_t zigzag = static_cast<int64_t>((v >> 1) ^ (~(v & 1ULL) + 1ULL));
            if (outSheet && ioRows < 400) {
                outSheet->rows.push_back({ fpath, std::to_wstring(fieldNum), L"0 (Varint)",
                    std::to_wstring(v) + L" (sint: " + std::to_wstring(zigzag) + L")" });
                ++ioRows;
            }
        } else if (wireType == 1) { // 64-bit
            if (pos + 8 > size) return false;
            uint64_t u = RdU64LE(data + pos);
            double d = 0.0; std::memcpy(&d, data + pos, 8);
            pos += 8;
            if (outSheet && ioRows < 400) {
                wchar_t buf[96] = {};
                swprintf_s(buf, L"0x%016llX (double: %.6g)", static_cast<unsigned long long>(u), d);
                outSheet->rows.push_back({ fpath, std::to_wstring(fieldNum), L"1 (Fixed64)", buf });
                ++ioRows;
            }
        } else if (wireType == 2) { // Length-delimited
            uint64_t len = 0;
            if (!ReadVarint64(data, size, pos, len) || len > size - pos) return false;
            const uint8_t* sub = data + pos;
            size_t subLen = static_cast<size_t>(len);
            pos += subLen;

            if (IsMostlyPrintableUtf8(sub, subLen)) {
                if (outSheet && ioRows < 400) {
                    size_t show = std::min<size_t>(subLen, 160);
                    outSheet->rows.push_back({ fpath, std::to_wstring(fieldNum), L"2 (String)",
                        L"\"" + PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(sub), show)) + L"\"" });
                    ++ioRows;
                }
            } else if (subLen >= 2 && depth < 4 && WalkProtobuf(sub, subLen, fpath, depth + 1, nullptr, ioRows)) {
                if (outSheet && ioRows < 400) {
                    outSheet->rows.push_back({ fpath, std::to_wstring(fieldNum), L"2 (Submessage)",
                        L"{" + std::to_wstring(subLen) + L" bytes}" });
                    ++ioRows;
                }
                WalkProtobuf(sub, subLen, fpath, depth + 1, outSheet, ioRows);
            } else if (outSheet && ioRows < 400) {
                outSheet->rows.push_back({ fpath, std::to_wstring(fieldNum), L"2 (Bytes)",
                    L"[" + std::to_wstring(subLen) + L" B] " + HexBytes(sub, subLen, 16) });
                ++ioRows;
            }
        } else if (wireType == 5) { // 32-bit
            if (pos + 4 > size) return false;
            uint32_t u = RdU32LE(data + pos);
            float f = 0.0f; std::memcpy(&f, data + pos, 4);
            pos += 4;
            if (outSheet && ioRows < 400) {
                wchar_t buf[64] = {};
                swprintf_s(buf, L"0x%08X (float: %.5g)", u, f);
                outSheet->rows.push_back({ fpath, std::to_wstring(fieldNum), L"5 (Fixed32)", buf });
                ++ioRows;
            }
        } else {
            return false; // Deprecated group or invalid wire type
        }
        ++fields;
    }
    return fields > 0;
}

} // namespace DetailStructuredDataDoc


inline bool ParseProtobufWireDoc(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    if ((ext != L".pb" && ext != L".pbf") || raw.size() < 2) return false;
    SpreadsheetSheet sh;
    sh.name = L"Protobuf_Wire";
    sh.maxCols = 4;
    sh.rows.push_back({ L"Field Path", L"Tag #", L"Wire Type", L"Decoded Value" });

    size_t rows = 0;
    if (!DetailStructuredDataDoc::WalkProtobuf(raw.data(), raw.size(), L"", 0, &sh, rows)) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Protocol Buffers Wire-Format Message (" + ext + L")";
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}


} // namespace Formats
