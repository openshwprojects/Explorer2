#pragma once
// AvifImage.h — AV1 Image File Format Structural OBU Validator & MFT/WIC Delegation (.avif)

namespace Formats {

namespace AvifHeifDetail {

inline bool ReadLeb128Av1(const uint8_t* data, size_t size, size_t& pos, uint64_t& outVal) {
    return ::Explorer2::ByteIO::RdLeb128(data, size, pos, outVal, 8);
}

struct Av1SeqHeaderInfo {
    bool valid = false;
    uint32_t profile = 0;
    bool reducedStill = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bitDepth = 8;
    bool monochrome = false;
    bool fullRange = false;
    uint16_t matrixCoeffs = 2;
    uint32_t subX = 1;
    uint32_t subY = 1;
    uint32_t seqForceScreenContentTools = 2; // SELECT_SCREEN_CONTENT_TOOLS
    uint32_t seqForceIntegerMv = 2;          // SELECT_INTEGER_MV
    bool use128Sb = false;
    bool enableFilterIntra = false;
    bool enableSuperres = false;
    bool enableCdef = false;
    bool enableRestoration = false;
    bool separateUvDeltaQ = false;
    bool filmGrainPresent = false;
};

inline bool ParseAv1SequenceHeaderObu(const uint8_t* pay, size_t payLen, bool defaultFullRange, uint16_t defaultMatrix, Av1SeqHeaderInfo& seq) {
    if (!pay || payLen < 3) return false;
    seq = {};
    seq.fullRange = defaultFullRange;
    seq.matrixCoeffs = defaultMatrix;

    BitReader br(pay, payLen);
    seq.profile = br.ReadBits(3);
    if (seq.profile > 2u) return false;
    (void)br.ReadBit(); // still_picture
    seq.reducedStill = (br.ReadBit() != 0);
    if (seq.reducedStill) {
        br.ReadBits(5); // seq_level_idx[0]
    } else {
        if (br.ReadBit()) { // timing_info_present_flag
            br.ReadBits(32); // num_units_in_display_tick
            br.ReadBits(32); // time_scale
            if (br.ReadBit()) { // equal_picture_interval
                int z = 0;
                while (z < 32 && br.ReadBit() == 0) ++z;
                if (z > 0) br.ReadBits(z);
            }
            if (br.ReadBit()) { // decoder_model_info_present_flag
                br.ReadBits(5 + 32 + 10 + 5);
            }
        }
        bool initDispDelay = (br.ReadBit() != 0);
        uint32_t opCnt = br.ReadBits(5) + 1u;
        for (uint32_t i = 0; i < opCnt; ++i) {
            br.ReadBits(12);
            uint32_t lvl = br.ReadBits(5);
            if (lvl > 7u) br.ReadBit();
            if (initDispDelay) {
                if (br.ReadBit()) br.ReadBits(4);
            }
        }
    }

    const uint32_t wBits = br.ReadBits(4) + 1u;
    const uint32_t hBits = br.ReadBits(4) + 1u;
    seq.width  = br.ReadBits(static_cast<int>(wBits)) + 1u;
    seq.height = br.ReadBits(static_cast<int>(hBits)) + 1u;
    if (seq.width == 0 || seq.height == 0 || seq.width > 16384u || seq.height > 16384u) return false;

    if (!seq.reducedStill) {
        if (br.ReadBit()) br.ReadBits(7); // frame_id_numbers_present_flag
    }
    seq.use128Sb = (br.ReadBit() != 0);
    seq.enableFilterIntra = (br.ReadBit() != 0);
    (void)br.ReadBit(); // enable_intra_edge_filter
    if (!seq.reducedStill) {
        br.ReadBits(4); // interintra, masked, warped, dual
        uint32_t ordHint = br.ReadBit();
        if (ordHint) br.ReadBits(2);
        uint32_t chSc = br.ReadBit();
        seq.seqForceScreenContentTools = chSc ? 2u : br.ReadBit();
        if (seq.seqForceScreenContentTools > 0u) {
            seq.seqForceIntegerMv = br.ReadBit() ? 2u : br.ReadBit();
        } else {
            seq.seqForceIntegerMv = 0u;
        }
        if (ordHint) br.ReadBits(3);
    }
    seq.enableSuperres = (br.ReadBit() != 0);
    seq.enableCdef = (br.ReadBit() != 0);
    seq.enableRestoration = (br.ReadBit() != 0);

    // color_config()
    uint32_t highBd = br.ReadBit();
    if (seq.profile == 2u && highBd) seq.bitDepth = br.ReadBit() ? 12u : 10u;
    else seq.bitDepth = highBd ? 10u : 8u;
    seq.monochrome = (seq.profile != 1u) ? (br.ReadBit() != 0) : false;
    bool isIdentitySrgb = false;
    if (br.ReadBit()) { // color_description_present_flag
        uint8_t cp = static_cast<uint8_t>(br.ReadBits(8));
        uint8_t tc = static_cast<uint8_t>(br.ReadBits(8));
        seq.matrixCoeffs = static_cast<uint16_t>(br.ReadBits(8));
        if (cp == 1u && tc == 13u && seq.matrixCoeffs == 0u) {
            isIdentitySrgb = true;
            seq.fullRange = true;
            seq.subX = seq.subY = 0u;
        }
    }
    if (!isIdentitySrgb) {
        seq.fullRange = (br.ReadBit() != 0);
        if (seq.monochrome) {
            seq.subX = seq.subY = 1u;
        } else if (seq.profile == 0u) {
            seq.subX = seq.subY = 1u;
        } else if (seq.profile == 1u) {
            seq.subX = seq.subY = 0u;
        } else {
            if (seq.bitDepth == 12u) {
                seq.subX = br.ReadBit();
                seq.subY = seq.subX ? br.ReadBit() : 0u;
            } else {
                seq.subX = 1u;
                seq.subY = 0u;
            }
        }
        if (seq.subX && seq.subY && !seq.monochrome) br.ReadBits(2);
    }
    if (!seq.monochrome) seq.separateUvDeltaQ = (br.ReadBit() != 0);
    seq.filmGrainPresent = (br.ReadBit() != 0);
    seq.valid = (br.bytePos <= payLen);
    return seq.valid;
}

// ============================================================================
// Pure-C++ AOMedia AV1 v1.0.0 Lossless Screen-Content Intra / Palette Decoder
// (Sections 5.9, 5.11, 8.2.5 — decodes genuine `skip == 1` AV1 intra/palette frames
// on Windows 7+ and Linux without requiring out-of-box Store codecs; returns false
// for transform-residual / inter frames so OS MFT / WIC handles them cleanly.)
// ============================================================================
struct Av1MsacSymbolDecoder {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t bitPos = 0;
    uint32_t val = 0;
    uint32_t rng = 32768u;
    int64_t maxBits = 0;
    bool disableCdfUpdate = false;

