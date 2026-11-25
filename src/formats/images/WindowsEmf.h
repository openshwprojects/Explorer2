#pragma once
// WindowsEmf.h — [MS-EMF] Windows Enhanced Metafile Vector, Path, Transform & DIB Interpreter (.emf)

namespace Formats {

namespace DetailEmfWmf {

// Pure-C++20 [MS-EMF] Enhanced Metafile Interpreter
inline bool DecodeEmfPureCpp(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 88) return false;
    if (DetailLayeredAndHdrImg::RdU32LE(data) != 1u || DetailLayeredAndHdrImg::RdU32LE(data + 40) != 0x464D4520u) return false;
    uint32_t hdrSize = DetailLayeredAndHdrImg::RdU32LE(data + 4);
    if (hdrSize < 88u || hdrSize > size) return false;

    int32_t bLeft   = DetailVectorAndRawImg::RdS32(data + 8,  false);
    int32_t bTop    = DetailVectorAndRawImg::RdS32(data + 12, false);
    int32_t bRight  = DetailVectorAndRawImg::RdS32(data + 16, false);
    int32_t bBottom = DetailVectorAndRawImg::RdS32(data + 20, false);
    int32_t fLeft   = DetailVectorAndRawImg::RdS32(data + 24, false);
    int32_t fTop    = DetailVectorAndRawImg::RdS32(data + 28, false);
    int32_t fRight  = DetailVectorAndRawImg::RdS32(data + 32, false);
    int32_t fBottom = DetailVectorAndRawImg::RdS32(data + 36, false);

    float devMinX = static_cast<float>(bLeft);
    float devMinY = static_cast<float>(bTop);
    float devW    = static_cast<float>(bRight - bLeft);
    float devH    = static_cast<float>(bBottom - bTop);
    if (devW < 2.0f || devH < 2.0f) {
        float fw = static_cast<float>(fRight - fLeft) / 26.458f;
        float fh = static_cast<float>(fBottom - fTop) / 26.458f;
        if (fw >= 2.0f && fh >= 2.0f) {
            devMinX = static_cast<float>(fLeft) / 26.458f;
            devMinY = static_cast<float>(fTop)  / 26.458f;
            devW = fw;
            devH = fh;
        } else {
            devMinX = 0.0f; devMinY = 0.0f; devW = 512.0f; devH = 384.0f;
        }
    }

    float aspect = devW / std::max(1.0f, devH);
    uint32_t outW = 512u, outH = 512u;
    if (aspect >= 1.0f) outH = std::clamp(static_cast<uint32_t>(std::lround(512.0f / aspect)), 64u, 512u);
    else                outW = std::clamp(static_cast<uint32_t>(std::lround(512.0f * aspect)), 64u, 512u);

    const float devToCanvasX = static_cast<float>(outW) / std::max(1.0f, devW);
    const float devToCanvasY = static_cast<float>(outH) / std::max(1.0f, devH);
    const float devToCanvasS = 0.5f * (devToCanvasX + devToCanvasY);

    std::vector<uint8_t> canvas(static_cast<size_t>(outW) * outH * 4u, 255u);
    GdiDcState dc;
    std::vector<GdiDcState> dcStack;
    std::unordered_map<uint32_t, GdiObjectSlot> objTable;

    bool inPath = false;
    std::vector<PathSubfigure> activePath;
    size_t drawnCount = 0;

    auto mapPt = [&](float lx, float ly) -> DetailVectorAndRawImg::Pt2f {
        float wx = lx * dc.world.m11 + ly * dc.world.m21 + dc.world.dx;
        float wy = lx * dc.world.m12 + ly * dc.world.m22 + dc.world.dy;
        float dx = wx - dc.winOrgX + dc.vpOrgX;
        float dy = wy - dc.winOrgY + dc.vpOrgY;
        if (dc.hasWinExt && dc.hasVpExt && std::abs(dc.winExtX) > 1e-6f && std::abs(dc.winExtY) > 1e-6f) {
            dx = (wx - dc.winOrgX) * (dc.vpExtX / dc.winExtX) + dc.vpOrgX;
            dy = (wy - dc.winOrgY) * (dc.vpExtY / dc.winExtY) + dc.vpOrgY;
        } else if (dc.mapMode >= 2u && dc.mapMode <= 6u) {
            // Metric/Twips GDI mapping modes have Y-up logical axes if viewport was not overridden
            dy = -(wy - dc.winOrgY) + dc.vpOrgY;
        }
        return {
            (dx - devMinX) * devToCanvasX,
            (dy - devMinY) * devToCanvasY
        };
    };

