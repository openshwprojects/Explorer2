#include "ImageFormatLoader.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>

namespace Explorer2 {

namespace {

constexpr uint32_t kMaxRasterDimension = 16384;

inline uint16_t ReadU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

inline uint16_t ReadU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

inline int16_t ReadI16BE(const uint8_t* p) {
    return static_cast<int16_t>(ReadU16BE(p));
}

inline uint32_t ReadU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           static_cast<uint32_t>(p[3]);
}

inline int32_t ReadI32BE(const uint8_t* p) {
    return static_cast<int32_t>(ReadU32BE(p));
}

inline uint64_t ReadU64BE(const uint8_t* p) {
    return (static_cast<uint64_t>(ReadU32BE(p)) << 32) | ReadU32BE(p + 4);
}

inline void WritePremulBGRA(uint8_t* dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (a == 255) {
        dst[0] = b;
        dst[1] = g;
        dst[2] = r;
        dst[3] = 255;
    } else if (a == 0) {
        dst[0] = dst[1] = dst[2] = dst[3] = 0;
    } else {
        dst[0] = static_cast<uint8_t>((static_cast<uint32_t>(b) * a + 127) / 255);
        dst[1] = static_cast<uint8_t>((static_cast<uint32_t>(g) * a + 127) / 255);
        dst[2] = static_cast<uint8_t>((static_cast<uint32_t>(r) * a + 127) / 255);
        dst[3] = a;
    }
}

void DownscaleIfRequested(uint32_t maxDimension, ExtendedImageInfo& info) {
    info.origWidth = info.width;
    info.origHeight = info.height;
    if (maxDimension == 0 || (info.width <= maxDimension && info.height <= maxDimension)) {
        return;
    }
    if (info.width == 0 || info.height == 0 || info.bgraPixels.size() < static_cast<size_t>(info.width) * info.height * 4) {
        return;
    }

    uint32_t dstW = info.width;
    uint32_t dstH = info.height;
    if (info.width >= info.height) {
        dstW = maxDimension;
        dstH = std::max(1u, static_cast<uint32_t>((static_cast<uint64_t>(info.height) * maxDimension) / info.width));
    } else {
        dstH = maxDimension;
        dstW = std::max(1u, static_cast<uint32_t>((static_cast<uint64_t>(info.width) * maxDimension) / info.height));
    }

    std::vector<uint8_t> scaled(static_cast<size_t>(dstW) * dstH * 4);
    for (uint32_t y = 0; y < dstH; ++y) {
        uint32_t srcY = std::min(info.height - 1, static_cast<uint32_t>((static_cast<uint64_t>(y) * info.height) / dstH));
        const uint8_t* srcRow = info.bgraPixels.data() + static_cast<size_t>(srcY) * info.width * 4;
        uint8_t* dstRow = scaled.data() + static_cast<size_t>(y) * dstW * 4;
        for (uint32_t x = 0; x < dstW; ++x) {
            uint32_t srcX = std::min(info.width - 1, static_cast<uint32_t>((static_cast<uint64_t>(x) * info.width) / dstW));
            const uint8_t* sp = srcRow + srcX * 4;
            uint8_t* dp = dstRow + x * 4;
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp[3] = sp[3];
        }
    }
    info.width = dstW;
    info.height = dstH;
    info.bgraPixels = std::move(scaled);
}

#include "formats/Tga.h"
#include "formats/Psd.h"
#include "formats/Xcf.h"
#include "formats/Hdr.h"
#include "formats/Netpbm.h"
#include "formats/Pcx.h"
#include "formats/Qoi.h"
#include "formats/Dds.h"
#include "formats/Xbm.h"
#include "formats/Xpm.h"
#include "formats/Svg.h"

} // namespace

#include "formats/Pam.h"
#include "formats/Pfm.h"
#include "formats/Sgi.h"
#include "formats/Ras.h"
#include "formats/Ktx.h"
#include "formats/Vtf.h"
#include "formats/Farbfeld.h"
#include "formats/Fits.h"
#include "formats/Pkm.h"
#include "formats/Astc.h"
#include "formats/Pvr.h"
#include "formats/Ilbm.h"
#include "formats/Tim.h"
#include "formats/Wbmp.h"
#include "formats/Bdf.h"
#include "formats/Psf.h"
#include "formats/Y4m.h"
#include "formats/Ani.h"
#include "formats/PaletteImg.h"
#include "formats/Dicom.h"
#include "formats/ExtendedImages100.h"
#include "formats/ExtendedImages200.h"

