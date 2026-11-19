#pragma once
// SafeTensorsModel.h — HuggingFace SafeTensors (.safetensors) streaming model reader.
#include "../common/AiModelCommon.h"

namespace Explorer2::AiModels::Detail {

// ============================================================================
// 1. SafeTensors (.safetensors)
// ============================================================================
struct StJsonCursor {
    const char* s = nullptr;
    size_t n = 0;
    size_t pos = 0;

    void SkipWs() {
        while (pos < n) {
            const char c = s[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++pos;
            else break;
        }
    }

    static void AppendUtf8Codepoint(std::string& out, uint32_t cp) {
        if (cp <= 0x7Fu) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FFu) {
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

    static bool ParseHex4(const char* p, uint32_t& out) {
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = p[i];
            uint32_t v = 0;
            if (c >= '0' && c <= '9') v = static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v = static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v = static_cast<uint32_t>(c - 'A' + 10);
            else return false;
            out = (out << 4) | v;
        }
        return true;
    }

    bool ParseString(std::string& out) {
        SkipWs();
        if (pos >= n || s[pos] != '"') return false;
        ++pos;
        out.clear();
        while (pos < n) {
            const unsigned char c = static_cast<unsigned char>(s[pos++]);
            if (c == '"') return true;
            if (c < 0x20u) return false;
            if (c == '\\') {
                if (pos >= n) return false;
                const char esc = s[pos++];
                switch (esc) {
                case '"':  out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/'); break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    if (pos + 4 > n) return false;
                    uint32_t u1 = 0;
                    if (!ParseHex4(s + pos, u1)) return false;
                    pos += 4;
                    if (u1 >= 0xD800u && u1 <= 0xDBFFu && pos + 6 <= n && s[pos] == '\\' && s[pos + 1] == 'u') {
                        uint32_t u2 = 0;
                        if (ParseHex4(s + pos + 2, u2) && u2 >= 0xDC00u && u2 <= 0xDFFFu) {
                            pos += 6;
                            const uint32_t cp = 0x10000u + (((u1 - 0xD800u) << 10) | (u2 - 0xDC00u));
                            AppendUtf8Codepoint(out, cp);
                            break;
                        }
                    }
                    AppendUtf8Codepoint(out, u1);
                    break;
                }
                default:
                    return false;
                }
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
        return false;
    }

    bool ParseUint64(uint64_t& out) {
        SkipWs();
        if (pos >= n || s[pos] < '0' || s[pos] > '9') return false;
        if (s[pos] == '0' && pos + 1 < n && s[pos + 1] >= '0' && s[pos + 1] <= '9') return false;
        out = 0;
        while (pos < n && s[pos] >= '0' && s[pos] <= '9') {
            const uint64_t d = static_cast<uint64_t>(s[pos] - '0');
            if (out > (UINT64_MAX - d) / 10ULL) return false;
            out = out * 10ULL + d;
            ++pos;
        }
        if (pos < n && (s[pos] == '.' || s[pos] == 'e' || s[pos] == 'E')) return false;
        return true;
    }

    bool SkipNumber() {
        if (pos < n && s[pos] == '-') ++pos;
        if (pos >= n || s[pos] < '0' || s[pos] > '9') return false;
        if (s[pos] == '0') {
            ++pos;
            if (pos < n && s[pos] >= '0' && s[pos] <= '9') return false;
        } else {
            while (pos < n && s[pos] >= '0' && s[pos] <= '9') ++pos;
        }
        if (pos < n && s[pos] == '.') {
            ++pos;
            if (pos >= n || s[pos] < '0' || s[pos] > '9') return false;
            while (pos < n && s[pos] >= '0' && s[pos] <= '9') ++pos;
        }
        if (pos < n && (s[pos] == 'e' || s[pos] == 'E')) {
            ++pos;
            if (pos < n && (s[pos] == '+' || s[pos] == '-')) ++pos;
            if (pos >= n || s[pos] < '0' || s[pos] > '9') return false;
            while (pos < n && s[pos] >= '0' && s[pos] <= '9') ++pos;
        }
        return true;
    }

    bool SkipValue(int depth = 0) {
        if (depth > 32) return false;
        SkipWs();
        if (pos >= n) return false;
        const char c = s[pos];
        if (c == '"') {
            std::string dummy;
            return ParseString(dummy);
        }
        if (c == '{') {
            ++pos;
            SkipWs();
            if (pos < n && s[pos] == '}') { ++pos; return true; }
            for (;;) {
                std::string k;
                if (!ParseString(k)) return false;
                SkipWs();
                if (pos >= n || s[pos] != ':') return false;
                ++pos;
                if (!SkipValue(depth + 1)) return false;
                SkipWs();
                if (pos < n && s[pos] == ',') { ++pos; continue; }
                if (pos < n && s[pos] == '}') { ++pos; return true; }
                return false;
            }
        }
        if (c == '[') {
            ++pos;
            SkipWs();
            if (pos < n && s[pos] == ']') { ++pos; return true; }
            for (;;) {
                if (!SkipValue(depth + 1)) return false;
                SkipWs();
                if (pos < n && s[pos] == ',') { ++pos; continue; }
                if (pos < n && s[pos] == ']') { ++pos; return true; }
                return false;
            }
        }
        if (c == 't') {
            if (pos + 4 <= n && std::memcmp(s + pos, "true", 4) == 0) { pos += 4; return true; }
            return false;
        }
        if (c == 'f') {
            if (pos + 5 <= n && std::memcmp(s + pos, "false", 5) == 0) { pos += 5; return true; }
            return false;
        }
        if (c == 'n') {
            if (pos + 4 <= n && std::memcmp(s + pos, "null", 4) == 0) { pos += 4; return true; }
            return false;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return SkipNumber();
        }
        return false;
    }

    bool ParseMetadataValueAsString(std::string& out) {
        SkipWs();
        if (pos >= n) return false;
        if (s[pos] == '"') return ParseString(out);
        const size_t start = pos;
        if (!SkipValue(1)) return false;
        out.assign(s + start, pos - start);
        return true;
    }
};

inline bool ParseSafeTensorsStream(AiModelStream& st, bool captureSamples, ParsedModelData& out) {
    out = {};
    const uint64_t fileSize = st.FileSize();
    if (fileSize < 10) return false;
    uint64_t hdrLen = 0;
    if (!st.ReadU64(hdrLen)) return false;
    if (hdrLen < 2 || hdrLen > (64ULL * 1024ULL * 1024ULL)) return false;
    if (8ULL + hdrLen > fileSize) return false;

    std::string json(static_cast<size_t>(hdrLen), '\0');
    if (!st.ReadBytes(json.data(), static_cast<size_t>(hdrLen))) return false;

    size_t firstNonWs = 0;
    while (firstNonWs < json.size() && (json[firstNonWs] == ' ' || json[firstNonWs] == '\t' || json[firstNonWs] == '\r' || json[firstNonWs] == '\n')) {
        ++firstNonWs;
    }
    size_t lastNonWs = json.size();
    while (lastNonWs > firstNonWs && (json[lastNonWs - 1] == ' ' || json[lastNonWs - 1] == '\t' || json[lastNonWs - 1] == '\r' || json[lastNonWs - 1] == '\n')) {
        --lastNonWs;
    }
    if (lastNonWs - firstNonWs < 2 || json[firstNonWs] != '{' || json[lastNonWs - 1] != '}') return false;

    StJsonCursor cur{ json.data(), json.size(), 0 };
    cur.SkipWs();
    if (cur.pos >= cur.n || cur.s[cur.pos] != '{') return false;
    ++cur.pos;
    cur.SkipWs();

    std::map<std::string, uint64_t> dtypeCounts;
    std::map<std::string, std::wstring> metaMap;
    std::vector<std::string> keptDtypesAscii;
    out.headerBytes = hdrLen;

    if (cur.pos < cur.n && cur.s[cur.pos] == '}') {
        ++cur.pos;
    } else {
        for (;;) {
            std::string topKey;
            if (!cur.ParseString(topKey)) return false;
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
                        if (!cur.ParseString(mk)) return false;
                        cur.SkipWs();
                        if (cur.pos >= cur.n || cur.s[cur.pos] != ':') return false;
                        ++cur.pos;
                        if (!cur.ParseMetadataValueAsString(mv)) return false;
                        ++out.metadataKeyCount;
                        std::wstring wv = PathUtils::Utf8ToWide(mv);
                        metaMap[mk] = wv;
                        if (out.kvEntries.size() < 512) {
                            out.kvEntries.push_back({ PathUtils::Utf8ToWide(mk), L"string", std::move(wv) });
                        }
                        cur.SkipWs();
                        if (cur.pos < cur.n && cur.s[cur.pos] == ',') { ++cur.pos; continue; }
                        if (cur.pos < cur.n && cur.s[cur.pos] == '}') { ++cur.pos; break; }
                        return false;
                    }
                }
            } else {
                if (topKey.empty()) return false;
                bool hasDtype = false, hasShape = false, hasOffsets = false;
                std::string dtype;
                std::vector<uint64_t> shape;
                uint64_t offBegin = 0, offEnd = 0;

                if (cur.pos < cur.n && cur.s[cur.pos] == '}') return false;
                for (;;) {
                    std::string fk;
                    if (!cur.ParseString(fk)) return false;
                    cur.SkipWs();
                    if (cur.pos >= cur.n || cur.s[cur.pos] != ':') return false;
                    ++cur.pos;
                    cur.SkipWs();
                    if (fk == "dtype") {
                        if (!cur.ParseString(dtype) || dtype.empty() || dtype.size() > 32) return false;
                        hasDtype = true;
                    } else if (fk == "shape") {
                        if (cur.pos >= cur.n || cur.s[cur.pos] != '[') return false;
                        ++cur.pos;
                        cur.SkipWs();
                        shape.clear();
                        if (cur.pos < cur.n && cur.s[cur.pos] == ']') {
                            ++cur.pos;
                        } else {
                            for (;;) {
                                uint64_t dim = 0;
                                if (!cur.ParseUint64(dim) || shape.size() >= 32) return false;
                                shape.push_back(dim);
                                cur.SkipWs();
                                if (cur.pos < cur.n && cur.s[cur.pos] == ',') { ++cur.pos; continue; }
                                if (cur.pos < cur.n && cur.s[cur.pos] == ']') { ++cur.pos; break; }
                                return false;
                            }
                        }
                        hasShape = true;
                    } else if (fk == "data_offsets") {
                        if (cur.pos >= cur.n || cur.s[cur.pos] != '[') return false;
                        ++cur.pos;
                        if (!cur.ParseUint64(offBegin)) return false;
                        cur.SkipWs();
                        if (cur.pos >= cur.n || cur.s[cur.pos] != ',') return false;
                        ++cur.pos;
                        if (!cur.ParseUint64(offEnd)) return false;
                        cur.SkipWs();
                        if (cur.pos >= cur.n || cur.s[cur.pos] != ']') return false;
                        ++cur.pos;
                        hasOffsets = true;
                    } else {
                        if (!cur.SkipValue(2)) return false;
                    }
                    cur.SkipWs();
                    if (cur.pos < cur.n && cur.s[cur.pos] == ',') { ++cur.pos; continue; }
                    if (cur.pos < cur.n && cur.s[cur.pos] == '}') { ++cur.pos; break; }
                    return false;
                }

                if (!hasDtype || !hasShape || !hasOffsets) return false;
                if (offBegin > offEnd) return false;
                if (offEnd > fileSize - (8ULL + hdrLen)) return false;

                uint64_t params = 1;
                if (shape.empty()) {
                    params = (offEnd > offBegin) ? 1ULL : 0ULL;
                } else {
                    for (uint64_t d : shape) {
                        if (d == 0) { params = 0; break; }
                        if (params > UINT64_MAX / d) { params = UINT64_MAX; break; }
                        params *= d;
                    }
                }

                ++out.tensorCount;
                if (out.totalParameters <= UINT64_MAX - params) out.totalParameters += params;
                else out.totalParameters = UINT64_MAX;
                dtypeCounts[dtype]++;

                if (out.tensors.size() < 1024) {
                    ParsedTensorRow tr;
                    tr.name = PathUtils::Utf8ToWide(topKey);
                    tr.dtype = PathUtils::Utf8ToWide(dtype);
                    tr.shape = L"[";
                    for (size_t i = 0; i < shape.size(); ++i) {
                        if (i > 0) tr.shape += L", ";
                        tr.shape += std::to_wstring(shape[i]);
                    }
                    tr.shape += L"]";
                    tr.paramCount = params;
                    tr.byteSize = offEnd - offBegin;
                    tr.dataOffset = offBegin;
                    out.tensors.push_back(std::move(tr));
                    if (captureSamples) keptDtypesAscii.push_back(dtype);
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

    if (captureSamples) {
        const uint64_t dataBase = 8ULL + hdrLen;
        uint8_t sampleBuf[32] = {};
        for (size_t i = 0; i < out.tensors.size() && i < keptDtypesAscii.size(); ++i) {
            auto& tr = out.tensors[i];
            const std::string& dt = keptDtypesAscii[i];
            const size_t wantBytes = static_cast<size_t>(std::min<uint64_t>(tr.byteSize, 32));
            if (wantBytes < 2) continue;
            if (!st.Seek(dataBase + tr.dataOffset) || !st.ReadBytes(sampleBuf, wantBytes)) continue;
            std::wstring sampleStr;
            if (dt == "F32" && wantBytes >= 4) {
                for (size_t k = 0; k + 4 <= wantBytes && k < 16; k += 4) {
                    if (!sampleStr.empty()) sampleStr += L", ";
                    sampleStr += FormatScalarDouble(RdF32LE(sampleBuf + k));
                }
            } else if (dt == "F16") {
                for (size_t k = 0; k + 2 <= wantBytes && k < 8; k += 2) {
                    if (!sampleStr.empty()) sampleStr += L", ";
                    sampleStr += FormatScalarDouble(HalfToFloat(RdU16LE(sampleBuf + k)));
                }
            } else if (dt == "BF16") {
                for (size_t k = 0; k + 2 <= wantBytes && k < 8; k += 2) {
                    if (!sampleStr.empty()) sampleStr += L", ";
                    sampleStr += FormatScalarDouble(BFloat16ToFloat(RdU16LE(sampleBuf + k)));
                }
            } else if (dt == "I32" && wantBytes >= 4) {
                for (size_t k = 0; k + 4 <= wantBytes && k < 16; k += 4) {
                    if (!sampleStr.empty()) sampleStr += L", ";
                    sampleStr += std::to_wstring(RdI32LE(sampleBuf + k));
                }
            } else if (dt == "I64" && wantBytes >= 8) {
                for (size_t k = 0; k + 8 <= wantBytes && k < 32; k += 8) {
                    if (!sampleStr.empty()) sampleStr += L", ";
                    sampleStr += std::to_wstring(RdI64LE(sampleBuf + k));
                }
            }
            tr.sampleValues = std::move(sampleStr);
        }
    }

    out.valid = true;
    out.formatName = L"SafeTensors";

    for (const auto& kv : dtypeCounts) {
        if (!out.precisionSummary.empty()) out.precisionSummary += L", ";
        out.precisionSummary += PathUtils::Utf8ToWide(kv.first) + L" (" + std::to_wstring(kv.second) + L")";
    }

    auto getMeta = [&](const char* k) -> std::wstring {
        auto it = metaMap.find(k);
        return (it != metaMap.end()) ? it->second : L"";
    };

    out.modelTitle = getMeta("modelspec.title");
    if (out.modelTitle.empty()) out.modelTitle = getMeta("ss_output_name");
    if (out.modelTitle.empty()) out.modelTitle = getMeta("name");

    out.arch = getMeta("modelspec.architecture");
    if (out.arch.empty()) out.arch = getMeta("ss_network_module");
    if (out.arch.empty()) out.arch = getMeta("architecture");

    out.baseModel = getMeta("modelspec.base_model");
    if (out.baseModel.empty()) out.baseModel = getMeta("ss_base_model_version");
    if (out.baseModel.empty()) out.baseModel = getMeta("ss_sd_model_name");

    const std::wstring dim = getMeta("ss_network_dim");
    const std::wstring alpha = getMeta("ss_network_alpha");
    if (!dim.empty()) {
        out.networkDimAlpha = L"Rank/Dim: " + dim + (alpha.empty() ? L"" : (L"  /  Alpha: " + alpha));
    }
    out.triggerWords = getMeta("modelspec.trigger_phrase");
    return true;
}


inline bool BuildSafeTensorsDocument(const ParsedModelData& m, ParsedOfficeDocument& out) {
    if (!m.valid) return false;
        SpreadsheetSheet metaSheet;
        metaSheet.name = L"SafeTensors_Metadata";
        metaSheet.rows.push_back({ L"Key", L"Value" });
        metaSheet.rows.push_back({ L"header_bytes", std::to_wstring(m.headerBytes) });
        for (const auto& kv : m.kvEntries) {
            metaSheet.rows.push_back({ kv.key, kv.value });
        }
        metaSheet.rows.push_back({ L"tensor_count", std::to_wstring(m.tensorCount) });
        metaSheet.rows.push_back({ L"total_parameters", std::to_wstring(m.totalParameters) });

        SpreadsheetSheet tensorSheet;
        tensorSheet.name = L"SafeTensors_Tensors";
        tensorSheet.rows.push_back({ L"#", L"Tensor Name", L"DType", L"Shape", L"Parameters", L"Byte Size", L"Sample Values" });
        for (size_t i = 0; i < m.tensors.size(); ++i) {
            const auto& t = m.tensors[i];
            tensorSheet.rows.push_back({
                std::to_wstring(i + 1),
                t.name,
                t.dtype,
                t.shape,
                std::to_wstring(t.paramCount),
                PathUtils::FormatByteSize(t.byteSize),
                t.sampleValues
            });
        }
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

} // namespace Explorer2::AiModels::Detail
