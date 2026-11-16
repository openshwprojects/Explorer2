#pragma once
// AiModelFormats.h — Unified streaming SafeTensors (.safetensors), GGUF (.gguf), and ONNX (.onnx)
// inspector shared by both FileContentPane (AI Model card view) and SpreadsheetDocViewer (Sheet/Doc tables).
// Streams directly from disk (with O(1) SetFilePointerEx skipping over multi-GB tensor payloads) or memory.

#include "../PathUtils.h"
#include "../SpreadsheetDocViewer.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Explorer2::AiModels {

class AiModelStream {
public:
    AiModelStream(HANDLE hFile, uint64_t fileSize)
        : m_hFile(hFile), m_fileSize(fileSize), m_buf(65536) {}

    AiModelStream(const uint8_t* memData, size_t memSize)
        : m_memData(memData), m_fileSize(memSize) {}

    uint64_t FileSize() const { return m_fileSize; }

    uint64_t Pos() const {
        return m_memData ? m_memPos : (m_bufStart + m_bufPos);
    }

    uint64_t Remaining() const {
        const uint64_t p = Pos();
        return (p < m_fileSize) ? (m_fileSize - p) : 0;
    }

    bool Seek(uint64_t target) {
        if (target > m_fileSize) return false;
        if (m_memData) {
            m_memPos = target;
            return true;
        }
        if (target >= m_bufStart && target <= m_bufStart + m_bufLen) {
            m_bufPos = static_cast<size_t>(target - m_bufStart);
            return true;
        }
        LARGE_INTEGER li{};
        li.QuadPart = static_cast<LONGLONG>(target);
        if (!SetFilePointerEx(m_hFile, li, nullptr, FILE_BEGIN)) return false;
        m_bufStart = target;
        m_bufLen = 0;
        m_bufPos = 0;
        return true;
    }

    bool Skip(uint64_t n) {
        if (n > Remaining()) return false;
        return Seek(Pos() + n);
    }

    bool ReadBytes(void* dst, size_t n) {
        if (n == 0) return true;
        if (n > Remaining()) return false;
        if (m_memData) {
            std::memcpy(dst, m_memData + static_cast<size_t>(m_memPos), n);
            m_memPos += n;
            return true;
        }
        uint8_t* out = static_cast<uint8_t*>(dst);
        size_t done = 0;
        while (done < n) {
            if (m_bufPos < m_bufLen) {
                const size_t avail = m_bufLen - m_bufPos;
                const size_t take = std::min(avail, n - done);
                std::memcpy(out + done, m_buf.data() + m_bufPos, take);
                m_bufPos += take;
                done += take;
            } else {
                const uint64_t curPos = m_bufStart + m_bufLen;
                if (n - done >= m_buf.size()) {
                    const DWORD chunk = static_cast<DWORD>(std::min<size_t>(n - done, 16u * 1024u * 1024u));
                    DWORD got = 0;
                    if (!ReadFile(m_hFile, out + done, chunk, &got, nullptr) || got != chunk) return false;
                    done += got;
                    m_bufStart = curPos + got;
                    m_bufLen = 0;
                    m_bufPos = 0;
                } else {
                    const uint64_t remFile = m_fileSize - curPos;
                    const DWORD toRead = static_cast<DWORD>(std::min<uint64_t>(m_buf.size(), remFile));
                    DWORD got = 0;
                    if (!ReadFile(m_hFile, m_buf.data(), toRead, &got, nullptr) || got == 0) return false;
                    m_bufStart = curPos;
                    m_bufLen = got;
                    m_bufPos = 0;
                }
            }
        }
        return true;
    }

    bool ReadU8(uint8_t& v) {
        if (m_memData) {
            if (m_memPos >= m_fileSize) return false;
            v = m_memData[static_cast<size_t>(m_memPos++)];
            return true;
        }
        if (m_bufPos < m_bufLen) {
            v = m_buf[m_bufPos++];
            return true;
        }
        return ReadBytes(&v, 1);
    }

    bool ReadU16(uint16_t& v) {
        uint8_t b[2];
        if (!ReadBytes(b, 2)) return false;
        v = static_cast<uint16_t>(b[0] | (static_cast<uint16_t>(b[1]) << 8));
        return true;
    }

    bool ReadU32(uint32_t& v) {
        uint8_t b[4];
        if (!ReadBytes(b, 4)) return false;
        v = static_cast<uint32_t>(b[0]) |
            (static_cast<uint32_t>(b[1]) << 8) |
            (static_cast<uint32_t>(b[2]) << 16) |
            (static_cast<uint32_t>(b[3]) << 24);
        return true;
    }

    bool ReadU64(uint64_t& v) {
        uint32_t lo = 0, hi = 0;
        if (!ReadU32(lo) || !ReadU32(hi)) return false;
        v = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
        return true;
    }

    bool ReadF32(float& v) {
        uint32_t u = 0;
        if (!ReadU32(u)) return false;
        std::memcpy(&v, &u, 4);
        return true;
    }

    bool ReadF64(double& v) {
        uint64_t u = 0;
        if (!ReadU64(u)) return false;
        std::memcpy(&v, &u, 8);
        return true;
    }

