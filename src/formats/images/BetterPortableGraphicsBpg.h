#pragma once
// BetterPortableGraphicsBpg.h — Better Portable Graphics HEVC Intra Decoder (.bpg)

namespace Formats {

namespace DetailJxlBpg {

// ----------------------------------------------------------------------------
// BPG / ITU-T H.265 (HEVC) Helpers: MSPS, Annex-B Builder, MF MFT, Pure-C++ I_PCM
// ----------------------------------------------------------------------------
struct MsbBitReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t bitPos = 0;
    bool eof = false;

    MsbBitReader(const uint8_t* d, size_t s) : data(d), size(s) {}

    uint32_t ReadBit() {
        size_t byteIdx = bitPos >> 3;
        if (byteIdx >= size) { eof = true; return 0u; }
        uint32_t v = (data[byteIdx] >> (7u - (bitPos & 7u))) & 1u;
        ++bitPos;
        return v;
    }

    uint32_t ReadBits(uint32_t n) {
        uint32_t v = 0u;
        for (uint32_t i = 0; i < n; ++i) {
            v = (v << 1u) | ReadBit();
        }
        return v;
    }

    uint32_t ReadUe() {
        uint32_t lz = 0u;
        while (!eof && lz <= 31u && ReadBit() == 0u) ++lz;
        if (eof || lz > 31u) { eof = true; return 0u; }
        if (lz == 0u) return 0u;
        return ((1u << lz) - 1u) + ReadBits(lz);
    }

    int32_t ReadSe() {
        uint32_t u = ReadUe();
        if (u == 0u) return 0;
        int32_t mag = static_cast<int32_t>((u + 1u) >> 1u);
        return ((u & 1u) != 0u) ? mag : -mag;
    }
};

struct MsbBitWriter {
    std::vector<uint8_t> bytes;
    uint8_t cur = 0;
    uint32_t used = 0;

    void PutBit(uint32_t b) {
        cur = static_cast<uint8_t>((cur << 1u) | (b & 1u));
        if (++used == 8u) {
            bytes.push_back(cur);
            cur = 0u;
            used = 0u;
        }
    }

    void PutBits(uint32_t v, uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) {
            PutBit((v >> (n - 1u - i)) & 1u);
        }
    }

    void PutUe(uint32_t v) {
        uint32_t codeNum = v + 1u;
        uint32_t bits = 0u;
        uint32_t tmp = codeNum;
        while (tmp > 1u) { tmp >>= 1u; ++bits; }
        for (uint32_t i = 0; i < bits; ++i) PutBit(0u);
        PutBits(codeNum, bits + 1u);
    }

    void PutRbspTrailing() {
        PutBit(1u);
        while (used != 0u) PutBit(0u);
    }
};

inline bool UnescapeRbsp(const uint8_t* src, size_t len, std::vector<uint8_t>& outRbsp) {
    outRbsp.clear();
    outRbsp.reserve(len);
    uint32_t zeros = 0u;
    for (size_t i = 0; i < len; ++i) {
        uint8_t b = src[i];
        if (zeros >= 2u && b == 0x03u) {
            if (i + 1u < len && src[i + 1u] > 0x03u) return false;
            zeros = 0u;
            continue;
        }
        outRbsp.push_back(b);
        zeros = (b == 0u) ? (zeros + 1u) : 0u;
    }
    return true;
}

inline void AppendAnnexBNal(std::vector<uint8_t>& dst, uint8_t nalType, uint8_t layerId, const std::vector<uint8_t>& rbsp) {
    dst.push_back(0x00u);
    dst.push_back(0x00u);
    dst.push_back(0x00u);
    dst.push_back(0x01u);
    dst.push_back(static_cast<uint8_t>(((nalType & 0x3Fu) << 1u) | ((layerId >> 5u) & 1u)));
    dst.push_back(static_cast<uint8_t>(((layerId & 0x1Fu) << 3u) | 1u));
    uint32_t zeros = 0u;
    for (uint8_t b : rbsp) {
        if (zeros >= 2u && b <= 0x03u) {
            dst.push_back(0x03u);
            zeros = 0u;
        }
        dst.push_back(b);
        zeros = (b == 0u) ? (zeros + 1u) : 0u;
    }
}

struct BpgMsps {
    uint32_t chromaFormatIdc = 1;
    bool separateColourPlane = false;
    uint32_t bitDepthLumaMinus8 = 0;
    uint32_t bitDepthChromaMinus8 = 0;
    uint32_t log2MaxPocLsbMinus4 = 0;
    uint32_t log2MinCbMinus3 = 0;
    uint32_t log2DiffMaxMinCb = 0;
    uint32_t log2MinTbMinus2 = 0;
    uint32_t log2DiffMaxMinTb = 0;
    uint32_t maxTrDepthIntra = 0;
    bool scalingListEnabled = false;
    bool ampEnabled = false;
    bool saoEnabled = false;
    bool pcmEnabled = false;
    uint32_t pcmBitDepthLumaMinus1 = 7;
    uint32_t pcmBitDepthChromaMinus1 = 7;
    uint32_t log2MinPcmCbMinus3 = 0;
    uint32_t log2DiffMaxMinPcmCb = 0;
    bool pcmLoopFilterDisabled = false;
    bool strongIntraSmoothing = false;
    bool spsExtensionPresent = false;
    uint32_t spsRangeExtFlags9 = 0;
    bool hasRangeExt = false;
};

inline bool ParseBpgMsps(const uint8_t* raw, size_t rawLen, BpgMsps& sps) {
    if (!raw || rawLen == 0u) return false;
    std::vector<uint8_t> rbsp;
    if (!UnescapeRbsp(raw, rawLen, rbsp) || rbsp.empty()) return false;
    MsbBitReader br(rbsp.data(), rbsp.size());

    sps.chromaFormatIdc = br.ReadUe();
    if (sps.chromaFormatIdc > 3u) return false;
    if (sps.chromaFormatIdc == 3u) {
        sps.separateColourPlane = br.ReadBit() != 0u;
        if (sps.separateColourPlane) return false;
    }
    sps.bitDepthLumaMinus8   = br.ReadUe();
    sps.bitDepthChromaMinus8 = br.ReadUe();
    sps.log2MaxPocLsbMinus4  = br.ReadUe();
    sps.log2MinCbMinus3      = br.ReadUe();
    sps.log2DiffMaxMinCb     = br.ReadUe();
    sps.log2MinTbMinus2      = br.ReadUe();
    sps.log2DiffMaxMinTb     = br.ReadUe();
    sps.maxTrDepthIntra      = br.ReadUe();
    if (br.eof || sps.bitDepthLumaMinus8 > 8u || sps.bitDepthChromaMinus8 > 8u ||
        sps.log2MaxPocLsbMinus4 > 12u || sps.log2MinCbMinus3 > 3u ||
        sps.log2MinCbMinus3 + sps.log2DiffMaxMinCb > 3u) {
        return false;
    }
    sps.scalingListEnabled = br.ReadBit() != 0u;
    if (sps.scalingListEnabled) return false;
    sps.ampEnabled = br.ReadBit() != 0u;
    sps.saoEnabled = br.ReadBit() != 0u;
    sps.pcmEnabled = br.ReadBit() != 0u;
    if (sps.pcmEnabled) {
        sps.pcmBitDepthLumaMinus1   = br.ReadBits(4u);
        sps.pcmBitDepthChromaMinus1 = br.ReadBits(4u);
        sps.log2MinPcmCbMinus3      = br.ReadUe();
        sps.log2DiffMaxMinPcmCb     = br.ReadUe();
        sps.pcmLoopFilterDisabled   = br.ReadBit() != 0u;
        if (sps.log2MinPcmCbMinus3 > 2u || sps.log2MinPcmCbMinus3 + sps.log2DiffMaxMinPcmCb > 2u) {
            return false;
        }
    }
    sps.strongIntraSmoothing = br.ReadBit() != 0u;
    sps.spsExtensionPresent  = br.ReadBit() != 0u;
    if (sps.spsExtensionPresent) {
        uint32_t extFlags = br.ReadBits(8u);
        if ((extFlags & 0x80u) != 0u) { // sps_range_extension_flag
            sps.hasRangeExt = true;
            sps.spsRangeExtFlags9 = br.ReadBits(9u);
        }
    }
    return !br.eof;
}

