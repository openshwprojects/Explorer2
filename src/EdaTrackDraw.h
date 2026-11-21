#pragma once
// A track (or a silk line) of the PCB view, drawn as KiCad draws it: with round ends. Segments that meet at an angle -
// the short 45-degree piece of a chamfered corner - then join smoothly. Direct2D's DrawLine ends a line flat at its
// end points, which left a notch on the outside of every bend and made the short diagonal of a chamfer stand out as
// a diamond (forum post #45, 2026-11-21). The Linux build's renderer draws round ends anyway, and the circles change
// nothing there. The ends of joined segments overlap, so the brush should be opaque: a see-through colour would come
// out darker at every joint.
#include <d2d1.h>

namespace Explorer2 {

inline void DrawTrack(ID2D1RenderTarget* rt, ID2D1Brush* brush, D2D1_POINT_2F a, D2D1_POINT_2F b, float width) {
    rt->DrawLine(a, b, brush, width);
    if (width >= 2.0f) {           // a thinner line has no visible ends
        const float r = 0.5f * width;
        rt->FillEllipse(D2D1::Ellipse(a, r, r), brush);
        rt->FillEllipse(D2D1::Ellipse(b, r, r), brush);
    }
}

} // namespace Explorer2
