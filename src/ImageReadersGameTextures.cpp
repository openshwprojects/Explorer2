// ImageReadersGameTextures.cpp — Modular Modding, Console & Retro Game Texture Readers
#include "ImageFormatLoader.h"
#include "ImageExtensions.h"
#include "FormatSupport.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "ByteIO.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Explorer2 {

namespace {
#include "formats/common/GpuTextureCodecs.h"
} // namespace

#include "formats/common/FormatPrologueCommon.h"
#include "formats/common/FormatLoaderDispatch.h"
#include "formats/common/ConsolePixelFormats.h"
#include "formats/common/QuakeMipTexCommon.h"
#include "formats/common/NintendoGxTileCommon.h"

#include "formats/images/NintendoNw4rTex0.h"
#include "formats/images/Nintendo3dsBclim.h"
#include "formats/images/NintendoWiiUBflim.h"
#include "formats/images/LithTechDtxImage.h"
#include "formats/images/RitualSinSwlImage.h"
#include "formats/images/EaSportsFshImage.h"
#include "formats/images/KonamiCtxrImage.h"
#include "formats/images/InfinityWardIwiImage.h"

namespace {

bool SniffTex0(const uint8_t* data, size_t size) {
    return size >= 0x44u && std::memcmp(data, "TEX0", 4) == 0;
}
bool SniffBclim(const uint8_t* data, size_t size) {
    return size >= 0x2Cu && std::memcmp(data + size - 0x28u, "CLIM", 4) == 0 &&
           std::memcmp(data + size - 0x14u, "imag", 4) == 0;
}
bool SniffBflim(const uint8_t* data, size_t size) {
    return size >= 0x2Cu && std::memcmp(data + size - 0x28u, "FLIM", 4) == 0 &&
           std::memcmp(data + size - 0x14u, "imag", 4) == 0;
}
bool SniffFsh(const uint8_t* data, size_t size) {
    return size >= 40u && std::memcmp(data, "SHPI", 4) == 0;
}
bool SniffCtxr(const uint8_t* data, size_t size) {
    return size >= 0x88u && std::memcmp(data, "TXRC", 4) == 0;
}
bool SniffIwi(const uint8_t* data, size_t size) {
    return size >= 32u && data[0] == 'I' && data[1] == 'W' && data[2] == 'i' &&
           (data[3] == 5u || data[3] == 6u || data[3] == 8u || data[3] == 13u);
}

constexpr LoaderDispatch::ImageEntry kGameTextureImageTable[] = {
    { L".tex0",  Formats::DecodeNintendoNw4rTex0,  SniffTex0  },
    { L".bclim", Formats::DecodeNintendo3dsBclim,  SniffBclim },
    { L".bflim", Formats::DecodeNintendoWiiUBflim, SniffBflim },
    { L".dtx",   Formats::DecodeLithTechDtx                   },
    { L".swl",   Formats::DecodeRitualSinSwl                  },
    { L".fsh",   Formats::DecodeEaSportsFsh,       SniffFsh   },
    { L".ctxr",  Formats::DecodeKonamiCtxr,        SniffCtxr  },
    { L".iwi",   Formats::DecodeInfinityWardIwi,   SniffIwi   },
};

} // namespace

bool TryDecodeWorkstationOrRetroImage(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo);

namespace ImageExt {

bool IsWorkstationOrRetroImageExt(std::wstring_view extLower);

bool IsGameTextureImageExt(std::wstring_view extLower) {
    return LoaderDispatch::HasImageExt(kGameTextureImageTable, extLower) ||
           IsWorkstationOrRetroImageExt(extLower);
}

} // namespace ImageExt

bool TryDecodeGameTextureImage(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo)
{
    bool matched = false;
    const bool ok = LoaderDispatch::DispatchImage(kGameTextureImageTable, data, size, extLower, maxDimension, outInfo, matched);
    return matched ? ok : TryDecodeWorkstationOrRetroImage(data, size, extLower, maxDimension, outInfo);
}

} // namespace Explorer2
