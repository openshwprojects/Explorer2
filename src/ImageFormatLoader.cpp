#include "ImageFormatLoader.h"
#include "ImageExtensions.h"
#include "FormatSupport.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "JsonDom.h"
#include "ByteIO.h"
#if defined(_WIN32) || defined(E2_WIN32_HAL)
// Windows codecs: WIC (JPEG / PNG inside other formats, HEIF), DirectWrite and GDI (fonts, metafiles), Media
// Foundation (HEVC, H.264). On Linux with E2_WIN32_HAL, WIC is supplied by linux/win32_hal/src/WicHal.cpp.
#include "OsCompat.h"
#include <wrl/client.h>
#include <wincodec.h>
#include <dwrite_3.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#if defined(_WIN32)
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")
#endif
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <map>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <functional>

namespace Explorer2 {

namespace {
#include "formats/common/GpuTextureCodecs.h"
} // namespace
namespace {
#include "formats/images/Tga.h"
#include "formats/images/Psd.h"
#include "formats/images/Xcf.h"
#include "formats/images/Hdr.h"
#include "formats/images/Netpbm.h"
#include "formats/images/Pcx.h"
#include "formats/images/Qoi.h"
#include "formats/images/Dds.h"
#include "formats/images/Xbm.h"
#include "formats/images/Xpm.h"
#include "formats/images/Svg.h"

} // namespace

#include "formats/images/Pam.h"
#include "formats/images/Pfm.h"
#include "formats/images/Sgi.h"
#include "formats/images/Ras.h"
#include "formats/images/Ktx.h"
#include "formats/images/Vtf.h"
#include "formats/images/Farbfeld.h"
#include "formats/images/Fits.h"
#include "formats/images/Pkm.h"
#include "formats/images/Astc.h"
#include "formats/images/Pvr.h"
#include "formats/images/Ilbm.h"
#include "formats/images/Tim.h"
#include "formats/images/Wbmp.h"
#include "formats/images/Bdf.h"
#include "formats/images/Psf.h"
#include "formats/images/Y4m.h"
#include "formats/common/ImageFormatCommon.h"
#include "formats/images/Ani.h"
#include "formats/images/GimpPaletteGpl.h"
#include "formats/images/JascPalettePal.h"
#include "formats/images/AdobeColorAct.h"
#include "formats/images/AdobeSwatchAco.h"
#include "formats/images/AdobeSwatchExchangeAse.h"
#include "formats/images/OpenExr.h"
#include "formats/images/Ktx2Image.h"
#include "formats/images/BasisUniversal.h"
#include "formats/images/AppleIcns.h"
#include "formats/images/JpegXl.h"
#include "formats/images/BetterPortableGraphicsBpg.h"
#include "formats/images/BlizzardBlp.h"
#include "formats/images/UnrealUtx.h"
#include "formats/images/SmpteDpx.h"
#include "formats/images/KodakCineon.h"
#include "formats/images/ColorLutCube.h"
#include "formats/images/ColorLut3dl.h"
#include "formats/images/PhotometricIes.h"
#include "formats/images/OpenRasterOra.h"
#include "formats/images/KritaKra.h"
#include "formats/images/ProcreateImage.h"
#include "formats/images/ClipStudioClip.h"
#include "formats/images/AsepriteImage.h"
#include "formats/common/FontOutlineCommon.h"
#include "formats/images/TrueTypeFontTtf.h"
#include "formats/images/OpenTypeFontOtf.h"
#include "formats/images/WoffFont.h"
#include "formats/images/Woff2Font.h"
#include "formats/images/WindowsCursorCur.h"
#include "formats/raw/CanonCr2.h"
#include "formats/raw/CanonCr3.h"
#include "formats/raw/NikonNef.h"
#include "formats/raw/SonyArw.h"
#include "formats/raw/AdobeDng.h"
#include "formats/raw/FujifilmRaf.h"
#include "formats/raw/OlympusOrf.h"
#include "formats/raw/PanasonicRw2.h"
#include "formats/raw/PentaxPef.h"
#include "formats/images/WindowsEmf.h"
#include "formats/images/WindowsWmf.h"
#include "formats/images/PostScriptEps.h"
#include "formats/images/AdobeIllustratorAi.h"
#include "formats/images/CorelDrawCdr.h"
#include "formats/images/AffinityImage.h"
#include "formats/images/CgmVector.h"
#include "formats/images/PhotoshopBrushAbr.h"
#include "formats/images/GimpPat.h"
#include "formats/images/PhotoshopPat.h"
#include "formats/images/EmbroideryDst.h"
#include "formats/images/EmbroideryPes.h"
#include "formats/images/SixelGraphics.h"
#include "formats/images/AnsiArt.h"
#include "formats/images/RenderWareTxd.h"
#include "formats/images/UnrealTexture2D.h"
#include "formats/images/AvifImage.h"
#include "formats/images/HeifImage.h"
#include "formats/images/Jpeg2000.h"
#include "formats/images/TiffImage.h"
#include "formats/images/WebpImage.h"
#include "formats/images/Quake2Wal.h"
#include "formats/images/QuakeHlWadImage.h"
#include "formats/images/QuakeHlSprite.h"
#include "formats/images/QuakeLmp.h"
#include "formats/images/BuildEngineArt.h"
#include "formats/images/SwitchBntx.h"
#include "formats/images/VitaGxt.h"
#include "formats/images/Ps3Gtf.h"
#include "formats/images/NamcoNut.h"
#include "formats/images/CapcomTex.h"
#include "formats/images/Source2VtexC.h"
#include "formats/images/PaintDotNetPdn.h"
#include "formats/images/JpegLs.h"
#include "formats/images/SketchDesign.h"
#include "formats/images/Svgz.h"
#include "formats/images/Dicom.h"

bool ImageFormatLoader::IsExtendedImageExtension(const std::wstring& extLower) {
    return ImageExt::IsExtendedImageExt(extLower);
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
    } else if ((size >= 16 && std::memcmp(data, "PDN3", 4) == 0) || extLower == L".pdn") {
        ok = Formats::DecodePaintDotNet(data, size, outInfo);
    } else if (extLower == L".jls" || Formats::IsLikelyJpegLsSignature(data, size)) {
        ok = Formats::DecodeJpegLs(data, size, outInfo);
    } else if (extLower == L".avif" || (extLower != L".cr3" && Formats::IsLikelyAvifSignature(data, size))) {
        ok = Formats::DecodeAvifImage(data, size, extLower, outInfo);
    } else if (extLower == L".heic" || extLower == L".heif" || extLower == L".avci" ||
               (extLower != L".cr3" && Formats::IsLikelyAvifOrHeifSignature(data, size))) {
        ok = Formats::DecodeHeifImage(data, size, extLower, outInfo);
    } else if (extLower == L".jp2" || extLower == L".j2k" || extLower == L".jpf" || extLower == L".jpx" || extLower == L".j2c" ||
               Formats::IsLikelyJpeg2000Signature(data, size)) {
        ok = Formats::DecodeJpeg2000(data, size, extLower, outInfo);
    } else if (extLower == L".webp" ||
               (size >= 16 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WEBP", 4) == 0)) {
        ok = Formats::DecodeWebP(data, size, outInfo);
    } else if (extLower == L".tif" || extLower == L".tiff" || extLower == L".btf" || extLower == L".tf8" ||
               (size >= 16 && ((data[0] == 'I' && data[1] == 'I' && data[2] == 43 && data[3] == 0) ||
                               (data[0] == 'M' && data[1] == 'M' && data[2] == 0  && data[3] == 43)))) {
        ok = Formats::DecodeTiffOrBigTiff(data, size, extLower, outInfo);
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
        ok = Formats::DecodeKtx2Image(data, size, outInfo);
    } else if (size >= 77 && data[0] == 's' && data[1] == 'B' && ReadU16LE(data + 4) >= 77) {
        ok = Formats::DecodeBasisUniversal(data, size, outInfo);
    } else if (size >= 32 && data[0] == 0x76 && data[1] == 0x2F && data[2] == 0x31 && data[3] == 0x01) {
        ok = Formats::DecodeOpenEXR(data, size, outInfo);
    } else if (size >= 24 && std::memcmp(data, "CSFCHUNK", 8) == 0) {
        ok = Formats::DecodeClipStudio(data, size, outInfo);
    } else if (size >= 128 && data[4] == 0xE0 && data[5] == 0xA5) {
        ok = Formats::DecodeAseprite(data, size, outInfo);
    } else if (size >= 16 && std::memcmp(data, "icns", 4) == 0) {
        ok = Formats::DecodeAppleICNS(data, size, outInfo);
    } else if (size >= 12 && std::memcmp(data, "ASEF", 4) == 0) {
        ok = Formats::DecodeAdobeSwatchExchangeAse(data, size, outInfo);
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
    } else if (extLower == L".svgz") {
        ok = Formats::DecodeSVGZ(data, size, maxDimension, outInfo);
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
    } else if (extLower == L".pvr" || (size >= 52 && (std::memcmp(data, "PVR\x03", 4) == 0 || std::memcmp(data + 44, "PVR!", 4) == 0))) {
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
    } else if (extLower == L".cur") {
        ok = Formats::DecodeCur(data, size, outInfo);
    } else if (extLower == L".spr" || (size >= 44 && std::memcmp(data, "IDSP", 4) == 0)) {
        ok = Formats::DecodeQuakeOrHlSprite(data, size, outInfo);
    } else if ((size >= 12 && (std::memcmp(data, "WAD2", 4) == 0 || std::memcmp(data, "WAD3", 4) == 0)) || extLower == L".wad") {
        ok = Formats::DecodeWad2OrWad3Atlas(data, size, outInfo);
    } else if (extLower == L".wal") {
        ok = Formats::DecodeQuake2Wal(data, size, outInfo);
    } else if (extLower == L".lmp") {
        ok = Formats::DecodeQuakeLmp(data, size, outInfo);
    } else if (extLower == L".art") {
        ok = Formats::DecodeBuildEngineArt(data, size, L"", outInfo);
    } else if (extLower == L".bntx" || (size >= 144 && std::memcmp(data, "BNTX", 4) == 0)) {
        ok = Formats::DecodeSwitchBntx(data, size, outInfo);
    } else if (extLower == L".gxt" || (size >= 64 && std::memcmp(data, "GXT\0", 4) == 0)) {
        ok = Formats::DecodeVitaGxt(data, size, outInfo);
    } else if (extLower == L".gtf") {
        ok = Formats::DecodePs3Gtf(data, size, outInfo);
    } else if (extLower == L".nut" || (size >= 64 && (std::memcmp(data, "NTP3", 4) == 0 || std::memcmp(data, "NTWU", 4) == 0 || std::memcmp(data, "NTWD", 4) == 0))) {
        ok = Formats::DecodeNamcoNut(data, size, outInfo);
    } else if (extLower == L".tex" || Formats::IsLikelyCapcomTexBinary(data, size)) {
        ok = Formats::DecodeCapcomTex(data, size, outInfo);
    } else if (extLower == L".vtex_c") {
        ok = Formats::DecodeSource2VtexC(data, size, outInfo);
    }
    if (!ok) {
        if (extLower == L".ani" || (size >= 12 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "ACON", 4) == 0)) {
            ok = Formats::DecodeANI(data, size, outInfo);
        } else if (extLower == L".gpl") {
        ok = Formats::DecodeGimpPaletteGpl(data, size, outInfo);
    } else if (extLower == L".pal") {
        ok = Formats::DecodeJascPalettePal(data, size, outInfo);
    } else if (extLower == L".act") {
        ok = Formats::DecodeAdobeColorAct(data, size, outInfo);
    } else if (extLower == L".aco") {
        ok = Formats::DecodeAdobeSwatchAco(data, size, outInfo);
    } else if (extLower == L".dcm" || extLower == L".dicom" || (size >= 132 && std::memcmp(data + 128, "DICM", 4) == 0)) {
        ok = Formats::DecodeDICOM(data, size, outInfo);
    } else if (extLower == L".exr") {
        ok = Formats::DecodeOpenEXR(data, size, outInfo);
    } else if (extLower == L".ktx2") {
        ok = Formats::DecodeKtx2Image(data, size, outInfo);
    } else if (extLower == L".basis") {
        ok = Formats::DecodeBasisUniversal(data, size, outInfo);
    } else if (extLower == L".sketch") {
        ok = Formats::DecodeSketchDocument(data, size, maxDimension, outInfo);
    } else if (extLower == L".ora") {
        ok = Formats::DecodeOpenRasterOra(data, size, outInfo);
    } else if (extLower == L".kra") {
        ok = Formats::DecodeKritaKra(data, size, outInfo);
    } else if (extLower == L".procreate") {
        ok = Formats::DecodeProcreateImage(data, size, outInfo);
    } else if (extLower == L".clip") {
        ok = Formats::DecodeClipStudio(data, size, outInfo);
    } else if (extLower == L".aseprite") {
        ok = Formats::DecodeAseprite(data, size, outInfo);
    } else if (extLower == L".ase") {
        ok = Formats::DecodeAdobeSwatchExchangeAse(data, size, outInfo) ||
             Formats::DecodeAseprite(data, size, outInfo);
    } else if (extLower == L".icns") {
        ok = Formats::DecodeAppleICNS(data, size, outInfo);
    } else if (extLower == L".bpg" || (size >= 8 && std::memcmp(data, "BPG\xFB", 4) == 0)) {
        ok = Formats::DecodeBetterPortableGraphicsBpg(data, size, outInfo);
    } else if (extLower == L".jxl" ||
               (size >= 12 && std::memcmp(data, "\x00\x00\x00\x0CJXL \x0D\x0A\x87\x0A", 12) == 0)) {
        ok = Formats::DecodeJpegXl(data, size, outInfo);
    } else if (extLower == L".uasset" ||
               (size >= 64 && ReadU32LE(data) == 0x9E2A83C1u && static_cast<int32_t>(ReadU32LE(data + 4)) < 0)) {
        ok = Formats::DecodeUnrealTexture2D(data, size, L"", outInfo);
    } else if (size >= 148 && (std::memcmp(data, "BLP2", 4) == 0 || std::memcmp(data, "BLP1", 4) == 0)) {
        ok = Formats::DecodeBlizzardBlp(data, size, outInfo);
    } else if (extLower == L".blp") {
        ok = Formats::DecodeBlizzardBlp(data, size, outInfo);
    } else if (extLower == L".utx" || (size >= 36 && ReadU32LE(data) == 0x9E2A83C1u)) {
        ok = Formats::DecodeUnrealUtx(data, size, outInfo);
    } else if (extLower == L".txd" || (size >= 36 && ReadU32LE(data) == 0x16u && Formats::RwStreamDetail::IsPlausibleRwVersion(ReadU32LE(data + 8)))) {
        ok = Formats::DecodeRenderWareTxd(data, size, outInfo);
    } else if (extLower == L".dpx") {
        ok = Formats::DecodeSmpteDpx(data, size, outInfo);
    } else if (extLower == L".cin") {
        ok = Formats::DecodeKodakCineon(data, size, outInfo);
    } else if (extLower == L".cube") {
        ok = Formats::DecodeColorLutCube(data, size, outInfo);
    } else if (extLower == L".3dl") {
        ok = Formats::DecodeColorLut3dl(data, size, outInfo);
    } else if (extLower == L".ies") {
        ok = Formats::DecodePhotometricIES(data, size, extLower, outInfo);
    } else if (size >= 48 && std::memcmp(data, "wOF2", 4) == 0) {
        ok = Formats::DecodeWoff2Font(data, size, outInfo);
    } else if (size >= 44 && std::memcmp(data, "wOFF", 4) == 0) {
        ok = Formats::DecodeWoffFont(data, size, outInfo);
    } else if (extLower == L".woff2") {
        ok = Formats::DecodeWoff2Font(data, size, outInfo);
    } else if (extLower == L".woff") {
        ok = Formats::DecodeWoffFont(data, size, outInfo);
    } else if (extLower == L".otf" || (size >= 28 && std::memcmp(data, "OTTO", 4) == 0)) {
        ok = Formats::DecodeOpenTypeFontOtf(data, size, extLower, outInfo);
    } else if (extLower == L".ttf" || extLower == L".ttc" || (size >= 28 && std::memcmp(data, "ttcf", 4) == 0)) {
        ok = Formats::DecodeTrueTypeFontTtf(data, size, extLower, outInfo);
    } else if (size >= 24 && std::memcmp(data + 20, "GPAT", 4) == 0) {
        ok = Formats::DecodeGimpPat(data, size, outInfo);
    } else if ((size >= 12 && std::memcmp(data, "8BPT", 4) == 0) || extLower == L".pat") {
        ok = Formats::DecodePhotoshopPat(data, size, outInfo) || Formats::DecodeGimpPat(data, size, outInfo);
    } else if (extLower == L".abr") {
        ok = Formats::DecodePhotoshopBrushAbr(data, size, outInfo);
    } else if (extLower == L".cr2") {
        ok = Formats::DecodeCanonCr2(data, size, outInfo);
    } else if (extLower == L".cr3") {
        ok = Formats::DecodeCanonCr3(data, size, outInfo);
    } else if (extLower == L".nef") {
        ok = Formats::DecodeNikonNef(data, size, outInfo);
    } else if (extLower == L".arw") {
        ok = Formats::DecodeSonyArw(data, size, outInfo);
    } else if (extLower == L".dng") {
        ok = Formats::DecodeAdobeDng(data, size, outInfo);
    } else if (extLower == L".raf") {
        ok = Formats::DecodeFujifilmRaf(data, size, outInfo);
    } else if (extLower == L".orf") {
        ok = Formats::DecodeOlympusOrf(data, size, outInfo);
    } else if (extLower == L".rw2") {
        ok = Formats::DecodePanasonicRw2(data, size, outInfo);
    } else if (extLower == L".pef") {
        ok = Formats::DecodePentaxPef(data, size, outInfo);
    } else if (extLower == L".eps") {
        ok = Formats::DecodePostScriptEps(data, size, outInfo);
    } else if (extLower == L".ai") {
        ok = Formats::DecodeAdobeIllustratorAi(data, size, outInfo);
    } else if (extLower == L".cdr") {
        ok = Formats::DecodeCorelDrawCdr(data, size, outInfo);
    } else if (extLower == L".afphoto" || extLower == L".afdesign" || extLower == L".afpub") {
        ok = Formats::DecodeAffinityImage(data, size, extLower, outInfo);
    } else if (extLower == L".emf" || extLower == L".wmf") {
        if (size >= 88 && ReadU32LE(data) == 1u && ReadU32LE(data + 40) == 0x464D4520u) {
            ok = Formats::DecodeWindowsEmf(data, size, outInfo);
        } else if (extLower == L".wmf" || (size >= 18 && (ReadU32LE(data) == 0x9AC6CDD7u ||
                   ((ReadU16LE(data) == 1u || ReadU16LE(data) == 2u) && ReadU16LE(data + 2) == 9u)))) {
            ok = Formats::DecodeWindowsWmf(data, size, outInfo);
        } else {
            ok = Formats::DecodeWindowsEmf(data, size, outInfo);
        }
    } else if (extLower == L".cgm") {
        ok = Formats::DecodeCgmVector(data, size, outInfo);
    } else if (extLower == L".dst") {
        ok = Formats::DecodeEmbroideryDst(data, size, outInfo);
    } else if (extLower == L".pes") {
        ok = Formats::DecodeEmbroideryPes(data, size, outInfo);
    } else if (extLower == L".sixel") {
        ok = Formats::DecodeSixelGraphics(data, size, extLower, outInfo);
    } else if (extLower == L".ans") {
        ok = Formats::DecodeAnsiArt(data, size, outInfo);
    }
    }
    if (!ok) {
        ok = TryDecodeConsoleOrGameImage(data, size, extLower, maxDimension, outInfo);
    }

    if (ok && outInfo.valid) {
        DownscaleIfRequested(maxDimension, outInfo);
        return true;
    }
    outInfo = {};
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
    if (ext == L".art") {
        if (Formats::DecodeBuildEngineArt(bytes.data(), bytes.size(), fullPath, outInfo) && outInfo.valid) {
            DownscaleIfRequested(maxDimension, outInfo);
            return true;
        }
        outInfo = {};
        return false;
    }
    if (ext == L".uasset" ||
        (bytes.size() >= 64 && ReadU32LE(bytes.data()) == 0x9E2A83C1u && static_cast<int32_t>(ReadU32LE(bytes.data() + 4)) < 0))
    {
        if (Formats::DecodeUnrealTexture2D(bytes.data(), bytes.size(), fullPath, outInfo) && outInfo.valid) {
            DownscaleIfRequested(maxDimension, outInfo);
            return true;
        }
        outInfo = {};
        return false;
    }
    return DecodeImageMemory(bytes.data(), bytes.size(), ext, maxDimension, outInfo);
} E2_READER_CATCH(L"Image reader", fullPath, { outInfo = ExtendedImageInfo(); return false; })

} // namespace Explorer2