    bool ReadPbVarint(uint64_t endPos, uint64_t& out) {
        out = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (Pos() >= endPos) return false;
            uint8_t b = 0;
            if (!ReadU8(b)) return false;
            if (shift == 63 && b > 1) return false;
            out |= static_cast<uint64_t>(b & 0x7Fu) << shift;
            if ((b & 0x80u) == 0) return true;
        }
        return false;
    }

    bool ReadPbTag(uint64_t endPos, uint32_t& fieldNum, uint8_t& wireType) {
        uint64_t tag = 0;
        if (!ReadPbVarint(endPos, tag) || tag == 0) return false;
        wireType = static_cast<uint8_t>(tag & 0x07u);
        const uint64_t fn = tag >> 3;
        if (fn == 0 || fn > 0x1FFFFFFFULL) return false;
        if (wireType != 0 && wireType != 1 && wireType != 2 && wireType != 5) return false;
        fieldNum = static_cast<uint32_t>(fn);
        return true;
    }

    bool SkipPbField(uint64_t endPos, uint8_t wireType) {
        switch (wireType) {
        case 0: {
            uint64_t dummy = 0;
            return ReadPbVarint(endPos, dummy);
        }
        case 1:
            return (Pos() + 8 <= endPos) && Skip(8);
        case 2: {
            uint64_t len = 0;
            if (!ReadPbVarint(endPos, len)) return false;
            return (Pos() + len <= endPos) && Skip(len);
        }
        case 5:
            return (Pos() + 4 <= endPos) && Skip(4);
        default:
            return false;
        }
    }

    bool ReadPbStringBounded(uint64_t endPos, std::string& out, size_t maxKeep = 1024) {
        uint64_t len = 0;
        if (!ReadPbVarint(endPos, len) || Pos() + len > endPos) return false;
        const size_t keep = static_cast<size_t>(std::min<uint64_t>(len, maxKeep));
        out.resize(keep);
        if (keep > 0 && !ReadBytes(out.data(), keep)) return false;
        if (len > keep && !Skip(len - keep)) return false;
        return true;
    }

private:
    HANDLE m_hFile = INVALID_HANDLE_VALUE;
    const uint8_t* m_memData = nullptr;
    uint64_t m_memPos = 0;
    uint64_t m_fileSize = 0;
    uint64_t m_bufStart = 0;
    size_t m_bufLen = 0;
    size_t m_bufPos = 0;
    std::vector<uint8_t> m_buf;
};

