#pragma once
// linux/win32_hal/include/d2d1.h - Direct2D HAL interface & struct definitions for Linux.

#include <unknwn.h>
#include <dwrite.h>
#include <cmath>

struct IWICBitmap;

using D2D1_TAG = UINT64;

#ifndef DXGI_FORMAT_DEFINED
#define DXGI_FORMAT_DEFINED
enum DXGI_FORMAT {
    DXGI_FORMAT_UNKNOWN = 0,
    DXGI_FORMAT_R8G8B8A8_UNORM = 28,
    DXGI_FORMAT_B8G8R8A8_UNORM = 87,
    DXGI_FORMAT_B8G8R8X8_UNORM = 88
};
#endif

enum D2D1_ALPHA_MODE {
    D2D1_ALPHA_MODE_UNKNOWN = 0,
    D2D1_ALPHA_MODE_PREMULTIPLIED = 1,
    D2D1_ALPHA_MODE_STRAIGHT = 2,
    D2D1_ALPHA_MODE_IGNORE = 3
};

enum D2D1_FACTORY_TYPE {
    D2D1_FACTORY_TYPE_SINGLE_THREADED = 0,
    D2D1_FACTORY_TYPE_MULTI_THREADED = 1
};

enum D2D1_RENDER_TARGET_TYPE {
    D2D1_RENDER_TARGET_TYPE_DEFAULT = 0,
    D2D1_RENDER_TARGET_TYPE_SOFTWARE = 1,
    D2D1_RENDER_TARGET_TYPE_HARDWARE = 2
};

enum D2D1_RENDER_TARGET_USAGE {
    D2D1_RENDER_TARGET_USAGE_NONE = 0,
    D2D1_RENDER_TARGET_USAGE_FORCE_BITMAP_REMOTING = 1,
    D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE = 2
};

enum D2D1_FEATURE_LEVEL {
    D2D1_FEATURE_LEVEL_DEFAULT = 0,
    D2D1_FEATURE_LEVEL_9 = 0x9100,
    D2D1_FEATURE_LEVEL_10 = 0xa000
};

enum D2D1_PRESENT_OPTIONS {
    D2D1_PRESENT_OPTIONS_NONE = 0,
    D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS = 1,
    D2D1_PRESENT_OPTIONS_IMMEDIATELY = 2
};

enum D2D1_ANTIALIAS_MODE {
    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE = 0,
    D2D1_ANTIALIAS_MODE_ALIASED = 1
};

enum D2D1_BITMAP_INTERPOLATION_MODE {
    D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR = 0,
    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR = 1
};

enum D2D1_DRAW_TEXT_OPTIONS {
    D2D1_DRAW_TEXT_OPTIONS_NO_SNAP = 0x00000001,
    D2D1_DRAW_TEXT_OPTIONS_CLIP = 0x00000002,
    D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT = 0x00000004,
    D2D1_DRAW_TEXT_OPTIONS_NONE = 0x00000000
};

enum D2D1_FIGURE_BEGIN {
    D2D1_FIGURE_BEGIN_FILLED = 0,
    D2D1_FIGURE_BEGIN_HOLLOW = 1
};

enum D2D1_FIGURE_END {
    D2D1_FIGURE_END_OPEN = 0,
    D2D1_FIGURE_END_CLOSED = 1
};

enum D2D1_SWEEP_DIRECTION {
    D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE = 0,
    D2D1_SWEEP_DIRECTION_CLOCKWISE = 1
};

enum D2D1_ARC_SIZE {
    D2D1_ARC_SIZE_SMALL = 0,
    D2D1_ARC_SIZE_LARGE = 1
};

#define D2DERR_RECREATE_TARGET (static_cast<HRESULT>(0x8899000Cu))

