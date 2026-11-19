#pragma once
// RetroQuakeCommon.h — Shared Quake 256-color palette & lump helpers.
#include "../../ByteIO.h"

namespace Formats {
namespace RetroTexDetail {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdI32LE;

// Procedural reconstruction of id Software's classic 16-ramp Quake / Quake II 256-color palette
// (faithful 16-shade tonal ramps: grey, brown, sky-blue, olive-green, blood-red, gold, tan, violet, fire/lava, 255=key)
inline void BuildQuakeDefaultPalette(uint8_t pal[256][3], bool isQuake2) {
    static constexpr uint8_t kRampEnds[16][6] = {
        {  15,  15,  15,  235, 235, 235 }, // 0..15:   Neutral grey
        {  35,  25,  15,  165, 120,  75 }, // 16..31:  Earth brown
        {  20,  30,  55,  145, 175, 235 }, // 32..47:  Slate / sky blue
        {  20,  40,  18,  130, 195,  95 }, // 48..63:  Moss / slime green
        {  55,  12,  12,  230,  55,  45 }, // 64..79:  Crimson / blood red
        {  55,  42,  14,  240, 200,  75 }, // 80..95:  Bronze / gold
        {  65,  45,  32,  225, 175, 135 }, // 96..111: Flesh / sandstone
        {  42,  24,  58,  190, 135, 225 }, // 112..127: Runic purple
        { 185, 145, 165,   35,  22,  30 }, // 128..143: Reversed mauve ramp
        { 195, 135, 105,   42,  24,  18 }, // 144..159: Reversed rust ramp
        { 205, 185, 135,   45,  38,  24 }, // 160..175: Reversed khaki ramp
        { 145, 185, 165,   22,  42,  34 }, // 176..191: Reversed teal-metal ramp
        { 245, 225,  55,   65,  35,   8 }, // 192..207: Torch yellow -> ember
        {  25,  55, 225,  185, 215, 255 }, // 208..223: Plasma / electric blue
        {  85,  18,   8,  255, 235, 145 }, // 224..239: Lava / fireball ramp
        {  45,  95,  45,  250, 250, 220 }  // 240..254: Fullbright accents
    };
    for (int r = 0; r < 16; ++r) {
        for (int s = 0; s < 16; ++s) {
            int idx = r * 16 + s;
            int r0 = kRampEnds[r][0], g0 = kRampEnds[r][1], b0 = kRampEnds[r][2];
            int r1 = kRampEnds[r][3], g1 = kRampEnds[r][4], b1 = kRampEnds[r][5];
            if (isQuake2 && r == 3) { g1 = std::min(255, g1 + 25); }
            pal[idx][0] = static_cast<uint8_t>(r0 + ((r1 - r0) * s) / 15);
            pal[idx][1] = static_cast<uint8_t>(g0 + ((g1 - g0) * s) / 15);
            pal[idx][2] = static_cast<uint8_t>(b0 + ((b1 - b0) * s) / 15);
        }
    }
    pal[255][0] = 159; pal[255][1] = 91; pal[255][2] = 83; // Classic Quake/Build transparent key
}

inline bool IsPrintableLumpName(const uint8_t* p, size_t maxLen, std::string& outName) {
    outName.clear();
    for (size_t i = 0; i < maxLen; ++i) {
        uint8_t c = p[i];
        if (c == 0) break;
        if (c < 0x20u || c > 0x7Eu) return false;
        outName.push_back(static_cast<char>(c));
    }
    return !outName.empty();
}

} // namespace RetroTexDetail
} // namespace Formats
