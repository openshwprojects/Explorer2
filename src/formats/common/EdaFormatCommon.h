#pragma once
// EdaFormatCommon.h — Shared S-expression, PCB/schematic geometry, JTAG/scope graticule,
// Altium OLE2/pipe, Vector CAN, waveform, gate-DAG, and manufacturing TAR/polygon helpers.
// NOTE: Included inside `namespace Explorer2` in src/EdaReaders.cpp. Do NOT add `#include` directives here.

inline bool ParseGerberCoordField(
    std::string_view rawDigits,
    int intDigits,
    int decDigits,
    bool trailingZeroOmit,
    double unitToMm,
    float& outValMm)
{
    if (rawDigits.empty()) return false;
    bool neg = false;
    size_t pos = 0;
    if (rawDigits[pos] == '-' || rawDigits[pos] == '+') {
        neg = (rawDigits[pos] == '-');
        ++pos;
    }
    if (pos >= rawDigits.size()) return false;
    std::string_view numPart = rawDigits.substr(pos);
    if (numPart.find('.') != std::string_view::npos) {
        char buf[64] = {};
        size_t n = std::min<size_t>(rawDigits.size(), 63);
        std::memcpy(buf, rawDigits.data(), n);
        double v = std::strtod(buf, nullptr) * unitToMm;
        outValMm = static_cast<float>(v);
        return std::isfinite(outValMm);
    }
    for (char c : numPart) {
        if (c < '0' || c > '9') return false;
    }
    std::string digits(numPart);
    const int totalDigits = std::max(1, intDigits + decDigits);
    if (trailingZeroOmit && static_cast<int>(digits.size()) < totalDigits) {
        digits.append(static_cast<size_t>(totalDigits - static_cast<int>(digits.size())), '0');
    }
    double intVal = 0.0;
    for (char c : digits) {
        intVal = intVal * 10.0 + static_cast<double>(c - '0');
    }
    if (neg) intVal = -intVal;
    double scaled = intVal * std::pow(10.0, -decDigits) * unitToMm;
    outValMm = static_cast<float>(scaled);
    return std::isfinite(outValMm);
}

inline EdaLayerId MapDxfLayerToEda(std::string_view layerName) {
    if (layerName.empty() || layerName == "0") return EdaLayerId::TopCopper;
    uint32_t h = 2166136261u;
    for (char c : layerName) {
        h ^= static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(c)));
        h *= 16777619u;
    }
    switch (h % 5) {
    case 0: return EdaLayerId::TopCopper;
    case 1: return EdaLayerId::BottomCopper;
    case 2: return EdaLayerId::TopSilk;
    case 3: return EdaLayerId::EdgeCuts;
    default: return EdaLayerId::InnerCopper;
    }
}

