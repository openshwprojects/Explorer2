#pragma once
// NintendoJSystemBcsv.h — Nintendo JSystem Binary CSV Parameter Table (.bcsv)
// Used across Super Mario Galaxy 1 & 2, Zelda, Pikmin, and Luigi's Mansion.
// Auto-detects big-endian (GameCube/Wii) vs little-endian (3DS) via exact `dataOffset == 16 + fieldCount * 12`.

namespace Formats {

inline bool ParseNintendoJSystemBcsv(const std::vector<uint8_t>& raw, ParsedOfficeDocument& outDoc) {
    const size_t size = raw.size();
    const uint8_t* data = raw.data();
    if (!data || size < 28u || size > (32u << 20)) return false;

    auto checkHeader = [&](bool be, uint32_t& nRows, uint32_t& nFields, uint32_t& datOff, uint32_t& rowStride) -> bool {
        nRows     = ByteIO::RdU32(data + 0u, be);
        nFields   = ByteIO::RdU32(data + 4u, be);
        datOff    = ByteIO::RdU32(data + 8u, be);
        rowStride = ByteIO::RdU32(data + 12u, be);
        if (nFields == 0u || nFields > 512u || nRows > 200000u) return false;
        if (rowStride == 0u || rowStride > 4096u) return false;
        const uint64_t expectedDatOff = 16ULL + static_cast<uint64_t>(nFields) * 12ULL;
        if (datOff != expectedDatOff) return false;
        const uint64_t rowsEnd = expectedDatOff + static_cast<uint64_t>(nRows) * rowStride;
        return rowsEnd <= size;
    };

    bool be = true;
    uint32_t entryCount = 0u, fieldCount = 0u, dataOff = 0u, entryStride = 0u;
    if (!checkHeader(true, entryCount, fieldCount, dataOff, entryStride)) {
        be = false;
        if (!checkHeader(false, entryCount, fieldCount, dataOff, entryStride)) return false;
    }

    struct BcsvField {
        uint32_t hash      = 0u;
        uint32_t bitmask   = 0xFFFFFFFFu;
        uint16_t offInRow  = 0u;
        uint8_t  shift     = 0u;
        uint8_t  dataType  = 0u; // 0=S32, 1=InlineChar[32], 2=F32, 3=U32, 4=S16, 5=U8, 6=StrPoolOff
    };

    std::vector<BcsvField> fields(fieldCount);
    SpreadsheetSheet sheet;
    sheet.name = L"BCSV";

    std::vector<std::wstring> hdrRow;
    hdrRow.reserve(fieldCount);
    for (uint32_t fi = 0u; fi < fieldCount; ++fi) {
        const uint8_t* fp = data + 16u + static_cast<size_t>(fi) * 12u;
        auto& f = fields[fi];
        f.hash     = ByteIO::RdU32(fp + 0u, be);
        f.bitmask  = ByteIO::RdU32(fp + 4u, be);
        f.offInRow = ByteIO::RdU16(fp + 8u, be);
        f.shift    = fp[10u];
        f.dataType = fp[11u];
        if (f.dataType > 6u || f.shift >= 32u) return false;

        size_t needBytes = 1u;
        if (f.dataType == 0u || f.dataType == 2u || f.dataType == 3u || f.dataType == 6u) needBytes = 4u;
        else if (f.dataType == 4u) needBytes = 2u;
        else if (f.dataType == 1u) needBytes = 32u;
        if (static_cast<size_t>(f.offInRow) + needBytes > entryStride) return false;

        wchar_t colName[32] = {};
        swprintf_s(colName, L"0x%08X", f.hash);
        hdrRow.emplace_back(colName);
    }
    sheet.rows.push_back(std::move(hdrRow));

    const size_t strPoolBase = static_cast<size_t>(dataOff) + static_cast<size_t>(entryCount) * entryStride;

    for (uint32_t ri = 0u; ri < entryCount && sheet.rows.size() < 16385u; ++ri) {
        const uint8_t* rp = data + dataOff + static_cast<size_t>(ri) * entryStride;
        std::vector<std::wstring> row;
        row.reserve(fieldCount);

        for (const auto& f : fields) {
            const uint8_t* cp = rp + f.offInRow;
            if (f.dataType == 0u) {
                const uint32_t rawU = (ByteIO::RdU32(cp, be) & f.bitmask) >> f.shift;
                if (f.bitmask == 0xFFFFFFFFu && f.shift == 0u) {
                    row.push_back(std::to_wstring(static_cast<int32_t>(rawU)));
                } else {
                    row.push_back(std::to_wstring(rawU));
                }
            } else if (f.dataType == 3u) {
                const uint32_t rawU = (ByteIO::RdU32(cp, be) & f.bitmask) >> f.shift;
                row.push_back(std::to_wstring(rawU));
            } else if (f.dataType == 2u) {
                const float fv = ByteIO::RdF32(cp, be);
                if (!std::isfinite(fv)) return false;
                wchar_t fBuf[48] = {};
                swprintf_s(fBuf, L"%.6g", fv);
                row.emplace_back(fBuf);
            } else if (f.dataType == 4u) {
                const uint16_t raw16 = ByteIO::RdU16(cp, be);
                if (f.bitmask == 0xFFFFu && f.shift == 0u) {
                    row.push_back(std::to_wstring(static_cast<int16_t>(raw16)));
                } else {
                    row.push_back(std::to_wstring((raw16 & f.bitmask) >> f.shift));
                }
            } else if (f.dataType == 5u) {
                row.push_back(std::to_wstring((cp[0] & f.bitmask) >> f.shift));
            } else if (f.dataType == 1u) {
                const size_t slen = strnlen(reinterpret_cast<const char*>(cp), 32u);
                row.push_back(BioWareCommon::DecodeBioWareString(std::string_view(reinterpret_cast<const char*>(cp), slen)));
            } else {
                // dataType == 6: offset into trailing string pool
                const uint32_t sOff = ByteIO::RdU32(cp, be);
                if (strPoolBase >= size || sOff >= size - strPoolBase) return false;
                const char* sp = reinterpret_cast<const char*>(data + strPoolBase + sOff);
                const size_t maxL = size - (strPoolBase + sOff);
                const size_t slen = strnlen(sp, maxL);
                if (slen == maxL) return false;
                row.push_back(BioWareCommon::DecodeBioWareString(std::string_view(sp, slen)));
            }
        }
        sheet.rows.push_back(std::move(row));
    }

    if (entryCount == 0u) return false;

    wchar_t title[160] = {};
    swprintf_s(title, L"Nintendo JSystem BCSV Table (.bcsv %ls, %u rows x %u fields)",
               be ? L"BE" : L"LE", entryCount, fieldCount);
    return BioWareCommon::EmitSpreadsheetDocument(outDoc, title, std::move(sheet));
}

} // namespace Formats