    auto mapPenWidth = [&]() -> float {
        float wScale = std::sqrt(0.5f * (dc.world.m11 * dc.world.m11 + dc.world.m12 * dc.world.m12 +
                                         dc.world.m21 * dc.world.m21 + dc.world.m22 * dc.world.m22));
        float vpScale = 1.0f;
        if (dc.hasWinExt && dc.hasVpExt && std::abs(dc.winExtX) > 1e-6f) {
            vpScale = std::abs(dc.vpExtX / dc.winExtX);
        }
        return std::max(1.0f, dc.pen.width * wScale * vpScale * devToCanvasS);
    };

    auto ensureSubpathAtCur = [&]() -> PathSubfigure& {
        DetailVectorAndRawImg::Pt2f cp = mapPt(dc.curX, dc.curY);
        if (activePath.empty() || activePath.back().closed) {
            activePath.push_back({ { cp }, false });
        } else if (activePath.back().pts.empty()) {
            activePath.back().pts.push_back(cp);
        }
        return activePath.back();
    };

    auto emitClosedShape = [&](std::vector<DetailVectorAndRawImg::Pt2f>&& ring) {
        if (ring.size() < 3u) return;
        if (inPath) {
            activePath.push_back({ std::move(ring), true });
            return;
        }
        std::vector<std::vector<DetailVectorAndRawImg::Pt2f>> rings = { ring };
        if (FillPolygons4x4AA(canvas, outW, outH, rings, dc.polyFillMode != 2u, dc.brush, dc.bkMode, dc.bkR, dc.bkG, dc.bkB)) {
            ++drawnCount;
        }
        if (StrokePolylineAA(canvas, outW, outH, ring, true, dc.pen, mapPenWidth())) {
            ++drawnCount;
        }
    };

    auto rdF32 = [](const uint8_t* p) -> float {
        uint32_t u = DetailLayeredAndHdrImg::RdU32LE(p);
        float f = 0.0f;
        std::memcpy(&f, &u, 4);
        return std::isfinite(f) ? f : 0.0f;
    };

    auto rdXForm = [&](const uint8_t* p) -> XForm2D {
        return { rdF32(p + 0), rdF32(p + 4), rdF32(p + 8), rdF32(p + 12), rdF32(p + 16), rdF32(p + 20) };
    };

