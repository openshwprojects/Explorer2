// ImageReadersConsoleRetro.cpp — Modular Modding, Console & Retro Game Texture Readers
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
#include "formats/images/Ps2Tim2Image.h"
#include "formats/images/NintendoTplImage.h"
#include "formats/images/NintendoBtiImage.h"
#include "formats/images/PspGimImage.h"
#include "formats/images/UberToolsFtx.h"
#include "formats/images/Heretic2M8Image.h"
#include "formats/images/Heretic2M32Image.h"
#include "formats/images/SegaGvrImage.h"

bool TryDecodeGameTextureImage(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo);

namespace {

bool SniffTim2(const uint8_t* data, size_t size) {
    return size >= 64u && std::memcmp(data, "TIM2", 4) == 0 &&
           (data[4] == 3u || data[4] == 4u) && data[5] <= 1u;
}
bool SniffTpl(const uint8_t* data, size_t size) {
    return size >= 0x38u && ByteIO::RdU32BE(data) == 0x0020AF30u;
}
bool SniffGim(const uint8_t* data, size_t size) {
    return size >= 64u && (std::memcmp(data, "MIG.00.1PSP\0", 12) == 0 ||
                           std::memcmp(data, ".GIM1.00\0PSP", 12) == 0);
}
bool SniffGvr(const uint8_t* data, size_t size) {
    return size >= 0x30u &&
           (std::memcmp(data, "GVRT", 4) == 0 ||
            ((std::memcmp(data, "GBIX", 4) == 0 || std::memcmp(data, "GCIX", 4) == 0) &&
             size >= 0x28u && std::memcmp(data + 16u, "GVRT", 4) == 0));
}

constexpr LoaderDispatch::ImageEntry kConsoleRetroImageTable[] = {
    { L".tm2",  Formats::DecodePs2Tim2,     SniffTim2 },
    { L".tim2", Formats::DecodePs2Tim2                },
    { L".tpl",  Formats::DecodeNintendoTpl, SniffTpl  },
    { L".bti",  Formats::DecodeNintendoBti            },
    { L".gim",  Formats::DecodePspGim,      SniffGim  },
    { L".ftx",  Formats::DecodeUberToolsFtx           },
    { L".m8",   Formats::DecodeHeretic2M8             },
    { L".m32",  Formats::DecodeHeretic2M32            },
    { L".gvr",  Formats::DecodeSegaGvr,     SniffGvr  },
};

} // namespace

namespace ImageExt {

bool IsGameTextureImageExt(std::wstring_view extLower);

bool IsConsoleOrGameImageExt(std::wstring_view extLower) {
    return LoaderDispatch::HasImageExt(kConsoleRetroImageTable, extLower) ||
           IsGameTextureImageExt(extLower);
}

} // namespace ImageExt

bool ImageFormatLoader::TryDecodeConsoleOrGameImage(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo)
{
    bool matched = false;
    const bool ok = LoaderDispatch::DispatchImage(kConsoleRetroImageTable, data, size, extLower, maxDimension, outInfo, matched);
    return matched ? ok : TryDecodeGameTextureImage(data, size, extLower, maxDimension, outInfo);
}

} // namespace Explorer2
