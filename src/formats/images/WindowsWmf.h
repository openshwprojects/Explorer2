#pragma once
// WindowsWmf.h — [MS-WMF] Windows / Aldus Placeable Metafile Vector & DIB Interpreter (.wmf)

namespace Formats {

namespace DetailEmfWmf {

// Pure-C++20 [MS-WMF] Windows / Aldus Placeable Metafile Interpreter
inline bool DecodeWmfPureCpp(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 18) return false;
    size_t wmfOff = 0;
    float initOrgX = 0.0f, initOrgY = 0.0f;
    float initExtX = 512.0f, initExtY = 384.0f;
    bool hasPlaceable = false;

    if (size >= 40 && DetailImg100::RdU32LE(data) == 0x9AC6CDD7u) {
        int16_t left   = DetailImg200::RdS16(data + 6,  false);
        int16_t top    = DetailImg200::RdS16(data + 8,  false);
        int16_t right  = DetailImg200::RdS16(data + 10, false);
        int16_t bottom = DetailImg200::RdS16(data + 12, false);
        if (right != left && bottom != top) {
            initOrgX = static_cast<float>(left);
            initOrgY = static_cast<float>(top);
            initExtX = static_cast<float>(right - left);
            initExtY = static_cast<float>(bottom - top);
            hasPlaceable = true;
        }
        wmfOff = 22u;
    }

    if (wmfOff + 18u > size) return false;
    const uint8_t* mh = data + wmfOff;
    uint16_t mtType       = DetailImg100::RdU16LE(mh + 0);
    uint16_t mtHeaderSize = DetailImg100::RdU16LE(mh + 2);
    uint16_t mtVersion    = DetailImg100::RdU16LE(mh + 4);
    if ((mtType != 1u && mtType != 2u) || mtHeaderSize != 9u) return false;
    if (mtVersion != 0x0100u && mtVersion != 0x0300u) return false;

    // Quick pre-scan for initial META_SETWINDOWORG / META_SETWINDOWEXT & geometry bounds if no placeable header
    if (!hasPlaceable) {
        size_t sp = wmfOff + 18u;
        float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
        bool sawWinExt = false;
        while (sp + 6u <= size) {
            uint32_t szWords = DetailImg100::RdU32LE(data + sp);
            uint16_t fn      = DetailImg100::RdU16LE(data + sp + 4);
            if (fn == 0u || szWords < 3u || szWords > (size - sp) / 2u) break;
            const uint8_t* prm = data + sp + 6u;
            size_t prmBytes = static_cast<size_t>(szWords - 3u) * 2u;
            if (fn == 0x020Bu && prmBytes >= 4u) { // META_SETWINDOWORG(y, x)
                initOrgY = static_cast<float>(DetailImg200::RdS16(prm + 0, false));
                initOrgX = static_cast<float>(DetailImg200::RdS16(prm + 2, false));
            } else if (fn == 0x020Cu && prmBytes >= 4u) { // META_SETWINDOWEXT(y, x)
                float ey = static_cast<float>(DetailImg200::RdS16(prm + 0, false));
                float ex = static_cast<float>(DetailImg200::RdS16(prm + 2, false));
                if (std::abs(ex) >= 1.0f && std::abs(ey) >= 1.0f) {
                    initExtX = ex; initExtY = ey;
                    sawWinExt = true;
                    break;
                }
            } else if ((fn == 0x041Bu || fn == 0x0418u) && prmBytes >= 8u) {
                float b = DetailImg200::RdS16(prm + 0, false), r = DetailImg200::RdS16(prm + 2, false);
                float t = DetailImg200::RdS16(prm + 4, false), l = DetailImg200::RdS16(prm + 6, false);
                minX = std::min({ minX, l, r }); maxX = std::max({ maxX, l, r });
                minY = std::min({ minY, t, b }); maxY = std::max({ maxY, t, b });
            } else if ((fn == 0x0324u || fn == 0x0325u) && prmBytes >= 6u) {
                uint16_t n = DetailImg100::RdU16LE(prm);
                for (uint16_t k = 0; k < n && 2u + static_cast<size_t>(k + 1u) * 4u <= prmBytes; ++k) {
                    float px = DetailImg200::RdS16(prm + 2u + k * 4u, false);
                    float py = DetailImg200::RdS16(prm + 4u + k * 4u, false);
                    minX = std::min(minX, px); maxX = std::max(maxX, px);
                    minY = std::min(minY, py); maxY = std::max(maxY, py);
                }
            }
            sp += static_cast<size_t>(szWords) * 2u;
        }
        if (!sawWinExt && maxX > minX && maxY > minY) {
            float padX = std::max(4.0f, (maxX - minX) * 0.05f);
            float padY = std::max(4.0f, (maxY - minY) * 0.05f);
            initOrgX = minX - padX;
            initOrgY = minY - padY;
            initExtX = (maxX - minX) + 2.0f * padX;
            initExtY = (maxY - minY) + 2.0f * padY;
        }
    }

