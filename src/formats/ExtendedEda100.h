#pragma once
// ExtendedEda100.h — Authentic Decoders for PCB Manufacturing, Altium OLE2, Automotive CAN/BLF,
// GTKWave FST, GHDL GHW, Xilinx Bitstream, STM32 DfuSe, VESA E-EDID, and JEDEC DDR SPD.
// NOTE: Included inside `namespace Explorer2` in src/EdaViewer.cpp. Do NOT add `#include` directives here.

namespace Formats {

namespace DetailEda100 {

inline uint16_t RdU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint16_t RdU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
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
inline uint32_t RdU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}
inline uint64_t RdU64LE(const uint8_t* p) {
    return static_cast<uint64_t>(RdU32LE(p)) | (static_cast<uint64_t>(RdU32LE(p + 4)) << 32);
}
inline uint64_t RdU64BE(const uint8_t* p) {
    return (static_cast<uint64_t>(RdU32BE(p)) << 32) | static_cast<uint64_t>(RdU32BE(p + 4));
}

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

inline void ExtractTarEntriesInMemory(
    const uint8_t* tarData,
    size_t tarSize,
    std::vector<std::pair<std::string, std::string>>& outFiles)
{
    size_t pos = 0;
    std::string gnuLongName;
    while (pos + 512 <= tarSize && outFiles.size() < 1024) {
        const uint8_t* hdr = tarData + pos;
        bool allZero = true;
        for (size_t i = 0; i < 512; ++i) {
            if (hdr[i] != 0) { allZero = false; break; }
        }
        if (allZero) break;

        uint64_t fsize = 0;
        for (size_t i = 124; i < 136; ++i) {
            uint8_t c = hdr[i];
            if (c >= '0' && c <= '7') fsize = (fsize << 3) | static_cast<uint64_t>(c - '0');
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
                std::string pfx(reinterpret_cast<const char*>(hdr + 345), plen);
                name = pfx + "/" + name;
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
                low.find("components") != std::string::npos)
            {
                outFiles.push_back({ low, UnpackMaybeCompressedText(tarData + pos, static_cast<size_t>(fsize)) });
            }
        }
        size_t padded = static_cast<size_t>((fsize + 511ULL) & ~511ULL);
        if (padded > tarSize - pos) break;
        pos += padded;
    }
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

inline bool ParseIpc2581Xml(std::string_view sv, ParsedEdaBoard& out) {
    if (sv.find("<IPC-2581") == std::string_view::npos &&
        sv.find("<Ecad") == std::string_view::npos &&
        sv.find("<CadData") == std::string_view::npos)
    {
        return false;
    }

    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"IPC-2581 Digital Twin PCB (.ipc2581)";

    float unitScale = 1.0f; // Default MILLIMETER
    struct StdPrim { float w = 1.0f; float h = 1.0f; uint8_t shape = 0; };
    std::unordered_map<std::string, float> lineDescMap;
    std::unordered_map<std::string, StdPrim> stdPrimMap;
    std::unordered_map<std::string, EdaLayerId> layerMap;
    std::set<std::string> netNames;

    std::string curEntryLineId;
    std::string curEntryStdId;
    EdaLayerId activeLayer = EdaLayerId::TopCopper;
    float activeLineWidth = 0.25f;
    int activeNetId = 0;

    bool inProfile = false;
    bool inPoly = false;
    float polyStartX = 0.0f, polyStartY = 0.0f;
    float polyCurX = 0.0f, polyCurY = 0.0f;
    bool hasPolyBegin = false;
    EdaLayerId polyLayer = EdaLayerId::EdgeCuts;

    bool inComponent = false;
    EdaComponent3D curComp;
    bool compHasLoc = false;

    bool inPad = false;
    EdaPadPrim curPad;
    bool padHasLoc = false;

    size_t pos = 0;
    while (pos < sv.size() && out.traces.size() < 50000) {
        size_t lt = sv.find('<', pos);
        if (lt == std::string_view::npos) break;
        if (lt + 4 <= sv.size() && sv.substr(lt, 4) == "<!--") {
            size_t ce = sv.find("-->", lt + 4);
            if (ce == std::string_view::npos) break;
            pos = ce + 3;
            continue;
        }
        size_t gt = sv.find('>', lt + 1);
        if (gt == std::string_view::npos) break;
        std::string_view rawTag = sv.substr(lt + 1, gt - lt - 1);
        pos = gt + 1;
        if (rawTag.empty() || rawTag[0] == '?' || rawTag[0] == '!') continue;

        bool isClosing = (rawTag[0] == '/');
        bool selfClosing = (!isClosing && rawTag.back() == '/');
        std::string_view tag = isClosing ? rawTag.substr(1) : (selfClosing ? rawTag.substr(0, rawTag.size() - 1) : rawTag);
        while (!tag.empty() && (tag.front() == ' ' || tag.front() == '\t')) tag.remove_prefix(1);

        size_t nameEnd = tag.find_first_of(" \t\r\n/");
        std::string_view tname = (nameEnd == std::string_view::npos) ? tag : tag.substr(0, nameEnd);
        size_t colon = tname.find(':');
        if (colon != std::string_view::npos) tname = tname.substr(colon + 1);

        if (isClosing) {
            if (tname == "EntryLineDesc") curEntryLineId.clear();
            else if (tname == "EntryStandard") curEntryStdId.clear();
            else if (tname == "Profile" || tname == "Outline") inProfile = false;
            else if (tname == "Polygon" || tname == "Contour") {
                inPoly = false;
                hasPolyBegin = false;
            } else if (tname == "Component") {
                if (inComponent && (!curComp.refDes.empty() || compHasLoc)) {
                    curComp.category = ClassifyComponent(curComp.refDes, curComp.footprint, curComp.widthMm, curComp.lengthMm, curComp.heightMm, curComp.pinCount);
                    out.components.push_back(curComp);
                }
                inComponent = false;
            } else if (tname == "Pad") {
                if (inPad && padHasLoc) out.pads.push_back(curPad);
                inPad = false;
            }
            continue;
        }

        if (tname == "CadHeader" || tname == "Units") {
            std::string_view u;
            if (GetXmlAttr(tag, "units", u)) {
                if (ContainsAsciiI(u, "INCH")) unitScale = 25.4f;
                else if (ContainsAsciiI(u, "MICRON")) unitScale = 0.001f;
                else if (ContainsAsciiI(u, "MIL") && !ContainsAsciiI(u, "MILLI")) unitScale = 0.0254f;
                else unitScale = 1.0f;
            }
        } else if (tname == "EntryLineDesc") {
            std::string_view id;
            if (GetXmlAttr(tag, "id", id) && !selfClosing) curEntryLineId = std::string(id);
        } else if (tname == "LineDesc") {
            float lw = GetXmlFloat(tag, "lineWidth", -1.0f);
            if (lw > 0.0f) {
                float wMm = std::max(0.05f, lw * unitScale);
                if (!curEntryLineId.empty()) lineDescMap[curEntryLineId] = wMm;
                activeLineWidth = wMm;
            }
        } else if (tname == "LineDescRef") {
            std::string_view id;
            if (GetXmlAttr(tag, "id", id)) {
                auto it = lineDescMap.find(std::string(id));
                if (it != lineDescMap.end()) activeLineWidth = it->second;
            }
        } else if (tname == "EntryStandard") {
            std::string_view id;
            if (GetXmlAttr(tag, "id", id) && !selfClosing) curEntryStdId = std::string(id);
        } else if (!curEntryStdId.empty() && (tname == "Circle" || tname == "RectCenter" || tname == "Oval" || tname == "Square")) {
            StdPrim sp;
            if (tname == "Circle") {
                float d = std::max(0.15f, GetXmlFloat(tag, "diameter", 1.0f) * unitScale);
                sp = { d, d, 0 };
            } else if (tname == "Square") {
                float s = std::max(0.15f, GetXmlFloat(tag, "size", 1.0f) * unitScale);
                sp = { s, s, 1 };
            } else {
                float w = std::max(0.15f, GetXmlFloat(tag, "width", 1.0f) * unitScale);
                float h = std::max(0.15f, GetXmlFloat(tag, "height", 1.0f) * unitScale);
                sp = { w, h, static_cast<uint8_t>((tname == "Oval") ? 3 : 1) };
            }
            stdPrimMap[curEntryStdId] = sp;
        } else if (tname == "Layer") {
            std::string_view nm, fn, sd;
            if (GetXmlAttr(tag, "name", nm)) {
                GetXmlAttr(tag, "layerFunction", fn);
                GetXmlAttr(tag, "side", sd);
                layerMap[std::string(nm)] = ClassifyIpcLayerName(nm, fn, sd);
            }
        } else if (tname == "Profile" || tname == "Outline") {
            if (!selfClosing) inProfile = true;
        } else if (tname == "Polygon" || tname == "Contour") {
            if (!selfClosing) {
                inPoly = true;
                hasPolyBegin = false;
                polyLayer = inProfile ? EdaLayerId::EdgeCuts : activeLayer;
            }
        } else if (inPoly && tname == "PolyBegin") {
            polyStartX = GetXmlFloat(tag, "x") * unitScale;
            polyStartY = GetXmlFloat(tag, "y") * unitScale;
            polyCurX = polyStartX;
            polyCurY = polyStartY;
            hasPolyBegin = true;
        } else if (inPoly && hasPolyBegin && tname == "PolyStepSegment") {
            float nx = GetXmlFloat(tag, "x") * unitScale;
            float ny = GetXmlFloat(tag, "y") * unitScale;
            float w = (polyLayer == EdaLayerId::EdgeCuts) ? 0.30f : std::max(0.12f, activeLineWidth);
            out.traces.push_back({ polyCurX, polyCurY, nx, ny, w, polyLayer, activeNetId });
            polyCurX = nx;
            polyCurY = ny;
        } else if (inPoly && hasPolyBegin && tname == "PolyStepCurve") {
            float nx = GetXmlFloat(tag, "x") * unitScale;
            float ny = GetXmlFloat(tag, "y") * unitScale;
            float cx = GetXmlFloat(tag, "centerX") * unitScale;
            float cy = GetXmlFloat(tag, "centerY") * unitScale;
            std::string_view cwSv;
            bool cw = GetXmlAttr(tag, "clockwise", cwSv) && ContainsAsciiI(cwSv, "true");
            float w = (polyLayer == EdaLayerId::EdgeCuts) ? 0.30f : std::max(0.12f, activeLineWidth);
            AddTessellatedArc(out, polyCurX, polyCurY, nx, ny, cx, cy, cw, w, polyLayer, activeNetId);
            polyCurX = nx;
            polyCurY = ny;
        } else if (tname == "LayerFeature") {
            std::string_view lref;
            if (GetXmlAttr(tag, "layerRef", lref)) {
                auto it = layerMap.find(std::string(lref));
                activeLayer = (it != layerMap.end()) ? it->second : ClassifyIpcLayerName(lref);
            }
        } else if (tname == "Set" || tname == "LogicalNet") {
            std::string_view netSv;
            if (GetXmlAttr(tag, "net", netSv) && !netSv.empty() && netSv != "No Net") {
                netNames.insert(std::string(netSv));
                activeNetId = static_cast<int>(netNames.size());
            } else if (tname == "LogicalNet" && GetXmlAttr(tag, "name", netSv) && !netSv.empty()) {
                netNames.insert(std::string(netSv));
            }
        } else if (tname == "Line") {
            float x1 = GetXmlFloat(tag, "startX") * unitScale;
            float y1 = GetXmlFloat(tag, "startY") * unitScale;
            float x2 = GetXmlFloat(tag, "endX") * unitScale;
            float y2 = GetXmlFloat(tag, "endY") * unitScale;
            float lw = GetXmlFloat(tag, "lineWidth", -1.0f);
            float w = (lw > 0.0f) ? (lw * unitScale) : activeLineWidth;
            EdaLayerId lid = inProfile ? EdaLayerId::EdgeCuts : activeLayer;
            out.traces.push_back({ x1, y1, x2, y2, std::max(0.08f, w), lid, activeNetId });
        } else if (tname == "Arc") {
            float xs = GetXmlFloat(tag, "startX") * unitScale;
            float ys = GetXmlFloat(tag, "startY") * unitScale;
            float xe = GetXmlFloat(tag, "endX") * unitScale;
            float ye = GetXmlFloat(tag, "endY") * unitScale;
            float xc = GetXmlFloat(tag, "centerX") * unitScale;
            float yc = GetXmlFloat(tag, "centerY") * unitScale;
            std::string_view cwSv;
            bool cw = GetXmlAttr(tag, "clockwise", cwSv) && ContainsAsciiI(cwSv, "true");
            float lw = GetXmlFloat(tag, "lineWidth", -1.0f);
            float w = (lw > 0.0f) ? (lw * unitScale) : activeLineWidth;
            AddTessellatedArc(out, xs, ys, xe, ye, xc, yc, cw, std::max(0.08f, w), inProfile ? EdaLayerId::EdgeCuts : activeLayer, activeNetId);
        } else if (tname == "Pad") {
            curPad = EdaPadPrim{};
            curPad.w = 1.1f; curPad.h = 1.1f; curPad.shape = 1;
            curPad.layer = (activeLayer == EdaLayerId::Drills) ? EdaLayerId::PadsThrough : activeLayer;
            curPad.rotDeg = GetXmlFloat(tag, "rotation", 0.0f);
            std::string_view pinSv;
            if (GetXmlAttr(tag, "padstackDefRef", pinSv)) curPad.pinName = PathUtils::Utf8ToWide(pinSv);
            std::string_view xSv, ySv;
            if (GetXmlAttr(tag, "x", xSv) && GetXmlAttr(tag, "y", ySv)) {
                curPad.x = GetXmlFloat(tag, "x") * unitScale;
                curPad.y = GetXmlFloat(tag, "y") * unitScale;
                padHasLoc = true;
            } else {
                padHasLoc = false;
            }
            if (selfClosing) {
                if (padHasLoc) out.pads.push_back(curPad);
            } else {
                inPad = true;
            }
        } else if (inPad && tname == "StandardPrimitiveRef") {
            std::string_view id;
            if (GetXmlAttr(tag, "id", id)) {
                auto it = stdPrimMap.find(std::string(id));
                if (it != stdPrimMap.end()) {
                    curPad.w = it->second.w;
                    curPad.h = it->second.h;
                    curPad.shape = it->second.shape;
                }
            }
        } else if (tname == "Hole") {
            float d = std::max(0.15f, GetXmlFloat(tag, "diameter", 0.4f) * unitScale);
            if (inPad) {
                curPad.drill = d;
                curPad.layer = EdaLayerId::PadsThrough;
            } else {
                float hx = GetXmlFloat(tag, "x") * unitScale;
                float hy = GetXmlFloat(tag, "y") * unitScale;
                out.vias.push_back({ hx, hy, d * 1.8f, d, activeNetId });
            }
        } else if (tname == "Component") {
            curComp = EdaComponent3D{};
            compHasLoc = false;
            std::string_view refSv, partSv, pkgSv, lyrSv;
            if (GetXmlAttr(tag, "refDes", refSv)) curComp.refDes = PathUtils::Utf8ToWide(refSv);
            if (GetXmlAttr(tag, "part", partSv)) curComp.value = PathUtils::Utf8ToWide(partSv);
            if (GetXmlAttr(tag, "packageRef", pkgSv)) curComp.footprint = PathUtils::Utf8ToWide(pkgSv);
            if (GetXmlAttr(tag, "layerRef", lyrSv)) {
                curComp.bottomSide = (ClassifyIpcLayerName(lyrSv) == EdaLayerId::BottomCopper || ClassifyIpcLayerName(lyrSv) == EdaLayerId::BottomSilk);
            }
            curComp.rotDeg = GetXmlFloat(tag, "rotation", 0.0f);
            std::string_view xSv, ySv;
            if (GetXmlAttr(tag, "x", xSv) && GetXmlAttr(tag, "y", ySv)) {
                curComp.x = GetXmlFloat(tag, "x") * unitScale;
                curComp.y = GetXmlFloat(tag, "y") * unitScale;
                compHasLoc = true;
            }
            if (selfClosing) {
                if (!curComp.refDes.empty() && compHasLoc) {
                    curComp.category = ClassifyComponent(curComp.refDes, curComp.footprint, curComp.widthMm, curComp.lengthMm, curComp.heightMm, curComp.pinCount);
                    out.components.push_back(curComp);
                }
            } else {
                inComponent = true;
            }
        } else if (tname == "Location") {
            float lx = GetXmlFloat(tag, "x") * unitScale;
            float ly = GetXmlFloat(tag, "y") * unitScale;
            if (inPad) {
                curPad.x = lx;
                curPad.y = ly;
                padHasLoc = true;
            } else if (inComponent) {
                curComp.x = lx;
                curComp.y = ly;
                compHasLoc = true;
            }
        } else if (tname == "Xform") {
            float rot = GetXmlFloat(tag, "rotation", -9999.0f);
            std::string_view mirSv;
            bool mir = GetXmlAttr(tag, "mirror", mirSv) && ContainsAsciiI(mirSv, "true");
            if (inPad && rot > -9000.0f) curPad.rotDeg = rot;
            if (inComponent) {
                if (rot > -9000.0f) curComp.rotDeg = rot;
                if (mir) curComp.bottomSide = true;
            }
        }
    }

    if (out.traces.empty() && out.pads.empty() && out.vias.empty() && out.components.empty() && out.circles.empty()) {
        return false;
    }
    out.netCount = std::max<size_t>(1, netNames.empty() ? out.components.size() : netNames.size());
    return true;
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

inline void ParseOdbFeaturesStream(std::string_view sv, EdaLayerId layerId, bool isProfile, ParsedEdaBoard& out, size_t& validRecords) {
    bool isMm = false; // ODB++ default is INCH (25.4 mm)
    float uScale = 25.4f;
    std::unordered_map<int, OdbSymInfo> symTable;

    bool inSurface = false;
    float surfStartX = 0.0f, surfStartY = 0.0f;
    float surfCurX = 0.0f, surfCurY = 0.0f;

    size_t pos = 0;
    while (pos < sv.size() && out.traces.size() < 60000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
        if (ln.empty() || ln[0] == '#') continue;

        if (ln.rfind("UNITS=", 0) == 0) {
            isMm = ContainsAsciiI(ln.substr(6), "MM");
            uScale = isMm ? 1.0f : 25.4f;
            continue;
        }
        if (ln == "U MM") { isMm = true; uScale = 1.0f; continue; }
        if (ln == "U INCH" || ln == "U IN") { isMm = false; uScale = 25.4f; continue; }

        if (ln[0] == '$' && ln.size() >= 3 && ln[1] >= '0' && ln[1] <= '9') {
            int idx = 0;
            int consumed = 0;
            std::string tmp(ln.substr(1));
            if (sscanf_s(tmp.c_str(), "%d%n", &idx, &consumed) == 1 && consumed > 0) {
                symTable[idx] = ParseOdbSymbolDef(std::string_view(tmp).substr(consumed), isMm);
                ++validRecords;
            }
            continue;
        }

        if (ln[0] == 'L' && ln.size() > 4 && (ln[1] == ' ' || ln[1] == '\t')) {
            float xs = 0, ys = 0, xe = 0, ye = 0;
            int symIdx = 0;
            std::string tmp(ln.substr(2));
            int n = sscanf_s(tmp.c_str(), "%f %f %f %f %d", &xs, &ys, &xe, &ye, &symIdx);
            if (n >= 4) {
                float w = 0.25f;
                if (n >= 5) {
                    auto it = symTable.find(symIdx);
                    if (it != symTable.end()) w = it->second.wMm;
                }
                EdaLayerId lid = isProfile ? EdaLayerId::EdgeCuts : layerId;
                out.traces.push_back({ xs * uScale, ys * uScale, xe * uScale, ye * uScale, w, lid, 1 });
                ++validRecords;
            }
        } else if (ln[0] == 'P' && ln.size() > 4 && (ln[1] == ' ' || ln[1] == '\t')) {
            float px = 0, py = 0;
            int symIdx = 0;
            std::string tmp(ln.substr(2));
            int n = sscanf_s(tmp.c_str(), "%f %f %d", &px, &py, &symIdx);
            if (n >= 2) {
                OdbSymInfo si;
                si.wMm = 1.0f; si.hMm = 1.0f; si.shape = 0;
                if (n >= 3) {
                    auto it = symTable.find(symIdx);
                    if (it != symTable.end()) si = it->second;
                }
                if (layerId == EdaLayerId::Drills) {
                    out.vias.push_back({ px * uScale, py * uScale, si.wMm * 1.7f, si.wMm, 0 });
                } else {
                    out.pads.push_back({ px * uScale, py * uScale, si.wMm, si.hMm, 0.0f, 0.0f, si.shape, layerId, L"", L"" });
                }
                ++validRecords;
            }
        } else if (ln[0] == 'A' && ln.size() > 8 && (ln[1] == ' ' || ln[1] == '\t')) {
            float xs = 0, ys = 0, xe = 0, ye = 0, xc = 0, yc = 0;
            int symIdx = 0;
            char pol = 'P', cwCh = 'Y';
            std::string tmp(ln.substr(2));
            int n = sscanf_s(tmp.c_str(), "%f %f %f %f %f %f %d %c %c",
                &xs, &ys, &xe, &ye, &xc, &yc, &symIdx, &pol, 1, &cwCh, 1);
            if (n >= 6) {
                float w = 0.25f;
                if (n >= 7) {
                    auto it = symTable.find(symIdx);
                    if (it != symTable.end()) w = it->second.wMm;
                }
                bool cw = (cwCh == 'Y' || cwCh == 'y' || cwCh == '1');
                EdaLayerId lid = isProfile ? EdaLayerId::EdgeCuts : layerId;
                AddTessellatedArc(out, xs * uScale, ys * uScale, xe * uScale, ye * uScale, xc * uScale, yc * uScale, cw, w, lid, 1);
                ++validRecords;
            }
        } else if (ln.rfind("OB ", 0) == 0 || ln.rfind("OB\t", 0) == 0) {
            float bx = 0, by = 0;
            std::string tmp(ln.substr(3));
            if (sscanf_s(tmp.c_str(), "%f %f", &bx, &by) == 2) {
                surfStartX = bx * uScale;
                surfStartY = by * uScale;
                surfCurX = surfStartX;
                surfCurY = surfStartY;
                inSurface = true;
                ++validRecords;
            }
        } else if (inSurface && (ln.rfind("OS ", 0) == 0 || ln.rfind("OS\t", 0) == 0)) {
            float nx = 0, ny = 0;
            std::string tmp(ln.substr(3));
            if (sscanf_s(tmp.c_str(), "%f %f", &nx, &ny) == 2) {
                float sx = nx * uScale, sy = ny * uScale;
                EdaLayerId lid = isProfile ? EdaLayerId::EdgeCuts : layerId;
                out.traces.push_back({ surfCurX, surfCurY, sx, sy, isProfile ? 0.32f : 0.20f, lid, 0 });
                surfCurX = sx;
                surfCurY = sy;
            }
        } else if (inSurface && (ln.rfind("OC ", 0) == 0 || ln.rfind("OC\t", 0) == 0)) {
            float xe = 0, ye = 0, xc = 0, yc = 0;
            char cwCh = 'Y';
            std::string tmp(ln.substr(3));
            if (sscanf_s(tmp.c_str(), "%f %f %f %f %c", &xe, &ye, &xc, &yc, &cwCh, 1) >= 4) {
                EdaLayerId lid = isProfile ? EdaLayerId::EdgeCuts : layerId;
                AddTessellatedArc(out, surfCurX, surfCurY, xe * uScale, ye * uScale, xc * uScale, yc * uScale,
                    cwCh == 'Y' || cwCh == 'y', isProfile ? 0.32f : 0.20f, lid, 0);
                surfCurX = xe * uScale;
                surfCurY = ye * uScale;
            }
        } else if (inSurface && ln.rfind("OE", 0) == 0) {
            if (std::hypot(surfCurX - surfStartX, surfCurY - surfStartY) > 1e-3f) {
                EdaLayerId lid = isProfile ? EdaLayerId::EdgeCuts : layerId;
                out.traces.push_back({ surfCurX, surfCurY, surfStartX, surfStartY, isProfile ? 0.32f : 0.20f, lid, 0 });
            }
            inSurface = false;
        }
    }
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

// ============================================================================
// 77 & 78. ODB++ (.odb: ZIP / TGZ / TAR / Feature Text) & IPC-2581 (.ipc2581 / .cvg)
// ============================================================================
inline bool ParseOdbOrIpc2581Board(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (!data || size < 8) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), size);

    // 1. IPC-2581 / CVG XML
    if (extLower == L".ipc2581" || extLower == L".cvg" ||
        sv.find("<IPC-2581") != std::string_view::npos ||
        (extLower == L".odb" && sv.find("<IPC-2581") != std::string_view::npos))
    {
        return DetailEda100::ParseIpc2581Xml(sv, out);
    }

    if (extLower != L".odb") return false;

    out.kind = EdaDocKind::KiCadPcb;
    out.formatTitle = L"ODB++ PCB Manufacturing Archive (.odb)";

    std::vector<std::pair<std::string, std::string>> vfiles;

    // Case A: ZIP archive (.odb / .odb.zip)
    if (size >= 4 && data[0] == 'P' && data[1] == 'K' && data[2] == 0x03 && data[3] == 0x04) {
        std::vector<ZipDeflate::ArchiveItemMeta> entries;
        if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries) || entries.empty()) return false;
        for (const auto& e : entries) {
            if (e.isDirectory || e.uncompressedSize == 0 || e.uncompressedSize > 32 * 1024 * 1024) continue;
            std::string low = PathUtils::ToLowerAscii(PathUtils::WideToUtf8(e.pathInArchive));
            for (char& ch : low) if (ch == '\\') ch = '/';
            if (low.find("matrix") != std::string::npos ||
                low.find("profile") != std::string::npos ||
                low.find("features") != std::string::npos ||
                low.find("components") != std::string::npos)
            {
                std::vector<uint8_t> rawEntry;
                if (ZipDeflate::ExtractZipEntryFromMemory(data, size, e, rawEntry, 32 * 1024 * 1024) && !rawEntry.empty()) {
                    vfiles.push_back({ low, DetailEda100::UnpackMaybeCompressedText(rawEntry.data(), rawEntry.size()) });
                }
            }
        }
    }
    // Case B: Gzipped TAR (.tgz / .tar.gz) or Gzipped feature stream
    else if (size >= 10 && data[0] == 0x1F && data[1] == 0x8B) {
        std::vector<uint8_t> gunz;
        if (!ZipDeflate::InflateGzip(data, size, gunz, 64 * 1024 * 1024) || gunz.empty()) return false;
        if (DetailEda100::LooksLikeTarBlock(gunz.data(), gunz.size())) {
            DetailEda100::ExtractTarEntriesInMemory(gunz.data(), gunz.size(), vfiles);
        } else {
            vfiles.push_back({ "layers/top/features", std::string(reinterpret_cast<const char*>(gunz.data()), gunz.size()) });
        }
    }
    // Case C: Uncompressed POSIX TAR archive
    else if (DetailEda100::LooksLikeTarBlock(data, size)) {
        DetailEda100::ExtractTarEntriesInMemory(data, size, vfiles);
    }
    // Case D: Raw ODB++ feature or component text file
    else {
        bool hasOdbMarker = (sv.find("UNITS=") != std::string_view::npos ||
                             sv.find("\n$0 ") != std::string_view::npos ||
                             sv.rfind("$0 ", 0) == 0 ||
                             sv.find("CMP ") != std::string_view::npos ||
                             sv.find("\nOB ") != std::string_view::npos ||
                             sv.rfind("OB ", 0) == 0);
        if (!hasOdbMarker) return false;
        if (sv.find("CMP ") != std::string_view::npos) {
            vfiles.push_back({ "steps/pcb/layers/comp_+_top/components", std::string(sv) });
        }
        vfiles.push_back({ "steps/pcb/layers/top/features", std::string(sv) });
    }

    if (vfiles.empty()) return false;

    std::unordered_map<std::string, EdaLayerId> matrixLayerMap;
    for (const auto& vf : vfiles) {
        if (vf.first.find("matrix") != std::string::npos) {
            DetailEda100::ParseOdbMatrixLayers(vf.second, matrixLayerMap);
        }
    }

    size_t validRecords = 0;
    for (const auto& vf : vfiles) {
        const std::string& path = vf.first;
        if (path.find("matrix") != std::string::npos) continue;

        if (path.find("components") != std::string::npos) {
            bool isBot = (path.find("comp_+_bot") != std::string::npos || path.find("bottom") != std::string::npos);
            DetailEda100::ParseOdbComponentsStream(vf.second, isBot, out, validRecords);
        } else if (path.find("profile") != std::string::npos) {
            DetailEda100::ParseOdbFeaturesStream(vf.second, EdaLayerId::EdgeCuts, true, out, validRecords);
        } else if (path.find("features") != std::string::npos) {
            // Extract parent folder name in layers/<layerName>/features
            std::string layerName = "top";
            size_t lpos = path.find("layers/");
            if (lpos != std::string::npos) {
                size_t s1 = lpos + 7;
                size_t s2 = path.find('/', s1);
                if (s2 != std::string::npos && s2 > s1) layerName = path.substr(s1, s2 - s1);
            }
            if (layerName.find("mask") != std::string::npos || layerName.find("paste") != std::string::npos) continue;
            auto it = matrixLayerMap.find(layerName);
            EdaLayerId lid = (it != matrixLayerMap.end()) ? it->second : DetailEda100::ClassifyIpcLayerName(layerName);
            DetailEda100::ParseOdbFeaturesStream(vf.second, lid, false, out, validRecords);
        }
    }

    if (out.traces.empty() && out.pads.empty() && out.vias.empty() && out.components.empty() && out.circles.empty()) {
        return false;
    }
    out.netCount = std::max<size_t>(1, out.components.empty() ? 1 : out.components.size());
    return true;
}