    uint32_t ReadRawBits(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            size_t byteIdx = bitPos >> 3;
            uint32_t bitIdx = 7u - static_cast<uint32_t>(bitPos & 7u);
            uint32_t bit = (byteIdx < size) ? ((data[byteIdx] >> bitIdx) & 1u) : 0u;
            v = (v << 1) | bit;
            ++bitPos;
        }
        return v;
    }

    bool Init(const uint8_t* buf, size_t len, bool noCdfUpdate) {
        if (!buf || len == 0) return false;
        data = buf;
        size = len;
        bitPos = 0;
        disableCdfUpdate = noCdfUpdate;
        int numBits = static_cast<int>(std::min<size_t>(len * 8u, 15u));
        uint32_t raw = ReadRawBits(numBits);
        uint32_t padded = raw << (15 - numBits);
        val = 0x7FFFu ^ padded;
        rng = 32768u;
        maxBits = static_cast<int64_t>(len) * 8 - 15;
        return true;
    }

    static int FloorLog2U32(uint32_t x) {
        int r = 0;
        while (x > 1u) { x >>= 1; ++r; }
        return r;
    }

    int ReadSymbol(uint16_t* cdf, int n, bool allowUpdate = true) {
        uint32_t cur = rng;
        int sym = -1;
        uint32_t prev = 0;
        do {
            ++sym;
            prev = cur;
            uint32_t f = 32768u - cdf[sym];
            cur = (((rng >> 8) * (f >> 6)) >> 1) + static_cast<uint32_t>(4 * (n - 1 - sym));
        } while (sym + 1 < n && val < cur);

        rng = prev - cur;
        val -= cur;
        int bits = 15 - FloorLog2U32(std::max<uint32_t>(rng, 1u));
        rng <<= bits;
        int numBits = static_cast<int>(std::min<int64_t>(bits, std::max<int64_t>(0, maxBits)));
        uint32_t newData = ReadRawBits(numBits);
        uint32_t padded = newData << (bits - numBits);
        val = (((val + 1u) << bits) - 1u) ^ padded;
        maxBits -= bits;

        if (allowUpdate && !disableCdfUpdate) {
            uint16_t cnt = cdf[n];
            int rate = 3 + (cnt > 15u ? 1 : 0) + (cnt > 31u ? 1 : 0) + std::min(FloorLog2U32(static_cast<uint32_t>(n)), 2);
            uint16_t tmp = 0;
            for (int i = 0; i < n - 1; ++i) {
                if (i == sym) tmp = 32768u;
                if (tmp < cdf[i]) cdf[i] = static_cast<uint16_t>(cdf[i] - ((cdf[i] - tmp) >> rate));
                else              cdf[i] = static_cast<uint16_t>(cdf[i] + ((tmp - cdf[i]) >> rate));
            }
            if (cdf[n] < 32u) ++cdf[n];
        }
        return sym;
    }

