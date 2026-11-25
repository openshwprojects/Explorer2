// FormatTestsImagesGpuGame.cpp: Stage 15 self-tests for console, game-engine, GPU block/swizzled
// texture formats, and X11 window dumps.
#include "SelfTestCommon.h"
#include "Types.h"
#include "ImageFormatLoader.h"
#include "PathUtils.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Explorer2::SelfTestSuite {

namespace {

uint64_t HashBgraBytes(const std::vector<uint8_t>& bytes) {
    uint64_t h = 14695981039346656037ull;
    for (uint8_t b : bytes) {
        h ^= static_cast<uint64_t>(b);
        h *= 1099511628211ull;
    }
    return h;
}

void PushU16LE(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}

void PushU16BE(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    b.push_back(static_cast<uint8_t>(v & 0xFFu));
}

void PushU32LE(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
}

void PushI32LE(std::vector<uint8_t>& b, int32_t v) {
    uint32_t u = 0u;
    std::memcpy(&u, &v, 4);
    PushU32LE(b, u);
}

void PushU32BE(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    b.push_back(static_cast<uint8_t>(v & 0xFFu));
}

void AssertImgRejected(TestCounter& tc, const wchar_t* testName, const std::vector<uint8_t>& badBytes, const wchar_t* ext) {
    ExtendedImageInfo badInfo;
    bool ok = ImageFormatLoader::DecodeImageMemory(badBytes.data(), badBytes.size(), ext, 0, badInfo);
    tc.Record(!ok && !badInfo.valid && badInfo.bgraPixels.empty(), testName,
              ok ? L"unexpectedly decoded corrupt input" : L"rejected cleanly");
}

} // namespace

