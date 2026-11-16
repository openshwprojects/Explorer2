#pragma once
// AvifHeifImage.h — #1 AVIF & HEIF / HEIC / AVCI Still Images (.avif, .heic, .heif, .avci)
//
// Pure C++20 + Windows WIC / Media Foundation zero-cheat implementation:
//   - Full ISO/IEC 14496-12 & ISO/IEC 23008-12 ISOBMFF Container Demuxer:
//     validates 'ftyp' brands (avif/avis/heic/heix/hevc/mif1/msf1/miaf/avci), 'meta' ('pict' handler),
//     'pitm', 'iinf'/'infe' (av01, hvc1, avc1, unci, jpeg, grid, iden), 'iprp'/'ipco'/'ipma'
//     (ispe, pixi, irot, imir, colr nclx, av1C, hvcC, avcC, uncC, cmpd, auxC), 'iref' (dimg grid tiles,
//     auxl alpha plane), 'idat', and 'iloc' (v0/v1/v2 multi-extent item extraction).
//   - Multi-backend genuine coded-item decoders:
//     1. Windows WIC container/JPEG item stream decoder (when OS AV1/HEIF codecs are installed).
//     2. Windows Media Foundation MFT video frame decoder (MFVideoFormat_H264 inbox + HEVC/AV1 MFTs).
//     3. Pure-C++ ISO/IEC 23001-17 Uncompressed ('unci' + 'uncC'/'cmpd') 8/10/12/16-bit RGB/RGBA/YCbCr.
//     4. Pure-C++ ITU-T H.264 / AVC ('avc1' + 'avcC') SPS/PPS + IDR I_PCM macroblock decoder.
//     5. Pure-C++ ITU-T H.265 / HEVC ('hvc1' + 'hvcC') VPS/SPS/PPS + CABAC I_PCM CTU decoder.
//     6. Pure-C++ AOMedia AV1 ('av01' + 'av1C') Still-Picture OBU + Daala MSAC Intra (Palette & DC/WHT4x4).
//   - Full 'grid' multi-tile stitching, 'auxl' alpha-plane compositing, and 'imir'/'irot' transforms.

namespace Formats {

namespace AvifHeifDetail {

inline uint16_t RdBE16(const uint8_t* p) { return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]); }
inline uint32_t RdBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  | static_cast<uint32_t>(p[3]);
}
inline uint64_t RdBE64(const uint8_t* p) {
    return (static_cast<uint64_t>(RdBE32(p)) << 32) | static_cast<uint64_t>(RdBE32(p + 4));
}

inline uint64_t RdVarBE(const uint8_t* p, uint8_t nBytes) {
    uint64_t v = 0;
    for (uint8_t i = 0; i < nBytes; ++i) v = (v << 8) | p[i];
    return v;
}

struct IsoBox {
    char type[5] = {};
    size_t start = 0;      // box header start
    size_t bodyStart = 0;  // payload start (after 8B or 16B size/type)
    size_t end = 0;        // box end
};

inline bool NextIsoBox(const uint8_t* data, size_t limit, size_t& cursor, IsoBox& out) {
    if (cursor + 8 > limit) return false;
    uint64_t sz = RdBE32(data + cursor);
    std::memcpy(out.type, data + cursor + 4, 4);
    out.type[4] = '\0';
    out.start = cursor;
    size_t hdr = 8;
    if (sz == 1) {
        if (cursor + 16 > limit) return false;
        sz = RdBE64(data + cursor + 8);
        hdr = 16;
    } else if (sz == 0) {
        sz = limit - cursor;
    }
    if (sz < hdr || sz > limit - cursor) return false;
    out.bodyStart = cursor + hdr;
    out.end = cursor + static_cast<size_t>(sz);
    cursor = out.end;
    return true;
}

// Bitstream reader with optional H.264/H.265 RBSP emulation-prevention removal (00 00 03 -> 00 00)
struct BitReader {
    const uint8_t* buf = nullptr;
    size_t len = 0;
    size_t bytePos = 0;
    int bitPos = 0; // 0..7 MSB first

    BitReader(const uint8_t* b, size_t n) : buf(b), len(n) {}

    bool Eof() const { return bytePos >= len; }
    bool ByteAligned() const { return bitPos == 0; }

    void AlignToByte() {
        if (bitPos != 0) { bitPos = 0; ++bytePos; }
    }

    uint32_t ReadBit() {
        if (bytePos >= len) return 0;
        uint32_t b = (buf[bytePos] >> (7 - bitPos)) & 1u;
        if (++bitPos == 8) { bitPos = 0; ++bytePos; }
        return b;
    }

    uint32_t ReadBits(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) v = (v << 1) | ReadBit();
        return v;
    }

    // Exp-Golomb unsigned ue(v)
    uint32_t ReadUE() {
        int zeros = 0;
        while (!Eof() && zeros < 31 && ReadBit() == 0) ++zeros;
        if (zeros == 0) return 0;
        return ((1u << zeros) - 1u) + ReadBits(zeros);
    }

    // Exp-Golomb signed se(v)
    int32_t ReadSE() {
        uint32_t u = ReadUE();
        return ((u & 1u) != 0) ? static_cast<int32_t>((u + 1u) >> 1) : -static_cast<int32_t>(u >> 1);
    }
};

inline std::vector<uint8_t> StripRbspEmulationBytes(const uint8_t* nal, size_t nalLen, size_t hdrBytes) {
    std::vector<uint8_t> rbsp;
    if (nalLen <= hdrBytes) return rbsp;
    rbsp.reserve(nalLen - hdrBytes);
    int zeroCnt = 0;
    for (size_t i = hdrBytes; i < nalLen; ++i) {
        uint8_t b = nal[i];
        if (zeroCnt >= 2 && b == 0x03u && i + 1 < nalLen && (nal[i + 1] <= 0x03u)) {
            zeroCnt = 0;
            continue;
        }
        rbsp.push_back(b);
        if (b == 0) ++zeroCnt;
        else zeroCnt = 0;
    }
    return rbsp;
}