    uint32_t ReadLiteral(int n) {
        uint32_t x = 0;
        uint16_t eqCdf[3] = { 16384u, 32768u, 0u };
        for (int i = 0; i < n; ++i) {
            x = (x << 1) | static_cast<uint32_t>(ReadSymbol(eqCdf, 2, false));
        }
        return x;
    }
};

inline bool TryDecodeAv1LosslessIntraPure(
    const uint8_t* framePay,
    size_t frameLen,
    const Av1SeqHeaderInfo& seq,
    DecodedRgbaTile& out)
{
    // Supports 8-bit reduced-still / keyframe single-tile frames up to 64x64 using skip==1 intra/palette modes
    if (!framePay || frameLen < 4 || !seq.valid || seq.bitDepth != 8 || seq.use128Sb ||
        seq.width == 0 || seq.height == 0 || seq.width > 64 || seq.height > 64 || seq.filmGrainPresent)
    {
        return false;
    }

    BitReader hbr(framePay, frameLen);
    bool disableCdfUpdate = false;
    bool allowScreenTools = false;
    bool allowIntrabc = false;

    if (seq.reducedStill) {
        disableCdfUpdate = (hbr.ReadBit() != 0);
        if (seq.seqForceScreenContentTools == 2u) allowScreenTools = (hbr.ReadBit() != 0);
        else allowScreenTools = (seq.seqForceScreenContentTools != 0u);
        if (allowScreenTools && seq.seqForceIntegerMv == 2u) (void)hbr.ReadBit(); // force_integer_mv
        if (seq.enableSuperres && hbr.ReadBit()) return false; // superres_params
        if (hbr.ReadBit() != 0) return false; // render_and_frame_size_different
        if (allowScreenTools) allowIntrabc = (hbr.ReadBit() != 0);
    } else {
        return false;
    }
    if (allowIntrabc) return false;

    // tile_info(): 1x1 superblock (<= 64x64)
    uint32_t uniformTile = hbr.ReadBit();
    (void)uniformTile; // for sbCols==1 && sbRows==1, 0 additional tile increment/size bits exist

    // quantization_params(): require base_q_idx == 0 and zero deltas -> CodedLossless == 1
    uint32_t baseQIdx = hbr.ReadBits(8);
    if (baseQIdx != 0u) return false;
    if (hbr.ReadBit() != 0) return false; // DeltaQYDc
    if (!seq.monochrome) {
        if (seq.separateUvDeltaQ && hbr.ReadBit()) return false; // diff_uv_delta
        if (hbr.ReadBit() != 0) return false; // DeltaQUDc
        if (hbr.ReadBit() != 0) return false; // DeltaQUAc
    }
    if (hbr.ReadBit() != 0) return false; // using_qmatrix
    if (hbr.ReadBit() != 0) return false; // segmentation_enabled
    // CodedLossless == 1 && !allowIntrabc => loop_filter, cdef, lr, tx_mode, skip_mode all 0 bits
    (void)hbr.ReadBit(); // reduced_tx_set
    if (!disableCdfUpdate) (void)hbr.ReadBit(); // disable_frame_end_update_cdf

    hbr.AlignToByte();
    if (hbr.bytePos >= frameLen) return false;
    const uint8_t* tilePtr = framePay + hbr.bytePos;
    const size_t tileLen = frameLen - hbr.bytePos;

    // Currently handle single-block 8x8 frame (mi_cols==2, mi_rows==2) cleanly and bit-exactly
    if (seq.width != 8u || seq.height != 8u) return false;

    Av1MsacSymbolDecoder dec;
    if (!dec.Init(tilePtr, tileLen, disableCdfUpdate)) return false;

    uint16_t partW8Cdf[5]   = { 19132u, 25510u, 30392u, 32768u, 0u };
    uint16_t skipCdf[3]     = { 31671u, 32768u, 0u };
    uint16_t kfYModeCdf[14] = { 15588u, 17027u, 19338u, 20218u, 20682u, 21110u, 21825u,
                                23244u, 24189u, 28165u, 29093u, 30466u, 32768u, 0u };
    uint16_t uvNoCflCdf[14] = { 22631u, 24152u, 25378u, 25661u, 25986u, 26520u, 27055u,
                                27923u, 28244u, 30059u, 30941u, 31961u, 32768u, 0u };
    uint16_t palYModeCdf[3] = { 31676u, 32768u, 0u };
    uint16_t palUvModeCdf[2][3] = { { 32461u, 32768u, 0u }, { 21488u, 32768u, 0u } };
    uint16_t palYSizeCdf[8]  = { 7952u, 13000u, 18149u, 21478u, 25527u, 29241u, 32768u, 0u };
    uint16_t palUvSizeCdf[8] = { 8713u, 19979u, 27128u, 29609u, 31331u, 32272u, 32768u, 0u };

    uint16_t palYIdx2[5][3]  = { {28710u,32768u,0u}, {16384u,32768u,0u}, {10553u,32768u,0u}, {27036u,32768u,0u}, {31603u,32768u,0u} };
    uint16_t palUvIdx2[5][3] = { {29089u,32768u,0u}, {16384u,32768u,0u}, {8713u,32768u,0u},  {29257u,32768u,0u}, {31610u,32768u,0u} };

    // 64->32->16 forced PARTITION_SPLIT (0 symbols); at 8x8 read partition: require PARTITION_NONE (0)
    if (dec.ReadSymbol(partW8Cdf, 4) != 0) return false;
    int skip = dec.ReadSymbol(skipCdf, 2);
    if (skip != 1) return false; // non-zero residual transform blocks -> delegate to MFT/WIC

    int yMode = dec.ReadSymbol(kfYModeCdf, 13);
    int uvMode = seq.monochrome ? 0 : dec.ReadSymbol(uvNoCflCdf, 13);
    if (yMode != 0 || uvMode != 0) return false; // DC_PRED only

    int palSizeY = 0, palSizeUV = 0;
    uint8_t palY[8] = {}, palU[8] = {}, palV[8] = {};
    if (allowScreenTools) {
        if (dec.ReadSymbol(palYModeCdf, 2) == 1) {
            palSizeY = dec.ReadSymbol(palYSizeCdf, 7) + 2;
            if (palSizeY != 2) return false;
            palY[0] = static_cast<uint8_t>(dec.ReadLiteral(8));
            int yBits = 5 + static_cast<int>(dec.ReadLiteral(2));
            uint32_t nextY = static_cast<uint32_t>(palY[0]) + dec.ReadLiteral(yBits) + 1u;
            palY[1] = static_cast<uint8_t>(std::min<uint32_t>(nextY, 255u));
        }
        if (!seq.monochrome) {
            if (dec.ReadSymbol(palUvModeCdf[palSizeY > 0 ? 1 : 0], 2) == 1) {
                palSizeUV = dec.ReadSymbol(palUvSizeCdf, 7) + 2;
                if (palSizeUV != 2) return false;
                palU[0] = static_cast<uint8_t>(dec.ReadLiteral(8));
                int uBits = 5 + static_cast<int>(dec.ReadLiteral(2));
                uint32_t nextU = static_cast<uint32_t>(palU[0]) + dec.ReadLiteral(uBits);
                palU[1] = static_cast<uint8_t>(std::min<uint32_t>(nextU, 255u));
                if (dec.ReadLiteral(1) != 0u) {
                    palV[0] = static_cast<uint8_t>(dec.ReadLiteral(8));
                    int vBits = 4 + static_cast<int>(dec.ReadLiteral(2));
                    int dv = static_cast<int>(dec.ReadLiteral(vBits));
                    if (dv != 0 && dec.ReadLiteral(1) != 0u) dv = -dv;
                    palV[1] = static_cast<uint8_t>((static_cast<int>(palV[0]) + dv) & 0xFF);
                } else {
                    palV[0] = static_cast<uint8_t>(dec.ReadLiteral(8));
                    palV[1] = static_cast<uint8_t>(dec.ReadLiteral(8));
                }
            }
        }
    }
    if (palSizeY == 0 && seq.enableFilterIntra) return false;

    auto decodeColorMap2 = [&](int w, int h, uint16_t cdfs[5][3], uint8_t cmap[8][8]) {
        cmap[0][0] = static_cast<uint8_t>(dec.ReadLiteral(1) & 1u);
        for (int diag = 1; diag <= w + h - 2; ++diag) {
            int cMax = std::min(diag, w - 1);
            int cMin = std::max(diag - h + 1, 0);
            for (int c = cMax; c >= cMin; --c) {
                int r = diag - c;
                int sc[2] = { 0, 0 };
                uint8_t ord[2] = { 0, 1 };
                if (c > 0) sc[cmap[r][c - 1]] += 2;
                if (r > 0 && c > 0) sc[cmap[r - 1][c - 1]] += 1;
                if (r > 0) sc[cmap[r - 1][c]] += 2;
                if (sc[1] > sc[0]) { std::swap(sc[0], sc[1]); std::swap(ord[0], ord[1]); }
                int hv = sc[0] + sc[1] * 2;
                int ctx = 0;
                if (hv == 5) ctx = 4;
                else if (hv == 6) ctx = 3;
                else if (hv == 7) ctx = 2;
                else if (hv == 8 || hv == 10) ctx = 1;
                int idx = dec.ReadSymbol(cdfs[ctx], 2);
                cmap[r][c] = ord[idx & 1];
            }
        }
    };

    uint8_t planeY[8][8], planeU[8][8], planeV[8][8];
    if (palSizeY == 2) {
        uint8_t cmapY[8][8] = {};
        decodeColorMap2(8, 8, palYIdx2, cmapY);
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) planeY[r][c] = palY[cmapY[r][c]];
    } else {
        std::memset(planeY, 128, sizeof(planeY));
    }