struct D2D1_POINT_2F { FLOAT x; FLOAT y; };
struct D2D1_SIZE_F   { FLOAT width; FLOAT height; };
struct D2D1_SIZE_U   { UINT32 width; UINT32 height; };
struct D2D1_RECT_F   { FLOAT left; FLOAT top; FLOAT right; FLOAT bottom; };
struct D2D1_RECT_U   { UINT32 left; UINT32 top; UINT32 right; UINT32 bottom; };
struct D2D1_COLOR_F  { FLOAT r; FLOAT g; FLOAT b; FLOAT a; };

struct D2D1_ROUNDED_RECT {
    D2D1_RECT_F rect;
    FLOAT radiusX;
    FLOAT radiusY;
};

struct D2D1_ELLIPSE {
    D2D1_POINT_2F point;
    FLOAT radiusX;
    FLOAT radiusY;
};

struct D2D1_ARC_SEGMENT {
    D2D1_POINT_2F point;
    D2D1_SIZE_F size;
    FLOAT rotationAngle;
    D2D1_SWEEP_DIRECTION sweepDirection;
    D2D1_ARC_SIZE arcSize;
};

struct D2D1_MATRIX_3X2_F {
    union {
        struct { FLOAT m11, m12, m21, m22, dx, dy; };
        struct { FLOAT _11, _12, _21, _22, _31, _32; };
        FLOAT m[3][2];
    };
};

struct D2D1_PIXEL_FORMAT {
    DXGI_FORMAT format;
    D2D1_ALPHA_MODE alphaMode;
};

struct D2D1_BITMAP_PROPERTIES {
    D2D1_PIXEL_FORMAT pixelFormat;
    FLOAT dpiX;
    FLOAT dpiY;
};

struct D2D1_BRUSH_PROPERTIES {
    FLOAT opacity;
    D2D1_MATRIX_3X2_F transform;
};

struct D2D1_RENDER_TARGET_PROPERTIES {
    D2D1_RENDER_TARGET_TYPE type;
    D2D1_PIXEL_FORMAT pixelFormat;
    FLOAT dpiX;
    FLOAT dpiY;
    D2D1_RENDER_TARGET_USAGE usage;
    D2D1_FEATURE_LEVEL minLevel;
};

struct D2D1_HWND_RENDER_TARGET_PROPERTIES {
    HWND hwnd;
    D2D1_SIZE_U pixelSize;
    D2D1_PRESENT_OPTIONS presentOptions;
};