bool ImageFormatLoader::IsExtendedImageExtension(const std::wstring& extLower) {
    return extLower == L".tga" || extLower == L".icb" || extLower == L".vda" || extLower == L".vst" ||
           extLower == L".psd" || extLower == L".psb" ||
           extLower == L".xcf" ||
           extLower == L".hdr" || extLower == L".pic" ||
           extLower == L".ppm" || extLower == L".pgm" || extLower == L".pbm" || extLower == L".pnm" ||
           extLower == L".pcx" ||
           extLower == L".qoi" ||
           extLower == L".dds" ||
           extLower == L".svg" ||
           extLower == L".xbm" ||
           extLower == L".xpm" ||
           extLower == L".pam" ||
           extLower == L".pfm" ||
           extLower == L".sgi" || extLower == L".rgb" || extLower == L".bw"  || extLower == L".rgba" ||
           extLower == L".ras" || extLower == L".sun" ||
           extLower == L".ktx" ||
           extLower == L".vtf" ||
           extLower == L".ff"  || extLower == L".farbfeld" ||
           extLower == L".fits" || extLower == L".fit" || extLower == L".fts" ||
           extLower == L".pkm" ||
           extLower == L".astc" ||
           extLower == L".pvr" ||
           extLower == L".iff" || extLower == L".ilbm" || extLower == L".lbm" ||
           extLower == L".tim" ||
           extLower == L".wbmp" ||
           extLower == L".bdf" ||
           extLower == L".psf" ||
           extLower == L".y4m" ||
           extLower == L".ani" ||
           extLower == L".gpl" || extLower == L".pal" || extLower == L".act" || extLower == L".aco" ||
           extLower == L".dcm" || extLower == L".dicom" ||
           extLower == L".exr" || extLower == L".ktx2" || extLower == L".basis" ||
           extLower == L".kra" || extLower == L".ora" || extLower == L".procreate" ||
           extLower == L".clip" || extLower == L".aseprite" || extLower == L".icns" ||
           extLower == L".jxl" || extLower == L".bpg" || extLower == L".blp" || extLower == L".utx" ||
           extLower == L".dpx" || extLower == L".cin" || extLower == L".cube" || extLower == L".3dl" ||
           extLower == L".ies" || extLower == L".ttf" || extLower == L".otf" || extLower == L".woff" || extLower == L".woff2" ||
           extLower == L".cr2" || extLower == L".cr3" || extLower == L".nef" || extLower == L".arw" ||
           extLower == L".dng" || extLower == L".raf" || extLower == L".orf" || extLower == L".rw2" ||
           extLower == L".pef" || extLower == L".eps" || extLower == L".ai"  || extLower == L".cdr" ||
           extLower == L".afphoto" || extLower == L".afdesign" || extLower == L".afpub" ||
           extLower == L".emf" || extLower == L".wmf" || extLower == L".cgm" ||
           extLower == L".abr" || extLower == L".pat" || extLower == L".dst" || extLower == L".pes" ||
           extLower == L".ans" || extLower == L".sixel";
}

