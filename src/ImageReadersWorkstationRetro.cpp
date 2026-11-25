// ImageReadersWorkstationRetro.cpp — Modular Workstation, Scientific, Home-Computer & Engine Texture Readers
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

#include "formats/images/SiliconGraphicsAliasPix.h"
#include "formats/images/UtahRleImage.h"
#include "formats/images/MagickImageFileMiff.h"
#include "formats/images/KhorosViffImage.h"
#include "formats/images/AmigaIconInfo.h"
#include "formats/images/AtariDegasImage.h"
#include "formats/images/CommodoreKoalaImage.h"
#include "formats/images/ZxSpectrumScrImage.h"
#include "formats/images/XboxXprImage.h"
#include "formats/images/GodotStreamTexture.h"
#include "formats/images/IdTechBimage.h"
#include "formats/images/XWindowDumpXwd.h"

namespace {

bool SniffUtahRle(const uint8_t* data, size_t size) {
    return size >= 16u && ByteIO::RdU16LE(data) == 0xCC52u &&
           data[12] == 8u && data[11] <= 4u && (data[11] > 0u || data[13] > 0u);
}

bool SniffMiff(const uint8_t* data, size_t size) {
    if (size < 24u) return false;
    size_t p = 0u;
    while (p < 64u && p < size && data[p] <= 0x20u) ++p;
    if (p + 14u > size) return false;
    static constexpr char kRef[14] = { 'i','d','=','i','m','a','g','e','m','a','g','i','c','k' };
    for (size_t i = 0; i < 14u; ++i) {
        char c = static_cast<char>(data[p + i]);
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != kRef[i]) return false;
    }
    return true;
}

bool SniffViff(const uint8_t* data, size_t size) {
    return size >= 1024u && data[0] == 0xABu && data[1] == 0x01u;
}

bool SniffAmigaInfo(const uint8_t* data, size_t size) {
    return size >= 78u && ByteIO::RdU16BE(data) == 0xE310u && ByteIO::RdU16BE(data + 2u) == 0x0001u;
}

bool SniffXpr(const uint8_t* data, size_t size) {
    return size >= 32u &&
           (std::memcmp(data, "XPR0", 4) == 0 ||
            std::memcmp(data, "XPR1", 4) == 0 ||
            std::memcmp(data, "XPR2", 4) == 0);
}

bool SniffGodotStex(const uint8_t* data, size_t size) {
    return size >= 20u && std::memcmp(data, "GDST", 4) == 0;
}

bool SniffGodotCtex(const uint8_t* data, size_t size) {
    return size >= 52u && std::memcmp(data, "GST2", 4) == 0;
}

bool SniffIdTechBimage(const uint8_t* data, size_t size) {
    if (size < 48u) return false;
    if (data[0] == 'B' && data[1] == 'I' && data[2] == 'M' && data[3] >= 7u && data[3] <= 32u) return true;
    return size >= 56u && data[8] == 'B' && data[9] == 'I' && data[10] == 'M' &&
           data[11] >= 7u && data[11] <= 32u;
}

bool SniffXwd(const uint8_t* data, size_t size) {
    if (size < 100u) return false;
    const uint32_t beHdr = ByteIO::RdU32BE(data);
    if (beHdr >= 100u && beHdr <= 4096u && ByteIO::RdU32BE(data + 4u) == 7u && ByteIO::RdU32BE(data + 8u) <= 2u) {
        return true;
    }
    const uint32_t leHdr = ByteIO::RdU32LE(data);
    return leHdr >= 100u && leHdr <= 4096u && ByteIO::RdU32LE(data + 4u) == 7u && ByteIO::RdU32LE(data + 8u) <= 2u;
}

constexpr LoaderDispatch::ImageEntry kWorkstationRetroImageTable[] = {
    { L".pix",    Formats::DecodeSiliconGraphicsAliasPix                    },
    { L".als",    Formats::DecodeSiliconGraphicsAliasPix                    },
    { L".rle",    Formats::DecodeUtahRleImage,           SniffUtahRle       },
    { L".miff",   Formats::DecodeMagickImageFileMiff,    SniffMiff          },
    { L".viff",   Formats::DecodeKhorosViffImage,        SniffViff          },
    { L".xv",     Formats::DecodeKhorosViffImage                            },
    { L".info",   Formats::DecodeAmigaIconInfo,          SniffAmigaInfo     },
    { L".pi1",    Formats::DecodeAtariDegasImage                            },
    { L".pi2",    Formats::DecodeAtariDegasImage                            },
    { L".pi3",    Formats::DecodeAtariDegasImage                            },
    { L".pc1",    Formats::DecodeAtariDegasImage                            },
    { L".pc2",    Formats::DecodeAtariDegasImage                            },
    { L".pc3",    Formats::DecodeAtariDegasImage                            },
    { L".koa",    Formats::DecodeCommodoreKoalaImage                        },
    { L".kla",    Formats::DecodeCommodoreKoalaImage                        },
    { L".scr",    Formats::DecodeZxSpectrumScrImage                         },
    { L".xpr",    Formats::DecodeXboxXprImage,           SniffXpr           },
    { L".stex",   Formats::DecodeGodotStreamTexture,     SniffGodotStex     },
    { L".ctex",   Formats::DecodeGodotStreamTexture,     SniffGodotCtex     },
    { L".bimage", Formats::DecodeIdTechBimage,           SniffIdTechBimage  },
    { L".xwd",    Formats::DecodeXWindowDumpXwd,         SniffXwd           },
};

} // namespace

namespace ImageExt {

bool IsWorkstationOrRetroImageExt(std::wstring_view extLower) {
    return LoaderDispatch::HasImageExt(kWorkstationRetroImageTable, extLower);
}

} // namespace ImageExt

bool TryDecodeWorkstationOrRetroImage(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo)
{
    bool matched = false;
    return LoaderDispatch::DispatchImage(kWorkstationRetroImageTable, data, size, extLower, maxDimension, outInfo, matched);
}

} // namespace Explorer2