inline void BuildHevcAnnexBFromBpg(
    const BpgMsps& sps, uint32_t picW, uint32_t picH,
    const uint8_t* annexBNals, size_t annexBLen,
    std::vector<uint8_t>& outAnnexB, uint32_t& outAlignedW, uint32_t& outAlignedH)
{
    uint32_t minCbSizeY = 1u << (sps.log2MinCbMinus3 + 3u);
    outAlignedW = (picW + minCbSizeY - 1u) & ~(minCbSizeY - 1u);
    outAlignedH = (picH + minCbSizeY - 1u) & ~(minCbSizeY - 1u);

    uint32_t profileIdc = (sps.chromaFormatIdc == 1u && sps.bitDepthLumaMinus8 == 0u) ? 1u :
                          ((sps.chromaFormatIdc == 1u && sps.bitDepthLumaMinus8 == 2u) ? 2u : 4u);
    auto writePtl = [&](MsbBitWriter& w) {
        w.PutBits(0u, 2u); // general_profile_space
        w.PutBit(0u);      // general_tier_flag
        w.PutBits(profileIdc, 5u);
        uint32_t compat = 1u << (31u - profileIdc);
        w.PutBits(compat, 32u);
        w.PutBit(1u); // progressive
        w.PutBit(0u); // interlaced
        w.PutBit(1u); // non_packed
        w.PutBit(1u); // frame_only
        w.PutBits(0u, 32u);
        w.PutBits(0u, 12u);
        w.PutBits(120u, 8u); // general_level_idc = Level 4.0
    };

    // 1. VPS (NAL 32)
    MsbBitWriter vps;
    vps.PutBits(0u, 4u);   // vps_id = 0
    vps.PutBits(3u, 2u);   // base_layer_internal=1, available=1
    vps.PutBits(0u, 6u);   // vps_max_layers_minus1 = 0
    vps.PutBits(0u, 3u);   // vps_max_sub_layers_minus1 = 0
    vps.PutBit(1u);        // vps_temporal_id_nesting_flag = 1
    vps.PutBits(0xFFFFu, 16u);
    writePtl(vps);
    vps.PutBit(1u);        // vps_sub_layer_ordering_info_present_flag
    vps.PutUe(0u); vps.PutUe(0u); vps.PutUe(0u);
    vps.PutBits(0u, 6u);   // vps_max_layer_id = 0
    vps.PutUe(0u);         // vps_num_layer_sets_minus1 = 0
    vps.PutBit(0u);        // vps_timing_info_present_flag = 0
    vps.PutBit(0u);        // vps_extension_flag = 0
    vps.PutRbspTrailing();

    // 2. SPS (NAL 33)
    MsbBitWriter sw;
    sw.PutBits(0u, 4u);    // sps_video_parameter_set_id = 0
    sw.PutBits(0u, 3u);    // sps_max_sub_layers_minus1 = 0
    sw.PutBit(1u);         // sps_temporal_id_nesting_flag = 1
    writePtl(sw);
    sw.PutUe(0u);          // sps_seq_parameter_set_id = 0
    sw.PutUe(sps.chromaFormatIdc);
    if (sps.chromaFormatIdc == 3u) sw.PutBit(0u);
    sw.PutUe(outAlignedW);
    sw.PutUe(outAlignedH);
    uint32_t subW = (sps.chromaFormatIdc == 1u || sps.chromaFormatIdc == 2u) ? 2u : 1u;
    uint32_t subH = (sps.chromaFormatIdc == 1u) ? 2u : 1u;
    if (outAlignedW != picW || outAlignedH != picH) {
        sw.PutBit(1u);
        sw.PutUe(0u);
        sw.PutUe((outAlignedW - picW) / subW);
        sw.PutUe(0u);
        sw.PutUe((outAlignedH - picH) / subH);
    } else {
        sw.PutBit(0u);
    }
    sw.PutUe(sps.bitDepthLumaMinus8);
    sw.PutUe(sps.bitDepthChromaMinus8);
    sw.PutUe(sps.log2MaxPocLsbMinus4);
    sw.PutBit(1u);
    sw.PutUe(0u); sw.PutUe(0u); sw.PutUe(0u);
    sw.PutUe(sps.log2MinCbMinus3);
    sw.PutUe(sps.log2DiffMaxMinCb);
    sw.PutUe(sps.log2MinTbMinus2);
    sw.PutUe(sps.log2DiffMaxMinTb);
    sw.PutUe(0u); // max_transform_hierarchy_depth_inter = 0
    sw.PutUe(sps.maxTrDepthIntra);
    sw.PutBit(0u); // scaling_list_enabled_flag = 0
    sw.PutBit(sps.ampEnabled ? 1u : 0u);
    sw.PutBit(sps.saoEnabled ? 1u : 0u);
    sw.PutBit(sps.pcmEnabled ? 1u : 0u);
    if (sps.pcmEnabled) {
        sw.PutBits(sps.pcmBitDepthLumaMinus1, 4u);
        sw.PutBits(sps.pcmBitDepthChromaMinus1, 4u);
        sw.PutUe(sps.log2MinPcmCbMinus3);
        sw.PutUe(sps.log2DiffMaxMinPcmCb);
        sw.PutBit(sps.pcmLoopFilterDisabled ? 1u : 0u);
    }
    sw.PutUe(0u); // num_short_term_ref_pic_sets = 0
    sw.PutBit(0u); // long_term_ref_pics_present_flag = 0
    sw.PutBit(0u); // sps_temporal_mvp_enabled_flag = 0
    sw.PutBit(sps.strongIntraSmoothing ? 1u : 0u);
    sw.PutBit(0u); // vui_parameters_present_flag = 0
    if (sps.hasRangeExt) {
        sw.PutBit(1u);
        sw.PutBits(0x80u, 8u);
        sw.PutBits(sps.spsRangeExtFlags9, 9u);
    } else {
        sw.PutBit(0u);
    }
    sw.PutRbspTrailing();

    outAnnexB.clear();
    AppendAnnexBNal(outAnnexB, 32u, 0u, vps.bytes);
    AppendAnnexBNal(outAnnexB, 33u, 0u, sw.bytes);
    outAnnexB.insert(outAnnexB.end(), annexBNals, annexBNals + annexBLen);
    // Append End-Of-Sequence (NAL 36) so MFT flushes the single Intra picture immediately
    static const uint8_t kEosNal[6] = { 0x00, 0x00, 0x00, 0x01, 0x48, 0x01 };
    outAnnexB.insert(outAnnexB.end(), kEosNal, kEosNal + 6);
}