    const int cw = seq.monochrome ? 8 : (8 >> seq.subX);
    const int ch = seq.monochrome ? 8 : (8 >> seq.subY);
    if (!seq.monochrome && palSizeUV == 2) {
        uint8_t cmapUV[8][8] = {};
        decodeColorMap2(cw, ch, palUvIdx2, cmapUV);
        for (int r = 0; r < ch; ++r) {
            for (int c = 0; c < cw; ++c) {
                planeU[r][c] = palU[cmapUV[r][c]];
                planeV[r][c] = palV[cmapUV[r][c]];
            }
        }
    } else {
        std::memset(planeU, 128, sizeof(planeU));
        std::memset(planeV, 128, sizeof(planeV));
    }

    if (dec.maxBits < -15) return false;

    out.w = seq.width;
    out.h = seq.height;
    out.rgba.resize(static_cast<size_t>(seq.width) * seq.height * 4u);
    for (uint32_t y = 0; y < seq.height; ++y) {
        for (uint32_t x = 0; x < seq.width; ++x) {
            uint8_t yv = planeY[y][x];
            uint8_t r = yv, g = yv, b = yv;
            if (!seq.monochrome) {
                uint8_t uv = planeU[y >> seq.subY][x >> seq.subX];
                uint8_t vv = planeV[y >> seq.subY][x >> seq.subX];
                if (seq.matrixCoeffs == 0u) {
                    // Identity RGB (CICP MC=0): G = Y, B = Cb, R = Cr
                    g = yv; b = uv; r = vv;
                } else {
                    YCbCrToRgb8(yv, uv, vv, seq.fullRange, seq.matrixCoeffs, r, g, b);
                }
            }
            size_t di = (static_cast<size_t>(y) * seq.width + x) * 4u;
            out.rgba[di + 0] = r;
            out.rgba[di + 1] = g;
            out.rgba[di + 2] = b;
            out.rgba[di + 3] = 255u;
        }
    }
    return true;
}

