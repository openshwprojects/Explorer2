#pragma once
// GgufModel.h — GGML Universal Format (.gguf v1/v2/v3) streaming quantized model reader.
#include "../common/AiModelCommon.h"

namespace Explorer2::AiModels::Detail {

// ============================================================================
// 2. GGUF (.gguf v1, v2, v3)
// ============================================================================
struct GgmlTypeTrait {
    const wchar_t* name;
    uint32_t blockElems;
    uint32_t blockBytes;
};

inline bool GetGgmlTypeTrait(uint32_t t, GgmlTypeTrait& out) {
    switch (t) {
    case 0:  out = { L"F32",         1,   4   }; return true;
    case 1:  out = { L"F16",         1,   2   }; return true;
    case 2:  out = { L"Q4_0",        32,  18  }; return true;
    case 3:  out = { L"Q4_1",        32,  20  }; return true;
    case 4:  out = { L"Q4_2",        16,  10  }; return true;
    case 5:  out = { L"Q4_3",        16,  12  }; return true;
    case 6:  out = { L"Q5_0",        32,  22  }; return true;
    case 7:  out = { L"Q5_1",        32,  24  }; return true;
    case 8:  out = { L"Q8_0",        32,  34  }; return true;
    case 9:  out = { L"Q8_1",        32,  36  }; return true;
    case 10: out = { L"Q2_K",        256, 84  }; return true;
    case 11: out = { L"Q3_K",        256, 110 }; return true;
    case 12: out = { L"Q4_K",        256, 144 }; return true;
    case 13: out = { L"Q5_K",        256, 176 }; return true;
    case 14: out = { L"Q6_K",        256, 210 }; return true;
    case 15: out = { L"Q8_K",        256, 292 }; return true;
    case 16: out = { L"IQ2_XXS",     256, 66  }; return true;
    case 17: out = { L"IQ2_XS",      256, 74  }; return true;
    case 18: out = { L"IQ3_XXS",     256, 98  }; return true;
    case 19: out = { L"IQ1_S",       256, 50  }; return true;
    case 20: out = { L"IQ4_NL",      32,  18  }; return true;
    case 21: out = { L"IQ3_S",       256, 110 }; return true;
    case 22: out = { L"IQ2_S",       256, 82  }; return true;
    case 23: out = { L"IQ4_XS",      256, 136 }; return true;
    case 24: out = { L"I8",          1,   1   }; return true;
    case 25: out = { L"I16",         1,   2   }; return true;
    case 26: out = { L"I32",         1,   4   }; return true;
    case 27: out = { L"I64",         1,   8   }; return true;
    case 28: out = { L"F64",         1,   8   }; return true;
    case 29: out = { L"IQ1_M",       256, 56  }; return true;
    case 30: out = { L"BF16",        1,   2   }; return true;
    case 31: out = { L"Q4_0_4_4",    32,  18  }; return true;
    case 32: out = { L"Q4_0_4_8",    32,  18  }; return true;
    case 33: out = { L"Q4_0_8_8",    32,  18  }; return true;
    case 34: out = { L"TQ1_0",       256, 54  }; return true;
    case 35: out = { L"TQ2_0",       256, 66  }; return true;
    case 36: out = { L"IQ4_NL_4_4",  32,  18  }; return true;
    case 37: out = { L"IQ4_NL_4_8",  32,  18  }; return true;
    case 38: out = { L"IQ4_NL_8_8",  32,  18  }; return true;
    case 39: out = { L"MXFP4",       32,  17  }; return true;
    default: return false;
    }
}

inline const wchar_t* GgufMetaTypeName(uint32_t vt) {
    switch (vt) {
    case 0:  return L"uint8";
    case 1:  return L"int8";
    case 2:  return L"uint16";
    case 3:  return L"int16";
    case 4:  return L"uint32";
    case 5:  return L"int32";
    case 6:  return L"float32";
    case 7:  return L"bool";
    case 8:  return L"string";
    case 9:  return L"array";
    case 10: return L"uint64";
    case 11: return L"int64";
    case 12: return L"float64";
    default: return L"unknown";
    }
}

inline uint32_t GgufFixedScalarByteSize(uint32_t vt) {
    switch (vt) {
    case 0: case 1: case 7:    return 1;
    case 2: case 3:            return 2;
    case 4: case 5: case 6:    return 4;
    case 10: case 11: case 12: return 8;
    default:                   return 0;
    }
}

inline bool ReadGgufCount(AiModelStream& st, uint32_t version, uint64_t& out) {
    if (version == 1) {
        uint32_t v32 = 0;
        if (!st.ReadU32(v32)) return false;
        out = v32;
        return true;
    }
    return st.ReadU64(out);
}

inline bool ReadGgufString(AiModelStream& st, uint32_t version, std::string& out, size_t maxKeep = 512) {
    uint64_t len = 0;
    if (!ReadGgufCount(st, version, len)) return false;
    if (len > st.Remaining()) return false;
    const size_t keep = static_cast<size_t>(std::min<uint64_t>(len, maxKeep));
    out.resize(keep);
    if (keep > 0 && !st.ReadBytes(out.data(), keep)) return false;
    if (len > keep && !st.Skip(len - keep)) return false;
    return true;
}

inline bool ReadGgufMetadataValue(AiModelStream& st, uint32_t version, uint32_t vtype, std::wstring& outVal, bool captureText, int depth = 0) {
    if (depth > 4) return false;
    switch (vtype) {
    case 0: { uint8_t v = 0;  if (!st.ReadU8(v)) return false;  if (captureText) outVal = std::to_wstring(v); return true; }
    case 1: { uint8_t v = 0;  if (!st.ReadU8(v)) return false;  if (captureText) outVal = std::to_wstring(static_cast<int8_t>(v)); return true; }
    case 2: { uint16_t v = 0; if (!st.ReadU16(v)) return false; if (captureText) outVal = std::to_wstring(v); return true; }
    case 3: { uint16_t v = 0; if (!st.ReadU16(v)) return false; if (captureText) outVal = std::to_wstring(static_cast<int16_t>(v)); return true; }
    case 4: { uint32_t v = 0; if (!st.ReadU32(v)) return false; if (captureText) outVal = std::to_wstring(v); return true; }
    case 5: { uint32_t v = 0; if (!st.ReadU32(v)) return false; if (captureText) outVal = std::to_wstring(static_cast<int32_t>(v)); return true; }
    case 6: { float v = 0.0f; if (!st.ReadF32(v)) return false; if (captureText) outVal = FormatScalarDouble(v); return true; }
    case 7: {
        uint8_t v = 0;
        if (!st.ReadU8(v) || v > 1u) return false;
        if (captureText) outVal = (v != 0) ? L"true" : L"false";
        return true;
    }
    case 8: {
        std::string s;
        if (!ReadGgufString(st, version, s, captureText ? 300 : 0)) return false;
        if (captureText) outVal = PathUtils::Utf8ToWide(s);
        return true;
    }
    case 9: {
        uint32_t elemType = 0;
        uint64_t count = 0;
        if (!st.ReadU32(elemType) || elemType > 12 || !ReadGgufCount(st, version, count)) return false;
        const uint32_t fixedSz = GgufFixedScalarByteSize(elemType);
        if (fixedSz > 0) {
            if (count > st.Remaining() / fixedSz) return false;
            if (!captureText) return st.Skip(count * fixedSz);
            const uint64_t previewN = std::min<uint64_t>(count, 6);
            std::wstring sample;
            for (uint64_t i = 0; i < previewN; ++i) {
                std::wstring ev;
                if (!ReadGgufMetadataValue(st, version, elemType, ev, true, depth + 1)) return false;
                if (i > 0) sample += L", ";
                sample += ev;
            }
            if (count > previewN) {
                if (!st.Skip((count - previewN) * fixedSz)) return false;
                outVal = L"[" + std::to_wstring(count) + L" \x00D7 " + GgufMetaTypeName(elemType) + L"] (" + sample + L", ...)";
            } else {
                outVal = L"[" + sample + L"]";
            }
            return true;
        }
        if (count > 10000000ULL) return false;
        const uint64_t previewN = captureText ? std::min<uint64_t>(count, 5) : 0;
        std::wstring sample;
        for (uint64_t i = 0; i < count; ++i) {
            std::wstring ev;
            const bool wantElem = (i < previewN);
            if (!ReadGgufMetadataValue(st, version, elemType, ev, wantElem, depth + 1)) return false;
            if (wantElem) {
                if (i > 0) sample += L", ";
                if (ev.size() > 24) ev = ev.substr(0, 24) + L"...";
                sample += L"\"" + ev + L"\"";
            }
        }
        if (captureText) {
            outVal = (count > previewN)
                ? (L"[" + std::to_wstring(count) + L" \x00D7 " + GgufMetaTypeName(elemType) + L"] (" + sample + L", ...)")
                : (L"[" + sample + L"]");
        }
        return true;
    }
    case 10: { uint64_t v = 0; if (!st.ReadU64(v)) return false; if (captureText) outVal = std::to_wstring(v); return true; }
    case 11: { uint64_t v = 0; if (!st.ReadU64(v)) return false; if (captureText) outVal = std::to_wstring(static_cast<int64_t>(v)); return true; }
    case 12: { double v = 0.0; if (!st.ReadF64(v)) return false; if (captureText) outVal = FormatScalarDouble(v); return true; }
    default: return false;
    }
}

inline bool ParseGgufStream(AiModelStream& st, ParsedModelData& out) {
    out = {};
    if (st.FileSize() < 16) return false;
    uint8_t magic[4] = {};
    uint32_t version = 0;
    if (!st.ReadBytes(magic, 4) || std::memcmp(magic, "GGUF", 4) != 0) return false;
    if (!st.ReadU32(version) || version < 1 || version > 3) return false;

    uint64_t tensorCount = 0;
    uint64_t kvCount = 0;
    if (!ReadGgufCount(st, version, tensorCount) || !ReadGgufCount(st, version, kvCount)) return false;
    if (kvCount > 1000000ULL || tensorCount > 5000000ULL) return false;

    std::map<std::string, std::wstring> kvMap;
    for (uint64_t i = 0; i < kvCount; ++i) {
        std::string key;
        if (!ReadGgufString(st, version, key, 256) || key.empty()) return false;
        uint32_t vtype = 0;
        if (!st.ReadU32(vtype) || vtype > 12) return false;
        const bool keepRow = (out.kvEntries.size() < 512);
        const bool needForSummary = keepRow || (key.rfind("general.", 0) == 0) || (key.find("context_length") != std::string::npos);
        std::wstring valW;
        if (!ReadGgufMetadataValue(st, version, vtype, valW, needForSummary, 0)) return false;
        if (needForSummary) kvMap[key] = valW;
        if (keepRow) {
            out.kvEntries.push_back({ PathUtils::Utf8ToWide(key), GgufMetaTypeName(vtype), std::move(valW) });
        }
    }

    std::map<std::wstring, uint64_t> quantHist;
    for (uint64_t t = 0; t < tensorCount; ++t) {
        std::string tName;
        if (!ReadGgufString(st, version, tName, 512) || tName.empty()) return false;
        uint32_t nDims = 0;
        if (!st.ReadU32(nDims) || nDims < 1 || nDims > 8) return false;

        uint64_t dims[8] = {};
        uint64_t params = 1;
        for (uint32_t d = 0; d < nDims; ++d) {
            if (!st.ReadU64(dims[d]) || dims[d] == 0) return false;
            if (params > UINT64_MAX / dims[d]) params = UINT64_MAX;
            else params *= dims[d];
        }

        uint32_t ggmlType = 0;
        uint64_t relOffset = 0;
        if (!st.ReadU32(ggmlType) || !st.ReadU64(relOffset)) return false;
        GgmlTypeTrait trait{};
        if (!GetGgmlTypeTrait(ggmlType, trait)) return false;
        if (relOffset > st.FileSize()) return false;

        const uint64_t blocks = (params + trait.blockElems - 1ULL) / trait.blockElems;
        const uint64_t byteSize = (blocks <= UINT64_MAX / trait.blockBytes) ? (blocks * trait.blockBytes) : UINT64_MAX;

        if (out.totalParameters <= UINT64_MAX - params) out.totalParameters += params;
        else out.totalParameters = UINT64_MAX;
        quantHist[trait.name]++;

        if (out.tensors.size() < 1024) {
            ParsedTensorRow tr;
            tr.name = PathUtils::Utf8ToWide(tName);
            tr.dtype = trait.name;
            tr.shape = L"[";
            for (uint32_t d = 0; d < nDims; ++d) {
                if (d > 0) tr.shape += L", ";
                tr.shape += std::to_wstring(dims[d]);
            }
            tr.shape += L"]";
            tr.paramCount = params;
            tr.byteSize = byteSize;
            tr.dataOffset = relOffset;
            out.tensors.push_back(std::move(tr));
        }
    }

    out.valid = true;
    out.formatVersion = version;
    out.formatName = L"GGUF v" + std::to_wstring(version);
    out.tensorCount = tensorCount;
    out.metadataKeyCount = kvCount;

    auto findKv = [&](const std::string& k) -> std::wstring {
        auto it = kvMap.find(k);
        return (it != kvMap.end()) ? it->second : L"";
    };

    out.arch = findKv("general.architecture");
    out.modelTitle = findKv("general.name");
    out.baseModel = findKv("general.basename");
    if (!out.arch.empty()) {
        const std::string archUtf8 = PathUtils::WideToUtf8(out.arch);
        out.contextLength = findKv(archUtf8 + ".context_length");
    }
    if (out.contextLength.empty()) {
        for (const auto& kv : kvMap) {
            if (kv.first.size() > 15 && kv.first.rfind(".context_length") == kv.first.size() - 15) {
                out.contextLength = kv.second;
                break;
            }
        }
    }

    for (const auto& q : quantHist) {
        if (!out.precisionSummary.empty()) out.precisionSummary += L", ";
        out.precisionSummary += q.first + L" (" + std::to_wstring(q.second) + L")";
    }
    if (out.precisionSummary.empty()) {
        std::wstring ft = findKv("general.file_type");
        if (!ft.empty()) out.precisionSummary = L"FileType #" + ft;
    }
    return true;
}


inline bool BuildGgufDocument(const ParsedModelData& m, ParsedOfficeDocument& out) {
    if (!m.valid) return false;
        SpreadsheetSheet metaSheet;
        metaSheet.name = L"GGUF_Metadata";
        metaSheet.rows.push_back({ L"Key", L"Value Type", L"Value" });
        metaSheet.rows.push_back({ L"gguf.version", L"uint32", std::to_wstring(m.formatVersion) });
        metaSheet.rows.push_back({ L"gguf.tensor_count", L"uint64", std::to_wstring(m.tensorCount) });
        metaSheet.rows.push_back({ L"gguf.kv_count", L"uint64", std::to_wstring(m.metadataKeyCount) });
        metaSheet.rows.push_back({ L"gguf.total_parameters", L"uint64", std::to_wstring(m.totalParameters) });
        for (const auto& kv : m.kvEntries) {
            metaSheet.rows.push_back({ kv.key, kv.typeName, kv.value });
        }

        SpreadsheetSheet tensorSheet;
        tensorSheet.name = L"GGUF_Tensors";
        tensorSheet.rows.push_back({ L"#", L"Tensor Name", L"Quantization", L"Shape", L"Parameters", L"Byte Size", L"Data Offset" });
        for (size_t i = 0; i < m.tensors.size(); ++i) {
            const auto& t = m.tensors[i];
            tensorSheet.rows.push_back({
                std::to_wstring(i + 1),
                t.name,
                t.dtype,
                t.shape,
                std::to_wstring(t.paramCount),
                PathUtils::FormatByteSize(t.byteSize),
                std::to_wstring(t.dataOffset)
            });
        }
        FinalizeSheetCols(metaSheet);
        FinalizeSheetCols(tensorSheet);

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"GGUF Quantized Model v" + std::to_wstring(m.formatVersion) + L" (.gguf)";
        out.sheets.push_back(std::move(metaSheet));
        if (tensorSheet.rows.size() > 1) out.sheets.push_back(std::move(tensorSheet));
        out.valid = true;
        return true;
}

} // namespace Explorer2::AiModels::Detail
