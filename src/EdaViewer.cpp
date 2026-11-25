#include "EdaViewer.h"
#include "EdaTrackDraw.h"
#include "FormatSupport.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "Ole2Compound.h"
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <tuple>
#include <unordered_map>

namespace Explorer2 {

namespace {

static D2D1_COLOR_F EdaColor(uint32_t rgb, float a = 1.0f) {
    return PathUtils::ColorFromRgb(rgb, a);
}

// One opaque colour for `rgb` seen through with `alpha` over the board: tracks are drawn with round ends that
// overlap at every joint (EdaTrackDraw.h), where a see-through colour would come out darker
static D2D1_COLOR_F EdaColorOverBoard(uint32_t rgb, float alpha) {
    constexpr uint32_t kBoard = 0x0E261E;       // the board's fill (0x0F291E at 85 %) as it shows over the canvas
    uint32_t out = 0;
    for (int shift : { 16, 8, 0 }) {
        const float c = static_cast<float>((rgb >> shift) & 0xFF) * alpha + static_cast<float>((kBoard >> shift) & 0xFF) * (1.0f - alpha);
        out |= static_cast<uint32_t>(std::lround(c)) << shift;
    }
    return EdaColor(out);
}

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct ProjectedQuad3D {
    D2D1_POINT_2F pts[4] = {};
    float depth = 0.0f;
    uint32_t fillRgb = 0x1E293B;
    uint32_t strokeRgb = 0x0F172A;
    float alpha = 1.0f;
    bool drawStroke = false;
};

} // namespace


EdaViewer::~EdaViewer() {
    Shutdown();
}

void EdaViewer::Initialize(HWND notifyHwnd, ID2D1Factory* d2dFactory, IDWriteFactory* dwriteFactory) {
    m_notifyHwnd = notifyHwnd;
    m_d2dFactory = d2dFactory;
    if (dwriteFactory && !m_fmtSmallTrim) {
        PathUtils::CreateTrimmedTextFormat(dwriteFactory, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 11.0f, &m_fmtSmallTrim);
    }
}

void EdaViewer::Shutdown() {
    Clear();
    if (m_fmtSmallTrim) {
        m_fmtSmallTrim->Release();
        m_fmtSmallTrim = nullptr;
    }
}

void EdaViewer::Clear() {
    m_currentPath.clear();
    m_board = ParsedEdaBoard{};
    m_selectedCompIdx = -1;
}

void EdaViewer::Fit2DView() {
    m_zoom2D = 1.0f;
    m_pan2DX = 0.0f;
    m_pan2DY = 0.0f;
}

void EdaViewer::Reset3DCamera() {
    m_yaw3D = 0.62f;
    m_pitch3D = 0.68f;
    m_zoom3D = 1.0f;
    m_pan3DX = 0.0f;
    m_pan3DY = 0.0f;
}

void EdaViewer::LoadFile(const std::wstring& fullPath, bool forceReload) {
    if (!forceReload && m_currentPath == fullPath && !m_currentPath.empty()) {
        return;
    }
    ParsedEdaBoard board;
    InspectEdaFile(fullPath, board);
    ShowLoadedBoard(fullPath, std::move(board));
}

void EdaViewer::ShowLoadedBoard(const std::wstring& fullPath, ParsedEdaBoard&& board) {
    m_currentPath = fullPath;
    m_selectedCompIdx = -1;
    m_bomScrollY = 0.0f;
    m_board = std::move(board);
    Fit2DView();
    Reset3DCamera();

    // Default to 3D PCB view if it's a board with components, or 2D CAD for schematics/gerbers (user can toggle anytime!)
    m_view3D = false;
}

void EdaViewer::StepAnimation() {
    if (!m_view3D || !m_autoRotate3D) return;
    ULONGLONG now = GetTickCount64();
    float dt = (m_lastAnimTick > 0 && now > m_lastAnimTick)
        ? std::min(0.1f, static_cast<float>(now - m_lastAnimTick) * 0.001f)
        : 0.016f;
    m_lastAnimTick = now;
    m_yaw3D += dt * 0.75f;
    if (m_yaw3D > 6.2831853f) m_yaw3D -= 6.2831853f;
}

void EdaViewer::Draw2DCanvas(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& canvasR,
    const ParsedEdaBoard& board,
    Lang lang)
{
    const bool isSch = (board.kind == EdaDocKind::KiCadSch || board.kind == EdaDocKind::EagleSch);
    brush->SetColor(isSch ? EdaColor(0xFFFDF7) : EdaColor(0x0B1320));
    rt->FillRectangle(canvasR, brush);

    float availW = std::max(40.0f, canvasR.right - canvasR.left - 32.0f);
    float availH = std::max(40.0f, canvasR.bottom - canvasR.top - 32.0f);
    float bw = std::max(1.0f, board.maxX - board.minX);
    float bh = std::max(1.0f, board.maxY - board.minY);
    float baseScale = std::min(availW / bw, availH / bh);
    float scale = baseScale * m_zoom2D;

    float cx = (canvasR.left + canvasR.right) * 0.5f + m_pan2DX;
    float cy = (canvasR.top + canvasR.bottom) * 0.5f + m_pan2DY;
    float midX = (board.minX + board.maxX) * 0.5f;
    float midY = (board.minY + board.maxY) * 0.5f;

    auto toScreen = [&](float bx, float by) -> D2D1_POINT_2F {
        return D2D1::Point2F(cx + (bx - midX) * scale, cy - (by - midY) * scale);
    };

    // Subtle CAD grid
    float gridStepMm = (scale > 15.0f) ? 1.27f : ((scale > 4.0f) ? 5.0f : 10.0f);
    float stepPx = gridStepMm * scale;
    if (stepPx >= 10.0f) {
        brush->SetColor(isSch ? EdaColor(0xE2E8F0, 0.7f) : EdaColor(0x1E293B, 0.8f));
        for (float gx = std::fmod(cx - canvasR.left, stepPx) + canvasR.left; gx < canvasR.right; gx += stepPx) {
            rt->DrawLine(D2D1::Point2F(gx, canvasR.top), D2D1::Point2F(gx, canvasR.bottom), brush, 0.5f);
        }
        for (float gy = std::fmod(cy - canvasR.top, stepPx) + canvasR.top; gy < canvasR.bottom; gy += stepPx) {
            rt->DrawLine(D2D1::Point2F(canvasR.left, gy), D2D1::Point2F(canvasR.right, gy), brush, 0.5f);
        }
    }

    // Board outline substrate fill in 2D PCB mode
    if (!isSch) {
        D2D1_POINT_2F pMin = toScreen(board.minX, board.maxY);
        D2D1_POINT_2F pMax = toScreen(board.maxX, board.minY);
        D2D1_RECT_F brdR = D2D1::RectF(pMin.x, pMin.y, pMax.x, pMax.y);
        brush->SetColor(EdaColor(0x0F291E, 0.85f));
        rt->FillRectangle(brdR, brush);
        brush->SetColor(EdaColor(0xFACC15, 0.9f));
        rt->DrawRectangle(brdR, brush, 1.4f);
    }

    // Tracks and silk lines have round ends, as in KiCad, in opaque colours (EdaTrackDraw.h)
    // 1. Bottom Copper traces
    if (m_showBotCu) {
        brush->SetColor(EdaColorOverBoard(0x3B82F6, 0.85f));
        for (const auto& t : board.traces) {
            if (t.layer != EdaLayerId::BottomCopper) continue;
            DrawTrack(rt, brush, toScreen(t.x1, t.y1), toScreen(t.x2, t.y2), std::max(1.2f, t.width * scale));
        }
    }

    // 2. Top Copper & Schematic traces
    if (m_showTopCu) {
        for (const auto& t : board.traces) {
            if (t.layer == EdaLayerId::BottomCopper || t.layer == EdaLayerId::TopSilk || t.layer == EdaLayerId::BottomSilk) continue;
            if (t.layer == EdaLayerId::EdgeCuts) brush->SetColor(EdaColor(0xFACC15));
            else if (t.layer == EdaLayerId::SchWire) brush->SetColor(EdaColor(0x15803D));
            else if (t.layer == EdaLayerId::SchSymbol) brush->SetColor(EdaColor(0x991B1B));
            else if (t.layer == EdaLayerId::InnerCopper) brush->SetColor(EdaColorOverBoard(0xA855F7, 0.8f));
            else brush->SetColor(EdaColorOverBoard(0xEF4444, 0.9f));
            DrawTrack(rt, brush, toScreen(t.x1, t.y1), toScreen(t.x2, t.y2), std::max(1.2f, t.width * scale));
        }
    }

    // 3. Silkscreen lines & circles
    if (m_showSilk) {
        for (const auto& t : board.traces) {
            if (t.layer != EdaLayerId::TopSilk && t.layer != EdaLayerId::BottomSilk) continue;
            brush->SetColor(t.layer == EdaLayerId::BottomSilk ? EdaColorOverBoard(0xC084FC, 0.8f) : EdaColorOverBoard(0xF8FAFC, 0.9f));
            DrawTrack(rt, brush, toScreen(t.x1, t.y1), toScreen(t.x2, t.y2), std::max(1.0f, t.width * scale));
        }
        for (const auto& c : board.circles) {
            D2D1_POINT_2F pc = toScreen(c.cx, c.cy);
            float r = std::max(1.5f, c.radius * scale);
            brush->SetColor(EdaColor(0xF8FAFC, 0.85f));
            rt->DrawEllipse(D2D1::Ellipse(pc, r, r), brush, std::max(1.0f, c.width * scale));
        }
    }

    // 4. Pads & Vias
    if (m_showPads) {
        for (const auto& p : board.pads) {
            if (p.layer == EdaLayerId::TopCopper && !m_showTopCu) continue;
            if (p.layer == EdaLayerId::BottomCopper && !m_showBotCu) continue;
            D2D1_POINT_2F pc = toScreen(p.x, p.y);
            float hw = std::max(1.8f, p.w * scale * 0.5f);
            float hh = std::max(1.8f, p.h * scale * 0.5f);
            if (p.layer == EdaLayerId::PadsThrough) brush->SetColor(EdaColor(0x10B981));
            else if (p.layer == EdaLayerId::BottomCopper) brush->SetColor(EdaColor(0x3B82F6));
            else brush->SetColor(EdaColor(0xF59E0B));

            if (p.shape == 0) {
                rt->FillEllipse(D2D1::Ellipse(pc, hw, hh), brush);
            } else {
                D2D1_RECT_F pr = D2D1::RectF(pc.x - hw, pc.y - hh, pc.x + hw, pc.y + hh);
                rt->FillRoundedRectangle(D2D1::RoundedRect(pr, 1.5f, 1.5f), brush);
            }
            if (p.drill > 0.05f) {
                float dr = std::max(1.0f, p.drill * scale * 0.5f);
                brush->SetColor(EdaColor(0x0B1320));
                rt->FillEllipse(D2D1::Ellipse(pc, dr, dr), brush);
            }
        }

        for (const auto& v : board.vias) {
            D2D1_POINT_2F pc = toScreen(v.x, v.y);
            float r = std::max(2.0f, v.diameter * scale * 0.5f);
            float dr = std::max(0.9f, v.drill * scale * 0.5f);
            brush->SetColor(EdaColor(0xEAB308));
            rt->FillEllipse(D2D1::Ellipse(pc, r, r), brush);
            brush->SetColor(EdaColor(0x0B1320));
            rt->FillEllipse(D2D1::Ellipse(pc, dr, dr), brush);
        }
    }

    // 5. Component RefDes Labels & Selection Highlight
    if (m_showSilk) {
        for (size_t i = 0; i < board.components.size(); ++i) {
            const auto& c = board.components[i];
            D2D1_POINT_2F pc = toScreen(c.x, c.y);
            float hw = std::max(4.0f, c.widthMm * scale * 0.5f);
            float hh = std::max(3.0f, c.lengthMm * scale * 0.5f);

            if (static_cast<int>(i) == m_selectedCompIdx) {
                D2D1_RECT_F selR = D2D1::RectF(pc.x - hw - 3.0f, pc.y - hh - 3.0f, pc.x + hw + 3.0f, pc.y + hh + 3.0f);
                brush->SetColor(EdaColor(0x22D3EE, 0.28f));
                rt->FillRoundedRectangle(D2D1::RoundedRect(selR, 3.0f, 3.0f), brush);
                brush->SetColor(EdaColor(0x22D3EE));
                rt->DrawRoundedRectangle(D2D1::RoundedRect(selR, 3.0f, 3.0f), brush, 2.0f);
            }

            if (scale >= 2.2f && !c.refDes.empty()) {
                brush->SetColor(isSch ? EdaColor(0x0F172A) : (c.bottomSide ? EdaColor(0x93C5FD) : EdaColor(0xFEF08A)));
                D2D1_RECT_F tr = D2D1::RectF(pc.x - 45.0f, pc.y - 10.0f, pc.x + 45.0f, pc.y + 10.0f);
                fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                rt->DrawTextW(c.refDes.c_str(), static_cast<UINT32>(c.refDes.size()), fmtSmall, tr, brush);
                fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
        }
    }

    // 6. Board & Schematic Text Labels (board.labels)
    if ((m_showSilk || isSch) && fmtSmall && !board.labels.empty()) {
        rt->PushAxisAlignedClip(canvasR, D2D1_ANTIALIAS_MODE_ALIASED);
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        const size_t maxLabels = 2048;
        size_t drawn = 0;
        for (const auto& lbl : board.labels) {
            if (lbl.text.empty()) continue;
            D2D1_POINT_2F pt = toScreen(lbl.x, lbl.y);
            if (pt.x < canvasR.left || pt.x > canvasR.right ||
                pt.y < canvasR.top  || pt.y > canvasR.bottom) {
                continue;
            }
            if (scale >= 2.2f && m_showSilk && board.components.size() <= 256) {
                bool dupCompRef = false;
                for (const auto& c : board.components) {
                    if (c.refDes == lbl.text &&
                        std::fabs(c.x - lbl.x) < 0.05f &&
                        std::fabs(c.y - lbl.y) < 0.05f) {
                        dupCompRef = true;
                        break;
                    }
                }
                if (dupCompRef) continue;
            }

            if (isSch) {
                if (lbl.layer == EdaLayerId::SchWire) brush->SetColor(EdaColor(0x166534));
                else if (lbl.layer == EdaLayerId::SchSymbol) brush->SetColor(EdaColor(0x7F1D1D));
                else brush->SetColor(EdaColor(0x1E293B));
            } else {
                if (lbl.layer == EdaLayerId::BottomCopper || lbl.layer == EdaLayerId::BottomSilk) {
                    brush->SetColor(EdaColor(0x93C5FD));
                } else if (lbl.layer == EdaLayerId::InnerCopper) {
                    brush->SetColor(EdaColor(0xD8B4FE));
                } else if (lbl.layer == EdaLayerId::EdgeCuts) {
                    brush->SetColor(EdaColor(0xFDE047));
                } else {
                    brush->SetColor(EdaColor(0xF8FAFC));
                }
            }

            D2D1_RECT_F tr = D2D1::RectF(pt.x, pt.y - 9.0f, std::min(canvasR.right, pt.x + 260.0f), pt.y + 11.0f);
            if (tr.right > tr.left + 2.0f) {
                rt->DrawTextW(lbl.text.c_str(), static_cast<UINT32>(lbl.text.size()), fmtSmall, tr, brush);
                if (++drawn >= maxLabels) break;
            }
        }
        rt->PopAxisAlignedClip();
    }
    (void)fmtBold;
    (void)lang;
}

void EdaViewer::Draw3DPcbViewport(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& canvasR,
    const ParsedEdaBoard& board,
    Lang lang)
{
    // Studio Dark 3D background
    brush->SetColor(EdaColor(0x0F172A));
    rt->FillRectangle(canvasR, brush);

    float cx = (canvasR.left + canvasR.right) * 0.5f + m_pan3DX;
    float cy = (canvasR.top + canvasR.bottom) * 0.5f + m_pan3DY;
    float availW = std::max(80.0f, canvasR.right - canvasR.left);
    float availH = std::max(80.0f, canvasR.bottom - canvasR.top);

    float bw = std::max(4.0f, board.maxX - board.minX);
    float bh = std::max(4.0f, board.maxY - board.minY);
    float midX = (board.minX + board.maxX) * 0.5f;
    float midY = (board.minY + board.maxY) * 0.5f;
    float maxSpan = std::max(bw, bh);
    float scale = (std::min(availW, availH) * 0.72f / maxSpan) * m_zoom3D;

    float cyaw = std::cos(m_yaw3D), syaw = std::sin(m_yaw3D);
    float cpitch = std::cos(m_pitch3D), spitch = std::sin(m_pitch3D);

    auto project3D = [&](float wx, float wy, float wz, float* outDepth = nullptr) -> D2D1_POINT_2F {
        float lx = wx - midX;
        float ly = wy - midY;
        float lz = wz;
        // Rotate around Z (Yaw) then tilt around X (Pitch)
        float rx = lx * cyaw - ly * syaw;
        float ry = lx * syaw + ly * cyaw;
        float sy2 = ry * cpitch - lz * spitch;
        float sz2 = ry * spitch + lz * cpitch;
        if (outDepth) *outDepth = sz2;
        float persp = 1.0f / std::max(0.35f, 1.0f - (sz2 / (maxSpan * 3.5f)));
        return D2D1::Point2F(cx + rx * scale * persp, cy - sy2 * scale * persp);
    };

    // Solder mask color palette: Green, Blue, Red, Matte Black, Purple, White
    static const uint32_t kMaskTopColors[6]  = { 0x14532D, 0x1E3A8A, 0x7F1D1D, 0x18181B, 0x4C1D95, 0xE2E8F0 };
    static const uint32_t kMaskEdgeColors[6] = { 0xA16207, 0x854D0E, 0x854D0E, 0x52525B, 0x713F12, 0x94A3B8 };
    uint32_t maskRgb = kMaskTopColors[m_solderMaskPreset % 6];
    uint32_t edgeRgb = kMaskEdgeColors[m_solderMaskPreset % 6];

    std::vector<ProjectedQuad3D> quads;
    quads.reserve(board.components.size() * 10 + board.pads.size() + 16);

    auto pushQuad3D = [&](Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, uint32_t fill, uint32_t stroke, bool drawStrk = true, float alpha = 1.0f) {
        ProjectedQuad3D q;
        float d0 = 0, d1 = 0, d2 = 0, d3 = 0;
        q.pts[0] = project3D(p0.x, p0.y, p0.z, &d0);
        q.pts[1] = project3D(p1.x, p1.y, p1.z, &d1);
        q.pts[2] = project3D(p2.x, p2.y, p2.z, &d2);
        q.pts[3] = project3D(p3.x, p3.y, p3.z, &d3);
        q.depth = (d0 + d1 + d2 + d3) * 0.25f;
        q.fillRgb = fill;
        q.strokeRgb = stroke;
        q.drawStroke = drawStrk;
        q.alpha = alpha;
        quads.push_back(q);
    };

    auto pushBox3D = [&](float cx3, float cy3, float zMin, float zMax, float w, float l, float rotDeg,
                         uint32_t topRgb, uint32_t sideRgb, uint32_t strokeRgb) {
        float hw = w * 0.5f, hl = l * 0.5f;
        float rad = rotDeg * 3.14159265f / 180.0f;
        float cs = std::cos(rad), sn = std::sin(rad);
        auto corner = [&](float dx, float dy, float z) -> Vec3 {
            return { cx3 + (dx * cs - dy * sn), cy3 + (dx * sn + dy * cs), z };
        };
        Vec3 b0 = corner(-hw, -hl, zMin), b1 = corner(hw, -hl, zMin), b2 = corner(hw, hl, zMin), b3 = corner(-hw, hl, zMin);
        Vec3 t0 = corner(-hw, -hl, zMax), t1 = corner(hw, -hl, zMax), t2 = corner(hw, hl, zMax), t3 = corner(-hw, hl, zMax);
        pushQuad3D(b0, b1, t1, t0, sideRgb, strokeRgb, true);
        pushQuad3D(b1, b2, t2, t1, sideRgb, strokeRgb, true);
        pushQuad3D(b2, b3, t3, t2, sideRgb, strokeRgb, true);
        pushQuad3D(b3, b0, t0, t3, sideRgb, strokeRgb, true);
        pushQuad3D(t0, t1, t2, t3, topRgb, strokeRgb, true);
    };

    // 1. Draw 3D FR4 PCB Board Substrate (-0.8 mm to +0.8 mm)
    float zBot = -0.8f, zTop = 0.8f;
    Vec3 brdB0{ board.minX, board.minY, zBot }, brdB1{ board.maxX, board.minY, zBot },
         brdB2{ board.maxX, board.maxY, zBot }, brdB3{ board.minX, board.maxY, zBot };
    Vec3 brdT0{ board.minX, board.minY, zTop }, brdT1{ board.maxX, board.minY, zTop },
         brdT2{ board.maxX, board.maxY, zTop }, brdT3{ board.minX, board.maxY, zTop };

    // Board side edges (FR4 epoxy glass) + Top/Bottom solder mask planes
    pushQuad3D(brdB0, brdB1, brdT1, brdT0, edgeRgb, 0xFACC15, true);
    pushQuad3D(brdB1, brdB2, brdT2, brdT1, edgeRgb, 0xFACC15, true);
    pushQuad3D(brdB2, brdB3, brdT3, brdT2, edgeRgb, 0xFACC15, true);
    pushQuad3D(brdB3, brdB0, brdT0, brdT3, edgeRgb, 0xFACC15, true);
    pushQuad3D(brdT0, brdT1, brdT2, brdT3, maskRgb, 0x22C55E, true, 0.96f);

    // 2. Render 3D Surface Pads (ENIG Gold / HASL Silver)
    if (m_showPads) {
        for (const auto& p : board.pads) {
            float pz = (p.layer == EdaLayerId::BottomCopper) ? (zBot - 0.04f) : (zTop + 0.04f);
            float hw = p.w * 0.5f, hh = p.h * 0.5f;
            uint32_t padRgb = (p.layer == EdaLayerId::PadsThrough) ? 0xFBBF24 : 0xE2E8F0;
            pushQuad3D(
                { p.x - hw, p.y - hh, pz },
                { p.x + hw, p.y - hh, pz },
                { p.x + hw, p.y + hh, pz },
                { p.x - hw, p.y + hh, pz },
                padRgb, 0x94A3B8, false);
        }
    }

    // 3. Render 3D Extruded Electronic Components (ICs, SMD Resistors/Capacitors, Connectors, Transistors, LEDs)
    if (m_show3DParts) {
        for (size_t i = 0; i < board.components.size(); ++i) {
            const auto& c = board.components[i];
            if (c.category == EdaPartCategory::MountingHole) continue;

            bool isSelected = (static_cast<int>(i) == m_selectedCompIdx);
            float baseZ = c.bottomSide ? (zBot - c.heightMm) : zTop;
            float topZ  = c.bottomSide ? zBot : (zTop + c.heightMm);
            uint32_t stroke = isSelected ? 0x22D3EE : 0x0F172A;

            switch (c.category) {
            case EdaPartCategory::ResistorSMD: {
                // Silver metallic end-caps + dark resistive ceramic body in center
                pushBox3D(c.x, c.y, baseZ, topZ, c.widthMm, c.lengthMm, c.rotDeg, 0xCBD5E1, 0x94A3B8, stroke);
                pushBox3D(c.x, c.y, baseZ, topZ + 0.03f, c.widthMm * 0.62f, c.lengthMm * 0.96f, c.rotDeg,
                    isSelected ? 0x0284C7 : 0x1E293B, 0x0F172A, stroke);
                break;
            }
            case EdaPartCategory::CapacitorSMD: {
                // Silver metallic end-caps + warm MLCC tan/brown ceramic body
                pushBox3D(c.x, c.y, baseZ, topZ, c.widthMm, c.lengthMm, c.rotDeg, 0xCBD5E1, 0x94A3B8, stroke);
                pushBox3D(c.x, c.y, baseZ, topZ + 0.03f, c.widthMm * 0.66f, c.lengthMm * 0.96f, c.rotDeg,
                    isSelected ? 0x0284C7 : 0xB45309, 0x78350F, stroke);
                break;
            }
            case EdaPartCategory::ElectrolyticCap: {
                // Black plastic base + tall aluminum cylinder body
                pushBox3D(c.x, c.y, baseZ, baseZ + 0.6f, c.widthMm, c.lengthMm, c.rotDeg, 0x1E293B, 0x0F172A, stroke);
                pushBox3D(c.x, c.y, baseZ + 0.6f, topZ, c.widthMm * 0.82f, c.lengthMm * 0.82f, c.rotDeg + 22.5f,
                    isSelected ? 0x38BDF8 : 0x94A3B8, 0x475569, stroke);
                break;
            }
            case EdaPartCategory::IC_Package:
            case EdaPartCategory::TransistorSOT: {
                // Dark molded epoxy IC package + metallic lead frame base
                pushBox3D(c.x, c.y, baseZ, baseZ + 0.25f, c.widthMm * 1.08f, c.lengthMm * 1.08f, c.rotDeg, 0xCBD5E1, 0x94A3B8, stroke);
                pushBox3D(c.x, c.y, baseZ + 0.15f, topZ, c.widthMm * 0.88f, c.lengthMm * 0.88f, c.rotDeg,
                    isSelected ? 0x0369A1 : 0x1E293B, 0x0F172A, stroke);
                break;
            }
            case EdaPartCategory::Connector: {
                // Beige/Black header housing + gold pin posts
                pushBox3D(c.x, c.y, baseZ, topZ, c.widthMm, c.lengthMm, c.rotDeg,
                    isSelected ? 0x0284C7 : 0xD6D3D1, 0x78716C, stroke);
                pushBox3D(c.x, c.y, baseZ, topZ + 0.6f, c.widthMm * 0.7f, c.lengthMm * 0.25f, c.rotDeg, 0xFACC15, 0xCA8A04, stroke);
                break;
            }
            case EdaPartCategory::DiodeLED: {
                pushBox3D(c.x, c.y, baseZ, topZ, c.widthMm, c.lengthMm, c.rotDeg, 0x22C55E, 0x15803D, stroke);
                break;
            }
            case EdaPartCategory::Crystal: {
                pushBox3D(c.x, c.y, baseZ, topZ, c.widthMm, c.lengthMm, c.rotDeg, 0xE2E8F0, 0x94A3B8, stroke);
                break;
            }
            default: {
                pushBox3D(c.x, c.y, baseZ, topZ, c.widthMm, c.lengthMm, c.rotDeg,
                    isSelected ? 0x0284C7 : 0x334155, 0x1E293B, stroke);
                break;
            }
            }
        }
    }

    // Painter's algorithm sort (back to front)
    std::sort(quads.begin(), quads.end(), [](const ProjectedQuad3D& a, const ProjectedQuad3D& b) {
        return a.depth < b.depth;
    });

    for (const auto& q : quads) {
        if (m_d2dFactory) {
            ID2D1PathGeometry* path = nullptr;
            if (SUCCEEDED(m_d2dFactory->CreatePathGeometry(&path)) && path) {
                ID2D1GeometrySink* sink = nullptr;
                if (SUCCEEDED(path->Open(&sink)) && sink) {
                    sink->BeginFigure(q.pts[0], D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(q.pts[1]);
                    sink->AddLine(q.pts[2]);
                    sink->AddLine(q.pts[3]);
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    sink->Release();

                    brush->SetColor(EdaColor(q.fillRgb, q.alpha));
                    rt->FillGeometry(path, brush);
                    if (q.drawStroke) {
                        brush->SetColor(EdaColor(q.strokeRgb, 0.85f));
                        rt->DrawGeometry(path, brush, 0.85f);
                    }
                }
                path->Release();
            }
        }
    }

    // Overlay 3D Copper Traces on Top Surface
    if (m_showTopCu) {
        brush->SetColor(EdaColor(0x22C55E, 0.75f));
        for (const auto& t : board.traces) {
            if (t.layer != EdaLayerId::TopCopper) continue;
            D2D1_POINT_2F p1 = project3D(t.x1, t.y1, zTop + 0.03f);
            D2D1_POINT_2F p2 = project3D(t.x2, t.y2, zTop + 0.03f);
            rt->DrawLine(p1, p2, brush, std::max(1.0f, t.width * scale * 0.8f));
        }
    }

    // Overlay 3D Component Silkscreen Designators on Top of 3D Packages
    if (m_showSilk && scale >= 2.2f) {
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        for (const auto& c : board.components) {
            if (c.refDes.empty()) continue;
            float labelZ = m_show3DParts ? (c.bottomSide ? (zBot - c.heightMm - 0.1f) : (zTop + c.heightMm + 0.1f)) : (zTop + 0.1f);
            D2D1_POINT_2F pt = project3D(c.x, c.y, labelZ);
            brush->SetColor(EdaColor(0xF8FAFC));
            rt->DrawTextW(c.refDes.c_str(), static_cast<UINT32>(c.refDes.size()), fmtSmall,
                D2D1::RectF(pt.x - 35.0f, pt.y - 8.0f, pt.x + 35.0f, pt.y + 8.0f), brush);
        }
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    (void)fmtBold;
    (void)lang;
}

void EdaViewer::Draw(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtRegular,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& rect,
    Lang lang,
    D2D1_POINT_2F /*mousePt*/)
{
    const bool pl = (lang == Lang::PL);
    m_toolbarHits.clear();

    float tbH = 30.0f;
    float statusH = 26.0f;
    D2D1_RECT_F tbR = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + tbH);
    brush->SetColor(EdaColor(0xF8FAFC));
    rt->FillRectangle(tbR, brush);
    brush->SetColor(EdaColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(tbR.left, tbR.bottom - 0.5f), D2D1::Point2F(tbR.right, tbR.bottom - 0.5f), brush, 1.0f);

    float bx = tbR.left + 6.0f;
    auto addBtn = [&](int id, float w, const wchar_t* label, bool active) {
        if (bx + w > tbR.right - 4.0f) return;
        D2D1_RECT_F br = D2D1::RectF(bx, tbR.top + 4.0f, bx + w, tbR.bottom - 4.0f);
        m_toolbarHits.push_back({ id, br });
        PathUtils::DrawToolbarButton(rt, brush, fmtSmall, br, label, active, m_hoverBtnId == id, false, false);
        bx += w + 4.0f;
    };

    addBtn(BTN_TOGGLE_2D_3D, pl ? 88.0f : 82.0f,
        m_view3D ? (pl ? L"\xD83E\xDDCA Widok 3D PCB" : L"\xD83E\xDDCA 3D PCB View")
                 : (pl ? L"\xD83D\xDCD0 Widok 2D CAD" : L"\xD83D\xDCD0 2D CAD View"),
        m_view3D);

    if (m_view3D) {
        addBtn(BTN_TOGGLE_3D_PARTS, pl ? 78.0f : 72.0f, pl ? L"Elementy 3D" : L"3D Parts", m_show3DParts);
        addBtn(BTN_SOLDER_MASK_COLOR, pl ? 74.0f : 70.0f, pl ? L"Kolor maski" : L"Mask Color", false);
        addBtn(BTN_AUTO_ROTATE_3D, pl ? 70.0f : 64.0f, pl ? L"Auto-obrót" : L"Turntable", m_autoRotate3D);
    } else {
        addBtn(BTN_LAYER_TOPCU, 54.0f, L"Top Cu", m_showTopCu);
        addBtn(BTN_LAYER_BOTCU, 54.0f, L"Bot Cu", m_showBotCu);
        addBtn(BTN_LAYER_SILK,  46.0f, L"Silk",   m_showSilk);
        addBtn(BTN_LAYER_PADS,  52.0f, L"Pads",   m_showPads);
    }
    addBtn(BTN_TOGGLE_BOM, 56.0f, L"BOM", m_showBomPanel);
    addBtn(BTN_FIT_RESET,  pl ? 58.0f : 52.0f, pl ? L"Dopasuj" : L"Fit", false);

    D2D1_RECT_F mainR = D2D1::RectF(rect.left, tbR.bottom, rect.right, std::max(tbR.bottom, rect.bottom - statusH));
    if (m_showBomPanel && (mainR.right - mainR.left) > 260.0f) {
        float bomW = std::min(190.0f, (mainR.right - mainR.left) * 0.42f);
        m_canvasRect = D2D1::RectF(mainR.left, mainR.top, mainR.right - bomW, mainR.bottom);
        m_bomRect = D2D1::RectF(mainR.right - bomW, mainR.top, mainR.right, mainR.bottom);
    } else {
        m_canvasRect = mainR;
        m_bomRect = {};
    }

    rt->PushAxisAlignedClip(m_canvasRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (m_view3D) {
        Draw3DPcbViewport(rt, brush, fmtSmall, fmtBold, m_canvasRect, m_board, lang);
    } else {
        Draw2DCanvas(rt, brush, fmtSmall, fmtBold, m_canvasRect, m_board, lang);
    }
    rt->PopAxisAlignedClip();

    // Optional Right-Side BOM Component List
    if (m_showBomPanel && m_bomRect.right > m_bomRect.left) {
        brush->SetColor(EdaColor(0xF8FAFC));
        rt->FillRectangle(m_bomRect, brush);
        brush->SetColor(EdaColor(0xCBD5E1));
        rt->DrawLine(D2D1::Point2F(m_bomRect.left, m_bomRect.top), D2D1::Point2F(m_bomRect.left, m_bomRect.bottom), brush, 1.0f);

        rt->PushAxisAlignedClip(m_bomRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float rowH = 20.0f;
        for (size_t i = 0; i < m_board.components.size(); ++i) {
            float ry = m_bomRect.top + i * rowH - m_bomScrollY;
            if (ry + rowH < m_bomRect.top || ry > m_bomRect.bottom) continue;
            D2D1_RECT_F rr = D2D1::RectF(m_bomRect.left + 2.0f, ry, m_bomRect.right - 2.0f, ry + rowH);
            if (static_cast<int>(i) == m_selectedCompIdx) {
                brush->SetColor(EdaColor(0xDBEAFE));
                rt->FillRectangle(rr, brush);
            }
            const auto& c = m_board.components[i];
            std::wstring lbl = c.refDes + (c.value.empty() ? L"" : (L"  " + c.value)) + (c.footprint.empty() ? L"" : (L" (" + c.footprint + L")"));
            brush->SetColor(EdaColor(0x0F172A));
            PathUtils::DrawSingleLineClipped(rt, lbl, m_fmtSmallTrim ? m_fmtSmallTrim : fmtSmall,
                D2D1::RectF(rr.left + 4.0f, rr.top, rr.right - 4.0f, rr.bottom), brush, m_lineScratch);
        }
        rt->PopAxisAlignedClip();
    }

    // Bottom Status Strip
    D2D1_RECT_F stR = D2D1::RectF(rect.left, mainR.bottom, rect.right, rect.bottom);
    brush->SetColor(EdaColor(0xF1F5F9));
    rt->FillRectangle(stR, brush);
    brush->SetColor(EdaColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(stR.left, stR.top), D2D1::Point2F(stR.right, stR.top), brush, 1.0f);

    wchar_t stBuf[320] = {};
    if (m_selectedCompIdx >= 0 && m_selectedCompIdx < static_cast<int>(m_board.components.size())) {
        const auto& c = m_board.components[m_selectedCompIdx];
        swprintf_s(stBuf,
            L"\x2713 %s  |  Val: %s  |  Pkg: %s  |  Pos: (%.2f, %.2f mm) %s",
            c.refDes.c_str(), c.value.c_str(), c.footprint.c_str(), c.x, c.y, c.bottomSide ? L"[Bottom]" : L"[Top]");
    } else {
        swprintf_s(stBuf,
            pl ? L"%s   \x2022   Elementy: %zu   |   Pady/Via: %zu   |   Ścieżki: %zu   |   Wymiary: %.1f \xD7 %.1f mm"
               : L"%s   \x2022   Parts: %zu   |   Pads/Vias: %zu   |   Traces: %zu   |   Board: %.1f \xD7 %.1f mm",
            m_board.formatTitle.c_str(),
            m_board.components.size(),
            m_board.pads.size() + m_board.vias.size(),
            m_board.traces.size(),
            std::abs(m_board.maxX - m_board.minX),
            std::abs(m_board.maxY - m_board.minY));
    }
    brush->SetColor(EdaColor(0x334155));
    PathUtils::DrawSingleLineClipped(rt, stBuf, m_fmtSmallTrim ? m_fmtSmallTrim : fmtSmall,
        D2D1::RectF(stR.left + 8.0f, stR.top, stR.right - 8.0f, stR.bottom), brush, m_lineScratch);
    (void)fmtRegular;
}

bool EdaViewer::OnMouseDown(float x, float y, bool rightButton, Lang /*lang*/, std::wstring* /*outStatusToast*/) {
    if (!rightButton) {
        for (const auto& hit : m_toolbarHits) {
            if (PathUtils::PtInRectF(x, y, hit.rect)) {
                switch (hit.id) {
                case BTN_TOGGLE_2D_3D:      m_view3D = !m_view3D; break;
                case BTN_LAYER_TOPCU:       m_showTopCu = !m_showTopCu; break;
                case BTN_LAYER_BOTCU:       m_showBotCu = !m_showBotCu; break;
                case BTN_LAYER_SILK:        m_showSilk = !m_showSilk; break;
                case BTN_LAYER_PADS:        m_showPads = !m_showPads; break;
                case BTN_TOGGLE_3D_PARTS:   m_show3DParts = !m_show3DParts; break;
                case BTN_SOLDER_MASK_COLOR: m_solderMaskPreset = (m_solderMaskPreset + 1) % 6; break;
                case BTN_AUTO_ROTATE_3D:    m_autoRotate3D = !m_autoRotate3D; m_lastAnimTick = GetTickCount64(); break;
                case BTN_TOGGLE_BOM:        m_showBomPanel = !m_showBomPanel; break;
                case BTN_FIT_RESET:
                    if (m_view3D) Reset3DCamera();
                    else Fit2DView();
                    break;
                }
                return true;
            }
        }

        if (m_showBomPanel && PathUtils::PtInRectF(x, y, m_bomRect)) {
            int idx = static_cast<int>((y - m_bomRect.top + m_bomScrollY) / 20.0f);
            if (idx >= 0 && idx < static_cast<int>(m_board.components.size())) {
                m_selectedCompIdx = idx;
            }
            return true;
        }
    }

    if (PathUtils::PtInRectF(x, y, m_canvasRect)) {
        m_lastMouseX = x;
        m_lastMouseY = y;
        if (rightButton) {
            m_draggingRight = true;
        } else {
            m_draggingLeft = true;
        }
        return true;
    }
    return false;
}

bool EdaViewer::OnMouseMove(float x, float y, WPARAM keyFlags) {
    if (m_draggingLeft || m_draggingRight) {
        float dx = x - m_lastMouseX;
        float dy = y - m_lastMouseY;
        m_lastMouseX = x;
        m_lastMouseY = y;
        if (m_view3D) {
            if (m_draggingRight || (keyFlags & MK_SHIFT)) {
                m_pan3DX += dx;
                m_pan3DY += dy;
            } else {
                m_yaw3D += dx * 0.01f;
                m_pitch3D = std::clamp(m_pitch3D + dy * 0.01f, 0.1f, 1.48f);
            }
        } else {
            m_pan2DX += dx;
            m_pan2DY += dy;
        }
        return true;
    }

    int newHov = 0;
    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            newHov = hit.id;
            break;
        }
    }
    if (newHov != m_hoverBtnId) {
        m_hoverBtnId = newHov;
        return true;
    }
    return false;
}

bool EdaViewer::OnMouseUp() {
    if (m_draggingLeft || m_draggingRight) {
        m_draggingLeft = false;
        m_draggingRight = false;
        return true;
    }
    return false;
}

bool EdaViewer::OnMouseWheel(short delta, float x, float y) {
    int notches = delta / WHEEL_DELTA;
    if (notches == 0) notches = (delta > 0) ? 1 : -1;

    if (m_showBomPanel && PathUtils::PtInRectF(x, y, m_bomRect)) {
        float maxScroll = std::max(0.0f, static_cast<float>(m_board.components.size()) * 20.0f - (m_bomRect.bottom - m_bomRect.top));
        m_bomScrollY = std::clamp(m_bomScrollY - notches * 40.0f, 0.0f, maxScroll);
        return true;
    }

    if (PathUtils::PtInRectF(x, y, m_canvasRect)) {
        float factor = std::pow(1.18f, static_cast<float>(notches));
        if (m_view3D) {
            m_zoom3D = std::clamp(m_zoom3D * factor, 0.25f, 18.0f);
        } else {
            m_zoom2D = std::clamp(m_zoom2D * factor, 0.2f, 40.0f);
        }
        return true;
    }
    return false;
}

} // namespace Explorer2