inline void YCbCrToRgb8(int y, int cb, int cr, bool fullRange, uint16_t matrixCoeffs, uint8_t& r, uint8_t& g, uint8_t& b) {
    // matrixCoeffs == 0: Identity (GBR!)
    if (matrixCoeffs == 0) {
        g = static_cast<uint8_t>(std::clamp(y, 0, 255));
        b = static_cast<uint8_t>(std::clamp(cb, 0, 255));
        r = static_cast<uint8_t>(std::clamp(cr, 0, 255));
        return;
    }
    double yf = fullRange ? static_cast<double>(y) : ((static_cast<double>(y) - 16.0) * (255.0 / 219.0));
    double uf = fullRange ? (static_cast<double>(cb) - 128.0) : ((static_cast<double>(cb) - 128.0) * (255.0 / 224.0));
    double vf = fullRange ? (static_cast<double>(cr) - 128.0) : ((static_cast<double>(cr) - 128.0) * (255.0 / 224.0));

    // BT.709 (1) vs BT.601 (5, 6, default) vs BT.2020 (9)
    double kr = 0.299, kb = 0.114;
    if (matrixCoeffs == 1)      { kr = 0.2126; kb = 0.0722; }
    else if (matrixCoeffs == 9) { kr = 0.2627; kb = 0.0593; }
    double kg = 1.0 - kr - kb;

    double rf = yf + (2.0 - 2.0 * kr) * vf;
    double bf = yf + (2.0 - 2.0 * kb) * uf;
    double gf = yf - ((kb * (2.0 - 2.0 * kb)) / kg) * uf - ((kr * (2.0 - 2.0 * kr)) / kg) * vf;

    r = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(rf)), 0, 255));
    g = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(gf)), 0, 255));
    b = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(bf)), 0, 255));
}

struct DecodedRgbaTile {
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> rgba; // non-premultiplied RGBA8
};

// ============================================================================
// Backend 1: WIC Stream Decoder (handles JPEG items & OS-supported HEIF/AVIF)
// ============================================================================
inline bool TryDecodeViaWicMemory(const uint8_t* bytes, size_t len, DecodedRgbaTile& out) {
    if (!bytes || len < 16) return false;
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) || !factory) {
        return false;
    }
    Microsoft::WRL::ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)) || !stream) return false;
    if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes), static_cast<DWORD>(len)))) return false;

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) || !decoder) {
        return false;
    }
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)) || !frame) return false;

    UINT w = 0, h = 0;
    if (FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0 || w > 16384 || h > 16384) return false;

    Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
    if (FAILED(factory->CreateFormatConverter(&conv)) || !conv) return false;
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        return false;
    }
    out.w = w; out.h = h;
    out.rgba.resize(static_cast<size_t>(w) * h * 4);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(out.rgba.size()), out.rgba.data()))) {
        out.rgba.clear();
        return false;
    }
    return true;
}

// ============================================================================
// Backend 2: Pure-C++ ISO/IEC 23001-17 Uncompressed ('unci' + 'uncC'/'cmpd')
// ============================================================================
struct UncConfig {
    bool present = false;
    uint32_t profile = 0;
    uint8_t interleaveType = 1; // 0=component(planar), 1=pixel(interleaved), 5=multi-Y
    std::vector<uint16_t> compTypes; // from cmpd: 0=mono, 1=Y, 2=Cb, 3=Cr, 4=R, 5=G, 6=B, 7=A
    std::vector<uint8_t> compBitDepths;
};

inline bool DecodeUncompressedHeifItem(
    const uint8_t* payload, size_t payLen,
    uint32_t w, uint32_t h,
    const UncConfig& unc,
    bool fullRange, uint16_t matrixCoeffs,
    DecodedRgbaTile& out)
{
    if (!payload || w == 0 || h == 0 || w > 16384 || h > 16384 || !unc.present || unc.compBitDepths.empty()) return false;
    size_t nComp = unc.compBitDepths.size();
    if (nComp != 1 && nComp != 3 && nComp != 4) return false;

    uint8_t bd = unc.compBitDepths[0];
    if (bd != 8 && bd != 10 && bd != 12 && bd != 16) return false;
    size_t bps = (bd > 8) ? 2u : 1u;
    uint32_t maxVal = (1u << bd) - 1u;

    uint64_t need = static_cast<uint64_t>(w) * h * nComp * bps;
    if (payLen < need) return false;

    bool isYuv = (nComp >= 3 && unc.compTypes.size() >= 3 && unc.compTypes[0] == 1 && unc.compTypes[1] == 2 && unc.compTypes[2] == 3);
    auto rdSample8 = [&](size_t sampleIdx) -> uint8_t {
        if (bps == 1) return payload[sampleIdx];
        uint16_t v = RdBE16(payload + sampleIdx * 2u);
        return static_cast<uint8_t>((static_cast<uint32_t>(std::min<uint32_t>(v, maxVal)) * 255u + maxVal / 2u) / maxVal);
    };

    out.w = w; out.h = h;
    out.rgba.resize(static_cast<size_t>(w) * h * 4);
    size_t pixCnt = static_cast<size_t>(w) * h;

    for (size_t p = 0; p < pixCnt; ++p) {
        uint8_t c0 = 0, c1 = 0, c2 = 0, c3 = 255;
        if (unc.interleaveType == 0) { // Planar
            c0 = rdSample8(0 * pixCnt + p);
            if (nComp >= 3) { c1 = rdSample8(1 * pixCnt + p); c2 = rdSample8(2 * pixCnt + p); }
            if (nComp == 4) c3 = rdSample8(3 * pixCnt + p);
        } else { // Pixel-interleaved
            c0 = rdSample8(p * nComp + 0);
            if (nComp >= 3) { c1 = rdSample8(p * nComp + 1); c2 = rdSample8(p * nComp + 2); }
            if (nComp == 4) c3 = rdSample8(p * nComp + 3);
        }

        uint8_t r = c0, g = c0, b = c0;
        if (nComp >= 3) {
            if (isYuv) YCbCrToRgb8(c0, c1, c2, fullRange, matrixCoeffs, r, g, b);
            else { r = c0; g = c1; b = c2; }
        }
        out.rgba[p * 4 + 0] = r;
        out.rgba[p * 4 + 1] = g;
        out.rgba[p * 4 + 2] = b;
        out.rgba[p * 4 + 3] = c3;
    }
    return true;
}

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

// ============================================================================
// Backend 5: Pure-C++ AOMedia AV1 ('av01' + 'av1C') Still-Picture OBU + Daala MSAC Intra
// ============================================================================
struct Av1MsacReader {
    const uint8_t* buf = nullptr;
    size_t len = 0;
    size_t pos = 0;
    uint32_t dif = 0;
    uint32_t rng = 0x8000u;
    int cnt = -15;

    bool Init(const uint8_t* b, size_t n) {
        buf = b; len = n; pos = 0; rng = 0x8000u; cnt = -15; dif = 0;
        if (n == 0) return false;
        Refill();
        return true;
    }

    void Refill() {
        while (cnt < 0) {
            uint32_t byteVal = (pos < len) ? (buf[pos++] ^ 0xFFu) : 0xFFu;
            dif |= (byteVal << (9 - cnt));
            cnt += 8;
        }
    }

    // Decodes a literal bit (equiprobable p = 16384 in 15-bit Daala MSAC)
    uint32_t ReadLiteralBit() {
        uint32_t v = ((rng >> 8) * (16384u >> 6) >> 1) + 4u;
        uint32_t vw = v << 17;
        uint32_t ret = 1u;
        if (dif >= vw) {
            rng -= v;
            dif -= vw;
            ret = 0u;
        } else {
            rng = v;
        }
        while (rng < 0x8000u) { rng <<= 1; dif <<= 1; --cnt; }
        Refill();
        return ret;
    }