    float aspect = std::abs(initExtX) / std::max(1.0f, std::abs(initExtY));
    uint32_t outW = 512u, outH = 512u;
    if (aspect >= 1.0f) outH = std::clamp(static_cast<uint32_t>(std::lround(512.0f / aspect)), 64u, 512u);
    else                outW = std::clamp(static_cast<uint32_t>(std::lround(512.0f * aspect)), 64u, 512u);

    std::vector<uint8_t> canvas(static_cast<size_t>(outW) * outH * 4u, 255u);
    GdiDcState dc;
    dc.winOrgX = initOrgX; dc.winOrgY = initOrgY;
    dc.winExtX = initExtX; dc.winExtY = initExtY;
    dc.vpOrgX  = 0.0f;     dc.vpOrgY  = 0.0f;
    dc.vpExtX  = static_cast<float>(outW);
    dc.vpExtY  = static_cast<float>(outH);
    std::vector<GdiDcState> dcStack;

    // WMF Object Table: allocates lowest unused index on Create*, frees on META_DELETEOBJECT
    std::vector<GdiObjectSlot> objTable;
    auto allocWmfObject = [&](const GdiObjectSlot& slot) {
        for (auto& s : objTable) {
            if (s.kind == GdiObjectSlot::Empty) { s = slot; return; }
        }
        if (objTable.size() < 4096u) objTable.push_back(slot);
    };

    auto mapPt = [&](float lx, float ly) -> DetailImg200::Pt2f {
        float ex = (std::abs(dc.winExtX) > 1e-6f) ? dc.winExtX : 1.0f;
        float ey = (std::abs(dc.winExtY) > 1e-6f) ? dc.winExtY : 1.0f;
        return {
            ((lx - dc.winOrgX) / ex) * dc.vpExtX + dc.vpOrgX,
            ((ly - dc.winOrgY) / ey) * dc.vpExtY + dc.vpOrgY
        };
    };

    auto mapPenWidth = [&]() -> float {
        float sx = std::abs(dc.vpExtX) / std::max(1.0f, std::abs(dc.winExtX));
        return std::max(1.0f, dc.pen.width * sx);
    };

    size_t drawnCount = 0;
    auto paintClosedRing = [&](const std::vector<DetailImg200::Pt2f>& ring) {
        if (ring.size() < 3u) return;
        std::vector<std::vector<DetailImg200::Pt2f>> rings = { ring };
        if (FillPolygons4x4AA(canvas, outW, outH, rings, dc.polyFillMode != 2u, dc.brush, dc.bkMode, dc.bkR, dc.bkG, dc.bkB)) ++drawnCount;
        if (StrokePolylineAA(canvas, outW, outH, ring, true, dc.pen, mapPenWidth())) ++drawnCount;
    };