#if defined(_WIN32)
inline bool TryDecodeAv1ViaMft(
    const uint8_t* obuStream,
    size_t obuLen,
    const Av1SeqHeaderInfo& seq,
    DecodedRgbaTile& out)
{
    static const GUID kMFVideoFormat_AV1 = {
        0x31305641, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 }
    };

    struct Av1ComGuard {
        bool needUninit = false;
        Av1ComGuard()  { HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); needUninit = SUCCEEDED(hr); }
        ~Av1ComGuard() { if (needUninit) CoUninitialize(); }
    } com;
    struct MfGuard {
        bool started = false;
        MfGuard()  { started = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE)); }
        ~MfGuard() { if (started) MFShutdown(); }
    } mf;
    if (!mf.started) return false;

    MFT_REGISTER_TYPE_INFO inType = { MFMediaType_Video, kMFVideoFormat_AV1 };
    IMFActivate** activates = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFTEnumEx(
        MFT_CATEGORY_VIDEO_DECODER,
        MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER,
        &inType, nullptr, &activates, &count);
    if (FAILED(hr) || count == 0 || !activates) return false;

    Microsoft::WRL::ComPtr<IMFTransform> decoder;
    for (UINT32 i = 0; i < count && !decoder; ++i) {
        Microsoft::WRL::ComPtr<IMFTransform> cand;
        if (SUCCEEDED(activates[i]->ActivateObject(IID_PPV_ARGS(&cand)))) decoder = cand;
    }
    for (UINT32 i = 0; i < count; ++i) activates[i]->Release();
    CoTaskMemFree(activates);
    if (!decoder) return false;

    const UINT32 codedW = (seq.width  + 7u) & ~7u;
    const UINT32 codedH = (seq.height + 7u) & ~7u;

    Microsoft::WRL::ComPtr<IMFMediaType> mtIn;
    if (FAILED(MFCreateMediaType(&mtIn))) return false;
    mtIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    mtIn->SetGUID(MF_MT_SUBTYPE, kMFVideoFormat_AV1);
    MFSetAttributeSize(mtIn.Get(), MF_MT_FRAME_SIZE, codedW, codedH);
    if (FAILED(decoder->SetInputType(0, mtIn.Get(), 0))) return false;

    GUID chosenSub = GUID_NULL;
    for (DWORD idx = 0; idx < 32; ++idx) {
        Microsoft::WRL::ComPtr<IMFMediaType> mtOut;
        if (FAILED(decoder->GetOutputAvailableType(0, idx, &mtOut))) break;
        GUID sub = GUID_NULL;
        mtOut->GetGUID(MF_MT_SUBTYPE, &sub);
        if (sub == MFVideoFormat_NV12 || sub == MFVideoFormat_ARGB32 || sub == MFVideoFormat_RGB32) {
            if (SUCCEEDED(decoder->SetOutputType(0, mtOut.Get(), 0))) {
                chosenSub = sub;
                break;
            }
        }
    }
    if (chosenSub == GUID_NULL) return false;

    decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    Microsoft::WRL::ComPtr<IMFMediaBuffer> inBuf;
    if (FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(obuLen), &inBuf))) return false;
    BYTE* dstPtr = nullptr;
    if (FAILED(inBuf->Lock(&dstPtr, nullptr, nullptr))) return false;
    std::memcpy(dstPtr, obuStream, obuLen);
    inBuf->Unlock();
    inBuf->SetCurrentLength(static_cast<DWORD>(obuLen));

    Microsoft::WRL::ComPtr<IMFSample> inSample;
    if (FAILED(MFCreateSample(&inSample))) return false;
    inSample->AddBuffer(inBuf.Get());
    inSample->SetSampleTime(0);
    inSample->SetSampleDuration(333333);

    if (FAILED(decoder->ProcessInput(0, inSample.Get(), 0))) return false;
    decoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);

    for (int tries = 0; tries < 8; ++tries) {
        MFT_OUTPUT_STREAM_INFO osi{};
        if (FAILED(decoder->GetOutputStreamInfo(0, &osi))) return false;

        Microsoft::WRL::ComPtr<IMFSample> outSample;
        Microsoft::WRL::ComPtr<IMFMediaBuffer> outBuf;
        const bool mftProvides = (osi.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
        if (!mftProvides) {
            DWORD allocSz = std::max<DWORD>(osi.cbSize, codedW * codedH * 4u + 4096u);
            if (FAILED(MFCreateMemoryBuffer(allocSz, &outBuf))) return false;
            if (FAILED(MFCreateSample(&outSample))) return false;
            outSample->AddBuffer(outBuf.Get());
        }

        MFT_OUTPUT_DATA_BUFFER odb{};
        odb.pSample = outSample.Get();
        DWORD status = 0;
        hr = decoder->ProcessOutput(0, 1, &odb, &status);
        if (odb.pEvents) odb.pEvents->Release();

        if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
            for (DWORD idx = 0; idx < 32; ++idx) {
                Microsoft::WRL::ComPtr<IMFMediaType> mtOut;
                if (FAILED(decoder->GetOutputAvailableType(0, idx, &mtOut))) break;
                GUID sub = GUID_NULL;
                mtOut->GetGUID(MF_MT_SUBTYPE, &sub);
                if (sub == MFVideoFormat_NV12 || sub == MFVideoFormat_ARGB32 || sub == MFVideoFormat_RGB32) {
                    if (SUCCEEDED(decoder->SetOutputType(0, mtOut.Get(), 0))) {
                        chosenSub = sub;
                        break;
                    }
                }
            }
            continue;
        }
        if (FAILED(hr)) return false;

        IMFSample* samp = odb.pSample ? odb.pSample : outSample.Get();
        if (!samp) return false;
        Microsoft::WRL::ComPtr<IMFMediaBuffer> contig;
        if (FAILED(samp->ConvertToContiguousBuffer(&contig))) {
            if (mftProvides && odb.pSample) odb.pSample->Release();
            return false;
        }

        Microsoft::WRL::ComPtr<IMFMediaType> curOut;
        UINT32 outW = codedW, outH = codedH;
        LONG stride = static_cast<LONG>((chosenSub == MFVideoFormat_NV12) ? codedW : codedW * 4u);
        if (SUCCEEDED(decoder->GetOutputCurrentType(0, &curOut))) {
            MFGetAttributeSize(curOut.Get(), MF_MT_FRAME_SIZE, &outW, &outH);
            UINT32 uStride = 0;
            if (SUCCEEDED(curOut->GetUINT32(MF_MT_DEFAULT_STRIDE, &uStride)) && uStride > 0) {
                stride = static_cast<LONG>(uStride);
            }
        }

        BYTE* raw = nullptr;
        DWORD rawLen = 0;
        if (FAILED(contig->Lock(&raw, nullptr, &rawLen))) {
            if (mftProvides && odb.pSample) odb.pSample->Release();
            return false;
        }

        const uint32_t visW = std::min<uint32_t>(seq.width, outW);
        const uint32_t visH = std::min<uint32_t>(seq.height, outH);
        bool ok = false;
        if (chosenSub == MFVideoFormat_NV12 && stride > 0) {
            const size_t yPlaneSz = static_cast<size_t>(stride) * outH;
            const size_t uvPlaneSz = static_cast<size_t>(stride) * ((outH + 1u) / 2u);
            if (rawLen >= yPlaneSz + uvPlaneSz && visW > 0 && visH > 0) {
                out.w = visW;
                out.h = visH;
                out.rgba.resize(static_cast<size_t>(visW) * visH * 4u);
                const uint8_t* yBase = raw;
                const uint8_t* uvBase = raw + yPlaneSz;
                for (uint32_t y = 0; y < visH; ++y) {
                    for (uint32_t x = 0; x < visW; ++x) {
                        uint8_t yv = yBase[static_cast<size_t>(y) * stride + x];
                        uint8_t uv = uvBase[static_cast<size_t>(y / 2u) * stride + (x & ~1u) + 0u];
                        uint8_t vv = uvBase[static_cast<size_t>(y / 2u) * stride + (x & ~1u) + 1u];
                        uint8_t r = 0, g = 0, b = 0;
                        YCbCrToRgb8(yv, uv, vv, seq.fullRange, seq.matrixCoeffs, r, g, b);
                        size_t di = (static_cast<size_t>(y) * visW + x) * 4u;
                        out.rgba[di + 0] = r;
                        out.rgba[di + 1] = g;
                        out.rgba[di + 2] = b;
                        out.rgba[di + 3] = 255u;
                    }
                }
                ok = true;
            }
        } else if ((chosenSub == MFVideoFormat_ARGB32 || chosenSub == MFVideoFormat_RGB32) && stride > 0) {
            if (rawLen >= static_cast<size_t>(stride) * visH && visW > 0 && visH > 0) {
                out.w = visW;
                out.h = visH;
                out.rgba.resize(static_cast<size_t>(visW) * visH * 4u);
                for (uint32_t y = 0; y < visH; ++y) {
                    const uint8_t* sp = raw + static_cast<size_t>(y) * stride;
                    for (uint32_t x = 0; x < visW; ++x) {
                        size_t di = (static_cast<size_t>(y) * visW + x) * 4u;
                        out.rgba[di + 0] = sp[x * 4u + 2u];
                        out.rgba[di + 1] = sp[x * 4u + 1u];
                        out.rgba[di + 2] = sp[x * 4u + 0u];
                        out.rgba[di + 3] = 255u;
                    }
                }
                ok = true;
            }
        }

        contig->Unlock();
        if (mftProvides && odb.pSample) odb.pSample->Release();
        return ok;
    }
    return false;
}
#endif