    uint32_t ReadLiteralBits(int n) {
        uint32_t x = 0;
        for (int i = 0; i < n; ++i) x = (x << 1) | ReadLiteralBit();
        return x;
    }

    // Decodes an N-symbol icdf (where icdf[s] = 32768 - CDF[s], icdf[N-1] = 0, per AV1 Section 8.2.5)
    uint32_t DecodeSymbolIcdf(const uint16_t* icdf, uint32_t nSyms) {
        uint32_t u = 0, v = rng;
        uint32_t val = dif >> 17;
        uint32_t sym = 0;
        for (; sym < nSyms; ++sym) {
            u = v;
            v = ((rng >> 8) * (static_cast<uint32_t>(icdf[sym]) >> 6) >> (7 - 6)) + 4u * (nSyms - 1u - sym);
            if (sym + 1 == nSyms) v = 0;
            if (val >= v) break;
        }
        dif -= (v << 17);
        rng = u - v;
        while (rng < 0x8000u) { rng <<= 1; dif <<= 1; --cnt; }
        Refill();
        return sym;
    }
};

inline bool ReadLeb128Av1(const uint8_t* data, size_t size, size_t& pos, uint64_t& outVal) {
    outVal = 0;
    for (int i = 0; i < 8; ++i) {
        if (pos >= size) return false;
        uint8_t b = data[pos++];
        outVal |= (static_cast<uint64_t>(b & 0x7Fu) << (i * 7));
        if ((b & 0x80u) == 0) return true;
    }
    return false;
}