namespace Formats {

namespace DetailSpecctra {

struct SNode {
    std::string atom;
    std::vector<SNode> list;
    bool isList = false;
};

inline bool ParseSExprTree(std::string_view sv, std::vector<SNode>& outRoots) {
    outRoots.clear();
    // Reject binary NULs
    for (unsigned char c : sv) {
        if (c == 0) return false;
    }

    char quoteChar = '"';
    size_t pos = 0;

    auto parseList = [&](auto& self, int depth, SNode& outList) -> bool {
        if (depth > 64) return false;
        outList.isList = true;
        while (pos < sv.size()) {
            while (pos < sv.size()) {
                unsigned char c = static_cast<unsigned char>(sv[pos]);
                if (c <= 0x20u) { ++pos; continue; }
                if (c == '#') {
                    while (pos < sv.size() && sv[pos] != '\n') ++pos;
                    continue;
                }
                break;
            }
            if (pos >= sv.size()) return false; // Unclosed '('!
            char c = sv[pos];
            if (c == ')') {
                ++pos;
                return true;
            }
            // Special Specctra rule: (string_quote ") has a single bare quote character token before ')'!
            if (outList.list.size() == 1 && !outList.list[0].isList && outList.list[0].atom == "string_quote" &&
                c != '(' && c != ')') {
                quoteChar = c;
                ++pos;
                SNode qNode;
                qNode.atom.assign(1, c);
                outList.list.push_back(std::move(qNode));
                continue;
            }
            if (c == '(') {
                ++pos;
                SNode child;
                if (!self(self, depth + 1, child)) return false;
                outList.list.push_back(std::move(child));
                continue;
            }
            if (c == quoteChar || c == '"') {
                char qc = c;
                ++pos;
                SNode atomNode;
                while (pos < sv.size() && sv[pos] != qc) {
                    if (sv[pos] == '\\' && pos + 1 < sv.size()) {
                        atomNode.atom.push_back(sv[++pos]);
                        ++pos;
                    } else {
                        atomNode.atom.push_back(sv[pos++]);
                    }
                }
                if (pos >= sv.size()) return false; // Unterminated string!
                ++pos;
                outList.list.push_back(std::move(atomNode));
                continue;
            }
            size_t s = pos;
            while (pos < sv.size()) {
                unsigned char ch = static_cast<unsigned char>(sv[pos]);
                if (ch <= 0x20u || ch == '(' || ch == ')' || ch == quoteChar || ch == '"') break;
                ++pos;
            }
            SNode atomNode;
            atomNode.atom.assign(sv.data() + s, pos - s);
            outList.list.push_back(std::move(atomNode));
        }
        return false;
    };

    while (pos < sv.size()) {
        while (pos < sv.size()) {
            unsigned char c = static_cast<unsigned char>(sv[pos]);
            if (c <= 0x20u) { ++pos; continue; }
            if (c == '#') { while (pos < sv.size() && sv[pos] != '\n') ++pos; continue; }
            break;
        }
        if (pos >= sv.size()) break;
        if (sv[pos] == ')') return false; // Stray ')'
        if (sv[pos] == '(') {
            ++pos;
            SNode root;
            if (!parseList(parseList, 0, root)) return false;
            outRoots.push_back(std::move(root));
        } else {
            return false;
        }
    }
    return !outRoots.empty();
}

inline std::string_view HeadKey(const SNode& n) {
    if (!n.isList || n.list.empty() || n.list[0].isList) return {};
    return n.list[0].atom;
}

inline bool AtomFloat(const SNode& n, double& outVal) {
    if (n.isList || n.atom.empty()) return false;
    char b[64] = {};
    std::memcpy(b, n.atom.data(), std::min<size_t>(n.atom.size(), 63));
    char* endp = nullptr;
    double v = std::strtod(b, &endp);
    if (endp == b || !std::isfinite(v)) return false;
    outVal = v;
    return true;
}

inline double UnitNameToMm(std::string_view u) {
    std::string s(u);
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (s == "mil" || s == "mils") return 0.0254;
    if (s == "in" || s == "inch" || s == "inches") return 25.4;
    if (s == "um" || s == "micron" || s == "microns") return 0.001;
    if (s == "cm") return 10.0;
    return 1.0; // mm
}

} // namespace DetailSpecctra

namespace DetailEda100 {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdI32LE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU64BE;

inline uint64_t ReadULeb128(const uint8_t* data, size_t size, size_t& pos) {
    uint64_t result = 0;
    int shift = 0;
    while (pos < size) {
        uint8_t b = data[pos++];
        if (shift < 64) {
            result |= static_cast<uint64_t>(b & 0x7Fu) << shift;
        }
        if ((b & 0x80u) == 0) break;
        shift += 7;
        if (shift > 70) break;
    }
    return result;
}

inline int64_t ReadSLeb128(const uint8_t* data, size_t size, size_t& pos) {
    int64_t result = 0;
    int shift = 0;
    uint8_t b = 0;
    while (pos < size) {
        b = data[pos++];
        if (shift < 64) {
            result |= static_cast<int64_t>(static_cast<uint64_t>(b & 0x7Fu) << shift);
        }
        shift += 7;
        if ((b & 0x80u) == 0) break;
        if (shift > 70) break;
    }
    if (shift < 64 && (b & 0x40u) != 0) {
        result |= -(static_cast<int64_t>(1) << shift);
    }
    return result;
}

inline void AddRectOutline(ParsedEdaBoard& out, float cx, float cy, float w, float h, EdaLayerId layer, float stroke = 0.28f) {
    float x0 = cx - w * 0.5f, x1 = cx + w * 0.5f;
    float y0 = cy - h * 0.5f, y1 = cy + h * 0.5f;
    out.traces.push_back({ x0, y0, x1, y0, stroke, layer, 0 });
    out.traces.push_back({ x1, y0, x1, y1, stroke, layer, 0 });
    out.traces.push_back({ x1, y1, x0, y1, stroke, layer, 0 });
    out.traces.push_back({ x0, y1, x0, y0, stroke, layer, 0 });
}

inline void AddTessellatedArc(
    ParsedEdaBoard& out,
    float xs, float ys, float xe, float ye, float xc, float yc,
    bool clockwise, float width, EdaLayerId layer, int netId = 0)
{
    float r = std::hypot(xs - xc, ys - yc);
    if (r < 1e-4f) {
        out.traces.push_back({ xs, ys, xe, ye, width, layer, netId });
        return;
    }
    float a0 = std::atan2(ys - yc, xs - xc);
    float a1 = std::atan2(ye - yc, xe - xc);
    constexpr float kTwoPi = 6.283185307f;
    float sweep = 0.0f;
    if (clockwise) {
        sweep = a0 - a1;
        while (sweep <= 1e-4f) sweep += kTwoPi;
        sweep = -sweep;
    } else {
        sweep = a1 - a0;
        while (sweep <= 1e-4f) sweep += kTwoPi;
    }
    if (std::hypot(xe - xs, ye - ys) < 1e-3f && std::fabs(std::fabs(sweep) - kTwoPi) < 0.05f) {
        out.circles.push_back({ xc, yc, r, width, false, layer });
        return;
    }
    int steps = std::clamp(static_cast<int>(std::ceil(std::fabs(sweep) * 8.0f)), 4, 32);
    float px = xs, py = ys;
    for (int i = 1; i <= steps; ++i) {
        float ang = a0 + sweep * (static_cast<float>(i) / static_cast<float>(steps));
        float nx = (i == steps) ? xe : (xc + r * std::cos(ang));
        float ny = (i == steps) ? ye : (yc + r * std::sin(ang));
        out.traces.push_back({ px, py, nx, ny, width, layer, netId });
        px = nx;
        py = ny;
    }
}

inline std::string TrimAsciiView(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n' || sv.back() == '\0')) sv.remove_suffix(1);
    return std::string(sv);
}

inline bool ContainsAsciiI(std::string_view hay, std::string_view needle) {
    if (needle.empty() || hay.size() < needle.size()) return false;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            char a = hay[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
            if (a != b) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

// Decompresses UNIX compress (.Z, 0x1F 0x9D) streams found in legacy ODB++ layer features.Z
inline bool DecompressUnixLzw(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, size_t maxOut = 32 * 1024 * 1024) {
    out.clear();
    if (!src || srcLen < 3 || src[0] != 0x1F || src[1] != 0x9D) return false;
    int maxBits = src[2] & 0x1F;
    bool blockMode = (src[2] & 0x80) != 0;
    if (maxBits < 9 || maxBits > 16) return false;

    const int maxCodeLimit = 1 << maxBits;
    std::vector<uint16_t> prefix(maxCodeLimit, 0);
    std::vector<uint8_t> suffix(maxCodeLimit, 0);
    std::vector<uint8_t> stack;
    stack.reserve(4096);
    for (int i = 0; i < 256; ++i) suffix[i] = static_cast<uint8_t>(i);

    int nBits = 9;
    int maxCode = (1 << nBits) - 1;
    int freeEnt = blockMode ? 257 : 256;
    size_t bitPos = 24;
    size_t clearBitPos = 24;

    auto readCode = [&]() -> int {
        size_t byteOff = bitPos >> 3;
        if (byteOff >= srcLen) return -1;
        uint32_t raw = src[byteOff];
        if (byteOff + 1 < srcLen) raw |= static_cast<uint32_t>(src[byteOff + 1]) << 8;
        if (byteOff + 2 < srcLen) raw |= static_cast<uint32_t>(src[byteOff + 2]) << 16;
        int shift = static_cast<int>(bitPos & 7);
        if ((bitPos + static_cast<size_t>(nBits)) > srcLen * 8) return -1;
        bitPos += static_cast<size_t>(nBits);
        return static_cast<int>((raw >> shift) & ((1u << nBits) - 1u));
    };

    int oldCode = readCode();
    if (oldCode < 0 || oldCode >= 256) return oldCode < 0;
    uint8_t finChar = static_cast<uint8_t>(oldCode);
    out.push_back(finChar);

    while (out.size() < maxOut) {
        if (freeEnt > maxCode && nBits < maxBits) {
            // Align to nBits*8 block boundary per UNIX compress spec
            size_t bitsSinceClear = bitPos - clearBitPos;
            size_t blockBits = static_cast<size_t>(nBits) * 8;
            size_t rem = bitsSinceClear % blockBits;
            if (rem != 0) bitPos += (blockBits - rem);
            clearBitPos = bitPos;
            ++nBits;
            maxCode = (nBits == maxBits) ? maxCodeLimit : ((1 << nBits) - 1);
        }
        int code = readCode();
        if (code < 0) break;
        if (code == 256 && blockMode) {
            size_t bitsSinceClear = bitPos - clearBitPos;
            size_t blockBits = static_cast<size_t>(nBits) * 8;
            size_t rem = bitsSinceClear % blockBits;
            if (rem != 0) bitPos += (blockBits - rem);
            clearBitPos = bitPos;
            nBits = 9;
            maxCode = (1 << nBits) - 1;
            freeEnt = 257;
            oldCode = readCode();
            if (oldCode < 0) break;
            if (oldCode >= 256) return false;
            finChar = static_cast<uint8_t>(oldCode);
            out.push_back(finChar);
            continue;
        }
        int inCode = code;
        stack.clear();
        if (code >= freeEnt) {
            if (code > freeEnt) return false;
            stack.push_back(finChar);
            code = oldCode;
        }
        int guard = 0;
        while (code >= 256 && guard++ < maxCodeLimit) {
            stack.push_back(suffix[code]);
            code = prefix[code];
        }
        finChar = suffix[code];
        stack.push_back(finChar);
        for (size_t k = stack.size(); k > 0 && out.size() < maxOut; --k) {
            out.push_back(stack[k - 1]);
        }
        if (freeEnt < maxCodeLimit) {
            prefix[freeEnt] = static_cast<uint16_t>(oldCode);
            suffix[freeEnt] = finChar;
            ++freeEnt;
        }
        oldCode = inCode;
    }
    return !out.empty();
}

inline std::string UnpackMaybeCompressedText(const uint8_t* bytes, size_t len) {
    if (!bytes || len == 0) return {};
    std::vector<uint8_t> dec;
    if (len >= 10 && bytes[0] == 0x1F && bytes[1] == 0x8B) {
        if (ZipDeflate::InflateGzip(bytes, len, dec, 32 * 1024 * 1024)) {
            return std::string(reinterpret_cast<const char*>(dec.data()), dec.size());
        }
    }
    if (len >= 3 && bytes[0] == 0x1F && bytes[1] == 0x9D) {
        if (DecompressUnixLzw(bytes, len, dec, 32 * 1024 * 1024)) {
            return std::string(reinterpret_cast<const char*>(dec.data()), dec.size());
        }
    }
    if (len >= 6 && (bytes[0] & 0x0F) == 0x08 && ((static_cast<uint16_t>(bytes[0]) * 256u + bytes[1]) % 31u) == 0) {
        if (ZipDeflate::InflateZlib(bytes, len, dec, 32 * 1024 * 1024)) {
            return std::string(reinterpret_cast<const char*>(dec.data()), dec.size());
        }
    }
    return std::string(reinterpret_cast<const char*>(bytes), len);
}

inline bool LooksLikeTarBlock(const uint8_t* data, size_t size) {
    if (size < 512) return false;
    if (std::memcmp(data + 257, "ustar", 5) == 0) return true;
    // Also accept classic V7 tar headers with valid octal checksum and printable ASCII path
    if (data[0] < 32 || data[0] >= 127) return false;
    uint32_t storedChk = 0;
    bool hasOct = false;
    for (size_t i = 148; i < 156; ++i) {
        if (data[i] >= '0' && data[i] <= '7') {
            storedChk = (storedChk << 3) | static_cast<uint32_t>(data[i] - '0');
            hasOct = true;
        }
    }
    if (!hasOct) return false;
    uint32_t sumU = 0;
    int32_t sumS = 0;
    for (size_t i = 0; i < 512; ++i) {
        uint8_t b = (i >= 148 && i < 156) ? 0x20u : data[i];
        sumU += b;
        sumS += static_cast<int8_t>(b);
    }
    return (storedChk == sumU || static_cast<int32_t>(storedChk) == sumS);
}

inline EdaLayerId ClassifyIpcLayerName(std::string_view name, std::string_view func = {}, std::string_view side = {}) {
    if (ContainsAsciiI(func, "BOARD_OUTLINE") || ContainsAsciiI(func, "ROUT") || ContainsAsciiI(func, "PROFILE"))
        return EdaLayerId::EdgeCuts;
    if (ContainsAsciiI(func, "DRILL"))
        return EdaLayerId::Drills;
    if (ContainsAsciiI(func, "SILK") || ContainsAsciiI(func, "LEGEND")) {
        return ContainsAsciiI(side, "BOT") ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk;
    }
    if (ContainsAsciiI(func, "CONDUCTOR") || ContainsAsciiI(func, "SIGNAL") || ContainsAsciiI(func, "PLANE") || ContainsAsciiI(func, "MIXED")) {
        if (ContainsAsciiI(side, "BOT")) return EdaLayerId::BottomCopper;
        if (ContainsAsciiI(side, "INT")) return EdaLayerId::InnerCopper;
        return EdaLayerId::TopCopper;
    }
    if (ContainsAsciiI(name, "OUTLINE") || ContainsAsciiI(name, "PROFILE") || ContainsAsciiI(name, "EDGE") || ContainsAsciiI(name, "ROUT"))
        return EdaLayerId::EdgeCuts;
    if (ContainsAsciiI(name, "DRILL"))
        return EdaLayerId::Drills;
    if (ContainsAsciiI(name, "SILK") || ContainsAsciiI(name, "LEGEND") || ContainsAsciiI(name, "SST") || ContainsAsciiI(name, "SSB") || ContainsAsciiI(name, "GTO") || ContainsAsciiI(name, "GBO")) {
        return (ContainsAsciiI(name, "BOT") || ContainsAsciiI(name, "SSB") || ContainsAsciiI(name, "GBO") || ContainsAsciiI(name, "B.") || ContainsAsciiI(name, "B_"))
            ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk;
    }
    if (ContainsAsciiI(name, "BOT") || ContainsAsciiI(name, "GBL") || ContainsAsciiI(name, "B.CU") || ContainsAsciiI(name, "B_CU"))
        return EdaLayerId::BottomCopper;
    if (ContainsAsciiI(name, "INNER") || ContainsAsciiI(name, "IN1") || ContainsAsciiI(name, "IN2") || ContainsAsciiI(name, "MID"))
        return EdaLayerId::InnerCopper;
    return EdaLayerId::TopCopper;
}

struct OdbSymInfo {
    float wMm = 0.254f;
    float hMm = 0.254f;
    float drillMm = 0.0f;
    uint8_t shape = 0; // 0=round, 1=rect, 3=oval
};

inline OdbSymInfo ParseOdbSymbolDef(std::string_view def, bool fileIsMm) {
    OdbSymInfo s;
    std::string d = TrimAsciiView(def);
    bool explicitMm = false, explicitIn = false;
    size_t sp = d.find_first_of(" \t");
    if (sp != std::string::npos) {
        std::string tail = TrimAsciiView(std::string_view(d).substr(sp + 1));
        if (!tail.empty() && (tail[0] == 'M' || tail[0] == 'm')) explicitMm = true;
        if (!tail.empty() && (tail[0] == 'I' || tail[0] == 'i')) explicitIn = true;
        d.resize(sp);
    }
    auto scaleDim = [&](float v) -> float {
        if (explicitMm) return v * 0.001f; // microns in ODB++ metric symbol
        if (explicitIn) return v * 0.0254f; // mils in ODB++ inch symbol
        if (fileIsMm && v >= 50.0f) return v * 0.001f; // microns (e.g. r250 = 0.25mm)
        return v * 0.0254f; // mils (e.g. r10 = 0.254mm)
    };

    if (d.rfind("rect", 0) == 0 && d.size() > 4) {
        float w = 0.0f, h = 0.0f;
        if (sscanf_s(d.c_str() + 4, "%fx%f", &w, &h) == 2) {
            s.wMm = std::max(0.08f, scaleDim(w));
            s.hMm = std::max(0.08f, scaleDim(h));
            s.shape = 1;
            return s;
        }
    }
    if (d.rfind("oval", 0) == 0 && d.size() > 4) {
        float w = 0.0f, h = 0.0f;
        if (sscanf_s(d.c_str() + 4, "%fx%f", &w, &h) == 2) {
            s.wMm = std::max(0.08f, scaleDim(w));
            s.hMm = std::max(0.08f, scaleDim(h));
            s.shape = 3;
            return s;
        }
    }
    if ((d[0] == 'r' || d[0] == 's') && d.size() > 1 && ((d[1] >= '0' && d[1] <= '9') || d[1] == '.')) {
        float v = std::strtof(d.c_str() + 1, nullptr);
        float mm = std::max(0.06f, scaleDim(v));
        s.wMm = mm;
        s.hMm = mm;
        s.shape = (d[0] == 's') ? 1 : 0;
    }
    return s;
}

inline void ParseOdbComponentsStream(std::string_view sv, bool bottomLayer, ParsedEdaBoard& out, size_t& validRecords) {
    bool isMm = false;
    float uScale = 25.4f;
    size_t pos = 0;
    while (pos < sv.size() && out.components.size() < 8000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.empty() || ln[0] == '#') continue;

        if (ln.rfind("UNITS=", 0) == 0) {
            isMm = ContainsAsciiI(ln.substr(6), "MM");
            uScale = isMm ? 1.0f : 25.4f;
            continue;
        }
        if (ln == "U MM") { isMm = true; uScale = 1.0f; continue; }
        if (ln == "U INCH" || ln == "U IN") { isMm = false; uScale = 25.4f; continue; }

        if (ln.rfind("CMP ", 0) == 0 && ln.size() > 8) {
            // CMP <pkg_ref> <x> <y> <rot> <mirror> <comp_name> [part_name] [attrs...]
            std::string body(ln.substr(4));
            size_t semi = body.find(';');
            if (semi != std::string::npos) body.resize(semi);

            int pkgIdx = 0;
            float cx = 0.0f, cy = 0.0f, rot = 0.0f;
            char mirrorCh = 'N';
            char compName[64] = {};
            char partName[64] = {};
            int n = sscanf_s(body.c_str(), "%d %f %f %f %c %63s %63s",
                &pkgIdx, &cx, &cy, &rot, &mirrorCh, 1,
                compName, static_cast<unsigned>(sizeof(compName)),
                partName, static_cast<unsigned>(sizeof(partName)));
            if (n >= 6) {
                EdaComponent3D c;
                c.refDes = PathUtils::Utf8ToWide(compName);
                c.value = (n >= 7) ? PathUtils::Utf8ToWide(partName) : L"";
                c.footprint = L"PKG_" + std::to_wstring(pkgIdx);
                c.x = cx * uScale;
                c.y = cy * uScale;
                c.rotDeg = rot;
                c.bottomSide = bottomLayer || (mirrorCh == 'M' || mirrorCh == 'm');
                c.category = ClassifyComponent(c.refDes, c.value.empty() ? c.footprint : c.value, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
                out.components.push_back(c);
                ++validRecords;
            }
        } else if (ln.rfind("TOP ", 0) == 0 && out.pads.size() < 40000) {
            // TOP <pin_num> <x> <y> <rot> <mirror> <net_num> <subnet_num> <pin_name>
            int pinNum = 0, netNum = 0, subNet = 0;
            float px = 0.0f, py = 0.0f, rot = 0.0f;
            char mir = 'N';
            char pinName[32] = {};
            std::string body(ln.substr(4));
            int n = sscanf_s(body.c_str(), "%d %f %f %f %c %d %d %31s",
                &pinNum, &px, &py, &rot, &mir, 1, &netNum, &subNet, pinName, static_cast<unsigned>(sizeof(pinName)));
            if (n >= 4) {
                EdaLayerId pl = (bottomLayer || mir == 'M') ? EdaLayerId::BottomCopper : EdaLayerId::TopCopper;
                out.pads.push_back({ px * uScale, py * uScale, 0.85f, 0.85f, rot, 0.0f, 1, pl,
                    (n >= 8) ? PathUtils::Utf8ToWide(pinName) : std::to_wstring(pinNum), L"" });
                ++validRecords;
            }
        }
    }
}

inline void ParseOdbMatrixLayers(std::string_view matrixTxt, std::unordered_map<std::string, EdaLayerId>& outLayerMap) {
    struct MatLayer { std::string name; std::string type; int row = 0; };
    std::vector<MatLayer> sigLayers;
    size_t pos = 0;
    while ((pos = matrixTxt.find("LAYER", pos)) != std::string_view::npos) {
        size_t ob = matrixTxt.find('{', pos);
        if (ob == std::string_view::npos) break;
        size_t cb = matrixTxt.find('}', ob);
        if (cb == std::string_view::npos) break;
        std::string_view blk = matrixTxt.substr(ob + 1, cb - ob - 1);
        pos = cb + 1;

        std::string name, type, ctx;
        int row = 0;
        size_t lp = 0;
        while (lp < blk.size()) {
            size_t eol = blk.find('\n', lp);
            if (eol == std::string_view::npos) eol = blk.size();
            std::string kv = TrimAsciiView(blk.substr(lp, eol - lp));
            lp = eol + 1;
            size_t eq = kv.find('=');
            if (eq == std::string::npos) continue;
            std::string k = PathUtils::ToLowerAscii(TrimAsciiView(std::string_view(kv).substr(0, eq)));
            std::string v = PathUtils::ToLowerAscii(TrimAsciiView(std::string_view(kv).substr(eq + 1)));
            if (k == "name") name = v;
            else if (k == "type") type = v;
            else if (k == "context") ctx = v;
            else if (k == "row") row = std::atoi(v.c_str());
        }
        if (name.empty()) continue;
        if (type == "signal" || type == "power_ground" || type == "mixed") {
            sigLayers.push_back({ name, type, row });
        } else if (type == "silk_screen") {
            outLayerMap[name] = ContainsAsciiI(name, "bot") || ContainsAsciiI(name, "ssb") ? EdaLayerId::BottomSilk : EdaLayerId::TopSilk;
        } else if (type == "drill") {
            outLayerMap[name] = EdaLayerId::Drills;
        } else if (type == "rout") {
            outLayerMap[name] = EdaLayerId::EdgeCuts;
        }
    }
    std::sort(sigLayers.begin(), sigLayers.end(), [](const MatLayer& a, const MatLayer& b) { return a.row < b.row; });
    for (size_t i = 0; i < sigLayers.size(); ++i) {
        if (i == 0) outLayerMap[sigLayers[i].name] = EdaLayerId::TopCopper;
        else if (i + 1 == sigLayers.size()) outLayerMap[sigLayers[i].name] = EdaLayerId::BottomCopper;
        else outLayerMap[sigLayers[i].name] = EdaLayerId::InnerCopper;
    }
}

} // namespace DetailEda100

namespace DetailEda200 {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU32BE;

struct JtagBitCycle {
    uint8_t tms = 0;
    uint8_t tdi = 0;
    uint8_t tdo = 0;
    bool hasTdo = false;
};

struct JtagTapTracker {
    static constexpr size_t kMaxVisualCycles = 96;
    // IEEE 1149.1 TAP next-state table indexed by XAPP503 state encoding (0x00..0x0F) and TMS (0 or 1)
    static constexpr uint8_t kNextTap[16][2] = {
        { 0x01, 0x00 }, // 0x00 Test-Logic-Reset
        { 0x01, 0x02 }, // 0x01 Run-Test/Idle
        { 0x03, 0x09 }, // 0x02 Select-DR-Scan
        { 0x04, 0x05 }, // 0x03 Capture-DR
        { 0x04, 0x05 }, // 0x04 Shift-DR
        { 0x06, 0x08 }, // 0x05 Exit1-DR
        { 0x06, 0x07 }, // 0x06 Pause-DR
        { 0x04, 0x08 }, // 0x07 Exit2-DR
        { 0x01, 0x02 }, // 0x08 Update-DR
        { 0x0A, 0x00 }, // 0x09 Select-IR-Scan
        { 0x0B, 0x0C }, // 0x0A Capture-IR
        { 0x0B, 0x0C }, // 0x0B Shift-IR
        { 0x0D, 0x0F }, // 0x0C Exit1-IR
        { 0x0D, 0x0E }, // 0x0D Pause-IR
        { 0x0B, 0x0F }, // 0x0E Exit2-IR
        { 0x01, 0x02 }, // 0x0F Update-IR
    };

    uint8_t curState = 0x00;
    std::vector<JtagBitCycle> cycles;

    void PushClock(uint8_t tms, uint8_t tdi = 0, uint8_t tdo = 0, bool hasTdo = false) {
        if (cycles.size() < kMaxVisualCycles) {
            cycles.push_back({ static_cast<uint8_t>(tms & 1u), static_cast<uint8_t>(tdi & 1u), static_cast<uint8_t>(tdo & 1u), hasTdo });
        }
        if (curState < 16u) {
            curState = kNextTap[curState][tms & 1u];
        }
    }

    void StepTo(uint8_t target, bool forceClockIfSame) {
        if (target > 0x0Fu) return;
        if (curState == target) {
            if (forceClockIfSame) {
                if (target == 0x00u) PushClock(1);
                else if (target == 0x01u || target == 0x04u || target == 0x06u || target == 0x0Bu || target == 0x0Du) PushClock(0);
            }
            return;
        }
        int prevSt[16];
        uint8_t prevTms[16] = {};
        for (int i = 0; i < 16; ++i) prevSt[i] = -1;
        uint8_t q[16] = {};
        int qh = 0, qt = 0;
        q[qt++] = curState;
        prevSt[curState] = static_cast<int>(curState);
        while (qh < qt) {
            uint8_t u = q[qh++];
            if (u == target) break;
            for (uint8_t tms = 0; tms <= 1u; ++tms) {
                uint8_t v = kNextTap[u][tms];
                if (prevSt[v] < 0) {
                    prevSt[v] = static_cast<int>(u);
                    prevTms[v] = tms;
                    q[qt++] = v;
                }
            }
        }
        if (prevSt[target] < 0) return;
        uint8_t path[16] = {};
        int plen = 0;
        uint8_t cur = target;
        while (cur != curState && plen < 16) {
            path[plen++] = prevTms[cur];
            cur = static_cast<uint8_t>(prevSt[cur]);
        }
        for (int i = plen - 1; i >= 0; --i) {
            PushClock(path[i]);
        }
    }

    void EmitIdleClocks(uint32_t count, uint8_t waitState = 0x01u) {
        StepTo(waitState, false);
        uint32_t vis = std::min<uint32_t>(count, 8u);
        uint8_t holdTms = (waitState == 0x00u) ? 1u : 0u;
        for (uint32_t i = 0; i < vis; ++i) {
            PushClock(holdTms);
        }
    }

    void ShiftBytesBE(
        uint8_t shiftState,
        uint8_t endState,
        uint32_t numBits,
        const uint8_t* tdiBytes,
        const uint8_t* tdoBytes,
        const std::vector<uint8_t>* tdoMask,
        bool exitOnLast)
    {
        if (numBits == 0u) return;
        StepTo(shiftState, false);
        const size_t nb = (static_cast<size_t>(numBits) + 7u) / 8u;
        const uint32_t emitLimit = (cycles.size() < kMaxVisualCycles)
            ? static_cast<uint32_t>(std::min<size_t>(static_cast<size_t>(numBits), kMaxVisualCycles - cycles.size()))
            : 0u;

        for (uint32_t b = 0; b < emitLimit; ++b) {
            bool isLast = exitOnLast && (b + 1u == numBits);
            size_t byteIdx = nb - 1u - static_cast<size_t>(b / 8u);
            int bitShift = static_cast<int>(b & 7u);
            uint8_t tdiBit = (tdiBytes != nullptr) ? static_cast<uint8_t>((tdiBytes[byteIdx] >> bitShift) & 1u) : 0u;
            bool careTdo = (tdoBytes != nullptr);
            if (careTdo && tdoMask != nullptr && tdoMask->size() == nb) {
                careTdo = ((((*tdoMask)[byteIdx] >> bitShift) & 1u) != 0u);
            }
            uint8_t tdoBit = (tdoBytes != nullptr) ? static_cast<uint8_t>((tdoBytes[byteIdx] >> bitShift) & 1u) : 0u;
            cycles.push_back({ static_cast<uint8_t>(isLast ? 1u : 0u), tdiBit, tdoBit, careTdo });
        }

        if (exitOnLast) {
            curState = kNextTap[shiftState][1];
            StepTo(endState, false);
        } else {
            curState = shiftState;
        }
    }
};

inline void DrawGraticule(ParsedEdaBoard& out, float x0, float y0, float w, float h, int nx = 10, int ny = 8) {
    DetailEda100::AddRectOutline(out, x0 + w * 0.5f, y0 + h * 0.5f, w, h, EdaLayerId::EdgeCuts, 0.35f);
    for (int i = 1; i < nx; ++i) {
        float x = x0 + (w * i) / nx;
        out.traces.push_back({ x, y0, x, y0 + h, 0.08f, EdaLayerId::BottomSilk, 0 });
    }
    for (int j = 1; j < ny; ++j) {
        float y = y0 + (h * j) / ny;
        out.traces.push_back({ x0, y, x0 + w, y, 0.08f, EdaLayerId::BottomSilk, 0 });
    }
}

inline void RenderJtag4LaneWaveforms(
    ParsedEdaBoard& out,
    const std::vector<JtagBitCycle>& cycles,
    const std::wstring& summaryLabel)
{
    DrawGraticule(out, -40.0f, -20.0f, 80.0f, 40.0f, 16, 4);
    out.labels.push_back({ -46.5f,  14.0f, 1.15f, EdaLayerId::TopSilk, L"TCK" });
    out.labels.push_back({ -46.5f,   5.0f, 1.15f, EdaLayerId::TopSilk, L"TMS" });
    out.labels.push_back({ -46.5f,  -5.0f, 1.15f, EdaLayerId::TopSilk, L"TDI" });
    out.labels.push_back({ -46.5f, -14.0f, 1.15f, EdaLayerId::TopSilk, L"TDO" });
    if (!summaryLabel.empty()) {
        out.labels.push_back({ -39.0f, 21.6f, 1.05f, EdaLayerId::TopSilk, summaryLabel });
    }

    const int n = static_cast<int>(std::min<size_t>(cycles.size(), JtagTapTracker::kMaxVisualCycles));
    if (n <= 0) return;
    const float step = 76.0f / static_cast<float>(n);

    for (int i = 0; i < n; ++i) {
        const float x0 = -38.0f + static_cast<float>(i) * step;
        const float xm = x0 + step * 0.5f;
        const float x1 = x0 + step;

        // Lane 1: TCK clock pulse
        out.traces.push_back({ x0, 12.2f, x0, 15.8f, 0.22f, EdaLayerId::TopSilk, 1 });
        out.traces.push_back({ x0, 15.8f, xm, 15.8f, 0.22f, EdaLayerId::TopSilk, 1 });
        out.traces.push_back({ xm, 15.8f, xm, 12.2f, 0.22f, EdaLayerId::TopSilk, 1 });
        out.traces.push_back({ xm, 12.2f, x1, 12.2f, 0.22f, EdaLayerId::TopSilk, 1 });

        // Lane 2: TMS state control
        const float yTms = cycles[static_cast<size_t>(i)].tms ? 6.8f : 3.2f;
        if (i > 0) {
            const float yPrev = cycles[static_cast<size_t>(i - 1)].tms ? 6.8f : 3.2f;
            if (yPrev != yTms) out.traces.push_back({ x0, yPrev, x0, yTms, 0.28f, EdaLayerId::InnerCopper, 2 });
        }
        out.traces.push_back({ x0, yTms, x1, yTms, 0.28f, EdaLayerId::InnerCopper, 2 });

        // Lane 3: TDI scan-in data
        const float yTdi = cycles[static_cast<size_t>(i)].tdi ? -3.2f : -6.8f;
        if (i > 0) {
            const float yPrev = cycles[static_cast<size_t>(i - 1)].tdi ? -3.2f : -6.8f;
            if (yPrev != yTdi) out.traces.push_back({ x0, yPrev, x0, yTdi, 0.28f, EdaLayerId::TopCopper, 3 });
        }
        out.traces.push_back({ x0, yTdi, x1, yTdi, 0.28f, EdaLayerId::TopCopper, 3 });

        // Lane 4: TDO expected scan-out (high/low when care bit active, mid-rail high-Z when masked/unused)
        const auto& cyc = cycles[static_cast<size_t>(i)];
        const float yTdo = cyc.hasTdo ? (cyc.tdo ? -12.2f : -15.8f) : -14.0f;
        const float wTdo = cyc.hasTdo ? 0.28f : 0.14f;
        if (i > 0) {
            const auto& prevCyc = cycles[static_cast<size_t>(i - 1)];
            const float yPrev = prevCyc.hasTdo ? (prevCyc.tdo ? -12.2f : -15.8f) : -14.0f;
            if (yPrev != yTdo) out.traces.push_back({ x0, yPrev, x0, yTdo, 0.24f, EdaLayerId::BottomCopper, 4 });
        }
        out.traces.push_back({ x0, yTdo, x1, yTdo, wTdo, EdaLayerId::BottomCopper, 4 });
    }
    out.netCount = 4;
}

// Parse BGA pin coordinate like "A1", "B12", "AA14" into (row, col)
inline bool ParseBgaBallCoord(std::string_view pin, int& outRow, int& outCol) {
    while (!pin.empty() && (pin.front() == '"' || pin.front() == ' ' || pin.front() == '\t')) pin.remove_prefix(1);
    while (!pin.empty() && (pin.back() == '"' || pin.back() == ' ' || pin.back() == '\r')) pin.remove_suffix(1);
    if (pin.rfind("PIN_", 0) == 0) pin.remove_prefix(4);
    if (pin.size() < 2 || pin.size() > 5) return false;

    static const char* kJedecRows = "ABCDEFGHJKLMNPRTUVWYZ";
    int r = 0, letters = 0;
    size_t i = 0;
    while (i < pin.size() && ((pin[i] >= 'A' && pin[i] <= 'Z') || (pin[i] >= 'a' && pin[i] <= 'z'))) {
        char up = static_cast<char>(toupper(static_cast<unsigned char>(pin[i++])));
        const char* f = std::strchr(kJedecRows, up);
        int idx = f ? static_cast<int>(f - kJedecRows) : (up - 'A');
        r = r * 20 + idx + 1;
        ++letters;
    }
    if (letters == 0 || i >= pin.size()) return false;
    int c = 0;
    while (i < pin.size() && pin[i] >= '0' && pin[i] <= '9') {
        c = c * 10 + (pin[i++] - '0');
    }
    if (c <= 0 || c > 64 || r <= 0 || r > 64) return false;
    outRow = r - 1;
    outCol = c - 1;
    return true;
}


inline bool RenderSingleScopeTrace(
    ParsedEdaBoard& out,
    const std::vector<float>& samples,
    const std::wstring& fmtTitle,
    const std::wstring& metaLabel)
{
        if (samples.size() < 2) return false;

        out.kind = EdaDocKind::KiCadSch;
        out.formatTitle = fmtTitle;
        DrawGraticule(out, -40.0f, -25.0f, 80.0f, 50.0f, 10, 8);

        float mn = samples[0], mx = samples[0];
        for (float v : samples) { mn = std::min(mn, v); mx = std::max(mx, v); }
        float span = std::max(1e-6f, mx - mn);

        // Zero-volt ground reference indicator on graticule
        float yZeroLine = std::clamp(-20.0f + ((0.0f - mn) / span) * 40.0f, -24.0f, 24.0f);
        out.traces.push_back({ -40.0f, yZeroLine, -38.5f, yZeroLine, 0.28f, EdaLayerId::InnerCopper, 2 });

        for (size_t i = 1; i < samples.size(); ++i) {
            float x0 = -39.0f + (static_cast<float>(i - 1) / static_cast<float>(samples.size() - 1)) * 78.0f;
            float x1 = -39.0f + (static_cast<float>(i)     / static_cast<float>(samples.size() - 1)) * 78.0f;
            float y0 = -20.0f + ((samples[i - 1] - mn) / span) * 40.0f;
            float y1 = -20.0f + ((samples[i]     - mn) / span) * 40.0f;
            out.traces.push_back({ x0, y0, x1, y1, 0.30f, EdaLayerId::TopCopper, 1 });
        }
        out.labels.push_back({ -39.0f, 26.8f, 1.15f, EdaLayerId::TopSilk, metaLabel });
        return true;
}

} // namespace DetailEda200

inline void DrawFieldbusSchRect(ParsedEdaBoard& out, float cx, float cy, float w, float h, float strokeW, EdaLayerId lid, int netId = 0) {
    float x0 = cx - w * 0.5f, x1 = cx + w * 0.5f;
    float y0 = cy - h * 0.5f, y1 = cy + h * 0.5f;
    out.traces.push_back({ x0, y0, x1, y0, strokeW, lid, netId });
    out.traces.push_back({ x1, y0, x1, y1, strokeW, lid, netId });
    out.traces.push_back({ x1, y1, x0, y1, strokeW, lid, netId });
    out.traces.push_back({ x0, y1, x0, y0, strokeW, lid, netId });
}

namespace DetailKiCadSymSesPos {
inline std::string LowerAsciiSymSes(std::string_view sv) { return ::Explorer2::PathUtils::ToLowerAscii(sv); }
} // namespace DetailKiCadSymSesPos

namespace DetailAltium {

using KvMap = std::unordered_map<std::string, std::string>;

inline KvMap ParsePipeRecord(std::string_view rec) {
    KvMap kv;
    size_t pos = 0;
    while (pos < rec.size()) {
        while (pos < rec.size() && (rec[pos] == '|' || rec[pos] == '\r' || rec[pos] == '\n' || rec[pos] == '\0')) ++pos;
        if (pos >= rec.size()) break;
        size_t nextBar = rec.find('|', pos);
        if (nextBar == std::string_view::npos) nextBar = rec.size();
        std::string_view item = rec.substr(pos, nextBar - pos);
        pos = nextBar + 1;
        size_t eq = item.find('=');
        if (eq == std::string_view::npos || eq == 0) continue;
        std::string k = PathUtils::ToLowerAscii(DetailEda100::TrimAsciiView(item.substr(0, eq)));
        std::string v = DetailEda100::TrimAsciiView(item.substr(eq + 1));
        if (k.rfind("%utf8%", 0) == 0 && k.size() > 6) {
            std::string baseK = k.substr(6);
            if (!v.empty()) kv[baseK] = v;
            kv[std::move(k)] = std::move(v);
        } else if (!kv.count("%utf8%" + k)) {
            kv[std::move(k)] = std::move(v);
        }
    }
    return kv;
}

inline std::string GetKvStr(const KvMap& kv, const char* keyLower) {
    auto it = kv.find(keyLower);
    return (it != kv.end()) ? it->second : std::string{};
}

inline int GetKvInt(const KvMap& kv, const char* keyLower, int defVal = 0) {
    auto it = kv.find(keyLower);
    return (it != kv.end() && !it->second.empty()) ? std::atoi(it->second.c_str()) : defVal;
}

// Altium SchDoc: 1 DXP unit = 10 mils = 0.254 mm; _FRAC is 1/100,000 of a DXP unit
inline float GetSchCoordMm(const KvMap& kv, const std::string& baseKeyLower, float defMm = 0.0f) {
    auto it = kv.find(baseKeyLower);
    if (it == kv.end() || it->second.empty()) return defMm;
    double v = std::strtod(it->second.c_str(), nullptr);
    auto itFrac = kv.find(baseKeyLower + "_frac");
    if (itFrac != kv.end() && !itFrac->second.empty()) {
        double frac = std::strtod(itFrac->second.c_str(), nullptr) / 100000.0;
        v += (v < 0.0) ? - std::fabs(frac) : std::fabs(frac);
    }
    return static_cast<float>(v * 0.254);
}

// Altium PcbDoc ASCII coordinate string: "1250mil", "25.4mm", or internal units (1e4 = 1 mil = 0.0254 mm)
inline float ParsePcbCoordStrMm(const std::string& s, float defMm = 0.0f) {
    if (s.empty()) return defMm;
    char* endp = nullptr;
    double v = std::strtod(s.c_str(), &endp);
    if (endp == s.c_str()) return defMm;
    std::string unit = PathUtils::ToLowerAscii(DetailEda100::TrimAsciiView(std::string_view(endp)));
    if (unit == "mm") return static_cast<float>(v);
    if (unit == "mil" || unit == "mils") return static_cast<float>(v * 0.0254);
    if (unit == "in" || unit == "inch") return static_cast<float>(v * 25.4);
    if (std::fabs(v) > 2500.0) return static_cast<float>(v * 0.00000254); // internal units
    return static_cast<float>(v * 0.0254); // default mils
}

inline EdaLayerId MapAltiumPcbLayerByte(uint8_t lyr) {
    if (lyr == 1) return EdaLayerId::TopCopper;
    if (lyr == 32) return EdaLayerId::BottomCopper;
    if (lyr >= 2 && lyr <= 31) return EdaLayerId::InnerCopper;
    if (lyr == 33) return EdaLayerId::TopSilk;
    if (lyr == 34) return EdaLayerId::BottomSilk;
    if (lyr == 74) return EdaLayerId::PadsThrough;
    if (lyr == 55 || lyr == 56 || (lyr >= 57 && lyr <= 72)) return EdaLayerId::EdgeCuts;
    return EdaLayerId::TopCopper;
}

inline EdaLayerId MapAltiumPcbLayerStr(const std::string& s) {
    std::string low = PathUtils::ToLowerAscii(s);
    if (low.find("bottom") != std::string::npos) {
        return (low.find("overlay") != std::string::npos || low.find("silk") != std::string::npos)
            ? EdaLayerId::BottomSilk : EdaLayerId::BottomCopper;
    }
    if (low.find("overlay") != std::string::npos || low.find("silk") != std::string::npos) return EdaLayerId::TopSilk;
    if (low.find("multi") != std::string::npos) return EdaLayerId::PadsThrough;
    if (low.find("keepout") != std::string::npos || low.find("mechanical") != std::string::npos || low.find("outline") != std::string::npos)
        return EdaLayerId::EdgeCuts;
    if (low.find("mid") != std::string::npos || low.find("internal") != std::string::npos) return EdaLayerId::InnerCopper;
    return EdaLayerId::TopCopper;
}

inline void WalkLengthPrefixedBlocks(
    const std::vector<uint8_t>& stream,
    std::vector<KvMap>& outRecords,
    bool onlyWithRecordKey,
    bool decodeBinarySchPins = false)
{
    size_t pos = 0;
    while (pos + 4 <= stream.size() && outRecords.size() < 32000) {
        uint32_t hdr = DetailEda100::RdU32LE(stream.data() + pos);
        uint32_t len = hdr & 0x00FFFFFFu;
        uint8_t flags = static_cast<uint8_t>(hdr >> 24);
        pos += 4;
        if (len == 0) continue;
        if (len > stream.size() - pos) break;
        if (flags == 0) {
            std::string_view sv(reinterpret_cast<const char*>(stream.data() + pos), len);
            if (!onlyWithRecordKey || DetailEda100::ContainsAsciiI(sv, "RECORD=")) {
                KvMap kv = ParsePipeRecord(sv);
                if (!kv.empty()) outRecords.push_back(std::move(kv));
            }
        } else if (flags == 1 && decodeBinarySchPins && len >= 22 && stream[pos] == 0x02) {
            // Altium .SchLib / .SchDoc binary pin record (record type 0x02)
            const uint8_t* p = stream.data() + pos;
            size_t descLen = p[9];
            size_t base = 10u + descLen;
            if (base + 9u <= len) {
                uint8_t pinCong = p[base + 2u];
                int16_t pinLenDxp = ::Explorer2::ByteIO::RdI16LE(p + base + 3u);
                int16_t locXDxp   = ::Explorer2::ByteIO::RdI16LE(p + base + 5u);
                int16_t locYDxp   = ::Explorer2::ByteIO::RdI16LE(p + base + 7u);

                std::string pinName, pinDesig;
                if (base + 14u <= len) {
                    size_t nameLen = p[base + 13u];
                    if (base + 14u + nameLen <= len) {
                        pinName.assign(reinterpret_cast<const char*>(p + base + 14u), nameLen);
                        size_t desigPos = base + 14u + nameLen;
                        if (desigPos < len) {
                            size_t desigLen = p[desigPos];
                            if (desigPos + 1u + desigLen <= len) {
                                pinDesig.assign(reinterpret_cast<const char*>(p + desigPos + 1u), desigLen);
                            }
                        }
                    }
                }

                KvMap kv;
                kv["record"] = "2";
                kv["isbinarypin"] = "1";
                kv["pinconglomerate"] = std::to_string(static_cast<int>(pinCong));
                kv["pinlength"] = std::to_string(static_cast<int>(pinLenDxp));
                kv["location.x"] = std::to_string(static_cast<int>(locXDxp));
                kv["location.y"] = std::to_string(static_cast<int>(locYDxp));
                if (!pinName.empty()) kv["name"] = std::move(pinName);
                if (!pinDesig.empty()) kv["designator"] = std::move(pinDesig);

                uint16_t partId = ::Explorer2::ByteIO::RdU16LE(p + 2u);
                if (partId >= 1u && partId <= 64u) {
                    kv["ownerpartid"] = std::to_string(static_cast<int>(partId));
                }
                int32_t rawOwner = DetailEda100::RdI32LE(p + 1u);
                if (rawOwner >= 0 && rawOwner < 32000 && p[3] == 0 && p[4] == 0) {
                    kv["binownerraw"] = std::to_string(rawOwner);
                }
                outRecords.push_back(std::move(kv));
            }
        }
        pos += len;
    }
}

} // namespace DetailAltium

namespace DetailCan {

struct CanFrame {
    double timestamp = 0.0;
    uint16_t channel = 1;
    uint32_t canId = 0;
    bool extId = false;
    bool isFd = false;
    bool isTx = false;
    uint8_t dlcBytes = 0;
    uint8_t data[64] = {};
};

inline uint8_t CanFdDlcToBytes(uint8_t dlc) {
    static const uint8_t kLut[16] = { 0,1,2,3,4,5,6,7,8, 12,16,20,24,32,48,64 };
    return (dlc < 16) ? kLut[dlc] : std::min<uint8_t>(dlc, 64);
}

inline bool RenderCanFramesToBoard(const std::vector<CanFrame>& frames, const wchar_t* titlePrefix, ParsedEdaBoard& out) {
    if (frames.empty()) return false;
    out.kind = EdaDocKind::KiCadSch;

    double minT = frames[0].timestamp, maxT = frames[0].timestamp;
    std::map<uint32_t, std::vector<const CanFrame*>> byId;
    for (const auto& f : frames) {
        minT = std::min(minT, f.timestamp);
        maxT = std::max(maxT, f.timestamp);
        if (byId.size() < 16 || byId.count(f.canId)) {
            byId[f.canId].push_back(&f);
        }
    }

    double spanT = maxT - minT;
    bool singleInstant = (spanT < 1e-9);
    if (singleInstant) spanT = 0.001;

    wchar_t hdrTitle[192] = {};
    swprintf_s(hdrTitle, L"%s (%zu frames, %zu IDs, %.3f..%.3f s)",
        titlePrefix, frames.size(), byId.size(), minT, maxT);
    out.formatTitle = hdrTitle;

    const float plotX0 = -24.0f, plotW = 84.0f;
    int row = 0;
    for (const auto& kv : byId) {
        uint32_t canId = kv.first;
        const auto& vec = kv.second;
        float yBase = 24.0f - row * 9.0f;

        wchar_t idHex[32] = {};
        swprintf_s(idHex, vec[0]->extId ? L"0x%08X" : L"0x%03X", canId);

        EdaComponent3D node;
        node.refDes = idHex;
        node.value = std::to_wstring(vec.size()) + (vec[0]->isFd ? L" FD" : L" Rx/Tx");
        node.footprint = L"CAN_ID_CH" + std::to_wstring(vec[0]->channel);
        node.x = -36.0f;
        node.y = yBase;
        node.widthMm = 14.0f;
        node.lengthMm = 5.5f;
        node.heightMm = 1.4f;
        node.category = EdaPartCategory::IC_Package;
        out.components.push_back(node);
        DetailEda100::AddRectOutline(out, node.x, node.y, node.widthMm, node.lengthMm, EdaLayerId::SchSymbol, 0.25f);
        out.labels.push_back({ node.x, yBase + 0.8f, 1.0f, EdaLayerId::SchLabel, node.refDes + L" (" + node.value + L")" });

        // Baseline rail
        out.traces.push_back({ plotX0, yBase - 2.6f, plotX0 + plotW, yBase - 2.6f, 0.12f, EdaLayerId::BottomSilk, 0 });

        EdaLayerId waveLayer = (row % 2 == 0) ? EdaLayerId::TopCopper : EdaLayerId::SchWire;
        float prevX = plotX0;
        float prevY = yBase - 2.2f;

        for (size_t fi = 0; fi < vec.size(); ++fi) {
            const CanFrame* f = vec[fi];
            float fx = singleInstant
                ? (plotX0 + (static_cast<float>(fi) / std::max<size_t>(1, vec.size())) * plotW)
                : (plotX0 + static_cast<float>((f->timestamp - minT) / spanT) * plotW);

            if (fx > prevX + 0.05f) {
                out.traces.push_back({ prevX, prevY, fx, prevY, 0.24f, waveLayer, row + 1 });
            }

            // Plot actual byte/bit transitions inside this frame burst
            float slotW = (vec.size() == 1) ? (plotW * 0.85f) : std::clamp(plotW / static_cast<float>(vec.size() * 2 + 2), 1.6f, 14.0f);
            int nBytes = std::max<int>(1, f->dlcBytes);
            float byteW = slotW / static_cast<float>(nBytes);
            float bx = fx;
            for (int b = 0; b < nBytes; ++b) {
                uint8_t byteVal = f->data[b];
                float levelY = (yBase - 2.2f) + (static_cast<float>(byteVal) / 255.0f) * 4.6f;
                out.traces.push_back({ bx, prevY, bx, levelY, 0.26f, EdaLayerId::TopSilk, row + 1 });
                out.traces.push_back({ bx, levelY, bx + byteW, levelY, 0.28f, waveLayer, row + 1 });
                prevY = levelY;
                bx += byteW;
            }
            prevX = std::min(plotX0 + plotW, bx);

            if (fi < 4) {
                wchar_t payTxt[48] = {};
                if (f->dlcBytes >= 2) {
                    swprintf_s(payTxt, L"t=%.4fs [%02X %02X..]", f->timestamp, f->data[0], f->data[1]);
                } else if (f->dlcBytes == 1) {
                    swprintf_s(payTxt, L"t=%.4fs [%02X]", f->timestamp, f->data[0]);
                } else {
                    swprintf_s(payTxt, L"t=%.4fs [DLC0]", f->timestamp);
                }
                out.labels.push_back({ fx + 4.0f, yBase + 2.9f, 0.85f, EdaLayerId::SchLabel, payTxt });
            }
        }
        if (prevX < plotX0 + plotW) {
            out.traces.push_back({ prevX, prevY, plotX0 + plotW, prevY, 0.22f, waveLayer, row + 1 });
        }
        ++row;
    }

    DetailEda100::AddRectOutline(out, plotX0 + plotW * 0.5f, 24.0f - (row - 1) * 4.5f, plotW + 4.0f, row * 9.0f + 4.0f, EdaLayerId::EdgeCuts, 0.32f);
    out.netCount = byId.size();
    return true;
}

} // namespace DetailCan

namespace DetailHwBin {

struct DecodedWaveSignal {
    std::wstring name;
    int bitWidth = 1;
    bool isReal = false;
    std::vector<std::pair<uint64_t, std::string>> changes; // (timestamp, state e.g. "0", "1", "x", "z", or multi-bit)
    std::vector<std::pair<uint64_t, double>> realChanges;  // (timestamp, IEEE-754 double for FST_VT_REAL)
};

inline bool RenderWaveSignalsToBoard(
    const std::vector<DecodedWaveSignal>& sigs,
    uint64_t startTime,
    uint64_t endTime,
    const std::wstring& title,
    ParsedEdaBoard& out)
{
    if (sigs.empty()) return false;
    out.kind = EdaDocKind::KiCadSch;
    out.formatTitle = title;

    uint64_t minT = startTime, maxT = endTime;
    for (const auto& s : sigs) {
        for (const auto& c : s.changes) {
            if (c.first < minT) minT = c.first;
            if (c.first > maxT) maxT = c.first;
        }
        for (const auto& rc : s.realChanges) {
            if (rc.first < minT) minT = rc.first;
            if (rc.first > maxT) maxT = rc.first;
        }
    }
    double spanT = (maxT > minT) ? static_cast<double>(maxT - minT) : 100.0;
    const float x0 = -24.0f, plotW = 84.0f;
    size_t maxRows = std::min<size_t>(sigs.size(), 24);

    for (size_t r = 0; r < maxRows; ++r) {
        const auto& s = sigs[r];
        float yMid = 24.0f - static_cast<float>(r) * 7.5f;
        EdaLayerId lid = (r % 2 == 0) ? EdaLayerId::TopCopper : EdaLayerId::SchWire;

        if (s.isReal) {
            double rMin = 1e300, rMax = -1e300;
            for (const auto& rc : s.realChanges) {
                if (std::isfinite(rc.second)) {
                    rMin = std::min(rMin, rc.second);
                    rMax = std::max(rMax, rc.second);
                }
            }
            if (rMin > rMax) { rMin = -1.0; rMax = 1.0; }
            double rSpan = (rMax - rMin > 1e-12) ? (rMax - rMin) : 1.0;

            wchar_t realLbl[160] = {};
            swprintf_s(realLbl, L"%s [%.3g..%.3g]", s.name.c_str(), rMin, rMax);
            out.labels.push_back({ -36.0f, yMid, 1.05f, EdaLayerId::SchLabel, realLbl });

            if (s.realChanges.empty()) {
                out.traces.push_back({ x0, yMid, x0 + plotW, yMid, 0.24f, lid, static_cast<int>(r + 1) });
                continue;
            }
            auto realToY = [&](double v) -> float {
                if (!std::isfinite(v)) return yMid;
                float norm = (rMax - rMin > 1e-12) ? static_cast<float>((v - rMin) / rSpan) : 0.5f;
                return (yMid - 2.3f) + std::clamp(norm, 0.0f, 1.0f) * 4.6f;
            };
            float prevX = x0;
            float prevY = realToY(s.realChanges[0].second);
            for (size_t ci = 0; ci < s.realChanges.size(); ++ci) {
                float cx = x0 + static_cast<float>((static_cast<double>(s.realChanges[ci].first - minT) / spanT)) * plotW;
                cx = std::clamp(cx, x0, x0 + plotW);
                float newY = realToY(s.realChanges[ci].second);
                if (ci > 0) {
                    out.traces.push_back({ prevX, prevY, cx, newY, 0.30f, EdaLayerId::InnerCopper, static_cast<int>(r + 1) });
                } else if (cx > prevX + 0.01f) {
                    out.traces.push_back({ prevX, prevY, cx, prevY, 0.28f, EdaLayerId::InnerCopper, static_cast<int>(r + 1) });
                }
                prevX = cx;
                prevY = newY;
            }
            if (prevX < x0 + plotW) {
                out.traces.push_back({ prevX, prevY, x0 + plotW, prevY, 0.28f, EdaLayerId::InnerCopper, static_cast<int>(r + 1) });
            }
            continue;
        }

        out.labels.push_back({ -36.0f, yMid, 1.05f, EdaLayerId::SchLabel, s.name });
        auto stateToLevel = [&](const std::string& st) -> float {
            if (st.empty()) return yMid - 2.0f;
            if (s.bitWidth == 1 && st.size() == 1) {
                char ch = st[0];
                if (ch == '1' || ch == 'H' || ch == 'h') return yMid + 2.2f;
                if (ch == '0' || ch == 'L' || ch == 'l') return yMid - 2.2f;
                return yMid; // 'X', 'Z', 'U', 'W'
            }
            // Multi-bit bus: map binary value to analog/stepped height + draw bus transition diamond
            uint32_t v = 0;
            for (char ch : st) {
                v = (v << 1) | ((ch == '1' || ch == 'H' || ch == 'h') ? 1u : 0u);
            }
            uint32_t maxV = (s.bitWidth >= 16) ? 65535u : ((1u << std::clamp(s.bitWidth, 1, 16)) - 1u);
            float norm = (maxV > 0) ? (static_cast<float>(v & maxV) / static_cast<float>(maxV)) : 0.0f;
            return (yMid - 2.2f) + norm * 4.4f;
        };

        if (s.changes.empty()) {
            out.traces.push_back({ x0, yMid - 2.2f, x0 + plotW, yMid - 2.2f, 0.24f, lid, static_cast<int>(r + 1) });
            continue;
        }

        float prevX = x0;
        float prevY = stateToLevel(s.changes[0].second);
        for (size_t ci = 0; ci < s.changes.size(); ++ci) {
            uint64_t t = s.changes[ci].first;
            float cx = x0 + static_cast<float>((static_cast<double>(t - minT) / spanT)) * plotW;
            cx = std::clamp(cx, x0, x0 + plotW);
            float newY = stateToLevel(s.changes[ci].second);
            if (cx > prevX + 0.01f) {
                out.traces.push_back({ prevX, prevY, cx, prevY, 0.28f, lid, static_cast<int>(r + 1) });
            }
            if (ci > 0 && std::fabs(newY - prevY) > 0.01f) {
                out.traces.push_back({ cx, prevY, cx, newY, 0.28f, EdaLayerId::TopSilk, static_cast<int>(r + 1) });
            }
            if (s.bitWidth > 1 && ci < 8) {
                uint32_t v = 0;
                for (char ch : s.changes[ci].second) v = (v << 1) | ((ch == '1') ? 1u : 0u);
                wchar_t busHex[24] = {};
                swprintf_s(busHex, L"0x%X", v);
                out.labels.push_back({ cx + 2.0f, yMid + 2.6f, 0.8f, EdaLayerId::TopSilk, busHex });
            }
            prevX = cx;
            prevY = newY;
        }
        if (prevX < x0 + plotW) {
            out.traces.push_back({ prevX, prevY, x0 + plotW, prevY, 0.28f, lid, static_cast<int>(r + 1) });
        }
    }

    DetailEda100::AddRectOutline(out, x0 + plotW * 0.5f, 24.0f - static_cast<float>(maxRows - 1) * 3.75f,
        plotW + 4.0f, static_cast<float>(maxRows) * 7.5f + 4.0f, EdaLayerId::EdgeCuts, 0.30f);
    out.netCount = sigs.size();
    return !out.traces.empty();
}

} // namespace DetailHwBin

namespace DetailGateTdms {

struct GatePinConn {
    std::string pinName;
    std::string netName;
    bool isOutput = false;
    bool inverted = false;
};

struct GateNode {
    std::string instName;
    std::string cellType;
    bool isPrimaryIn = false;
    bool isPrimaryOut = false;
    bool isSeq = false;
    std::vector<GatePinConn> pins;
    int level = 0;
    float x = 0.0f, y = 0.0f;
    float w = 8.0f, h = 5.0f;
};

inline bool RenderGateDagToBoard(
    const std::wstring& title,
    std::vector<GateNode>& nodes,
    ParsedEdaBoard& out)
{
    if (nodes.empty()) return false;
    const size_t n = std::min<size_t>(nodes.size(), 512);
    nodes.resize(n);

    // Map netName -> driver node index
    std::unordered_map<std::string, size_t> netDriver;
    std::set<std::string> allNets;
    for (size_t i = 0; i < n; ++i) {
        if (nodes[i].isPrimaryIn) {
            for (const auto& p : nodes[i].pins) {
                if (!p.netName.empty()) { netDriver[p.netName] = i; allNets.insert(p.netName); }
            }
        } else {
            for (const auto& p : nodes[i].pins) {
                if (!p.netName.empty()) allNets.insert(p.netName);
                if (p.isOutput && !p.netName.empty()) netDriver[p.netName] = i;
            }
        }
    }

    // Compute topological rank (cycle-safe memoized DFS; sequential latches/FFs break combinational feedback loops)
    std::vector<int> rankState(n, 0); // 0=unvisited, 1=visiting, 2=done
    auto computeRank = [&](auto& self, size_t u) -> int {
        if (rankState[u] == 2) return nodes[u].level;
        if (rankState[u] == 1) return 0; // Feedback loop broken
        rankState[u] = 1;
        if (nodes[u].isPrimaryIn) {
            nodes[u].level = 0;
        } else {
            int maxPred = 0;
            bool anyPred = false;
            for (const auto& p : nodes[u].pins) {
                if (p.isOutput) continue;
                auto it = netDriver.find(p.netName);
                if (it != netDriver.end() && it->second != u) {
                    anyPred = true;
                    // Sequential elements placed after combinational inputs unless feedback
                    int pr = (nodes[it->second].isSeq && nodes[u].isSeq) ? 0 : self(self, it->second);
                    maxPred = std::max(maxPred, pr);
                }
            }
            nodes[u].level = anyPred ? (maxPred + 1) : (nodes[u].isSeq ? 1 : 1);
        }
        rankState[u] = 2;
        return nodes[u].level;
    };

    int maxCombLevel = 1;
    for (size_t i = 0; i < n; ++i) {
        if (!nodes[i].isPrimaryOut) {
            maxCombLevel = std::max(maxCombLevel, computeRank(computeRank, i));
        }
    }
    for (size_t i = 0; i < n; ++i) {
        if (nodes[i].isPrimaryOut) {
            nodes[i].level = maxCombLevel + 1;
        }
    }

    // Group by level column and position vertically
    std::map<int, std::vector<size_t>> cols;
    for (size_t i = 0; i < n; ++i) cols[nodes[i].level].push_back(i);

    const float kColSpacing = 24.0f;
    const float kRowSpacing = 9.5f;
    for (auto& kv : cols) {
        auto& vec = kv.second;
        // Barycenter sort using fanin Y positions from earlier columns
        if (kv.first > 0) {
            std::stable_sort(vec.begin(), vec.end(), [&](size_t a, size_t b) {
                auto avgInY = [&](size_t idx) -> float {
                    float sum = 0.0f; int cnt = 0;
                    for (const auto& p : nodes[idx].pins) {
                        if (p.isOutput) continue;
                        auto it = netDriver.find(p.netName);
                        if (it != netDriver.end() && nodes[it->second].level < kv.first) {
                            sum += nodes[it->second].y; ++cnt;
                        }
                    }
                    return cnt > 0 ? (sum / static_cast<float>(cnt)) : 0.0f;
                };
                return avgInY(a) > avgInY(b);
            });
        }
        float totalH = static_cast<float>(vec.size() - 1) * kRowSpacing;
        for (size_t ri = 0; ri < vec.size(); ++ri) {
            GateNode& gn = nodes[vec[ri]];
            gn.x = static_cast<float>(kv.first) * kColSpacing;
            gn.y = totalH * 0.5f - static_cast<float>(ri) * kRowSpacing;
            size_t inCnt = 0, outCnt = 0;
            for (const auto& p : gn.pins) { if (p.isOutput) ++outCnt; else ++inCnt; }
            gn.w = (gn.isPrimaryIn || gn.isPrimaryOut) ? 5.0f : 9.0f;
            gn.h = (gn.isPrimaryIn || gn.isPrimaryOut) ? 2.4f : std::max(4.8f, static_cast<float>(std::max(inCnt, outCnt)) * 2.0f + 1.4f);
        }
    }

    out.kind = EdaDocKind::KiCadSch;
    out.formatTitle = title;

    // Record exact pin world positions per (netName) -> driver pt + sink pts
    struct PinAnchor { float x, y; int netId; };
    std::unordered_map<std::string, PinAnchor> driverPt;
    std::unordered_map<std::string, std::vector<PinAnchor>> sinkPts;
    std::unordered_map<std::string, int> netIdLookup;
    int nextNetId = 1;
    for (const auto& nm : allNets) netIdLookup[nm] = nextNetId++;

    for (size_t i = 0; i < n; ++i) {
        const GateNode& gn = nodes[i];
        float hw = gn.w * 0.5f, hh = gn.h * 0.5f;

        if (gn.isPrimaryIn) {
            // Input port chevron
            out.traces.push_back({ gn.x - hw, gn.y - hh, gn.x + hw * 0.5f, gn.y - hh, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ gn.x + hw * 0.5f, gn.y - hh, gn.x + hw, gn.y, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ gn.x + hw, gn.y, gn.x + hw * 0.5f, gn.y + hh, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ gn.x + hw * 0.5f, gn.y + hh, gn.x - hw, gn.y + hh, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ gn.x - hw, gn.y + hh, gn.x - hw, gn.y - hh, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.labels.push_back({ gn.x - hw + 0.4f, gn.y - 0.3f, 0.85f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(gn.instName) });
            if (!gn.pins.empty()) {
                int nid = netIdLookup[gn.pins[0].netName];
                driverPt[gn.pins[0].netName] = { gn.x + hw, gn.y, nid };
                out.pads.push_back({ gn.x + hw, gn.y, 0.55f, 0.55f, 0, 0, 0, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(gn.instName), L"" });
            }
            continue;
        }
        if (gn.isPrimaryOut) {
            out.traces.push_back({ gn.x - hw, gn.y, gn.x - hw * 0.5f, gn.y - hh, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ gn.x - hw * 0.5f, gn.y - hh, gn.x + hw, gn.y - hh, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ gn.x + hw, gn.y - hh, gn.x + hw, gn.y + hh, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ gn.x + hw, gn.y + hh, gn.x - hw * 0.5f, gn.y + hh, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ gn.x - hw * 0.5f, gn.y + hh, gn.x - hw, gn.y, 0.24f, EdaLayerId::SchSymbol, 0 });
            out.labels.push_back({ gn.x - hw * 0.3f, gn.y - 0.3f, 0.85f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(gn.instName) });
            if (!gn.pins.empty()) {
                int nid = netIdLookup[gn.pins[0].netName];
                sinkPts[gn.pins[0].netName].push_back({ gn.x - hw, gn.y, nid });
                out.pads.push_back({ gn.x - hw, gn.y, 0.55f, 0.55f, 0, 0, 0, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(gn.instName), L"" });
            }
            continue;
        }

        // Logic gate / DFF / standard cell block
        out.traces.push_back({ gn.x - hw, gn.y - hh, gn.x + hw, gn.y - hh, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ gn.x + hw, gn.y - hh, gn.x + hw, gn.y + hh, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ gn.x + hw, gn.y + hh, gn.x - hw, gn.y + hh, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ gn.x - hw, gn.y + hh, gn.x - hw, gn.y - hh, 0.28f, EdaLayerId::SchSymbol, 0 });
        if (gn.isSeq) {
            // IEEE clock '>' wedge on bottom-left of sequential DFF/Latch
            out.traces.push_back({ gn.x - hw, gn.y - hh + 0.6f, gn.x - hw + 1.1f, gn.y - hh + 1.2f, 0.22f, EdaLayerId::SchSymbol, 0 });
            out.traces.push_back({ gn.x - hw + 1.1f, gn.y - hh + 1.2f, gn.x - hw, gn.y - hh + 1.8f, 0.22f, EdaLayerId::SchSymbol, 0 });
        }

        std::vector<const GatePinConn*> ins, outs;
        for (const auto& p : gn.pins) {
            if (p.isOutput) outs.push_back(&p);
            else ins.push_back(&p);
        }
        for (size_t pi = 0; pi < ins.size(); ++pi) {
            float py = gn.y + hh - (gn.h / static_cast<float>(ins.size() + 1)) * static_cast<float>(pi + 1);
            float tipX = gn.x - hw - 1.6f;
            if (ins[pi]->inverted) {
                out.circles.push_back({ gn.x - hw - 0.45f, py, 0.42f, 0.20f, false, EdaLayerId::SchSymbol });
                out.traces.push_back({ tipX, py, gn.x - hw - 0.9f, py, 0.22f, EdaLayerId::SchSymbol, 0 });
            } else {
                out.traces.push_back({ tipX, py, gn.x - hw, py, 0.22f, EdaLayerId::SchSymbol, 0 });
            }
            out.pads.push_back({ tipX, py, 0.55f, 0.55f, 0, 0, 0, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(ins[pi]->pinName), PathUtils::Utf8ToWide(ins[pi]->netName) });
            if (!ins[pi]->netName.empty()) {
                sinkPts[ins[pi]->netName].push_back({ tipX, py, netIdLookup[ins[pi]->netName] });
            }
        }
        for (size_t po = 0; po < outs.size(); ++po) {
            float py = gn.y + hh - (gn.h / static_cast<float>(outs.size() + 1)) * static_cast<float>(po + 1);
            float tipX = gn.x + hw + 1.6f;
            out.traces.push_back({ gn.x + hw, py, tipX, py, 0.22f, EdaLayerId::SchSymbol, 0 });
            out.pads.push_back({ tipX, py, 0.55f, 0.55f, 0, 0, 0, EdaLayerId::SchSymbol, PathUtils::Utf8ToWide(outs[po]->pinName), PathUtils::Utf8ToWide(outs[po]->netName) });
            if (!outs[po]->netName.empty()) {
                driverPt[outs[po]->netName] = { tipX, py, netIdLookup[outs[po]->netName] };
            }
        }

        EdaComponent3D comp;
        comp.refDes = PathUtils::Utf8ToWide(gn.instName);
        comp.value = PathUtils::Utf8ToWide(gn.cellType);
        comp.footprint = PathUtils::Utf8ToWide(gn.cellType);
        comp.x = gn.x; comp.y = gn.y;
        comp.widthMm = gn.w; comp.lengthMm = gn.h; comp.heightMm = 1.2f;
        comp.pinCount = std::max(2, static_cast<int>(gn.pins.size()));
        comp.category = EdaPartCategory::IC_Package;
        out.labels.push_back({ gn.x - hw + 0.5f, gn.y + 0.3f, 0.80f, EdaLayerId::SchLabel, comp.refDes });
        out.labels.push_back({ gn.x - hw + 0.5f, gn.y - 0.9f, 0.72f, EdaLayerId::TopSilk, comp.value });
        out.components.push_back(std::move(comp));
    }

    // Route Manhattan orthogonal net wires from driver to each sink
    size_t wireIdx = 0;
    for (const auto& kv : sinkPts) {
        auto itDrv = driverPt.find(kv.first);
        if (itDrv == driverPt.end()) continue;
        const PinAnchor& src = itDrv->second;
        float channelJog = static_cast<float>((wireIdx % 7) - 3) * 0.65f;
        ++wireIdx;
        for (const auto& dst : kv.second) {
            if (out.traces.size() + 3 > 120000) break;
            float midX = (src.x + dst.x) * 0.5f + channelJog;
            if (dst.x <= src.x) {
                // Feedback wire routed over top/bottom channel
                float loopY = std::max(src.y, dst.y) + 4.2f + std::fabs(channelJog);
                out.traces.push_back({ src.x, src.y, src.x + 1.5f, src.y, 0.20f, EdaLayerId::SchWire, src.netId });
                out.traces.push_back({ src.x + 1.5f, src.y, src.x + 1.5f, loopY, 0.20f, EdaLayerId::SchWire, src.netId });
                out.traces.push_back({ src.x + 1.5f, loopY, dst.x - 1.5f, loopY, 0.20f, EdaLayerId::SchWire, src.netId });
                out.traces.push_back({ dst.x - 1.5f, loopY, dst.x - 1.5f, dst.y, 0.20f, EdaLayerId::SchWire, src.netId });
                out.traces.push_back({ dst.x - 1.5f, dst.y, dst.x, dst.y, 0.20f, EdaLayerId::SchWire, src.netId });
            } else {
                out.traces.push_back({ src.x, src.y, midX, src.y, 0.22f, EdaLayerId::SchWire, src.netId });
                out.traces.push_back({ midX, src.y, midX, dst.y, 0.22f, EdaLayerId::SchWire, src.netId });
                out.traces.push_back({ midX, dst.y, dst.x, dst.y, 0.22f, EdaLayerId::SchWire, src.netId });
            }
        }
        if (kv.second.size() >= 2 && out.vias.size() < 20000) {
            out.vias.push_back({ src.x + (kv.second[0].x - src.x) * 0.5f + channelJog, src.y, 0.55f, 0.0f, src.netId });
        }
    }

    out.netCount = std::max<size_t>(1, allNets.size());
    return !out.traces.empty();
}

inline std::string EdifIdName(const DetailSpecctra::SNode& n) {
    if (!n.isList) return n.atom;
    if (n.list.size() >= 3 && !n.list[0].isList) {
        std::string k = n.list[0].atom;
        for (char& c : k) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (k == "rename") return n.list[2].isList ? n.list[1].atom : n.list[2].atom;
        if (k == "member" && !n.list[1].isList && !n.list[2].isList) return n.list[1].atom + "[" + n.list[2].atom + "]";
    }
    return (!n.list.empty() && !n.list[0].isList) ? n.list[0].atom : "";
}

inline std::string LowerAsciiCopy(std::string_view sv) { return ::Explorer2::PathUtils::ToLowerAscii(sv); }

} // namespace DetailGateTdms

namespace DetailMfg69 {

inline void HatchPolygonHorizontal(
    const std::vector<std::pair<float, float>>& pts,
    EdaLayerId layer,
    int netId,
    ParsedEdaBoard& out)
{
    if (pts.size() < 3 || out.traces.size() > 55000) return;
    float minY = pts[0].second, maxY = pts[0].second;
    float minX = pts[0].first,  maxX = pts[0].first;
    for (const auto& p : pts) {
        minX = std::min(minX, p.first);  maxX = std::max(maxX, p.first);
        minY = std::min(minY, p.second); maxY = std::max(maxY, p.second);
    }
    const float spanY = maxY - minY;
    if (spanY < 0.05f || (maxX - minX) < 0.05f) return;

    const float pitch = std::clamp(spanY / 20.0f, 0.28f, 1.25f);
    const float strokeW = pitch * 0.65f;
    std::vector<float> xs;
    xs.reserve(16);
    for (float y = minY + pitch * 0.5f; y < maxY - 1e-4f && out.traces.size() < 58000; y += pitch) {
        xs.clear();
        for (size_t i = 0; i < pts.size(); ++i) {
            const auto& a = pts[i];
            const auto& b = pts[(i + 1) % pts.size()];
            if ((a.second <= y && b.second > y) || (b.second <= y && a.second > y)) {
                float t = (y - a.second) / (b.second - a.second);
                xs.push_back(a.first + t * (b.first - a.first));
            }
        }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            if (xs[k + 1] - xs[k] > 0.02f) {
                out.traces.push_back({ xs[k], y, xs[k + 1], y, strokeW, layer, netId });
            }
        }
    }
}

inline bool HasOdbPathSignature(std::string_view lowPath) {
    return lowPath.find("matrix/matrix") != std::string_view::npos ||
           (lowPath.find("steps/") != std::string_view::npos &&
            (lowPath.find("/layers/") != std::string_view::npos ||
             lowPath.find("/profile") != std::string_view::npos ||
             lowPath.find("/eda/data") != std::string_view::npos));
}

inline bool TarBufferHasOdbSignature(const uint8_t* tarData, size_t tarSize) {
    if (!DetailEda100::LooksLikeTarBlock(tarData, tarSize)) return false;
    size_t pos = 0;
    std::string gnuLong;
    int entriesChecked = 0;
    while (pos + 512 <= tarSize && entriesChecked < 512) {
        const uint8_t* hdr = tarData + pos;
        bool allZero = true;
        for (size_t i = 0; i < 512; ++i) {
            if (hdr[i] != 0) { allZero = false; break; }
        }
        if (allZero) break;
        uint64_t fsize = 0;
        for (size_t i = 124; i < 136; ++i) {
            if (hdr[i] >= '0' && hdr[i] <= '7') fsize = (fsize << 3) | static_cast<uint64_t>(hdr[i] - '0');
        }
        char tf = static_cast<char>(hdr[156]);
        std::string name;
        if (!gnuLong.empty()) {
            name = std::move(gnuLong);
            gnuLong.clear();
        } else {
            size_t nl = 0;
            while (nl < 100 && hdr[nl] != 0) ++nl;
            name.assign(reinterpret_cast<const char*>(hdr), nl);
            if (std::memcmp(hdr + 257, "ustar", 5) == 0 && hdr[345] != 0) {
                size_t pl = 0;
                while (pl < 155 && hdr[345 + pl] != 0) ++pl;
                name = std::string(reinterpret_cast<const char*>(hdr + 345), pl) + "/" + name;
            }
        }
        pos += 512;
        if (fsize > tarSize - pos) break;
        if (tf == 'L' && fsize > 0 && fsize < 4096) {
            gnuLong.assign(reinterpret_cast<const char*>(tarData + pos), static_cast<size_t>(fsize));
            while (!gnuLong.empty() && gnuLong.back() == '\0') gnuLong.pop_back();
        } else {
            std::string low = PathUtils::ToLowerAscii(name);
            for (char& ch : low) if (ch == '\\') ch = '/';
            if (HasOdbPathSignature(low)) return true;
        }
        size_t padded = static_cast<size_t>((fsize + 511ULL) & ~511ULL);
        if (padded > tarSize - pos) break;
        pos += padded;
        ++entriesChecked;
    }
    return false;
}

inline void ExtractOdbTarEntriesFull(
    const uint8_t* tarData,
    size_t tarSize,
    std::vector<std::pair<std::string, std::string>>& outFiles)
{
    size_t pos = 0;
    std::string gnuLongName;
    while (pos + 512 <= tarSize && outFiles.size() < 1500) {
        const uint8_t* hdr = tarData + pos;
        bool allZero = true;
        for (size_t i = 0; i < 512; ++i) {
            if (hdr[i] != 0) { allZero = false; break; }
        }
        if (allZero) break;

        uint64_t fsize = 0;
        for (size_t i = 124; i < 136; ++i) {
            if (hdr[i] >= '0' && hdr[i] <= '7') fsize = (fsize << 3) | static_cast<uint64_t>(hdr[i] - '0');
        }
        char typeflag = static_cast<char>(hdr[156]);
        std::string name;
        if (!gnuLongName.empty()) {
            name = std::move(gnuLongName);
            gnuLongName.clear();
        } else {
            size_t nlen = 0;
            while (nlen < 100 && hdr[nlen] != 0) ++nlen;
            name.assign(reinterpret_cast<const char*>(hdr), nlen);
            if (std::memcmp(hdr + 257, "ustar", 5) == 0 && hdr[345] != 0) {
                size_t plen = 0;
                while (plen < 155 && hdr[345 + plen] != 0) ++plen;
                name = std::string(reinterpret_cast<const char*>(hdr + 345), plen) + "/" + name;
            }
        }
        pos += 512;
        if (fsize > tarSize - pos) break;

        if (typeflag == 'L' && fsize > 0 && fsize < 4096) {
            gnuLongName.assign(reinterpret_cast<const char*>(tarData + pos), static_cast<size_t>(fsize));
            while (!gnuLongName.empty() && gnuLongName.back() == '\0') gnuLongName.pop_back();
        } else if ((typeflag == '0' || typeflag == '\0') && fsize > 0 && fsize <= 32 * 1024 * 1024) {
            std::string low = PathUtils::ToLowerAscii(name);
            for (char& ch : low) if (ch == '\\') ch = '/';
            if (low.find("matrix") != std::string::npos ||
                low.find("profile") != std::string::npos ||
                low.find("features") != std::string::npos ||
                low.find("components") != std::string::npos ||
                low.find("eda/data") != std::string::npos ||
                low.find("misc/info") != std::string::npos)
            {
                outFiles.push_back({ low, DetailEda100::UnpackMaybeCompressedText(tarData + pos, static_cast<size_t>(fsize)) });
            }
        }
        size_t padded = static_cast<size_t>((fsize + 511ULL) & ~511ULL);
        if (padded > tarSize - pos) break;
        pos += padded;
    }
}

} // namespace DetailMfg69


namespace DetailEasyEda {

inline bool IsBalancedJsonText(std::string_view sv) {
    int braces = 0, brackets = 0;
    bool inStr = false;
    bool sawStruct = false;
    for (size_t i = 0; i < sv.size(); ++i) {
        unsigned char uc = static_cast<unsigned char>(sv[i]);
        if (uc == 0) return false;
        char c = sv[i];
        if (inStr) {
            if (c == '\\' && i + 1 < sv.size()) { ++i; continue; }
            if (c == '"') inStr = false;
        } else {
            if (c == '"') inStr = true;
            else if (c == '{') { ++braces; sawStruct = true; }
            else if (c == '}') { if (--braces < 0) return false; }
            else if (c == '[') { ++brackets; sawStruct = true; }
            else if (c == ']') { if (--brackets < 0) return false; }
        }
    }
    return sawStruct && !inStr && braces == 0 && brackets == 0;
}

inline std::vector<std::string_view> SplitByChar(std::string_view sv, char delim) {
    std::vector<std::string_view> out;
    size_t p = 0;
    while (p <= sv.size()) {
        size_t e = sv.find(delim, p);
        if (e == std::string_view::npos) {
            out.push_back(sv.substr(p));
            break;
        }
        out.push_back(sv.substr(p, e - p));
        p = e + 1;
    }
    return out;
}

inline std::vector<std::string_view> SplitBySubstr(std::string_view sv, std::string_view delim) {
    std::vector<std::string_view> out;
    size_t p = 0;
    while (p <= sv.size()) {
        size_t e = sv.find(delim, p);
        if (e == std::string_view::npos) {
            out.push_back(sv.substr(p));
            break;
        }
        out.push_back(sv.substr(p, e - p));
        p = e + delim.size();
    }
    return out;
}

inline float ParseF32Sv(std::string_view sv, float defVal = 0.0f) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
    if (sv.empty()) return defVal;
    char b[64] = {};
    std::memcpy(b, sv.data(), std::min<size_t>(sv.size(), 63));
    char* endp = nullptr;
    float v = std::strtof(b, &endp);
    return (endp != b && std::isfinite(v)) ? v : defVal;
}

inline void ParseSpaceFloats(std::string_view sv, std::vector<float>& outNums) {
    outNums.clear();
    size_t p = 0;
    while (p < sv.size()) {
        while (p < sv.size() && (sv[p] == ' ' || sv[p] == '\t' || sv[p] == ',' ||
               sv[p] == 'M' || sv[p] == 'L' || sv[p] == 'Z' || sv[p] == 'z' || sv[p] == 'm' || sv[p] == 'l')) ++p;
        if (p >= sv.size()) break;
        // Skip SVG arc flags if 'A' encountered; caller handles 'A' separately when needed
        if ((sv[p] >= 'A' && sv[p] <= 'Z') || (sv[p] >= 'a' && sv[p] <= 'z')) { ++p; continue; }
        size_t e = p;
        while (e < sv.size() && ((sv[e] >= '0' && sv[e] <= '9') || sv[e] == '.' || sv[e] == '-' || sv[e] == '+' || sv[e] == 'e' || sv[e] == 'E')) ++e;
        if (e == p) { ++p; continue; }
        outNums.push_back(ParseF32Sv(sv.substr(p, e - p)));
        p = e;
    }
}

inline EdaLayerId MapEasyEdaStdLayer(int layerId) {
    if (layerId == 1) return EdaLayerId::TopCopper;
    if (layerId == 2) return EdaLayerId::BottomCopper;
    if (layerId == 3) return EdaLayerId::TopSilk;
    if (layerId == 4) return EdaLayerId::BottomSilk;
    if (layerId == 10 || layerId == 12 || layerId == 99 || layerId == 100) return EdaLayerId::EdgeCuts;
    if (layerId == 11) return EdaLayerId::PadsThrough;
    if (layerId >= 21 && layerId <= 52) return EdaLayerId::InnerCopper;
    return EdaLayerId::TopSilk;
}

inline EdaLayerId MapEasyEdaProLayer(int layerId) {
    // EasyEDA Pro layer IDs: 1=TopLayer, 2=BottomLayer, 3=TopSilk, 4=BottomSilk, 11=BoardOutline, 12=MultiLayer, 15..46=InnerLayer
    if (layerId == 1) return EdaLayerId::TopCopper;
    if (layerId == 2) return EdaLayerId::BottomCopper;
    if (layerId == 3) return EdaLayerId::TopSilk;
    if (layerId == 4) return EdaLayerId::BottomSilk;
    if (layerId == 11 || layerId == 47 || layerId == 48) return EdaLayerId::EdgeCuts;
    if (layerId == 12) return EdaLayerId::PadsThrough;
    if (layerId >= 15 && layerId <= 46) return EdaLayerId::InnerCopper;
    return EdaLayerId::TopCopper;
}

// EasyEDA documents and JSON-Lines records are read with the shared JSON reader (JsonDom.h)
using JVal = ::Explorer2::Json::Value;

// EasyEDA's readers treat a JSON boolean as the number 1 / 0 (e.g. layer visibility flags)
inline void BoolsAsNumbers(JVal& v, int depth = 0) {
    if (depth > 64) return;
    if (v.kind == JVal::Kind::Bool) v.numVal = v.boolVal ? 1.0 : 0.0;
    for (auto& e : v.arrVal) BoolsAsNumbers(e, depth + 1);
    for (auto& kv : v.objVal) BoolsAsNumbers(kv.second, depth + 1);
}

// One JSON value starting at sv[p] (leading whitespace skipped); p moves past it
inline bool ParseJsonValue(std::string_view sv, size_t& p, int depth, JVal& out) {
    if (p > sv.size()) return false;
    ::Explorer2::Json::Limits limits;
    limits.maxDepth = std::max(1, 48 - depth);
    limits.maxNodes = 8000000;
    ::Explorer2::Json::Parser parser(sv.substr(p), limits);
    if (!parser.ParseValue(out)) return false;
    p += parser.Position();
    BoolsAsNumbers(out);
    return true;
}

} // namespace DetailEasyEda

} // namespace Formats

namespace DetailKiCadSExpr {

// Fast S-expression helpers for KiCad (.kicad_pcb, .kicad_sch, .kicad_mod)
inline size_t FindMatchingParen(std::string_view sv, size_t openPos) {
    int depth = 0;
    bool inQuotes = false;
    for (size_t i = openPos; i < sv.size(); ++i) {
        char c = sv[i];
        if (c == '"' && (i == 0 || sv[i - 1] != '\\')) {
            inQuotes = !inQuotes;
        } else if (!inQuotes) {
            if (c == '(') ++depth;
            else if (c == ')') {
                --depth;
                if (depth == 0) return i;
            }
        }
    }
    return std::string_view::npos;
}

inline bool ExtractSExprFloats(std::string_view block, std::string_view key, float& f1, float& f2, float* f3 = nullptr) {
    size_t pos = block.find(key);
    if (pos == std::string_view::npos) return false;
    pos += key.size();
    char buf[96] = {};
    size_t end = block.find(')', pos);
    if (end == std::string_view::npos) return false;
    size_t len = std::min<size_t>(end - pos, 95);
    std::memcpy(buf, block.data() + pos, len);
    char* p = buf;
    char* next = nullptr;
    f1 = std::strtof(p, &next);
    if (next == p) return false;
    p = next;
    f2 = std::strtof(p, &next);
    if (f3 && next != p) {
        p = next;
        *f3 = std::strtof(p, &next);
    }
    return true;
}

inline bool ExtractSExprSingleFloat(std::string_view block, std::string_view key, float& outVal) {
    size_t pos = block.find(key);
    if (pos == std::string_view::npos) return false;
    pos += key.size();
    size_t end = block.find(')', pos);
    if (end == std::string_view::npos) return false;
    char buf[48] = {};
    std::memcpy(buf, block.data() + pos, std::min<size_t>(end - pos, 47));
    char* ep = nullptr;
    float v = std::strtof(buf, &ep);
    if (ep == buf) return false;
    outVal = v;
    return true;
}

inline std::string ExtractSExprQuotedOrAtom(std::string_view block, std::string_view prefix) {
    size_t pos = block.find(prefix);
    if (pos == std::string_view::npos) return "";
    pos += prefix.size();
    while (pos < block.size() && (block[pos] == ' ' || block[pos] == '\t')) ++pos;
    if (pos >= block.size()) return "";
    if (block[pos] == '"') {
        ++pos;
        size_t endQ = block.find('"', pos);
        if (endQ == std::string_view::npos) return "";
        return std::string(block.substr(pos, endQ - pos));
    }
    size_t endA = pos;
    while (endA < block.size() && block[endA] != ' ' && block[endA] != ')' && block[endA] != '\r' && block[endA] != '\n') {
        ++endA;
    }
    return std::string(block.substr(pos, endA - pos));
}

inline EdaLayerId MapKiCadLayer(std::string_view lyr) {
    if (lyr.find("F.Cu") != std::string_view::npos || lyr.find("*.Cu") != std::string_view::npos) return EdaLayerId::TopCopper;
    if (lyr.find("B.Cu") != std::string_view::npos) return EdaLayerId::BottomCopper;
    if (lyr.find("Edge.Cuts") != std::string_view::npos) return EdaLayerId::EdgeCuts;
    if (lyr.find("B.Silk") != std::string_view::npos || lyr.find("B.Fab") != std::string_view::npos) return EdaLayerId::BottomSilk;
    if (lyr.find(".Cu") != std::string_view::npos) return EdaLayerId::InnerCopper;
    return EdaLayerId::TopSilk;
}

} // namespace DetailKiCadSExpr

namespace DetailKiCadLegacy {

struct LegSeg  { float x1 = 0, y1 = 0, x2 = 0, y2 = 0, wMm = 0.22f; int unit = 0, conv = 0; };
struct LegCirc { float cx = 0, cy = 0, r = 20.0f, wMm = 0.22f; bool filled = false; int unit = 0, conv = 0; };
struct LegPin  {
    std::string name;
    std::string number;
    float x = 0, y = 0, len = 100.0f; // mils, +Y UP in symbol space
    char dir = 'R';                   // U, D, L, R (direction from tip INTO symbol body)
    std::string shape;                // I, C, CI, N, X, ...
    int unit = 0, conv = 0;
    bool hidden = false;
};

struct LegSymDef {
    std::string name;
    std::string refPrefix = "U";
    bool drawPinNums  = true;
    bool drawPinNames = true;
    bool isPowerSym   = false;
    std::vector<std::string> aliases;
    std::vector<LegSeg> segs;
    std::vector<LegCirc> circs;
    std::vector<LegPin> pins;
};

inline std::string_view StripUtf8BomView(std::string_view sv) {
    if (sv.size() >= 3 &&
        static_cast<uint8_t>(sv[0]) == 0xEFu &&
        static_cast<uint8_t>(sv[1]) == 0xBBu &&
        static_cast<uint8_t>(sv[2]) == 0xBFu)
    {
        sv.remove_prefix(3);
    }
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    return sv;
}

inline void TokenizeLegacyLine(std::string_view ln, std::vector<std::string>& out) {
    out.clear();
    size_t i = 0;
    while (i < ln.size()) {
        while (i < ln.size() && static_cast<unsigned char>(ln[i]) <= 0x20u) ++i;
        if (i >= ln.size()) break;
        std::string tok;
        if (ln[i] == '"') {
            ++i;
            while (i < ln.size() && ln[i] != '"') {
                if (ln[i] == '\\' && i + 1 < ln.size()) ++i;
                tok.push_back(ln[i++]);
            }
            if (i < ln.size()) ++i;
        } else {
            while (i < ln.size() && static_cast<unsigned char>(ln[i]) > 0x20u) {
                tok.push_back(ln[i++]);
            }
        }
        out.push_back(std::move(tok));
    }
}

inline std::string NormalizeSymLookupKey(std::string_view raw) {
    size_t colon = raw.rfind(':');
    if (colon != std::string_view::npos && colon + 1 < raw.size()) {
        raw.remove_prefix(colon + 1);
    }
    std::string s(raw);
    for (char& c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}

inline bool ParseLegacySymLibraryMap(std::string_view txt, std::unordered_map<std::string, LegSymDef>& outMap, std::vector<std::string>* outOrder = nullptr) {
    txt = StripUtf8BomView(txt);
    if (txt.rfind("EESchema-LIBRARY", 0) != 0) return false;
    for (unsigned char c : txt) {
        if (c == 0) return false;
    }

    bool inDef = false, inDraw = false;
    LegSymDef cur;
    std::vector<std::string> toks;
    size_t parsedSyms = 0;

    size_t pos = 0;
    while (pos < txt.size() && parsedSyms < 512) {
        size_t eol = txt.find('\n', pos);
        if (eol == std::string_view::npos) eol = txt.size();
        std::string_view ln = txt.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        if (ln.empty() || ln.front() == '#') continue;

        if (!inDef) {
            if (ln.rfind("DEF ", 0) == 0) {
                TokenizeLegacyLine(ln, toks);
                if (toks.size() >= 3) {
                    cur = LegSymDef{};
                    cur.name = toks[1];
                    if (!cur.name.empty() && cur.name[0] == '~') cur.name.erase(cur.name.begin());
                    cur.refPrefix = toks[2];
                    if (toks.size() >= 6) cur.drawPinNums  = (toks[5] != "N");
                    if (toks.size() >= 7) cur.drawPinNames = (toks[6] != "N");
                    if (toks.size() >= 10 && toks[9] == "P") cur.isPowerSym = true;
                    inDef = true;
                    inDraw = false;
                }
            }
            continue;
        }

        if (ln == "ENDDEF") {
            if (!cur.name.empty()) {
                std::string kMain = NormalizeSymLookupKey(cur.name);
                if (outOrder && !outMap.count(kMain)) outOrder->push_back(kMain);
                std::vector<std::string> aliases = cur.aliases;
                outMap[kMain] = cur;
                for (const auto& al : aliases) {
                    std::string kAl = NormalizeSymLookupKey(al);
                    if (!kAl.empty()) outMap[kAl] = cur;
                }
                ++parsedSyms;
            }
            inDef = false;
            inDraw = false;
            continue;
        }
        if (ln == "DRAW")    { inDraw = true;  continue; }
        if (ln == "ENDDRAW") { inDraw = false; continue; }

        if (!inDraw) {
            if (ln.rfind("ALIAS ", 0) == 0) {
                TokenizeLegacyLine(ln, toks);
                for (size_t i = 1; i < toks.size(); ++i) cur.aliases.push_back(toks[i]);
            }
            continue;
        }

        TokenizeLegacyLine(ln, toks);
        if (toks.empty()) continue;
        const std::string& cmd = toks[0];

        auto thickToMm = [](int thMil) -> float {
            return (thMil > 0) ? std::clamp(static_cast<float>(thMil) * 0.0254f, 0.16f, 1.4f) : 0.24f;
        };

        if (cmd == "S" && toks.size() >= 8) {
            // S startx starty endx endy unit convert thickness [fill]
            float x1 = static_cast<float>(std::atof(toks[1].c_str()));
            float y1 = static_cast<float>(std::atof(toks[2].c_str()));
            float x2 = static_cast<float>(std::atof(toks[3].c_str()));
            float y2 = static_cast<float>(std::atof(toks[4].c_str()));
            int u = std::atoi(toks[5].c_str());
            int c = std::atoi(toks[6].c_str());
            float w = thickToMm(std::atoi(toks[7].c_str()));
            cur.segs.push_back({ x1, y1, x2, y1, w, u, c });
            cur.segs.push_back({ x2, y1, x2, y2, w, u, c });
            cur.segs.push_back({ x2, y2, x1, y2, w, u, c });
            cur.segs.push_back({ x1, y2, x1, y1, w, u, c });
        } else if ((cmd == "P" || cmd == "B") && toks.size() >= 7) {
            // P count unit convert thickness x0 y0 x1 y1 ... [fill]
            int cnt = std::clamp(std::atoi(toks[1].c_str()), 0, 256);
            int u = std::atoi(toks[2].c_str());
            int c = std::atoi(toks[3].c_str());
            float w = thickToMm(std::atoi(toks[4].c_str()));
            std::vector<std::pair<float, float>> pts;
            for (int i = 0; i < cnt && (5 + i * 2 + 1) < static_cast<int>(toks.size()); ++i) {
                pts.push_back({
                    static_cast<float>(std::atof(toks[static_cast<size_t>(5 + i * 2)].c_str())),
                    static_cast<float>(std::atof(toks[static_cast<size_t>(5 + i * 2 + 1)].c_str()))
                });
            }
            for (size_t i = 1; i < pts.size(); ++i) {
                cur.segs.push_back({ pts[i - 1].first, pts[i - 1].second, pts[i].first, pts[i].second, w, u, c });
            }
        } else if (cmd == "C" && toks.size() >= 7) {
            // C posx posy radius unit convert thickness [fill]
            float cx = static_cast<float>(std::atof(toks[1].c_str()));
            float cy = static_cast<float>(std::atof(toks[2].c_str()));
            float r  = std::max(2.0f, static_cast<float>(std::atof(toks[3].c_str())));
            int u = std::atoi(toks[4].c_str());
            int c = std::atoi(toks[5].c_str());
            float w = thickToMm(std::atoi(toks[6].c_str()));
            bool fill = (toks.size() >= 8 && toks[7] == "F");
            cur.circs.push_back({ cx, cy, r, w, fill, u, c });
        } else if (cmd == "A" && toks.size() >= 9) {
            // A posx posy radius start_tenths end_tenths unit convert thickness [fill] [sx sy ex ey]
            float cx = static_cast<float>(std::atof(toks[1].c_str()));
            float cy = static_cast<float>(std::atof(toks[2].c_str()));
            float r  = std::max(2.0f, static_cast<float>(std::atof(toks[3].c_str())));
            double aStart = std::atof(toks[4].c_str()) * 0.1 * (3.14159265358979323846 / 180.0);
            double aEnd   = std::atof(toks[5].c_str()) * 0.1 * (3.14159265358979323846 / 180.0);
            int u = std::atoi(toks[6].c_str());
            int c = std::atoi(toks[7].c_str());
            float w = thickToMm(std::atoi(toks[8].c_str()));
            double sweep = aEnd - aStart;
            while (sweep >  3.14159265358979323846) sweep -= 6.2831853071795864769;
            while (sweep < -3.14159265358979323846) sweep += 6.2831853071795864769;
            if (std::fabs(sweep) < 1e-3) sweep = 3.14159265358979323846;
            int segs = std::clamp(static_cast<int>(std::ceil(std::fabs(sweep) / 0.26)), 4, 24);
            float px = 0, py = 0;
            for (int s = 0; s <= segs; ++s) {
                double ang = aStart + sweep * (static_cast<double>(s) / segs);
                float nx = cx + static_cast<float>(std::cos(ang)) * r;
                float ny = cy + static_cast<float>(std::sin(ang)) * r;
                if (s > 0) cur.segs.push_back({ px, py, nx, ny, w, u, c });
                px = nx; py = ny;
            }
        } else if (cmd == "X" && toks.size() >= 12) {
            // X name num posx posy length dir snum snom unit convert etype [shape]
            LegPin p;
            p.name = (toks[1] == "~") ? "" : toks[1];
            p.number = (toks[2] == "~") ? "" : toks[2];
            p.x = static_cast<float>(std::atof(toks[3].c_str()));
            p.y = static_cast<float>(std::atof(toks[4].c_str()));
            p.len = std::max(0.0f, static_cast<float>(std::atof(toks[5].c_str())));
            p.dir = toks[6].empty() ? 'R' : static_cast<char>(std::toupper(static_cast<unsigned char>(toks[6][0])));
            p.unit = std::atoi(toks[9].c_str());
            p.conv = std::atoi(toks[10].c_str());
            if (toks.size() >= 13) {
                p.shape = toks[12];
                if (p.shape.find('N') != std::string::npos) p.hidden = true;
            }
            cur.pins.push_back(std::move(p));
        }
    }
    return parsedSyms > 0;
}

inline bool GetBuiltinLegacySym(std::string_view rawName, LegSymDef& out) {
    out = LegSymDef{};
    std::string k = NormalizeSymLookupKey(rawName);
    out.name = k;

    if (k == "GND" || k == "GNDA" || k == "GNDD" || k == "GNDREF" || k == "AGND" || k == "DGND" || k == "EARTH") {
        out.isPowerSym = true;
        out.segs = {
            {   0,   0,   0, -50, 0.24f, 0, 0 },
            { -50, -50,  50, -50, 0.26f, 0, 0 },
            {  50, -50,   0,-100, 0.24f, 0, 0 },
            {   0,-100, -50, -50, 0.24f, 0, 0 }
        };
        out.pins = { { "GND", "1", 0, 0, 0, 'D', "N", 0, 0, true } };
        return true;
    }
    if (k == "VCC" || k == "VDD" || k == "+3V3" || k == "+3.3V" || k == "+5V" || k == "+12V" || k == "+24V" || k == "VBAT" || k == "VBUS") {
        out.isPowerSym = true;
        out.segs = {
            {   0,  0,   0,  70, 0.24f, 0, 0 },
            { -45, 70,  45,  70, 0.28f, 0, 0 }
        };
        out.pins = { { k, "1", 0, 0, 0, 'U', "N", 0, 0, true } };
        return true;
    }
    if (k == "R" || k == "R_SMALL" || k == "RES" || k == "R_US") {
        out.refPrefix = "R";
        out.segs = {
            { -40,  100,  40,  100, 0.25f, 0, 0 },
            {  40,  100,  40, -100, 0.25f, 0, 0 },
            {  40, -100, -40, -100, 0.25f, 0, 0 },
            { -40, -100, -40,  100, 0.25f, 0, 0 }
        };
        out.pins = {
            { "", "1", 0,  150, 50, 'D', "", 0, 0, false },
            { "", "2", 0, -150, 50, 'U', "", 0, 0, false }
        };
        return true;
    }
    if (k == "C" || k == "C_SMALL" || k == "CAP" || k == "CP" || k == "CP1") {
        out.refPrefix = "C";
        out.segs = {
            { -80,  30,  80,  30, 0.28f, 0, 0 },
            { -80, -30,  80, -30, 0.28f, 0, 0 }
        };
        if (k == "CP" || k == "CP1") {
            out.segs.push_back({ -55, 55, -25, 55, 0.20f, 0, 0 });
            out.segs.push_back({ -40, 40, -40, 70, 0.20f, 0, 0 });
        }
        out.pins = {
            { "", "1", 0,  150, 120, 'D', "", 0, 0, false },
            { "", "2", 0, -150, 120, 'U', "", 0, 0, false }
        };
        return true;
    }
    if (k == "L" || k == "L_SMALL" || k == "INDUCTOR") {
        out.refPrefix = "L";
        for (int i = 0; i < 4; ++i) {
            float y0 = 100.0f - static_cast<float>(i) * 50.0f;
            float ym = y0 - 25.0f;
            out.segs.push_back({ 0, y0, 35, ym, 0.24f, 0, 0 });
            out.segs.push_back({ 35, ym, 0, y0 - 50.0f, 0.24f, 0, 0 });
        }
        out.pins = {
            { "", "1", 0,  150, 50, 'D', "", 0, 0, false },
            { "", "2", 0, -150, 50, 'U', "", 0, 0, false }
        };
        return true;
    }
    if (k == "D" || k == "LED" || k == "D_SCHOTTKY" || k == "D_ZENER" || k == "DIODE") {
        out.refPrefix = "D";
        out.segs = {
            {  50, -50,  50,  50, 0.26f, 0, 0 },
            {  50,   0, -50,  50, 0.26f, 0, 0 },
            { -50,  50, -50, -50, 0.26f, 0, 0 },
            { -50, -50,  50,   0, 0.26f, 0, 0 }
        };
        if (k == "D_ZENER") {
            out.segs.push_back({ -50,  50, -70,  70, 0.24f, 0, 0 });
            out.segs.push_back({ -50, -50, -30, -70, 0.24f, 0, 0 });
        }
        out.pins = {
            { "K", "1", -150, 0, 100, 'R', "", 0, 0, false },
            { "A", "2",  150, 0, 100, 'L', "", 0, 0, false }
        };
        return true;
    }
    if (k.rfind("Q_NPN", 0) == 0 || k.rfind("Q_PNP", 0) == 0 || k == "NPN" || k == "PNP") {
        out.refPrefix = "Q";
        out.circs = { { 20, 0, 110, 0.22f, false, 0, 0 } };
        out.segs = {
            { -50,  70, -50, -70, 0.30f, 0, 0 },
            { -50,  25,  50, 100, 0.24f, 0, 0 },
            { -50, -25,  50,-100, 0.24f, 0, 0 }
        };
        out.pins = {
            { "B", "1", -200,    0, 150, 'R', "", 0, 0, false },
            { "C", "2",   50,  200, 100, 'D', "", 0, 0, false },
            { "E", "3",   50, -200, 100, 'U', "", 0, 0, false }
        };
        return true;
    }
    if (k.rfind("OPAMP", 0) == 0 || k == "LM358" || k == "TL072" || k == "MCP6002") {
        out.refPrefix = "U";
        out.segs = {
            { -150,  150, -150, -150, 0.28f, 0, 0 },
            { -150, -150,  150,    0, 0.28f, 0, 0 },
            {  150,    0, -150,  150, 0.28f, 0, 0 }
        };
        out.pins = {
            { "+", "3", -250,   75, 100, 'R', "", 1, 0, false },
            { "-", "2", -250,  -75, 100, 'R', "", 1, 0, false },
            { "",  "1",  250,    0, 100, 'L', "", 1, 0, false }
        };
        return true;
    }
    return false;
}

inline void LoadCompanionLegacyLibs(const std::wstring& schFullPath, std::unordered_map<std::string, LegSymDef>& symMap) {
    if (schFullPath.empty()) return;
    size_t sep = schFullPath.find_last_of(L"\\/");
    if (sep == std::wstring::npos) return;
    std::wstring dir = schFullPath.substr(0, sep);

    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW((dir + L"\\*.lib").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    int loadedLibs = 0;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        std::wstring libPath = dir + L"\\" + fd.cFileName;
        std::vector<uint8_t> raw;
        if (PathUtils::ReadFileBytes(libPath, 8 * 1024 * 1024, raw) && raw.size() >= 24) {
            std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
            if (ParseLegacySymLibraryMap(sv, symMap, nullptr)) {
                if (++loadedLibs >= 12) break;
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

inline void StampLegacySymInstance(
    const LegSymDef* sym,
    float posXMil, float posYMil,
    int ma, int mb, int mc, int md,
    int placedUnit, int placedConv,
    const std::wstring& refDes,
    const std::wstring& valStr,
    const std::wstring& fpStr,
    bool emitCompEntry,
    ParsedEdaBoard& out)
{
    constexpr float kMilToMm = 0.0254f;
    // Transform symbol-local (lx, ly) [+Y UP in mils] -> sheet (sx, sy) [+Y DOWN in mils] -> board (bx, by) [+Y UP in mm]:
    //   sheetX = posX + ma * lx + mb * ly
    //   sheetY = posY + mc * lx + md * ly
    //   bx = sheetX * kMilToMm,  by = -sheetY * kMilToMm
    auto xfPt = [&](float lx, float ly, float& outBx, float& outBy) {
        float sx = posXMil + static_cast<float>(ma) * lx + static_cast<float>(mb) * ly;
        float sy = posYMil + static_cast<float>(mc) * lx + static_cast<float>(md) * ly;
        outBx =  sx * kMilToMm;
        outBy = -sy * kMilToMm;
    };

    auto matchUC = [&](int u, int c) -> bool {
        return (u == 0 || u == placedUnit) && (c == 0 || c == placedConv);
    };

    float bMinX = 1e9f, bMinY = 1e9f, bMaxX = -1e9f, bMaxY = -1e9f;
    auto expB = [&](float x, float y) {
        bMinX = std::min(bMinX, x); bMaxX = std::max(bMaxX, x);
        bMinY = std::min(bMinY, y); bMaxY = std::max(bMaxY, y);
    };

    int pinCnt = 0;
    bool drewAny = false;

    if (sym) {
        for (const auto& sg : sym->segs) {
            if (!matchUC(sg.unit, sg.conv) || out.traces.size() >= 100000) continue;
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            xfPt(sg.x1, sg.y1, x1, y1);
            xfPt(sg.x2, sg.y2, x2, y2);
            out.traces.push_back({ x1, y1, x2, y2, sg.wMm, EdaLayerId::SchSymbol, 0 });
            expB(x1, y1); expB(x2, y2);
            drewAny = true;
        }
        for (const auto& cr : sym->circs) {
            if (!matchUC(cr.unit, cr.conv)) continue;
            float cx = 0, cy = 0;
            xfPt(cr.cx, cr.cy, cx, cy);
            float rMm = std::max(0.2f, cr.r * kMilToMm);
            out.circles.push_back({ cx, cy, rMm, cr.wMm, cr.filled, EdaLayerId::SchSymbol });
            expB(cx - rMm, cy - rMm); expB(cx + rMm, cy + rMm);
            drewAny = true;
        }
        for (const auto& p : sym->pins) {
            if (!matchUC(p.unit, p.conv)) continue;
            float dx = 0, dy = 0;
            if (p.dir == 'R') dx =  1.0f;
            else if (p.dir == 'L') dx = -1.0f;
            else if (p.dir == 'U') dy =  1.0f;
            else if (p.dir == 'D') dy = -1.0f;

            float tipX = 0, tipY = 0, bodyX = 0, bodyY = 0;
            xfPt(p.x, p.y, tipX, tipY);
            xfPt(p.x + dx * p.len, p.y + dy * p.len, bodyX, bodyY);

            if (!p.hidden) {
                bool isInv = (p.shape.find('I') != std::string::npos);
                if (p.len > 1.0f) {
                    float stemLen = isInv ? std::max(0.0f, p.len - 36.0f) : p.len;
                    float sx2 = 0, sy2 = 0;
                    xfPt(p.x + dx * stemLen, p.y + dy * stemLen, sx2, sy2);
                    out.traces.push_back({ tipX, tipY, sx2, sy2, 0.22f, EdaLayerId::SchSymbol, 0 });
                }
                if (isInv) {
                    float bcx = 0, bcy = 0;
                    xfPt(p.x + dx * std::max(0.0f, p.len - 18.0f), p.y + dy * std::max(0.0f, p.len - 18.0f), bcx, bcy);
                    out.circles.push_back({ bcx, bcy, 0.45f, 0.20f, false, EdaLayerId::SchSymbol });
                }

                EdaPadPrim pad{};
                pad.x = tipX; pad.y = tipY;
                pad.w = 0.58f; pad.h = 0.58f;
                pad.shape = 0;
                pad.layer = EdaLayerId::SchSymbol;
                pad.pinName = PathUtils::Utf8ToWide(p.number.empty() ? p.name : p.number);
                pad.netName = PathUtils::Utf8ToWide(p.name);
                out.pads.push_back(std::move(pad));

                if (sym->drawPinNames && !p.name.empty() && out.labels.size() < 4096) {
                    float lx = 0, ly = 0;
                    xfPt(p.x + dx * (p.len + 25.0f), p.y + dy * (p.len + 25.0f), lx, ly);
                    out.labels.push_back({ lx - 0.3f, ly - 0.25f, 0.78f, EdaLayerId::SchLabel, PathUtils::Utf8ToWide(p.name) });
                }
                expB(tipX, tipY);
                expB(bodyX, bodyY);
                drewAny = true;
            }
            ++pinCnt;
        }
    }

    if (!drewAny) {
        float cx = posXMil * kMilToMm;
        float cy = -posYMil * kMilToMm;
        bMinX = cx - 3.0f; bMaxX = cx + 3.0f;
        bMinY = cy - 2.0f; bMaxY = cy + 2.0f;
        out.traces.push_back({ bMinX, bMinY, bMaxX, bMinY, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ bMaxX, bMinY, bMaxX, bMaxY, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ bMaxX, bMaxY, bMinX, bMaxY, 0.28f, EdaLayerId::SchSymbol, 0 });
        out.traces.push_back({ bMinX, bMaxY, bMinX, bMinY, 0.28f, EdaLayerId::SchSymbol, 0 });
    }

    const bool isHiddenPwrRef = (!refDes.empty() && refDes[0] == L'#');
    if (sym && sym->isPowerSym) {
        if (!valStr.empty() && out.labels.size() < 4096) {
            out.labels.push_back({ (bMinX + bMaxX) * 0.5f - 0.8f, bMaxY + 0.6f, 0.90f, EdaLayerId::SchLabel, valStr });
        }
        if (!emitCompEntry) return;
        return; // Power symbols (#PWR) don't clutter BOM component list
    }

    std::wstring disp = isHiddenPwrRef ? valStr : (refDes + (valStr.empty() ? L"" : (L" (" + valStr + L")")));
    if (!disp.empty() && out.labels.size() < 4096) {
        out.labels.push_back({ bMinX, bMaxY + 0.9f, 0.98f, EdaLayerId::SchLabel, disp });
    }

    if (emitCompEntry && !isHiddenPwrRef) {
        EdaComponent3D comp;
        comp.refDes = refDes.empty() ? L"U?" : refDes;
        comp.value = valStr;
        comp.footprint = fpStr;
        comp.x = (bMinX + bMaxX) * 0.5f;
        comp.y = (bMinY + bMaxY) * 0.5f;
        comp.widthMm  = std::clamp(bMaxX - bMinX, 1.5f, 80.0f);
        comp.lengthMm = std::clamp(bMaxY - bMinY, 1.5f, 80.0f);
        comp.heightMm = 1.2f;
        comp.pinCount = std::max(2, pinCnt);
        out.components.push_back(std::move(comp));
    }
}

} // namespace DetailKiCadLegacy

namespace DetailEagleXml {

    struct EagleSymDef {
        std::vector<EdaTraceSeg> lines;
        std::vector<EdaCirclePrim> circles;
        std::vector<EdaPadPrim> pins;
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;

        void Expand(float x, float y) {
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    };
    struct EagleGateDef {
        std::string symName;
        float gx = 0.0f;
        float gy = 0.0f;
    };
    struct EagleDevSetDef {
        std::string libName;
        std::string firstGate;
        std::unordered_map<std::string, EagleGateDef> gates;
    };
    struct EaglePartDef {
        std::string libName;
        std::string devSet;
        std::wstring value;
    };

inline void AddCurvedOrStraightWire(
    std::vector<EdaTraceSeg>& dst, float x1, float y1, float x2, float y2,
    float curveDeg, float w, EdaLayerId lid)
{
        if (std::fabs(curveDeg) < 1.0f || (std::fabs(x2 - x1) < 1e-5f && std::fabs(y2 - y1) < 1e-5f)) {
            dst.push_back({ x1, y1, x2, y2, w, lid, 0 });
            return;
        }
        float rad = curveDeg * 3.14159265f / 180.0f;
        float dx = x2 - x1, dy = y2 - y1;
        float chord = std::hypot(dx, dy);
        float halfSin = std::sin(std::fabs(rad) * 0.5f);
        if (halfSin < 1e-4f) {
            dst.push_back({ x1, y1, x2, y2, w, lid, 0 });
            return;
        }
        float radius = (chord * 0.5f) / halfSin;
        float mx = (x1 + x2) * 0.5f, my = (y1 + y2) * 0.5f;
        float h = std::sqrt(std::max(0.0f, radius * radius - (chord * chord * 0.25f)));
        float sign = (std::fabs(rad) > 3.14159265f) ? -1.0f : 1.0f;
        if (rad < 0.0f) sign = -sign;
        float nx = -dy / chord, ny = dx / chord;
        float cx = mx + nx * h * sign;
        float cy = my + ny * h * sign;
        float a0 = std::atan2(y1 - cy, x1 - cx);
        int steps = std::clamp(static_cast<int>(std::fabs(curveDeg) / 15.0f), 4, 24);
        float px = x1, py = y1;
        for (int s = 1; s <= steps; ++s) {
            float a = a0 + rad * (static_cast<float>(s) / static_cast<float>(steps));
            float qx = cx + radius * std::cos(a);
            float qy = cy + radius * std::sin(a);
            if (s == steps) { qx = x2; qy = y2; }
            dst.push_back({ px, py, qx, qy, w, lid, 0 });
            px = qx; py = qy;
        }
}

inline void ParseEagleRot(std::string_view rotSv, bool& outMirror, float& outDeg) {
        outMirror = false;
        outDeg = 0.0f;
        if (rotSv.empty()) return;
        if (rotSv.find('M') != std::string_view::npos) outMirror = true;
        size_t rPos = rotSv.find('R');
        if (rPos != std::string_view::npos && rPos + 1 < rotSv.size()) {
            char rbuf[16] = {};
            std::memcpy(rbuf, rotSv.data() + rPos + 1, std::min<size_t>(15, rotSv.size() - rPos - 1));
            outDeg = std::strtof(rbuf, nullptr);
        }
}

} // namespace DetailEagleXml
