#pragma once
// BioWareCommon.h — Shared BioWare Aurora/Odyssey resource table & binary I/O helpers.
#if defined(E2_BIOWARE_TOP_LEVEL) || !defined(E2_DRACO_NOINLINE)
#include "../../PlatformCompat.h"
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

namespace Explorer2::Formats {
#else
namespace Formats {
#endif
namespace BioWareDetail {

inline uint16_t BwU16(const uint8_t* p) { uint16_t v = 0; std::memcpy(&v, p, 2); return v; }
inline uint32_t BwU32(const uint8_t* p) { uint32_t v = 0; std::memcpy(&v, p, 4); return v; }
inline float    BwF32(const uint8_t* p) { float    v = 0.0f; std::memcpy(&v, p, 4); return v; }

inline std::wstring ResTypeToExtension(uint16_t rt) {
    switch (rt) {
    case 1:    return L".bmp";
    case 3:    return L".tga";
    case 4:    return L".wav";
    case 6:    return L".plt";
    case 7:    return L".ini";
    case 10:   return L".txt";
    case 2002: return L".mdl";
    case 2009: return L".nss";
    case 2010: return L".ncs";
    case 2012: return L".are";
    case 2013: return L".set";
    case 2014: return L".ifo";
    case 2015: return L".bic";
    case 2016: return L".wok";
    case 2017: return L".2da";
    case 2022: return L".txi";
    case 2023: return L".git";
    case 2025: return L".uti";
    case 2027: return L".utc";
    case 2029: return L".dlg";
    case 2030: return L".itp";
    case 2032: return L".utt";
    case 2035: return L".uts";
    case 2036: return L".ltr";
    case 2037: return L".gff";
    case 2038: return L".fac";
    case 2040: return L".ute";
    case 2042: return L".utd";
    case 2044: return L".utp";
    case 2045: return L".dft";
    case 2046: return L".gic";
    case 2047: return L".gui";
    case 2051: return L".utm";
    case 2052: return L".dwk";
    case 2053: return L".pwk";
    case 2056: return L".jrl";
    case 2058: return L".utw";
    case 2060: return L".ssf";
    case 2064: return L".ndb";
    case 2065: return L".ptm";
    case 2066: return L".ptt";
    case 3000: return L".lyt";
    case 3001: return L".vis";
    case 3002: return L".rim";
    case 3003: return L".pth";
    case 3004: return L".lip";
    case 3007: return L".tpc";
    case 3008: return L".mdx";
    default: {
        wchar_t buf[16] = {};
        swprintf_s(buf, L".res%u", static_cast<unsigned>(rt));
        return buf;
    }
    }
}

inline std::wstring ReadFixedResRef(const uint8_t* p, size_t maxChars) {
    std::wstring out;
    out.reserve(maxChars);
    for (size_t i = 0; i < maxChars; ++i) {
        uint8_t c = p[i];
        if (c == 0) break;
        if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') {
            return {};
        }
        out.push_back(static_cast<wchar_t>(std::tolower(c)));
    }
    while (!out.empty() && out.back() == L' ') out.pop_back();
    return out;
}

inline std::wstring ReadFixedResRef16(const uint8_t* p) {
    return ReadFixedResRef(p, 16);
}

} // namespace BioWareDetail
#if defined(E2_BIOWARE_TOP_LEVEL) || !defined(E2_DRACO_NOINLINE)
} // namespace Explorer2::Formats
#else
} // namespace Formats
#endif