namespace D2D1 {

inline constexpr D2D1_POINT_2F Point2F(FLOAT x = 0.f, FLOAT y = 0.f) noexcept { return { x, y }; }
inline constexpr D2D1_SIZE_F SizeF(FLOAT w = 0.f, FLOAT h = 0.f) noexcept { return { w, h }; }
inline constexpr D2D1_SIZE_U SizeU(UINT32 w = 0u, UINT32 h = 0u) noexcept { return { w, h }; }
inline constexpr D2D1_RECT_F RectF(FLOAT l = 0.f, FLOAT t = 0.f, FLOAT r = 0.f, FLOAT b = 0.f) noexcept { return { l, t, r, b }; }
inline constexpr D2D1_RECT_U RectU(UINT32 l = 0u, UINT32 t = 0u, UINT32 r = 0u, UINT32 b = 0u) noexcept { return { l, t, r, b }; }
inline constexpr D2D1_ROUNDED_RECT RoundedRect(const D2D1_RECT_F& rect, FLOAT rx, FLOAT ry) noexcept { return { rect, rx, ry }; }
inline constexpr D2D1_ELLIPSE Ellipse(const D2D1_POINT_2F& center, FLOAT rx, FLOAT ry) noexcept { return { center, rx, ry }; }
inline constexpr D2D1_ARC_SEGMENT ArcSegment(
    const D2D1_POINT_2F& point,
    const D2D1_SIZE_F& size,
    FLOAT rotationAngle,
    D2D1_SWEEP_DIRECTION sweepDirection,
    D2D1_ARC_SIZE arcSize) noexcept
{
    return { point, size, rotationAngle, sweepDirection, arcSize };
}

class ColorF : public D2D1_COLOR_F {
public:
    enum Enum : UINT32 {
        Black = 0x000000u,
        White = 0xFFFFFFu,
        Red   = 0xFF0000u,
        Green = 0x008000u,
        Blue  = 0x0000FFu,
        Gray  = 0x808080u
    };
    constexpr ColorF(FLOAT red, FLOAT green, FLOAT blue, FLOAT alpha = 1.0f) noexcept
        : D2D1_COLOR_F{ red, green, blue, alpha } {}
    constexpr ColorF(UINT32 rgb, FLOAT alpha = 1.0f) noexcept
        : D2D1_COLOR_F{
            static_cast<FLOAT>((rgb >> 16) & 0xFFu) / 255.0f,
            static_cast<FLOAT>((rgb >> 8) & 0xFFu) / 255.0f,
            static_cast<FLOAT>(rgb & 0xFFu) / 255.0f,
            alpha } {}
    constexpr ColorF(Enum knownColor, FLOAT alpha = 1.0f) noexcept
        : ColorF(static_cast<UINT32>(knownColor), alpha) {}
};

class Matrix3x2F : public D2D1_MATRIX_3X2_F {
public:
    constexpr Matrix3x2F(FLOAT m11_ = 1.f, FLOAT m12_ = 0.f, FLOAT m21_ = 0.f, FLOAT m22_ = 1.f, FLOAT dx_ = 0.f, FLOAT dy_ = 0.f) noexcept {
        _11 = m11_; _12 = m12_; _21 = m21_; _22 = m22_; _31 = dx_; _32 = dy_;
    }
    static constexpr Matrix3x2F Identity() noexcept { return Matrix3x2F(1.f, 0.f, 0.f, 1.f, 0.f, 0.f); }
    static constexpr Matrix3x2F Translation(FLOAT x, FLOAT y) noexcept { return Matrix3x2F(1.f, 0.f, 0.f, 1.f, x, y); }
    static constexpr Matrix3x2F Scale(FLOAT sx, FLOAT sy, D2D1_POINT_2F center = Point2F()) noexcept {
        return Matrix3x2F(sx, 0.f, 0.f, sy, center.x - sx * center.x, center.y - sy * center.y);
    }
};

inline constexpr D2D1_PIXEL_FORMAT PixelFormat(
    DXGI_FORMAT dxgiFormat = DXGI_FORMAT_UNKNOWN,
    D2D1_ALPHA_MODE alphaMode = D2D1_ALPHA_MODE_UNKNOWN) noexcept
{
    return { dxgiFormat, alphaMode };
}

inline constexpr D2D1_BITMAP_PROPERTIES BitmapProperties(
    const D2D1_PIXEL_FORMAT& pixelFormat = PixelFormat(),
    FLOAT dpiX = 96.0f,
    FLOAT dpiY = 96.0f) noexcept
{
    return { pixelFormat, dpiX, dpiY };
}

inline constexpr D2D1_RENDER_TARGET_PROPERTIES RenderTargetProperties(
    D2D1_RENDER_TARGET_TYPE type = D2D1_RENDER_TARGET_TYPE_DEFAULT,
    const D2D1_PIXEL_FORMAT& pixelFormat = PixelFormat(),
    FLOAT dpiX = 0.0f,
    FLOAT dpiY = 0.0f,
    D2D1_RENDER_TARGET_USAGE usage = D2D1_RENDER_TARGET_USAGE_NONE,
    D2D1_FEATURE_LEVEL minLevel = D2D1_FEATURE_LEVEL_DEFAULT) noexcept
{
    return { type, pixelFormat, dpiX, dpiY, usage, minLevel };
}

inline constexpr D2D1_HWND_RENDER_TARGET_PROPERTIES HwndRenderTargetProperties(
    HWND hwnd,
    D2D1_SIZE_U pixelSize = SizeU(),
    D2D1_PRESENT_OPTIONS presentOptions = D2D1_PRESENT_OPTIONS_NONE) noexcept
{
    return { hwnd, pixelSize, presentOptions };
}

} // namespace D2D1