void RunFormatTestsImagesGpuGame(TestCounter& tc) {
    const std::wstring imagesDir = TestDataDir() + L"\\images";

    // ========================================================================
    // 1. Sony PlayStation 2 "TIM2" v4 Texture (.tm2 / .tim2)
    // ========================================================================
    {
        tc.Record(ImageFormatLoader::IsExtendedImageExtension(L".tm2") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".tim2"),
                  L"Stage15.Images.Ps2Tim2_ExtensionsRegistered");

        ExtendedImageInfo img1;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\ps2_hud_icon.tm2", 0, img1);
        tc.Record(ok1 && img1.valid && img1.width == 16 && img1.height == 16 &&
                  img1.bgraPixels.size() == 16u * 16u * 4u &&
                  img1.bgraPixels[0] == 0u && img1.bgraPixels[1] == 0u &&
                  img1.bgraPixels[2] == 255u && img1.bgraPixels[3] == 255u,
                  L"Stage15.Images.Ps2Tim2_Csm1ClutUnswizzleAndGsAlpha");

        std::vector<uint8_t> tm2Rgba(128u + 48u + 4u * 4u * 4u, 0u);
        std::memcpy(tm2Rgba.data(), "TIM2", 4);
        tm2Rgba[4] = 4u;
        tm2Rgba[5] = 1u; // 128-byte aligned
        tm2Rgba[6] = 1u;
        uint8_t* ph = tm2Rgba.data() + 128u;
        const uint32_t imgSz = 64u, totSz = 48u + 64u;
        std::memcpy(ph + 0x00, &totSz, 4);
        std::memcpy(ph + 0x08, &imgSz, 4);
        ph[0x0C] = 48u;
        ph[0x11] = 1u;
        ph[0x13] = 3u; // TIM2_RGB32
        ph[0x14] = 4u;
        ph[0x16] = 4u;
        for (size_t p = 0; p < 16u; ++p) {
            uint8_t* px = tm2Rgba.data() + 128u + 48u + p * 4u;
            px[0] = 32u; px[1] = 140u; px[2] = 240u; px[3] = 255u;
        }
        ExtendedImageInfo img2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(tm2Rgba.data(), tm2Rgba.size(), L".tim2", 0, img2);
        tc.Record(ok2 && img2.valid && img2.width == 4 && img2.height == 4 &&
                  HashBgraBytes(img1.bgraPixels) != HashBgraBytes(img2.bgraPixels),
                  L"Stage15.Images.Ps2Tim2_128ByteAlignedRgba32AndDistinctHash");

        std::vector<uint8_t> truncTm2(tm2Rgba.begin(), tm2Rgba.begin() + 180u);
        AssertImgRejected(tc, L"Stage15.Images.Ps2Tim2_RejectTruncatedPixels", truncTm2, L".tm2");

        std::vector<uint8_t> badTypeTm2 = tm2Rgba;
        badTypeTm2[128u + 0x13] = 99u;
        AssertImgRejected(tc, L"Stage15.Images.Ps2Tim2_RejectInvalidImageType", badTypeTm2, L".tm2");
    }

    // ========================================================================
    // 2. Nintendo GameCube / Wii Texture Palette Library (.tpl)
    // ========================================================================
    {
        tc.Record(ImageFormatLoader::IsExtendedImageExtension(L".tpl"),
                  L"Stage15.Images.NintendoTpl_ExtensionRegistered");

        ExtendedImageInfo img1;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\wii_banner_cmpr.tpl", 0, img1);
        const uint8_t* p00 = img1.bgraPixels.size() >= 256u ? &img1.bgraPixels[0] : nullptr;
        const uint8_t* p40 = img1.bgraPixels.size() >= 256u ? &img1.bgraPixels[4u * 4u] : nullptr;
        tc.Record(ok1 && img1.valid && img1.width == 8 && img1.height == 8 && p00 && p40 &&
                  p00[2] == 255u && p00[0] == 0u && p40[0] == 255u && p40[2] == 0u,
                  L"Stage15.Images.NintendoTpl_CmprSubBlocksDecoded");

        std::vector<uint8_t> tplCi8;
        PushU32BE(tplCi8, 0x0020AF30u);
        PushU32BE(tplCi8, 1u);
        PushU32BE(tplCi8, 0x0Cu);
        PushU32BE(tplCi8, 0x20u);
        PushU32BE(tplCi8, 0x50u);
        tplCi8.resize(0x20u, 0u);
        PushU16BE(tplCi8, 4u);
        PushU16BE(tplCi8, 8u);
        PushU32BE(tplCi8, 9u);
        PushU32BE(tplCi8, 0x80u);
        tplCi8.resize(0x50u, 0u);
        PushU16BE(tplCi8, 4u);
        PushU16BE(tplCi8, 0u);
        PushU32BE(tplCi8, 2u);
        PushU32BE(tplCi8, 0x60u);
        tplCi8.resize(0x60u, 0u);
        PushU16BE(tplCi8, 0x8000u);
        PushU16BE(tplCi8, 0x83E0u);
        PushU16BE(tplCi8, 0xFC00u);
        PushU16BE(tplCi8, 0xFFFFu);
        tplCi8.resize(0x80u, 0u);
        tplCi8.insert(tplCi8.end(), 32u, 1u);

        ExtendedImageInfo img2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(tplCi8.data(), tplCi8.size(), L".tpl", 0, img2);
        tc.Record(ok2 && img2.valid && img2.width == 8 && img2.height == 4 &&
                  img2.bgraPixels[1] == 255u &&
                  HashBgraBytes(img1.bgraPixels) != HashBgraBytes(img2.bgraPixels),
                  L"Stage15.Images.NintendoTpl_Ci8Rgb5A3PaletteAndDistinctHash");

        std::vector<uint8_t> noPalTpl = tplCi8;
        noPalTpl[0x10] = noPalTpl[0x11] = noPalTpl[0x12] = noPalTpl[0x13] = 0u;
        AssertImgRejected(tc, L"Stage15.Images.NintendoTpl_RejectCi8WithoutPalette", noPalTpl, L".tpl");

        std::vector<uint8_t> truncTpl(tplCi8.begin(), tplCi8.begin() + 0x90u);
        AssertImgRejected(tc, L"Stage15.Images.NintendoTpl_RejectTruncatedTile", truncTpl, L".tpl");
    }

    // ========================================================================
    // 3. Nintendo GameCube / Wii JSystem Binary Texture (.bti)
    // ========================================================================
    {
        tc.Record(ImageFormatLoader::IsExtendedImageExtension(L".bti"),
                  L"Stage15.Images.NintendoBti_ExtensionRegistered");

        ExtendedImageInfo img1;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\windwaker_rupee.bti", 0, img1);
        tc.Record(ok1 && img1.valid && img1.width == 8 && img1.height == 8 &&
                  img1.bgraPixels[0] == 40u && img1.bgraPixels[1] == 0u &&
                  img1.bgraPixels[2] == 250u && img1.bgraPixels[3] == 255u,
                  L"Stage15.Images.NintendoBti_Rgba8DualCachelineDecoded");

        std::vector<uint8_t> bti5a3(0x20u + 32u, 0u);
        bti5a3[0x00] = 0x05u;
        bti5a3[0x01] = 0x01u;
        bti5a3[0x03] = 4u;
        bti5a3[0x05] = 4u;
        bti5a3[0x14] = 1u;
        bti5a3[0x15] = 1u;
        bti5a3[0x18] = 1u;
        bti5a3[0x1F] = 0x20u;
        for (size_t i = 0; i < 16u; ++i) {
            bti5a3[0x20u + i * 2u + 0u] = 0x80u;
            bti5a3[0x20u + i * 2u + 1u] = 0x1Fu;
        }
        ExtendedImageInfo img2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(bti5a3.data(), bti5a3.size(), L".bti", 0, img2);
        tc.Record(ok2 && img2.valid && img2.width == 4 && img2.height == 4 &&
                  img2.bgraPixels[0] == 255u && img2.bgraPixels[2] == 0u &&
                  HashBgraBytes(img1.bgraPixels) != HashBgraBytes(img2.bgraPixels),
                  L"Stage15.Images.NintendoBti_Rgb5A3AndDistinctHash");

        std::vector<uint8_t> badWrap = bti5a3;
        badWrap[0x06] = 9u;
        AssertImgRejected(tc, L"Stage15.Images.NintendoBti_RejectInvalidWrapMode", badWrap, L".bti");

        std::vector<uint8_t> badOff = bti5a3;
        badOff[0x1E] = 0x10u;
        AssertImgRejected(tc, L"Stage15.Images.NintendoBti_RejectOffsetPastEof", badOff, L".bti");
    }

    // ========================================================================
    // 4. Sony PlayStation Portable Graphic Image Map (.gim)
    // ========================================================================
    {
        tc.Record(ImageFormatLoader::IsExtendedImageExtension(L".gim"),
                  L"Stage15.Images.PspGim_ExtensionRegistered");

        ExtendedImageInfo img1;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\psp_xmb_icon.gim", 0, img1);
        const uint8_t* pxL = img1.bgraPixels.size() >= 32u * 8u * 4u ? &img1.bgraPixels[0] : nullptr;
        const uint8_t* pxR = img1.bgraPixels.size() >= 32u * 8u * 4u ? &img1.bgraPixels[20u * 4u] : nullptr;
        tc.Record(ok1 && img1.valid && img1.width == 32 && img1.height == 8 && pxL && pxR &&
                  pxL[2] == 255u && pxL[0] == 40u && pxR[0] == 255u && pxR[1] == 220u && pxR[2] == 0u,
                  L"Stage15.Images.PspGim_Ge16x8UnswizzleAndIndex8Palette");

        std::vector<uint8_t> beGim = { '.', 'G', 'I', 'M', '1', '.', '0', '0', 0, 'P', 'S', 'P', 0, 0, 0, 0 };
        const uint32_t imgBodySz = 0x40u + 4u * 4u * 2u;
        const uint32_t imgChkSz  = 0x10u + imgBodySz;
        const uint32_t rootChkSz = 0x10u + imgChkSz;
        PushU16BE(beGim, 0x0002u); PushU16BE(beGim, 0u);
        PushU32BE(beGim, rootChkSz); PushU32BE(beGim, rootChkSz); PushU32BE(beGim, 0x10u);
        PushU16BE(beGim, 0x0004u); PushU16BE(beGim, 0u);
        PushU32BE(beGim, imgChkSz); PushU32BE(beGim, imgChkSz); PushU32BE(beGim, 0x10u);
        const size_t hb = beGim.size();
        beGim.resize(hb + 0x40u, 0u);
        beGim[hb + 0x01] = 0x30u;
        beGim[hb + 0x05] = 0x00u;
        beGim[hb + 0x07] = 0x00u;
        beGim[hb + 0x09] = 4u;
        beGim[hb + 0x0B] = 4u;
        beGim[hb + 0x0D] = 16u;
        beGim[hb + 0x0F] = 2u;
        beGim[hb + 0x11] = 1u;
        beGim[hb + 0x1B] = 0x30u;
        beGim[hb + 0x33] = 0x40u;
        for (size_t i = 0; i < 16u; ++i) PushU16BE(beGim, 0x07E0u);

        ExtendedImageInfo img2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(beGim.data(), beGim.size(), L".gim", 0, img2);
        tc.Record(ok2 && img2.valid && img2.width == 4 && img2.height == 4 &&
                  img2.bgraPixels[1] == 255u &&
                  HashBgraBytes(img1.bgraPixels) != HashBgraBytes(img2.bgraPixels),
                  L"Stage15.Images.PspGim_BigEndianRgba5650AndDistinctHash");

        std::vector<uint8_t> truncGim(beGim.begin(), beGim.begin() + beGim.size() - 12u);
        AssertImgRejected(tc, L"Stage15.Images.PspGim_RejectTruncatedPixels", truncGim, L".gim");

        std::vector<uint8_t> badMagic = beGim;
        badMagic[0] = 'X';
        AssertImgRejected(tc, L"Stage15.Images.PspGim_RejectWrongMagic", badMagic, L".gim");

        std::vector<uint8_t> wrapGim = beGim;
        wrapGim[hb + 0x18] = 0xFFu; wrapGim[hb + 0x19] = 0xFFu;
        wrapGim[hb + 0x1A] = 0xFFu; wrapGim[hb + 0x1B] = 0xFFu;
        wrapGim[hb + 0x1C] = 0u; wrapGim[hb + 0x1D] = 0u; wrapGim[hb + 0x1E] = 0u; wrapGim[hb + 0x1F] = 0u;
        AssertImgRejected(tc, L"Stage15.Images.PspGim_FuzzReg_OffsetsOffWrap", wrapGim, L".gim");

        std::vector<uint8_t> bppMismatch = beGim;
        bppMismatch[hb + 0x05] = 0x03u;
        AssertImgRejected(tc, L"Stage15.Images.PspGim_FuzzReg_FormatBppMismatch", bppMismatch, L".gim");
    }

    // ========================================================================
    // 5. Ritual Entertainment UberTools Texture (.ftx)
    // ========================================================================
    {
        tc.Record(ImageFormatLoader::IsExtendedImageExtension(L".ftx"),
                  L"Stage15.Images.UberToolsFtx_ExtensionRegistered");

        ExtendedImageInfo img1;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\alice_vorpal_blade.ftx", 0, img1);
        tc.Record(ok1 && img1.valid && img1.width == 16 && img1.height == 16 &&
                  img1.channels == 4u && img1.bgraPixels.back() == 128u,
                  L"Stage15.Images.UberToolsFtx_TranslucentRgbaDecoded");

        std::vector<uint8_t> ftxOpaque;
        PushI32LE(ftxOpaque, 4);
        PushI32LE(ftxOpaque, 4);
        PushI32LE(ftxOpaque, 0);
        for (size_t i = 0; i < 16u; ++i) {
            ftxOpaque.push_back(220u); ftxOpaque.push_back(90u); ftxOpaque.push_back(30u); ftxOpaque.push_back(0u);
        }
        ExtendedImageInfo img2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(ftxOpaque.data(), ftxOpaque.size(), L".ftx", 0, img2);
        tc.Record(ok2 && img2.valid && img2.width == 4 && img2.height == 4 &&
                  img2.channels == 3u && img2.bgraPixels[3] == 255u &&
                  HashBgraBytes(img1.bgraPixels) != HashBgraBytes(img2.bgraPixels),
                  L"Stage15.Images.UberToolsFtx_OpaqueRgbxAndDistinctHash");

        std::vector<uint8_t> badSize = ftxOpaque;
        badSize.push_back(0xFFu);
        AssertImgRejected(tc, L"Stage15.Images.UberToolsFtx_RejectSizeMismatch", badSize, L".ftx");

        std::vector<uint8_t> badFlag = ftxOpaque;
        badFlag[8] = 5u;
        AssertImgRejected(tc, L"Stage15.Images.UberToolsFtx_RejectInvalidAlphaFlag", badFlag, L".ftx");
    }

    // ========================================================================
    // 6. Raven Software Heretic II 8-bit Paletted MipTexture (.m8)
    // ========================================================================
    {
        tc.Record(ImageFormatLoader::IsExtendedImageExtension(L".m8"),
                  L"Stage15.Images.Heretic2M8_ExtensionRegistered");

        ExtendedImageInfo img1;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\heretic2_mana_rune.m8", 0, img1);
        tc.Record(ok1 && img1.valid && img1.width == 16 && img1.height == 16 &&
                  img1.bgraPixels[0] == 255u && img1.bgraPixels[1] == 180u && img1.bgraPixels[2] == 40u &&
                  img1.bgraPixels[15u * 4u + 3u] == 0u,
                  L"Stage15.Images.Heretic2M8_PaletteMipChainAndMagentaCutout");

        std::vector<uint8_t> m8b(1040u + 64u, 0u);
        m8b[0] = 2u;
        std::memcpy(m8b.data() + 4u, "stone_wall", 10);
        m8b[0x24] = 8u;
        m8b[0x64] = 8u;
        const uint32_t off0 = 1040u;
        std::memcpy(m8b.data() + 0xA4, &off0, 4);
        m8b[260u + 2u * 3u + 0u] = 210u;
        m8b[260u + 2u * 3u + 1u] = 175u;
        m8b[260u + 2u * 3u + 2u] = 45u;
        std::fill(m8b.begin() + 1040u, m8b.end(), 2u);

        ExtendedImageInfo img2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(m8b.data(), m8b.size(), L".m8", 0, img2);
        tc.Record(ok2 && img2.valid && img2.width == 8 && img2.height == 8 && img2.channels == 3u &&
                  HashBgraBytes(img1.bgraPixels) != HashBgraBytes(img2.bgraPixels),
                  L"Stage15.Images.Heretic2M8_OpaqueStoneAndDistinctHash");

        std::vector<uint8_t> badVer = m8b;
        badVer[0] = 4u;
        AssertImgRejected(tc, L"Stage15.Images.Heretic2M8_RejectWrongVersion", badVer, L".m8");

        std::vector<uint8_t> badOff = m8b;
        const uint32_t insideHdr = 512u;
        std::memcpy(badOff.data() + 0xA4, &insideHdr, 4);
        AssertImgRejected(tc, L"Stage15.Images.Heretic2M8_RejectOffsetInsideHeader", badOff, L".m8");
    }

    // ========================================================================
    // 7. Raven Software Heretic II / SoF 32-bit RGBA MipTexture (.m32)
    // ========================================================================
    {
        tc.Record(ImageFormatLoader::IsExtendedImageExtension(L".m32"),
                  L"Stage15.Images.Heretic2M32_ExtensionRegistered");

        ExtendedImageInfo img1;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\sof_hud_crosshair.m32", 0, img1);
        const size_t cOff = (7u * 16u + 7u) * 4u;
        tc.Record(ok1 && img1.valid && img1.width == 16 && img1.height == 16 &&
                  img1.bgraPixels[3] == 64u &&
                  img1.bgraPixels[cOff + 1u] == 250u && img1.bgraPixels[cOff + 3u] == 255u,
                  L"Stage15.Images.Heretic2M32_RgbaCrosshairAndAlphaDecoded");

        std::vector<uint8_t> m32b(968u + 4u * 4u * 4u, 0u);
        m32b[0] = 4u;
        std::memcpy(m32b.data() + 4u, "fx/spark", 8);
        m32b[0x204] = 4u;
        m32b[0x244] = 4u;
        const uint32_t off0 = 968u;
        std::memcpy(m32b.data() + 0x284, &off0, 4);
        for (size_t i = 0; i < 16u; ++i) {
            uint8_t* p = m32b.data() + 968u + i * 4u;
            p[0] = 255u; p[1] = 180u; p[2] = 20u; p[3] = 255u;
        }
        ExtendedImageInfo img2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(m32b.data(), m32b.size(), L".m32", 0, img2);
        tc.Record(ok2 && img2.valid && img2.width == 4 && img2.height == 4 &&
                  HashBgraBytes(img1.bgraPixels) != HashBgraBytes(img2.bgraPixels),
                  L"Stage15.Images.Heretic2M32_SparkAndDistinctHash");

        std::vector<uint8_t> badVer = m32b;
        badVer[0] = 2u;
        AssertImgRejected(tc, L"Stage15.Images.Heretic2M32_RejectWrongVersion", badVer, L".m32");

        std::vector<uint8_t> truncM32(m32b.begin(), m32b.begin() + 980u);
        AssertImgRejected(tc, L"Stage15.Images.Heretic2M32_RejectTruncatedPixels", truncM32, L".m32");
    }

    // ========================================================================
    // 8. Sega GameCube / Wii "GVRT" (+ "GBIX") Texture (.gvr)
    // ========================================================================
    {
        tc.Record(ImageFormatLoader::IsExtendedImageExtension(L".gvr"),
                  L"Stage15.Images.SegaGvr_ExtensionRegistered");

        ExtendedImageInfo img1;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\sonic_ring_gc.gvr", 0, img1);
        tc.Record(ok1 && img1.valid && img1.width == 8 && img1.height == 8 &&
                  img1.bgraPixels[0] == 0u && img1.bgraPixels[1] == 255u && img1.bgraPixels[2] == 255u &&
                  img1.bgraPixels[5u * 4u + 0u] == 255u && img1.bgraPixels[5u * 4u + 2u] == 0u,
                  L"Stage15.Images.SegaGvr_GbixAndRgb565TilesDecoded");

        std::vector<uint8_t> gvrCi4 = { 'G', 'V', 'R', 'T' };
        PushU32LE(gvrCi4, 8u + 32u + 32u);
        PushU16LE(gvrCi4, 0u);
        gvrCi4.push_back(0x18u);
        gvrCi4.push_back(0x08u);
        PushU16BE(gvrCi4, 8u);
        PushU16BE(gvrCi4, 8u);
        for (size_t i = 0; i < 16u; ++i) PushU16BE(gvrCi4, i == 2u ? 0xF800u : 0x0000u);
        gvrCi4.insert(gvrCi4.end(), 32u, 0x22u);

        ExtendedImageInfo img2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(gvrCi4.data(), gvrCi4.size(), L".gvr", 0, img2);
        tc.Record(ok2 && img2.valid && img2.width == 8 && img2.height == 8 &&
                  img2.bgraPixels[2] == 255u &&
                  HashBgraBytes(img1.bgraPixels) != HashBgraBytes(img2.bgraPixels),
                  L"Stage15.Images.SegaGvr_EmbeddedCi4PaletteAndDistinctHash");

        std::vector<uint8_t> extPalGvr = gvrCi4;
        extPalGvr[0x0A] = 0x02u;
        AssertImgRejected(tc, L"Stage15.Images.SegaGvr_RejectExternalPaletteOnly", extPalGvr, L".gvr");

        std::vector<uint8_t> badGbix = { 'G', 'B', 'I', 'X', 0xFF, 0x00, 0x00, 0x00 };
        badGbix.insert(badGbix.end(), 48u, 0u);
        AssertImgRejected(tc, L"Stage15.Images.SegaGvr_RejectInvalidGbixLength", badGbix, L".gvr");
    }

    // ========================================================================
    // 9. Nintendo Wii NW4R Binary Texture (.tex0)
    // ========================================================================
    {
        tc.Record(ImageFormatLoader::IsExtendedImageExtension(L".tex0"), L"Stage15.Images.Nw4rTEX0_ExtensionRegistered");

        ExtendedImageInfo img1;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\brawl_emblem.tex0", 0, img1);
        tc.Record(ok1 && img1.valid && img1.width == 8 && img1.height == 8 &&
                  img1.bgraPixels[0] == 255u && img1.bgraPixels[1] == 220u && img1.bgraPixels[2] == 0u,
                  L"Stage15.Images.Nw4rTEX0_Rgba8DualCachelineSample");

        std::vector<uint8_t> t2(0x40u + 32u, 0u);
        std::memcpy(t2.data(), "TEX0", 4);
        t2[0x07] = 0x60u;
        t2[0x0B] = 1u;
        t2[0x13] = 0x40u;
        t2[0x1D] = 4u;
        t2[0x1F] = 4u;
        t2[0x23] = 4u;
        t2[0x27] = 1u;
        for (size_t i = 0; i < 16u; ++i) { t2[0x40u + i * 2u] = 0xF8u; t2[0x40u + i * 2u + 1u] = 0x00u; }
        ExtendedImageInfo img2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(t2.data(), t2.size(), L".tex0", 0, img2);
        tc.Record(ok2 && img2.valid && img2.width == 4 && img2.height == 4 &&
                  HashBgraBytes(img1.bgraPixels) != HashBgraBytes(img2.bgraPixels),
                  L"Stage15.Images.Nw4rTEX0_Rgb565AndDistinctHash");

        t2[0x0B] = 99u;
        AssertImgRejected(tc, L"Stage15.Images.Nw4rTEX0_RejectInvalidVersion", t2, L".tex0");
    }

    // ========================================================================
    // 10. Nintendo 3DS CTR Binary Layout Image (.bclim)
    // ========================================================================
    {
        tc.Record(ImageFormatLoader::IsExtendedImageExtension(L".bclim"), L"Stage15.Images.CtrBCLIM_ExtensionRegistered");

        ExtendedImageInfo img1;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\3ds_badge_morton.bclim", 0, img1);
        tc.Record(ok1 && img1.valid && img1.width == 8 && img1.height == 8 &&
                  img1.bgraPixels[2] == 240u && img1.bgraPixels[3] == 255u,
                  L"Stage15.Images.CtrBCLIM_MortonRgba8Sample");

        std::vector<uint8_t> b2(64u, 180u);
        b2.insert(b2.end(), { 'C','L','I','M' });
        PushU16LE(b2, 0xFEFFu); PushU16LE(b2, 0x14u); PushU32LE(b2, 0x02020000u);
        PushU32LE(b2, 64u + 0x28u); PushU16LE(b2, 1u); PushU16LE(b2, 0u);
        b2.insert(b2.end(), { 'i','m','a','g' });
        PushU32LE(b2, 0x10u); PushU16LE(b2, 8u); PushU16LE(b2, 8u); PushU32LE(b2, 0u); PushU32LE(b2, 64u);
        ExtendedImageInfo img2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(b2.data(), b2.size(), L".bclim", 0, img2);
        tc.Record(ok2 && img2.valid && HashBgraBytes(img1.bgraPixels) != HashBgraBytes(img2.bgraPixels),
                  L"Stage15.Images.CtrBCLIM_L8AndDistinctHash");

        b2[b2.size() - 4u] = 0xFFu;
        AssertImgRejected(tc, L"Stage15.Images.CtrBCLIM_RejectOobDataLen", b2, L".bclim");
    }

    // ========================================================================
    // 11. Nintendo Wii U Café Binary Layout Image (.bflim — GX2 Tiled Swizzle & Linear)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".bflim") && ImageFormatLoader::IsExtendedImageExtension(L".bflim"),
                  L"Stage15.Images.WiiUBFLIM_ExtensionRegistered");

        ExtendedImageInfo bflimSwiz, bflimLin;
        bool okSwiz = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\wiiu_gx2_swizzled.bflim", 0, bflimSwiz);
        bool okLin  = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\wiiu_banner_icon.bflim", 0, bflimLin);
        bool swizOk = okSwiz && bflimSwiz.valid && bflimSwiz.width == 16 && bflimSwiz.height == 16 &&
                      bflimSwiz.formatName.find(L"BFLIM") != std::wstring::npos &&
                      bflimSwiz.bgraPixels[(4 * 16 + 4) * 4 + 2] == 230u &&
                      bflimSwiz.bgraPixels[(4 * 16 + 4) * 4 + 1] == 60u &&
                      bflimSwiz.bgraPixels[(4 * 16 + 12) * 4 + 2] == 35u &&
                      bflimSwiz.bgraPixels[(12 * 16 + 4) * 4 + 1] == 210u &&
                      bflimSwiz.bgraPixels[(12 * 16 + 12) * 4 + 2] == 35u &&
                      bflimSwiz.bgraPixels[(12 * 16 + 12) * 4 + 1] == 210u;
        bool linOk  = okLin && bflimLin.valid && bflimLin.width == 8 && bflimLin.height == 8 &&
                      bflimLin.formatName.find(L"BFLIM") != std::wstring::npos &&
                      bflimLin.bgraPixels[1] == 180u && bflimLin.bgraPixels[0] == 0u &&
                      bflimLin.bgraPixels[(7 * 8 + 7) * 4 + 0] == 252u;
        tc.Record(swizOk && linOk && HashBgraBytes(bflimSwiz.bgraPixels) != HashBgraBytes(bflimLin.bgraPixels),
                  L"Stage15.Images.WiiUBFLIM_Gx2TiledSwizzleAndLinearSamples");

        std::vector<uint8_t> f2 = { 0x00, 0xF8, 0xE0, 0x07, 0x00, 0x00, 0xFF, 0xFF };
        f2.insert(f2.end(), { 'F','L','I','M' });
        PushU16LE(f2, 0xFEFFu); PushU16LE(f2, 0x14u); PushU32LE(f2, 1u);
        PushU32LE(f2, 8u + 0x28u); PushU16LE(f2, 1u); PushU16LE(f2, 0u);
        f2.insert(f2.end(), { 'i','m','a','g' });
        PushU32LE(f2, 0x10u); PushU16LE(f2, 4u); PushU16LE(f2, 4u); PushU16LE(f2, 64u);
        f2.push_back(0x0Cu); f2.push_back(0u); PushU32LE(f2, 8u);
        ExtendedImageInfo imgBc1;
        bool okBc1 = ImageFormatLoader::DecodeImageMemory(f2.data(), f2.size(), L".bflim", 0, imgBc1);
        tc.Record(okBc1 && imgBc1.valid && imgBc1.width == 4 && imgBc1.height == 4 &&
                  HashBgraBytes(bflimSwiz.bgraPixels) != HashBgraBytes(imgBc1.bgraPixels),
                  L"Stage15.Images.WiiUBFLIM_Bc1AndDistinctHash");

        f2[f2.size() - 6u] = 0x7Fu;
        AssertImgRejected(tc, L"Stage15.Images.WiiUBFLIM_RejectUnknownFormat", f2, L".bflim");
    }

    // ========================================================================
    // 12. Monolith LithTech Engine Texture (.dtx)
    // ========================================================================
    {
        tc.Record(ImageFormatLoader::IsExtendedImageExtension(L".dtx"), L"Stage15.Images.LithTechDTX_ExtensionRegistered");

        ExtendedImageInfo img1;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\nolf_crate_wall.dtx", 0, img1);
        tc.Record(ok1 && img1.valid && img1.width == 8 && img1.height == 8 &&
                  img1.formatName.find(L".dtx") != std::wstring::npos,
                  L"Stage15.Images.LithTechDTX_Lt2Bpp32Sample");

        std::vector<uint8_t> dtx2(164u, 0u);
        int32_t verNeg5 = -5;
        std::memcpy(dtx2.data() + 4u, &verNeg5, 4);
        dtx2[8] = 4u; dtx2[10] = 4u; dtx2[12] = 1u; dtx2[0x1E] = 4u;
        dtx2.insert(dtx2.end(), { 0x1F, 0x00, 0x00, 0xF8, 0xAA, 0xAA, 0x55, 0x55 });
        ExtendedImageInfo img2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(dtx2.data(), dtx2.size(), L".dtx", 0, img2);
        tc.Record(ok2 && img2.valid && img2.width == 4 && img2.height == 4 &&
                  HashBgraBytes(img1.bgraPixels) != HashBgraBytes(img2.bgraPixels),
                  L"Stage15.Images.LithTechDTX_Dxt1AndDistinctHash");

        dtx2[0] = 9u;
        AssertImgRejected(tc, L"Stage15.Images.LithTechDTX_RejectNonTextureResType", dtx2, L".dtx");
    }

    // ========================================================================
    // 13. Ritual Entertainment SiN MipTexture (.swl)
    // ========================================================================
    {
        tc.Record(ImageFormatLoader::IsExtendedImageExtension(L".swl"), L"Stage15.Images.RitualSWL_ExtensionRegistered");

        ExtendedImageInfo img1;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\sin_sec_panel.swl", 0, img1);
        tc.Record(ok1 && img1.valid && img1.width == 16 && img1.height == 16 &&
                  img1.formatName.find(L".swl") != std::wstring::npos,
                  L"Stage15.Images.RitualSWL_PalettedMipSample");

        std::vector<uint8_t> swl2(0x4CCu + 85u, 0u);
        std::memcpy(swl2.data(), "sin/alarm", 9);
        swl2[0x80] = 8u; swl2[0x84] = 8u;
        swl2[0x88 + 4u + 0u] = 250u; swl2[0x88 + 4u + 3u] = 255u;
        uint32_t m0 = 0x4CCu, m1 = m0 + 64u, m2 = m1 + 16u, m3 = m2 + 4u;
        std::memcpy(swl2.data() + 0x4BCu, &m0, 4);
        std::memcpy(swl2.data() + 0x4C0u, &m1, 4);
        std::memcpy(swl2.data() + 0x4C4u, &m2, 4);
        std::memcpy(swl2.data() + 0x4C8u, &m3, 4);
        std::fill(swl2.begin() + 0x4CCu, swl2.end(), 1u);
        ExtendedImageInfo img2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(swl2.data(), swl2.size(), L".swl", 0, img2);
        tc.Record(ok2 && img2.valid && img2.width == 8 && img2.height == 8 &&
                  HashBgraBytes(img1.bgraPixels) != HashBgraBytes(img2.bgraPixels),
                  L"Stage15.Images.RitualSWL_8x8AndDistinctHash");

        swl2[0x80] = 7u;
        AssertImgRejected(tc, L"Stage15.Images.RitualSWL_RejectUnalignedDimension", swl2, L".swl");
    }

    // ========================================================================
    // 14. EA Sports / Need for Speed SHPI/FSH (.fsh)
    // ========================================================================
    {
        tc.Record(ImageFormatLoader::IsExtendedImageExtension(L".fsh"), L"Stage15.Images.EaSportsFSH_ExtensionRegistered");

        ExtendedImageInfo img1;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\nfs_car_decal.fsh", 0, img1);
        tc.Record(ok1 && img1.valid && img1.width == 8 && img1.height == 8 &&
                  img1.formatName.find(L"SHPI/FSH") != std::wstring::npos,
                  L"Stage15.Images.EaSportsFSH_Bgra32Sample");

        std::vector<uint8_t> fsh2;
        fsh2.insert(fsh2.end(), { 'S','H','P','I' });
        const uint32_t totSz = 24u + 16u + 16u + 16u + 8u;
        PushU32LE(fsh2, totSz); PushU32LE(fsh2, 1u);
        fsh2.insert(fsh2.end(), { 'G','I','M','X', 't','e','x','0' });
        PushU32LE(fsh2, 24u);
        fsh2.push_back(0x7Bu); fsh2.push_back(32u); fsh2.push_back(0u); fsh2.push_back(0u);
        PushU16LE(fsh2, 4u); PushU16LE(fsh2, 4u); PushU32LE(fsh2, 0u); PushU32LE(fsh2, 0u);
        for (int i = 0; i < 16; ++i) fsh2.push_back(static_cast<uint8_t>(i & 1));
        fsh2.push_back(0x2Au); fsh2.push_back(0u); fsh2.push_back(0u); fsh2.push_back(0u);
        PushU16LE(fsh2, 2u); PushU16LE(fsh2, 1u); PushU32LE(fsh2, 0u); PushU32LE(fsh2, 0u);
        fsh2.insert(fsh2.end(), { 0, 200, 0, 255,   255, 0, 120, 255 });
        ExtendedImageInfo img2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(fsh2.data(), fsh2.size(), L".fsh", 0, img2);
        tc.Record(ok2 && img2.valid && img2.width == 4 && img2.height == 4 &&
                  HashBgraBytes(img1.bgraPixels) != HashBgraBytes(img2.bgraPixels),
                  L"Stage15.Images.EaSportsFSH_Paletted8AndDistinctHash");

        fsh2[20] = 0xFFu;
        AssertImgRejected(tc, L"Stage15.Images.EaSportsFSH_RejectOobDirOffset", fsh2, L".fsh");
    }

    // ========================================================================
    // 15. Konami Metal Gear Solid HD Texture (.ctxr)
    // ========================================================================
    {
        tc.Record(ImageFormatLoader::IsExtendedImageExtension(L".ctxr"), L"Stage15.Images.KonamiCTXR_ExtensionRegistered");

        ExtendedImageInfo img1;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\mgs2_codec_icon.ctxr", 0, img1);
        tc.Record(ok1 && img1.valid && img1.width == 8 && img1.height == 8 &&
                  img1.bgraPixels[1] == 210u && img1.bgraPixels[2] == 60u && img1.bgraPixels[3] == 255u,
                  L"Stage15.Images.KonamiCTXR_TxrcSample");

        std::vector<uint8_t> c2(0x84u + 16u, 0u);
        std::memcpy(c2.data(), "TXRC", 4);
        c2[0x07] = 7u; c2[0x09] = 2u; c2[0x0B] = 2u; c2[0x0D] = 1u; c2[0x26] = 1u; c2[0x83] = 16u;
        for (size_t i = 0; i < 4u; ++i) { c2[0x84u + i * 4u + 0u] = 255u; c2[0x84u + i * 4u + 1u] = 220u; }
        ExtendedImageInfo img2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(c2.data(), c2.size(), L".ctxr", 0, img2);
        tc.Record(ok2 && img2.valid && img2.width == 2 && img2.height == 2 &&
                  HashBgraBytes(img1.bgraPixels) != HashBgraBytes(img2.bgraPixels),
                  L"Stage15.Images.KonamiCTXR_2x2AndDistinctHash");

        c2[0x83] = 4u;
        AssertImgRejected(tc, L"Stage15.Images.KonamiCTXR_RejectUndersizedMip0Len", c2, L".ctxr");
    }

    // ========================================================================
    // 16. Infinity Ward Call of Duty Texture (.iwi)
    // ========================================================================
    {
        tc.Record(ImageFormatLoader::IsExtendedImageExtension(L".iwi"), L"Stage15.Images.InfinityWardIWI_ExtensionRegistered");

        ExtendedImageInfo img1;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\cod4_reticle.iwi", 0, img1);
        tc.Record(ok1 && img1.valid && img1.width == 4 && img1.height == 4 &&
                  img1.bgraPixels[0] == 50u && img1.bgraPixels[1] == 190u && img1.bgraPixels[2] == 240u,
                  L"Stage15.Images.InfinityWardIWI_ReverseMipTailSample");

        std::vector<uint8_t> iwi8(32u, 0u);
        std::memcpy(iwi8.data(), "IWi\x08", 4);
        iwi8[8] = 0x0Bu; iwi8[10] = 4u; iwi8[12] = 4u; iwi8[14] = 1u; iwi8[16] = 32u;
        iwi8.insert(iwi8.end(), { 0xE0, 0x07, 0x1F, 0x00, 0x05, 0x05, 0x50, 0x50 });
        ExtendedImageInfo img2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(iwi8.data(), iwi8.size(), L".iwi", 0, img2);
        tc.Record(ok2 && img2.valid && img2.width == 4 && img2.height == 4 &&
                  HashBgraBytes(img1.bgraPixels) != HashBgraBytes(img2.bgraPixels),
                  L"Stage15.Images.InfinityWardIWI_Mw2V8Dxt1AndDistinctHash");

        iwi8[3] = 99u;
        AssertImgRejected(tc, L"Stage15.Images.InfinityWardIWI_RejectUnsupportedVersion", iwi8, L".iwi");
    }

    // ========================================================================
    // 17. Console & Modern Game GPU Textures (.bntx, .gxt, .gtf, .nut, .tex, .vtex_c)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".bntx") && IsImageExtension(L".gxt") &&
                  IsImageExtension(L".gtf") && IsImageExtension(L".nut") && IsImageExtension(L".vtex_c") &&
                  !IsImageExtension(L".tex") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".bntx") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".tex") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".vtex_c"),
                  L"Stage15.Images.ConsoleGameTex_ExtensionsAndLatexDisambiguation");

        ExtendedImageInfo bntxInfo, gxtInfo, gtfInfo, nutInfo, reTexInfo, vtexMip, vtexLz4;
        bool okBntx = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\switch_tegra_gob.bntx", 0, bntxInfo);
        bool okGxt  = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\psvita_scegxm_morton.gxt", 0, gxtInfo);
        bool okGtf  = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\ps3_cellgcm_dxt1.gtf", 0, gtfInfo);
        bool okNut  = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\namco_smash_ntp3.nut", 0, nutInfo);
        bool okReTx = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\capcom_re_engine.tex", 0, reTexInfo);
        bool okVtex = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\valve_source2.vtex_c", 0, vtexMip);
        bool okVLz4 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\source2_lz4_mip0.vtex_c", 0, vtexLz4);

        bool bntxOk = okBntx && bntxInfo.valid && bntxInfo.width == 16 && bntxInfo.height == 8 &&
                      !bntxInfo.layerNames.empty() && bntxInfo.layerNames[0] == L"zelda_shield" &&
                      bntxInfo.bgraPixels[(2 * 16 + 3) * 4 + 2] == 240 &&
                      bntxInfo.bgraPixels[(2 * 16 + 13) * 4 + 1] == 225 &&
                      bntxInfo.bgraPixels[(6 * 16 + 3) * 4 + 0] == 245 &&
                      bntxInfo.bgraPixels[(6 * 16 + 13) * 4 + 3] == 180;
        bool gxtOk  = okGxt && gxtInfo.valid && gxtInfo.width == 8 && gxtInfo.height == 8 &&
                      gxtInfo.bgraPixels[(1 * 8 + 6) * 4 + 2] == 235 &&
                      gxtInfo.bgraPixels[(1 * 8 + 6) * 4 + 0] == 220 &&
                      gxtInfo.bgraPixels[(6 * 8 + 1) * 4 + 0] == 240 &&
                      gxtInfo.bgraPixels[(6 * 8 + 1) * 4 + 2] == 25;
        bool gtfOk  = okGtf && gtfInfo.valid && gtfInfo.width == 8 && gtfInfo.height == 8 &&
                      gtfInfo.bgraPixels[(1 * 8 + 1) * 4 + 2] > 235 &&
                      gtfInfo.bgraPixels[(1 * 8 + 5) * 4 + 1] > 235 &&
                      gtfInfo.bgraPixels[(5 * 8 + 1) * 4 + 0] > 235;
        tc.Record(bntxOk && gxtOk && gtfOk && HashBgraBytes(bntxInfo.bgraPixels) != HashBgraBytes(gxtInfo.bgraPixels),
                  L"Stage15.Images.ConsoleGameTex_SampleSwitchBntxVitaGxtAndPs3Gtf");

        bool nutOk  = okNut && nutInfo.valid && nutInfo.width == 8 && nutInfo.height == 8 &&
                      nutInfo.bgraPixels[(1 * 8 + 1) * 4 + 1] > 215 &&
                      nutInfo.bgraPixels[(1 * 8 + 1) * 4 + 0] > 205 &&
                      nutInfo.bgraPixels[(1 * 8 + 5) * 4 + 2] > 225;
        bool reOk   = okReTx && reTexInfo.valid && reTexInfo.width == 8 && reTexInfo.height == 8 &&
                      reTexInfo.bgraPixels[(1 * 8 + 1) * 4 + 2] > 225 &&
                      reTexInfo.bgraPixels[(1 * 8 + 5) * 4 + 1] > 205 &&
                      reTexInfo.bgraPixels[(5 * 8 + 5) * 4 + 3] >= 126 && reTexInfo.bgraPixels[(5 * 8 + 5) * 4 + 3] <= 130;
        bool vtexOk = okVtex && vtexMip.valid && vtexMip.width == 8 && vtexMip.height == 8 &&
                      vtexMip.bgraPixels[(1 * 8 + 1) * 4 + 2] > 230 &&
                      vtexMip.bgraPixels[(1 * 8 + 5) * 4 + 1] > 225 &&
                      vtexMip.bgraPixels[(5 * 8 + 1) * 4 + 0] > 230 &&
                      okVLz4 && vtexLz4.valid && vtexLz4.width == 8 && vtexLz4.height == 8 &&
                      vtexLz4.bgraPixels[2] == 230u && vtexLz4.bgraPixels[1] == 120u && vtexLz4.bgraPixels[0] == 40u &&
                      HashBgraBytes(vtexMip.bgraPixels) != HashBgraBytes(vtexLz4.bgraPixels);
        tc.Record(nutOk && reOk && vtexOk && HashBgraBytes(nutInfo.bgraPixels) != HashBgraBytes(reTexInfo.bgraPixels),
                  L"Stage15.Images.ConsoleGameTex_SampleNamcoNutCapcomTexAndSource2VtexCLz4");

        const char* latexStr = "\\documentclass{article}\n\\begin{document}\nHello LaTeX!\n\\end{document}\n";
        std::vector<uint8_t> latexBytes(reinterpret_cast<const uint8_t*>(latexStr),
                                        reinterpret_cast<const uint8_t*>(latexStr) + std::strlen(latexStr));
        std::vector<uint8_t> badBntx  = { 'B','N','T','X',0,0,0,0, 0,0,4,0, 0xFF,0xFE, 0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 };
        std::vector<uint8_t> badVtexC = { 24,0,0,0, 12,0, 2,0, 8,0,0,0, 1,0,0,0, 'D','A','T','A', 4,0,0,0, 8,0,0,0, 0,0,0,0 };
        AssertImgRejected(tc, L"Stage15.Images.ConsoleGameTex_RejectLatexDocumentTex", latexBytes, L".tex");
        AssertImgRejected(tc, L"Stage15.Images.ConsoleGameTex_RejectTruncatedSwitchBntx", badBntx, L".bntx");
        AssertImgRejected(tc, L"Stage15.Images.ConsoleGameTex_RejectCorruptSource2VtexC", badVtexC, L".vtex_c");
    }

    // ========================================================================
    // 18. GPU #1: Original Xbox XPR0 / XPR1 / XPR2 Packed Resource (.xpr)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".xpr") && ImageFormatLoader::IsExtendedImageExtension(L".xpr"),
                  L"Stage15.Images.XboxXpr_ExtensionRegistered");

        ExtendedImageInfo xprSwiz;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\xbox_nv2a_swizzled.xpr", 0, xprSwiz);
        tc.Record(ok1 && xprSwiz.valid && xprSwiz.width == 16 && xprSwiz.height == 16 &&
                  xprSwiz.formatName == L"Original Xbox XPR Texture (.xpr)" &&
                  xprSwiz.bgraPixels[2] == 220u && xprSwiz.bgraPixels[1] == 0u && xprSwiz.bgraPixels[0] == 0u &&
                  xprSwiz.bgraPixels[(4 * 16 + 12) * 4 + 0] == 192u &&
                  xprSwiz.bgraPixels[(4 * 16 + 12) * 4 + 1] == 64u &&
                  xprSwiz.bgraPixels[(12 * 16 + 4) * 4 + 1] == 192u &&
                  xprSwiz.bgraPixels[(15 * 16 + 15) * 4 + 0] == 240u &&
                  xprSwiz.bgraPixels[(15 * 16 + 15) * 4 + 1] == 240u,
                  L"Stage15.Images.XboxXpr_SampleNv2aMortonSwizzleDecoded");

        // Second variant: XPR1 4x4 NV2A DXT1 (gpuFmt=0x0C, dims=2, logW=2, logH=2)
        std::vector<uint8_t> xprDxt1 = { 'X', 'P', 'R', '1' };
        PushU32LE(xprDxt1, 40u); // totalSize = 40
        PushU32LE(xprDxt1, 32u); // hdrSize = 32
        PushU32LE(xprDxt1, 0x00040001u); // D3DTexture type = 4 << 16
        PushU32LE(xprDxt1, 0u);  // dataRelOff = 0
        PushU32LE(xprDxt1, 0u);  // lock = 0
        const uint32_t dwFmtDxt1 = (2u << 24u) | (2u << 20u) | (0x0Cu << 8u) | (2u << 4u) | 1u;
        PushU32LE(xprDxt1, dwFmtDxt1);
        PushU32LE(xprDxt1, 0u);  // dwSize = 0 (pot texture)
        xprDxt1.insert(xprDxt1.end(), { 0xE0u, 0x07u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u }); // pure green BC1
        ExtendedImageInfo xpr2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(xprDxt1.data(), xprDxt1.size(), L".xpr", 0, xpr2);
        tc.Record(ok2 && xpr2.valid && xpr2.width == 4 && xpr2.height == 4 &&
                  xpr2.formatName == L"Original Xbox XPR (DXT1 .xpr)" &&
                  xpr2.bgraPixels[1] == 255u &&
                  HashBgraBytes(xpr2.bgraPixels) != HashBgraBytes(xprSwiz.bgraPixels),
                  L"Stage15.Images.XboxXpr_Xpr1Dxt1AndDistinctHash");

        std::vector<uint8_t> badMagicXpr = xprDxt1;
        badMagicXpr[3] = '9';
        AssertImgRejected(tc, L"Stage15.Images.XboxXpr_RejectInvalidMagic", badMagicXpr, L".xpr");

        std::vector<uint8_t> badFmtXpr = xprDxt1;
        badFmtXpr[25] = 0xFEu; // unsupported gpuFmt
        AssertImgRejected(tc, L"Stage15.Images.XboxXpr_RejectUnsupportedNv2aFormat", badFmtXpr, L".xpr");
        AssertImgRejected(tc, L"Stage15.Images.XboxXpr_RejectTruncatedPayload",
                          std::vector<uint8_t>(xprDxt1.begin(), xprDxt1.begin() + 34u), L".xpr");
    }

    // ========================================================================
    // 19. Engine #2: Godot Engine 3 ("GDST" .stex) & Godot 4 ("GST2" .ctex)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".stex") && IsImageExtension(L".ctex") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".stex") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".ctex"),
                  L"Stage15.Images.GodotStreamTexture_ExtensionsRegistered");

        ExtendedImageInfo stexLossless, ctexLossless;
        bool okStex = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\godot3_stream.stex", 0, stexLossless);
        bool okCtex = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\godot4_compressed.ctex", 0, ctexLossless);
        tc.Record(okStex && okCtex && stexLossless.valid && ctexLossless.valid &&
                  stexLossless.width == 16 && stexLossless.height == 16 &&
                  ctexLossless.width == 16 && ctexLossless.height == 16 &&
                  stexLossless.formatName == L"Godot 3 StreamTexture (.stex)" &&
                  ctexLossless.formatName == L"Godot 4 CompressedTexture2D (.ctex)" &&
                  stexLossless.bgraPixels[0] == 191u && stexLossless.bgraPixels[1] == 140u && stexLossless.bgraPixels[2] == 71u,
                  L"Stage15.Images.GodotStreamTexture_SampleStexAndCtexDecoded");

        // Raw GDST 4x4 FORMAT_RGBA8 (fmt=5) + Raw GST2 v1 4x4 FORMAT_DXT1 (fmt=17)
        std::vector<uint8_t> gdstRaw = { 'G', 'D', 'S', 'T' };
        PushU16LE(gdstRaw, 4u); PushU16LE(gdstRaw, 0u);
        PushU16LE(gdstRaw, 4u); PushU16LE(gdstRaw, 0u);
        PushU32LE(gdstRaw, 0u); PushU32LE(gdstRaw, 5u); // FORMAT_RGBA8 = 5
        for (int i = 0; i < 16; ++i) gdstRaw.insert(gdstRaw.end(), { 240u, 45u, 65u, 255u });

        std::vector<uint8_t> gst2Dxt1(52u, 0u);
        std::memcpy(gst2Dxt1.data(), "GST2", 4);
        gst2Dxt1[4] = 1u;  // ver = 1
        gst2Dxt1[8] = 4u;  gst2Dxt1[12] = 4u; // disp 4x4
        gst2Dxt1[40] = 4u; gst2Dxt1[42] = 4u; // img 4x4
        gst2Dxt1[44] = 1u; gst2Dxt1[48] = 17u; // dfMode=0 (IMAGE), imgFmt=17 (DXT1)
        gst2Dxt1.insert(gst2Dxt1.end(), { 0xE0u, 0xFFu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u }); // Yellow BC1

        ExtendedImageInfo gdstInfo, gst2Info;
        bool okRaw3 = ImageFormatLoader::DecodeImageMemory(gdstRaw.data(), gdstRaw.size(), L".stex", 0, gdstInfo);
        bool okRaw4 = ImageFormatLoader::DecodeImageMemory(gst2Dxt1.data(), gst2Dxt1.size(), L".ctex", 0, gst2Info);
        tc.Record(okRaw3 && okRaw4 && gdstInfo.valid && gst2Info.valid &&
                  gdstInfo.bgraPixels[2] == 240u && gst2Info.bgraPixels[2] == 255u && gst2Info.bgraPixels[1] == 255u &&
                  HashBgraBytes(gdstInfo.bgraPixels) != HashBgraBytes(gst2Info.bgraPixels) &&
                  HashBgraBytes(gdstInfo.bgraPixels) != HashBgraBytes(stexLossless.bgraPixels),
                  L"Stage15.Images.GodotStreamTexture_RawRgba8Dxt1AndDistinctHashes");

        std::vector<uint8_t> badGst2Ver = gst2Dxt1;
        badGst2Ver[4] = 99u;
        AssertImgRejected(tc, L"Stage15.Images.GodotStreamTexture_RejectUnsupportedGst2Version", badGst2Ver, L".ctex");
        AssertImgRejected(tc, L"Stage15.Images.GodotStreamTexture_RejectTruncatedGdstSurface",
                          std::vector<uint8_t>(gdstRaw.begin(), gdstRaw.begin() + 36u), L".stex");
    }

    // ========================================================================
    // 20. Engine #3: id Tech 5 / DOOM 3 BFG Binary Image (.bimage)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".bimage") && ImageFormatLoader::IsExtendedImageExtension(L".bimage"),
                  L"Stage15.Images.IdTechBimage_ExtensionRegistered");

        ExtendedImageInfo bimYcocg;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\doom3_bfg_ycocg.bimage", 0, bimYcocg);
        tc.Record(ok1 && bimYcocg.valid && bimYcocg.width == 16 && bimYcocg.height == 16 &&
                  bimYcocg.formatName == L"id Tech 5 Binary Image (BIM v10 .bimage)" &&
                  bimYcocg.bgraPixels[2] == 204u && bimYcocg.bgraPixels[1] == 121u && bimYcocg.bgraPixels[0] == 34u,
                  L"Stage15.Images.IdTechBimage_SampleYCoCgDxt5Decoded");

        // Second variant: BIM v10 4x4 FMT_RGBA8 (1) + CFM_NORMAL_DXT5 (colorFmt=1) -> tangent-space Z reconstruction
        std::vector<uint8_t> bimNorm(8u, 0u); // 8B timestamp
        bimNorm.insert(bimNorm.end(), { 'B', 'I', 'M', 0x0Au });
        PushU32BE(bimNorm, 0u);  // TT_2D
        PushU32BE(bimNorm, 1u);  // FMT_RGBA8
        PushU32BE(bimNorm, 1u);  // CFM_NORMAL_DXT5 (nx in A, ny in G -> reconstructs +Z in B)
        PushU32BE(bimNorm, 4u);  PushU32BE(bimNorm, 4u);
        PushU32BE(bimNorm, 1u);  // numLevels = 1
        PushU32BE(bimNorm, 0u);  PushU32BE(bimNorm, 0u);
        PushU32BE(bimNorm, 4u);  PushU32BE(bimNorm, 4u);
        PushU32BE(bimNorm, 64u); // dataSize = 64
        for (int i = 0; i < 16; ++i) bimNorm.insert(bimNorm.end(), { 0u, 128u, 0u, 128u });
        ExtendedImageInfo bim2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(bimNorm.data(), bimNorm.size(), L".bimage", 0, bim2);
        tc.Record(ok2 && bim2.valid && bim2.width == 4 && bim2.height == 4 &&
                  bim2.bgraPixels[0] >= 250u && bim2.bgraPixels[2] == 128u &&
                  HashBgraBytes(bim2.bgraPixels) != HashBgraBytes(bimYcocg.bgraPixels),
                  L"Stage15.Images.IdTechBimage_NormalMapZReconstructAndDistinctHash");

        std::vector<uint8_t> badBimVer = bimNorm;
        badBimVer[11] = 2u; // version < 7
        AssertImgRejected(tc, L"Stage15.Images.IdTechBimage_RejectUnsupportedVersion", badBimVer, L".bimage");
        AssertImgRejected(tc, L"Stage15.Images.IdTechBimage_RejectTruncatedMipData",
                          std::vector<uint8_t>(bimNorm.begin(), bimNorm.begin() + 64u), L".bimage");
    }

    // ========================================================================
    // 21. Workstation / X11 #4: X11 Window Dump v7 (.xwd — XWDFileHeader)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".xwd") && ImageFormatLoader::IsExtendedImageExtension(L".xwd"),
                  L"Stage15.Images.XWindowDumpXwd_ExtensionRegistered");

        ExtendedImageInfo xwdTrue;
        bool ok1 = ImageFormatLoader::DecodeImageFile(imagesDir + L"\\x11_window_dump.xwd", 0, xwdTrue);
        tc.Record(ok1 && xwdTrue.valid && xwdTrue.width == 16 && xwdTrue.height == 16 &&
                  xwdTrue.formatName == L"X11 Window Dump v7 (.xwd)" &&
                  xwdTrue.bgraPixels[0] == 180u && xwdTrue.bgraPixels[2] == 15u &&
                  xwdTrue.bgraPixels[(4 * 16 + 12) * 4 + 2] == 207u &&
                  xwdTrue.bgraPixels[(4 * 16 + 12) * 4 + 1] == 79u &&
                  xwdTrue.bgraPixels[(15 * 16 + 15) * 4 + 2] == 255u &&
                  xwdTrue.bgraPixels[(15 * 16 + 15) * 4 + 1] == 255u,
                  L"Stage15.Images.XWindowDumpXwd_SampleTrueColorDecoded");

        // Second variant: 8-bit PseudoColor ZPixmap 4x4 with 2 XWDColor LUT entries
        std::vector<uint8_t> xwdPseudo(104u, 0u);
        auto wrCard32 = [&](size_t off, uint32_t v) {
            xwdPseudo[off + 0] = static_cast<uint8_t>((v >> 24) & 0xFFu);
            xwdPseudo[off + 1] = static_cast<uint8_t>((v >> 16) & 0xFFu);
            xwdPseudo[off + 2] = static_cast<uint8_t>((v >> 8) & 0xFFu);
            xwdPseudo[off + 3] = static_cast<uint8_t>(v & 0xFFu);
        };
        wrCard32(0,  104u); // headerSize = 104 (100B + "x11\0")
        wrCard32(4,  7u);   // fileVersion = 7
        wrCard32(8,  2u);   // ZPixmap
        wrCard32(12, 8u);   // depth = 8
        wrCard32(16, 4u);   // width = 4
        wrCard32(20, 4u);   // height = 4
        wrCard32(28, 1u);   // MSBFirst
        wrCard32(32, 8u);   // bitmapUnit = 8
        wrCard32(36, 1u);   // bitmapBitOrder = MSBFirst
        wrCard32(40, 8u);   // bitmapPad = 8
        wrCard32(44, 8u);   // bitsPerPixel = 8
        wrCard32(48, 4u);   // bytesPerLine = 4
        wrCard32(52, 3u);   // PseudoColor = 3
        wrCard32(76, 2u);   // ncolors = 2
        std::memcpy(xwdPseudo.data() + 100u, "x11", 4);
        // XWDColor[0]: pixel=0, RGB=(0, 65535, 32768); XWDColor[1]: pixel=1, RGB=(65535, 16384, 0)
        PushU32BE(xwdPseudo, 0u); PushU16BE(xwdPseudo, 0u);     PushU16BE(xwdPseudo, 65535u); PushU16BE(xwdPseudo, 32768u); PushU16BE(xwdPseudo, 0x0700u);
        PushU32BE(xwdPseudo, 1u); PushU16BE(xwdPseudo, 65535u); PushU16BE(xwdPseudo, 16384u); PushU16BE(xwdPseudo, 0u);     PushU16BE(xwdPseudo, 0x0700u);
        for (int i = 0; i < 16; ++i) xwdPseudo.push_back(static_cast<uint8_t>((i < 8) ? 1u : 0u));

        ExtendedImageInfo xwd2;
        bool ok2 = ImageFormatLoader::DecodeImageMemory(xwdPseudo.data(), xwdPseudo.size(), L".xwd", 0, xwd2);
        tc.Record(ok2 && xwd2.valid && xwd2.width == 4 && xwd2.height == 4 &&
                  xwd2.bgraPixels[2] == 255u && xwd2.bgraPixels[(3 * 4 + 0) * 4 + 1] == 255u &&
                  HashBgraBytes(xwd2.bgraPixels) != HashBgraBytes(xwdTrue.bgraPixels),
                  L"Stage15.Images.XWindowDumpXwd_PseudoColorLutAndDistinctHash");

        std::vector<uint8_t> badVerXwd = xwdPseudo;
        badVerXwd[7] = 6u; // version != 7
        AssertImgRejected(tc, L"Stage15.Images.XWindowDumpXwd_RejectWrongVersion", badVerXwd, L".xwd");
        AssertImgRejected(tc, L"Stage15.Images.XWindowDumpXwd_RejectTruncatedScanlines",
                          std::vector<uint8_t>(xwdPseudo.begin(), xwdPseudo.begin() + 132u), L".xwd");
    }
}

} // namespace Explorer2::SelfTestSuite