inline bool ParseOdbOrIpc2581Board(std::string_view sv, const std::wstring& extLower, ParsedEdaBoard& out) {
    return ParseOdbOrIpc2581Board(reinterpret_cast<const uint8_t*>(sv.data()), sv.size(), extLower, out);
}

// ============================================================================
// 79. Altium Designer PCB & Schematic (.pcbdoc / .schdoc — OLE2 Compound & Pipe ASCII)
// ============================================================================
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
        kv[std::move(k)] = std::move(v);
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

struct SchCompBuilder {
    std::wstring refDes;
    std::wstring value;
    std::wstring libRef;
    float x = 0.0f, y = 0.0f;
    float rotDeg = 0.0f;
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    int pinCount = 0;
    bool hasBody = false;
};

inline bool ProcessSchRecords(const std::vector<KvMap>& records, ParsedEdaBoard& out) {
    std::unordered_map<int, SchCompBuilder> compMap;
    std::set<std::wstring> netSet;
    size_t decodedCount = 0;

    for (size_t idx = 0; idx < records.size(); ++idx) {
        const KvMap& kv = records[idx];
        std::string recStr = GetKvStr(kv, "record");
        if (recStr.empty()) continue;
        int recId = std::atoi(recStr.c_str());
        int ownerIdx = GetKvInt(kv, "ownerindex", -1);

        switch (recId) {
        case 1: { // SchComponent
            SchCompBuilder cb;
            cb.libRef = PathUtils::Utf8ToWide(GetKvStr(kv, "libreference"));
            if (cb.libRef.empty()) cb.libRef = PathUtils::Utf8ToWide(GetKvStr(kv, "designitemid"));
            std::string inlDes = GetKvStr(kv, "designator");
            if (!inlDes.empty()) cb.refDes = PathUtils::Utf8ToWide(inlDes);
            std::string inlCmt = GetKvStr(kv, "comment");
            if (!inlCmt.empty()) cb.value = PathUtils::Utf8ToWide(inlCmt);
            cb.x = GetSchCoordMm(kv, "location.x");
            cb.y = GetSchCoordMm(kv, "location.y");
            cb.rotDeg = static_cast<float>((GetKvInt(kv, "orientation", 0) & 3) * 90);
            compMap[static_cast<int>(idx)] = cb;
            ++decodedCount;
            break;
        }
        case 2: { // Pin
            float px = GetSchCoordMm(kv, "location.x");
            float py = GetSchCoordMm(kv, "location.y");
            float plen = GetSchCoordMm(kv, "pinlength", 5.08f);
            int cong = GetKvInt(kv, "pinconglomerate", 0);
            int orient = cong & 3; // 0=0 deg (right), 1=90 (up), 2=180 (left), 3=270 (down)
            float dx = (orient == 0) ? plen : ((orient == 2) ? -plen : 0.0f);
            float dy = (orient == 1) ? plen : ((orient == 3) ? -plen : 0.0f);
            float tipX = px + dx, tipY = py + dy;
            out.traces.push_back({ px, py, tipX, tipY, 0.25f, EdaLayerId::SchSymbol, 0 });

            std::wstring pName = PathUtils::Utf8ToWide(GetKvStr(kv, "name"));
            std::wstring pDes  = PathUtils::Utf8ToWide(GetKvStr(kv, "designator"));
            out.pads.push_back({ tipX, tipY, 0.7f, 0.7f, 0.0f, 0.0f, 0, EdaLayerId::SchSymbol, pDes, pName });
            if (!pName.empty()) {
                out.labels.push_back({ px, py + 0.8f, 0.9f, EdaLayerId::SchLabel, pName });
            }
            auto it = compMap.find(ownerIdx);
            if (it != compMap.end()) {
                ++it->second.pinCount;
                it->second.minX = std::min({ it->second.minX, px, tipX });
                it->second.maxX = std::max({ it->second.maxX, px, tipX });
                it->second.minY = std::min({ it->second.minY, py, tipY });
                it->second.maxY = std::max({ it->second.maxY, py, tipY });
            }
            ++decodedCount;
            break;
        }
        case 14: { // Rectangle
            float x1 = GetSchCoordMm(kv, "location.x");
            float y1 = GetSchCoordMm(kv, "location.y");
            float x2 = GetSchCoordMm(kv, "corner.x", x1 + 10.16f);
            float y2 = GetSchCoordMm(kv, "corner.y", y1 + 7.62f);
            DetailEda100::AddRectOutline(out, (x1 + x2) * 0.5f, (y1 + y2) * 0.5f, std::fabs(x2 - x1), std::fabs(y2 - y1), EdaLayerId::SchSymbol, 0.30f);
            auto it = compMap.find(ownerIdx);
            if (it != compMap.end()) {
                it->second.hasBody = true;
                it->second.minX = std::min({ it->second.minX, x1, x2 });
                it->second.maxX = std::max({ it->second.maxX, x1, x2 });
                it->second.minY = std::min({ it->second.minY, y1, y2 });
                it->second.maxY = std::max({ it->second.maxY, y1, y2 });
            }
            ++decodedCount;
            break;
        }
        case 13: { // Line
            float x1 = GetSchCoordMm(kv, "location.x");
            float y1 = GetSchCoordMm(kv, "location.y");
            float x2 = GetSchCoordMm(kv, "corner.x");
            float y2 = GetSchCoordMm(kv, "corner.y");
            out.traces.push_back({ x1, y1, x2, y2, 0.25f, EdaLayerId::SchSymbol, 0 });
            ++decodedCount;
            break;
        }
        case 6: // Polyline
        case 26: // Bus
        case 27: { // Wire
            int cnt = std::clamp(GetKvInt(kv, "locationcount", 0), 0, 128);
            EdaLayerId lid = (recId == 6) ? EdaLayerId::SchSymbol : EdaLayerId::SchWire;
            float stroke = (recId == 26) ? 0.55f : 0.28f;
            for (int i = 1; i < cnt; ++i) {
                std::string kx1 = "x" + std::to_string(i), ky1 = "y" + std::to_string(i);
                std::string kx2 = "x" + std::to_string(i + 1), ky2 = "y" + std::to_string(i + 1);
                float x1 = GetSchCoordMm(kv, kx1);
                float y1 = GetSchCoordMm(kv, ky1);
                float x2 = GetSchCoordMm(kv, kx2);
                float y2 = GetSchCoordMm(kv, ky2);
                out.traces.push_back({ x1, y1, x2, y2, stroke, lid, 1 });
            }
            if (cnt >= 2) ++decodedCount;
            break;
        }
        case 8:  // Ellipse
        case 12: { // Arc
            float cx = GetSchCoordMm(kv, "location.x");
            float cy = GetSchCoordMm(kv, "location.y");
            float r  = std::max(0.4f, GetSchCoordMm(kv, "radius", 1.27f));
            out.circles.push_back({ cx, cy, r, 0.22f, false, EdaLayerId::SchSymbol });
            ++decodedCount;
            break;
        }
        case 17: // PowerPort
        case 18: // Port
        case 25: // NetLabel
        case 4: { // Text Label
            std::wstring txt = PathUtils::Utf8ToWide(GetKvStr(kv, "text"));
            if (txt.empty()) txt = PathUtils::Utf8ToWide(GetKvStr(kv, "name"));
            float lx = GetSchCoordMm(kv, "location.x");
            float ly = GetSchCoordMm(kv, "location.y");
            if (!txt.empty()) {
                out.labels.push_back({ lx, ly, 1.15f, EdaLayerId::SchLabel, txt });
                if (recId == 17 || recId == 25) netSet.insert(txt);
                if (recId == 17) {
                    out.traces.push_back({ lx, ly, lx, ly + 1.8f, 0.25f, EdaLayerId::SchSymbol, 0 });
                    out.traces.push_back({ lx - 1.2f, ly + 1.8f, lx + 1.2f, ly + 1.8f, 0.30f, EdaLayerId::SchSymbol, 0 });
                }
                ++decodedCount;
            }
            break;
        }
        case 29: { // Junction
            float jx = GetSchCoordMm(kv, "location.x");
            float jy = GetSchCoordMm(kv, "location.y");
            out.circles.push_back({ jx, jy, 0.50f, 0.50f, true, EdaLayerId::SchWire });
            ++decodedCount;
            break;
        }
        case 34: { // Designator
            std::wstring des = PathUtils::Utf8ToWide(GetKvStr(kv, "text"));
            auto it = compMap.find(ownerIdx);
            if (it != compMap.end() && !des.empty()) {
                it->second.refDes = des;
            }
            if (!des.empty() && GetKvStr(kv, "ishidden") != "T") {
                out.labels.push_back({ GetSchCoordMm(kv, "location.x"), GetSchCoordMm(kv, "location.y"), 1.1f, EdaLayerId::SchLabel, des });
            }
            ++decodedCount;
            break;
        }
        case 41: { // Parameter
            std::string pName = PathUtils::ToLowerAscii(GetKvStr(kv, "name"));
            std::wstring pVal = PathUtils::Utf8ToWide(GetKvStr(kv, "text"));
            auto it = compMap.find(ownerIdx);
            if (it != compMap.end() && !pVal.empty()) {
                if (pName == "comment" || pName == "value") it->second.value = pVal;
                else if (pName == "designator" && it->second.refDes.empty()) it->second.refDes = pVal;
            }
            break;
        }
        default:
            break;
        }
    }

    for (auto& kvComp : compMap) {
        SchCompBuilder& cb = kvComp.second;
        EdaComponent3D c;
        c.refDes = cb.refDes.empty() ? (cb.libRef.empty() ? (L"U" + std::to_wstring(kvComp.first + 1)) : cb.libRef) : cb.refDes;
        c.value = cb.value;
        c.footprint = cb.libRef.empty() ? L"SCH_SYM" : cb.libRef;
        c.x = cb.x;
        c.y = cb.y;
        c.rotDeg = cb.rotDeg;
        c.pinCount = std::max(2, cb.pinCount);
        if (cb.minX < cb.maxX && cb.minY < cb.maxY) {
            c.widthMm = std::clamp(cb.maxX - cb.minX, 3.0f, 40.0f);
            c.lengthMm = std::clamp(cb.maxY - cb.minY, 2.5f, 40.0f);
            c.x = (cb.minX + cb.maxX) * 0.5f;
            c.y = (cb.minY + cb.maxY) * 0.5f;
        } else {
            c.widthMm = 7.62f;
            c.lengthMm = 5.08f;
        }
        if (!cb.hasBody) {
            DetailEda100::AddRectOutline(out, c.x, c.y, c.widthMm, c.lengthMm, EdaLayerId::SchSymbol, 0.28f);
        }
        c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
        out.components.push_back(c);
    }

    out.netCount = std::max<size_t>(1, netSet.empty() ? out.components.size() : netSet.size());
    return decodedCount > 0 && (!out.traces.empty() || !out.components.empty() || !out.pads.empty() || !out.circles.empty());
}

