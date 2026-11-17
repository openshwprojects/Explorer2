#pragma once
// JsonDom.h — the JSON (RFC 8259) reader shared by the format parsers: glTF, Jupyter, Avro schemas,
// Electron ASAR headers, GeoJSON (2D and 3D), JSON Lines, HAR, SARIF, CycloneDX / SPDX, Terraform state,
// OpenTimelineIO, Gerber job files and EasyEDA. Large arrays can be streamed element by element.
//
// Strict grammar (no comments, no trailing commas, no single quotes, no raw control characters in
// strings), \u escapes with surrogate pairs (a lone surrogate becomes U+FFFD), an optional UTF-8 BOM,
// and hard limits on nesting depth and node count so hostile input cannot exhaust the stack or memory.
// Numbers keep their literal text, so 64-bit integers (Avro longs, ASAR sizes) come back exactly.
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Explorer2::Json {

struct Value {
    enum class Kind : uint8_t { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolVal = false;
    double numVal = 0.0;
    std::string strVal; // String: the decoded UTF-8 text. Number: the literal as written.
    std::vector<Value> arrVal;
    std::vector<std::pair<std::string, Value>> objVal;

    bool IsNull()   const { return kind == Kind::Null; }
    bool IsBool()   const { return kind == Kind::Bool; }
    bool IsNumber() const { return kind == Kind::Number; }
    bool IsString() const { return kind == Kind::String; }
    bool IsArray()  const { return kind == Kind::Array; }
    bool IsObject() const { return kind == Kind::Object; }

    // First member with this key (JSON objects may repeat keys; the first one wins)
    const Value* Find(std::string_view key) const {
        if (kind != Kind::Object) return nullptr;
        for (const auto& kv : objVal) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }

    // Exact integer value of a Number written as an integer literal (no fraction or exponent)
    bool AsInt64(int64_t& out) const {
        if (kind != Kind::Number || strVal.empty()) return false;
        const char* b = strVal.data();
        const char* e = b + strVal.size();
        int64_t v = 0;
        auto res = std::from_chars(b, e, v);
        if (res.ec != std::errc{} || res.ptr != e) return false;
        out = v;
        return true;
    }
    bool AsUint64(uint64_t& out) const {
        if (kind != Kind::Number || strVal.empty() || strVal[0] == '-') return false;
        const char* b = strVal.data();
        const char* e = b + strVal.size();
        uint64_t v = 0;
        auto res = std::from_chars(b, e, v);
        if (res.ec != std::errc{} || res.ptr != e) return false;
        out = v;
        return true;
    }

    int GetInt(std::string_view key, int defVal) const {
        const Value* v = Find(key);
        if (!v || !v->IsNumber() || !std::isfinite(v->numVal)) return defVal;
        if (v->numVal < -2147483648.0 || v->numVal > 2147483647.0) return defVal;
        return static_cast<int>(std::llround(v->numVal));
    }
    int64_t GetInt64(std::string_view key, int64_t defVal) const {
        const Value* v = Find(key);
        if (!v || !v->IsNumber()) return defVal;
        int64_t exact = 0;
        if (v->AsInt64(exact)) return exact;
        if (!std::isfinite(v->numVal) || v->numVal < -9.0e15 || v->numVal > 9.0e15) return defVal;
        return static_cast<int64_t>(std::llround(v->numVal));
    }
    bool GetBool(std::string_view key, bool defVal) const {
        const Value* v = Find(key);
        return (v && v->IsBool()) ? v->boolVal : defVal;
    }
    std::string GetString(std::string_view key, std::string_view defVal = {}) const {
        const Value* v = Find(key);
        if (!v || !v->IsString()) return std::string(defVal);
        return v->strVal;
    }
};

inline int HexDigitValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline void AppendUtf8(std::string& s, uint32_t cp) {
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

struct Limits {
    int maxDepth = 64;
    size_t maxNodes = 2000000;
};

class Parser {
public:
    explicit Parser(std::string_view text, Limits limits = {}) : m_s(text), m_limits(limits) {}

    // Parses one value starting at the current position (leading whitespace skipped)
    bool ParseValue(Value& out) { return Value_(out, 0); }

    // Streaming access for documents too large to hold as one tree (or cut short by a read limit):
    // StreamObject calls onMember(key) for each member, which must consume the member's value (ParseValue,
    // StreamArray, StreamObject); StreamArray calls onElement(Value&&) for each element, with the node budget
    // reset per element. Both return false at the first malformed or missing byte; members / elements before it
    // have been delivered.
    template <class OnMember>
    bool StreamObject(OnMember&& onMember, int depth = 0) {
        if (depth > m_limits.maxDepth || !Consume('{')) return false;
        if (Consume('}')) return true;
        while (true) {
            std::string key;
            SkipWs();
            if (!String_(key) || !Consume(':')) return false;
            if (!onMember(key)) return false;
            if (Consume(',')) continue;
            return Consume('}');
        }
    }
    template <class OnElement>
    bool StreamArray(OnElement&& onElement) {
        if (!Consume('[')) return false;
        if (Consume(']')) return true;
        while (true) {
            Value v;
            m_nodes = 0;
            if (!ParseValue(v)) return false;
            onElement(std::move(v));
            if (Consume(',')) continue;
            return Consume(']');
        }
    }
    // Like StreamArray, but onElement() consumes each element itself (ParseValue, StreamObject, StreamArray...),
    // so an element can in turn be streamed
    template <class OnElement>
    bool StreamArrayItems(OnElement&& onElement) {
        if (!Consume('[')) return false;
        if (Consume(']')) return true;
        while (true) {
            m_nodes = 0;
            if (!onElement()) return false;
            if (Consume(',')) continue;
            return Consume(']');
        }
    }
    bool Consume(char c) {
        SkipWs();
        if (m_pos < m_s.size() && m_s[m_pos] == c) {
            ++m_pos;
            return true;
        }
        return false;
    }
    char Peek() {
        SkipWs();
        return m_pos < m_s.size() ? m_s[m_pos] : '\0';
    }

    void SkipWs() {
        while (m_pos < m_s.size()) {
            char c = m_s[m_pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++m_pos;
            else break;
        }
    }
    bool AtEnd() { SkipWs(); return m_pos >= m_s.size(); }
    size_t Position() const { return m_pos; }

private:
    std::string_view m_s;
    size_t m_pos = 0;
    size_t m_nodes = 0;
    Limits m_limits;

    bool ReadHex4(uint32_t& out) {
        if (m_s.size() - m_pos < 4) return false;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            int h = HexDigitValue(m_s[m_pos + static_cast<size_t>(i)]);
            if (h < 0) return false;
            v = (v << 4) | static_cast<uint32_t>(h);
        }
        m_pos += 4;
        out = v;
        return true;
    }

    bool String_(std::string& out) {
        if (m_pos >= m_s.size() || m_s[m_pos] != '"') return false;
        ++m_pos;
        out.clear();
        while (m_pos < m_s.size()) {
            // Copy the run of plain characters in one go
            size_t runStart = m_pos;
            while (m_pos < m_s.size()) {
                unsigned char c = static_cast<unsigned char>(m_s[m_pos]);
                if (c == '"' || c == '\\' || c < 0x20u) break;
                ++m_pos;
            }
            out.append(m_s.data() + runStart, m_pos - runStart);
            if (m_pos >= m_s.size()) return false;
            char c = m_s[m_pos++];
            if (c == '"') return true;
            if (c != '\\') return false; // raw control character
            if (m_pos >= m_s.size()) return false;
            char esc = m_s[m_pos++];
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
                        uint32_t lo = 0;
                        size_t save = m_pos;
                        if (m_s.size() - m_pos >= 6 && m_s[m_pos] == '\\' && m_s[m_pos + 1] == 'u') {
                            m_pos += 2;
                            if (ReadHex4(lo) && lo >= 0xDC00u && lo <= 0xDFFFu) {
                                cp = 0x10000u + (((cp - 0xD800u) << 10) | (lo - 0xDC00u));
                            } else {
                                m_pos = save;
                                cp = 0xFFFDu;
                            }
                        } else {
                            cp = 0xFFFDu;
                        }
                    } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
                        cp = 0xFFFDu;
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default:
                    return false;
            }
        }
        return false;
    }

    bool Number_(Value& out) {
        const size_t start = m_pos;
        auto isDigit = [&](size_t i) { return i < m_s.size() && m_s[i] >= '0' && m_s[i] <= '9'; };
        if (m_pos < m_s.size() && m_s[m_pos] == '-') ++m_pos;
        if (!isDigit(m_pos)) return false;
        if (m_s[m_pos] == '0') {
            ++m_pos;
        } else {
            while (isDigit(m_pos)) ++m_pos;
        }
        if (m_pos < m_s.size() && m_s[m_pos] == '.') {
            ++m_pos;
            if (!isDigit(m_pos)) return false;
            while (isDigit(m_pos)) ++m_pos;
        }
        if (m_pos < m_s.size() && (m_s[m_pos] == 'e' || m_s[m_pos] == 'E')) {
            ++m_pos;
            if (m_pos < m_s.size() && (m_s[m_pos] == '+' || m_s[m_pos] == '-')) ++m_pos;
            if (!isDigit(m_pos)) return false;
            while (isDigit(m_pos)) ++m_pos;
        }
        size_t len = m_pos - start;
        if (len == 0 || len > 512) return false;
        double val = 0.0;
        auto res = std::from_chars(m_s.data() + start, m_s.data() + m_pos, val);
        if (res.ec != std::errc{} || res.ptr != m_s.data() + m_pos || !std::isfinite(val)) return false;
        out.kind = Value::Kind::Number;
        out.numVal = val;
        out.strVal.assign(m_s.data() + start, len);
        return true;
    }

    bool Literal_(std::string_view word) {
        if (m_s.size() - m_pos < word.size() || m_s.compare(m_pos, word.size(), word) != 0) return false;
        m_pos += word.size();
        return true;
    }

    bool Value_(Value& out, int depth) {
        if (depth > m_limits.maxDepth || ++m_nodes > m_limits.maxNodes) return false;
        SkipWs();
        if (m_pos >= m_s.size()) return false;
        out = Value{};
        char c = m_s[m_pos];
        if (c == '{') {
            ++m_pos;
            out.kind = Value::Kind::Object;
            SkipWs();
            if (m_pos < m_s.size() && m_s[m_pos] == '}') { ++m_pos; return true; }
            while (true) {
                SkipWs();
                std::string key;
                if (!String_(key)) return false;
                SkipWs();
                if (m_pos >= m_s.size() || m_s[m_pos] != ':') return false;
                ++m_pos;
                out.objVal.emplace_back(std::move(key), Value{});
                if (!Value_(out.objVal.back().second, depth + 1)) return false;
                SkipWs();
                if (m_pos >= m_s.size()) return false;
                if (m_s[m_pos] == ',') { ++m_pos; continue; }
                if (m_s[m_pos] == '}') { ++m_pos; return true; }
                return false;
            }
        }
        if (c == '[') {
            ++m_pos;
            out.kind = Value::Kind::Array;
            SkipWs();
            if (m_pos < m_s.size() && m_s[m_pos] == ']') { ++m_pos; return true; }
            while (true) {
                out.arrVal.emplace_back();
                if (!Value_(out.arrVal.back(), depth + 1)) return false;
                SkipWs();
                if (m_pos >= m_s.size()) return false;
                if (m_s[m_pos] == ',') { ++m_pos; continue; }
                if (m_s[m_pos] == ']') { ++m_pos; return true; }
                return false;
            }
        }
        if (c == '"') {
            out.kind = Value::Kind::String;
            return String_(out.strVal);
        }
        if (c == 't') {
            if (!Literal_("true")) return false;
            out.kind = Value::Kind::Bool;
            out.boolVal = true;
            return true;
        }
        if (c == 'f') {
            if (!Literal_("false")) return false;
            out.kind = Value::Kind::Bool;
            return true;
        }
        if (c == 'n') {
            if (!Literal_("null")) return false;
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) return Number_(out);
        return false;
    }
};

inline std::string_view StripUtf8Bom(std::string_view text) {
    if (text.size() >= 3 && static_cast<uint8_t>(text[0]) == 0xEFu &&
        static_cast<uint8_t>(text[1]) == 0xBBu && static_cast<uint8_t>(text[2]) == 0xBFu) {
        text.remove_prefix(3);
    }
    return text;
}

// A complete document: one value, nothing but whitespace after it
inline bool ParseDocument(std::string_view text, Value& out, Limits limits = {}) {
    Parser p(StripUtf8Bom(text), limits);
    return p.ParseValue(out) && p.AtEnd();
}

// Compact single-line rendering for table cells and summaries; stops adding once out reaches maxLen
inline void AppendCompact(const Value& v, std::string& out, size_t maxLen, int depth = 0) {
    if (out.size() >= maxLen) return;
    switch (v.kind) {
        case Value::Kind::Null:   out += "null"; break;
        case Value::Kind::Bool:   out += v.boolVal ? "true" : "false"; break;
        case Value::Kind::Number: out += v.strVal; break;
        case Value::Kind::String: out += v.strVal; break;
        case Value::Kind::Array:
            out += '[';
            for (size_t i = 0; i < v.arrVal.size() && out.size() < maxLen; ++i) {
                if (i) out += ", ";
                if (depth >= 8) { out += "..."; break; }
                if (v.arrVal[i].IsString()) { out += '"'; AppendCompact(v.arrVal[i], out, maxLen, depth + 1); out += '"'; }
                else AppendCompact(v.arrVal[i], out, maxLen, depth + 1);
            }
            out += ']';
            break;
        case Value::Kind::Object:
            out += '{';
            for (size_t i = 0; i < v.objVal.size() && out.size() < maxLen; ++i) {
                if (i) out += ", ";
                out += '"';
                out += v.objVal[i].first;
                out += "\": ";
                if (depth >= 8) { out += "..."; break; }
                if (v.objVal[i].second.IsString()) { out += '"'; AppendCompact(v.objVal[i].second, out, maxLen, depth + 1); out += '"'; }
                else AppendCompact(v.objVal[i].second, out, maxLen, depth + 1);
            }
            out += '}';
            break;
    }
}

// Compact rendering cut to about maxLen bytes (on a UTF-8 character boundary, marked with "...")
inline std::string Compact(const Value& v, size_t maxLen = 256) {
    std::string s;
    AppendCompact(v, s, maxLen);
    if (s.size() > maxLen) {
        size_t cut = maxLen;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0u) == 0x80u) --cut;
        s.resize(cut);
        s += "...";
    }
    return s;
}

} // namespace Explorer2::Json
