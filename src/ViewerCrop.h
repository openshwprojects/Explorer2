#pragma once
// ViewerCrop.h - the crop rectangle of the full viewer: which part of it a press takes hold of, and what a drag
// makes of it. Plain arithmetic without a window, so that the self-tests can check it.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Explorer2 {

struct CropRectF {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

enum ViewerCropGrip : int {
    kCropGripNone = 0,
    kCropGripTopLeft = 1,
    kCropGripTop = 2,
    kCropGripTopRight = 3,
    kCropGripRight = 4,
    kCropGripBottomRight = 5,
    kCropGripBottom = 6,
    kCropGripBottomLeft = 7,
    kCropGripLeft = 8,
    kCropGripMove = 9,      // inside: the whole rectangle is moved
    kCropGripNew = 10       // outside: a new rectangle is drawn from that point
};

// What a press at (x, y) takes hold of. Everything is in the units the rectangle is drawn in; `reach` is how
// near to a corner or an edge still counts as on it.
inline int HitCropGrip(float x, float y, const CropRectF& drawn, float reach) {
    const bool nearLeft = std::fabs(x - drawn.left) <= reach;
    const bool nearRight = std::fabs(x - drawn.right) <= reach;
    const bool nearTop = std::fabs(y - drawn.top) <= reach;
    const bool nearBottom = std::fabs(y - drawn.bottom) <= reach;
    const bool alongX = x >= drawn.left - reach && x <= drawn.right + reach;
    const bool alongY = y >= drawn.top - reach && y <= drawn.bottom + reach;
    if (nearLeft && nearTop) return kCropGripTopLeft;
    if (nearRight && nearTop) return kCropGripTopRight;
    if (nearRight && nearBottom) return kCropGripBottomRight;
    if (nearLeft && nearBottom) return kCropGripBottomLeft;
    if (nearTop && alongX) return kCropGripTop;
    if (nearBottom && alongX) return kCropGripBottom;
    if (nearLeft && alongY) return kCropGripLeft;
    if (nearRight && alongY) return kCropGripRight;
    if (x > drawn.left && x < drawn.right && y > drawn.top && y < drawn.bottom) return kCropGripMove;
    return kCropGripNew;
}

// The rectangle after `grip` was dragged from (startX, startY) to (x, y), all in picture pixels. `start` is the
// rectangle as it was when the drag began. The result lies inside the picture (w by h) and is at least `least`
// pixels wide and high: the edge that is being dragged stops there.
inline CropRectF DragCropRect(int grip, const CropRectF& start, float startX, float startY, float x, float y,
                              float w, float h, float least) {
    least = std::max(1.0f, std::min(least, std::min(w, h)));
    x = std::clamp(x, 0.0f, w);
    y = std::clamp(y, 0.0f, h);
    const float dx = x - startX;
    const float dy = y - startY;
    CropRectF r = start;

    if (grip == kCropGripMove) {
        const float rw = start.right - start.left;
        const float rh = start.bottom - start.top;
        r.left = std::clamp(start.left + dx, 0.0f, std::max(0.0f, w - rw));
        r.top = std::clamp(start.top + dy, 0.0f, std::max(0.0f, h - rh));
        r.right = r.left + rw;
        r.bottom = r.top + rh;
        return r;
    }
    if (grip == kCropGripNew) {
        r.left = std::min(startX, x);
        r.right = std::max(startX, x);
        r.top = std::min(startY, y);
        r.bottom = std::max(startY, y);
        // too small yet: it grows towards the bottom right, or the other way at the picture's edge
        if (r.right - r.left < least) {
            r.right = std::min(w, r.left + least);
            r.left = r.right - least;
        }
        if (r.bottom - r.top < least) {
            r.bottom = std::min(h, r.top + least);
            r.top = r.bottom - least;
        }
        return r;
    }

    const bool movesLeft = grip == kCropGripTopLeft || grip == kCropGripLeft || grip == kCropGripBottomLeft;
    const bool movesRight = grip == kCropGripTopRight || grip == kCropGripRight || grip == kCropGripBottomRight;
    const bool movesTop = grip == kCropGripTopLeft || grip == kCropGripTop || grip == kCropGripTopRight;
    const bool movesBottom = grip == kCropGripBottomLeft || grip == kCropGripBottom || grip == kCropGripBottomRight;
    if (movesLeft) r.left = std::clamp(start.left + dx, 0.0f, std::max(0.0f, start.right - least));
    if (movesRight) r.right = std::clamp(start.right + dx, std::min(w, start.left + least), w);
    if (movesTop) r.top = std::clamp(start.top + dy, 0.0f, std::max(0.0f, start.bottom - least));
    if (movesBottom) r.bottom = std::clamp(start.bottom + dy, std::min(h, start.top + least), h);
    return r;
}

// The rectangle in whole pixels, inside the picture and never empty
inline void RoundCropRect(const CropRectF& r, uint32_t w, uint32_t h, uint32_t& outX, uint32_t& outY, uint32_t& outW, uint32_t& outH) {
    if (w == 0 || h == 0) {
        outX = outY = outW = outH = 0;
        return;
    }
    const long left = std::clamp(std::lround(std::min(r.left, r.right)), 0L, static_cast<long>(w) - 1);
    const long top = std::clamp(std::lround(std::min(r.top, r.bottom)), 0L, static_cast<long>(h) - 1);
    const long right = std::clamp(std::lround(std::max(r.left, r.right)), left + 1, static_cast<long>(w));
    const long bottom = std::clamp(std::lround(std::max(r.top, r.bottom)), top + 1, static_cast<long>(h));
    outX = static_cast<uint32_t>(left);
    outY = static_cast<uint32_t>(top);
    outW = static_cast<uint32_t>(right - left);
    outH = static_cast<uint32_t>(bottom - top);
}

} // namespace Explorer2
