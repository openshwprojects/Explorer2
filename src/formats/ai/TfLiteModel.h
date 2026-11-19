#pragma once
// TfLiteModel.h — TensorFlow Lite FlatBuffer Model (.tflite) Reader

namespace Formats {
namespace AiDataDetail {

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

} // namespace AiDataDetail

inline bool ParseTfLiteModel(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return AiDataDetail::ParseTfLiteModel(raw, out); }

} // namespace Formats
