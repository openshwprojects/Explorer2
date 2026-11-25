#pragma once
// SmpteDpx.h — SMPTE 268M Digital Picture Exchange Decoder (.dpx "SDPX"/"XPDS")

namespace Formats {

inline bool DecodeSmpteDpx(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 1664) return false;
    const bool isDpxBE = (std::memcmp(data, "SDPX", 4) == 0);
    const bool isDpxLE = (std::memcmp(data, "XPDS", 4) == 0);
    if (!isDpxBE && !isDpxLE) return false;
    const bool isLittle = isDpxLE;
    auto rd16 = [&](size_t off) -> uint16_t {
        if (off + 2 > size) return 0;
        return isLittle ? DetailLayeredAndHdrImg::RdU16LE(data + off) : DetailLayeredAndHdrImg::RdU16BE(data + off);
    };
    auto rd32 = [&](size_t off) -> uint32_t {
        if (off + 4 > size) return 0;
        return isLittle ? DetailLayeredAndHdrImg::RdU32LE(data + off) : DetailLayeredAndHdrImg::RdU32BE(data + off);
    };
    using DpxElemSpec = DetailDpxCin::DpxElemSpec;
    uint32_t dataOff = rd32(4);
    uint32_t w = 0, h = 0;
    uint16_t orientation = 0;
    std::vector<DpxElemSpec> elems;
        orientation = rd16(768);
        uint16_t numElements = rd16(770);
        w = rd32(772);
        h = rd32(776);
        if (w == 0 || w > 8192 || h == 0 || h > 8192) return false;

        if (numElements >= 1 && numElements <= 8) {
            for (uint16_t e = 0; e < numElements; ++e) {
                size_t eb = 780 + static_cast<size_t>(e) * 72;
                if (eb + 72 > size) return false;
                DpxElemSpec el{};
                el.dataSign   = rd32(eb + 0);
                el.descriptor = data[eb + 20];
                el.transfer   = data[eb + 21];
                el.bitSize    = data[eb + 23];
                el.packing    = rd16(eb + 24);
                el.encoding   = rd16(eb + 26);
                uint32_t rawElemOff = rd32(eb + 28);
                if (rawElemOff != 0 && rawElemOff != 0xFFFFFFFFu) {
                    if (rawElemOff < 768 || rawElemOff >= size) return false;
                    el.elemOff = rawElemOff;
                } else {
                    if (dataOff < 768 || dataOff >= size) return false;
                    el.elemOff = dataOff;
                }
                if (el.bitSize != 8 && el.bitSize != 10 && el.bitSize != 12 &&
                    el.bitSize != 16 && el.bitSize != 32 && el.bitSize != 64) {
                    return false;
                }
                if (el.packing > 2 || el.encoding > 1) return false;
                elems.push_back(el);
            }
        } else {
            return false;
        }
    return DetailDpxCin::RenderDpxOrCineonElements(data, size, isLittle, false, w, h, orientation, elems, outInfo);
}

} // namespace Formats