inline void WalkLengthPrefixedBlocks(const std::vector<uint8_t>& stream, std::vector<KvMap>& outRecords, bool onlyWithRecordKey) {
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
        }
        pos += len;
    }
}

inline bool ParseAltiumSchOle2(const Explorer2::Ole2::CompoundFile& cf, ParsedEdaBoard& out) {
    std::vector<KvMap> records;
    const auto* fh = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"FileHeader");
    if (fh) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *fh, bytes, 32 * 1024 * 1024)) {
            WalkLengthPrefixedBlocks(bytes, records, true);
        }
    }
    const auto* add = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Additional");
    if (add) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *add, bytes, 32 * 1024 * 1024)) {
            WalkLengthPrefixedBlocks(bytes, records, true);
        }
    }
    if (records.empty()) return false;
    return ProcessSchRecords(records, out);
}

inline bool ParseAltiumPcbOle2(const Explorer2::Ole2::CompoundFile& cf, ParsedEdaBoard& out) {
    constexpr float kAltiumUnitToMm = 0.00000254f; // 10,000 internal units = 1 mil = 0.0254 mm
    size_t netCount = 0;

    // 1. Board6/Data (board outline vertices VX0/VY0..VXn/VYn)
    if (const auto* brdEnt = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Board6/Data")) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *brdEnt, bytes, 16 * 1024 * 1024)) {
            std::vector<KvMap> bRecs;
            WalkLengthPrefixedBlocks(bytes, bRecs, false);
            for (const auto& kv : bRecs) {
                std::vector<std::pair<float, float>> verts;
                for (int vi = 0; vi < 256; ++vi) {
                    std::string kx = "vx" + std::to_string(vi);
                    std::string ky = "vy" + std::to_string(vi);
                    auto ix = kv.find(kx), iy = kv.find(ky);
                    if (ix == kv.end() || iy == kv.end()) break;
                    verts.push_back({ ParsePcbCoordStrMm(ix->second), ParsePcbCoordStrMm(iy->second) });
                }
                if (verts.size() >= 3) {
                    for (size_t k = 0; k < verts.size(); ++k) {
                        const auto& p0 = verts[k];
                        const auto& p1 = verts[(k + 1) % verts.size()];
                        out.traces.push_back({ p0.first, p0.second, p1.first, p1.second, 0.35f, EdaLayerId::EdgeCuts, 0 });
                    }
                }
            }
        }
    }

    // 2. Nets6/Data
    if (const auto* netEnt = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Nets6/Data")) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *netEnt, bytes, 16 * 1024 * 1024)) {
            std::vector<KvMap> nRecs;
            WalkLengthPrefixedBlocks(bytes, nRecs, false);
            for (const auto& kv : nRecs) {
                if (!GetKvStr(kv, "name").empty()) ++netCount;
            }
        }
    }

    // 3. Components6/Data
    if (const auto* cmpEnt = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Components6/Data")) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *cmpEnt, bytes, 16 * 1024 * 1024)) {
            std::vector<KvMap> cRecs;
            WalkLengthPrefixedBlocks(bytes, cRecs, false);
            for (const auto& kv : cRecs) {
                std::string des = GetKvStr(kv, "sourcedesignator");
                if (des.empty()) des = GetKvStr(kv, "designator");
                if (des.empty()) des = GetKvStr(kv, "name");
                std::string pat = GetKvStr(kv, "pattern");
                if (des.empty() && pat.empty()) continue;

                EdaComponent3D c;
                c.refDes = PathUtils::Utf8ToWide(des.empty() ? pat : des);
                c.value = PathUtils::Utf8ToWide(GetKvStr(kv, "comment"));
                c.footprint = PathUtils::Utf8ToWide(pat.empty() ? "ALTIUM_PKG" : pat);
                c.x = ParsePcbCoordStrMm(GetKvStr(kv, "x"));
                c.y = ParsePcbCoordStrMm(GetKvStr(kv, "y"));
                c.rotDeg = static_cast<float>(std::strtod(GetKvStr(kv, "rotation").c_str(), nullptr));
                c.bottomSide = (MapAltiumPcbLayerStr(GetKvStr(kv, "layer")) == EdaLayerId::BottomCopper);
                c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
                out.components.push_back(c);
            }
        }
    }

    // 4. Tracks6/Data (binary recordType == 4 OR pipe-delimited ASCII fallback)
    if (const auto* trkEnt = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Tracks6/Data")) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *trkEnt, bytes, 32 * 1024 * 1024) && !bytes.empty()) {
            size_t pos = 0;
            size_t binTracks = 0;
            while (pos + 5 <= bytes.size() && out.traces.size() < 80000) {
                uint8_t recType = bytes[pos];
                uint32_t blkLen = DetailEda100::RdU32LE(bytes.data() + pos + 1) & 0x00FFFFFFu;
                if (recType != 4 || blkLen < 33 || blkLen > bytes.size() - (pos + 5)) break;
                const uint8_t* pay = bytes.data() + pos + 5;
                EdaLayerId lid = MapAltiumPcbLayerByte(pay[0]);
                uint16_t netRaw = DetailEda100::RdU16LE(pay + 3);
                int netId = (netRaw == 0xFFFFu) ? 0 : static_cast<int>(netRaw + 1);
                float x1 = static_cast<float>(DetailEda100::RdI32LE(pay + 13)) * kAltiumUnitToMm;
                float y1 = static_cast<float>(DetailEda100::RdI32LE(pay + 17)) * kAltiumUnitToMm;
                float x2 = static_cast<float>(DetailEda100::RdI32LE(pay + 21)) * kAltiumUnitToMm;
                float y2 = static_cast<float>(DetailEda100::RdI32LE(pay + 25)) * kAltiumUnitToMm;
                float w  = std::max(0.06f, static_cast<float>(DetailEda100::RdI32LE(pay + 29)) * kAltiumUnitToMm);
                out.traces.push_back({ x1, y1, x2, y2, w, lid, netId });
                pos += 5 + blkLen;
                ++binTracks;
            }
            if (binTracks == 0) {
                std::vector<KvMap> tRecs;
                WalkLengthPrefixedBlocks(bytes, tRecs, false);
                for (const auto& kv : tRecs) {
                    if (kv.find("x1") != kv.end() && kv.find("x2") != kv.end()) {
                        float x1 = ParsePcbCoordStrMm(GetKvStr(kv, "x1"));
                        float y1 = ParsePcbCoordStrMm(GetKvStr(kv, "y1"));
                        float x2 = ParsePcbCoordStrMm(GetKvStr(kv, "x2"));
                        float y2 = ParsePcbCoordStrMm(GetKvStr(kv, "y2"));
                        float w  = std::max(0.10f, ParsePcbCoordStrMm(GetKvStr(kv, "width"), 0.25f));
                        out.traces.push_back({ x1, y1, x2, y2, w, MapAltiumPcbLayerStr(GetKvStr(kv, "layer")), 1 });
                    }
                }
            }
        }
    }

    // 5. Vias6/Data (recordType == 3)
    if (const auto* viaEnt = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Vias6/Data")) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *viaEnt, bytes, 16 * 1024 * 1024)) {
            size_t pos = 0;
            while (pos + 5 <= bytes.size() && out.vias.size() < 40000) {
                uint8_t recType = bytes[pos];
                uint32_t blkLen = DetailEda100::RdU32LE(bytes.data() + pos + 1) & 0x00FFFFFFu;
                if (recType != 3 || blkLen < 29 || blkLen > bytes.size() - (pos + 5)) break;
                const uint8_t* pay = bytes.data() + pos + 5;
                uint16_t netRaw = DetailEda100::RdU16LE(pay + 3);
                float vx = static_cast<float>(DetailEda100::RdI32LE(pay + 13)) * kAltiumUnitToMm;
                float vy = static_cast<float>(DetailEda100::RdI32LE(pay + 17)) * kAltiumUnitToMm;
                float diam = std::max(0.25f, static_cast<float>(DetailEda100::RdI32LE(pay + 21)) * kAltiumUnitToMm);
                float hole = std::max(0.12f, static_cast<float>(DetailEda100::RdI32LE(pay + 25)) * kAltiumUnitToMm);
                out.vias.push_back({ vx, vy, diam, hole, (netRaw == 0xFFFFu) ? 0 : static_cast<int>(netRaw + 1) });
                pos += 5 + blkLen;
            }
        }
    }

    // 6. Pads6/Data (recordType == 2 + subrecords)
    if (const auto* padEnt = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Pads6/Data")) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *padEnt, bytes, 32 * 1024 * 1024)) {
            size_t pos = 0;
            while (pos + 5 <= bytes.size() && out.pads.size() < 60000) {
                if (bytes[pos] != 2) break;
                ++pos;
                auto readSub = [&](const uint8_t*& outPtr, uint32_t& outLen) -> bool {
                    if (pos + 4 > bytes.size()) return false;
                    outLen = DetailEda100::RdU32LE(bytes.data() + pos) & 0x00FFFFFFu;
                    pos += 4;
                    if (outLen > bytes.size() - pos) return false;
                    outPtr = bytes.data() + pos;
                    pos += outLen;
                    return true;
                };
                const uint8_t* s0 = nullptr; uint32_t l0 = 0;
                const uint8_t* s1 = nullptr; uint32_t l1 = 0;
                if (!readSub(s0, l0) || !readSub(s1, l1)) break;

                std::wstring padName;
                if (l0 >= 1) {
                    size_t strOff = (l0 > 1 && s0[0] == l0 - 1) ? 1 : 0;
                    size_t strLen = l0 - strOff;
                    while (strLen > 0 && s0[strOff + strLen - 1] == 0) --strLen;
                    padName = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(s0 + strOff), strLen));
                }

                const uint8_t* geom = nullptr;
                uint32_t geomLen = 0;
                if (l1 >= 33) {
                    // Compact 2-subrecord pad representation (designator + geometry)
                    geom = s1;
                    geomLen = l1;
                } else {
                    // Standard Altium 6-subrecord pad representation (sub0..sub3 strings, sub4 geometry, sub5 stack)
                    const uint8_t *s2 = nullptr, *s3 = nullptr, *s4 = nullptr, *s5 = nullptr;
                    uint32_t l2 = 0, l3 = 0, l4 = 0, l5 = 0;
                    if (!readSub(s2, l2) || !readSub(s3, l3) || !readSub(s4, l4)) break;
                    geom = s4;
                    geomLen = l4;
                    // Optional 6th subrecord if next byte is not start of next pad record
                    if (pos + 4 <= bytes.size() && (pos + 4 == bytes.size() || bytes[pos] != 2 ||
                        (DetailEda100::RdU32LE(bytes.data() + pos + 1) & 0x00FFFFFFu) > 64))
                    {
                        readSub(s5, l5);
                    }
                }

                if (geom && geomLen >= 29) {
                    EdaPadPrim p;
                    p.layer = MapAltiumPcbLayerByte(geom[0]);
                    p.x = static_cast<float>(DetailEda100::RdI32LE(geom + 13)) * kAltiumUnitToMm;
                    p.y = static_cast<float>(DetailEda100::RdI32LE(geom + 17)) * kAltiumUnitToMm;
                    p.w = std::max(0.15f, static_cast<float>(DetailEda100::RdI32LE(geom + 21)) * kAltiumUnitToMm);
                    p.h = std::max(0.15f, static_cast<float>(DetailEda100::RdI32LE(geom + 25)) * kAltiumUnitToMm);
                    if (geomLen >= 49) {
                        p.drill = std::max(0.0f, static_cast<float>(DetailEda100::RdI32LE(geom + 45)) * kAltiumUnitToMm);
                        uint8_t sh = (geomLen >= 50) ? geom[49] : 2;
                        p.shape = (sh == 1) ? 0 : ((sh == 2) ? 1 : 2);
                    } else if (geomLen >= 33) {
                        p.drill = std::max(0.0f, static_cast<float>(DetailEda100::RdI32LE(geom + 29)) * kAltiumUnitToMm);
                        p.shape = (p.drill > 0.05f) ? 0 : 1;
                    }
                    if (p.drill > 0.05f) p.layer = EdaLayerId::PadsThrough;
                    if (geomLen >= 60) {
                        double rotD = 0.0;
                        std::memcpy(&rotD, geom + 52, 8);
                        if (std::isfinite(rotD) && std::fabs(rotD) <= 360.0) p.rotDeg = static_cast<float>(rotD);
                    }
                    p.pinName = padName;
                    out.pads.push_back(p);
                }
            }
        }
    }

    // 7. Arcs6/Data (recordType == 1)
    if (const auto* arcEnt = Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"Arcs6/Data")) {
        std::vector<uint8_t> bytes;
        if (Explorer2::Ole2::ExtractStream(cf, *arcEnt, bytes, 16 * 1024 * 1024)) {
            size_t pos = 0;
            while (pos + 5 <= bytes.size() && out.traces.size() < 80000) {
                uint8_t recType = bytes[pos];
                uint32_t blkLen = DetailEda100::RdU32LE(bytes.data() + pos + 1) & 0x00FFFFFFu;
                if (recType != 1 || blkLen < 45 || blkLen > bytes.size() - (pos + 5)) break;
                const uint8_t* pay = bytes.data() + pos + 5;
                EdaLayerId lid = MapAltiumPcbLayerByte(pay[0]);
                float cx = static_cast<float>(DetailEda100::RdI32LE(pay + 13)) * kAltiumUnitToMm;
                float cy = static_cast<float>(DetailEda100::RdI32LE(pay + 17)) * kAltiumUnitToMm;
                float r  = std::max(0.1f, static_cast<float>(DetailEda100::RdI32LE(pay + 21)) * kAltiumUnitToMm);
                double a0Deg = 0.0, a1Deg = 360.0;
                std::memcpy(&a0Deg, pay + 25, 8);
                std::memcpy(&a1Deg, pay + 33, 8);
                float w = std::max(0.08f, static_cast<float>(DetailEda100::RdI32LE(pay + 41)) * kAltiumUnitToMm);
                if (!std::isfinite(a0Deg) || !std::isfinite(a1Deg) || std::fabs(a1Deg - a0Deg) >= 359.5) {
                    out.circles.push_back({ cx, cy, r, w, false, lid });
                } else {
                    constexpr double kDeg2Rad = 3.141592653589793 / 180.0;
                    float xs = cx + r * static_cast<float>(std::cos(a0Deg * kDeg2Rad));
                    float ys = cy + r * static_cast<float>(std::sin(a0Deg * kDeg2Rad));
                    float xe = cx + r * static_cast<float>(std::cos(a1Deg * kDeg2Rad));
                    float ye = cy + r * static_cast<float>(std::sin(a1Deg * kDeg2Rad));
                    DetailEda100::AddTessellatedArc(out, xs, ys, xe, ye, cx, cy, false, w, lid, 0);
                }
                pos += 5 + blkLen;
            }
        }
    }

    if (out.traces.empty() && out.pads.empty() && out.vias.empty() && out.components.empty() && out.circles.empty()) {
        return false;
    }
    out.netCount = std::max<size_t>(1, netCount > 0 ? netCount : out.components.size());
    return true;
}

} // namespace DetailAltium

inline bool ParseAltiumDocBoard(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (!data || size < 8 || (extLower != L".pcbdoc" && extLower != L".schdoc")) return false;
    bool isSch = (extLower == L".schdoc");
    out.kind = isSch ? EdaDocKind::EagleSch : EdaDocKind::KiCadPcb;
    out.formatTitle = isSch ? L"Altium Designer Schematic (.SchDoc)" : L"Altium Designer 3D PCB (.PcbDoc)";

    // Case 1: Native Microsoft OLE2 Compound File Binary (D0 CF 11 E0 A1 B1 1A E1)
    static const uint8_t kOle2Magic[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    if (size >= 512 && std::memcmp(data, kOle2Magic, 8) == 0) {
        Explorer2::Ole2::CompoundFile cf;
        if (!Explorer2::Ole2::ParseCompoundFile(data, size, cf)) return false;
        if (isSch || Explorer2::Ole2::FindStreamCaseInsensitive(cf, L"FileHeader") != nullptr) {
            out.kind = EdaDocKind::EagleSch;
            out.formatTitle = L"Altium Designer Schematic (.SchDoc)";
            if (DetailAltium::ParseAltiumSchOle2(cf, out)) return true;
        }
        out.kind = EdaDocKind::KiCadPcb;
        out.formatTitle = L"Altium Designer 3D PCB (.PcbDoc)";
        return DetailAltium::ParseAltiumPcbOle2(cf, out);
    }

    // Case 2: Pipe-delimited ASCII Altium export (|RECORD=...|...)
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (!DetailEda100::ContainsAsciiI(sv, "RECORD=")) return false;

    std::vector<DetailAltium::KvMap> asciiRecords;
    size_t pos = 0;
    while (pos < sv.size() && asciiRecords.size() < 20000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        if (!DetailEda100::ContainsAsciiI(ln, "RECORD=")) continue;
        DetailAltium::KvMap kv = DetailAltium::ParsePipeRecord(ln);
        if (!kv.empty()) asciiRecords.push_back(std::move(kv));
    }
    if (asciiRecords.empty()) return false;

    if (isSch) {
        return DetailAltium::ProcessSchRecords(asciiRecords, out);
    }

    // ASCII .PcbDoc records: Track, Pad, Via, Arc, Component
    for (const auto& kv : asciiRecords) {
        std::string rec = PathUtils::ToLowerAscii(DetailAltium::GetKvStr(kv, "record"));
        if (rec == "track") {
            float x1 = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "x1"));
            float y1 = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "y1"));
            float x2 = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "x2"));
            float y2 = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "y2"));
            float w  = std::max(0.08f, DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "width"), 0.25f));
            out.traces.push_back({ x1, y1, x2, y2, w, DetailAltium::MapAltiumPcbLayerStr(DetailAltium::GetKvStr(kv, "layer")), 1 });
        } else if (rec == "via") {
            float vx = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "x"));
            float vy = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "y"));
            float d  = std::max(0.25f, DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "diameter"), 0.8f));
            float h  = std::max(0.12f, DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "holesize"), 0.4f));
            out.vias.push_back({ vx, vy, d, h, 1 });
        } else if (rec == "pad") {
            EdaPadPrim p;
            p.x = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "x"));
            p.y = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "y"));
            p.w = std::max(0.2f, DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "xsize"), 1.2f));
            p.h = std::max(0.2f, DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "ysize"), 1.2f));
            p.drill = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "holesize"), 0.0f);
            p.layer = (p.drill > 0.05f) ? EdaLayerId::PadsThrough : DetailAltium::MapAltiumPcbLayerStr(DetailAltium::GetKvStr(kv, "layer"));
            p.shape = 1;
            p.pinName = PathUtils::Utf8ToWide(DetailAltium::GetKvStr(kv, "name"));
            out.pads.push_back(p);
        } else if (rec == "component") {
            std::string des = DetailAltium::GetKvStr(kv, "sourcedesignator");
            if (des.empty()) des = DetailAltium::GetKvStr(kv, "designator");
            if (des.empty()) des = DetailAltium::GetKvStr(kv, "name");
            if (!des.empty()) {
                EdaComponent3D c;
                c.refDes = PathUtils::Utf8ToWide(des);
                c.value = PathUtils::Utf8ToWide(DetailAltium::GetKvStr(kv, "comment"));
                c.footprint = PathUtils::Utf8ToWide(DetailAltium::GetKvStr(kv, "pattern"));
                c.x = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "x"));
                c.y = DetailAltium::ParsePcbCoordStrMm(DetailAltium::GetKvStr(kv, "y"));
                c.rotDeg = static_cast<float>(std::strtod(DetailAltium::GetKvStr(kv, "rotation").c_str(), nullptr));
                c.bottomSide = (DetailAltium::MapAltiumPcbLayerStr(DetailAltium::GetKvStr(kv, "layer")) == EdaLayerId::BottomCopper);
                c.category = ClassifyComponent(c.refDes, c.footprint, c.widthMm, c.lengthMm, c.heightMm, c.pinCount);
                out.components.push_back(c);
            }
        }
    }

    if (out.traces.empty() && out.pads.empty() && out.vias.empty() && out.components.empty()) {
        return DetailAltium::ProcessSchRecords(asciiRecords, out);
    }
    out.netCount = std::max<size_t>(1, out.components.size());
    return true;
}

// ============================================================================
// 80 & 81. Vector CAN Database (.dbc), CAN ASCII Log (.asc/.can) & Binary BLF (.blf)
// ============================================================================
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