// ---- Direct2D Interfaces --------------------------------------------------------------------------------------
struct ID2D1Factory;
struct ID2D1StrokeStyle : public IUnknown {};

struct ID2D1Resource : public IUnknown {};
struct ID2D1Image : public ID2D1Resource {};

struct ID2D1Bitmap : public ID2D1Image {
    virtual D2D1_SIZE_F STDMETHODCALLTYPE GetSize() const = 0;
    virtual D2D1_SIZE_U STDMETHODCALLTYPE GetPixelSize() const = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyFromMemory(const D2D1_RECT_U* dstRect, const void* srcData, UINT32 pitch) = 0;
};

struct ID2D1Brush : public ID2D1Resource {};

struct ID2D1SolidColorBrush : public ID2D1Brush {
    virtual void STDMETHODCALLTYPE SetColor(const D2D1_COLOR_F& color) = 0;
    void SetColor(const D2D1_COLOR_F* color) { if (color) SetColor(*color); }
    virtual D2D1_COLOR_F STDMETHODCALLTYPE GetColor() const = 0;
};

struct ID2D1SimplifiedGeometrySink : public IUnknown {
    virtual void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F startPoint, D2D1_FIGURE_BEGIN figureBegin) = 0;
    virtual void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END figureEnd) = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};

struct ID2D1GeometrySink : public ID2D1SimplifiedGeometrySink {
    virtual void STDMETHODCALLTYPE AddLine(D2D1_POINT_2F point) = 0;
    virtual void STDMETHODCALLTYPE AddArc(const D2D1_ARC_SEGMENT& arc) = 0;
    void AddArc(const D2D1_ARC_SEGMENT* arc) { if (arc) AddArc(*arc); }
};

struct ID2D1Geometry : public ID2D1Resource {};

struct ID2D1PathGeometry : public ID2D1Geometry {
    virtual HRESULT STDMETHODCALLTYPE Open(ID2D1GeometrySink** geometrySink) = 0;
};

struct ID2D1RenderTarget : public ID2D1Resource {
    virtual HRESULT STDMETHODCALLTYPE CreateBitmap(
        D2D1_SIZE_U size,
        const void* srcData,
        UINT32 pitch,
        const D2D1_BITMAP_PROPERTIES* bitmapProperties,
        ID2D1Bitmap** bitmap) = 0;

    HRESULT CreateBitmap(
        D2D1_SIZE_U size,
        const void* srcData,
        UINT32 pitch,
        const D2D1_BITMAP_PROPERTIES& bitmapProperties,
        ID2D1Bitmap** bitmap)
    {
        return CreateBitmap(size, srcData, pitch, &bitmapProperties, bitmap);
    }

    virtual HRESULT STDMETHODCALLTYPE CreateSolidColorBrush(
        const D2D1_COLOR_F* color,
        const D2D1_BRUSH_PROPERTIES* brushProperties,
        ID2D1SolidColorBrush** solidColorBrush) = 0;

    HRESULT CreateSolidColorBrush(const D2D1_COLOR_F& color, ID2D1SolidColorBrush** solidColorBrush) {
        return CreateSolidColorBrush(&color, nullptr, solidColorBrush);
    }

    virtual void STDMETHODCALLTYPE DrawLine(
        D2D1_POINT_2F point0,
        D2D1_POINT_2F point1,
        ID2D1Brush* brush,
        FLOAT strokeWidth = 1.0f,
        ID2D1StrokeStyle* strokeStyle = nullptr) = 0;

    virtual void STDMETHODCALLTYPE DrawRectangle(
        const D2D1_RECT_F* rect,
        ID2D1Brush* brush,
        FLOAT strokeWidth = 1.0f,
        ID2D1StrokeStyle* strokeStyle = nullptr) = 0;
    void DrawRectangle(const D2D1_RECT_F& rect, ID2D1Brush* brush, FLOAT strokeWidth = 1.0f, ID2D1StrokeStyle* strokeStyle = nullptr) {
        DrawRectangle(&rect, brush, strokeWidth, strokeStyle);
    }