namespace Detail {

inline uint16_t RdU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint32_t RdU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
inline int32_t RdI32LE(const uint8_t* p) {
    return static_cast<int32_t>(RdU32LE(p));
}
inline int64_t RdI64LE(const uint8_t* p) {
    uint64_t u = static_cast<uint64_t>(RdU32LE(p)) | (static_cast<uint64_t>(RdU32LE(p + 4)) << 32);
    return static_cast<int64_t>(u);
}
inline float RdF32LE(const uint8_t* p) {
    uint32_t u = RdU32LE(p);
    float f = 0.0f;
    std::memcpy(&f, &u, 4);
    return f;
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

inline std::wstring FormatScalarDouble(double v) {
    if (std::isnan(v)) return L"NaN";
    if (std::isinf(v)) return v < 0 ? L"-Inf" : L"Inf";
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%.6g", v);
    return buf;
}

inline void FinalizeSheetCols(SpreadsheetSheet& sh) {
    size_t mc = 0;
    for (const auto& r : sh.rows) mc = std::max(mc, r.size());
    sh.maxCols = mc;
}

inline std::wstring TruncateWithEllipsis(const std::wstring& s, size_t maxLen) {
    return (s.size() > maxLen) ? (s.substr(0, maxLen) + L"...") : s;
}

struct ParsedTensorRow {
    std::wstring name;
    std::wstring dtype;
    std::wstring shape;
    uint64_t paramCount = 0;
    uint64_t byteSize = 0;
    uint64_t dataOffset = 0;
    std::wstring sampleValues;
};

struct ParsedKvEntry {
    std::wstring key;
    std::wstring typeName;
    std::wstring value;
};

struct ParsedModelData {
    bool valid = false;
    uint32_t formatVersion = 0;
    uint64_t headerBytes = 0;
    std::wstring formatName;
    std::wstring arch;
    std::wstring modelTitle;
    std::wstring baseModel;
    std::wstring precisionSummary;
    std::wstring networkDimAlpha;
    std::wstring contextLength;
    std::wstring triggerWords;
    uint64_t tensorCount = 0;
    uint64_t totalParameters = 0;
    uint64_t metadataKeyCount = 0;
    std::vector<ParsedKvEntry> kvEntries;
    std::vector<ParsedTensorRow> tensors;
    // ONNX extra table rows (when requested by Sheet/Doc mode)
    std::vector<std::wstring> onnxOpsets;
    std::vector<std::vector<std::wstring>> onnxSheetRows;
    std::vector<std::vector<std::wstring>> onnxNodeRows;
    std::wstring onnxProducerVersion;
};

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

// ============================================================================
// 3. ONNX (.onnx Protobuf ModelProto)
// ============================================================================
inline const wchar_t* OnnxElemTypeName(uint64_t dt, uint32_t& outElemBytes) {
    outElemBytes = 0;
    switch (dt) {
    case 1:  outElemBytes = 4;  return L"FLOAT32";
    case 2:  outElemBytes = 1;  return L"UINT8";
    case 3:  outElemBytes = 1;  return L"INT8";
    case 4:  outElemBytes = 2;  return L"UINT16";
    case 5:  outElemBytes = 2;  return L"INT16";
    case 6:  outElemBytes = 4;  return L"INT32";
    case 7:  outElemBytes = 8;  return L"INT64";
    case 8:  outElemBytes = 0;  return L"STRING";
    case 9:  outElemBytes = 1;  return L"BOOL";
    case 10: outElemBytes = 2;  return L"FLOAT16";
    case 11: outElemBytes = 8;  return L"DOUBLE";
    case 12: outElemBytes = 4;  return L"UINT32";
    case 13: outElemBytes = 8;  return L"UINT64";
    case 14: outElemBytes = 8;  return L"COMPLEX64";
    case 15: outElemBytes = 16; return L"COMPLEX128";
    case 16: outElemBytes = 2;  return L"BFLOAT16";
    case 17: outElemBytes = 1;  return L"FLOAT8E4M3FN";
    case 18: outElemBytes = 1;  return L"FLOAT8E4M3FNUZ";
    case 19: outElemBytes = 1;  return L"FLOAT8E5M2";
    case 20: outElemBytes = 1;  return L"FLOAT8E5M2FNUZ";
    case 21: outElemBytes = 1;  return L"UINT4";
    case 22: outElemBytes = 1;  return L"INT4";
    default: return L"UNKNOWN";
    }
}

inline bool ParseOnnxValueInfoStream(AiModelStream& st, uint64_t endPos, std::wstring& outName, std::wstring& outDtype, std::wstring& outShape) {
    std::string name;
    uint64_t elemType = 0;
    std::vector<std::wstring> dims;
    while (st.Pos() < endPos) {
        uint32_t fn = 0; uint8_t wt = 0;
        if (!st.ReadPbTag(endPos, fn, wt)) return false;
        if (fn == 1 && wt == 2) {
            if (!st.ReadPbStringBounded(endPos, name, 512)) return false;
        } else if (fn == 2 && wt == 2) { // TypeProto
            uint64_t tLen = 0;
            if (!st.ReadPbVarint(endPos, tLen) || st.Pos() + tLen > endPos) return false;
            const uint64_t tEnd = st.Pos() + tLen;
            while (st.Pos() < tEnd) {
                uint32_t tfn = 0; uint8_t twt = 0;
                if (!st.ReadPbTag(tEnd, tfn, twt)) return false;
                if (tfn == 1 && twt == 2) { // TypeProto.Tensor
                    uint64_t ttLen = 0;
                    if (!st.ReadPbVarint(tEnd, ttLen) || st.Pos() + ttLen > tEnd) return false;
                    const uint64_t ttEnd = st.Pos() + ttLen;
                    while (st.Pos() < ttEnd) {
                        uint32_t xfn = 0; uint8_t xwt = 0;
                        if (!st.ReadPbTag(ttEnd, xfn, xwt)) return false;
                        if (xfn == 1 && xwt == 0) {
                            if (!st.ReadPbVarint(ttEnd, elemType)) return false;
                        } else if (xfn == 2 && xwt == 2) { // TensorShapeProto
                            uint64_t sLen = 0;
                            if (!st.ReadPbVarint(ttEnd, sLen) || st.Pos() + sLen > ttEnd) return false;
                            const uint64_t sEnd = st.Pos() + sLen;
                            while (st.Pos() < sEnd) {
                                uint32_t sfn = 0; uint8_t swt = 0;
                                if (!st.ReadPbTag(sEnd, sfn, swt)) return false;
                                if (sfn == 1 && swt == 2) { // Dimension
                                    uint64_t dLen = 0;
                                    if (!st.ReadPbVarint(sEnd, dLen) || st.Pos() + dLen > sEnd) return false;
                                    const uint64_t dEnd = st.Pos() + dLen;
                                    std::wstring dimTxt = L"?";
                                    while (st.Pos() < dEnd) {
                                        uint32_t dfn = 0; uint8_t dwt = 0;
                                        if (!st.ReadPbTag(dEnd, dfn, dwt)) return false;
                                        if (dfn == 1 && dwt == 0) {
                                            uint64_t dv = 0;
                                            if (!st.ReadPbVarint(dEnd, dv)) return false;
                                            dimTxt = std::to_wstring(static_cast<int64_t>(dv));
                                        } else if (dfn == 2 && dwt == 2) {
                                            std::string dp;
                                            if (!st.ReadPbStringBounded(dEnd, dp, 128)) return false;
                                            dimTxt = PathUtils::Utf8ToWide(dp);
                                        } else if (!st.SkipPbField(dEnd, dwt)) {
                                            return false;
                                        }
                                    }
                                    if (dims.size() < 32) dims.push_back(std::move(dimTxt));
                                } else if (!st.SkipPbField(sEnd, swt)) {
                                    return false;
                                }
                            }
                        } else if (!st.SkipPbField(ttEnd, xwt)) {
                            return false;
                        }
                    }
                } else if (!st.SkipPbField(tEnd, twt)) {
                    return false;
                }
            }
        } else if (!st.SkipPbField(endPos, wt)) {
            return false;
        }
    }
    uint32_t dummyBytes = 0;
    outName = PathUtils::Utf8ToWide(name);
    outDtype = OnnxElemTypeName(elemType, dummyBytes);
    outShape = L"[";
    for (size_t i = 0; i < dims.size(); ++i) {
        if (i > 0) outShape += L", ";
        outShape += dims[i];
    }
    outShape += L"]";
    return true;
}

inline bool ParseOnnxStream(AiModelStream& st, bool captureSheetDetails, ParsedModelData& out) {
    out = {};
    const uint64_t fileSize = st.FileSize();
    if (fileSize < 4) return false;

    uint64_t irVersion = 0;
    uint64_t modelVersion = 0;
    std::string producerName, producerVersion, domain, docString, graphName;
    std::vector<std::pair<std::wstring, std::wstring>> metaProps;
    std::map<std::string, uint64_t> opHist;
    std::map<std::wstring, uint64_t> dtypeHist;
    std::vector<ParsedTensorRow> ioEntries;

    bool hasGraph = false;
    uint64_t nodeCount = 0;
    uint64_t initCount = 0;
    uint64_t ioCount = 0;

    while (st.Pos() < fileSize) {
        uint32_t fn = 0; uint8_t wt = 0;
        if (!st.ReadPbTag(fileSize, fn, wt)) return false;
        if (fn == 1 && wt == 0) {
            if (!st.ReadPbVarint(fileSize, irVersion)) return false;
        } else if (fn == 2 && wt == 2) {
            if (!st.ReadPbStringBounded(fileSize, producerName, 256)) return false;
        } else if (fn == 3 && wt == 2) {
            if (!st.ReadPbStringBounded(fileSize, producerVersion, 128)) return false;
        } else if (fn == 4 && wt == 2) {
            if (!st.ReadPbStringBounded(fileSize, domain, 256)) return false;
        } else if (fn == 5 && wt == 0) {
            if (!st.ReadPbVarint(fileSize, modelVersion)) return false;
        } else if (fn == 6 && wt == 2) {
            if (!st.ReadPbStringBounded(fileSize, docString, 512)) return false;
        } else if (fn == 8 && wt == 2) { // opset_import
            uint64_t oLen = 0;
            if (!st.ReadPbVarint(fileSize, oLen) || st.Pos() + oLen > fileSize) return false;
            const uint64_t oEnd = st.Pos() + oLen;
            std::string opDom;
            uint64_t opVer = 0;
            while (st.Pos() < oEnd) {
                uint32_t ofn = 0; uint8_t owt = 0;
                if (!st.ReadPbTag(oEnd, ofn, owt)) return false;
                if (ofn == 1 && owt == 2) { if (!st.ReadPbStringBounded(oEnd, opDom, 128)) return false; }
                else if (ofn == 2 && owt == 0) { if (!st.ReadPbVarint(oEnd, opVer)) return false; }
                else if (!st.SkipPbField(oEnd, owt)) return false;
            }
            if (opVer == 0 || opVer > 1000) return false;
            out.onnxOpsets.push_back((opDom.empty() ? L"ai.onnx" : PathUtils::Utf8ToWide(opDom)) + L" v" + std::to_wstring(opVer));
        } else if (fn == 14 && wt == 2) { // metadata_props
            uint64_t mLen = 0;
            if (!st.ReadPbVarint(fileSize, mLen) || st.Pos() + mLen > fileSize) return false;
            const uint64_t mEnd = st.Pos() + mLen;
            std::string mk, mv;
            while (st.Pos() < mEnd) {
                uint32_t mfn = 0; uint8_t mwt = 0;
                if (!st.ReadPbTag(mEnd, mfn, mwt)) return false;
                if (mfn == 1 && mwt == 2) { if (!st.ReadPbStringBounded(mEnd, mk, 256)) return false; }
                else if (mfn == 2 && mwt == 2) { if (!st.ReadPbStringBounded(mEnd, mv, 512)) return false; }
                else if (!st.SkipPbField(mEnd, mwt)) return false;
            }
            if (!mk.empty()) {
                std::wstring wmk = PathUtils::Utf8ToWide(mk);
                std::wstring wmv = PathUtils::Utf8ToWide(mv);
                if (metaProps.size() < 128) metaProps.push_back({ wmk, wmv });
                if (captureSheetDetails && out.onnxSheetRows.size() < 300) {
                    out.onnxSheetRows.push_back({ L"Metadata Property", std::move(wmk), L"string", std::move(wmv) });
                }
            }
        } else if (fn == 7 && wt == 2) { // GraphProto
            hasGraph = true;
            uint64_t gLen = 0;
            if (!st.ReadPbVarint(fileSize, gLen) || st.Pos() + gLen > fileSize) return false;
            const uint64_t gEnd = st.Pos() + gLen;
            while (st.Pos() < gEnd) {
                uint32_t gfn = 0; uint8_t gwt = 0;
                if (!st.ReadPbTag(gEnd, gfn, gwt)) return false;
                if (gfn == 2 && gwt == 2) {
                    if (!st.ReadPbStringBounded(gEnd, graphName, 256)) return false;
                } else if (gfn == 1 && gwt == 2) { // NodeProto
                    uint64_t nLen = 0;
                    if (!st.ReadPbVarint(gEnd, nLen) || st.Pos() + nLen > gEnd) return false;
                    const uint64_t nEnd = st.Pos() + nLen;
                    const bool wantNodeRow = captureSheetDetails && (out.onnxNodeRows.size() < 500);
                    std::string nName, opType;
                    std::vector<std::string> ins, outs;
                    while (st.Pos() < nEnd) {
                        uint32_t nfn = 0; uint8_t nwt = 0;
                        if (!st.ReadPbTag(nEnd, nfn, nwt)) return false;
                        if (wantNodeRow && nfn == 1 && nwt == 2) {
                            std::string s;
                            if (!st.ReadPbStringBounded(nEnd, s, 256)) return false;
                            if (ins.size() < 32) ins.push_back(std::move(s));
                        } else if (wantNodeRow && nfn == 2 && nwt == 2) {
                            std::string s;
                            if (!st.ReadPbStringBounded(nEnd, s, 256)) return false;
                            if (outs.size() < 32) outs.push_back(std::move(s));
                        } else if (wantNodeRow && nfn == 3 && nwt == 2) {
                            if (!st.ReadPbStringBounded(nEnd, nName, 256)) return false;
                        } else if (nfn == 4 && nwt == 2) {
                            if (!st.ReadPbStringBounded(nEnd, opType, 128)) return false;
                        } else if (!st.SkipPbField(nEnd, nwt)) {
                            return false;
                        }
                    }
                    ++nodeCount;
                    if (!opType.empty()) opHist[opType]++;
                    if (wantNodeRow) {
                        auto joinVec = [](const std::vector<std::string>& v) {
                            std::wstring w;
                            for (size_t i = 0; i < v.size(); ++i) {
                                if (i > 0) w += L", ";
                                w += PathUtils::Utf8ToWide(v[i]);
                            }
                            return w;
                        };
                        out.onnxNodeRows.push_back({
                            std::to_wstring(nodeCount),
                            PathUtils::Utf8ToWide(opType),
                            PathUtils::Utf8ToWide(nName),
                            joinVec(ins),
                            joinVec(outs)
                        });
                    }
                } else if (gfn == 5 && gwt == 2) { // initializer TensorProto
                    uint64_t tLen = 0;
                    if (!st.ReadPbVarint(gEnd, tLen) || st.Pos() + tLen > gEnd) return false;
                    const uint64_t tEnd = st.Pos() + tLen;
                    std::vector<int64_t> dims;
                    uint64_t dt = 0;
                    std::string tName;
                    uint64_t rawDataBytes = 0;
                    std::vector<float> sampleFloats;
                    uint8_t rawHead[16] = {};
                    size_t rawHeadLen = 0;

                    while (st.Pos() < tEnd) {
                        uint32_t tfn = 0; uint8_t twt = 0;
                        if (!st.ReadPbTag(tEnd, tfn, twt)) return false;
                        if (tfn == 1 && twt == 0) {
                            uint64_t dv = 0;
                            if (!st.ReadPbVarint(tEnd, dv)) return false;
                            if (dims.size() < 32) dims.push_back(static_cast<int64_t>(dv));
                        } else if (tfn == 1 && twt == 2) { // packed repeated int64 dims
                            uint64_t pLen = 0;
                            if (!st.ReadPbVarint(tEnd, pLen) || st.Pos() + pLen > tEnd) return false;
                            const uint64_t pEnd = st.Pos() + pLen;
                            while (st.Pos() < pEnd) {
                                uint64_t dv = 0;
                                if (!st.ReadPbVarint(pEnd, dv)) return false;
                                if (dims.size() < 32) dims.push_back(static_cast<int64_t>(dv));
                            }
                        } else if (tfn == 2 && twt == 0) {
                            if (!st.ReadPbVarint(tEnd, dt)) return false;
                        } else if (captureSheetDetails && tfn == 4 && twt == 5) {
                            float fv = 0.0f;
                            if (st.Pos() + 4 > tEnd || !st.ReadF32(fv)) return false;
                            if (sampleFloats.size() < 4) sampleFloats.push_back(fv);
                        } else if (captureSheetDetails && tfn == 4 && twt == 2) {
                            uint64_t fLen = 0;
                            if (!st.ReadPbVarint(tEnd, fLen) || st.Pos() + fLen > tEnd) return false;
                            const size_t take = static_cast<size_t>(std::min<uint64_t>(fLen, 16));
                            uint8_t fb[16] = {};
                            if (take > 0 && !st.ReadBytes(fb, take)) return false;
                            if (fLen > take && !st.Skip(fLen - take)) return false;
                            for (size_t k = 0; k + 4 <= take && sampleFloats.size() < 4; k += 4) {
                                sampleFloats.push_back(RdF32LE(fb + k));
                            }
                        } else if (tfn == 8 && twt == 2) {
                            if (!st.ReadPbStringBounded(tEnd, tName, 512)) return false;
                        } else if (tfn == 9 && twt == 2) { // raw_data: sample up to 16B, then O(1) seek past multi-GB weights
                            uint64_t rLen = 0;
                            if (!st.ReadPbVarint(tEnd, rLen) || st.Pos() + rLen > tEnd) return false;
                            rawDataBytes = rLen;
                            rawHeadLen = captureSheetDetails ? static_cast<size_t>(std::min<uint64_t>(rLen, 16)) : 0;
                            if (rawHeadLen > 0 && !st.ReadBytes(rawHead, rawHeadLen)) return false;
                            if (rLen > rawHeadLen && !st.Skip(rLen - rawHeadLen)) return false;
                        } else if (!st.SkipPbField(tEnd, twt)) {
                            return false;
                        }
                    }

                    uint32_t elemBytes = 0;
                    const wchar_t* dtName = OnnxElemTypeName(dt, elemBytes);
                    if (dt == 0 || dt > 22) return false;

                    if (captureSheetDetails && dt == 1 && sampleFloats.empty() && rawHeadLen >= 4) {
                        for (size_t k = 0; k + 4 <= rawHeadLen && sampleFloats.size() < 4; k += 4) {
                            sampleFloats.push_back(RdF32LE(rawHead + k));
                        }
                    }

                    uint64_t params = 1;
                    if (!dims.empty()) {
                        for (int64_t d : dims) {
                            if (d <= 0) { params = 0; break; }
                            const uint64_t ud = static_cast<uint64_t>(d);
                            if (params > UINT64_MAX / ud) { params = UINT64_MAX; break; }
                            params *= ud;
                        }
                    }
                    const uint64_t byteSz = (rawDataBytes > 0)
                        ? rawDataBytes
                        : ((elemBytes > 0 && params <= UINT64_MAX / elemBytes) ? (params * elemBytes) : 0ULL);

                    ++initCount;
                    if (out.totalParameters <= UINT64_MAX - params) out.totalParameters += params;
                    else out.totalParameters = UINT64_MAX;
                    dtypeHist[dtName]++;

                    std::wstring shStr = L"[";
                    for (size_t i = 0; i < dims.size(); ++i) {
                        if (i > 0) shStr += L", ";
                        shStr += std::to_wstring(dims[i]);
                    }
                    shStr += L"]";

                    if (captureSheetDetails && out.onnxSheetRows.size() < 300) {
                        std::wstring shWithSample = shStr;
                        if (!sampleFloats.empty()) {
                            shWithSample += L"  e.g. ";
                            for (size_t i = 0; i < sampleFloats.size(); ++i) {
                                if (i > 0) shWithSample += L", ";
                                shWithSample += FormatScalarDouble(sampleFloats[i]);
                            }
                        }
                        out.onnxSheetRows.push_back({ L"Initializer Weight", PathUtils::Utf8ToWide(tName), dtName, std::move(shWithSample) });
                    }

                    if (out.tensors.size() < 512) {
                        ParsedTensorRow tr;
                        tr.name = tName.empty() ? (L"initializer_" + std::to_wstring(initCount)) : PathUtils::Utf8ToWide(tName);
                        tr.dtype = dtName;
                        tr.shape = std::move(shStr);
                        tr.paramCount = params;
                        tr.byteSize = byteSz;
                        out.tensors.push_back(std::move(tr));
                    }
                } else if ((gfn == 11 || gfn == 12) && gwt == 2) { // input / output ValueInfoProto
                    uint64_t vLen = 0;
                    if (!st.ReadPbVarint(gEnd, vLen) || st.Pos() + vLen > gEnd) return false;
                    const uint64_t vEnd = st.Pos() + vLen;
                    std::wstring vName, vDt, vShape;
                    if (!ParseOnnxValueInfoStream(st, vEnd, vName, vDt, vShape)) return false;
                    ++ioCount;
                    if (captureSheetDetails && out.onnxSheetRows.size() < 300) {
                        out.onnxSheetRows.push_back({ gfn == 11 ? L"Graph Input" : L"Graph Output", vName, vDt, vShape });
                    }
                    if (ioEntries.size() < 128) {
                        ParsedTensorRow ioTr;
                        ioTr.name = (gfn == 11 ? L"[in] " : L"[out] ") + vName;
                        ioTr.dtype = std::move(vDt);
                        ioTr.shape = std::move(vShape);
                        ioEntries.push_back(std::move(ioTr));
                    }
                } else if (!st.SkipPbField(gEnd, gwt)) {
                    return false;
                }
            }
        } else if (!st.SkipPbField(fileSize, wt)) {
            return false;
        }
    }

    if (st.Pos() != fileSize) return false;
    if (irVersion < 1 || irVersion > 20) return false;
    if (!hasGraph || (nodeCount == 0 && initCount == 0 && ioCount == 0)) return false;

    if (!ioEntries.empty()) {
        std::vector<ParsedTensorRow> merged;
        merged.reserve(ioEntries.size() + out.tensors.size());
        for (auto& e : ioEntries) merged.push_back(std::move(e));
        for (auto& e : out.tensors) {
            if (merged.size() >= 512) break;
            merged.push_back(std::move(e));
        }
        out.tensors = std::move(merged);
    }

    out.valid = true;
    out.formatVersion = static_cast<uint32_t>(irVersion);
    out.formatName = L"ONNX v" + std::to_wstring(irVersion);
    out.modelTitle = graphName.empty() ? L"" : PathUtils::Utf8ToWide(graphName);
    out.onnxProducerVersion = PathUtils::Utf8ToWide(producerVersion);
    out.arch = producerName.empty()
        ? L""
        : (PathUtils::Utf8ToWide(producerName) + (producerVersion.empty() ? L"" : (L" " + out.onnxProducerVersion)));
    out.baseModel = PathUtils::Utf8ToWide(producerName);
    out.tensorCount = initCount > 0 ? initCount : ioCount;

    for (const auto& kv : dtypeHist) {
        if (!out.precisionSummary.empty()) out.precisionSummary += L", ";
        out.precisionSummary += kv.first + L" (" + std::to_wstring(kv.second) + L")";
    }

    out.kvEntries.push_back({ L"ir_version", L"int64", std::to_wstring(irVersion) });
    if (!producerName.empty()) out.kvEntries.push_back({ L"producer", L"string", out.arch });
    if (!domain.empty()) out.kvEntries.push_back({ L"domain", L"string", PathUtils::Utf8ToWide(domain) });
    if (modelVersion != 0) out.kvEntries.push_back({ L"model_version", L"int64", std::to_wstring(modelVersion) });
    if (!graphName.empty()) out.kvEntries.push_back({ L"graph.name", L"string", out.modelTitle });
    out.kvEntries.push_back({ L"graph.nodes", L"uint64", std::to_wstring(nodeCount) });
    out.kvEntries.push_back({ L"graph.initializers", L"uint64", std::to_wstring(initCount) });
    out.kvEntries.push_back({ L"graph.io_ports", L"uint64", std::to_wstring(ioCount) });
    if (!out.onnxOpsets.empty()) {
        std::wstring opJoined;
        for (size_t i = 0; i < out.onnxOpsets.size(); ++i) {
            if (i > 0) opJoined += L", ";
            opJoined += out.onnxOpsets[i];
        }
        out.kvEntries.push_back({ L"opset_import", L"string", std::move(opJoined) });
    }
    if (!opHist.empty()) {
        std::vector<std::pair<std::string, uint64_t>> sortedOps(opHist.begin(), opHist.end());
        std::sort(sortedOps.begin(), sortedOps.end(), [](const auto& a, const auto& b) {
            return (a.second != b.second) ? (a.second > b.second) : (a.first < b.first);
        });
        std::wstring histStr;
        for (size_t i = 0; i < sortedOps.size() && i < 16; ++i) {
            if (i > 0) histStr += L", ";
            histStr += PathUtils::Utf8ToWide(sortedOps[i].first) + L" (" + std::to_wstring(sortedOps[i].second) + L")";
        }
        out.kvEntries.push_back({ L"operators", L"string", std::move(histStr) });
    }
    if (!docString.empty()) {
        out.kvEntries.push_back({ L"doc_string", L"string", TruncateWithEllipsis(PathUtils::Utf8ToWide(docString), 180) });
    }
    for (auto& mp : metaProps) {
        out.kvEntries.push_back({ std::move(mp.first), L"string", std::move(mp.second) });
    }
    out.metadataKeyCount = out.kvEntries.size();
    return true;
}

inline bool BuildDocumentFromParsedModel(const ParsedModelData& m, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (!m.valid) return false;
    if (extLower == L".safetensors") {
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

    if (extLower == L".gguf") {
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

    if (extLower == L".onnx") {
        if (m.onnxNodeRows.empty() && m.onnxSheetRows.empty()) return false;
        SpreadsheetSheet ioSheet;
        ioSheet.name = L"ONNX_Model_And_Tensors";
        ioSheet.rows.push_back({ L"Role / Property", L"Name", L"Data Type", L"Shape / Value" });
        ioSheet.rows.push_back({ L"IR Version", L"ir_version", L"int64", std::to_wstring(m.formatVersion) });
        if (!m.baseModel.empty()) {
            ioSheet.rows.push_back({ L"Producer", m.baseModel, L"string", m.onnxProducerVersion });
        }
        if (!m.modelTitle.empty()) {
            ioSheet.rows.push_back({ L"Graph Name", m.modelTitle, L"GraphProto", std::to_wstring(m.onnxNodeRows.size()) + L" nodes" });
        }
        for (const auto& ops : m.onnxOpsets) {
            ioSheet.rows.push_back({ L"Opset Import", ops, L"OperatorSetId", L"" });
        }
        for (const auto& r : m.onnxSheetRows) {
            ioSheet.rows.push_back(r);
        }

        SpreadsheetSheet nodeSheet;
        nodeSheet.name = L"ONNX_Graph_Nodes";
        nodeSheet.rows.push_back({ L"#", L"OpType", L"Node Name", L"Inputs", L"Outputs" });
        for (const auto& nr : m.onnxNodeRows) {
            nodeSheet.rows.push_back(nr);
        }
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

    return false;
}

struct ScopedFileHandle {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~ScopedFileHandle() {
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }
    ScopedFileHandle() = default;
    ScopedFileHandle(const ScopedFileHandle&) = delete;
    ScopedFileHandle& operator=(const ScopedFileHandle&) = delete;
};

inline bool OpenAiModelReadHandle(const std::wstring& path, ScopedFileHandle& outHandle, uint64_t& outSize) {
    outHandle.h = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (outHandle.h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER li{};
    if (!GetFileSizeEx(outHandle.h, &li) || li.QuadPart <= 0) {
        return false;
    }
    outSize = static_cast<uint64_t>(li.QuadPart);
    return true;
}

} // namespace Detail

inline bool IsSupportedAiModelExt(const std::wstring& ext) {
    const std::wstring extLower = PathUtils::ToLower(ext);
    return extLower == L".safetensors" || extLower == L".gguf" || extLower == L".onnx";
}

template <typename SummaryT>
inline SummaryT InspectAiModelFileToSummary(const std::wstring& path, const std::wstring& ext) {
    SummaryT info{};
    Detail::ScopedFileHandle fh;
    uint64_t fileSize = 0;
    if (!Detail::OpenAiModelReadHandle(path, fh, fileSize)) return info;

    AiModelStream stream(fh.h, fileSize);
    const std::wstring extLower = PathUtils::ToLower(ext.empty() ? PathUtils::GetExtension(path) : ext);
    Detail::ParsedModelData m;
    bool ok = false;
    if (extLower == L".safetensors") ok = Detail::ParseSafeTensorsStream(stream, false, m);
    else if (extLower == L".gguf")   ok = Detail::ParseGgufStream(stream, m);
    else if (extLower == L".onnx")   ok = Detail::ParseOnnxStream(stream, false, m);
    if (!ok || !m.valid) return info;

    info.valid = true;
    info.formatName = std::move(m.formatName);
    info.arch = std::move(m.arch);
    info.modelTitle = std::move(m.modelTitle);
    info.baseModel = (extLower == L".onnx") ? std::wstring{} : std::move(m.baseModel);
    info.precisionSummary = std::move(m.precisionSummary);
    info.networkDimAlpha = std::move(m.networkDimAlpha);
    info.contextLength = std::move(m.contextLength);
    info.triggerWords = std::move(m.triggerWords);
    info.tensorCount = m.tensorCount;
    info.totalParameters = m.totalParameters;
    info.metadataKeyCount = m.metadataKeyCount;

    const size_t maxKv = 256;
    const size_t truncLen = (extLower == L".safetensors") ? 220 : 160;
    for (size_t i = 0; i < m.kvEntries.size() && i < maxKv; ++i) {
        info.kvRows.push_back({
            m.kvEntries[i].key,
            Detail::TruncateWithEllipsis(m.kvEntries[i].value, truncLen)
        });
    }

    const size_t maxTensors = (extLower == L".safetensors") ? 1024 : 512;
    for (size_t i = 0; i < m.tensors.size() && i < maxTensors; ++i) {
        typename SummaryT::TensorEntry te;
        te.name = std::move(m.tensors[i].name);
        te.dtype = std::move(m.tensors[i].dtype);
        te.shape = std::move(m.tensors[i].shape);
        te.paramCount = m.tensors[i].paramCount;
        te.byteSize = m.tensors[i].byteSize;
        info.tensors.push_back(std::move(te));
    }
    return info;
}

inline bool ParseAiModelStreamToDocument(AiModelStream& stream, const std::wstring& extLower, ParsedOfficeDocument& out) {
    Detail::ParsedModelData m;
    bool ok = false;
    if (extLower == L".safetensors") ok = Detail::ParseSafeTensorsStream(stream, true, m);
    else if (extLower == L".gguf")   ok = Detail::ParseGgufStream(stream, m);
    else if (extLower == L".onnx")   ok = Detail::ParseOnnxStream(stream, true, m);
    if (!ok || !m.valid) return false;
    return Detail::BuildDocumentFromParsedModel(m, extLower, out);
}

inline bool ParseAiModelFileToDocument(const std::wstring& fullPath, const std::wstring& extLower, ParsedOfficeDocument& out) {
    Detail::ScopedFileHandle fh;
    uint64_t fileSize = 0;
    if (!Detail::OpenAiModelReadHandle(fullPath, fh, fileSize)) return false;
    AiModelStream stream(fh.h, fileSize);
    return ParseAiModelStreamToDocument(stream, PathUtils::ToLower(extLower), out);
}

inline bool ParseAiModelMemoryToDocument(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (!data || size == 0) return false;
    AiModelStream stream(data, size);
    return ParseAiModelStreamToDocument(stream, extLower, out);
}

} // namespace Explorer2::AiModels
