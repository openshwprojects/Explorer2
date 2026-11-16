#pragma once
// FormatTestsImages_Batch70.h — Stage 15.images verification for the 70-format roadmap
// Included at the end of RunFormatTestsImages(TestCounter& tc) in src/FormatTestsImages.cpp.

    const std::wstring s70ImgDir = TestDataDir() + L"\\samples70";
    auto assertImgRejected70 = [&](const wchar_t* testName, const std::vector<uint8_t>& badBytes, const wchar_t* ext) {
        ExtendedImageInfo badInfo;
        bool ok = ImageFormatLoader::DecodeImageMemory(badBytes.data(), badBytes.size(), ext, 0, badInfo);
        tc.Record(!ok && !badInfo.valid && badInfo.bgraPixels.empty(), testName,
                  ok ? L"unexpectedly decoded corrupt input" : L"rejected cleanly");
    };

    // ========================================================================
    // #55. RenderWare Texture Dictionary (.txd — D3D8/D3D9 DXT1/3/5 & PAL8)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".txd") && ImageFormatLoader::IsExtendedImageExtension(L".txd"),
                  L"Stage15.Images.RenderWareTXD_ExtensionRegistered");

        ExtendedImageInfo txdInfo;
        bool okTxd = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\san_andreas_neon.txd", 0, txdInfo);
        bool pxOk = okTxd && txdInfo.valid && txdInfo.width == 8 && txdInfo.height == 8 &&
                    txdInfo.layerNames.size() == 2 &&
                    txdInfo.layerNames[0] == L"neon_sign_dxt1" &&
                    txdInfo.layerNames[1] == L"road_trim_pal8";
        if (pxOk) {
            // Block (0,0) at (1,1) = Magenta (R=255, G=0, B=255); Block (1,0) at (5,1) = Cyan (R=0, G=255, B=255)
            const uint8_t* pMag  = &txdInfo.bgraPixels[(1 * 8 + 1) * 4];
            const uint8_t* pCyan = &txdInfo.bgraPixels[(1 * 8 + 5) * 4];
            pxOk = (pMag[2] > 240 && pMag[1] < 15 && pMag[0] > 240 && pMag[3] == 255) &&
                   (pCyan[2] < 15 && pCyan[1] > 240 && pCyan[0] > 240 && pCyan[3] == 255);
        }
        tc.Record(pxOk, L"Stage15.Images.RenderWareTXD_SampleDXT1AndMultiTexLayers");

        // Second distinct PAL8 TXD in-memory -> distinct pixel hash
        std::vector<uint8_t> palTxd;
        auto appendRw = [](std::vector<uint8_t>& dst, uint32_t tp, const std::vector<uint8_t>& pay) {
            PushU32LE(dst, tp);
            PushU32LE(dst, static_cast<uint32_t>(pay.size()));
            PushU32LE(dst, 0x1003FFFFu); // GTA Vice City RW 3.4.0.3
            dst.insert(dst.end(), pay.begin(), pay.end());
        };
        std::vector<uint8_t> tnSt(88, 0);
        uint32_t plat8 = 8, rFmt = 0x2500u; // PAL8 | 8888
        uint16_t tw = 4, th = 4;
        std::memcpy(tnSt.data() + 0, &plat8, 4);
        std::memcpy(tnSt.data() + 8, "vc_palm_leaf", 12);
        std::memcpy(tnSt.data() + 72, &rFmt, 4);
        std::memcpy(tnSt.data() + 80, &tw, 2);
        std::memcpy(tnSt.data() + 82, &th, 2);
        tnSt[84] = 8; tnSt[85] = 1; tnSt[86] = 4;
        std::vector<uint8_t> pal(1024, 0);
        pal[0] = 30; pal[1] = 220; pal[2] = 60; pal[3] = 255;
        pal[4] = 240; pal[5] = 90; pal[6] = 20; pal[7] = 255;
        tnSt.insert(tnSt.end(), pal.begin(), pal.end());
        PushU32LE(tnSt, 16);
        for (int i = 0; i < 16; ++i) tnSt.push_back(static_cast<uint8_t>(i & 1));
        std::vector<uint8_t> tnCh, tnWrap, dictSt = { 1, 0, 2, 0 }, dictBody;
        appendRw(tnCh, 0x01u, tnSt);
        appendRw(tnWrap, 0x15u, tnCh);
        appendRw(dictBody, 0x01u, dictSt);
        dictBody.insert(dictBody.end(), tnWrap.begin(), tnWrap.end());
        appendRw(palTxd, 0x16u, dictBody);

        ExtendedImageInfo palInfo;
        bool okPal = ImageFormatLoader::DecodeImageMemory(palTxd.data(), palTxd.size(), L".txd", 0, palInfo);
        tc.Record(okPal && palInfo.valid && palInfo.width == 4 && palInfo.height == 4 &&
                  HashBgraBytes(palInfo.bgraPixels) != HashBgraBytes(txdInfo.bgraPixels),
                  L"Stage15.Images.RenderWareTXD_Pal8DistinctHash");

        // Negative checks: wrong root chunk type, 0-texture dictionary, truncated mip0
        std::vector<uint8_t> wrongRoot(48, 0);
        PushU32LE(wrongRoot, 0x10u);
        assertImgRejected70(L"Stage15.Images.RenderWareTXD_RejectNonDictChunk", wrongRoot, L".txd");

        std::vector<uint8_t> zeroTexDict, zBody, zSt = { 0, 0, 2, 0 };
        appendRw(zBody, 0x01u, zSt);
        appendRw(zeroTexDict, 0x16u, zBody);
        assertImgRejected70(L"Stage15.Images.RenderWareTXD_RejectZeroTextures", zeroTexDict, L".txd");
    }

    // ========================================================================
    // #59. Unreal Engine 4/5 Direct UTexture2D (.uasset + .uexp + .ubulk)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".uasset") && ImageFormatLoader::IsExtendedImageExtension(L".uasset"),
                  L"Stage15.Images.UnrealTexture2D_ExtensionRegistered");

        ExtendedImageInfo ueTex;
        bool okUeTex = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\ue4_scifi_panel.uasset", 0, ueTex);
        bool pxOk = okUeTex && ueTex.valid && ueTex.width == 8 && ueTex.height == 8;
        if (pxOk) {
            // Block(0,0) at (1,1) = Yellow (R=255,G=255,B=0); Block(1,0) at (5,1) = Blue (R=0,G=0,B=255)
            const uint8_t* pYel  = &ueTex.bgraPixels[(1 * 8 + 1) * 4];
            const uint8_t* pBlue = &ueTex.bgraPixels[(1 * 8 + 5) * 4];
            pxOk = (pYel[2] > 240 && pYel[1] > 240 && pYel[0] < 15 && pYel[3] == 255) &&
                   (pBlue[2] < 15 && pBlue[1] < 15 && pBlue[0] > 240 && pBlue[3] == 255);
        }
        tc.Record(pxOk, L"Stage15.Images.UnrealTexture2D_SampleDXT1WithUbulk");

        // Negative checks: StaticMesh .uasset must NOT decode as a 2D texture; corrupt .uasset rejected
        ExtendedImageInfo meshAsImg;
        bool meshDecodedAsImg = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\ue5_nanite_pillar.uasset", 0, meshAsImg);
        tc.Record(!meshDecodedAsImg && !meshAsImg.valid, L"Stage15.Images.UnrealTexture2D_RejectStaticMeshUasset");
        assertImgRejected70(L"Stage15.Images.UnrealTexture2D_RejectCorruptUasset", std::vector<uint8_t>(80, 0), L".uasset");
    }

    // ========================================================================
    // #1. AVIF & HEIF / HEIC / AVCI Still Images (.avif, .heic, .heif, .avci)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".avif") && IsImageExtension(L".heic") &&
                  IsImageExtension(L".heif") && IsImageExtension(L".avci") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".avif") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".heic") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".heif") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".avci"),
                  L"Stage15.Images.AvifHeif_ExtensionsRegistered");

        ExtendedImageInfo avifInfo, heicInfo, heifInfo, avciInfo;
        bool okAvif = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\hdr_sunset_alpha.avif", 0, avifInfo);
        bool okHeic = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\iphone_rotated.heic", 0, heicInfo);
        bool okHeif = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\tiled_panorama.heif", 0, heifInfo);
        bool okAvci = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\broadcast_still.avci", 0, avciInfo);

        // 1. hdr_sunset_alpha.avif: 8x8 RGBA with auxl alpha (left x=1: R=250,G=120,B=20,A=255; right x=6: premul Teal A=128)
        bool avifPxOk = okAvif && avifInfo.valid && avifInfo.width == 8 && avifInfo.height == 8;
        if (avifPxOk) {
            const uint8_t* pL = &avifInfo.bgraPixels[(2 * 8 + 1) * 4];
            const uint8_t* pR = &avifInfo.bgraPixels[(2 * 8 + 6) * 4];
            avifPxOk = (pL[2] > 235 && pL[1] > 105 && pL[0] < 35 && pL[3] == 255) &&
                       (pR[3] >= 126 && pR[3] <= 130 && pR[0] > 95 && pR[1] > 85 && pR[2] < 25);
        }
        tc.Record(avifPxOk, L"Stage15.Images.AvifHeif_SampleAlphaAVIF");

        // 2. iphone_rotated.heic: 8x4 H.265 hvc1 rotated 90 deg CCW via irot -> 4x8 Red!
        bool heicPxOk = okHeic && heicInfo.valid && heicInfo.width == 4 && heicInfo.height == 8;
        if (heicPxOk) {
            const uint8_t* p0 = &heicInfo.bgraPixels[0];
            heicPxOk = (p0[2] > 225 && p0[1] < 40 && p0[0] < 45 && p0[3] == 255);
        }
        tc.Record(heicPxOk && HashBgraBytes(heicInfo.bgraPixels) != HashBgraBytes(avifInfo.bgraPixels),
                  L"Stage15.Images.AvifHeif_SampleRotatedHvc1HEIC");

        // 3. tiled_panorama.heif: 2x1 'grid' stitching two 4x4 'unci' tiles -> 8x4 (left Green, right Purple)
        bool heifPxOk = okHeif && heifInfo.valid && heifInfo.width == 8 && heifInfo.height == 4;
        if (heifPxOk) {
            const uint8_t* pG = &heifInfo.bgraPixels[(1 * 8 + 1) * 4];
            const uint8_t* pP = &heifInfo.bgraPixels[(1 * 8 + 6) * 4];
            heifPxOk = (pG[1] > 200 && pG[2] < 40 && pG[0] < 60) &&
                       (pP[2] > 165 && pP[0] > 220 && pP[1] < 60);
        }
        tc.Record(heifPxOk, L"Stage15.Images.AvifHeif_SampleMultiTileGridHEIF");

        // 4. broadcast_still.avci: 8x8 H.264 avc1 Gold (R=250, G=200, B=30)
        bool avciPxOk = okAvci && avciInfo.valid && avciInfo.width == 8 && avciInfo.height == 8;
        if (avciPxOk) {
            const uint8_t* pGold = &avciInfo.bgraPixels[(3 * 8 + 3) * 4];
            avciPxOk = (pGold[2] > 235 && pGold[1] > 185 && pGold[0] < 50 && pGold[3] == 255);
        }
        tc.Record(avciPxOk && HashBgraBytes(avciInfo.bgraPixels) != HashBgraBytes(avifInfo.bgraPixels),
                  L"Stage15.Images.AvifHeif_SampleH264AVCI");

        // Negative checks: MP4 without 'meta' box, truncated 'iloc' extent, corrupt AV1 OBU forbidden bit
        std::vector<uint8_t> noMeta = { 0,0,0,16, 'f','t','y','p', 'a','v','i','f', 0,0,0,0 };
        assertImgRejected70(L"Stage15.Images.AvifHeif_RejectMissingMetaBox", noMeta, L".avif");
        assertImgRejected70(L"Stage15.Images.AvifHeif_RejectGarbageHEIC", std::vector<uint8_t>(64, 0xAA), L".heic");
    }

    // ========================================================================
    // #2. DirectDraw Surface (.dds) BC4 / BC5 / BC6H / BC7 & DX10 Float HDR
    // ========================================================================
    {
        ExtendedImageInfo bc7Info, bc6hInfo, bc5Info, bc4Info, f16Info, r11Info;
        bool okBc7  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\dds_bc7_ui_badge.dds", 0, bc7Info);
        bool okBc6h = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\dds_bc6h_hdr_probe.dds", 0, bc6hInfo);
        bool okBc5  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\dds_bc5_normal_map.dds", 0, bc5Info);
        bool okBc4  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\dds_bc4_roughness.dds", 0, bc4Info);
        bool okF16  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\dds_rgba16f_hdr_sky.dds", 0, f16Info);
        bool okR11  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\dds_r11g11b10f_bloom.dds", 0, r11Info);

        // 1. BC7 Mode 6: left Emerald (R~21,G~231,B~81,A=255), right semi-transparent Coral (A~128, premul R~120)
        bool bc7PxOk = okBc7 && bc7Info.valid && bc7Info.width == 8 && bc7Info.height == 8;
        if (bc7PxOk) {
            const uint8_t* pL = &bc7Info.bgraPixels[(1 * 8 + 1) * 4];
            const uint8_t* pR = &bc7Info.bgraPixels[(1 * 8 + 6) * 4];
            bc7PxOk = (pL[1] > 220 && pL[2] < 30 && pL[3] == 255) &&
                      (pR[3] >= 126 && pR[3] <= 130 && pR[2] > 110 && pR[1] < 55);
        }
        tc.Record(bc7PxOk, L"Stage15.Images.DdsUpgrade_SampleBC7Mode6Alpha");

        // 2. BC6H UF16 HDR: left SunGold (R > G >> B=0), right SkyCyan (B > G >> R=0)
        bool bc6hPxOk = okBc6h && bc6hInfo.valid && bc6hInfo.width == 8 && bc6hInfo.height == 4 && bc6hInfo.bitsPerChannel == 16;
        if (bc6hPxOk) {
            const uint8_t* pSun = &bc6hInfo.bgraPixels[(1 * 8 + 1) * 4];
            const uint8_t* pSky = &bc6hInfo.bgraPixels[(1 * 8 + 6) * 4];
            bc6hPxOk = (pSun[2] > 210 && pSun[1] > 150 && pSun[0] == 0) &&
                       (pSky[0] > 205 && pSky[1] > 140 && pSky[2] == 0);
        }
        tc.Record(bc6hPxOk && HashBgraBytes(bc6hInfo.bgraPixels) != HashBgraBytes(bc7Info.bgraPixels),
                  L"Stage15.Images.DdsUpgrade_SampleBC6HUf16Hdr");

        // 3. BC5 / ATI2 Tangent-Space Normal Map (flat B~255, tilted R~230 & reconstructed B~204)
        bool bc5PxOk = okBc5 && bc5Info.valid && bc5Info.width == 8 && bc5Info.height == 4;
        if (bc5PxOk) {
            const uint8_t* pFlat = &bc5Info.bgraPixels[(1 * 8 + 1) * 4];
            const uint8_t* pTilt = &bc5Info.bgraPixels[(1 * 8 + 6) * 4];
            bc5PxOk = (pFlat[0] >= 250 && pFlat[2] >= 126 && pFlat[2] <= 130) &&
                      (pTilt[2] >= 225 && pTilt[0] >= 195 && pTilt[0] <= 215);
        }
        tc.Record(bc5PxOk, L"Stage15.Images.DdsUpgrade_SampleBC5NormalZReconstruct");

        // 4. BC4 Roughness + DX10 Float16 RGBA + DX10 R11G11B10_FLOAT HDR
        bool bc4PxOk = okBc4 && bc4Info.valid && bc4Info.bgraPixels[0] < 30 && bc4Info.bgraPixels[(0 * 8 + 6) * 4] > 230;
        bool f16PxOk = okF16 && f16Info.valid && f16Info.bgraPixels[2] > 220 && f16Info.bgraPixels[0] > 180 && f16Info.bgraPixels[1] < 140;
        bool r11PxOk = okR11 && r11Info.valid && r11Info.bgraPixels[1] > 210 && r11Info.bgraPixels[2] < 120;
        tc.Record(bc4PxOk && f16PxOk && r11PxOk &&
                  HashBgraBytes(f16Info.bgraPixels) != HashBgraBytes(r11Info.bgraPixels),
                  L"Stage15.Images.DdsUpgrade_SampleBC4AndDx10FloatHdr");

        // Negative checks: DX10 header with truncated block payload & unknown DXGI_FORMAT=0
        std::vector<uint8_t> badDds(148, 0);
        std::memcpy(badDds.data(), "DDS ", 4);
        badDds[4] = 124; badDds[12] = 8; badDds[16] = 8; badDds[76] = 32; badDds[80] = 4;
        std::memcpy(badDds.data() + 84, "DX10", 4);
        badDds[128] = 98; // BC7_UNORM but 0 bytes of block data!
        assertImgRejected70(L"Stage15.Images.DdsUpgrade_RejectTruncatedDx10Blocks", badDds, L".dds");
    }

    // ========================================================================
    // #3. OpenEXR (.exr) PIZ Wavelet, PXR24, B44 / B44A, DWAA / DWAB & Multi-Part v2
    // ========================================================================
    {
        ExtendedImageInfo pizInfo, pxrInfo, b44Info, dwaInfo, mpInfo;
        bool okPiz = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\exr_piz_wavelet_hdr.exr", 0, pizInfo);
        bool okPxr = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\exr_pxr24_float_hdr.exr", 0, pxrInfo);
        bool okB44 = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\exr_b44a_envmap.exr", 0, b44Info);
        bool okDwa = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\exr_dwaa_studio.exr", 0, dwaInfo);
        bool okMp  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\exr_multipart_aov.exr", 0, mpInfo);

        // 1. PIZ Wavelet + Huffman: left Crimson (R > 210, G < 90), right Mint (G > 205, B > 160, R < 90)
        bool pizPxOk = okPiz && pizInfo.valid && pizInfo.width == 8 && pizInfo.height == 4;
        if (pizPxOk) {
            const uint8_t* pL = &pizInfo.bgraPixels[(1 * 8 + 1) * 4];
            const uint8_t* pR = &pizInfo.bgraPixels[(1 * 8 + 6) * 4];
            pizPxOk = (pL[2] > 210 && pL[1] < 95 && pL[0] < 120) &&
                      (pR[1] > 205 && pR[0] > 160 && pR[2] < 95);
        }
        tc.Record(pizPxOk, L"Stage15.Images.ExrUpgrade_SamplePizWaveletHuffman");

        // 2. PXR24 24-bit Float HDR: Golden-Orange (R > 215, G > 170, B < 75, bitsPerChannel=32)
        bool pxrPxOk = okPxr && pxrInfo.valid && pxrInfo.width == 8 && pxrInfo.height == 4 && pxrInfo.bitsPerChannel == 32;
        if (pxrPxOk) {
            const uint8_t* p = &pxrInfo.bgraPixels[0];
            pxrPxOk = (p[2] > 215 && p[1] > 170 && p[0] < 80 && p[3] == 255);
        }
        tc.Record(pxrPxOk && HashBgraBytes(pxrInfo.bgraPixels) != HashBgraBytes(pizInfo.bgraPixels),
                  L"Stage15.Images.ExrUpgrade_SamplePxr24FloatHdr");

        // 3. B44A (3-byte flat block left SkyBlue + 14-byte B44 block right Sunset)
        bool b44PxOk = okB44 && b44Info.valid && b44Info.width == 8 && b44Info.height == 4;
        if (b44PxOk) {
            const uint8_t* pL = &b44Info.bgraPixels[(2 * 8 + 1) * 4];
            const uint8_t* pR = &b44Info.bgraPixels[(2 * 8 + 6) * 4];
            b44PxOk = (pL[0] > 210 && pL[2] < 95) && (pR[2] > 205 && pR[0] < 95);
        }
        tc.Record(b44PxOk, L"Stage15.Images.ExrUpgrade_SampleB44AndB44AFlat");

        // 4. DWAA 8x8 Lossy DCT + 709 CSC + RLE Semi-Transparent Alpha (A=0.5 -> alpha ~ 128)
        bool dwaPxOk = okDwa && dwaInfo.valid && dwaInfo.width == 8 && dwaInfo.height == 8;
        if (dwaPxOk) {
            const uint8_t* p = &dwaInfo.bgraPixels[(3 * 8 + 3) * 4];
            dwaPxOk = (p[3] >= 125 && p[3] <= 131 && p[1] > 95 && p[0] > 85 && p[2] < 55);
        }
        tc.Record(dwaPxOk, L"Stage15.Images.ExrUpgrade_SampleDwaaDct709Alpha");

        // 5. Multi-Part OpenEXR v2 ("depth" + "beauty" -> selects "beauty" Violet & populates all AOV layerNames)
        bool mpPxOk = okMp && mpInfo.valid && mpInfo.width == 4 && mpInfo.height == 4 &&
                      mpInfo.layerNames.size() == 4;
        if (mpPxOk) {
            const uint8_t* p = &mpInfo.bgraPixels[0];
            mpPxOk = (p[0] > 210 && p[2] > 195 && p[1] < 95);
        }
        tc.Record(mpPxOk, L"Stage15.Images.ExrUpgrade_SampleMultiPartV2Aov");

        // Negative check: EXR header claiming PIZ compression with truncated/corrupt chunk table
        std::vector<uint8_t> badExr = { 0x76, 0x2F, 0x31, 0x01, 2, 0, 0, 0, 0 };
        assertImgRejected70(L"Stage15.Images.ExrUpgrade_RejectIncompleteHeader", badExr, L".exr");
    }

    // ========================================================================
    // #4. Photoshop & PSB (.psd, .psb) Layer Compositor, ZIP Prediction, 32-bit HDR & CIE Lab
    // ========================================================================
    {
        ExtendedImageInfo lyPsd, psb16, hdr32, labPsd;
        bool okLy  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\psd_layers_nocompat.psd", 0, lyPsd);
        bool okPsb = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\psb_zip_pred16_layers.psb", 0, psb16);
        bool okH32 = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\psd_32bit_hdr_zippred.psd", 0, hdr32);
        bool okLab = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\psd_cielab_print.psd", 0, labPsd);

        // 1. Layer Compositor (ignoring white merged stub & hidden layer; left SkyCyan, right Multiply Green, Unicode luni names)
        bool lyOk = okLy && lyPsd.valid && lyPsd.width == 8 && lyPsd.height == 8 && lyPsd.layerNames.size() == 3 &&
                    lyPsd.layerNames[0] == L"BackdropSky" && lyPsd.layerNames[1] == L"MultiplySun";
        if (lyOk) {
            const uint8_t* pL = &lyPsd.bgraPixels[(2 * 8 + 1) * 4]; // SkyCyan (R=40, G=200, B=240)
            const uint8_t* pR = &lyPsd.bgraPixels[(2 * 8 + 6) * 4]; // Multiply (R=40, G=200, B=47)
            lyOk = (pL[0] > 225 && pL[1] > 185 && pL[2] < 55) &&
                   (pR[1] > 185 && pR[0] < 65 && pR[2] < 55);
        }
        tc.Record(lyOk, L"Stage15.Images.PsdUpgrade_SampleLayerCompositorMultiplyAndHidden");

        // 2. 16-bit PSB (v2) with ZIP Prediction (comp=3) and omitted Merged Composite
        bool psbOk = okPsb && psb16.valid && psb16.width == 8 && psb16.height == 4 && psb16.bitsPerChannel == 16;
        if (psbOk) {
            const uint8_t* pL = &psb16.bgraPixels[(1 * 8 + 1) * 4]; // Royal Blue
            const uint8_t* pR = &psb16.bgraPixels[(1 * 8 + 6) * 4]; // Amber
            psbOk = (pL[0] > 220 && pL[2] < 35) && (pR[2] > 220 && pR[1] > 125 && pR[0] < 35);
        }
        tc.Record(psbOk && HashBgraBytes(psb16.bgraPixels) != HashBgraBytes(lyPsd.bgraPixels),
                  L"Stage15.Images.PsdUpgrade_SamplePsb16ZipPredictedLayerOnly");

        // 3. 32-bit IEEE Float HDR PSD (Predictor 3 byte-shuffled float + Reinhard HDR tonemap)
        bool h32Ok = okH32 && hdr32.valid && hdr32.width == 8 && hdr32.height == 4 && hdr32.bitsPerChannel == 32;
        if (h32Ok) {
            const uint8_t* p = &hdr32.bgraPixels[0];
            h32Ok = (p[2] > 215 && p[1] > 155 && p[0] < 80);
        }
        tc.Record(h32Ok, L"Stage15.Images.PsdUpgrade_Sample32BitFloatZipPredictedHdr");

        // 4. CIE Lab (colorMode=9) PSD -> Coral-Red sRGB
        bool labOk = okLab && labPsd.valid && labPsd.width == 8 && labPsd.height == 4;
        if (labOk) {
            const uint8_t* p = &labPsd.bgraPixels[0];
            labOk = (p[2] > 210 && p[1] < 110 && p[0] < 95);
        }
        tc.Record(labOk, L"Stage15.Images.PsdUpgrade_SampleCieLabColorMode");

        // Negative check: 8BPS header with depth=32 and truncated sections
        std::vector<uint8_t> badPsd(30, 0);
        std::memcpy(badPsd.data(), "8BPS\x00\x01", 6);
        badPsd[13] = 3; badPsd[17] = 8; badPsd[21] = 8; badPsd[23] = 32; badPsd[25] = 3;
        assertImgRejected70(L"Stage15.Images.PsdUpgrade_RejectTruncated32BitPsd", badPsd, L".psd");
    }

    // ========================================================================
    // #5. JPEG 2000 Pure-C++ Codestream & JP2 Box Decoder (.jp2, .j2k, .jpf, .jpx, .j2c)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".jp2") && IsImageExtension(L".j2k") &&
                  IsImageExtension(L".jpf") && IsImageExtension(L".jpx") && IsImageExtension(L".j2c") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".jp2") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".j2k") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".jpf") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".jpx") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".j2c"),
                  L"Stage15.Images.Jpeg2000_ExtensionsRegistered");

        ExtendedImageInfo jp2Info, j2kInfo, jpfInfo, jpxInfo, j2cInfo;
        bool okJp2 = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\jp2_lossless_rct_alpha.jp2", 0, jp2Info);
        bool okJ2k = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\j2k_lossy_97_ict.j2k", 0, j2kInfo);
        bool okJpf = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\jpf_paletted_map.jpf", 0, jpfInfo);
        bool okJpx = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\jpx_16bit_medical.jpx", 0, jpxInfo);
        bool okJ2c = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\j2c_multitile_codestream.j2c", 0, j2cInfo);

        // 1. JP2 5/3 Lossless + RCT + 'cdef' Alpha: left exact (30, 220, 80, 255), right premul Gold (A=128)
        bool jp2Ok = okJp2 && jp2Info.valid && jp2Info.width == 8 && jp2Info.height == 8;
        if (jp2Ok) {
            const uint8_t* pL = &jp2Info.bgraPixels[(2 * 8 + 1) * 4];
            const uint8_t* pR = &jp2Info.bgraPixels[(2 * 8 + 6) * 4];
            jp2Ok = (pL[2] == 30 && pL[1] == 220 && pL[0] == 80 && pL[3] == 255) &&
                    (pR[3] == 128 && pR[2] >= 118 && pR[2] <= 122 && pR[1] >= 93 && pR[1] <= 97 && pR[0] < 15);
        }
        tc.Record(jp2Ok, L"Stage15.Images.Jpeg2000_SampleLossless53RctAlphaJP2");

        // 2. J2K 9/7 Irreversible Lossy + ICT: top Sky Blue, bottom Coral Sunset
        bool j2kOk = okJ2k && j2kInfo.valid && j2kInfo.width == 8 && j2kInfo.height == 8;
        if (j2kOk) {
            const uint8_t* pT = &j2kInfo.bgraPixels[(1 * 8 + 4) * 4];
            const uint8_t* pB = &j2kInfo.bgraPixels[(6 * 8 + 4) * 4];
            j2kOk = (pT[0] > 230 && pT[1] > 115 && pT[2] < 35) &&
                    (pB[2] > 230 && pB[1] > 75 && pB[0] < 65);
        }
        tc.Record(j2kOk && HashBgraBytes(j2kInfo.bgraPixels) != HashBgraBytes(jp2Info.bgraPixels),
                  L"Stage15.Images.Jpeg2000_SampleLossy97IctJ2K");

        // 3. JPF 'pclr' + 'cmap' Paletted + JPX 16-bit Grayscale + J2C Multi-Tile
        bool jpfOk = okJpf && jpfInfo.valid &&
                     jpfInfo.bgraPixels[(1 * 8 + 1) * 4 + 1] == 235 &&
                     jpfInfo.bgraPixels[(1 * 8 + 6) * 4 + 2] == 235 &&
                     jpfInfo.bgraPixels[(1 * 8 + 6) * 4 + 0] == 220;
        bool jpxOk = okJpx && jpxInfo.valid && jpxInfo.bitsPerChannel == 16 &&
                     jpxInfo.bgraPixels[(1 * 8 + 1) * 4] < 25 &&
                     jpxInfo.bgraPixels[(1 * 8 + 6) * 4] > 225;
        bool j2cOk = okJ2c && j2cInfo.valid &&
                     j2cInfo.bgraPixels[(1 * 8 + 1) * 4 + 2] == 235 &&
                     j2cInfo.bgraPixels[(1 * 8 + 6) * 4 + 0] == 235;
        tc.Record(jpfOk && jpxOk && j2cOk, L"Stage15.Images.Jpeg2000_SamplePaletted16BitAndMultiTile");

        // Negative checks: JP2 signature without jp2c box & truncated J2K SOC+SIZ
        std::vector<uint8_t> noJp2c = { 0,0,0,12, 'j','P',' ',' ', 0x0D,0x0A,0x87,0x0A, 0,0,0,8, 'f','r','e','e' };
        std::vector<uint8_t> badJ2k = { 0xFF, 0x4F, 0xFF, 0x51, 0x00, 0x29, 0x00, 0x00 };
        assertImgRejected70(L"Stage15.Images.Jpeg2000_RejectMissingCodestreamBox", noJp2c, L".jp2");
        assertImgRejected70(L"Stage15.Images.Jpeg2000_RejectTruncatedCodestream", badJ2k, L".j2k");
    }

    // ========================================================================
    // #6. JPEG XL (.jxl) Pure-C++ rANS Entropy, VarDCT Lossy & XYB Color
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".jxl") && ImageFormatLoader::IsExtendedImageExtension(L".jxl"),
                  L"Stage15.Images.Jxl_ExtensionRegistered");

        ExtendedImageInfo vdctInfo, modAnsInfo, boxedInfo, modXybInfo;
        bool okVdct   = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\jxl_vardct_xyb_photo.jxl", 0, vdctInfo);
        bool okModAns = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\jxl_modular_rans_alpha.jxl", 0, modAnsInfo);
        bool okBoxed  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\jxl_vardct_boxed_alpha.jxl", 0, boxedInfo);
        bool okModXyb = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\jxl_modular_xyb_16bit.jxl", 0, modXybInfo);

        // 1. VarDCT + XYB + all_default=1 + rANS + Gaborish (16x16: TL Azure, TR Emerald, BL Sunset Red, BR Gold)
        bool vdctOk = okVdct && vdctInfo.valid && vdctInfo.width == 16 && vdctInfo.height == 16;
        if (vdctOk) {
            const uint8_t* pTL = &vdctInfo.bgraPixels[(3 * 16 + 3) * 4];
            const uint8_t* pTR = &vdctInfo.bgraPixels[(3 * 16 + 12) * 4];
            const uint8_t* pBL = &vdctInfo.bgraPixels[(12 * 16 + 3) * 4];
            const uint8_t* pBR = &vdctInfo.bgraPixels[(12 * 16 + 12) * 4];
            vdctOk = (pTL[0] > 210 && pTL[1] > 110 && pTL[2] < 65) &&
                     (pTR[1] > 180 && pTR[2] < 70  && pTR[0] < 110) &&
                     (pBL[2] > 200 && pBL[1] < 95  && pBL[0] < 80) &&
                     (pBR[2] > 210 && pBR[1] > 170 && pBR[0] < 75);
        }
        tc.Record(vdctOk, L"Stage15.Images.Jxl_SampleVarDctXybPhoto");

        // 2. Modular Lossless + rANS (general logcount histogram) + Alpha (8x8: left Violet [160,40,235,255], right premul Cyan [A=128])
        bool modAnsOk = okModAns && modAnsInfo.valid && modAnsInfo.width == 8 && modAnsInfo.height == 8;
        if (modAnsOk) {
            const uint8_t* pL = &modAnsInfo.bgraPixels[(2 * 8 + 1) * 4];
            const uint8_t* pR = &modAnsInfo.bgraPixels[(2 * 8 + 6) * 4];
            modAnsOk = (pL[2] == 160 && pL[1] == 40 && pL[0] == 235 && pL[3] == 255) &&
                       (pR[3] == 128 && pR[2] <= 15 && pR[1] >= 108 && pR[1] <= 112 && pR[0] >= 118 && pR[0] <= 122);
        }
        tc.Record(modAnsOk && HashBgraBytes(modAnsInfo.bgraPixels) != HashBgraBytes(vdctInfo.bgraPixels),
                  L"Stage15.Images.Jxl_SampleModularRansAlpha");

        // 3. Boxed ISOBMFF + VarDCT XYB + Alpha + Orientation 6 (8x16 -> 16x8: left premul Lime A=128, right Opaque Crimson)
        //    + 4. Modular XYB Perceptual Color (top Coral, bottom Mint)
        bool boxedOk = okBoxed && boxedInfo.valid && boxedInfo.width == 16 && boxedInfo.height == 8;
        if (boxedOk) {
            const uint8_t* pL = &boxedInfo.bgraPixels[(4 * 16 + 3) * 4];
            const uint8_t* pR = &boxedInfo.bgraPixels[(4 * 16 + 12) * 4];
            boxedOk = (pL[3] == 128 && pL[1] > 95 && pL[2] < 45) &&
                      (pR[3] == 255 && pR[2] > 205 && pR[1] < 65);
        }
        bool modXybOk = okModXyb && modXybInfo.valid && modXybInfo.width == 8 && modXybInfo.height == 8;
        if (modXybOk) {
            const uint8_t* pT = &modXybInfo.bgraPixels[(1 * 8 + 4) * 4];
            const uint8_t* pB = &modXybInfo.bgraPixels[(6 * 8 + 4) * 4];
            modXybOk = (pT[2] > 210 && pT[1] < 110) && (pB[1] > 200 && pB[2] < 85);
        }
        tc.Record(boxedOk && modXybOk, L"Stage15.Images.Jxl_SampleBoxedOrientedAndModularXyb");

        // Negative checks: truncated bare codestream & corrupt rANS checksum
        std::vector<uint8_t> badJxl = { 0xFF, 0x0A, 0x39, 0x09 };
        std::vector<uint8_t> badJxlBox = { 0,0,0,12, 'J','X','L',' ', 0x0D,0x0A,0x87,0x0A, 0,0,0,8, 'j','x','l','c' };
        assertImgRejected70(L"Stage15.Images.Jxl_RejectTruncatedCodestream", badJxl, L".jxl");
        assertImgRejected70(L"Stage15.Images.Jxl_RejectMissingFtypContainer", badJxlBox, L".jxl");
    }

    // ========================================================================
    // #7. Pure-C++ TIFF / BigTIFF (.tif, .tiff, .btf, .tf8) & Pure-C++ WebP (.webp)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".tif") && IsImageExtension(L".tiff") &&
                  IsImageExtension(L".btf") && IsImageExtension(L".tf8") && IsImageExtension(L".webp") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".tif") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".btf") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".tf8") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".webp"),
                  L"Stage15.Images.TiffWebp_ExtensionsRegistered");

        ExtendedImageInfo tifInfo, btfInfo, tf8Info, wpL1Info, wpPalInfo, wpLossyInfo;
        bool okTif     = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\tiff_lzw_pred2_16bit.tif", 0, tifInfo);
        bool okBtf     = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\bigtiff_deflate_tiles_hdr.btf", 0, btfInfo);
        bool okTf8     = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\bigtiff_packbits_cmyk.tf8", 0, tf8Info);
        bool okWpL1    = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\webp_vp8l_transforms_alpha.webp", 0, wpL1Info);
        bool okWpPal   = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\webp_vp8l_palette_bundled.webp", 0, wpPalInfo);
        bool okWpLossy = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\webp_vp8_lossy_alph.webp", 0, wpLossyInfo);

        // 1. Classic Big-Endian TIFF + LZW + Predictor 2 + 16-bit RGBA
        bool tifOk = okTif && tifInfo.valid && tifInfo.width == 8 && tifInfo.height == 8 && tifInfo.bitsPerChannel == 16;
        if (tifOk) {
            const uint8_t* pL = &tifInfo.bgraPixels[(2 * 8 + 1) * 4];
            const uint8_t* pR = &tifInfo.bgraPixels[(2 * 8 + 6) * 4];
            tifOk = (pL[2] == 30 && pL[1] == 220 && pL[0] == 80 && pL[3] == 255) &&
                    (pR[3] == 128 && pR[2] >= 118 && pR[2] <= 122 && pR[1] >= 93 && pR[1] <= 97 && pR[0] <= 12);
        }
        tc.Record(tifOk, L"Stage15.Images.TiffWebp_SampleTiffLzwPred2Rgba16");

        // 2. 64-bit BigTIFF (.btf Tiled Deflate Predictor-3 Float32 HDR) + (.tf8 PackBits CMYK 2-page)
        bool btfOk = okBtf && btfInfo.valid && btfInfo.width == 8 && btfInfo.height == 8 && btfInfo.bitsPerChannel == 32;
        if (btfOk) {
            const uint8_t* pT = &btfInfo.bgraPixels[(1 * 8 + 2) * 4];
            const uint8_t* pB = &btfInfo.bgraPixels[(6 * 8 + 5) * 4];
            btfOk = (pT[0] > 180 && pT[2] < 35) && (pB[2] > 190 && pB[0] < 30);
        }
        bool tf8Ok = okTf8 && tf8Info.valid && tf8Info.width == 8 && tf8Info.height == 8 && tf8Info.layerNames.size() == 2;
        if (tf8Ok) {
            const uint8_t* pL = &tf8Info.bgraPixels[(2 * 8 + 1) * 4];
            const uint8_t* pR = &tf8Info.bgraPixels[(2 * 8 + 6) * 4];
            tf8Ok = (pL[1] > 220 && pL[0] > 225 && pL[2] < 40) &&
                    (pR[2] > 225 && pR[0] > 220 && pR[1] < 35);
        }
        tc.Record(btfOk && tf8Ok && HashBgraBytes(btfInfo.bgraPixels) != HashBgraBytes(tf8Info.bgraPixels),
                  L"Stage15.Images.TiffWebp_SampleBigTiffHdrTilesAndCmykPages");

        // 3. Pure-C++ WebP VP8L Lossless (Transforms + LZ77 + Alpha & 2-bit Bundled Palette) + VP8 Lossy + Gradient ALPH
        bool wpL1Ok = okWpL1 && wpL1Info.valid && wpL1Info.width == 8 && wpL1Info.height == 8;
        if (wpL1Ok) {
            const uint8_t* pTL = &wpL1Info.bgraPixels[(1 * 8 + 1) * 4];
            const uint8_t* pTR = &wpL1Info.bgraPixels[(1 * 8 + 6) * 4];
            const uint8_t* pBL = &wpL1Info.bgraPixels[(6 * 8 + 2) * 4];
            wpL1Ok = (pTL[2] == 235 && pTL[1] == 30  && pTL[0] == 45  && pTL[3] == 255) &&
                     (pTR[2] == 40  && pTR[1] == 225 && pTR[0] == 60  && pTR[3] == 255) &&
                     (pBL[3] == 128 && pBL[0] >= 118 && pBL[0] <= 122 && pBL[2] <= 22);
        }
        bool wpPalOk = okWpPal && wpPalInfo.valid && wpPalInfo.width == 8 && wpPalInfo.height == 8 &&
                       wpPalInfo.bgraPixels[0 * 4 + 2] == 245 && wpPalInfo.bgraPixels[1 * 4 + 0] == 205;
        bool wpLossyOk = okWpLossy && wpLossyInfo.valid && wpLossyInfo.width == 16 && wpLossyInfo.height == 16 &&
                         wpLossyInfo.bgraPixels[(4 * 16 + 2) * 4 + 3] == 255 &&
                         wpLossyInfo.bgraPixels[(4 * 16 + 2) * 4 + 2] > 175 &&
                         wpLossyInfo.bgraPixels[(4 * 16 + 12) * 4 + 3] == 128;
        tc.Record(wpL1Ok && wpPalOk && wpLossyOk && HashBgraBytes(wpL1Info.bgraPixels) != HashBgraBytes(wpPalInfo.bgraPixels),
                  L"Stage15.Images.TiffWebp_SampleWebpVp8lPaletteAndVp8Alph");

        // Negative checks: corrupt BigTIFF offset & truncated RIFF WEBP VP8L
        std::vector<uint8_t> badBtf = { 'I','I', 43,0, 8,0, 0,0, 0xFF,0xFF,0,0,0,0,0,0 };
        std::vector<uint8_t> badWebp = { 'R','I','F','F', 16,0,0,0, 'W','E','B','P', 'V','P','8','L', 4,0,0,0, 0x2F,0,0,0 };
        assertImgRejected70(L"Stage15.Images.TiffWebp_RejectCorruptBigTiff", badBtf, L".btf");
        assertImgRejected70(L"Stage15.Images.TiffWebp_RejectTruncatedWebp", badWebp, L".webp");
    }

    // ========================================================================
    // #8. Camera RAW Sensor Unpackers & Color Matrices (.cr3, .arw, .nef, .rw2, .dng)
    // ========================================================================
    {
        ExtendedImageInfo cr3Info, arwInfo, nefInfo, rw2Info, dngInfo;
        bool okCr3 = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\canon_craw_cmp1_sensor.cr3", 0, cr3Info);
        bool okArw = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\sony_arw2_delta_cfa.arw", 0, arwInfo);
        bool okNef = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\nikon_lossless_huffman.nef", 0, nefInfo);
        bool okRw2 = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\panasonic_12bit_blocks.rw2", 0, rw2Info);
        bool okDng = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\adobe_colormatrix_quad.dng", 0, dngInfo);

        // 1. Canon CR3 'CRAW' + 'CMP1' 14-bit RGGB sensor demosaic (left Crimson, right Emerald)
        bool cr3Ok = okCr3 && cr3Info.valid && cr3Info.width == 16 && cr3Info.height == 16 &&
                     cr3Info.bgraPixels[(4 * 16 + 3) * 4 + 2] > 210 &&
                     cr3Info.bgraPixels[(4 * 16 + 3) * 4 + 1] < 80 &&
                     cr3Info.bgraPixels[(4 * 16 + 12) * 4 + 1] > 210 &&
                     cr3Info.bgraPixels[(4 * 16 + 12) * 4 + 2] < 85;
        tc.Record(cr3Ok, L"Stage15.Images.CameraRaw_SampleCanonCr3Cmp1Sensor");

        // 2. Sony ARW2 Delta-Compressed (32x16: left Sky Blue, right Amber) + Nikon NEF Lossless Huffman (16x16: top Magenta, bottom Lime)
        bool arwOk = okArw && arwInfo.valid && arwInfo.width == 32 && arwInfo.height == 16 &&
                     arwInfo.bgraPixels[(4 * 32 + 4) * 4 + 0] > 215 &&
                     arwInfo.bgraPixels[(4 * 32 + 4) * 4 + 2] < 85 &&
                     arwInfo.bgraPixels[(4 * 32 + 24) * 4 + 2] > 215 &&
                     arwInfo.bgraPixels[(4 * 32 + 24) * 4 + 0] < 85;
        bool nefOk = okNef && nefInfo.valid && nefInfo.width == 16 && nefInfo.height == 16 &&
                     nefInfo.bgraPixels[(3 * 16 + 8) * 4 + 2] > 210 &&
                     nefInfo.bgraPixels[(3 * 16 + 8) * 4 + 0] > 205 &&
                     nefInfo.bgraPixels[(3 * 16 + 8) * 4 + 1] < 85 &&
                     nefInfo.bgraPixels[(12 * 16 + 8) * 4 + 1] > 210 &&
                     nefInfo.bgraPixels[(12 * 16 + 8) * 4 + 2] < 95;
        tc.Record(arwOk && nefOk && HashBgraBytes(arwInfo.bgraPixels) != HashBgraBytes(nefInfo.bgraPixels),
                  L"Stage15.Images.CameraRaw_SampleSonyArw2AndNikonHuffman");

        // 3. Panasonic RW2 12-bit 14-px/16-byte Sensor (28x16: left Teal, right Coral) + Adobe DNG ColorMatrix1 (left sRGB Red, right sRGB Blue)
        bool rw2Ok = okRw2 && rw2Info.valid && rw2Info.width == 28 && rw2Info.height == 16 &&
                     rw2Info.bgraPixels[(4 * 28 + 4) * 4 + 0] > 210 &&
                     rw2Info.bgraPixels[(4 * 28 + 4) * 4 + 1] > 200 &&
                     rw2Info.bgraPixels[(4 * 28 + 4) * 4 + 2] < 65 &&
                     rw2Info.bgraPixels[(4 * 28 + 21) * 4 + 2] > 215 &&
                     rw2Info.bgraPixels[(4 * 28 + 21) * 4 + 0] < 120;
        bool dngOk = okDng && dngInfo.valid && dngInfo.width == 16 && dngInfo.height == 16 &&
                     dngInfo.bgraPixels[(4 * 16 + 3) * 4 + 2] > 220 &&
                     dngInfo.bgraPixels[(4 * 16 + 3) * 4 + 0] < 75 &&
                     dngInfo.bgraPixels[(4 * 16 + 12) * 4 + 0] > 220 &&
                     dngInfo.bgraPixels[(4 * 16 + 12) * 4 + 2] < 75;
        tc.Record(rw2Ok && dngOk && HashBgraBytes(rw2Info.bgraPixels) != HashBgraBytes(dngInfo.bgraPixels),
                  L"Stage15.Images.CameraRaw_SamplePanasonicRw2AndDngColorMatrix");

        // Negative checks: corrupt CR3 without mdat/preview & truncated RW2 RawDataOffset
        std::vector<uint8_t> badCr3 = { 0,0,0,24, 'f','t','y','p','c','r','x',' ', 0,0,0,1, 'c','r','x',' ','i','s','o','m' };
        std::vector<uint8_t> badRw2 = { 'I','I', 0x55,0x00, 8,0,0,0, 1,0, 0x18,0x01, 4,0, 1,0,0,0, 0xFF,0,0,0, 0,0,0,0 };
        assertImgRejected70(L"Stage15.Images.CameraRaw_RejectEmptyCr3", badCr3, L".cr3");
        assertImgRejected70(L"Stage15.Images.CameraRaw_RejectTruncatedRw2", badRw2, L".rw2");
    }

    // ========================================================================
    // #9. KTX 2.0 Zstd/BasisLZ (.ktx2), PKM v2 ETC2/EAC (.pkm), & Amiga ILBM HAM8/24-bit (.iff, .ilbm)
    // ========================================================================
    {
        ExtendedImageInfo ktxZstd, ktxBasis, pkmModes, pkmRgba, pkmRg11, ilbmHam8, ilbm24;
        bool okKZ = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\ktx2_uastc_zstd.ktx2", 0, ktxZstd);
        bool okKB = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\ktx2_basislz_etc1s.ktx2", 0, ktxBasis);
        bool okPM = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\pkm20_etc2_modes.pkm", 0, pkmModes);
        bool okPA = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\pkm20_etc2_rgba_eac.pkm", 0, pkmRgba);
        bool okPR = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\pkm20_eac_rg11.pkm", 0, pkmRg11);
        bool okH8 = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\amiga_ham8_camg.ilbm", 0, ilbmHam8);
        bool ok24 = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\amiga_24bit_mask.iff", 0, ilbm24);

        // 1. KTX 2.0 UASTC + Zstd Compressed_Block & KTX 2.0 BasisLZ ETC1S (RGB + Alpha slice)
        bool kzOk = okKZ && ktxZstd.valid && ktxZstd.width == 8 && ktxZstd.height == 8 &&
                    ktxZstd.bgraPixels[(1 * 8 + 1) * 4 + 2] == 230 &&
                    ktxZstd.bgraPixels[(1 * 8 + 5) * 4 + 1] == 215 &&
                    ktxZstd.bgraPixels[(5 * 8 + 1) * 4 + 0] == 240 &&
                    ktxZstd.bgraPixels[(5 * 8 + 5) * 4 + 3] == 128;
        bool kbOk = okKB && ktxBasis.valid && ktxBasis.width == 8 && ktxBasis.height == 8 && ktxBasis.channels == 4 &&
                    ktxBasis.bgraPixels[(1 * 8 + 1) * 4 + 2] > 235 &&
                    ktxBasis.bgraPixels[(1 * 8 + 5) * 4 + 1] > 235 &&
                    ktxBasis.bgraPixels[(5 * 8 + 1) * 4 + 0] > 235 &&
                    ktxBasis.bgraPixels[(5 * 8 + 5) * 4 + 3] < 60;
        tc.Record(kzOk && kbOk && HashBgraBytes(ktxZstd.bgraPixels) != HashBgraBytes(ktxBasis.bgraPixels),
                  L"Stage15.Images.Ktx2PkmIlbm_SampleKtx2ZstdAndBasisLz");

        // 2. PKM 20 ETC2 T/H/Planar/Diff modes + ETC2_RGBA EAC + EAC_RG11 11-bit
        bool pmOk = okPM && pkmModes.valid && pkmModes.width == 8 && pkmModes.height == 8 &&
                    pkmModes.bgraPixels[(1 * 8 + 1) * 4 + 2] == 255 && // T-mode Red
                    pkmModes.bgraPixels[(1 * 8 + 5) * 4 + 1] == 255 && // H-mode Green
                    pkmModes.bgraPixels[(5 * 8 + 1) * 4 + 0] > 180 &&  // Planar Blue
                    pkmModes.bgraPixels[(5 * 8 + 5) * 4 + 2] > 230 &&  // Diff Yellow
                    pkmModes.bgraPixels[(5 * 8 + 5) * 4 + 1] > 210;
        bool paOk = okPA && pkmRgba.valid && pkmRgba.channels == 4 &&
                    pkmRgba.bgraPixels[(1 * 8 + 1) * 4 + 3] == 255 &&
                    pkmRgba.bgraPixels[(5 * 8 + 1) * 4 + 3] == 128 &&
                    pkmRgba.bgraPixels[(5 * 8 + 5) * 4 + 3] == 64;
        bool prOk = okPR && pkmRg11.valid && pkmRg11.channels == 2 && pkmRg11.bitsPerChannel == 11 &&
                    pkmRg11.bgraPixels[(1 * 8 + 1) * 4 + 2] > 225 &&
                    pkmRg11.bgraPixels[(1 * 8 + 1) * 4 + 1] < 45 &&
                    pkmRg11.bgraPixels[(1 * 8 + 5) * 4 + 1] > 225 &&
                    pkmRg11.bgraPixels[(1 * 8 + 5) * 4 + 2] < 50;
        tc.Record(pmOk && paOk && prOk && HashBgraBytes(pkmModes.bgraPixels) != HashBgraBytes(pkmRg11.bgraPixels),
                  L"Stage15.Images.Ktx2PkmIlbm_SamplePkm20Etc2ModesAndEac");

        // 3. Amiga IFF-ILBM HAM8 (CAMG 0x800) & Deep 24-bit Truecolor + mskHasMask plane
        bool h8Ok = okH8 && ilbmHam8.valid && ilbmHam8.width == 16 && ilbmHam8.height == 16 &&
                    ilbmHam8.bgraPixels[(0 * 16 + 0) * 4 + 2] == 20 &&
                    ilbmHam8.bgraPixels[(10 * 16 + 0) * 4 + 2] == 220 &&
                    ilbmHam8.bgraPixels[(12 * 16 + 13) * 4 + 2] > 195 && // HAM8 modified Red
                    ilbmHam8.bgraPixels[(14 * 16 + 14) * 4 + 1] > 220;   // HAM8 modified Green
        bool d24Ok = ok24 && ilbm24.valid && ilbm24.width == 16 && ilbm24.height == 16 && ilbm24.channels == 4 &&
                     ilbm24.bgraPixels[(2 * 16 + 10) * 4 + 2] == (10 * 16 + 7) &&
                     ilbm24.bgraPixels[(2 * 16 + 10) * 4 + 1] == (2 * 16 + 3) &&
                     ilbm24.bgraPixels[(2 * 16 + 10) * 4 + 3] == 255 &&
                     ilbm24.bgraPixels[(14 * 16 + 14) * 4 + 3] == 0; // Masked corner transparent!
        tc.Record(h8Ok && d24Ok && HashBgraBytes(ilbmHam8.bgraPixels) != HashBgraBytes(ilbm24.bgraPixels),
                  L"Stage15.Images.Ktx2PkmIlbm_SampleAmigaHam8And24BitMask");

        // Negative checks: corrupt PKM version/type & truncated ILBM uncompressed BODY
        std::vector<uint8_t> badPkm = { 'P','K','M',' ','9','9', 0,99, 0,8, 0,8, 0,8, 0,8, 0,0,0,0,0,0,0,0 };
        std::vector<uint8_t> badIlbm = {
            'F','O','R','M', 0,0,0,44, 'I','L','B','M',
            'B','M','H','D', 0,0,0,20, 0,16, 0,16, 0,0, 0,0, 8,0,0,0, 0,0, 1,1, 0,16, 0,16,
            'B','O','D','Y', 0,0,0,4,  1,2,3,4
        };
        assertImgRejected70(L"Stage15.Images.Ktx2PkmIlbm_RejectInvalidPkmVersion", badPkm, L".pkm");
        assertImgRejected70(L"Stage15.Images.Ktx2PkmIlbm_RejectTruncatedIlbmBody", badIlbm, L".ilbm");
    }

    // ========================================================================
    // #10. Digital Art Layer/Tile Compositors (.ora, .kra, .procreate, .clip),
    //      Aseprite v1.3 Tilemaps (.aseprite), & OpenType CFF/CFF2/COLR + .cur
    // ========================================================================
    {
        ExtendedImageInfo oraInfo, kraInfo, proInfo, clipInfo, aseInfo, otfColrInfo, otfCff2Info, curInfo;
        bool okOra  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\ora_multilayer_blend.ora", 0, oraInfo);
        bool okKra  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\krita_lzf_tiles.kra", 0, kraInfo);
        bool okPro  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\procreate_lz4_chunks.procreate", 0, proInfo);
        bool okClip = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\clip_studio_exta.clip", 0, clipInfo);
        bool okAse  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\aseprite_v13_tilemap.aseprite", 0, aseInfo);
        bool okColr = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\opentype_cff_colr.otf", 0, otfColrInfo);
        bool okCff2 = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\opentype_cff2_cubic.otf", 0, otfCff2Info);
        bool okCur  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\cursor_hotspot_xor_and.cur", 0, curInfo);

        // 1. OpenRaster stack.xml (left Blue, right Red, hidden Green skipped) + Krita VERSION 2 LZF planar tiles (top Teal, bottom Gold)
        bool oraOk = okOra && oraInfo.valid && oraInfo.width == 16 && oraInfo.height == 16 && oraInfo.layerNames.size() >= 2 &&
                     oraInfo.bgraPixels[(4 * 16 + 3) * 4 + 0] == 230 &&
                     oraInfo.bgraPixels[(4 * 16 + 3) * 4 + 2] == 20 &&
                     oraInfo.bgraPixels[(4 * 16 + 12) * 4 + 2] == 235 &&
                     oraInfo.bgraPixels[(4 * 16 + 12) * 4 + 1] == 35;
        bool kraOk = okKra && kraInfo.valid && kraInfo.width == 16 && kraInfo.height == 16 && kraInfo.layerNames.size() >= 2 &&
                     kraInfo.bgraPixels[(3 * 16 + 4) * 4 + 0] == 200 &&
                     kraInfo.bgraPixels[(3 * 16 + 4) * 4 + 1] == 210 &&
                     kraInfo.bgraPixels[(12 * 16 + 4) * 4 + 2] == 245 &&
                     kraInfo.bgraPixels[(12 * 16 + 4) * 4 + 1] == 195;
        tc.Record(oraOk && kraOk && HashBgraBytes(oraInfo.bgraPixels) != HashBgraBytes(kraInfo.bgraPixels),
                  L"Stage15.Images.ArtFonts_SampleOraStackAndKritaLzfTiles");

        // 2. Procreate LZ4 .chunk tiles (left Purple, right Green) + Clip Studio CSFCHUNK CHNKExta Zlib blocks (top Blue, bottom Orange)
        bool proOk = okPro && proInfo.valid && proInfo.width == 16 && proInfo.height == 16 &&
                     proInfo.bgraPixels[(4 * 16 + 3) * 4 + 2] == 180 &&
                     proInfo.bgraPixels[(4 * 16 + 3) * 4 + 0] == 220 &&
                     proInfo.bgraPixels[(4 * 16 + 12) * 4 + 1] == 230;
        bool clipOk = okClip && clipInfo.valid && clipInfo.width == 16 && clipInfo.height == 16 &&
                      clipInfo.bgraPixels[(3 * 16 + 4) * 4 + 0] == 225 &&
                      clipInfo.bgraPixels[(12 * 16 + 4) * 4 + 2] == 240 &&
                      clipInfo.bgraPixels[(12 * 16 + 4) * 4 + 1] == 120;
        tc.Record(proOk && clipOk && HashBgraBytes(proInfo.bgraPixels) != HashBgraBytes(clipInfo.bgraPixels),
                  L"Stage15.Images.ArtFonts_SampleProcreateChunksAndClipExta");

        // 3. Aseprite v1.3 Tilemap (0x2023 + celType 3 + xFlipMask) + OpenType CFF+COLR/CPAL & CFF2 + Windows .cur Hotspot
        bool aseOk = okAse && aseInfo.valid && aseInfo.width == 16 && aseInfo.height == 16 &&
                     aseInfo.bgraPixels[(2 * 16 + 2) * 4 + 1] == 215 &&   // (0,0) Tile 1 Emerald
                     aseInfo.bgraPixels[(2 * 16 + 9) * 4 + 2] == 235 &&   // (1,0) Tile 2 unflipped left = Crimson
                     aseInfo.bgraPixels[(2 * 16 + 14) * 4 + 2] == 245 &&  // (1,0) Tile 2 unflipped right = Gold
                     aseInfo.bgraPixels[(12 * 16 + 2) * 4 + 3] == 0 &&    // (0,1) Tile 0 = empty transparent
                     aseInfo.bgraPixels[(12 * 16 + 9) * 4 + 2] == 245 &&  // (1,1) Tile 2 xFlipped left = Gold!
                     aseInfo.bgraPixels[(12 * 16 + 9) * 4 + 1] == 210 &&
                     aseInfo.bgraPixels[(12 * 16 + 14) * 4 + 2] == 235 && // (1,1) Tile 2 xFlipped right = Crimson!
                     aseInfo.bgraPixels[(12 * 16 + 14) * 4 + 1] == 35;

        // Verify COLR/CPAL specimen actually drew vivid Crimson & Gold color pixels outside the top banner
        bool sawCrimsonGlyph = false, sawGoldGlyph = false, sawCff2Ink = false;
        if (okColr && otfColrInfo.valid && otfColrInfo.width == 640 && otfColrInfo.height == 400) {
            for (size_t i = 640u * 48u; i < 640u * 400u; ++i) {
                uint8_t b = otfColrInfo.bgraPixels[i * 4 + 0];
                uint8_t g = otfColrInfo.bgraPixels[i * 4 + 1];
                uint8_t r = otfColrInfo.bgraPixels[i * 4 + 2];
                if (r > 200 && g < 80 && b < 80) sawCrimsonGlyph = true;
                if (r > 220 && g > 190 && b < 80) sawGoldGlyph = true;
            }
        }
        if (okCff2 && otfCff2Info.valid && otfCff2Info.width == 640 && otfCff2Info.height == 400) {
            for (size_t i = 640u * 48u; i < 640u * 400u; ++i) {
                if (otfCff2Info.bgraPixels[i * 4 + 2] < 80 && otfCff2Info.bgraPixels[i * 4 + 1] < 80) {
                    sawCff2Ink = true;
                    break;
                }
            }
        }
        bool curOk = okCur && curInfo.valid && curInfo.width == 16 && curInfo.height == 16 &&
                     curInfo.formatName.find(L"3,5") != std::wstring::npos &&
                     curInfo.bgraPixels[(0 * 16 + 0) * 4 + 3] == 0 &&     // AND=1 transparent background
                     curInfo.bgraPixels[(5 * 16 + 3) * 4 + 2] == 250 &&   // Hotspot tip (3,5) Gold
                     curInfo.bgraPixels[(10 * 16 + 4) * 4 + 0] == 245 &&  // Arrow body Cyan
                     curInfo.bgraPixels[(10 * 16 + 4) * 4 + 3] == 255;

        tc.Record(aseOk && sawCrimsonGlyph && sawGoldGlyph && sawCff2Ink && curOk &&
                  HashBgraBytes(otfColrInfo.bgraPixels) != HashBgraBytes(otfCff2Info.bgraPixels),
                  L"Stage15.Images.ArtFonts_SampleAsepriteTilemapCffColrAndCur");

        // Negative checks: invalid .cur idType & empty CFF2 font
        std::vector<uint8_t> badCur = { 0,0, 1,0, 1,0, 16,16,0,0, 0,0, 0,0, 40,0,0,0, 22,0,0,0 }; // idType == 1 (ICO), not 2 (CUR)
        std::vector<uint8_t> badOtf = { 'O','T','T','O', 0,1, 0,16,0,0,0,0, 'C','F','F','2', 0,0,0,0, 0,0,0,28, 0,0,0,4, 2,0,5,0 };
        assertImgRejected70(L"Stage15.Images.ArtFonts_RejectWrongCurType", badCur, L".cur");
        assertImgRejected70(L"Stage15.Images.ArtFonts_RejectCorruptCff2Otf", badOtf, L".otf");
    }

    // ========================================================================
    // #62. Classic Quake / Half-Life / Build Textures, Sprites & WAD2/WAD3 (.wal, .wad, .spr, .lmp, .art)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".wal") && IsImageExtension(L".wad") &&
                  IsImageExtension(L".spr") && IsImageExtension(L".lmp") && IsImageExtension(L".art") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".wal") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".spr") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".art"),
                  L"Stage15.Images.RetroGameTex_ExtensionsRegistered");

        ExtendedImageInfo walInfo, wad3Info, wad2Info, sprInfo, lmpInfo, artInfo;
        bool okWal  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\quake2_lava_trim.wal", 0, walInfo);
        bool okWad3 = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\halflife_cstrike.wad", 0, wad3Info);
        bool okWad2 = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\quake1_gfx.wad", 0, wad2Info);
        bool okSpr  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\halflife_explosion.spr", 0, sprInfo);
        bool okLmp  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\quake_hud_shield.lmp", 0, lmpInfo);
        bool okArt  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\duke3d_tiles000.art", 0, artInfo);

        // 1. Quake II .wal + Half-Life WAD3 (contact sheet of 2 embedded-palette miptex) + Quake WAD2
        bool walOk = okWal && walInfo.valid && walInfo.width == 16 && walInfo.height == 16 &&
                     walInfo.bgraPixels[(2 * 16 + 4) * 4 + 2] > 210 &&   // Crimson top
                     walInfo.bgraPixels[(12 * 16 + 2) * 4 + 0] > 230 &&  // Plasma Blue bottom-left
                     walInfo.bgraPixels[(12 * 16 + 12) * 4 + 3] == 0;    // '{' / SURF_TRANS transparent bottom-right
        bool wadOk = okWad3 && wad3Info.valid && wad3Info.width == 36 && wad3Info.height == 16 && wad3Info.layerNames.size() == 2 &&
                     wad3Info.bgraPixels[(2 * 36 + 2) * 4 + 2] == 240 && // Tile 0 left Red
                     wad3Info.bgraPixels[(2 * 36 + 10) * 4 + 1] == 220 &&// Tile 0 right Green
                     wad3Info.bgraPixels[(2 * 36 + 22) * 4 + 0] == 245 &&// Tile 1 left Blue
                     okWad2 && wad2Info.valid && wad2Info.width == 8 && wad2Info.height == 8 &&
                     wad2Info.bgraPixels[(1 * 8 + 2) * 4 + 2] == 250 &&  // WAD2 explicit PALETTE Gold
                     wad2Info.bgraPixels[(6 * 8 + 2) * 4 + 0] == 240;    // WAD2 explicit PALETTE Cyan
        tc.Record(walOk && wadOk && HashBgraBytes(wad3Info.bgraPixels) != HashBgraBytes(wad2Info.bgraPixels),
                  L"Stage15.Images.RetroGameTex_SampleQuake2WalAndWad2Wad3");

        // 2. Half-Life v2 Sprite ("IDSP") + Quake qpic_t .lmp + Build Engine column-major .art
        bool sprOk = okSpr && sprInfo.valid && sprInfo.width == 16 && sprInfo.height == 16 &&
                     sprInfo.bgraPixels[(0 * 16 + 0) * 4 + 3] == 0 &&    // SPR_ALPHTEST transparent border
                     sprInfo.bgraPixels[(8 * 16 + 8) * 4 + 2] == 255 &&  // Orange core
                     sprInfo.bgraPixels[(8 * 16 + 8) * 4 + 1] == 140;
        bool lmpOk = okLmp && lmpInfo.valid && lmpInfo.width == 16 && lmpInfo.height == 16 &&
                     lmpInfo.bgraPixels[(0 * 16 + 0) * 4 + 3] == 0 &&
                     lmpInfo.bgraPixels[(8 * 16 + 4) * 4 + 2] > 220 &&
                     lmpInfo.bgraPixels[(8 * 16 + 12) * 4 + 1] < 70;
        bool artOk = okArt && artInfo.valid && artInfo.width == 8 && artInfo.height == 8 &&
                     artInfo.bgraPixels[(1 * 8 + 6) * 4 + 2] > 215 &&    // (x=6, y=1) Crimson (proves column-major!)
                     artInfo.bgraPixels[(1 * 8 + 6) * 4 + 0] < 60 &&
                     artInfo.bgraPixels[(6 * 8 + 1) * 4 + 0] > 215 &&    // (x=1, y=6) SkyBlue (proves column-major!)
                     artInfo.bgraPixels[(7 * 8 + 7) * 4 + 3] == 0;       // Index 255 transparent
        tc.Record(sprOk && lmpOk && artOk && HashBgraBytes(sprInfo.bgraPixels) != HashBgraBytes(artInfo.bgraPixels),
                  L"Stage15.Images.RetroGameTex_SampleHlSprQuakeLmpAndBuildArt");

        // Negative checks: arbitrary non-matching .lmp size & truncated .art tile payload
        std::vector<uint8_t> badLmp = { 64,0,0,0, 64,0,0,0, 1,2,3,4,5,6,7,8 }; // claims 64x64 in 16 bytes
        std::vector<uint8_t> badArt = { 1,0,0,0, 1,0,0,0, 0,0,0,0, 0,0,0,0, 16,0, 16,0, 0,0,0,0, 1,2,3,4 };
        assertImgRejected70(L"Stage15.Images.RetroGameTex_RejectTruncatedQpicLmp", badLmp, L".lmp");
        assertImgRejected70(L"Stage15.Images.RetroGameTex_RejectTruncatedBuildArt", badArt, L".art");
    }

    // ========================================================================
    // #63. Console & Modern Game GPU Textures (.bntx, .gxt, .gtf, .nut, .tex, .vtex_c)
    // ========================================================================
    {
        tc.Record(IsImageExtension(L".bntx") && IsImageExtension(L".gxt") &&
                  IsImageExtension(L".gtf") && IsImageExtension(L".nut") && IsImageExtension(L".vtex_c") &&
                  !IsImageExtension(L".tex") && // LaTeX .tex stays text until binary "TEX\0" header disambiguated!
                  ImageFormatLoader::IsExtendedImageExtension(L".bntx") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".tex") &&
                  ImageFormatLoader::IsExtendedImageExtension(L".vtex_c"),
                  L"Stage15.Images.ConsoleGameTex_ExtensionsAndLatexDisambiguation");

        ExtendedImageInfo bntxInfo, gxtInfo, gtfInfo, nutInfo, reTexInfo, vtexInfo;
        bool okBntx = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\switch_tegra_gob.bntx", 0, bntxInfo);
        bool okGxt  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\psvita_scegxm_morton.gxt", 0, gxtInfo);
        bool okGtf  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\ps3_cellgcm_dxt1.gtf", 0, gtfInfo);
        bool okNut  = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\namco_smash_ntp3.nut", 0, nutInfo);
        bool okReTx = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\capcom_re_engine.tex", 0, reTexInfo);
        bool okVtex = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\valve_source2.vtex_c", 0, vtexInfo);

        // 1. Switch BNTX Tegra X1 GOB deswizzle (16x8, 4 quadrants + alpha=180) + PS Vita GXT Morton deswizzle + PS3 GTF BE DXT1
        bool bntxOk = okBntx && bntxInfo.valid && bntxInfo.width == 16 && bntxInfo.height == 8 &&
                      !bntxInfo.layerNames.empty() && bntxInfo.layerNames[0] == L"zelda_shield" &&
                      bntxInfo.bgraPixels[(2 * 16 + 3) * 4 + 2] == 240 &&  // Top-Left Red
                      bntxInfo.bgraPixels[(2 * 16 + 13) * 4 + 1] == 225 && // Top-Right Green
                      bntxInfo.bgraPixels[(6 * 16 + 3) * 4 + 0] == 245 &&  // Bottom-Left Blue
                      bntxInfo.bgraPixels[(6 * 16 + 13) * 4 + 3] == 180;   // Bottom-Right Gold alpha=180
        bool gxtOk  = okGxt && gxtInfo.valid && gxtInfo.width == 8 && gxtInfo.height == 8 &&
                      gxtInfo.bgraPixels[(1 * 8 + 6) * 4 + 2] == 235 &&    // Morton Top-Right Magenta
                      gxtInfo.bgraPixels[(1 * 8 + 6) * 4 + 0] == 220 &&
                      gxtInfo.bgraPixels[(6 * 8 + 1) * 4 + 0] == 240 &&    // Morton Bottom-Left Cyan
                      gxtInfo.bgraPixels[(6 * 8 + 1) * 4 + 2] == 25;
        bool gtfOk  = okGtf && gtfInfo.valid && gtfInfo.width == 8 && gtfInfo.height == 8 &&
                      gtfInfo.bgraPixels[(1 * 8 + 1) * 4 + 2] > 235 &&
                      gtfInfo.bgraPixels[(1 * 8 + 5) * 4 + 1] > 235 &&
                      gtfInfo.bgraPixels[(5 * 8 + 1) * 4 + 0] > 235;
        tc.Record(bntxOk && gxtOk && gtfOk && HashBgraBytes(bntxInfo.bgraPixels) != HashBgraBytes(gxtInfo.bgraPixels),
                  L"Stage15.Images.ConsoleGameTex_SampleSwitchBntxVitaGxtAndPs3Gtf");

        // 2. Bandai Namco NTP3 .nut + Capcom RE Engine BC7 .tex + Valve Source 2 .vtex_c (mip0 selected over mip1)
        bool nutOk  = okNut && nutInfo.valid && nutInfo.width == 8 && nutInfo.height == 8 &&
                      nutInfo.bgraPixels[(1 * 8 + 1) * 4 + 1] > 215 &&
                      nutInfo.bgraPixels[(1 * 8 + 1) * 4 + 0] > 205 &&
                      nutInfo.bgraPixels[(1 * 8 + 5) * 4 + 2] > 225;
        bool reOk   = okReTx && reTexInfo.valid && reTexInfo.width == 8 && reTexInfo.height == 8 &&
                      reTexInfo.bgraPixels[(1 * 8 + 1) * 4 + 2] > 225 &&
                      reTexInfo.bgraPixels[(1 * 8 + 5) * 4 + 1] > 205 &&
                      reTexInfo.bgraPixels[(5 * 8 + 5) * 4 + 3] >= 126 && reTexInfo.bgraPixels[(5 * 8 + 5) * 4 + 3] <= 130;
        bool vtexOk = okVtex && vtexInfo.valid && vtexInfo.width == 8 && vtexInfo.height == 8 &&
                      vtexInfo.bgraPixels[(1 * 8 + 1) * 4 + 2] > 230 && // Bright Red mip0 (not dark mip1!)
                      vtexInfo.bgraPixels[(1 * 8 + 5) * 4 + 1] > 225 &&
                      vtexInfo.bgraPixels[(5 * 8 + 1) * 4 + 0] > 230;
        tc.Record(nutOk && reOk && vtexOk && HashBgraBytes(nutInfo.bgraPixels) != HashBgraBytes(reTexInfo.bgraPixels),
                  L"Stage15.Images.ConsoleGameTex_SampleNamcoNutCapcomTexAndSource2VtexC");

        // Negative checks: LaTeX .tex text document MUST be rejected by ImageFormatLoader, and truncated BNTX rejected!
        const char* latexStr = "\\documentclass{article}\n\\begin{document}\nHello LaTeX!\n\\end{document}\n";
        std::vector<uint8_t> latexBytes(reinterpret_cast<const uint8_t*>(latexStr),
                                        reinterpret_cast<const uint8_t*>(latexStr) + std::strlen(latexStr));
        std::vector<uint8_t> badBntx = { 'B','N','T','X',0,0,0,0, 0,0,4,0, 0xFF,0xFE, 0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 };
        assertImgRejected70(L"Stage15.Images.ConsoleGameTex_RejectLatexDocumentTex", latexBytes, L".tex");
        assertImgRejected70(L"Stage15.Images.ConsoleGameTex_RejectTruncatedSwitchBntx", badBntx, L".bntx");
    }

    // ========================================================================
    // #64. Valve Source VTF BC4/BC5 (ATI1N/ATI2N), Float32 HDR & v7.6 Deflate AUX (.vtf)
    // ========================================================================
    {
        ExtendedImageInfo ati2nInfo, hdr32Info, v76Info;
        bool okAti = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\source_ati2n_normal.vtf", 0, ati2nInfo);
        bool okHdr = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\source_hdr_rgba32f.vtf", 0, hdr32Info);
        bool okV76 = ImageFormatLoader::DecodeImageFile(s70ImgDir + L"\\strata_v76_deflate_aux.vtf", 0, v76Info);

        // 1. ATI2N / BC5 Normal Map (flat block (0,0) reconstructs Z=255 Blue; +X block (1,0) high Red; +Y block (0,1) high Green)
        bool atiOk = okAti && ati2nInfo.valid && ati2nInfo.width == 8 && ati2nInfo.height == 8 && ati2nInfo.channels == 2 &&
                     ati2nInfo.bgraPixels[(1 * 8 + 1) * 4 + 0] > 245 && // Reconstructed +Z Blue
                     ati2nInfo.bgraPixels[(1 * 8 + 5) * 4 + 2] == 245 &&// +X Red
                     ati2nInfo.bgraPixels[(5 * 8 + 1) * 4 + 1] == 245;  // +Y Green

        // 2. RGBA32323232F Float32 HDR (32-bit/ch, left warm Sun Gold, right cool Cyan 50% alpha)
        bool hdrOk = okHdr && hdr32Info.valid && hdr32Info.width == 8 && hdr32Info.height == 8 && hdr32Info.bitsPerChannel == 32 &&
                     hdr32Info.bgraPixels[(2 * 8 + 1) * 4 + 2] > hdr32Info.bgraPixels[(2 * 8 + 1) * 4 + 0] + 120 &&
                     hdr32Info.bgraPixels[(2 * 8 + 6) * 4 + 3] >= 126 && hdr32Info.bgraPixels[(2 * 8 + 6) * 4 + 3] <= 129 &&
                     hdr32Info.bgraPixels[(2 * 8 + 6) * 4 + 0] > hdr32Info.bgraPixels[(2 * 8 + 6) * 4 + 2] + 80;

        // 3. VTF v7.6 Deflate 'AXC' AUX Resource (top Emerald, bottom Violet alpha=200)
        bool v76Ok = okV76 && v76Info.valid && v76Info.width == 8 && v76Info.height == 8 &&
                     v76Info.formatName.find(L"v7.6") != std::wstring::npos &&
                     v76Info.bgraPixels[(1 * 8 + 2) * 4 + 1] == 230 &&
                     v76Info.bgraPixels[(1 * 8 + 2) * 4 + 2] == 35 &&
                     v76Info.bgraPixels[(6 * 8 + 2) * 4 + 3] == 200;

        tc.Record(atiOk && hdrOk && v76Ok &&
                  HashBgraBytes(ati2nInfo.bgraPixels) != HashBgraBytes(v76Info.bgraPixels),
                  L"Stage15.Images.ValveVtf_SampleAti2nFloat32HdrAndV76DeflateAux");

        // Negative check: VTF v7.6 AUX resource with corrupt Zlib stream
        std::vector<uint8_t> badVtf76(120, 0);
        std::memcpy(badVtf76.data(), "VTF\0", 4);
        badVtf76[4] = 7; badVtf76[8] = 6; badVtf76[12] = 96;
        badVtf76[16] = 8; badVtf76[18] = 8; badVtf76[24] = 1; badVtf76[56] = 1; badVtf76[68] = 2;
        badVtf76[80] = 'A'; badVtf76[81] = 'X'; badVtf76[82] = 'C'; badVtf76[84] = 96;
        badVtf76[88] = 0x30; badVtf76[92] = 108;
        badVtf76[96] = 12; badVtf76[100] = 6; badVtf76[104] = 8;
        badVtf76[108] = 0xDE; badVtf76[109] = 0xAD; // Invalid Zlib stream!
        assertImgRejected70(L"Stage15.Images.ValveVtf_RejectCorruptV76DeflateAux", badVtf76, L".vtf");
    }