bool ImageFormatLoader::DecodeImageMemory(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo)
try {
    outInfo = {};
    if (!data || size < 6) return false;

    bool ok = false;
    // Check magic signatures first so even misnamed files decode reliably
    if (size >= 4 && std::memcmp(data, "8BPS", 4) == 0) {
        ok = DecodePSD(data, size, outInfo);
    } else if (size >= 4 && std::memcmp(data, "DDS ", 4) == 0) {
        ok = DecodeDDS(data, size, outInfo);
    } else if (size >= 9 && std::memcmp(data, "gimp xcf ", 9) == 0) {
        ok = DecodeXCF(data, size, outInfo);
    } else if (size >= 4 && std::memcmp(data, "qoif", 4) == 0) {
        ok = DecodeQOI(data, size, outInfo);
    } else if (size >= 10 && (std::memcmp(data, "#?RADIANCE", 10) == 0 || std::memcmp(data, "#?RGBE", 6) == 0)) {
        ok = DecodeHDR(data, size, outInfo);
    } else if (size >= 16 && std::memcmp(data, "farbfeld", 8) == 0) {
        ok = Formats::DecodeFarbfeld(data, size, outInfo);
    } else if (size >= 64 && std::memcmp(data, "VTF\0", 4) == 0) {
        ok = Formats::DecodeVTF(data, size, outInfo);
    } else if (size >= 12 && data[0] == 0xAB && std::memcmp(data + 1, "KTX 11", 6) == 0) {
        ok = Formats::DecodeKTX(data, size, outInfo);
    } else if (size >= 12 && data[0] == 0xAB && std::memcmp(data + 1, "KTX 20", 6) == 0) {
        ok = Formats::DecodeKTX2OrBasis(data, size, extLower, outInfo);
    } else if (size >= 32 && data[0] == 0x76 && data[1] == 0x2F && data[2] == 0x31 && data[3] == 0x01) {
        ok = Formats::DecodeOpenEXR(data, size, outInfo);
    } else if (size >= 24 && std::memcmp(data, "CSFCHUNK", 8) == 0) {
        ok = Formats::DecodeClipStudio(data, size, outInfo);
    } else if (size >= 128 && data[4] == 0xE0 && data[5] == 0xA5) {
        ok = Formats::DecodeAseprite(data, size, outInfo);
    } else if (size >= 16 && std::memcmp(data, "icns", 4) == 0) {
        ok = Formats::DecodeAppleICNS(data, size, outInfo);
    } else if (size >= 12 && std::memcmp(data, "ASEF", 4) == 0) {
        ok = Formats::DecodePaletteSwatch(data, size, extLower, outInfo);
    } else if (size >= 32 && data[0] == 0x59 && data[1] == 0xA6 && data[2] == 0x6A && data[3] == 0x95) {
        ok = Formats::DecodeSunRaster(data, size, outInfo);
    } else if (size >= 512 && data[0] == 0x01 && data[1] == 0xDA) {
        ok = Formats::DecodeSGI(data, size, outInfo);
    } else if (size >= 2880 && std::memcmp(data, "SIMPLE  =", 9) == 0) {
        ok = Formats::DecodeFITS(data, size, outInfo);
    } else if (extLower == L".pam" || (size >= 16 && data[0] == 'P' && data[1] == '7')) {
        ok = Formats::DecodePAM(data, size, outInfo);
    } else if (extLower == L".pfm" || (size >= 16 && data[0] == 'P' && (data[1] == 'F' || data[1] == 'f'))) {
        ok = Formats::DecodePFM(data, size, outInfo);
    } else if (extLower == L".sgi" || extLower == L".rgb" || extLower == L".bw" || extLower == L".rgba") {
        ok = Formats::DecodeSGI(data, size, outInfo);
    } else if (extLower == L".ras" || extLower == L".sun") {
        ok = Formats::DecodeSunRaster(data, size, outInfo);
    } else if (extLower == L".ktx") {
        ok = Formats::DecodeKTX(data, size, outInfo);
    } else if (extLower == L".vtf") {
        ok = Formats::DecodeVTF(data, size, outInfo);
    } else if (extLower == L".ff" || extLower == L".farbfeld") {
        ok = Formats::DecodeFarbfeld(data, size, outInfo);
    } else if (extLower == L".fits" || (extLower == L".fit" && size >= 80 && std::memcmp(data, "SIMPLE", 6) == 0) || extLower == L".fts") {
        ok = Formats::DecodeFITS(data, size, outInfo);
    } else if (extLower == L".svg") {
        ok = DecodeSVG(data, size, maxDimension, outInfo);
    } else if (extLower == L".dds") {
        ok = DecodeDDS(data, size, outInfo);
    } else if (extLower == L".xbm") {
        ok = DecodeXBM(data, size, outInfo);
    } else if (extLower == L".xpm") {
        ok = DecodeXPM(data, size, outInfo);
    } else if (extLower == L".ppm" || extLower == L".pgm" || extLower == L".pbm" || extLower == L".pnm") {
        ok = DecodeNetpbm(data, size, outInfo);
    } else if (extLower == L".pcx") {
        ok = DecodePCX(data, size, outInfo);
    } else if (extLower == L".tga" || extLower == L".icb" || extLower == L".vda" || extLower == L".vst") {
        ok = DecodeTGA(data, size, outInfo);
    } else if (extLower == L".hdr" || extLower == L".pic") {
        ok = DecodeHDR(data, size, outInfo);
    } else if (extLower == L".psd" || extLower == L".psb") {
        ok = DecodePSD(data, size, outInfo);
    } else if (extLower == L".xcf") {
        ok = DecodeXCF(data, size, outInfo);
    } else if (extLower == L".qoi") {
        ok = DecodeQOI(data, size, outInfo);
    } else if (extLower == L".pkm" || (size >= 16 && std::memcmp(data, "PKM ", 4) == 0)) {
        ok = Formats::DecodePKM(data, size, outInfo);
    } else if (extLower == L".astc" || (size >= 16 && data[0] == 0x13 && data[1] == 0xAB && data[2] == 0xA1 && data[3] == 0x5C)) {
        ok = Formats::DecodeASTC(data, size, outInfo);
    } else if (extLower == L".pvr" || (size >= 52 && std::memcmp(data, "PVR\x03", 4) == 0)) {
        ok = Formats::DecodePVR(data, size, outInfo);
    } else if (extLower == L".iff" || extLower == L".ilbm" || extLower == L".lbm" || (size >= 12 && std::memcmp(data, "FORM", 4) == 0)) {
        ok = Formats::DecodeILBM(data, size, outInfo);
    } else if (extLower == L".tim") {
        ok = Formats::DecodeTIM(data, size, outInfo);
    } else if (extLower == L".wbmp") {
        ok = Formats::DecodeWBMP(data, size, outInfo);
    } else if (extLower == L".bdf") {
        ok = Formats::DecodeBDF(data, size, outInfo);
    } else if (extLower == L".psf") {
        ok = Formats::DecodePSF(data, size, outInfo);
    } else if (extLower == L".y4m" || (size >= 10 && std::memcmp(data, "YUV4MPEG2 ", 10) == 0)) {
        ok = Formats::DecodeY4M(data, size, outInfo);
    } else if (extLower == L".ani" || (size >= 12 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "ACON", 4) == 0)) {
        ok = Formats::DecodeANI(data, size, outInfo);
    } else if (extLower == L".gpl" || extLower == L".pal" || extLower == L".act" || extLower == L".aco") {
        ok = Formats::DecodePaletteSwatch(data, size, extLower, outInfo);
    } else if (extLower == L".dcm" || extLower == L".dicom" || (size >= 132 && std::memcmp(data + 128, "DICM", 4) == 0)) {
        ok = Formats::DecodeDICOM(data, size, outInfo);
    } else if (extLower == L".exr") {
        ok = Formats::DecodeOpenEXR(data, size, outInfo);
    } else if (extLower == L".ktx2" || extLower == L".basis") {
        ok = Formats::DecodeKTX2OrBasis(data, size, extLower, outInfo);
    } else if (extLower == L".kra" || extLower == L".ora" || extLower == L".procreate") {
        ok = Formats::DecodeZipArtworkContainer(data, size, extLower, outInfo);
    } else if (extLower == L".clip") {
        ok = Formats::DecodeClipStudio(data, size, outInfo);
    } else if (extLower == L".aseprite") {
        ok = Formats::DecodeAseprite(data, size, outInfo);
    } else if (extLower == L".icns") {
        ok = Formats::DecodeAppleICNS(data, size, outInfo);
    } else if (extLower == L".jxl" || extLower == L".bpg") {
        ok = Formats::DecodeJxlOrBpg(data, size, extLower, outInfo);
    } else if (extLower == L".blp" || extLower == L".utx") {
        ok = Formats::DecodeBlpOrUtx(data, size, extLower, outInfo);
    } else if (extLower == L".dpx" || extLower == L".cin") {
        ok = Formats::DecodeDpxOrCineon(data, size, extLower, outInfo);
    } else if (extLower == L".cube" || extLower == L".3dl") {
        ok = Formats::DecodeColorLutCube(data, size, extLower, outInfo);
    } else if (extLower == L".ies") {
        ok = Formats::DecodePhotometricIES(data, size, extLower, outInfo);
    } else if (extLower == L".ttf" || extLower == L".otf" || extLower == L".woff" || extLower == L".woff2") {
        ok = Formats::DecodeFontSpecimen(data, size, extLower, outInfo);
    } else {
        ok = Formats::DecodeExtendedImages200(data, size, extLower, outInfo);
    }

    if (ok && outInfo.valid) {
        DownscaleIfRequested(maxDimension, outInfo);
        return true;
    }
    return false;
} E2_READER_CATCH(L"Image reader", extLower, { outInfo = ExtendedImageInfo(); return false; })

bool ImageFormatLoader::DecodeImageFile(
    const std::wstring& fullPath,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo)
try {
    outInfo = {};
    std::vector<uint8_t> bytes;
    if (!PathUtils::ReadFileBytes(fullPath, 256ULL * 1024 * 1024, bytes) || bytes.empty()) {
        return false;
    }
    std::wstring ext = PathUtils::GetExtension(fullPath);
    return DecodeImageMemory(bytes.data(), bytes.size(), ext, maxDimension, outInfo);
} E2_READER_CATCH(L"Image reader", fullPath, { outInfo = ExtendedImageInfo(); return false; })

} // namespace Explorer2