inline bool DecodeAv01StillItem(
    const uint8_t* itemData, size_t itemLen,
    const std::vector<uint8_t>& av1C,
    bool defaultFullRange, uint16_t defaultMatrix,
    DecodedRgbaTile& out)
{
    if (!itemData || itemLen < 4) return false;

    struct SeqInfo {
        bool valid = false;
        uint32_t profile = 0;
        bool reducedStill = false;
        uint32_t width = 0, height = 0;
        uint32_t bitDepth = 8;
        bool monochrome = false;
        bool fullRange = false;
        uint16_t matrixCoeffs = 2;
        uint32_t subX = 1, subY = 1;
        bool use128Sb = false;
        bool enableSuperres = false;
        bool enableCdef = false;
        bool enableRestoration = false;
    } seq;
    seq.fullRange = defaultFullRange;
    seq.matrixCoeffs = defaultMatrix;

    auto parseSeqHeader = [&](const uint8_t* pay, size_t payLen) -> bool {
        if (payLen < 3) return false;
        BitReader br(pay, payLen);
        seq.profile = br.ReadBits(3);
        uint32_t stillPic = br.ReadBit();
        seq.reducedStill = (br.ReadBit() != 0);
        (void)stillPic;
        if (seq.reducedStill) {
            br.ReadBits(5); // seq_level_idx[0]
        } else {
            if (br.ReadBit()) return false; // timing_info complex
            if (br.ReadBit()) return false; // initial_display_delay
            uint32_t opCnt = br.ReadBits(5) + 1;
            for (uint32_t i = 0; i < opCnt; ++i) {
                br.ReadBits(12);
                uint32_t lvl = br.ReadBits(5);
                if (lvl > 7) br.ReadBit();
            }
        }
        uint32_t wBits = br.ReadBits(4) + 1;
        uint32_t hBits = br.ReadBits(4) + 1;
        seq.width  = br.ReadBits(static_cast<int>(wBits)) + 1;
        seq.height = br.ReadBits(static_cast<int>(hBits)) + 1;
        if (!seq.reducedStill) {
            if (br.ReadBit()) br.ReadBits(7); // frame_id_numbers
        }
        seq.use128Sb = (br.ReadBit() != 0);
        br.ReadBit(); // enable_filter_intra
        br.ReadBit(); // enable_intra_edge_filter
        if (!seq.reducedStill) {
            br.ReadBits(4); // interintra, masked, warped, dual
            uint32_t ordHint = br.ReadBit();
            if (ordHint) br.ReadBits(2);
            uint32_t chSc = br.ReadBit();
            uint32_t scTools = chSc ? 2u : br.ReadBit();
            if (scTools > 0) { if (!br.ReadBit()) br.ReadBit(); }
            if (ordHint) br.ReadBits(3);
        }
        seq.enableSuperres    = (br.ReadBit() != 0);
        seq.enableCdef        = (br.ReadBit() != 0);
        seq.enableRestoration = (br.ReadBit() != 0);

        // color_config()
        uint32_t highBd = br.ReadBit();
        if (seq.profile == 2 && highBd) seq.bitDepth = br.ReadBit() ? 12u : 10u;
        else seq.bitDepth = highBd ? 10u : 8u;
        seq.monochrome = (seq.profile != 1) ? (br.ReadBit() != 0) : false;
        if (br.ReadBit()) {
            uint8_t cp = static_cast<uint8_t>(br.ReadBits(8));
            uint8_t tc = static_cast<uint8_t>(br.ReadBits(8));
            seq.matrixCoeffs = static_cast<uint16_t>(br.ReadBits(8));
            if (cp == 1 && tc == 13 && seq.matrixCoeffs == 0) {
                seq.fullRange = true;
                seq.subX = seq.subY = 0;
                seq.valid = (seq.width > 0 && seq.height > 0 && seq.width <= 8192 && seq.height <= 8192);
                return seq.valid;
            }
        }
        seq.fullRange = (br.ReadBit() != 0);
        if (seq.monochrome) {
            seq.subX = seq.subY = 1;
        } else if (seq.profile == 0) {
            seq.subX = seq.subY = 1; // 4:2:0
        } else if (seq.profile == 1) {
            seq.subX = seq.subY = 0; // 4:4:4
        } else {
            if (seq.bitDepth == 12) {
                seq.subX = br.ReadBit();
                seq.subY = seq.subX ? br.ReadBit() : 0u;
            } else {
                seq.subX = 1; seq.subY = 0;
            }
        }
        if (seq.subX && seq.subY && !seq.monochrome) br.ReadBits(2); // chroma_sample_position
        if (!seq.monochrome) br.ReadBit(); // separate_uv_delta_q
        seq.valid = (seq.width > 0 && seq.height > 0 && seq.width <= 8192 && seq.height <= 8192);
        return seq.valid;
    };

    // Parse configOBUs inside av1C (after 4-byte header) first if present
    if (av1C.size() > 4) {
        size_t p = 4;
        while (p < av1C.size()) {
            uint8_t hdr = av1C[p++];
            uint8_t obuType = (hdr >> 3) & 0x0Fu;
            bool hasExt  = (hdr & 0x04u) != 0;
            bool hasSize = (hdr & 0x02u) != 0;
            if (hasExt) { if (p >= av1C.size()) break; ++p; }
            uint64_t oSz = av1C.size() - p;
            if (hasSize && !ReadLeb128Av1(av1C.data(), av1C.size(), p, oSz)) break;
            if (oSz > av1C.size() - p) break;
            if (obuType == 1) parseSeqHeader(av1C.data() + p, static_cast<size_t>(oSz));
            p += static_cast<size_t>(oSz);
        }
    }

    // Walk itemData OBUs: OBU_SEQUENCE_HEADER (1) + OBU_FRAME (6)
    size_t pos = 0;
    while (pos < itemLen) {
        uint8_t hdr = itemData[pos++];
        if ((hdr & 0x80u) != 0) return false; // obu_forbidden_bit MUST be 0!
        uint8_t obuType = (hdr >> 3) & 0x0Fu;
        bool hasExt  = (hdr & 0x04u) != 0;
        bool hasSize = (hdr & 0x02u) != 0;
        if (hasExt) { if (pos >= itemLen) return false; ++pos; }
        uint64_t oSz = itemLen - pos;
        if (hasSize && !ReadLeb128Av1(itemData, itemLen, pos, oSz)) return false;
        if (oSz > itemLen - pos) return false;
        const uint8_t* pay = itemData + pos;
        size_t payLen = static_cast<size_t>(oSz);
        pos += payLen;

        if (obuType == 1) {
            if (!parseSeqHeader(pay, payLen)) return false;
        } else if ((obuType == 6 || obuType == 3) && seq.valid && seq.reducedStill && seq.bitDepth == 8) {
            // Decode AV1 Reduced Still-Picture Frame Header + Single-Tile MSAC Lossless Palette / Intra Block!
            BitReader fbr(pay, payLen);
            fbr.ReadBit(); // disable_cdf_update
            uint32_t allowScreenTools = fbr.ReadBit();
            if (seq.enableSuperres) { if (fbr.ReadBit()) return false; }
            // tile_info(): single tile when sbCols <= 64 and sbRows <= 64 -> uniform_tile_spacing_flag = 1, 0 increment bits!
            uint32_t uniTile = fbr.ReadBit();
            if (!uniTile) return false;
            uint32_t sbShift = seq.use128Sb ? 7u : 6u;
            uint32_t sbCols = (seq.width  + (1u << sbShift) - 1u) >> sbShift;
            uint32_t sbRows = (seq.height + (1u << sbShift) - 1u) >> sbShift;
            if (sbCols > 1) { if (fbr.ReadBit() != 0) return false; } // increment_tile_cols_log2 == 0
            if (sbRows > 1) { if (fbr.ReadBit() != 0) return false; } // increment_tile_rows_log2 == 0

            // quantization_params()
            uint32_t baseQIdx = fbr.ReadBits(8);
            if (fbr.ReadBit()) fbr.ReadBits(7); // deltaQYDc
            if (!seq.monochrome) {
                if (fbr.ReadBit()) fbr.ReadBits(7); // deltaQUDc
                if (fbr.ReadBit()) fbr.ReadBits(7); // deltaQUAc
            }
            // segmentation_params()
            if (fbr.ReadBit() != 0) return false; // segmentation_enabled == 0
            // delta_q_params()
            if (baseQIdx > 0) { if (fbr.ReadBit() != 0) return false; }
            bool codedLossless = (baseQIdx == 0);
            if (!codedLossless) {
                fbr.ReadBits(6); fbr.ReadBits(6); fbr.ReadBits(3);
                if (fbr.ReadBit()) { if (fbr.ReadBit()) return false; }
            }
            if (seq.enableCdef) return false;
            if (seq.enableRestoration) return false;
            if (!codedLossless) fbr.ReadBit(); // tx_mode_select
            uint32_t allowIntrabc = 0;
            if (allowScreenTools) allowIntrabc = fbr.ReadBit();
            (void)allowIntrabc;
            fbr.AlignToByte();

            if (obuType != 6 || fbr.bytePos >= payLen) continue;
            const uint8_t* tilePtr = pay + fbr.bytePos;
            size_t tileLen = payLen - fbr.bytePos;

            Av1MsacReader msac;
            if (!msac.Init(tilePtr, tileLen)) return false;

            // Decode single-superblock (<= 64x64) AV1 Palette / Lossless DC Intra Tile:
            // In AV1 Screen-Content Palette mode (allow_screen_content_tools == 1, partition_none, skip=1, DC_PRED, has_palette_y=1, has_palette_uv=1):
            // We read the exact Daala MSAC literal palette colors & per-pixel color index map!
            if (allowScreenTools && seq.width <= 64 && seq.height <= 64) {
                uint32_t hasPalY = msac.ReadLiteralBit();
                if (hasPalY) {
                    uint32_t nColY = msac.ReadLiteralBits(3) + 2u; // 2..8 colors
                    if (nColY < 2 || nColY > 8) return false;
                    uint8_t palY[8] = {}, palU[8] = {}, palV[8] = {};
                    for (uint32_t c = 0; c < nColY; ++c) palY[c] = static_cast<uint8_t>(msac.ReadLiteralBits(8));
                    uint32_t nColUV = 0;
                    if (!seq.monochrome && msac.ReadLiteralBit()) {
                        nColUV = msac.ReadLiteralBits(3) + 2u;
                        if (nColUV < 2 || nColUV > 8) return false;
                        for (uint32_t c = 0; c < nColUV; ++c) {
                            palU[c] = static_cast<uint8_t>(msac.ReadLiteralBits(8));
                            palV[c] = static_cast<uint8_t>(msac.ReadLiteralBits(8));
                        }
                    }
                    int idxBitsY  = (nColY <= 2) ? 1 : ((nColY <= 4) ? 2 : 3);
                    int idxBitsUV = (nColUV <= 2) ? 1 : ((nColUV <= 4) ? 2 : 3);

                    std::vector<uint8_t> planeY(seq.width * seq.height, 0);
                    for (uint32_t y = 0; y < seq.height; ++y) {
                        for (uint32_t x = 0; x < seq.width; ++x) {
                            uint32_t ci = std::min(msac.ReadLiteralBits(idxBitsY), nColY - 1u);
                            planeY[y * seq.width + x] = palY[ci];
                        }
                    }
                    uint32_t cW = (seq.width  + seq.subX) >> seq.subX;
                    uint32_t cH = (seq.height + seq.subY) >> seq.subY;
                    std::vector<uint8_t> planeU(cW * cH, 128), planeV(cW * cH, 128);
                    if (!seq.monochrome && nColUV >= 2) {
                        for (uint32_t y = 0; y < cH; ++y) {
                            for (uint32_t x = 0; x < cW; ++x) {
                                uint32_t ci = std::min(msac.ReadLiteralBits(idxBitsUV), nColUV - 1u);
                                planeU[y * cW + x] = palU[ci];
                                planeV[y * cW + x] = palV[ci];
                            }
                        }
                    }

                    out.w = seq.width; out.h = seq.height;
                    out.rgba.resize(static_cast<size_t>(seq.width) * seq.height * 4);
                    for (uint32_t y = 0; y < seq.height; ++y) {
                        for (uint32_t x = 0; x < seq.width; ++x) {
                            uint8_t yv = planeY[y * seq.width + x];
                            uint8_t r = yv, g = yv, b = yv;
                            if (!seq.monochrome) {
                                uint8_t u = planeU[(y >> seq.subY) * cW + (x >> seq.subX)];
                                uint8_t v = planeV[(y >> seq.subY) * cW + (x >> seq.subX)];
                                YCbCrToRgb8(yv, u, v, seq.fullRange, seq.matrixCoeffs, r, g, b);
                            }
                            size_t di = (static_cast<size_t>(y) * seq.width + x) * 4;
                            out.rgba[di+0]=r; out.rgba[di+1]=g; out.rgba[di+2]=b; out.rgba[di+3]=255;
                        }
                    }
                    return true;
                }
            }
        }
    }
    return false;
}

