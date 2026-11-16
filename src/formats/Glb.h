#pragma once
// Glb.h — Spec-compliant glTF 2.0 (.glb, .gltf, .vrm) JSON + Binary Scene-Graph 3D Mesh Parser

namespace Formats {

namespace DetailGltf {

inline uint32_t RdU32LE(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

inline uint16_t RdU16LE(const uint8_t* p) {
    uint16_t v = 0;
    std::memcpy(&v, p, 2);
    return v;
}

inline int16_t RdI16LE(const uint8_t* p) {
    int16_t v = 0;
    std::memcpy(&v, p, 2);
    return v;
}

inline float RdF32LE(const uint8_t* p) {
    float v = 0.0f;
    std::memcpy(&v, p, 4);
    return v;
}

struct JsonVal {
    enum class Kind : uint8_t { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolVal = false;
    double numVal = 0.0;
    std::string strVal;
    std::vector<JsonVal> arrVal;
    std::vector<std::pair<std::string, JsonVal>> objVal;

    bool IsNull()   const { return kind == Kind::Null; }
    bool IsBool()   const { return kind == Kind::Bool; }
    bool IsNumber() const { return kind == Kind::Number; }
    bool IsString() const { return kind == Kind::String; }
    bool IsArray()  const { return kind == Kind::Array; }
    bool IsObject() const { return kind == Kind::Object; }

    const JsonVal* Find(std::string_view key) const {
        if (kind != Kind::Object) return nullptr;
        for (const auto& kv : objVal) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }

    int GetInt(std::string_view key, int defVal) const {
        const JsonVal* v = Find(key);
        if (!v || !v->IsNumber() || !std::isfinite(v->numVal)) return defVal;
        if (v->numVal < -2147483648.0 || v->numVal > 2147483647.0) return defVal;
        return static_cast<int>(std::llround(v->numVal));
    }

    int64_t GetInt64(std::string_view key, int64_t defVal) const {
        const JsonVal* v = Find(key);
        if (!v || !v->IsNumber() || !std::isfinite(v->numVal)) return defVal;
        if (v->numVal < -9.0e15 || v->numVal > 9.0e15) return defVal;
        return static_cast<int64_t>(std::llround(v->numVal));
    }

    bool GetBool(std::string_view key, bool defVal) const {
        const JsonVal* v = Find(key);
        if (!v || !v->IsBool()) return defVal;
        return v->boolVal;
    }

    std::string GetString(std::string_view key, std::string_view defVal = {}) const {
        const JsonVal* v = Find(key);
        if (!v || !v->IsString()) return std::string(defVal);
        return v->strVal;
    }
};

struct JsonParser {
    const char* cur = nullptr;
    const char* end = nullptr;

    void SkipWs() {
        while (cur < end) {
            unsigned char c = static_cast<unsigned char>(*cur);
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++cur;
            } else {
                break;
            }
        }
    }

    static int HexDigit(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool ReadHex4(uint32_t& out) {
        if (end - cur < 4) return false;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            int h = HexDigit(cur[i]);
            if (h < 0) return false;
            v = (v << 4) | static_cast<uint32_t>(h);
        }
        cur += 4;
        out = v;
        return true;
    }

    static void AppendUtf8(std::string& s, uint32_t cp) {
        if (cp <= 0x7Fu) {
            s.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FFu) {
            s.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            s.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0xFFFFu) {
            s.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            s.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            s.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0x10FFFFu) {
            s.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            s.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            s.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            s.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }

    bool ParseString(std::string& out) {
        if (cur >= end || *cur != '"') return false;
        ++cur;
        out.clear();
        while (cur < end) {
            char c = *cur++;
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20u) return false;
            if (c == '\\') {
                if (cur >= end) return false;
                char esc = *cur++;
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
                        uint32_t cp = 0;
                        if (!ReadHex4(cp)) return false;
                        if (cp >= 0xD800u && cp <= 0xDBFFu) {
                            if (end - cur >= 6 && cur[0] == '\\' && cur[1] == 'u') {
                                const char* save = cur;
                                cur += 2;
                                uint32_t lo = 0;
                                if (ReadHex4(lo) && lo >= 0xDC00u && lo <= 0xDFFFu) {
                                    cp = 0x10000u + (((cp - 0xD800u) << 10) | (lo - 0xDC00u));
                                } else {
                                    cur = save;
                                }
                            }
                        }
                        AppendUtf8(out, cp);
                        break;
                    }
                    default:
                        return false;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool ParseNumber(double& out) {
        const char* start = cur;
        if (cur < end && *cur == '-') ++cur;
        if (cur >= end) return false;
        if (*cur == '0') {
            ++cur;
        } else if (*cur >= '1' && *cur <= '9') {
            while (cur < end && *cur >= '0' && *cur <= '9') ++cur;
        } else {
            return false;
        }
        if (cur < end && *cur == '.') {
            ++cur;
            if (cur >= end || *cur < '0' || *cur > '9') return false;
            while (cur < end && *cur >= '0' && *cur <= '9') ++cur;
        }
        if (cur < end && (*cur == 'e' || *cur == 'E')) {
            ++cur;
            if (cur < end && (*cur == '+' || *cur == '-')) ++cur;
            if (cur >= end || *cur < '0' || *cur > '9') return false;
            while (cur < end && *cur >= '0' && *cur <= '9') ++cur;
        }
        size_t len = static_cast<size_t>(cur - start);
        if (len == 0 || len > 64) return false;
        char tmp[72];
        std::memcpy(tmp, start, len);
        tmp[len] = '\0';
        char* endPtr = nullptr;
        double val = std::strtod(tmp, &endPtr);
        if (endPtr != tmp + len || !std::isfinite(val)) return false;
        out = val;
        return true;
    }

    bool ParseValue(JsonVal& out, int depth) {
        if (depth > 64) return false;
        SkipWs();
        if (cur >= end) return false;
        char c = *cur;
        if (c == '{') {
            ++cur;
            out = JsonVal{};
            out.kind = JsonVal::Kind::Object;
            SkipWs();
            if (cur < end && *cur == '}') {
                ++cur;
                return true;
            }
            while (cur < end) {
                SkipWs();
                std::string key;
                if (!ParseString(key)) return false;
                SkipWs();
                if (cur >= end || *cur != ':') return false;
                ++cur;
                JsonVal child;
                if (!ParseValue(child, depth + 1)) return false;
                out.objVal.emplace_back(std::move(key), std::move(child));
                SkipWs();
                if (cur >= end) return false;
                if (*cur == ',') {
                    ++cur;
                } else if (*cur == '}') {
                    ++cur;
                    return true;
                } else {
                    return false;
                }
            }
            return false;
        }
        if (c == '[') {
            ++cur;
            out = JsonVal{};
            out.kind = JsonVal::Kind::Array;
            SkipWs();
            if (cur < end && *cur == ']') {
                ++cur;
                return true;
            }
            while (cur < end) {
                JsonVal elem;
                if (!ParseValue(elem, depth + 1)) return false;
                out.arrVal.push_back(std::move(elem));
                SkipWs();
                if (cur >= end) return false;
                if (*cur == ',') {
                    ++cur;
                } else if (*cur == ']') {
                    ++cur;
                    return true;
                } else {
                    return false;
                }
            }
            return false;
        }
        if (c == '"') {
            out = JsonVal{};
            out.kind = JsonVal::Kind::String;
            return ParseString(out.strVal);
        }
        if (c == 't') {
            if (end - cur < 4 || std::memcmp(cur, "true", 4) != 0) return false;
            cur += 4;
            out = JsonVal{};
            out.kind = JsonVal::Kind::Bool;
            out.boolVal = true;
            return true;
        }
        if (c == 'f') {
            if (end - cur < 5 || std::memcmp(cur, "false", 5) != 0) return false;
            cur += 5;
            out = JsonVal{};
            out.kind = JsonVal::Kind::Bool;
            out.boolVal = false;
            return true;
        }
        if (c == 'n') {
            if (end - cur < 4 || std::memcmp(cur, "null", 4) != 0) return false;
            cur += 4;
            out = JsonVal{};
            out.kind = JsonVal::Kind::Null;
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            out = JsonVal{};
            out.kind = JsonVal::Kind::Number;
            return ParseNumber(out.numVal);
        }
        return false;
    }
};

inline bool ParseJsonDocument(const char* data, size_t len, JsonVal& root) {
    if (!data || len == 0) return false;
    // Strip optional UTF-8 BOM
    if (len >= 3 &&
        static_cast<uint8_t>(data[0]) == 0xEFu &&
        static_cast<uint8_t>(data[1]) == 0xBBu &&
        static_cast<uint8_t>(data[2]) == 0xBFu) {
        data += 3;
        len -= 3;
    }
    JsonParser p{ data, data + len };
    if (!p.ParseValue(root, 0) || !root.IsObject()) return false;
    p.SkipWs();
    while (p.cur < p.end && *p.cur == '\0') ++p.cur;
    p.SkipWs();
    return p.cur == p.end;
}

inline bool DecodeBase64DataUri(std::string_view uri, std::vector<uint8_t>& out, size_t maxBytes) {
    if (uri.size() < 5) return false;
    auto eqAsciiCI = [](char a, char b) {
        unsigned char ua = static_cast<unsigned char>(a);
        unsigned char ub = static_cast<unsigned char>(b);
        if (ua >= 'A' && ua <= 'Z') ua = static_cast<unsigned char>(ua + ('a' - 'A'));
        if (ub >= 'A' && ub <= 'Z') ub = static_cast<unsigned char>(ub + ('a' - 'A'));
        return ua == ub;
    };
    const char* prefix = "data:";
    for (size_t i = 0; i < 5; ++i) {
        if (!eqAsciiCI(uri[i], prefix[i])) return false;
    }
    size_t comma = uri.find(',', 5);
    if (comma == std::string_view::npos) return false;
    std::string_view meta = uri.substr(5, comma - 5);
    bool isBase64 = false;
    if (meta.size() >= 6) {
        for (size_t i = 0; i + 6 <= meta.size(); ++i) {
            if (eqAsciiCI(meta[i], 'b') && eqAsciiCI(meta[i + 1], 'a') &&
                eqAsciiCI(meta[i + 2], 's') && eqAsciiCI(meta[i + 3], 'e') &&
                meta[i + 4] == '6' && meta[i + 5] == '4') {
                isBase64 = true;
                break;
            }
        }
    }
    if (!isBase64) return false;

    std::string_view b64 = uri.substr(comma + 1);
    size_t est = (b64.size() * 3) / 4;
    if (est > maxBytes) return false;
    out.clear();
    out.reserve(est);

    uint32_t acc = 0;
    int bits = -8;
    for (char c : b64) {
        if (c == '=') break;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        int d = -1;
        if (c >= 'A' && c <= 'Z') d = c - 'A';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
        else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '+') d = 62;
        else if (c == '/') d = 63;
        else return false;

        acc = (acc << 6) | static_cast<uint32_t>(d);
        bits += 6;
        if (bits >= 0) {
            if (out.size() >= maxBytes) return false;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFFu));
            bits -= 8;
        }
    }
    return true;
}

inline bool ReadSafeRelativeBinFile(
    const std::wstring& modelFullPath,
    std::string_view rawUri,
    std::vector<uint8_t>& outBytes,
    size_t maxBytes)
{
    if (rawUri.empty() || modelFullPath.empty()) return false;

    // Strip query/fragment if any? Reject '?' or '#' or ':' for strict path safety
    if (rawUri.find(':') != std::string_view::npos ||
        rawUri.find('?') != std::string_view::npos ||
        rawUri.find('#') != std::string_view::npos) {
        return false;
    }

    // Percent-decode URI
    std::string decoded;
    decoded.reserve(rawUri.size());
    for (size_t i = 0; i < rawUri.size(); ++i) {
        char c = rawUri[i];
        if (c == '%') {
            if (i + 2 >= rawUri.size()) return false;
            int hi = JsonParser::HexDigit(rawUri[i + 1]);
            int lo = JsonParser::HexDigit(rawUri[i + 2]);
            if (hi < 0 || lo < 0) return false;
            uint8_t b = static_cast<uint8_t>((hi << 4) | lo);
            if (b == 0) return false;
            decoded.push_back(static_cast<char>(b));
            i += 2;
        } else {
            if (static_cast<unsigned char>(c) < 0x20u) return false;
            decoded.push_back(c);
        }
    }

    if (decoded.empty()) return false;
    if (decoded.find(':') != std::string::npos) return false;
    if (decoded[0] == '/' || decoded[0] == '\\') return false;

    // Normalize slashes and reject ".." or empty/invalid traversal segments
    std::string cleanRel;
    size_t pos = 0;
    while (pos < decoded.size()) {
        size_t sep = decoded.find_first_of("/\\", pos);
        if (sep == std::string::npos) sep = decoded.size();
        std::string_view seg(decoded.data() + pos, sep - pos);
        pos = sep + 1;
        if (seg.empty() || seg == ".") continue;
        if (seg == "..") return false;
        // Reject trailing dots/spaces or wildcard characters on Windows
        if (seg.back() == '.' || seg.back() == ' ') return false;
        for (char ch : seg) {
            if (ch == '<' || ch == '>' || ch == '"' || ch == '|' || ch == '*') return false;
        }
        if (!cleanRel.empty()) cleanRel.push_back('\\');
        cleanRel.append(seg.data(), seg.size());
    }
    if (cleanRel.empty()) return false;

    size_t lastSep = modelFullPath.find_last_of(L"\\/");
    if (lastSep == std::wstring::npos) return false;
    std::wstring parentDir = modelFullPath.substr(0, lastSep);

    int wLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        cleanRel.data(), static_cast<int>(cleanRel.size()), nullptr, 0);
    if (wLen <= 0) return false;
    std::wstring relW(static_cast<size_t>(wLen), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        cleanRel.data(), static_cast<int>(cleanRel.size()), relW.data(), wLen);