inline void WalkBlfObjects(const uint8_t* buf, size_t sz, std::vector<CanFrame>& outFrames, int depth) {
    if (!buf || sz < 16 || depth > 4) return;
    size_t pos = 0;
    while (pos + 16 <= sz && outFrames.size() < 20000) {
        if (std::memcmp(buf + pos, "LOBJ", 4) != 0) {
            // Try 4-byte alignment step if previous object wasn't padded
            size_t next = (pos + 3) & ~size_t(3);
            if (next > pos && next + 16 <= sz && std::memcmp(buf + next, "LOBJ", 4) == 0) {
                pos = next;
            } else {
                break;
            }
        }
        uint16_t hdrSize = DetailEda100::RdU16LE(buf + pos + 4);
        uint32_t objSize = DetailEda100::RdU32LE(buf + pos + 8);
        uint32_t objType = DetailEda100::RdU32LE(buf + pos + 12);
        if (hdrSize < 16 || objSize < hdrSize || objSize > sz - pos) break;

        if (objType == 10) { // BL_OBJ_TYPE_LOG_CONTAINER
            if (objSize > static_cast<uint32_t>(hdrSize) + 16u) {
                const uint8_t* cpay = buf + pos + hdrSize;
                size_t cpayLen = objSize - hdrSize;
                uint16_t compMethod = DetailEda100::RdU16LE(cpay);
                uint32_t uncompLen  = DetailEda100::RdU32LE(cpay + 8);
                const uint8_t* inner = cpay + 16;
                size_t innerLen = cpayLen - 16;
                if (compMethod == 0) {
                    WalkBlfObjects(inner, innerLen, outFrames, depth + 1);
                } else if (compMethod == 2 && innerLen >= 2) {
                    size_t cap = std::clamp<size_t>(uncompLen > 0 ? uncompLen + 64 : 16 * 1024 * 1024, 1024, 32 * 1024 * 1024);
                    std::vector<uint8_t> decomp;
                    if (ZipDeflate::InflateZlib(inner, innerLen, decomp, cap) && !decomp.empty()) {
                        WalkBlfObjects(decomp.data(), decomp.size(), outFrames, depth + 1);
                    }
                }
            }
        } else if ((objType == 1 || objType == 86 || objType == 100 || objType == 101) && hdrSize >= 32) {
            uint32_t objFlags = DetailEda100::RdU32LE(buf + pos + 16);
            uint64_t tsRaw    = DetailEda100::RdU64LE(buf + pos + 24);
            double tsSec = (objFlags == 1) ? (static_cast<double>(tsRaw) * 1e-5) : (static_cast<double>(tsRaw) * 1e-9);
            const uint8_t* pay = buf + pos + hdrSize;
            size_t payLen = objSize - hdrSize;

            if ((objType == 1 || objType == 86) && payLen >= 16) { // CAN_MESSAGE / CAN_MESSAGE2
                CanFrame f;
                f.timestamp = tsSec;
                f.channel = DetailEda100::RdU16LE(pay);
                f.isTx = (pay[2] & 0x01) != 0;
                f.dlcBytes = std::min<uint8_t>(pay[3], 8);
                uint32_t rawId = DetailEda100::RdU32LE(pay + 4);
                f.extId = (rawId & 0x80000000u) != 0;
                f.canId = rawId & 0x1FFFFFFFu;
                std::memcpy(f.data, pay + 8, f.dlcBytes);
                outFrames.push_back(f);
            } else if (objType == 100 && payLen >= 20) { // CAN_FD_MESSAGE
                CanFrame f;
                f.timestamp = tsSec;
                f.channel = DetailEda100::RdU16LE(pay);
                f.isTx = (pay[2] & 0x01) != 0;
                f.isFd = true;
                uint8_t validBytes = (pay[14] > 0) ? pay[14] : CanFdDlcToBytes(pay[3]);
                f.dlcBytes = static_cast<uint8_t>(std::min<size_t>({ validBytes, size_t(64), payLen - 20 }));
                uint32_t rawId = DetailEda100::RdU32LE(pay + 4);
                f.extId = (rawId & 0x80000000u) != 0;
                f.canId = rawId & 0x1FFFFFFFu;
                if (f.dlcBytes > 0) std::memcpy(f.data, pay + 20, f.dlcBytes);
                outFrames.push_back(f);
            } else if (objType == 101 && payLen >= 40) { // CAN_FD_MESSAGE_64
                CanFrame f;
                f.timestamp = tsSec;
                f.channel = pay[0];
                f.isFd = true;
                uint8_t validBytes = (pay[2] > 0) ? pay[2] : CanFdDlcToBytes(pay[1]);
                f.dlcBytes = static_cast<uint8_t>(std::min<size_t>({ validBytes, size_t(64), payLen - 40 }));
                uint32_t rawId = DetailEda100::RdU32LE(pay + 4);
                f.extId = (rawId & 0x80000000u) != 0;
                f.canId = rawId & 0x1FFFFFFFu;
                if (f.dlcBytes > 0) std::memcpy(f.data, pay + 40, f.dlcBytes);
                outFrames.push_back(f);
            }
        }

        size_t step = (objSize + 3u) & ~size_t(3);
        if (pos + objSize + 4 <= sz && std::memcmp(buf + pos + objSize, "LOBJ", 4) == 0) {
            pos += objSize;
        } else if (step <= sz - pos) {
            pos += step;
        } else {
            break;
        }
    }
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

inline bool ParseVectorCanBoard(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (!data || size < 8) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), size);

    // 1. Vector Binary Logging Format (.blf, "LOGG" magic)
    if (extLower == L".blf" || (size >= 72 && std::memcmp(data, "LOGG", 4) == 0)) {
        if (size < 72 || std::memcmp(data, "LOGG", 4) != 0) return false;
        uint32_t hdrSize = DetailEda100::RdU32LE(data + 4);
        if (hdrSize < 32 || hdrSize > size) return false;

        std::vector<DetailCan::CanFrame> frames;
        DetailCan::WalkBlfObjects(data + hdrSize, size - hdrSize, frames, 0);
        return DetailCan::RenderCanFramesToBoard(frames, L"Vector BLF CAN/CAN-FD Log", out);
    }

    // 2. Vector CAN Database (.dbc)
    if (extLower == L".dbc" || (extLower != L".asc" && sv.find("BO_ ") != std::string_view::npos)) {
        struct DbcSig {
            std::string name;
            int startBit = 0;
            int bitSize = 8;
            bool littleEndian = true;
            double scale = 1.0, offset = 0.0;
            std::string unit;
        };
        struct DbcMsg {
            uint32_t canId = 0;
            bool extId = false;
            int dlc = 8;
            std::string name;
            std::string txNode;
            std::vector<DbcSig> sigs;
        };
        std::vector<DbcMsg> msgs;

        size_t pos = 0;
        while (pos < sv.size() && msgs.size() < 24) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view ln = sv.substr(pos, eol - pos);
            pos = eol + 1;
            while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
            while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
            if (ln.empty()) continue;

            if (ln.rfind("BO_ ", 0) == 0) {
                unsigned int rawId = 0;
                char msgName[64] = {}, txNode[64] = {};
                int dlc = 8;
                std::string tmp(ln.substr(4));
                if (sscanf_s(tmp.c_str(), "%u %63[^:]: %d %63s",
                    &rawId, msgName, static_cast<unsigned>(sizeof(msgName)),
                    &dlc, txNode, static_cast<unsigned>(sizeof(txNode))) >= 2)
                {
                    DbcMsg m;
                    m.extId = (rawId & 0x80000000u) != 0;
                    m.canId = rawId & 0x1FFFFFFFu;
                    m.dlc = std::clamp(dlc, 1, 64);
                    m.name = DetailEda100::TrimAsciiView(msgName);
                    m.txNode = txNode;
                    msgs.push_back(std::move(m));
                }
            } else if (ln.rfind("SG_ ", 0) == 0 && !msgs.empty()) {
                size_t colon = ln.find(':');
                if (colon != std::string_view::npos) {
                    std::string_view hdrPart = ln.substr(4, colon - 4);
                    size_t nameEnd = hdrPart.find_first_of(" \t");
                    std::string sigName = DetailEda100::TrimAsciiView(hdrPart.substr(0, nameEnd));
                    std::string spec(ln.substr(colon + 1));

                    int startBit = 0, bitSize = 8;
                    char byteOrd = '1', signCh = '+';
                    double sc = 1.0, off = 0.0;
                    if (sscanf_s(spec.c_str(), " %d|%d@%c%c (%lf,%lf)",
                        &startBit, &bitSize, &byteOrd, 1, &signCh, 1, &sc, &off) >= 4)
                    {
                        DbcSig s;
                        s.name = sigName;
                        s.startBit = std::clamp(startBit, 0, 511);
                        s.bitSize = std::clamp(bitSize, 1, 512);
                        s.littleEndian = (byteOrd == '1');
                        s.scale = sc;
                        s.offset = off;
                        size_t q1 = spec.find('"');
                        if (q1 != std::string::npos) {
                            size_t q2 = spec.find('"', q1 + 1);
                            if (q2 != std::string::npos) s.unit = spec.substr(q1 + 1, q2 - q1 - 1);
                        }
                        msgs.back().sigs.push_back(std::move(s));
                    }
                }
            }
        }

        if (msgs.empty()) return false;

        out.kind = EdaDocKind::KiCadSch;
        size_t totalSigs = 0;
        for (const auto& m : msgs) totalSigs += m.sigs.size();
        out.formatTitle = L"Vector CAN Database (.dbc — " + std::to_wstring(msgs.size()) +
                          L" msgs, " + std::to_wstring(totalSigs) + L" signals)";

        // Differential CAN_H / CAN_L bus rails at top
        out.traces.push_back({ -48.0f, 30.0f, 48.0f, 30.0f, 0.42f, EdaLayerId::TopCopper, 1 });
        out.traces.push_back({ -48.0f, 27.5f, 48.0f, 27.5f, 0.42f, EdaLayerId::BottomCopper, 2 });
        out.labels.push_back({ -44.0f, 31.6f, 1.0f, EdaLayerId::SchLabel, L"CAN_H / CAN_L Differential Bus" });

        for (size_t mi = 0; mi < msgs.size(); ++mi) {
            const auto& m = msgs[mi];
            float yRow = 18.0f - static_cast<float>(mi) * 11.0f;
            int totalBits = std::max(8, m.dlc * 8);

            wchar_t idHex[48] = {};
            swprintf_s(idHex, m.extId ? L"0x%08X [%dB]" : L"0x%03X [%dB]", m.canId, m.dlc);

            EdaComponent3D node;
            node.refDes = PathUtils::Utf8ToWide(m.name);
            node.value = idHex;
            node.footprint = PathUtils::Utf8ToWide(m.txNode.empty() ? "CAN_ECU" : m.txNode);
            node.x = -36.0f;
            node.y = yRow;
            node.widthMm = 18.0f;
            node.lengthMm = 6.5f;
            node.heightMm = 1.5f;
            node.category = EdaPartCategory::IC_Package;
            out.components.push_back(node);
            DetailEda100::AddRectOutline(out, node.x, node.y, node.widthMm, node.lengthMm, EdaLayerId::SchSymbol, 0.28f);
            out.labels.push_back({ node.x, yRow + 1.0f, 1.0f, EdaLayerId::SchLabel, node.refDes });
            out.labels.push_back({ node.x, yRow - 1.5f, 0.85f, EdaLayerId::TopSilk, idHex });

            // Bitfield payload bar (-22.0f .. +46.0f = 68.0 mm for `totalBits`)
            const float barX0 = -22.0f, barW = 68.0f, barH = 5.4f;
            DetailEda100::AddRectOutline(out, barX0 + barW * 0.5f, yRow, barW, barH, EdaLayerId::EdgeCuts, 0.22f);
            for (int b = 1; b < m.dlc && b < 16; ++b) {
                float bx = barX0 + (static_cast<float>(b * 8) / static_cast<float>(totalBits)) * barW;
                out.traces.push_back({ bx, yRow - barH * 0.5f, bx, yRow + barH * 0.5f, 0.10f, EdaLayerId::BottomSilk, 0 });
            }

            for (size_t si = 0; si < m.sigs.size(); ++si) {
                const auto& s = m.sigs[si];
                int effStart = s.littleEndian ? s.startBit : std::max(0, s.startBit - s.bitSize + 1);
                float sx0 = barX0 + (static_cast<float>(std::min(effStart, totalBits - 1)) / static_cast<float>(totalBits)) * barW;
                float sw  = std::max(1.2f, (static_cast<float>(std::min(s.bitSize, totalBits)) / static_cast<float>(totalBits)) * barW - 0.3f);
                sx0 = std::min(sx0, barX0 + barW - sw);
                float scx = sx0 + sw * 0.5f;

                EdaLayerId slid = (si % 2 == 0) ? EdaLayerId::TopCopper : EdaLayerId::SchWire;
                DetailEda100::AddRectOutline(out, scx, yRow, sw, barH - 1.0f, slid, 0.30f);
                out.pads.push_back({ scx, yRow, std::max(0.8f, sw - 0.6f), barH - 1.8f, 0.0f, 0.0f, 1,
                    s.littleEndian ? EdaLayerId::TopCopper : EdaLayerId::BottomCopper,
                    PathUtils::Utf8ToWide(s.name), PathUtils::Utf8ToWide(s.unit) });

                wchar_t sigLbl[96] = {};
                swprintf_s(sigLbl, L"%s [%d:%d%s *%.3g%+g]",
                    PathUtils::Utf8ToWide(s.name).c_str(), s.startBit, s.bitSize,
                    s.littleEndian ? L"LE" : L"BE", s.scale, s.offset);
                out.labels.push_back({ scx, yRow + ((si % 2 == 0) ? 3.5f : -3.6f), 0.8f, EdaLayerId::SchLabel, sigLbl });
            }
        }
        out.netCount = msgs.size() + 2;
        return true;
    }

    // 3. Vector CAN / CAN-FD ASCII Log (.asc / .can)
    if (extLower == L".asc" || extLower == L".can") {
        bool baseDec = false;
        std::vector<DetailCan::CanFrame> frames;
        size_t pos = 0;
        while (pos < sv.size() && frames.size() < 20000) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view ln = sv.substr(pos, eol - pos);
            pos = eol + 1;
            while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
            while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
            if (ln.empty() || ln[0] == ';' || ln.rfind("//", 0) == 0) continue;

            if (DetailEda100::ContainsAsciiI(ln, "base dec")) { baseDec = true; continue; }
            if (DetailEda100::ContainsAsciiI(ln, "base hex")) { baseDec = false; continue; }
            if (!((ln[0] >= '0' && ln[0] <= '9') || ln[0] == '.')) continue;

            std::vector<std::string_view> toks;
            size_t tp = 0;
            while (tp < ln.size() && toks.size() < 80) {
                while (tp < ln.size() && (ln[tp] == ' ' || ln[tp] == '\t')) ++tp;
                size_t te = ln.find_first_of(" \t", tp);
                if (te == std::string_view::npos) te = ln.size();
                if (te > tp) toks.push_back(ln.substr(tp, te - tp));
                tp = te;
            }
            if (toks.size() < 6) continue;

            char tbuf[32] = {};
            std::memcpy(tbuf, toks[0].data(), std::min<size_t>(toks[0].size(), 31));
            double ts = std::strtod(tbuf, nullptr);

            // CANFD line: <ts> CANFD <ch> Rx|Tx <id> [<sym>] <brs> <esi> <dlc> <dataLen> <b0> ...
            if (DetailEda100::ContainsAsciiI(toks[1], "CANFD") && toks.size() >= 10) {
                DetailCan::CanFrame f;
                f.timestamp = ts;
                f.isFd = true;
                f.channel = static_cast<uint16_t>(std::atoi(std::string(toks[2]).c_str()));
                f.isTx = DetailEda100::ContainsAsciiI(toks[3], "Tx");
                std::string idStr(toks[4]);
                if (!idStr.empty() && (idStr.back() == 'x' || idStr.back() == 'X')) {
                    f.extId = true;
                    idStr.pop_back();
                }
                f.canId = static_cast<uint32_t>(std::strtoul(idStr.c_str(), nullptr, baseDec ? 10 : 16)) & 0x1FFFFFFFu;
                // Find dataLen token (toks[9] when no symbolic name, or toks[10] with symbolic name)
                size_t dlIdx = 9;
                if (toks.size() > 10 && toks[5].size() > 1 && !isdigit(static_cast<unsigned char>(toks[5][0]))) dlIdx = 10;
                if (dlIdx < toks.size()) {
                    int dlen = std::clamp(std::atoi(std::string(toks[dlIdx]).c_str()), 0, 64);
                    f.dlcBytes = static_cast<uint8_t>(std::min<size_t>(dlen, toks.size() - dlIdx - 1));
                    for (uint8_t b = 0; b < f.dlcBytes; ++b) {
                        f.data[b] = static_cast<uint8_t>(std::strtoul(std::string(toks[dlIdx + 1 + b]).c_str(), nullptr, baseDec ? 10 : 16));
                    }
                    frames.push_back(f);
                }
                continue;
            }

            // Classic CAN frame: <ts> <ch> <id>[x] Rx|Tx d <dlc> <b0> ...
            if ((DetailEda100::ContainsAsciiI(toks[3], "Rx") || DetailEda100::ContainsAsciiI(toks[3], "Tx")) &&
                (toks[4] == "d" || toks[4] == "D"))
            {
                DetailCan::CanFrame f;
                f.timestamp = ts;
                f.channel = static_cast<uint16_t>(std::atoi(std::string(toks[1]).c_str()));
                std::string idStr(toks[2]);
                if (!idStr.empty() && (idStr.back() == 'x' || idStr.back() == 'X')) {
                    f.extId = true;
                    idStr.pop_back();
                }
                char* idEnd = nullptr;
                unsigned long parsedId = std::strtoul(idStr.c_str(), &idEnd, baseDec ? 10 : 16);
                if (idEnd == idStr.c_str()) continue;
                f.canId = static_cast<uint32_t>(parsedId) & 0x1FFFFFFFu;
                f.isTx = DetailEda100::ContainsAsciiI(toks[3], "Tx");
                int dlc = std::clamp(std::atoi(std::string(toks[5]).c_str()), 0, 64);
                f.dlcBytes = static_cast<uint8_t>(std::min<size_t>(dlc, toks.size() - 6));
                for (uint8_t b = 0; b < f.dlcBytes; ++b) {
                    f.data[b] = static_cast<uint8_t>(std::strtoul(std::string(toks[6 + b]).c_str(), nullptr, baseDec ? 10 : 16));
                }
                frames.push_back(f);
            }
        }
        return DetailCan::RenderCanFramesToBoard(frames, L"Vector CAN ASCII Trace (.asc)", out);
    }

    return false;
}

inline bool ParseVectorCanBoard(std::string_view sv, const std::wstring& extLower, ParsedEdaBoard& out) {
    return ParseVectorCanBoard(reinterpret_cast<const uint8_t*>(sv.data()), sv.size(), extLower, out);
}

// ============================================================================
// 82–86. Waveforms (.fst / .ghw), FPGA (.bit / .rpd), DFU (.dfu / .ufd), EDID (.edid), SPD (.spd)
// ============================================================================
namespace DetailHwBin {

struct DecodedWaveSignal {
    std::wstring name;
    int bitWidth = 1;
    std::vector<std::pair<uint64_t, std::string>> changes; // (timestamp, state e.g. "0", "1", "x", "z", or multi-bit)
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
    }
    double spanT = (maxT > minT) ? static_cast<double>(maxT - minT) : 100.0;
    const float x0 = -24.0f, plotW = 84.0f;
    size_t maxRows = std::min<size_t>(sigs.size(), 24);

