#pragma once
// NumPyNpy.h — NumPy N-Dimensional Tensor (.npy) Reader

namespace Formats {
namespace AiDataDetail {

inline bool ParseSingleNpyBuffer(
    const uint8_t* data,
    size_t size,
    const std::wstring& arrayLabel,
    std::vector<SpreadsheetSheet>& outSheets,
    bool emitMetadataSheet)
{
    if (!data || size < 10 || std::memcmp(data, "\x93NUMPY", 6) != 0) return false;
    uint8_t maj = data[6];
    uint8_t minVer = data[7];
    if (maj < 1 || maj > 3) return false;

    size_t hdrStart = (maj >= 2) ? 12 : 10;
    if (size < hdrStart) return false;
    uint32_t hdrLen = (maj >= 2) ? RdU32LE(data + 8) : static_cast<uint32_t>(RdU16LE(data + 8));
    if (hdrLen == 0 || hdrLen > size - hdrStart) return false;

    std::string hdr(reinterpret_cast<const char*>(data + hdrStart), hdrLen);
    size_t dataOff = hdrStart + hdrLen;
    size_t availBytes = size - dataOff;

    // Extract 'descr'
    std::string descr;
    size_t dPos = hdr.find("descr");
    if (dPos == std::string::npos) return false;
    size_t colon = hdr.find(':', dPos);
    if (colon == std::string::npos) return false;
    for (size_t i = colon + 1; i < hdr.size(); ++i) {
        if (hdr[i] == '\'' || hdr[i] == '"') {
            char q = hdr[i];
            size_t j = hdr.find(q, i + 1);
            if (j != std::string::npos) descr = hdr.substr(i + 1, j - i - 1);
            break;
        }
    }
    if (descr.size() < 2) return false;

    char endianCh = descr[0];
    char typeCh = descr[1];
    size_t itemCountOrBytes = 0;
    if (endianCh != '<' && endianCh != '>' && endianCh != '|' && endianCh != '=') {
        endianCh = '=';
        typeCh = descr[0];
        itemCountOrBytes = static_cast<size_t>(std::strtoul(descr.c_str() + 1, nullptr, 10));
    } else {
        itemCountOrBytes = static_cast<size_t>(std::strtoul(descr.c_str() + 2, nullptr, 10));
    }
    bool bigEndian = (endianCh == '>');
    size_t elemBytes = itemCountOrBytes;
    if (typeCh == 'U') elemBytes = itemCountOrBytes * 4;
    if (elemBytes == 0 && (typeCh == 'b' || typeCh == 'B')) elemBytes = 1;
    if (elemBytes == 0) return false;

    bool validType = (typeCh == 'f' && (elemBytes == 2 || elemBytes == 4 || elemBytes == 8)) ||
                     ((typeCh == 'i' || typeCh == 'u') && (elemBytes == 1 || elemBytes == 2 || elemBytes == 4 || elemBytes == 8)) ||
                     (typeCh == 'b' && elemBytes == 1) ||
                     (typeCh == 'S' && elemBytes >= 1) ||
                     (typeCh == 'U' && elemBytes >= 4);
    if (!validType) return false;

    bool fortranOrder = false;
    size_t fPos = hdr.find("fortran_order");
    if (fPos != std::string::npos) {
        size_t fc = hdr.find(':', fPos);
        if (fc != std::string::npos) {
            size_t tPos = hdr.find("True", fc);
            size_t commaPos = hdr.find(',', fc);
            if (tPos != std::string::npos && (commaPos == std::string::npos || tPos < commaPos)) {
                fortranOrder = true;
            }
        }
    }

    std::vector<uint64_t> shape;
    size_t sPos = hdr.find("shape");
    if (sPos != std::string::npos) {
        size_t p1 = hdr.find('(', sPos);
        size_t p2 = (p1 != std::string::npos) ? hdr.find(')', p1) : std::string::npos;
        if (p1 == std::string::npos || p2 == std::string::npos) return false;
        size_t k = p1 + 1;
        while (k < p2) {
            while (k < p2 && (hdr[k] == ' ' || hdr[k] == ',' || hdr[k] == '\t')) ++k;
            if (k >= p2) break;
            char* endPtr = nullptr;
            uint64_t dim = std::strtoull(hdr.c_str() + k, &endPtr, 10);
            if (endPtr == hdr.c_str() + k) break;
            shape.push_back(dim);
            k = static_cast<size_t>(endPtr - hdr.c_str());
        }
    } else {
        // Fallback for compact 1D headers omitting 'shape' (Stage12.Test13 compatibility)
        shape.push_back(availBytes / elemBytes);
    }

    uint64_t totalElems = 1;
    for (uint64_t d : shape) {
        if (d == 0) { totalElems = 0; break; }
        if (totalElems > (availBytes / elemBytes) / d + 1) {
            totalElems = availBytes / elemBytes;
            break;
        }
        totalElems *= d;
    }
    uint64_t readableElems = std::min<uint64_t>(totalElems, availBytes / elemBytes);

    auto readNumeric = [&](size_t idx) -> double {
        const uint8_t* p = data + dataOff + idx * elemBytes;
        if (typeCh == 'f') {
            if (elemBytes == 2) return static_cast<double>(HalfToFloat(bigEndian ? RdU16BE(p) : RdU16LE(p)));
            if (elemBytes == 4) return static_cast<double>(bigEndian ? RdF32BE(p) : RdF32LE(p));
            return bigEndian ? RdF64BE(p) : RdF64LE(p);
        }
        if (typeCh == 'i') {
            if (elemBytes == 1) return static_cast<double>(static_cast<int8_t>(p[0]));
            if (elemBytes == 2) return static_cast<double>(bigEndian ? RdI16BE(p) : RdI16LE(p));
            if (elemBytes == 4) return static_cast<double>(bigEndian ? RdI32BE(p) : RdI32LE(p));
            return static_cast<double>(bigEndian ? RdI64BE(p) : RdI64LE(p));
        }
        if (typeCh == 'u') {
            if (elemBytes == 1) return static_cast<double>(p[0]);
            if (elemBytes == 2) return static_cast<double>(bigEndian ? RdU16BE(p) : RdU16LE(p));
            if (elemBytes == 4) return static_cast<double>(bigEndian ? RdU32BE(p) : RdU32LE(p));
            return static_cast<double>(bigEndian ? RdU64BE(p) : RdU64LE(p));
        }
        if (typeCh == 'b') return p[0] != 0 ? 1.0 : 0.0;
        return 0.0;
    };

    auto readElemStr = [&](size_t idx) -> std::wstring {
        const uint8_t* p = data + dataOff + idx * elemBytes;
        if (typeCh == 'b') return p[0] != 0 ? L"True" : L"False";
        if (typeCh == 'S') return CleanAsciiFixed(p, elemBytes);
        if (typeCh == 'U') {
            std::wstring ws;
            for (size_t c = 0; c < itemCountOrBytes; ++c) {
                uint32_t cp = bigEndian ? RdU32BE(p + c * 4) : RdU32LE(p + c * 4);
                if (cp == 0) break;
                if (cp < 0x10000u) ws.push_back(static_cast<wchar_t>(cp));
                else ws.push_back(L'?');
            }
            return ws;
        }
        if (typeCh == 'i' && elemBytes == 8) return std::to_wstring(bigEndian ? RdI64BE(p) : RdI64LE(p));
        if (typeCh == 'u' && elemBytes == 8) return std::to_wstring(bigEndian ? RdU64BE(p) : RdU64LE(p));
        return FmtDouble(readNumeric(idx));
    };

    bool isNumeric = (typeCh == 'f' || typeCh == 'i' || typeCh == 'u');
    double minVal = 0.0, maxVal = 0.0, sumVal = 0.0;
    size_t finiteCnt = 0;
    if (isNumeric && readableElems > 0) {
        size_t statCap = static_cast<size_t>(std::min<uint64_t>(readableElems, 100000));
        for (size_t i = 0; i < statCap; ++i) {
            double v = readNumeric(i);
            if (std::isfinite(v)) {
                if (finiteCnt == 0) { minVal = v; maxVal = v; }
                else { minVal = std::min(minVal, v); maxVal = std::max(maxVal, v); }
                sumVal += v;
                ++finiteCnt;
            }
        }
    }

    std::wstring shapeStr = L"(";
    for (size_t i = 0; i < shape.size(); ++i) {
        if (i > 0) shapeStr += L", ";
        shapeStr += std::to_wstring(shape[i]);
    }
    if (shape.size() == 1) shapeStr += L",";
    shapeStr += L")";

    if (emitMetadataSheet) {
        SpreadsheetSheet meta;
        meta.name = L"NumPy_Metadata";
        meta.rows.push_back({ L"Property", L"Value" });
        meta.rows.push_back({ L"Array Name", arrayLabel });
        meta.rows.push_back({ L"Format Version", std::to_wstring(maj) + L"." + std::to_wstring(minVer) });
        meta.rows.push_back({ L"Data Type (descr)", PathUtils::Utf8ToWide(descr) });
        meta.rows.push_back({ L"Shape", shapeStr });
        meta.rows.push_back({ L"Memory Order", fortranOrder ? L"Fortran (Column-Major)" : L"C (Row-Major)" });
        meta.rows.push_back({ L"Total Elements", std::to_wstring(totalElems) });
        meta.rows.push_back({ L"Element Size", std::to_wstring(elemBytes) + L" B" });
        if (finiteCnt > 0) {
            meta.rows.push_back({ L"Min Value", FmtDouble(minVal) });
            meta.rows.push_back({ L"Max Value", FmtDouble(maxVal) });
            meta.rows.push_back({ L"Mean Value", FmtDouble(sumVal / static_cast<double>(finiteCnt)) });
        }
        FinalizeSheetCols(meta);
        outSheets.push_back(std::move(meta));
    }

    SpreadsheetSheet dataSh;
    dataSh.name = arrayLabel.empty() ? L"NumPy_Array" : arrayLabel;
    if (shape.size() == 2 && shape[0] > 0 && shape[1] > 0) {
        size_t rows = static_cast<size_t>(std::min<uint64_t>(shape[0], 256));
        size_t cols = static_cast<size_t>(std::min<uint64_t>(shape[1], 64));
        std::vector<std::wstring> hdrRow = { L"Row \\ Col" };
        for (size_t c = 0; c < cols; ++c) hdrRow.push_back(L"[" + std::to_wstring(c) + L"]");
        dataSh.rows.push_back(std::move(hdrRow));
        for (size_t r = 0; r < rows; ++r) {
            std::vector<std::wstring> rvec = { L"[" + std::to_wstring(r) + L"]" };
            for (size_t c = 0; c < cols; ++c) {
                uint64_t flatIdx = fortranOrder ? (r + c * shape[0]) : (r * shape[1] + c);
                if (flatIdx < readableElems) rvec.push_back(readElemStr(static_cast<size_t>(flatIdx)));
                else rvec.push_back(L"");
            }
            dataSh.rows.push_back(std::move(rvec));
        }
    } else {
        dataSh.rows.push_back({ L"Index / Coordinates", L"Value (" + PathUtils::Utf8ToWide(descr) + L")", L"Shape: " + shapeStr });
        size_t cap = static_cast<size_t>(std::min<uint64_t>(readableElems, 512));
        for (size_t i = 0; i < cap; ++i) {
            std::wstring coordStr;
            if (shape.size() <= 1) {
                coordStr = L"[" + std::to_wstring(i) + L"]";
            } else {
                uint64_t rem = i;
                std::vector<uint64_t> coords(shape.size(), 0);
                if (fortranOrder) {
                    for (size_t d = 0; d < shape.size(); ++d) {
                        coords[d] = shape[d] ? (rem % shape[d]) : 0;
                        rem = shape[d] ? (rem / shape[d]) : 0;
                    }
                } else {
                    for (size_t d = shape.size(); d-- > 0;) {
                        coords[d] = shape[d] ? (rem % shape[d]) : 0;
                        rem = shape[d] ? (rem / shape[d]) : 0;
                    }
                }
                coordStr = L"[";
                for (size_t d = 0; d < coords.size(); ++d) {
                    if (d > 0) coordStr += L", ";
                    coordStr += std::to_wstring(coords[d]);
                }
                coordStr += L"]";
            }
            dataSh.rows.push_back({ coordStr, readElemStr(i), finiteCnt > 0 && i == 0 ? (L"min=" + FmtDouble(minVal) + L", max=" + FmtDouble(maxVal)) : L"" });
        }
    }
    FinalizeSheetCols(dataSh);
    outSheets.push_back(std::move(dataSh));
    return true;
}

} // namespace AiDataDetail

inline bool ParseNumPyNpy(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
        std::vector<SpreadsheetSheet> sheets;
        if (!AiDataDetail::ParseSingleNpyBuffer(raw.data(), raw.size(), L"NumPy_Array", sheets, true)) return false;
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"NumPy N-Dimensional Tensor (.npy)";
        out.sheets = std::move(sheets);
        out.valid = true;
        return true;

}

} // namespace Formats