inline bool TryDecodeHevcViaMediaFoundation(
    const std::vector<uint8_t>& annexB,
    uint32_t alignedW, uint32_t alignedH,
    uint32_t cropW, uint32_t cropH,
    std::vector<uint8_t>& outBgra)
{
#ifndef _WIN32
    return false;   // the Linux build of the readers has no Media Foundation HEVC decoder
#else
    if (annexB.empty() || alignedW == 0u || alignedH == 0u) return false;
    if (!::Explorer2::IsMediaFoundationAvailable()) return false;
    HRESULT hrMf = ::MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hrMf)) return false;

    bool ok = false;
    MFT_REGISTER_TYPE_INFO inInfo = { MFMediaType_Video, MFVideoFormat_HEVC };
    IMFActivate** activates = nullptr;
    UINT32 actCount = 0u;
    HRESULT hr = ::MFTEnumEx(
        MFT_CATEGORY_VIDEO_DECODER,
        MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER,
        &inInfo, nullptr, &activates, &actCount);

    if (SUCCEEDED(hr) && activates && actCount > 0u) {
        IMFTransform* mft = nullptr;
        if (SUCCEEDED(activates[0]->ActivateObject(IID_PPV_ARGS(&mft))) && mft) {
            IMFMediaType* inType = nullptr;
            if (SUCCEEDED(::MFCreateMediaType(&inType)) && inType) {
                inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_HEVC);
                ::MFSetAttributeSize(inType, MF_MT_FRAME_SIZE, alignedW, alignedH);
                inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
                if (SUCCEEDED(mft->SetInputType(0, inType, 0))) {
                    GUID chosenSub = GUID_NULL;
                    for (DWORD idx = 0; idx < 32u; ++idx) {
                        IMFMediaType* avail = nullptr;
                        if (FAILED(mft->GetOutputAvailableType(0, idx, &avail)) || !avail) break;
                        GUID sub = GUID_NULL;
                        avail->GetGUID(MF_MT_SUBTYPE, &sub);
                        if (sub == MFVideoFormat_RGB32 || sub == MFVideoFormat_ARGB32 ||
                            sub == MFVideoFormat_NV12  || sub == MFVideoFormat_YV12   ||
                            sub == MFVideoFormat_IYUV) {
                            if (SUCCEEDED(mft->SetOutputType(0, avail, 0))) {
                                chosenSub = sub;
                                avail->Release();
                                break;
                            }
                        }
                        avail->Release();
                    }

                    if (chosenSub != GUID_NULL) {
                        mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
                        mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

                        IMFSample* inSample = nullptr;
                        IMFMediaBuffer* inBuf = nullptr;
                        if (SUCCEEDED(::MFCreateMemoryBuffer(static_cast<DWORD>(annexB.size()), &inBuf)) && inBuf) {
                            BYTE* dstPtr = nullptr;
                            if (SUCCEEDED(inBuf->Lock(&dstPtr, nullptr, nullptr)) && dstPtr) {
                                std::memcpy(dstPtr, annexB.data(), annexB.size());
                                inBuf->Unlock();
                                inBuf->SetCurrentLength(static_cast<DWORD>(annexB.size()));
                                if (SUCCEEDED(::MFCreateSample(&inSample)) && inSample) {
                                    inSample->AddBuffer(inBuf);
                                    inSample->SetSampleTime(0);
                                    inSample->SetSampleDuration(333333);
                                    mft->ProcessInput(0, inSample, 0);
                                    inSample->Release();
                                }
                            }
                            inBuf->Release();
                        }

                        mft->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);

                        for (int attempt = 0; attempt < 4 && !ok; ++attempt) {
                            MFT_OUTPUT_STREAM_INFO osi = {};
                            mft->GetOutputStreamInfo(0, &osi);
                            bool mftAlloc = (osi.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0u;
                            IMFSample* outSample = nullptr;
                            IMFMediaBuffer* outAllocBuf = nullptr;
                            if (!mftAlloc) {
                                DWORD cbOut = std::max<DWORD>(osi.cbSize, alignedW * alignedH * 4u);
                                if (SUCCEEDED(::MFCreateMemoryBuffer(cbOut, &outAllocBuf)) && outAllocBuf &&
                                    SUCCEEDED(::MFCreateSample(&outSample)) && outSample) {
                                    outSample->AddBuffer(outAllocBuf);
                                }
                            }
                            MFT_OUTPUT_DATA_BUFFER odb = {};
                            odb.dwStreamID = 0;
                            odb.pSample = outSample;
                            DWORD status = 0;
                            HRESULT hrOut = mft->ProcessOutput(0, 1, &odb, &status);
                            if (odb.pEvents) odb.pEvents->Release();

                            if (hrOut == MF_E_TRANSFORM_STREAM_CHANGE) {
                                for (DWORD idx = 0; idx < 32u; ++idx) {
                                    IMFMediaType* avail = nullptr;
                                    if (FAILED(mft->GetOutputAvailableType(0, idx, &avail)) || !avail) break;
                                    GUID sub = GUID_NULL;
                                    avail->GetGUID(MF_MT_SUBTYPE, &sub);
                                    if (sub == MFVideoFormat_RGB32 || sub == MFVideoFormat_ARGB32 || sub == MFVideoFormat_NV12) {
                                        if (SUCCEEDED(mft->SetOutputType(0, avail, 0))) {
                                            chosenSub = sub;
                                            avail->Release();
                                            break;
                                        }
                                    }
                                    avail->Release();
                                }
                            } else if (SUCCEEDED(hrOut) && odb.pSample) {
                                IMFMediaType* curOut = nullptr;
                                UINT32 realW = alignedW, realH = alignedH;
                                INT32 stride = 0;
                                if (SUCCEEDED(mft->GetOutputCurrentType(0, &curOut)) && curOut) {
                                    ::MFGetAttributeSize(curOut, MF_MT_FRAME_SIZE, &realW, &realH);
                                    stride = static_cast<INT32>(::MFGetAttributeUINT32(curOut, MF_MT_DEFAULT_STRIDE, 0));
                                    curOut->Release();
                                }
                                IMFMediaBuffer* contig = nullptr;
                                if (SUCCEEDED(odb.pSample->ConvertToContiguousBuffer(&contig)) && contig) {
                                    BYTE* raw = nullptr;
                                    DWORD rawLen = 0;
                                    if (SUCCEEDED(contig->Lock(&raw, nullptr, &rawLen)) && raw) {
                                        outBgra.resize(static_cast<size_t>(cropW) * cropH * 4u);
                                        if (chosenSub == MFVideoFormat_RGB32 || chosenSub == MFVideoFormat_ARGB32) {
                                            size_t rowBytes = (stride > 0) ? static_cast<size_t>(stride) : (static_cast<size_t>(realW) * 4u);
                                            if (rawLen >= rowBytes * cropH) {
                                                for (uint32_t y = 0; y < cropH; ++y) {
                                                    const uint8_t* sp = raw + static_cast<size_t>(y) * rowBytes;
                                                    for (uint32_t x = 0; x < cropW; ++x) {
                                                        WritePremulBGRA(&outBgra[(static_cast<size_t>(y) * cropW + x) * 4u],
                                                                        sp[x * 4u + 2u], sp[x * 4u + 1u], sp[x * 4u + 0u], 255u);
                                                    }
                                                }
                                                ok = true;
                                            }
                                        } else if (chosenSub == MFVideoFormat_NV12) {
                                            size_t pitch = (stride > 0) ? static_cast<size_t>(stride) : static_cast<size_t>(realW);
                                            size_t uvBase = pitch * realH;
                                            if (rawLen >= uvBase + pitch * ((cropH + 1u) / 2u)) {
                                                for (uint32_t y = 0; y < cropH; ++y) {
                                                    for (uint32_t x = 0; x < cropW; ++x) {
                                                        int yv = static_cast<int>(raw[y * pitch + x]);
                                                        const uint8_t* uv = raw + uvBase + (y / 2u) * pitch + (x & ~1u);
                                                        int cb = static_cast<int>(uv[0]) - 128;
                                                        int cr = static_cast<int>(uv[1]) - 128;
                                                        uint8_t r8 = static_cast<uint8_t>(std::clamp((298 * (yv - 16) + 409 * cr + 128) >> 8, 0, 255));
                                                        uint8_t g8 = static_cast<uint8_t>(std::clamp((298 * (yv - 16) - 100 * cb - 208 * cr + 128) >> 8, 0, 255));
                                                        uint8_t b8 = static_cast<uint8_t>(std::clamp((298 * (yv - 16) + 516 * cb + 128) >> 8, 0, 255));
                                                        WritePremulBGRA(&outBgra[(static_cast<size_t>(y) * cropW + x) * 4u], r8, g8, b8, 255u);
                                                    }
                                                }
                                                ok = true;
                                            }
                                        }
                                        contig->Unlock();
                                    }
                                    contig->Release();
                                }
                            }
                            if (outAllocBuf) outAllocBuf->Release();
                            if (outSample) outSample->Release();
                            else if (mftAlloc && odb.pSample) odb.pSample->Release();
                            if (hrOut != MF_E_TRANSFORM_STREAM_CHANGE && FAILED(hrOut)) break;
                        }
                    }
                }
                inType->Release();
            }
            mft->Release();
        }
        for (UINT32 i = 0; i < actCount; ++i) {
            if (activates[i]) activates[i]->Release();
        }
        ::CoTaskMemFree(activates);
    }

    ::MFShutdown();
    return ok;