    std::wstring targetPath = parentDir + L"\\" + relW;
    HANDLE hFile = CreateFileW(targetPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER liSize = {};
    if (!GetFileSizeEx(hFile, &liSize) || liSize.QuadPart < 0 ||
        static_cast<uint64_t>(liSize.QuadPart) > static_cast<uint64_t>(maxBytes)) {
        CloseHandle(hFile);
        return false;
    }

    size_t fSize = static_cast<size_t>(liSize.QuadPart);
    outBytes.resize(fSize);
    DWORD bytesRead = 0;
    bool ok = true;
    if (fSize > 0) {
        if (!ReadFile(hFile, outBytes.data(), static_cast<DWORD>(fSize), &bytesRead, nullptr) ||
            bytesRead != fSize) {
            ok = false;
        }
    }
    CloseHandle(hFile);
    return ok;
}

struct BufferSlice {
    std::vector<uint8_t> owned;
    const uint8_t* ptr = nullptr;
    size_t size = 0;
};

struct BufferViewDesc {
    int buffer = 0;
    size_t byteOffset = 0;
    size_t byteLength = 0;
    size_t byteStride = 0;
    bool valid = false;
};

struct SparseDesc {
    bool present = false;
    uint32_t count = 0;
    int idxBufferView = -1;
    size_t idxByteOffset = 0;
    int idxComponentType = 0;
    int valBufferView = -1;
    size_t valByteOffset = 0;
};

struct AccessorDesc {
    int bufferView = -1;
    size_t byteOffset = 0;
    int componentType = 0;
    bool normalized = false;
    uint32_t count = 0;
    int numComponents = 0; // 1=SCALAR, 2=VEC2, 3=VEC3, 4=VEC4, 9=MAT3, 16=MAT4
    SparseDesc sparse;
    bool valid = false;
};

inline size_t ComponentByteSize(int compType) {
    switch (compType) {
        case 5120: // BYTE
        case 5121: // UBYTE
            return 1;
        case 5122: // SHORT
        case 5123: // USHORT
            return 2;
        case 5125: // UINT
        case 5126: // FLOAT
            return 4;
        default:
            return 0;
    }
}

inline int TypeComponentCount(std::string_view typeStr) {
    if (typeStr == "SCALAR") return 1;
    if (typeStr == "VEC2")   return 2;
    if (typeStr == "VEC3")   return 3;
    if (typeStr == "VEC4" || typeStr == "MAT2") return 4;
    if (typeStr == "MAT3")   return 9;
    if (typeStr == "MAT4")   return 16;
    return 0;
}

inline float DecodeComponentAsFloat(const uint8_t* p, int compType, bool normalized) {
    switch (compType) {
        case 5126: // FLOAT
            return RdF32LE(p);
        case 5120: { // BYTE (int8)
            int8_t v = static_cast<int8_t>(p[0]);
            return normalized ? std::max(-1.0f, static_cast<float>(v) / 127.0f) : static_cast<float>(v);
        }
        case 5121: { // UBYTE (uint8)
            uint8_t v = p[0];
            return normalized ? (static_cast<float>(v) / 255.0f) : static_cast<float>(v);
        }
        case 5122: { // SHORT (int16)
            int16_t v = RdI16LE(p);
            return normalized ? std::max(-1.0f, static_cast<float>(v) / 32767.0f) : static_cast<float>(v);
        }
        case 5123: { // USHORT (uint16)
            uint16_t v = RdU16LE(p);
            return normalized ? (static_cast<float>(v) / 65535.0f) : static_cast<float>(v);
        }
        case 5125: { // UINT (uint32)
            uint32_t v = RdU32LE(p);
            return normalized ? static_cast<float>(static_cast<double>(v) / 4294967295.0) : static_cast<float>(v);
        }
        default:
            return 0.0f;
    }
}

inline bool DecodeComponentAsU32(const uint8_t* p, int compType, uint32_t& out) {
    switch (compType) {
        case 5121: out = p[0]; return true;
        case 5123: out = RdU16LE(p); return true;
        case 5125: out = RdU32LE(p); return true;
        case 5120: {
            int8_t v = static_cast<int8_t>(p[0]);
            if (v < 0) return false;
            out = static_cast<uint32_t>(v);
            return true;
        }
        case 5122: {
            int16_t v = RdI16LE(p);
            if (v < 0) return false;
            out = static_cast<uint32_t>(v);
            return true;
        }
        default:
            return false;
    }
}

inline bool ResolveViewPointer(
    int bvIndex,
    size_t accByteOffset,
    uint32_t elemCount,
    size_t elemSize,
    bool forceTightStride,
    const std::vector<BufferSlice>& buffers,
    const std::vector<BufferViewDesc>& views,
    const uint8_t*& outPtr,
    size_t& outStride)
{
    if (bvIndex < 0 || static_cast<size_t>(bvIndex) >= views.size()) return false;
    const BufferViewDesc& bv = views[static_cast<size_t>(bvIndex)];
    if (!bv.valid || bv.buffer < 0 || static_cast<size_t>(bv.buffer) >= buffers.size()) return false;
    const BufferSlice& buf = buffers[static_cast<size_t>(bv.buffer)];
    if (!buf.ptr || bv.byteOffset > buf.size || bv.byteLength > buf.size - bv.byteOffset) return false;

    size_t stride = (!forceTightStride && bv.byteStride != 0) ? bv.byteStride : elemSize;
    if (stride < elemSize) return false;
    if (elemCount == 0) {
        outPtr = buf.ptr + bv.byteOffset;
        outStride = stride;
        return true;
    }
    if (accByteOffset > bv.byteLength) return false;
    uint64_t needed = static_cast<uint64_t>(elemCount - 1) * static_cast<uint64_t>(stride) + static_cast<uint64_t>(elemSize);
    if (needed > static_cast<uint64_t>(bv.byteLength - accByteOffset)) return false;

    outPtr = buf.ptr + bv.byteOffset + accByteOffset;
    outStride = stride;
    return true;
}

inline bool ReadPositionAccessor(
    const AccessorDesc& acc,
    const std::vector<BufferSlice>& buffers,
    const std::vector<BufferViewDesc>& views,
    std::vector<Vec3f>& outVerts)
{
    if (!acc.valid || acc.numComponents < 3 || acc.count == 0 || acc.count > 4000000u) return false;
    size_t compSz = ComponentByteSize(acc.componentType);
    if (compSz == 0) return false;
    size_t elemSz = static_cast<size_t>(acc.numComponents) * compSz;

    outVerts.assign(acc.count, Vec3f{ 0.0f, 0.0f, 0.0f });

    if (acc.bufferView >= 0) {
        const uint8_t* basePtr = nullptr;
        size_t stride = 0;
        if (!ResolveViewPointer(acc.bufferView, acc.byteOffset, acc.count, elemSz, false,
                                buffers, views, basePtr, stride)) {
            return false;
        }
        for (uint32_t i = 0; i < acc.count; ++i) {
            const uint8_t* elem = basePtr + static_cast<size_t>(i) * stride;
            float x = DecodeComponentAsFloat(elem + 0 * compSz, acc.componentType, acc.normalized);
            float y = DecodeComponentAsFloat(elem + 1 * compSz, acc.componentType, acc.normalized);
            float z = DecodeComponentAsFloat(elem + 2 * compSz, acc.componentType, acc.normalized);
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
            outVerts[i] = { x, y, z };
        }
    } else if (!acc.sparse.present) {
        return false;
    }

    if (acc.sparse.present && acc.sparse.count > 0) {
        if (acc.sparse.count > acc.count) return false;
        size_t idxSz = ComponentByteSize(acc.sparse.idxComponentType);
        if (idxSz == 0) return false;
        const uint8_t* idxPtr = nullptr;
        size_t idxStride = 0;
        const uint8_t* valPtr = nullptr;
        size_t valStride = 0;
        if (!ResolveViewPointer(acc.sparse.idxBufferView, acc.sparse.idxByteOffset, acc.sparse.count, idxSz, true,
                                buffers, views, idxPtr, idxStride)) {
            return false;
        }
        if (!ResolveViewPointer(acc.sparse.valBufferView, acc.sparse.valByteOffset, acc.sparse.count, elemSz, true,
                                buffers, views, valPtr, valStride)) {
            return false;
        }
        for (uint32_t s = 0; s < acc.sparse.count; ++s) {
            uint32_t dstIdx = 0;
            if (!DecodeComponentAsU32(idxPtr + static_cast<size_t>(s) * idxStride,
                                      acc.sparse.idxComponentType, dstIdx)) {
                return false;
            }
            if (dstIdx >= outVerts.size()) return false;
            const uint8_t* elem = valPtr + static_cast<size_t>(s) * valStride;
            float x = DecodeComponentAsFloat(elem + 0 * compSz, acc.componentType, acc.normalized);
            float y = DecodeComponentAsFloat(elem + 1 * compSz, acc.componentType, acc.normalized);
            float z = DecodeComponentAsFloat(elem + 2 * compSz, acc.componentType, acc.normalized);
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
            outVerts[dstIdx] = { x, y, z };
        }
    }
    return true;
}

inline bool ReadIndexAccessor(
    const AccessorDesc& acc,
    const std::vector<BufferSlice>& buffers,
    const std::vector<BufferViewDesc>& views,
    std::vector<uint32_t>& outIndices)
{
    if (!acc.valid || acc.numComponents != 1 || acc.count == 0 || acc.count > 12000000u) return false;
    size_t compSz = ComponentByteSize(acc.componentType);
    if (compSz == 0 || acc.componentType == 5126) return false;

    outIndices.assign(acc.count, 0u);

    if (acc.bufferView >= 0) {
        const uint8_t* basePtr = nullptr;
        size_t stride = 0;
        if (!ResolveViewPointer(acc.bufferView, acc.byteOffset, acc.count, compSz, false,
                                buffers, views, basePtr, stride)) {
            return false;
        }
        for (uint32_t i = 0; i < acc.count; ++i) {
            uint32_t idxVal = 0;
            if (!DecodeComponentAsU32(basePtr + static_cast<size_t>(i) * stride, acc.componentType, idxVal)) {
                return false;
            }
            outIndices[i] = idxVal;
        }
    } else if (!acc.sparse.present) {
        return false;
    }

    if (acc.sparse.present && acc.sparse.count > 0) {
        if (acc.sparse.count > acc.count) return false;
        size_t idxSz = ComponentByteSize(acc.sparse.idxComponentType);
        if (idxSz == 0) return false;
        const uint8_t* idxPtr = nullptr;
        size_t idxStride = 0;
        const uint8_t* valPtr = nullptr;
        size_t valStride = 0;
        if (!ResolveViewPointer(acc.sparse.idxBufferView, acc.sparse.idxByteOffset, acc.sparse.count, idxSz, true,
                                buffers, views, idxPtr, idxStride)) {
            return false;
        }
        if (!ResolveViewPointer(acc.sparse.valBufferView, acc.sparse.valByteOffset, acc.sparse.count, compSz, true,
                                buffers, views, valPtr, valStride)) {
            return false;
        }
        for (uint32_t s = 0; s < acc.sparse.count; ++s) {
            uint32_t dstIdx = 0;
            uint32_t v = 0;
            if (!DecodeComponentAsU32(idxPtr + static_cast<size_t>(s) * idxStride, acc.sparse.idxComponentType, dstIdx) ||
                !DecodeComponentAsU32(valPtr + static_cast<size_t>(s) * valStride, acc.componentType, v)) {
                return false;
            }
            if (dstIdx >= outIndices.size()) return false;
            outIndices[dstIdx] = v;
        }
    }
    return true;
}

// Column-major 4x4 matrix matching glTF 2.0 specification:
// m[col * 4 + row]
struct Mat4Col {
    std::array<float, 16> m = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    static Mat4Col Identity() { return Mat4Col{}; }

