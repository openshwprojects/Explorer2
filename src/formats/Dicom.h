#pragma once
// Dicom.h — DICOM Medical Imaging (.dcm / .dicom, DICM at 128) Decoder

namespace Formats {

inline bool DecodeDICOM(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 140 || std::memcmp(data + 128, "DICM", 4) != 0) return false;
    uint16_t rows = 0, cols = 0, bitsAlloc = 8;
    const uint8_t* pixelPtr = nullptr;
    size_t pixelBytes = 0;

    size_t pos = 132;
    while (pos + 8 <= size) {
        uint16_t grp = 0, elem = 0;
        std::memcpy(&grp, data + pos, 2);
        std::memcpy(&elem, data + pos + 2, 2);
        char vr0 = static_cast<char>(data[pos + 4]);
        char vr1 = static_cast<char>(data[pos + 5]);
        bool explicitVr = (vr0 >= 'A' && vr0 <= 'Z' && vr1 >= 'A' && vr1 <= 'Z');
        uint32_t vLen = 0;
        size_t hdrAdv = 8;
        if (explicitVr) {
            if ((vr0 == 'O' && (vr1 == 'B' || vr1 == 'W' || vr1 == 'F')) ||
                (vr0 == 'S' && vr1 == 'Q') || (vr0 == 'U' && (vr1 == 'N' || vr1 == 'T'))) {
                if (pos + 12 > size) break;
                std::memcpy(&vLen, data + pos + 8, 4);
                hdrAdv = 12;
            } else {
                uint16_t len16 = 0;
                std::memcpy(&len16, data + pos + 6, 2);
                vLen = len16;
                hdrAdv = 8;
            }
        } else {
            std::memcpy(&vLen, data + pos + 4, 4);
            hdrAdv = 8;
        }
        pos += hdrAdv;
        if (vLen == 0xFFFFFFFFu || pos + vLen > size) break;

        if (grp == 0x0028 && elem == 0x0010 && vLen >= 2) std::memcpy(&rows, data + pos, 2);
        else if (grp == 0x0028 && elem == 0x0011 && vLen >= 2) std::memcpy(&cols, data + pos, 2);
        else if (grp == 0x0028 && elem == 0x0100 && vLen >= 2) std::memcpy(&bitsAlloc, data + pos, 2);
        else if (grp == 0x7FE0 && elem == 0x0010) {
            pixelPtr = data + pos;
            pixelBytes = vLen;
            break;
        }
        pos += vLen;
    }
    if (rows == 0 || cols == 0 || !pixelPtr) return false;
    size_t count = static_cast<size_t>(rows) * cols;
    uint32_t bps = (bitsAlloc >= 16) ? 2 : 1;
    if (pixelBytes < count * bps) return false;

    outInfo.width = cols;
    outInfo.height = rows;
    outInfo.channels = 1;
    outInfo.bitsPerChannel = bps * 8;
    outInfo.formatName = L"DICOM Medical Image (.dcm)";
    outInfo.bgraPixels.resize(count * 4);

    for (size_t i = 0; i < count; ++i) {
        uint8_t c = (bps == 2) ? static_cast<uint8_t>((pixelPtr[i * 2] | (pixelPtr[i * 2 + 1] << 8)) >> 4) : pixelPtr[i];
        uint8_t* dst = &outInfo.bgraPixels[i * 4];
        dst[0] = c; dst[1] = c; dst[2] = c; dst[3] = 255;
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