#endif
}

// Pure-C++ ITU-T H.265 Intra (I-slice + CABAC + Quadtree + I_PCM) Plane Decoder
struct HevcPps {
    bool dependentSliceEnabled = false;
    bool outputFlagPresent = false;
    uint32_t numExtraSliceHdrBits = 0;
    int32_t initQpMinus26 = 0;
    bool sliceChromaQpOffsetsPresent = false;
    bool transquantBypassEnabled = false;
    bool tilesEnabled = false;
    bool entropyCodingSyncEnabled = false;
    bool loopFilterAcrossSlicesEnabled = false;
    bool deblockingControlPresent = false;
    bool deblockingOverrideEnabled = false;
    bool deblockingDisabled = false;
    bool sliceHeaderExtPresent = false;
};

inline bool ParseHevcPps(const std::vector<uint8_t>& rbsp, HevcPps& pps) {
    if (rbsp.empty()) return false;
    MsbBitReader br(rbsp.data(), rbsp.size());
    (void)br.ReadUe(); // pps_id
    (void)br.ReadUe(); // sps_id
    pps.dependentSliceEnabled = br.ReadBit() != 0u;
    pps.outputFlagPresent     = br.ReadBit() != 0u;
    pps.numExtraSliceHdrBits  = br.ReadBits(3u);
    (void)br.ReadBit(); // sign_data_hiding
    (void)br.ReadBit(); // cabac_init_present
    (void)br.ReadUe();  // num_ref_l0
    (void)br.ReadUe();  // num_ref_l1
    pps.initQpMinus26 = br.ReadSe();
    if (pps.initQpMinus26 < -60 || pps.initQpMinus26 > 25) return false;
    (void)br.ReadBit(); // constrained_intra_pred
    (void)br.ReadBit(); // transform_skip_enabled
    if (br.ReadBit() != 0u) (void)br.ReadUe(); // cu_qp_delta
    (void)br.ReadSe(); // cb_qp_offset
    (void)br.ReadSe(); // cr_qp_offset
    pps.sliceChromaQpOffsetsPresent = br.ReadBit() != 0u;
    (void)br.ReadBit(); // weighted_pred
    (void)br.ReadBit(); // weighted_bipred
    pps.transquantBypassEnabled = br.ReadBit() != 0u;
    pps.tilesEnabled            = br.ReadBit() != 0u;
    pps.entropyCodingSyncEnabled = br.ReadBit() != 0u;
    if (pps.tilesEnabled) {
        uint32_t colsMinus1 = br.ReadUe();
        uint32_t rowsMinus1 = br.ReadUe();
        if (colsMinus1 > 64u || rowsMinus1 > 64u) return false;
        bool uniform = br.ReadBit() != 0u;
        if (!uniform) {
            for (uint32_t i = 0; i < colsMinus1; ++i) (void)br.ReadUe();
            for (uint32_t i = 0; i < rowsMinus1; ++i) (void)br.ReadUe();
        }
        (void)br.ReadBit(); // loop_filter_across_tiles
    }
    pps.loopFilterAcrossSlicesEnabled = br.ReadBit() != 0u;
    pps.deblockingControlPresent = br.ReadBit() != 0u;
    if (pps.deblockingControlPresent) {
        pps.deblockingOverrideEnabled = br.ReadBit() != 0u;
        pps.deblockingDisabled        = br.ReadBit() != 0u;
        if (!pps.deblockingDisabled) {
            (void)br.ReadSe();
            (void)br.ReadSe();
        }
    }
    if (br.ReadBit() != 0u) return false; // pps_scaling_list_data_present_flag
    (void)br.ReadBit(); // lists_modification_present
    (void)br.ReadUe();  // log2_parallel_merge_level_minus2
    pps.sliceHeaderExtPresent = br.ReadBit() != 0u;
    return !br.eof;
}

struct HevcCabacCtx {
    uint8_t state = 0;
    uint8_t mps = 0;

    void Init(uint8_t initVal, int sliceQp) {
        int slope = (initVal >> 4u) * 5 - 45;
        int inter = ((initVal & 15u) << 3) - 16;
        int pre = std::clamp(((slope * std::clamp(sliceQp, 0, 51)) >> 4) + inter, 1, 126);
        mps = (pre <= 63) ? 0u : 1u;
        state = static_cast<uint8_t>((mps != 0u) ? (pre - 64) : (63 - pre));
    }
};

struct HevcCabacDecoder {
    MsbBitReader& br;
    uint32_t range = 510;
    uint32_t offset = 0;
    bool error = false;

    explicit HevcCabacDecoder(MsbBitReader& r) : br(r) {}

    bool InitEngine() {
        range = 510u;
        offset = br.ReadBits(9u);
        if (br.eof || offset >= 510u) {
            error = true;
            return false;
        }
        return true;
    }

    void Renorm() {
        while (range < 256u && !br.eof) {
            range <<= 1u;
            offset = (offset << 1u) | br.ReadBit();
        }
        if (br.eof) error = true;
    }

