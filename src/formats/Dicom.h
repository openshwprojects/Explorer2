#pragma once
// Dicom.h — DICOM PS3.5 / PS3.10 Medical Imaging (.dcm / .dicom) Decoder

namespace Formats {

namespace DetailDicom {

using ::Explorer2::ByteIO::RdU16; // (p, bigEndian): ByteIO.h

using ::Explorer2::ByteIO::RdU32;

using ::Explorer2::ByteIO::RdU64;

inline void StoreBgraOpaque(uint8_t* dst, uint8_t r, uint8_t g, uint8_t b) {
    dst[0] = b;
    dst[1] = g;
    dst[2] = r;
    dst[3] = 255;
}

// Exact 34 standard 2-character DICOM PS3.5 Value Representations
inline bool IsStandardVR(char c0, char c1) {
    static const char kVRs[34][2] = {
        {'A','E'}, {'A','S'}, {'A','T'}, {'C','S'}, {'D','A'}, {'D','S'}, {'D','T'},
        {'F','L'}, {'F','D'}, {'I','S'}, {'L','O'}, {'L','T'}, {'O','B'}, {'O','D'},
        {'O','F'}, {'O','L'}, {'O','V'}, {'O','W'}, {'P','N'}, {'S','H'}, {'S','L'},
        {'S','Q'}, {'S','S'}, {'S','T'}, {'S','V'}, {'T','M'}, {'U','C'}, {'U','I'},
        {'U','L'}, {'U','N'}, {'U','R'}, {'U','S'}, {'U','T'}, {'U','V'}
    };
    for (const auto& vr : kVRs) {
        if (vr[0] == c0 && vr[1] == c1) return true;
    }
    return false;
}

// Explicit VRs with 2-byte reserved + 4-byte uint32 length (12-byte tag header)
inline bool IsLongExplicitVR(char c0, char c1) {
    if (c0 == 'O') {
        return c1 == 'B' || c1 == 'D' || c1 == 'F' || c1 == 'L' || c1 == 'V' || c1 == 'W';
    }
    if (c0 == 'S') {
        return c1 == 'Q' || c1 == 'V';
    }
    if (c0 == 'U') {
        return c1 == 'C' || c1 == 'N' || c1 == 'R' || c1 == 'T' || c1 == 'V';
    }
    return false;
}

inline std::string TrimDicomAscii(const uint8_t* p, size_t n) {
    size_t s = 0;
    while (s < n && (p[s] == ' ' || p[s] == '\t' || p[s] == '\r' || p[s] == '\n' || p[s] == '\0')) ++s;
    size_t e = n;
    while (e > s && (p[e - 1] == ' ' || p[e - 1] == '\t' || p[e - 1] == '\r' || p[e - 1] == '\n' || p[e - 1] == '\0')) --e;
    return std::string(reinterpret_cast<const char*>(p + s), e - s);
}

inline bool ParseDicomDouble(const uint8_t* p, size_t n, char vr0, char vr1, bool be, double& outVal) {
    if (vr0 == 'F' && vr1 == 'D' && n >= 8) {
        uint64_t u = RdU64(p, be);
        double d = 0.0;
        std::memcpy(&d, &u, 8);
        if (!std::isfinite(d)) return false;
        outVal = d;
        return true;
    }
    if (vr0 == 'F' && vr1 == 'L' && n >= 4) {
        uint32_t u = RdU32(p, be);
        float f = 0.0f;
        std::memcpy(&f, &u, 4);
        if (!std::isfinite(f)) return false;
        outVal = static_cast<double>(f);
        return true;
    }
    if (n == 0) return false;
    size_t lim = std::min<size_t>(n, 64);
    char buf[68] = {};
    size_t w = 0;
    for (size_t i = 0; i < lim; ++i) {
        char ch = static_cast<char>(p[i]);
        if (ch == '\0' || ch == '\\') break; // First value of multi-valued DS
        buf[w++] = ch;
    }
    buf[w] = '\0';
    char* endPtr = nullptr;
    double v = std::strtod(buf, &endPtr);
    if (endPtr == buf || !std::isfinite(v)) return false;
    outVal = v;
    return true;
}

inline uint32_t ParseDicomUInt(const uint8_t* p, size_t n, char vr0, char vr1, bool be, uint32_t defVal = 0) {
    if (vr0 == 'I' && vr1 == 'S') {
        std::string s = TrimDicomAscii(p, std::min<size_t>(n, 32));
        if (s.empty()) return defVal;
        char* endPtr = nullptr;
        long v = std::strtol(s.c_str(), &endPtr, 10);
        return (endPtr != s.c_str() && v >= 0) ? static_cast<uint32_t>(v) : defVal;
    }
    if ((vr0 == 'U' && vr1 == 'L') || (vr0 == 'S' && vr1 == 'L')) {
        return (n >= 4) ? RdU32(p, be) : defVal;
    }
    if (n >= 2) return RdU16(p, be);
    if (n == 1) return p[0];
    return defVal;
}

inline bool ReadElementHeader(const uint8_t* data, size_t size, size_t& pos,
                              bool explicitVr, bool be,
                              uint16_t& outGrp, uint16_t& outElem,
                              char& outVr0, char& outVr1, uint32_t& outLen)
{
    if (pos + 8 > size) return false;
    outGrp = RdU16(data + pos, be);
    outElem = RdU16(data + pos + 2, be);
    outVr0 = 0;
    outVr1 = 0;

    // Group FFFE Item/Delimiter tags NEVER carry VR bytes even in Explicit VR
    if (outGrp == 0xFFFEu) {
        outLen = RdU32(data + pos + 4, be);
        pos += 8;
        return true;
    }

    if (explicitVr) {
        outVr0 = static_cast<char>(data[pos + 4]);
        outVr1 = static_cast<char>(data[pos + 5]);
        if (IsLongExplicitVR(outVr0, outVr1)) {
            if (pos + 12 > size) return false;
            outLen = RdU32(data + pos + 8, be);
            pos += 12;
        } else {
            outLen = RdU16(data + pos + 6, be);
            pos += 8;
        }
    } else {
        outLen = RdU32(data + pos + 4, be);
        pos += 8;
    }
    return true;
}

// Recursively skip Undefined-Length SQ / UN / OB elements via (FFFE,E000) Items -> (FFFE,E0DD)
inline bool SkipUndefinedLengthElement(const uint8_t* data, size_t size, size_t& pos,
                                       bool explicitVr, bool be, int depth)
{
    if (depth > 16) return false;
    while (pos + 8 <= size) {
        uint16_t iGrp = RdU16(data + pos, be);
        uint16_t iElem = RdU16(data + pos + 2, be);
        uint32_t iLen = RdU32(data + pos + 4, be);
        pos += 8;

        if (iGrp != 0xFFFEu) return false;
        if (iElem == 0xE0DDu) { // Sequence Delimitation Item
            if (iLen != 0 && iLen != 0xFFFFFFFFu) {
                if (iLen > size - pos) return false;
                pos += iLen;
            }
            return true;
        }
        if (iElem != 0xE000u) return false; // Must be Item tag (FFFE,E000)

        if (iLen != 0xFFFFFFFFu) {
            if (iLen > size - pos) return false;
            pos += iLen;
        } else {
            // Undefined-Length Item: walk nested dataset elements until (FFFE,E00D) Item Delimitation
            bool closedItem = false;
            while (pos + 8 <= size) {
                uint16_t g = 0, e = 0;
                char v0 = 0, v1 = 0;
                uint32_t vl = 0;
                if (!ReadElementHeader(data, size, pos, explicitVr, be, g, e, v0, v1, vl)) return false;
                if (g == 0xFFFEu) {
                    if (e == 0xE00Du) {
                        closedItem = true;
                        break;
                    }
                    return false;
                }
                if (vl == 0xFFFFFFFFu) {
                    if (!SkipUndefinedLengthElement(data, size, pos, explicitVr, be, depth + 1)) return false;
                } else {
                    if (vl > size - pos) return false;
                    pos += vl;
                }
            }
            if (!closedItem) return false;
        }
    }
    return false;
}

inline bool DecodeWicFragment(const uint8_t* fragData, size_t fragSize, ExtendedImageInfo& outInfo) {
#ifndef _WIN32
    return false;   // the Linux build of the readers has no Windows Imaging Component (WIC)
#else
    if (!fragData || fragSize < 8 || fragSize > 0x7FFFFFFFu) return false;
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool needUninit = (hrCo == S_OK || hrCo == S_FALSE);

    bool ok = false;
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr) && factory) {
        hr = factory->CreateStream(&stream);
        if (SUCCEEDED(hr) && stream) {
            hr = stream->InitializeFromMemory(const_cast<BYTE*>(fragData), static_cast<DWORD>(fragSize));
        }
        if (SUCCEEDED(hr)) {
            hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
        }
        if (SUCCEEDED(hr) && decoder) {
            hr = decoder->GetFrame(0, &frame);
        }
        UINT w = 0, h = 0;
        if (SUCCEEDED(hr) && frame) {
            hr = frame->GetSize(&w, &h);
            if (SUCCEEDED(hr) && (w == 0 || h == 0 || w > kMaxRasterDimension || h > kMaxRasterDimension)) {
                hr = E_FAIL;
            }
        }
        if (SUCCEEDED(hr) && frame) {
            hr = factory->CreateFormatConverter(&converter);
        }
        if (SUCCEEDED(hr) && converter) {
            hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                       WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        }
        if (SUCCEEDED(hr) && converter) {
            uint64_t totalBytes = static_cast<uint64_t>(w) * static_cast<uint64_t>(h) * 4ull;
            if (totalBytes <= 0x7FFFFFFFull) {
                outInfo.bgraPixels.resize(static_cast<size_t>(totalBytes));
                hr = converter->CopyPixels(nullptr, w * 4u, static_cast<UINT>(totalBytes), outInfo.bgraPixels.data());
                if (SUCCEEDED(hr)) {
                    outInfo.width = w;
                    outInfo.height = h;
                    outInfo.channels = 4;
                    outInfo.bitsPerChannel = 8;
                    outInfo.formatName = L"DICOM Medical Image (.dcm)";
                    outInfo.valid = true;
                    ok = true;
                }
            }
        }
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    if (needUninit) CoUninitialize();
    return ok;
#endif
}

// Decompress DICOM PS3.5 Annex G RLE Lossless frame into Little-Endian pixel-interleaved native buffer
inline bool DecompressDicomRleFrame(const uint8_t* frag, size_t fragSize,
                                    uint32_t rows, uint32_t cols,
                                    uint32_t samplesPerPixel, uint32_t bytesPerSample,
                                    std::vector<uint8_t>& outNativeLE)
{
    if (fragSize < 64 || samplesPerPixel == 0 || bytesPerSample == 0) return false;
    uint32_t expectedSegs = samplesPerPixel * bytesPerSample;
    if (expectedSegs == 0 || expectedSegs > 15) return false;

    uint32_t numSegments = RdU32(frag, false);
    if (numSegments != expectedSegs) return false;

    uint32_t segOffsets[15] = {};
    for (uint32_t s = 0; s < numSegments; ++s) {
        segOffsets[s] = RdU32(frag + 4 + s * 4, false);
        if (segOffsets[s] < 64 || segOffsets[s] > fragSize) return false;
        if (s > 0 && segOffsets[s] < segOffsets[s - 1]) return false;
    }

    size_t numPixels = static_cast<size_t>(rows) * static_cast<size_t>(cols);
    size_t pixelStride = static_cast<size_t>(samplesPerPixel) * bytesPerSample;
    outNativeLE.assign(numPixels * pixelStride, 0);

    std::vector<uint8_t> plane(numPixels);
    for (uint32_t s = 0; s < numSegments; ++s) {
        size_t p = segOffsets[s];
        size_t segEnd = (s + 1 < numSegments) ? static_cast<size_t>(segOffsets[s + 1]) : fragSize;
        size_t outPos = 0;

        while (p < segEnd && outPos < numPixels) {
            int n = static_cast<int>(static_cast<int8_t>(frag[p++]));
            if (n >= 0) {
                size_t count = static_cast<size_t>(n) + 1;
                if (count > segEnd - p || count > numPixels - outPos) return false;
                std::memcpy(plane.data() + outPos, frag + p, count);
                p += count;
                outPos += count;
            } else if (n != -128) {
                if (p >= segEnd) return false;
                uint8_t val = frag[p++];
                size_t count = static_cast<size_t>(1 - n);
                if (count > numPixels - outPos) return false;
                std::memset(plane.data() + outPos, val, count);
                outPos += count;
            }
        }
        if (outPos != numPixels) return false;

        // Segment s = channel c * bytesPerSample + bytePlane b (b=0 is MSB, b=bytesPerSample-1 is LSB)
        uint32_t c = s / bytesPerSample;
        uint32_t b = s % bytesPerSample;
        uint32_t leByteIdx = (bytesPerSample - 1u) - b;
        size_t dstBase = static_cast<size_t>(c) * bytesPerSample + leByteIdx;
        for (size_t i = 0; i < numPixels; ++i) {
            outNativeLE[i * pixelStride + dstBase] = plane[i];
        }
    }
    return true;
}

struct DicomLut {
    uint32_t numEntries = 0;
    int32_t firstMapped = 0;
    uint16_t bitsPerEntry = 0;
    std::vector<uint8_t> data;
    bool bigEndian = false;

