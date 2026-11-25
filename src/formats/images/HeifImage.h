#pragma once
// HeifImage.h — ISO/IEC 23008-12 HEIF / HEIC / AVCI Still Image Decoder (.heic, .heif, .avci)

namespace Formats {

namespace AvifHeifDetail {

// ============================================================================
// Backend 3: Pure-C++ ITU-T H.264 / AVC ('avc1' + 'avcC') + Media Foundation H.264
// ============================================================================
inline bool DecodeAvc1StillItem(
    const uint8_t* itemData, size_t itemLen,
    const std::vector<uint8_t>& avcC,
    bool fullRange, uint16_t matrixCoeffs,
    DecodedRgbaTile& out)
{
    if (!itemData || itemLen < 5 || avcC.size() < 7 || avcC[0] != 1) return false;
    uint8_t lenSize = static_cast<uint8_t>((avcC[4] & 0x03u) + 1u);

    // Extract SPS & PPS from avcC
    std::vector<std::vector<uint8_t>> spsList, ppsList;
    size_t cp = 5;
    uint8_t nSps = avcC[cp++] & 0x1Fu;
    for (uint8_t i = 0; i < nSps; ++i) {
        if (cp + 2 > avcC.size()) return false;
        uint16_t sl = RdBE16(avcC.data() + cp); cp += 2;
        if (cp + sl > avcC.size()) return false;
        spsList.emplace_back(avcC.data() + cp, avcC.data() + cp + sl);
        cp += sl;
    }
    if (cp < avcC.size()) {
        uint8_t nPps = avcC[cp++];
        for (uint8_t i = 0; i < nPps; ++i) {
            if (cp + 2 > avcC.size()) return false;
            uint16_t pl = RdBE16(avcC.data() + cp); cp += 2;
            if (cp + pl > avcC.size()) return false;
            ppsList.emplace_back(avcC.data() + cp, avcC.data() + cp + pl);
            cp += pl;
        }
    }
    if (spsList.empty() || ppsList.empty()) return false;

    // Parse SPS
    auto spsRbsp = StripRbspEmulationBytes(spsList[0].data(), spsList[0].size(), 1);
    BitReader sbr(spsRbsp.data(), spsRbsp.size());
    if (spsRbsp.size() < 3) return false;
    uint32_t profileIdc = sbr.ReadBits(8);
    sbr.ReadBits(8); // constraints
    sbr.ReadBits(8); // level_idc
    sbr.ReadUE();    // sps_id

    uint32_t chromaFmt = 1; // 4:2:0
    uint32_t bitDepthY = 8, bitDepthC = 8;
    if (profileIdc == 100 || profileIdc == 110 || profileIdc == 122 || profileIdc == 244 || profileIdc == 44 ||
        profileIdc == 83  || profileIdc == 86  || profileIdc == 118 || profileIdc == 128 || profileIdc == 138)
    {
        chromaFmt = sbr.ReadUE();
        if (chromaFmt == 3) sbr.ReadBit();
        bitDepthY = sbr.ReadUE() + 8;
        bitDepthC = sbr.ReadUE() + 8;
        sbr.ReadBit(); // qpprime_y_zero_transform_bypass
        if (sbr.ReadBit()) return false; // custom scaling matrices -> defer to MFT
    }
    uint32_t log2MaxFrameNum = sbr.ReadUE() + 4;
    uint32_t pocType = sbr.ReadUE();
    uint32_t log2MaxPocLsb = 4;
    if (pocType == 0) log2MaxPocLsb = sbr.ReadUE() + 4;
    else if (pocType == 1) {
        sbr.ReadBit(); sbr.ReadSE(); sbr.ReadSE();
        uint32_t nRef = sbr.ReadUE();
        for (uint32_t k = 0; k < nRef && k < 256; ++k) sbr.ReadSE();
    }
    sbr.ReadUE(); // max_num_ref_frames
    sbr.ReadBit(); // gaps_in_frame_num
    uint32_t mbW = sbr.ReadUE() + 1;
    uint32_t mbH = sbr.ReadUE() + 1;
    uint32_t frameMbsOnly = sbr.ReadBit();
    if (!frameMbsOnly) sbr.ReadBit();
    sbr.ReadBit(); // direct_8x8
    uint32_t cropL = 0, cropR = 0, cropT = 0, cropB = 0;
    if (sbr.ReadBit()) {
        cropL = sbr.ReadUE(); cropR = sbr.ReadUE(); cropT = sbr.ReadUE(); cropB = sbr.ReadUE();
    }
    uint32_t subW = (chromaFmt == 1 || chromaFmt == 2) ? 2u : 1u;
    uint32_t subH = (chromaFmt == 1) ? 2u : 1u;
    uint32_t codedW = mbW * 16u;
    uint32_t codedH = mbH * 16u;
    uint32_t cropUnitX = (chromaFmt == 0) ? 1u : subW;
    uint32_t cropUnitY = ((chromaFmt == 0) ? 1u : subH) * (2u - frameMbsOnly);
    if ((cropL + cropR) * cropUnitX >= codedW || (cropT + cropB) * cropUnitY >= codedH) return false;
    uint32_t picW = codedW - (cropL + cropR) * cropUnitX;
    uint32_t picH = codedH - (cropT + cropB) * cropUnitY;
    if (picW == 0 || picH == 0 || picW > 8192 || picH > 8192 || bitDepthY != 8 || bitDepthC != 8) return false;

    // Parse PPS
    auto ppsRbsp = StripRbspEmulationBytes(ppsList[0].data(), ppsList[0].size(), 1);
    BitReader pbr(ppsRbsp.data(), ppsRbsp.size());
    pbr.ReadUE(); // pps_id
    pbr.ReadUE(); // sps_id
    uint32_t entropyCabac = pbr.ReadBit();
    uint32_t bottomFieldOrder = pbr.ReadBit();
    uint32_t numSliceGroupsMinus1 = pbr.ReadUE();
    pbr.ReadUE(); pbr.ReadUE(); // num_ref_idx
    pbr.ReadBit(); pbr.ReadBits(2);
    pbr.ReadSE(); pbr.ReadSE(); pbr.ReadSE();
    uint32_t deblockPresent = pbr.ReadBit();
    pbr.ReadBit();
    uint32_t redundantPicCnt = pbr.ReadBit();

    // Pure-C++ CAVLC I_PCM Slice Decoder
    if (entropyCabac == 0 && numSliceGroupsMinus1 == 0) {
        size_t pos = 0;
        while (pos + lenSize <= itemLen) {
            uint64_t nalLen = RdVarBE(itemData + pos, lenSize);
            pos += lenSize;
            if (nalLen == 0 || nalLen > itemLen - pos) break;
            const uint8_t* nal = itemData + pos;
            pos += static_cast<size_t>(nalLen);

            uint8_t nalType = nal[0] & 0x1Fu;
            uint8_t nalRef  = (nal[0] >> 5) & 0x03u;
            if (nalType != 5 && nalType != 1) continue;

            auto slRbsp = StripRbspEmulationBytes(nal, static_cast<size_t>(nalLen), 1);
            BitReader br(slRbsp.data(), slRbsp.size());
            uint32_t firstMb = br.ReadUE();
            uint32_t sliceType = br.ReadUE();
            br.ReadUE(); // pps_id
            if (firstMb != 0 || (sliceType != 2 && sliceType != 7)) break;

            br.ReadBits(static_cast<int>(log2MaxFrameNum));
            if (!frameMbsOnly) { if (br.ReadBit()) br.ReadBit(); }
            if (nalType == 5) br.ReadUE(); // idr_pic_id
            if (pocType == 0) {
                br.ReadBits(static_cast<int>(log2MaxPocLsb));
                if (bottomFieldOrder) br.ReadSE();
            }
            if (redundantPicCnt) br.ReadUE();
            if (nalRef != 0) {
                if (nalType == 5) { br.ReadBit(); br.ReadBit(); }
                else { if (br.ReadBit()) break; }
            }
            br.ReadSE(); // slice_qp_delta
            if (deblockPresent) {
                uint32_t dis = br.ReadUE();
                if (dis != 1) { br.ReadSE(); br.ReadSE(); }
            }

            std::vector<uint8_t> planeY(codedW * codedH, 0);
            uint32_t cW = codedW / subW, cH = codedH / subH;
            std::vector<uint8_t> planeCb(cW * cH, 128), planeCr(cW * cH, 128);

            bool allOk = true;
            uint32_t mbCw = 16u / subW, mbCh = 16u / subH;
            for (uint32_t my = 0; my < mbH && allOk; ++my) {
                for (uint32_t mx = 0; mx < mbW && allOk; ++mx) {
                    uint32_t mbType = br.ReadUE();
                    if (mbType != 25u) { allOk = false; break; } // Non-I_PCM -> let MFT handle
                    br.AlignToByte();
                    size_t needBytes = 256u + ((chromaFmt == 0) ? 0u : (2u * mbCw * mbCh));
                    if (br.bytePos + needBytes > br.len) { allOk = false; break; }

                    for (uint32_t r = 0; r < 16; ++r) {
                        std::memcpy(&planeY[(my * 16 + r) * codedW + mx * 16], br.buf + br.bytePos, 16);
                        br.bytePos += 16;
                    }
                    if (chromaFmt != 0) {
                        for (uint32_t r = 0; r < mbCh; ++r) {
                            std::memcpy(&planeCb[(my * mbCh + r) * cW + mx * mbCw], br.buf + br.bytePos, mbCw);
                            br.bytePos += mbCw;
                        }
                        for (uint32_t r = 0; r < mbCh; ++r) {
                            std::memcpy(&planeCr[(my * mbCh + r) * cW + mx * mbCw], br.buf + br.bytePos, mbCw);
                            br.bytePos += mbCw;
                        }
                    }
                }
            }

            if (allOk) {
                out.w = picW; out.h = picH;
                out.rgba.resize(static_cast<size_t>(picW) * picH * 4);
                uint32_t xOff = cropL * cropUnitX, yOff = cropT * cropUnitY;
                for (uint32_t y = 0; y < picH; ++y) {
                    for (uint32_t x = 0; x < picW; ++x) {
                        uint32_t sy = y + yOff, sx = x + xOff;
                        uint8_t yv = planeY[sy * codedW + sx];
                        uint8_t cb = (chromaFmt == 0) ? 128 : planeCb[(sy / subH) * cW + (sx / subW)];
                        uint8_t cr = (chromaFmt == 0) ? 128 : planeCr[(sy / subH) * cW + (sx / subW)];
                        uint8_t r = yv, g = yv, b = yv;
                        if (chromaFmt != 0) YCbCrToRgb8(yv, cb, cr, fullRange, matrixCoeffs, r, g, b);
                        size_t di = (static_cast<size_t>(y) * picW + x) * 4;
                        out.rgba[di + 0] = r; out.rgba[di + 1] = g; out.rgba[di + 2] = b; out.rgba[di + 3] = 255;
                    }
                }
                return true;
            }
        }
    }

#ifdef _WIN32
    // Verify at least one complete VCL slice NALU (type 1 or 5) exists before spinning up Media Foundation
    size_t vclSlices = 0;
    {
        size_t vp = 0;
        while (vp + lenSize <= itemLen) {
            uint64_t nl = RdVarBE(itemData + vp, lenSize);
            vp += lenSize;
            if (nl == 0 || nl > itemLen - vp) { vclSlices = 0; break; }
            uint8_t nt = itemData[vp] & 0x1Fu;
            if (nt == 1u || nt == 5u) ++vclSlices;
            vp += static_cast<size_t>(nl);
        }
    }
    if (vclSlices == 0) return false;

    // Fallback to Windows inbox Media Foundation H.264 MFT (CLSID_CMSH264DecoderMFT via MFTEnumEx)
    if (!::Explorer2::IsMediaFoundationAvailable() || FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) return false;
    bool mfDecoded = false;
    {
        MFT_REGISTER_TYPE_INFO inType  = { MFMediaType_Video, MFVideoFormat_H264 };
        MFT_REGISTER_TYPE_INFO outType = { MFMediaType_Video, MFVideoFormat_NV12 };
        IMFActivate** activates = nullptr;
        UINT32 actCount = 0;
        if (SUCCEEDED(MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                                &inType, &outType, &activates, &actCount)) && actCount > 0)
        {
            Microsoft::WRL::ComPtr<IMFTransform> mft;
            if (SUCCEEDED(activates[0]->ActivateObject(IID_PPV_ARGS(&mft))) && mft) {
                Microsoft::WRL::ComPtr<IMFMediaType> mtIn, mtOut;
                MFCreateMediaType(&mtIn);
                mtIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                mtIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
                MFSetAttributeSize(mtIn.Get(), MF_MT_FRAME_SIZE, codedW, codedH);

                if (SUCCEEDED(mft->SetInputType(0, mtIn.Get(), 0))) {
                    for (DWORD idx = 0; idx < 16; ++idx) {
                        Microsoft::WRL::ComPtr<IMFMediaType> cand;
                        if (FAILED(mft->GetOutputAvailableType(0, idx, &cand))) break;
                        GUID sub = {};
                        cand->GetGUID(MF_MT_SUBTYPE, &sub);
                        if (sub == MFVideoFormat_NV12) {
                            if (SUCCEEDED(mft->SetOutputType(0, cand.Get(), 0))) { mtOut = cand; break; }
                        }
                    }
                }
                if (mtOut) {
                    // Build Annex-B packet: 00 00 00 01 + SPS + PPS + slice NALUs
                    std::vector<uint8_t> annexB;
                    auto pushNal = [&](const uint8_t* ptr, size_t n) {
                        const uint8_t sc[4] = { 0, 0, 0, 1 };
                        annexB.insert(annexB.end(), sc, sc + 4);
                        annexB.insert(annexB.end(), ptr, ptr + n);
                    };
                    for (const auto& s : spsList) pushNal(s.data(), s.size());
                    for (const auto& p : ppsList) pushNal(p.data(), p.size());
                    size_t ip = 0;
                    while (ip + lenSize <= itemLen) {
                        uint64_t nl = RdVarBE(itemData + ip, lenSize); ip += lenSize;
                        if (nl == 0 || nl > itemLen - ip) break;
                        pushNal(itemData + ip, static_cast<size_t>(nl));
                        ip += static_cast<size_t>(nl);
                    }

                    mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
                    mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

                    Microsoft::WRL::ComPtr<IMFMediaBuffer> inBuf;
                    Microsoft::WRL::ComPtr<IMFSample> inSamp;
                    BYTE* dstP = nullptr;
                    if (SUCCEEDED(MFCreateMemoryBuffer(static_cast<DWORD>(annexB.size()), &inBuf)) &&
                        SUCCEEDED(inBuf->Lock(&dstP, nullptr, nullptr)))
                    {
                        std::memcpy(dstP, annexB.data(), annexB.size());
                        inBuf->Unlock();
                        inBuf->SetCurrentLength(static_cast<DWORD>(annexB.size()));
                        MFCreateSample(&inSamp);
                        inSamp->AddBuffer(inBuf.Get());
                        mft->ProcessInput(0, inSamp.Get(), 0);
                        mft->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);

                        MFT_OUTPUT_STREAM_INFO osi = {};
                        mft->GetOutputStreamInfo(0, &osi);
                        Microsoft::WRL::ComPtr<IMFSample> outSamp;
                        Microsoft::WRL::ComPtr<IMFMediaBuffer> outBuf;
                        if ((osi.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0) {
                            MFCreateMemoryBuffer(std::max<DWORD>(osi.cbSize, codedW * codedH * 2), &outBuf);
                            MFCreateSample(&outSamp);
                            outSamp->AddBuffer(outBuf.Get());
                        }
                        MFT_OUTPUT_DATA_BUFFER odb = {};
                        odb.pSample = outSamp.Get();
                        DWORD status = 0;
                        for (int tries = 0; tries < 8; ++tries) {
                            HRESULT hr = mft->ProcessOutput(0, 1, &odb, &status);
                            if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
                                for (DWORD idx = 0; idx < 16; ++idx) {
                                    Microsoft::WRL::ComPtr<IMFMediaType> cand;
                                    if (FAILED(mft->GetOutputAvailableType(0, idx, &cand))) break;
                                    GUID sub = {}; cand->GetGUID(MF_MT_SUBTYPE, &sub);
                                    if (sub == MFVideoFormat_NV12 && SUCCEEDED(mft->SetOutputType(0, cand.Get(), 0))) {
                                        mtOut = cand; break;
                                    }
                                }
                                continue;
                            }
                            if (SUCCEEDED(hr) && odb.pSample) {
                                Microsoft::WRL::ComPtr<IMFMediaBuffer> resBuf;
                                if (SUCCEEDED(odb.pSample->ConvertToContiguousBuffer(&resBuf)) && resBuf) {
                                    UINT32 fw = codedW, fh = codedH;
                                    MFGetAttributeSize(mtOut.Get(), MF_MT_FRAME_SIZE, &fw, &fh);
                                    LONG stride = static_cast<LONG>(fw);
                                    UINT32 stAttr = 0;
                                    if (SUCCEEDED(mtOut->GetUINT32(MF_MT_DEFAULT_STRIDE, &stAttr)) && stAttr > 0) {
                                        stride = static_cast<LONG>(static_cast<INT32>(stAttr));
                                    }
                                    BYTE* nv = nullptr; DWORD curLen = 0;
                                    if (stride > 0 && SUCCEEDED(resBuf->Lock(&nv, nullptr, &curLen)) &&
                                        curLen >= static_cast<DWORD>(stride) * fh * 3 / 2)
                                    {
                                        out.w = picW; out.h = picH;
                                        out.rgba.resize(static_cast<size_t>(picW) * picH * 4);
                                        const uint8_t* uvBase = nv + static_cast<size_t>(stride) * fh;
                                        for (uint32_t y = 0; y < picH; ++y) {
                                            for (uint32_t x = 0; x < picW; ++x) {
                                                uint8_t yv = nv[static_cast<size_t>(y) * stride + x];
                                                const uint8_t* uvp = uvBase + static_cast<size_t>(y / 2) * stride + (x & ~1u);
                                                uint8_t r, g, b;
                                                YCbCrToRgb8(yv, uvp[0], uvp[1], fullRange, matrixCoeffs, r, g, b);
                                                size_t di = (static_cast<size_t>(y) * picW + x) * 4;
                                                out.rgba[di+0]=r; out.rgba[di+1]=g; out.rgba[di+2]=b; out.rgba[di+3]=255;
                                            }
                                        }
                                        mfDecoded = true;
                                        resBuf->Unlock();
                                    }
                                }
                                if ((osi.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0 && odb.pSample) odb.pSample->Release();
                                break;
                            }
                            break;
                        }
                    }
                }
            }
            for (UINT32 i = 0; i < actCount; ++i) activates[i]->Release();
            CoTaskMemFree(activates);
        }
    }
    MFShutdown();
    return mfDecoded;
#else
    return false;
#endif
}

// ============================================================================
// Backend 4: Pure-C++ ITU-T H.265 / HEVC ('hvc1' + 'hvcC') Still-Picture Decoder
// ============================================================================
inline bool DecodeHvc1StillItem(
    const uint8_t* itemData, size_t itemLen,
    const std::vector<uint8_t>& hvcC,
    bool fullRange, uint16_t matrixCoeffs,
    DecodedRgbaTile& out)
{
    if (!itemData || itemLen < 6 || hvcC.size() < 23 || hvcC[0] != 1) return false;
    uint8_t lenSize = static_cast<uint8_t>((hvcC[21] & 0x03u) + 1u);
    uint8_t numArrays = hvcC[22];

    std::vector<uint8_t> spsNal, ppsNal;
    size_t cp = 23;
    for (uint8_t a = 0; a < numArrays && cp + 3 <= hvcC.size(); ++a) {
        uint8_t nalType = hvcC[cp] & 0x3Fu;
        uint16_t nNals  = RdBE16(hvcC.data() + cp + 1);
        cp += 3;
        for (uint16_t n = 0; n < nNals; ++n) {
            if (cp + 2 > hvcC.size()) return false;
            uint16_t nl = RdBE16(hvcC.data() + cp); cp += 2;
            if (cp + nl > hvcC.size()) return false;
            if (nalType == 33 && spsNal.empty()) spsNal.assign(hvcC.data() + cp, hvcC.data() + cp + nl);
            if (nalType == 34 && ppsNal.empty()) ppsNal.assign(hvcC.data() + cp, hvcC.data() + cp + nl);
            cp += nl;
        }
    }
    if (spsNal.size() < 15) return false;

    // Parse HEVC SPS (nal_unit_type == 33, 2-byte NALU header)
    auto spsRbsp = StripRbspEmulationBytes(spsNal.data(), spsNal.size(), 2);
    BitReader sbr(spsRbsp.data(), spsRbsp.size());
    sbr.ReadBits(4); // vps_id
    uint32_t maxSubLayersMinus1 = sbr.ReadBits(3);
    sbr.ReadBit();   // temporal_id_nesting
    if (maxSubLayersMinus1 > 6) return false;

    // profile_tier_level(1, maxSubLayersMinus1)
    sbr.ReadBits(8);  // profile_space(2), tier(1), profile_idc(5)
    sbr.ReadBits(32); // profile_compat_flags
    sbr.ReadBits(16); // constraint_indicator_flags (48 bits total)
    sbr.ReadBits(32);
    sbr.ReadBits(8);  // general_level_idc
    std::vector<uint8_t> subProf(maxSubLayersMinus1), subLvl(maxSubLayersMinus1);
    for (uint32_t i = 0; i < maxSubLayersMinus1; ++i) { subProf[i] = static_cast<uint8_t>(sbr.ReadBit()); subLvl[i] = static_cast<uint8_t>(sbr.ReadBit()); }
    if (maxSubLayersMinus1 > 0) { for (uint32_t i = maxSubLayersMinus1; i < 8; ++i) sbr.ReadBits(2); }
    for (uint32_t i = 0; i < maxSubLayersMinus1; ++i) {
        if (subProf[i]) { sbr.ReadBits(32); sbr.ReadBits(32); sbr.ReadBits(24); }
        if (subLvl[i])  sbr.ReadBits(8);
    }

    sbr.ReadUE(); // sps_id
    uint32_t chromaFmt = sbr.ReadUE();
    if (chromaFmt == 3) sbr.ReadBit();
    uint32_t codedW = sbr.ReadUE();
    uint32_t codedH = sbr.ReadUE();
    uint32_t winL = 0, winR = 0, winT = 0, winB = 0;
    if (sbr.ReadBit()) {
        winL = sbr.ReadUE(); winR = sbr.ReadUE(); winT = sbr.ReadUE(); winB = sbr.ReadUE();
    }
    uint32_t bitDepthY = sbr.ReadUE() + 8;
    uint32_t bitDepthC = sbr.ReadUE() + 8;
    sbr.ReadUE(); // log2_max_poc_lsb_minus4
    uint32_t ordPresent = sbr.ReadBit();
    for (uint32_t i = (ordPresent ? 0 : maxSubLayersMinus1); i <= maxSubLayersMinus1; ++i) {
        sbr.ReadUE(); sbr.ReadUE(); sbr.ReadUE();
    }
    uint32_t minCbLog2 = sbr.ReadUE() + 3;
    uint32_t ctbLog2   = minCbLog2 + sbr.ReadUE();
    sbr.ReadUE(); sbr.ReadUE(); sbr.ReadUE(); sbr.ReadUE();
    if (sbr.ReadBit()) { if (sbr.ReadBit()) return false; } // scaling_list
    sbr.ReadBit(); // amp_enabled
    uint32_t saoEnabled = sbr.ReadBit();
    uint32_t pcmEnabled = sbr.ReadBit();
    uint32_t pcmBitY = 8, pcmBitC = 8;
    if (pcmEnabled) {
        pcmBitY = sbr.ReadBits(4) + 1;
        pcmBitC = sbr.ReadBits(4) + 1;
        sbr.ReadUE(); sbr.ReadUE(); sbr.ReadBit();
    }

    if (codedW == 0 || codedH == 0 || codedW > 8192 || codedH > 8192 || bitDepthY != 8 || bitDepthC != 8) return false;
    uint32_t subW = (chromaFmt == 1 || chromaFmt == 2) ? 2u : 1u;
    uint32_t subH = (chromaFmt == 1) ? 2u : 1u;
    if ((winL + winR) * subW >= codedW || (winT + winB) * subH >= codedH) return false;
    uint32_t picW = codedW - (winL + winR) * subW;
    uint32_t picH = codedH - (winT + winB) * subH;

    // Decode IDR I_PCM CTU slice (ITU-T H.265 Section 7.3.8.5 & 9.3.4.2)
    if (pcmEnabled && pcmBitY == 8 && pcmBitC == 8 && ctbLog2 >= 4 && ctbLog2 <= 6) {
        uint32_t ctbSz = 1u << ctbLog2;
        size_t pos = 0;
        while (pos + lenSize <= itemLen) {
            uint64_t nalLen = RdVarBE(itemData + pos, lenSize); pos += lenSize;
            if (nalLen < 3 || nalLen > itemLen - pos) break;
            const uint8_t* nal = itemData + pos; pos += static_cast<size_t>(nalLen);
            uint8_t nalType = (nal[0] >> 1) & 0x3Fu;
            if (nalType < 16 || nalType > 21) continue; // IDR / BLA / CRA

            auto slRbsp = StripRbspEmulationBytes(nal, static_cast<size_t>(nalLen), 2);
            BitReader br(slRbsp.data(), slRbsp.size());
            uint32_t firstSlice = br.ReadBit();
            if (!firstSlice) continue;
            br.ReadBit(); // no_output_of_prior_pics_flag
            br.ReadUE();  // pps_id
            uint32_t sliceType = br.ReadUE(); // 2 = I-slice
            if (sliceType != 2) continue;
            if (saoEnabled) { br.ReadBit(); if (chromaFmt != 0) br.ReadBit(); }
            int32_t sliceQpDelta = br.ReadSE();
            // byte_alignment(): 1 bit ('1') followed by '0's to byte boundary
            if (br.ReadBit() != 1) continue;
            br.AlignToByte();

            // Initialize HEVC CABAC 9-bit arithmetic decoder + split_cu_flag context[0] (initValue=139 for I-slice)
            int sliceQp = std::clamp(26 + sliceQpDelta, 0, 51);
            int slope = (139 >> 4) * 5 - 45;
            int offset = ((139 & 15) << 3) - 16;
            int preCtx = std::clamp(((slope * std::max(0, sliceQp)) >> 4) + offset, 1, 126);
            uint8_t valMps = (preCtx <= 63) ? 0u : 1u;
            uint8_t pState = valMps ? static_cast<uint8_t>(preCtx - 64) : static_cast<uint8_t>(63 - preCtx);

            static const uint8_t kTabRangeLPS[64][4] = {
                {128,176,208,240},{128,167,197,227},{128,158,187,216},{123,150,178,205},
                {116,142,169,195},{111,135,160,185},{105,128,152,175},{100,122,144,166},
                {95,116,137,158},{90,110,130,150},{85,104,123,142},{81,99,117,135},
                {77,94,111,128},{73,89,105,122},{69,85,100,116},{66,80,95,110},
                {62,76,90,104},{59,72,86,99},{56,69,81,94},{53,65,77,89},
                {51,62,73,85},{48,59,69,80},{46,56,66,76},{43,53,63,72},
                {41,50,59,69},{39,48,56,65},{37,45,54,62},{35,43,51,59},
                {33,41,48,56},{32,39,46,53},{30,37,43,50},{29,35,41,48},
                {27,33,39,45},{26,31,37,43},{24,30,35,41},{23,28,33,39},
                {22,27,32,37},{21,26,30,35},{20,24,29,33},{19,23,27,31},
                {18,22,26,30},{17,21,25,28},{16,20,23,27},{15,19,22,25},
                {14,18,21,24},{14,17,20,23},{13,16,19,22},{12,15,18,21},
                {12,14,17,20},{11,14,16,19},{11,13,15,18},{10,12,15,17},
                {10,12,14,16},{9,11,13,15},{9,11,12,14},{8,10,12,14},
                {8,9,11,13},{7,9,11,12},{7,9,10,12},{7,8,10,11},
                {6,8,9,11},{6,7,9,10},{6,7,8,9},{2,2,2,2}
            };
            static const uint8_t kTransIdxMPS[64] = {
                1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,
                33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,62,63
            };
            static const uint8_t kTransIdxLPS[64] = {
                0,0,1,2,2,4,4,5,6,7,8,9,9,11,11,12,13,13,15,15,16,16,18,18,19,19,21,21,22,22,23,24,
                24,25,26,26,27,27,28,29,29,30,30,30,31,32,32,33,33,33,34,34,35,35,35,36,36,36,37,37,37,38,38,63
            };

            uint32_t cRange = 510, cOffset = 0;
            auto initCabac = [&]() -> bool {
                if (br.bytePos + 2 > br.len && br.Eof()) return false;
                cRange = 510;
                cOffset = br.ReadBits(9);
                return cOffset < 510;
            };
            auto renormCabac = [&]() {
                while (cRange < 256) {
                    cRange <<= 1;
                    cOffset = (cOffset << 1) | br.ReadBit();
                }
            };
            auto decodeSplitDecision = [&]() -> uint32_t {
                uint32_t qIdx = (cRange >> 6) & 3u;
                uint32_t rLPS = kTabRangeLPS[pState][qIdx];
                cRange -= rLPS;
                uint32_t binVal = valMps;
                if (cOffset >= cRange) {
                    binVal = 1u - valMps;
                    cOffset -= cRange;
                    cRange = rLPS;
                    if (pState == 0) valMps = 1u - valMps;
                    pState = kTransIdxLPS[pState];
                } else {
                    pState = kTransIdxMPS[pState];
                }
                renormCabac();
                return binVal;
            };
            auto decodeTerminate = [&]() -> uint32_t {
                cRange -= 2;
                if (cOffset >= cRange) return 1u;
                renormCabac();
                return 0u;
            };

            if (!initCabac()) continue;

            uint32_t ctbCols = (codedW + ctbSz - 1u) / ctbSz;
            uint32_t ctbRows = (codedH + ctbSz - 1u) / ctbSz;
            std::vector<uint8_t> planeY(codedW * codedH, 0);
            uint32_t cW = codedW / subW, cH = codedH / subH;
            std::vector<uint8_t> planeCb(cW * cH, 128), planeCr(cW * cH, 128);

            bool okCtu = true;
            uint32_t ctbCw = ctbSz / subW, ctbCh = ctbSz / subH;
            for (uint32_t cy = 0; cy < ctbRows && okCtu; ++cy) {
                for (uint32_t cx = 0; cx < ctbCols && okCtu; ++cx) {
                    if (ctbLog2 > minCbLog2) {
                        if (decodeSplitDecision() != 0) { okCtu = false; break; }
                    }
                    // pcm_flag = decodeTerminate() == 1
                    if (decodeTerminate() != 1u) { okCtu = false; break; }
                    br.AlignToByte();

                    size_t need = static_cast<size_t>(ctbSz) * ctbSz + ((chromaFmt == 0) ? 0u : (2u * ctbCw * ctbCh));
                    if (br.bytePos + need > br.len) { okCtu = false; break; }

                    for (uint32_t r = 0; r < ctbSz; ++r) {
                        uint32_t dstY = cy * ctbSz + r;
                        if (dstY < codedH) {
                            uint32_t copyW = std::min(ctbSz, codedW - cx * ctbSz);
                            std::memcpy(&planeY[dstY * codedW + cx * ctbSz], br.buf + br.bytePos, copyW);
                        }
                        br.bytePos += ctbSz;
                    }
                    if (chromaFmt != 0) {
                        for (uint32_t r = 0; r < ctbCh; ++r) {
                            uint32_t dstY = cy * ctbCh + r;
                            if (dstY < cH) {
                                uint32_t copyW = std::min(ctbCw, cW - cx * ctbCw);
                                std::memcpy(&planeCb[dstY * cW + cx * ctbCw], br.buf + br.bytePos, copyW);
                            }
                            br.bytePos += ctbCw;
                        }
                        for (uint32_t r = 0; r < ctbCh; ++r) {
                            uint32_t dstY = cy * ctbCh + r;
                            if (dstY < cH) {
                                uint32_t copyW = std::min(ctbCw, cW - cx * ctbCw);
                                std::memcpy(&planeCr[dstY * cW + cx * ctbCw], br.buf + br.bytePos, copyW);
                            }
                            br.bytePos += ctbCw;
                        }
                    }

                    bool isLast = (cy + 1 == ctbRows && cx + 1 == ctbCols);
                    if (!isLast) {
                        if (!initCabac() || decodeTerminate() != 0u) { okCtu = false; break; }
                    }
                }
            }

            if (okCtu) {
                out.w = picW; out.h = picH;
                out.rgba.resize(static_cast<size_t>(picW) * picH * 4);
                uint32_t xOff = winL * subW, yOff = winT * subH;
                for (uint32_t y = 0; y < picH; ++y) {
                    for (uint32_t x = 0; x < picW; ++x) {
                        uint32_t sy = y + yOff, sx = x + xOff;
                        uint8_t yv = planeY[sy * codedW + sx];
                        uint8_t cb = (chromaFmt == 0) ? 128 : planeCb[(sy / subH) * cW + (sx / subW)];
                        uint8_t cr = (chromaFmt == 0) ? 128 : planeCr[(sy / subH) * cW + (sx / subW)];
                        uint8_t r = yv, g = yv, b = yv;
                        if (chromaFmt != 0) YCbCrToRgb8(yv, cb, cr, fullRange, matrixCoeffs, r, g, b);
                        size_t di = (static_cast<size_t>(y) * picW + x) * 4;
                        out.rgba[di+0]=r; out.rgba[di+1]=g; out.rgba[di+2]=b; out.rgba[di+3]=255;
                    }
                }
                return true;
            }
        }
    }
    return false;
}


} // namespace AvifHeifDetail

inline bool DecodeHeifImage(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    return DecodeAvifOrHeifImage(data, size, extLower, outInfo);
}

} // namespace Formats
