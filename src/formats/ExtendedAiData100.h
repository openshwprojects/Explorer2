#pragma once
// ExtendedAiData100.h — Binary Decoders for AI/ML Models, Tensors, Columnar IPC & Statistical Datasets
// Covers: .npy, .npz, .ipynb, .cbor, .msgpack/.mpk, .onnx, .pt/.pth/.ckpt, .tflite,
//         .feather/.arrow/.ipc, .orc, .h5/.hdf5, .mat, .sav, .dta, .sas7bdat, .rds, .duckdb

namespace Formats {

namespace AiDataDetail {

inline uint16_t RdU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint16_t RdU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
inline int16_t RdI16LE(const uint8_t* p) { return static_cast<int16_t>(RdU16LE(p)); }
inline int16_t RdI16BE(const uint8_t* p) { return static_cast<int16_t>(RdU16BE(p)); }

inline uint32_t RdU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
inline uint32_t RdU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}
inline int32_t RdI32LE(const uint8_t* p) { return static_cast<int32_t>(RdU32LE(p)); }
inline int32_t RdI32BE(const uint8_t* p) { return static_cast<int32_t>(RdU32BE(p)); }

inline uint64_t RdU64LE(const uint8_t* p) {
    return static_cast<uint64_t>(RdU32LE(p)) | (static_cast<uint64_t>(RdU32LE(p + 4)) << 32);
}
inline uint64_t RdU64BE(const uint8_t* p) {
    return (static_cast<uint64_t>(RdU32BE(p)) << 32) | static_cast<uint64_t>(RdU32BE(p + 4));
}
inline int64_t RdI64LE(const uint8_t* p) { return static_cast<int64_t>(RdU64LE(p)); }
inline int64_t RdI64BE(const uint8_t* p) { return static_cast<int64_t>(RdU64BE(p)); }

inline float RdF32LE(const uint8_t* p) {
    uint32_t u = RdU32LE(p);
    float f = 0.0f;
    std::memcpy(&f, &u, 4);
    return f;
}
inline float RdF32BE(const uint8_t* p) {
    uint32_t u = RdU32BE(p);
    float f = 0.0f;
    std::memcpy(&f, &u, 4);
    return f;
}
inline double RdF64LE(const uint8_t* p) {
    uint64_t u = RdU64LE(p);
    double d = 0.0;
    std::memcpy(&d, &u, 8);
    return d;
}
inline double RdF64BE(const uint8_t* p) {
    uint64_t u = RdU64BE(p);
    double d = 0.0;
    std::memcpy(&d, &u, 8);
    return d;
}

inline float HalfToFloat(uint16_t h) {
    uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    uint32_t exp  = (h >> 10) & 0x1Fu;
    uint32_t mant = h & 0x03FFu;
    uint32_t outBits = 0;
    if (exp == 0) {
        if (mant == 0) {
            outBits = sign;
        } else {
            exp = 1;
            while ((mant & 0x0400u) == 0) {
                mant <<= 1;
                --exp;
            }
            mant &= 0x03FFu;
            outBits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        outBits = sign | 0x7F800000u | (mant << 13);
    } else {
        outBits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
    }
    float f = 0.0f;
    std::memcpy(&f, &outBits, 4);
    return f;
}

inline float BFloat16ToFloat(uint16_t bf) {
    uint32_t u = static_cast<uint32_t>(bf) << 16;
    float f = 0.0f;
    std::memcpy(&f, &u, 4);
    return f;
}

inline std::wstring FmtDouble(double v) {
    if (std::isnan(v)) return L"NaN";
    if (std::isinf(v)) return v < 0 ? L"-Inf" : L"Inf";
    wchar_t buf[64] = {};
    double av = std::fabs(v);
    if (av == 0.0) return L"0";
    if (av >= 1e-4 && av < 1e9 && std::floor(v) == v) {
        swprintf_s(buf, L"%.0f", v);
    } else {
        swprintf_s(buf, L"%.6g", v);
    }
    return buf;
}

inline std::wstring CleanAsciiFixed(const uint8_t* p, size_t n) {
    size_t end = n;
    while (end > 0 && (p[end - 1] == 0 || p[end - 1] == ' ' || p[end - 1] == '\r' || p[end - 1] == '\n')) {
        --end;
    }
    std::string s;
    s.reserve(end);
    for (size_t i = 0; i < end; ++i) {
        if (p[i] == 0) break;
        s.push_back(p[i] >= 32 || p[i] == '\t' ? static_cast<char>(p[i]) : '?');
    }
    return PathUtils::Utf8ToWide(s);
}

inline void FinalizeSheetCols(SpreadsheetSheet& sh) {
    size_t mc = 0;
    for (const auto& r : sh.rows) mc = std::max(mc, r.size());
    sh.maxCols = mc;
}

// ============================================================================
// Minimal Protobuf Wire Reader (for .onnx and .orc)
// ============================================================================
struct PbReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;

    PbReader(const uint8_t* d, size_t s) : data(d), size(s), pos(0) {}

    bool ReadVarint(uint64_t& out) {
        out = 0;
        int shift = 0;
        while (pos < size && shift < 70) {
            uint8_t b = data[pos++];
            out |= static_cast<uint64_t>(b & 0x7Fu) << shift;
            if ((b & 0x80u) == 0) return true;
            shift += 7;
        }
        return false;
    }

    bool ReadTag(uint32_t& fieldNum, uint8_t& wireType) {
        uint64_t tag = 0;
        if (!ReadVarint(tag) || tag == 0) return false;
        wireType = static_cast<uint8_t>(tag & 0x07u);
        fieldNum = static_cast<uint32_t>(tag >> 3);
        return fieldNum > 0;
    }

    bool ReadLengthDelimited(const uint8_t*& ptr, size_t& len) {
        uint64_t n = 0;
        if (!ReadVarint(n) || n > size - pos) return false;
        ptr = data + pos;
        len = static_cast<size_t>(n);
        pos += len;
        return true;
    }

    bool ReadString(std::string& out) {
        const uint8_t* p = nullptr;
        size_t n = 0;
        if (!ReadLengthDelimited(p, n)) return false;
        out.assign(reinterpret_cast<const char*>(p), n);
        return true;
    }

    bool SkipField(uint8_t wireType) {
        if (wireType == 0) {
            uint64_t v = 0;
            return ReadVarint(v);
        }
        if (wireType == 1) {
            if (pos + 8 > size) return false;
            pos += 8;
            return true;
        }
        if (wireType == 2) {
            const uint8_t* p = nullptr;
            size_t n = 0;
            return ReadLengthDelimited(p, n);
        }
        if (wireType == 5) {
            if (pos + 4 > size) return false;
            pos += 4;
            return true;
        }
        return false;
    }
};

// ============================================================================
// Minimal Bounds-Checked FlatBuffers Table Reader (for .tflite & .arrow/.feather)
// ============================================================================
struct FbTable {
    const uint8_t* buf = nullptr;
    size_t len = 0;
    size_t tablePos = 0;
    size_t vtablePos = 0;
    uint16_t vtableSize = 0;

    bool Init(const uint8_t* b, size_t l, size_t tPos) {
        buf = b;
        len = l;
        tablePos = tPos;
        if (!buf || tablePos + 4 > len) return false;
        int32_t vtRel = RdI32LE(buf + tablePos);
        int64_t vtAbs = static_cast<int64_t>(tablePos) - static_cast<int64_t>(vtRel);
        if (vtAbs < 0 || static_cast<size_t>(vtAbs) + 4 > len) return false;
        vtablePos = static_cast<size_t>(vtAbs);
        vtableSize = RdU16LE(buf + vtablePos);
        if (vtableSize < 4 || vtablePos + vtableSize > len) return false;
        return true;
    }

    size_t FieldPos(uint16_t slot) const {
        uint32_t vtOff = 4u + static_cast<uint32_t>(slot) * 2u;
        if (vtOff + 2 > vtableSize) return 0;
        uint16_t fieldRel = RdU16LE(buf + vtablePos + vtOff);
        if (fieldRel == 0 || tablePos + fieldRel > len) return 0;
        return tablePos + fieldRel;
    }

    uint8_t GetU8(uint16_t slot, uint8_t def = 0) const {
        size_t p = FieldPos(slot);
        return (p && p + 1 <= len) ? buf[p] : def;
    }
    int8_t GetI8(uint16_t slot, int8_t def = 0) const {
        return static_cast<int8_t>(GetU8(slot, static_cast<uint8_t>(def)));
    }
    uint16_t GetU16(uint16_t slot, uint16_t def = 0) const {
        size_t p = FieldPos(slot);
        return (p && p + 2 <= len) ? RdU16LE(buf + p) : def;
    }
    int16_t GetI16(uint16_t slot, int16_t def = 0) const {
        return static_cast<int16_t>(GetU16(slot, static_cast<uint16_t>(def)));
    }
    uint32_t GetU32(uint16_t slot, uint32_t def = 0) const {
        size_t p = FieldPos(slot);
        return (p && p + 4 <= len) ? RdU32LE(buf + p) : def;
    }
    int32_t GetI32(uint16_t slot, int32_t def = 0) const {
        return static_cast<int32_t>(GetU32(slot, static_cast<uint32_t>(def)));
    }
    uint64_t GetU64(uint16_t slot, uint64_t def = 0) const {
        size_t p = FieldPos(slot);
        return (p && p + 8 <= len) ? RdU64LE(buf + p) : def;
    }
    int64_t GetI64(uint16_t slot, int64_t def = 0) const {
        return static_cast<int64_t>(GetU64(slot, static_cast<uint64_t>(def)));
    }

    size_t IndirectPos(uint16_t slot) const {
        size_t p = FieldPos(slot);
        if (!p || p + 4 > len) return 0;
        uint32_t uoff = RdU32LE(buf + p);
        if (uoff == 0 || uoff > len - p) return 0;
        return p + uoff;
    }

    std::string GetString(uint16_t slot) const {
        size_t ip = IndirectPos(slot);
        if (!ip || ip + 4 > len) return {};
        uint32_t slen = RdU32LE(buf + ip);
        if (slen > len - (ip + 4)) return {};
        return std::string(reinterpret_cast<const char*>(buf + ip + 4), slen);
    }

    size_t GetVector(uint16_t slot, uint32_t& outCount) const {
        outCount = 0;
        size_t ip = IndirectPos(slot);
        if (!ip || ip + 4 > len) return 0;
        outCount = RdU32LE(buf + ip);
        return ip + 4;
    }

    bool GetTable(uint16_t slot, FbTable& out) const {
        size_t ip = IndirectPos(slot);
        if (!ip) return false;
        return out.Init(buf, len, ip);
    }
};

inline bool GetFbRootTable(const uint8_t* buf, size_t len, FbTable& out) {
    if (!buf || len < 8) return false;
    uint32_t rootOff = RdU32LE(buf);
    if (rootOff < 4 || rootOff + 4 > len) return false;
    return out.Init(buf, len, rootOff);
}

// ============================================================================
// 1. NumPy .npy & .npz Decoder
// ============================================================================
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

inline bool ParseNumPyNpz(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 22 || raw[0] != 'P' || raw[1] != 'K') return false;
    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromMemory(raw.data(), raw.size(), entries) || entries.empty()) return false;

    SpreadsheetSheet summary;
    summary.name = L"NPZ_Archive";
    summary.rows.push_back({ L"Array Key", L"Archive Entry", L"Compressed", L"Uncompressed" });

    std::vector<SpreadsheetSheet> arraySheets;
    for (const auto& e : entries) {
        if (e.isDirectory) continue;
        std::wstring ext = PathUtils::ToLower(PathUtils::GetExtension(e.pathInArchive));
        if (ext != L".npy") continue;
        std::vector<uint8_t> npyBytes;
        if (!ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), e, npyBytes) || npyBytes.empty()) continue;

        std::wstring key = e.name.empty() ? e.pathInArchive : e.name;
        if (key.size() > 4 && PathUtils::ToLower(key.substr(key.size() - 4)) == L".npy") {
            key.resize(key.size() - 4);
        }
        if (ParseSingleNpyBuffer(npyBytes.data(), npyBytes.size(), key, arraySheets, false)) {
            summary.rows.push_back({
                key,
                e.pathInArchive,
                PathUtils::FormatByteSize(e.compressedSize),
                PathUtils::FormatByteSize(e.uncompressedSize)
            });
        }
        if (arraySheets.size() >= 24) break;
    }

    if (arraySheets.empty()) return false;
    FinalizeSheetCols(summary);
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"NumPy Compressed Tensor Archive (.npz)";
    out.sheets.push_back(std::move(summary));
    for (auto& s : arraySheets) out.sheets.push_back(std::move(s));
    out.valid = true;
    return true;
}

// ============================================================================
// 2. Jupyter Notebook (.ipynb) Strict JSON Parser
// ============================================================================
struct MiniJsonVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    std::string str;
    double num = 0.0;
    bool bval = false;
    std::vector<MiniJsonVal> arr;
    std::vector<std::pair<std::string, MiniJsonVal>> obj;

    const MiniJsonVal* Find(std::string_view k) const {
        if (type != Obj) return nullptr;
        for (const auto& kv : obj) {
            if (kv.first == k) return &kv.second;
        }
        return nullptr;
    }
};

struct MiniJsonParser {
    const char* s = nullptr;
    size_t len = 0;
    size_t pos = 0;

    void SkipWs() {
        while (pos < len && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n')) ++pos;
    }

    bool ParseString(std::string& out) {
        SkipWs();
        if (pos >= len || s[pos] != '"') return false;
        ++pos;
        out.clear();
        while (pos < len) {
            char c = s[pos++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos >= len) return false;
                char e = s[pos++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        if (pos + 4 > len) return false;
                        char hex[5] = { s[pos], s[pos + 1], s[pos + 2], s[pos + 3], 0 };
                        pos += 4;
                        uint32_t cp = static_cast<uint32_t>(std::strtoul(hex, nullptr, 16));
                        if (cp < 0x80u) out.push_back(static_cast<char>(cp));
                        else if (cp < 0x800u) {
                            out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
                            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                        } else {
                            out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
                            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                        }
                        break;
                    }
                    default: out.push_back(e); break;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool ParseVal(MiniJsonVal& out, int depth = 0) {
        if (depth > 64) return false;
        SkipWs();
        if (pos >= len) return false;
        char c = s[pos];
        if (c == '"') {
            out.type = MiniJsonVal::Str;
            return ParseString(out.str);
        }
        if (c == '{') {
            out.type = MiniJsonVal::Obj;
            ++pos;
            SkipWs();
            if (pos < len && s[pos] == '}') { ++pos; return true; }
            while (pos < len) {
                std::string k;
                if (!ParseString(k)) return false;
                SkipWs();
                if (pos >= len || s[pos] != ':') return false;
                ++pos;
                MiniJsonVal v;
                if (!ParseVal(v, depth + 1)) return false;
                out.obj.emplace_back(std::move(k), std::move(v));
                SkipWs();
                if (pos < len && s[pos] == ',') { ++pos; continue; }
                if (pos < len && s[pos] == '}') { ++pos; return true; }
                return false;
            }
            return false;
        }
        if (c == '[') {
            out.type = MiniJsonVal::Arr;
            ++pos;
            SkipWs();
            if (pos < len && s[pos] == ']') { ++pos; return true; }
            while (pos < len) {
                MiniJsonVal v;
                if (!ParseVal(v, depth + 1)) return false;
                out.arr.push_back(std::move(v));
                SkipWs();
                if (pos < len && s[pos] == ',') { ++pos; continue; }
                if (pos < len && s[pos] == ']') { ++pos; return true; }
                return false;
            }
            return false;
        }
        if (c == 't' && pos + 4 <= len && std::memcmp(s + pos, "true", 4) == 0) {
            out.type = MiniJsonVal::Bool; out.bval = true; pos += 4; return true;
        }
        if (c == 'f' && pos + 5 <= len && std::memcmp(s + pos, "false", 5) == 0) {
            out.type = MiniJsonVal::Bool; out.bval = false; pos += 5; return true;
        }
        if (c == 'n' && pos + 4 <= len && std::memcmp(s + pos, "null", 4) == 0) {
            out.type = MiniJsonVal::Null; pos += 4; return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            char* endPtr = nullptr;
            out.num = std::strtod(s + pos, &endPtr);
            if (endPtr == s + pos) return false;
            out.type = MiniJsonVal::Num;
            pos = static_cast<size_t>(endPtr - s);
            return true;
        }
        return false;
    }
};

inline bool ParseIpynbNotebook(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    size_t start = 0;
    if (raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) start = 3;
    MiniJsonParser jp{ reinterpret_cast<const char*>(raw.data() + start), raw.size() - start, 0 };
    MiniJsonVal root;
    if (!jp.ParseVal(root)) return false;
    jp.SkipWs();
    if (jp.pos != jp.len || root.type != MiniJsonVal::Obj) return false;

    const MiniJsonVal* cells = root.Find("cells");
    const MiniJsonVal* nbf = root.Find("nbformat");
    if (!cells && !nbf) return false;

    out.kind = OfficeFileKind::WordDocx;
    out.isSpreadsheet = false;
    out.formatTitle = L"Jupyter Interactive Notebook (.ipynb)";

    std::wstring hdr = L"Jupyter Interactive Notebook";
    if (nbf && nbf->type == MiniJsonVal::Num) {
        hdr += L" (nbformat v" + std::to_wstring(static_cast<int>(nbf->num));
        const MiniJsonVal* nbfMin = root.Find("nbformat_minor");
        if (nbfMin && nbfMin->type == MiniJsonVal::Num) hdr += L"." + std::to_wstring(static_cast<int>(nbfMin->num));
        hdr += L")";
    }
    out.paragraphs.push_back({ hdr, 1, false, false });

    if (const MiniJsonVal* meta = root.Find("metadata")) {
        if (const MiniJsonVal* ks = meta->Find("kernelspec")) {
            const MiniJsonVal* dn = ks->Find("display_name");
            const MiniJsonVal* lang = ks->Find("language");
            std::string kinfo;
            if (dn && dn->type == MiniJsonVal::Str) kinfo += "Kernel: " + dn->str;
            if (lang && lang->type == MiniJsonVal::Str) {
                if (!kinfo.empty()) kinfo += " | ";
                kinfo += "Language: " + lang->str;
            }
            if (!kinfo.empty()) out.paragraphs.push_back({ PathUtils::Utf8ToWide(kinfo), 3, false, false });
        }
    }

    auto joinLines = [](const MiniJsonVal* v) -> std::string {
        if (!v) return {};
        if (v->type == MiniJsonVal::Str) return v->str;
        if (v->type == MiniJsonVal::Arr) {
            std::string res;
            for (const auto& item : v->arr) {
                if (item.type == MiniJsonVal::Str) res += item.str;
            }
            return res;
        }
        return {};
    };

    if (cells && cells->type == MiniJsonVal::Arr) {
        size_t idx = 1;
        for (const auto& cell : cells->arr) {
            if (cell.type != MiniJsonVal::Obj) continue;
            const MiniJsonVal* ct = cell.Find("cell_type");
            std::string ctype = (ct && ct->type == MiniJsonVal::Str) ? ct->str : "code";
            out.paragraphs.push_back({ L"Cell [" + std::to_wstring(idx++) + L"] — " + PathUtils::Utf8ToWide(ctype), 2, false, false });

            std::string src = joinLines(cell.Find("source"));
            if (!src.empty()) {
                out.paragraphs.push_back({ PathUtils::Trim(PathUtils::Utf8ToWide(src)), 0, false, false });
            }

            if (const MiniJsonVal* outs = cell.Find("outputs")) {
                if (outs->type == MiniJsonVal::Arr) {
                    for (const auto& o : outs->arr) {
                        std::string txt = joinLines(o.Find("text"));
                        if (!txt.empty()) {
                            out.paragraphs.push_back({ L"Output: " + PathUtils::Trim(PathUtils::Utf8ToWide(txt)), 0, true, false });
                        }
                    }
                }
            }
            if (out.paragraphs.size() >= 300) break;
        }
    }

    size_t words = 0, chars = 0;
    for (const auto& p : out.paragraphs) {
        chars += p.text.size();
        bool inW = false;
        for (wchar_t ch : p.text) {
            if (ch > 32) { if (!inW) { ++words; inW = true; } }
            else inW = false;
        }
    }
    out.wordCount = words;
    out.charCount = chars;
    out.valid = !out.paragraphs.empty();
    return out.valid;
}

// ============================================================================
// 3. Shared Tree Node for CBOR (RFC 8949) & MessagePack
// ============================================================================
struct BinTreeNode {
    enum Kind { Null, Bool, UInt, Int, Float, Str, Bytes, Array, Map } kind = Null;
    std::wstring typeLabel;
    bool bval = false;
    uint64_t uval = 0;
    int64_t ival = 0;
    double fval = 0.0;
    std::string str;
    std::vector<uint8_t> bytes;
    std::vector<BinTreeNode> children;
    std::vector<std::pair<BinTreeNode, BinTreeNode>> entries;

    std::wstring ScalarSummary() const {
        switch (kind) {
            case Null:  return typeLabel == L"undefined" ? L"undefined" : L"null";
            case Bool:  return bval ? L"true" : L"false";
            case UInt:  return std::to_wstring(uval);
            case Int:   return std::to_wstring(ival);
            case Float: return FmtDouble(fval);
            case Str:   return PathUtils::Utf8ToWide(str);
            case Bytes: {
                std::wstring h = L"0x";
                size_t cap = std::min<size_t>(bytes.size(), 24);
                wchar_t b[8] = {};
                for (size_t i = 0; i < cap; ++i) {
                    swprintf_s(b, L"%02X", bytes[i]);
                    h += b;
                }
                if (bytes.size() > cap) h += L"... (" + std::to_wstring(bytes.size()) + L" B)";
                return h;
            }
            case Array: return L"[" + std::to_wstring(children.size()) + L" items]";
            case Map:   return L"{" + std::to_wstring(entries.size()) + L" keys}";
        }
        return L"";
    }
};

inline void FlattenBinTree(const BinTreeNode& node, const std::wstring& path, SpreadsheetSheet& sh, int depth = 0) {
    if (sh.rows.size() >= 600 || depth > 24) return;
    if (node.kind == BinTreeNode::Map) {
        if (node.entries.empty()) {
            sh.rows.push_back({ path.empty() ? L"(root)" : path, node.typeLabel, L"{}" });
            return;
        }
        for (const auto& kv : node.entries) {
            std::wstring k = kv.first.ScalarSummary();
            std::wstring sub = path.empty() ? k : (path + L"." + k);
            FlattenBinTree(kv.second, sub, sh, depth + 1);
            if (sh.rows.size() >= 600) break;
        }
    } else if (node.kind == BinTreeNode::Array) {
        if (node.children.empty()) {
            sh.rows.push_back({ path.empty() ? L"(root)" : path, node.typeLabel, L"[]" });
            return;
        }
        for (size_t i = 0; i < node.children.size(); ++i) {
            std::wstring sub = (path.empty() ? L"" : path) + L"[" + std::to_wstring(i) + L"]";
            FlattenBinTree(node.children[i], sub, sh, depth + 1);
            if (sh.rows.size() >= 600) break;
        }
    } else {
        sh.rows.push_back({ path.empty() ? L"(root)" : path, node.typeLabel, node.ScalarSummary() });
    }
}

inline void PopulateSheetsFromBinTree(
    const std::vector<BinTreeNode>& roots,
    const std::wstring& prefix,
    std::vector<SpreadsheetSheet>& outSheets)
{
    // Check if root is an array of maps or multiple top-level maps -> tabular sheet
    const std::vector<BinTreeNode>* mapList = nullptr;
    if (roots.size() == 1 && roots[0].kind == BinTreeNode::Array && !roots[0].children.empty() &&
        roots[0].children[0].kind == BinTreeNode::Map)
    {
        mapList = &roots[0].children;
    } else if (roots.size() > 1 && roots[0].kind == BinTreeNode::Map) {
        mapList = &roots;
    }

    if (mapList) {
        std::vector<std::wstring> cols;
        std::set<std::wstring> seen;
        for (const auto& item : *mapList) {
            if (item.kind != BinTreeNode::Map) continue;
            for (const auto& kv : item.entries) {
                std::wstring k = kv.first.ScalarSummary();
                if (seen.insert(k).second) cols.push_back(k);
                if (cols.size() >= 48) break;
            }
            if (cols.size() >= 48) break;
        }
        if (!cols.empty()) {
            SpreadsheetSheet tbl;
            tbl.name = prefix + L"_Table";
            tbl.rows.push_back(cols);
            size_t rcap = std::min<size_t>(mapList->size(), 500);
            for (size_t r = 0; r < rcap; ++r) {
                const auto& item = (*mapList)[r];
                if (item.kind != BinTreeNode::Map) continue;
                std::vector<std::wstring> row(cols.size());
                for (const auto& kv : item.entries) {
                    std::wstring k = kv.first.ScalarSummary();
                    auto it = std::find(cols.begin(), cols.end(), k);
                    if (it != cols.end()) {
                        row[static_cast<size_t>(it - cols.begin())] = kv.second.ScalarSummary();
                    }
                }
                tbl.rows.push_back(std::move(row));
            }
            FinalizeSheetCols(tbl);
            outSheets.push_back(std::move(tbl));
        }
    }

    SpreadsheetSheet flat;
    flat.name = prefix + L"_Fields";
    flat.rows.push_back({ L"Field Path", L"Wire Type", L"Decoded Value" });
    if (roots.size() == 1) {
        FlattenBinTree(roots[0], L"", flat, 0);
    } else {
        for (size_t i = 0; i < roots.size() && flat.rows.size() < 600; ++i) {
            FlattenBinTree(roots[i], L"[" + std::to_wstring(i) + L"]", flat, 0);
        }
    }
    FinalizeSheetCols(flat);
    outSheets.push_back(std::move(flat));
}

// --- RFC 8949 CBOR Decoder ---
struct CborDecoder {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    size_t totalItems = 0;

    bool ReadArg(uint8_t ai, uint64_t& val, bool& indef) {
        indef = false;
        val = 0;
        if (ai < 24) { val = ai; return true; }
        if (ai == 24) { if (pos + 1 > size) return false; val = data[pos++]; return true; }
        if (ai == 25) { if (pos + 2 > size) return false; val = RdU16BE(data + pos); pos += 2; return true; }
        if (ai == 26) { if (pos + 4 > size) return false; val = RdU32BE(data + pos); pos += 4; return true; }
        if (ai == 27) { if (pos + 8 > size) return false; val = RdU64BE(data + pos); pos += 8; return true; }
        if (ai == 31) { indef = true; return true; }
        return false; // 28..30 reserved/invalid in RFC 8949
    }

    bool Decode(BinTreeNode& out, int depth = 0) {
        if (pos >= size || depth > 48 || ++totalItems > 50000) return false;
        uint8_t ib = data[pos++];
        uint8_t major = ib >> 5;
        uint8_t ai = ib & 0x1Fu;

        uint64_t arg = 0;
        bool indef = false;
        if (!ReadArg(ai, arg, indef)) return false;

        switch (major) {
            case 0: // Unsigned integer
                if (indef) return false;
                out.kind = BinTreeNode::UInt;
                out.typeLabel = L"uint";
                out.uval = arg;
                return true;

            case 1: // Negative integer (-1 - arg)
                if (indef) return false;
                out.kind = BinTreeNode::Int;
                out.typeLabel = L"nint";
                out.ival = (arg <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
                    ? (-1 - static_cast<int64_t>(arg))
                    : std::numeric_limits<int64_t>::min();
                return true;

            case 2: // Byte string
                out.kind = BinTreeNode::Bytes;
                out.typeLabel = L"bstr";
                if (!indef) {
                    if (arg > size - pos) return false;
                    out.bytes.assign(data + pos, data + pos + static_cast<size_t>(arg));
                    pos += static_cast<size_t>(arg);
                    return true;
                }
                while (pos < size) {
                    if (data[pos] == 0xFFu) { ++pos; return true; }
                    BinTreeNode chunk;
                    if (!Decode(chunk, depth + 1) || chunk.kind != BinTreeNode::Bytes) return false;
                    out.bytes.insert(out.bytes.end(), chunk.bytes.begin(), chunk.bytes.end());
                }
                return false;

            case 3: // UTF-8 text string
                out.kind = BinTreeNode::Str;
                out.typeLabel = L"tstr";
                if (!indef) {
                    if (arg > size - pos) return false;
                    out.str.assign(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(arg));
                    pos += static_cast<size_t>(arg);
                    return true;
                }
                while (pos < size) {
                    if (data[pos] == 0xFFu) { ++pos; return true; }
                    BinTreeNode chunk;
                    if (!Decode(chunk, depth + 1) || chunk.kind != BinTreeNode::Str) return false;
                    out.str += chunk.str;
                }
                return false;

            case 4: // Array
                out.kind = BinTreeNode::Array;
                out.typeLabel = L"array";
                if (!indef) {
                    if (arg > 65536) return false;
                    out.children.reserve(static_cast<size_t>(std::min<uint64_t>(arg, 256)));
                    for (uint64_t i = 0; i < arg; ++i) {
                        BinTreeNode elem;
                        if (!Decode(elem, depth + 1)) return false;
                        out.children.push_back(std::move(elem));
                    }
                    return true;
                }
                while (pos < size) {
                    if (data[pos] == 0xFFu) { ++pos; return true; }
                    BinTreeNode elem;
                    if (!Decode(elem, depth + 1)) return false;
                    out.children.push_back(std::move(elem));
                }
                return false;

            case 5: // Map
                out.kind = BinTreeNode::Map;
                out.typeLabel = L"map";
                if (!indef) {
                    if (arg > 65536) return false;
                    for (uint64_t i = 0; i < arg; ++i) {
                        BinTreeNode k, v;
                        if (!Decode(k, depth + 1) || !Decode(v, depth + 1)) return false;
                        out.entries.emplace_back(std::move(k), std::move(v));
                    }
                    return true;
                }
                while (pos < size) {
                    if (data[pos] == 0xFFu) { ++pos; return true; }
                    BinTreeNode k, v;
                    if (!Decode(k, depth + 1) || !Decode(v, depth + 1)) return false;
                    out.entries.emplace_back(std::move(k), std::move(v));
                }
                return false;

            case 6: { // Semantic tag
                if (indef) return false;
                BinTreeNode inner;
                if (!Decode(inner, depth + 1)) return false;
                std::wstring tagLbl = L"tag(" + std::to_wstring(arg) + L"):" + inner.typeLabel;
                out = std::move(inner);
                out.typeLabel = tagLbl;
                return true;
            }

            case 7: // Float / Simple
                if (indef) return false; // Unmatched break byte at top level
                if (ai == 20) { out.kind = BinTreeNode::Bool; out.typeLabel = L"bool"; out.bval = false; return true; }
                if (ai == 21) { out.kind = BinTreeNode::Bool; out.typeLabel = L"bool"; out.bval = true;  return true; }
                if (ai == 22) { out.kind = BinTreeNode::Null; out.typeLabel = L"null"; return true; }
                if (ai == 23) { out.kind = BinTreeNode::Null; out.typeLabel = L"undefined"; return true; }
                if (ai == 25) {
                    out.kind = BinTreeNode::Float;
                    out.typeLabel = L"float16";
                    out.fval = static_cast<double>(HalfToFloat(static_cast<uint16_t>(arg)));
                    return true;
                }
                if (ai == 26) {
                    uint32_t u = static_cast<uint32_t>(arg);
                    float f = 0.0f;
                    std::memcpy(&f, &u, 4);
                    out.kind = BinTreeNode::Float;
                    out.typeLabel = L"float32";
                    out.fval = static_cast<double>(f);
                    return true;
                }
                if (ai == 27) {
                    double d = 0.0;
                    std::memcpy(&d, &arg, 8);
                    out.kind = BinTreeNode::Float;
                    out.typeLabel = L"float64";
                    out.fval = d;
                    return true;
                }
                if (ai <= 24) {
                    out.kind = BinTreeNode::UInt;
                    out.typeLabel = L"simple";
                    out.uval = arg;
                    return true;
                }
                return false;
        }
        return false;
    }
};

inline bool ParseCborDocument(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.empty()) return false;
    CborDecoder dec{ raw.data(), raw.size(), 0, 0 };
    std::vector<BinTreeNode> roots;
    while (dec.pos < dec.size && roots.size() < 256) {
        BinTreeNode n;
        if (!dec.Decode(n, 0)) return false;
        roots.push_back(std::move(n));
    }
    if (roots.empty() || dec.pos != dec.size) return false;
    // Reject accidental multi-scalar ASCII text masquerading as CBOR integers/strings
    if (roots.size() > 1 && roots[0].kind != BinTreeNode::Map && roots[0].kind != BinTreeNode::Array) {
        return false;
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"RFC 8949 Concise Binary Object Representation (.cbor)";
    PopulateSheetsFromBinTree(roots, L"CBOR", out.sheets);
    out.valid = !out.sheets.empty();
    return out.valid;
}

// --- MessagePack Decoder ---
struct MsgPackDecoder {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    size_t totalItems = 0;

    bool Decode(BinTreeNode& out, int depth = 0) {
        if (pos >= size || depth > 48 || ++totalItems > 50000) return false;
        uint8_t b = data[pos++];

        if (b <= 0x7Fu) {
            out.kind = BinTreeNode::UInt; out.typeLabel = L"fixint"; out.uval = b; return true;
        }
        if (b >= 0xE0u) {
            out.kind = BinTreeNode::Int; out.typeLabel = L"nfixint"; out.ival = static_cast<int8_t>(b); return true;
        }
        if ((b & 0xF0u) == 0x80u) return DecodeMap(b & 0x0Fu, out, depth);
        if ((b & 0xF0u) == 0x90u) return DecodeArray(b & 0x0Fu, out, depth);
        if ((b & 0xE0u) == 0xA0u) return DecodeStr(b & 0x1Fu, out);

        switch (b) {
            case 0xC0: out.kind = BinTreeNode::Null; out.typeLabel = L"nil"; return true;
            case 0xC1: return false; // Never-used opcode in MessagePack spec!
            case 0xC2: out.kind = BinTreeNode::Bool; out.typeLabel = L"bool"; out.bval = false; return true;
            case 0xC3: out.kind = BinTreeNode::Bool; out.typeLabel = L"bool"; out.bval = true;  return true;

            case 0xC4: if (pos + 1 > size) return false; return DecodeBin(data[pos++], out);
            case 0xC5: if (pos + 2 > size) return false; { uint16_t n = RdU16BE(data + pos); pos += 2; return DecodeBin(n, out); }
            case 0xC6: if (pos + 4 > size) return false; { uint32_t n = RdU32BE(data + pos); pos += 4; return DecodeBin(n, out); }

            case 0xC7: if (pos + 1 > size) return false; { uint8_t n = data[pos++]; return DecodeExt(n, out); }
            case 0xC8: if (pos + 2 > size) return false; { uint16_t n = RdU16BE(data + pos); pos += 2; return DecodeExt(n, out); }
            case 0xC9: if (pos + 4 > size) return false; { uint32_t n = RdU32BE(data + pos); pos += 4; return DecodeExt(n, out); }

            case 0xCA:
                if (pos + 4 > size) return false;
                out.kind = BinTreeNode::Float; out.typeLabel = L"float32"; out.fval = RdF32BE(data + pos); pos += 4; return true;
            case 0xCB:
                if (pos + 8 > size) return false;
                out.kind = BinTreeNode::Float; out.typeLabel = L"float64"; out.fval = RdF64BE(data + pos); pos += 8; return true;

            case 0xCC: if (pos + 1 > size) return false; out.kind = BinTreeNode::UInt; out.typeLabel = L"uint8";  out.uval = data[pos++]; return true;
            case 0xCD: if (pos + 2 > size) return false; out.kind = BinTreeNode::UInt; out.typeLabel = L"uint16"; out.uval = RdU16BE(data + pos); pos += 2; return true;
            case 0xCE: if (pos + 4 > size) return false; out.kind = BinTreeNode::UInt; out.typeLabel = L"uint32"; out.uval = RdU32BE(data + pos); pos += 4; return true;
            case 0xCF: if (pos + 8 > size) return false; out.kind = BinTreeNode::UInt; out.typeLabel = L"uint64"; out.uval = RdU64BE(data + pos); pos += 8; return true;

            case 0xD0: if (pos + 1 > size) return false; out.kind = BinTreeNode::Int; out.typeLabel = L"int8";  out.ival = static_cast<int8_t>(data[pos++]); return true;
            case 0xD1: if (pos + 2 > size) return false; out.kind = BinTreeNode::Int; out.typeLabel = L"int16"; out.ival = RdI16BE(data + pos); pos += 2; return true;
            case 0xD2: if (pos + 4 > size) return false; out.kind = BinTreeNode::Int; out.typeLabel = L"int32"; out.ival = RdI32BE(data + pos); pos += 4; return true;
            case 0xD3: if (pos + 8 > size) return false; out.kind = BinTreeNode::Int; out.typeLabel = L"int64"; out.ival = RdI64BE(data + pos); pos += 8; return true;

            case 0xD4: return DecodeExt(1, out);
            case 0xD5: return DecodeExt(2, out);
            case 0xD6: return DecodeExt(4, out);
            case 0xD7: return DecodeExt(8, out);
            case 0xD8: return DecodeExt(16, out);

            case 0xD9: if (pos + 1 > size) return false; return DecodeStr(data[pos++], out);
            case 0xDA: if (pos + 2 > size) return false; { uint16_t n = RdU16BE(data + pos); pos += 2; return DecodeStr(n, out); }
            case 0xDB: if (pos + 4 > size) return false; { uint32_t n = RdU32BE(data + pos); pos += 4; return DecodeStr(n, out); }

            case 0xDC: if (pos + 2 > size) return false; { uint16_t n = RdU16BE(data + pos); pos += 2; return DecodeArray(n, out, depth); }
            case 0xDD: if (pos + 4 > size) return false; { uint32_t n = RdU32BE(data + pos); pos += 4; return DecodeArray(n, out, depth); }

            case 0xDE: if (pos + 2 > size) return false; { uint16_t n = RdU16BE(data + pos); pos += 2; return DecodeMap(n, out, depth); }
            case 0xDF: if (pos + 4 > size) return false; { uint32_t n = RdU32BE(data + pos); pos += 4; return DecodeMap(n, out, depth); }
        }
        return false;
    }

    bool DecodeStr(uint32_t n, BinTreeNode& out) {
        if (n > size - pos) return false;
        out.kind = BinTreeNode::Str;
        out.typeLabel = L"str";
        out.str.assign(reinterpret_cast<const char*>(data + pos), n);
        pos += n;
        return true;
    }

    bool DecodeBin(uint32_t n, BinTreeNode& out) {
        if (n > size - pos) return false;
        out.kind = BinTreeNode::Bytes;
        out.typeLabel = L"bin";
        out.bytes.assign(data + pos, data + pos + n);
        pos += n;
        return true;
    }

    bool DecodeExt(uint32_t n, BinTreeNode& out) {
        if (pos >= size || n > size - pos - 1) return false;
        int8_t extType = static_cast<int8_t>(data[pos++]);
        out.kind = BinTreeNode::Bytes;
        out.typeLabel = L"ext(" + std::to_wstring(extType) + L")";
        out.bytes.assign(data + pos, data + pos + n);
        pos += n;
        return true;
    }

    bool DecodeArray(uint32_t n, BinTreeNode& out, int depth) {
        if (n > 65536) return false;
        out.kind = BinTreeNode::Array;
        out.typeLabel = L"array";
        out.children.reserve(std::min<uint32_t>(n, 256));
        for (uint32_t i = 0; i < n; ++i) {
            BinTreeNode e;
            if (!Decode(e, depth + 1)) return false;
            out.children.push_back(std::move(e));
        }
        return true;
    }

    bool DecodeMap(uint32_t n, BinTreeNode& out, int depth) {
        if (n > 65536) return false;
        out.kind = BinTreeNode::Map;
        out.typeLabel = L"map";
        for (uint32_t i = 0; i < n; ++i) {
            BinTreeNode k, v;
            if (!Decode(k, depth + 1) || !Decode(v, depth + 1)) return false;
            out.entries.emplace_back(std::move(k), std::move(v));
        }
        return true;
    }
};

inline bool ParseMsgPackDocument(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (raw.empty()) return false;
    MsgPackDecoder dec{ raw.data(), raw.size(), 0, 0 };
    std::vector<BinTreeNode> roots;
    while (dec.pos < dec.size && roots.size() < 256) {
        BinTreeNode n;
        if (!dec.Decode(n, 0)) return false;
        roots.push_back(std::move(n));
    }
    if (roots.empty() || dec.pos != dec.size) return false;
    // Plain ASCII text consists of bytes 0x20..0x7E which are positive fixints in MsgPack; reject multi-fixint ASCII!
    if (roots.size() > 1 && roots[0].kind != BinTreeNode::Map && roots[0].kind != BinTreeNode::Array) {
        return false;
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"MessagePack Binary Object (" + extLower + L")";
    PopulateSheetsFromBinTree(roots, L"MsgPack", out.sheets);
    out.valid = !out.sheets.empty();
    return out.valid;
}

// ============================================================================
// 4. ONNX Neural Network Protobuf Decoder (.onnx)
// ============================================================================
inline const wchar_t* OnnxDataTypeName(uint64_t dt) {
    switch (dt) {
        case 1:  return L"FLOAT32";
        case 2:  return L"UINT8";
        case 3:  return L"INT8";
        case 4:  return L"UINT16";
        case 5:  return L"INT16";
        case 6:  return L"INT32";
        case 7:  return L"INT64";
        case 8:  return L"STRING";
        case 9:  return L"BOOL";
        case 10: return L"FLOAT16";
        case 11: return L"FLOAT64";
        case 12: return L"UINT32";
        case 13: return L"UINT64";
        case 14: return L"COMPLEX64";
        case 15: return L"COMPLEX128";
        case 16: return L"BFLOAT16";
        case 17: return L"FLOAT8E4M3FN";
        case 18: return L"FLOAT8E4M3FNUZ";
        case 19: return L"FLOAT8E5M2";
        case 20: return L"FLOAT8E5M2FNUZ";
        case 21: return L"UINT4";
        case 22: return L"INT4";
        default: return L"TENSOR";
    }
}

inline bool ParseOnnxValueInfo(const uint8_t* ptr, size_t len, std::wstring& outName, std::wstring& outDtype, std::wstring& outShape) {
    PbReader r(ptr, len);
    std::string name;
    uint64_t elemType = 0;
    std::vector<std::wstring> dims;
    uint32_t fn = 0; uint8_t wt = 0;
    while (r.pos < r.size) {
        if (!r.ReadTag(fn, wt)) return false;
        if (fn == 1 && wt == 2) {
            if (!r.ReadString(name)) return false;
        } else if (fn == 2 && wt == 2) { // TypeProto
            const uint8_t* tp = nullptr; size_t tl = 0;
            if (!r.ReadLengthDelimited(tp, tl)) return false;
            PbReader tr(tp, tl);
            uint32_t tfn = 0; uint8_t twt = 0;
            while (tr.pos < tr.size) {
                if (!tr.ReadTag(tfn, twt)) return false;
                if (tfn == 1 && twt == 2) { // Tensor
                    const uint8_t* ttp = nullptr; size_t ttl = 0;
                    if (!tr.ReadLengthDelimited(ttp, ttl)) return false;
                    PbReader ttr(ttp, ttl);
                    uint32_t xfn = 0; uint8_t xwt = 0;
                    while (ttr.pos < ttr.size) {
                        if (!ttr.ReadTag(xfn, xwt)) return false;
                        if (xfn == 1 && xwt == 0) {
                            if (!ttr.ReadVarint(elemType)) return false;
                        } else if (xfn == 2 && xwt == 2) { // TensorShapeProto
                            const uint8_t* sp = nullptr; size_t sl = 0;
                            if (!ttr.ReadLengthDelimited(sp, sl)) return false;
                            PbReader sr(sp, sl);
                            uint32_t sfn = 0; uint8_t swt = 0;
                            while (sr.pos < sr.size) {
                                if (!sr.ReadTag(sfn, swt)) return false;
                                if (sfn == 1 && swt == 2) { // Dimension
                                    const uint8_t* dp = nullptr; size_t dl = 0;
                                    if (!sr.ReadLengthDelimited(dp, dl)) return false;
                                    PbReader dr(dp, dl);
                                    uint32_t dfn = 0; uint8_t dwt = 0;
                                    std::wstring dimTxt = L"?";
                                    while (dr.pos < dr.size) {
                                        if (!dr.ReadTag(dfn, dwt)) return false;
                                        if (dfn == 1 && dwt == 0) {
                                            uint64_t dv = 0; if (!dr.ReadVarint(dv)) return false;
                                            dimTxt = std::to_wstring(static_cast<int64_t>(dv));
                                        } else if (dfn == 2 && dwt == 2) {
                                            std::string dpStr; if (!dr.ReadString(dpStr)) return false;
                                            dimTxt = PathUtils::Utf8ToWide(dpStr);
                                        } else if (!dr.SkipField(dwt)) return false;
                                    }
                                    dims.push_back(dimTxt);
                                } else if (!sr.SkipField(swt)) return false;
                            }
                        } else if (!ttr.SkipField(xwt)) return false;
                    }
                } else if (!tr.SkipField(twt)) return false;
            }
        } else if (!r.SkipField(wt)) return false;
    }
    outName = PathUtils::Utf8ToWide(name);
    outDtype = OnnxDataTypeName(elemType);
    outShape = L"[";
    for (size_t i = 0; i < dims.size(); ++i) {
        if (i > 0) outShape += L", ";
        outShape += dims[i];
    }
    outShape += L"]";
    return true;
}

inline bool ParseOnnxModel(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 4) return false;
    PbReader r(raw.data(), raw.size());

    uint64_t irVersion = 0;
    uint64_t modelVersion = 0;
    std::string producerName, producerVersion, domain, docString, graphName;
    std::vector<std::wstring> opsets;
    bool hasGraph = false;

    SpreadsheetSheet ioSheet;
    ioSheet.name = L"ONNX_Model_And_Tensors";
    ioSheet.rows.push_back({ L"Role / Property", L"Name", L"Data Type", L"Shape / Value" });

    SpreadsheetSheet nodeSheet;
    nodeSheet.name = L"ONNX_Graph_Nodes";
    nodeSheet.rows.push_back({ L"#", L"OpType", L"Node Name", L"Inputs", L"Outputs" });

    std::vector<std::vector<std::wstring>> tensorRows;

    uint32_t fn = 0; uint8_t wt = 0;
    while (r.pos < r.size) {
        if (!r.ReadTag(fn, wt)) return false;
        if (fn == 1 && wt == 0) {
            if (!r.ReadVarint(irVersion)) return false;
        } else if (fn == 2 && wt == 2) {
            if (!r.ReadString(producerName)) return false;
        } else if (fn == 3 && wt == 2) {
            if (!r.ReadString(producerVersion)) return false;
        } else if (fn == 4 && wt == 2) {
            if (!r.ReadString(domain)) return false;
        } else if (fn == 5 && wt == 0) {
            if (!r.ReadVarint(modelVersion)) return false;
        } else if (fn == 6 && wt == 2) {
            if (!r.ReadString(docString)) return false;
        } else if (fn == 8 && wt == 2) { // opset_import
            const uint8_t* op = nullptr; size_t ol = 0;
            if (!r.ReadLengthDelimited(op, ol)) return false;
            PbReader ordr(op, ol);
            std::string opDom; uint64_t opVer = 0;
            uint32_t ofn = 0; uint8_t owt = 0;
            while (ordr.pos < ordr.size) {
                if (!ordr.ReadTag(ofn, owt)) return false;
                if (ofn == 1 && owt == 2) { if (!ordr.ReadString(opDom)) return false; }
                else if (ofn == 2 && owt == 0) { if (!ordr.ReadVarint(opVer)) return false; }
                else if (!ordr.SkipField(owt)) return false;
            }
            opsets.push_back((opDom.empty() ? L"ai.onnx" : PathUtils::Utf8ToWide(opDom)) + L" v" + std::to_wstring(opVer));
        } else if (fn == 7 && wt == 2) { // GraphProto
            hasGraph = true;
            const uint8_t* gp = nullptr; size_t gl = 0;
            if (!r.ReadLengthDelimited(gp, gl)) return false;
            PbReader gr(gp, gl);
            uint32_t gfn = 0; uint8_t gwt = 0;
            size_t nodeIdx = 0;
            while (gr.pos < gr.size) {
                if (!gr.ReadTag(gfn, gwt)) return false;
                if (gfn == 2 && gwt == 2) {
                    if (!gr.ReadString(graphName)) return false;
                } else if (gfn == 1 && gwt == 2) { // NodeProto
                    const uint8_t* np = nullptr; size_t nl = 0;
                    if (!gr.ReadLengthDelimited(np, nl)) return false;
                    PbReader nr(np, nl);
                    std::string nName, opType;
                    std::vector<std::string> ins, outs;
                    uint32_t nfn = 0; uint8_t nwt = 0;
                    while (nr.pos < nr.size) {
                        if (!nr.ReadTag(nfn, nwt)) return false;
                        if (nfn == 1 && nwt == 2) { std::string s; if (!nr.ReadString(s)) return false; ins.push_back(std::move(s)); }
                        else if (nfn == 2 && nwt == 2) { std::string s; if (!nr.ReadString(s)) return false; outs.push_back(std::move(s)); }
                        else if (nfn == 3 && nwt == 2) { if (!nr.ReadString(nName)) return false; }
                        else if (nfn == 4 && nwt == 2) { if (!nr.ReadString(opType)) return false; }
                        else if (!nr.SkipField(nwt)) return false;
                    }
                    ++nodeIdx;
                    if (nodeSheet.rows.size() <= 500) {
                        auto joinVec = [](const std::vector<std::string>& v) {
                            std::wstring w;
                            for (size_t i = 0; i < v.size(); ++i) {
                                if (i > 0) w += L", ";
                                w += PathUtils::Utf8ToWide(v[i]);
                            }
                            return w;
                        };
                        nodeSheet.rows.push_back({
                            std::to_wstring(nodeIdx),
                            PathUtils::Utf8ToWide(opType),
                            PathUtils::Utf8ToWide(nName),
                            joinVec(ins),
                            joinVec(outs)
                        });
                    }
                } else if ((gfn == 11 || gfn == 12) && gwt == 2) { // input / output ValueInfoProto
                    const uint8_t* vp = nullptr; size_t vl = 0;
                    if (!gr.ReadLengthDelimited(vp, vl)) return false;
                    std::wstring vName, vDt, vShape;
                    if (ParseOnnxValueInfo(vp, vl, vName, vDt, vShape) && tensorRows.size() < 300) {
                        tensorRows.push_back({ gfn == 11 ? L"Graph Input" : L"Graph Output", vName, vDt, vShape });
                    }
                } else if (gfn == 5 && gwt == 2) { // initializer TensorProto
                    const uint8_t* tp = nullptr; size_t tl = 0;
                    if (!gr.ReadLengthDelimited(tp, tl)) return false;
                    PbReader tr(tp, tl);
                    std::vector<int64_t> dims;
                    uint64_t dt = 0;
                    std::string tName;
                    std::vector<float> sampleFloats;
                    uint32_t tfn = 0; uint8_t twt = 0;
                    while (tr.pos < tr.size) {
                        if (!tr.ReadTag(tfn, twt)) return false;
                        if (tfn == 1 && twt == 0) {
                            uint64_t dv = 0; if (!tr.ReadVarint(dv)) return false;
                            dims.push_back(static_cast<int64_t>(dv));
                        } else if (tfn == 1 && twt == 2) { // packed dims
                            const uint8_t* dp = nullptr; size_t dl = 0;
                            if (!tr.ReadLengthDelimited(dp, dl)) return false;
                            PbReader dr(dp, dl);
                            while (dr.pos < dr.size) {
                                uint64_t dv = 0; if (!dr.ReadVarint(dv)) return false;
                                dims.push_back(static_cast<int64_t>(dv));
                            }
                        } else if (tfn == 2 && twt == 0) {
                            if (!tr.ReadVarint(dt)) return false;
                        } else if (tfn == 4 && twt == 5) {
                            if (tr.pos + 4 > tr.size) return false;
                            if (sampleFloats.size() < 4) sampleFloats.push_back(RdF32LE(tr.data + tr.pos));
                            tr.pos += 4;
                        } else if (tfn == 4 && twt == 2) {
                            const uint8_t* fp = nullptr; size_t fl = 0;
                            if (!tr.ReadLengthDelimited(fp, fl)) return false;
                            for (size_t k = 0; k + 4 <= fl && sampleFloats.size() < 4; k += 4) {
                                sampleFloats.push_back(RdF32LE(fp + k));
                            }
                        } else if (tfn == 8 && twt == 2) {
                            if (!tr.ReadString(tName)) return false;
                        } else if (tfn == 9 && twt == 2) {
                            const uint8_t* rp = nullptr; size_t rl = 0;
                            if (!tr.ReadLengthDelimited(rp, rl)) return false;
                            if (dt == 1) {
                                for (size_t k = 0; k + 4 <= rl && sampleFloats.size() < 4; k += 4) {
                                    sampleFloats.push_back(RdF32LE(rp + k));
                                }
                            }
                        } else if (!tr.SkipField(twt)) return false;
                    }
                    if (tensorRows.size() < 300) {
                        std::wstring shStr = L"[";
                        for (size_t i = 0; i < dims.size(); ++i) {
                            if (i > 0) shStr += L", ";
                            shStr += std::to_wstring(dims[i]);
                        }
                        shStr += L"]";
                        if (!sampleFloats.empty()) {
                            shStr += L"  e.g. ";
                            for (size_t i = 0; i < sampleFloats.size(); ++i) {
                                if (i > 0) shStr += L", ";
                                shStr += FmtDouble(sampleFloats[i]);
                            }
                        }
                        tensorRows.push_back({ L"Initializer Weight", PathUtils::Utf8ToWide(tName), OnnxDataTypeName(dt), shStr });
                    }
                } else if (!gr.SkipField(gwt)) return false;
            }
        } else if (fn == 14 && wt == 2) { // metadata_props (StringStringEntryProto)
            const uint8_t* mp = nullptr; size_t ml = 0;
            if (!r.ReadLengthDelimited(mp, ml)) return false;
            PbReader mr(mp, ml);
            std::string mk, mv;
            uint32_t mfn = 0; uint8_t mwt = 0;
            while (mr.pos < mr.size) {
                if (!mr.ReadTag(mfn, mwt)) return false;
                if (mfn == 1 && mwt == 2) { if (!mr.ReadString(mk)) return false; }
                else if (mfn == 2 && mwt == 2) { if (!mr.ReadString(mv)) return false; }
                else if (!mr.SkipField(mwt)) return false;
            }
            if (!mk.empty() && tensorRows.size() < 300) {
                tensorRows.push_back({ L"Metadata Property", PathUtils::Utf8ToWide(mk), L"string", PathUtils::Utf8ToWide(mv) });
            }
        } else if (!r.SkipField(wt)) return false;
    }

    if (r.pos != r.size || irVersion == 0 || irVersion > 20 || !hasGraph ||
        (nodeSheet.rows.size() <= 1 && tensorRows.empty())) {
        return false;
    }

    ioSheet.rows.push_back({ L"IR Version", L"ir_version", L"int64", std::to_wstring(irVersion) });
    if (!producerName.empty()) {
        ioSheet.rows.push_back({ L"Producer", PathUtils::Utf8ToWide(producerName), L"string", PathUtils::Utf8ToWide(producerVersion) });
    }
    if (!graphName.empty()) {
        ioSheet.rows.push_back({ L"Graph Name", PathUtils::Utf8ToWide(graphName), L"GraphProto", std::to_wstring(nodeSheet.rows.size() - 1) + L" nodes" });
    }
    for (const auto& ops : opsets) {
        ioSheet.rows.push_back({ L"Opset Import", ops, L"OperatorSetId", L"" });
    }
    for (auto& tr : tensorRows) ioSheet.rows.push_back(std::move(tr));

    FinalizeSheetCols(ioSheet);
    FinalizeSheetCols(nodeSheet);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"ONNX Neural Network Graph (.onnx)";
    out.sheets.push_back(std::move(ioSheet));
    if (nodeSheet.rows.size() > 1) out.sheets.push_back(std::move(nodeSheet));
    out.valid = true;
    return true;
}

// ============================================================================
// 4b. GGUF Quantized LLM Container (.gguf v1, v2, v3)
// ============================================================================
struct GgufMemCursor {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    uint32_t version = 3;

    size_t Remaining() const { return (pos < size) ? (size - pos) : 0; }

    bool ReadBytes(void* dst, size_t n) {
        if (n > Remaining()) return false;
        if (n > 0) std::memcpy(dst, data + pos, n);
        pos += n;
        return true;
    }
    bool Skip(size_t n) {
        if (n > Remaining()) return false;
        pos += n;
        return true;
    }
    bool ReadU8(uint8_t& v) {
        if (pos >= size) return false;
        v = data[pos++];
        return true;
    }
    bool ReadU16(uint16_t& v) {
        if (Remaining() < 2) return false;
        v = RdU16LE(data + pos); pos += 2;
        return true;
    }
    bool ReadU32(uint32_t& v) {
        if (Remaining() < 4) return false;
        v = RdU32LE(data + pos); pos += 4;
        return true;
    }
    bool ReadU64(uint64_t& v) {
        if (Remaining() < 8) return false;
        v = RdU64LE(data + pos); pos += 8;
        return true;
    }
    bool ReadCount(uint64_t& out) {
        if (version == 1) {
            uint32_t v32 = 0;
            if (!ReadU32(v32)) return false;
            out = v32;
            return true;
        }
        return ReadU64(out);
    }
    bool ReadStr(std::string& out, size_t maxKeep = 512) {
        uint64_t len = 0;
        if (!ReadCount(len) || len > Remaining()) return false;
        const size_t keep = static_cast<size_t>(std::min<uint64_t>(len, maxKeep));
        out.assign(reinterpret_cast<const char*>(data + pos), keep);
        pos += static_cast<size_t>(len);
        return true;
    }
};

inline bool GetGgmlSheetTrait(uint32_t t, const wchar_t*& outName, uint32_t& blockElems, uint32_t& blockBytes) {
    switch (t) {
    case 0:  outName = L"F32";     blockElems = 1;   blockBytes = 4;   return true;
    case 1:  outName = L"F16";     blockElems = 1;   blockBytes = 2;   return true;
    case 2:  outName = L"Q4_0";    blockElems = 32;  blockBytes = 18;  return true;
    case 3:  outName = L"Q4_1";    blockElems = 32;  blockBytes = 20;  return true;
    case 4:  outName = L"Q4_2";    blockElems = 16;  blockBytes = 10;  return true;
    case 5:  outName = L"Q4_3";    blockElems = 16;  blockBytes = 12;  return true;
    case 6:  outName = L"Q5_0";    blockElems = 32;  blockBytes = 22;  return true;
    case 7:  outName = L"Q5_1";    blockElems = 32;  blockBytes = 24;  return true;
    case 8:  outName = L"Q8_0";    blockElems = 32;  blockBytes = 34;  return true;
    case 9:  outName = L"Q8_1";    blockElems = 32;  blockBytes = 36;  return true;
    case 10: outName = L"Q2_K";    blockElems = 256; blockBytes = 84;  return true;
    case 11: outName = L"Q3_K";    blockElems = 256; blockBytes = 110; return true;
    case 12: outName = L"Q4_K";    blockElems = 256; blockBytes = 144; return true;
    case 13: outName = L"Q5_K";    blockElems = 256; blockBytes = 176; return true;
    case 14: outName = L"Q6_K";    blockElems = 256; blockBytes = 210; return true;
    case 15: outName = L"Q8_K";    blockElems = 256; blockBytes = 292; return true;
    case 16: outName = L"IQ2_XXS"; blockElems = 256; blockBytes = 66;  return true;
    case 17: outName = L"IQ2_XS";  blockElems = 256; blockBytes = 74;  return true;
    case 18: outName = L"IQ3_XXS"; blockElems = 256; blockBytes = 98;  return true;
    case 19: outName = L"IQ1_S";   blockElems = 256; blockBytes = 50;  return true;
    case 20: outName = L"IQ4_NL";  blockElems = 32;  blockBytes = 18;  return true;
    case 21: outName = L"IQ3_S";   blockElems = 256; blockBytes = 110; return true;
    case 22: outName = L"IQ2_S";   blockElems = 256; blockBytes = 82;  return true;
    case 23: outName = L"IQ4_XS";  blockElems = 256; blockBytes = 136; return true;
    case 24: outName = L"I8";      blockElems = 1;   blockBytes = 1;   return true;
    case 25: outName = L"I16";     blockElems = 1;   blockBytes = 2;   return true;
    case 26: outName = L"I32";     blockElems = 1;   blockBytes = 4;   return true;
    case 27: outName = L"I64";     blockElems = 1;   blockBytes = 8;   return true;
    case 28: outName = L"F64";     blockElems = 1;   blockBytes = 8;   return true;
    case 29: outName = L"IQ1_M";   blockElems = 256; blockBytes = 56;  return true;
    case 30: outName = L"BF16";    blockElems = 1;   blockBytes = 2;   return true;
    case 31: outName = L"Q4_0_4_4";blockElems = 32;  blockBytes = 18;  return true;
    case 32: outName = L"Q4_0_4_8";blockElems = 32;  blockBytes = 18;  return true;
    case 33: outName = L"Q4_0_8_8";blockElems = 32;  blockBytes = 18;  return true;
    case 34: outName = L"TQ1_0";   blockElems = 256; blockBytes = 54;  return true;
    case 35: outName = L"TQ2_0";   blockElems = 256; blockBytes = 66;  return true;
    case 36: outName = L"IQ4_NL_4_4"; blockElems = 32; blockBytes = 18; return true;
    case 37: outName = L"IQ4_NL_4_8"; blockElems = 32; blockBytes = 18; return true;
    case 38: outName = L"IQ4_NL_8_8"; blockElems = 32; blockBytes = 18; return true;
    case 39: outName = L"MXFP4";   blockElems = 32;  blockBytes = 17;  return true;
    default: return false;
    }
}

inline const wchar_t* GgufSheetValTypeName(uint32_t vt) {
    switch (vt) {
    case 0: return L"uint8";   case 1: return L"int8";    case 2: return L"uint16";  case 3: return L"int16";
    case 4: return L"uint32";  case 5: return L"int32";   case 6: return L"float32"; case 7: return L"bool";
    case 8: return L"string";  case 9: return L"array";   case 10: return L"uint64"; case 11: return L"int64";
    case 12: return L"float64"; default: return L"unknown";
    }
}

inline bool ReadGgufSheetVal(GgufMemCursor& c, uint32_t vt, std::wstring& outVal, bool capture, int depth = 0) {
    if (depth > 4) return false;
    switch (vt) {
    case 0: { uint8_t v = 0; if (!c.ReadU8(v)) return false; if (capture) outVal = std::to_wstring(v); return true; }
    case 1: { uint8_t v = 0; if (!c.ReadU8(v)) return false; if (capture) outVal = std::to_wstring(static_cast<int8_t>(v)); return true; }
    case 2: { uint16_t v = 0; if (!c.ReadU16(v)) return false; if (capture) outVal = std::to_wstring(v); return true; }
    case 3: { uint16_t v = 0; if (!c.ReadU16(v)) return false; if (capture) outVal = std::to_wstring(static_cast<int16_t>(v)); return true; }
    case 4: { uint32_t v = 0; if (!c.ReadU32(v)) return false; if (capture) outVal = std::to_wstring(v); return true; }
    case 5: { uint32_t v = 0; if (!c.ReadU32(v)) return false; if (capture) outVal = std::to_wstring(static_cast<int32_t>(v)); return true; }
    case 6: {
        if (c.Remaining() < 4) return false;
        float f = RdF32LE(c.data + c.pos); c.pos += 4;
        if (capture) outVal = FmtDouble(f);
        return true;
    }
    case 7: {
        uint8_t v = 0;
        if (!c.ReadU8(v) || v > 1u) return false;
        if (capture) outVal = v ? L"true" : L"false";
        return true;
    }
    case 8: {
        std::string s;
        if (!c.ReadStr(s, capture ? 300 : 0)) return false;
        if (capture) outVal = PathUtils::Utf8ToWide(s);
        return true;
    }
    case 9: {
        uint32_t elemType = 0; uint64_t count = 0;
        if (!c.ReadU32(elemType) || elemType > 12 || !c.ReadCount(count)) return false;
        size_t fixedSz = 0;
        if (elemType <= 1 || elemType == 7) fixedSz = 1;
        else if (elemType <= 3) fixedSz = 2;
        else if (elemType <= 6) fixedSz = 4;
        else if (elemType >= 10 && elemType <= 12) fixedSz = 8;
        if (fixedSz > 0) {
            if (count > c.Remaining() / fixedSz) return false;
            if (!capture) return c.Skip(static_cast<size_t>(count * fixedSz));
            const uint64_t showN = std::min<uint64_t>(count, 6);
            std::wstring sample;
            for (uint64_t i = 0; i < showN; ++i) {
                std::wstring ev;
                if (!ReadGgufSheetVal(c, elemType, ev, true, depth + 1)) return false;
                if (i > 0) sample += L", ";
                sample += ev;
            }
            if (count > showN) {
                if (!c.Skip(static_cast<size_t>((count - showN) * fixedSz))) return false;
                outVal = L"[" + std::to_wstring(count) + L" \x00D7 " + GgufSheetValTypeName(elemType) + L"] (" + sample + L", ...)";
            } else {
                outVal = L"[" + sample + L"]";
            }
            return true;
        }
        if (count > 10000000ULL) return false;
        const uint64_t showN = capture ? std::min<uint64_t>(count, 5) : 0;
        std::wstring sample;
        for (uint64_t i = 0; i < count; ++i) {
            std::wstring ev;
            const bool want = (i < showN);
            if (!ReadGgufSheetVal(c, elemType, ev, want, depth + 1)) return false;
            if (want) {
                if (i > 0) sample += L", ";
                if (ev.size() > 24) ev = ev.substr(0, 24) + L"...";
                sample += L"\"" + ev + L"\"";
            }
        }
        if (capture) {
            outVal = (count > showN)
                ? (L"[" + std::to_wstring(count) + L" \x00D7 " + GgufSheetValTypeName(elemType) + L"] (" + sample + L", ...)")
                : (L"[" + sample + L"]");
        }
        return true;
    }
    case 10: { uint64_t v = 0; if (!c.ReadU64(v)) return false; if (capture) outVal = std::to_wstring(v); return true; }
    case 11: { uint64_t v = 0; if (!c.ReadU64(v)) return false; if (capture) outVal = std::to_wstring(static_cast<int64_t>(v)); return true; }
    case 12: {
        if (c.Remaining() < 8) return false;
        double d = RdF64LE(c.data + c.pos); c.pos += 8;
        if (capture) outVal = FmtDouble(d);
        return true;
    }
    default: return false;
    }
}

inline bool ParseGgufModel(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 16 || std::memcmp(raw.data(), "GGUF", 4) != 0) return false;
    GgufMemCursor cur{ raw.data(), raw.size(), 4, 3 };
    if (!cur.ReadU32(cur.version) || cur.version < 1 || cur.version > 3) return false;

    uint64_t tensorCount = 0, kvCount = 0;
    if (!cur.ReadCount(tensorCount) || !cur.ReadCount(kvCount)) return false;
    if (kvCount > 1000000ULL || tensorCount > 5000000ULL) return false;

    SpreadsheetSheet metaSheet;
    metaSheet.name = L"GGUF_Metadata";
    metaSheet.rows.push_back({ L"Key", L"Value Type", L"Value" });
    metaSheet.rows.push_back({ L"gguf.version", L"uint32", std::to_wstring(cur.version) });
    metaSheet.rows.push_back({ L"gguf.tensor_count", L"uint64", std::to_wstring(tensorCount) });
    metaSheet.rows.push_back({ L"gguf.kv_count", L"uint64", std::to_wstring(kvCount) });

    for (uint64_t i = 0; i < kvCount; ++i) {
        std::string key;
        if (!cur.ReadStr(key, 256) || key.empty()) return false;
        uint32_t vt = 0;
        if (!cur.ReadU32(vt) || vt > 12) return false;
        std::wstring valW;
        const bool keep = (metaSheet.rows.size() < 512);
        if (!ReadGgufSheetVal(cur, vt, valW, keep, 0)) return false;
        if (keep) {
            metaSheet.rows.push_back({ PathUtils::Utf8ToWide(key), GgufSheetValTypeName(vt), std::move(valW) });
        }
    }

    SpreadsheetSheet tensorSheet;
    tensorSheet.name = L"GGUF_Tensors";
    tensorSheet.rows.push_back({ L"#", L"Tensor Name", L"Quantization", L"Shape", L"Parameters", L"Byte Size", L"Data Offset" });

    uint64_t totalParams = 0;
    for (uint64_t t = 0; t < tensorCount; ++t) {
        std::string name;
        if (!cur.ReadStr(name, 512) || name.empty()) return false;
        uint32_t nDims = 0;
        if (!cur.ReadU32(nDims) || nDims < 1 || nDims > 8) return false;
        uint64_t dims[8] = {};
        uint64_t params = 1;
        for (uint32_t d = 0; d < nDims; ++d) {
            if (!cur.ReadU64(dims[d]) || dims[d] == 0) return false;
            if (params > UINT64_MAX / dims[d]) params = UINT64_MAX;
            else params *= dims[d];
        }
        uint32_t ggmlType = 0;
        uint64_t relOff = 0;
        if (!cur.ReadU32(ggmlType) || !cur.ReadU64(relOff)) return false;
        const wchar_t* qName = nullptr;
        uint32_t blkElems = 1, blkBytes = 1;
        if (!GetGgmlSheetTrait(ggmlType, qName, blkElems, blkBytes)) return false;
        if (relOff > raw.size()) return false;

        const uint64_t blocks = (params + blkElems - 1ULL) / blkElems;
        const uint64_t byteSz = (blocks <= UINT64_MAX / blkBytes) ? (blocks * blkBytes) : UINT64_MAX;
        if (totalParams <= UINT64_MAX - params) totalParams += params;
        else totalParams = UINT64_MAX;

        if (tensorSheet.rows.size() <= 1024) {
            std::wstring shStr = L"[";
            for (uint32_t d = 0; d < nDims; ++d) {
                if (d > 0) shStr += L", ";
                shStr += std::to_wstring(dims[d]);
            }
            shStr += L"]";
            tensorSheet.rows.push_back({
                std::to_wstring(t + 1),
                PathUtils::Utf8ToWide(name),
                qName,
                std::move(shStr),
                std::to_wstring(params),
                PathUtils::FormatByteSize(byteSz),
                std::to_wstring(relOff)
            });
        }
    }

    metaSheet.rows.insert(metaSheet.rows.begin() + 4, { L"gguf.total_parameters", L"uint64", std::to_wstring(totalParams) });
    FinalizeSheetCols(metaSheet);
    FinalizeSheetCols(tensorSheet);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"GGUF Quantized Model v" + std::to_wstring(cur.version) + L" (.gguf)";
    out.sheets.push_back(std::move(metaSheet));
    if (tensorSheet.rows.size() > 1) out.sheets.push_back(std::move(tensorSheet));
    out.valid = true;
    return true;
}

// ============================================================================
// 4c. HuggingFace SafeTensors (.safetensors)
// ============================================================================
struct StSheetJson {
    const char* s = nullptr;
    size_t n = 0;
    size_t pos = 0;

    void SkipWs() {
        while (pos < n && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n')) ++pos;
    }
    static bool Hex4(const char* p, uint32_t& out) {
        out = 0;
        for (int i = 0; i < 4; ++i) {
            char c = p[i];
            uint32_t v = 0;
            if (c >= '0' && c <= '9') v = static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v = static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v = static_cast<uint32_t>(c - 'A' + 10);
            else return false;
            out = (out << 4) | v;
        }
        return true;
    }
    static void AppendUtf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7Fu) out.push_back(static_cast<char>(cp));
        else if (cp <= 0x7FFu) {
            out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0xFFFFu) {
            out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0x10FFFFu) {
            out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }
    bool ParseStr(std::string& out) {
        SkipWs();
        if (pos >= n || s[pos] != '"') return false;
        ++pos;
        out.clear();
        while (pos < n) {
            unsigned char c = static_cast<unsigned char>(s[pos++]);
            if (c == '"') return true;
            if (c < 0x20u) return false;
            if (c == '\\') {
                if (pos >= n) return false;
                char e = s[pos++];
                switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (pos + 4 > n) return false;
                    uint32_t u1 = 0;
                    if (!Hex4(s + pos, u1)) return false;
                    pos += 4;
                    if (u1 >= 0xD800u && u1 <= 0xDBFFu && pos + 6 <= n && s[pos] == '\\' && s[pos + 1] == 'u') {
                        uint32_t u2 = 0;
                        if (Hex4(s + pos + 2, u2) && u2 >= 0xDC00u && u2 <= 0xDFFFu) {
                            pos += 6;
                            AppendUtf8(out, 0x10000u + (((u1 - 0xD800u) << 10) | (u2 - 0xDC00u)));
                            break;
                        }
                    }
                    AppendUtf8(out, u1);
                    break;
                }
                default: return false;
                }
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
        return false;
    }
    bool ParseU64(uint64_t& out) {
        SkipWs();
        if (pos >= n || s[pos] < '0' || s[pos] > '9') return false;
        if (s[pos] == '0' && pos + 1 < n && s[pos + 1] >= '0' && s[pos + 1] <= '9') return false;
        out = 0;
        while (pos < n && s[pos] >= '0' && s[pos] <= '9') {
            uint64_t d = static_cast<uint64_t>(s[pos] - '0');
            if (out > (UINT64_MAX - d) / 10ULL) return false;
            out = out * 10ULL + d;
            ++pos;
        }
        if (pos < n && (s[pos] == '.' || s[pos] == 'e' || s[pos] == 'E')) return false;
        return true;
    }
    bool SkipVal(int depth = 0) {
        if (depth > 32) return false;
        SkipWs();
        if (pos >= n) return false;
        char c = s[pos];
        if (c == '"') { std::string tmp; return ParseStr(tmp); }
        if (c == '{') {
            ++pos; SkipWs();
            if (pos < n && s[pos] == '}') { ++pos; return true; }
            for (;;) {
                std::string k; if (!ParseStr(k)) return false;
                SkipWs(); if (pos >= n || s[pos] != ':') return false; ++pos;
                if (!SkipVal(depth + 1)) return false;
                SkipWs();
                if (pos < n && s[pos] == ',') { ++pos; continue; }
                if (pos < n && s[pos] == '}') { ++pos; return true; }
                return false;
            }
        }
        if (c == '[') {
            ++pos; SkipWs();
            if (pos < n && s[pos] == ']') { ++pos; return true; }
            for (;;) {
                if (!SkipVal(depth + 1)) return false;
                SkipWs();
                if (pos < n && s[pos] == ',') { ++pos; continue; }
                if (pos < n && s[pos] == ']') { ++pos; return true; }
                return false;
            }
        }
        if (c == 't' && pos + 4 <= n && std::memcmp(s + pos, "true", 4) == 0) { pos += 4; return true; }
        if (c == 'f' && pos + 5 <= n && std::memcmp(s + pos, "false", 5) == 0) { pos += 5; return true; }
        if (c == 'n' && pos + 4 <= n && std::memcmp(s + pos, "null", 4) == 0) { pos += 4; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            if (c == '-') ++pos;
            if (pos >= n || s[pos] < '0' || s[pos] > '9') return false;
            if (s[pos] == '0') { ++pos; if (pos < n && s[pos] >= '0' && s[pos] <= '9') return false; }
            else { while (pos < n && s[pos] >= '0' && s[pos] <= '9') ++pos; }
            if (pos < n && s[pos] == '.') {
                ++pos; if (pos >= n || s[pos] < '0' || s[pos] > '9') return false;
                while (pos < n && s[pos] >= '0' && s[pos] <= '9') ++pos;
            }
            if (pos < n && (s[pos] == 'e' || s[pos] == 'E')) {
                ++pos; if (pos < n && (s[pos] == '+' || s[pos] == '-')) ++pos;
                if (pos >= n || s[pos] < '0' || s[pos] > '9') return false;
                while (pos < n && s[pos] >= '0' && s[pos] <= '9') ++pos;
            }
            return true;
        }
        return false;
    }
    bool ParseMetaVal(std::string& out) {
        SkipWs();
        if (pos >= n) return false;
        if (s[pos] == '"') return ParseStr(out);
        size_t st = pos;
        if (!SkipVal(1)) return false;
        out.assign(s + st, pos - st);
        return true;
    }
};

inline bool ParseSafeTensorsModel(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 10) return false;
    const uint64_t hdrLen = RdU64LE(raw.data());
    if (hdrLen < 2 || hdrLen > (64ULL * 1024ULL * 1024ULL)) return false;
    if (8ULL + hdrLen > raw.size()) return false;

    const char* jsonPtr = reinterpret_cast<const char*>(raw.data() + 8);
    const size_t jsonLen = static_cast<size_t>(hdrLen);
    size_t first = 0, last = jsonLen;
    while (first < last && (jsonPtr[first] == ' ' || jsonPtr[first] == '\t' || jsonPtr[first] == '\r' || jsonPtr[first] == '\n')) ++first;
    while (last > first && (jsonPtr[last - 1] == ' ' || jsonPtr[last - 1] == '\t' || jsonPtr[last - 1] == '\r' || jsonPtr[last - 1] == '\n')) --last;
    if (last - first < 2 || jsonPtr[first] != '{' || jsonPtr[last - 1] != '}') return false;

    StSheetJson cur{ jsonPtr, jsonLen, 0 };
    cur.SkipWs();
    if (cur.pos >= cur.n || cur.s[cur.pos] != '{') return false;
    ++cur.pos;
    cur.SkipWs();

    SpreadsheetSheet metaSheet;
    metaSheet.name = L"SafeTensors_Metadata";
    metaSheet.rows.push_back({ L"Key", L"Value" });
    metaSheet.rows.push_back({ L"header_bytes", std::to_wstring(hdrLen) });

    SpreadsheetSheet tensorSheet;
    tensorSheet.name = L"SafeTensors_Tensors";
    tensorSheet.rows.push_back({ L"#", L"Tensor Name", L"DType", L"Shape", L"Parameters", L"Byte Size", L"Sample Values" });

    uint64_t tensorIdx = 0;
    uint64_t totalParams = 0;
    const uint64_t dataBase = 8ULL + hdrLen;

    if (cur.pos < cur.n && cur.s[cur.pos] == '}') {
        ++cur.pos;
    } else {
        for (;;) {
            std::string topKey;
            if (!cur.ParseStr(topKey)) return false;
            cur.SkipWs();
            if (cur.pos >= cur.n || cur.s[cur.pos] != ':') return false;
            ++cur.pos;
            cur.SkipWs();
            if (cur.pos >= cur.n || cur.s[cur.pos] != '{') return false;
            ++cur.pos;
            cur.SkipWs();

            if (topKey == "__metadata__") {
                if (cur.pos < cur.n && cur.s[cur.pos] == '}') {
                    ++cur.pos;
                } else {
                    for (;;) {
                        std::string mk, mv;
                        if (!cur.ParseStr(mk)) return false;
                        cur.SkipWs();
                        if (cur.pos >= cur.n || cur.s[cur.pos] != ':') return false;
                        ++cur.pos;
                        if (!cur.ParseMetaVal(mv)) return false;
                        if (metaSheet.rows.size() < 512) {
                            metaSheet.rows.push_back({ PathUtils::Utf8ToWide(mk), PathUtils::Utf8ToWide(mv) });
                        }
                        cur.SkipWs();
                        if (cur.pos < cur.n && cur.s[cur.pos] == ',') { ++cur.pos; continue; }
                        if (cur.pos < cur.n && cur.s[cur.pos] == '}') { ++cur.pos; break; }
                        return false;
                    }
                }
            } else {
                if (topKey.empty()) return false;
                bool hasDt = false, hasSh = false, hasOff = false;
                std::string dt;
                std::vector<uint64_t> sh;
                uint64_t bOff = 0, eOff = 0;
                if (cur.pos < cur.n && cur.s[cur.pos] == '}') return false;
                for (;;) {
                    std::string fk;
                    if (!cur.ParseStr(fk)) return false;
                    cur.SkipWs();
                    if (cur.pos >= cur.n || cur.s[cur.pos] != ':') return false;
                    ++cur.pos;
                    cur.SkipWs();
                    if (fk == "dtype") {
                        if (!cur.ParseStr(dt) || dt.empty() || dt.size() > 32) return false;
                        hasDt = true;
                    } else if (fk == "shape") {
                        if (cur.pos >= cur.n || cur.s[cur.pos] != '[') return false;
                        ++cur.pos; cur.SkipWs();
                        sh.clear();
                        if (cur.pos < cur.n && cur.s[cur.pos] == ']') {
                            ++cur.pos;
                        } else {
                            for (;;) {
                                uint64_t dv = 0;
                                if (!cur.ParseU64(dv) || sh.size() >= 32) return false;
                                sh.push_back(dv);
                                cur.SkipWs();
                                if (cur.pos < cur.n && cur.s[cur.pos] == ',') { ++cur.pos; continue; }
                                if (cur.pos < cur.n && cur.s[cur.pos] == ']') { ++cur.pos; break; }
                                return false;
                            }
                        }
                        hasSh = true;
                    } else if (fk == "data_offsets") {
                        if (cur.pos >= cur.n || cur.s[cur.pos] != '[') return false;
                        ++cur.pos;
                        if (!cur.ParseU64(bOff)) return false;
                        cur.SkipWs();
                        if (cur.pos >= cur.n || cur.s[cur.pos] != ',') return false;
                        ++cur.pos;
                        if (!cur.ParseU64(eOff)) return false;
                        cur.SkipWs();
                        if (cur.pos >= cur.n || cur.s[cur.pos] != ']') return false;
                        ++cur.pos;
                        hasOff = true;
                    } else {
                        if (!cur.SkipVal(2)) return false;
                    }
                    cur.SkipWs();
                    if (cur.pos < cur.n && cur.s[cur.pos] == ',') { ++cur.pos; continue; }
                    if (cur.pos < cur.n && cur.s[cur.pos] == '}') { ++cur.pos; break; }
                    return false;
                }
                if (!hasDt || !hasSh || !hasOff || bOff > eOff || eOff > raw.size() - dataBase) return false;

                uint64_t params = 1;
                if (sh.empty()) {
                    params = (eOff > bOff) ? 1ULL : 0ULL;
                } else {
                    for (uint64_t d : sh) {
                        if (d == 0) { params = 0; break; }
                        if (params > UINT64_MAX / d) { params = UINT64_MAX; break; }
                        params *= d;
                    }
                }
                ++tensorIdx;
                if (totalParams <= UINT64_MAX - params) totalParams += params;
                else totalParams = UINT64_MAX;

                if (tensorSheet.rows.size() <= 1024) {
                    std::wstring shStr = L"[";
                    for (size_t i = 0; i < sh.size(); ++i) {
                        if (i > 0) shStr += L", ";
                        shStr += std::to_wstring(sh[i]);
                    }
                    shStr += L"]";

                    std::wstring sampleStr;
                    const uint8_t* tPtr = raw.data() + static_cast<size_t>(dataBase + bOff);
                    const uint64_t tLen = eOff - bOff;
                    if (dt == "F32" && tLen >= 4) {
                        for (size_t k = 0; k + 4 <= tLen && k < 16; k += 4) {
                            if (!sampleStr.empty()) sampleStr += L", ";
                            sampleStr += FmtDouble(RdF32LE(tPtr + k));
                        }
                    } else if (dt == "F16" && tLen >= 2) {
                        for (size_t k = 0; k + 2 <= tLen && k < 8; k += 2) {
                            if (!sampleStr.empty()) sampleStr += L", ";
                            sampleStr += FmtDouble(HalfToFloat(RdU16LE(tPtr + k)));
                        }
                    } else if (dt == "BF16" && tLen >= 2) {
                        for (size_t k = 0; k + 2 <= tLen && k < 8; k += 2) {
                            if (!sampleStr.empty()) sampleStr += L", ";
                            sampleStr += FmtDouble(BFloat16ToFloat(RdU16LE(tPtr + k)));
                        }
                    } else if (dt == "I32" && tLen >= 4) {
                        for (size_t k = 0; k + 4 <= tLen && k < 16; k += 4) {
                            if (!sampleStr.empty()) sampleStr += L", ";
                            sampleStr += std::to_wstring(RdI32LE(tPtr + k));
                        }
                    } else if (dt == "I64" && tLen >= 8) {
                        for (size_t k = 0; k + 8 <= tLen && k < 32; k += 8) {
                            if (!sampleStr.empty()) sampleStr += L", ";
                            sampleStr += std::to_wstring(RdI64LE(tPtr + k));
                        }
                    }

                    tensorSheet.rows.push_back({
                        std::to_wstring(tensorIdx),
                        PathUtils::Utf8ToWide(topKey),
                        PathUtils::Utf8ToWide(dt),
                        std::move(shStr),
                        std::to_wstring(params),
                        PathUtils::FormatByteSize(tLen),
                        std::move(sampleStr)
                    });
                }
            }
            cur.SkipWs();
            if (cur.pos < cur.n && cur.s[cur.pos] == ',') { ++cur.pos; continue; }
            if (cur.pos < cur.n && cur.s[cur.pos] == '}') { ++cur.pos; break; }
            return false;
        }
    }

    cur.SkipWs();
    if (cur.pos != cur.n) return false;

    metaSheet.rows.push_back({ L"tensor_count", std::to_wstring(tensorIdx) });
    metaSheet.rows.push_back({ L"total_parameters", std::to_wstring(totalParams) });

    FinalizeSheetCols(metaSheet);
    FinalizeSheetCols(tensorSheet);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"SafeTensors Model (.safetensors)";
    out.sheets.push_back(std::move(metaSheet));
    if (tensorSheet.rows.size() > 1) out.sheets.push_back(std::move(tensorSheet));
    out.valid = true;
    return true;
}

// ============================================================================
// 5. PyTorch Checkpoint (.pt / .pth / .ckpt — ZIP + Pickle VM)
// ============================================================================
struct PickleVal {
    enum Kind { Other, Int, Float, Str, Tuple, Storage, Tensor } kind = Other;
    int64_t ival = 0;
    double fval = 0.0;
    std::string str;
    std::vector<PickleVal> items;
    std::string storageType;
    std::string storageKey;
    int64_t storageNumel = 0;
    std::vector<int64_t> shape;
    std::vector<int64_t> stride;
};

struct PyTorchTensorDesc {
    std::string name;
    std::string dtype;
    std::string storageKey;
    std::vector<int64_t> shape;
    std::vector<int64_t> stride;
};

inline bool WalkPyTorchPickle(
    const uint8_t* data,
    size_t size,
    std::vector<PyTorchTensorDesc>& outTensors,
    std::vector<std::pair<std::string, std::string>>& outScalarKv)
{
    if (!data || size < 2 || data[0] != 0x80u || data[1] < 2 || data[1] > 5) return false;
    size_t pos = 2;

    std::vector<PickleVal> stack;
    std::vector<size_t> marks;
    std::unordered_map<uint32_t, PickleVal> memo;
    uint32_t nextMemoId = 0;
    bool sawStop = false;

    auto popMark = [&](std::vector<PickleVal>& popped) -> bool {
        if (marks.empty()) return false;
        size_t m = marks.back();
        marks.pop_back();
        if (m > stack.size()) return false;
        popped.assign(stack.begin() + static_cast<ptrdiff_t>(m), stack.end());
        stack.resize(m);
        return true;
    };

    auto recordKv = [&](const PickleVal& k, const PickleVal& v) {
        if (k.kind != PickleVal::Str) return;
        if (v.kind == PickleVal::Tensor) {
            PyTorchTensorDesc td;
            td.name = k.str;
            td.dtype = v.storageType;
            td.storageKey = v.storageKey;
            td.shape = v.shape;
            td.stride = v.stride;
            outTensors.push_back(std::move(td));
        } else if (v.kind == PickleVal::Str || v.kind == PickleVal::Int || v.kind == PickleVal::Float) {
            std::string vs = (v.kind == PickleVal::Str) ? v.str :
                             (v.kind == PickleVal::Int) ? std::to_string(v.ival) :
                             PathUtils::WideToUtf8(FmtDouble(v.fval));
            outScalarKv.emplace_back(k.str, vs);
        }
    };

    while (pos < size && !sawStop) {
        uint8_t op = data[pos++];
        switch (op) {
            case 0x80: if (pos + 1 > size) return false; pos += 1; break; // PROTO
            case 0x95: if (pos + 8 > size) return false; pos += 8; break; // FRAME (skip 8B frame length; opcodes follow inline!)
            case '.': sawStop = true; break;                              // STOP
            case '(': marks.push_back(stack.size()); break;               // MARK
            case '}': case ']': stack.push_back(PickleVal{}); break;      // EMPTY_DICT / EMPTY_LIST
            case ')': { PickleVal t; t.kind = PickleVal::Tuple; stack.push_back(std::move(t)); break; }
            case 'N': case 0x88: case 0x89: stack.push_back(PickleVal{}); break; // NONE / NEWTRUE / NEWFALSE
            case 'K': { if (pos + 1 > size) return false; PickleVal v; v.kind = PickleVal::Int; v.ival = data[pos++]; stack.push_back(v); break; }
            case 'M': { if (pos + 2 > size) return false; PickleVal v; v.kind = PickleVal::Int; v.ival = RdU16LE(data + pos); pos += 2; stack.push_back(v); break; }
            case 'J': { if (pos + 4 > size) return false; PickleVal v; v.kind = PickleVal::Int; v.ival = RdI32LE(data + pos); pos += 4; stack.push_back(v); break; }
            case 0x8A: { // LONG1
                if (pos >= size) return false;
                uint8_t n = data[pos++];
                if (n > size - pos) return false;
                int64_t iv = 0;
                for (uint8_t i = 0; i < std::min<uint8_t>(n, 8); ++i) iv |= static_cast<int64_t>(data[pos + i]) << (i * 8);
                pos += n;
                PickleVal v; v.kind = PickleVal::Int; v.ival = iv; stack.push_back(v);
                break;
            }
            case 'G': {
                if (pos + 8 > size) return false;
                PickleVal v; v.kind = PickleVal::Float; v.fval = RdF64BE(data + pos); pos += 8; stack.push_back(v);
                break;
            }
            case 0x8C: case 'C': case 'U': { // SHORT_BINUNICODE / SHORT_BINBYTES / SHORT_BINSTRING
                if (pos >= size) return false;
                uint8_t n = data[pos++];
                if (n > size - pos) return false;
                PickleVal v; v.kind = PickleVal::Str; v.str.assign(reinterpret_cast<const char*>(data + pos), n); pos += n;
                stack.push_back(std::move(v));
                break;
            }
            case 'X': case 'B': case 'T': { // BINUNICODE / BINBYTES / BINSTRING
                if (pos + 4 > size) return false;
                uint32_t n = RdU32LE(data + pos); pos += 4;
                if (n > size - pos) return false;
                PickleVal v; v.kind = PickleVal::Str; v.str.assign(reinterpret_cast<const char*>(data + pos), std::min<uint32_t>(n, 4096)); pos += n;
                stack.push_back(std::move(v));
                break;
            }
            case 0x8D: case 0x8E: { // BINUNICODE8 / BINBYTES8
                if (pos + 8 > size) return false;
                uint64_t n = RdU64LE(data + pos); pos += 8;
                if (n > size - pos) return false;
                PickleVal v; v.kind = PickleVal::Str; v.str.assign(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(std::min<uint64_t>(n, 4096))); pos += static_cast<size_t>(n);
                stack.push_back(std::move(v));
                break;
            }
            case 'c': { // GLOBAL module\nname\n
                size_t nl1 = pos; while (nl1 < size && data[nl1] != '\n') ++nl1;
                if (nl1 >= size) return false;
                size_t nl2 = nl1 + 1; while (nl2 < size && data[nl2] != '\n') ++nl2;
                if (nl2 >= size) return false;
                PickleVal v; v.kind = PickleVal::Str;
                v.str = std::string(reinterpret_cast<const char*>(data + pos), nl1 - pos) + "." +
                        std::string(reinterpret_cast<const char*>(data + nl1 + 1), nl2 - nl1 - 1);
                pos = nl2 + 1;
                stack.push_back(std::move(v));
                break;
            }
            case 0x93: { // STACK_GLOBAL
                if (stack.size() < 2) return false;
                PickleVal nm = std::move(stack.back()); stack.pop_back();
                PickleVal mod = std::move(stack.back()); stack.pop_back();
                PickleVal v; v.kind = PickleVal::Str; v.str = mod.str + "." + nm.str;
                stack.push_back(std::move(v));
                break;
            }
            case 't': {
                std::vector<PickleVal> items;
                if (!popMark(items)) return false;
                PickleVal t; t.kind = PickleVal::Tuple; t.items = std::move(items);
                stack.push_back(std::move(t));
                break;
            }
            case 0x85: case 0x86: case 0x87: { // TUPLE1 / TUPLE2 / TUPLE3
                size_t cnt = (op == 0x85) ? 1 : (op == 0x86 ? 2 : 3);
                if (stack.size() < cnt) return false;
                PickleVal t; t.kind = PickleVal::Tuple;
                t.items.assign(stack.end() - static_cast<ptrdiff_t>(cnt), stack.end());
                stack.resize(stack.size() - cnt);
                stack.push_back(std::move(t));
                break;
            }
            case 'Q': { // BINPERSID
                if (stack.empty()) return false;
                PickleVal pid = std::move(stack.back()); stack.pop_back();
                PickleVal st;
                st.kind = PickleVal::Storage;
                if (pid.kind == PickleVal::Tuple && pid.items.size() >= 5) {
                    st.storageType = pid.items[1].str;
                    st.storageKey = (pid.items[2].kind == PickleVal::Int) ? std::to_string(pid.items[2].ival) : pid.items[2].str;
                    st.storageNumel = pid.items[4].ival;
                }
                stack.push_back(std::move(st));
                break;
            }
            case 'R': case 0x81: { // REDUCE / NEWOBJ
                if (stack.size() < 2) return false;
                PickleVal args = std::move(stack.back()); stack.pop_back();
                PickleVal fn = std::move(stack.back()); stack.pop_back();
                PickleVal res;
                if (fn.str.find("rebuild_tensor") != std::string::npos && args.kind == PickleVal::Tuple && args.items.size() >= 3) {
                    res.kind = PickleVal::Tensor;
                    if (args.items[0].kind == PickleVal::Storage) {
                        res.storageType = args.items[0].storageType;
                        res.storageKey = args.items[0].storageKey;
                    }
                    if (args.items[2].kind == PickleVal::Tuple) {
                        for (const auto& d : args.items[2].items) res.shape.push_back(d.ival);
                    }
                    if (args.items.size() >= 4 && args.items[3].kind == PickleVal::Tuple) {
                        for (const auto& d : args.items[3].items) res.stride.push_back(d.ival);
                    }
                    if (outTensors.size() < 512) {
                        // Also record anonymous tensor in case pickle root is a single tensor
                        if (stack.empty()) {
                            PyTorchTensorDesc td{ "(root_tensor)", res.storageType, res.storageKey, res.shape, res.stride };
                            outTensors.push_back(std::move(td));
                        }
                    }
                } else if (args.kind == PickleVal::Tuple && args.items.size() == 1 && args.items[0].kind != PickleVal::Other) {
                    res = std::move(args.items[0]);
                }
                stack.push_back(std::move(res));
                break;
            }
            case 'b': { // BUILD
                if (stack.size() < 2) return false;
                stack.pop_back();
                break;
            }
            case 's': { // SETITEM
                if (stack.size() < 3) return false;
                PickleVal v = std::move(stack.back()); stack.pop_back();
                PickleVal k = std::move(stack.back()); stack.pop_back();
                recordKv(k, v);
                break;
            }
            case 'u': { // SETITEMS
                std::vector<PickleVal> kvs;
                if (!popMark(kvs) || stack.empty()) return false;
                for (size_t i = 0; i + 1 < kvs.size(); i += 2) recordKv(kvs[i], kvs[i + 1]);
                break;
            }
            case 'a': { if (stack.size() < 2) return false; stack.pop_back(); break; } // APPEND
            case 'e': { std::vector<PickleVal> items; if (!popMark(items) || stack.empty()) return false; break; } // APPENDS
            case 'q': { if (pos >= size || stack.empty()) return false; memo[data[pos++]] = stack.back(); break; } // BINPUT
            case 'r': { if (pos + 4 > size || stack.empty()) return false; memo[RdU32LE(data + pos)] = stack.back(); pos += 4; break; } // LONG_BINPUT
            case 0x94: { if (stack.empty()) return false; memo[nextMemoId++] = stack.back(); break; } // MEMOIZE
            case 'h': { if (pos >= size) return false; stack.push_back(memo[data[pos++]]); break; } // BINGET
            case 'j': { if (pos + 4 > size) return false; stack.push_back(memo[RdU32LE(data + pos)]); pos += 4; break; } // LONG_BINGET
            case '0': { if (stack.empty()) return false; stack.pop_back(); break; } // POP
            case '2': { if (stack.empty()) return false; stack.push_back(stack.back()); break; } // DUP
            default:
                // Unknown opcode -> stop cleanly if we already decoded structure, else fail
                return !outTensors.empty() || !outScalarKv.empty();
        }
    }
    if (sawStop && outTensors.empty() && outScalarKv.empty() && !stack.empty()) {
        const auto& top = stack.back();
        if (top.kind == PickleVal::Str || top.kind == PickleVal::Int || top.kind == PickleVal::Float) {
            PickleVal k; k.kind = PickleVal::Str; k.str = "(root)";
            recordKv(k, top);
        }
    }
    return sawStop;
}

inline bool ParsePyTorchCheckpoint(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    std::vector<PyTorchTensorDesc> tensors;
    std::vector<std::pair<std::string, std::string>> scalars;
    std::unordered_map<std::string, std::vector<uint8_t>> storageBlobs;
    std::wstring archiveVer;

    if (raw.size() >= 22 && raw[0] == 'P' && raw[1] == 'K') {
        std::vector<ZipDeflate::ArchiveItemMeta> entries;
        if (!ZipDeflate::ListZipEntriesFromMemory(raw.data(), raw.size(), entries) || entries.empty()) return false;

        std::vector<uint8_t> pklBytes;
        for (const auto& e : entries) {
            if (e.isDirectory) continue;
            std::wstring pLow = PathUtils::ToLower(e.pathInArchive);
            if (pLow.size() >= 8 && pLow.substr(pLow.size() - 8) == L"data.pkl") {
                ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), e, pklBytes);
            } else if (pklBytes.empty() && PathUtils::ToLower(PathUtils::GetExtension(e.pathInArchive)) == L".pkl") {
                ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), e, pklBytes);
            } else if (pLow.size() >= 7 && pLow.substr(pLow.size() - 7) == L"version") {
                std::vector<uint8_t> vb;
                if (ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), e, vb)) {
                    archiveVer = PathUtils::Trim(CleanAsciiFixed(vb.data(), vb.size()));
                }
            } else if (pLow.find(L"/data/") != std::wstring::npos || pLow.find(L"\\data\\") != std::wstring::npos) {
                if (storageBlobs.size() < 32 && e.uncompressedSize <= 4 * 1024 * 1024) {
                    std::vector<uint8_t> sb;
                    if (ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), e, sb)) {
                        storageBlobs[PathUtils::WideToUtf8(e.name)] = std::move(sb);
                    }
                }
            }
        }
        if (pklBytes.empty() || !WalkPyTorchPickle(pklBytes.data(), pklBytes.size(), tensors, scalars)) {
            return false;
        }
    } else if (raw.size() >= 4 && raw[0] == 0x80u && raw[1] >= 2 && raw[1] <= 5) {
        if (!WalkPyTorchPickle(raw.data(), raw.size(), tensors, scalars)) return false;
    } else {
        return false;
    }

    SpreadsheetSheet sh;
    sh.name = L"PyTorch_StateDict";
    sh.rows.push_back({ L"Parameter / Key", L"Storage Type", L"Shape", L"Stride", L"Weight Preview / Value" });

    if (!archiveVer.empty()) {
        sh.rows.push_back({ L"(archive_version)", L"PyTorch ZIP", archiveVer, L"", L"" });
    }
    for (const auto& t : tensors) {
        std::wstring shStr = L"[";
        for (size_t i = 0; i < t.shape.size(); ++i) { if (i > 0) shStr += L", "; shStr += std::to_wstring(t.shape[i]); }
        shStr += L"]";
        std::wstring stStr = L"[";
        for (size_t i = 0; i < t.stride.size(); ++i) { if (i > 0) stStr += L", "; stStr += std::to_wstring(t.stride[i]); }
        stStr += L"]";

        std::wstring sample;
        auto it = storageBlobs.find(t.storageKey);
        if (it != storageBlobs.end() && !it->second.empty()) {
            const auto& sb = it->second;
            if (t.dtype.find("Half") != std::string::npos) {
                for (size_t k = 0; k + 2 <= sb.size() && k < 8; k += 2) {
                    if (!sample.empty()) sample += L", ";
                    sample += FmtDouble(HalfToFloat(RdU16LE(sb.data() + k)));
                }
            } else if (t.dtype.find("BFloat16") != std::string::npos) {
                for (size_t k = 0; k + 2 <= sb.size() && k < 8; k += 2) {
                    if (!sample.empty()) sample += L", ";
                    sample += FmtDouble(BFloat16ToFloat(RdU16LE(sb.data() + k)));
                }
            } else if (t.dtype.find("Double") != std::string::npos) {
                for (size_t k = 0; k + 8 <= sb.size() && k < 32; k += 8) {
                    if (!sample.empty()) sample += L", ";
                    sample += FmtDouble(RdF64LE(sb.data() + k));
                }
            } else if (t.dtype.find("Long") != std::string::npos) {
                for (size_t k = 0; k + 8 <= sb.size() && k < 32; k += 8) {
                    if (!sample.empty()) sample += L", ";
                    sample += std::to_wstring(RdI64LE(sb.data() + k));
                }
            } else {
                for (size_t k = 0; k + 4 <= sb.size() && k < 16; k += 4) {
                    if (!sample.empty()) sample += L", ";
                    sample += FmtDouble(RdF32LE(sb.data() + k));
                }
            }
        }
        sh.rows.push_back({ PathUtils::Utf8ToWide(t.name), PathUtils::Utf8ToWide(t.dtype), shStr, stStr, sample });
    }
    for (const auto& kv : scalars) {
        if (sh.rows.size() >= 500) break;
        sh.rows.push_back({ PathUtils::Utf8ToWide(kv.first), L"metadata", L"scalar", L"", PathUtils::Utf8ToWide(kv.second) });
    }
    if (sh.rows.size() <= 1) return false;
    FinalizeSheetCols(sh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"PyTorch Checkpoint (" + extLower + L")";
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}

// ============================================================================
// 6. TensorFlow Lite FlatBuffer Model (.tflite)
// ============================================================================
inline const wchar_t* TfLiteTypeName(uint8_t t) {
    switch (t) {
        case 0:  return L"FLOAT32";
        case 1:  return L"FLOAT16";
        case 2:  return L"INT32";
        case 3:  return L"UINT8";
        case 4:  return L"INT64";
        case 5:  return L"STRING";
        case 6:  return L"BOOL";
        case 7:  return L"INT16";
        case 8:  return L"COMPLEX64";
        case 9:  return L"INT8";
        case 10: return L"FLOAT64";
        case 16: return L"BFLOAT16";
        default: return L"TENSOR";
    }
}

inline std::wstring TfLiteBuiltinOpName(int32_t code) {
    switch (code) {
        case 0:  return L"ADD";
        case 1:  return L"AVERAGE_POOL_2D";
        case 2:  return L"CONCATENATION";
        case 3:  return L"CONV_2D";
        case 4:  return L"DEPTHWISE_CONV_2D";
        case 9:  return L"FULLY_CONNECTED";
        case 14: return L"LOGISTIC";
        case 17: return L"MAX_POOL_2D";
        case 18: return L"MUL";
        case 19: return L"RELU";
        case 21: return L"RELU6";
        case 22: return L"RESHAPE";
        case 25: return L"SOFTMAX";
        case 32: return L"CUSTOM";
        case 34: return L"PAD";
        case 40: return L"MEAN";
        case 41: return L"SUB";
        case 42: return L"DIV";
        case 43: return L"SQUEEZE";
        case 83: return L"PACK";
        case 114: return L"QUANTIZE";
        case 6:  return L"DEQUANTIZE";
        default: return L"OP_" + std::to_wstring(code);
    }
}

inline bool ParseTfLiteModel(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 16) return false;
    const uint8_t* buf = raw.data();
    size_t len = raw.size();
    // Support standard ("TFL3" at +4) and size-prefixed ("TFL3" at +8) FlatBuffers
    if (std::memcmp(buf + 4, "TFL3", 4) != 0 && std::memcmp(buf + 4, "TFL2", 4) != 0 && std::memcmp(buf + 4, "TFLI", 4) != 0) {
        if (len >= 20 && (std::memcmp(buf + 8, "TFL3", 4) == 0 || std::memcmp(buf + 8, "TFL2", 4) == 0 || std::memcmp(buf + 8, "TFLI", 4) == 0)) {
            buf += 4;
            len -= 4;
        } else {
            return false;
        }
    }

    FbTable model;
    if (!GetFbRootTable(buf, len, model)) return false;

    uint32_t version = model.GetU32(0, 3);
    std::string desc = model.GetString(3);

    // Operator codes (slot 1)
    std::vector<std::wstring> opCodes;
    uint32_t opCodeCnt = 0;
    size_t opCodeVec = model.GetVector(1, opCodeCnt);
    if (opCodeVec && opCodeCnt <= 4096) {
        for (uint32_t i = 0; i < opCodeCnt && opCodeVec + (i + 1) * 4 <= len; ++i) {
            size_t elemOff = opCodeVec + i * 4;
            uint32_t uoff = RdU32LE(buf + elemOff);
            FbTable oc;
            if (uoff && elemOff + uoff <= len && oc.Init(buf, len, elemOff + uoff)) {
                int8_t depCode = oc.GetI8(0, 0);
                std::string custom = oc.GetString(1);
                int32_t builtinCode = oc.GetI32(3, static_cast<int32_t>(static_cast<uint8_t>(depCode)));
                if (builtinCode == 0 && depCode != 0) builtinCode = static_cast<uint8_t>(depCode);
                std::wstring nm = TfLiteBuiltinOpName(builtinCode);
                if (!custom.empty()) nm += L" (" + PathUtils::Utf8ToWide(custom) + L")";
                opCodes.push_back(nm);
            }
        }
    }

    // Buffers (slot 4) for weight previews
    uint32_t bufCnt = 0;
    size_t bufVec = model.GetVector(4, bufCnt);
    auto getBufferBytes = [&](uint32_t bIdx, const uint8_t*& outPtr, uint32_t& outLen) -> bool {
        outPtr = nullptr; outLen = 0;
        if (!bufVec || bIdx >= bufCnt || bufVec + (bIdx + 1) * 4 > len) return false;
        size_t elemOff = bufVec + bIdx * 4;
        uint32_t uoff = RdU32LE(buf + elemOff);
        FbTable bt;
        if (!uoff || elemOff + uoff > len || !bt.Init(buf, len, elemOff + uoff)) return false;
        size_t dp = bt.GetVector(0, outLen);
        if (!dp || outLen > len - dp) return false;
        outPtr = buf + dp;
        return outLen > 0;
    };

    SpreadsheetSheet tensorSheet;
    tensorSheet.name = L"TFLite_Tensors";
    tensorSheet.rows.push_back({ L"Subgraph", L"Tensor #", L"Name", L"Type", L"Shape", L"Buffer Preview" });
    tensorSheet.rows.push_back({ L"(model)", L"v" + std::to_wstring(version), PathUtils::Utf8ToWide(desc), L"FlatBuffer", std::to_wstring(bufCnt) + L" buffers", L"" });

    SpreadsheetSheet opSheet;
    opSheet.name = L"TFLite_Operators";
    opSheet.rows.push_back({ L"Subgraph", L"Op #", L"Operator", L"Input Tensors", L"Output Tensors" });

    uint32_t sgCnt = 0;
    size_t sgVec = model.GetVector(2, sgCnt);
    if (sgVec && sgCnt <= 256) {
        for (uint32_t s = 0; s < sgCnt && sgVec + (s + 1) * 4 <= len; ++s) {
            size_t sgElem = sgVec + s * 4;
            uint32_t sgOff = RdU32LE(buf + sgElem);
            FbTable sg;
            if (!sgOff || sgElem + sgOff > len || !sg.Init(buf, len, sgElem + sgOff)) continue;
            std::string sgName = sg.GetString(4);
            std::wstring sgLbl = sgName.empty() ? (L"sg_" + std::to_wstring(s)) : PathUtils::Utf8ToWide(sgName);

            uint32_t tCnt = 0;
            size_t tVec = sg.GetVector(0, tCnt);
            for (uint32_t t = 0; t < std::min<uint32_t>(tCnt, 500) && tVec && tVec + (t + 1) * 4 <= len; ++t) {
                size_t tElem = tVec + t * 4;
                uint32_t tOff = RdU32LE(buf + tElem);
                FbTable tt;
                if (!tOff || tElem + tOff > len || !tt.Init(buf, len, tElem + tOff)) continue;

                uint32_t dimCnt = 0;
                size_t dimVec = tt.GetVector(0, dimCnt);
                std::wstring shStr = L"[";
                for (uint32_t d = 0; d < std::min<uint32_t>(dimCnt, 16) && dimVec && dimVec + (d + 1) * 4 <= len; ++d) {
                    if (d > 0) shStr += L", ";
                    shStr += std::to_wstring(RdI32LE(buf + dimVec + d * 4));
                }
                shStr += L"]";

                uint8_t dt = tt.GetU8(1, 0);
                uint32_t bIdx = tt.GetU32(2, 0);
                std::string tName = tt.GetString(3);

                std::wstring preview;
                const uint8_t* bp = nullptr; uint32_t bl = 0;
                if (getBufferBytes(bIdx, bp, bl)) {
                    preview = std::to_wstring(bl) + L" B";
                    if (dt == 0 && bl >= 4) preview += L" (" + FmtDouble(RdF32LE(bp)) + L"...)";
                }
                tensorSheet.rows.push_back({ sgLbl, std::to_wstring(t), PathUtils::Utf8ToWide(tName), TfLiteTypeName(dt), shStr, preview });
            }

            uint32_t oCnt = 0;
            size_t oVec = sg.GetVector(3, oCnt);
            for (uint32_t o = 0; o < std::min<uint32_t>(oCnt, 500) && oVec && oVec + (o + 1) * 4 <= len; ++o) {
                size_t oElem = oVec + o * 4;
                uint32_t oOff = RdU32LE(buf + oElem);
                FbTable ot;
                if (!oOff || oElem + oOff > len || !ot.Init(buf, len, oElem + oOff)) continue;
                uint32_t ocIdx = ot.GetU32(0, 0);
                std::wstring opName = (ocIdx < opCodes.size()) ? opCodes[ocIdx] : (L"OP_" + std::to_wstring(ocIdx));

                auto fmtIdxVec = [&](uint16_t slot) {
                    uint32_t n = 0; size_t vp = ot.GetVector(slot, n);
                    std::wstring w;
                    for (uint32_t k = 0; k < std::min<uint32_t>(n, 16) && vp && vp + (k + 1) * 4 <= len; ++k) {
                        if (k > 0) w += L", ";
                        w += std::to_wstring(RdI32LE(buf + vp + k * 4));
                    }
                    return w;
                };
                opSheet.rows.push_back({ sgLbl, std::to_wstring(o), opName, fmtIdxVec(1), fmtIdxVec(2) });
            }
        }
    }

    FinalizeSheetCols(tensorSheet);
    FinalizeSheetCols(opSheet);
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"TensorFlow Lite FlatBuffer Model (.tflite)";
    out.sheets.push_back(std::move(tensorSheet));
    if (opSheet.rows.size() > 1) out.sheets.push_back(std::move(opSheet));
    out.valid = true;
    return true;
}

// ============================================================================
// 7. Apache Arrow IPC File / Stream & Feather v1 / v2 (.arrow / .feather / .ipc)
// ============================================================================
struct ArrowColSchema {
    std::wstring name;
    std::wstring typeLabel;
    bool nullable = true;
    uint8_t typeId = 0;    // Arrow Type union: 2=Int, 3=FloatingPoint, 5=Utf8, 6=Bool, 4=Binary, 20=LargeUtf8
    int32_t bitWidth = 32; // for Int / FloatingPoint (0=HALF,1=SINGLE,2=DOUBLE)
    bool isSigned = true;
    bool hasDict = false;
    int64_t dictId = 0;
    int32_t dictIdxBitWidth = 32;
    bool dictIdxSigned = true;
};

inline void ParseArrowSchemaFb(const FbTable& schemaTbl, std::vector<ArrowColSchema>& outCols) {
    uint32_t fCnt = 0;
    size_t fVec = schemaTbl.GetVector(1, fCnt);
    for (uint32_t i = 0; i < std::min<uint32_t>(fCnt, 256) && fVec && fVec + (i + 1) * 4 <= schemaTbl.len; ++i) {
        size_t fElem = fVec + i * 4;
        uint32_t fOff = RdU32LE(schemaTbl.buf + fElem);
        FbTable ft;
        if (!fOff || fElem + fOff > schemaTbl.len || !ft.Init(schemaTbl.buf, schemaTbl.len, fElem + fOff)) continue;

        ArrowColSchema col;
        col.name = PathUtils::Utf8ToWide(ft.GetString(0));
        col.nullable = ft.GetU8(1, 1) != 0;
        col.typeId = ft.GetU8(2, 0);
        FbTable typeTbl;
        bool hasTypeTbl = ft.GetTable(3, typeTbl);

        FbTable dictTbl;
        if (ft.GetTable(4, dictTbl)) {
            col.hasDict = true;
            col.dictId = dictTbl.GetI64(0, 0);
            FbTable idxIntTbl;
            if (dictTbl.GetTable(1, idxIntTbl)) {
                col.dictIdxBitWidth = idxIntTbl.GetI32(0, 32);
                col.dictIdxSigned   = idxIntTbl.GetU8(1, 1) != 0;
            }
        }

        switch (col.typeId) {
            case 1: col.typeLabel = L"Null"; break;
            case 2: // Int
                col.bitWidth = hasTypeTbl ? typeTbl.GetI32(0, 32) : 32;
                col.isSigned = hasTypeTbl ? (typeTbl.GetU8(1, 1) != 0) : true;
                col.typeLabel = (col.isSigned ? L"Int" : L"UInt") + std::to_wstring(col.bitWidth);
                break;
            case 3: { // FloatingPoint
                int16_t prec = hasTypeTbl ? typeTbl.GetI16(0, 1) : 1;
                col.bitWidth = (prec == 0) ? 16 : (prec == 2 ? 64 : 32);
                col.typeLabel = L"Float" + std::to_wstring(col.bitWidth);
                break;
            }
            case 4: col.typeLabel = L"Binary"; break;
            case 5: col.typeLabel = L"Utf8"; break;
            case 6: col.typeLabel = L"Bool"; break;
            case 7: col.typeLabel = L"Decimal"; break;
            case 8: col.typeLabel = L"Date"; col.bitWidth = (hasTypeTbl && typeTbl.GetI16(0, 1) == 0) ? 32 : 64; break;
            case 10: col.typeLabel = L"Timestamp"; col.bitWidth = 64; break;
            case 19: col.typeLabel = L"LargeBinary"; break;
            case 20: col.typeLabel = L"LargeUtf8"; break;
            default: col.typeLabel = L"ArrowType(" + std::to_wstring(col.typeId) + L")"; break;
        }
        if (col.hasDict) {
            col.typeLabel = L"Dict<" + col.typeLabel + L">";
        }
        outCols.push_back(std::move(col));
    }
}

inline bool DecodeArrowRecordBatch(
    const FbTable& rbTbl,
    const uint8_t* bodyPtr,
    size_t bodyLen,
    const std::vector<ArrowColSchema>& cols,
    const std::unordered_map<int64_t, std::vector<std::wstring>>& dictMap,
    std::vector<std::vector<std::wstring>>& outRows,
    int64_t& totalRowsAccum,
    std::wstring* outCompressionLabel = nullptr)
{
    int64_t batchRows = rbTbl.GetI64(0, 0);
    if (batchRows < 0) return false;
    if (batchRows == 0) return true;
    totalRowsAccum += batchRows;

    uint32_t nodeCnt = 0;
    size_t nodeVec = rbTbl.GetVector(1, nodeCnt);
    uint32_t bufCnt = 0;
    size_t bufVec = rbTbl.GetVector(2, bufCnt);
    if (!bufVec) return false;

    bool hasBodyComp = false;
    int8_t compCodec = 0;  // 0 = LZ4_FRAME, 1 = ZSTD
    int8_t compMethod = 0; // 0 = BUFFER
    FbTable compTbl;
    if (rbTbl.GetTable(3, compTbl)) {
        hasBodyComp = true;
        compCodec   = compTbl.GetI8(0, 0);
        compMethod  = compTbl.GetI8(1, 0);
        if (compMethod != 0 || (compCodec != 0 && compCodec != 1)) return false;
        if (outCompressionLabel) {
            *outCompressionLabel = (compCodec == 1) ? L"ZSTD" : L"LZ4_FRAME";
        }
    }

    size_t rowsToEmit = static_cast<size_t>(std::min<int64_t>(batchRows, static_cast<int64_t>(500 - std::min<size_t>(outRows.size(), 500))));
    if (rowsToEmit == 0) return true;

    std::vector<std::vector<uint8_t>> decompHolders;
    decompHolders.reserve(bufCnt);

    bool corruptComp = false;
    auto getBuf = [&](uint32_t idx, const uint8_t*& p, size_t& l) -> bool {
        p = nullptr; l = 0;
        if (idx >= bufCnt || bufVec + static_cast<size_t>(idx + 1) * 16 > rbTbl.len) return false;
        int64_t off = RdI64LE(rbTbl.buf + bufVec + static_cast<size_t>(idx) * 16);
        int64_t bl  = RdI64LE(rbTbl.buf + bufVec + static_cast<size_t>(idx) * 16 + 8);
        if (off < 0 || bl < 0 || static_cast<uint64_t>(off) + static_cast<uint64_t>(bl) > bodyLen) {
            corruptComp = true;
            return false;
        }
        if (bl == 0) return true;
        const uint8_t* rawP = bodyPtr + static_cast<size_t>(off);
        size_t rawL = static_cast<size_t>(bl);
        if (!hasBodyComp) {
            p = rawP;
            l = rawL;
            return true;
        }
        if (rawL < 8) { corruptComp = true; return false; }
        int64_t uncompLen = RdI64LE(rawP);
        if (uncompLen == -1) {
            p = rawP + 8;
            l = rawL - 8;
            return true;
        }
        if (uncompLen == 0) return true;
        if (uncompLen < -1 || uncompLen > 128LL * 1024LL * 1024LL) { corruptComp = true; return false; }
        std::vector<uint8_t> dec;
        size_t maxCap = static_cast<size_t>(uncompLen) + 64;
        bool okDec = (compCodec == 0)
            ? StreamCodecs::DecompressLz4Frame(rawP + 8, rawL - 8, dec, maxCap)
            : StreamCodecs::DecompressZstdFrame(rawP + 8, rawL - 8, dec, maxCap);
        if (!okDec || dec.size() != static_cast<size_t>(uncompLen)) {
            corruptComp = true;
            return false;
        }
        decompHolders.push_back(std::move(dec));
        p = decompHolders.back().data();
        l = decompHolders.back().size();
        return true;
    };

    size_t baseRow = outRows.size();
    outRows.resize(baseRow + rowsToEmit, std::vector<std::wstring>(cols.size()));

    uint32_t bCursor = 0;
    (void)nodeVec;
    (void)nodeCnt;
    for (size_t c = 0; c < cols.size(); ++c) {
        const auto& col = cols[c];
        const uint8_t* validBmp = nullptr; size_t validLen = 0;
        if (!getBuf(bCursor++, validBmp, validLen) && corruptComp) return false;

        auto isNull = [&](size_t r) -> bool {
            if (!validBmp || validLen == 0) return false;
            size_t byteIdx = r >> 3;
            if (byteIdx >= validLen) return false;
            return ((validBmp[byteIdx] >> (r & 7)) & 1u) == 0;
        };

        if (col.hasDict) {
            const uint8_t* idxBuf = nullptr; size_t idxLen = 0;
            if (!getBuf(bCursor++, idxBuf, idxLen) && corruptComp) return false;
            size_t bw = static_cast<size_t>(std::max(8, col.dictIdxBitWidth) / 8);
            auto itD = dictMap.find(col.dictId);
            for (size_t r = 0; r < rowsToEmit; ++r) {
                if (isNull(r)) { outRows[baseRow + r][c] = L"NULL"; continue; }
                if (!idxBuf || (r + 1) * bw > idxLen) continue;
                const uint8_t* p = idxBuf + r * bw;
                int64_t dv = 0;
                if (col.dictIdxSigned) {
                    dv = (bw == 1) ? static_cast<int8_t>(p[0]) :
                         (bw == 2) ? RdI16LE(p) :
                         (bw == 4) ? RdI32LE(p) : RdI64LE(p);
                } else {
                    dv = static_cast<int64_t>((bw == 1) ? p[0] :
                         (bw == 2) ? RdU16LE(p) :
                         (bw == 4) ? RdU32LE(p) : RdU64LE(p));
                }
                if (itD != dictMap.end() && dv >= 0 && static_cast<size_t>(dv) < itD->second.size()) {
                    outRows[baseRow + r][c] = itD->second[static_cast<size_t>(dv)];
                } else {
                    outRows[baseRow + r][c] = std::to_wstring(dv);
                }
            }
            continue;
        }

        if (col.typeId == 5 || col.typeId == 4) { // Utf8 / Binary (validity, int32 offsets, data)
            const uint8_t* offBuf = nullptr; size_t offLen = 0;
            const uint8_t* valBuf = nullptr; size_t valLen = 0;
            if (!getBuf(bCursor++, offBuf, offLen) && corruptComp) return false;
            if (!getBuf(bCursor++, valBuf, valLen) && corruptComp) return false;
            for (size_t r = 0; r < rowsToEmit; ++r) {
                if (isNull(r)) { outRows[baseRow + r][c] = L"NULL"; continue; }
                if (offBuf && (r + 2) * 4 <= offLen) {
                    int32_t o0 = RdI32LE(offBuf + r * 4);
                    int32_t o1 = RdI32LE(offBuf + (r + 1) * 4);
                    if (o0 >= 0 && o1 >= o0 && (o1 == o0 || (valBuf && static_cast<size_t>(o1) <= valLen))) {
                        std::string s = (o1 > o0) ? std::string(reinterpret_cast<const char*>(valBuf + o0), static_cast<size_t>(o1 - o0)) : std::string{};
                        outRows[baseRow + r][c] = PathUtils::Utf8ToWide(s);
                    }
                }
            }
        } else if (col.typeId == 20 || col.typeId == 19) { // LargeUtf8 / LargeBinary (validity, int64 offsets, data)
            const uint8_t* offBuf = nullptr; size_t offLen = 0;
            const uint8_t* valBuf = nullptr; size_t valLen = 0;
            if (!getBuf(bCursor++, offBuf, offLen) && corruptComp) return false;
            if (!getBuf(bCursor++, valBuf, valLen) && corruptComp) return false;
            for (size_t r = 0; r < rowsToEmit; ++r) {
                if (isNull(r)) { outRows[baseRow + r][c] = L"NULL"; continue; }
                if (offBuf && (r + 2) * 8 <= offLen) {
                    int64_t o0 = RdI64LE(offBuf + r * 8);
                    int64_t o1 = RdI64LE(offBuf + (r + 1) * 8);
                    if (o0 >= 0 && o1 >= o0 && (o1 == o0 || (valBuf && static_cast<uint64_t>(o1) <= valLen))) {
                        std::string s = (o1 > o0) ? std::string(reinterpret_cast<const char*>(valBuf + o0), static_cast<size_t>(o1 - o0)) : std::string{};
                        outRows[baseRow + r][c] = PathUtils::Utf8ToWide(s);
                    }
                }
            }
        } else { // Fixed-width primitive (validity, data)
            const uint8_t* valBuf = nullptr; size_t valLen = 0;
            if (!getBuf(bCursor++, valBuf, valLen) && corruptComp) return false;
            for (size_t r = 0; r < rowsToEmit; ++r) {
                if (isNull(r)) { outRows[baseRow + r][c] = L"NULL"; continue; }
                if (!valBuf) continue;
                if (col.typeId == 6) { // Bool
                    if ((r >> 3) < valLen) {
                        outRows[baseRow + r][c] = (((valBuf[r >> 3] >> (r & 7)) & 1u) != 0) ? L"true" : L"false";
                    }
                } else if (col.typeId == 2 || col.typeId == 8 || col.typeId == 10) {
                    size_t bw = static_cast<size_t>(col.bitWidth / 8);
                    if (bw > 0 && (r + 1) * bw <= valLen) {
                        const uint8_t* p = valBuf + r * bw;
                        if (col.isSigned) {
                            int64_t v = (bw == 1) ? static_cast<int8_t>(p[0]) :
                                        (bw == 2) ? RdI16LE(p) :
                                        (bw == 4) ? RdI32LE(p) : RdI64LE(p);
                            outRows[baseRow + r][c] = std::to_wstring(v);
                        } else {
                            uint64_t v = (bw == 1) ? p[0] :
                                         (bw == 2) ? RdU16LE(p) :
                                         (bw == 4) ? RdU32LE(p) : RdU64LE(p);
                            outRows[baseRow + r][c] = std::to_wstring(v);
                        }
                    }
                } else if (col.typeId == 3) {
                    size_t bw = static_cast<size_t>(col.bitWidth / 8);
                    if (bw > 0 && (r + 1) * bw <= valLen) {
                        const uint8_t* p = valBuf + r * bw;
                        double v = (bw == 2) ? HalfToFloat(RdU16LE(p)) :
                                   (bw == 4) ? RdF32LE(p) : RdF64LE(p);
                        outRows[baseRow + r][c] = FmtDouble(v);
                    }
                }
            }
        }
    }
    return !corruptComp;
}

inline bool DecodeArrowDictionaryBatch(
    const FbTable& dbTbl,
    const uint8_t* bodyPtr,
    size_t bodyLen,
    const std::vector<ArrowColSchema>& cols,
    std::unordered_map<int64_t, std::vector<std::wstring>>& dictMap)
{
    int64_t dictId = dbTbl.GetI64(0, 0);
    FbTable rbTbl;
    if (!dbTbl.GetTable(1, rbTbl)) return false;
    bool isDelta = dbTbl.GetU8(2, 0) != 0;

    // Find a schema column using this dictId to determine the dictionary value type
    ArrowColSchema valCol;
    bool found = false;
    for (const auto& c : cols) {
        if (c.hasDict && c.dictId == dictId) {
            valCol = c;
            valCol.hasDict = false; // Decode physical dictionary values!
            found = true;
            break;
        }
    }
    if (!found) return true;

    std::vector<ArrowColSchema> singleCol = { valCol };
    std::vector<std::vector<std::wstring>> decRows;
    int64_t dummyAccum = 0;
    std::unordered_map<int64_t, std::vector<std::wstring>> emptyDict;
    if (!DecodeArrowRecordBatch(rbTbl, bodyPtr, bodyLen, singleCol, emptyDict, decRows, dummyAccum)) {
        return false;
    }
    auto& dst = dictMap[dictId];
    if (!isDelta) dst.clear();
    for (auto& r : decRows) {
        if (!r.empty()) dst.push_back(std::move(r[0]));
    }
    return true;
}

inline bool FinalizeArrowSheets(
    const std::wstring& extLower,
    const std::wstring& compLabel,
    size_t batchCount,
    int64_t totalRows,
    const std::vector<ArrowColSchema>& cols,
    std::vector<std::vector<std::wstring>>& dataRows,
    ParsedOfficeDocument& out)
{
    if (cols.empty()) return false;
    if (totalRows > 0 && dataRows.empty()) return false; // Corrupt or undecodable record batch!

    SpreadsheetSheet schemaSh;
    schemaSh.name = L"Arrow_Schema";
    schemaSh.rows.push_back({ L"#", L"Field Name", L"Arrow Type", L"Nullable" });
    schemaSh.rows.push_back({
        L"(summary)",
        L"RecordBatches: " + std::to_wstring(batchCount),
        L"Total Rows: " + std::to_wstring(totalRows),
        compLabel.empty() ? L"UNCOMPRESSED" : compLabel
    });
    for (size_t i = 0; i < cols.size(); ++i) {
        schemaSh.rows.push_back({ std::to_wstring(i), cols[i].name, cols[i].typeLabel, cols[i].nullable ? L"true" : L"false" });
    }
    FinalizeSheetCols(schemaSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Apache Arrow Columnar IPC (" + extLower +
        (compLabel.empty() ? L"" : (L", " + compLabel)) + L")";
    out.sheets.push_back(std::move(schemaSh));

    if (!dataRows.empty()) {
        SpreadsheetSheet dataSh;
        dataSh.name = L"Arrow_Data";
        std::vector<std::wstring> hdr;
        for (const auto& c : cols) hdr.push_back(c.name);
        dataSh.rows.push_back(std::move(hdr));
        for (auto& r : dataRows) dataSh.rows.push_back(std::move(r));
        FinalizeSheetCols(dataSh);
        out.sheets.push_back(std::move(dataSh));
    }
    out.valid = true;
    return true;
}

inline bool ParseArrowOrFeather(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    // 1. Feather v1 ("FEA1" ... "FEA1")
    if (raw.size() >= 16 && std::memcmp(raw.data(), "FEA1", 4) == 0 &&
        std::memcmp(raw.data() + raw.size() - 4, "FEA1", 4) == 0)
    {
        uint32_t metaLen = RdU32LE(raw.data() + raw.size() - 8);
        if (metaLen == 0 || metaLen > raw.size() - 12) return false;
        size_t metaOff = raw.size() - 8 - metaLen;
        FbTable ctable;
        if (!GetFbRootTable(raw.data() + metaOff, metaLen, ctable)) return false;

        int64_t numRows = ctable.GetI64(1, 0);
        uint32_t colCnt = 0;
        size_t colVec = ctable.GetVector(2, colCnt);
        if (!colVec || colCnt == 0) return false;

        SpreadsheetSheet schemaSh;
        schemaSh.name = L"Feather_Schema";
        schemaSh.rows.push_back({ L"#", L"Column Name", L"Feather Type", L"Values", L"Nulls" });

        std::vector<std::wstring> hdrRow;
        size_t previewRows = static_cast<size_t>(std::min<int64_t>(std::max<int64_t>(0, numRows), 500));
        std::vector<std::vector<std::wstring>> grid(previewRows, std::vector<std::wstring>(std::min<uint32_t>(colCnt, 128)));

        for (uint32_t c = 0; c < std::min<uint32_t>(colCnt, 128) && colVec + (c + 1) * 4 <= ctable.len; ++c) {
            size_t cElem = colVec + c * 4;
            uint32_t cOff = RdU32LE(ctable.buf + cElem);
            FbTable ct;
            if (!cOff || cElem + cOff > ctable.len || !ct.Init(ctable.buf, ctable.len, cElem + cOff)) continue;
            std::wstring cname = PathUtils::Utf8ToWide(ct.GetString(0));
            hdrRow.push_back(cname);

            FbTable pa;
            int8_t ptype = 0;
            int64_t dataOff = 0, vlen = 0, nulls = 0;
            if (ct.GetTable(1, pa)) {
                ptype = pa.GetI8(0, 0);
                dataOff = pa.GetI64(2, 0);
                vlen = pa.GetI64(3, 0);
                nulls = pa.GetI64(4, 0);
            }
            schemaSh.rows.push_back({ std::to_wstring(c), cname, L"Type(" + std::to_wstring(ptype) + L")", std::to_wstring(vlen), std::to_wstring(nulls) });

            size_t valPos = static_cast<size_t>(dataOff);
            if (nulls > 0) valPos += ((static_cast<size_t>(vlen) + 7) / 8 + 7) & ~size_t(7);
            for (size_t r = 0; r < previewRows; ++r) {
                if (ptype == 11 && valPos + (r + 1) * 4 <= raw.size()) grid[r][c] = FmtDouble(RdF32LE(raw.data() + valPos + r * 4));
                else if (ptype == 12 && valPos + (r + 1) * 8 <= raw.size()) grid[r][c] = FmtDouble(RdF64LE(raw.data() + valPos + r * 8));
                else if (ptype == 3 && valPos + (r + 1) * 4 <= raw.size()) grid[r][c] = std::to_wstring(RdI32LE(raw.data() + valPos + r * 4));
                else if (ptype == 4 && valPos + (r + 1) * 8 <= raw.size()) grid[r][c] = std::to_wstring(RdI64LE(raw.data() + valPos + r * 8));
            }
        }
        FinalizeSheetCols(schemaSh);
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Apache Arrow Feather v1 Table (" + extLower + L")";
        out.sheets.push_back(std::move(schemaSh));
        if (!hdrRow.empty()) {
            SpreadsheetSheet dataSh;
            dataSh.name = L"Feather_Data";
            dataSh.rows.push_back(std::move(hdrRow));
            for (auto& r : grid) dataSh.rows.push_back(std::move(r));
            FinalizeSheetCols(dataSh);
            out.sheets.push_back(std::move(dataSh));
        }
        out.valid = true;
        return true;
    }

    std::vector<ArrowColSchema> cols;
    std::unordered_map<int64_t, std::vector<std::wstring>> dictMap;
    std::vector<std::vector<std::wstring>> dataRows;
    std::wstring compLabel;
    int64_t totalRows = 0;
    size_t batchCount = 0;

    // 2. Arrow IPC Random-Access File / Feather v2 ("ARROW1" ... "ARROW1")
    if (raw.size() >= 18 && std::memcmp(raw.data(), "ARROW1", 6) == 0) {
        if (std::memcmp(raw.data() + raw.size() - 6, "ARROW1", 6) != 0) return false;
        uint32_t footerLen = RdU32LE(raw.data() + raw.size() - 10);
        if (footerLen == 0 || footerLen > raw.size() - 16) return false;
        size_t footerOff = raw.size() - 10 - footerLen;
        FbTable footer;
        if (!GetFbRootTable(raw.data() + footerOff, footerLen, footer)) return false;

        FbTable schemaTbl;
        if (!footer.GetTable(1, schemaTbl)) return false;
        ParseArrowSchemaFb(schemaTbl, cols);

        auto parseIpcBlockAt = [&](int64_t blkOff, int32_t metaLen, int64_t bodyLen, uint8_t expectedType) -> bool {
            if (blkOff < 0 || metaLen < 8 || bodyLen < 0) return false;
            if (static_cast<uint64_t>(blkOff) + static_cast<uint64_t>(metaLen) + static_cast<uint64_t>(bodyLen) > footerOff) return false;
            size_t msgPos = static_cast<size_t>(blkOff);
            uint32_t firstWord = RdU32LE(raw.data() + msgPos);
            size_t fbLen = (firstWord == 0xFFFFFFFFu) ? RdU32LE(raw.data() + msgPos + 4) : firstWord;
            size_t fbOff = (firstWord == 0xFFFFFFFFu) ? (msgPos + 8) : (msgPos + 4);
            if (fbLen == 0 || fbOff + fbLen > msgPos + static_cast<size_t>(metaLen)) return false;
            FbTable msg;
            if (!GetFbRootTable(raw.data() + fbOff, fbLen, msg) || msg.GetU8(1, 0) != expectedType) return false;
            const uint8_t* bodyPtr = raw.data() + static_cast<size_t>(blkOff) + static_cast<size_t>(metaLen);
            if (expectedType == 2) {
                FbTable db;
                return msg.GetTable(2, db) && DecodeArrowDictionaryBatch(db, bodyPtr, static_cast<size_t>(bodyLen), cols, dictMap);
            }
            FbTable rb;
            return msg.GetTable(2, rb) &&
                   DecodeArrowRecordBatch(rb, bodyPtr, static_cast<size_t>(bodyLen), cols, dictMap, dataRows, totalRows, &compLabel);
        };

        // Footer slot 2: dictionaries: [Block]
        uint32_t dictCnt = 0;
        size_t dictVec = footer.GetVector(2, dictCnt);
        for (uint32_t d = 0; d < dictCnt && dictVec && dictVec + (d + 1) * 24 <= footer.len; ++d) {
            if (!parseIpcBlockAt(RdI64LE(footer.buf + dictVec + d * 24),
                                 RdI32LE(footer.buf + dictVec + d * 24 + 8),
                                 RdI64LE(footer.buf + dictVec + d * 24 + 16), 2)) return false;
        }

        // Footer slot 3: recordBatches: [Block]
        uint32_t rbCnt = 0;
        size_t rbVec = footer.GetVector(3, rbCnt);
        batchCount = rbCnt;
        for (uint32_t b = 0; b < rbCnt && rbVec && rbVec + (b + 1) * 24 <= footer.len; ++b) {
            if (!parseIpcBlockAt(RdI64LE(footer.buf + rbVec + b * 24),
                                 RdI32LE(footer.buf + rbVec + b * 24 + 8),
                                 RdI64LE(footer.buf + rbVec + b * 24 + 16), 3)) return false;
        }
    } else if (raw.size() >= 12) {
        // 3. Arrow IPC Stream Format (0xFFFFFFFF + uint32 metaLen + Message FlatBuffer + Body)
        size_t pos = 0;
        while (pos + 8 <= raw.size()) {
            uint32_t w0 = RdU32LE(raw.data() + pos);
            uint32_t metaLen = 0;
            if (w0 == 0xFFFFFFFFu) {
                metaLen = RdU32LE(raw.data() + pos + 4);
                pos += 8;
            } else {
                if (pos == 0 && w0 > raw.size()) return false;
                metaLen = w0;
                pos += 4;
            }
            if (metaLen == 0) break;
            if (metaLen > raw.size() - pos) return false;

            FbTable msg;
            if (!GetFbRootTable(raw.data() + pos, metaLen, msg)) return false;
            pos += metaLen;

            uint8_t hdrType = msg.GetU8(1, 0);
            int64_t bodyLen = msg.GetI64(3, 0);
            if (bodyLen < 0 || static_cast<uint64_t>(bodyLen) > raw.size() - pos) return false;

            if (hdrType == 1) {
                FbTable schemaTbl;
                if (msg.GetTable(2, schemaTbl)) ParseArrowSchemaFb(schemaTbl, cols);
            } else if (hdrType == 2 && !cols.empty()) {
                FbTable db;
                if (!msg.GetTable(2, db) ||
                    !DecodeArrowDictionaryBatch(db, raw.data() + pos, static_cast<size_t>(bodyLen), cols, dictMap)) return false;
            } else if (hdrType == 3 && !cols.empty()) {
                ++batchCount;
                FbTable rb;
                if (!msg.GetTable(2, rb) ||
                    !DecodeArrowRecordBatch(rb, raw.data() + pos, static_cast<size_t>(bodyLen), cols, dictMap, dataRows, totalRows, &compLabel)) {
                    return false;
                }
            } else if (pos == metaLen + 8 && cols.empty()) {
                return false;
            }
            pos += static_cast<size_t>(bodyLen);
        }
    }

    return FinalizeArrowSheets(extLower, compLabel, batchCount, totalRows, cols, dataRows, out);
}

// Streaming & random-access Arrow IPC / Feather v1 & v2 reader supporting >32 MB / multi-GB files
inline bool ParseArrowOrFeatherFile(const std::wstring& fullPath, const std::wstring& extLower, ParsedOfficeDocument& out) {
    ParquetDetail::WinParquetRandomFile rf;
    if (!rf.Open(fullPath) || rf.fileSize < 12) return false;

    if (rf.fileSize <= 16ULL * 1024ULL * 1024ULL) {
        std::vector<uint8_t> raw(static_cast<size_t>(rf.fileSize));
        if (!rf.ReadAt(0, raw.data(), raw.size())) return false;
        return ParseArrowOrFeather(raw, extLower, out);
    }

    uint8_t head8[8] = {};
    if (!rf.ReadAt(0, head8, 8)) return false;

    std::vector<ArrowColSchema> cols;
    std::unordered_map<int64_t, std::vector<std::wstring>> dictMap;
    std::vector<std::vector<std::wstring>> dataRows;
    std::wstring compLabel;
    int64_t totalRows = 0;
    size_t batchCount = 0;

    if (std::memcmp(head8, "ARROW1", 6) == 0) {
        uint8_t tail10[10] = {};
        if (!rf.ReadAt(rf.fileSize - 10, tail10, 10) || std::memcmp(tail10 + 4, "ARROW1", 6) != 0) return false;
        uint32_t footerLen = RdU32LE(tail10);
        if (footerLen == 0 || footerLen > 64u * 1024u * 1024u || static_cast<uint64_t>(footerLen) + 16ULL > rf.fileSize) return false;
        uint64_t footerOff = rf.fileSize - 10ULL - footerLen;
        std::vector<uint8_t> footerBuf(footerLen);
        if (!rf.ReadAt(footerOff, footerBuf.data(), footerLen)) return false;

        FbTable footer;
        if (!GetFbRootTable(footerBuf.data(), footerBuf.size(), footer)) return false;
        FbTable schemaTbl;
        if (!footer.GetTable(1, schemaTbl)) return false;
        ParseArrowSchemaFb(schemaTbl, cols);

        std::vector<uint8_t> msgBuf, bodyBuf;
        auto readMsgTable = [&](uint64_t blkOff, int32_t metaLen, FbTable& outMsg) -> bool {
            if (metaLen < 8 || metaLen > 16 * 1024 * 1024 || blkOff + static_cast<uint64_t>(metaLen) > footerOff) return false;
            msgBuf.resize(static_cast<size_t>(metaLen));
            if (!rf.ReadAt(blkOff, msgBuf.data(), msgBuf.size())) return false;
            uint32_t w0 = RdU32LE(msgBuf.data());
            size_t fbLen = (w0 == 0xFFFFFFFFu) ? RdU32LE(msgBuf.data() + 4) : w0;
            size_t fbOff = (w0 == 0xFFFFFFFFu) ? 8 : 4;
            if (fbLen == 0 || fbOff + fbLen > msgBuf.size()) return false;
            return GetFbRootTable(msgBuf.data() + fbOff, fbLen, outMsg);
        };

        uint32_t dictCnt = 0;
        size_t dictVec = footer.GetVector(2, dictCnt);
        for (uint32_t d = 0; d < dictCnt && dictVec && dictVec + (d + 1) * 24 <= footer.len; ++d) {
            int64_t blkOff  = RdI64LE(footer.buf + dictVec + d * 24);
            int32_t metaLen = RdI32LE(footer.buf + dictVec + d * 24 + 8);
            int64_t bodyLen = RdI64LE(footer.buf + dictVec + d * 24 + 16);
            if (blkOff < 0 || bodyLen < 0 || bodyLen > 64LL * 1024LL * 1024LL) return false;
            FbTable msg;
            if (!readMsgTable(static_cast<uint64_t>(blkOff), metaLen, msg) || msg.GetU8(1, 0) != 2) return false;
            bodyBuf.resize(static_cast<size_t>(bodyLen));
            if (bodyLen > 0 && !rf.ReadAt(static_cast<uint64_t>(blkOff + metaLen), bodyBuf.data(), bodyBuf.size())) return false;
            FbTable db;
            if (!msg.GetTable(2, db) || !DecodeArrowDictionaryBatch(db, bodyBuf.data(), bodyBuf.size(), cols, dictMap)) return false;
        }

        uint32_t rbCnt = 0;
        size_t rbVec = footer.GetVector(3, rbCnt);
        batchCount = rbCnt;
        for (uint32_t b = 0; b < rbCnt && rbVec && rbVec + (b + 1) * 24 <= footer.len; ++b) {
            int64_t blkOff  = RdI64LE(footer.buf + rbVec + b * 24);
            int32_t metaLen = RdI32LE(footer.buf + rbVec + b * 24 + 8);
            int64_t bodyLen = RdI64LE(footer.buf + rbVec + b * 24 + 16);
            if (blkOff < 0 || bodyLen < 0 || static_cast<uint64_t>(blkOff + metaLen + bodyLen) > footerOff) return false;
            FbTable msg;
            if (!readMsgTable(static_cast<uint64_t>(blkOff), metaLen, msg) || msg.GetU8(1, 0) != 3) return false;
            FbTable rb;
            if (!msg.GetTable(2, rb)) return false;
            if (dataRows.size() < 500 && bodyLen <= 128LL * 1024LL * 1024LL) {
                bodyBuf.resize(static_cast<size_t>(bodyLen));
                if (bodyLen > 0 && !rf.ReadAt(static_cast<uint64_t>(blkOff + metaLen), bodyBuf.data(), bodyBuf.size())) return false;
                if (!DecodeArrowRecordBatch(rb, bodyBuf.data(), bodyBuf.size(), cols, dictMap, dataRows, totalRows, &compLabel)) {
                    return false;
                }
            } else {
                totalRows += std::max<int64_t>(0, rb.GetI64(0, 0));
            }
        }
        return FinalizeArrowSheets(extLower, compLabel, batchCount, totalRows, cols, dataRows, out);
    }

    // Large >16 MB Arrow IPC Stream (0xFFFFFFFF + metaLen + Message + Body)
    uint64_t pos = 0;
    std::vector<uint8_t> metaBuf, bodyBuf;
    while (pos + 8 <= rf.fileSize) {
        uint8_t pfx[8] = {};
        if (!rf.ReadAt(pos, pfx, 8)) break;
        uint32_t w0 = RdU32LE(pfx);
        uint32_t metaLen = 0;
        if (w0 == 0xFFFFFFFFu) {
            metaLen = RdU32LE(pfx + 4);
            pos += 8;
        } else {
            if (pos == 0 && w0 > 16 * 1024 * 1024) return false;
            metaLen = w0;
            pos += 4;
        }
        if (metaLen == 0) break;
        if (metaLen > 16 * 1024 * 1024 || metaLen > rf.fileSize - pos) return false;

        metaBuf.resize(metaLen);
        if (!rf.ReadAt(pos, metaBuf.data(), metaLen)) return false;
        pos += metaLen;

        FbTable msg;
        if (!GetFbRootTable(metaBuf.data(), metaLen, msg)) return false;
        uint8_t hdrType = msg.GetU8(1, 0);
        int64_t bodyLen = msg.GetI64(3, 0);
        if (bodyLen < 0 || static_cast<uint64_t>(bodyLen) > rf.fileSize - pos) return false;

        if (hdrType == 1) {
            FbTable schemaTbl;
            if (msg.GetTable(2, schemaTbl)) ParseArrowSchemaFb(schemaTbl, cols);
        } else if (hdrType == 2 && !cols.empty() && bodyLen <= 64LL * 1024LL * 1024LL) {
            bodyBuf.resize(static_cast<size_t>(bodyLen));
            if (bodyLen > 0 && !rf.ReadAt(pos, bodyBuf.data(), bodyBuf.size())) return false;
            FbTable db;
            if (!msg.GetTable(2, db) || !DecodeArrowDictionaryBatch(db, bodyBuf.data(), bodyBuf.size(), cols, dictMap)) return false;
        } else if (hdrType == 3 && !cols.empty()) {
            ++batchCount;
            FbTable rb;
            if (!msg.GetTable(2, rb)) return false;
            if (dataRows.size() < 500 && bodyLen <= 128LL * 1024LL * 1024LL) {
                bodyBuf.resize(static_cast<size_t>(bodyLen));
                if (bodyLen > 0 && !rf.ReadAt(pos, bodyBuf.data(), bodyBuf.size())) return false;
                if (!DecodeArrowRecordBatch(rb, bodyBuf.data(), bodyBuf.size(), cols, dictMap, dataRows, totalRows, &compLabel)) {
                    return false;
                }
            } else {
                totalRows += std::max<int64_t>(0, rb.GetI64(0, 0));
            }
        } else if (cols.empty()) {
            return false;
        }
        pos += static_cast<uint64_t>(bodyLen);
    }

    return FinalizeArrowSheets(extLower, compLabel, batchCount, totalRows, cols, dataRows, out);
}

// ============================================================================
// 8. Apache ORC Columnar Storage (.orc)
// ============================================================================
inline const wchar_t* OrcTypeKindName(uint64_t k) {
    switch (k) {
        case 0: return L"BOOLEAN";   case 1: return L"BYTE";      case 2: return L"SHORT";
        case 3: return L"INT";       case 4: return L"LONG";      case 5: return L"FLOAT";
        case 6: return L"DOUBLE";    case 7: return L"STRING";    case 8: return L"BINARY";
        case 9: return L"TIMESTAMP"; case 10: return L"LIST";     case 11: return L"MAP";
        case 12: return L"STRUCT";   case 13: return L"UNION";    case 14: return L"DECIMAL";
        case 15: return L"DATE";     case 16: return L"VARCHAR";  case 17: return L"CHAR";
        default: return L"ORC_TYPE";
    }
}

inline bool DecompressOrcStream(const uint8_t* src, size_t srcLen, uint64_t codec, std::vector<uint8_t>& out) {
    out.clear();
    if (codec == 0) { // NONE
        out.assign(src, src + srcLen);
        return true;
    }
    size_t pos = 0;
    while (pos + 3 <= srcLen) {
        uint32_t hdr = static_cast<uint32_t>(src[pos]) |
                       (static_cast<uint32_t>(src[pos + 1]) << 8) |
                       (static_cast<uint32_t>(src[pos + 2]) << 16);
        pos += 3;
        bool isOriginal = (hdr & 1u) != 0;
        size_t chunkLen = hdr >> 1;
        if (chunkLen > srcLen - pos) return false;
        if (isOriginal) {
            out.insert(out.end(), src + pos, src + pos + chunkLen);
        } else if (codec == 1) { // ZLIB in ORC is raw deflate!
            std::vector<uint8_t> dec;
            if (!ZipDeflate::InflateRaw(src + pos, chunkLen, dec, 16 * 1024 * 1024)) return false;
            out.insert(out.end(), dec.begin(), dec.end());
        } else if (codec == 2) { // SNAPPY
            std::vector<uint8_t> dec;
            if (!ParquetDetail::SnappyDecompress(src + pos, chunkLen, dec, 16 * 1024 * 1024)) return false;
            out.insert(out.end(), dec.begin(), dec.end());
        } else {
            return false;
        }
        pos += chunkLen;
        if (out.size() > 32 * 1024 * 1024) return false;
    }
    return pos == srcLen;
}

inline bool ParseApacheOrc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 6) return false;
    uint8_t psLen = raw.back();
    if (psLen == 0 || static_cast<size_t>(psLen) + 1 >= raw.size()) return false;

    size_t psPos = raw.size() - 1 - psLen;
    PbReader ps(raw.data() + psPos, psLen);
    uint64_t footerLength = 0, compression = 0, compBlockSize = 0;
    std::vector<uint64_t> version;
    std::string magic;
    uint32_t fn = 0; uint8_t wt = 0;
    while (ps.pos < ps.size) {
        if (!ps.ReadTag(fn, wt)) return false;
        if (fn == 1 && wt == 0) { if (!ps.ReadVarint(footerLength)) return false; }
        else if (fn == 2 && wt == 0) { if (!ps.ReadVarint(compression)) return false; }
        else if (fn == 3 && wt == 0) { if (!ps.ReadVarint(compBlockSize)) return false; }
        else if (fn == 4 && wt == 0) { uint64_t v = 0; if (!ps.ReadVarint(v)) return false; version.push_back(v); }
        else if (fn == 4 && wt == 2) {
            const uint8_t* vp = nullptr; size_t vl = 0;
            if (!ps.ReadLengthDelimited(vp, vl)) return false;
            PbReader vr(vp, vl);
            while (vr.pos < vr.size) { uint64_t v = 0; if (!vr.ReadVarint(v)) return false; version.push_back(v); }
        }
        else if (fn == 8000 && wt == 2) { if (!ps.ReadString(magic)) return false; }
        else if (!ps.SkipField(wt)) return false;
    }

    bool headerMagic = (raw.size() >= 3 && std::memcmp(raw.data(), "ORC", 3) == 0);
    if (magic != "ORC" && !headerMagic) return false;
    if (footerLength == 0 || footerLength > psPos) return false;

    size_t footerPos = psPos - static_cast<size_t>(footerLength);
    std::vector<uint8_t> footerBytes;
    const wchar_t* codecNames[] = { L"NONE", L"ZLIB", L"SNAPPY", L"LZO", L"LZ4", L"ZSTD" };
    std::wstring codecStr = (compression < 6) ? codecNames[compression] : L"CODEC";

    bool footerDecoded = DecompressOrcStream(raw.data() + footerPos, static_cast<size_t>(footerLength), compression, footerBytes);

    uint64_t numberOfRows = 0;
    struct OrcTypeNode {
        uint64_t kind = 0;
        std::vector<uint32_t> subtypes;
        std::vector<std::string> fieldNames;
    };
    std::vector<OrcTypeNode> types;
    struct OrcStripe { uint64_t offset = 0, indexLen = 0, dataLen = 0, footerLen = 0, numRows = 0; };
    std::vector<OrcStripe> stripes;
    struct OrcColStat { uint64_t numValues = 0; std::wstring minMax; };
    std::vector<OrcColStat> stats;

    if (footerDecoded && !footerBytes.empty()) {
        PbReader fr(footerBytes.data(), footerBytes.size());
        while (fr.pos < fr.size) {
            if (!fr.ReadTag(fn, wt)) break;
            if (fn == 6 && wt == 0) {
                fr.ReadVarint(numberOfRows);
            } else if (fn == 3 && wt == 2) { // StripeInformation
                const uint8_t* sp = nullptr; size_t sl = 0;
                if (!fr.ReadLengthDelimited(sp, sl)) break;
                PbReader sr(sp, sl);
                OrcStripe st;
                uint32_t sfn = 0; uint8_t swt = 0;
                while (sr.pos < sr.size && sr.ReadTag(sfn, swt)) {
                    if (sfn == 1 && swt == 0) sr.ReadVarint(st.offset);
                    else if (sfn == 2 && swt == 0) sr.ReadVarint(st.indexLen);
                    else if (sfn == 3 && swt == 0) sr.ReadVarint(st.dataLen);
                    else if (sfn == 4 && swt == 0) sr.ReadVarint(st.footerLen);
                    else if (sfn == 5 && swt == 0) sr.ReadVarint(st.numRows);
                    else if (!sr.SkipField(swt)) break;
                }
                stripes.push_back(st);
            } else if (fn == 4 && wt == 2) { // Type
                const uint8_t* tp = nullptr; size_t tl = 0;
                if (!fr.ReadLengthDelimited(tp, tl)) break;
                PbReader tr(tp, tl);
                OrcTypeNode tn;
                uint32_t tfn = 0; uint8_t twt = 0;
                while (tr.pos < tr.size && tr.ReadTag(tfn, twt)) {
                    if (tfn == 1 && twt == 0) tr.ReadVarint(tn.kind);
                    else if (tfn == 2 && twt == 0) { uint64_t s = 0; tr.ReadVarint(s); tn.subtypes.push_back(static_cast<uint32_t>(s)); }
                    else if (tfn == 2 && twt == 2) {
                        const uint8_t* xp = nullptr; size_t xl = 0;
                        if (tr.ReadLengthDelimited(xp, xl)) {
                            PbReader xr(xp, xl);
                            while (xr.pos < xr.size) { uint64_t s = 0; if (!xr.ReadVarint(s)) break; tn.subtypes.push_back(static_cast<uint32_t>(s)); }
                        }
                    } else if (tfn == 3 && twt == 2) {
                        std::string nm; if (tr.ReadString(nm)) tn.fieldNames.push_back(std::move(nm));
                    } else if (!tr.SkipField(twt)) break;
                }
                types.push_back(std::move(tn));
            } else if (fn == 7 && wt == 2) { // ColumnStatistics
                const uint8_t* cp = nullptr; size_t cl = 0;
                if (!fr.ReadLengthDelimited(cp, cl)) break;
                PbReader cr(cp, cl);
                OrcColStat cs;
                uint32_t cfn = 0; uint8_t cwt = 0;
                while (cr.pos < cr.size && cr.ReadTag(cfn, cwt)) {
                    if (cfn == 1 && cwt == 0) cr.ReadVarint(cs.numValues);
                    else if (cfn == 2 && cwt == 2) { // IntStatistics (1=min zigzag, 2=max zigzag)
                        const uint8_t* ip = nullptr; size_t il = 0;
                        if (cr.ReadLengthDelimited(ip, il)) {
                            PbReader ir(ip, il);
                            int64_t mn = 0, mx = 0; bool hmn = false, hmx = false;
                            uint32_t ifn = 0; uint8_t iwt = 0;
                            while (ir.pos < ir.size && ir.ReadTag(ifn, iwt)) {
                                if ((ifn == 1 || ifn == 2) && iwt == 0) {
                                    uint64_t zz = 0; ir.ReadVarint(zz);
                                    int64_t sv = static_cast<int64_t>((zz >> 1) ^ (~(zz & 1ULL) + 1ULL));
                                    if (ifn == 1) { mn = sv; hmn = true; } else { mx = sv; hmx = true; }
                                } else if (!ir.SkipField(iwt)) break;
                            }
                            if (hmn || hmx) cs.minMax = L"[" + std::to_wstring(mn) + L" .. " + std::to_wstring(mx) + L"]";
                        }
                    } else if (cfn == 4 && cwt == 2) { // StringStatistics
                        const uint8_t* sp = nullptr; size_t sl = 0;
                        if (cr.ReadLengthDelimited(sp, sl)) {
                            PbReader sr(sp, sl);
                            std::string mn, mx;
                            uint32_t sfn = 0; uint8_t swt = 0;
                            while (sr.pos < sr.size && sr.ReadTag(sfn, swt)) {
                                if (sfn == 1 && swt == 2) sr.ReadString(mn);
                                else if (sfn == 2 && swt == 2) sr.ReadString(mx);
                                else if (!sr.SkipField(swt)) break;
                            }
                            if (!mn.empty() || !mx.empty()) cs.minMax = L"[" + PathUtils::Utf8ToWide(mn) + L" .. " + PathUtils::Utf8ToWide(mx) + L"]";
                        }
                    } else if (!cr.SkipField(cwt)) break;
                }
                stats.push_back(std::move(cs));
            } else if (!fr.SkipField(wt)) break;
        }
    }

    SpreadsheetSheet sh;
    sh.name = L"ORC_Schema_And_Stats";
    sh.rows.push_back({ L"Type ID / Metric", L"Column Name", L"ORC Type", L"Values", L"Statistics / Info" });
    std::wstring verStr = version.size() >= 2 ? (std::to_wstring(version[0]) + L"." + std::to_wstring(version[1])) : L"0.12";
    sh.rows.push_back({ L"(PostScript)", L"Format Version " + verStr, L"Codec: " + codecStr, std::to_wstring(numberOfRows) + L" rows", L"BlockSize=" + std::to_wstring(compBlockSize) });

    std::unordered_map<uint32_t, std::string> typeNames;
    for (const auto& t : types) {
        for (size_t i = 0; i < t.subtypes.size() && i < t.fieldNames.size(); ++i) {
            typeNames[t.subtypes[i]] = t.fieldNames[i];
        }
    }
    for (size_t i = 0; i < types.size(); ++i) {
        std::wstring nm = (i == 0) ? L"(root_struct)" : PathUtils::Utf8ToWide(typeNames[static_cast<uint32_t>(i)]);
        std::wstring valCnt = (i < stats.size()) ? std::to_wstring(stats[i].numValues) : L"";
        std::wstring mm = (i < stats.size()) ? stats[i].minMax : L"";
        sh.rows.push_back({ std::to_wstring(i), nm, OrcTypeKindName(types[i].kind), valCnt, mm });
    }
    FinalizeSheetCols(sh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Apache ORC Columnar Storage (.orc)";
    out.sheets.push_back(std::move(sh));

    if (!stripes.empty()) {
        SpreadsheetSheet stSh;
        stSh.name = L"ORC_Stripes";
        stSh.rows.push_back({ L"Stripe #", L"Offset", L"Index Len", L"Data Len", L"Footer Len", L"Rows" });
        for (size_t i = 0; i < stripes.size(); ++i) {
            stSh.rows.push_back({
                std::to_wstring(i),
                std::to_wstring(stripes[i].offset),
                std::to_wstring(stripes[i].indexLen),
                std::to_wstring(stripes[i].dataLen),
                std::to_wstring(stripes[i].footerLen),
                std::to_wstring(stripes[i].numRows)
            });
        }
        FinalizeSheetCols(stSh);
        out.sheets.push_back(std::move(stSh));
    }
    out.valid = true;
    return true;
}

// ============================================================================
// 9. HDF5 Hierarchical Data Format (.h5 / .hdf5 / NetCDF-4 / MATLAB v7.3)
// ============================================================================
struct Hdf5Ctx {
    const uint8_t* data = nullptr;
    size_t size = 0;
    uint8_t offSize = 8;
    uint8_t lenSize = 8;
    uint64_t baseAddr = 0;
    std::set<uint64_t> visitedObjs;

    uint64_t RdOff(const uint8_t* p) const {
        if (offSize == 4) { uint32_t v = RdU32LE(p); return v == 0xFFFFFFFFu ? UINT64_MAX : v; }
        if (offSize == 2) { uint16_t v = RdU16LE(p); return v == 0xFFFFu ? UINT64_MAX : v; }
        return RdU64LE(p);
    }
    uint64_t RdLen(const uint8_t* p) const {
        if (lenSize == 4) return RdU32LE(p);
        if (lenSize == 2) return RdU16LE(p);
        return RdU64LE(p);
    }
    bool AddrOk(uint64_t relAddr, size_t need) const {
        if (relAddr == UINT64_MAX) return false;
        uint64_t abs = baseAddr + relAddr;
        return abs <= size && need <= size - static_cast<size_t>(abs);
    }
    const uint8_t* Ptr(uint64_t relAddr) const {
        return data + static_cast<size_t>(baseAddr + relAddr);
    }
};

struct Hdf5FilterDesc {
    uint16_t id = 0;    // 1=deflate, 2=shuffle, 3=fletcher32
    uint16_t flags = 0; // bit 0: optional
    std::vector<uint32_t> cdVals;
};

struct Hdf5DatasetInfo {
    std::wstring path;
    std::wstring dtypeDesc;
    uint8_t dtClass = 0xFF; // 0=int, 1=float, 3=string, 6=compound
    uint32_t dtSize = 0;
    bool dtSigned = true;
    bool dtBigEndian = false;
    std::vector<uint64_t> dims;
    std::vector<uint8_t> compactData;
    uint64_t contigAddr = UINT64_MAX;
    uint64_t contigSize = 0;
    // Chunked storage + filter pipeline (#32)
    bool isChunked = false;
    uint8_t chunkLayoutVer = 0;
    uint8_t chunkIdxType = 0; // v4: 1=SingleChunk, 2=Implicit
    uint64_t chunkAddr = UINT64_MAX;
    uint64_t singleChunkCompSize = 0;
    uint32_t singleChunkFilterMask = 0;
    std::vector<uint64_t> chunkDims;
    std::vector<Hdf5FilterDesc> filters;
};

inline std::string ReadHdf5HeapString(const Hdf5Ctx& ctx, uint64_t heapAddr, uint64_t strOff) {
    if (!ctx.AddrOk(heapAddr, 8 + 2 * ctx.lenSize + ctx.offSize)) return {};
    const uint8_t* hp = ctx.Ptr(heapAddr);
    if (std::memcmp(hp, "HEAP", 4) != 0) return {};
    uint64_t segSize = ctx.RdLen(hp + 8);
    uint64_t dataAddr = ctx.RdOff(hp + 8 + 2 * ctx.lenSize);
    if (strOff >= segSize || !ctx.AddrOk(dataAddr + strOff, 1)) return {};
    const uint8_t* sp = ctx.Ptr(dataAddr + strOff);
    size_t maxLen = std::min<size_t>(256, ctx.size - static_cast<size_t>(ctx.baseAddr + dataAddr + strOff));
    size_t n = 0;
    while (n < maxLen && sp[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(sp), n);
}

inline void WalkHdf5Object(Hdf5Ctx& ctx, uint64_t objAddr, const std::wstring& curPath,
    std::vector<Hdf5DatasetInfo>& outDatasets, std::vector<std::pair<std::wstring, std::wstring>>& outGroupsAndAttrs, int depth);

inline void WalkHdf5SnodOrBtree(Hdf5Ctx& ctx, uint64_t nodeAddr, uint64_t heapAddr,
    const std::wstring& parentPath, std::vector<Hdf5DatasetInfo>& outDatasets,
    std::vector<std::pair<std::wstring, std::wstring>>& outGroupsAndAttrs, int depth)
{
    if (depth > 20 || !ctx.AddrOk(nodeAddr, 8) || outDatasets.size() >= 64) return;
    const uint8_t* p = ctx.Ptr(nodeAddr);
    if (std::memcmp(p, "SNOD", 4) == 0) {
        uint16_t nSyms = RdU16LE(p + 6);
        size_t entrySize = 2 * ctx.offSize + 4 + 4 + 16;
        size_t cur = 8;
        for (uint16_t i = 0; i < nSyms && ctx.AddrOk(nodeAddr + cur, entrySize); ++i, cur += entrySize) {
            const uint8_t* ep = p + cur;
            uint64_t nameOff = ctx.RdOff(ep);
            uint64_t childHdr = ctx.RdOff(ep + ctx.offSize);
            uint32_t cacheType = RdU32LE(ep + 2 * ctx.offSize);
            std::string nm = ReadHdf5HeapString(ctx, heapAddr, nameOff);
            std::wstring subPath = parentPath == L"/" ? (L"/" + PathUtils::Utf8ToWide(nm)) : (parentPath + L"/" + PathUtils::Utf8ToWide(nm));
            if (cacheType == 1) {
                uint64_t subBtree = ctx.RdOff(ep + 2 * ctx.offSize + 8);
                uint64_t subHeap  = ctx.RdOff(ep + 3 * ctx.offSize + 8);
                outGroupsAndAttrs.emplace_back(subPath, L"Group");
                WalkHdf5SnodOrBtree(ctx, subBtree, subHeap, subPath, outDatasets, outGroupsAndAttrs, depth + 1);
            }
            if (childHdr != UINT64_MAX) {
                WalkHdf5Object(ctx, childHdr, subPath, outDatasets, outGroupsAndAttrs, depth + 1);
            }
        }
    } else if (std::memcmp(p, "TREE", 4) == 0 && p[4] == 0) { // Group B-tree (type 0)
        uint8_t level = p[5];
        uint16_t entriesUsed = RdU16LE(p + 6);
        size_t cur = 8 + 2 * ctx.offSize; // skip left/right sibling addresses
        // Interleaved: key0 (lenSize), child0 (offSize), key1 (lenSize), child1 (offSize), ..., keyN
        for (uint16_t i = 0; i < entriesUsed; ++i) {
            cur += ctx.lenSize; // skip key_i
            if (!ctx.AddrOk(nodeAddr + cur, ctx.offSize)) break;
            uint64_t childAddr = ctx.RdOff(p + cur);
            cur += ctx.offSize;
            if (level == 0) {
                WalkHdf5SnodOrBtree(ctx, childAddr, heapAddr, parentPath, outDatasets, outGroupsAndAttrs, depth + 1);
            } else {
                WalkHdf5SnodOrBtree(ctx, childAddr, heapAddr, parentPath, outDatasets, outGroupsAndAttrs, depth + 1);
            }
        }
    }
}

inline void ParseHdf5HeaderMessage(
    Hdf5Ctx& ctx,
    uint16_t msgType,
    const uint8_t* mp,
    size_t msize,
    const std::wstring& curPath,
    Hdf5DatasetInfo& ds,
    bool& hasDataspace,
    bool& hasDatatype,
    std::vector<std::pair<std::wstring, uint64_t>>& childLinks,
    uint64_t& symBtree,
    uint64_t& symHeap,
    std::vector<std::pair<std::wstring, std::wstring>>& outGroupsAndAttrs);

inline void WalkHdf5V1Messages(
    Hdf5Ctx& ctx, const uint8_t* p, size_t avail, const std::wstring& curPath,
    Hdf5DatasetInfo& ds, bool& hasDataspace, bool& hasDatatype,
    std::vector<std::pair<std::wstring, uint64_t>>& childLinks,
    uint64_t& symBtree, uint64_t& symHeap,
    std::vector<std::pair<std::wstring, std::wstring>>& outGroupsAndAttrs, int contDepth)
{
    size_t pos = 0;
    while (pos + 8 <= avail) {
        uint16_t mtype = RdU16LE(p + pos);
        uint16_t msize = RdU16LE(p + pos + 2);
        pos += 8;
        if (msize > avail - pos) break;
        if (mtype == 0x0010 && contDepth < 8 && msize >= static_cast<size_t>(ctx.offSize + ctx.lenSize)) {
            uint64_t cOff = ctx.RdOff(p + pos);
            uint64_t cLen = ctx.RdLen(p + pos + ctx.offSize);
            if (cLen <= 256 * 1024 && ctx.AddrOk(cOff, static_cast<size_t>(cLen))) {
                WalkHdf5V1Messages(ctx, ctx.Ptr(cOff), static_cast<size_t>(cLen), curPath, ds,
                    hasDataspace, hasDatatype, childLinks, symBtree, symHeap, outGroupsAndAttrs, contDepth + 1);
            }
        } else {
            ParseHdf5HeaderMessage(ctx, mtype, p + pos, msize, curPath, ds, hasDataspace, hasDatatype,
                childLinks, symBtree, symHeap, outGroupsAndAttrs);
        }
        pos += (static_cast<size_t>(msize) + 7u) & ~size_t(7u);
    }
}

inline void WalkHdf5V2Messages(
    Hdf5Ctx& ctx, const uint8_t* p, size_t avail, uint8_t ohFlags, const std::wstring& curPath,
    Hdf5DatasetInfo& ds, bool& hasDataspace, bool& hasDatatype,
    std::vector<std::pair<std::wstring, uint64_t>>& childLinks,
    uint64_t& symBtree, uint64_t& symHeap,
    std::vector<std::pair<std::wstring, std::wstring>>& outGroupsAndAttrs, int contDepth)
{
    size_t hdrExtra = (ohFlags & 0x04u) ? 2 : 0; // creation order tracked
    size_t pos = 0;
    while (pos + 4 + hdrExtra <= avail) {
        uint8_t mtype = p[pos];
        uint16_t msize = RdU16LE(p + pos + 1);
        pos += 4 + hdrExtra;
        if (mtype == 0 && msize == 0) break;
        if (msize > avail - pos) break;
        if (mtype == 0x10 && contDepth < 8 && msize >= static_cast<size_t>(ctx.offSize + ctx.lenSize)) {
            uint64_t cOff = ctx.RdOff(p + pos);
            uint64_t cLen = ctx.RdLen(p + pos + ctx.offSize);
            if (cLen >= 4 && cLen <= 256 * 1024 && ctx.AddrOk(cOff, static_cast<size_t>(cLen))) {
                const uint8_t* cp = ctx.Ptr(cOff);
                if (std::memcmp(cp, "OCHK", 4) == 0) {
                    WalkHdf5V2Messages(ctx, cp + 4, static_cast<size_t>(cLen - 4), ohFlags, curPath, ds,
                        hasDataspace, hasDatatype, childLinks, symBtree, symHeap, outGroupsAndAttrs, contDepth + 1);
                }
            }
        } else {
            ParseHdf5HeaderMessage(ctx, mtype, p + pos, msize, curPath, ds, hasDataspace, hasDatatype,
                childLinks, symBtree, symHeap, outGroupsAndAttrs);
        }
        pos += msize;
    }
}

inline void ParseHdf5HeaderMessage(
    Hdf5Ctx& ctx,
    uint16_t msgType,
    const uint8_t* mp,
    size_t msize,
    const std::wstring& curPath,
    Hdf5DatasetInfo& ds,
    bool& hasDataspace,
    bool& hasDatatype,
    std::vector<std::pair<std::wstring, uint64_t>>& childLinks,
    uint64_t& symBtree,
    uint64_t& symHeap,
    std::vector<std::pair<std::wstring, std::wstring>>& outGroupsAndAttrs)
{
    if (msgType == 0x0001 && msize >= 4) { // Dataspace
        uint8_t ver = mp[0];
        uint8_t rank = mp[1];
        size_t dimOff = (ver == 1) ? 8 : 4;
        if (ver == 2 && msize >= 4 && mp[3] == 2) { // Null dataspace
            hasDataspace = true;
            return;
        }
        hasDataspace = true;
        ds.dims.clear();
        for (uint8_t d = 0; d < rank && dimOff + ctx.lenSize <= msize; ++d, dimOff += ctx.lenSize) {
            ds.dims.push_back(ctx.RdLen(mp + dimOff));
        }
    } else if (msgType == 0x0003 && msize >= 8) { // Datatype
        hasDatatype = true;
        uint8_t cls = mp[0] & 0x0Fu;
        uint8_t bf0 = mp[1];
        uint32_t sz = RdU32LE(mp + 4);
        ds.dtClass = cls;
        ds.dtSize = sz;
        ds.dtBigEndian = (bf0 & 0x01u) != 0;
        ds.dtSigned = (bf0 & 0x08u) != 0;
        if (cls == 0) ds.dtypeDesc = (ds.dtSigned ? L"int" : L"uint") + std::to_wstring(sz * 8);
        else if (cls == 1) ds.dtypeDesc = L"float" + std::to_wstring(sz * 8);
        else if (cls == 3) ds.dtypeDesc = L"string(" + std::to_wstring(sz) + L")";
        else if (cls == 6) ds.dtypeDesc = L"compound(" + std::to_wstring(sz) + L" B)";
        else if (cls == 9) ds.dtypeDesc = L"vlen";
        else ds.dtypeDesc = L"class(" + std::to_wstring(cls) + L"," + std::to_wstring(sz) + L"B)";
    } else if (msgType == 0x0008 && msize >= 2) { // Data Layout
        uint8_t ver = mp[0];
        if (ver == 3) {
            uint8_t layoutClass = mp[1];
            if (layoutClass == 0 && msize >= 4) { // Compact
                uint16_t csz = RdU16LE(mp + 2);
                if (4u + csz <= msize) ds.compactData.assign(mp + 4, mp + 4 + csz);
            } else if (layoutClass == 1 && msize >= static_cast<size_t>(2 + ctx.offSize + ctx.lenSize)) { // Contiguous
                ds.contigAddr = ctx.RdOff(mp + 2);
                ds.contigSize = ctx.RdLen(mp + 2 + ctx.offSize);
            } else if (layoutClass == 2 && msize >= static_cast<size_t>(3 + ctx.offSize + 4)) { // Chunked v3
                uint8_t nd = mp[2]; // dimensionality = rank + 1 (last is element byte size)
                ds.isChunked = true;
                ds.chunkLayoutVer = 3;
                ds.chunkAddr = ctx.RdOff(mp + 3);
                ds.chunkDims.clear();
                size_t doff = 3 + ctx.offSize;
                for (uint8_t d = 0; d < nd && doff + 4 <= msize; ++d, doff += 4) {
                    ds.chunkDims.push_back(RdU32LE(mp + doff));
                }
            }
        } else if (ver == 4 && msize >= 2) {
            uint8_t layoutClass = mp[1];
            if (layoutClass == 0 && msize >= 4) {
                uint16_t csz = RdU16LE(mp + 2);
                if (4u + csz <= msize) ds.compactData.assign(mp + 4, mp + 4 + csz);
            } else if (layoutClass == 1 && msize >= static_cast<size_t>(2 + ctx.offSize + ctx.lenSize)) {
                ds.contigAddr = ctx.RdOff(mp + 2);
                ds.contigSize = ctx.RdLen(mp + 2 + ctx.offSize);
            } else if (layoutClass == 2 && msize >= 6) { // Chunked v4
                uint8_t lflags = mp[2];
                uint8_t nd = mp[3];
                uint8_t dimBytes = mp[4];
                if (dimBytes >= 1 && dimBytes <= 8 && 5u + static_cast<size_t>(nd) * dimBytes + 1u <= msize) {
                    ds.isChunked = true;
                    ds.chunkLayoutVer = 4;
                    ds.chunkDims.clear();
                    size_t doff = 5;
                    for (uint8_t d = 0; d < nd; ++d, doff += dimBytes) {
                        uint64_t dv = 0;
                        for (uint8_t b = 0; b < dimBytes; ++b) dv |= static_cast<uint64_t>(mp[doff + b]) << (b * 8);
                        ds.chunkDims.push_back(dv);
                    }
                    ds.chunkIdxType = mp[doff++];
                    if (ds.chunkIdxType == 1) { // Single Chunk index
                        if (lflags & 0x02u) { // Filtered single chunk
                            if (doff + ctx.lenSize + 4 + ctx.offSize <= msize) {
                                ds.singleChunkCompSize = ctx.RdLen(mp + doff);
                                ds.singleChunkFilterMask = RdU32LE(mp + doff + ctx.lenSize);
                                ds.chunkAddr = ctx.RdOff(mp + doff + ctx.lenSize + 4);
                            }
                        } else if (doff + ctx.offSize <= msize) {
                            ds.chunkAddr = ctx.RdOff(mp + doff);
                        }
                    } else if (ds.chunkIdxType == 2 && doff + ctx.offSize <= msize) { // Implicit index
                        ds.chunkAddr = ctx.RdOff(mp + doff);
                    }
                }
            }
        } else if ((ver == 1 || ver == 2) && msize >= 4) {
            uint8_t rank = mp[1];
            uint8_t layoutClass = mp[2];
            size_t off = 8;
            if (layoutClass == 1 && off + ctx.offSize <= msize) { // Contiguous v1/v2
                ds.contigAddr = ctx.RdOff(mp + off);
                off += ctx.offSize;
                uint64_t prod = 1;
                for (uint8_t d = 0; d < rank && off + 4 <= msize; ++d, off += 4) prod *= RdU32LE(mp + off);
                ds.contigSize = prod;
            } else if (layoutClass == 2 && off + ctx.offSize <= msize) { // Chunked v1/v2
                ds.isChunked = true;
                ds.chunkLayoutVer = ver;
                ds.chunkAddr = ctx.RdOff(mp + off);
                off += ctx.offSize;
                ds.chunkDims.clear();
                for (uint8_t d = 0; d < rank && off + 4 <= msize; ++d, off += 4) {
                    ds.chunkDims.push_back(RdU32LE(mp + off));
                }
            }
        }
    } else if (msgType == 0x000B && msize >= 2) { // Filter Pipeline (v1 / v2)
        uint8_t ver = mp[0];
        uint8_t nFilt = mp[1];
        size_t pos = (ver == 1) ? 8 : 2;
        if ((ver == 1 || ver == 2) && nFilt <= 16 && pos <= msize) {
            ds.filters.clear();
            for (uint8_t fi = 0; fi < nFilt; ++fi) {
                Hdf5FilterDesc fd;
                if (ver == 1) {
                    if (pos + 8 > msize) break;
                    fd.id = RdU16LE(mp + pos);
                    uint16_t nameLen = RdU16LE(mp + pos + 2);
                    fd.flags = RdU16LE(mp + pos + 4);
                    uint16_t nCd = RdU16LE(mp + pos + 6);
                    pos += 8;
                    if (nameLen > 0) {
                        size_t npad = (static_cast<size_t>(nameLen) + 7u) & ~size_t(7u);
                        if (npad > msize - pos) break;
                        pos += npad;
                    }
                    if (static_cast<size_t>(nCd) * 4u > msize - pos) break;
                    for (uint16_t c = 0; c < nCd; ++c) {
                        fd.cdVals.push_back(RdU32LE(mp + pos + c * 4u));
                    }
                    pos += static_cast<size_t>(nCd) * 4u;
                    if ((nCd & 1u) != 0 && pos + 4 <= msize) pos += 4;
                } else {
                    if (pos + 6 > msize) break;
                    fd.id = RdU16LE(mp + pos);
                    pos += 2;
                    uint16_t nameLen = 0;
                    if (fd.id >= 256) {
                        if (pos + 2 > msize) break;
                        nameLen = RdU16LE(mp + pos);
                        pos += 2;
                    }
                    if (pos + 4 > msize) break;
                    fd.flags = RdU16LE(mp + pos);
                    uint16_t nCd = RdU16LE(mp + pos + 2);
                    pos += 4;
                    if (nameLen > msize - pos) break;
                    pos += nameLen;
                    if (static_cast<size_t>(nCd) * 4u > msize - pos) break;
                    for (uint16_t c = 0; c < nCd; ++c) {
                        fd.cdVals.push_back(RdU32LE(mp + pos + c * 4u));
                    }
                    pos += static_cast<size_t>(nCd) * 4u;
                }
                ds.filters.push_back(std::move(fd));
            }
        }
    } else if (msgType == 0x0011 && msize >= static_cast<size_t>(2 * ctx.offSize)) { // Symbol Table
        symBtree = ctx.RdOff(mp);
        symHeap  = ctx.RdOff(mp + ctx.offSize);
    } else if (msgType == 0x0006 && msize >= 4 && mp[0] == 1) { // Link message (v2 groups)
        uint8_t flags = mp[1];
        size_t pos = 2;
        uint8_t linkType = 0; // 0 = hard link
        if (flags & 0x08u) { if (pos >= msize) return; linkType = mp[pos++]; }
        if (flags & 0x04u) pos += 8; // creation order
        if (flags & 0x10u) pos += 1; // link name charset
        size_t nameLenBytes = size_t(1) << (flags & 0x03u);
        if (pos + nameLenBytes > msize) return;
        uint64_t nameLen = (nameLenBytes == 1) ? mp[pos] : (nameLenBytes == 2 ? RdU16LE(mp + pos) : (nameLenBytes == 4 ? RdU32LE(mp + pos) : RdU64LE(mp + pos)));
        pos += nameLenBytes;
        if (nameLen > msize - pos) return;
        std::string linkName(reinterpret_cast<const char*>(mp + pos), static_cast<size_t>(nameLen));
        pos += static_cast<size_t>(nameLen);
        if (linkType == 0 && pos + ctx.offSize <= msize) {
            uint64_t targetAddr = ctx.RdOff(mp + pos);
            std::wstring sub = curPath == L"/" ? (L"/" + PathUtils::Utf8ToWide(linkName)) : (curPath + L"/" + PathUtils::Utf8ToWide(linkName));
            childLinks.emplace_back(sub, targetAddr);
        }
    } else if (msgType == 0x000C && msize >= 8) { // Attribute
        uint8_t ver = mp[0];
        size_t pos = (ver == 1) ? 2 : 2;
        if (ver == 1 && pos + 6 <= msize) {
            uint16_t nl = RdU16LE(mp + 2);
            uint16_t dtl = RdU16LE(mp + 4);
            uint16_t dsl = RdU16LE(mp + 6);
            pos = 8;
            if (nl > 0 && nl <= msize - pos) {
                std::wstring aname = CleanAsciiFixed(mp + pos, nl);
                size_t dtOff = pos + ((nl + 7u) & ~size_t(7u));
                size_t dsOff = dtOff + ((dtl + 7u) & ~size_t(7u));
                size_t valOff = dsOff + ((dsl + 7u) & ~size_t(7u));
                std::wstring aval;
                if (dtOff + 8 <= msize && valOff < msize) {
                    uint8_t acls = mp[dtOff] & 0x0Fu;
                    uint32_t asz = RdU32LE(mp + dtOff + 4);
                    if (acls == 3 && valOff + asz <= msize) aval = CleanAsciiFixed(mp + valOff, asz);
                    else if (acls == 0 && asz == 4 && valOff + 4 <= msize) aval = std::to_wstring(RdI32LE(mp + valOff));
                    else if (acls == 1 && asz == 8 && valOff + 8 <= msize) aval = FmtDouble(RdF64LE(mp + valOff));
                }
                if (!aname.empty() && outGroupsAndAttrs.size() < 200) {
                    outGroupsAndAttrs.emplace_back(curPath + L"@" + aname, aval.empty() ? L"(attribute)" : aval);
                }
            }
        } else if ((ver == 2 || ver == 3) && pos + 6 <= msize) {
            uint16_t nl = RdU16LE(mp + 2);
            uint16_t dtl = RdU16LE(mp + 4);
            uint16_t dsl = RdU16LE(mp + 6);
            pos = (ver == 3) ? 9 : 8;
            if (nl > 0 && nl <= msize - pos) {
                std::wstring aname = CleanAsciiFixed(mp + pos, nl);
                size_t dtOff = pos + nl;
                size_t dsOff = dtOff + dtl;
                size_t valOff = dsOff + dsl;
                std::wstring aval;
                if (dtOff + 8 <= msize && valOff < msize) {
                    uint8_t acls = mp[dtOff] & 0x0Fu;
                    uint32_t asz = RdU32LE(mp + dtOff + 4);
                    if (acls == 3 && valOff + asz <= msize) aval = CleanAsciiFixed(mp + valOff, asz);
                }
                if (!aname.empty() && outGroupsAndAttrs.size() < 200) {
                    outGroupsAndAttrs.emplace_back(curPath + L"@" + aname, aval.empty() ? L"(attribute)" : aval);
                }
            }
        }
    }
}

inline void WalkHdf5Object(
    Hdf5Ctx& ctx,
    uint64_t objAddr,
    const std::wstring& curPath,
    std::vector<Hdf5DatasetInfo>& outDatasets,
    std::vector<std::pair<std::wstring, std::wstring>>& outGroupsAndAttrs,
    int depth)
{
    if (depth > 20 || !ctx.visitedObjs.insert(objAddr).second || outDatasets.size() >= 64) return;
    if (!ctx.AddrOk(objAddr, 12)) return;
    const uint8_t* p = ctx.Ptr(objAddr);

    Hdf5DatasetInfo ds;
    ds.path = curPath;
    bool hasDataspace = false, hasDatatype = false;
    std::vector<std::pair<std::wstring, uint64_t>> childLinks;
    uint64_t symBtree = UINT64_MAX, symHeap = UINT64_MAX;

    if (std::memcmp(p, "OHDR", 4) == 0 && p[4] == 2) { // v2 Object Header
        uint8_t flags = p[5];
        size_t pos = 6;
        if (flags & 0x20u) pos += 16; // access/mod/change/birth times
        if (flags & 0x10u) pos += 4;  // attr phase change values
        size_t chunkLenBytes = size_t(1) << (flags & 0x03u);
        if (!ctx.AddrOk(objAddr + pos, chunkLenBytes)) return;
        uint64_t chunk0Size = (chunkLenBytes == 1) ? p[pos] : (chunkLenBytes == 2 ? RdU16LE(p + pos) : (chunkLenBytes == 4 ? RdU32LE(p + pos) : RdU64LE(p + pos)));
        pos += chunkLenBytes;
        if (chunk0Size > 256 * 1024 || !ctx.AddrOk(objAddr + pos, static_cast<size_t>(chunk0Size))) return;
        WalkHdf5V2Messages(ctx, p + pos, static_cast<size_t>(chunk0Size), flags, curPath, ds,
            hasDataspace, hasDatatype, childLinks, symBtree, symHeap, outGroupsAndAttrs, 0);
    } else if (p[0] == 1) { // v1 Object Header
        uint32_t objHdrSize = RdU32LE(p + 8);
        size_t pos = 16; // 12B v1 header + 4B alignment padding
        if (objHdrSize == 0 || objHdrSize > 256 * 1024 || !ctx.AddrOk(objAddr + pos, objHdrSize)) return;
        WalkHdf5V1Messages(ctx, p + pos, objHdrSize, curPath, ds,
            hasDataspace, hasDatatype, childLinks, symBtree, symHeap, outGroupsAndAttrs, 0);
    } else {
        return;
    }

    if (hasDataspace && hasDatatype) {
        outDatasets.push_back(std::move(ds));
    } else if (!curPath.empty()) {
        outGroupsAndAttrs.emplace_back(curPath, L"Group");
    }

    if (symBtree != UINT64_MAX && symHeap != UINT64_MAX) {
        WalkHdf5SnodOrBtree(ctx, symBtree, symHeap, curPath, outDatasets, outGroupsAndAttrs, depth + 1);
    }
    for (const auto& lk : childLinks) {
        WalkHdf5Object(ctx, lk.second, lk.first, outDatasets, outGroupsAndAttrs, depth + 1);
    }
}

inline bool ApplyHdf5ChunkFiltersReverse(
    const uint8_t* src,
    size_t srcLen,
    uint32_t filterMask,
    size_t expectedUncompBytes,
    const Hdf5DatasetInfo& ds,
    std::vector<uint8_t>& outBuf)
{
    outBuf.assign(src, src + srcLen);
    if (ds.filters.empty()) return true;

    const size_t maxCap = std::max<size_t>(expectedUncompBytes + 65536, 65536);
    for (int fi = static_cast<int>(ds.filters.size()) - 1; fi >= 0; --fi) {
        if ((filterMask & (1u << fi)) != 0) continue; // Filter was bypassed on write
        const auto& f = ds.filters[static_cast<size_t>(fi)];
        if (f.id == 1) { // H5Z_FILTER_DEFLATE (zlib / deflate)
            std::vector<uint8_t> dec;
            if (!ZipDeflate::InflateZlib(outBuf.data(), outBuf.size(), dec, maxCap) &&
                !ZipDeflate::InflateRaw(outBuf.data(), outBuf.size(), dec, maxCap))
            {
                return false;
            }
            outBuf = std::move(dec);
        } else if (f.id == 2) { // H5Z_FILTER_SHUFFLE (byte-unshuffle)
            size_t elemSz = (!f.cdVals.empty() && f.cdVals[0] > 0) ? static_cast<size_t>(f.cdVals[0]) : static_cast<size_t>(ds.dtSize);
            if (elemSz > 1 && outBuf.size() >= elemSz) {
                size_t nElems = outBuf.size() / elemSz;
                std::vector<uint8_t> unshuf(outBuf.size());
                for (size_t i = 0; i < nElems; ++i) {
                    for (size_t b = 0; b < elemSz; ++b) {
                        unshuf[i * elemSz + b] = outBuf[b * nElems + i];
                    }
                }
                size_t remOff = nElems * elemSz;
                if (remOff < outBuf.size()) {
                    std::memcpy(unshuf.data() + remOff, outBuf.data() + remOff, outBuf.size() - remOff);
                }
                outBuf = std::move(unshuf);
            }
        } else if (f.id == 3) { // H5Z_FILTER_FLETCHER32 (4-byte trailing checksum)
            if (outBuf.size() < 4) return false;
            outBuf.resize(outBuf.size() - 4);
        } else if ((f.flags & 0x0001u) == 0) {
            return false; // Unsupported mandatory filter
        }
    }
    return outBuf.size() >= expectedUncompBytes || expectedUncompBytes == 0;
}

inline void ScatterHdf5ChunkToLinear(
    const uint8_t* chunkData,
    size_t chunkDataBytes,
    const std::vector<uint64_t>& coords,
    const std::vector<uint64_t>& cDims,
    const std::vector<uint64_t>& dsDims,
    uint32_t elemBytes,
    std::vector<uint8_t>& dstLinear)
{
    if (elemBytes == 0 || dstLinear.empty()) return;
    const size_t rank = dsDims.size();
    if (rank == 0) {
        if (chunkDataBytes >= elemBytes && dstLinear.size() >= elemBytes) {
            std::memcpy(dstLinear.data(), chunkData, elemBytes);
        }
        return;
    }
    if (coords.size() < rank || cDims.size() < rank) return;

    // General N-D row-major coordinate scatter (exact for 1D, 2D, 3D, N-D!)
    uint64_t chunkTotalElems = 1;
    for (size_t d = 0; d < rank; ++d) {
        if (cDims[d] == 0) return;
        chunkTotalElems *= cDims[d];
    }
    const size_t maxSrcElems = std::min<size_t>(static_cast<size_t>(chunkTotalElems), chunkDataBytes / elemBytes);
    const size_t maxDstElems = dstLinear.size() / elemBytes;

    std::vector<uint64_t> dsStrides(rank, 1);
    std::vector<uint64_t> chStrides(rank, 1);
    for (size_t d = rank - 1; d > 0; --d) {
        dsStrides[d - 1] = dsStrides[d] * dsDims[d];
        chStrides[d - 1] = chStrides[d] * cDims[d];
    }

    for (size_t lin = 0; lin < maxSrcElems; ++lin) {
        uint64_t rem = lin;
        uint64_t dstIdx = 0;
        bool inBounds = true;
        for (size_t d = 0; d < rank; ++d) {
            uint64_t localC = rem / chStrides[d];
            rem %= chStrides[d];
            uint64_t globalC = coords[d] + localC;
            if (globalC >= dsDims[d]) { inBounds = false; break; }
            dstIdx += globalC * dsStrides[d];
        }
        if (inBounds && dstIdx < maxDstElems) {
            std::memcpy(dstLinear.data() + static_cast<size_t>(dstIdx) * elemBytes,
                        chunkData + lin * elemBytes, elemBytes);
        }
    }
}

inline bool WalkHdf5ChunkBTreeV1(
    const Hdf5Ctx& ctx,
    uint64_t nodeAddr,
    const Hdf5DatasetInfo& ds,
    const std::vector<uint64_t>& cDims,
    size_t uncompChunkBytes,
    std::vector<uint8_t>& dstLinear,
    size_t& decodedChunks,
    int depth)
{
    if (depth > 16 || !ctx.AddrOk(nodeAddr, 8 + 2 * ctx.offSize)) return false;
    const uint8_t* p = ctx.Ptr(nodeAddr);
    if (std::memcmp(p, "TREE", 4) != 0 || p[4] != 1) return false; // Must be B-tree type 1 (Raw Data Chunks)
    uint8_t level = p[5];
    uint16_t entriesUsed = RdU16LE(p + 6);
    if (entriesUsed == 0 || entriesUsed > 16384) return false;

    const size_t keyDimCnt = ds.dims.size() + 1; // HDF5 v1 chunk B-tree stores rank + 1 uint64 offsets
    const size_t keySize = 8 + keyDimCnt * 8;
    size_t cur = 8 + 2 * ctx.offSize;

    for (uint16_t i = 0; i < entriesUsed && decodedChunks < 4096; ++i) {
        if (!ctx.AddrOk(nodeAddr + cur, keySize + ctx.offSize)) return false;
        const uint8_t* kp = p + cur;
        uint32_t chunkByteSize = RdU32LE(kp);
        uint32_t filterMask    = RdU32LE(kp + 4);
        std::vector<uint64_t> coords(ds.dims.size(), 0);
        for (size_t d = 0; d < ds.dims.size(); ++d) {
            coords[d] = RdU64LE(kp + 8 + d * 8);
        }
        cur += keySize;
        uint64_t childAddr = ctx.RdOff(p + cur);
        cur += ctx.offSize;

        if (level > 0) {
            if (!WalkHdf5ChunkBTreeV1(ctx, childAddr, ds, cDims, uncompChunkBytes, dstLinear, decodedChunks, depth + 1)) {
                return false;
            }
        } else {
            if (chunkByteSize == 0 || chunkByteSize > 32 * 1024 * 1024 || !ctx.AddrOk(childAddr, chunkByteSize)) {
                return false;
            }
            std::vector<uint8_t> decChunk;
            if (!ApplyHdf5ChunkFiltersReverse(ctx.Ptr(childAddr), chunkByteSize, filterMask, uncompChunkBytes, ds, decChunk)) {
                return false;
            }
            ScatterHdf5ChunkToLinear(decChunk.data(), decChunk.size(), coords, cDims, ds.dims, ds.dtSize, dstLinear);
            ++decodedChunks;
        }
    }
    return decodedChunks > 0;
}

inline bool ReconstructHdf5ChunkedDataset(
    const Hdf5Ctx& ctx,
    const Hdf5DatasetInfo& ds,
    uint64_t totalElems,
    std::vector<uint8_t>& outBuf)
{
    outBuf.clear();
    if (!ds.isChunked || ds.dtSize == 0 || ds.chunkAddr == UINT64_MAX) return false;

    std::vector<uint64_t> cDims = ds.chunkDims;
    if (cDims.size() == ds.dims.size() + 1) cDims.pop_back();
    if (cDims.size() != ds.dims.size()) return false;

    uint64_t chunkElems = 1;
    for (uint64_t cd : cDims) {
        if (cd == 0 || cd > 64 * 1024 * 1024) return false;
        chunkElems *= cd;
        if (chunkElems > 64 * 1024 * 1024) return false;
    }
    const size_t uncompChunkBytes = static_cast<size_t>(chunkElems) * ds.dtSize;
    const uint64_t cappedElems = std::min<uint64_t>(totalElems, (16ULL * 1024ULL * 1024ULL) / ds.dtSize);
    if (cappedElems == 0) return false;
    outBuf.assign(static_cast<size_t>(cappedElems) * ds.dtSize, 0);

    if (ds.chunkLayoutVer == 4 && ds.chunkIdxType == 1) { // v4 Single Chunk
        size_t compSz = (ds.singleChunkCompSize > 0) ? static_cast<size_t>(ds.singleChunkCompSize) : uncompChunkBytes;
        if (compSz == 0 || compSz > 32 * 1024 * 1024 || !ctx.AddrOk(ds.chunkAddr, compSz)) return false;
        std::vector<uint8_t> dec;
        if (!ApplyHdf5ChunkFiltersReverse(ctx.Ptr(ds.chunkAddr), compSz, ds.singleChunkFilterMask, uncompChunkBytes, ds, dec)) {
            return false;
        }
        std::vector<uint64_t> zeroCoords(ds.dims.size(), 0);
        ScatterHdf5ChunkToLinear(dec.data(), dec.size(), zeroCoords, cDims, ds.dims, ds.dtSize, outBuf);
        return true;
    }

    size_t decodedChunks = 0;
    return WalkHdf5ChunkBTreeV1(ctx, ds.chunkAddr, ds, cDims, uncompChunkBytes, outBuf, decodedChunks, 0);
}

inline bool ParseHdf5Doc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    static const uint8_t kSig[8] = { 0x89, 'H', 'D', 'F', '\r', '\n', 0x1A, '\n' };
    size_t sbOff = SIZE_MAX;
    for (size_t cand : { size_t(0), size_t(512), size_t(1024), size_t(2048) }) {
        if (cand + 36 <= raw.size() && std::memcmp(raw.data() + cand, kSig, 8) == 0) {
            sbOff = cand;
            break;
        }
    }
    if (sbOff == SIZE_MAX) return false;

    const uint8_t* sb = raw.data() + sbOff;
    uint8_t sbVer = sb[8];
    if (sbVer > 3) return false;

    Hdf5Ctx ctx;
    ctx.data = raw.data();
    ctx.size = raw.size();

    uint64_t rootObjAddr = UINT64_MAX;
    uint64_t rootBtree = UINT64_MAX, rootHeap = UINT64_MAX;

    if (sbVer == 0 || sbVer == 1) {
        ctx.offSize = sb[13];
        ctx.lenSize = sb[14];
        if ((ctx.offSize != 2 && ctx.offSize != 4 && ctx.offSize != 8) ||
            (ctx.lenSize != 2 && ctx.lenSize != 4 && ctx.lenSize != 8)) return false;
        size_t pos = 24;
        if (sbVer == 1) pos = 28;
        if (sbOff + pos + 4 * ctx.offSize + 24 > raw.size()) return false;
        ctx.baseAddr = ctx.RdOff(sb + pos);
        if (ctx.baseAddr == UINT64_MAX || ctx.baseAddr >= raw.size()) ctx.baseAddr = sbOff;
        pos += 4 * ctx.offSize;
        rootObjAddr = ctx.RdOff(sb + pos + ctx.offSize);
        uint32_t cacheType = RdU32LE(sb + pos + 2 * ctx.offSize);
        if (cacheType == 1 && sbOff + pos + 4 * ctx.offSize + 8 <= raw.size()) {
            rootBtree = ctx.RdOff(sb + pos + 2 * ctx.offSize + 8);
            rootHeap  = ctx.RdOff(sb + pos + 3 * ctx.offSize + 8);
        }
    } else {
        ctx.offSize = sb[9];
        ctx.lenSize = sb[10];
        if ((ctx.offSize != 2 && ctx.offSize != 4 && ctx.offSize != 8) ||
            (ctx.lenSize != 2 && ctx.lenSize != 4 && ctx.lenSize != 8)) return false;
        if (sbOff + 12 + 4 * ctx.offSize > raw.size()) return false;
        ctx.baseAddr = ctx.RdOff(sb + 12);
        if (ctx.baseAddr == UINT64_MAX || ctx.baseAddr >= raw.size()) ctx.baseAddr = sbOff;
        rootObjAddr = ctx.RdOff(sb + 12 + 3 * ctx.offSize);
    }

    std::vector<Hdf5DatasetInfo> datasets;
    std::vector<std::pair<std::wstring, std::wstring>> groupsAndAttrs;
    if (rootObjAddr != UINT64_MAX) {
        WalkHdf5Object(ctx, rootObjAddr, L"/", datasets, groupsAndAttrs, 0);
    }
    if (rootBtree != UINT64_MAX && rootHeap != UINT64_MAX) {
        WalkHdf5SnodOrBtree(ctx, rootBtree, rootHeap, L"/", datasets, groupsAndAttrs, 0);
    }
    if (datasets.empty() && groupsAndAttrs.empty()) {
        return false; // Corrupt/truncated HDF5 superblock with no valid root group or objects
    }

    SpreadsheetSheet overview;
    overview.name = L"HDF5_Datasets";
    overview.rows.push_back({ L"Object Path", L"Type", L"Shape", L"Storage / Preview" });
    overview.rows.push_back({ L"(superblock)", L"HDF5 v" + std::to_wstring(sbVer),
        L"off=" + std::to_wstring(ctx.offSize) + L"B, len=" + std::to_wstring(ctx.lenSize) + L"B",
        PathUtils::FormatByteSize(raw.size()) });

    std::vector<SpreadsheetSheet> dsSheets;
    std::vector<double> ncLatVals, ncLonVals;

    for (const auto& ds : datasets) {
        std::wstring shStr = L"[";
        uint64_t totalElems = 1;
        for (size_t i = 0; i < ds.dims.size(); ++i) {
            if (i > 0) shStr += L", ";
            shStr += std::to_wstring(ds.dims[i]);
            totalElems *= ds.dims[i];
        }
        shStr += L"]";

        std::vector<uint8_t> chunkReconstructed;
        const uint8_t* rawBuf = nullptr;
        size_t rawAvail = 0;
        if (!ds.compactData.empty()) {
            rawBuf = ds.compactData.data();
            rawAvail = ds.compactData.size();
        } else if (ds.isChunked && totalElems > 0) {
            if (!ReconstructHdf5ChunkedDataset(ctx, ds, totalElems, chunkReconstructed)) {
                return false; // Corrupt chunk B-tree or corrupt deflate/shuffle stream!
            }
            rawBuf = chunkReconstructed.data();
            rawAvail = chunkReconstructed.size();
        } else if (ds.contigAddr != UINT64_MAX) {
            if (!ctx.AddrOk(ds.contigAddr, 1)) return false;
            rawBuf = ctx.Ptr(ds.contigAddr);
            rawAvail = std::min<uint64_t>(ds.contigSize > 0 ? ds.contigSize : (ctx.size - (ctx.baseAddr + ds.contigAddr)),
                                          ctx.size - static_cast<size_t>(ctx.baseAddr + ds.contigAddr));
        }

        auto rdDoubleVal = [&](size_t idx) -> double {
            const uint8_t* p = rawBuf + idx * ds.dtSize;
            if (ds.dtClass == 1) {
                if (ds.dtSize == 2) return HalfToFloat(ds.dtBigEndian ? RdU16BE(p) : RdU16LE(p));
                if (ds.dtSize == 4) return ds.dtBigEndian ? RdF32BE(p) : RdF32LE(p);
                if (ds.dtSize == 8) return ds.dtBigEndian ? RdF64BE(p) : RdF64LE(p);
            } else if (ds.dtClass == 0) {
                if (ds.dtSigned) {
                    return static_cast<double>(
                        (ds.dtSize == 1) ? static_cast<int8_t>(p[0]) :
                        (ds.dtSize == 2) ? (ds.dtBigEndian ? RdI16BE(p) : RdI16LE(p)) :
                        (ds.dtSize == 4) ? (ds.dtBigEndian ? RdI32BE(p) : RdI32LE(p)) :
                                           (ds.dtBigEndian ? RdI64BE(p) : RdI64LE(p)));
                }
                return static_cast<double>(
                    (ds.dtSize == 1) ? p[0] :
                    (ds.dtSize == 2) ? (ds.dtBigEndian ? RdU16BE(p) : RdU16LE(p)) :
                    (ds.dtSize == 4) ? (ds.dtBigEndian ? RdU32BE(p) : RdU32LE(p)) :
                                       (ds.dtBigEndian ? RdU64BE(p) : RdU64LE(p)));
            }
            return 0.0;
        };

        auto rdVal = [&](size_t idx) -> std::wstring {
            const uint8_t* p = rawBuf + idx * ds.dtSize;
            if (ds.dtClass == 1) {
                return FmtDouble(rdDoubleVal(idx));
            } else if (ds.dtClass == 0) {
                if (ds.dtSigned) {
                    int64_t v = (ds.dtSize == 1) ? static_cast<int8_t>(p[0]) :
                                (ds.dtSize == 2) ? (ds.dtBigEndian ? RdI16BE(p) : RdI16LE(p)) :
                                (ds.dtSize == 4) ? (ds.dtBigEndian ? RdI32BE(p) : RdI32LE(p)) :
                                                   (ds.dtBigEndian ? RdI64BE(p) : RdI64LE(p));
                    return std::to_wstring(v);
                } else {
                    uint64_t v = (ds.dtSize == 1) ? p[0] :
                                 (ds.dtSize == 2) ? (ds.dtBigEndian ? RdU16BE(p) : RdU16LE(p)) :
                                 (ds.dtSize == 4) ? (ds.dtBigEndian ? RdU32BE(p) : RdU32LE(p)) :
                                                    (ds.dtBigEndian ? RdU64BE(p) : RdU64LE(p));
                    return std::to_wstring(v);
                }
            } else if (ds.dtClass == 3) {
                return CleanAsciiFixed(p, ds.dtSize);
            }
            return L"";
        };

        std::wstring preview;
        if (rawBuf && ds.dtSize > 0 && rawAvail >= ds.dtSize) {
            size_t cnt = static_cast<size_t>(std::min<uint64_t>(totalElems, rawAvail / ds.dtSize));
            for (size_t i = 0; i < std::min<size_t>(cnt, 4); ++i) {
                if (i > 0) preview += L", ";
                preview += rdVal(i);
            }

            // Capture lat/lon coordinate vectors for NetCDF-4 map tracks
            std::wstring lowLeaf = PathUtils::ToLower(ds.path);
            size_t slash = lowLeaf.find_last_of(L'/');
            if (slash != std::wstring::npos) lowLeaf = lowLeaf.substr(slash + 1);
            if ((ds.dtClass == 0 || ds.dtClass == 1) && ds.dims.size() == 1 && cnt > 0) {
                if (lowLeaf == L"lat" || lowLeaf == L"latitude") {
                    for (size_t i = 0; i < std::min<size_t>(cnt, 2000); ++i) ncLatVals.push_back(rdDoubleVal(i));
                } else if (lowLeaf == L"lon" || lowLeaf == L"longitude") {
                    for (size_t i = 0; i < std::min<size_t>(cnt, 2000); ++i) ncLonVals.push_back(rdDoubleVal(i));
                }
            }

            if (dsSheets.size() < 8 && cnt > 0 && (ds.dtClass == 0 || ds.dtClass == 1 || ds.dtClass == 3)) {
                SpreadsheetSheet dsh;
                dsh.name = ds.path.size() > 1 ? ds.path.substr(1) : L"Dataset";
                if (ds.dims.size() == 2 && ds.dims[0] > 0 && ds.dims[1] > 0) {
                    size_t nr = static_cast<size_t>(std::min<uint64_t>(ds.dims[0], 128));
                    size_t nc = static_cast<size_t>(std::min<uint64_t>(ds.dims[1], 32));
                    std::vector<std::wstring> hr = { L"Row \\ Col" };
                    for (size_t c = 0; c < nc; ++c) hr.push_back(L"[" + std::to_wstring(c) + L"]");
                    dsh.rows.push_back(std::move(hr));
                    for (size_t r = 0; r < nr; ++r) {
                        std::vector<std::wstring> rv = { L"[" + std::to_wstring(r) + L"]" };
                        for (size_t c = 0; c < nc; ++c) {
                            uint64_t idx = r * ds.dims[1] + c;
                            rv.push_back(idx < cnt ? rdVal(static_cast<size_t>(idx)) : L"");
                        }
                        dsh.rows.push_back(std::move(rv));
                    }
                } else {
                    dsh.rows.push_back({ L"Index", ds.path + L" (" + ds.dtypeDesc + L")" });
                    for (size_t i = 0; i < std::min<size_t>(cnt, 256); ++i) {
                        dsh.rows.push_back({ std::to_wstring(i), rdVal(i) });
                    }
                }
                FinalizeSheetCols(dsh);
                dsSheets.push_back(std::move(dsh));
            }
        } else {
            preview = L"(empty/unallocated)";
        }
        overview.rows.push_back({ ds.path, ds.dtypeDesc, shStr, preview });
    }
    for (const auto& ga : groupsAndAttrs) {
        if (overview.rows.size() >= 400) break;
        overview.rows.push_back({ ga.first, ga.second == L"Group" ? L"Group" : L"Attribute", L"", ga.second });
    }
    FinalizeSheetCols(overview);

    if (!ncLatVals.empty() && ncLatVals.size() == ncLonVals.size()) {
        for (size_t i = 0; i < ncLatVals.size(); ++i) {
            double lonNorm = (ncLonVals[i] > 180.0 && ncLonVals[i] <= 360.0) ? (ncLonVals[i] - 360.0) : ncLonVals[i];
            if (std::fabs(ncLatVals[i]) <= 90.0 && std::fabs(lonNorm) <= 180.0) {
                GpsTrackPoint gp;
                gp.lat = ncLatVals[i];
                gp.lon = lonNorm;
                gp.rowIndex = static_cast<int>(i + 1);
                out.geoPoints.push_back(std::move(gp));
            }
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = (extLower == L".mat") ? L"MATLAB v7.3 HDF5 Workspace (.mat)" :
                      (extLower == L".nc" || extLower == L".cdf") ? L"NetCDF-4 HDF5 Dataset (" + extLower + L")" :
                      L"HDF5 Hierarchical Data Format (" + extLower + L")";
    out.sheets.push_back(std::move(overview));
    for (auto& s : dsSheets) out.sheets.push_back(std::move(s));
    out.valid = true;
    return true;
}

// ============================================================================
// 10. MATLAB Level 5 MAT-File (.mat — Numeric, Char, Cell, Struct & Sparse)
// ============================================================================
inline bool ReadMatTag(const uint8_t* data, size_t size, size_t& pos, bool be,
    uint32_t& outType, const uint8_t*& outPayload, size_t& outLen)
{
    if (pos + 4 > size) return false;
    uint32_t w0 = be ? RdU32BE(data + pos) : RdU32LE(data + pos);
    if ((w0 >> 16) != 0) { // Small Data Element (4B header + 4B inline)
        if (pos + 8 > size) return false;
        outLen = w0 >> 16;
        outType = w0 & 0xFFFFu;
        if (outLen > 4) return false;
        outPayload = data + pos + 4;
        pos += 8;
        return true;
    }
    if (pos + 8 > size) return false;
    outType = w0;
    uint32_t nBytes = be ? RdU32BE(data + pos + 4) : RdU32LE(data + pos + 4);
    pos += 8;
    if (nBytes > size - pos) return false;
    outPayload = data + pos;
    outLen = nBytes;
    size_t padded = (static_cast<size_t>(nBytes) + 7u) & ~size_t(7u);
    pos += (outType == 14 || outType == 15) ? nBytes : std::min(size - pos, padded);
    return true;
}

inline std::wstring ReadMatNumericAt(uint32_t t, const uint8_t* p, size_t l, bool be, size_t idx, size_t& elemStep) {
    switch (t) {
        case 1:  elemStep = 1; return (idx < l) ? std::to_wstring(static_cast<int8_t>(p[idx])) : L"";
        case 2:  elemStep = 1; return (idx < l) ? std::to_wstring(p[idx]) : L"";
        case 3:  elemStep = 2; return (idx * 2 + 2 <= l) ? std::to_wstring(be ? RdI16BE(p + idx * 2) : RdI16LE(p + idx * 2)) : L"";
        case 4:  elemStep = 2; return (idx * 2 + 2 <= l) ? std::to_wstring(be ? RdU16BE(p + idx * 2) : RdU16LE(p + idx * 2)) : L"";
        case 5:  elemStep = 4; return (idx * 4 + 4 <= l) ? std::to_wstring(be ? RdI32BE(p + idx * 4) : RdI32LE(p + idx * 4)) : L"";
        case 6:  elemStep = 4; return (idx * 4 + 4 <= l) ? std::to_wstring(be ? RdU32BE(p + idx * 4) : RdU32LE(p + idx * 4)) : L"";
        case 7:  elemStep = 4; return (idx * 4 + 4 <= l) ? FmtDouble(be ? RdF32BE(p + idx * 4) : RdF32LE(p + idx * 4)) : L"";
        case 9:  elemStep = 8; return (idx * 8 + 8 <= l) ? FmtDouble(be ? RdF64BE(p + idx * 8) : RdF64LE(p + idx * 8)) : L"";
        case 12: elemStep = 8; return (idx * 8 + 8 <= l) ? std::to_wstring(be ? RdI64BE(p + idx * 8) : RdI64LE(p + idx * 8)) : L"";
        case 13: elemStep = 8; return (idx * 8 + 8 <= l) ? std::to_wstring(be ? RdU64BE(p + idx * 8) : RdU64LE(p + idx * 8)) : L"";
        default: elemStep = 0; return L"";
    }
}

// Compact inline formatter for nested miMATRIX elements inside MATLAB cell & struct arrays
inline std::wstring SummarizeMatNestedMatrix(const uint8_t* mdata, size_t msize, bool be, int depth = 0) {
    if (!mdata || msize < 16 || depth > 6) return L"[]";
    size_t pos = 0;
    uint32_t t = 0; const uint8_t* p = nullptr; size_t l = 0;
    if (!ReadMatTag(mdata, msize, pos, be, t, p, l) || l < 8) return L"[]";
    uint8_t mxClass = static_cast<uint8_t>((be ? RdU32BE(p) : RdU32LE(p)) & 0xFFu);
    if (!ReadMatTag(mdata, msize, pos, be, t, p, l) || l < 4) return L"[]";
    uint64_t totalElems = 1;
    for (size_t i = 0; i + 4 <= l; i += 4) {
        totalElems *= static_cast<uint64_t>(std::max<int32_t>(0, be ? RdI32BE(p + i) : RdI32LE(p + i)));
    }
    if (!ReadMatTag(mdata, msize, pos, be, t, p, l)) return L"[]"; // skip name
    if (totalElems == 0) return L"[]";

    if (mxClass == 4) { // char
        if (!ReadMatTag(mdata, msize, pos, be, t, p, l)) return L"''";
        if (t == 16 || t == 1 || t == 2) return CleanAsciiFixed(p, std::min<size_t>(l, 128));
        std::wstring ws;
        for (size_t i = 0; i + 2 <= std::min<size_t>(l, 256); i += 2) {
            uint16_t ch = be ? RdU16BE(p + i) : RdU16LE(p + i);
            if (ch == 0) break;
            ws.push_back(static_cast<wchar_t>(ch));
        }
        return ws;
    }
    if (mxClass >= 6 && mxClass <= 15) {
        if (!ReadMatTag(mdata, msize, pos, be, t, p, l)) return L"[]";
        size_t step = 0;
        ReadMatNumericAt(t, p, l, be, 0, step);
        if (step == 0 || l < step) return L"[]";
        size_t cnt = l / step;
        if (cnt == 1) return ReadMatNumericAt(t, p, l, be, 0, step);
        std::wstring s = L"[";
        for (size_t i = 0; i < std::min<size_t>(cnt, 4); ++i) {
            if (i) s += L", ";
            s += ReadMatNumericAt(t, p, l, be, i, step);
        }
        if (cnt > 4) s += L", ...";
        s += L"]";
        return s;
    }
    if (mxClass == 1) { // nested cell
        std::wstring s = L"{";
        for (uint64_t i = 0; i < std::min<uint64_t>(totalElems, 4) && pos < msize; ++i) {
            if (!ReadMatTag(mdata, msize, pos, be, t, p, l) || t != 14) break;
            size_t pad = ((l + 7u) & ~size_t(7u)) - l;
            if (pad <= msize - pos) pos += pad;
            if (i) s += L", ";
            s += SummarizeMatNestedMatrix(p, l, be, depth + 1);
        }
        s += L"}";
        return s;
    }
    if (mxClass == 2) return L"<struct>";
    if (mxClass == 5) return L"<sparse>";
    return L"<array>";
}

inline bool ParseMatMatrixElement(
    const uint8_t* mdata, size_t msize, bool be,
    SpreadsheetSheet& summarySh, std::vector<SpreadsheetSheet>& varSheets)
{
    size_t pos = 0;
    uint32_t t = 0; const uint8_t* p = nullptr; size_t l = 0;

    // 1. Array Flags (miUINT32 = 6, 8 bytes)
    if (!ReadMatTag(mdata, msize, pos, be, t, p, l) || l < 8) return false;
    uint32_t flags0 = be ? RdU32BE(p) : RdU32LE(p);
    uint32_t nzmax  = be ? RdU32BE(p + 4) : RdU32LE(p + 4);
    uint8_t mxClass = static_cast<uint8_t>(flags0 & 0xFFu);
    if (mxClass == 0 || mxClass > 17) return false;

    // 2. Dimensions Array (miINT32 = 5)
    if (!ReadMatTag(mdata, msize, pos, be, t, p, l) || l < 4) return false;
    std::vector<int32_t> dims;
    for (size_t i = 0; i + 4 <= l; i += 4) dims.push_back(be ? RdI32BE(p + i) : RdI32LE(p + i));

    // 3. Array Name
    if (!ReadMatTag(mdata, msize, pos, be, t, p, l)) return false;
    std::wstring varName = CleanAsciiFixed(p, l);
    if (varName.empty()) varName = L"ans";

    std::wstring dimStr = L"[";
    uint64_t totalElems = 1;
    for (size_t i = 0; i < dims.size(); ++i) {
        if (i > 0) dimStr += L"x";
        dimStr += std::to_wstring(dims[i]);
        totalElems *= static_cast<uint64_t>(std::max<int32_t>(0, dims[i]));
    }
    dimStr += L"]";

    const wchar_t* clsNames[] = {
        L"unknown", L"cell", L"struct", L"object", L"char", L"sparse", L"double", L"single",
        L"int8", L"uint8", L"int16", L"uint16", L"int32", L"uint32", L"int64", L"uint64"
    };
    std::wstring clsStr = (mxClass < 16) ? clsNames[mxClass] : L"array";
    std::wstring preview;

    auto readChildMatrixAligned = [&](const uint8_t*& cp, size_t& cl) -> bool {
        uint32_t ct = 0;
        if (!ReadMatTag(mdata, msize, pos, be, ct, cp, cl)) return false;
        if (ct != 14) return false;
        size_t pad = ((cl + 7u) & ~size_t(7u)) - cl;
        if (pad <= msize - pos) pos += pad;
        return true;
    };

    if (mxClass == 1 && totalElems > 0) { // mxCELL_CLASS
        size_t nRows = dims.size() >= 1 ? static_cast<size_t>(std::max<int32_t>(1, dims[0])) : 1;
        size_t nCols = dims.size() >= 2 ? static_cast<size_t>(std::max<int32_t>(1, dims[1])) : 1;
        size_t cap = static_cast<size_t>(std::min<uint64_t>(totalElems, 1024));
        std::vector<std::wstring> cells;
        cells.reserve(cap);
        for (size_t i = 0; i < cap && pos < msize; ++i) {
            const uint8_t* cp = nullptr; size_t cl = 0;
            if (!readChildMatrixAligned(cp, cl)) break;
            cells.push_back(SummarizeMatNestedMatrix(cp, cl, be, 0));
        }
        preview = L"{";
        for (size_t i = 0; i < std::min<size_t>(cells.size(), 4); ++i) {
            if (i) preview += L", ";
            preview += cells[i];
        }
        if (cells.size() > 4) preview += L", ...";
        preview += L"}";

        if (varSheets.size() < 16 && !cells.empty()) {
            SpreadsheetSheet vsh;
            vsh.name = varName;
            size_t rCap = std::min<size_t>(nRows, 128);
            size_t cCap = std::min<size_t>(nCols, 32);
            std::vector<std::wstring> hr = { L"Row \\ Col" };
            for (size_t c = 0; c < cCap; ++c) hr.push_back(L"{:," + std::to_wstring(c + 1) + L"}");
            vsh.rows.push_back(std::move(hr));
            for (size_t r = 0; r < rCap; ++r) {
                std::vector<std::wstring> rv = { L"{" + std::to_wstring(r + 1) + L",:}" };
                for (size_t c = 0; c < cCap; ++c) {
                    size_t idx = r + c * nRows; // column-major
                    rv.push_back(idx < cells.size() ? cells[idx] : L"");
                }
                vsh.rows.push_back(std::move(rv));
            }
            FinalizeSheetCols(vsh);
            varSheets.push_back(std::move(vsh));
        }
    } else if (mxClass == 2 && totalElems > 0) { // mxSTRUCT_CLASS
        // 4. Field Name Length (miINT32 = 5)
        if (!ReadMatTag(mdata, msize, pos, be, t, p, l) || l < 4) return false;
        int32_t maxFieldLen = be ? RdI32BE(p) : RdI32LE(p);
        if (maxFieldLen <= 0 || maxFieldLen > 256) return false;
        // 5. Field Names (miINT8 = 1)
        if (!ReadMatTag(mdata, msize, pos, be, t, p, l) || (l % static_cast<size_t>(maxFieldLen)) != 0) return false;
        size_t nFields = l / static_cast<size_t>(maxFieldLen);
        std::vector<std::wstring> fieldNames;
        fieldNames.reserve(nFields);
        for (size_t f = 0; f < nFields; ++f) {
            fieldNames.push_back(CleanAsciiFixed(p + f * static_cast<size_t>(maxFieldLen), static_cast<size_t>(maxFieldLen)));
        }

        SpreadsheetSheet vsh;
        vsh.name = varName;
        std::vector<std::wstring> hr = { L"Element #" };
        for (const auto& fn : fieldNames) hr.push_back(fn);
        vsh.rows.push_back(std::move(hr));

        size_t elemCap = static_cast<size_t>(std::min<uint64_t>(totalElems, 256));
        for (size_t e = 0; e < elemCap && pos < msize; ++e) {
            std::vector<std::wstring> rv = { std::to_wstring(e + 1) };
            for (size_t f = 0; f < nFields; ++f) {
                const uint8_t* cp = nullptr; size_t cl = 0;
                if (!readChildMatrixAligned(cp, cl)) { rv.push_back(L""); continue; }
                std::wstring valStr = SummarizeMatNestedMatrix(cp, cl, be, 0);
                if (e == 0 && f < 3) {
                    if (!preview.empty()) preview += L", ";
                    preview += fieldNames[f] + L"=" + valStr;
                }
                rv.push_back(std::move(valStr));
            }
            vsh.rows.push_back(std::move(rv));
        }
        if (varSheets.size() < 16 && vsh.rows.size() > 1) {
            FinalizeSheetCols(vsh);
            varSheets.push_back(std::move(vsh));
        }
    } else if (mxClass == 5) { // mxSPARSE_CLASS (CSC: ir, jc, pr)
        const uint8_t *irPtr = nullptr, *jcPtr = nullptr, *prPtr = nullptr;
        size_t irLen = 0, jcLen = 0, prLen = 0;
        uint32_t irType = 0, jcType = 0, prType = 0;
        if (!ReadMatTag(mdata, msize, pos, be, irType, irPtr, irLen)) return false;
        if (!ReadMatTag(mdata, msize, pos, be, jcType, jcPtr, jcLen)) return false;
        if (!ReadMatTag(mdata, msize, pos, be, prType, prPtr, prLen)) return false;

        size_t nCols = dims.size() >= 2 ? static_cast<size_t>(std::max<int32_t>(0, dims[1])) : 0;
        size_t jcCount = jcLen / 4;
        size_t irCount = irLen / 4;
        size_t prStep = 0;
        ReadMatNumericAt(prType, prPtr, prLen, be, 0, prStep);
        size_t prCount = (prStep > 0) ? (prLen / prStep) : 0;

        SpreadsheetSheet vsh;
        vsh.name = varName;
        vsh.rows.push_back({ L"Row (1-based)", L"Col (1-based)", L"Non-Zero Value" });

        size_t nnzActual = 0;
        for (size_t c = 0; c < nCols && c + 1 < jcCount; ++c) {
            int32_t kStart = be ? RdI32BE(jcPtr + c * 4) : RdI32LE(jcPtr + c * 4);
            int32_t kEnd   = be ? RdI32BE(jcPtr + (c + 1) * 4) : RdI32LE(jcPtr + (c + 1) * 4);
            if (kStart < 0 || kEnd < kStart) continue;
            for (int32_t k = kStart; k < kEnd && static_cast<size_t>(k) < irCount && static_cast<size_t>(k) < prCount; ++k) {
                int32_t r0 = be ? RdI32BE(irPtr + static_cast<size_t>(k) * 4) : RdI32LE(irPtr + static_cast<size_t>(k) * 4);
                std::wstring vStr = ReadMatNumericAt(prType, prPtr, prLen, be, static_cast<size_t>(k), prStep);
                if (vsh.rows.size() <= 500) {
                    vsh.rows.push_back({ std::to_wstring(r0 + 1), std::to_wstring(c + 1), vStr });
                }
                if (nnzActual < 3) {
                    if (!preview.empty()) preview += L", ";
                    preview += L"(" + std::to_wstring(r0 + 1) + L"," + std::to_wstring(c + 1) + L")=" + vStr;
                }
                ++nnzActual;
            }
        }
        preview = L"nnz=" + std::to_wstring(nnzActual) + L" (nzmax=" + std::to_wstring(nzmax) + L") " + preview;
        if (varSheets.size() < 16 && vsh.rows.size() > 1) {
            FinalizeSheetCols(vsh);
            varSheets.push_back(std::move(vsh));
        }
    } else if (pos < msize && ReadMatTag(mdata, msize, pos, be, t, p, l)) {
        if (mxClass == 4) { // mxCHAR_CLASS
            if (t == 16 || t == 1 || t == 2) {
                preview = CleanAsciiFixed(p, std::min<size_t>(l, 256));
            } else if (t == 4 || t == 17) {
                for (size_t i = 0; i + 2 <= std::min<size_t>(l, 512); i += 2) {
                    uint16_t ch = be ? RdU16BE(p + i) : RdU16LE(p + i);
                    if (ch == 0) break;
                    preview.push_back(static_cast<wchar_t>(ch));
                }
            }
            if (varSheets.size() < 16) {
                SpreadsheetSheet vsh;
                vsh.name = varName;
                vsh.rows.push_back({ varName + L" (char " + dimStr + L")" });
                vsh.rows.push_back({ preview });
                FinalizeSheetCols(vsh);
                varSheets.push_back(std::move(vsh));
            }
        } else if (mxClass >= 6 && mxClass <= 15 && totalElems > 0) {
            size_t step = 0;
            ReadMatNumericAt(t, p, l, be, 0, step);
            if (step > 0 && l >= step) {
                size_t availElems = l / step;
                for (size_t i = 0; i < std::min<size_t>(availElems, 4); ++i) {
                    if (i > 0) preview += L", ";
                    preview += ReadMatNumericAt(t, p, l, be, i, step);
                }
                if (varSheets.size() < 16) {
                    SpreadsheetSheet vsh;
                    vsh.name = varName;
                    size_t nRows = dims.size() >= 1 ? static_cast<size_t>(std::max<int32_t>(1, dims[0])) : 1;
                    size_t nCols = dims.size() >= 2 ? static_cast<size_t>(std::max<int32_t>(1, dims[1])) : 1;
                    size_t rCap = std::min<size_t>(nRows, 256);
                    size_t cCap = std::min<size_t>(nCols, 64);
                    std::vector<std::wstring> hr = { L"Row \\ Col" };
                    for (size_t c = 0; c < cCap; ++c) hr.push_back(L"(:," + std::to_wstring(c + 1) + L")");
                    vsh.rows.push_back(std::move(hr));
                    for (size_t r = 0; r < rCap; ++r) {
                        std::vector<std::wstring> rv = { L"(" + std::to_wstring(r + 1) + L",:)" };
                        for (size_t c = 0; c < cCap; ++c) {
                            size_t idx = r + c * nRows;
                            rv.push_back(idx < availElems ? ReadMatNumericAt(t, p, l, be, idx, step) : L"");
                        }
                        vsh.rows.push_back(std::move(rv));
                    }
                    FinalizeSheetCols(vsh);
                    varSheets.push_back(std::move(vsh));
                }
            }
        }
    }

    summarySh.rows.push_back({ varName, clsStr, dimStr, preview });
    return true;
}

inline bool ParseMatlabMat(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    // Check v7.3 HDF5 first (at 0 or 512)
    if ((raw.size() >= 36 && std::memcmp(raw.data(), "\x89HDF\r\n\x1A\n", 8) == 0) ||
        (raw.size() >= 548 && std::memcmp(raw.data() + 512, "\x89HDF\r\n\x1A\n", 8) == 0))
    {
        return ParseHdf5Doc(raw, L".mat", out);
    }

    if (raw.size() < 128) return false;
    bool be = false;
    if (raw[126] == 'I' && raw[127] == 'M') be = false;
    else if (raw[126] == 'M' && raw[127] == 'I') be = true;
    else return false;

    uint16_t matVer = be ? RdU16BE(raw.data() + 124) : RdU16LE(raw.data() + 124);
    if (matVer != 0x0100 && matVer != 0x0200) return false;

    SpreadsheetSheet summary;
    summary.name = L"MAT_Workspace";
    summary.rows.push_back({ L"Variable", L"MATLAB Class", L"Dimensions", L"Preview" });
    summary.rows.push_back({ L"(header)", L"Level 5 MAT", be ? L"Big-Endian (MI)" : L"Little-Endian (IM)", CleanAsciiFixed(raw.data(), 116) });

    std::vector<SpreadsheetSheet> varSheets;
    size_t pos = 128;
    size_t validVars = 0;
    while (pos + 8 <= raw.size()) {
        uint32_t t = 0; const uint8_t* p = nullptr; size_t l = 0;
        if (!ReadMatTag(raw.data(), raw.size(), pos, be, t, p, l)) return false;
        if (t == 14) { // miMATRIX
            if (!ParseMatMatrixElement(p, l, be, summary, varSheets)) return false;
            ++validVars;
        } else if (t == 15) { // miCOMPRESSED (zlib)
            std::vector<uint8_t> uncomp;
            if (!ZipDeflate::InflateZlib(p, l, uncomp, 32 * 1024 * 1024) &&
                !ZipDeflate::InflateRaw(p, l, uncomp, 32 * 1024 * 1024))
            {
                return false; // Corrupt zlib compressed variable!
            }
            size_t ipos = 0; uint32_t it = 0; const uint8_t* ip = nullptr; size_t il = 0;
            if (!ReadMatTag(uncomp.data(), uncomp.size(), ipos, be, it, ip, il) || it != 14) return false;
            if (!ParseMatMatrixElement(ip, il, be, summary, varSheets)) return false;
            ++validVars;
        } else {
            return false;
        }
        pos = (pos + 7u) & ~size_t(7u);
    }

    if (validVars == 0) return false;

    FinalizeSheetCols(summary);
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"MATLAB Level 5 Workspace (.mat)";
    out.sheets.push_back(std::move(summary));
    for (auto& s : varSheets) out.sheets.push_back(std::move(s));
    out.valid = true;
    return true;
}

// ============================================================================
// 11. SPSS System File (.sav — $FL2 / $FL3)
// ============================================================================
inline bool ParseSpssSav(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 176) return false;
    if (std::memcmp(raw.data(), "$FL2", 4) != 0 && std::memcmp(raw.data(), "$FL3", 4) != 0) return false;

    int32_t layoutLE = RdI32LE(raw.data() + 60);
    int32_t layoutBE = RdI32BE(raw.data() + 60);
    bool be = false;
    if (layoutLE == 2 || layoutLE == 3) be = false;
    else if (layoutBE == 2 || layoutBE == 3) be = true;
    else return false;

    auto rdI32 = [&](size_t off) { return be ? RdI32BE(raw.data() + off) : RdI32LE(raw.data() + off); };
    auto rdF64 = [&](size_t off) { return be ? RdF64BE(raw.data() + off) : RdF64LE(raw.data() + off); };

    int32_t nominalCaseSize = rdI32(64);
    int32_t compressed = rdI32(68);
    int32_t ncases = rdI32(76);
    double bias = rdF64(80);
    std::wstring cDate = CleanAsciiFixed(raw.data() + 88, 9);
    std::wstring cTime = CleanAsciiFixed(raw.data() + 97, 8);
    std::wstring fileLabel = CleanAsciiFixed(raw.data() + 105, 64);

    struct SavCol {
        std::string shortName;
        std::wstring displayName;
        std::wstring label;
        int32_t width = 0; // 0 = numeric, >0 = string bytes
        size_t slotCount = 1;
        std::map<double, std::wstring> numValLabels;
    };
    std::vector<SavCol> cols;
    std::vector<int> slotToCol; // >=0 logical col index (head), -1 continuation slot

    size_t pos = 176;
    bool foundEnd = false;
    std::vector<std::pair<double, std::wstring>> pendingLabels;

    while (pos + 4 <= raw.size()) {
        int32_t recType = rdI32(pos);
        pos += 4;
        if (recType == 999) {
            if (pos + 4 <= raw.size()) pos += 4; // filler int32
            foundEnd = true;
            break;
        }
        if (recType == 2) { // Variable Record (28B remaining fixed)
            if (pos + 28 > raw.size()) return false;
            int32_t vType = rdI32(pos);
            int32_t hasLbl = rdI32(pos + 4);
            int32_t nMiss = rdI32(pos + 8);
            std::string sname(reinterpret_cast<const char*>(raw.data() + pos + 20), 8);
            while (!sname.empty() && (sname.back() == ' ' || sname.back() == '\0')) sname.pop_back();
            pos += 28;

            std::wstring vLabel;
            if (hasLbl == 1) {
                if (pos + 4 > raw.size()) return false;
                int32_t lblLen = rdI32(pos);
                pos += 4;
                if (lblLen < 0 || static_cast<size_t>(lblLen) > raw.size() - pos) return false;
                vLabel = CleanAsciiFixed(raw.data() + pos, static_cast<size_t>(lblLen));
                pos += (static_cast<size_t>(lblLen) + 3u) & ~size_t(3u);
            }
            if (nMiss != 0) {
                size_t mSlots = static_cast<size_t>(std::abs(nMiss));
                if (mSlots * 8 > raw.size() - pos) return false;
                pos += mSlots * 8;
            }

            if (vType == -1) { // Continuation slot of previous string variable
                slotToCol.push_back(-1);
                if (!cols.empty()) cols.back().slotCount++;
            } else if (vType >= 0) {
                slotToCol.push_back(static_cast<int>(cols.size()));
                SavCol c;
                c.shortName = sname;
                c.displayName = PathUtils::Utf8ToWide(sname);
                c.label = vLabel;
                c.width = vType;
                c.slotCount = 1;
                cols.push_back(std::move(c));
            } else {
                return false;
            }
        } else if (recType == 3) { // Value Labels
            if (pos + 4 > raw.size()) return false;
            int32_t lblCnt = rdI32(pos);
            pos += 4;
            pendingLabels.clear();
            if (lblCnt < 0 || lblCnt > 65536) return false;
            for (int32_t i = 0; i < lblCnt; ++i) {
                if (pos + 9 > raw.size()) return false;
                double v = rdF64(pos);
                uint8_t slen = raw[pos + 8];
                pos += 8;
                size_t adv = ((static_cast<size_t>(slen) + 1u + 7u) & ~size_t(7u));
                if (adv > raw.size() - pos) return false;
                pendingLabels.emplace_back(v, CleanAsciiFixed(raw.data() + pos + 1, slen));
                pos += adv;
            }
        } else if (recType == 4) { // Value Label Variable Indices (1-based into slotToCol)
            if (pos + 4 > raw.size()) return false;
            int32_t vCnt = rdI32(pos);
            pos += 4;
            if (vCnt < 0 || static_cast<size_t>(vCnt) * 4 > raw.size() - pos) return false;
            for (int32_t i = 0; i < vCnt; ++i) {
                int32_t slotIdx1 = rdI32(pos + static_cast<size_t>(i) * 4);
                if (slotIdx1 >= 1 && static_cast<size_t>(slotIdx1 - 1) < slotToCol.size()) {
                    int colIdx = slotToCol[static_cast<size_t>(slotIdx1 - 1)];
                    if (colIdx >= 0 && static_cast<size_t>(colIdx) < cols.size()) {
                        for (const auto& kv : pendingLabels) cols[static_cast<size_t>(colIdx)].numValLabels[kv.first] = kv.second;
                    }
                }
            }
            pos += static_cast<size_t>(vCnt) * 4;
        } else if (recType == 6) { // Document Record
            if (pos + 4 > raw.size()) return false;
            int32_t nLines = rdI32(pos);
            pos += 4;
            if (nLines < 0 || static_cast<size_t>(nLines) * 80 > raw.size() - pos) return false;
            pos += static_cast<size_t>(nLines) * 80;
        } else if (recType == 7) { // Extended Info Record
            if (pos + 12 > raw.size()) return false;
            int32_t subType = rdI32(pos);
            int32_t elemSize = rdI32(pos + 4);
            int32_t elemCnt  = rdI32(pos + 8);
            pos += 12;
            if (elemSize <= 0 || elemCnt < 0 || static_cast<uint64_t>(elemSize) * static_cast<uint64_t>(elemCnt) > raw.size() - pos) return false;
            size_t totalBytes = static_cast<size_t>(elemSize) * static_cast<size_t>(elemCnt);
            if (subType == 13) { // Long Variable Names: SHORT=LongName\tSHORT2=LongName2
                std::string mapStr(reinterpret_cast<const char*>(raw.data() + pos), totalBytes);
                size_t cur = 0;
                while (cur < mapStr.size()) {
                    size_t tab = mapStr.find('\t', cur);
                    std::string pair = mapStr.substr(cur, tab == std::string::npos ? std::string::npos : (tab - cur));
                    size_t eq = pair.find('=');
                    if (eq != std::string::npos) {
                        std::string k = pair.substr(0, eq);
                        std::string v = pair.substr(eq + 1);
                        while (!v.empty() && (v.back() == '\0' || v.back() == ' ')) v.pop_back();
                        for (auto& c : cols) {
                            if (c.shortName == k && !v.empty()) c.displayName = PathUtils::Utf8ToWide(v);
                        }
                    }
                    if (tab == std::string::npos) break;
                    cur = tab + 1;
                }
            }
            pos += totalBytes;
        } else {
            return false;
        }
    }

    if (!foundEnd || cols.empty()) return false;

    SpreadsheetSheet varSh;
    varSh.name = L"SPSS_Variables";
    varSh.rows.push_back({ L"#", L"Variable Name", L"Type", L"Variable Label", L"File Info" });
    varSh.rows.push_back({ L"(meta)", fileLabel, compressed == 1 ? L"Bytecode Compressed" : (compressed == 2 ? L"ZSAV" : L"Uncompressed"),
        L"Cases: " + std::to_wstring(ncases), cDate + L" " + cTime });
    for (size_t i = 0; i < cols.size(); ++i) {
        varSh.rows.push_back({
            std::to_wstring(i + 1),
            cols[i].displayName,
            cols[i].width == 0 ? L"Numeric (F8)" : (L"String (A" + std::to_wstring(cols[i].width) + L")"),
            cols[i].label,
            L""
        });
    }
    FinalizeSheetCols(varSh);

    SpreadsheetSheet dataSh;
    dataSh.name = L"SPSS_Data";
    std::vector<std::wstring> hdr;
    for (const auto& c : cols) hdr.push_back(c.displayName);
    dataSh.rows.push_back(std::move(hdr));

    size_t slotsPerCase = slotToCol.size();
    if (nominalCaseSize > 0 && static_cast<size_t>(nominalCaseSize) > slotsPerCase) {
        slotsPerCase = static_cast<size_t>(nominalCaseSize);
    }

    // Decode SPSS 8-byte slots (uncompressed or bytecode-compressed)
    uint8_t cluster[8] = {};
    int clusterIdx = 8;
    bool eofReached = false;

    auto readNextSlot = [&](uint8_t outSlot[8], bool& isSysMiss, bool& isNumVal, double& numVal) -> bool {
        isSysMiss = false;
        isNumVal = false;
        numVal = 0.0;
        if (compressed == 0) {
            if (pos + 8 > raw.size()) return false;
            std::memcpy(outSlot, raw.data() + pos, 8);
            pos += 8;
            double dv = be ? RdF64BE(outSlot) : RdF64LE(outSlot);
            if (!std::isfinite(dv) || std::fabs(dv) > 1e307) isSysMiss = true;
            else { isNumVal = true; numVal = dv; }
            return true;
        }
        if (compressed != 1) return false;

        while (!eofReached) {
            if (clusterIdx >= 8) {
                if (pos + 8 > raw.size()) return false;
                std::memcpy(cluster, raw.data() + pos, 8);
                pos += 8;
                clusterIdx = 0;
            }
            uint8_t code = cluster[clusterIdx++];
            if (code == 0) continue; // padding/ignore
            if (code == 252) { eofReached = true; return false; }
            if (code == 253) { // literal 8-byte slot
                if (pos + 8 > raw.size()) return false;
                std::memcpy(outSlot, raw.data() + pos, 8);
                pos += 8;
                double dv = be ? RdF64BE(outSlot) : RdF64LE(outSlot);
                if (!std::isfinite(dv) || std::fabs(dv) > 1e307) isSysMiss = true;
                else { isNumVal = true; numVal = dv; }
                return true;
            }
            if (code == 254) { // 8 spaces
                std::memset(outSlot, ' ', 8);
                return true;
            }
            if (code == 255) { // SYSMISS
                std::memset(outSlot, 0, 8);
                isSysMiss = true;
                return true;
            }
            // 1..251 -> compressed integer/value = code - bias
            isNumVal = true;
            numVal = static_cast<double>(code) - bias;
            std::memset(outSlot, ' ', 8);
            return true;
        }
        return false;
    };

    size_t maxCases = (ncases > 0) ? static_cast<size_t>(std::min<int32_t>(ncases, 500)) : 500;
    for (size_t r = 0; r < maxCases && slotsPerCase > 0; ++r) {
        std::vector<std::wstring> row(cols.size());
        std::string strAccum;
        int activeStrCol = -1;
        bool rowOk = true;

        for (size_t s = 0; s < slotsPerCase; ++s) {
            uint8_t slot[8] = {};
            bool sysMiss = false, isNum = false;
            double nv = 0.0;
            if (!readNextSlot(slot, sysMiss, isNum, nv)) { rowOk = false; break; }

            if (s >= slotToCol.size()) continue;
            int cIdx = slotToCol[s];
            if (cIdx >= 0) {
                if (activeStrCol >= 0) {
                    size_t w = static_cast<size_t>(cols[static_cast<size_t>(activeStrCol)].width);
                    if (strAccum.size() > w) strAccum.resize(w);
                    row[static_cast<size_t>(activeStrCol)] = PathUtils::Trim(CleanAsciiFixed(reinterpret_cast<const uint8_t*>(strAccum.data()), strAccum.size()));
                    strAccum.clear();
                    activeStrCol = -1;
                }
                const auto& col = cols[static_cast<size_t>(cIdx)];
                if (col.width == 0) {
                    if (sysMiss || !isNum) {
                        row[static_cast<size_t>(cIdx)] = L"SYSMIS";
                    } else {
                        auto it = col.numValLabels.find(nv);
                        if (it != col.numValLabels.end()) {
                            row[static_cast<size_t>(cIdx)] = FmtDouble(nv) + L" (" + it->second + L")";
                        } else {
                            row[static_cast<size_t>(cIdx)] = FmtDouble(nv);
                        }
                    }
                } else {
                    activeStrCol = cIdx;
                    strAccum.assign(reinterpret_cast<const char*>(slot), 8);
                }
            } else if (activeStrCol >= 0) {
                strAccum.append(reinterpret_cast<const char*>(slot), 8);
            }
        }
        if (activeStrCol >= 0) {
            size_t w = static_cast<size_t>(cols[static_cast<size_t>(activeStrCol)].width);
            if (strAccum.size() > w) strAccum.resize(w);
            row[static_cast<size_t>(activeStrCol)] = PathUtils::Trim(CleanAsciiFixed(reinterpret_cast<const uint8_t*>(strAccum.data()), strAccum.size()));
        }
        if (!rowOk) break;
        dataSh.rows.push_back(std::move(row));
    }
    FinalizeSheetCols(dataSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"SPSS Statistical System Dataset (.sav)";
    if (dataSh.rows.size() > 1) {
        out.sheets.push_back(std::move(dataSh));
        out.sheets.push_back(std::move(varSh));
    } else {
        out.sheets.push_back(std::move(varSh));
    }
    out.valid = true;
    return true;
}

// ============================================================================
// 12. Stata Dataset (.dta — Modern 117–120 <stata_dta> & Classic 113–115)
// ============================================================================
inline bool ParseStataDta(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());

    struct DtaCol {
        std::wstring name;
        std::wstring label;
        uint16_t typeCode = 0; // normalized to 117+ codes: 1..2045=strf, 65526=double, 65527=float, 65528=int32, 65529=int16, 65530=int8
        size_t byteWidth = 0;
    };
    std::vector<DtaCol> cols;
    bool be = false;
    uint64_t nobs = 0;
    size_t dataStart = 0;
    size_t dataEnd = raw.size();
    std::wstring dsLabel;
    int releaseVer = 0;

    if (sv.size() >= 32 && sv.substr(0, 11) == "<stata_dta>") {
        auto findTagSpan = [&](std::string_view openTag, std::string_view closeTag, size_t& pStart, size_t& pLen) -> bool {
            size_t a = sv.find(openTag);
            if (a == std::string_view::npos) return false;
            a += openTag.size();
            size_t b = sv.find(closeTag, a);
            if (b == std::string_view::npos) return false;
            pStart = a;
            pLen = b - a;
            return true;
        };

        size_t s = 0, l = 0;
        if (!findTagSpan("<release>", "</release>", s, l)) return false;
        releaseVer = std::atoi(std::string(sv.substr(s, l)).c_str());
        if (releaseVer < 117 || releaseVer > 125) return false;

        if (findTagSpan("<byteorder>", "</byteorder>", s, l)) {
            be = (sv.substr(s, l) == "MSF");
        }

        uint32_t K = 0;
        if (!findTagSpan("<K>", "</K>", s, l)) return false;
        if (l == 2) K = be ? RdU16BE(raw.data() + s) : RdU16LE(raw.data() + s);
        else if (l == 4) K = be ? RdU32BE(raw.data() + s) : RdU32LE(raw.data() + s);
        if (K == 0 || K > 32768) return false;

        if (findTagSpan("<N>", "</N>", s, l)) {
            if (l == 4) nobs = be ? RdU32BE(raw.data() + s) : RdU32LE(raw.data() + s);
            else if (l == 8) nobs = be ? RdU64BE(raw.data() + s) : RdU64LE(raw.data() + s);
        }
        if (findTagSpan("<label>", "</label>", s, l) && l >= 2) {
            size_t hdrBytes = (releaseVer >= 118) ? 2 : 1;
            if (l >= hdrBytes) dsLabel = CleanAsciiFixed(raw.data() + s + hdrBytes, l - hdrBytes);
        }

        if (!findTagSpan("<variable_types>", "</variable_types>", s, l) || l < static_cast<size_t>(K) * 2) return false;
        cols.resize(K);
        for (uint32_t i = 0; i < K; ++i) {
            uint16_t tc = be ? RdU16BE(raw.data() + s + i * 2) : RdU16LE(raw.data() + s + i * 2);
            cols[i].typeCode = tc;
            if (tc >= 1 && tc <= 2045) cols[i].byteWidth = tc;
            else if (tc == 32768) cols[i].byteWidth = 8; // strL (v, o) ref
            else if (tc == 65526) cols[i].byteWidth = 8; // double
            else if (tc == 65527) cols[i].byteWidth = 4; // float
            else if (tc == 65528) cols[i].byteWidth = 4; // long (int32)
            else if (tc == 65529) cols[i].byteWidth = 2; // int (int16)
            else if (tc == 65530) cols[i].byteWidth = 1; // byte (int8)
            else return false;
        }

        if (findTagSpan("<varnames>", "</varnames>", s, l) && K > 0) {
            size_t stride = l / K;
            for (uint32_t i = 0; i < K && (i + 1) * stride <= l; ++i) {
                cols[i].name = CleanAsciiFixed(raw.data() + s + i * stride, stride);
            }
        }
        if (findTagSpan("<varlabels>", "</varlabels>", s, l) && K > 0) {
            size_t stride = l / K;
            for (uint32_t i = 0; i < K && (i + 1) * stride <= l; ++i) {
                cols[i].label = CleanAsciiFixed(raw.data() + s + i * stride, stride);
            }
        }
        if (findTagSpan("<data>", "</data>", s, l)) {
            dataStart = s;
            dataEnd = s + l;
        }
    } else if (raw.size() >= 118 && (raw[0] == 113 || raw[0] == 114 || raw[0] == 115) &&
               (raw[1] == 1 || raw[1] == 2) && (raw[2] == 1 || raw[2] == 2))
    {
        // Classic Stata 113 / 114 / 115 binary format
        releaseVer = raw[0];
        be = (raw[1] == 1);
        uint16_t K = be ? RdU16BE(raw.data() + 4) : RdU16LE(raw.data() + 4);
        nobs = be ? RdU32BE(raw.data() + 6) : RdU32LE(raw.data() + 6);
        if (K == 0 || K > 32767) return false;

        size_t labelLen = (releaseVer == 113) ? 81 : 81;
        dsLabel = CleanAsciiFixed(raw.data() + 10, labelLen);
        size_t pos = 10 + labelLen + 18; // after timestamp (109B total!)

        if (pos + K > raw.size()) return false;
        cols.resize(K);
        for (uint16_t i = 0; i < K; ++i) {
            uint8_t b = raw[pos + i];
            if (b >= 1 && b <= 244) { cols[i].typeCode = b; cols[i].byteWidth = b; }
            else if (b == 251) { cols[i].typeCode = 65530; cols[i].byteWidth = 1; }
            else if (b == 252) { cols[i].typeCode = 65529; cols[i].byteWidth = 2; }
            else if (b == 253) { cols[i].typeCode = 65528; cols[i].byteWidth = 4; }
            else if (b == 254) { cols[i].typeCode = 65527; cols[i].byteWidth = 4; }
            else if (b == 255) { cols[i].typeCode = 65526; cols[i].byteWidth = 8; }
            else return false;
        }
        pos += K;

        size_t fmtStride = (releaseVer == 113) ? 12 : (releaseVer == 114 ? 49 : 49);
        size_t needDict = static_cast<size_t>(K) * 33 + (static_cast<size_t>(K) + 1) * 2 +
                          static_cast<size_t>(K) * fmtStride + static_cast<size_t>(K) * 33 +
                          static_cast<size_t>(K) * 81;
        if (needDict > raw.size() - pos) return false;

        for (uint16_t i = 0; i < K; ++i) cols[i].name = CleanAsciiFixed(raw.data() + pos + static_cast<size_t>(i) * 33, 33);
        pos += static_cast<size_t>(K) * 33;
        pos += (static_cast<size_t>(K) + 1) * 2; // srtlist
        pos += static_cast<size_t>(K) * fmtStride; // fmtlist
        pos += static_cast<size_t>(K) * 33; // lbllist
        for (uint16_t i = 0; i < K; ++i) cols[i].label = CleanAsciiFixed(raw.data() + pos + static_cast<size_t>(i) * 81, 81);
        pos += static_cast<size_t>(K) * 81;

        // Expansion fields: (byte ov, int32 len, bytes[len]) until ov == 0 && len == 0
        while (pos + 5 <= raw.size()) {
            uint8_t ov = raw[pos];
            uint32_t elen = be ? RdU32BE(raw.data() + pos + 1) : RdU32LE(raw.data() + pos + 1);
            pos += 5;
            if (ov == 0 && elen == 0) break;
            if (elen > raw.size() - pos) return false;
            pos += elen;
        }
        dataStart = pos;
        dataEnd = raw.size();
    } else {
        return false;
    }

    size_t rowWidth = 0;
    for (const auto& c : cols) rowWidth += c.byteWidth;

    SpreadsheetSheet dataSh;
    dataSh.name = L"Stata_Data";
    std::vector<std::wstring> hdr;
    for (size_t i = 0; i < cols.size(); ++i) {
        hdr.push_back(cols[i].name.empty() ? (L"var" + std::to_wstring(i + 1)) : cols[i].name);
    }
    dataSh.rows.push_back(std::move(hdr));

    if (rowWidth > 0 && dataStart < dataEnd) {
        size_t maxObs = static_cast<size_t>(std::min<uint64_t>(nobs, 500));
        size_t pos = dataStart;
        for (size_t r = 0; r < maxObs && pos + rowWidth <= dataEnd; ++r, pos += rowWidth) {
            std::vector<std::wstring> row(cols.size());
            size_t off = pos;
            for (size_t c = 0; c < cols.size(); ++c) {
                const uint8_t* p = raw.data() + off;
                uint16_t tc = cols[c].typeCode;
                if (tc >= 1 && tc <= 2045) {
                    row[c] = CleanAsciiFixed(p, cols[c].byteWidth);
                } else if (tc == 65530) { // byte (missing >= 101)
                    int8_t v = static_cast<int8_t>(p[0]);
                    row[c] = (v >= 101) ? L"." : std::to_wstring(v);
                } else if (tc == 65529) { // int16 (missing >= 32741)
                    int16_t v = be ? RdI16BE(p) : RdI16LE(p);
                    row[c] = (v >= 32741) ? L"." : std::to_wstring(v);
                } else if (tc == 65528) { // int32 (missing >= 2147483621)
                    int32_t v = be ? RdI32BE(p) : RdI32LE(p);
                    row[c] = (v >= 2147483621) ? L"." : std::to_wstring(v);
                } else if (tc == 65527) { // float
                    float v = be ? RdF32BE(p) : RdF32LE(p);
                    row[c] = (!std::isfinite(v) || v > 1.7e38f) ? L"." : FmtDouble(v);
                } else if (tc == 65526) { // double
                    double v = be ? RdF64BE(p) : RdF64LE(p);
                    row[c] = (!std::isfinite(v) || v > 8.9e307) ? L"." : FmtDouble(v);
                } else {
                    row[c] = L"(strL)";
                }
                off += cols[c].byteWidth;
            }
            dataSh.rows.push_back(std::move(row));
        }
    }
    FinalizeSheetCols(dataSh);

    SpreadsheetSheet varSh;
    varSh.name = L"Stata_Variables";
    varSh.rows.push_back({ L"#", L"Name", L"Stata Type", L"Variable Label" });
    varSh.rows.push_back({ L"(meta)", L"Release " + std::to_wstring(releaseVer), L"Obs: " + std::to_wstring(nobs), dsLabel });
    for (size_t i = 0; i < cols.size(); ++i) {
        std::wstring tname = (cols[i].typeCode <= 2045) ? (L"str" + std::to_wstring(cols[i].byteWidth)) :
                             (cols[i].typeCode == 65526) ? L"double" :
                             (cols[i].typeCode == 65527) ? L"float" :
                             (cols[i].typeCode == 65528) ? L"long" :
                             (cols[i].typeCode == 65529) ? L"int" : L"byte";
        varSh.rows.push_back({ std::to_wstring(i + 1), cols[i].name, tname, cols[i].label });
    }
    FinalizeSheetCols(varSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Stata Statistical Dataset (.dta)";
    out.sheets.push_back(std::move(dataSh));
    out.sheets.push_back(std::move(varSh));
    out.valid = true;
    return true;
}

// ============================================================================
// 13. SAS7BDAT Binary Dataset (.sas7bdat)
// ============================================================================
inline bool ParseSas7bdat(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    static const uint8_t kSasMagic[32] = {
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0xC2,0xEA,0x81,0x60,
        0xB3,0x14,0x11,0xCF,0xBD,0x92,0x08,0x00, 0x09,0xC7,0x31,0x8C,0x18,0x1F,0x10,0x11
    };
    if (raw.size() < 288 || std::memcmp(raw.data(), kSasMagic, 32) != 0) return false;

    bool u64 = (raw[32] == 0x33);
    size_t align1 = (raw[32] == 0x33) ? 4 : 0;
    size_t align2 = (raw[35] == 0x33) ? 4 : 0;
    bool le = (raw[37] == 0x01);

    auto rdU16 = [&](const uint8_t* p) { return le ? RdU16LE(p) : RdU16BE(p); };
    auto rdU32 = [&](const uint8_t* p) { return le ? RdU32LE(p) : RdU32BE(p); };
    auto rdInt = [&](const uint8_t* p) -> uint64_t {
        return u64 ? (le ? RdU64LE(p) : RdU64BE(p)) : rdU32(p);
    };

    std::wstring dsName = CleanAsciiFixed(raw.data() + 92, 32);
    size_t hdrLenOff = 196 + align1;
    if (hdrLenOff + 16 > raw.size()) return false;
    uint32_t headerLen = rdU32(raw.data() + hdrLenOff);
    uint32_t pageSize  = rdU32(raw.data() + hdrLenOff + 4);
    uint64_t pageCount = u64 ? (le ? RdU64LE(raw.data() + hdrLenOff + 8) : RdU64BE(raw.data() + hdrLenOff + 8))
                             : rdU32(raw.data() + hdrLenOff + 8);
    (void)align2;
    if (headerLen < 256 || pageSize < 256 || headerLen > raw.size()) return false;

    struct SasCol {
        std::wstring name;
        uint64_t offset = 0;
        uint32_t width = 0;
        uint8_t type = 1; // 1 = Numeric, 2 = Character
    };
    std::vector<SasCol> cols;
    std::vector<std::string> colTexts;
    uint64_t rowLength = 0;
    uint64_t totalRows = 0;
    uint64_t colCount = 0;
    uint64_t mixRowCount = 0;

    std::vector<std::vector<std::wstring>> decodedRows;
    size_t intSize = u64 ? 8 : 4;
    size_t pageHdrOff = u64 ? 32 : 16;
    size_t subPtrSize = u64 ? 24 : 12;

    size_t maxPages = static_cast<size_t>(std::min<uint64_t>(pageCount > 0 ? pageCount : 64, 64));
    for (size_t pg = 0; pg < maxPages; ++pg) {
        size_t pgPos = headerLen + pg * static_cast<size_t>(pageSize);
        if (pgPos + pageHdrOff + 8 > raw.size()) break;
        size_t availPg = std::min<size_t>(pageSize, raw.size() - pgPos);
        const uint8_t* pp = raw.data() + pgPos;

        uint16_t pageType   = rdU16(pp + pageHdrOff);
        uint16_t blockCount = rdU16(pp + pageHdrOff + 2);
        uint16_t subCount   = rdU16(pp + pageHdrOff + 4);

        // Parse metadata subheaders on META (0), MIX (512/640), or AMD (1024) pages
        if (pageType == 0 || pageType == 512 || pageType == 640 || pageType == 1024) {
            for (uint16_t s = 0; s < subCount; ++s) {
                size_t ptrOff = pageHdrOff + 8 + static_cast<size_t>(s) * subPtrSize;
                if (ptrOff + subPtrSize > availPg) break;
                uint64_t shOff = rdInt(pp + ptrOff);
                uint64_t shLen = rdInt(pp + ptrOff + intSize);
                uint8_t compFlag = pp[ptrOff + 2 * intSize];
                if (shLen < 4 || compFlag == 1 || shOff + shLen > availPg) continue;

                const uint8_t* sp = pp + static_cast<size_t>(shOff);
                uint32_t sig = RdU32LE(sp);
                if (u64 && shLen >= 8) {
                    uint32_t sigHi = RdU32LE(sp + 4);
                    if (sig == 0xFFFFFFFFu && sigHi != 0xFFFFFFFFu) sig = sigHi;
                }

                if ((sig == 0xF7F7F7F7u || sig == 0x000000F7u) && shLen >= 12 * intSize) { // RowSize
                    rowLength   = rdInt(sp + 5 * intSize);
                    totalRows   = rdInt(sp + 6 * intSize);
                    mixRowCount = rdInt(sp + ((16 * intSize <= shLen) ? (15 * intSize) : (6 * intSize)));
                } else if ((sig == 0xF6F6F6F6u || sig == 0x000000F6u) && shLen >= 2 * intSize) { // ColumnSize
                    colCount = rdInt(sp + intSize);
                    if (colCount <= 4096 && cols.size() < colCount) cols.resize(static_cast<size_t>(colCount));
                } else if (sig == 0xFDFFFFFFu || sig == 0xFFFFFFFDu) { // ColumnText
                    size_t txtHdr = intSize + 2;
                    if (shLen > txtHdr) {
                        colTexts.emplace_back(reinterpret_cast<const char*>(sp + intSize), static_cast<size_t>(shLen - intSize));
                    }
                } else if (sig == 0xFFFFFFFFu && shLen > intSize + 8) { // ColumnName
                    size_t entryLen = u64 ? 8 : 8;
                    size_t cur = intSize + 8;
                    size_t cIdx = 0;
                    while (cur + entryLen <= shLen && cIdx < 4096) {
                        uint16_t txtIdx  = rdU16(sp + cur);
                        uint16_t nameOff = rdU16(sp + cur + 2);
                        uint16_t nameLen = rdU16(sp + cur + 4);
                        if (cols.size() <= cIdx) cols.resize(cIdx + 1);
                        if (txtIdx < colTexts.size() && static_cast<size_t>(nameOff) + nameLen <= colTexts[txtIdx].size()) {
                            std::string nm = colTexts[txtIdx].substr(nameOff, nameLen);
                            cols[cIdx].name = PathUtils::Utf8ToWide(nm);
                        }
                        ++cIdx;
                        cur += entryLen;
                    }
                } else if ((sig == 0xFCFFFFFFu || sig == 0xFFFFFFFCu) && shLen > intSize + 8) { // ColumnAttrs
                    size_t entryLen = u64 ? 16 : 12;
                    size_t cur = intSize + 8;
                    size_t cIdx = 0;
                    while (cur + entryLen <= shLen && cIdx < 4096) {
                        if (cols.size() <= cIdx) cols.resize(cIdx + 1);
                        cols[cIdx].offset = rdInt(sp + cur);
                        cols[cIdx].width  = rdU32(sp + cur + intSize);
                        cols[cIdx].type   = sp[cur + intSize + 6];
                        ++cIdx;
                        cur += entryLen;
                    }
                }
            }
        }

        // Decode uncompressed rows on MIX (512/640) or DATA (256) pages
        if ((pageType == 256 || pageType == 512 || pageType == 640) && rowLength > 0 && !cols.empty() && decodedRows.size() < 500) {
            size_t dataOff = pageHdrOff + 8;
            size_t rowsOnPage = 0;
            if (pageType == 256) {
                rowsOnPage = blockCount;
            } else {
                dataOff = pageHdrOff + 8 + static_cast<size_t>(subCount) * subPtrSize;
                dataOff = (dataOff + 7u) & ~size_t(7u);
                rowsOnPage = static_cast<size_t>(std::min<uint64_t>(mixRowCount > 0 ? mixRowCount : totalRows, 500));
            }
            for (size_t r = 0; r < rowsOnPage && decodedRows.size() < 500 && dataOff + rowLength <= availPg; ++r, dataOff += static_cast<size_t>(rowLength)) {
                if (totalRows > 0 && decodedRows.size() >= totalRows) break;
                const uint8_t* rp = pp + dataOff;
                std::vector<std::wstring> rvec(cols.size());
                for (size_t c = 0; c < cols.size(); ++c) {
                    if (cols[c].width == 0 || cols[c].offset + cols[c].width > rowLength) continue;
                    const uint8_t* cp = rp + static_cast<size_t>(cols[c].offset);
                    if (cols[c].type == 2) {
                        rvec[c] = PathUtils::Trim(CleanAsciiFixed(cp, cols[c].width));
                    } else if (cols[c].width <= 8) {
                        uint8_t dblBytes[8] = {};
                        if (le) {
                            std::memcpy(dblBytes + (8 - cols[c].width), cp, cols[c].width);
                            double dv = RdF64LE(dblBytes);
                            rvec[c] = std::isfinite(dv) ? FmtDouble(dv) : L".";
                        } else {
                            std::memcpy(dblBytes, cp, cols[c].width);
                            double dv = RdF64BE(dblBytes);
                            rvec[c] = std::isfinite(dv) ? FmtDouble(dv) : L".";
                        }
                    }
                }
                decodedRows.push_back(std::move(rvec));
            }
        }
    }

    SpreadsheetSheet metaSh;
    metaSh.name = L"SAS_Dataset_Info";
    metaSh.rows.push_back({ L"Property / Column #", L"Name", L"Type", L"Width / Value" });
    metaSh.rows.push_back({ L"(dataset)", dsName.empty() ? L"SAS7BDAT" : dsName, u64 ? L"64-bit" : L"32-bit",
        L"Rows=" + std::to_wstring(totalRows) + L", Cols=" + std::to_wstring(cols.size()) + L", PageSize=" + std::to_wstring(pageSize) });
    for (size_t i = 0; i < cols.size(); ++i) {
        std::wstring cname = cols[i].name.empty() ? (L"COL_" + std::to_wstring(i + 1)) : cols[i].name;
        metaSh.rows.push_back({ std::to_wstring(i + 1), cname, cols[i].type == 2 ? L"Character" : L"Numeric", std::to_wstring(cols[i].width) + L" B" });
    }
    FinalizeSheetCols(metaSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"SAS Binary Dataset (.sas7bdat)";
    if (!decodedRows.empty()) {
        SpreadsheetSheet dataSh;
        dataSh.name = dsName.empty() ? L"SAS_Data" : dsName;
        std::vector<std::wstring> hdr;
        for (size_t i = 0; i < cols.size(); ++i) hdr.push_back(cols[i].name.empty() ? (L"COL_" + std::to_wstring(i + 1)) : cols[i].name);
        dataSh.rows.push_back(std::move(hdr));
        for (auto& r : decodedRows) dataSh.rows.push_back(std::move(r));
        FinalizeSheetCols(dataSh);
        out.sheets.push_back(std::move(dataSh));
    }
    out.sheets.push_back(std::move(metaSh));
    out.valid = true;
    return true;
}

// ============================================================================
// 14. R Serialized Data (.rds — XDR 'X' / Native 'B', Gzipped or Raw)
// ============================================================================
struct RNode {
    uint8_t sexpType = 0;
    std::string sym;
    std::vector<std::wstring> strs; // atomic values or STRSXP strings
    std::vector<RNode> listItems;   // VECSXP elements
    std::vector<std::pair<std::string, RNode>> attrs;

    const RNode* FindAttr(std::string_view k) const {
        for (const auto& kv : attrs) if (kv.first == k) return &kv.second;
        return nullptr;
    }
};

struct RdsReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    bool be = true;
    std::vector<std::string> refTable; // 1-based reference table for SYMSXP / ENVSXP
    size_t nodesDecoded = 0;

    bool RdI32(int32_t& v) {
        if (pos + 4 > size) return false;
        v = be ? RdI32BE(data + pos) : RdI32LE(data + pos);
        pos += 4;
        return true;
    }
    bool RdF64(double& v) {
        if (pos + 8 > size) return false;
        v = be ? RdF64BE(data + pos) : RdF64LE(data + pos);
        pos += 8;
        return true;
    }

    bool ReadItem(RNode& out, int depth = 0) {
        if (depth > 48 || ++nodesDecoded > 50000) return false;
        int32_t flags = 0;
        if (!RdI32(flags)) return false;

        uint8_t stype = static_cast<uint8_t>(static_cast<uint32_t>(flags) & 0xFFu);
        bool hasAttr = (flags & (1 << 9)) != 0;
        bool hasTag  = (flags & (1 << 10)) != 0;
        out.sexpType = stype;

        if (stype == 254 || stype == 0) { // NILVALUE_SXP / NILSXP
            return true;
        }
        if (stype == 255) { // REFSXP
            int32_t refIdx = static_cast<int32_t>(static_cast<uint32_t>(flags) >> 8);
            if (refIdx == 0 && !RdI32(refIdx)) return false;
            if (refIdx >= 1 && static_cast<size_t>(refIdx - 1) < refTable.size()) {
                out.sym = refTable[static_cast<size_t>(refIdx - 1)];
            }
            return true;
        }
        if (stype == 1) { // SYMSXP -> followed by CHARSXP
            RNode ch;
            if (!ReadItem(ch, depth + 1)) return false;
            out.sym = ch.sym;
            refTable.push_back(out.sym);
            return true;
        }
        if (stype == 9) { // CHARSXP
            int32_t slen = 0;
            if (!RdI32(slen)) return false;
            if (slen == -1) {
                out.sym = "NA";
                return true;
            }
            if (slen < 0 || static_cast<size_t>(slen) > size - pos) return false;
            out.sym.assign(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(slen));
            pos += static_cast<size_t>(slen);
            if (hasAttr) { RNode dummy; if (!ReadItem(dummy, depth + 1)) return false; }
            return true;
        }
        if (stype == 2) { // LISTSXP (dotted pairlist used for ATTRIB!)
            while (true) {
                if (hasAttr) { RNode dummy; if (!ReadItem(dummy, depth + 1)) return false; }
                std::string tagName;
                if (hasTag) {
                    RNode tagNode;
                    if (!ReadItem(tagNode, depth + 1)) return false;
                    tagName = tagNode.sym;
                }
                RNode car;
                if (!ReadItem(car, depth + 1)) return false;
                out.attrs.emplace_back(std::move(tagName), std::move(car));

                // Next CDR cell
                int32_t cdrFlags = 0;
                if (!RdI32(cdrFlags)) return false;
                uint8_t cdrType = static_cast<uint8_t>(static_cast<uint32_t>(cdrFlags) & 0xFFu);
                if (cdrType == 254 || cdrType == 0) break;
                if (cdrType != 2) {
                    pos -= 4;
                    RNode tail;
                    if (!ReadItem(tail, depth + 1)) return false;
                    break;
                }
                hasAttr = (cdrFlags & (1 << 9)) != 0;
                hasTag  = (cdrFlags & (1 << 10)) != 0;
            }
            return true;
        }

        // Vector types: LGLSXP(10), INTSXP(13), REALSXP(14), CPLXSXP(15), STRSXP(16), VECSXP(19), RAWSXP(24)
        if (stype == 10 || stype == 13 || stype == 14 || stype == 15 || stype == 16 || stype == 19 || stype == 24) {
            int32_t vlen = 0;
            if (!RdI32(vlen) || vlen < 0) return false;
            size_t len = static_cast<size_t>(vlen);

            if (stype == 10 || stype == 13) { // LGLSXP / INTSXP
                if (len * 4 > size - pos) return false;
                size_t cap = std::min<size_t>(len, 1000);
                out.strs.reserve(cap);
                for (size_t i = 0; i < len; ++i) {
                    int32_t iv = 0; RdI32(iv);
                    if (i < cap) {
                        if (iv == std::numeric_limits<int32_t>::min()) out.strs.push_back(L"NA");
                        else if (stype == 10) out.strs.push_back(iv != 0 ? L"TRUE" : L"FALSE");
                        else out.strs.push_back(std::to_wstring(iv));
                    }
                }
            } else if (stype == 14) { // REALSXP
                if (len * 8 > size - pos) return false;
                size_t cap = std::min<size_t>(len, 1000);
                out.strs.reserve(cap);
                for (size_t i = 0; i < len; ++i) {
                    uint64_t bits = be ? RdU64BE(data + pos) : RdU64LE(data + pos);
                    double dv = 0.0; RdF64(dv);
                    if (i < cap) {
                        if ((bits & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL && (bits & 0xFFFULL) == 0x7A2ULL) {
                            out.strs.push_back(L"NA");
                        } else {
                            out.strs.push_back(FmtDouble(dv));
                        }
                    }
                }
            } else if (stype == 15) { // CPLXSXP
                if (len * 16 > size - pos) return false;
                for (size_t i = 0; i < len; ++i) {
                    double re = 0.0, im = 0.0; RdF64(re); RdF64(im);
                    if (i < 500) out.strs.push_back(FmtDouble(re) + (im >= 0 ? L"+" : L"") + FmtDouble(im) + L"i");
                }
            } else if (stype == 24) { // RAWSXP
                if (len > size - pos) return false;
                for (size_t i = 0; i < std::min<size_t>(len, 500); ++i) {
                    wchar_t hb[8] = {}; swprintf_s(hb, L"%02X", data[pos + i]);
                    out.strs.push_back(hb);
                }
                pos += len;
            } else if (stype == 16) { // STRSXP
                if (len > 1000000) return false;
                for (size_t i = 0; i < len; ++i) {
                    RNode ch;
                    if (!ReadItem(ch, depth + 1)) return false;
                    if (i < 1000) out.strs.push_back(PathUtils::Utf8ToWide(ch.sym));
                }
            } else if (stype == 19) { // VECSXP (generic list / data.frame)
                if (len > 10000) return false;
                out.listItems.reserve(std::min<size_t>(len, 128));
                for (size_t i = 0; i < len; ++i) {
                    RNode elem;
                    if (!ReadItem(elem, depth + 1)) return false;
                    if (i < 128) out.listItems.push_back(std::move(elem));
                }
            }

            if (hasAttr) {
                RNode attrList;
                if (!ReadItem(attrList, depth + 1)) return false;
                out.attrs = std::move(attrList.attrs);
            }
            return true;
        }
        return false;
    }
};

inline bool ParseRdsData(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::vector<uint8_t> uncomp;
    const uint8_t* ptr = raw.data();
    size_t len = raw.size();
    if (len >= 10 && ptr[0] == 0x1Fu && ptr[1] == 0x8Bu) {
        if (!ZipDeflate::InflateGzip(ptr, len, uncomp, 64 * 1024 * 1024) || uncomp.empty()) return false;
        ptr = uncomp.data();
        len = uncomp.size();
    }
    if (len < 14) return false;
    bool be = true;
    if (ptr[0] == 'X' && ptr[1] == '\n') be = true;
    else if (ptr[0] == 'B' && ptr[1] == '\n') be = false;
    else return false;

    RdsReader rr{ ptr, len, 2, be, {}, 0 };
    int32_t fmtVer = 0, rVer = 0, minVer = 0;
    if (!rr.RdI32(fmtVer) || !rr.RdI32(rVer) || !rr.RdI32(minVer)) return false;
    if (fmtVer != 2 && fmtVer != 3) return false;
    if (fmtVer == 3) {
        int32_t encLen = 0;
        if (!rr.RdI32(encLen) || encLen < 0 || static_cast<size_t>(encLen) > rr.size - rr.pos) return false;
        rr.pos += static_cast<size_t>(encLen);
    }

    RNode root;
    if (!rr.ReadItem(root, 0)) return false;

    SpreadsheetSheet sh;
    sh.name = L"RDS_Data";

    if (root.sexpType == 19 && !root.listItems.empty()) { // data.frame or list
        const RNode* namesAttr = root.FindAttr("names");
        const RNode* levelsCols = namesAttr;
        std::vector<std::wstring> hdr;
        size_t maxRows = 0;
        for (size_t c = 0; c < root.listItems.size(); ++c) {
            std::wstring cname = (levelsCols && c < levelsCols->strs.size()) ? levelsCols->strs[c] : (L"V" + std::to_wstring(c + 1));
            hdr.push_back(cname);
            maxRows = std::max(maxRows, root.listItems[c].strs.size());
        }
        sh.rows.push_back(std::move(hdr));
        size_t rCap = std::min<size_t>(maxRows, 500);
        for (size_t r = 0; r < rCap; ++r) {
            std::vector<std::wstring> row(root.listItems.size());
            for (size_t c = 0; c < root.listItems.size(); ++c) {
                const auto& colNode = root.listItems[c];
                const RNode* facLvls = colNode.FindAttr("levels");
                if (r < colNode.strs.size()) {
                    if (facLvls && !facLvls->strs.empty()) {
                        int lvlIdx = std::atoi(PathUtils::WideToUtf8(colNode.strs[r]).c_str());
                        if (lvlIdx >= 1 && static_cast<size_t>(lvlIdx - 1) < facLvls->strs.size()) {
                            row[c] = facLvls->strs[static_cast<size_t>(lvlIdx - 1)];
                        } else {
                            row[c] = colNode.strs[r];
                        }
                    } else {
                        row[c] = colNode.strs[r];
                    }
                }
            }
            sh.rows.push_back(std::move(row));
        }
    } else {
        const RNode* dimAttr = root.FindAttr("dim");
        if (dimAttr && dimAttr->strs.size() == 2) {
            size_t nr = static_cast<size_t>(std::max(1, _wtoi(dimAttr->strs[0].c_str())));
            size_t nc = static_cast<size_t>(std::max(1, _wtoi(dimAttr->strs[1].c_str())));
            std::vector<std::wstring> hdr = { L"Row \\ Col" };
            for (size_t c = 0; c < std::min<size_t>(nc, 64); ++c) hdr.push_back(L"[," + std::to_wstring(c + 1) + L"]");
            sh.rows.push_back(std::move(hdr));
            for (size_t r = 0; r < std::min<size_t>(nr, 256); ++r) {
                std::vector<std::wstring> rv = { L"[" + std::to_wstring(r + 1) + L",]" };
                for (size_t c = 0; c < std::min<size_t>(nc, 64); ++c) {
                    size_t idx = r + c * nr; // R matrices are column-major
                    rv.push_back(idx < root.strs.size() ? root.strs[idx] : L"");
                }
                sh.rows.push_back(std::move(rv));
            }
        } else {
            const RNode* namesAttr = root.FindAttr("names");
            sh.rows.push_back({ L"Index / Name", L"Value (RDS v" + std::to_wstring(fmtVer) + L")" });
            for (size_t i = 0; i < std::min<size_t>(root.strs.size(), 500); ++i) {
                std::wstring nm = (namesAttr && i < namesAttr->strs.size()) ? namesAttr->strs[i] : (L"[" + std::to_wstring(i + 1) + L"]");
                sh.rows.push_back({ nm, root.strs[i] });
            }
        }
    }
    FinalizeSheetCols(sh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"R Serialized Object (.rds)";
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}

// ============================================================================
// 15. DuckDB Analytical Database (.duckdb — Linked MetaBlocks + BinaryMetadataDeserializer)
// ============================================================================
struct DuckMetaPtr {
    uint64_t blockPtr = UINT64_MAX;
    uint32_t offset = 0;
};

class DuckMetaStream {
public:
    DuckMetaStream(const std::vector<uint8_t>& raw, uint64_t blockAlloc, uint64_t startMetaPtr, uint32_t startOff = 0)
        : m_raw(raw), m_blockAlloc(blockAlloc)
    {
        m_valid = OpenChunk(startMetaPtr, startOff);
    }

    bool Valid() const { return m_valid; }

    bool ReadByte(uint8_t& out) {
        if (!m_valid) return false;
        while (m_pos >= m_end) {
            if (m_nextPtr == UINT64_MAX) {
                m_valid = false;
                return false;
            }
            if (!OpenChunk(m_nextPtr, 0)) {
                m_valid = false;
                return false;
            }
        }
        out = m_raw[m_pos++];
        return true;
    }

    bool ReadBytes(void* dst, size_t n) {
        uint8_t* p = static_cast<uint8_t*>(dst);
        for (size_t i = 0; i < n; ++i) {
            if (!ReadByte(p[i])) return false;
        }
        return true;
    }

    bool ReadU16(uint16_t& out) {
        uint8_t b[2];
        if (!ReadBytes(b, 2)) return false;
        out = RdU16LE(b);
        return true;
    }

    bool ReadU32(uint32_t& out) {
        uint8_t b[4];
        if (!ReadBytes(b, 4)) return false;
        out = RdU32LE(b);
        return true;
    }

    bool ReadU64(uint64_t& out) {
        uint8_t b[8];
        if (!ReadBytes(b, 8)) return false;
        out = RdU64LE(b);
        return true;
    }

    bool ReadBool(bool& out) {
        uint8_t b = 0;
        if (!ReadByte(b) || b > 1u) return false;
        out = (b != 0);
        return true;
    }

    bool ReadVarU64(uint64_t& out) {
        out = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            uint8_t b = 0;
            if (!ReadByte(b)) return false;
            if (shift == 63 && b > 1u) return false;
            out |= static_cast<uint64_t>(b & 0x7Fu) << shift;
            if ((b & 0x80u) == 0) return true;
        }
        return false;
    }

    bool ReadVarI64(int64_t& out) {
        uint64_t u = 0;
        if (!ReadVarU64(u)) return false;
        out = static_cast<int64_t>((u >> 1) ^ (~(u & 1ULL) + 1ULL));
        return true;
    }

    bool ReadStr(std::string& out, size_t maxLen = 65536) {
        uint64_t len = 0;
        if (!ReadVarU64(len) || len > maxLen) return false;
        out.resize(static_cast<size_t>(len));
        if (len > 0 && !ReadBytes(out.data(), static_cast<size_t>(len))) return false;
        return true;
    }

private:
    bool OpenChunk(uint64_t metaPtr, uint32_t subOff) {
        if (metaPtr == UINT64_MAX || m_hops >= 4096) return false;
        if (subOff == 0 && !m_visited.insert(metaPtr).second) return false; // cycle detected
        ++m_hops;

        const uint64_t blockId = metaPtr & 0x00FFFFFFFFFFFFFFULL;
        const uint8_t subIdx = static_cast<uint8_t>(metaPtr >> 56);
        if (subIdx >= 64) return false;

        if (m_blockAlloc < 4096 || blockId > (m_raw.size() - 12288ULL) / m_blockAlloc) return false;
        const uint64_t physStart = 12288ULL + blockId * m_blockAlloc;
        if (physStart + 16ULL > m_raw.size()) return false;

        const uint64_t usableStart = physStart + 8ULL; // skip 8B physical block checksum
        const uint64_t usableCap = std::min<uint64_t>(m_blockAlloc - 8ULL, m_raw.size() - usableStart);

        uint64_t subSize = usableCap;
        if (m_blockAlloc >= 65536ULL || subIdx > 0) {
            subSize = ((m_blockAlloc - 8ULL) / 64ULL) & ~7ULL;
            if (subSize < 64ULL) return false;
        }
        const uint64_t chunkStart = usableStart + static_cast<uint64_t>(subIdx) * subSize;
        if (chunkStart + 8ULL > m_raw.size()) return false;
        const uint64_t chunkEnd = std::min<uint64_t>(chunkStart + subSize, m_raw.size());
        if (chunkEnd <= chunkStart + 8ULL) return false;

        m_nextPtr = RdU64LE(m_raw.data() + static_cast<size_t>(chunkStart));
        const uint64_t effOff = (subOff < 8u) ? 8ULL : static_cast<uint64_t>(subOff);
        if (chunkStart + effOff > chunkEnd) return false;

        m_pos = static_cast<size_t>(chunkStart + effOff);
        m_end = static_cast<size_t>(chunkEnd);
        return true;
    }

    const std::vector<uint8_t>& m_raw;
    uint64_t m_blockAlloc = 262144;
    size_t m_pos = 0;
    size_t m_end = 0;
    uint64_t m_nextPtr = UINT64_MAX;
    uint32_t m_hops = 0;
    bool m_valid = false;
    std::set<uint64_t> m_visited;
};

struct DuckColDef {
    std::string name;
    uint8_t typeId = 0;
    uint8_t decWidth = 0;
    uint8_t decScale = 0;
};

struct DuckSegPtr {
    uint64_t rowStart = 0;
    uint64_t tupleCount = 0;
    int64_t blockId = -1;
    uint32_t offset = 0;
    uint8_t compression = 1; // 1=UNCOMPRESSED, 2=CONSTANT, 3=RLE, 6=DICTIONARY
    bool hasNull = false;
    bool hasNoNull = true;
    bool hasConstVal = false;
    std::wstring constVal;
};

struct DuckTableEntry {
    uint8_t catType = 0; // 1=TABLE, 2=SCHEMA, 3=VIEW
    std::string schema;
    std::string name;
    std::string sql;
    bool internal = false;
    std::vector<DuckColDef> cols;
    DuckMetaPtr tablePtr;
    uint64_t totalRows = 0;
    std::vector<std::vector<DuckSegPtr>> colSegments;
};

inline const wchar_t* DuckTypeName(const DuckColDef& c) {
    switch (c.typeId) {
    case 10: return L"BOOLEAN";
    case 11: return L"TINYINT";
    case 12: return L"SMALLINT";
    case 13: return L"INTEGER";
    case 14: return L"BIGINT";
    case 15: return L"DATE";
    case 16: return L"TIME";
    case 17: return L"TIMESTAMP_S";
    case 18: return L"TIMESTAMP_MS";
    case 19: return L"TIMESTAMP";
    case 20: return L"TIMESTAMP_NS";
    case 21: return L"DECIMAL";
    case 22: return L"FLOAT";
    case 23: return L"DOUBLE";
    case 24: return L"CHAR";
    case 25: return L"VARCHAR";
    case 26: return L"BLOB";
    case 27: return L"INTERVAL";
    case 28: return L"UTINYINT";
    case 29: return L"USMALLINT";
    case 30: return L"UINTEGER";
    case 31: return L"UBIGINT";
    case 32: return L"TIMESTAMPTZ";
    case 49: return L"UHUGEINT";
    case 50: return L"HUGEINT";
    case 54: return L"UUID";
    case 100: return L"STRUCT";
    case 101: return L"LIST";
    case 102: return L"MAP";
    default: return L"ANY";
    }
}

inline std::wstring FormatDuckDateDays(int32_t z) {
    // Howard Hinnant civil_from_days (days since 1970-01-01)
    int64_t zz = static_cast<int64_t>(z) + 719468LL;
    const int64_t era = (zz >= 0 ? zz : zz - 146096LL) / 146097LL;
    const uint32_t doe = static_cast<uint32_t>(zz - era * 146097LL);
    const uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    int64_t y = static_cast<int64_t>(yoe) + era * 400LL;
    const uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const uint32_t mp = (5u * doy + 2u) / 153u;
    const uint32_t d = doy - (153u * mp + 2u) / 5u + 1u;
    const uint32_t m = mp < 10u ? (mp + 3u) : (mp - 9u);
    y += (m <= 2u) ? 1 : 0;
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%04lld-%02u-%02u", static_cast<long long>(y), m, d);
    return buf;
}

inline std::wstring FormatDuckTimestampUs(int64_t us) {
    int64_t secs = us / 1000000LL;
    int64_t remUs = us % 1000000LL;
    if (remUs < 0) { remUs += 1000000LL; --secs; }
    int64_t days = secs / 86400LL;
    int64_t sod = secs % 86400LL;
    if (sod < 0) { sod += 86400LL; --days; }
    const uint32_t hh = static_cast<uint32_t>(sod / 3600LL);
    const uint32_t mm = static_cast<uint32_t>((sod % 3600LL) / 60LL);
    const uint32_t ss = static_cast<uint32_t>(sod % 60LL);
    wchar_t tbuf[32] = {};
    swprintf_s(tbuf, L" %02u:%02u:%02u", hh, mm, ss);
    return FormatDuckDateDays(static_cast<int32_t>(days)) + tbuf;
}

inline bool ReadDuckMetaPtrObj(DuckMetaStream& ms, DuckMetaPtr& out) {
    out = {};
    for (;;) {
        uint16_t fid = 0;
        if (!ms.ReadU16(fid)) return false;
        if (fid == 0xFFFFu) return true;
        if (fid == 100) {
            if (!ms.ReadVarU64(out.blockPtr)) return false;
        } else if (fid == 101) {
            uint64_t off = 0;
            if (!ms.ReadVarU64(off)) return false;
            out.offset = static_cast<uint32_t>(off);
        } else {
            return false;
        }
    }
}

inline bool ReadDuckLogicalTypeObj(DuckMetaStream& ms, DuckColDef& col) {
    for (;;) {
        uint16_t fid = 0;
        if (!ms.ReadU16(fid)) return false;
        if (fid == 0xFFFFu) return true;
        if (fid == 100) {
            uint64_t tid = 0;
            if (!ms.ReadVarU64(tid) || tid > 255) return false;
            col.typeId = static_cast<uint8_t>(tid);
        } else if (fid == 101) {
            // Optional ExtraTypeInfo nullable pointer (1B bool + object)
            bool present = false;
            if (!ms.ReadBool(present)) return false;
            if (present) {
                for (;;) {
                    uint16_t ef = 0;
                    if (!ms.ReadU16(ef)) return false;
                    if (ef == 0xFFFFu) break;
                    if (ef == 100) { uint64_t et = 0; if (!ms.ReadVarU64(et)) return false; }
                    else if (ef == 101) { std::string alias; if (!ms.ReadStr(alias)) return false; }
                    else if (ef == 200) { uint64_t w = 0; if (!ms.ReadVarU64(w)) return false; col.decWidth = static_cast<uint8_t>(w); }
                    else if (ef == 201) { uint64_t sc = 0; if (!ms.ReadVarU64(sc)) return false; col.decScale = static_cast<uint8_t>(sc); }
                    else return false;
                }
            }
        } else {
            return false;
        }
    }
}

inline bool ReadDuckColumnDefObj(DuckMetaStream& ms, DuckColDef& out) {
    out = {};
    for (;;) {
        uint16_t fid = 0;
        if (!ms.ReadU16(fid)) return false;
        if (fid == 0xFFFFu) return !out.name.empty();
        if (fid == 100) {
            if (!ms.ReadStr(out.name, 4096)) return false;
        } else if (fid == 101) {
            if (!ReadDuckLogicalTypeObj(ms, out)) return false;
        } else if (fid == 102 || fid == 103) {
            uint64_t v = 0;
            if (!ms.ReadVarU64(v)) return false;
        } else if (fid == 104 || fid == 200) {
            bool hasDef = false;
            if (!ms.ReadBool(hasDef) || hasDef) return false;
        } else {
            return false;
        }
    }
}

inline bool ReadDuckCreateInfoObj(DuckMetaStream& ms, DuckTableEntry& ent) {
    for (;;) {
        uint16_t fid = 0;
        if (!ms.ReadU16(fid)) return false;
        if (fid == 0xFFFFu) return true;
        switch (fid) {
        case 100: {
            uint64_t t = 0;
            if (!ms.ReadVarU64(t) || t > 64) return false;
            ent.catType = static_cast<uint8_t>(t);
            break;
        }
        case 101: {
            std::string cat;
            if (!ms.ReadStr(cat, 4096)) return false;
            break;
        }
        case 102: {
            if (!ms.ReadStr(ent.schema, 4096)) return false;
            break;
        }
        case 103: {
            bool tmp = false;
            if (!ms.ReadBool(tmp)) return false;
            break;
        }
        case 104: {
            if (!ms.ReadBool(ent.internal)) return false;
            break;
        }
        case 105:
        case 108: {
            if (!ms.ReadStr(ent.sql, 65536)) return false;
            break;
        }
        case 106: { // dependencies list (empty or list of {100..0xFFFF})
            uint64_t depCount = 0;
            if (!ms.ReadVarU64(depCount) || depCount > 4096) return false;
            for (uint64_t i = 0; i < depCount; ++i) {
                for (;;) {
                    uint16_t df = 0;
                    if (!ms.ReadU16(df)) return false;
                    if (df == 0xFFFFu) break;
                    if (df == 100) {
                        for (;;) {
                            uint16_t bf = 0;
                            if (!ms.ReadU16(bf)) return false;
                            if (bf == 0xFFFFu) break;
                            if (bf == 100) { uint64_t dt = 0; if (!ms.ReadVarU64(dt)) return false; }
                            else if (bf == 101 || bf == 102) { std::string s; if (!ms.ReadStr(s, 4096)) return false; }
                            else return false;
                        }
                    } else if (df == 101) {
                        std::string cat;
                        if (!ms.ReadStr(cat, 4096)) return false;
                    } else {
                        return false;
                    }
                }
            }
            break;
        }
        case 107: { // optional comment (bool has_val + string)
            bool hasCmt = false;
            if (!ms.ReadBool(hasCmt)) return false;
            if (hasCmt) {
                std::string cmt;
                if (!ms.ReadStr(cmt, 16384)) return false;
            }
            break;
        }
        case 200: { // table / view name
            if (!ms.ReadStr(ent.name, 4096)) return false;
            break;
        }
        case 201: {
            if (ent.catType == 1) {
                // ColumnList object { 100: vector<ColumnDefinition>, 0xFFFF }
                for (;;) {
                    uint16_t cf = 0;
                    if (!ms.ReadU16(cf)) return false;
                    if (cf == 0xFFFFu) break;
                    if (cf == 100) {
                        uint64_t colCount = 0;
                        if (!ms.ReadVarU64(colCount) || colCount > 4096) return false;
                        ent.cols.reserve(static_cast<size_t>(colCount));
                        for (uint64_t c = 0; c < colCount; ++c) {
                            DuckColDef cd;
                            if (!ReadDuckColumnDefObj(ms, cd)) return false;
                            ent.cols.push_back(std::move(cd));
                        }
                    } else {
                        return false;
                    }
                }
            } else if (ent.catType == 3) {
                // View aliases vector<string>
                uint64_t cnt = 0;
                if (!ms.ReadVarU64(cnt) || cnt > 4096) return false;
                for (uint64_t i = 0; i < cnt; ++i) {
                    std::string a;
                    if (!ms.ReadStr(a, 4096)) return false;
                }
            } else {
                return false;
            }
            break;
        }
        case 202: {
            if (ent.catType == 1) {
                // constraints vector<unique_ptr<Constraint>> (support empty or simple NOT NULL / PK)
                uint64_t cnt = 0;
                if (!ms.ReadVarU64(cnt) || cnt > 4096) return false;
                for (uint64_t i = 0; i < cnt; ++i) {
                    bool present = false;
                    if (!ms.ReadBool(present)) return false;
                    if (present) {
                        for (;;) {
                            uint16_t kf = 0;
                            if (!ms.ReadU16(kf)) return false;
                            if (kf == 0xFFFFu) break;
                            if (kf == 100 || kf == 200) { uint64_t v = 0; if (!ms.ReadVarU64(v)) return false; }
                            else if (kf == 201) {
                                uint64_t n = 0; if (!ms.ReadVarU64(n) || n > 1024) return false;
                                for (uint64_t j = 0; j < n; ++j) { std::string s; if (!ms.ReadStr(s, 1024)) return false; }
                            } else return false;
                        }
                    }
                }
            } else if (ent.catType == 3) {
                uint64_t cnt = 0;
                if (!ms.ReadVarU64(cnt) || cnt > 4096) return false;
                for (uint64_t i = 0; i < cnt; ++i) {
                    DuckColDef cd;
                    if (!ReadDuckLogicalTypeObj(ms, cd)) return false;
                    ent.cols.push_back(std::move(cd));
                }
            } else {
                return false;
            }
            break;
        }
        case 204: {
            if (ent.catType != 3) return false;
            uint64_t cnt = 0;
            if (!ms.ReadVarU64(cnt) || cnt > 4096) return false;
            for (uint64_t i = 0; i < cnt; ++i) {
                std::string cn;
                if (!ms.ReadStr(cn, 4096)) return false;
                if (i < ent.cols.size()) ent.cols[static_cast<size_t>(i)].name = std::move(cn);
            }
            break;
        }
        default:
            return false;
        }
    }
}

inline bool ReadDuckBaseStatsVal(DuckMetaStream& ms, const DuckColDef& col, std::wstring& outVal) {
    switch (col.typeId) {
    case 10: { bool b = false; if (!ms.ReadBool(b)) return false; outVal = b ? L"true" : L"false"; return true; }
    case 11: case 12: case 13: case 14: {
        int64_t v = 0; if (!ms.ReadVarI64(v)) return false; outVal = std::to_wstring(v); return true;
    }
    case 15: {
        int64_t v = 0; if (!ms.ReadVarI64(v)) return false; outVal = FormatDuckDateDays(static_cast<int32_t>(v)); return true;
    }
    case 19: case 32: {
        int64_t v = 0; if (!ms.ReadVarI64(v)) return false; outVal = FormatDuckTimestampUs(v); return true;
    }
    case 22: {
        uint32_t u = 0; if (!ms.ReadU32(u)) return false;
        float f = 0.0f; std::memcpy(&f, &u, 4);
        outVal = FmtDouble(f); return true;
    }
    case 23: {
        uint64_t u = 0; if (!ms.ReadU64(u)) return false;
        double d = 0.0; std::memcpy(&d, &u, 8);
        outVal = FmtDouble(d); return true;
    }
    case 24: case 25: case 26: {
        std::string s; if (!ms.ReadStr(s, 4096)) return false;
        outVal = PathUtils::Utf8ToWide(s); return true;
    }
    case 28: case 29: case 30: case 31: {
        uint64_t v = 0; if (!ms.ReadVarU64(v)) return false; outVal = std::to_wstring(v); return true;
    }
    default: {
        std::string s; if (!ms.ReadStr(s, 4096)) return false;
        outVal = PathUtils::Utf8ToWide(s); return true;
    }
    }
}

inline bool ReadDuckBaseStatsObj(DuckMetaStream& ms, const DuckColDef& col, DuckSegPtr& seg) {
    for (;;) {
        uint16_t fid = 0;
        if (!ms.ReadU16(fid)) return false;
        if (fid == 0xFFFFu) return true;
        if (fid == 100) { if (!ms.ReadBool(seg.hasNull)) return false; }
        else if (fid == 101) { if (!ms.ReadBool(seg.hasNoNull)) return false; }
        else if (fid == 102) { uint64_t dc = 0; if (!ms.ReadVarU64(dc)) return false; }
        else if (fid == 103) {
            for (;;) {
                uint16_t tf = 0;
                if (!ms.ReadU16(tf)) return false;
                if (tf == 0xFFFFu) break;
                if (tf == 200) {
                    bool hasMin = false;
                    if (!ms.ReadBool(hasMin)) return false;
                    seg.hasConstVal = hasMin;
                } else if (tf == 201) {
                    if (!ReadDuckBaseStatsVal(ms, col, seg.constVal)) return false;
                    seg.hasConstVal = true;
                } else if (tf == 202) {
                    bool hasMax = false;
                    if (!ms.ReadBool(hasMax)) return false;
                } else if (tf == 203) {
                    std::wstring dummy;
                    if (!ReadDuckBaseStatsVal(ms, col, dummy)) return false;
                } else {
                    return false;
                }
            }
        } else {
            return false;
        }
    }
}

inline bool ReadDuckDataPointerObj(DuckMetaStream& ms, const DuckColDef& col, DuckSegPtr& seg) {
    seg = {};
    for (;;) {
        uint16_t fid = 0;
        if (!ms.ReadU16(fid)) return false;
        if (fid == 0xFFFFu) return true;
        if (fid == 100) {
            if (!ms.ReadVarU64(seg.rowStart)) return false;
        } else if (fid == 101) {
            if (!ms.ReadVarU64(seg.tupleCount)) return false;
        } else if (fid == 102) {
            // BlockPointer { 100: block_id (int64 zigzag), 101: offset (uint32 varint), 0xFFFF }
            for (;;) {
                uint16_t bf = 0;
                if (!ms.ReadU16(bf)) return false;
                if (bf == 0xFFFFu) break;
                if (bf == 100) { if (!ms.ReadVarI64(seg.blockId)) return false; }
                else if (bf == 101) { uint64_t off = 0; if (!ms.ReadVarU64(off)) return false; seg.offset = static_cast<uint32_t>(off); }
                else return false;
            }
        } else if (fid == 103) {
            uint64_t comp = 0;
            if (!ms.ReadVarU64(comp) || comp > 32) return false;
            seg.compression = static_cast<uint8_t>(comp);
        } else if (fid == 104) {
            if (!ReadDuckBaseStatsObj(ms, col, seg)) return false;
        } else if (fid == 105) {
            bool hasState = false;
            if (!ms.ReadBool(hasState)) return false;
            if (hasState) {
                for (;;) {
                    uint16_t sf = 0;
                    if (!ms.ReadU16(sf)) return false;
                    if (sf == 0xFFFFu) break;
                    return false;
                }
            }
        } else {
            return false;
        }
    }
}

inline bool ReadDuckColumnDataObj(DuckMetaStream& ms, const DuckColDef& col, std::vector<DuckSegPtr>& outSegs) {
    for (;;) {
        uint16_t fid = 0;
        if (!ms.ReadU16(fid)) return false;
        if (fid == 0xFFFFu) return true;
        if (fid == 100) {
            uint64_t cnt = 0;
            if (!ms.ReadVarU64(cnt) || cnt > 65536) return false;
            for (uint64_t i = 0; i < cnt; ++i) {
                DuckSegPtr sp;
                if (!ReadDuckDataPointerObj(ms, col, sp)) return false;
                outSegs.push_back(std::move(sp));
            }
        } else {
            return false;
        }
    }
}

inline bool ReadDuckRowGroupObj(const std::vector<uint8_t>& raw, uint64_t blockAlloc, DuckMetaStream& ms, DuckTableEntry& tbl) {
    uint64_t rgRowStart = 0, rgTupleCount = 0;
    std::vector<DuckMetaPtr> colMetaPtrs;
    for (;;) {
        uint16_t fid = 0;
        if (!ms.ReadU16(fid)) return false;
        if (fid == 0xFFFFu) break;
        if (fid == 100) {
            if (!ms.ReadVarU64(rgRowStart)) return false;
        } else if (fid == 101) {
            if (!ms.ReadVarU64(rgTupleCount)) return false;
        } else if (fid == 102) {
            uint64_t colCnt = 0;
            if (!ms.ReadVarU64(colCnt) || colCnt > 4096) return false;
            colMetaPtrs.reserve(static_cast<size_t>(colCnt));
            for (uint64_t c = 0; c < colCnt; ++c) {
                DuckMetaPtr mp;
                if (!ReadDuckMetaPtrObj(ms, mp)) return false;
                colMetaPtrs.push_back(mp);
            }
        } else if (fid == 103) {
            uint64_t delCnt = 0;
            if (!ms.ReadVarU64(delCnt) || delCnt > 4096) return false;
            for (uint64_t d = 0; d < delCnt; ++d) {
                DuckMetaPtr mp;
                if (!ReadDuckMetaPtrObj(ms, mp)) return false;
            }
        } else if (fid == 104) {
            bool hasMeta = false;
            if (!ms.ReadBool(hasMeta)) return false;
        } else if (fid == 105) {
            uint64_t extraCnt = 0;
            if (!ms.ReadVarU64(extraCnt) || extraCnt > 65536) return false;
            for (uint64_t e = 0; e < extraCnt; ++e) {
                uint64_t blk = 0;
                if (!ms.ReadVarU64(blk)) return false;
            }
        } else {
            return false;
        }
    }
    (void)rgRowStart;
    (void)rgTupleCount;

    if (tbl.colSegments.size() < tbl.cols.size()) {
        tbl.colSegments.resize(tbl.cols.size());
    }
    const size_t nCols = std::min(colMetaPtrs.size(), tbl.cols.size());
    for (size_t c = 0; c < nCols; ++c) {
        if (colMetaPtrs[c].blockPtr == UINT64_MAX) continue;
        DuckMetaStream cms(raw, blockAlloc, colMetaPtrs[c].blockPtr, colMetaPtrs[c].offset);
        if (!cms.Valid()) return false;
        if (!ReadDuckColumnDataObj(cms, tbl.cols[c], tbl.colSegments[c])) return false;
    }
    return true;
}

inline bool ReadDuckPersistentTableData(const std::vector<uint8_t>& raw, uint64_t blockAlloc, DuckTableEntry& tbl) {
    if (tbl.tablePtr.blockPtr == UINT64_MAX) return true;
    DuckMetaStream ms(raw, blockAlloc, tbl.tablePtr.blockPtr, tbl.tablePtr.offset);
    if (!ms.Valid()) return false;

    DuckMetaPtr rgStreamPtr;
    uint64_t rgCount = 0;
    for (;;) {
        uint16_t fid = 0;
        if (!ms.ReadU16(fid)) return false;
        if (fid == 0xFFFFu) break;
        if (fid == 100) {
            if (!ms.ReadVarU64(tbl.totalRows)) return false;
        } else if (fid == 101) {
            // Inline row_groups list: varint count + RowGroup objects
            uint64_t inlineRgCnt = 0;
            if (!ms.ReadVarU64(inlineRgCnt) || inlineRgCnt > 65536) return false;
            for (uint64_t r = 0; r < inlineRgCnt; ++r) {
                if (!ReadDuckRowGroupObj(raw, blockAlloc, ms, tbl)) return false;
            }
        } else if (fid == 102) {
            if (!ReadDuckMetaPtrObj(ms, rgStreamPtr)) return false;
        } else if (fid == 103) {
            if (!ms.ReadVarU64(rgCount) || rgCount > 65536) return false;
        } else {
            return false;
        }
    }

    if (rgStreamPtr.blockPtr != UINT64_MAX && rgCount > 0) {
        DuckMetaStream rgMs(raw, blockAlloc, rgStreamPtr.blockPtr, rgStreamPtr.offset);
        if (!rgMs.Valid()) return false;
        for (uint64_t r = 0; r < rgCount; ++r) {
            if (!ReadDuckRowGroupObj(raw, blockAlloc, rgMs, tbl)) return false;
        }
    }
    return true;
}

inline void DecodeDuckColumnValues(
    const std::vector<uint8_t>& raw,
    uint64_t blockAlloc,
    const DuckColDef& col,
    const std::vector<DuckSegPtr>& segs,
    size_t maxRows,
    std::vector<std::wstring>& outVals)
{
    for (const auto& seg : segs) {
        if (outVals.size() >= maxRows) break;
        const size_t want = static_cast<size_t>(std::min<uint64_t>(seg.tupleCount, maxRows - outVals.size()));
        if (want == 0) continue;

        // CONSTANT compression (2)
        if (seg.compression == 2) {
            const std::wstring v = (seg.hasNull && !seg.hasNoNull && !seg.hasConstVal) ? L"NULL" : seg.constVal;
            for (size_t i = 0; i < want; ++i) outVals.push_back(v);
            continue;
        }

        if (seg.blockId < 0) continue;
        const uint64_t bid = static_cast<uint64_t>(seg.blockId);
        if (bid > (raw.size() - 12288ULL) / blockAlloc) continue;
        const uint64_t segFileOff = 12288ULL + bid * blockAlloc + 8ULL + seg.offset;
        if (segFileOff >= raw.size()) continue;
        const uint8_t* p = raw.data() + static_cast<size_t>(segFileOff);
        const size_t avail = raw.size() - static_cast<size_t>(segFileOff);

        auto decodeFixedElem = [&](const uint8_t* ep, size_t elemSz) -> std::wstring {
            switch (col.typeId) {
            case 10: return ep[0] ? L"true" : L"false";
            case 11: return std::to_wstring(static_cast<int8_t>(ep[0]));
            case 12: return std::to_wstring(RdI16LE(ep));
            case 13: return std::to_wstring(RdI32LE(ep));
            case 14: return std::to_wstring(RdI64LE(ep));
            case 15: return FormatDuckDateDays(RdI32LE(ep));
            case 17: return FormatDuckTimestampUs(RdI64LE(ep) * 1000000LL);
            case 18: return FormatDuckTimestampUs(RdI64LE(ep) * 1000LL);
            case 19:
            case 32: return FormatDuckTimestampUs(RdI64LE(ep));
            case 20: return FormatDuckTimestampUs(RdI64LE(ep) / 1000LL);
            case 21: {
                int64_t rawDec = (elemSz == 2) ? RdI16LE(ep) : ((elemSz == 4) ? RdI32LE(ep) : RdI64LE(ep));
                if (col.decScale == 0) return std::to_wstring(rawDec);
                double sc = std::pow(10.0, static_cast<int>(col.decScale));
                return FmtDouble(static_cast<double>(rawDec) / sc);
            }
            case 22: return FmtDouble(RdF32LE(ep));
            case 23: return FmtDouble(RdF64LE(ep));
            case 28: return std::to_wstring(ep[0]);
            case 29: return std::to_wstring(RdU16LE(ep));
            case 30: return std::to_wstring(RdU32LE(ep));
            case 31: return std::to_wstring(RdU64LE(ep));
            default: return L"";
            }
        };

        size_t fixedSz = 0;
        switch (col.typeId) {
        case 10: case 11: case 28: fixedSz = 1; break;
        case 12: case 29: fixedSz = 2; break;
        case 13: case 15: case 22: case 30: fixedSz = 4; break;
        case 14: case 16: case 17: case 18: case 19: case 20: case 23: case 31: case 32: fixedSz = 8; break;
        case 21: fixedSz = (col.decWidth <= 4) ? 2 : ((col.decWidth <= 9) ? 4 : 8); break;
        default: break;
        }

        // UNCOMPRESSED (1)
        if (seg.compression == 1) {
            if (fixedSz > 0) {
                const uint64_t valBytes = seg.tupleCount * fixedSz;
                const uint8_t* validityMask = (seg.hasNull && valBytes + ((seg.tupleCount + 63ULL) / 64ULL) * 8ULL <= avail)
                    ? (p + static_cast<size_t>(valBytes))
                    : nullptr;
                for (size_t i = 0; i < want && (i + 1) * fixedSz <= avail; ++i) {
                    if (validityMask) {
                        const uint64_t word = RdU64LE(validityMask + (i / 64) * 8);
                        if (((word >> (i % 64)) & 1ULL) == 0) {
                            outVals.push_back(L"NULL");
                            continue;
                        }
                    }
                    outVals.push_back(decodeFixedElem(p + i * fixedSz, fixedSz));
                }
            } else if (col.typeId == 24 || col.typeId == 25 || col.typeId == 26) {
                // Layout A: DuckDB StringUncompressed dictionary-at-end ([u32 dict_size][u32 dict_end][i32 end_offsets[tupleCount]] ... dict at end)
                if (avail >= 8 + want * 4) {
                    const uint32_t dictSize = RdU32LE(p);
                    const uint32_t dictEnd  = RdU32LE(p + 4);
                    if (dictEnd >= 8 + seg.tupleCount * 4 && dictEnd <= avail && dictSize <= dictEnd) {
                        uint32_t prevAbs = 0;
                        bool okDict = true;
                        std::vector<std::wstring> tmpStrs;
                        tmpStrs.reserve(want);
                        for (size_t i = 0; i < want; ++i) {
                            const int32_t rawOff = RdI32LE(p + 8 + i * 4);
                            const uint32_t curAbs = static_cast<uint32_t>(rawOff < 0 ? -rawOff : rawOff);
                            if (curAbs < prevAbs || curAbs > dictSize || curAbs > dictEnd) { okDict = false; break; }
                            const uint32_t sLen = curAbs - prevAbs;
                            const char* sPtr = reinterpret_cast<const char*>(p + dictEnd - curAbs);
                            tmpStrs.push_back(PathUtils::Utf8ToWide(std::string(sPtr, sLen)));
                            prevAbs = curAbs;
                        }
                        if (okDict) {
                            for (auto& s : tmpStrs) outVals.push_back(std::move(s));
                            continue;
                        }
                    }
                }
                // Layout B: DuckDB 16-byte string_t vector ([u32 len][12B inline OR 4B prefix + u64 relOffset])
                if (avail >= want * 16) {
                    for (size_t i = 0; i < want; ++i) {
                        const uint8_t* sp = p + i * 16;
                        const uint32_t sLen = RdU32LE(sp);
                        if (sLen <= 12) {
                            outVals.push_back(PathUtils::Utf8ToWide(std::string(reinterpret_cast<const char*>(sp + 4), sLen)));
                        } else {
                            const uint64_t strOff = RdU64LE(sp + 8);
                            if (strOff <= avail && sLen <= avail - static_cast<size_t>(strOff)) {
                                outVals.push_back(PathUtils::Utf8ToWide(std::string(reinterpret_cast<const char*>(p + strOff), sLen)));
                            } else {
                                outVals.push_back(PathUtils::Utf8ToWide(std::string(reinterpret_cast<const char*>(sp + 4), 4)) + L"...");
                            }
                        }
                    }
                }
            }
        } else if (seg.compression == 3 && fixedSz > 0 && avail >= 4) {
            // RLE compression: [u32 run_count] followed by runs of [u16 count][T value]
            const uint32_t runCount = RdU32LE(p);
            size_t cur = 4;
            for (uint32_t r = 0; r < runCount && outVals.size() < maxRows; ++r) {
                if (cur + 2 + fixedSz > avail) break;
                const uint16_t rep = RdU16LE(p + cur);
                const std::wstring val = decodeFixedElem(p + cur + 2, fixedSz);
                cur += 2 + fixedSz;
                for (uint16_t k = 0; k < rep && outVals.size() < maxRows; ++k) {
                    outVals.push_back(val);
                }
            }
        } else if (seg.compression == 6 && (col.typeId == 24 || col.typeId == 25) && avail >= 12) {
            // DICTIONARY compression: [u32 dict_size][u32 dict_end][u32 dict_count][u32 index[tupleCount]][u32 end_offsets[dict_count]] ...
            const uint32_t dictSize  = RdU32LE(p);
            const uint32_t dictEnd   = RdU32LE(p + 4);
            const uint32_t dictCount = RdU32LE(p + 8);
            const uint64_t idxTableBytes = seg.tupleCount * 4ULL;
            const uint64_t offTableStart = 12ULL + idxTableBytes;
            if (dictEnd <= avail && dictSize <= dictEnd && dictCount <= 65536 && offTableStart + dictCount * 4ULL <= dictEnd) {
                std::vector<std::wstring> dictEntries(dictCount);
                uint32_t prev = 0;
                bool ok = true;
                for (uint32_t d = 0; d < dictCount; ++d) {
                    const uint32_t curAbs = RdU32LE(p + static_cast<size_t>(offTableStart) + d * 4);
                    if (curAbs < prev || curAbs > dictSize) { ok = false; break; }
                    const uint32_t sLen = curAbs - prev;
                    dictEntries[d] = PathUtils::Utf8ToWide(std::string(reinterpret_cast<const char*>(p + dictEnd - curAbs), sLen));
                    prev = curAbs;
                }
                if (ok) {
                    for (size_t i = 0; i < want; ++i) {
                        const uint32_t idx = RdU32LE(p + 12 + i * 4);
                        outVals.push_back(idx < dictEntries.size() ? dictEntries[idx] : L"NULL");
                    }
                }
            }
        }
    }
}

inline bool ParseDuckDb(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    // DuckDB MainHeader at sector 0 (0..4095), DatabaseHeader 0 at 4096, DatabaseHeader 1 at 8192, blocks at 12288+
    if (raw.size() < 12288 + 32 || std::memcmp(raw.data() + 8, "DUCK", 4) != 0) return false;

    const uint64_t fmtVersion = RdU64LE(raw.data() + 12);
    if (fmtVersion == 0 || fmtVersion > 100000) return false;

    std::wstring gitDesc = CleanAsciiFixed(raw.data() + 52, 64);
    std::wstring gitHash = CleanAsciiFixed(raw.data() + 116, 64);
    if (gitDesc.empty()) {
        gitDesc = CleanAsciiFixed(raw.data() + 28, 64);
        gitHash = CleanAsciiFixed(raw.data() + 92, 64);
    }

    auto resolveAlloc = [&](uint64_t hdrAlloc) -> uint64_t {
        if (hdrAlloc >= 4096ULL && hdrAlloc <= 16777216ULL && (hdrAlloc & (hdrAlloc - 1ULL)) == 0) {
            return hdrAlloc;
        }
        if (raw.size() >= 12288ULL + 262144ULL) return 262144ULL;
        const uint64_t dataBytes = raw.size() - 12288ULL;
        if (dataBytes >= 16384ULL && (dataBytes % 16384ULL) == 0) return 16384ULL;
        if (dataBytes >= 4096ULL && (dataBytes % 4096ULL) == 0) return 4096ULL;
        return 262144ULL;
    };

    struct DbSlot {
        bool valid = false;
        size_t sectorOff = 0;
        uint64_t iteration = 0;
        uint64_t metaBlock = UINT64_MAX;
        uint64_t freeList = UINT64_MAX;
        uint64_t blockCount = 0;
        uint64_t blockAlloc = 262144;
        uint64_t vectorSize = 2048;
    };

    auto readSlot = [&](size_t off) -> DbSlot {
        DbSlot s;
        s.sectorOff = off;
        s.iteration  = RdU64LE(raw.data() + off + 8);
        s.metaBlock  = RdU64LE(raw.data() + off + 16);
        s.freeList   = RdU64LE(raw.data() + off + 24);
        s.blockCount = RdU64LE(raw.data() + off + 32);
        const uint64_t rawAlloc = RdU64LE(raw.data() + off + 40);
        s.blockAlloc = resolveAlloc(rawAlloc);
        s.vectorSize = RdU64LE(raw.data() + off + 48);

        if (s.metaBlock == UINT64_MAX) return s;
        if (s.iteration == 0 && s.blockCount == 0) return s;
        const uint64_t physId = s.metaBlock & 0x00FFFFFFFFFFFFFFULL;
        const uint8_t subIdx  = static_cast<uint8_t>(s.metaBlock >> 56);
        if (subIdx >= 64) return s;
        if (physId > (raw.size() - 12288ULL) / s.blockAlloc) return s;
        if (12288ULL + physId * s.blockAlloc + 18ULL > raw.size()) return s;
        s.valid = true;
        return s;
    };

    const DbSlot slot0 = readSlot(4096);
    const DbSlot slot1 = readSlot(8192);
    if (!slot0.valid && !slot1.valid) return false;
    const DbSlot& active = (!slot0.valid) ? slot1 : ((!slot1.valid) ? slot0 : ((slot1.iteration >= slot0.iteration) ? slot1 : slot0));

    // Walk linked MetaBlocks and deserialize the binary Catalog via DuckDB BinaryMetadataDeserializer
    DuckMetaStream rootStream(raw, active.blockAlloc, active.metaBlock, 0);
    if (!rootStream.Valid()) return false;

    std::vector<DuckTableEntry> entries;
    for (size_t entryIdx = 0; entryIdx < 4096; ++entryIdx) {
        uint16_t outerFid = 0;
        if (!rootStream.ReadU16(outerFid)) break;
        if (outerFid == 0xFFFFu) break; // explicit end of checkpoint stream
        if (outerFid != 100u) return false; // reject non-DuckDB / scraped text blocks!

        DuckTableEntry ent;
        if (!ReadDuckCreateInfoObj(rootStream, ent)) return false;

        // Read remaining fields of checkpoint catalog wrapper (101 = MetaBlockPointer table_pointer, terminated by 0xFFFF)
        for (;;) {
            uint16_t wf = 0;
            if (!rootStream.ReadU16(wf)) return false;
            if (wf == 0xFFFFu) break;
            if (wf == 101) {
                if (!ReadDuckMetaPtrObj(rootStream, ent.tablePtr)) return false;
            } else {
                return false;
            }
        }

        if (ent.catType == 0) break; // CatalogType::INVALID sentinel marks end of catalog
        entries.push_back(std::move(ent));
    }

    if (entries.empty()) return false;

    for (auto& ent : entries) {
        if (ent.catType == 1 && ent.tablePtr.blockPtr != UINT64_MAX) {
            if (!ReadDuckPersistentTableData(raw, active.blockAlloc, ent)) return false;
        }
    }

    SpreadsheetSheet catSheet;
    catSheet.name = L"DuckDB_Header_And_Catalog";
    catSheet.rows.push_back({ L"Storage Field / Catalog Object", L"Category", L"Value / Schema Details" });
    catSheet.rows.push_back({ L"Storage Magic", L"MainHeader", L"DUCK" });
    catSheet.rows.push_back({ L"Storage Format Version", L"MainHeader", std::to_wstring(fmtVersion) });
    if (!gitDesc.empty()) catSheet.rows.push_back({ L"Engine Version", L"MainHeader", gitDesc });
    if (!gitHash.empty()) catSheet.rows.push_back({ L"Engine Git Hash", L"MainHeader", gitHash });
    catSheet.rows.push_back({ L"Active Checkpoint Iteration", L"DatabaseHeader", std::to_wstring(active.iteration) });
    catSheet.rows.push_back({ L"Meta Block Pointer", L"DatabaseHeader",
        L"block=" + std::to_wstring(active.metaBlock & 0x00FFFFFFFFFFFFFFULL) +
        L", sub=" + std::to_wstring(active.metaBlock >> 56) });
    catSheet.rows.push_back({ L"Block Count", L"DatabaseHeader", std::to_wstring(active.blockCount) });
    catSheet.rows.push_back({ L"Block Allocation Size", L"DatabaseHeader", PathUtils::FormatByteSize(active.blockAlloc) });
    if (active.vectorSize > 0) {
        catSheet.rows.push_back({ L"Vector Size", L"DatabaseHeader", std::to_wstring(active.vectorSize) });
    }

    std::vector<SpreadsheetSheet> tableSheets;
    for (const auto& ent : entries) {
        const std::wstring schW = ent.schema.empty() ? L"main" : PathUtils::Utf8ToWide(ent.schema);
        const std::wstring nameW = PathUtils::Utf8ToWide(ent.name);
        if (ent.catType == 2) {
            catSheet.rows.push_back({ schW, L"Schema", ent.internal ? L"internal schema" : L"user schema" });
        } else if (ent.catType == 3) {
            std::wstring detail = ent.sql.empty() ? L"VIEW" : PathUtils::Utf8ToWide(ent.sql);
            catSheet.rows.push_back({ schW + L"." + nameW, L"View", std::move(detail) });
        } else if (ent.catType == 1) {
            std::wstring colSummary;
            for (size_t c = 0; c < ent.cols.size(); ++c) {
                if (c > 0) colSummary += L", ";
                colSummary += PathUtils::Utf8ToWide(ent.cols[c].name) + L" " + DuckTypeName(ent.cols[c]);
            }
            std::wstring infoStr = L"(" + colSummary + L") \x2014 " + std::to_wstring(ent.totalRows) + L" rows";
            catSheet.rows.push_back({ schW + L"." + nameW, L"Table", std::move(infoStr) });

            if (!ent.internal && !ent.cols.empty() && tableSheets.size() < 24) {
                SpreadsheetSheet tsh;
                tsh.name = nameW.empty() ? L"Table" : nameW;
                std::vector<std::wstring> hdrRow;
                hdrRow.reserve(ent.cols.size());
                for (const auto& cd : ent.cols) {
                    hdrRow.push_back(PathUtils::Utf8ToWide(cd.name) + L" (" + DuckTypeName(cd) + L")");
                }
                tsh.rows.push_back(std::move(hdrRow));

                const size_t maxPreviewRows = static_cast<size_t>(std::min<uint64_t>(ent.totalRows, 256));
                if (maxPreviewRows > 0 && ent.colSegments.size() == ent.cols.size()) {
                    std::vector<std::vector<std::wstring>> decodedCols(ent.cols.size());
                    size_t rowCap = 0;
                    for (size_t c = 0; c < ent.cols.size(); ++c) {
                        DecodeDuckColumnValues(raw, active.blockAlloc, ent.cols[c], ent.colSegments[c], maxPreviewRows, decodedCols[c]);
                        rowCap = std::max(rowCap, decodedCols[c].size());
                    }
                    for (size_t r = 0; r < rowCap; ++r) {
                        std::vector<std::wstring> row(ent.cols.size());
                        for (size_t c = 0; c < ent.cols.size(); ++c) {
                            if (r < decodedCols[c].size()) row[c] = std::move(decodedCols[c][r]);
                        }
                        tsh.rows.push_back(std::move(row));
                    }
                }
                FinalizeSheetCols(tsh);
                tableSheets.push_back(std::move(tsh));
            }
        }
    }

    FinalizeSheetCols(catSheet);
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"DuckDB Analytical Database (.duckdb)";
    out.sheets.push_back(std::move(catSheet));
    for (auto& ts : tableSheets) out.sheets.push_back(std::move(ts));
    out.valid = true;
    return true;
}

} // namespace AiDataDetail

// Exposed in namespace Formats so ExtendedGeoSci100.h (NetCDF-4) and SpreadsheetDocViewer.cpp (>32 MB Arrow/Feather) can invoke directly
inline bool ParseHdf5Doc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    return AiDataDetail::ParseHdf5Doc(raw, extLower, out);
}

inline bool ParseArrowOrFeatherFile(const std::wstring& fullPath, const std::wstring& extLower, ParsedOfficeDocument& out) {
    return AiDataDetail::ParseArrowOrFeatherFile(fullPath, extLower, out);
}

inline bool ParseExtendedAiDataDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (extLower == L".npy" || (raw.size() >= 10 && std::memcmp(raw.data(), "\x93NUMPY", 6) == 0)) {
        std::vector<SpreadsheetSheet> sheets;
        if (!AiDataDetail::ParseSingleNpyBuffer(raw.data(), raw.size(), L"NumPy_Array", sheets, true)) return false;
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"NumPy N-Dimensional Tensor (.npy)";
        out.sheets = std::move(sheets);
        out.valid = true;
        return true;
    }
    if (extLower == L".npz") return AiDataDetail::ParseNumPyNpz(raw, out);
    if (extLower == L".ipynb") return AiDataDetail::ParseIpynbNotebook(raw, out);
    if (extLower == L".cbor") return AiDataDetail::ParseCborDocument(raw, out);
    if (extLower == L".msgpack" || extLower == L".mpk") return AiDataDetail::ParseMsgPackDocument(raw, extLower, out);
    if (extLower == L".onnx") return AiDataDetail::ParseOnnxModel(raw, out);
    if (extLower == L".gguf") return AiDataDetail::ParseGgufModel(raw, out);
    if (extLower == L".safetensors") return AiDataDetail::ParseSafeTensorsModel(raw, out);
    if (extLower == L".pt" || extLower == L".pth" || extLower == L".ckpt") return AiDataDetail::ParsePyTorchCheckpoint(raw, extLower, out);
    if (extLower == L".tflite") return AiDataDetail::ParseTfLiteModel(raw, out);
    if (extLower == L".feather" || extLower == L".arrow" || extLower == L".ipc") return AiDataDetail::ParseArrowOrFeather(raw, extLower, out);
    if (extLower == L".orc") return AiDataDetail::ParseApacheOrc(raw, out);
    if (extLower == L".h5" || extLower == L".hdf5") return AiDataDetail::ParseHdf5Doc(raw, extLower, out);
    if (extLower == L".mat") return AiDataDetail::ParseMatlabMat(raw, out);
    if (extLower == L".sav") return AiDataDetail::ParseSpssSav(raw, out);
    if (extLower == L".dta") return AiDataDetail::ParseStataDta(raw, out);
    if (extLower == L".sas7bdat") return AiDataDetail::ParseSas7bdat(raw, out);
    if (extLower == L".rds") return AiDataDetail::ParseRdsData(raw, out);
    if (extLower == L".duckdb") return AiDataDetail::ParseDuckDb(raw, out);
    return false;
}

} // namespace Formats