    uint8_t Lookup(int32_t pixelVal) const {
        if (data.empty() || numEntries == 0) return 0;
        int32_t idx = pixelVal - firstMapped;
        if (idx < 0) idx = 0;
        if (static_cast<uint32_t>(idx) >= numEntries) idx = static_cast<int32_t>(numEntries - 1);
        if (data.size() >= static_cast<size_t>(numEntries) * 2) {
            uint16_t v = RdU16(data.data() + static_cast<size_t>(idx) * 2, bigEndian);
            return (bitsPerEntry > 8 || v > 255u) ? static_cast<uint8_t>(v >> 8) : static_cast<uint8_t>(v & 0xFFu);
        }
        if (static_cast<size_t>(idx) < data.size()) {
            return data[static_cast<size_t>(idx)];
        }
        return 0;
    }
};

} // namespace DetailDicom

inline bool DecodeDICOM(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    using namespace DetailDicom;
    if (!data || size < 32) return false;

    size_t pos = 0;
    if (size >= 140 && std::memcmp(data + 128, "DICM", 4) == 0) {
        pos = 132;
    } else {
        // Strict legacy ACR-NEMA check (no 128-byte preamble): must begin with valid Group 0008 tag
        uint16_t g0 = RdU16(data, false);
        uint16_t e0 = RdU16(data + 2, false);
        if (g0 != 0x0008u || e0 > 0x0060u) return false;
        bool exp0 = IsStandardVR(static_cast<char>(data[4]), static_cast<char>(data[5]));
        if (!exp0) {
            uint32_t l0 = RdU32(data + 4, false);
            if ((e0 == 0x0000u && l0 != 4u) || l0 > 128u || 8u + l0 > size) return false;
        }
        pos = 0;
    }

    bool hasTransferSyntax = false;
    bool datasetVrLocked = false;
    bool datasetExplicitVr = true;
    bool datasetBigEndian = false;
    bool isRleLossless = false;
    bool isEncapsulatedCompressed = false;
    bool inMetaGroup = true;

    uint32_t samplesPerPixel = 1;
    std::string photometric = "MONOCHROME2";
    uint32_t planarConfig = 0;
    uint32_t numFrames = 1;
    uint32_t rows = 0;
    uint32_t cols = 0;
    uint32_t bitsAllocated = 8;
    uint32_t bitsStored = 0;
    uint32_t highBit = 0xFFFFu;
    uint32_t pixelRep = 0;

    bool hasWinCenter = false;
    bool hasWinWidth = false;
    double winCenter = 0.0;
    double winWidth = 0.0;
    double rescaleIntercept = 0.0;
    double rescaleSlope = 1.0;
    bool hasRescale = false;

    DicomLut lutR, lutG, lutB;

    const uint8_t* nativePixelPtr = nullptr;
    size_t nativePixelBytes = 0;
    bool foundPixelData = false;
    bool pixelDataEncapsulated = false;
    size_t encapStartPos = 0;

    while (pos + 8 <= size) {
        if (inMetaGroup && RdU16(data + pos, false) != 0x0002u) {
            inMetaGroup = false;
            if (!hasTransferSyntax && !datasetVrLocked) {
                char c0 = static_cast<char>(data[pos + 4]);
                char c1 = static_cast<char>(data[pos + 5]);
                datasetExplicitVr = IsStandardVR(c0, c1);
                datasetBigEndian = false;
                datasetVrLocked = true;
            }
        }

        bool curExplicit = inMetaGroup ? true : datasetExplicitVr;
        bool curBe = inMetaGroup ? false : datasetBigEndian;

        uint16_t grp = 0, elem = 0;
        char vr0 = 0, vr1 = 0;
        uint32_t vLen = 0;
        if (!ReadElementHeader(data, size, pos, curExplicit, curBe, grp, elem, vr0, vr1, vLen)) {
            return false;
        }

        if (grp == 0x7FE0u && elem == 0x0010u) {
            foundPixelData = true;
            if (vLen == 0xFFFFFFFFu) {
                pixelDataEncapsulated = true;
                encapStartPos = pos;
            } else {
                if (vLen > size - pos) return false;
                nativePixelPtr = data + pos;
                nativePixelBytes = vLen;
            }
            break;
        }

        if (vLen == 0xFFFFFFFFu) {
            if (!SkipUndefinedLengthElement(data, size, pos, curExplicit, curBe, 0)) {
                return false;
            }
            continue;
        }

        if (vLen > size - pos) return false;
        const uint8_t* valPtr = data + pos;

        if (grp == 0x0002u && elem == 0x0010u) {
            std::string tsuid = TrimDicomAscii(valPtr, vLen);
            hasTransferSyntax = true;
            datasetVrLocked = true;
            if (tsuid == "1.2.840.10008.1.2") {
                datasetExplicitVr = false;
                datasetBigEndian = false;
            } else if (tsuid == "1.2.840.10008.1.2.2") {
                datasetExplicitVr = true;
                datasetBigEndian = true;
            } else if (tsuid == "1.2.840.10008.1.2.5") {
                datasetExplicitVr = true;
                datasetBigEndian = false;
                isRleLossless = true;
            } else if (tsuid.rfind("1.2.840.10008.1.2.4.", 0) == 0) {
                datasetExplicitVr = true;
                datasetBigEndian = false;
                isEncapsulatedCompressed = true;
            } else {
                datasetExplicitVr = true;
                datasetBigEndian = false;
            }
        } else if (grp == 0x0028u) {
            switch (elem) {
                case 0x0002u: samplesPerPixel = ParseDicomUInt(valPtr, vLen, vr0, vr1, curBe, 1); break;
                case 0x0004u: {
                    photometric = TrimDicomAscii(valPtr, vLen);
                    for (char& ch : photometric) {
                        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
                    }
                    break;
                }
                case 0x0006u: planarConfig = ParseDicomUInt(valPtr, vLen, vr0, vr1, curBe, 0); break;
                case 0x0008u: numFrames = std::max<uint32_t>(1u, ParseDicomUInt(valPtr, vLen, 'I', 'S', curBe, 1)); break;
                case 0x0010u: rows = ParseDicomUInt(valPtr, vLen, vr0, vr1, curBe, 0); break;
                case 0x0011u: cols = ParseDicomUInt(valPtr, vLen, vr0, vr1, curBe, 0); break;
                case 0x0100u: bitsAllocated = ParseDicomUInt(valPtr, vLen, vr0, vr1, curBe, 8); break;
                case 0x0101u: bitsStored = ParseDicomUInt(valPtr, vLen, vr0, vr1, curBe, 0); break;
                case 0x0102u: highBit = ParseDicomUInt(valPtr, vLen, vr0, vr1, curBe, 0xFFFFu); break;
                case 0x0103u: pixelRep = ParseDicomUInt(valPtr, vLen, vr0, vr1, curBe, 0); break;
                case 0x1050u: if (ParseDicomDouble(valPtr, vLen, vr0, vr1, curBe, winCenter)) hasWinCenter = true; break;
                case 0x1051u: if (ParseDicomDouble(valPtr, vLen, vr0, vr1, curBe, winWidth)) hasWinWidth = true; break;
                case 0x1052u: if (ParseDicomDouble(valPtr, vLen, vr0, vr1, curBe, rescaleIntercept)) hasRescale = true; break;
                case 0x1053u: if (ParseDicomDouble(valPtr, vLen, vr0, vr1, curBe, rescaleSlope)) hasRescale = true; break;
                case 0x1101u:
                case 0x1102u:
                case 0x1103u: {
                    if (vLen >= 6) {
                        DicomLut& lut = (elem == 0x1101u) ? lutR : (elem == 0x1102u) ? lutG : lutB;
                        uint16_t nEnt = RdU16(valPtr, curBe);
                        lut.numEntries = (nEnt == 0) ? 65536u : static_cast<uint32_t>(nEnt);
                        uint16_t fm = RdU16(valPtr + 2, curBe);
                        lut.firstMapped = (vr0 == 'S' && vr1 == 'S') ? static_cast<int32_t>(static_cast<int16_t>(fm))
                                                                     : static_cast<int32_t>(fm);
                        lut.bitsPerEntry = RdU16(valPtr + 4, curBe);
                        lut.bigEndian = curBe;
                    }
                    break;
                }
                case 0x1201u: lutR.data.assign(valPtr, valPtr + vLen); lutR.bigEndian = curBe; break;
                case 0x1202u: lutG.data.assign(valPtr, valPtr + vLen); lutG.bigEndian = curBe; break;
                case 0x1203u: lutB.data.assign(valPtr, valPtr + vLen); lutB.bigEndian = curBe; break;
                default: break;
            }
        }

        pos += vLen;
    }

    if (!foundPixelData || rows == 0 || cols == 0 || rows > kMaxRasterDimension || cols > kMaxRasterDimension) {
        return false;
    }
    if (samplesPerPixel != 1 && samplesPerPixel != 3 && samplesPerPixel != 4) {
        return false;
    }
    if (bitsAllocated != 1 && bitsAllocated != 8 && bitsAllocated != 16 && bitsAllocated != 32) {
        return false;
    }
    if (bitsStored == 0 || bitsStored > bitsAllocated) {
        bitsStored = bitsAllocated;
    }
    if (highBit >= bitsAllocated) {
        highBit = bitsStored - 1u;
    }
    if (rescaleSlope == 0.0) {
        rescaleSlope = 1.0;
    }

    std::vector<uint8_t> decodedRleNative;
    bool pixelBe = datasetBigEndian;

    if (pixelDataEncapsulated) {
        // Parse Basic Offset Table item (FFFE,E000) followed by Fragment items (FFFE,E000)
        size_t epos = encapStartPos;
        if (epos + 8 > size) return false;
        uint16_t botGrp = RdU16(data + epos, false);
        uint16_t botElem = RdU16(data + epos + 2, false);
        uint32_t botLen = RdU32(data + epos + 4, false);
        epos += 8;
        if (botGrp != 0xFFFEu || botElem != 0xE000u || botLen > size - epos) return false;

        uint32_t frame1RelOffset = 0;
        bool hasFrame1Limit = false;
        if (numFrames > 1 && botLen >= 8) {
            frame1RelOffset = RdU32(data + epos + 4, false);
            hasFrame1Limit = (frame1RelOffset > 0);
        }
        epos += botLen;

        size_t firstFragTagPos = epos;
        std::vector<uint8_t> frame0Frag;
        while (epos + 8 <= size) {
            size_t itemTagOffset = epos - firstFragTagPos;
            if (hasFrame1Limit && itemTagOffset >= frame1RelOffset && !frame0Frag.empty()) {
                break;
            }
            uint16_t fg = RdU16(data + epos, false);
            uint16_t fe = RdU16(data + epos + 2, false);
            uint32_t fl = RdU32(data + epos + 4, false);
            epos += 8;
            if (fg != 0xFFFEu) return false;
            if (fe == 0xE0DDu) break;
            if (fe != 0xE000u || fl > size - epos) return false;

            if (fl > 0) {
                frame0Frag.insert(frame0Frag.end(), data + epos, data + epos + fl);
                epos += fl;
                // In RLE Lossless or multi-frame without BOT, Frame 0 is 1 fragment
                if (isRleLossless || (numFrames > 1 && !hasFrame1Limit)) {
                    break;
                }
            }
        }

        if (frame0Frag.empty()) return false;

        if (isRleLossless) {
            uint32_t bps = std::max<uint32_t>(1u, bitsAllocated / 8u);
            if (!DecompressDicomRleFrame(frame0Frag.data(), frame0Frag.size(),
                                         rows, cols, samplesPerPixel, bps, decodedRleNative)) {
                return false;
            }
            nativePixelPtr = decodedRleNative.data();
            nativePixelBytes = decodedRleNative.size();
            planarConfig = 0;
            pixelBe = false;
        } else {
            (void)isEncapsulatedCompressed;
            return DecodeWicFragment(frame0Frag.data(), frame0Frag.size(), outInfo);
        }
    }

    if (!nativePixelPtr) return false;

    const uint64_t numPixels64 = static_cast<uint64_t>(rows) * static_cast<uint64_t>(cols);
    if (numPixels64 == 0 || numPixels64 > 268435456ull) return false;
    const size_t numPixels = static_cast<size_t>(numPixels64);

    if (bitsAllocated == 1 && samplesPerPixel == 1) {
        size_t reqBytes = (numPixels + 7u) / 8u;
        if (nativePixelBytes < reqBytes) return false;
        outInfo.width = cols;
        outInfo.height = rows;
        outInfo.channels = 1;
        outInfo.bitsPerChannel = 1;
        outInfo.formatName = L"DICOM Medical Image (.dcm)";
        outInfo.bgraPixels.resize(numPixels * 4u);
        bool invert = (photometric == "MONOCHROME1");
        for (size_t i = 0; i < numPixels; ++i) {
            uint8_t bit = (nativePixelPtr[i >> 3] >> (i & 7u)) & 1u;
            uint8_t g = bit ? 255u : 0u;
            if (invert) g = static_cast<uint8_t>(255u - g);
            StoreBgraOpaque(&outInfo.bgraPixels[i * 4u], g, g, g);
        }
        outInfo.valid = true;
        return true;
    }

    const uint32_t bytesPerSample = bitsAllocated / 8u;
    const uint64_t frameBytes64 = numPixels64 * static_cast<uint64_t>(samplesPerPixel) * static_cast<uint64_t>(bytesPerSample);
    if (nativePixelBytes < frameBytes64) return false;

    const uint32_t lowBit = (highBit + 1u >= bitsStored) ? (highBit + 1u - bitsStored) : 0u;
    const uint32_t bitMask = (bitsStored >= 32u) ? 0xFFFFFFFFu : ((1u << bitsStored) - 1u);

    auto readSampleSignedOrUnsigned = [&](const uint8_t* sp) -> int64_t {
        uint32_t rawWord = 0;
        if (bytesPerSample == 1) rawWord = sp[0];
        else if (bytesPerSample == 2) rawWord = RdU16(sp, pixelBe);
        else rawWord = RdU32(sp, pixelBe);

        uint32_t masked = (rawWord >> lowBit) & bitMask;
        if (pixelRep == 1) {
            if (bitsStored >= 32u) {
                return static_cast<int64_t>(static_cast<int32_t>(masked));
            }
            uint32_t signBit = 1u << (bitsStored - 1u);
            if ((masked & signBit) != 0) {
                uint32_t signExt = masked | (~bitMask);
                return static_cast<int64_t>(static_cast<int32_t>(signExt));
            }
        }
        return static_cast<int64_t>(masked);
    };

    outInfo.width = cols;
    outInfo.height = rows;
    outInfo.formatName = L"DICOM Medical Image (.dcm)";
    outInfo.bgraPixels.resize(numPixels * 4u);

    // RGB / YBR_FULL (3 or 4 samples per pixel)
    if (samplesPerPixel >= 3) {
        outInfo.channels = samplesPerPixel;
        outInfo.bitsPerChannel = bitsAllocated;
        bool isYbr = (photometric.rfind("YBR", 0) == 0);
        uint32_t shiftTo8 = (bitsStored > 8u) ? (bitsStored - 8u) : 0u;

        for (size_t i = 0; i < numPixels; ++i) {
            const uint8_t* p0 = nullptr;
            const uint8_t* p1 = nullptr;
            const uint8_t* p2 = nullptr;
            if (planarConfig == 1) {
                p0 = nativePixelPtr + (0 * numPixels + i) * bytesPerSample;
                p1 = nativePixelPtr + (1 * numPixels + i) * bytesPerSample;
                p2 = nativePixelPtr + (2 * numPixels + i) * bytesPerSample;
            } else {
                const uint8_t* base = nativePixelPtr + i * samplesPerPixel * bytesPerSample;
                p0 = base;
                p1 = base + bytesPerSample;
                p2 = base + 2u * bytesPerSample;
            }

            int64_t c0 = readSampleSignedOrUnsigned(p0);
            int64_t c1 = readSampleSignedOrUnsigned(p1);
            int64_t c2 = readSampleSignedOrUnsigned(p2);
            uint8_t s0 = static_cast<uint8_t>(std::clamp<int64_t>(c0 >> shiftTo8, 0, 255));
            uint8_t s1 = static_cast<uint8_t>(std::clamp<int64_t>(c1 >> shiftTo8, 0, 255));
            uint8_t s2 = static_cast<uint8_t>(std::clamp<int64_t>(c2 >> shiftTo8, 0, 255));

            uint8_t r = s0, g = s1, b = s2;
            if (isYbr) {
                double y  = static_cast<double>(s0);
                double cb = static_cast<double>(s1) - 128.0;
                double cr = static_cast<double>(s2) - 128.0;
                r = static_cast<uint8_t>(std::clamp(std::round(y + 1.402 * cr), 0.0, 255.0));
                g = static_cast<uint8_t>(std::clamp(std::round(y - 0.344136 * cb - 0.714136 * cr), 0.0, 255.0));
                b = static_cast<uint8_t>(std::clamp(std::round(y + 1.772 * cb), 0.0, 255.0));
            }
            StoreBgraOpaque(&outInfo.bgraPixels[i * 4u], r, g, b);
        }
        outInfo.valid = true;
        return true;
    }

    // PALETTE COLOR (1 sample per pixel + Red/Green/Blue LUTs)
    if (photometric == "PALETTE COLOR" && !lutR.data.empty() && !lutG.data.empty() && !lutB.data.empty()) {
        if (lutR.numEntries == 0) lutR.numEntries = static_cast<uint32_t>(lutR.data.size() >= 512 ? lutR.data.size() / 2 : lutR.data.size());
        if (lutG.numEntries == 0) lutG.numEntries = lutR.numEntries;
        if (lutB.numEntries == 0) lutB.numEntries = lutR.numEntries;
        outInfo.channels = 3;
        outInfo.bitsPerChannel = 8;
        for (size_t i = 0; i < numPixels; ++i) {
            int32_t idx = static_cast<int32_t>(readSampleSignedOrUnsigned(nativePixelPtr + i * bytesPerSample));
            StoreBgraOpaque(&outInfo.bgraPixels[i * 4u], lutR.Lookup(idx), lutG.Lookup(idx), lutB.Lookup(idx));
        }
        outInfo.valid = true;
        return true;
    }

    // Grayscale MONOCHROME1 / MONOCHROME2 with Rescale Slope/Intercept + VOI Window
    outInfo.channels = 1;
    outInfo.bitsPerChannel = bitsAllocated;
    const bool invertMono = (photometric == "MONOCHROME1");
    const bool hasValidWindow = (hasWinCenter && hasWinWidth && winWidth > 0.0);

    // Direct 8-bit unsigned Secondary Capture fast path when no rescale and no VOI window
    if (!hasValidWindow && bitsAllocated == 8 && bitsStored == 8 && pixelRep == 0 &&
        (!hasRescale || (rescaleSlope == 1.0 && rescaleIntercept == 0.0)))
    {
        for (size_t i = 0; i < numPixels; ++i) {
            uint8_t g = nativePixelPtr[i];
            if (invertMono) g = static_cast<uint8_t>(255u - g);
            StoreBgraOpaque(&outInfo.bgraPixels[i * 4u], g, g, g);
        }
        outInfo.valid = true;
        return true;
    }

    double autoMin = 0.0, autoMax = 0.0;
    if (!hasValidWindow) {
        for (size_t i = 0; i < numPixels; ++i) {
            double hu = static_cast<double>(readSampleSignedOrUnsigned(nativePixelPtr + i * bytesPerSample)) * rescaleSlope + rescaleIntercept;
            if (i == 0) {
                autoMin = hu;
                autoMax = hu;
            } else {
                if (hu < autoMin) autoMin = hu;
                if (hu > autoMax) autoMax = hu;
            }
        }
    }

    for (size_t i = 0; i < numPixels; ++i) {
        double hu = static_cast<double>(readSampleSignedOrUnsigned(nativePixelPtr + i * bytesPerSample)) * rescaleSlope + rescaleIntercept;
        uint8_t g = 0;
        if (hasValidWindow) {
            // DICOM PS3.3 C.11.2.1.2.1 Exact Linear VOI LUT Function
            if (winWidth <= 1.0) {
                g = (hu <= winCenter - 0.5) ? 0u : 255u;
            } else {
                double low  = winCenter - 0.5 - (winWidth - 1.0) * 0.5;
                double high = winCenter - 0.5 + (winWidth - 1.0) * 0.5;
                if (hu <= low) g = 0u;
                else if (hu > high) g = 255u;
                else {
                    double norm = ((hu - (winCenter - 0.5)) / (winWidth - 1.0) + 0.5) * 255.0;
                    g = static_cast<uint8_t>(std::clamp(std::round(norm), 0.0, 255.0));
                }
            }
        } else {
            if (autoMax > autoMin) {
                double norm = ((hu - autoMin) / (autoMax - autoMin)) * 255.0;
                g = static_cast<uint8_t>(std::clamp(std::round(norm), 0.0, 255.0));
            } else {
                g = (autoMin <= 0.0) ? 0u : static_cast<uint8_t>(std::clamp(std::round(autoMin), 0.0, 255.0));
            }
        }
        if (invertMono) g = static_cast<uint8_t>(255u - g);
        StoreBgraOpaque(&outInfo.bgraPixels[i * 4u], g, g, g);
    }

    outInfo.valid = true;
    return true;
}

} // namespace Formats