    for (size_t r = 0; r < maxRows; ++r) {
        const auto& s = sigs[r];
        float yMid = 24.0f - static_cast<float>(r) * 7.5f;
        out.labels.push_back({ -36.0f, yMid, 1.05f, EdaLayerId::SchLabel, s.name });

        EdaLayerId lid = (r % 2 == 0) ? EdaLayerId::TopCopper : EdaLayerId::SchWire;
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

// Authentic GTKWave FST (.fst) Binary Waveform Reader
inline bool ParseGtkWaveFst(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 33) return false;
    if (data[0] != 0) return false; // Must begin with FST_BL_HDR (0)

    struct FstHandleDef {
        std::wstring name;
        uint32_t bitLen = 1;
        bool isReal = false;
        std::vector<std::pair<uint64_t, std::string>> changes;
    };
    std::vector<FstHandleDef> handles; // index 0 = handle 1
    uint64_t hdrStartTime = 0, hdrEndTime = 0;
    std::string writerName;
    int8_t timescale = -9;
    bool sawValidHdr = false;

    size_t pos = 0;
    while (pos + 9 <= size) {
        uint8_t blkType = data[pos];
        uint64_t blkLen = DetailEda100::RdU64BE(data + pos + 1);
        if (blkLen < 8) break;

        // In fstapi.c, seclen includes the 8-byte length field (next = pos + 1 + blkLen).
        // Also gracefully handle writers where blkLen is payload-only if 1 + blkLen overshoots or doesn't land cleanly.
        size_t payStart = pos + 9;
        size_t payLen = 0;
        size_t nextPos = 0;
        if (blkLen - 8 <= size - payStart &&
            (pos + 1 + blkLen == size || (pos + 1 + blkLen < size && data[pos + 1 + blkLen] <= 8)))
        {
            payLen = static_cast<size_t>(blkLen - 8);
            nextPos = pos + 1 + static_cast<size_t>(blkLen);
        } else if (blkLen <= size - payStart) {
            payLen = static_cast<size_t>(blkLen);
            nextPos = payStart + payLen;
        } else {
            break;
        }

        const uint8_t* pay = data + payStart;

        if (blkType == 0) { // FST_BL_HDR
            if (payLen < 65) return false;
            hdrStartTime = DetailEda100::RdU64BE(pay + 0);
            hdrEndTime   = DetailEda100::RdU64BE(pay + 8);
            double dLE = 0.0, dBE = 0.0;
            std::memcpy(&dLE, pay + 16, 8);
            uint8_t rev[8] = { pay[23], pay[22], pay[21], pay[20], pay[19], pay[18], pay[17], pay[16] };
            std::memcpy(&dBE, rev, 8);
            bool validE = (std::isfinite(dLE) && std::fabs(dLE - 2.718281828459045) < 1e-3) ||
                          (std::isfinite(dBE) && std::fabs(dBE - 2.718281828459045) < 1e-3);
            if (!validE) return false;
            timescale = static_cast<int8_t>(pay[64]);
            if (payLen >= 193) {
                size_t wlen = 0;
                while (wlen < 128 && pay[65 + wlen] != 0) ++wlen;
                writerName.assign(reinterpret_cast<const char*>(pay + 65), wlen);
            }
            sawValidHdr = true;
        } else if (blkType == 4 || blkType == 5) { // FST_BL_HIER / FST_BL_HIER_GZ
            std::vector<uint8_t> hierBuf;
            const uint8_t* hp = pay;
            size_t hlen = payLen;
            if (blkType == 5) {
                if (payLen < 10) { pos = nextPos; continue; }
                // FST_BL_HIER_GZ begins with uint64_be uncompressed length followed by gzip or zlib stream
                size_t zOff = (pay[8] == 0x1F && pay[9] == 0x8B) || ((pay[8] & 0x0F) == 0x08) ? 8 : 0;
                bool okZ = false;
                if (pay[zOff] == 0x1F && pay[zOff + 1] == 0x8B) {
                    okZ = ZipDeflate::InflateGzip(pay + zOff, payLen - zOff, hierBuf, 32 * 1024 * 1024);
                } else {
                    okZ = ZipDeflate::InflateZlib(pay + zOff, payLen - zOff, hierBuf, 32 * 1024 * 1024);
                }
                if (!okZ || hierBuf.empty()) { pos = nextPos; continue; }
                hp = hierBuf.data();
                hlen = hierBuf.size();
            }

            std::vector<std::string> scopeStack;
            size_t hpos = 0;
            while (hpos < hlen && handles.size() < 4096) {
                uint8_t tag = hp[hpos++];
                if (tag == 254) { // FST_ST_VCD_SCOPE
                    if (hpos >= hlen) break;
                    ++hpos; // scopeType
                    size_t s1 = hpos;
                    while (hpos < hlen && hp[hpos] != 0) ++hpos;
                    std::string sname(reinterpret_cast<const char*>(hp + s1), hpos - s1);
                    if (hpos < hlen) ++hpos;
                    while (hpos < hlen && hp[hpos] != 0) ++hpos; // compName
                    if (hpos < hlen) ++hpos;
                    if (!sname.empty()) scopeStack.push_back(std::move(sname));
                } else if (tag == 255) { // FST_ST_VCD_UPSCOPE
                    if (!scopeStack.empty()) scopeStack.pop_back();
                } else if (tag == 252) { // FST_ST_GEN_ATTRBEGIN
                    if (hpos + 2 > hlen) break;
                    hpos += 2;
                    while (hpos < hlen && hp[hpos] != 0) ++hpos;
                    if (hpos < hlen) ++hpos;
                    DetailEda100::ReadULeb128(hp, hlen, hpos);
                } else if (tag == 253) { // FST_ST_GEN_ATTREND
                    continue;
                } else if (tag <= 30) { // FST_VT_* variable declaration
                    if (hpos >= hlen) break;
                    ++hpos; // varDir
                    size_t n1 = hpos;
                    while (hpos < hlen && hp[hpos] != 0) ++hpos;
                    std::string vname(reinterpret_cast<const char*>(hp + n1), hpos - n1);
                    if (hpos < hlen) ++hpos;
                    uint64_t bitLen = DetailEda100::ReadULeb128(hp, hlen, hpos);
                    uint64_t alias  = DetailEda100::ReadULeb128(hp, hlen, hpos);

                    std::string fullName;
                    if (!scopeStack.empty()) fullName = scopeStack.back() + "." + vname;
                    else fullName = vname;

                    if (alias == 0) {
                        FstHandleDef hd;
                        hd.name = PathUtils::Utf8ToWide(fullName);
                        hd.isReal = (tag == 3 || tag == 4 || tag == 20 || tag == 29);
                        hd.bitLen = hd.isReal ? 64 : static_cast<uint32_t>(std::clamp<uint64_t>(bitLen, 1, 512));
                        handles.push_back(std::move(hd));
                    }
                } else {
                    break;
                }
            }
        } else if ((blkType == 1 || blkType == 3 || blkType == 8) && !handles.empty() && payLen >= 56) {
            // FST_BL_VCDATA / DYN_ALIAS / DYN_ALIAS2
            uint64_t vcStart = DetailEda100::RdU64BE(pay + 0);
            uint64_t vcEnd   = DetailEda100::RdU64BE(pay + 8);
            hdrEndTime = std::max(hdrEndTime, vcEnd);

            // 1. Decode Time Table from end of VCDATA block
            uint64_t timeUncompLen = DetailEda100::RdU64BE(pay + payLen - 24);
            uint64_t timeCompLen   = DetailEda100::RdU64BE(pay + payLen - 16);
            uint64_t timeCount     = DetailEda100::RdU64BE(pay + payLen - 8);
            if (timeCompLen > payLen - 32 || timeCount == 0 || timeCount > 1000000) {
                pos = nextPos;
                continue;
            }
            size_t timeOff = payLen - 24 - static_cast<size_t>(timeCompLen);
            std::vector<uint8_t> timeRaw;
            const uint8_t* tp = pay + timeOff;
            size_t tlen = static_cast<size_t>(timeCompLen);
            if (timeCompLen != timeUncompLen) {
                if (!ZipDeflate::InflateZlib(tp, tlen, timeRaw, 32 * 1024 * 1024)) { pos = nextPos; continue; }
                tp = timeRaw.data();
                tlen = timeRaw.size();
            }
            std::vector<uint64_t> timestamps;
            timestamps.reserve(static_cast<size_t>(std::min<uint64_t>(timeCount, 200000)));
            size_t tcur = 0;
            uint64_t accTime = 0;
            for (uint64_t ti = 0; ti < timeCount && tcur < tlen; ++ti) {
                accTime += DetailEda100::ReadULeb128(tp, tlen, tcur);
                timestamps.push_back(accTime);
            }
            if (timestamps.empty()) timestamps.push_back(vcStart);

            // 2. Decode Initial Value Bits at pay + 24
            size_t vpos = 24;
            uint64_t bitsUncomp = DetailEda100::ReadULeb128(pay, timeOff, vpos);
            uint64_t bitsComp   = DetailEda100::ReadULeb128(pay, timeOff, vpos);
            if (bitsComp > timeOff - vpos) { pos = nextPos; continue; }
            std::vector<uint8_t> initBits;
            if (bitsComp == bitsUncomp) {
                initBits.assign(pay + vpos, pay + vpos + static_cast<size_t>(bitsComp));
            } else if (bitsComp > 0) {
                ZipDeflate::InflateZlib(pay + vpos, static_cast<size_t>(bitsComp), initBits, 16 * 1024 * 1024);
            }
            vpos += static_cast<size_t>(bitsComp);

            size_t bitCursor = 0;
            uint64_t initT = timestamps.front();
            for (auto& h : handles) {
                if (!h.isReal && bitCursor + h.bitLen <= initBits.size()) {
                    std::string initState(reinterpret_cast<const char*>(initBits.data() + bitCursor), h.bitLen);
                    if (h.changes.empty() || h.changes.back().second != initState) {
                        h.changes.push_back({ initT, initState });
                    }
                    bitCursor += h.bitLen;
                }
            }

            // 3. Waves header + Position table just before timeOff
            if (vpos + 2 > timeOff || timeOff < 8) { pos = nextPos; continue; }
            uint64_t wavesCount = DetailEda100::ReadULeb128(pay, timeOff - 8, vpos);
            if (vpos >= timeOff - 8) { pos = nextPos; continue; }
            uint8_t packType = pay[vpos++];
            size_t wavesDataStart = vpos;

            uint64_t posTableLen = DetailEda100::RdU64BE(pay + timeOff - 8);
            if (posTableLen > (timeOff - 8) - wavesDataStart) { pos = nextPos; continue; }
            size_t posTableStart = (timeOff - 8) - static_cast<size_t>(posTableLen);
            size_t wavesDataEnd = posTableStart;

            // Decode per-handle wave chunk lengths from position table
            size_t nHandles = std::min<size_t>(handles.size(), static_cast<size_t>(wavesCount));
            std::vector<int64_t> chunkLens(nHandles, 0); // >0 = byte len, <0 = dynamic alias (-handleIdx1Based)
            size_t pcur = posTableStart;
            if (blkType == 1) {
                for (size_t hi = 0; hi < nHandles && pcur < timeOff - 8; ++hi) {
                    chunkLens[hi] = static_cast<int64_t>(DetailEda100::ReadULeb128(pay, timeOff - 8, pcur));
                }
            } else if (blkType == 3) {
                size_t hi = 0;
                while (hi < nHandles && pcur < timeOff - 8) {
                    uint64_t v = DetailEda100::ReadULeb128(pay, timeOff - 8, pcur);
                    if (v == 0) {
                        uint64_t run = DetailEda100::ReadULeb128(pay, timeOff - 8, pcur);
                        hi += std::max<size_t>(1, static_cast<size_t>(run));
                    } else if ((v & 1ULL) != 0) {
                        chunkLens[hi++] = -static_cast<int64_t>(v >> 1);
                    } else {
                        chunkLens[hi++] = static_cast<int64_t>(v >> 1);
                    }
                }
            } else { // blkType == 8 (DYN_ALIAS2)
                size_t hi = 0;
                while (hi < nHandles && pcur < timeOff - 8) {
                    if (pay[pcur] == 0) {
                        ++pcur;
                        uint64_t run = DetailEda100::ReadULeb128(pay, timeOff - 8, pcur);
                        hi += std::max<size_t>(1, static_cast<size_t>(run));
                    } else {
                        chunkLens[hi++] = DetailEda100::ReadSLeb128(pay, timeOff - 8, pcur);
                    }
                }
            }

            // 4. Decode each handle's wave transitions
            size_t wcur = wavesDataStart;
            for (size_t hi = 0; hi < nHandles; ++hi) {
                if (chunkLens[hi] < 0) {
                    size_t aliasIdx = static_cast<size_t>(-chunkLens[hi]) - 1;
                    if (aliasIdx < hi) handles[hi].changes = handles[aliasIdx].changes;
                    continue;
                }
                if (chunkLens[hi] == 0) continue;
                size_t clen = static_cast<size_t>(chunkLens[hi]);
                if (clen > wavesDataEnd - wcur) break;
                const uint8_t* cptr = pay + wcur;
                wcur += clen;

                size_t subPos = 0;
                uint64_t uncompWaveLen = DetailEda100::ReadULeb128(cptr, clen, subPos);
                std::vector<uint8_t> waveRaw;
                const uint8_t* wp = cptr + subPos;
                size_t wlen = clen - subPos;
                if (uncompWaveLen != 0 && (packType == 'Z' || packType == 0)) {
                    if (!ZipDeflate::InflateZlib(wp, wlen, waveRaw, 16 * 1024 * 1024)) continue;
                    wp = waveRaw.data();
                    wlen = waveRaw.size();
                } else if (uncompWaveLen != 0) {
                    continue; // Unsupported LZ4/FastLZ block
                }

                FstHandleDef& hd = handles[hi];
                if (hd.isReal) continue;
                size_t vp = 0;
                size_t timeIdx = 0;
                while (vp < wlen && hd.changes.size() < 8192) {
                    uint64_t vli = DetailEda100::ReadULeb128(wp, wlen, vp);
                    if (hd.bitLen == 1) {
                        if ((vli & 1ULL) == 0) {
                            timeIdx += static_cast<size_t>(vli >> 2);
                            char bitCh = ((vli >> 1) & 1ULL) ? '1' : '0';
                            if (timeIdx < timestamps.size()) {
                                hd.changes.push_back({ timestamps[timeIdx], std::string(1, bitCh) });
                            }
                        } else {
                            timeIdx += static_cast<size_t>(vli >> 4);
                            static const char kStates[8] = { 'x','z','h','u','w','l','-','?' };
                            char stCh = kStates[(vli >> 1) & 7ULL];
                            if (timeIdx < timestamps.size()) {
                                hd.changes.push_back({ timestamps[timeIdx], std::string(1, stCh) });
                            }
                        }
                    } else {
                        timeIdx += static_cast<size_t>(vli >> 1);
                        std::string bits;
                        bits.reserve(hd.bitLen);
                        if ((vli & 1ULL) == 0) {
                            size_t nBytes = (hd.bitLen + 7u) / 8u;
                            if (vp + nBytes > wlen) break;
                            for (uint32_t bi = 0; bi < hd.bitLen; ++bi) {
                                uint8_t b = wp[vp + (bi / 8u)];
                                int shift = 7 - static_cast<int>(bi % 8u);
                                bits.push_back(((b >> shift) & 1u) ? '1' : '0');
                            }
                            vp += nBytes;
                        } else {
                            if (vp + hd.bitLen > wlen) break;
                            bits.assign(reinterpret_cast<const char*>(wp + vp), hd.bitLen);
                            vp += hd.bitLen;
                        }
                        if (timeIdx < timestamps.size()) {
                            hd.changes.push_back({ timestamps[timeIdx], std::move(bits) });
                        }
                    }
                }
            }
        }

        pos = nextPos;
    }

    if (!sawValidHdr || handles.empty()) return false;

    std::vector<DecodedWaveSignal> sigs;
    sigs.reserve(handles.size());
    for (auto& h : handles) {
        if (h.isReal) continue;
        DecodedWaveSignal ds;
        ds.name = h.name;
        ds.bitWidth = static_cast<int>(h.bitLen);
        ds.changes = std::move(h.changes);
        sigs.push_back(std::move(ds));
    }
    std::wstring title = L"GTKWave FST Waveform (" + std::to_wstring(sigs.size()) + L" signals";
    if (!writerName.empty()) title += L", " + PathUtils::Utf8ToWide(writerName);
    title += L", 10^" + std::to_wstring(static_cast<int>(timescale)) + L"s)";
    return RenderWaveSignalsToBoard(sigs, hdrStartTime, hdrEndTime, title, out);
}

// Authentic GHDL Waveform (.ghw) Reader
inline bool ParseGhdlGhw(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || size < 24 || std::memcmp(data, "GHDLwave\n", 9) != 0) return false;
    uint8_t hdrLen = data[9];
    uint8_t endian = data[12]; // 1 = LE, 2 = BE
    if (hdrLen < 12 || hdrLen > size || (endian != 1 && endian != 2)) return false;
    const bool be = (endian == 2);

    auto rd32 = [&](const uint8_t* p) -> uint32_t {
        return be ? DetailEda100::RdU32BE(p) : DetailEda100::RdU32LE(p);
    };
    auto rd64 = [&](const uint8_t* p) -> int64_t {
        return static_cast<int64_t>(be ? DetailEda100::RdU64BE(p) : DetailEda100::RdU64LE(p));
    };

    std::vector<std::string> strTable(1); // 1-based
    enum class GhwScalarKind : uint8_t { Enum1B = 0, IntLeb = 1, Float64 = 2 };
    struct GhwTypeDef {
        GhwScalarKind kind = GhwScalarKind::Enum1B;
        std::vector<char> enumChars; // e.g. {'U','X','0','1','Z','W','L','H','-'}
        uint32_t elemCount = 1;
        uint32_t elemTypeId = 0; // 1-based for arrays
    };
    std::vector<GhwTypeDef> types(1); // 1-based

    struct GhwBasicSig {
        uint32_t typeId = 1;
        std::vector<std::pair<uint64_t, char>> changes;
    };
    std::vector<GhwBasicSig> basicSigs; // 1-based

    struct GhwSignalDecl {
        std::wstring name;
        std::vector<uint32_t> elIds; // 1-based basic signal IDs
    };
    std::vector<GhwSignalDecl> declSignals;

    uint64_t minTimeFs = 0, maxTimeFs = 0;
    size_t pos = hdrLen;

    auto findMarker = [&](const char marker[4], size_t startOff) -> size_t {
        for (size_t i = startOff; i + 4 <= size; ++i) {
            if (std::memcmp(data + i, marker, 4) == 0) return i;
        }
        return std::string_view::npos;
    };

