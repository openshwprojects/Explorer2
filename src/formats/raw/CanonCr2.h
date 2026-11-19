#pragma once
// CanonCr2.h — Canon EOS CR2 Lossless-JPEG Sliced CFA RAW Decoder (.cr2)

namespace Formats {

namespace DetailImg200 {

// Canon .cr2 horizontal slice unslicing (tag 0xC640: [count, sliceWidth, lastSliceWidth])
inline void UnsliceCanonCr2Sensor(
    uint32_t width, uint32_t height, uint16_t effSpp,
    const uint16_t cr2Slices[3],
    std::vector<uint16_t>& fullSamples)
{
    uint32_t sCount = cr2Slices[0];
    uint32_t sWidth = cr2Slices[1];
    uint32_t sLast  = cr2Slices[2];
    uint32_t rowElems = width * effSpp;
    if (effSpp > 1 && sCount * sWidth + sLast == width) {
        sWidth *= effSpp;
        sLast  *= effSpp;
    }
    if ((sCount == 0 || sWidth > 0) && sLast > 0 && sCount * sWidth + sLast == rowElems) {
        std::vector<uint16_t> unsliced(fullSamples.size(), 0);
        size_t regularSamples = static_cast<size_t>(sCount) * height * sWidth;
        size_t sliceSamples   = static_cast<size_t>(height) * sWidth;
        for (size_t j = 0; j < fullSamples.size(); ++j) {
            size_t dstY = 0, dstX = 0;
            if (j < regularSamples && sliceSamples > 0) {
                size_t sliceIdx = j / sliceSamples;
                size_t rem = j % sliceSamples;
                dstY = rem / sWidth;
                dstX = sliceIdx * sWidth + (rem % sWidth);
            } else {
                size_t rem = j - regularSamples;
                dstY = rem / sLast;
                dstX = static_cast<size_t>(sCount) * sWidth + (rem % sLast);
            }
            if (dstY < height && dstX < rowElems) {
                unsliced[dstY * rowElems + dstX] = fullSamples[j];
            }
        }
        fullSamples = std::move(unsliced);
    }
}

} // namespace DetailImg200

inline bool DecodeCanonCr2(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return DetailImg200::ParseTiffRawIfds(data, size, L"Canon CR2 RAW (.cr2)", outInfo);
}

} // namespace Formats