    virtual void STDMETHODCALLTYPE FillRectangle(const D2D1_RECT_F* rect, ID2D1Brush* brush) = 0;
    void FillRectangle(const D2D1_RECT_F& rect, ID2D1Brush* brush) { FillRectangle(&rect, brush); }

    virtual void STDMETHODCALLTYPE DrawRoundedRectangle(
        const D2D1_ROUNDED_RECT* roundedRect,
        ID2D1Brush* brush,
        FLOAT strokeWidth = 1.0f,
        ID2D1StrokeStyle* strokeStyle = nullptr) = 0;
    void DrawRoundedRectangle(const D2D1_ROUNDED_RECT& roundedRect, ID2D1Brush* brush, FLOAT strokeWidth = 1.0f, ID2D1StrokeStyle* strokeStyle = nullptr) {
        DrawRoundedRectangle(&roundedRect, brush, strokeWidth, strokeStyle);
    }

    virtual void STDMETHODCALLTYPE FillRoundedRectangle(const D2D1_ROUNDED_RECT* roundedRect, ID2D1Brush* brush) = 0;
    void FillRoundedRectangle(const D2D1_ROUNDED_RECT& roundedRect, ID2D1Brush* brush) { FillRoundedRectangle(&roundedRect, brush); }

    virtual void STDMETHODCALLTYPE DrawEllipse(
        const D2D1_ELLIPSE* ellipse,
        ID2D1Brush* brush,
        FLOAT strokeWidth = 1.0f,
        ID2D1StrokeStyle* strokeStyle = nullptr) = 0;
    void DrawEllipse(const D2D1_ELLIPSE& ellipse, ID2D1Brush* brush, FLOAT strokeWidth = 1.0f, ID2D1StrokeStyle* strokeStyle = nullptr) {
        DrawEllipse(&ellipse, brush, strokeWidth, strokeStyle);
    }

    virtual void STDMETHODCALLTYPE FillEllipse(const D2D1_ELLIPSE* ellipse, ID2D1Brush* brush) = 0;
    void FillEllipse(const D2D1_ELLIPSE& ellipse, ID2D1Brush* brush) { FillEllipse(&ellipse, brush); }

    virtual void STDMETHODCALLTYPE DrawGeometry(
        ID2D1Geometry* geometry,
        ID2D1Brush* brush,
        FLOAT strokeWidth = 1.0f,
        ID2D1StrokeStyle* strokeStyle = nullptr) = 0;

    virtual void STDMETHODCALLTYPE FillGeometry(
        ID2D1Geometry* geometry,
        ID2D1Brush* brush,
        ID2D1Brush* opacityBrush = nullptr) = 0;

    virtual void STDMETHODCALLTYPE DrawBitmap(
        ID2D1Bitmap* bitmap,
        const D2D1_RECT_F* destinationRectangle = nullptr,
        FLOAT opacity = 1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE interpolationMode = D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
        const D2D1_RECT_F* sourceRectangle = nullptr) = 0;

    void DrawBitmap(
        ID2D1Bitmap* bitmap,
        const D2D1_RECT_F& destinationRectangle,
        FLOAT opacity = 1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE interpolationMode = D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
        const D2D1_RECT_F* sourceRectangle = nullptr)
    {
        DrawBitmap(bitmap, &destinationRectangle, opacity, interpolationMode, sourceRectangle);
    }

    virtual void STDMETHODCALLTYPE DrawText(
        const WCHAR* string,
        UINT32 stringLength,
        IDWriteTextFormat* textFormat,
        const D2D1_RECT_F* layoutRect,
        ID2D1Brush* defaultFillBrush,
        D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_NONE,
        DWRITE_MEASURING_MODE measuringMode = DWRITE_MEASURING_MODE_NATURAL) = 0;