    while (pos + 4 <= size) {
        const uint8_t* sec = data + pos;
        if (std::memcmp(sec, "TAI\0", 4) == 0 || std::memcmp(sec, "DIR\0", 4) == 0) {
            pos += 4;
            if (std::memcmp(sec, "DIR\0", 4) == 0) {
                size_t eod = findMarker("EOD\0", pos);
                if (eod == std::string_view::npos) break;
                pos = eod + 4;
            } else {
                break;
            }
            continue;
        }

        if (std::memcmp(sec, "STR\0", 4) == 0) {
            if (pos + 16 > size) return false;
            uint32_t nbrStr = rd32(data + pos + 8);
            size_t strStart = pos + 16;
            size_t eosPos = findMarker("EOS\0", strStart);
            if (eosPos == std::string_view::npos || nbrStr > 200000) return false;

            strTable.assign(nbrStr + 1, std::string{});
            size_t sp = strStart;
            // Check whether string 1 is preceded by an explicit 0x00 prefix-len byte or starts with ASCII directly
            if (nbrStr > 0 && sp < eosPos && data[sp] == 0) {
                size_t ctrlRuns = 0;
                bool inCtrl = false;
                for (size_t k = sp; k < eosPos; ++k) {
                    bool isCtrl = (data[k] < 32 || (data[k] >= 128 && data[k] < 160));
                    if (isCtrl && !inCtrl) ++ctrlRuns;
                    inCtrl = isCtrl;
                }
                if (ctrlRuns >= nbrStr + 1) ++sp;
            }

            size_t prevLen = 0;
            for (uint32_t i = 1; i <= nbrStr && sp < eosPos; ++i) {
                std::string cur;
                if (prevLen > 0 && prevLen <= strTable[i - 1].size()) {
                    cur = strTable[i - 1].substr(0, prevLen);
                }
                while (sp < eosPos) {
                    uint8_t c = data[sp];
                    if (c < 32 || (c >= 128 && c < 160)) break;
                    cur.push_back(static_cast<char>(c));
                    ++sp;
                }
                strTable[i] = std::move(cur);
                // Decode prev_len for string i + 1
                prevLen = 0;
                int sh = 0;
                while (sp < eosPos) {
                    uint8_t c = data[sp++];
                    prevLen |= static_cast<size_t>(c & 0x1Fu) << sh;
                    sh += 5;
                    if (c < 128) break;
                }
            }
            pos = eosPos + 4;
            continue;
        }

        if (std::memcmp(sec, "TYP\0", 4) == 0) {
            if (pos + 12 > size) return false;
            uint32_t nbrTypes = rd32(data + pos + 8);
            size_t tp = pos + 12;
            size_t eosPos = findMarker("EOS\0", tp);
            if (eosPos == std::string_view::npos || nbrTypes > 100000) return false;
            types.assign(nbrTypes + 1, GhwTypeDef{});

            for (uint32_t ti = 1; ti <= nbrTypes && tp < eosPos; ++ti) {
                uint8_t rtik = data[tp++];
                GhwTypeDef td;
                auto litCharFromStrId = [&](uint64_t sid) -> char {
                    if (sid < strTable.size() && !strTable[sid].empty()) {
                        const std::string& s = strTable[sid];
                        if (s.size() >= 3 && s[0] == '\'') return s[1];
                        if (DetailEda100::ContainsAsciiI(s, "true")) return '1';
                        if (DetailEda100::ContainsAsciiI(s, "false")) return '0';
                        return s[0];
                    }
                    return '0';
                };
                if (rtik == 22) { // ghw_rtik_type_b2
                    DetailEda100::ReadULeb128(data, eosPos, tp); // name
                    uint64_t l0 = DetailEda100::ReadULeb128(data, eosPos, tp);
                    uint64_t l1 = DetailEda100::ReadULeb128(data, eosPos, tp);
                    td.kind = GhwScalarKind::Enum1B;
                    td.enumChars = { litCharFromStrId(l0), litCharFromStrId(l1) };
                } else if (rtik == 23) { // ghw_rtik_type_e8
                    DetailEda100::ReadULeb128(data, eosPos, tp); // name
                    uint64_t nLits = DetailEda100::ReadULeb128(data, eosPos, tp);
                    td.kind = GhwScalarKind::Enum1B;
                    for (uint64_t li = 0; li < nLits && li < 256 && tp < eosPos; ++li) {
                        uint64_t lsid = DetailEda100::ReadULeb128(data, eosPos, tp);
                        td.enumChars.push_back(litCharFromStrId(lsid));
                    }
                } else if (rtik == 25 || rtik == 26) { // i32 / i64
                    DetailEda100::ReadULeb128(data, eosPos, tp);
                    td.kind = GhwScalarKind::IntLeb;
                } else if (rtik == 27) { // f64
                    DetailEda100::ReadULeb128(data, eosPos, tp);
                    td.kind = GhwScalarKind::Float64;
                } else if (rtik == 28 || rtik == 29) { // p32 / p64
                    DetailEda100::ReadULeb128(data, eosPos, tp);
                    uint64_t nUnits = DetailEda100::ReadULeb128(data, eosPos, tp);
                    for (uint64_t ui = 0; ui < nUnits && tp < eosPos; ++ui) {
                        DetailEda100::ReadULeb128(data, eosPos, tp);
                        DetailEda100::ReadSLeb128(data, eosPos, tp);
                    }
                    td.kind = GhwScalarKind::IntLeb;
                } else if (rtik == 31) { // type_array
                    DetailEda100::ReadULeb128(data, eosPos, tp);
                    td.elemTypeId = static_cast<uint32_t>(DetailEda100::ReadULeb128(data, eosPos, tp));
                    uint64_t nDims = DetailEda100::ReadULeb128(data, eosPos, tp);
                    for (uint64_t di = 0; di < nDims && tp < eosPos; ++di) {
                        DetailEda100::ReadULeb128(data, eosPos, tp);
                    }
                } else if (rtik == 34) { // subtype_scalar
                    DetailEda100::ReadULeb128(data, eosPos, tp);
                    uint32_t baseId = static_cast<uint32_t>(DetailEda100::ReadULeb128(data, eosPos, tp));
                    if (baseId < ti) td = types[baseId];
                    if (tp < eosPos) {
                        uint8_t rkind = data[tp++] & 0x7Fu;
                        if (rkind == 22 || rkind == 23) tp += 2;
                        else if (rkind == 25 || rkind == 26 || rkind == 28 || rkind == 29) {
                            DetailEda100::ReadSLeb128(data, eosPos, tp);
                            DetailEda100::ReadSLeb128(data, eosPos, tp);
                        } else if (rkind == 27) tp += 16;
                    }
                } else if (rtik == 35) { // subtype_array (constrained 1D array e.g. std_logic_vector(7 downto 0))
                    DetailEda100::ReadULeb128(data, eosPos, tp);
                    uint32_t baseArr = static_cast<uint32_t>(DetailEda100::ReadULeb128(data, eosPos, tp));
                    if (baseArr < ti) {
                        td.elemTypeId = types[baseArr].elemTypeId;
                        if (td.elemTypeId < ti) td.kind = types[td.elemTypeId].kind;
                    }
                    if (tp < eosPos) {
                        uint8_t rkind = data[tp++];
                        bool desc = (rkind & 0x80u) != 0;
                        uint8_t rk = rkind & 0x7Fu;
                        int64_t left = 0, right = 0;
                        if (rk == 22 || rk == 23) {
                            if (tp + 2 <= eosPos) { left = data[tp]; right = data[tp + 1]; tp += 2; }
                        } else {
                            left  = DetailEda100::ReadSLeb128(data, eosPos, tp);
                            right = DetailEda100::ReadSLeb128(data, eosPos, tp);
                        }
                        int64_t len = desc ? (left - right + 1) : (right - left + 1);
                        td.elemCount = static_cast<uint32_t>(std::clamp<int64_t>(len, 1, 512));
                    }
                } else {
                    break;
                }
                types[ti] = std::move(td);
            }
            pos = eosPos + 4;
            continue;
        }

        if (std::memcmp(sec, "WKT\0", 4) == 0) {
            size_t eosPos = findMarker("EOS\0", pos + 8);
            if (eosPos == std::string_view::npos) break;
            pos = eosPos + 4;
            continue;
        }

        if (std::memcmp(sec, "HIE\0", 4) == 0) {
            if (pos + 20 > size) return false;
            uint32_t nbrBasicSigs = rd32(data + pos + 16);
            size_t hp = pos + 20;
            size_t eosPos = findMarker("EOS\0", hp);
            if (eosPos == std::string_view::npos || nbrBasicSigs > 200000) return false;

            basicSigs.assign(nbrBasicSigs + 1, GhwBasicSig{});
            std::vector<std::string> scopes;

            while (hp < eosPos) {
                uint8_t hkind = data[hp++];
                if (hkind == 0) break; // end of HIE
                if (hkind == 15) { // ghw_hie_eos (upscope)
                    if (!scopes.empty()) scopes.pop_back();
                } else if (hkind == 1 || hkind == 3 || hkind == 4 || hkind == 5 || hkind == 6 || hkind == 7 || hkind == 13) {
                    uint64_t sid = DetailEda100::ReadULeb128(data, eosPos, hp);
                    std::string sname = (sid < strTable.size()) ? strTable[sid] : "";
                    if (hkind == 5) { // generate_for
                        uint32_t itType = static_cast<uint32_t>(DetailEda100::ReadULeb128(data, eosPos, hp));
                        if (itType < types.size() && types[itType].kind == GhwScalarKind::Enum1B) ++hp;
                        else DetailEda100::ReadSLeb128(data, eosPos, hp);
                    }
                    if (hkind != 13) scopes.push_back(std::move(sname)); // process has no upscope
                } else if (hkind >= 16 && hkind <= 21) { // signal / port_*
                    uint64_t sid = DetailEda100::ReadULeb128(data, eosPos, hp);
                    uint32_t tid = static_cast<uint32_t>(DetailEda100::ReadULeb128(data, eosPos, hp));
                    std::string vname = (sid < strTable.size()) ? strTable[sid] : ("sig_" + std::to_string(declSignals.size()));
                    if (!scopes.empty() && !scopes.back().empty()) vname = scopes.back() + "." + vname;

                    uint32_t nElems = (tid < types.size()) ? types[tid].elemCount : 1u;
                    uint32_t scalarTid = (tid < types.size() && types[tid].elemTypeId != 0) ? types[tid].elemTypeId : tid;
                    GhwSignalDecl decl;
                    decl.name = PathUtils::Utf8ToWide(vname);
                    for (uint32_t ei = 0; ei < nElems && hp < eosPos; ++ei) {
                        uint32_t elId = static_cast<uint32_t>(DetailEda100::ReadULeb128(data, eosPos, hp));
                        if (elId >= 1 && elId < basicSigs.size()) {
                            basicSigs[elId].typeId = scalarTid;
                            decl.elIds.push_back(elId);
                        }
                    }
                    if (!decl.elIds.empty()) declSignals.push_back(std::move(decl));
                } else {
                    break;
                }
            }
            pos = eosPos + 4;
            continue;
        }

        auto readBasicSigChar = [&](uint32_t sigId, size_t& cur, size_t limit) -> char {
            uint32_t tid = (sigId < basicSigs.size()) ? basicSigs[sigId].typeId : 1u;
            const GhwTypeDef* td = (tid < types.size()) ? &types[tid] : nullptr;
            if (!td || td->kind == GhwScalarKind::Enum1B) {
                if (cur >= limit) return '0';
                uint8_t idx = data[cur++];
                if (td && idx < td->enumChars.size()) return td->enumChars[idx];
                return (idx == 3 || idx == 1) ? '1' : '0';
            }
            if (td->kind == GhwScalarKind::IntLeb) {
                int64_t v = DetailEda100::ReadSLeb128(data, limit, cur);
                return (v != 0) ? '1' : '0';
            }
            cur = std::min(limit, cur + 8);
            return '0';
        };

        if (std::memcmp(sec, "SNP\0", 4) == 0) {
            if (pos + 16 > size) break;
            int64_t snapFs = std::max<int64_t>(0, rd64(data + pos + 8));
            minTimeFs = static_cast<uint64_t>(snapFs);
            maxTimeFs = std::max(maxTimeFs, minTimeFs);
            size_t sp = pos + 16;
            size_t esnPos = findMarker("ESN\0", sp);
            size_t limit = (esnPos != std::string_view::npos) ? esnPos : size;
            for (size_t si = 1; si < basicSigs.size() && sp < limit; ++si) {
                char ch = readBasicSigChar(static_cast<uint32_t>(si), sp, limit);
                basicSigs[si].changes.push_back({ static_cast<uint64_t>(snapFs), ch });
            }
            if (esnPos == std::string_view::npos) break;
            pos = esnPos + 4;
            continue;
        }

        if (std::memcmp(sec, "CYC\0", 4) == 0) {
            if (pos + 12 > size) break;
            int64_t curFs = std::max<int64_t>(0, rd64(data + pos + 4));
            size_t cp = pos + 12;
            size_t ecyPos = findMarker("ECY\0", cp);
            size_t limit = (ecyPos != std::string_view::npos) ? ecyPos : size;

            while (cp < limit) {
                uint32_t sigIdx = 0;
                while (cp < limit) {
                    uint64_t deltaSig = DetailEda100::ReadULeb128(data, limit, cp);
                    if (deltaSig == 0) break;
                    sigIdx += static_cast<uint32_t>(deltaSig);
                    char ch = readBasicSigChar(sigIdx, cp, limit);
                    if (sigIdx >= 1 && sigIdx < basicSigs.size() && basicSigs[sigIdx].changes.size() < 8192) {
                        basicSigs[sigIdx].changes.push_back({ static_cast<uint64_t>(curFs), ch });
                    }
                }
                maxTimeFs = std::max(maxTimeFs, static_cast<uint64_t>(curFs));
                if (cp >= limit) break;
                int64_t dtFs = DetailEda100::ReadSLeb128(data, limit, cp);
                if (dtFs < 0) break; // -1 terminates CYC
                curFs += dtFs;
            }
            if (ecyPos == std::string_view::npos) break;
            pos = ecyPos + 4;
            continue;
        }

        break;
    }

    if (declSignals.empty()) return false;

    std::vector<DecodedWaveSignal> sigs;
    sigs.reserve(declSignals.size());
    for (const auto& d : declSignals) {
        DecodedWaveSignal ws;
        ws.name = d.name;
        ws.bitWidth = static_cast<int>(d.elIds.size());
        if (d.elIds.size() == 1) {
            for (const auto& kv : basicSigs[d.elIds[0]].changes) {
                ws.changes.push_back({ kv.first, std::string(1, kv.second) });
            }
        } else {
            // Merge multi-bit vector timestamps across scalar element signals
            std::set<uint64_t> allTimes;
            for (uint32_t eid : d.elIds) {
                for (const auto& kv : basicSigs[eid].changes) allTimes.insert(kv.first);
            }
            std::vector<size_t> cursors(d.elIds.size(), 0);
            for (uint64_t t : allTimes) {
                std::string bits(d.elIds.size(), '0');
                for (size_t bi = 0; bi < d.elIds.size(); ++bi) {
                    const auto& chVec = basicSigs[d.elIds[bi]].changes;
                    while (cursors[bi] + 1 < chVec.size() && chVec[cursors[bi] + 1].first <= t) ++cursors[bi];
                    if (!chVec.empty()) bits[bi] = chVec[cursors[bi]].second;
                }
                ws.changes.push_back({ t, std::move(bits) });
            }
        }
        sigs.push_back(std::move(ws));
    }

    wchar_t titleBuf[160] = {};
    swprintf_s(titleBuf, L"GHDL VHDL Waveform (.ghw — %zu signals, %.3f ns)",
        sigs.size(), static_cast<double>(maxTimeFs) * 1e-6);
    return RenderWaveSignalsToBoard(sigs, minTimeFs, maxTimeFs, titleBuf, out);
}

// Xilinx .bit & FPGA .rpd Bitstream Parser
inline bool ParseFpgaBitstream(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (!data || size < 32) return false;
    out.kind = EdaDocKind::KiCadPcb;

    if (extLower == L".bit") {
        // Parse Xilinx .bit TLV header: [u16_be syncHdrLen][syncHdr][u16_be 1]['a'][u16 len][design]...['e'][u32_be bitLen]
        size_t pos = 0;
        std::string designName, partName, buildDate, buildTime;
        const uint8_t* cfgBytes = nullptr;
        size_t cfgLen = 0;

        if (size >= 16) {
            uint16_t syncHdrLen = DetailEda100::RdU16BE(data);
            if (syncHdrLen >= 2 && syncHdrLen <= 64 && static_cast<size_t>(2 + syncHdrLen + 2) < size) {
                pos = 2 + syncHdrLen;
                uint16_t firstKeyLen = DetailEda100::RdU16BE(data + pos);
                if (firstKeyLen == 1) {
                    pos += 2;
                    while (pos < size) {
                        uint8_t key = data[pos++];
                        if (key == 'e') {
                            if (pos + 4 > size) return false;
                            uint32_t blen = DetailEda100::RdU32BE(data + pos);
                            pos += 4;
                            if (blen == 0 || pos >= size) return false;
                            cfgBytes = data + pos;
                            cfgLen = std::min<size_t>(blen, size - pos);
                            break;
                        } else if (key >= 'a' && key <= 'd') {
                            if (pos + 2 > size) return false;
                            uint16_t slen = DetailEda100::RdU16BE(data + pos);
                            pos += 2;
                            if (slen > size - pos) return false;
                            std::string s = DetailEda100::TrimAsciiView(
                                std::string_view(reinterpret_cast<const char*>(data + pos), slen));
                            pos += slen;
                            if (key == 'a') designName = s;
                            else if (key == 'b') partName = s;
                            else if (key == 'c') buildDate = s;
                            else if (key == 'd') buildTime = s;
                            // Optional u16_be keyLen==1 before next key in some Xilinx tools, or direct key char
                            if (pos + 3 <= size && data[pos] == 0x00 && data[pos + 1] == 0x01 &&
                                data[pos + 2] >= 'a' && data[pos + 2] <= 'e')
                            {
                                pos += 2;
                            }
                        } else {
                            break;
                        }
                    }
                }
            }
        }

        // Also locate 0xAA995566 sync word in bitstream
        if (!cfgBytes) {
            cfgBytes = data;
            cfgLen = size;
        }
        size_t syncOff = std::string_view::npos;
        for (size_t i = 0; i + 4 <= cfgLen; ++i) {
            if (DetailEda100::RdU32BE(cfgBytes + i) == 0xAA995566u) {
                syncOff = i;
                break;
            }
        }
        if (syncOff == std::string_view::npos && partName.empty()) return false;

        uint32_t idcode = 0;
        size_t fdriWords = 0;
        const uint8_t* frameStart = cfgBytes;
        size_t frameBytes = cfgLen;

        if (syncOff != std::string_view::npos) {
            size_t wp = syncOff + 4;
            while (wp + 4 <= cfgLen) {
                uint32_t w = DetailEda100::RdU32BE(cfgBytes + wp);
                wp += 4;
                uint32_t pktType = (w >> 29) & 0x7u;
                if (pktType == 1) {
                    uint32_t op  = (w >> 27) & 0x3u;
                    uint32_t reg = (w >> 13) & 0x1Fu;
                    uint32_t cnt = w & 0x7FFu;
                    if (op == 2 && reg == 12 && cnt >= 1 && wp + 4 <= cfgLen) { // IDCODE
                        idcode = DetailEda100::RdU32BE(cfgBytes + wp);
                    }
                    if (op == 2 && reg == 2) { // FDRI
                        if (cnt > 0 && wp + cnt * 4u <= cfgLen) {
                            frameStart = cfgBytes + wp;
                            frameBytes = cnt * 4u;
                            fdriWords += cnt;
                        } else if (cnt == 0 && wp + 4 <= cfgLen) {
                            uint32_t w2 = DetailEda100::RdU32BE(cfgBytes + wp);
                            if (((w2 >> 29) & 0x7u) == 2) {
                                wp += 4;
                                uint32_t cnt2 = w2 & 0x07FFFFFFu;
                                size_t avail = std::min<size_t>(static_cast<size_t>(cnt2) * 4u, cfgLen - wp);
                                frameStart = cfgBytes + wp;
                                frameBytes = avail;
                                fdriWords += avail / 4u;
                                wp += avail;
                                continue;
                            }
                        }
                    }
                    size_t skip = std::min<size_t>(static_cast<size_t>(cnt) * 4u, cfgLen - wp);
                    wp += skip;
                }
            }
        }

        // Partition configuration frames across an 8x8 CLB tile floorplan
        constexpr int kGrid = 8;
        DetailEda100::AddRectOutline(out, 0.0f, 0.0f, 52.0f, 52.0f, EdaLayerId::EdgeCuts, 0.45f);
        size_t activeTiles = 0;
        for (int r = 0; r < kGrid; ++r) {
            for (int c = 0; c < kGrid; ++c) {
                size_t tileIdx = static_cast<size_t>(r * kGrid + c);
                size_t b0 = (frameBytes * tileIdx) / (kGrid * kGrid);
                size_t b1 = (frameBytes * (tileIdx + 1)) / (kGrid * kGrid);
                if (b1 <= b0 && frameBytes > 0) { b0 = tileIdx % frameBytes; b1 = b0 + 1; }
                size_t setBits = 0, totalBits = (b1 - b0) * 8;
                for (size_t bi = b0; bi < b1; ++bi) {
                    uint8_t v = frameStart[bi];
                    while (v) { setBits += (v & 1u); v >>= 1; }
                }
                float util = (totalBits > 0) ? (static_cast<float>(setBits) / static_cast<float>(totalBits)) : 0.0f;
                float tx = -19.25f + c * 5.5f;
                float ty = 19.25f - r * 5.5f;
                if (util > 0.02f) {
                    ++activeTiles;
                    float sz = std::clamp(1.2f + util * 3.6f, 1.2f, 4.6f);
                    EdaLayerId tl = (util > 0.35f) ? EdaLayerId::TopCopper : EdaLayerId::InnerCopper;
                    out.pads.push_back({ tx, ty, sz, sz, 0.0f, 0.0f, 1, tl, L"CLB", L"" });
                    if (c > 0 && util > 0.15f) {
                        out.traces.push_back({ tx - 5.5f, ty, tx, ty, 0.18f, EdaLayerId::SchWire, static_cast<int>(tileIdx + 1) });
                    }
                } else {
                    DetailEda100::AddRectOutline(out, tx, ty, 3.8f, 3.8f, EdaLayerId::BottomSilk, 0.08f);
                }
            }
        }

        std::wstring wPart = partName.empty() ? L"Xilinx_FPGA" : PathUtils::Utf8ToWide(partName);
        std::wstring wDes  = designName.empty() ? L"bitstream" : PathUtils::Utf8ToWide(designName);
        wchar_t idHex[32] = {};
        if (idcode != 0) swprintf_s(idHex, L"IDCODE 0x%08X", idcode);

        EdaComponent3D die;
        die.refDes = wPart;
        die.value = wDes;
        die.footprint = (idcode != 0) ? idHex : L"FPGA_CLB_ARRAY";
        die.x = 0.0f; die.y = 0.0f; die.widthMm = 46.0f; die.lengthMm = 46.0f; die.heightMm = 1.8f;
        die.category = EdaPartCategory::IC_Package;
        out.components.push_back(die);

        out.labels.push_back({ 0.0f, 28.5f, 1.3f, EdaLayerId::SchLabel, wPart + L" — " + wDes });
        if (!buildDate.empty() || idcode != 0) {
            std::wstring meta = PathUtils::Utf8ToWide(buildDate + (buildTime.empty() ? "" : (" " + buildTime)));
            if (idcode != 0) meta += (meta.empty() ? L"" : L" | ") + std::wstring(idHex);
            out.labels.push_back({ 0.0f, -28.5f, 1.0f, EdaLayerId::TopSilk, meta });
        }
        out.formatTitle = L"Xilinx FPGA Bitstream (" + wPart + L", " + std::to_wstring(fdriWords) +
                          L" FDRI words, " + std::to_wstring(activeTiles) + L"/64 CLB tiles)";
        out.netCount = std::max<size_t>(1, activeTiles);
        return true;
    }

    // .rpd (Raw Programming Data — require non-trivial programmed pages, not all 0x00 or all 0xFF)
    if (extLower == L".rpd" && size >= 64) {
        size_t nonFF = 0, nonZero = 0;
        for (size_t i = 0; i < size; ++i) {
            if (data[i] != 0xFF) ++nonFF;
            if (data[i] != 0x00) ++nonZero;
        }
        if (nonFF == 0 || nonZero == 0) return false;
        DetailEda100::AddRectOutline(out, 0.0f, 0.0f, 48.0f, 48.0f, EdaLayerId::EdgeCuts, 0.4f);
        size_t activePages = 0;
        for (int i = 0; i < 64; ++i) {
            size_t p0 = (size * static_cast<size_t>(i)) / 64;
            size_t p1 = (size * static_cast<size_t>(i + 1)) / 64;
            size_t used = 0;
            for (size_t k = p0; k < p1; ++k) if (data[k] != 0xFF && data[k] != 0x00) ++used;
            float ratio = (p1 > p0) ? (static_cast<float>(used) / static_cast<float>(p1 - p0)) : 0.0f;
            float px = -19.25f + (i % 8) * 5.5f;
            float py = 19.25f - (i / 8) * 5.5f;
            if (ratio > 0.02f) {
                ++activePages;
                out.pads.push_back({ px, py, 1.4f + ratio * 3.0f, 1.4f + ratio * 3.0f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, L"", L"" });
            }
        }
        EdaComponent3D ic;
        ic.refDes = L"U_CFG_FLASH";
        ic.value = PathUtils::FormatByteSize(size);
        ic.footprint = L"RPD_IMAGE";
        ic.widthMm = 42.0f; ic.lengthMm = 42.0f; ic.heightMm = 1.5f;
        ic.category = EdaPartCategory::IC_Package;
        out.components.push_back(ic);
        out.formatTitle = L"FPGA Raw Programming Data (.rpd — " + std::to_wstring(activePages) + L"/64 active blocks)";
        out.netCount = std::max<size_t>(1, activePages);
        return activePages > 0;
    }

    return false;
}