    uint32_t DecodeDecision(HevcCabacCtx& ctx) {
        static constexpr uint8_t kRangeTabLps[64][4] = {
            {128,176,208,240},{128,167,197,227},{128,158,187,216},{123,150,178,205},
            {116,142,169,195},{111,135,160,185},{105,128,152,175},{100,122,144,166},
            { 95,116,137,158},{ 90,110,130,150},{ 85,104,123,142},{ 81, 99,117,135},
            { 77, 94,111,128},{ 73, 89,105,122},{ 69, 85,100,116},{ 66, 80, 95,110},
            { 62, 76, 90,104},{ 59, 72, 86, 99},{ 56, 69, 81, 94},{ 53, 65, 77, 89},
            { 51, 62, 73, 85},{ 48, 59, 69, 80},{ 46, 56, 66, 76},{ 43, 53, 63, 72},
            { 41, 50, 59, 69},{ 39, 48, 56, 65},{ 37, 45, 54, 62},{ 35, 43, 51, 59},
            { 33, 41, 48, 56},{ 32, 39, 46, 53},{ 30, 37, 43, 50},{ 29, 35, 41, 48},
            { 27, 33, 39, 45},{ 26, 31, 37, 43},{ 24, 30, 35, 41},{ 23, 28, 33, 39},
            { 22, 27, 32, 37},{ 21, 26, 30, 35},{ 20, 24, 29, 33},{ 19, 23, 27, 31},
            { 18, 22, 26, 30},{ 17, 21, 25, 28},{ 16, 20, 23, 27},{ 15, 19, 22, 25},
            { 14, 18, 21, 24},{ 14, 17, 20, 23},{ 13, 16, 19, 22},{ 12, 15, 18, 21},
            { 12, 14, 17, 20},{ 11, 14, 16, 19},{ 11, 13, 15, 18},{ 10, 12, 15, 17},
            { 10, 12, 14, 16},{  9, 11, 13, 15},{  9, 11, 12, 14},{  8, 10, 12, 14},
            {  8,  9, 11, 13},{  7,  9, 11, 12},{  7,  9, 10, 12},{  7,  8, 10, 11},
            {  6,  8,  9, 11},{  6,  7,  9, 10},{  6,  7,  8,  9},{  2,  2,  2,  2}
        };
        static constexpr uint8_t kTransIdxLps[64] = {
             0, 0, 1, 2, 2, 4, 4, 5, 6, 7, 8, 9, 9,11,11,12,
            13,13,15,15,16,16,18,18,19,19,21,21,22,22,23,24,
            24,25,26,26,27,27,28,29,29,30,30,30,31,32,32,33,
            33,33,34,34,35,35,35,36,36,36,37,37,37,38,38,63
        };
        uint32_t qIdx = (range >> 6u) & 3u;
        uint32_t lpsRange = kRangeTabLps[ctx.state][qIdx];
        range -= lpsRange;
        uint32_t bin = 0u;
        if (offset >= range) {
            bin = 1u - ctx.mps;
            offset -= range;
            range = lpsRange;
            if (ctx.state == 0u) ctx.mps = 1u - ctx.mps;
            ctx.state = kTransIdxLps[ctx.state];
        } else {
            bin = ctx.mps;
            if (ctx.state < 62u) ++ctx.state;
        }
        Renorm();
        return bin;
    }

    uint32_t DecodeTerminate() {
        if (range < 2u) { error = true; return 1u; }
        range -= 2u;
        if (offset >= range) {
            return 1u;
        }
        Renorm();
        return 0u;
    }
};

struct HevcPlanes {
    uint32_t alignedW = 0, alignedH = 0;
    uint32_t subW = 1, subH = 1;
    std::vector<uint16_t> yPlane, cbPlane, crPlane;
};