    size_t pos = wmfOff + 18u;
    size_t guard = 0;
    while (pos + 6u <= size && ++guard <= 262144u) {
        uint32_t szWords = DetailImg100::RdU32LE(data + pos);
        uint16_t fn      = DetailImg100::RdU16LE(data + pos + 4);
        if (fn == 0x0000u) break; // META_EOF
        if (szWords < 3u || szWords > (size - pos) / 2u) break;

        const uint8_t* prm = data + pos + 6u;
        const size_t prmBytes = static_cast<size_t>(szWords - 3u) * 2u;

        switch (fn) {
        case 0x001E: // META_SAVEDC
            if (dcStack.size() < 64u) dcStack.push_back(dc);
            break;
        case 0x0127: // META_RESTOREDC
            if (prmBytes >= 2u && !dcStack.empty()) {
                int16_t rel = DetailImg200::RdS16(prm, false);
                size_t pops = (rel < 0) ? static_cast<size_t>(std::min<int>(-rel, static_cast<int>(dcStack.size()))) : 1u;
                for (size_t k = 0; k < pops && !dcStack.empty(); ++k) { dc = dcStack.back(); dcStack.pop_back(); }
            }
            break;
        case 0x0102: if (prmBytes >= 2u) dc.bkMode = DetailImg100::RdU16LE(prm); break;       // META_SETBKMODE
        case 0x0103: if (prmBytes >= 2u) dc.mapMode = DetailImg100::RdU16LE(prm); break;      // META_SETMAPMODE
        case 0x0106: if (prmBytes >= 2u) dc.polyFillMode = DetailImg100::RdU16LE(prm); break; // META_SETPOLYFILLMODE
        case 0x012E: if (prmBytes >= 2u) dc.textAlign = DetailImg100::RdU16LE(prm); break;    // META_SETTEXTALIGN
        case 0x0201: if (prmBytes >= 4u) UnpackColorRef(DetailImg100::RdU32LE(prm), dc.bkR, dc.bkG, dc.bkB); break;
        case 0x0209: if (prmBytes >= 4u) UnpackColorRef(DetailImg100::RdU32LE(prm), dc.textR, dc.textG, dc.textB); break;
        case 0x020B: // META_SETWINDOWORG(y, x)
            if (prmBytes >= 4u) {
                dc.winOrgY = static_cast<float>(DetailImg200::RdS16(prm + 0, false));
                dc.winOrgX = static_cast<float>(DetailImg200::RdS16(prm + 2, false));
            }
            break;
        case 0x020C: // META_SETWINDOWEXT(y, x)
            if (prmBytes >= 4u) {
                float ey = static_cast<float>(DetailImg200::RdS16(prm + 0, false));
                float ex = static_cast<float>(DetailImg200::RdS16(prm + 2, false));
                if (std::abs(ex) > 0.5f && std::abs(ey) > 0.5f) { dc.winExtX = ex; dc.winExtY = ey; }
            }
            break;
        case 0x020D: // META_SETVIEWPORTORG(y, x)
            if (prmBytes >= 4u) {
                dc.vpOrgY = static_cast<float>(DetailImg200::RdS16(prm + 0, false));
                dc.vpOrgX = static_cast<float>(DetailImg200::RdS16(prm + 2, false));
            }
            break;
        case 0x020E: // META_SETVIEWPORTEXT(y, x)
            if (prmBytes >= 4u) {
                float ey = static_cast<float>(DetailImg200::RdS16(prm + 0, false));
                float ex = static_cast<float>(DetailImg200::RdS16(prm + 2, false));
                if (std::abs(ex) > 0.5f && std::abs(ey) > 0.5f) { dc.vpExtX = ex; dc.vpExtY = ey; }
            }
            break;
        case 0x02FA: // META_CREATEPENINDIRECT: [style:2][widthX:2][widthY:2][colorRef:4]
            if (prmBytes >= 10u) {
                GdiObjectSlot s;
                s.kind = GdiObjectSlot::Pen;
                s.pen.style = DetailImg100::RdU16LE(prm + 0);
                s.pen.width = static_cast<float>(std::max<int>(0, DetailImg200::RdS16(prm + 2, false)));
                UnpackColorRef(DetailImg100::RdU32LE(prm + 6), s.pen.r, s.pen.g, s.pen.b);
                allocWmfObject(s);
            }
            break;
        case 0x02FC: // META_CREATEBRUSHINDIRECT: [style:2][colorRef:4][hatch:2]
            if (prmBytes >= 8u) {
                GdiObjectSlot s;
                s.kind = GdiObjectSlot::Brush;
                s.brush.style = DetailImg100::RdU16LE(prm + 0);
                UnpackColorRef(DetailImg100::RdU32LE(prm + 2), s.brush.r, s.brush.g, s.brush.b);
                s.brush.hatch = DetailImg100::RdU16LE(prm + 6);
                allocWmfObject(s);
            }
            break;
        case 0x02FB: // META_CREATEFONTINDIRECT
            if (prmBytes >= 6u) {
                GdiObjectSlot s;
                s.kind = GdiObjectSlot::Font;
                s.font.height = static_cast<float>(std::abs(DetailImg200::RdS16(prm + 0, false)));
                s.font.escapementDeg = static_cast<float>(DetailImg200::RdS16(prm + 4, false)) * 0.1f;
                allocWmfObject(s);
            }
            break;
        case 0x00F7: // META_CREATEPALETTE
        case 0x0142: // META_DIBCREATEPATTERNBRUSH
        case 0x01F9: // META_CREATEPATTERNBRUSH
        case 0x06FF: // META_CREATEREGION
        {
            GdiObjectSlot s;
            s.kind = GdiObjectSlot::Other;
            allocWmfObject(s);
            break;
        }
        case 0x012D: // META_SELECTOBJECT
            if (prmBytes >= 2u) {
                uint16_t idx = DetailImg100::RdU16LE(prm);
                if ((idx & 0x8000u) != 0u) {
                    ApplyStockObject(0x80000000u | (idx & 0x7FFFu), dc);
                } else if (idx < objTable.size()) {
                    const auto& o = objTable[idx];
                    if (o.kind == GdiObjectSlot::Pen)   dc.pen   = o.pen;
                    if (o.kind == GdiObjectSlot::Brush) dc.brush = o.brush;
                    if (o.kind == GdiObjectSlot::Font)  dc.font  = o.font;
                }
            }
            break;
        case 0x01F0: // META_DELETEOBJECT
            if (prmBytes >= 2u) {
                uint16_t idx = DetailImg100::RdU16LE(prm);
                if (idx < objTable.size()) objTable[idx].kind = GdiObjectSlot::Empty;
            }
            break;
        case 0x0214: // META_MOVETO(y, x)
            if (prmBytes >= 4u) {
                dc.curY = static_cast<float>(DetailImg200::RdS16(prm + 0, false));
                dc.curX = static_cast<float>(DetailImg200::RdS16(prm + 2, false));
            }
            break;
        case 0x0213: // META_LINETO(y, x)
            if (prmBytes >= 4u) {
                float ny = static_cast<float>(DetailImg200::RdS16(prm + 0, false));
                float nx = static_cast<float>(DetailImg200::RdS16(prm + 2, false));
                std::vector<DetailImg200::Pt2f> seg = { mapPt(dc.curX, dc.curY), mapPt(nx, ny) };
                if (StrokePolylineAA(canvas, outW, outH, seg, false, dc.pen, mapPenWidth())) ++drawnCount;
                dc.curX = nx; dc.curY = ny;
            }
            break;
        case 0x041B: // META_RECTANGLE(bottom, right, top, left)
            if (prmBytes >= 8u) {
                float b = static_cast<float>(DetailImg200::RdS16(prm + 0, false));
                float r = static_cast<float>(DetailImg200::RdS16(prm + 2, false));
                float t = static_cast<float>(DetailImg200::RdS16(prm + 4, false));
                float l = static_cast<float>(DetailImg200::RdS16(prm + 6, false));
                paintClosedRing({ mapPt(l, t), mapPt(r, t), mapPt(r, b), mapPt(l, b) });
            }
            break;
        case 0x061C: // META_ROUNDRECT(eh, ew, bottom, right, top, left)
            if (prmBytes >= 12u) {
                float eh = static_cast<float>(DetailImg200::RdS16(prm + 0, false));
                float ew = static_cast<float>(DetailImg200::RdS16(prm + 2, false));
                auto p0 = mapPt(static_cast<float>(DetailImg200::RdS16(prm + 10, false)), static_cast<float>(DetailImg200::RdS16(prm + 8, false)));
                auto p1 = mapPt(static_cast<float>(DetailImg200::RdS16(prm + 6, false)),  static_cast<float>(DetailImg200::RdS16(prm + 4, false)));
                auto c0 = mapPt(0.0f, 0.0f);
                auto c1 = mapPt(ew, eh);
                std::vector<DetailImg200::Pt2f> ring;
                BuildRoundRectRing(p0.x, p0.y, p1.x, p1.y, std::abs(c1.x - c0.x), std::abs(c1.y - c0.y), ring);
                paintClosedRing(ring);
            }
            break;
        case 0x0418: // META_ELLIPSE(bottom, right, top, left)
            if (prmBytes >= 8u) {
                auto p0 = mapPt(static_cast<float>(DetailImg200::RdS16(prm + 6, false)), static_cast<float>(DetailImg200::RdS16(prm + 4, false)));
                auto p1 = mapPt(static_cast<float>(DetailImg200::RdS16(prm + 2, false)), static_cast<float>(DetailImg200::RdS16(prm + 0, false)));
                std::vector<DetailImg200::Pt2f> ring;
                BuildEllipseRing(p0.x, p0.y, p1.x, p1.y, ring);
                paintClosedRing(ring);
            }
            break;
        case 0x0817: // META_ARC
        case 0x081A: // META_PIE
        case 0x0830: // META_CHORD
            if (prmBytes >= 16u) {
                auto eP = mapPt(static_cast<float>(DetailImg200::RdS16(prm + 2, false)),  static_cast<float>(DetailImg200::RdS16(prm + 0, false)));
                auto sP = mapPt(static_cast<float>(DetailImg200::RdS16(prm + 6, false)),  static_cast<float>(DetailImg200::RdS16(prm + 4, false)));
                auto b1 = mapPt(static_cast<float>(DetailImg200::RdS16(prm + 10, false)), static_cast<float>(DetailImg200::RdS16(prm + 8, false)));
                auto b0 = mapPt(static_cast<float>(DetailImg200::RdS16(prm + 14, false)), static_cast<float>(DetailImg200::RdS16(prm + 12, false)));
                std::vector<DetailImg200::Pt2f> arcPts;
                BuildEllipticalArcPoints(b0.x, b0.y, b1.x, b1.y, sP.x, sP.y, eP.x, eP.y, true, arcPts);
                if (fn == 0x081Au && arcPts.size() >= 2u) {
                    arcPts.push_back({ (b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f });
                    paintClosedRing(arcPts);
                } else if (fn == 0x0830u && arcPts.size() >= 3u) {
                    paintClosedRing(arcPts);
                } else if (!arcPts.empty()) {
                    if (StrokePolylineAA(canvas, outW, outH, arcPts, false, dc.pen, mapPenWidth())) ++drawnCount;
                }
            }
            break;
        case 0x0324: // META_POLYGON
        case 0x0325: // META_POLYLINE
            if (prmBytes >= 6u) {
                uint16_t nPts = DetailImg100::RdU16LE(prm);
                if (nPts >= 2u && 2u + static_cast<size_t>(nPts) * 4u <= prmBytes) {
                    std::vector<DetailImg200::Pt2f> pts;
                    pts.reserve(nPts);
                    for (uint16_t k = 0; k < nPts; ++k) {
                        float px = static_cast<float>(DetailImg200::RdS16(prm + 2u + k * 4u, false));
                        float py = static_cast<float>(DetailImg200::RdS16(prm + 4u + k * 4u, false));
                        pts.push_back(mapPt(px, py));
                    }
                    if (fn == 0x0324u) paintClosedRing(pts);
                    else if (StrokePolylineAA(canvas, outW, outH, pts, false, dc.pen, mapPenWidth())) ++drawnCount;
                }
            }
            break;
        case 0x0538: // META_POLYPOLYGON
            if (prmBytes >= 8u) {
                uint16_t nPolys = DetailImg100::RdU16LE(prm);
                size_t ptsOff = 2u + static_cast<size_t>(nPolys) * 2u;
                if (nPolys > 0u && ptsOff <= prmBytes) {
                    std::vector<std::vector<DetailImg200::Pt2f>> rings;
                    for (uint16_t p = 0; p < nPolys; ++p) {
                        uint16_t cnt = DetailImg100::RdU16LE(prm + 2u + p * 2u);
                        if (ptsOff + static_cast<size_t>(cnt) * 4u > prmBytes) break;
                        std::vector<DetailImg200::Pt2f> ring;
                        ring.reserve(cnt);
                        for (uint16_t k = 0; k < cnt; ++k) {
                            float px = static_cast<float>(DetailImg200::RdS16(prm + ptsOff + k * 4u, false));
                            float py = static_cast<float>(DetailImg200::RdS16(prm + ptsOff + k * 4u + 2u, false));
                            ring.push_back(mapPt(px, py));
                        }
                        ptsOff += static_cast<size_t>(cnt) * 4u;
                        rings.push_back(std::move(ring));
                    }
                    if (FillPolygons4x4AA(canvas, outW, outH, rings, dc.polyFillMode != 2u, dc.brush, dc.bkMode, dc.bkR, dc.bkG, dc.bkB)) ++drawnCount;
                    for (const auto& r : rings) {
                        if (StrokePolylineAA(canvas, outW, outH, r, true, dc.pen, mapPenWidth())) ++drawnCount;
                    }
                }
            }
            break;
        case 0x0F43: // META_STRETCHDIB
            if (prmBytes > 62u) {
                int16_t sh = DetailImg200::RdS16(prm + 6,  false);
                int16_t sw = DetailImg200::RdS16(prm + 8,  false);
                int16_t sy = DetailImg200::RdS16(prm + 10, false);
                int16_t sx = DetailImg200::RdS16(prm + 12, false);
                float dh = static_cast<float>(DetailImg200::RdS16(prm + 14, false));
                float dw = static_cast<float>(DetailImg200::RdS16(prm + 16, false));
                float dy = static_cast<float>(DetailImg200::RdS16(prm + 18, false));
                float dx = static_cast<float>(DetailImg200::RdS16(prm + 20, false));
                auto d0 = mapPt(dx, dy);
                auto d1 = mapPt(dx + dw, dy + dh);
                if (BlitSplitOrPackedDib(canvas, outW, outH, d0.x, d0.y, d1.x, d1.y,
                                         sx, sy, sw, sh, prm + 22u, prmBytes - 22u, nullptr, 0u)) {
                    ++drawnCount;
                }
            }
            break;
        case 0x0B41: // META_DIBSTRETCHBLT
            if (prmBytes > 60u) {
                int16_t sh = DetailImg200::RdS16(prm + 4,  false);
                int16_t sw = DetailImg200::RdS16(prm + 6,  false);
                int16_t sy = DetailImg200::RdS16(prm + 8,  false);
                int16_t sx = DetailImg200::RdS16(prm + 10, false);
                float dh = static_cast<float>(DetailImg200::RdS16(prm + 12, false));
                float dw = static_cast<float>(DetailImg200::RdS16(prm + 14, false));
                float dy = static_cast<float>(DetailImg200::RdS16(prm + 16, false));
                float dx = static_cast<float>(DetailImg200::RdS16(prm + 18, false));
                auto d0 = mapPt(dx, dy);
                auto d1 = mapPt(dx + dw, dy + dh);
                if (BlitSplitOrPackedDib(canvas, outW, outH, d0.x, d0.y, d1.x, d1.y,
                                         sx, sy, sw, sh, prm + 20u, prmBytes - 20u, nullptr, 0u)) {
                    ++drawnCount;
                }
            }
            break;
        case 0x0521: // META_TEXTOUT: [len:2][string:padded even][y:2][x:2]
            if (prmBytes >= 6u) {
                uint16_t sLen = DetailImg100::RdU16LE(prm);
                size_t padLen = static_cast<size_t>(sLen) + (sLen & 1u);
                if (sLen > 0u && 2u + padLen + 4u <= prmBytes) {
                    float ly = static_cast<float>(DetailImg200::RdS16(prm + 2u + padLen, false));
                    float lx = static_cast<float>(DetailImg200::RdS16(prm + 4u + padLen, false));
                    std::string s(reinterpret_cast<const char*>(prm + 2u), sLen);
                    auto p0 = mapPt(lx, ly);
                    auto pH = mapPt(lx, ly + std::max(8.0f, dc.font.height));
                    int scale = std::clamp(static_cast<int>(std::round(std::abs(pH.y - p0.y) / 8.0f)), 1, 6);
                    DetailImg100::DrawBitmapText5x7(
                        canvas, outW, outH,
                        static_cast<uint32_t>(std::max(0, static_cast<int>(std::lround(p0.x)))),
                        static_cast<uint32_t>(std::max(0, static_cast<int>(std::lround(p0.y)))),
                        s, scale, dc.textR, dc.textG, dc.textB);
                    ++drawnCount;
                }
            }
            break;
        case 0x0A32: // META_EXTTEXTOUT: [y:2][x:2][len:2][opts:2][optRect:8][string...]
            if (prmBytes >= 8u) {
                float ly = static_cast<float>(DetailImg200::RdS16(prm + 0, false));
                float lx = static_cast<float>(DetailImg200::RdS16(prm + 2, false));
                uint16_t sLen = DetailImg100::RdU16LE(prm + 4);
                uint16_t opts = DetailImg100::RdU16LE(prm + 6);
                size_t strOff = ((opts & 0x06u) != 0u) ? 16u : 8u;
                if ((opts & 0x02u) != 0u && prmBytes >= 16u) {
                    auto b0 = mapPt(static_cast<float>(DetailImg200::RdS16(prm + 8, false)),  static_cast<float>(DetailImg200::RdS16(prm + 10, false)));
                    auto b1 = mapPt(static_cast<float>(DetailImg200::RdS16(prm + 12, false)), static_cast<float>(DetailImg200::RdS16(prm + 14, false)));
                    GdiBrush bgB{ 0u, dc.bkR, dc.bkG, dc.bkB, 0u };
                    std::vector<std::vector<DetailImg200::Pt2f>> rr = { { {b0.x, b0.y}, {b1.x, b0.y}, {b1.x, b1.y}, {b0.x, b1.y} } };
                    if (FillPolygons4x4AA(canvas, outW, outH, rr, true, bgB, 2u, dc.bkR, dc.bkG, dc.bkB)) ++drawnCount;
                }
                if (sLen > 0u && strOff + sLen <= prmBytes) {
                    std::string s(reinterpret_cast<const char*>(prm + strOff), sLen);
                    auto p0 = mapPt(lx, ly);
                    auto pH = mapPt(lx, ly + std::max(8.0f, dc.font.height));
                    int scale = std::clamp(static_cast<int>(std::round(std::abs(pH.y - p0.y) / 8.0f)), 1, 6);
                    DetailImg100::DrawBitmapText5x7(
                        canvas, outW, outH,
                        static_cast<uint32_t>(std::max(0, static_cast<int>(std::lround(p0.x)))),
                        static_cast<uint32_t>(std::max(0, static_cast<int>(std::lround(p0.y)))),
                        s, scale, dc.textR, dc.textG, dc.textB);
                    ++drawnCount;
                }
            }
            break;
        default:
            break;
        }
        pos += static_cast<size_t>(szWords) * 2u;
    }

    if (drawnCount == 0u) return false;

    outInfo.width = outW;
    outInfo.height = outH;
    outInfo.origWidth = static_cast<uint32_t>(std::max(1.0f, std::round(std::abs(initExtX))));
    outInfo.origHeight = static_cast<uint32_t>(std::max(1.0f, std::round(std::abs(initExtY))));
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Windows Metafile (.wmf)";
    outInfo.bgraPixels = std::move(canvas);
    outInfo.valid = true;
    return true;
}


} // namespace DetailEmfWmf

inline bool DecodeWindowsWmf(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 18) return false;
    if (DetailImg100::RdU32LE(data) == 0x9AC6CDD7u ||
        ((DetailImg100::RdU16LE(data) == 1u || DetailImg100::RdU16LE(data) == 2u) && DetailImg100::RdU16LE(data + 2) == 9u))
    {
        if (DetailEmfWmf::DecodeWmfPureCpp(data, size, outInfo)) return true;
    }
#if !defined(_WIN32) && !defined(E2_WIN32_HAL)
    return false;
#else
    if (size < 22) return false;
    size_t wmfOff = 0;
    if (DetailImg100::RdU32LE(data) == 0x9AC6CDD7u) {
        if (size <= 22) return false;
        wmfOff = 22;
    }
    HENHMETAFILE hEmf = SetWinMetaFileBits(static_cast<UINT>(size - wmfOff), data + wmfOff, nullptr, nullptr);
    return DetailEmfWmf::RenderEnhMetaFileViaGdi(hEmf, L"Windows Metafile (.wmf)", outInfo);
#endif
}

} // namespace Formats
