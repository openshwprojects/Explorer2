#pragma once
// Ilbm.h — Amiga IFF-ILBM Interleaved Bitmap (.iff / .ilbm / .lbm) Decoder

namespace Formats {

inline bool DecodeILBM(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 40 || std::memcmp(data, "FORM", 4) != 0 ||
        (std::memcmp(data + 8, "ILBM", 4) != 0 && std::memcmp(data + 8, "PBM ", 4) != 0)) {
        return false;
    }
    bool isPbm = (std::memcmp(data + 8, "PBM ", 4) == 0);
    auto rdBE16 = [](const uint8_t* p) -> uint16_t { return static_cast<uint16_t>((p[0] << 8) | p[1]); };
    auto rdBE32 = [](const uint8_t* p) -> uint32_t {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8)  |  static_cast<uint32_t>(p[3]);
    };

    uint16_t w = 0, h = 0;
    uint8_t nPlanes = 0, compression = 0;
    uint8_t cmap[256][3] = {};
    const uint8_t* bodyPtr = nullptr;
    size_t bodyLen = 0;

    size_t pos = 12;
    while (pos + 8 <= size) {
        const uint8_t* ck = data + pos;
        uint32_t ckLen = rdBE32(ck + 4);
        pos += 8;
        if (pos + ckLen > size) break;
        if (std::memcmp(ck, "BMHD", 4) == 0 && ckLen >= 20) {
            w = rdBE16(data + pos);
            h = rdBE16(data + pos + 2);
            nPlanes = data[pos + 8];
            compression = data[pos + 10];
        } else if (std::memcmp(ck, "CMAP", 4) == 0) {
            size_t nCols = std::min<size_t>(256, ckLen / 3);
            for (size_t i = 0; i < nCols; ++i) {
                cmap[i][0] = data[pos + i * 3 + 0];
                cmap[i][1] = data[pos + i * 3 + 1];
                cmap[i][2] = data[pos + i * 3 + 2];
            }
        } else if (std::memcmp(ck, "BODY", 4) == 0) {
            bodyPtr = data + pos;
            bodyLen = ckLen;
        }
        pos += ckLen + (ckLen & 1);
    }
    if (w == 0 || h == 0 || w > 4096 || h > 4096 || nPlanes == 0 || !bodyPtr) return false;

    size_t rowBytesPerPlane = ((w + 15) / 16) * 2;
    size_t unpackedSize = isPbm ? (static_cast<size_t>(w) * h) : (rowBytesPerPlane * nPlanes * h);
    std::vector<uint8_t> raw(unpackedSize, 0);

    if (compression == 1) { // ByteRun1 (PackBits)
        size_t s = 0, d = 0;
        while (s < bodyLen && d < unpackedSize) {
            int8_t n = static_cast<int8_t>(bodyPtr[s++]);
            if (n >= 0) {
                size_t cnt = static_cast<size_t>(n) + 1;
                while (cnt-- && s < bodyLen && d < unpackedSize) raw[d++] = bodyPtr[s++];
            } else if (n != -128) {
                if (s >= bodyLen) break;
                uint8_t v = bodyPtr[s++];
                size_t cnt = static_cast<size_t>(-n) + 1;
                while (cnt-- && d < unpackedSize) raw[d++] = v;
            }
        }
    } else {
        std::memcpy(raw.data(), bodyPtr, std::min(bodyLen, unpackedSize));
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 3;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Amiga IFF-ILBM (.ilbm)";
    outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4, 255);

    for (uint16_t y = 0; y < h; ++y) {
        for (uint16_t x = 0; x < w; ++x) {
            uint8_t idx = 0;
            if (isPbm) {
                idx = raw[static_cast<size_t>(y) * w + x];
            } else {
                for (uint8_t p = 0; p < std::min<uint8_t>(nPlanes, 8); ++p) {
                    const uint8_t* planeRow = &raw[(static_cast<size_t>(y) * nPlanes + p) * rowBytesPerPlane];
                    uint8_t bit = (planeRow[x / 8] >> (7 - (x & 7))) & 1;
                    idx |= static_cast<uint8_t>(bit << p);
                }
            }
            uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(y) * w + x) * 4];
            dst[0] = cmap[idx][2]; dst[1] = cmap[idx][1]; dst[2] = cmap[idx][0]; dst[3] = 255;
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