    void DrawText(
        const WCHAR* string,
        UINT32 stringLength,
        IDWriteTextFormat* textFormat,
        const D2D1_RECT_F& layoutRect,
        ID2D1Brush* defaultFillBrush,
        D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_NONE,
        DWRITE_MEASURING_MODE measuringMode = DWRITE_MEASURING_MODE_NATURAL)
    {
        DrawText(string, stringLength, textFormat, &layoutRect, defaultFillBrush, options, measuringMode);
    }

    void DrawTextW(
        const WCHAR* string,
        UINT32 stringLength,
        IDWriteTextFormat* textFormat,
        const D2D1_RECT_F& layoutRect,
        ID2D1Brush* defaultFillBrush,
        D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_NONE,
        DWRITE_MEASURING_MODE measuringMode = DWRITE_MEASURING_MODE_NATURAL)
    {
        DrawText(string, stringLength, textFormat, &layoutRect, defaultFillBrush, options, measuringMode);
    }

    void DrawTextW(
        const WCHAR* string,
        UINT32 stringLength,
        IDWriteTextFormat* textFormat,
        const D2D1_RECT_F* layoutRect,
        ID2D1Brush* defaultFillBrush,
        D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_NONE,
        DWRITE_MEASURING_MODE measuringMode = DWRITE_MEASURING_MODE_NATURAL)
    {
        DrawText(string, stringLength, textFormat, layoutRect, defaultFillBrush, options, measuringMode);
    }

    virtual void STDMETHODCALLTYPE DrawTextLayout(
        D2D1_POINT_2F origin,
        IDWriteTextLayout* textLayout,
        ID2D1Brush* defaultFillBrush,
        D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_NONE) = 0;

    virtual void STDMETHODCALLTYPE SetTransform(const D2D1_MATRIX_3X2_F* transform) = 0;
    void SetTransform(const D2D1_MATRIX_3X2_F& transform) { SetTransform(&transform); }

    virtual void STDMETHODCALLTYPE Clear(const D2D1_COLOR_F* clearColor = nullptr) = 0;
    void Clear(const D2D1_COLOR_F& clearColor) { Clear(&clearColor); }

    virtual void STDMETHODCALLTYPE BeginDraw() = 0;
    virtual HRESULT STDMETHODCALLTYPE EndDraw(D2D1_TAG* tag1 = nullptr, D2D1_TAG* tag2 = nullptr) = 0;

    virtual void STDMETHODCALLTYPE PushAxisAlignedClip(const D2D1_RECT_F* clipRect, D2D1_ANTIALIAS_MODE antialiasMode) = 0;
    void PushAxisAlignedClip(const D2D1_RECT_F& clipRect, D2D1_ANTIALIAS_MODE antialiasMode) { PushAxisAlignedClip(&clipRect, antialiasMode); }
    virtual void STDMETHODCALLTYPE PopAxisAlignedClip() = 0;

    virtual void STDMETHODCALLTYPE SetDpi(FLOAT dpiX, FLOAT dpiY) = 0;
    virtual void STDMETHODCALLTYPE GetDpi(FLOAT* dpiX, FLOAT* dpiY) const = 0;
    virtual D2D1_SIZE_F STDMETHODCALLTYPE GetSize() const = 0;
    virtual D2D1_SIZE_U STDMETHODCALLTYPE GetPixelSize() const = 0;
};

struct ID2D1HwndRenderTarget : public ID2D1RenderTarget {
    virtual HRESULT STDMETHODCALLTYPE Resize(const D2D1_SIZE_U* pixelSize) = 0;
    HRESULT Resize(const D2D1_SIZE_U& pixelSize) { return Resize(&pixelSize); }
    virtual HWND STDMETHODCALLTYPE GetHwnd() const = 0;
};

struct ID2D1DCRenderTarget : public ID2D1RenderTarget {
    virtual HRESULT STDMETHODCALLTYPE BindDC(const HDC hDC, const RECT* pSubRect) = 0;
};