inline bool DecodeHevcLayerPureCpp(
    const BpgMsps& sps,
    uint32_t picW, uint32_t picH,
    const std::vector<uint8_t>& ppsRbsp,
    const std::vector<uint8_t>& sliceRbsp,
    uint8_t sliceNalType,
    HevcPlanes& outPlanes)
{
    if (!sps.pcmEnabled) return false; // Pure-C++ fallback decodes I_PCM CUs deterministically
    HevcPps pps;
    if (!ParseHevcPps(ppsRbsp, pps) || sliceRbsp.empty()) return false;

    uint32_t minCbLog2 = sps.log2MinCbMinus3 + 3u;
    uint32_t ctbLog2   = minCbLog2 + sps.log2DiffMaxMinCb;
    uint32_t minCbSize = 1u << minCbLog2;
    uint32_t ctbSize   = 1u << ctbLog2;
    uint32_t alignedW  = (picW + minCbSize - 1u) & ~(minCbSize - 1u);
    uint32_t alignedH  = (picH + minCbSize - 1u) & ~(minCbSize - 1u);
    uint32_t ctbW      = (alignedW + ctbSize - 1u) / ctbSize;
    uint32_t ctbH      = (alignedH + ctbSize - 1u) / ctbSize;
    uint32_t numCtbs   = ctbW * ctbH;

    uint32_t minPcmLog2 = sps.log2MinPcmCbMinus3 + 3u;
    uint32_t maxPcmLog2 = minPcmLog2 + sps.log2DiffMaxMinPcmCb;
    uint32_t pcmBdY     = sps.pcmBitDepthLumaMinus1 + 1u;
    uint32_t pcmBdC     = sps.pcmBitDepthChromaMinus1 + 1u;
    uint32_t bdY        = sps.bitDepthLumaMinus8 + 8u;
    uint32_t bdC        = sps.bitDepthChromaMinus8 + 8u;
    if (pcmBdY > bdY || pcmBdC > bdC || pcmBdY == 0u || pcmBdC == 0u) return false;

    // Parse slice_segment_header (ITU-T H.265 7.3.6.1)
    MsbBitReader br(sliceRbsp.data(), sliceRbsp.size());
    bool firstSlice = br.ReadBit() != 0u;
    if (!firstSlice) return false;
    if (sliceNalType >= 16u && sliceNalType <= 23u) {
        (void)br.ReadBit(); // no_output_of_prior_pics_flag
    }
    (void)br.ReadUe(); // slice_pic_parameter_set_id
    for (uint32_t i = 0; i < pps.numExtraSliceHdrBits; ++i) (void)br.ReadBit();
    uint32_t sliceType = br.ReadUe();
    if (br.eof || sliceType != 2u) return false; // Must be I-slice (2)

    if (pps.outputFlagPresent) (void)br.ReadBit();
    if (sliceNalType != 19u && sliceNalType != 20u) {
        (void)br.ReadBits(sps.log2MaxPocLsbMinus4 + 4u);
        if (br.ReadBit() == 0u) return false; // Expect empty short_term_ref_pic_set for still picture
    }
    bool sliceSaoLuma = false, sliceSaoChroma = false;
    if (sps.saoEnabled) {
        sliceSaoLuma = br.ReadBit() != 0u;
        if (sps.chromaFormatIdc != 0u) sliceSaoChroma = br.ReadBit() != 0u;
    }
    if (sliceSaoLuma || sliceSaoChroma) return false;

    int32_t sliceQpDelta = br.ReadSe();
    int32_t sliceQpY = 26 + pps.initQpMinus26 + sliceQpDelta;
    if (br.eof || sliceQpY < -static_cast<int32_t>(6u * sps.bitDepthLumaMinus8) || sliceQpY > 51) return false;

    if (pps.sliceChromaQpOffsetsPresent) {
        (void)br.ReadSe();
        (void)br.ReadSe();
    }
    bool sliceDeblockDisabled = pps.deblockingDisabled;
    if (pps.deblockingOverrideEnabled) {
        if (br.ReadBit() != 0u) {
            sliceDeblockDisabled = br.ReadBit() != 0u;
            if (!sliceDeblockDisabled) { (void)br.ReadSe(); (void)br.ReadSe(); }
        }
    }
    if (pps.loopFilterAcrossSlicesEnabled && !sliceDeblockDisabled) {
        (void)br.ReadBit();
    }
    if (pps.tilesEnabled || pps.entropyCodingSyncEnabled) {
        uint32_t numEp = br.ReadUe();
        if (numEp > 0u) {
            uint32_t offLen = br.ReadUe() + 1u;
            for (uint32_t i = 0; i < numEp; ++i) (void)br.ReadBits(offLen);
        }
    }
    if (pps.sliceHeaderExtPresent) {
        uint32_t extLen = br.ReadUe();
        for (uint32_t i = 0; i < extLen; ++i) (void)br.ReadBits(8u);
    }

    // byte_alignment(): alignment_bit_equal_to_one == 1, followed by alignment_bit_equal_to_zero == 0
    if (br.ReadBit() != 1u) return false;
    while ((br.bitPos & 7u) != 0u) {
        if (br.ReadBit() != 0u) return false;
    }
    if (br.eof) return false;

    outPlanes.alignedW = alignedW;
    outPlanes.alignedH = alignedH;
    outPlanes.subW = (sps.chromaFormatIdc == 1u || sps.chromaFormatIdc == 2u) ? 2u : 1u;
    outPlanes.subH = (sps.chromaFormatIdc == 1u) ? 2u : 1u;
    outPlanes.yPlane.assign(static_cast<size_t>(alignedW) * alignedH, 0u);
    if (sps.chromaFormatIdc != 0u) {
        outPlanes.cbPlane.assign(static_cast<size_t>(alignedW / outPlanes.subW) * (alignedH / outPlanes.subH), 0u);
        outPlanes.crPlane.assign(static_cast<size_t>(alignedW / outPlanes.subW) * (alignedH / outPlanes.subH), 0u);
    }

    // Quadtree CtDepth grid in units of MinCbSizeY
    uint32_t gridW = alignedW >> minCbLog2;
    uint32_t gridH = alignedH >> minCbLog2;
    std::vector<uint8_t> ctDepth(static_cast<size_t>(gridW) * gridH, 0u);

    HevcCabacCtx ctxSplit[3];
    ctxSplit[0].Init(139u, sliceQpY);
    ctxSplit[1].Init(141u, sliceQpY);
    ctxSplit[2].Init(157u, sliceQpY);
    HevcCabacCtx ctxTqBypass;
    ctxTqBypass.Init(154u, sliceQpY);
    HevcCabacCtx ctxPartMode;
    ctxPartMode.Init(184u, sliceQpY);

    HevcCabacDecoder cabac(br);
    if (!cabac.InitEngine()) return false;

    auto decodeCu = [&](auto& self, uint32_t x0, uint32_t y0, uint32_t log2Cb, uint32_t depth) -> bool {
        uint32_t cbSize = 1u << log2Cb;
        bool split = false;
        if (x0 + cbSize <= alignedW && y0 + cbSize <= alignedH && log2Cb > minCbLog2) {
            uint32_t gx = x0 >> minCbLog2;
            uint32_t gy = y0 >> minCbLog2;
            uint32_t inc = ((gx > 0u && ctDepth[gy * gridW + (gx - 1u)] > depth) ? 1u : 0u) +
                           ((gy > 0u && ctDepth[(gy - 1u) * gridW + gx] > depth) ? 1u : 0u);
            split = cabac.DecodeDecision(ctxSplit[inc]) != 0u;
            if (cabac.error) return false;
        } else if ((x0 + cbSize > alignedW || y0 + cbSize > alignedH) && log2Cb > minCbLog2) {
            split = true;
        }

        if (split) {
            uint32_t half = cbSize >> 1u;
            if (!self(self, x0, y0, log2Cb - 1u, depth + 1u)) return false;
            if (x0 + half < alignedW && !self(self, x0 + half, y0, log2Cb - 1u, depth + 1u)) return false;
            if (y0 + half < alignedH && !self(self, x0, y0 + half, log2Cb - 1u, depth + 1u)) return false;
            if (x0 + half < alignedW && y0 + half < alignedH && !self(self, x0 + half, y0 + half, log2Cb - 1u, depth + 1u)) return false;
            return true;
        }

        uint32_t gx0 = x0 >> minCbLog2, gy0 = y0 >> minCbLog2;
        uint32_t gSpan = 1u << (log2Cb - minCbLog2);
        for (uint32_t dy = 0; dy < gSpan && gy0 + dy < gridH; ++dy) {
            for (uint32_t dx = 0; dx < gSpan && gx0 + dx < gridW; ++dx) {
                ctDepth[(gy0 + dy) * gridW + (gx0 + dx)] = static_cast<uint8_t>(depth);
            }
        }

        if (pps.transquantBypassEnabled) {
            (void)cabac.DecodeDecision(ctxTqBypass);
            if (cabac.error) return false;
        }
        bool part2Nx2N = true;
        if (log2Cb == minCbLog2) {
            part2Nx2N = cabac.DecodeDecision(ctxPartMode) != 0u;
            if (cabac.error) return false;
        }
        if (!part2Nx2N || log2Cb < minPcmLog2 || log2Cb > maxPcmLog2) {
            return false; // Non-PCM Intra TU requires hardware/OS HEVC MFT
        }
        uint32_t pcmFlag = cabac.DecodeTerminate();
        if (cabac.error || pcmFlag != 1u) return false;

        // pcm_alignment_zero_bit until byte-aligned
        while ((br.bitPos & 7u) != 0u) {
            if (br.ReadBit() != 0u) return false;
        }
        if (br.eof) return false;

        // Read pcm_sample luma + Cb + Cr
        uint32_t shiftY = bdY - pcmBdY;
        for (uint32_t py = 0; py < cbSize; ++py) {
            for (uint32_t px = 0; px < cbSize; ++px) {
                uint32_t s = br.ReadBits(pcmBdY) << shiftY;
                outPlanes.yPlane[static_cast<size_t>(y0 + py) * alignedW + (x0 + px)] = static_cast<uint16_t>(s);
            }
        }
        if (sps.chromaFormatIdc != 0u) {
            uint32_t cw = cbSize / outPlanes.subW;
            uint32_t ch = cbSize / outPlanes.subH;
            uint32_t cStride = alignedW / outPlanes.subW;
            uint32_t cx0 = x0 / outPlanes.subW;
            uint32_t cy0 = y0 / outPlanes.subH;
            uint32_t shiftC = bdC - pcmBdC;
            for (uint32_t py = 0; py < ch; ++py) {
                for (uint32_t px = 0; px < cw; ++px) {
                    uint32_t s = br.ReadBits(pcmBdC) << shiftC;
                    outPlanes.cbPlane[static_cast<size_t>(cy0 + py) * cStride + (cx0 + px)] = static_cast<uint16_t>(s);
                }
            }
            for (uint32_t py = 0; py < ch; ++py) {
                for (uint32_t px = 0; px < cw; ++px) {
                    uint32_t s = br.ReadBits(pcmBdC) << shiftC;
                    outPlanes.crPlane[static_cast<size_t>(cy0 + py) * cStride + (cx0 + px)] = static_cast<uint16_t>(s);
                }
            }
        }
        if (br.eof) return false;

        // Re-initialize CABAC arithmetic engine right after pcm_sample() (ITU-T H.265 Clause 9.3.1)
        return cabac.InitEngine();
    };

    for (uint32_t ctbIdx = 0; ctbIdx < numCtbs; ++ctbIdx) {
        uint32_t xCtb = (ctbIdx % ctbW) * ctbSize;
        uint32_t yCtb = (ctbIdx / ctbW) * ctbSize;
        if (!decodeCu(decodeCu, xCtb, yCtb, ctbLog2, 0u)) return false;
        uint32_t eos = cabac.DecodeTerminate();
        if (cabac.error) return false;
        if (ctbIdx + 1u == numCtbs) {
            if (eos != 1u) return false;
        } else {
            if (eos != 0u) return false;
        }
    }
    return true;
}

} // namespace DetailJxlBpg