    static Mat4Col FromTRS(const float t[3], const float qIn[4], const float s[3]) {
        float x = qIn[0], y = qIn[1], z = qIn[2], w = qIn[3];
        float lenSq = x * x + y * y + z * z + w * w;
        if (lenSq > 1e-20f) {
            float inv = 1.0f / std::sqrt(lenSq);
            x *= inv; y *= inv; z *= inv; w *= inv;
        } else {
            x = 0.0f; y = 0.0f; z = 0.0f; w = 1.0f;
        }

        float xx = x * x, yy = y * y, zz = z * z;
        float xy = x * y, xz = x * z, yz = y * z;
        float wx = w * x, wy = w * y, wz = w * z;

        Mat4Col out;
        // Column 0
        out.m[0]  = (1.0f - 2.0f * (yy + zz)) * s[0];
        out.m[1]  = (2.0f * (xy + wz)) * s[0];
        out.m[2]  = (2.0f * (xz - wy)) * s[0];
        out.m[3]  = 0.0f;
        // Column 1
        out.m[4]  = (2.0f * (xy - wz)) * s[1];
        out.m[5]  = (1.0f - 2.0f * (xx + zz)) * s[1];
        out.m[6]  = (2.0f * (yz + wx)) * s[1];
        out.m[7]  = 0.0f;
        // Column 2
        out.m[8]  = (2.0f * (xz + wy)) * s[2];
        out.m[9]  = (2.0f * (yz - wx)) * s[2];
        out.m[10] = (1.0f - 2.0f * (xx + yy)) * s[2];
        out.m[11] = 0.0f;
        // Column 3
        out.m[12] = t[0];
        out.m[13] = t[1];
        out.m[14] = t[2];
        out.m[15] = 1.0f;
        return out;
    }

