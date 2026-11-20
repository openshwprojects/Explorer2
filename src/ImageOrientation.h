#pragma once
// ImageOrientation.h - the eight ways a picture can be turned, and cutting a part out of one.
//
// A photo file may say that its pixels are stored turned: EXIF orientation, a number from 1 to 8. Phones store
// what the sensor saw and note how the phone was held. The same eight numbers name every quarter turn and mirror
// image, so they are the vocabulary for "rotate right" and "flip" here as well: turning a picture by code N is
// what makes a picture stored with orientation N upright.
//
// The viewer shows pictures upright (ImageCache), and everything that saves a picture - rotate, crop, resize -
// works on the upright picture and writes a file that needs no turning (ShellMenuEmulator::TransformImageFile).
#include <cstdint>
#include <cstring>
#include <vector>

namespace Explorer2::ImageTurn {

constexpr int kNone = 1;
constexpr int kFlipHorizontal = 2;
constexpr int kRotate180 = 3;
constexpr int kFlipVertical = 4;
constexpr int kTranspose = 5;       // mirrored along the diagonal from the top left corner
constexpr int kRotateRight = 6;     // a quarter turn clockwise
constexpr int kTransverse = 7;      // mirrored along the other diagonal
constexpr int kRotateLeft = 8;      // a quarter turn counter-clockwise

inline bool IsValid(int code) { return code >= 1 && code <= 8; }
// A quarter turn (with or without mirroring) makes the width the height
inline bool SwapsSides(int code) { return code >= 5 && code <= 8; }

// Where the pixel (x, y) of a picture w wide and h high lands
inline void MapPoint(int code, uint32_t w, uint32_t h, uint32_t x, uint32_t y, uint32_t& outX, uint32_t& outY) {
    switch (code) {
    case kFlipHorizontal: outX = w - 1u - x; outY = y;          break;
    case kRotate180:      outX = w - 1u - x; outY = h - 1u - y; break;
    case kFlipVertical:   outX = x;          outY = h - 1u - y; break;
    case kTranspose:      outX = y;          outY = x;          break;
    case kRotateRight:    outX = h - 1u - y; outY = x;          break;
    case kTransverse:     outX = h - 1u - y; outY = w - 1u - x; break;
    case kRotateLeft:     outX = y;          outY = w - 1u - x; break;
    default:              outX = x;          outY = y;          break;
    }
}

// The single turn that does what `first` followed by `second` does
inline int Compose(int first, int second) {
    if (!IsValid(first)) first = kNone;
    if (!IsValid(second)) second = kNone;
    const uint32_t w = 3, h = 2;        // three corners of a 3 x 2 picture are enough to tell the eight apart
    const uint32_t w2 = SwapsSides(first) ? h : w;
    const uint32_t h2 = SwapsSides(first) ? w : h;
    static const uint32_t kPoints[3][2] = { { 0, 0 }, { 2, 0 }, { 0, 1 } };
    for (int candidate = 1; candidate <= 8; ++candidate) {
        bool same = true;
        for (const auto& p : kPoints) {
            uint32_t ax, ay, bx, by, cx, cy;
            MapPoint(first, w, h, p[0], p[1], ax, ay);
            MapPoint(second, w2, h2, ax, ay, bx, by);
            MapPoint(candidate, w, h, p[0], p[1], cx, cy);
            if (bx != cx || by != cy) { same = false; break; }
        }
        if (same) return candidate;
    }
    return kNone;
}

// Turns a picture of 4-byte pixels (rows from the top). w and h are replaced by the new sides.
inline void TurnPixels(int code, uint32_t& w, uint32_t& h, std::vector<uint8_t>& pixels) {
    if (code <= kNone || code > 8 || w == 0 || h == 0) return;
    if (pixels.size() < static_cast<size_t>(w) * h * 4) return;
    const uint32_t srcW = w, srcH = h;
    const uint32_t dstW = SwapsSides(code) ? srcH : srcW;
    const uint32_t dstH = SwapsSides(code) ? srcW : srcH;
    std::vector<uint8_t> turned(static_cast<size_t>(dstW) * dstH * 4);
    for (uint32_t y = 0; y < srcH; ++y) {
        const uint8_t* src = &pixels[static_cast<size_t>(y) * srcW * 4];
        for (uint32_t x = 0; x < srcW; ++x) {
            uint32_t dx, dy;
            MapPoint(code, srcW, srcH, x, y, dx, dy);
            std::memcpy(&turned[(static_cast<size_t>(dy) * dstW + dx) * 4], src + static_cast<size_t>(x) * 4, 4);
        }
    }
    w = dstW;
    h = dstH;
    pixels = std::move(turned);
}

// Keeps the rectangle (x, y, cropW, cropH) of a picture of 4-byte pixels. False, and nothing changed, when the
// rectangle is empty or does not lie inside the picture.
inline bool CropPixels(uint32_t& w, uint32_t& h, std::vector<uint8_t>& pixels, uint32_t x, uint32_t y, uint32_t cropW, uint32_t cropH) {
    if (cropW == 0 || cropH == 0 || x >= w || y >= h || cropW > w - x || cropH > h - y) return false;
    if (pixels.size() < static_cast<size_t>(w) * h * 4) return false;
    if (x == 0 && y == 0 && cropW == w && cropH == h) return true;
    std::vector<uint8_t> cut(static_cast<size_t>(cropW) * cropH * 4);
    for (uint32_t row = 0; row < cropH; ++row) {
        std::memcpy(&cut[static_cast<size_t>(row) * cropW * 4],
                    &pixels[(static_cast<size_t>(y + row) * w + x) * 4], static_cast<size_t>(cropW) * 4);
    }
    w = cropW;
    h = cropH;
    pixels = std::move(cut);
    return true;
}

} // namespace Explorer2::ImageTurn