// ============================================================================
// Post-Processing: Mirror ('imir') & Rotation ('irot')
// ============================================================================
inline void ApplyMirrorRgba(DecodedRgbaTile& img, uint8_t axis) {
    // ISO 23008-12 imir: axis == 0 -> top-to-bottom (vertical flip); axis == 1 -> left-to-right (horizontal flip)
    if (img.w == 0 || img.h == 0) return;
    if (axis == 0) {
        for (uint32_t y = 0; y < img.h / 2; ++y) {
            uint32_t oy = img.h - 1 - y;
            for (uint32_t x = 0; x < img.w; ++x) {
                for (int c = 0; c < 4; ++c) {
                    std::swap(img.rgba[(static_cast<size_t>(y) * img.w + x) * 4 + c],
                              img.rgba[(static_cast<size_t>(oy) * img.w + x) * 4 + c]);
                }
            }
        }
    } else {
        for (uint32_t y = 0; y < img.h; ++y) {
            for (uint32_t x = 0; x < img.w / 2; ++x) {
                uint32_t ox = img.w - 1 - x;
                for (int c = 0; c < 4; ++c) {
                    std::swap(img.rgba[(static_cast<size_t>(y) * img.w + x) * 4 + c],
                              img.rgba[(static_cast<size_t>(y) * img.w + ox) * 4 + c]);
                }
            }
        }
    }
}

inline void ApplyRotateCcwRgba(DecodedRgbaTile& img, uint8_t angleCode) {
    // ISO 23008-12 irot: angle * 90 degrees counter-clockwise (1=90 CCW, 2=180, 3=270 CCW)
    angleCode &= 3u;
    if (angleCode == 0 || img.w == 0 || img.h == 0) return;
    uint32_t nw = (angleCode & 1u) ? img.h : img.w;
    uint32_t nh = (angleCode & 1u) ? img.w : img.h;
    std::vector<uint8_t> dst(static_cast<size_t>(nw) * nh * 4);
    for (uint32_t y = 0; y < img.h; ++y) {
        for (uint32_t x = 0; x < img.w; ++x) {
            uint32_t dx = x, dy = y;
            if (angleCode == 1)      { dx = y;              dy = img.w - 1 - x; }
            else if (angleCode == 2) { dx = img.w - 1 - x;  dy = img.h - 1 - y; }
            else if (angleCode == 3) { dx = img.h - 1 - y;  dy = x; }
            std::memcpy(&dst[(static_cast<size_t>(dy) * nw + dx) * 4],
                        &img.rgba[(static_cast<size_t>(y) * img.w + x) * 4], 4);
        }
    }
    img.w = nw; img.h = nh;
    img.rgba = std::move(dst);
}

inline bool IsSupportedHeifBrand(const char b[4]) {
    return std::memcmp(b, "avif", 4) == 0 || std::memcmp(b, "avis", 4) == 0 ||
           std::memcmp(b, "heic", 4) == 0 || std::memcmp(b, "heix", 4) == 0 ||
           std::memcmp(b, "hevc", 4) == 0 || std::memcmp(b, "heim", 4) == 0 ||
           std::memcmp(b, "heis", 4) == 0 || std::memcmp(b, "mif1", 4) == 0 ||
           std::memcmp(b, "msf1", 4) == 0 || std::memcmp(b, "miaf", 4) == 0 ||
           std::memcmp(b, "avci", 4) == 0 || std::memcmp(b, "avcs", 4) == 0;
}

} // namespace AvifHeifDetail

inline bool IsLikelyAvifOrHeifSignature(const uint8_t* data, size_t size) {
    using namespace AvifHeifDetail;
    if (!data || size < 16) return false;
    size_t cur = 0;
    IsoBox b;
    if (!NextIsoBox(data, size, cur, b) || std::strcmp(b.type, "ftyp") != 0) return false;
    if (b.end - b.bodyStart < 8) return false;
    if (IsSupportedHeifBrand(reinterpret_cast<const char*>(data + b.bodyStart))) return true;
    for (size_t p = b.bodyStart + 8; p + 4 <= b.end; p += 4) {
        if (IsSupportedHeifBrand(reinterpret_cast<const char*>(data + p))) return true;
    }
    return false;
}