    size_t pos = hdrSize;
    size_t guard = 0;
    while (pos + 8u <= size && ++guard <= 262144u) {
        const uint8_t* rec = data + pos;
        uint32_t rType = DetailLayeredAndHdrImg::RdU32LE(rec + 0);
        uint32_t rSize = DetailLayeredAndHdrImg::RdU32LE(rec + 4);
        if (rSize < 8u || (rSize & 3u) != 0u || pos + rSize > size) break;
        if (rType == 14u) break; // EMR_EOF

        switch (rType) {
        case 9: // EMR_SETWINDOWEXTEX
            if (rSize >= 16u) {
                dc.winExtX = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 8, false));
                dc.winExtY = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 12, false));
                dc.hasWinExt = true;
            }
            break;
        case 10: // EMR_SETWINDOWORGEX
            if (rSize >= 16u) {
                dc.winOrgX = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 8, false));
                dc.winOrgY = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 12, false));
            }
            break;
        case 11: // EMR_SETVIEWPORTEXTEX
            if (rSize >= 16u) {
                dc.vpExtX = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 8, false));
                dc.vpExtY = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 12, false));
                dc.hasVpExt = true;
            }
            break;
        case 12: // EMR_SETVIEWPORTORGEX
            if (rSize >= 16u) {
                dc.vpOrgX = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 8, false));
                dc.vpOrgY = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 12, false));
            }
            break;
        case 15: // EMR_SETPIXELV
            if (rSize >= 20u) {
                auto p = mapPt(static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 8, false)),
                               static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 12, false)));
                int px = static_cast<int>(std::lround(p.x));
                int py = static_cast<int>(std::lround(p.y));
                if (px >= 0 && px < static_cast<int>(outW) && py >= 0 && py < static_cast<int>(outH)) {
                    uint8_t r = 0, g = 0, b = 0;
                    UnpackColorRef(DetailLayeredAndHdrImg::RdU32LE(rec + 16), r, g, b);
                    BlendPixelBgra(&canvas[(static_cast<size_t>(py) * outW + static_cast<size_t>(px)) * 4u], r, g, b, 1.0f);
                    ++drawnCount;
                }
            }
            break;
        case 17: if (rSize >= 12u) dc.mapMode = DetailLayeredAndHdrImg::RdU32LE(rec + 8); break;       // EMR_SETMAPMODE
        case 18: if (rSize >= 12u) dc.bkMode = DetailLayeredAndHdrImg::RdU32LE(rec + 8); break;        // EMR_SETBKMODE
        case 19: if (rSize >= 12u) dc.polyFillMode = DetailLayeredAndHdrImg::RdU32LE(rec + 8); break;  // EMR_SETPOLYFILLMODE
        case 22: if (rSize >= 12u) dc.textAlign = DetailLayeredAndHdrImg::RdU32LE(rec + 8); break;     // EMR_SETTEXTALIGN
        case 24: if (rSize >= 12u) UnpackColorRef(DetailLayeredAndHdrImg::RdU32LE(rec + 8), dc.textR, dc.textG, dc.textB); break;
        case 25: if (rSize >= 12u) UnpackColorRef(DetailLayeredAndHdrImg::RdU32LE(rec + 8), dc.bkR, dc.bkG, dc.bkB); break;
        case 105: // EMR_SETDCPENCOLOR (0x69) & EMR_SETDCBRUSHCOLOR (0x6A = 106) -> check [MS-EMF]: 18=DC_BRUSH, 19=DC_PEN
            break;
        case 27: // EMR_MOVETOEX
            if (rSize >= 16u) {
                dc.curX = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 8, false));
                dc.curY = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 12, false));
                if (inPath) {
                    activePath.push_back({ { mapPt(dc.curX, dc.curY) }, false });
                }
            }
            break;
        case 54: // EMR_LINETO
            if (rSize >= 16u) {
                float nx = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 8, false));
                float ny = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 12, false));
                if (inPath) {
                    auto& sp = ensureSubpathAtCur();
                    sp.pts.push_back(mapPt(nx, ny));
                } else {
                    std::vector<DetailVectorAndRawImg::Pt2f> seg = { mapPt(dc.curX, dc.curY), mapPt(nx, ny) };
                    if (StrokePolylineAA(canvas, outW, outH, seg, false, dc.pen, mapPenWidth())) ++drawnCount;
                }
                dc.curX = nx; dc.curY = ny;
            }
            break;
        case 33: // EMR_SAVEDC
            if (dcStack.size() < 64u) dcStack.push_back(dc);
            break;
        case 34: // EMR_RESTOREDC
            if (rSize >= 12u && !dcStack.empty()) {
                int32_t rel = DetailVectorAndRawImg::RdS32(rec + 8, false);
                size_t pops = (rel < 0) ? static_cast<size_t>(std::min<int64_t>(-static_cast<int64_t>(rel), dcStack.size())) : 1u;
                for (size_t k = 0; k < pops && !dcStack.empty(); ++k) {
                    dc = dcStack.back();
                    dcStack.pop_back();
                }
            }
            break;
        case 35: // EMR_SETWORLDTRANSFORM
            if (rSize >= 32u) dc.world = rdXForm(rec + 8);
            break;
        case 36: // EMR_MODIFYWORLDTRANSFORM
            if (rSize >= 36u) {
                XForm2D xf = rdXForm(rec + 8);
                uint32_t mode = DetailLayeredAndHdrImg::RdU32LE(rec + 32);
                if (mode == 1u)      dc.world = XForm2D{};
                else if (mode == 2u) dc.world = XForm2D::Multiply(xf, dc.world);
                else if (mode == 3u) dc.world = XForm2D::Multiply(dc.world, xf);
                else if (mode == 4u) dc.world = xf;
            }
            break;
        case 37: // EMR_SELECTOBJECT
            if (rSize >= 12u) {
                uint32_t ih = DetailLayeredAndHdrImg::RdU32LE(rec + 8);
                if ((ih & 0x80000000u) != 0u) {
                    ApplyStockObject(ih, dc);
                } else {
                    auto it = objTable.find(ih);
                    if (it != objTable.end()) {
                        if (it->second.kind == GdiObjectSlot::Pen)   { dc.pen   = it->second.pen;   dc.useDcPen   = false; }
                        if (it->second.kind == GdiObjectSlot::Brush) { dc.brush = it->second.brush; dc.useDcBrush = false; }
                        if (it->second.kind == GdiObjectSlot::Font)  { dc.font  = it->second.font; }
                    }
                }
            }
            break;
        case 38: // EMR_CREATEPEN
            if (rSize >= 28u && objTable.size() < 4096u) {
                uint32_t ih = DetailLayeredAndHdrImg::RdU32LE(rec + 8);
                GdiObjectSlot s;
                s.kind = GdiObjectSlot::Pen;
                s.pen.style = DetailLayeredAndHdrImg::RdU32LE(rec + 12);
                s.pen.width = static_cast<float>(std::max(0, DetailVectorAndRawImg::RdS32(rec + 16, false)));
                UnpackColorRef(DetailLayeredAndHdrImg::RdU32LE(rec + 24), s.pen.r, s.pen.g, s.pen.b);
                objTable[ih] = s;
            }
            break;
        case 95: // EMR_EXTCREATEPEN
            if (rSize >= 48u && objTable.size() < 4096u) {
                uint32_t ih = DetailLayeredAndHdrImg::RdU32LE(rec + 8);
                GdiObjectSlot s;
                s.kind = GdiObjectSlot::Pen;
                s.pen.style = DetailLayeredAndHdrImg::RdU32LE(rec + 28);
                s.pen.width = static_cast<float>(DetailLayeredAndHdrImg::RdU32LE(rec + 32));
                uint32_t bStyle = DetailLayeredAndHdrImg::RdU32LE(rec + 36);
                if (bStyle == 1u) s.pen.style = 5u; // BS_NULL pen
                UnpackColorRef(DetailLayeredAndHdrImg::RdU32LE(rec + 40), s.pen.r, s.pen.g, s.pen.b);
                objTable[ih] = s;
            }
            break;
        case 39: // EMR_CREATEBRUSHINDIRECT
            if (rSize >= 24u && objTable.size() < 4096u) {
                uint32_t ih = DetailLayeredAndHdrImg::RdU32LE(rec + 8);
                GdiObjectSlot s;
                s.kind = GdiObjectSlot::Brush;
                s.brush.style = DetailLayeredAndHdrImg::RdU32LE(rec + 12);
                UnpackColorRef(DetailLayeredAndHdrImg::RdU32LE(rec + 16), s.brush.r, s.brush.g, s.brush.b);
                s.brush.hatch = DetailLayeredAndHdrImg::RdU32LE(rec + 20);
                objTable[ih] = s;
            }
            break;
        case 82: // EMR_EXTCREATEFONTINDIRECTW
            if (rSize >= 32u && objTable.size() < 4096u) {
                uint32_t ih = DetailLayeredAndHdrImg::RdU32LE(rec + 8);
                GdiObjectSlot s;
                s.kind = GdiObjectSlot::Font;
                s.font.height = static_cast<float>(std::abs(DetailVectorAndRawImg::RdS32(rec + 12, false)));
                s.font.escapementDeg = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 20, false)) * 0.1f;
                objTable[ih] = s;
            }
            break;
        case 40: // EMR_DELETEOBJECT
            if (rSize >= 12u) objTable.erase(DetailLayeredAndHdrImg::RdU32LE(rec + 8));
            break;
        case 2:  // EMR_POLYBEZIER (32-bit)
        case 3:  // EMR_POLYGON (32-bit)
        case 4:  // EMR_POLYLINE (32-bit)
        case 5:  // EMR_POLYBEZIERTO (32-bit)
        case 6:  // EMR_POLYLINETO (32-bit)
        case 85: // EMR_POLYBEZIER16
        case 86: // EMR_POLYGON16
        case 87: // EMR_POLYLINE16
        case 88: // EMR_POLYBEZIERTO16
        case 89: // EMR_POLYLINETO16
        {
            if (rSize < 28u) break;
            const bool is16 = (rType >= 85u);
            const size_t ptBytes = is16 ? 4u : 8u;
            uint32_t cpt = DetailLayeredAndHdrImg::RdU32LE(rec + 24);
            if (cpt == 0u || cpt > 262144u || 28u + static_cast<size_t>(cpt) * ptBytes > rSize) break;

            auto readPt = [&](uint32_t idx) -> std::pair<float, float> {
                const uint8_t* p = rec + 28u + static_cast<size_t>(idx) * ptBytes;
                if (is16) return { static_cast<float>(DetailVectorAndRawImg::RdS16(p, false)), static_cast<float>(DetailVectorAndRawImg::RdS16(p + 2, false)) };
                return { static_cast<float>(DetailVectorAndRawImg::RdS32(p, false)), static_cast<float>(DetailVectorAndRawImg::RdS32(p + 4, false)) };
            };

            const bool isBez  = (rType == 2u || rType == 5u || rType == 85u || rType == 88u);
            const bool isTo   = (rType == 5u || rType == 6u || rType == 88u || rType == 89u);
            const bool isPoly = (rType == 3u || rType == 86u);

            std::vector<DetailVectorAndRawImg::Pt2f> pts;
            if (isBez) {
                if (isTo) {
                    if (cpt < 3u) break;
                    pts.push_back(mapPt(dc.curX, dc.curY));
                    for (uint32_t i = 0; i + 2u < cpt; i += 3u) {
                        auto c1 = readPt(i);
                        auto c2 = readPt(i + 1u);
                        auto ep = readPt(i + 2u);
                        AppendCubicBezier(pts, pts.back(), mapPt(c1.first, c1.second), mapPt(c2.first, c2.second), mapPt(ep.first, ep.second));
                        dc.curX = ep.first; dc.curY = ep.second;
                    }
                } else {
                    if (cpt < 4u) break;
                    auto p0 = readPt(0);
                    pts.push_back(mapPt(p0.first, p0.second));
                    for (uint32_t i = 1; i + 2u < cpt; i += 3u) {
                        auto c1 = readPt(i);
                        auto c2 = readPt(i + 1u);
                        auto ep = readPt(i + 2u);
                        AppendCubicBezier(pts, pts.back(), mapPt(c1.first, c1.second), mapPt(c2.first, c2.second), mapPt(ep.first, ep.second));
                    }
                }
            } else {
                if (isTo) pts.push_back(mapPt(dc.curX, dc.curY));
                for (uint32_t i = 0; i < cpt; ++i) {
                    auto lp = readPt(i);
                    pts.push_back(mapPt(lp.first, lp.second));
                    if (isTo && i + 1u == cpt) { dc.curX = lp.first; dc.curY = lp.second; }
                }
            }

            if (isPoly) {
                emitClosedShape(std::move(pts));
            } else if (inPath) {
                if (isTo) {
                    auto& sp = ensureSubpathAtCur();
                    for (size_t k = 1u; k < pts.size(); ++k) sp.pts.push_back(pts[k]);
                } else if (!pts.empty()) {
                    activePath.push_back({ std::move(pts), false });
                }
            } else {
                if (StrokePolylineAA(canvas, outW, outH, pts, false, dc.pen, mapPenWidth())) ++drawnCount;
            }
            break;
        }
        case 7:  // EMR_POLYPOLYLINE
        case 8:  // EMR_POLYPOLYGON
        case 90: // EMR_POLYPOLYLINE16
        case 91: // EMR_POLYPOLYGON16
        {
            if (rSize < 36u) break;
            const bool is16 = (rType >= 90u);
            const bool isPoly = (rType == 8u || rType == 91u);
            const size_t ptBytes = is16 ? 4u : 8u;
            uint32_t nPolys = DetailLayeredAndHdrImg::RdU32LE(rec + 24);
            uint32_t cpts   = DetailLayeredAndHdrImg::RdU32LE(rec + 28);
            if (nPolys == 0u || nPolys > 65536u || cpts > 262144u) break;
            size_t ptsOff = 32u + static_cast<size_t>(nPolys) * 4u;
            if (ptsOff + static_cast<size_t>(cpts) * ptBytes > rSize) break;

            std::vector<std::vector<DetailVectorAndRawImg::Pt2f>> rings;
            uint32_t curIdx = 0;
            for (uint32_t p = 0; p < nPolys; ++p) {
                uint32_t cnt = DetailLayeredAndHdrImg::RdU32LE(rec + 32u + static_cast<size_t>(p) * 4u);
                if (curIdx + cnt > cpts) break;
                std::vector<DetailVectorAndRawImg::Pt2f> ring;
                ring.reserve(cnt);
                for (uint32_t k = 0; k < cnt; ++k) {
                    const uint8_t* pp = rec + ptsOff + static_cast<size_t>(curIdx + k) * ptBytes;
                    float lx = is16 ? static_cast<float>(DetailVectorAndRawImg::RdS16(pp, false)) : static_cast<float>(DetailVectorAndRawImg::RdS32(pp, false));
                    float ly = is16 ? static_cast<float>(DetailVectorAndRawImg::RdS16(pp + 2, false)) : static_cast<float>(DetailVectorAndRawImg::RdS32(pp + 4, false));
                    ring.push_back(mapPt(lx, ly));
                }
                curIdx += cnt;
                rings.push_back(std::move(ring));
            }
            if (inPath) {
                for (auto& r : rings) activePath.push_back({ std::move(r), isPoly });
            } else if (isPoly) {
                if (FillPolygons4x4AA(canvas, outW, outH, rings, dc.polyFillMode != 2u, dc.brush, dc.bkMode, dc.bkR, dc.bkG, dc.bkB)) ++drawnCount;
                for (const auto& r : rings) {
                    if (StrokePolylineAA(canvas, outW, outH, r, true, dc.pen, mapPenWidth())) ++drawnCount;
                }
            } else {
                for (const auto& r : rings) {
                    if (StrokePolylineAA(canvas, outW, outH, r, false, dc.pen, mapPenWidth())) ++drawnCount;
                }
            }
            break;
        }
        case 42: // EMR_ELLIPSE
            if (rSize >= 24u) {
                auto p0 = mapPt(static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 8, false)),  static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 12, false)));
                auto p1 = mapPt(static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 16, false)), static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 20, false)));
                std::vector<DetailVectorAndRawImg::Pt2f> ring;
                BuildEllipseRing(p0.x, p0.y, p1.x, p1.y, ring);
                emitClosedShape(std::move(ring));
            }
            break;
        case 43: // EMR_RECTANGLE
            if (rSize >= 24u) {
                float x0 = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 8, false));
                float y0 = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 12, false));
                float x1 = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 16, false));
                float y1 = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 20, false));
                emitClosedShape({ mapPt(x0, y0), mapPt(x1, y0), mapPt(x1, y1), mapPt(x0, y1) });
            }
            break;
        case 44: // EMR_ROUNDRECT
            if (rSize >= 32u) {
                auto p0 = mapPt(static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 8, false)),  static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 12, false)));
                auto p1 = mapPt(static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 16, false)), static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 20, false)));
                auto c0 = mapPt(0.0f, 0.0f);
                auto c1 = mapPt(static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 24, false)), static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 28, false)));
                std::vector<DetailVectorAndRawImg::Pt2f> ring;
                BuildRoundRectRing(p0.x, p0.y, p1.x, p1.y, std::abs(c1.x - c0.x), std::abs(c1.y - c0.y), ring);
                emitClosedShape(std::move(ring));
            }
            break;
        case 45: // EMR_ARC
        case 46: // EMR_CHORD
        case 47: // EMR_PIE
        case 55: // EMR_ARCTO
            if (rSize >= 40u) {
                auto b0 = mapPt(static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 8, false)),  static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 12, false)));
                auto b1 = mapPt(static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 16, false)), static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 20, false)));
                auto sP = mapPt(static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 24, false)), static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 28, false)));
                auto eP = mapPt(static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 32, false)), static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 36, false)));
                std::vector<DetailVectorAndRawImg::Pt2f> arcPts;
                BuildEllipticalArcPoints(b0.x, b0.y, b1.x, b1.y, sP.x, sP.y, eP.x, eP.y, true, arcPts);
                if (rType == 47u && arcPts.size() >= 2u) { // PIE includes center
                    arcPts.push_back({ (b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f });
                    emitClosedShape(std::move(arcPts));
                } else if (rType == 46u && arcPts.size() >= 3u) { // CHORD closes endpoints
                    emitClosedShape(std::move(arcPts));
                } else if (!arcPts.empty()) {
                    if (inPath) activePath.push_back({ std::move(arcPts), false });
                    else if (StrokePolylineAA(canvas, outW, outH, arcPts, false, dc.pen, mapPenWidth())) ++drawnCount;
                }
            }
            break;
        case 59: // EMR_BEGINPATH
            inPath = true;
            activePath.clear();
            break;
        case 60: // EMR_ENDPATH
            inPath = false;
            break;
        case 61: // EMR_CLOSEFIGURE
            if (!activePath.empty()) activePath.back().closed = true;
            break;
        case 68: // EMR_ABORTPATH
            inPath = false;
            activePath.clear();
            break;
        case 62: // EMR_FILLPATH
        case 63: // EMR_STROKEANDFILLPATH
        case 64: // EMR_STROKEPATH
        {
            inPath = false;
            if (rType == 62u || rType == 63u) {
                std::vector<std::vector<DetailVectorAndRawImg::Pt2f>> rings;
                for (const auto& sp : activePath) {
                    if (sp.pts.size() >= 3u) rings.push_back(sp.pts);
                }
                if (FillPolygons4x4AA(canvas, outW, outH, rings, dc.polyFillMode != 2u, dc.brush, dc.bkMode, dc.bkR, dc.bkG, dc.bkB)) ++drawnCount;
            }
            if (rType == 63u || rType == 64u) {
                float pw = mapPenWidth();
                for (const auto& sp : activePath) {
                    if (StrokePolylineAA(canvas, outW, outH, sp.pts, sp.closed, dc.pen, pw)) ++drawnCount;
                }
            }
            activePath.clear();
            break;
        }
        case 76: // EMR_BITBLT
            if (rSize >= 100u) {
                float xd = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 24, false));
                float yd = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 28, false));
                float wd = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 32, false));
                float hd = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 36, false));
                uint32_t rop = DetailLayeredAndHdrImg::RdU32LE(rec + 40);
                int32_t xs = DetailVectorAndRawImg::RdS32(rec + 44, false);
                int32_t ys = DetailVectorAndRawImg::RdS32(rec + 48, false);
                uint32_t offBmi  = DetailLayeredAndHdrImg::RdU32LE(rec + 84);
                uint32_t cbBmi   = DetailLayeredAndHdrImg::RdU32LE(rec + 88);
                uint32_t offBits = DetailLayeredAndHdrImg::RdU32LE(rec + 92);
                uint32_t cbBits  = DetailLayeredAndHdrImg::RdU32LE(rec + 96);
                auto d0 = mapPt(xd, yd);
                auto d1 = mapPt(xd + wd, yd + hd);
                if (cbBmi >= 40u && cbBits > 0u && offBmi + cbBmi <= rSize && offBits + cbBits <= rSize) {
                    if (BlitSplitOrPackedDib(canvas, outW, outH, d0.x, d0.y, d1.x, d1.y,
                                             xs, ys, static_cast<int32_t>(wd), static_cast<int32_t>(hd),
                                             rec + offBmi, cbBmi, rec + offBits, cbBits)) {
                        ++drawnCount;
                    }
                } else if (rop == 0x00F00021u || rop == 0x00000042u || rop == 0x00FF0062u) { // PATCOPY / BLACKNESS / WHITENESS
                    GdiBrush b = dc.brush;
                    if (rop == 0x00000042u)      b = { 0u, 0u, 0u, 0u, 0u };
                    else if (rop == 0x00FF0062u) b = { 0u, 255u, 255u, 255u, 0u };
                    std::vector<std::vector<DetailVectorAndRawImg::Pt2f>> rr = { { {d0.x, d0.y}, {d1.x, d0.y}, {d1.x, d1.y}, {d0.x, d1.y} } };
                    if (FillPolygons4x4AA(canvas, outW, outH, rr, true, b, dc.bkMode, dc.bkR, dc.bkG, dc.bkB)) ++drawnCount;
                }
            }
            break;
        case 81: // EMR_STRETCHDIBITS
            if (rSize >= 80u) {
                float xd = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 24, false));
                float yd = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 28, false));
                int32_t xs = DetailVectorAndRawImg::RdS32(rec + 32, false);
                int32_t ys = DetailVectorAndRawImg::RdS32(rec + 36, false);
                int32_t ws = DetailVectorAndRawImg::RdS32(rec + 40, false);
                int32_t hs = DetailVectorAndRawImg::RdS32(rec + 44, false);
                uint32_t offBmi  = DetailLayeredAndHdrImg::RdU32LE(rec + 48);
                uint32_t cbBmi   = DetailLayeredAndHdrImg::RdU32LE(rec + 52);
                uint32_t offBits = DetailLayeredAndHdrImg::RdU32LE(rec + 56);
                uint32_t cbBits  = DetailLayeredAndHdrImg::RdU32LE(rec + 60);
                float wd = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 72, false));
                float hd = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 76, false));
                if (cbBmi >= 40u && cbBits > 0u && offBmi + cbBmi <= rSize && offBits + cbBits <= rSize) {
                    auto d0 = mapPt(xd, yd);
                    auto d1 = mapPt(xd + wd, yd + hd);
                    if (BlitSplitOrPackedDib(canvas, outW, outH, d0.x, d0.y, d1.x, d1.y,
                                             xs, ys, ws, hs, rec + offBmi, cbBmi, rec + offBits, cbBits)) {
                        ++drawnCount;
                    }
                }
            }
            break;
        case 83: // EMR_EXTTEXTOUTA
        case 84: // EMR_EXTTEXTOUTW
            if (rSize >= 76u) {
                float rx = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 36, false));
                float ry = static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 40, false));
                uint32_t nChars = DetailLayeredAndHdrImg::RdU32LE(rec + 44);
                uint32_t offStr = DetailLayeredAndHdrImg::RdU32LE(rec + 48);
                uint32_t opts   = DetailLayeredAndHdrImg::RdU32LE(rec + 52);
                if ((opts & 0x02u) != 0u) { // ETO_OPAQUE background box
                    auto b0 = mapPt(static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 56, false)), static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 60, false)));
                    auto b1 = mapPt(static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 64, false)), static_cast<float>(DetailVectorAndRawImg::RdS32(rec + 68, false)));
                    GdiBrush bgB{ 0u, dc.bkR, dc.bkG, dc.bkB, 0u };
                    std::vector<std::vector<DetailVectorAndRawImg::Pt2f>> rr = { { {b0.x, b0.y}, {b1.x, b0.y}, {b1.x, b1.y}, {b0.x, b1.y} } };
                    if (FillPolygons4x4AA(canvas, outW, outH, rr, true, bgB, 2u, dc.bkR, dc.bkG, dc.bkB)) ++drawnCount;
                }
                const size_t chBytes = (rType == 84u) ? 2u : 1u;
                if (nChars > 0u && nChars <= 4096u && offStr + static_cast<size_t>(nChars) * chBytes <= rSize) {
                    std::string ascii;
                    ascii.reserve(nChars);
                    for (uint32_t i = 0; i < nChars; ++i) {
                        uint16_t cp = (rType == 84u) ? DetailLayeredAndHdrImg::RdU16LE(rec + offStr + i * 2u) : rec[offStr + i];
                        ascii.push_back((cp >= 32u && cp < 127u) ? static_cast<char>(cp) : '?');
                    }
                    auto p0 = mapPt(rx, ry);
                    auto pH = mapPt(rx, ry + std::max(8.0f, dc.font.height));
                    int scale = std::clamp(static_cast<int>(std::round(std::abs(pH.y - p0.y) / 8.0f)), 1, 6);
                    int tx = static_cast<int>(std::lround(p0.x));
                    int ty = static_cast<int>(std::lround(p0.y));
                    if (tx < static_cast<int>(outW) && ty < static_cast<int>(outH)) {
                        DetailLayeredAndHdrImg::DrawBitmapText5x7(
                            canvas, outW, outH,
                            static_cast<uint32_t>(std::max(0, tx)),
                            static_cast<uint32_t>(std::max(0, ty)),
                            ascii, scale, dc.textR, dc.textG, dc.textB);
                        ++drawnCount;
                    }
                }
            }
            break;
        default:
            break;
        }
        pos += rSize;
    }

    if (drawnCount == 0u) return false;

    outInfo.width = outW;
    outInfo.height = outH;
    outInfo.origWidth = static_cast<uint32_t>(std::max(1.0f, std::round(devW)));
    outInfo.origHeight = static_cast<uint32_t>(std::max(1.0f, std::round(devH)));
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Windows Enhanced Metafile (.emf)";
    outInfo.bgraPixels = std::move(canvas);
    outInfo.valid = true;
    return true;
}


} // namespace DetailEmfWmf

inline bool DecodeWindowsEmf(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 18) return false;
    if (size >= 88 && DetailLayeredAndHdrImg::RdU32LE(data) == 1u && DetailLayeredAndHdrImg::RdU32LE(data + 40) == 0x464D4520u) {
        if (DetailEmfWmf::DecodeEmfPureCpp(data, size, outInfo)) return true;
    }
#if !defined(_WIN32) && !defined(E2_WIN32_HAL)
    return false;
#else
    if (size < 22) return false;
    HENHMETAFILE hEmf = SetEnhMetaFileBits(static_cast<UINT>(size), data);
    return DetailEmfWmf::RenderEnhMetaFileViaGdi(hEmf, L"Windows Enhanced Metafile (.emf)", outInfo);
#endif
}

} // namespace Formats
