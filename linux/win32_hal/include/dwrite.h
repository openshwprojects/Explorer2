#pragma once
// linux/win32_hal/include/dwrite.h - DirectWrite HAL interface definitions for Linux.

#include <unknwn.h>

#ifndef DWRITE_MEASURING_MODE_DEFINED
#define DWRITE_MEASURING_MODE_DEFINED
enum DWRITE_MEASURING_MODE {
    DWRITE_MEASURING_MODE_NATURAL = 0,
    DWRITE_MEASURING_MODE_GDI_CLASSIC = 1,
    DWRITE_MEASURING_MODE_GDI_NATURAL = 2
};
#endif

enum DWRITE_FACTORY_TYPE {
    DWRITE_FACTORY_TYPE_SHARED = 0,
    DWRITE_FACTORY_TYPE_ISOLATED = 1
};

enum DWRITE_FONT_WEIGHT {
    DWRITE_FONT_WEIGHT_THIN = 100,
    DWRITE_FONT_WEIGHT_LIGHT = 300,
    DWRITE_FONT_WEIGHT_NORMAL = 400,
    DWRITE_FONT_WEIGHT_REGULAR = 400,
    DWRITE_FONT_WEIGHT_MEDIUM = 500,
    DWRITE_FONT_WEIGHT_SEMI_BOLD = 600,
    DWRITE_FONT_WEIGHT_BOLD = 700,
    DWRITE_FONT_WEIGHT_EXTRA_BOLD = 800,
    DWRITE_FONT_WEIGHT_BLACK = 900
};

enum DWRITE_FONT_STYLE {
    DWRITE_FONT_STYLE_NORMAL = 0,
    DWRITE_FONT_STYLE_OBLIQUE = 1,
    DWRITE_FONT_STYLE_ITALIC = 2
};

enum DWRITE_FONT_STRETCH {
    DWRITE_FONT_STRETCH_UNDEFINED = 0,
    DWRITE_FONT_STRETCH_NORMAL = 5
};

enum DWRITE_TEXT_ALIGNMENT {
    DWRITE_TEXT_ALIGNMENT_LEADING = 0,
    DWRITE_TEXT_ALIGNMENT_TRAILING = 1,
    DWRITE_TEXT_ALIGNMENT_CENTER = 2,
    DWRITE_TEXT_ALIGNMENT_JUSTIFIED = 3
};

enum DWRITE_PARAGRAPH_ALIGNMENT {
    DWRITE_PARAGRAPH_ALIGNMENT_NEAR = 0,
    DWRITE_PARAGRAPH_ALIGNMENT_FAR = 1,
    DWRITE_PARAGRAPH_ALIGNMENT_CENTER = 2
};

enum DWRITE_WORD_WRAPPING {
    DWRITE_WORD_WRAPPING_WRAP = 0,
    DWRITE_WORD_WRAPPING_NO_WRAP = 1,
    DWRITE_WORD_WRAPPING_EMERGENCY_BREAK = 2,
    DWRITE_WORD_WRAPPING_WHOLE_WORD = 3,
    DWRITE_WORD_WRAPPING_CHARACTER = 4
};

enum DWRITE_TRIMMING_GRANULARITY {
    DWRITE_TRIMMING_GRANULARITY_NONE = 0,
    DWRITE_TRIMMING_GRANULARITY_CHARACTER = 1,
    DWRITE_TRIMMING_GRANULARITY_WORD = 2
};

struct DWRITE_TRIMMING {
    DWRITE_TRIMMING_GRANULARITY granularity;
    UINT32 delimiter;
    UINT32 delimiterCount;
};

struct DWRITE_TEXT_METRICS {
    FLOAT left;
    FLOAT top;
    FLOAT width;
    FLOAT widthIncludingTrailingWhitespace;
    FLOAT height;
    FLOAT layoutWidth;
    FLOAT layoutHeight;
    UINT32 maxBidiReorderingDepth;
    UINT32 lineCount;
};

struct IDWriteFontCollection : public IUnknown {};
struct IDWriteInlineObject : public IUnknown {};

struct IDWriteTextFormat : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetTextAlignment(DWRITE_TEXT_ALIGNMENT textAlignment) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetWordWrapping(DWRITE_WORD_WRAPPING wordWrapping) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTrimming(const DWRITE_TRIMMING* trimmingOptions, IDWriteInlineObject* trimmingSign) = 0;
    virtual DWRITE_TEXT_ALIGNMENT STDMETHODCALLTYPE GetTextAlignment() = 0;
    virtual DWRITE_PARAGRAPH_ALIGNMENT STDMETHODCALLTYPE GetParagraphAlignment() = 0;
    virtual DWRITE_WORD_WRAPPING STDMETHODCALLTYPE GetWordWrapping() = 0;
    virtual FLOAT STDMETHODCALLTYPE GetFontSize() = 0;
    virtual const WCHAR* STDMETHODCALLTYPE GetFontFamilyNamePtr() = 0;
    virtual DWRITE_FONT_WEIGHT STDMETHODCALLTYPE GetFontWeight() = 0;
    virtual DWRITE_FONT_STYLE STDMETHODCALLTYPE GetFontStyle() = 0;
};

struct IDWriteTextLayout : public IDWriteTextFormat {
    virtual HRESULT STDMETHODCALLTYPE GetMetrics(DWRITE_TEXT_METRICS* textMetrics) = 0;
    virtual const WCHAR* STDMETHODCALLTYPE GetTextPtr() = 0;
    virtual UINT32 STDMETHODCALLTYPE GetTextLength() = 0;
    virtual FLOAT STDMETHODCALLTYPE GetMaxWidth() = 0;
    virtual FLOAT STDMETHODCALLTYPE GetMaxHeight() = 0;
};

struct IDWriteFactory : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateTextFormat(
        const WCHAR* fontFamilyName,
        IDWriteFontCollection* fontCollection,
        DWRITE_FONT_WEIGHT fontWeight,
        DWRITE_FONT_STYLE fontStyle,
        DWRITE_FONT_STRETCH fontStretch,
        FLOAT fontSize,
        const WCHAR* localeName,
        IDWriteTextFormat** textFormat) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateEllipsisTrimmingSign(
        IDWriteTextFormat* textFormat,
        IDWriteInlineObject** trimmingSign) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateTextLayout(
        const WCHAR* string,
        UINT32 stringLength,
        IDWriteTextFormat* textFormat,
        FLOAT maxWidth,
        FLOAT maxHeight,
        IDWriteTextLayout** textLayout) = 0;
};

E2_DECLARE_UUID(IDWriteInlineObject, 0x8339fde3u, 0x106fu, 0x47abu, 0x83u, 0x73u, 0x1cu, 0x62u, 0x95u, 0xebu, 0x10u, 0xb3u)
E2_DECLARE_UUID(IDWriteTextFormat,   0x9c906818u, 0x31d7u, 0x4fd3u, 0xa1u, 0x51u, 0x7cu, 0x5eu, 0x22u, 0x5du, 0xb5u, 0x5au)
E2_DECLARE_UUID(IDWriteTextLayout,   0x53737037u, 0x6d14u, 0x410bu, 0x9bu, 0xfeu, 0x0bu, 0x18u, 0x2bu, 0xb7u, 0x09u, 0x61u)
E2_DECLARE_UUID(IDWriteFactory,      0xb859ee5au, 0xd838u, 0x4b5bu, 0xa2u, 0xe8u, 0x1au, 0xdcu, 0x7du, 0x93u, 0xdbu, 0x48u)

HRESULT DWriteCreateFactory(DWRITE_FACTORY_TYPE factoryType, REFIID iid, IUnknown** factory);