// STM32 DfuSe (.dfu) & USB DFU (.ufd) Firmware Image Parser
inline bool ParseStm32Dfu(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (!data || size < 24) return false;
    out.kind = EdaDocKind::KiCadPcb;

    struct FlashElem {
        uint32_t address = 0;
        uint32_t byteSize = 0;
        float fillRatio = 0.0f;
        uint32_t fnvHash = 0;
        std::wstring targetName;
    };
    std::vector<FlashElem> elems;

    // Decode optional 16-byte USB DFU 1.0/1.1 Suffix ("UFD" at offset +8..+10 of suffix)
    uint16_t vid = 0, pid = 0, bcdDev = 0;
    bool hasUfdSuffix = false;
    if (size >= 16) {
        const uint8_t* sfx = data + size - 16;
        if (sfx[8] == 'U' && sfx[9] == 'F' && sfx[10] == 'D' && sfx[11] >= 16) {
            bcdDev = DetailEda100::RdU16LE(sfx + 0);
            pid    = DetailEda100::RdU16LE(sfx + 2);
            vid    = DetailEda100::RdU16LE(sfx + 4);
            hasUfdSuffix = true;
        }
    }

    if (size >= 11 + 274 + 8 && std::memcmp(data, "DfuSe", 5) == 0 && data[5] == 0x01) {
        uint8_t numTargets = data[10];
        if (numTargets == 0 || numTargets > 32) return false;

        size_t pos = 11;
        size_t limit = hasUfdSuffix ? (size - 16) : size;
        for (uint8_t ti = 0; ti < numTargets && pos + 274 <= limit; ++ti) {
            if (std::memcmp(data + pos, "Target", 6) != 0) return false;
            bool named = DetailEda100::RdU32LE(data + pos + 7) != 0;
            std::string tname;
            if (named) {
                size_t nlen = 0;
                while (nlen < 255 && data[pos + 11 + nlen] != 0) ++nlen;
                tname = DetailEda100::TrimAsciiView(std::string_view(reinterpret_cast<const char*>(data + pos + 11), nlen));
            }
            if (tname.empty()) tname = "Target_" + std::to_string(data[pos + 6]);
            uint32_t nbElems = DetailEda100::RdU32LE(data + pos + 270);
            pos += 274;
            if (nbElems == 0 || nbElems > 512) return false;

            for (uint32_t ei = 0; ei < nbElems; ++ei) {
                if (pos + 8 > limit) return false;
                uint32_t elAddr = DetailEda100::RdU32LE(data + pos);
                uint32_t elSize = DetailEda100::RdU32LE(data + pos + 4);
                pos += 8;
                if (elSize > limit - pos) return false;

                size_t nonFF = 0;
                uint32_t h = 2166136261u;
                for (uint32_t k = 0; k < elSize; ++k) {
                    uint8_t b = data[pos + k];
                    if (b != 0xFF) ++nonFF;
                    h = (h ^ b) * 16777619u;
                }
                FlashElem fe;
                fe.address = elAddr;
                fe.byteSize = elSize;
                fe.fillRatio = (elSize > 0) ? (static_cast<float>(nonFF) / static_cast<float>(elSize)) : 0.0f;
                fe.fnvHash = h;
                fe.targetName = PathUtils::Utf8ToWide(tname);
                elems.push_back(std::move(fe));
                pos += elSize;
            }
        }
    } else if ((extLower == L".ufd" || extLower == L".dfu") && hasUfdSuffix && size > 16) {
        size_t payLen = size - 16;
        size_t nonFF = 0;
        for (size_t k = 0; k < payLen; ++k) if (data[k] != 0xFF) ++nonFF;
        FlashElem fe;
        fe.address = 0x08000000u;
        fe.byteSize = static_cast<uint32_t>(payLen);
        fe.fillRatio = static_cast<float>(nonFF) / static_cast<float>(payLen);
        fe.targetName = L"USB_DFU_RAW";
        elems.push_back(std::move(fe));
    } else {
        return false;
    }

    if (elems.empty()) return false;

    const float mapW = 72.0f;
    float totalH = static_cast<float>(elems.size()) * 11.0f + 8.0f;
    DetailEda100::AddRectOutline(out, 0.0f, 0.0f, mapW + 8.0f, totalH, EdaLayerId::EdgeCuts, 0.40f);

    uint64_t totalBytes = 0;
    for (size_t i = 0; i < elems.size(); ++i) {
        const auto& e = elems[i];
        totalBytes += e.byteSize;
        float cy = (totalH * 0.5f - 7.5f) - static_cast<float>(i) * 11.0f;

        wchar_t addrHex[48] = {};
        swprintf_s(addrHex, L"0x%08X", e.address);
        wchar_t valStr[96] = {};
        swprintf_s(valStr, L"%u B (%.1f%% code)", e.byteSize, e.fillRatio * 100.0f);

        EdaComponent3D secComp;
        secComp.refDes = addrHex;
        secComp.value = valStr;
        secComp.footprint = e.targetName;
        secComp.x = 0.0f;
        secComp.y = cy;
        secComp.widthMm = mapW;
        secComp.lengthMm = 7.5f;
        secComp.heightMm = 1.4f;
        secComp.category = EdaPartCategory::IC_Package;
        out.components.push_back(secComp);

        DetailEda100::AddRectOutline(out, 0.0f, cy, mapW, 7.5f, EdaLayerId::TopSilk, 0.25f);
        float fillW = std::clamp(e.fillRatio * (mapW - 2.0f), 1.0f, mapW - 2.0f);
        float fillCx = -(mapW - 2.0f) * 0.5f + fillW * 0.5f;
        out.pads.push_back({ fillCx, cy, fillW, 5.2f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, addrHex, valStr });
        out.labels.push_back({ 0.0f, cy, 1.05f, EdaLayerId::SchLabel,
            std::wstring(addrHex) + L"  [" + e.targetName + L"]  " + valStr });
    }

    wchar_t hdrBuf[192] = {};
    if (hasUfdSuffix) {
        swprintf_s(hdrBuf, L"STM32 DfuSe Firmware (%zu sectors, %llu B, VID:PID %04X:%04X, rev %04X)",
            elems.size(), static_cast<unsigned long long>(totalBytes), vid, pid, bcdDev);
    } else {
        swprintf_s(hdrBuf, L"STM32 DfuSe Firmware (%zu sectors, %llu B)",
            elems.size(), static_cast<unsigned long long>(totalBytes));
    }
    out.formatTitle = hdrBuf;
    out.netCount = elems.size();
    return true;
}

