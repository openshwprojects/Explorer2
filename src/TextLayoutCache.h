#pragma once
// Reuses DirectWrite text layouts from one paint to the next.
//
// ID2D1RenderTarget::DrawText lays its text out from scratch on every call: it looks up the fonts, shapes the
// glyphs and breaks the lines, and then throws the layout away. The main window draws a few hundred strings
// per paint, nearly all of them the same as in the paint before, so this work was most of the cost of a paint.
// The cache keeps the layouts of recently drawn strings and draws them again with DrawTextLayout, which gives
// the same picture. A kept layout also keeps the fonts it uses open, so a symbol that comes from a fallback
// font does not make DirectWrite open that font file again on every paint, which could stall a paint for more
// than a second when the disk was busy.
//
// A layout depends on the text, the size of its box and the settings of the format when it was made. The
// drawing code changes the alignment of shared formats between calls, so those settings are part of the key.
// Long texts (a git log, for example) are not kept: drawn in a box that changes size while a pane is dragged,
// each would leave a large layout behind on every frame. For use on one thread only (the UI thread).
#include <d2d1.h>
#include <dwrite.h>
#include <algorithm>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>

namespace Explorer2 {

class TextLayoutCache {
public:
    explicit TextLayoutCache(size_t capacity = 3000) : m_capacity(capacity) {}
    ~TextLayoutCache() { Clear(); }
    TextLayoutCache(const TextLayoutCache&) = delete;
    TextLayoutCache& operator=(const TextLayoutCache&) = delete;

    static constexpr UINT32 kLongestKept = 512;     // characters

    // Draws like rt->DrawText(text, length, format, rect, brush)
    void Draw(IDWriteFactory* factory, ID2D1RenderTarget* rt, const wchar_t* text, UINT32 length,
              IDWriteTextFormat* format, const D2D1_RECT_F& rect, ID2D1Brush* brush) {
        if (!rt || !format || !brush) return;
        if (!text) {
            text = L"";
            length = 0;
        }
        // A box turned inside out (a pane squeezed to nothing): DrawText draws nothing there either
        if (rect.right < rect.left || rect.bottom < rect.top) return;
        IDWriteTextLayout* layout = (factory && length <= kLongestKept) ? Get(factory, text, length, format, rect) : nullptr;
        if (layout) {
            rt->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout, brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
        } else {
            rt->DrawText(text, length, format, rect, brush);
        }
    }

    // The layout for this text in a box of this size, made with the format as it is set now. Owned by the
    // cache: valid until the next call or Clear().
    IDWriteTextLayout* Get(IDWriteFactory* factory, const wchar_t* text, UINT32 length, IDWriteTextFormat* format,
                           const D2D1_RECT_F& rect) {
        const float width = std::max(0.0f, rect.right - rect.left);
        const float height = std::max(0.0f, rect.bottom - rect.top);
        const KeyHeader header = {
            reinterpret_cast<UINT64>(format), width, height,
            static_cast<UINT32>(format->GetTextAlignment()), static_cast<UINT32>(format->GetParagraphAlignment()),
            static_cast<UINT32>(format->GetWordWrapping()), length };
        m_key.assign(reinterpret_cast<const wchar_t*>(&header), sizeof(KeyHeader) / sizeof(wchar_t));
        m_key.append(text, length);

        auto it = m_index.find(m_key);
        if (it != m_index.end()) {
            m_entries.splice(m_entries.begin(), m_entries, it->second);     // now the most recently used
            ++m_hits;
            return it->second->layout;
        }
        ++m_misses;
        IDWriteTextLayout* layout = nullptr;
        if (FAILED(factory->CreateTextLayout(text, length, format, width, height, &layout)) || !layout) return nullptr;
        m_entries.push_front({ m_key, layout });
        m_index.emplace(m_entries.front().key, m_entries.begin());
        while (m_entries.size() > m_capacity) {
            Entry& oldest = m_entries.back();
            m_index.erase(oldest.key);
            oldest.layout->Release();
            m_entries.pop_back();
        }
        return layout;
    }

    void Clear() {
        for (Entry& e : m_entries) e.layout->Release();
        m_entries.clear();
        m_index.clear();
    }

    size_t Size() const { return m_entries.size(); }
    uint64_t Hits() const { return m_hits; }
    uint64_t Misses() const { return m_misses; }

private:
    struct KeyHeader {
        UINT64 format;
        float width;
        float height;
        UINT32 textAlignment;
        UINT32 paragraphAlignment;
        UINT32 wordWrapping;
        UINT32 length;
    };
    static_assert(sizeof(KeyHeader) % sizeof(wchar_t) == 0, "the key header is stored as wchar_t");

    struct Entry {
        std::wstring key;
        IDWriteTextLayout* layout = nullptr;
    };

    size_t m_capacity;
    std::list<Entry> m_entries;                                            // most recently used first
    std::unordered_map<std::wstring, std::list<Entry>::iterator> m_index;
    std::wstring m_key;                                                    // reused, to save an allocation per call
    uint64_t m_hits = 0;
    uint64_t m_misses = 0;
};

} // namespace Explorer2
