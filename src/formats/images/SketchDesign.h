#pragma once
// SketchDesign.h — Zero-dependency C++20 Sketch Vector Design Document (.sketch) v43+ ZIP JSON artboard & Bezier rasterizer.

namespace Formats {

namespace DetailSketch {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU24LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU64BE;

inline bool DecodeEmbeddedRasterStraightRgba(
    const uint8_t* bytes, size_t len,
    uint32_t& outW, uint32_t& outH,
    std::vector<uint8_t>& outRgba)
{
    if (!bytes || len < 12) return false;
    if (DetailPngPure::DecodePng(bytes, len, outW, outH, outRgba)) return true;

    if (len >= 4 && bytes[0] == 0xFF && bytes[1] == 0xD8) {
        DetailBlpJpeg::RawJpegDecoded jpg;
        if (DetailBlpJpeg::DecodeBaselineJpegRawChannels(bytes, len, jpg) && jpg.width > 0 && jpg.height > 0) {
            outW = jpg.width;
            outH = jpg.height;
            size_t n = static_cast<size_t>(outW) * outH;
            outRgba.resize(n * 4u);
            for (size_t i = 0; i < n; ++i) {
                if (jpg.components == 1) {
                    uint8_t yv = jpg.pixels[i];
                    outRgba[i * 4 + 0] = yv; outRgba[i * 4 + 1] = yv; outRgba[i * 4 + 2] = yv; outRgba[i * 4 + 3] = 255;
                } else {
                    // Standard JFIF YCbCr -> RGB
                    int yv = jpg.pixels[i * jpg.components + 0];
                    int cb = static_cast<int>(jpg.pixels[i * jpg.components + 1]) - 128;
                    int cr = static_cast<int>(jpg.pixels[i * jpg.components + 2]) - 128;
                    int r = yv + ((91881 * cr + 32768) >> 16);
                    int g = yv - ((22554 * cb + 46802 * cr + 32768) >> 16);
                    int b = yv + ((116130 * cb + 32768) >> 16);
                    outRgba[i * 4 + 0] = static_cast<uint8_t>(std::clamp(r, 0, 255));
                    outRgba[i * 4 + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
                    outRgba[i * 4 + 2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
                    outRgba[i * 4 + 3] = 255;
                }
            }
            return true;
        }
    }

    if (len >= 16 && std::memcmp(bytes, "RIFF", 4) == 0 && std::memcmp(bytes + 8, "WEBP", 4) == 0) {
        ExtendedImageInfo wInfo;
        if (DecodeWebP(bytes, len, wInfo) && wInfo.valid && wInfo.width > 0 && wInfo.height > 0) {
            outW = wInfo.width;
            outH = wInfo.height;
            size_t n = static_cast<size_t>(outW) * outH;
            outRgba.resize(n * 4u);
            for (size_t i = 0; i < n; ++i) {
                uint8_t b = wInfo.bgraPixels[i * 4 + 0];
                uint8_t g = wInfo.bgraPixels[i * 4 + 1];
                uint8_t r = wInfo.bgraPixels[i * 4 + 2];
                uint8_t a = wInfo.bgraPixels[i * 4 + 3];
                if (a > 0 && a < 255) {
                    r = static_cast<uint8_t>(std::min<uint32_t>(255u, (r * 255u + (a >> 1)) / a));
                    g = static_cast<uint8_t>(std::min<uint32_t>(255u, (g * 255u + (a >> 1)) / a));
                    b = static_cast<uint8_t>(std::min<uint32_t>(255u, (b * 255u + (a >> 1)) / a));
                }
                outRgba[i * 4 + 0] = r; outRgba[i * 4 + 1] = g; outRgba[i * 4 + 2] = b; outRgba[i * 4 + 3] = a;
            }
            return true;
        }
    }

    ExtendedImageInfo wicInfo;
    if (DetailLayeredAndHdrImg::DecodeEmbeddedWicBuffer(bytes, len, L"", wicInfo) && wicInfo.valid && wicInfo.width > 0 && wicInfo.height > 0) {
        outW = wicInfo.width;
        outH = wicInfo.height;
        size_t n = static_cast<size_t>(outW) * outH;
        outRgba.resize(n * 4u);
        for (size_t i = 0; i < n; ++i) {
            uint8_t b = wicInfo.bgraPixels[i * 4 + 0];
            uint8_t g = wicInfo.bgraPixels[i * 4 + 1];
            uint8_t r = wicInfo.bgraPixels[i * 4 + 2];
            uint8_t a = wicInfo.bgraPixels[i * 4 + 3];
            if (a > 0 && a < 255) {
                r = static_cast<uint8_t>(std::min<uint32_t>(255u, (r * 255u + (a >> 1)) / a));
                g = static_cast<uint8_t>(std::min<uint32_t>(255u, (g * 255u + (a >> 1)) / a));
                b = static_cast<uint8_t>(std::min<uint32_t>(255u, (b * 255u + (a >> 1)) / a));
            }
            outRgba[i * 4 + 0] = r; outRgba[i * 4 + 1] = g; outRgba[i * 4 + 2] = b; outRgba[i * 4 + 3] = a;
        }
        return true;
    }
    return false;
}


struct SkPt { double x = 0.0, y = 0.0; };

struct SkAffine {
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, tx = 0.0, ty = 0.0;

    SkPt Apply(double x, double y) const {
        return { a * x + c * y + tx, b * x + d * y + ty };
    }
    static SkAffine Multiply(const SkAffine& p, const SkAffine& l) {
        // p * l: apply local l first, then parent p
        SkAffine r;
        r.a  = p.a * l.a + p.c * l.b;
        r.b  = p.b * l.a + p.d * l.b;
        r.c  = p.a * l.c + p.c * l.d;
        r.d  = p.b * l.c + p.d * l.d;
        r.tx = p.a * l.tx + p.c * l.ty + p.tx;
        r.ty = p.b * l.tx + p.d * l.ty + p.ty;
        return r;
    }
    bool Invert(SkAffine& inv) const {
        double det = a * d - b * c;
        if (std::fabs(det) < 1e-12) return false;
        double id = 1.0 / det;
        inv.a  =  d * id;
        inv.b  = -b * id;
        inv.c  = -c * id;
        inv.d  =  a * id;
        inv.tx = (c * ty - d * tx) * id;
        inv.ty = (b * tx - a * ty) * id;
        return true;
    }
    double AvgScale() const {
        double sx = std::hypot(a, b);
        double sy = std::hypot(c, d);
        return 0.5 * (sx + sy);
    }
};

struct SkColor {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
};

struct SkGradStop {
    float pos = 0.0f;
    SkColor col;
};

struct SkPaint {
    int fillType = 0;     // 0=Solid, 1=Gradient, 4=Pattern
    SkColor solid;
    int gradientType = 0; // 0=Linear, 1=Radial, 2=Angular
    SkPt gradFrom{ 0.5, 0.0 };
    SkPt gradTo{ 0.5, 1.0 };
    double ellipseLength = 1.0;
    std::vector<SkGradStop> stops;
    SkAffine localToCanvas; // Maps normalized [0..1]x[0..1] unit square of shape to canvas pixels
    SkAffine canvasToUnit;
    bool hasUnitInv = false;
    float opacityMul = 1.0f;

    const std::vector<uint8_t>* patRgba = nullptr;
    uint32_t patW = 0, patH = 0;

    SkColor SampleAtCanvas(double cx, double cy) const {
        SkColor c = solid;
        if (fillType == 1 && !stops.empty() && hasUnitInv) {
            SkPt u = canvasToUnit.Apply(cx, cy);
            double t = 0.0;
            if (gradientType == 0) {
                double dx = gradTo.x - gradFrom.x;
                double dy = gradTo.y - gradFrom.y;
                double len2 = dx * dx + dy * dy;
                t = (len2 > 1e-10) ? (((u.x - gradFrom.x) * dx + (u.y - gradFrom.y) * dy) / len2) : 0.0;
            } else if (gradientType == 1) {
                double dx = gradTo.x - gradFrom.x;
                double dy = gradTo.y - gradFrom.y;
                double rad = std::hypot(dx, dy);
                if (rad > 1e-8) {
                    double px = u.x - gradFrom.x;
                    double py = u.y - gradFrom.y;
                    if (ellipseLength > 1e-4 && std::fabs(ellipseLength - 1.0) > 1e-4) {
                        double ang = std::atan2(dy, dx);
                        double ca = std::cos(-ang), sa = std::sin(-ang);
                        double rx = ca * px - sa * py;
                        double ry = (sa * px + ca * py) / ellipseLength;
                        t = std::hypot(rx, ry) / rad;
                    } else {
                        t = std::hypot(px, py) / rad;
                    }
                }
            } else {
                double ang = std::atan2(u.y - gradFrom.y, u.x - gradFrom.x) / (2.0 * 3.14159265358979323846);
                if (ang < 0.0) ang += 1.0;
                t = ang;
            }
            float tf = static_cast<float>(std::clamp(t, 0.0, 1.0));
            if (tf <= stops.front().pos) {
                c = stops.front().col;
            } else if (tf >= stops.back().pos) {
                c = stops.back().col;
            } else {
                for (size_t i = 0; i + 1 < stops.size(); ++i) {
                    if (tf >= stops[i].pos && tf <= stops[i + 1].pos) {
                        float span = std::max(1e-6f, stops[i + 1].pos - stops[i].pos);
                        float f = (tf - stops[i].pos) / span;
                        c.r = stops[i].col.r + (stops[i + 1].col.r - stops[i].col.r) * f;
                        c.g = stops[i].col.g + (stops[i + 1].col.g - stops[i].col.g) * f;
                        c.b = stops[i].col.b + (stops[i + 1].col.b - stops[i].col.b) * f;
                        c.a = stops[i].col.a + (stops[i + 1].col.a - stops[i].col.a) * f;
                        break;
                    }
                }
            }
        } else if (fillType == 4 && patRgba && patW > 0 && patH > 0 && hasUnitInv) {
            SkPt u = canvasToUnit.Apply(cx, cy);
            double ux = std::clamp(u.x, 0.0, 0.999999) * patW;
            double uy = std::clamp(u.y, 0.0, 0.999999) * patH;
            uint32_t px = std::min(patW - 1u, static_cast<uint32_t>(ux));
            uint32_t py = std::min(patH - 1u, static_cast<uint32_t>(uy));
            const uint8_t* p = &(*patRgba)[(static_cast<size_t>(py) * patW + px) * 4u];
            c.r = p[0] * (1.0f / 255.0f);
            c.g = p[1] * (1.0f / 255.0f);
            c.b = p[2] * (1.0f / 255.0f);
            c.a = p[3] * (1.0f / 255.0f);
        }
        c.a = std::clamp(c.a * opacityMul, 0.0f, 1.0f);
        return c;
    }
};

inline double JsonNum(const ::Explorer2::Json::Value* obj, std::string_view key, double defVal = 0.0) {
    if (!obj) return defVal;
    const auto* v = obj->Find(key);
    if (!v || !v->IsNumber() || !std::isfinite(v->numVal)) return defVal;
    return v->numVal;
}

inline SkColor ParseSketchColor(const ::Explorer2::Json::Value* colObj) {
    SkColor c{ 0.0f, 0.0f, 0.0f, 1.0f };
    if (!colObj || !colObj->IsObject()) return c;
    c.r = static_cast<float>(std::clamp(JsonNum(colObj, "red",   0.0), 0.0, 1.0));
    c.g = static_cast<float>(std::clamp(JsonNum(colObj, "green", 0.0), 0.0, 1.0));
    c.b = static_cast<float>(std::clamp(JsonNum(colObj, "blue",  0.0), 0.0, 1.0));
    c.a = static_cast<float>(std::clamp(JsonNum(colObj, "alpha", 1.0), 0.0, 1.0));
    return c;
}

// Parses Sketch point string "{0.25, 0.75}" OR object {"x": 0.25, "y": 0.75}
inline SkPt ParseSketchPointVal(const ::Explorer2::Json::Value* v, SkPt defPt = { 0.0, 0.0 }) {
    if (!v) return defPt;
    if (v->IsObject()) {
        return { JsonNum(v, "x", defPt.x), JsonNum(v, "y", defPt.y) };
    }
    if (!v->IsString()) return defPt;
    const std::string& s = v->strVal;
    size_t i = 0;
    while (i < s.size() && (s[i] == '{' || s[i] == ' ' || s[i] == '\t' || s[i] == '(')) ++i;
    char* end1 = nullptr;
    double x = std::strtod(s.c_str() + i, &end1);
    if (!end1 || end1 == s.c_str() + i || !std::isfinite(x)) return defPt;
    const char* p2 = end1;
    while (*p2 && (*p2 == ',' || *p2 == ' ' || *p2 == '\t')) ++p2;
    char* end2 = nullptr;
    double y = std::strtod(p2, &end2);
    if (!end2 || end2 == p2 || !std::isfinite(y)) return defPt;
    return { x, y };
}

inline void SubdivideCubicSk(SkPt p0, SkPt p1, SkPt p2, SkPt p3, std::vector<SkPt>& out, int depth = 0) {
    double dx = p3.x - p0.x, dy = p3.y - p0.y;
    double d1 = std::fabs((p1.x - p3.x) * dy - (p1.y - p3.y) * dx);
    double d2 = std::fabs((p2.x - p3.x) * dy - (p2.y - p3.y) * dx);
    double len2 = dx * dx + dy * dy;
    if (depth >= 8 || (d1 + d2) * (d1 + d2) <= 0.16 * (len2 + 1.0)) {
        out.push_back(p3);
        return;
    }
    SkPt p01{ 0.5 * (p0.x + p1.x), 0.5 * (p0.y + p1.y) };
    SkPt p12{ 0.5 * (p1.x + p2.x), 0.5 * (p1.y + p2.y) };
    SkPt p23{ 0.5 * (p2.x + p3.x), 0.5 * (p2.y + p3.y) };
    SkPt p012{ 0.5 * (p01.x + p12.x), 0.5 * (p01.y + p12.y) };
    SkPt p123{ 0.5 * (p12.x + p23.x), 0.5 * (p12.y + p23.y) };
    SkPt mid{ 0.5 * (p012.x + p123.x), 0.5 * (p012.y + p123.y) };
    SubdivideCubicSk(p0, p01, p012, mid, out, depth + 1);
    SubdivideCubicSk(mid, p123, p23, p3, out, depth + 1);
}

struct SkContour {
    std::vector<SkPt> pts;
    bool closed = true;
};

struct SketchCanvas {
    uint32_t w = 0, h = 0;
    std::vector<float> premulRgba; // Premultiplied float RGBA [0..1] for artifact-free AA compositing

    void Init(uint32_t width, uint32_t height) {
        w = width;
        h = height;
        premulRgba.assign(static_cast<size_t>(w) * h * 4u, 0.0f);
    }

    void BlendPremul(uint32_t x, uint32_t y, SkColor src, float cov) {
        float sa = std::clamp(src.a * cov, 0.0f, 1.0f);
        if (sa <= 1e-6f) return;
        float* dst = &premulRgba[(static_cast<size_t>(y) * w + x) * 4u];
        float invA = 1.0f - sa;
        dst[0] = src.r * sa + dst[0] * invA;
        dst[1] = src.g * sa + dst[1] * invA;
        dst[2] = src.b * sa + dst[2] * invA;
        dst[3] = sa         + dst[3] * invA;
    }

    // Computes 4x4 supersampled interior coverage mask [0..1] over bounding box [ix0..ix1]x[iy0..iy1]
    void ComputeFillCoverage(
        const std::vector<SkContour>& contours, bool evenOdd,
        int& outX0, int& outY0, int& outX1, int& outY1,
        std::vector<float>& outCov) const
    {
        outCov.clear();
        double minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30;
        size_t totalPts = 0;
        for (const auto& c : contours) {
            if (c.pts.size() < 3) continue;
            totalPts += c.pts.size();
            for (const auto& p : c.pts) {
                minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
            }
        }
        if (totalPts < 3) return;
        outX0 = std::max(0, static_cast<int>(std::floor(minX)));
        outY0 = std::max(0, static_cast<int>(std::floor(minY)));
        outX1 = std::min(static_cast<int>(w) - 1, static_cast<int>(std::ceil(maxX)));
        outY1 = std::min(static_cast<int>(h) - 1, static_cast<int>(std::ceil(maxY)));
        if (outX0 > outX1 || outY0 > outY1) return;

        int bw = outX1 - outX0 + 1;
        int bh = outY1 - outY0 + 1;
        outCov.assign(static_cast<size_t>(bw) * bh, 0.0f);

        struct Edge { double x0, y0, x1, y1; int dir; };
        std::vector<Edge> edges;
        edges.reserve(totalPts);
        for (const auto& c : contours) {
            if (c.pts.size() < 3) continue;
            size_t n = c.pts.size();
            for (size_t i = 0; i < n; ++i) {
                SkPt a = c.pts[i];
                SkPt b = c.pts[(i + 1) % n];
                if (std::fabs(a.y - b.y) < 1e-9) continue;
                if (a.y < b.y) edges.push_back({ a.x, a.y, b.x, b.y, +1 });
                else           edges.push_back({ b.x, b.y, a.x, a.y, -1 });
            }
        }
        if (edges.empty()) return;

        std::vector<uint8_t> rowHits(static_cast<size_t>(bw), 0u);
        std::vector<std::pair<double, int>> xings;
        xings.reserve(edges.size());

        for (int py = outY0; py <= outY1; ++py) {
            std::fill(rowHits.begin(), rowHits.end(), 0u);
            for (int sy = 0; sy < 4; ++sy) {
                double scanY = py + (sy + 0.5) * 0.25;
                xings.clear();
                for (const auto& e : edges) {
                    if (scanY >= e.y0 && scanY < e.y1) {
                        double ix = e.x0 + (scanY - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0);
                        xings.push_back({ ix, e.dir });
                    }
                }
                if (xings.size() < 2) continue;
                std::sort(xings.begin(), xings.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

                auto addSpan = [&](double sx0, double sx1) {
                    int px0 = std::max(outX0, static_cast<int>(std::floor(sx0)));
                    int px1 = std::min(outX1, static_cast<int>(std::floor(sx1)));
                    for (int px = px0; px <= px1; ++px) {
                        uint8_t hits = 0;
                        for (int subX = 0; subX < 4; ++subX) {
                            double sampleX = px + (subX + 0.5) * 0.25;
                            if (sampleX >= sx0 && sampleX < sx1) ++hits;
                        }
                        rowHits[static_cast<size_t>(px - outX0)] = static_cast<uint8_t>(rowHits[static_cast<size_t>(px - outX0)] + hits);
                    }
                };

                if (evenOdd) {
                    for (size_t k = 0; k + 1 < xings.size(); k += 2) {
                        addSpan(xings[k].first, xings[k + 1].first);
                    }
                } else {
                    int winding = 0;
                    double spanStart = 0.0;
                    for (const auto& xw : xings) {
                        int prev = winding;
                        winding += xw.second;
                        if (prev == 0 && winding != 0) spanStart = xw.first;
                        else if (prev != 0 && winding == 0) addSpan(spanStart, xw.first);
                    }
                }
            }
            float* dstRow = &outCov[static_cast<size_t>(py - outY0) * bw];
            for (int bx = 0; bx < bw; ++bx) {
                dstRow[bx] = static_cast<float>(rowHits[static_cast<size_t>(bx)]) * (1.0f / 16.0f);
            }
        }
    }

    void FillCoverageWithPaint(
        int ix0, int iy0, int ix1, int iy1,
        const std::vector<float>& cov,
        const SkPaint& paint)
    {
        if (cov.empty() || ix0 > ix1 || iy0 > iy1) return;
        int bw = ix1 - ix0 + 1;
        for (int py = iy0; py <= iy1; ++py) {
            const float* row = &cov[static_cast<size_t>(py - iy0) * bw];
            for (int px = ix0; px <= ix1; ++px) {
                float c = row[px - ix0];
                if (c > 0.0f) {
                    SkColor col = paint.SampleAtCanvas(px + 0.5, py + 0.5);
                    BlendPremul(static_cast<uint32_t>(px), static_cast<uint32_t>(py), col, c);
                }
            }
        }
    }

    // Strokes contours with exact Center (pos=0), Inside (pos=1), or Outside (pos=2) alignment
    void StrokeContours(
        const std::vector<SkContour>& contours,
        double thickness, int borderPos,
        const SkPaint& paint,
        int fillX0, int fillY0, int fillX1, int fillY1,
        const std::vector<float>* optFillCov)
    {
        if (thickness <= 0.0) return;
        bool useMask = (optFillCov && !optFillCov->empty() && (borderPos == 1 || borderPos == 2));
        double halfW = useMask ? thickness : (0.5 * thickness);
        halfW = std::max(0.5, halfW);
        double outerR = halfW + 0.75;

        double minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30;
        for (const auto& c : contours) {
            for (const auto& p : c.pts) {
                minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
            }
        }
        if (minX > maxX) return;
        int sx0 = std::max(0, static_cast<int>(std::floor(minX - outerR)));
        int sy0 = std::max(0, static_cast<int>(std::floor(minY - outerR)));
        int sx1 = std::min(static_cast<int>(w) - 1, static_cast<int>(std::ceil(maxX + outerR)));
        int sy1 = std::min(static_cast<int>(h) - 1, static_cast<int>(std::ceil(maxY + outerR)));
        if (sx0 > sx1 || sy0 > sy1) return;

        int sw = sx1 - sx0 + 1;
        int sh = sy1 - sy0 + 1;
        std::vector<float> strokeCov(static_cast<size_t>(sw) * sh, 0.0f);

        for (const auto& c : contours) {
            if (c.pts.size() < 2) continue;
            size_t segCount = c.closed ? c.pts.size() : (c.pts.size() - 1u);
            for (size_t i = 0; i < segCount; ++i) {
                SkPt a = c.pts[i];
                SkPt b = c.pts[(i + 1u) % c.pts.size()];
                int bx0 = std::max(sx0, static_cast<int>(std::floor(std::min(a.x, b.x) - outerR)));
                int by0 = std::max(sy0, static_cast<int>(std::floor(std::min(a.y, b.y) - outerR)));
                int bx1 = std::min(sx1, static_cast<int>(std::ceil(std::max(a.x, b.x) + outerR)));
                int by1 = std::min(sy1, static_cast<int>(std::ceil(std::max(a.y, b.y) + outerR)));
                double dx = b.x - a.x, dy = b.y - a.y;
                double len2 = dx * dx + dy * dy;

                for (int py = by0; py <= by1; ++py) {
                    double cy = py + 0.5;
                    float* sRow = &strokeCov[static_cast<size_t>(py - sy0) * sw];
                    for (int px = bx0; px <= bx1; ++px) {
                        double cx = px + 0.5;
                        double t = (len2 > 1e-10) ? std::clamp(((cx - a.x) * dx + (cy - a.y) * dy) / len2, 0.0, 1.0) : 0.0;
                        double qx = a.x + t * dx;
                        double qy = a.y + t * dy;
                        double dist = std::hypot(cx - qx, cy - qy);
                        float cov = static_cast<float>(std::clamp(halfW + 0.5 - dist, 0.0, 1.0));
                        if (cov > sRow[px - sx0]) sRow[px - sx0] = cov;
                    }
                }
            }
        }

        int fbw = fillX1 - fillX0 + 1;
        for (int py = sy0; py <= sy1; ++py) {
            const float* sRow = &strokeCov[static_cast<size_t>(py - sy0) * sw];
            for (int px = sx0; px <= sx1; ++px) {
                float cov = sRow[px - sx0];
                if (cov <= 0.0f) continue;
                if (useMask) {
                    float inCov = 0.0f;
                    if (px >= fillX0 && px <= fillX1 && py >= fillY0 && py <= fillY1) {
                        inCov = (*optFillCov)[static_cast<size_t>(py - fillY0) * fbw + (px - fillX0)];
                    }
                    cov *= (borderPos == 1) ? inCov : (1.0f - inCov);
                    if (cov <= 1e-4f) continue;
                }
                SkColor col = paint.SampleAtCanvas(px + 0.5, py + 0.5);
                BlendPremul(static_cast<uint32_t>(px), static_cast<uint32_t>(py), col, cov);
            }
        }
    }
};

inline SkContour BuildLocalShapeContour(const ::Explorer2::Json::Value& layer, double fw, double fh) {
    SkContour out;
    std::string cls = layer.GetString("_class");
    out.closed = layer.GetBool("isClosed", cls != "shapePath");

    const auto* ptsArr = layer.Find("points");
    if (ptsArr && ptsArr->IsArray() && !ptsArr->arrVal.empty()) {
        struct RawCurvePt {
            SkPt pt;
            SkPt curveFrom;
            SkPt curveTo;
            int curveMode = 1;
            bool hasCurveFrom = false;
            bool hasCurveTo = false;
            double cornerRadius = 0.0;
        };
        double fixedRad = JsonNum(&layer, "fixedRadius", 0.0);
        std::vector<RawCurvePt> rpts;
        rpts.reserve(ptsArr->arrVal.size());
        for (const auto& pv : ptsArr->arrVal) {
            if (!pv.IsObject()) continue;
            RawCurvePt rp;
            SkPt np = ParseSketchPointVal(pv.Find("point"), { 0.0, 0.0 });
            SkPt ncf = ParseSketchPointVal(pv.Find("curveFrom"), np);
            SkPt nct = ParseSketchPointVal(pv.Find("curveTo"), np);
            rp.pt = { np.x * fw, np.y * fh };
            rp.curveFrom = { ncf.x * fw, ncf.y * fh };
            rp.curveTo = { nct.x * fw, nct.y * fh };
            rp.curveMode = pv.GetInt("curveMode", 1);
            rp.hasCurveFrom = pv.GetBool("hasCurveFrom", rp.curveMode >= 2 && pv.Find("curveFrom") != nullptr);
            rp.hasCurveTo = pv.GetBool("hasCurveTo", rp.curveMode >= 2 && pv.Find("curveTo") != nullptr);
            rp.cornerRadius = JsonNum(&pv, "cornerRadius", fixedRad);
            rpts.push_back(rp);
        }
        if (rpts.size() < 2) return out;

        // Expand straight vertices with cornerRadius > 0 into quarter-arc Beziers
        struct SegEndpoint {
            SkPt pos;
            SkPt outCtrl;
            SkPt inCtrl;
            bool curvedOut = false;
            bool curvedIn = false;
        };
        std::vector<SegEndpoint> nodes;
        nodes.reserve(rpts.size() * 2u);
        const size_t n = rpts.size();
        for (size_t i = 0; i < n; ++i) {
            const auto& cur = rpts[i];
            bool isEndOpen = (!out.closed && (i == 0 || i + 1 == n));
            if (cur.curveMode == 1 && !cur.hasCurveFrom && !cur.hasCurveTo && cur.cornerRadius > 0.25 && n >= 3 && !isEndOpen) {
                const auto& prev = rpts[(i + n - 1u) % n];
                const auto& next = rpts[(i + 1u) % n];
                double vx0 = prev.pt.x - cur.pt.x, vy0 = prev.pt.y - cur.pt.y;
                double vx1 = next.pt.x - cur.pt.x, vy1 = next.pt.y - cur.pt.y;
                double l0 = std::hypot(vx0, vy0), l1 = std::hypot(vx1, vy1);
                double r = std::min({ cur.cornerRadius, 0.5 * l0, 0.5 * l1 });
                if (r > 0.25 && l0 > 1e-6 && l1 > 1e-6) {
                    constexpr double kKappa = 0.5522847498307936;
                    SkPt pA{ cur.pt.x + (vx0 / l0) * r, cur.pt.y + (vy0 / l0) * r };
                    SkPt pB{ cur.pt.x + (vx1 / l1) * r, cur.pt.y + (vy1 / l1) * r };
                    SkPt cA{ pA.x - (vx0 / l0) * (r * kKappa), pA.y - (vy0 / l0) * (r * kKappa) };
                    SkPt cB{ pB.x - (vx1 / l1) * (r * kKappa), pB.y - (vy1 / l1) * (r * kKappa) };
                    nodes.push_back({ pA, cA, pA, true, false });
                    nodes.push_back({ pB, pB, cB, false, true });
                    continue;
                }
            }
            bool co = (cur.curveMode != 1 && cur.hasCurveFrom);
            bool ci = (cur.curveMode != 1 && cur.hasCurveTo);
            nodes.push_back({ cur.pt, co ? cur.curveFrom : cur.pt, ci ? cur.curveTo : cur.pt, co, ci });
        }

        if (nodes.size() < 2) return out;
        out.pts.push_back(nodes[0].pos);
        size_t segCnt = out.closed ? nodes.size() : (nodes.size() - 1u);
        for (size_t i = 0; i < segCnt; ++i) {
            const auto& a = nodes[i];
            const auto& b = nodes[(i + 1u) % nodes.size()];
            if (!a.curvedOut && !b.curvedIn) {
                out.pts.push_back(b.pos);
            } else {
                SubdivideCubicSk(a.pos, a.outCtrl, b.inCtrl, b.pos, out.pts);
            }
        }
        return out;
    }

    // Parametric fallbacks when `points` array is omitted
    constexpr double kPi = 3.14159265358979323846;
    if (cls == "rectangle") {
        double r = std::clamp(JsonNum(&layer, "fixedRadius", 0.0), 0.0, 0.5 * std::min(fw, fh));
        if (r <= 0.25) {
            out.pts = { {0.0, 0.0}, {fw, 0.0}, {fw, fh}, {0.0, fh} };
        } else {
            constexpr double kK = 0.5522847498307936;
            out.pts.push_back({ r, 0.0 });
            out.pts.push_back({ fw - r, 0.0 });
            SubdivideCubicSk({ fw - r, 0.0 }, { fw - r + r * kK, 0.0 }, { fw, r - r * kK }, { fw, r }, out.pts);
            out.pts.push_back({ fw, fh - r });
            SubdivideCubicSk({ fw, fh - r }, { fw, fh - r + r * kK }, { fw - r + r * kK, fh }, { fw - r, fh }, out.pts);
            out.pts.push_back({ r, fh });
            SubdivideCubicSk({ r, fh }, { r - r * kK, fh }, { 0.0, fh - r + r * kK }, { 0.0, fh - r }, out.pts);
            out.pts.push_back({ 0.0, r });
            SubdivideCubicSk({ 0.0, r }, { 0.0, r - r * kK }, { r - r * kK, 0.0 }, { r, 0.0 }, out.pts);
        }
    } else if (cls == "oval") {
        int segs = 48;
        for (int i = 0; i < segs; ++i) {
            double ang = (2.0 * kPi * i) / segs;
            out.pts.push_back({ fw * (0.5 + 0.5 * std::cos(ang)), fh * (0.5 + 0.5 * std::sin(ang)) });
        }
    } else if (cls == "triangle") {
        out.pts = { { 0.5 * fw, 0.0 }, { fw, fh }, { 0.0, fh } };
    } else if (cls == "polygon") {
        int n = std::clamp(layer.GetInt("numberOfPoints", 5), 3, 64);
        for (int i = 0; i < n; ++i) {
            double ang = -0.5 * kPi + (2.0 * kPi * i) / n;
            out.pts.push_back({ fw * (0.5 + 0.5 * std::cos(ang)), fh * (0.5 + 0.5 * std::sin(ang)) });
        }
    } else if (cls == "star") {
        int n = std::clamp(layer.GetInt("numberOfPoints", 5), 3, 64);
        double innerRatio = std::clamp(JsonNum(&layer, "radius", 0.5), 0.05, 1.0);
        for (int i = 0; i < n * 2; ++i) {
            double rad = (i & 1) ? (0.5 * innerRatio) : 0.5;
            double ang = -0.5 * kPi + (kPi * i) / n;
            out.pts.push_back({ fw * (0.5 + rad * std::cos(ang)), fh * (0.5 + rad * std::sin(ang)) });
        }
    }
    return out;
}

inline SkAffine BuildSketchLayerTransform(const SkAffine& parentXf, const ::Explorer2::Json::Value& layer, double& outW, double& outH) {
    const auto* frame = layer.Find("frame");
    double fx = JsonNum(frame, "x", 0.0);
    double fy = JsonNum(frame, "y", 0.0);
    outW = std::max(0.0, JsonNum(frame, "width", 0.0));
    outH = std::max(0.0, JsonNum(frame, "height", 0.0));

    double rotDeg = JsonNum(&layer, "rotation", 0.0);
    bool flipH = layer.GetBool("isFlippedHorizontal", false);
    bool flipV = layer.GetBool("isFlippedVertical", false);

    // Local frame translate -> center pivot -> rotate (-rotDeg in Y-down screen space) -> flip -> unpivot
    SkAffine loc{ 1.0, 0.0, 0.0, 1.0, fx, fy };
    if (std::fabs(rotDeg) > 1e-7 || flipH || flipV) {
        double cx = 0.5 * outW, cy = 0.5 * outH;
        double rad = -rotDeg * (3.14159265358979323846 / 180.0);
        double cs = std::cos(rad), sn = std::sin(rad);
        double sx = flipH ? -1.0 : 1.0;
        double sy = flipV ? -1.0 : 1.0;

        // M = T(fx + cx, fy + cy) * R(rad) * S(sx, sy) * T(-cx, -cy)
        double a = cs * sx;
        double b = sn * sx;
        double c = -sn * sy;
        double d = cs * sy;
        loc.a = a; loc.b = b; loc.c = c; loc.d = d;
        loc.tx = fx + cx - (a * cx + c * cy);
        loc.ty = fy + cy - (b * cx + d * cy);
    }
    return SkAffine::Multiply(parentXf, loc);
}

inline bool IsSketchShapeClass(std::string_view cls) {
    return cls == "rectangle" || cls == "oval" || cls == "triangle" ||
           cls == "polygon" || cls == "star" || cls == "shapePath";
}

struct SketchZipImageCache {
    const uint8_t* zipData = nullptr;
    size_t zipSize = 0;
    const std::vector<ZipDeflate::ArchiveItemMeta>* entries = nullptr;
    struct DecodedImg { uint32_t w = 0, h = 0; std::vector<uint8_t> rgba; bool ok = false; };
    std::unordered_map<std::string, DecodedImg> cache;

    const DecodedImg* GetImage(const std::string& refPath) {
        if (refPath.empty() || !entries) return nullptr;
        auto it = cache.find(refPath);
        if (it != cache.end()) return it->second.ok ? &it->second : nullptr;

        DecodedImg& slot = cache[refPath];
        std::wstring wRef = PathUtils::Utf8ToWide(refPath);
        for (wchar_t& ch : wRef) {
            if (ch == L'\\') ch = L'/';
            ch = static_cast<wchar_t>(towlower(ch));
        }

        const ZipDeflate::ArchiveItemMeta* matched = nullptr;
        for (const auto& e : *entries) {
            if (e.isDirectory) continue;
            std::wstring ep = e.pathInArchive;
            for (wchar_t& ch : ep) {
                if (ch == L'\\') ch = L'/';
                ch = static_cast<wchar_t>(towlower(ch));
            }
            if (ep == wRef || (ep.rfind(wRef + L".", 0) == 0)) {
                matched = &e;
                break;
            }
        }
        if (!matched) return nullptr;
        std::vector<uint8_t> raw;
        if (!ZipDeflate::ExtractZipEntryFromMemory(zipData, zipSize, *matched, raw, 64u * 1024u * 1024u)) {
            return nullptr;
        }
        slot.ok = DetailSketch::DecodeEmbeddedRasterStraightRgba(raw.data(), raw.size(), slot.w, slot.h, slot.rgba);
        return slot.ok ? &slot : nullptr;
    }
};

inline SkPaint BuildSketchPaint(
    const ::Explorer2::Json::Value& item,
    const SkAffine& shapeXf, double fw, double fh,
    float layerOpacity, SketchZipImageCache& imgCache)
{
    SkPaint p;
    p.fillType = item.GetInt("fillType", 0);
    p.solid = ParseSketchColor(item.Find("color"));
    float itemOp = 1.0f;
    if (const auto* cs = item.Find("contextSettings")) {
        itemOp = static_cast<float>(std::clamp(JsonNum(cs, "opacity", 1.0), 0.0, 1.0));
    }
    p.opacityMul = layerOpacity * itemOp;

    // Unit square [0..1]x[0..1] -> local [0..fw]x[0..fh] -> canvas
    SkAffine scaleUnit{ std::max(1e-6, fw), 0.0, 0.0, std::max(1e-6, fh), 0.0, 0.0 };
    p.localToCanvas = SkAffine::Multiply(shapeXf, scaleUnit);
    p.hasUnitInv = p.localToCanvas.Invert(p.canvasToUnit);

    if (p.fillType == 1) {
        if (const auto* grad = item.Find("gradient"); grad && grad->IsObject()) {
            p.gradientType = grad->GetInt("gradientType", 0);
            p.gradFrom = ParseSketchPointVal(grad->Find("from"), { 0.5, 0.0 });
            p.gradTo   = ParseSketchPointVal(grad->Find("to"),   { 0.5, 1.0 });
            p.ellipseLength = JsonNum(grad, "elipseLength", 1.0);
            if (const auto* stops = grad->Find("stops"); stops && stops->IsArray()) {
                for (const auto& sv : stops->arrVal) {
                    if (!sv.IsObject()) continue;
                    SkGradStop gs;
                    gs.pos = static_cast<float>(std::clamp(JsonNum(&sv, "position", 0.0), 0.0, 1.0));
                    gs.col = ParseSketchColor(sv.Find("color"));
                    p.stops.push_back(gs);
                }
                std::sort(p.stops.begin(), p.stops.end(), [](const auto& a, const auto& b) { return a.pos < b.pos; });
            }
        }
    } else if (p.fillType == 4) {
        if (const auto* im = item.Find("image"); im && im->IsObject()) {
            std::string ref = im->GetString("_ref");
            if (const auto* dec = imgCache.GetImage(ref)) {
                p.patRgba = &dec->rgba;
                p.patW = dec->w;
                p.patH = dec->h;
            }
        }
    }
    return p;
}

inline void ApplySketchStyleToContours(
    SketchCanvas& canvas,
    const std::vector<SkContour>& canvasContours,
    const ::Explorer2::Json::Value* styleObj,
    const SkAffine& shapeXf, double fw, double fh,
    float layerOpacity, SketchZipImageCache& imgCache)
{
    if (!styleObj || !styleObj->IsObject() || canvasContours.empty()) return;
    int windingRule = styleObj->GetInt("windingRule", 0); // 0=NonZero, 1=EvenOdd
    if (const auto* bs = styleObj->Find("borderOptions"); bs && bs->IsObject()) {
        (void)bs;
    }

    int fx0 = 0, fy0 = 0, fx1 = -1, fy1 = -1;
    std::vector<float> fillCov;
    bool computedCov = false;
    auto ensureCov = [&]() {
        if (!computedCov) {
            computedCov = true;
            canvas.ComputeFillCoverage(canvasContours, windingRule == 1, fx0, fy0, fx1, fy1, fillCov);
        }
    };

    if (const auto* fills = styleObj->Find("fills"); fills && fills->IsArray()) {
        for (const auto& fv : fills->arrVal) {
            if (!fv.IsObject() || !fv.GetBool("isEnabled", true)) continue;
            ensureCov();
            SkPaint paint = BuildSketchPaint(fv, shapeXf, fw, fh, layerOpacity, imgCache);
            canvas.FillCoverageWithPaint(fx0, fy0, fx1, fy1, fillCov, paint);
        }
    }

    if (const auto* borders = styleObj->Find("borders"); borders && borders->IsArray()) {
        double scale = shapeXf.AvgScale();
        for (const auto& bv : borders->arrVal) {
            if (!bv.IsObject() || !bv.GetBool("isEnabled", true)) continue;
            double thick = JsonNum(&bv, "thickness", 1.0) * scale;
            if (thick <= 0.05) continue;
            int posMode = bv.GetInt("position", 0); // 0=Center, 1=Inside, 2=Outside
            bool hasClosed = false;
            for (const auto& c : canvasContours) if (c.closed && c.pts.size() >= 3) { hasClosed = true; break; }
            if (hasClosed && (posMode == 1 || posMode == 2)) ensureCov();
            SkPaint paint = BuildSketchPaint(bv, shapeXf, fw, fh, layerOpacity, imgCache);
            canvas.StrokeContours(canvasContours, thick, hasClosed ? posMode : 0, paint,
                                  fx0, fy0, fx1, fy1, hasClosed ? &fillCov : nullptr);
        }
    }
}

inline void DrawSketchTextLayer(
    SketchCanvas& canvas, const SkAffine& xf, double fw, double fh,
    const ::Explorer2::Json::Value& layer, float opacity)
{
    std::string text;
    SkColor col{ 0.12f, 0.12f, 0.14f, 1.0f };
    double fontSize = 14.0;

    if (const auto* as = layer.Find("attributedString"); as && as->IsObject()) {
        text = as->GetString("string");
        if (const auto* attrs = as->Find("attributes"); attrs && attrs->IsArray() && !attrs->arrVal.empty()) {
            if (const auto* a0 = attrs->arrVal[0].Find("attributes"); a0 && a0->IsObject()) {
                if (const auto* ca = a0->Find("MSAttributedStringColorAttribute"); ca && ca->IsObject()) {
                    col = ParseSketchColor(ca);
                }
                if (const auto* fa = a0->Find("MSAttributedStringFontAttribute"); fa && fa->IsObject()) {
                    if (const auto* fattr = fa->Find("attributes"); fattr && fattr->IsObject()) {
                        fontSize = JsonNum(fattr, "size", fontSize);
                    }
                }
            }
        }
    }
    if (text.empty()) text = layer.GetString("name");
    if (text.empty()) return;

    col.a = std::clamp(col.a * opacity, 0.0f, 1.0f);
    double scale = xf.AvgScale();
    int glyphScale = std::clamp(static_cast<int>(std::lround((fontSize * scale) / 7.0)), 1, 8);
    double advanceX = (6.0 * glyphScale) / std::max(1e-4, scale);
    double lineH = (9.0 * glyphScale) / std::max(1e-4, scale);

    double curX = 0.0, curY = 0.0;
    for (char ch : text) {
        if (ch == '\r') continue;
        if (ch == '\n') { curX = 0.0; curY += lineH; continue; }
        if (fw > 4.0 && curX + advanceX > fw && curX > 0.0) { curX = 0.0; curY += lineH; }
        if (fh > 4.0 && curY > fh) break;
        if (ch != ' ') {
            for (int ry = 0; ry < 7; ++ry) {
                uint8_t bits = DetailLayeredAndHdrImg::GlyphRow5x7(ch, ry);
                for (int rx = 0; rx < 5; ++rx) {
                    if (((bits >> (4 - rx)) & 1) == 0) continue;
                    SkPt lp{ curX + (rx * glyphScale) / std::max(1e-4, scale),
                             curY + (ry * glyphScale) / std::max(1e-4, scale) };
                    SkPt cp = xf.Apply(lp.x, lp.y);
                    for (int sy = 0; sy < glyphScale; ++sy) {
                        for (int sx = 0; sx < glyphScale; ++sx) {
                            int px = static_cast<int>(std::lround(cp.x)) + sx;
                            int py = static_cast<int>(std::lround(cp.y)) + sy;
                            if (px >= 0 && px < static_cast<int>(canvas.w) && py >= 0 && py < static_cast<int>(canvas.h)) {
                                canvas.BlendPremul(static_cast<uint32_t>(px), static_cast<uint32_t>(py), col, 1.0f);
                            }
                        }
                    }
                }
            }
        }
        curX += advanceX;
    }
}

inline void RenderSketchLayerRecursive(
    SketchCanvas& canvas,
    const SkAffine& parentXf,
    const ::Explorer2::Json::Value& layer,
    float parentOpacity,
    SketchZipImageCache& imgCache,
    std::vector<std::wstring>& ioLayerNames,
    int depth)
{
    if (depth > 32 || !layer.IsObject()) return;
    if (!layer.GetBool("isVisible", true)) return;

    std::string cls = layer.GetString("_class");
    std::string nm = layer.GetString("name");
    if (!nm.empty() && ioLayerNames.size() < 64) {
        ioLayerNames.push_back(PathUtils::Utf8ToWide(nm));
    }

    float localOp = 1.0f;
    const auto* style = layer.Find("style");
    if (style && style->IsObject()) {
        if (const auto* cs = style->Find("contextSettings"); cs && cs->IsObject()) {
            localOp = static_cast<float>(std::clamp(JsonNum(cs, "opacity", 1.0), 0.0, 1.0));
        }
    }
    float curOp = parentOpacity * localOp;
    if (curOp <= 1e-4f) return;

    double fw = 0.0, fh = 0.0;
    SkAffine xf = BuildSketchLayerTransform(parentXf, layer, fw, fh);

    if (IsSketchShapeClass(cls)) {
        SkContour localC = BuildLocalShapeContour(layer, fw, fh);
        if (localC.pts.size() >= 2) {
            SkContour canvasC;
            canvasC.closed = localC.closed;
            canvasC.pts.reserve(localC.pts.size());
            for (const auto& pt : localC.pts) canvasC.pts.push_back(xf.Apply(pt.x, pt.y));
            std::vector<SkContour> vec = { std::move(canvasC) };
            ApplySketchStyleToContours(canvas, vec, style, xf, fw, fh, curOp, imgCache);
        }
        return;
    }

    if (cls == "shapeGroup") {
        bool hasGroupPaint = false;
        if (style && style->IsObject()) {
            if (const auto* fills = style->Find("fills"); fills && fills->IsArray()) {
                for (const auto& fv : fills->arrVal) if (fv.GetBool("isEnabled", true)) hasGroupPaint = true;
            }
            if (const auto* borders = style->Find("borders"); borders && borders->IsArray()) {
                for (const auto& bv : borders->arrVal) if (bv.GetBool("isEnabled", true)) hasGroupPaint = true;
            }
        }
        const auto* children = layer.Find("layers");
        if (hasGroupPaint && children && children->IsArray()) {
            std::vector<SkContour> combined;
            for (const auto& ch : children->arrVal) {
                if (!ch.IsObject() || !ch.GetBool("isVisible", true)) continue;
                std::string ccls = ch.GetString("_class");
                if (!IsSketchShapeClass(ccls)) continue;
                double cfw = 0.0, cfh = 0.0;
                SkAffine cxf = BuildSketchLayerTransform(xf, ch, cfw, cfh);
                SkContour lc = BuildLocalShapeContour(ch, cfw, cfh);
                if (lc.pts.size() >= 2) {
                    SkContour cc;
                    cc.closed = lc.closed;
                    cc.pts.reserve(lc.pts.size());
                    for (const auto& pt : lc.pts) cc.pts.push_back(cxf.Apply(pt.x, pt.y));
                    combined.push_back(std::move(cc));
                }
            }
            if (!combined.empty()) {
                ApplySketchStyleToContours(canvas, combined, style, xf, fw, fh, curOp, imgCache);
                return;
            }
        }
    }

    if (cls == "bitmap") {
        if (const auto* im = layer.Find("image"); im && im->IsObject() && fw > 0.5 && fh > 0.5) {
            std::string ref = im->GetString("_ref");
            if (const auto* dec = imgCache.GetImage(ref)) {
                SkAffine scaleUnit{ fw, 0.0, 0.0, fh, 0.0, 0.0 };
                SkAffine u2c = SkAffine::Multiply(xf, scaleUnit);
                SkAffine c2u;
                if (u2c.Invert(c2u)) {
                    SkPt c0 = u2c.Apply(0, 0), c1 = u2c.Apply(1, 0), c2 = u2c.Apply(1, 1), c3 = u2c.Apply(0, 1);
                    int x0 = std::max(0, static_cast<int>(std::floor(std::min({ c0.x, c1.x, c2.x, c3.x }))));
                    int y0 = std::max(0, static_cast<int>(std::floor(std::min({ c0.y, c1.y, c2.y, c3.y }))));
                    int x1 = std::min(static_cast<int>(canvas.w) - 1, static_cast<int>(std::ceil(std::max({ c0.x, c1.x, c2.x, c3.x }))));
                    int y1 = std::min(static_cast<int>(canvas.h) - 1, static_cast<int>(std::ceil(std::max({ c0.y, c1.y, c2.y, c3.y }))));
                    for (int py = y0; py <= y1; ++py) {
                        for (int px = x0; px <= x1; ++px) {
                            SkPt u = c2u.Apply(px + 0.5, py + 0.5);
                            if (u.x < 0.0 || u.x >= 1.0 || u.y < 0.0 || u.y >= 1.0) continue;
                            uint32_t sx = std::min(dec->w - 1u, static_cast<uint32_t>(u.x * dec->w));
                            uint32_t sy = std::min(dec->h - 1u, static_cast<uint32_t>(u.y * dec->h));
                            const uint8_t* sp = &dec->rgba[(static_cast<size_t>(sy) * dec->w + sx) * 4u];
                            SkColor sc{ sp[0] / 255.0f, sp[1] / 255.0f, sp[2] / 255.0f, (sp[3] / 255.0f) * curOp };
                            canvas.BlendPremul(static_cast<uint32_t>(px), static_cast<uint32_t>(py), sc, 1.0f);
                        }
                    }
                }
            }
        }
        return;
    }

    if (cls == "text") {
        DrawSketchTextLayer(canvas, xf, fw, fh, layer, curOp);
        return;
    }

    // Container layers: artboard, symbolMaster, group, shapeGroup fallback
    if (cls == "artboard" || cls == "symbolMaster") {
        if (layer.GetBool("hasBackgroundColor", false) && fw > 0.5 && fh > 0.5) {
            SkContour bgRect;
            bgRect.closed = true;
            bgRect.pts = { xf.Apply(0, 0), xf.Apply(fw, 0), xf.Apply(fw, fh), xf.Apply(0, fh) };
            std::vector<SkContour> bgVec = { std::move(bgRect) };
            int bx0 = 0, by0 = 0, bx1 = -1, by1 = -1;
            std::vector<float> bgCov;
            canvas.ComputeFillCoverage(bgVec, false, bx0, by0, bx1, by1, bgCov);
            SkPaint bgPaint;
            bgPaint.fillType = 0;
            bgPaint.solid = ParseSketchColor(layer.Find("backgroundColor"));
            bgPaint.opacityMul = curOp;
            canvas.FillCoverageWithPaint(bx0, by0, bx1, by1, bgCov, bgPaint);
        }
    }

    if (const auto* children = layer.Find("layers"); children && children->IsArray()) {
        for (const auto& ch : children->arrVal) {
            RenderSketchLayerRecursive(canvas, xf, ch, curOp, imgCache, ioLayerNames, depth + 1);
        }
    }
}

} // namespace DetailSketch

inline bool DecodeSketchDocument(const uint8_t* data, size_t size, uint32_t maxDimension, ExtendedImageInfo& outInfo) {
    using namespace DetailSketch;

    if (!data || size < 30 || data[0] != 'P' || data[1] != 'K') return false;

    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries) || entries.empty()) return false;

    const ZipDeflate::ArchiveItemMeta* docEntry = nullptr;
    const ZipDeflate::ArchiveItemMeta* metaEntry = nullptr;
    const ZipDeflate::ArchiveItemMeta* previewEntry = nullptr;
    std::vector<const ZipDeflate::ArchiveItemMeta*> pageEntries;

    for (const auto& e : entries) {
        if (e.isDirectory) continue;
        std::wstring p = e.pathInArchive;
        for (wchar_t& ch : p) {
            if (ch == L'\\') ch = L'/';
            ch = static_cast<wchar_t>(towlower(ch));
        }
        if (p == L"document.json") docEntry = &e;
        else if (p == L"meta.json") metaEntry = &e;
        else if (p == L"previews/preview.png") previewEntry = &e;
        else if (p.rfind(L"pages/", 0) == 0 && p.size() > 11 && p.substr(p.size() - 5) == L".json") {
            pageEntries.push_back(&e);
        }
    }

    if (!docEntry && !metaEntry && pageEntries.empty()) return false;

    std::vector<std::wstring> orderedPagePaths;
    if (docEntry) {
        std::vector<uint8_t> docBytes;
        if (ZipDeflate::ExtractZipEntryFromMemory(data, size, *docEntry, docBytes, 32u * 1024u * 1024u)) {
            ::Explorer2::Json::Value docJson;
            std::string_view docSv(reinterpret_cast<const char*>(docBytes.data()), docBytes.size());
            if (::Explorer2::Json::ParseDocument(docSv, docJson) && docJson.IsObject()) {
                if (const auto* pagesArr = docJson.Find("pages"); pagesArr && pagesArr->IsArray()) {
                    for (const auto& pr : pagesArr->arrVal) {
                        std::string ref = pr.GetString("_ref");
                        if (!ref.empty()) {
                            if (ref.size() < 5 || ref.substr(ref.size() - 5) != ".json") ref += ".json";
                            std::wstring wref = PathUtils::Utf8ToWide(ref);
                            for (wchar_t& ch : wref) {
                                if (ch == L'\\') ch = L'/';
                                ch = static_cast<wchar_t>(towlower(ch));
                            }
                            orderedPagePaths.push_back(std::move(wref));
                        }
                    }
                }
            }
        }
    }

    std::vector<const ZipDeflate::ArchiveItemMeta*> orderedPages;
    for (const auto& wp : orderedPagePaths) {
        for (const auto* pe : pageEntries) {
            std::wstring p = pe->pathInArchive;
            for (wchar_t& ch : p) {
                if (ch == L'\\') ch = L'/';
                ch = static_cast<wchar_t>(towlower(ch));
            }
            if (p == wp) { orderedPages.push_back(pe); break; }
        }
    }
    for (const auto* pe : pageEntries) {
        if (std::find(orderedPages.begin(), orderedPages.end(), pe) == orderedPages.end()) {
            orderedPages.push_back(pe);
        }
    }

    SketchZipImageCache imgCache{ data, size, &entries, {} };

    for (const auto* pe : orderedPages) {
        std::vector<uint8_t> pageBytes;
        if (!ZipDeflate::ExtractZipEntryFromMemory(data, size, *pe, pageBytes, 64u * 1024u * 1024u)) continue;
        ::Explorer2::Json::Value pageJson;
        std::string_view pageSv(reinterpret_cast<const char*>(pageBytes.data()), pageBytes.size());
        if (!::Explorer2::Json::ParseDocument(pageSv, pageJson) || !pageJson.IsObject()) continue;

        const auto* layersArr = pageJson.Find("layers");
        if (!layersArr || !layersArr->IsArray() || layersArr->arrVal.empty()) continue;

        // Prefer first visible artboard (or symbolMaster); else union bounds of visible top-level layers
        const ::Explorer2::Json::Value* chosenArtboard = nullptr;
        for (const auto& lv : layersArr->arrVal) {
            if (!lv.IsObject() || !lv.GetBool("isVisible", true)) continue;
            std::string cls = lv.GetString("_class");
            if (cls == "artboard" || cls == "symbolMaster") {
                const auto* fr = lv.Find("frame");
                if (JsonNum(fr, "width", 0.0) >= 1.0 && JsonNum(fr, "height", 0.0) >= 1.0) {
                    chosenArtboard = &lv;
                    break;
                }
            }
        }

        double docX = 0.0, docY = 0.0, docW = 0.0, docH = 0.0;
        if (chosenArtboard) {
            const auto* fr = chosenArtboard->Find("frame");
            docX = JsonNum(fr, "x", 0.0);
            docY = JsonNum(fr, "y", 0.0);
            docW = JsonNum(fr, "width", 1.0);
            docH = JsonNum(fr, "height", 1.0);
        } else {
            double minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30;
            for (const auto& lv : layersArr->arrVal) {
                if (!lv.IsObject() || !lv.GetBool("isVisible", true)) continue;
                const auto* fr = lv.Find("frame");
                if (!fr || !fr->IsObject()) continue;
                double lx = JsonNum(fr, "x", 0.0), ly = JsonNum(fr, "y", 0.0);
                double lw = JsonNum(fr, "width", 0.0), lh = JsonNum(fr, "height", 0.0);
                if (lw <= 0.0 && lh <= 0.0) continue;
                minX = std::min(minX, lx); minY = std::min(minY, ly);
                maxX = std::max(maxX, lx + lw); maxY = std::max(maxY, ly + lh);
            }
            if (minX >= maxX || minY >= maxY) continue;
            docX = minX; docY = minY;
            docW = maxX - minX; docH = maxY - minY;
        }

        if (docW <= 0.5 || docH <= 0.5) continue;
        uint32_t targetMax = (maxDimension > 0) ? std::min<uint32_t>(maxDimension, 2048u) : 2048u;
        double scale = 1.0;
        double maxSide = std::max(docW, docH);
        if (maxSide > targetMax) scale = static_cast<double>(targetMax) / maxSide;
        else if (maxSide < 64.0) scale = std::min(8.0, 256.0 / maxSide);

        uint32_t outW = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(docW * scale)), 1u, 4096u);
        uint32_t outH = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(docH * scale)), 1u, 4096u);

        SketchCanvas canvas;
        canvas.Init(outW, outH);
        outInfo.layerNames.clear();

        SkAffine rootXf{ outW / docW, 0.0, 0.0, outH / docH, -docX * (outW / docW), -docY * (outH / docH) };
        if (chosenArtboard) {
            RenderSketchLayerRecursive(canvas, rootXf, *chosenArtboard, 1.0f, imgCache, outInfo.layerNames, 0);
        } else {
            for (const auto& lv : layersArr->arrVal) {
                RenderSketchLayerRecursive(canvas, rootXf, lv, 1.0f, imgCache, outInfo.layerNames, 0);
            }
        }

        const size_t numPx = static_cast<size_t>(outW) * outH;
        bool drewVisiblePixel = false;
        for (size_t i = 0; i < numPx; ++i) {
            if (canvas.premulRgba[i * 4u + 3] > (1.0f / 512.0f)) {
                drewVisiblePixel = true;
                break;
            }
        }
        if (!drewVisiblePixel) {
            // 0 visible shapes/bitmaps/backgrounds rendered on this page -> try next page or fall back to previews/preview.png
            continue;
        }

        outInfo.width = outW;
        outInfo.height = outH;
        outInfo.origWidth = static_cast<uint32_t>(std::max(1l, std::lround(docW)));
        outInfo.origHeight = static_cast<uint32_t>(std::max(1l, std::lround(docH)));
        outInfo.channels = 4;
        outInfo.bitsPerChannel = 8;
        outInfo.formatName = L"Sketch Vector Document (.sketch)";
        outInfo.bgraPixels.resize(numPx * 4u);
        for (size_t i = 0; i < numPx; ++i) {
            const float* p = &canvas.premulRgba[i * 4u];
            outInfo.bgraPixels[i * 4u + 0] = static_cast<uint8_t>(std::clamp(std::lround(p[2] * 255.0f), 0l, 255l));
            outInfo.bgraPixels[i * 4u + 1] = static_cast<uint8_t>(std::clamp(std::lround(p[1] * 255.0f), 0l, 255l));
            outInfo.bgraPixels[i * 4u + 2] = static_cast<uint8_t>(std::clamp(std::lround(p[0] * 255.0f), 0l, 255l));
            outInfo.bgraPixels[i * 4u + 3] = static_cast<uint8_t>(std::clamp(std::lround(p[3] * 255.0f), 0l, 255l));
        }
        outInfo.valid = true;
        return true;
    }

    // Fallback to previews/preview.png only if no page JSON layers could be rendered
    if (previewEntry) {
        std::vector<uint8_t> prevBytes;
        if (ZipDeflate::ExtractZipEntryFromMemory(data, size, *previewEntry, prevBytes, 32u * 1024u * 1024u)) {
            uint32_t pw = 0, ph = 0;
            std::vector<uint8_t> rgba;
            if (DecodeEmbeddedRasterStraightRgba(prevBytes.data(), prevBytes.size(), pw, ph, rgba) && pw > 0 && ph > 0) {
                size_t n = static_cast<size_t>(pw) * ph;
                outInfo.width = pw;
                outInfo.height = ph;
                outInfo.channels = 4;
                outInfo.bitsPerChannel = 8;
                outInfo.formatName = L"Sketch Vector Document (.sketch)";
                outInfo.bgraPixels.resize(n * 4u);
                for (size_t i = 0; i < n; ++i) {
                    WritePremulBGRA(&outInfo.bgraPixels[i * 4u],
                                    rgba[i * 4u + 0], rgba[i * 4u + 1], rgba[i * 4u + 2], rgba[i * 4u + 3]);
                }
                outInfo.valid = true;
                return true;
            }
        }
    }
    return false;
}

} // namespace Formats