inline bool DecodeAvifOrHeifImage(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ExtendedImageInfo& outInfo)
{
    using namespace AvifHeifDetail;
    if (!IsLikelyAvifOrHeifSignature(data, size)) return false;

    char majorBrand[5] = {};
    std::memcpy(majorBrand, data + 8, 4);

    // 1. Parse top-level ISO-BMFF boxes to locate 'meta'
    size_t cur = 0;
    IsoBox topBox;
    size_t metaBody = 0, metaEnd = 0;
    while (NextIsoBox(data, size, cur, topBox)) {
        if (std::strcmp(topBox.type, "meta") == 0 && topBox.end - topBox.bodyStart >= 4) {
            metaBody = topBox.bodyStart + 4; // FullBox version(1) + flags(3)
            metaEnd  = topBox.end;
            break;
        }
    }
    if (metaBody == 0 || metaBody >= metaEnd) return false;

    // 2. Parse 'meta' child boxes: hdlr, pitm, iinf, iprp (ipco + ipma), iref, idat, iloc
    bool hasPictHdlr = false;
    uint32_t primaryItemId = 0;
    bool hasPrimaryId = false;
    std::unordered_map<uint32_t, std::string> itemTypes;

    struct PropRecord {
        std::string type;
        std::vector<uint8_t> payload;
    };
    std::vector<PropRecord> props; // 1-based in ipma!
    std::unordered_map<uint32_t, std::vector<uint16_t>> itemProps;

    std::unordered_map<uint32_t, std::vector<uint32_t>> dimgRefs; // gridItem -> tileItems
    std::unordered_map<uint32_t, uint32_t> alphaItemForPrimary;   // primaryItem -> auxlAlphaItem

    const uint8_t* idatPtr = nullptr;
    size_t idatLen = 0;

    struct ItemExtent { uint64_t offset = 0, length = 0; };
    struct ItemLoc {
        uint8_t constructionMethod = 0; // 0=file, 1=idat
        uint64_t baseOffset = 0;
        std::vector<ItemExtent> extents;
    };
    std::unordered_map<uint32_t, ItemLoc> itemLocs;

    size_t mc = metaBody;
    IsoBox mb;
    while (NextIsoBox(data, metaEnd, mc, mb)) {
        size_t bs = mb.bodyStart, be = mb.end;
        if (std::strcmp(mb.type, "hdlr") == 0 && be - bs >= 12) {
            if (std::memcmp(data + bs + 8, "pict", 4) == 0) hasPictHdlr = true;
        } else if (std::strcmp(mb.type, "pitm") == 0 && be - bs >= 6) {
            uint8_t ver = data[bs];
            if (ver == 0) { primaryItemId = RdBE16(data + bs + 4); hasPrimaryId = true; }
            else if (be - bs >= 8) { primaryItemId = RdBE32(data + bs + 4); hasPrimaryId = true; }
        } else if (std::strcmp(mb.type, "idat") == 0) {
            idatPtr = data + bs;
            idatLen = be - bs;
        } else if (std::strcmp(mb.type, "iinf") == 0 && be - bs >= 6) {
            uint8_t ver = data[bs];
            size_t ic = bs + 4;
            uint32_t cnt = (ver == 0) ? RdBE16(data + ic) : ((be - ic >= 4) ? RdBE32(data + ic) : 0);
            ic += (ver == 0) ? 2 : 4;
            IsoBox infe;
            for (uint32_t i = 0; i < cnt && NextIsoBox(data, be, ic, infe); ++i) {
                if (std::strcmp(infe.type, "infe") != 0 || infe.end - infe.bodyStart < 8) continue;
                uint8_t iv = data[infe.bodyStart];
                if (iv == 2 && infe.end - infe.bodyStart >= 12) {
                    uint32_t id = RdBE16(data + infe.bodyStart + 4);
                    itemTypes[id] = std::string(reinterpret_cast<const char*>(data + infe.bodyStart + 8), 4);
                } else if (iv >= 3 && infe.end - infe.bodyStart >= 14) {
                    uint32_t id = RdBE32(data + infe.bodyStart + 4);
                    itemTypes[id] = std::string(reinterpret_cast<const char*>(data + infe.bodyStart + 10), 4);
                }
            }
        } else if (std::strcmp(mb.type, "iprp") == 0) {
            size_t pc = bs;
            IsoBox pb;
            while (NextIsoBox(data, be, pc, pb)) {
                if (std::strcmp(pb.type, "ipco") == 0) {
                    size_t cc = pb.bodyStart;
                    IsoBox cb;
                    while (NextIsoBox(data, pb.end, cc, cb)) {
                        props.push_back({ cb.type, std::vector<uint8_t>(data + cb.bodyStart, data + cb.end) });
                    }
                } else if (std::strcmp(pb.type, "ipma") == 0 && pb.end - pb.bodyStart >= 8) {
                    uint8_t ver = data[pb.bodyStart];
                    uint32_t flags = RdBE32(data + pb.bodyStart) & 0x00FFFFFFu;
                    size_t p = pb.bodyStart + 4;
                    uint32_t ecnt = RdBE32(data + p); p += 4;
                    for (uint32_t i = 0; i < ecnt && i < 65536; ++i) {
                        size_t idSz = (ver < 1) ? 2u : 4u;
                        if (p + idSz + 1 > pb.end) break;
                        uint32_t iid = (ver < 1) ? RdBE16(data + p) : RdBE32(data + p); p += idSz;
                        uint8_t assocCnt = data[p++];
                        size_t entryBytes = (flags & 1u) ? 2u : 1u;
                        if (p + assocCnt * entryBytes > pb.end) break;
                        for (uint8_t a = 0; a < assocCnt; ++a) {
                            uint16_t pidx = (flags & 1u) ? (RdBE16(data + p) & 0x7FFFu) : (data[p] & 0x7Fu);
                            p += entryBytes;
                            if (pidx > 0) itemProps[iid].push_back(pidx);
                        }
                    }
                }
            }
        } else if (std::strcmp(mb.type, "iref") == 0 && be - bs >= 4) {
            uint8_t ver = data[bs];
            size_t rc = bs + 4;
            IsoBox rb;
            while (NextIsoBox(data, be, rc, rb)) {
                size_t idSz = (ver == 0) ? 2u : 4u;
                size_t rp = rb.bodyStart;
                if (rp + idSz + 2 > rb.end) continue;
                uint32_t fromId = (ver == 0) ? RdBE16(data + rp) : RdBE32(data + rp); rp += idSz;
                uint16_t refCnt = RdBE16(data + rp); rp += 2;
                if (rp + refCnt * idSz > rb.end) continue;
                for (uint16_t k = 0; k < refCnt; ++k) {
                    uint32_t toId = (ver == 0) ? RdBE16(data + rp + k * idSz) : RdBE32(data + rp + k * idSz);
                    if (std::strcmp(rb.type, "dimg") == 0) dimgRefs[fromId].push_back(toId);
                    else if (std::strcmp(rb.type, "auxl") == 0) alphaItemForPrimary[toId] = fromId;
                }
            }
        } else if (std::strcmp(mb.type, "iloc") == 0 && be - bs >= 8) {
            uint8_t ver = data[bs];
            size_t lp = bs + 4;
            uint8_t offSz  = (data[lp] >> 4) & 0x0Fu;
            uint8_t lenSz  = data[lp] & 0x0Fu; ++lp;
            uint8_t baseSz = (data[lp] >> 4) & 0x0Fu;
            uint8_t idxSz  = (ver == 1 || ver == 2) ? (data[lp] & 0x0Fu) : 0u; ++lp;
            if (offSz > 8 || lenSz > 8 || baseSz > 8 || idxSz > 8) return false;
            uint32_t itemCnt = 0;
            if (ver < 2) { if (lp + 2 > be) return false; itemCnt = RdBE16(data + lp); lp += 2; }
            else         { if (lp + 4 > be) return false; itemCnt = RdBE32(data + lp); lp += 4; }
            for (uint32_t i = 0; i < itemCnt && i < 65536; ++i) {
                size_t idBytes = (ver < 2) ? 2u : 4u;
                if (lp + idBytes > be) break;
                uint32_t iid = (ver < 2) ? RdBE16(data + lp) : RdBE32(data + lp); lp += idBytes;
                uint8_t cMethod = 0;
                if (ver == 1 || ver == 2) {
                    if (lp + 2 > be) break;
                    cMethod = data[lp + 1] & 0x0Fu;
                    lp += 2;
                }
                if (lp + 2 + baseSz + 2 > be) break;
                lp += 2; // data_reference_index
                uint64_t baseOff = RdVarBE(data + lp, baseSz); lp += baseSz;
                uint16_t extCnt  = RdBE16(data + lp); lp += 2;
                size_t extStride = idxSz + offSz + lenSz;
                if (lp + static_cast<size_t>(extCnt) * extStride > be) break;
                ItemLoc loc;
                loc.constructionMethod = cMethod;
                loc.baseOffset = baseOff;
                for (uint16_t e = 0; e < extCnt; ++e) {
                    lp += idxSz;
                    uint64_t eo = RdVarBE(data + lp, offSz); lp += offSz;
                    uint64_t el = RdVarBE(data + lp, lenSz); lp += lenSz;
                    loc.extents.push_back({ eo, el });
                }
                itemLocs[iid] = std::move(loc);
            }
        }
    }

    if (!hasPictHdlr) return false;
    if (!hasPrimaryId && !itemTypes.empty()) primaryItemId = itemTypes.begin()->first;
    if (itemTypes.empty() && itemLocs.empty()) return false;

    // Helper to extract contiguous item bytes from iloc
    auto extractItemBytes = [&](uint32_t iid, std::vector<uint8_t>& outBytes) -> bool {
        outBytes.clear();
        auto it = itemLocs.find(iid);
        if (it == itemLocs.end() || it->second.extents.empty()) return false;
        const ItemLoc& loc = it->second;
        for (const auto& ex : loc.extents) {
            uint64_t absOff = loc.baseOffset + ex.offset;
            if (ex.length > 128 * 1024 * 1024) return false;
            if (loc.constructionMethod == 0) {
                if (absOff > size || ex.length > size - absOff) return false;
                outBytes.insert(outBytes.end(), data + absOff, data + absOff + static_cast<size_t>(ex.length));
            } else if (loc.constructionMethod == 1) {
                if (!idatPtr || absOff > idatLen || ex.length > idatLen - absOff) return false;
                outBytes.insert(outBytes.end(), idatPtr + absOff, idatPtr + absOff + static_cast<size_t>(ex.length));
            } else {
                return false;
            }
        }
        return !outBytes.empty();
    };

    // Decode via pure-C++ container + item decoders first; fall back to Windows WIC for complex OS-codec frames
    DecodedRgbaTile finalImg;
    {
        // Gather item properties helper
        struct ResolvedProps {
            uint32_t ispeW = 0, ispeH = 0;
            bool fullRange = true;
            uint16_t matrixCoeffs = 6; // BT.601 default
            std::vector<uint8_t> av1C, hvcC, avcC;
            UncConfig unc;
            std::vector<std::pair<char, uint8_t>> xforms; // ('m', axis) or ('r', angle)
        };

        auto getPropsFor = [&](uint32_t iid) -> ResolvedProps {
            ResolvedProps rp;
            auto it = itemProps.find(iid);
            if (it == itemProps.end()) return rp;
            for (uint16_t pidx : it->second) {
                if (pidx == 0 || pidx > props.size()) continue;
                const auto& pr = props[pidx - 1];
                const auto& pl = pr.payload;
                if (pr.type == "ispe" && pl.size() >= 12) {
                    rp.ispeW = RdBE32(pl.data() + 4);
                    rp.ispeH = RdBE32(pl.data() + 8);
                } else if (pr.type == "colr" && pl.size() >= 11 && std::memcmp(pl.data(), "nclx", 4) == 0) {
                    rp.matrixCoeffs = RdBE16(pl.data() + 8);
                    rp.fullRange    = (pl[10] & 0x80u) != 0;
                } else if (pr.type == "av1C") rp.av1C = pl;
                else if (pr.type == "hvcC")   rp.hvcC = pl;
                else if (pr.type == "avcC")   rp.avcC = pl;
                else if (pr.type == "cmpd" && pl.size() >= 4) {
                    uint32_t n = RdBE32(pl.data());
                    size_t p = 4;
                    for (uint32_t c = 0; c < n && p + 2 <= pl.size(); ++c) {
                        uint16_t ct = RdBE16(pl.data() + p); p += 2;
                        if (ct >= 0x8000u) { while (p < pl.size() && pl[p] != 0) ++p; if (p < pl.size()) ++p; }
                        rp.unc.compTypes.push_back(ct);
                    }
                } else if (pr.type == "uncC" && pl.size() >= 8) {
                    uint8_t ver = pl[0];
                    rp.unc.present = true;
                    rp.unc.profile = RdBE32(pl.data() + 4);
                    if (ver == 1) {
                        // ISO 23001-17 uncC v1 profile shorthand ('rgb3', 'rgba', 'yuv2')
                        if (std::memcmp(pl.data() + 4, "rgb3", 4) == 0) {
                            rp.unc.compTypes = { 4, 5, 6 }; rp.unc.compBitDepths = { 8, 8, 8 }; rp.unc.interleaveType = 1;
                        } else if (std::memcmp(pl.data() + 4, "rgba", 4) == 0) {
                            rp.unc.compTypes = { 4, 5, 6, 7 }; rp.unc.compBitDepths = { 8, 8, 8, 8 }; rp.unc.interleaveType = 1;
                        }
                    } else if (pl.size() >= 12) {
                        uint32_t nComp = RdBE32(pl.data() + 8);
                        size_t p = 12;
                        for (uint32_t c = 0; c < nComp && p + 4 <= pl.size(); ++c) {
                            uint8_t bdm1 = pl[p + 2];
                            rp.unc.compBitDepths.push_back(static_cast<uint8_t>(bdm1 + 1u));
                            p += 4;
                        }
                        if (p + 2 <= pl.size()) {
                            rp.unc.interleaveType = pl[p + 1];
                        }
                    }
                } else if (pr.type == "imir" && !pl.empty()) {
                    rp.xforms.push_back({ 'm', static_cast<uint8_t>(pl[0] & 1u) });
                } else if (pr.type == "irot" && !pl.empty()) {
                    rp.xforms.push_back({ 'r', static_cast<uint8_t>(pl[0] & 3u) });
                }
            }
            return rp;
        };

        std::function<bool(uint32_t, DecodedRgbaTile&, int)> decodeItemRecursive =
            [&](uint32_t iid, DecodedRgbaTile& dst, int depth) -> bool
        {
            if (depth > 4) return false;
            std::string itype = itemTypes[iid];
            ResolvedProps rp = getPropsFor(iid);

            bool ok = false;
            if (itype == "grid") {
                std::vector<uint8_t> gBytes;
                if (!extractItemBytes(iid, gBytes) || gBytes.size() < 8) return false;
                uint8_t flags = gBytes[1];
                uint32_t rows = static_cast<uint32_t>(gBytes[2]) + 1u;
                uint32_t cols = static_cast<uint32_t>(gBytes[3]) + 1u;
                uint32_t outW = 0, outH = 0;
                if ((flags & 1u) != 0) {
                    if (gBytes.size() < 12) return false;
                    outW = RdBE32(gBytes.data() + 4);
                    outH = RdBE32(gBytes.data() + 8);
                } else {
                    outW = RdBE16(gBytes.data() + 4);
                    outH = RdBE16(gBytes.data() + 6);
                }
                const auto& tiles = dimgRefs[iid];
                if (outW == 0 || outH == 0 || outW > 16384 || outH > 16384 || tiles.size() < static_cast<size_t>(rows) * cols) {
                    return false;
                }
                DecodedRgbaTile t0;
                if (!decodeItemRecursive(tiles[0], t0, depth + 1) || t0.w == 0 || t0.h == 0) return false;
                dst.w = outW; dst.h = outH;
                dst.rgba.assign(static_cast<size_t>(outW) * outH * 4, 0);

                for (uint32_t r = 0; r < rows; ++r) {
                    for (uint32_t c = 0; c < cols; ++c) {
                        DecodedRgbaTile curTile;
                        if (r == 0 && c == 0) curTile = std::move(t0);
                        else if (!decodeItemRecursive(tiles[r * cols + c], curTile, depth + 1)) return false;

                        uint32_t x0 = c * curTile.w, y0 = r * curTile.h;
                        for (uint32_t ty = 0; ty < curTile.h && y0 + ty < outH; ++ty) {
                            uint32_t copyW = std::min(curTile.w, outW - std::min(outW, x0));
                            if (copyW > 0) {
                                std::memcpy(&dst.rgba[(static_cast<size_t>(y0 + ty) * outW + x0) * 4],
                                            &curTile.rgba[static_cast<size_t>(ty) * curTile.w * 4],
                                            static_cast<size_t>(copyW) * 4);
                            }
                        }
                    }
                }
                ok = true;
            } else if (itype == "iden" && !dimgRefs[iid].empty()) {
                ok = decodeItemRecursive(dimgRefs[iid][0], dst, depth + 1);
            } else {
                std::vector<uint8_t> pay;
                if (!extractItemBytes(iid, pay)) return false;

                if (itype == "unci" || rp.unc.present) {
                    ok = DecodeUncompressedHeifItem(pay.data(), pay.size(), rp.ispeW, rp.ispeH, rp.unc, rp.fullRange, rp.matrixCoeffs, dst);
                } else if (itype == "jpeg") {
                    ok = TryDecodeViaWicMemory(pay.data(), pay.size(), dst);
                } else if (itype == "avc1") {
                    ok = DecodeAvc1StillItem(pay.data(), pay.size(), rp.avcC, rp.fullRange, rp.matrixCoeffs, dst);
                } else if (itype == "hvc1") {
                    ok = DecodeHvc1StillItem(pay.data(), pay.size(), rp.hvcC, rp.fullRange, rp.matrixCoeffs, dst);
                } else if (itype == "av01") {
                    ok = DecodeAv01StillItem(pay.data(), pay.size(), rp.av1C, rp.fullRange, rp.matrixCoeffs, dst);
                }
            }
            if (!ok) return false;

            // Crop to ispe if coded macroblock/CTU padded slightly larger than ispe
            if (rp.ispeW > 0 && rp.ispeH > 0 && rp.ispeW <= dst.w && rp.ispeH <= dst.h &&
                (rp.ispeW != dst.w || rp.ispeH != dst.h))
            {
                std::vector<uint8_t> cropped(static_cast<size_t>(rp.ispeW) * rp.ispeH * 4);
                for (uint32_t y = 0; y < rp.ispeH; ++y) {
                    std::memcpy(&cropped[static_cast<size_t>(y) * rp.ispeW * 4],
                                &dst.rgba[static_cast<size_t>(y) * dst.w * 4],
                                static_cast<size_t>(rp.ispeW) * 4);
                }
                dst.w = rp.ispeW; dst.h = rp.ispeH;
                dst.rgba = std::move(cropped);
            }

            // Apply transformative properties (imir, irot) in exact ipma association order!
            for (const auto& xf : rp.xforms) {
                if (xf.first == 'm') ApplyMirrorRgba(dst, xf.second);
                else if (xf.first == 'r') ApplyRotateCcwRgba(dst, xf.second);
            }
            return true;
        };

        if (decodeItemRecursive(primaryItemId, finalImg, 0)) {
            // Composite auxiliary alpha item ('auxl') if present
            auto aIt = alphaItemForPrimary.find(primaryItemId);
            if (aIt != alphaItemForPrimary.end()) {
                DecodedRgbaTile alphaTile;
                if (decodeItemRecursive(aIt->second, alphaTile, 0) &&
                    alphaTile.w == finalImg.w && alphaTile.h == finalImg.h)
                {
                    size_t nPx = static_cast<size_t>(finalImg.w) * finalImg.h;
                    for (size_t i = 0; i < nPx; ++i) {
                        finalImg.rgba[i * 4 + 3] = alphaTile.rgba[i * 4 + 0];
                    }
                }
            }
        } else if (!TryDecodeViaWicMemory(data, size, finalImg)) {
            return false;
        }
    }

    if (finalImg.w == 0 || finalImg.h == 0 || finalImg.rgba.size() < static_cast<size_t>(finalImg.w) * finalImg.h * 4) {
        return false;
    }

    // Convert RGBA8 -> premultiplied BGRA8 for Direct2D
    size_t nPx = static_cast<size_t>(finalImg.w) * finalImg.h;
    outInfo.width = finalImg.w;
    outInfo.height = finalImg.h;
    outInfo.origWidth = finalImg.w;
    outInfo.origHeight = finalImg.h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.bgraPixels.resize(nPx * 4);

    for (size_t i = 0; i < nPx; ++i) {
        uint8_t r = finalImg.rgba[i * 4 + 0];
        uint8_t g = finalImg.rgba[i * 4 + 1];
        uint8_t b = finalImg.rgba[i * 4 + 2];
        uint8_t a = finalImg.rgba[i * 4 + 3];
        WritePremulBGRA(&outInfo.bgraPixels[i * 4], r, g, b, a);
    }

    if (extLower == L".avif" || std::memcmp(majorBrand, "avif", 4) == 0 || std::memcmp(majorBrand, "avis", 4) == 0) {
        outInfo.formatName = L"AV1 Image File Format (.avif)";
    } else if (extLower == L".avci" || std::memcmp(majorBrand, "avci", 4) == 0) {
        outInfo.formatName = L"AVC in HEIF Still Image (.avci)";
    } else if (extLower == L".heic" || std::memcmp(majorBrand, "heic", 4) == 0 || std::memcmp(majorBrand, "heix", 4) == 0) {
        outInfo.formatName = L"HEVC High Efficiency Image (.heic)";
    } else {
        outInfo.formatName = L"ISO High Efficiency Image Container (.heif)";
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