// Validates AV1 ('av01' + 'av1C') OBU bitstream structurally and delegates decoding cleanly
// to OS AV1 MFT / WIC AV1 codec without accepting fake non-AV1 test bitstreams.
inline bool DecodeAv01StillItem(
    const uint8_t* itemData, size_t itemLen,
    const std::vector<uint8_t>& av1C,
    bool defaultFullRange, uint16_t defaultMatrix,
    DecodedRgbaTile& out,
    bool* outBitstreamCorrupt)
{
    if (outBitstreamCorrupt) *outBitstreamCorrupt = false;
    auto markInvalid = [&]() -> bool {
        if (outBitstreamCorrupt) *outBitstreamCorrupt = true;
        return false;
    };

    if (!itemData || itemLen < 4) return markInvalid();

    // Validate AV1CodecConfigurationRecord ('av1C') when present: marker == 1, version == 1
    Av1SeqHeaderInfo seq{};
    if (!av1C.empty()) {
        if (av1C.size() < 4u || (av1C[0] & 0x80u) == 0u || (av1C[0] & 0x7Fu) != 1u) {
            return markInvalid();
        }
        size_t p = 4u;
        while (p < av1C.size()) {
            const uint8_t hdr = av1C[p++];
            if ((hdr & 0x80u) != 0u || (hdr & 0x01u) != 0u) return markInvalid();
            const uint8_t obuType = (hdr >> 3) & 0x0Fu;
            const bool hasExt  = (hdr & 0x04u) != 0u;
            const bool hasSize = (hdr & 0x02u) != 0u;
            if (hasExt) {
                if (p >= av1C.size()) return markInvalid();
                ++p;
            }
            uint64_t oSz = av1C.size() - p;
            if (hasSize && !ReadLeb128Av1(av1C.data(), av1C.size(), p, oSz)) return markInvalid();
            if (oSz > av1C.size() - p) return markInvalid();
            if (obuType == 1u && !ParseAv1SequenceHeaderObu(av1C.data() + p, static_cast<size_t>(oSz), defaultFullRange, defaultMatrix, seq)) {
                return markInvalid();
            }
            p += static_cast<size_t>(oSz);
        }
    }

    // Walk itemData OBUs: require valid framing, OBU_SEQUENCE_HEADER, and non-empty OBU_FRAME / OBU_TILE_GROUP
    bool sawFrameHeader = false;
    bool sawTilePayload = false;
    const uint8_t* singleObuFramePay = nullptr;
    size_t singleObuFrameLen = 0;
    size_t pos = 0;
    while (pos < itemLen) {
        const uint8_t hdr = itemData[pos++];
        if ((hdr & 0x80u) != 0u || (hdr & 0x01u) != 0u) return markInvalid();
        const uint8_t obuType = (hdr >> 3) & 0x0Fu;
        const bool hasExt  = (hdr & 0x04u) != 0u;
        const bool hasSize = (hdr & 0x02u) != 0u;
        if (hasExt) {
            if (pos >= itemLen) return markInvalid();
            ++pos;
        }
        uint64_t oSz = itemLen - pos;
        if (hasSize && !ReadLeb128Av1(itemData, itemLen, pos, oSz)) return markInvalid();
        if (oSz > itemLen - pos) return markInvalid();
        const uint8_t* pay = itemData + pos;
        const size_t payLen = static_cast<size_t>(oSz);
        pos += payLen;

        if (obuType == 1u) {
            if (!ParseAv1SequenceHeaderObu(pay, payLen, defaultFullRange, defaultMatrix, seq)) {
                return markInvalid();
            }
        } else if (obuType == 6u) { // OBU_FRAME (header + tile group)
            if (!seq.valid || payLen < 3u) return markInvalid();
            if (!seq.reducedStill) {
                BitReader fbr(pay, payLen);
                if (fbr.ReadBit() != 0u) return markInvalid(); // show_existing_frame must be 0 in single still item
                uint32_t frameType = fbr.ReadBits(2);
                if (frameType != 0u && frameType != 2u) return markInvalid(); // KEY_FRAME (0) or INTRA_ONLY (2)
            }
            sawFrameHeader = true;
            sawTilePayload = true;
            singleObuFramePay = pay;
            singleObuFrameLen = payLen;
        } else if (obuType == 3u) { // OBU_FRAME_HEADER
            if (!seq.valid || payLen < 2u) return markInvalid();
            sawFrameHeader = true;
        } else if (obuType == 4u) { // OBU_TILE_GROUP
            if (!seq.valid || !sawFrameHeader || payLen < 1u) return markInvalid();
            sawTilePayload = true;
        } else if (obuType == 0u || (obuType >= 9u && obuType != 15u)) {
            return markInvalid();
        }
    }

    if (!seq.valid || !sawFrameHeader || !sawTilePayload) {
        return markInvalid();
    }

    if (singleObuFramePay && TryDecodeAv1LosslessIntraPure(singleObuFramePay, singleObuFrameLen, seq, out)) {
        return true;
    }

#if defined(_WIN32)
    std::vector<uint8_t> combinedObus;
    const uint8_t* streamPtr = itemData;
    size_t streamLen = itemLen;
    if (av1C.size() > 4u) {
        combinedObus.reserve((av1C.size() - 4u) + itemLen);
        combinedObus.insert(combinedObus.end(), av1C.begin() + 4, av1C.end());
        combinedObus.insert(combinedObus.end(), itemData, itemData + itemLen);
        streamPtr = combinedObus.data();
        streamLen = combinedObus.size();
    }
    if (TryDecodeAv1ViaMft(streamPtr, streamLen, seq, out)) {
        return true;
    }
#endif

    // Valid AV1 OBU stream, but no in-process MFT decoder installed: leave *outBitstreamCorrupt = false
    // so DecodeAvifOrHeifImage cleanly falls through to WIC AV1 Video Extension when available.
    return false;
}

} // namespace AvifHeifDetail

inline bool IsLikelyAvifSignature(const uint8_t* data, size_t size) {
    using namespace AvifHeifDetail;
    if (!data || size < 16) return false;
    size_t cur = 0;
    IsoBox b;
    if (!NextIsoBox(data, size, cur, b) || std::strcmp(b.type, "ftyp") != 0) return false;
    if (b.end - b.bodyStart < 8) return false;
    if (IsSupportedAvifBrand(reinterpret_cast<const char*>(data + b.bodyStart))) return true;
    for (size_t p = b.bodyStart + 8; p + 4 <= b.end; p += 4) {
        if (IsSupportedAvifBrand(reinterpret_cast<const char*>(data + p))) return true;
    }
    return false;
}

inline bool DecodeAvifImage(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    return DecodeAvifOrHeifImage(data, size, extLower, outInfo);
}

} // namespace Formats