struct ID2D1Factory : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreatePathGeometry(ID2D1PathGeometry** pathGeometry) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateHwndRenderTarget(
        const D2D1_RENDER_TARGET_PROPERTIES* renderTargetProperties,
        const D2D1_HWND_RENDER_TARGET_PROPERTIES* hwndRenderTargetProperties,
        ID2D1HwndRenderTarget** hwndRenderTarget) = 0;

    HRESULT CreateHwndRenderTarget(
        const D2D1_RENDER_TARGET_PROPERTIES& renderTargetProperties,
        const D2D1_HWND_RENDER_TARGET_PROPERTIES& hwndRenderTargetProperties,
        ID2D1HwndRenderTarget** hwndRenderTarget)
    {
        return CreateHwndRenderTarget(&renderTargetProperties, &hwndRenderTargetProperties, hwndRenderTarget);
    }

    virtual HRESULT STDMETHODCALLTYPE CreateDCRenderTarget(
        const D2D1_RENDER_TARGET_PROPERTIES* renderTargetProperties,
        ID2D1DCRenderTarget** dcRenderTarget) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateWicBitmapRenderTarget(
        IWICBitmap* target,
        const D2D1_RENDER_TARGET_PROPERTIES* renderTargetProperties,
        ID2D1RenderTarget** renderTarget) = 0;

    HRESULT CreateWicBitmapRenderTarget(
        IWICBitmap* target,
        const D2D1_RENDER_TARGET_PROPERTIES& renderTargetProperties,
        ID2D1RenderTarget** renderTarget)
    {
        return CreateWicBitmapRenderTarget(target, &renderTargetProperties, renderTarget);
    }
};

E2_DECLARE_UUID(ID2D1Bitmap,           0xa2296057u, 0xea42u, 0x4099u, 0x98u, 0x3bu, 0x53u, 0x9fu, 0xb6u, 0x50u, 0x54u, 0x26u)
E2_DECLARE_UUID(ID2D1SolidColorBrush,  0x2cd906a9u, 0x12e2u, 0x11dcu, 0x9fu, 0xedu, 0x00u, 0x11u, 0x43u, 0xa0u, 0x55u, 0xf9u)
E2_DECLARE_UUID(ID2D1GeometrySink,     0x2cd9069fu, 0x12e2u, 0x11dcu, 0x9fu, 0xedu, 0x00u, 0x11u, 0x43u, 0xa0u, 0x55u, 0xf9u)
E2_DECLARE_UUID(ID2D1PathGeometry,     0x2cd906a5u, 0x12e2u, 0x11dcu, 0x9fu, 0xedu, 0x00u, 0x11u, 0x43u, 0xa0u, 0x55u, 0xf9u)
E2_DECLARE_UUID(ID2D1RenderTarget,     0x2cd90694u, 0x12e2u, 0x11dcu, 0x9fu, 0xedu, 0x00u, 0x11u, 0x43u, 0xa0u, 0x55u, 0xf9u)
E2_DECLARE_UUID(ID2D1HwndRenderTarget, 0x2cd90698u, 0x12e2u, 0x11dcu, 0x9fu, 0xedu, 0x00u, 0x11u, 0x43u, 0xa0u, 0x55u, 0xf9u)
E2_DECLARE_UUID(ID2D1DCRenderTarget,   0x1c51bc64u, 0xde61u, 0x46fdu, 0x98u, 0x99u, 0x63u, 0xa5u, 0xd8u, 0xf0u, 0x39u, 0x50u)
E2_DECLARE_UUID(ID2D1Factory,          0x06152247u, 0x6f50u, 0x465au, 0x92u, 0x45u, 0x11u, 0x8bu, 0xfdu, 0x3bu, 0x60u, 0x07u)

HRESULT D2D1CreateFactory(D2D1_FACTORY_TYPE factoryType, REFIID riid, const void* pFactoryOptions, void** ppIFactory);

template <class Factory>
inline HRESULT D2D1CreateFactory(D2D1_FACTORY_TYPE factoryType, Factory** ppFactory) {
    return D2D1CreateFactory(factoryType, __uuidof(Factory), nullptr, reinterpret_cast<void**>(ppFactory));
}