// VESA E-EDID 1.3 / 1.4 + CTA-861 (.edid) Parser
inline bool ParseVesaEdid(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    static const uint8_t kEdidMagic[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    if (!data || size < 128 || std::memcmp(data, kEdidMagic, 8) != 0) return false;
    if (data[18] == 0 || data[18] > 4) return false;

    out.kind = EdaDocKind::KiCadSch;

    uint16_t mfgRaw = DetailEda100::RdU16BE(data + 8);
    wchar_t c1 = static_cast<wchar_t>(L'A' + (((mfgRaw >> 10) & 0x1Fu) ? (((mfgRaw >> 10) & 0x1Fu) - 1u) : 0u));
    wchar_t c2 = static_cast<wchar_t>(L'A' + (((mfgRaw >> 5)  & 0x1Fu) ? (((mfgRaw >> 5)  & 0x1Fu) - 1u) : 0u));
    wchar_t c3 = static_cast<wchar_t>(L'A' + ((mfgRaw         & 0x1Fu) ? ((mfgRaw         & 0x1Fu) - 1u) : 0u));
    std::wstring mfg = { c1, c2, c3 };

    uint16_t prodCode  = DetailEda100::RdU16LE(data + 10);
    uint32_t serialNum = DetailEda100::RdU32LE(data + 12);
    int mfgWeek = data[16];
    int mfgYear = 1990 + data[17];
    int edidVer = data[18], edidRev = data[19];

    bool isDigital = (data[20] & 0x80u) != 0;
    std::wstring iface = L"Analog VGA";
    int bpc = 0;
    if (isDigital) {
        static const int kBpcLut[8] = { 0, 6, 8, 10, 12, 14, 16, 0 };
        bpc = kBpcLut[(data[20] >> 4) & 7u];
        switch (data[20] & 0x0Fu) {
        case 1: iface = L"DVI"; break;
        case 2: iface = L"HDMI-a"; break;
        case 3: iface = L"HDMI-b"; break;
        case 4: iface = L"MDDI"; break;
        case 5: iface = L"DisplayPort"; break;
        default: iface = L"Digital"; break;
        }
    }

    int hScreenCm = data[21], vScreenCm = data[22];
    float gamma = (data[23] != 0xFF) ? ((static_cast<float>(data[23]) + 100.0f) / 100.0f) : 2.2f;

    // 10-bit CIE 1931 Chromaticity coordinates
    float rx = static_cast<float>((static_cast<int>(data[27]) << 2) | ((data[25] >> 6) & 3)) / 1024.0f;
    float ry = static_cast<float>((static_cast<int>(data[28]) << 2) | ((data[25] >> 4) & 3)) / 1024.0f;
    float gx = static_cast<float>((static_cast<int>(data[29]) << 2) | ((data[25] >> 2) & 3)) / 1024.0f;
    float gy = static_cast<float>((static_cast<int>(data[30]) << 2) | ( data[25]       & 3)) / 1024.0f;
    float bx = static_cast<float>((static_cast<int>(data[31]) << 2) | ((data[26] >> 6) & 3)) / 1024.0f;
    float by = static_cast<float>((static_cast<int>(data[32]) << 2) | ((data[26] >> 4) & 3)) / 1024.0f;
    float wx = static_cast<float>((static_cast<int>(data[33]) << 2) | ((data[26] >> 2) & 3)) / 1024.0f;
    float wy = static_cast<float>((static_cast<int>(data[34]) << 2) | ( data[26]       & 3)) / 1024.0f;

    int prefHActive = 0, prefHBlank = 0, prefVActive = 0, prefVBlank = 0;
    int prefHImgMm = 0, prefVImgMm = 0;
    double prefHz = 0.0;
    std::wstring monName, monSerial, monText;
    int minVHz = 0, maxVHz = 0, maxPixClkMHz = 0;

    auto parseDtd = [&](const uint8_t* d) {
        uint16_t pixClk10k = DetailEda100::RdU16LE(d);
        if (pixClk10k == 0) return;
        int ha = d[2] | ((d[4] & 0xF0) << 4);
        int hb = d[3] | ((d[4] & 0x0F) << 8);
        int va = d[5] | ((d[7] & 0xF0) << 4);
        int vb = d[6] | ((d[7] & 0x0F) << 8);
        int hm = d[12] | ((d[14] & 0xF0) << 4);
        int vm = d[13] | ((d[14] & 0x0F) << 8);
        double tot = static_cast<double>(ha + hb) * static_cast<double>(va + vb);
        double hz = (tot > 0.0) ? ((static_cast<double>(pixClk10k) * 10000.0) / tot) : 0.0;
        if (prefHActive == 0 && ha > 0 && va > 0) {
            prefHActive = ha; prefHBlank = hb;
            prefVActive = va; prefVBlank = vb;
            prefHImgMm = hm;  prefVImgMm = vm;
            prefHz = hz;
        }
    };

    for (int i = 0; i < 4; ++i) {
        const uint8_t* d = data + 54 + i * 18;
        if (DetailEda100::RdU16LE(d) != 0) {
            parseDtd(d);
        } else if (d[2] == 0) {
            uint8_t tag = d[3];
            if (tag == 0xFC || tag == 0xFF || tag == 0xFE) {
                std::string s(reinterpret_cast<const char*>(d + 5), 13);
                size_t nl = s.find('\n');
                if (nl != std::string::npos) s.resize(nl);
                std::wstring ws = PathUtils::Utf8ToWide(DetailEda100::TrimAsciiView(s));
                if (tag == 0xFC) monName = ws;
                else if (tag == 0xFF) monSerial = ws;
                else monText = ws;
            } else if (tag == 0xFD) {
                minVHz = d[5];
                maxVHz = d[6];
                maxPixClkMHz = static_cast<int>(d[9]) * 10;
            }
        }
    }

    // Parse CTA-861 Extension Blocks if present
    int extCount = data[126];
    int ctaVics = 0, ctaAudio = 0;
    bool hasHdmi = false;
    for (int ei = 0; ei < extCount && static_cast<size_t>((ei + 2) * 128) <= size; ++ei) {
        const uint8_t* ext = data + (ei + 1) * 128;
        if (ext[0] == 0x02) { // CTA-861
            uint8_t dtdOff = ext[2];
            if (dtdOff >= 4 && dtdOff <= 127) {
                size_t cp = 4;
                while (cp < dtdOff) {
                    uint8_t hdr = ext[cp++];
                    uint8_t blkTag = (hdr >> 5) & 7u;
                    uint8_t blkLen = hdr & 0x1Fu;
                    if (cp + blkLen > dtdOff) break;
                    if (blkTag == 1) ctaAudio += blkLen / 3;
                    else if (blkTag == 2) ctaVics += blkLen;
                    else if (blkTag == 3 && blkLen >= 3) {
                        if ((ext[cp] == 0x03 && ext[cp + 1] == 0x0C && ext[cp + 2] == 0x00) ||
                            (ext[cp] == 0xD8 && ext[cp + 1] == 0x5D && ext[cp + 2] == 0xC4))
                        {
                            hasHdmi = true;
                        }
                    }
                    cp += blkLen;
                }
                for (size_t dp = dtdOff; dp + 18 <= 127; dp += 18) {
                    if (DetailEda100::RdU16LE(ext + dp) != 0) parseDtd(ext + dp);
                }
            }
        }
    }
    if (hasHdmi && iface == L"Digital") iface = L"HDMI";

    // Proportional physical display frame
    float aspect = 16.0f / 9.0f;
    if (prefHImgMm > 0 && prefVImgMm > 0) aspect = static_cast<float>(prefHImgMm) / static_cast<float>(prefVImgMm);
    else if (hScreenCm > 0 && vScreenCm > 0) aspect = static_cast<float>(hScreenCm) / static_cast<float>(vScreenCm);
    else if (prefHActive > 0 && prefVActive > 0) aspect = static_cast<float>(prefHActive) / static_cast<float>(prefVActive);
    aspect = std::clamp(aspect, 0.5f, 3.6f);

    float panelH = 34.0f;
    float panelW = panelH * aspect;
    // Outer total blanking timing frame + inner active display frame
    float blankScaleX = (prefHActive > 0) ? (static_cast<float>(prefHActive + prefHBlank) / static_cast<float>(prefHActive)) : 1.08f;
    float blankScaleY = (prefVActive > 0) ? (static_cast<float>(prefVActive + prefVBlank) / static_cast<float>(prefVActive)) : 1.05f;
    DetailEda100::AddRectOutline(out, -12.0f, 0.0f, panelW * blankScaleX, panelH * blankScaleY, EdaLayerId::BottomSilk, 0.16f);
    DetailEda100::AddRectOutline(out, -12.0f, 0.0f, panelW, panelH, EdaLayerId::EdgeCuts, 0.45f);

    // CIE 1931 RGB Chromaticity Gamut Triangle on right side (x: +28..+54, y: -14..+14)
    const float gx0 = 28.0f, gy0 = -14.0f, gSpan = 26.0f;
    DetailEda100::AddRectOutline(out, gx0 + gSpan * 0.5f, gy0 + gSpan * 0.5f, gSpan, gSpan, EdaLayerId::SchSymbol, 0.20f);
    if (rx > 0.01f || gx > 0.01f || bx > 0.01f) {
        float prx = gx0 + rx * gSpan, pry = gy0 + ry * gSpan;
        float pgx = gx0 + gx * gSpan, pgy = gy0 + gy * gSpan;
        float pbx = gx0 + bx * gSpan, pby = gy0 + by * gSpan;
        float pwx = gx0 + wx * gSpan, pwy = gy0 + wy * gSpan;
        out.traces.push_back({ prx, pry, pgx, pgy, 0.30f, EdaLayerId::TopCopper, 1 });
        out.traces.push_back({ pgx, pgy, pbx, pby, 0.30f, EdaLayerId::SchWire, 2 });
        out.traces.push_back({ pbx, pby, prx, pry, 0.30f, EdaLayerId::BottomCopper, 3 });
        out.circles.push_back({ pwx, pwy, 0.6f, 0.25f, true, EdaLayerId::TopSilk });
    }

    std::wstring dispName = monName.empty() ? (mfg + L"_" + std::to_wstring(prodCode)) : (mfg + L" " + monName);
    wchar_t resInfo[160] = {};
    if (prefHActive > 0 && prefVActive > 0) {
        swprintf_s(resInfo, L"%dx%d @ %.2f Hz (%s%s)",
            prefHActive, prefVActive, prefHz, iface.c_str(),
            bpc > 0 ? (L" " + std::to_wstring(bpc) + L"bpc").c_str() : L"");
    } else {
        swprintf_s(resInfo, L"EDID v%d.%d (%s, %d x %d cm)", edidVer, edidRev, iface.c_str(), hScreenCm, vScreenCm);
    }

    EdaComponent3D panelComp;
    panelComp.refDes = dispName;
    panelComp.value = resInfo;
    panelComp.footprint = L"EDID_PANEL";
    panelComp.x = -12.0f; panelComp.y = 0.0f;
    panelComp.widthMm = panelW * 0.9f; panelComp.lengthMm = panelH * 0.9f; panelComp.heightMm = 1.2f;
    panelComp.category = EdaPartCategory::IC_Package;
    out.components.push_back(panelComp);

    EdaComponent3D eep;
    eep.refDes = L"U_EDID";
    eep.value = L"24C02_DDC";
    eep.footprint = L"SOIC-8";
    eep.x = gx0 + gSpan * 0.5f; eep.y = -18.5f;
    eep.widthMm = 5.0f; eep.lengthMm = 4.0f; eep.heightMm = 1.5f;
    eep.category = EdaPartCategory::IC_Package;
    out.components.push_back(eep);

    out.labels.push_back({ -12.0f, 5.0f, 1.35f, EdaLayerId::SchLabel, dispName });
    out.labels.push_back({ -12.0f, 0.0f, 1.15f, EdaLayerId::TopSilk, resInfo });

    wchar_t metaBuf[192] = {};
    swprintf_s(metaBuf, L"Mfg %d/W%d | Gamma %.2f | SN %u%s",
        mfgYear, mfgWeek, gamma, serialNum,
        maxVHz > 0 ? (L" | VRR " + std::to_wstring(minVHz) + L"-" + std::to_wstring(maxVHz) + L"Hz " + std::to_wstring(maxPixClkMHz) + L"MHz").c_str() : L"");
    out.labels.push_back({ -12.0f, -5.5f, 0.95f, EdaLayerId::SchLabel, metaBuf });
    if (ctaVics > 0 || ctaAudio > 0) {
        out.labels.push_back({ -12.0f, -10.0f, 0.90f, EdaLayerId::TopSilk,
            L"CTA-861: " + std::to_wstring(ctaVics) + L" VICs, " + std::to_wstring(ctaAudio) + L" Audio" });
    }
    if (!monSerial.empty() || !monText.empty()) {
        out.labels.push_back({ -12.0f, -13.5f, 0.85f, EdaLayerId::SchLabel, monSerial.empty() ? monText : monSerial });
    }

    out.formatTitle = L"VESA E-EDID v" + std::to_wstring(edidVer) + L"." + std::to_wstring(edidRev) +
                      L" (" + dispName + L" — " + resInfo + L")";
    out.netCount = 4 + extCount;
    return true;
}

inline std::wstring DecodeJedecMfg(uint8_t bankRaw, uint8_t codeRaw) {
    if (bankRaw == 0 && codeRaw == 0) return L"JEDEC";
    uint8_t bank = bankRaw & 0x7Fu;
    uint8_t c7   = codeRaw & 0x7Fu;
    if (bank == 0) {
        if (c7 == 0x2C) return L"Micron";
        if (c7 == 0x2D) return L"SK Hynix"; // 0xAD & 0x7F
        if (c7 == 0x4E) return L"Samsung";  // 0xCE & 0x7F
        if (c7 == 0x40 || c7 == 0x41) return L"Infineon";
    } else if (bank == 1) {
        if (c7 == 0x18) return L"Kingston"; // 0x98 & 0x7F
        if (c7 == 0x7E) return L"Elpida";
    } else if (bank == 2) {
        if (c7 == 0x1E) return L"Corsair";  // 0x9E & 0x7F
    } else if (bank == 3) {
        if (c7 == 0x0B) return L"Nanya";    // 0x8B / 0x0B
    } else if (bank == 4) {
        if (c7 == 0x4D) return L"G.Skill";  // 0xCD & 0x7F
        if (c7 == 0x4B) return L"ADATA";    // 0xCB & 0x7F
    } else if (bank == 5) {
        if (c7 == 0x1B) return L"Crucial";  // 0x9B & 0x7F
    } else if (bank == 6) {
        if (c7 == 0x43) return L"TeamGroup";
    }
    wchar_t hexBuf[32] = {};
    swprintf_s(hexBuf, L"Mfg_%02X%02X", bankRaw, codeRaw);
    return hexBuf;
}

inline int SnapJedecMtPerSec(double rawMt) {
    static const int kBins[] = {
        400, 533, 667, 800, 1066, 1333, 1600, 1866, 2133, 2400, 2666, 2933, 3200,
        3600, 4000, 4400, 4800, 5200, 5600, 6000, 6400, 6800, 7200, 7600, 8000, 8400, 8800
    };
    int rounded = static_cast<int>(std::llround(rawMt));
    for (int bin : kBins) {
        if (std::abs(rounded - bin) <= 16) return bin;
    }
    return rounded;
}

// JEDEC DDR2 / DDR3 / DDR4 / DDR5 SPD EEPROM (.spd) Parser
inline bool ParseJedecSpd(const uint8_t* data, size_t size, ParsedEdaBoard& out) {
    if (!data || (size != 128 && size != 256 && size != 512 && size != 1024)) return false;

    uint8_t dramType = data[2];
    if (dramType != 0x08 && dramType != 0x0B && dramType != 0x0C && dramType != 0x0E &&
        dramType != 0x10 && dramType != 0x11 && dramType != 0x12 && dramType != 0x13)
    {
        return false;
    }
    uint8_t modType = data[3] & 0x0Fu;
    if ((data[3] & 0xE0u) != 0 || modType > 0x0Du) return false;

    out.kind = EdaDocKind::KiCadPcb;

    std::wstring genName = L"DDR4";
    std::wstring modTypeStr = L"UDIMM";
    switch (modType) {
    case 0x01: modTypeStr = L"RDIMM"; break;
    case 0x02: modTypeStr = L"UDIMM"; break;
    case 0x03: modTypeStr = L"SODIMM"; break;
    case 0x04: modTypeStr = L"LRDIMM"; break;
    case 0x0A: modTypeStr = L"DDIMM"; break;
    case 0x0B: modTypeStr = L"Soldered"; break;
    default:   modTypeStr = L"UDIMM"; break;
    }

    uint64_t dieDensityMb = 8192;
    int diePerPkg = 1;
    int sdramWidth = 8; // x4 / x8 / x16
    int ranks = 1;
    int busWidth = 64;
    double tCK_ps = 625.0;
    double tAA_ps = 13750.0;
    std::wstring mfgName = L"JEDEC";
    std::wstring partNumber;

    if (dramType == 0x12 || dramType == 0x13) { // DDR5 / LPDDR5 (JESD400-5)
        genName = (dramType == 0x13) ? L"LPDDR5" : L"DDR5";
        static const uint64_t kDdr5DensMb[16] = {
            0, 4096, 8192, 12288, 16384, 24576, 32768, 49152, 65536, 0,0,0,0,0,0,0
        };
        uint8_t dIdx = data[4] & 0x1Fu;
        dieDensityMb = (dIdx < 16 && kDdr5DensMb[dIdx] > 0) ? kDdr5DensMb[dIdx] : 16384ULL;
        uint8_t pkgRaw = (data[4] >> 5) & 0x07u;
        if (pkgRaw == 1 || pkgRaw == 2) diePerPkg = 2;
        else if (pkgRaw == 3) diePerPkg = 4;
        else if (pkgRaw == 4) diePerPkg = 8;

        // Byte 6 bits 7..5: First SDRAM I/O width (0=x4, 1=x8, 2=x16, 3=x32)
        // Note: if bytes 6, 234, 235 are all 0 (minimal synthetic DDR5 stub), default I/O width to x8
        bool minimalStub = (data[6] == 0 && (size <= 235 || (data[234] == 0 && data[235] == 0)));
        sdramWidth = minimalStub ? 8 : (4 << std::min<int>((data[6] >> 5) & 0x03u, 3));

        if (size > 235) {
            ranks = static_cast<int>((data[234] >> 3) & 0x07u) + 1;
            if (data[235] != 0) {
                int subChWidth = 8 << std::min<int>(data[235] & 0x03u, 3); // 2 -> 32 bits
                int numCh = 1 << std::min<int>((data[235] >> 5) & 0x03u, 2); // 1 -> 2 channels
                busWidth = std::clamp(subChWidth * numCh, 16, 128);
            }
        }
        uint16_t tckRaw = DetailEda100::RdU16LE(data + 20);
        uint16_t taaRaw = DetailEda100::RdU16LE(data + 30);
        tCK_ps = (tckRaw >= 100 && tckRaw <= 2000) ? static_cast<double>(tckRaw) : 416.0; // default DDR5-4800
        tAA_ps = (taaRaw >= 1000 && taaRaw <= 60000) ? static_cast<double>(taaRaw) : (tCK_ps * 40.0);

        if (size >= 552) {
            mfgName = DecodeJedecMfg(data[512], data[513]);
            if (mfgName == L"JEDEC" && size >= 554) mfgName = DecodeJedecMfg(data[552], data[553]);
            std::string pn = DetailEda100::TrimAsciiView(std::string_view(reinterpret_cast<const char*>(data + 521), 30));
            partNumber = PathUtils::Utf8ToWide(pn);
        }
    } else if (dramType == 0x0C || dramType == 0x0E || dramType == 0x10 || dramType == 0x11) { // DDR4 (JESD21-C Annex L)
        genName = (dramType == 0x10 || dramType == 0x11) ? L"LPDDR4" : L"DDR4";
        uint8_t dIdx = data[4] & 0x0Fu;
        dieDensityMb = (dIdx <= 8) ? (256ULL << dIdx) : 8192ULL;
        if ((data[6] & 0x03u) == 0x02u) diePerPkg = static_cast<int>((data[6] >> 4) & 0x07u) + 1;

        sdramWidth = (data[12] == 0 && data[13] == 0) ? 8 : (4 << std::min<int>(data[12] & 0x03u, 3));
        ranks = static_cast<int>((data[12] >> 3) & 0x07u) + 1;
        busWidth = (data[13] != 0) ? (8 << std::min<int>(data[13] & 0x07u, 3)) : 64;

        double mtbPs = 125.0, ftbPs = 1.0;
        double tck = static_cast<double>(data[18]) * mtbPs + static_cast<double>(static_cast<int8_t>(data[125])) * ftbPs;
        double taa = static_cast<double>(data[24]) * mtbPs + static_cast<double>(static_cast<int8_t>(data[123])) * ftbPs;
        tCK_ps = (tck >= 200.0 && tck <= 3000.0) ? tck : 625.0;
        tAA_ps = (taa >= 2000.0 && taa <= 50000.0) ? taa : 13750.0;

        if (size >= 349) {
            mfgName = DecodeJedecMfg(data[320], data[321]);
            if (mfgName == L"JEDEC" && size >= 352) mfgName = DecodeJedecMfg(data[350], data[351]);
            std::string pn = DetailEda100::TrimAsciiView(std::string_view(reinterpret_cast<const char*>(data + 329), 20));
            partNumber = PathUtils::Utf8ToWide(pn);
        }
    } else if (dramType == 0x0B) { // DDR3 (JESD21-C Annex K)
        genName = L"DDR3";
        uint8_t dIdx = data[4] & 0x0Fu;
        dieDensityMb = (dIdx <= 7) ? (256ULL << dIdx) : 4096ULL;
        sdramWidth = (data[7] == 0 && data[8] == 0) ? 8 : (4 << std::min<int>(data[7] & 0x03u, 3));
        ranks = static_cast<int>((data[7] >> 3) & 0x07u) + 1;
        busWidth = (data[8] != 0) ? (8 << std::min<int>(data[8] & 0x07u, 3)) : 64;

        double mtbPs = (data[10] > 0 && data[11] > 0)
            ? (1000.0 * static_cast<double>(data[10]) / static_cast<double>(data[11])) : 125.0;
        double ftbPs = ((data[9] & 0x0Fu) > 0 && (data[9] >> 4) > 0)
            ? (static_cast<double>(data[9] >> 4) / static_cast<double>(data[9] & 0x0Fu)) : 1.0;
        double tck = static_cast<double>(data[12]) * mtbPs + ((size > 34) ? static_cast<double>(static_cast<int8_t>(data[34])) * ftbPs : 0.0);
        double taa = static_cast<double>(data[16]) * mtbPs + ((size > 35) ? static_cast<double>(static_cast<int8_t>(data[35])) * ftbPs : 0.0);
        tCK_ps = (tck >= 400.0 && tck <= 5000.0) ? tck : 1250.0;
        tAA_ps = (taa >= 4000.0 && taa <= 50000.0) ? taa : 13750.0;

        if (size >= 146) {
            mfgName = DecodeJedecMfg(data[117], data[118]);
            std::string pn = DetailEda100::TrimAsciiView(std::string_view(reinterpret_cast<const char*>(data + 128), 18));
            partNumber = PathUtils::Utf8ToWide(pn);
        }
    } else { // DDR2 (0x08)
        genName = L"DDR2";
        ranks = static_cast<int>(data[5] & 0x07u) + 1;
        sdramWidth = (data[13] == 4 || data[13] == 8 || data[13] == 16) ? data[13] : 8;
        busWidth = 64;
        dieDensityMb = 1024;
        uint8_t bcd = data[9];
        double tck = static_cast<double>((bcd >> 4) * 1000 + (bcd & 0x0Fu) * 100);
        tCK_ps = (tck >= 1000.0 && tck <= 8000.0) ? tck : 2500.0;
        tAA_ps = 15000.0;
        if (size >= 91) {
            uint8_t cont = 0, code = 0;
            for (size_t k = 64; k < 72; ++k) {
                if (data[k] == 0x7F) ++cont;
                else { code = data[k]; break; }
            }
            mfgName = DecodeJedecMfg(cont, code);
            std::string pn = DetailEda100::TrimAsciiView(std::string_view(reinterpret_cast<const char*>(data + 73), 18));
            partNumber = PathUtils::Utf8ToWide(pn);
        }
    }

    int chipsPerRank = std::clamp(busWidth / std::max(4, sdramWidth), 1, 36);
    int totalDramChips = chipsPerRank * ranks;
    uint64_t totalMegabits = dieDensityMb * static_cast<uint64_t>(diePerPkg) * static_cast<uint64_t>(totalDramChips);
    double capGB = static_cast<double>(totalMegabits) / 8192.0; // 8192 Mb = 1 GB
    int mtPerSec = SnapJedecMtPerSec(2000000.0 / tCK_ps);
    int casLatency = static_cast<int>(std::ceil((tAA_ps - 0.5) / tCK_ps));

    bool isSoDimm = (modType == 0x03);
    float pcbW = isSoDimm ? 69.60f : 133.35f;
    float pcbH = isSoDimm ? 30.00f : 31.25f;
    DetailEda100::AddRectOutline(out, 0.0f, 0.0f, pcbW, pcbH, EdaLayerId::EdgeCuts, 0.50f);

    bool isDdr5 = (dramType == 0x12 || dramType == 0x13);
    wchar_t chipVal[64] = {};
    swprintf_s(chipVal, L"%s_%llux%d", genName.c_str(), static_cast<unsigned long long>(dieDensityMb / 1024), sdramWidth);

    // Place exact SDRAM IC packages per rank (Rank 0 on Top, Rank 1+ on Bottom)
    for (int rk = 0; rk < ranks && rk < 4; ++rk) {
        bool bot = (rk % 2 == 1);
        float yRow = bot ? -2.2f : 3.2f;
        float spanX = pcbW - 14.0f;
        for (int ci = 0; ci < chipsPerRank; ++ci) {
            // Leave center gap for PMIC / SPD Hub / RCD controller
            float frac = (chipsPerRank > 1) ? (static_cast<float>(ci) / static_cast<float>(chipsPerRank - 1)) : 0.5f;
            float cx = -spanX * 0.5f + frac * spanX;
            if (!isSoDimm && chipsPerRank >= 4) {
                cx += (ci < chipsPerRank / 2) ? -3.5f : 3.5f;
            }
            EdaComponent3D dram;
            dram.refDes = L"U" + std::to_wstring(rk * chipsPerRank + ci + 1);
            dram.value = chipVal;
            dram.footprint = (sdramWidth == 16) ? L"FBGA-96" : L"FBGA-78";
            dram.x = cx;
            dram.y = yRow;
            dram.widthMm = isSoDimm ? std::min(7.5f, spanX / (chipsPerRank + 1)) : std::min(9.5f, spanX / (chipsPerRank + 1));
            dram.lengthMm = 11.5f;
            dram.heightMm = 1.15f;
            dram.bottomSide = bot;
            dram.category = EdaPartCategory::IC_Package;
            out.components.push_back(dram);
        }
    }

    // Add DDR5 PMIC + SPD5118 Hub (or DDR3/DDR4 SPD EEPROM + optional RDIMM RCD)
    if (isDdr5) {
        EdaComponent3D pmic;
        pmic.refDes = L"U_PMIC";
        pmic.value = L"PMIC5100";
        pmic.footprint = L"QFN-36";
        pmic.x = 0.0f; pmic.y = 5.5f;
        pmic.widthMm = 5.0f; pmic.lengthMm = 5.0f; pmic.heightMm = 1.3f;
        pmic.category = EdaPartCategory::IC_Package;
        out.components.push_back(pmic);

        EdaComponent3D hub;
        hub.refDes = L"U_SPD_HUB";
        hub.value = L"SPD5118";
        hub.footprint = L"DFN-8";
        hub.x = 0.0f; hub.y = -2.5f;
        hub.widthMm = 3.0f; hub.lengthMm = 2.0f; hub.heightMm = 0.9f;
        hub.category = EdaPartCategory::IC_Package;
        out.components.push_back(hub);
    } else {
        EdaComponent3D spdIc;
        spdIc.refDes = L"U_SPD";
        spdIc.value = L"EE1004";
        spdIc.footprint = L"TDFN-8";
        spdIc.x = 0.0f; spdIc.y = -4.0f;
        spdIc.widthMm = 3.0f; spdIc.lengthMm = 2.0f; spdIc.heightMm = 0.9f;
        spdIc.category = EdaPartCategory::IC_Package;
        out.components.push_back(spdIc);
    }

    // Gold edge connector fingers
    int nFingers = isSoDimm ? 26 : 44;
    float fPitch = (pcbW - 8.0f) / static_cast<float>(nFingers);
    int keyIdx = isDdr5 ? (nFingers / 2 - 2) : (nFingers / 2 + 3);
    for (int f = 0; f < nFingers; ++f) {
        if (f == keyIdx) continue;
        float fx = -(pcbW - 8.0f) * 0.5f + (static_cast<float>(f) + 0.5f) * fPitch;
        out.pads.push_back({ fx, -pcbH * 0.5f + 1.4f, fPitch * 0.62f, 2.4f, 0.0f, 0.0f, 1, EdaLayerId::TopCopper, L"", L"" });
    }

    wchar_t capStr[32] = {};
    if (capGB >= 1.0 && std::fabs(capGB - std::round(capGB)) < 0.05) {
        swprintf_s(capStr, L"%d GB", static_cast<int>(std::llround(capGB)));
    } else {
        swprintf_s(capStr, L"%.1f GB", capGB);
    }

    wchar_t summary[240] = {};
    swprintf_s(summary, L"%s %s-%d CL%d %s (%dRx%d, %s%s%s)",
        capStr, genName.c_str(), mtPerSec, casLatency, modTypeStr.c_str(),
        ranks, sdramWidth, mfgName.c_str(),
        partNumber.empty() ? L"" : L" ",
        partNumber.c_str());

    out.formatTitle = std::wstring(L"JEDEC SPD — ") + summary;
    out.labels.push_back({ 0.0f, pcbH * 0.5f + 2.5f, 1.25f, EdaLayerId::SchLabel, summary });
    out.netCount = static_cast<size_t>(totalDramChips);
    return true;
}

} // namespace DetailHwBin

inline bool ParseHardwareBinBoard(const uint8_t* data, size_t size, const std::wstring& extLower, ParsedEdaBoard& out) {
    if (!data || size < 16) return false;
    if (extLower == L".fst")  return DetailHwBin::ParseGtkWaveFst(data, size, out);
    if (extLower == L".ghw")  return DetailHwBin::ParseGhdlGhw(data, size, out);
    if (extLower == L".bit" || extLower == L".rpd") return DetailHwBin::ParseFpgaBitstream(data, size, extLower, out);
    if (extLower == L".dfu" || extLower == L".ufd") return DetailHwBin::ParseStm32Dfu(data, size, extLower, out);
    if (extLower == L".edid") return DetailHwBin::ParseVesaEdid(data, size, out);
    if (extLower == L".spd")  return DetailHwBin::ParseJedecSpd(data, size, out);
    return false;
}

} // namespace Formats