inline bool DecodeBetterPortableGraphicsBpg(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 10u || std::memcmp(data, "BPG\xFB", 4) != 0) return false;
    {
        uint8_t pixFmt = (data[4] >> 5u) & 0x07u;
        bool alpha1 = ((data[4] >> 4u) & 0x01u) != 0u;
        uint8_t bitDepthMinus8 = data[4] & 0x0Fu;
        uint8_t colorSpace = (data[5] >> 4u) & 0x0Fu;
        bool extPresent = ((data[5] >> 3u) & 0x01u) != 0u;
        bool alpha2 = ((data[5] >> 2u) & 0x01u) != 0u;
        bool limitedRange = ((data[5] >> 1u) & 0x01u) != 0u;
        bool animation = (data[5] & 0x01u) != 0u;
        if (pixFmt > 5u || bitDepthMinus8 > 6u || colorSpace > 5u || animation) return false;
        if (!alpha1 && alpha2) return false; // alpha1=0, alpha2=1 is reserved CMYK unless 4-plane supported

        size_t pos = 6u;
        auto rdUe7 = [&](uint64_t& outVal) -> bool {
            outVal = 0ull;
            for (int i = 0; i < 5; ++i) {
                if (pos >= size) return false;
                uint8_t b = data[pos++];
                outVal = (outVal << 7u) | static_cast<uint64_t>(b & 0x7Fu);
                if ((b & 0x80u) == 0u) return true;
            }
            return false;
        };

        uint64_t picW64 = 0ull, picH64 = 0ull, picDataLen = 0ull;
        if (!rdUe7(picW64) || !rdUe7(picH64) || !rdUe7(picDataLen)) return false;
        if (picW64 == 0ull || picW64 > 16384ull || picH64 == 0ull || picH64 > 16384ull || picDataLen == 0ull) {
            return false;
        }
        if (extPresent) {
            uint64_t extDataLen = 0ull;
            if (!rdUe7(extDataLen) || extDataLen > size - pos) return false;
            pos += static_cast<size_t>(extDataLen);
        }
        if (picDataLen > size - pos) return false;

        const uint32_t picW = static_cast<uint32_t>(picW64);
        const uint32_t picH = static_cast<uint32_t>(picH64);
        const size_t picEnd = pos + static_cast<size_t>(picDataLen);

        // Parse main HEVC mini-SPS (MSPS) and optional alpha MSPS
        uint64_t mainHdrLen = 0ull;
        if (!rdUe7(mainHdrLen) || mainHdrLen == 0ull || mainHdrLen > picEnd - pos) return false;
        DetailJxlBpg::BpgMsps mainSps;
        if (!DetailJxlBpg::ParseBpgMsps(data + pos, static_cast<size_t>(mainHdrLen), mainSps)) return false;
        pos += static_cast<size_t>(mainHdrLen);

        DetailJxlBpg::BpgMsps alphaSps;
        if (alpha1) {
            uint64_t alphaHdrLen = 0ull;
            if (!rdUe7(alphaHdrLen) || alphaHdrLen == 0ull || alphaHdrLen > picEnd - pos) return false;
            if (!DetailJxlBpg::ParseBpgMsps(data + pos, static_cast<size_t>(alphaHdrLen), alphaSps)) return false;
            pos += static_cast<size_t>(alphaHdrLen);
        }

        // Split remaining picture_data into Annex-B NAL units, validating PPS & Intra Slice presence
        if (picEnd - pos < 6u) return false;
        const uint8_t* nalStream = data + pos;
        const size_t nalStreamLen = picEnd - pos;
        if (!(nalStream[0] == 0u && nalStream[1] == 0u &&
              (nalStream[2] == 1u || (nalStream[2] == 0u && nalStream[3] == 1u)))) {
            return false;
        }

        std::vector<uint8_t> mainPpsRbsp, mainSliceRbsp, alphaPpsRbsp, alphaSliceRbsp;
        std::vector<uint8_t> mainAnnexBNals;
        uint8_t mainSliceNalType = 0u, alphaSliceNalType = 0u;

        size_t npos = 0u;
        while (npos < nalStreamLen) {
            while (npos < nalStreamLen && nalStream[npos] == 0u) ++npos;
            if (npos >= nalStreamLen || nalStream[npos] != 1u) return false;
            ++npos; // Past 0x01
            size_t nalStart = npos;
            while (npos < nalStreamLen) {
                if (npos + 3u <= nalStreamLen && nalStream[npos] == 0u && nalStream[npos + 1u] == 0u &&
                    (nalStream[npos + 2u] == 1u || (npos + 4u <= nalStreamLen && nalStream[npos + 2u] == 0u && nalStream[npos + 3u] == 1u))) {
                    break;
                }
                ++npos;
            }
            size_t nalLen = npos - nalStart;
            if (nalLen < 2u) return false;
            uint8_t hdr0 = nalStream[nalStart];
            uint8_t hdr1 = nalStream[nalStart + 1u];
            if ((hdr0 & 0x80u) != 0u || (hdr1 & 0x07u) == 0u) return false;
            uint8_t nalType = (hdr0 >> 1u) & 0x3Fu;
            uint8_t layerId = static_cast<uint8_t>(((hdr0 & 1u) << 5u) | (hdr1 >> 3u));

            if (layerId == 0u) {
                mainAnnexBNals.push_back(0x00u);
                mainAnnexBNals.push_back(0x00u);
                mainAnnexBNals.push_back(0x00u);
                mainAnnexBNals.push_back(0x01u);
                mainAnnexBNals.insert(mainAnnexBNals.end(), nalStream + nalStart, nalStream + nalStart + nalLen);
            }

            std::vector<uint8_t> rbsp;
            if (!DetailJxlBpg::UnescapeRbsp(nalStream + nalStart + 2u, nalLen - 2u, rbsp)) return false;

            if (layerId == 0u) {
                if (nalType == 34u) mainPpsRbsp = std::move(rbsp);
                else if (nalType <= 21u && mainSliceRbsp.empty()) { mainSliceRbsp = std::move(rbsp); mainSliceNalType = nalType; }
            } else if (layerId == 1u && alpha1) {
                if (nalType == 34u) alphaPpsRbsp = std::move(rbsp);
                else if (nalType <= 21u && alphaSliceRbsp.empty()) { alphaSliceRbsp = std::move(rbsp); alphaSliceNalType = nalType; }
            }
        }

        if (mainPpsRbsp.empty() || mainSliceRbsp.empty()) {
            return false; // Strictly reject BPG files missing valid PPS or Slice NALs!
        }
        if (alpha1 && (alphaPpsRbsp.empty() || alphaSliceRbsp.empty())) {
            return false;
        }

        // 1. Try Media Foundation HEVC Video Decoder MFT for standard 4:2:0 YCbCr opaque streams
        if (!alpha1 && pixFmt != 0u && colorSpace != 1u && colorSpace != 2u && mainSps.chromaFormatIdc == 1u) {
            std::vector<uint8_t> fullAnnexB;
            uint32_t alignedW = 0u, alignedH = 0u;
            DetailJxlBpg::BuildHevcAnnexBFromBpg(mainSps, picW, picH, mainAnnexBNals.data(), mainAnnexBNals.size(), fullAnnexB, alignedW, alignedH);
            std::vector<uint8_t> mfBgra;
            if (DetailJxlBpg::TryDecodeHevcViaMediaFoundation(fullAnnexB, alignedW, alignedH, picW, picH, mfBgra)) {
                outInfo.width = picW;
                outInfo.height = picH;
                outInfo.origWidth = picW;
                outInfo.origHeight = picH;
                outInfo.channels = 3u;
                outInfo.bitsPerChannel = 8u + bitDepthMinus8;
                outInfo.formatName = L"Better Portable Graphics HEVC (" + std::to_wstring(picW) + L"x" + std::to_wstring(picH) + L" .bpg)";
                outInfo.bgraPixels = std::move(mfBgra);
                outInfo.valid = true;
                return true;
            }
        }

        // 2. Pure-C++ ITU-T H.265 Intra (I-slice + CABAC + Quadtree + I_PCM) fallback
        DetailJxlBpg::HevcPlanes mainPlanes;
        if (!DetailJxlBpg::DecodeHevcLayerPureCpp(mainSps, picW, picH, mainPpsRbsp, mainSliceRbsp, mainSliceNalType, mainPlanes)) {
            return false;
        }
        DetailJxlBpg::HevcPlanes alphaPlanes;
        if (alpha1) {
            if (!DetailJxlBpg::DecodeHevcLayerPureCpp(alphaSps, picW, picH, alphaPpsRbsp, alphaSliceRbsp, alphaSliceNalType, alphaPlanes)) {
                return false;
            }
        }

        const uint32_t bdY = mainSps.bitDepthLumaMinus8 + 8u;
        const uint32_t bdC = mainSps.bitDepthChromaMinus8 + 8u;
        const uint32_t bdA = alpha1 ? (alphaSps.bitDepthLumaMinus8 + 8u) : 8u;
        const int64_t maxY = (1ll << bdY) - 1ll;
        const int64_t maxC = (1ll << bdC) - 1ll;
        const int64_t midC = 1ll << (bdC - 1u);
        const int64_t maxA = (1ll << bdA) - 1ll;

        outInfo.width = picW;
        outInfo.height = picH;
        outInfo.origWidth = picW;
        outInfo.origHeight = picH;
        outInfo.channels = (pixFmt == 0u) ? (alpha1 ? 2u : 1u) : (alpha1 ? 4u : 3u);
        outInfo.bitsPerChannel = 8u + bitDepthMinus8;
        outInfo.formatName = L"Better Portable Graphics HEVC (" + std::to_wstring(picW) + L"x" + std::to_wstring(picH) + L" .bpg)";
        outInfo.bgraPixels.resize(static_cast<size_t>(picW) * picH * 4u);

        auto norm8 = [](int64_t s, int64_t mx) -> uint8_t {
            s = std::clamp<int64_t>(s, 0ll, mx);
            return static_cast<uint8_t>((s * 255ll + (mx >> 1)) / mx);
        };

        uint32_t cStride = mainPlanes.alignedW / mainPlanes.subW;
        for (uint32_t y = 0; y < picH; ++y) {
            for (uint32_t x = 0; x < picW; ++x) {
                int64_t ySample = mainPlanes.yPlane[static_cast<size_t>(y) * mainPlanes.alignedW + x];
                int64_t cbSample = midC, crSample = midC;
                if (mainSps.chromaFormatIdc != 0u) {
                    size_t cIdx = static_cast<size_t>(y / mainPlanes.subH) * cStride + (x / mainPlanes.subW);
                    cbSample = mainPlanes.cbPlane[cIdx];
                    crSample = mainPlanes.crPlane[cIdx];
                }

                uint8_t r8 = 0u, g8 = 0u, b8 = 0u;
                if (pixFmt == 0u || mainSps.chromaFormatIdc == 0u) {
                    if (limitedRange) {
                        int64_t yMin = 16ll << (bdY - 8u);
                        int64_t ySpan = 219ll << (bdY - 8u);
                        r8 = g8 = b8 = static_cast<uint8_t>(std::clamp<int64_t>(((ySample - yMin) * 255ll + (ySpan >> 1)) / ySpan, 0ll, 255ll));
                    } else {
                        r8 = g8 = b8 = norm8(ySample, maxY);
                    }
                } else if (colorSpace == 1u) {
                    // BPG_CS_RGB stores GBR planes: plane 0 = G, plane 1 = B, plane 2 = R
                    g8 = norm8(ySample,  maxY);
                    b8 = norm8(cbSample, maxC);
                    r8 = norm8(crSample, maxC);
                } else if (colorSpace == 2u) {
                    // BPG_CS_YCgCo: plane 0 = Y, plane 1 = Cg, plane 2 = Co
                    int64_t yv = norm8(ySample, maxY);
                    int64_t cg = static_cast<int64_t>(norm8(cbSample, maxC)) - 128ll;
                    int64_t co = static_cast<int64_t>(norm8(crSample, maxC)) - 128ll;
                    int64_t t = yv - cg;
                    g8 = static_cast<uint8_t>(std::clamp<int64_t>(yv + cg, 0ll, 255ll));
                    b8 = static_cast<uint8_t>(std::clamp<int64_t>(t - co,  0ll, 255ll));
                    r8 = static_cast<uint8_t>(std::clamp<int64_t>(t + co,  0ll, 255ll));
                } else {
                    double yv = 0.0, cb = 0.0, cr = 0.0;
                    if (limitedRange) {
                        double yMin = static_cast<double>(16ll << (bdY - 8u));
                        double ySpan = static_cast<double>(219ll << (bdY - 8u));
                        double cSpan = static_cast<double>(224ll << (bdC - 8u));
                        yv = (static_cast<double>(ySample) - yMin) * (255.0 / ySpan);
                        cb = (static_cast<double>(cbSample) - static_cast<double>(midC)) * (255.0 / cSpan);
                        cr = (static_cast<double>(crSample) - static_cast<double>(midC)) * (255.0 / cSpan);
                    } else {
                        yv = static_cast<double>(ySample) * (255.0 / static_cast<double>(maxY));
                        cb = (static_cast<double>(cbSample) - static_cast<double>(midC)) * (255.0 / static_cast<double>(maxC));
                        cr = (static_cast<double>(crSample) - static_cast<double>(midC)) * (255.0 / static_cast<double>(maxC));
                    }
                    double kr = (colorSpace == 3u) ? 0.2126 : ((colorSpace >= 4u) ? 0.2627 : 0.2990);
                    double kb = (colorSpace == 3u) ? 0.0722 : ((colorSpace >= 4u) ? 0.0593 : 0.1140);
                    double kg = 1.0 - kr - kb;
                    double rF = yv + (2.0 - 2.0 * kr) * cr;
                    double bF = yv + (2.0 - 2.0 * kb) * cb;
                    double gF = yv - (2.0 * kb * (1.0 - kb) / kg) * cb - (2.0 * kr * (1.0 - kr) / kg) * cr;
                    r8 = static_cast<uint8_t>(std::clamp(std::lround(rF), 0l, 255l));
                    g8 = static_cast<uint8_t>(std::clamp(std::lround(gF), 0l, 255l));
                    b8 = static_cast<uint8_t>(std::clamp(std::lround(bF), 0l, 255l));
                }

                uint8_t a8 = 255u;
                if (alpha1) {
                    int64_t aSample = alphaPlanes.yPlane[static_cast<size_t>(y) * alphaPlanes.alignedW + x];
                    a8 = norm8(aSample, maxA);
                }

                uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(y) * picW + x) * 4u];
                if (alpha1 && alpha2) {
                    dst[0] = std::min(b8, a8);
                    dst[1] = std::min(g8, a8);
                    dst[2] = std::min(r8, a8);
                    dst[3] = a8;
                } else {
                    WritePremulBGRA(dst, r8, g8, b8, a8);
                }
            }
        }
        outInfo.valid = true;
        return true;
    }
}

} // namespace Formats