    static Mat4Col Mul(const Mat4Col& a, const Mat4Col& b) {
        Mat4Col r;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                r.m[col * 4 + row] =
                    a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                    a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                    a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                    a.m[3 * 4 + row] * b.m[col * 4 + 3];
            }
        }
        return r;
    }

    Vec3f TransformPoint(const Vec3f& v) const {
        return {
            m[0] * v.x + m[4] * v.y + m[8]  * v.z + m[12],
            m[1] * v.x + m[5] * v.y + m[9]  * v.z + m[13],
            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14]
        };
    }
};

struct DecodedPrim {
    std::vector<Vec3f> positions;
    std::vector<std::array<uint32_t, 3>> tris;
};

struct DecodedMesh {
    std::vector<DecodedPrim> prims;
};

struct GltfNode {
    int meshIndex = -1;
    std::vector<int> children;
    Mat4Col localMat;
};

} // namespace DetailGltf

inline bool ParseGltfOrGlbMesh(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    const std::wstring& ext,
    LoadedMesh3D& mesh)
{
    using namespace DetailGltf;
    if (!data || size < 16) return false;

    if (ext == L".vrm") {
        mesh.formatName = L"VRoid / glTF Humanoid Avatar (.vrm)";
    } else if (ext == L".gltf") {
        mesh.formatName = L"JSON glTF 2.0 (.gltf)";
    } else {
        mesh.formatName = L"Binary glTF 2.0 (.glb)";
    }

    const char* jsonPtr = nullptr;
    size_t jsonLen = 0;
    const uint8_t* glbBinPtr = nullptr;
    size_t glbBinLen = 0;
    bool isBinaryContainer = (ext == L".glb" || ext == L".vrm");

    if (isBinaryContainer || (size >= 20 && RdU32LE(data) == 0x46546C67u)) {
        if (size < 20) return false;
        uint32_t magic = RdU32LE(data + 0);
        uint32_t version = RdU32LE(data + 4);
        uint32_t totalLen = RdU32LE(data + 8);
        if (magic != 0x46546C67u || version != 2u) return false;
        if (totalLen < 20u || static_cast<size_t>(totalLen) > size) return false;

        size_t pos = 12;
        bool firstChunk = true;
        while (pos < static_cast<size_t>(totalLen)) {
            if (pos + 8 > static_cast<size_t>(totalLen)) return false;
            uint32_t cLen = RdU32LE(data + pos);
            uint32_t cType = RdU32LE(data + pos + 4);
            if (static_cast<size_t>(cLen) > static_cast<size_t>(totalLen) - (pos + 8)) return false;

            if (firstChunk) {
                // Per glTF 2.0 §4.4, chunk 0 MUST be JSON (0x4E4F534A)
                if (cType != 0x4E4F534Au || cLen == 0) return false;
                jsonPtr = reinterpret_cast<const char*>(data + pos + 8);
                jsonLen = static_cast<size_t>(cLen);
                firstChunk = false;
            } else if (cType == 0x004E4942u && !glbBinPtr) {
                glbBinPtr = data + pos + 8;
                glbBinLen = static_cast<size_t>(cLen);
            } else if (cType == 0x4E4F534Au) {
                // Duplicate JSON chunk is invalid in GLB 2.0
                return false;
            }
            pos += 8 + static_cast<size_t>(cLen);
        }
        if (!jsonPtr || jsonLen == 0) return false;
        isBinaryContainer = true;
    } else {
        jsonPtr = reinterpret_cast<const char*>(data);
        jsonLen = size;
    }

    JsonVal root;
    if (!ParseJsonDocument(jsonPtr, jsonLen, root)) return false;

    // Require glTF 2.0 asset version if "asset" object is present, and require "meshes"
    if (const JsonVal* asset = root.Find("asset")) {
        if (!asset->IsObject()) return false;
        std::string ver = asset->GetString("version");
        if (!ver.empty() && ver[0] != '2') return false;
    }
    const JsonVal* jMeshes = root.Find("meshes");
    if (!jMeshes || !jMeshes->IsArray() || jMeshes->arrVal.empty()) return false;

    // 1. Resolve buffers
    constexpr size_t kMaxBufferBytes = 64ULL * 1024ULL * 1024ULL;
    std::vector<BufferSlice> buffers;
    const JsonVal* jBuffers = root.Find("buffers");
    if (jBuffers && jBuffers->IsArray() && !jBuffers->arrVal.empty()) {
        if (jBuffers->arrVal.size() > 1024) return false;
        buffers.resize(jBuffers->arrVal.size());
        size_t totalOwnedBytes = 0;

        for (size_t bi = 0; bi < jBuffers->arrVal.size(); ++bi) {
            const JsonVal& jb = jBuffers->arrVal[bi];
            if (!jb.IsObject()) return false;
            int64_t byteLen64 = jb.GetInt64("byteLength", -1);
            bool hasByteLen = (byteLen64 >= 0);
            if (hasByteLen && static_cast<uint64_t>(byteLen64) > 256ULL * 1024ULL * 1024ULL) return false;
            size_t reqLen = hasByteLen ? static_cast<size_t>(byteLen64) : 0;

            const JsonVal* jUri = jb.Find("uri");
            bool isMeshoptFallback = false;
            if (const JsonVal* jBufExt = jb.Find("extensions")) {
                if (const JsonVal* jMo = jBufExt->Find("EXT_meshopt_compression")) {
                    isMeshoptFallback = jMo->GetBool("fallback", false);
                } else if (const JsonVal* jKMo = jBufExt->Find("KHR_meshopt_compression")) {
                    isMeshoptFallback = jKMo->GetBool("fallback", false);
                }
            }
            if (isMeshoptFallback && (!jUri || jUri->IsNull())) {
                buffers[bi].ptr = nullptr;
                buffers[bi].size = 0;
            } else if (!jUri || jUri->IsNull()) {
                // Undefined URI refers to the GLB BIN\0 chunk (valid on buffer 0 in GLB/VRM)
                if (!isBinaryContainer || !glbBinPtr) return false;
                if (hasByteLen && reqLen > glbBinLen) return false;
                buffers[bi].ptr = glbBinPtr;
                buffers[bi].size = glbBinLen;
            } else if (jUri->IsString()) {
                const std::string& uri = jUri->strVal;
                if (uri.empty()) return false;
                if (uri.size() >= 5 &&
                    (uri.rfind("data:", 0) == 0 || uri.rfind("DATA:", 0) == 0 ||
                     uri.rfind("Data:", 0) == 0)) {
                    if (!DecodeBase64DataUri(uri, buffers[bi].owned, kMaxBufferBytes)) return false;
                } else {
                    if (!ReadSafeRelativeBinFile(fullPath, uri, buffers[bi].owned, kMaxBufferBytes)) return false;
                }
                if (hasByteLen && buffers[bi].owned.size() < reqLen) return false;
                totalOwnedBytes += buffers[bi].owned.size();
                if (totalOwnedBytes > 128ULL * 1024ULL * 1024ULL) return false;
                buffers[bi].ptr = buffers[bi].owned.data();
                buffers[bi].size = buffers[bi].owned.size();
            } else {
                return false;
            }
        }
    } else if (isBinaryContainer && glbBinPtr && glbBinLen > 0) {
        // Minimal synthetic GLB/VRM with implicit buffer 0
        BufferSlice implicitBin;
        implicitBin.ptr = glbBinPtr;
        implicitBin.size = glbBinLen;
        buffers.push_back(std::move(implicitBin));
    }

    // 2. Resolve bufferViews (including EXT_meshopt_compression / KHR_meshopt_compression)
    std::vector<BufferViewDesc> bufferViews;
    if (const JsonVal* jViews = root.Find("bufferViews")) {
        if (!jViews->IsArray() || jViews->arrVal.size() > 65536) return false;
        bufferViews.resize(jViews->arrVal.size());
        for (size_t vi = 0; vi < jViews->arrVal.size(); ++vi) {
            const JsonVal& jv = jViews->arrVal[vi];
            if (!jv.IsObject()) return false;

            const JsonVal* jMeshopt = nullptr;
            if (const JsonVal* jExt = jv.Find("extensions")) {
                jMeshopt = jExt->Find("EXT_meshopt_compression");
                if (!jMeshopt) jMeshopt = jExt->Find("KHR_meshopt_compression");
            }
            if (jMeshopt && jMeshopt->IsObject()) {
                int moBuf = jMeshopt->GetInt("buffer", -1);
                int64_t moOff = jMeshopt->GetInt64("byteOffset", 0);
                int64_t moLen = jMeshopt->GetInt64("byteLength", -1);
                int64_t moStride = jMeshopt->GetInt64("byteStride", 0);
                int64_t moCount = jMeshopt->GetInt64("count", 0);
                std::string moMode = jMeshopt->GetString("mode");
                std::string moFilter = jMeshopt->GetString("filter", "NONE");

                if (moBuf < 0 || static_cast<size_t>(moBuf) >= buffers.size() ||
                    moOff < 0 || moLen <= 0 || moStride <= 0 || moStride > 256 || moCount <= 0) {
                    return false;
                }
                const BufferSlice& cbs = buffers[static_cast<size_t>(moBuf)];
                if (!cbs.ptr || static_cast<size_t>(moOff) > cbs.size ||
                    static_cast<size_t>(moLen) > cbs.size - static_cast<size_t>(moOff)) {
                    return false;
                }
                BufferSlice decSlice;
                if (!DecodeMeshoptBufferView(
                        cbs.ptr + static_cast<size_t>(moOff),
                        static_cast<size_t>(moLen),
                        static_cast<size_t>(moCount),
                        static_cast<size_t>(moStride),
                        moMode,
                        moFilter,
                        decSlice.owned)) {
                    return false;
                }
                decSlice.ptr = decSlice.owned.data();
                decSlice.size = decSlice.owned.size();
                int newBufIdx = static_cast<int>(buffers.size());
                buffers.push_back(std::move(decSlice));

                int64_t viewStride = jv.GetInt64("byteStride", 0);
                BufferViewDesc vd;
                vd.buffer = newBufIdx;
                vd.byteOffset = 0;
                vd.byteLength = buffers[static_cast<size_t>(newBufIdx)].size;
                vd.byteStride = (viewStride > 0 && viewStride <= 255) ? static_cast<size_t>(viewStride) : 0;
                vd.valid = true;
                bufferViews[vi] = vd;
                continue;
            }

            int bufIdx = jv.GetInt("buffer", 0);
            int64_t off64 = jv.GetInt64("byteOffset", 0);
            int64_t len64 = jv.GetInt64("byteLength", -1);
            int64_t stride64 = jv.GetInt64("byteStride", 0);
            if (bufIdx < 0 || static_cast<size_t>(bufIdx) >= buffers.size()) continue;
            if (off64 < 0 || len64 < 0 || stride64 < 0 || stride64 > 255) continue;
            const BufferSlice& bs = buffers[static_cast<size_t>(bufIdx)];
            if (!bs.ptr) continue;
            size_t off = static_cast<size_t>(off64);
            size_t len = static_cast<size_t>(len64);
            if (off > bs.size || len > bs.size - off) continue;

            BufferViewDesc vd;
            vd.buffer = bufIdx;
            vd.byteOffset = off;
            vd.byteLength = len;
            vd.byteStride = static_cast<size_t>(stride64);
            vd.valid = true;
            bufferViews[vi] = vd;
        }
    }

    // 3. Resolve accessors (including sparse substitution descriptors)
    std::vector<AccessorDesc> accessors;
    if (const JsonVal* jAccs = root.Find("accessors")) {
        if (!jAccs->IsArray() || jAccs->arrVal.size() > 65536) return false;
        accessors.resize(jAccs->arrVal.size());
        for (size_t ai = 0; ai < jAccs->arrVal.size(); ++ai) {
            const JsonVal& ja = jAccs->arrVal[ai];
            if (!ja.IsObject()) return false;
            int bv = ja.GetInt("bufferView", -1);
            int64_t off64 = ja.GetInt64("byteOffset", 0);
            int compType = ja.GetInt("componentType", 0);
            bool norm = ja.GetBool("normalized", false);
            int64_t cnt64 = ja.GetInt64("count", -1);
            int numComp = TypeComponentCount(ja.GetString("type"));

            if (off64 < 0 || cnt64 <= 0 || cnt64 > 12000000 || numComp == 0 || ComponentByteSize(compType) == 0) {
                continue;
            }

            AccessorDesc ad;
            ad.bufferView = bv;
            ad.byteOffset = static_cast<size_t>(off64);
            ad.componentType = compType;
            ad.normalized = norm;
            ad.count = static_cast<uint32_t>(cnt64);
            ad.numComponents = numComp;

            if (const JsonVal* jSp = ja.Find("sparse")) {
                if (!jSp->IsObject()) continue;
                int64_t spCnt = jSp->GetInt64("count", 0);
                const JsonVal* jSpIdx = jSp->Find("indices");
                const JsonVal* jSpVal = jSp->Find("values");
                if (spCnt < 0 || spCnt > cnt64 || !jSpIdx || !jSpIdx->IsObject() || !jSpVal || !jSpVal->IsObject()) {
                    continue;
                }
                int64_t idxOff = jSpIdx->GetInt64("byteOffset", 0);
                int64_t valOff = jSpVal->GetInt64("byteOffset", 0);
                if (idxOff < 0 || valOff < 0) continue;

                ad.sparse.present = true;
                ad.sparse.count = static_cast<uint32_t>(spCnt);
                ad.sparse.idxBufferView = jSpIdx->GetInt("bufferView", -1);
                ad.sparse.idxByteOffset = static_cast<size_t>(idxOff);
                ad.sparse.idxComponentType = jSpIdx->GetInt("componentType", 0);
                ad.sparse.valBufferView = jSpVal->GetInt("bufferView", -1);
                ad.sparse.valByteOffset = static_cast<size_t>(valOff);
            }

            ad.valid = true;
            accessors[ai] = ad;
        }
    }

    // 4. Decode meshes & primitives
    if (jMeshes->arrVal.size() > 16384) return false;
    std::vector<DecodedMesh> decodedMeshes(jMeshes->arrVal.size());

    for (size_t mi = 0; mi < jMeshes->arrVal.size(); ++mi) {
        const JsonVal& jm = jMeshes->arrVal[mi];
        if (!jm.IsObject()) continue;
        const JsonVal* jPrims = jm.Find("primitives");
        if (!jPrims || !jPrims->IsArray()) continue;

        for (const JsonVal& jp : jPrims->arrVal) {
            if (!jp.IsObject()) continue;
            int mode = jp.GetInt("mode", 4); // default 4 = TRIANGLES
            if (mode != 4 && mode != 5 && mode != 6) {
                continue; // skip point/line-only modes (0..3)
            }

            // Check KHR_draco_mesh_compression extension on primitive
            if (const JsonVal* jPrimExt = jp.Find("extensions")) {
                if (const JsonVal* jDraco = jPrimExt->Find("KHR_draco_mesh_compression")) {
                    if (!jDraco->IsObject()) return false;
                    int dViewIdx = jDraco->GetInt("bufferView", -1);
                    if (dViewIdx < 0 || static_cast<size_t>(dViewIdx) >= bufferViews.size() ||
                        !bufferViews[static_cast<size_t>(dViewIdx)].valid) {
                        return false;
                    }
                    int dracoPosId = -1;
                    if (const JsonVal* jDAttrs = jDraco->Find("attributes")) {
                        dracoPosId = jDAttrs->GetInt("POSITION", -1);
                    }
                    const BufferViewDesc& dvd = bufferViews[static_cast<size_t>(dViewIdx)];
                    const BufferSlice& dbs = buffers[static_cast<size_t>(dvd.buffer)];
                    DecodedPrim dpDraco;
                    uint8_t dEncType = 0, dMaj = 0, dMin = 0;
                    if (!DecodeDracoBufferToMesh(
                            dbs.ptr + dvd.byteOffset,
                            dvd.byteLength,
                            dracoPosId,
                            dpDraco.positions,
                            dpDraco.tris,
                            dEncType,
                            dMaj,
                            dMin) || dpDraco.positions.empty() || dpDraco.tris.empty()) {
                        return false;
                    }
                    decodedMeshes[mi].prims.push_back(std::move(dpDraco));
                    continue;
                }
            }

            const JsonVal* jAttrs = jp.Find("attributes");
            if (!jAttrs || !jAttrs->IsObject()) continue;
            int posAccIdx = jAttrs->GetInt("POSITION", -1);
            if (posAccIdx < 0 || static_cast<size_t>(posAccIdx) >= accessors.size()) continue;

            DecodedPrim dp;
            if (!ReadPositionAccessor(accessors[static_cast<size_t>(posAccIdx)], buffers, bufferViews, dp.positions)) {
                continue;
            }
            if (dp.positions.size() < 3) continue;

            std::vector<uint32_t> idxBuf;
            bool hasIndices = false;
            if (const JsonVal* jIdx = jp.Find("indices")) {
                if (!jIdx->IsNull()) {
                    if (!jIdx->IsNumber()) continue;
                    int idxAcc = static_cast<int>(std::llround(jIdx->numVal));
                    if (idxAcc < 0 || static_cast<size_t>(idxAcc) >= accessors.size()) continue;
                    if (!ReadIndexAccessor(accessors[static_cast<size_t>(idxAcc)], buffers, bufferViews, idxBuf)) {
                        continue;
                    }
                    hasIndices = true;
                }
            }

            const size_t nSeq = hasIndices ? idxBuf.size() : dp.positions.size();
            const size_t vCount = dp.positions.size();
            auto getIdx = [&](size_t k) -> uint32_t {
                return hasIndices ? idxBuf[k] : static_cast<uint32_t>(k);
            };
            auto pushTri = [&](uint32_t a, uint32_t b, uint32_t c) {
                if (a < vCount && b < vCount && c < vCount && a != b && b != c && a != c) {
                    dp.tris.push_back({ a, b, c });
                }
            };

            if (mode == 4) { // TRIANGLES
                for (size_t i = 0; i + 2 < nSeq; i += 3) {
                    pushTri(getIdx(i), getIdx(i + 1), getIdx(i + 2));
                }
            } else if (mode == 5) { // TRIANGLE_STRIP (glTF 2.0 alternating winding)
                for (size_t i = 0; i + 2 < nSeq; ++i) {
                    uint32_t v0 = getIdx(i);
                    uint32_t v1 = getIdx(i + 1);
                    uint32_t v2 = getIdx(i + 2);
                    if ((i & 1u) == 0u) pushTri(v0, v1, v2);
                    else                pushTri(v1, v0, v2);
                }
            } else if (mode == 6) { // TRIANGLE_FAN
                if (nSeq >= 3) {
                    uint32_t center = getIdx(0);
                    for (size_t i = 1; i + 1 < nSeq; ++i) {
                        pushTri(center, getIdx(i), getIdx(i + 1));
                    }
                }
            }

            if (!dp.tris.empty()) {
                decodedMeshes[mi].prims.push_back(std::move(dp));
            }
        }
    }

    // 5. Parse nodes & scenes hierarchy
    std::vector<GltfNode> nodes;
    if (const JsonVal* jNodes = root.Find("nodes")) {
        if (jNodes->IsArray() && jNodes->arrVal.size() <= 65536) {
            nodes.resize(jNodes->arrVal.size());
            for (size_t ni = 0; ni < jNodes->arrVal.size(); ++ni) {
                const JsonVal& jn = jNodes->arrVal[ni];
                if (!jn.IsObject()) continue;
                GltfNode gn;
                gn.meshIndex = jn.GetInt("mesh", -1);

                if (const JsonVal* jCh = jn.Find("children")) {
                    if (jCh->IsArray()) {
                        for (const JsonVal& jc : jCh->arrVal) {
                            if (jc.IsNumber() && std::isfinite(jc.numVal)) {
                                int ci = static_cast<int>(std::llround(jc.numVal));
                                if (ci >= 0) gn.children.push_back(ci);
                            }
                        }
                    }
                }

                bool hasMatrix = false;
                if (const JsonVal* jMat = jn.Find("matrix")) {
                    if (jMat->IsArray() && jMat->arrVal.size() == 16) {
                        Mat4Col m;
                        bool okM = true;
                        for (size_t k = 0; k < 16; ++k) {
                            if (!jMat->arrVal[k].IsNumber() || !std::isfinite(jMat->arrVal[k].numVal)) {
                                okM = false;
                                break;
                            }
                            m.m[k] = static_cast<float>(jMat->arrVal[k].numVal);
                        }
                        if (okM) {
                            gn.localMat = m;
                            hasMatrix = true;
                        }
                    }
                }

                if (!hasMatrix) {
                    float t[3] = { 0.0f, 0.0f, 0.0f };
                    float r[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
                    float s[3] = { 1.0f, 1.0f, 1.0f };

                    if (const JsonVal* jT = jn.Find("translation")) {
                        if (jT->IsArray() && jT->arrVal.size() == 3) {
                            for (int k = 0; k < 3; ++k) {
                                if (jT->arrVal[static_cast<size_t>(k)].IsNumber())
                                    t[k] = static_cast<float>(jT->arrVal[static_cast<size_t>(k)].numVal);
                            }
                        }
                    }
                    if (const JsonVal* jR = jn.Find("rotation")) {
                        if (jR->IsArray() && jR->arrVal.size() == 4) {
                            for (int k = 0; k < 4; ++k) {
                                if (jR->arrVal[static_cast<size_t>(k)].IsNumber())
                                    r[k] = static_cast<float>(jR->arrVal[static_cast<size_t>(k)].numVal);
                            }
                        }
                    }
                    if (const JsonVal* jS = jn.Find("scale")) {
                        if (jS->IsArray() && jS->arrVal.size() == 3) {
                            for (int k = 0; k < 3; ++k) {
                                if (jS->arrVal[static_cast<size_t>(k)].IsNumber())
                                    s[k] = static_cast<float>(jS->arrVal[static_cast<size_t>(k)].numVal);
                            }
                        }
                    }
                    gn.localMat = Mat4Col::FromTRS(t, r, s);
                }

                nodes[ni] = std::move(gn);
            }
        }
    }

    constexpr size_t kMaxOutputVerts = 4000000;
    constexpr size_t kMaxOutputTris  = 4000000;

    auto instantiateMesh = [&](int meshIdx, const Mat4Col& worldMat) {
        if (meshIdx < 0 || static_cast<size_t>(meshIdx) >= decodedMeshes.size()) return;
        const DecodedMesh& dm = decodedMeshes[static_cast<size_t>(meshIdx)];
        for (const DecodedPrim& dp : dm.prims) {
            if (mesh.vertices.size() + dp.positions.size() > kMaxOutputVerts ||
                mesh.tris.size() + dp.tris.size() > kMaxOutputTris) {
                return;
            }
            uint32_t baseOff = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.reserve(mesh.vertices.size() + dp.positions.size());
            for (const Vec3f& v : dp.positions) {
                Vec3f tv = worldMat.TransformPoint(v);
                if (!std::isfinite(tv.x) || !std::isfinite(tv.y) || !std::isfinite(tv.z)) {
                    tv = { 0.0f, 0.0f, 0.0f };
                }
                mesh.vertices.push_back(tv);
            }
            mesh.tris.reserve(mesh.tris.size() + dp.tris.size());
            for (const auto& tri : dp.tris) {
                mesh.tris.push_back({ baseOff + tri[0], baseOff + tri[1], baseOff + tri[2], {} });
            }
        }
    };

    std::vector<uint8_t> activeAncestors(nodes.size(), 0);
    std::function<void(int, const Mat4Col&, int)> traverseNode =
        [&](int nodeIdx, const Mat4Col& parentMat, int depth) {
            if (depth > 64 || nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= nodes.size()) return;
            size_t uIdx = static_cast<size_t>(nodeIdx);
            if (activeAncestors[uIdx]) return; // cycle guard
            activeAncestors[uIdx] = 1;

            const GltfNode& nd = nodes[uIdx];
            Mat4Col worldMat = Mat4Col::Mul(parentMat, nd.localMat);
            if (nd.meshIndex >= 0) {
                instantiateMesh(nd.meshIndex, worldMat);
            }
            for (int childIdx : nd.children) {
                traverseNode(childIdx, worldMat, depth + 1);
            }

            activeAncestors[uIdx] = 0;
        };

    // Traverse active scene root nodes if scenes/nodes are present
    const JsonVal* jScenes = root.Find("scenes");
    if (jScenes && jScenes->IsArray() && !jScenes->arrVal.empty() && !nodes.empty()) {
        int activeScene = root.GetInt("scene", 0);
        if (activeScene < 0 || static_cast<size_t>(activeScene) >= jScenes->arrVal.size()) {
            activeScene = 0;
        }
        const JsonVal& sc = jScenes->arrVal[static_cast<size_t>(activeScene)];
        if (sc.IsObject()) {
            if (const JsonVal* jRootNodes = sc.Find("nodes")) {
                if (jRootNodes->IsArray()) {
                    for (const JsonVal& jrn : jRootNodes->arrVal) {
                        if (jrn.IsNumber() && std::isfinite(jrn.numVal)) {
                            traverseNode(static_cast<int>(std::llround(jrn.numVal)), Mat4Col::Identity(), 0);
                        }
                    }
                }
            }
        }
    } else if (!nodes.empty()) {
        std::vector<uint8_t> hasParent(nodes.size(), 0);
        for (const GltfNode& nd : nodes) {
            for (int ch : nd.children) {
                if (ch >= 0 && static_cast<size_t>(ch) < nodes.size()) {
                    hasParent[static_cast<size_t>(ch)] = 1;
                }
            }
        }
        for (size_t ni = 0; ni < nodes.size(); ++ni) {
            if (!hasParent[ni]) {
                traverseNode(static_cast<int>(ni), Mat4Col::Identity(), 0);
            }
        }
    }

    // Fallback: if scenes/nodes did not instantiate any mesh (e.g. minimal unit-test glTF with only "meshes"),
    // instantiate all entries in "meshes" with identity transform.
    if (mesh.tris.empty()) {
        mesh.vertices.clear();
        for (size_t mi = 0; mi < decodedMeshes.size(); ++mi) {
            instantiateMesh(static_cast<int>(mi), Mat4Col::Identity());
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) {
        mesh.vertices.clear();
        mesh.tris.clear();
        return false;
    }
    return true;
}

} // namespace Formats
