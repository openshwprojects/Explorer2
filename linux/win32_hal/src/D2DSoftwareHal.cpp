// linux/win32_hal/src/D2DSoftwareHal.cpp
// Pure-C++ Anti-Aliased Software Direct2D + DirectWrite + TrueType Font Rasterizer for Linux (32-bit & 64-bit).

#include <windows.h>
#include <d2d1.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include "PathUtils.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace Explorer2::Hal {
bool BlitBgraToDcDib(HDC hdc, const RECT& subRect, const uint8_t* srcBgra, int srcW, int srcH);
__attribute__((weak)) void PresentHwndFramebuffer(HWND, const uint8_t*, uint32_t, uint32_t) {}
} // namespace Explorer2::Hal

namespace {

// ==============================================================================================================
// 1. Minimal Pure-C++ TrueType (.ttf) Outline Parser & 4x4 Supersampled Glyph Cache
// ==============================================================================================================
inline uint16_t RdBE16(const uint8_t* p) { return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]); }
inline int16_t  RdBES16(const uint8_t* p) { return static_cast<int16_t>(RdBE16(p)); }
inline uint32_t RdBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  | static_cast<uint32_t>(p[3]);
}

struct RasterGlyph {
    int width = 0;
    int height = 0;
    int bearingX = 0;
    int bearingY = 0; // offset from baseline upwards (top = baselineY - bearingY)
    float advancePx = 0.0f;
    std::vector<uint8_t> alpha; // width * height coverage [0..255]
};

class TrueTypeFace {
public:
    bool LoadFromFile(const char* path) {
        FILE* f = std::fopen(path, "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        const long sz = std::ftell(f);
        if (sz <= 64 || sz > 32 * 1024 * 1024) { std::fclose(f); return false; }
        std::fseek(f, 0, SEEK_SET);
        m_data.resize(static_cast<size_t>(sz));
        if (std::fread(m_data.data(), 1, m_data.size(), f) != m_data.size()) {
            std::fclose(f);
            m_data.clear();
            return false;
        }
        std::fclose(f);
        return ParseTables();
    }

    bool IsLoaded() const { return m_valid; }

    const RasterGlyph& GetGlyph(uint32_t cp, int pxHeight) {
        pxHeight = std::clamp(pxHeight, 6, 128);
        const uint64_t key = (static_cast<uint64_t>(pxHeight) << 32) | cp;
        {
            std::lock_guard<std::mutex> lk(m_cacheMutex);
            auto it = m_cache.find(key);
            if (it != m_cache.end()) return it->second;
        }
        RasterGlyph rg = RasterizeCodepoint(cp, pxHeight);
        std::lock_guard<std::mutex> lk(m_cacheMutex);
        if (m_cache.size() > 8192) m_cache.clear();
        auto [insIt, _] = m_cache.emplace(key, std::move(rg));
        return insIt->second;
    }

    float GetAscentPx(float fontSizePx) const {
        if (!m_valid || m_unitsPerEm == 0) return fontSizePx * 0.8f;
        return static_cast<float>(m_ascent) * fontSizePx / static_cast<float>(m_unitsPerEm);
    }
    float GetLineHeightPx(float fontSizePx) const {
        if (!m_valid || m_unitsPerEm == 0) return fontSizePx * 1.25f;
        const float h = static_cast<float>(m_ascent - m_descent + m_lineGap) * fontSizePx / static_cast<float>(m_unitsPerEm);
        return std::max(fontSizePx * 1.15f, h);
    }

private:
    struct Seg2D { float x0, y0, x1, y1; };

    bool ParseTables() {
        if (m_data.size() < 12) return false;
        const uint16_t numTables = RdBE16(m_data.data() + 4);
        if (12u + static_cast<size_t>(numTables) * 16u > m_data.size()) return false;

        auto findTbl = [&](const char tag[4], uint32_t& off, uint32_t& len) -> bool {
            for (uint16_t i = 0; i < numTables; ++i) {
                const uint8_t* rec = m_data.data() + 12 + static_cast<size_t>(i) * 16;
                if ( std::memcmp(rec, tag, 4) == 0 ) {
                    off = RdBE32(rec + 8);
                    len = RdBE32(rec + 12);
                    return static_cast<uint64_t>(off) + len <= m_data.size();
                }
            }
            return false;
        };

        uint32_t headOff=0, headLen=0, hheaOff=0, hheaLen=0, maxpOff=0, maxpLen=0;
        if (!findTbl("head", headOff, headLen) || headLen < 54) return false;
        if (!findTbl("hhea", hheaOff, hheaLen) || hheaLen < 36) return false;
        if (!findTbl("maxp", maxpOff, maxpLen) || maxpLen < 6) return false;
        if (!findTbl("hmtx", m_hmtxOff, m_hmtxLen)) return false;
        if (!findTbl("loca", m_locaOff, m_locaLen)) return false;
        if (!findTbl("glyf", m_glyfOff, m_glyfLen)) return false;
        if (!findTbl("cmap", m_cmapOff, m_cmapLen)) return false;

        m_unitsPerEm   = RdBE16(m_data.data() + headOff + 18);
        m_locaFormat   = RdBES16(m_data.data() + headOff + 50);
        m_ascent       = RdBES16(m_data.data() + hheaOff + 4);
        m_descent      = RdBES16(m_data.data() + hheaOff + 6);
        m_lineGap      = RdBES16(m_data.data() + hheaOff + 8);
        m_numHMetrics  = RdBE16(m_data.data() + hheaOff + 34);
        m_numGlyphs    = RdBE16(m_data.data() + maxpOff + 4);
        if (m_unitsPerEm == 0 || m_numGlyphs == 0) return false;

        // Locate best Unicode cmap subtable (prefer Format 12 UCS-4, fallback Format 4 BMP)
        const uint8_t* cm = m_data.data() + m_cmapOff;
        if (m_cmapLen >= 4) {
            const uint16_t nSub = RdBE16(cm + 2);
            for (uint16_t i = 0; i < nSub && 4u + (i + 1u) * 8u <= m_cmapLen; ++i) {
                const uint16_t plat = RdBE16(cm + 4 + i * 8);
                const uint16_t enc  = RdBE16(cm + 4 + i * 8 + 2);
                const uint32_t sub  = RdBE32(cm + 4 + i * 8 + 4);
                if (sub + 6 > m_cmapLen) continue;
                if (plat == 0 || (plat == 3 && (enc == 1 || enc == 10))) {
                    const uint16_t fmt = RdBE16(cm + sub);
                    if (fmt == 12) { m_cmapSubOff = m_cmapOff + sub; m_cmapFmt = 12; break; }
                    if (fmt == 4 && m_cmapFmt == 0) { m_cmapSubOff = m_cmapOff + sub; m_cmapFmt = 4; }
                }
            }
        }
        m_valid = (m_cmapFmt != 0);
        return m_valid;
    }

    uint16_t MapCodepointToGlyph(uint32_t cp) const {
        if (!m_valid) return 0;
        const uint8_t* sub = m_data.data() + m_cmapSubOff;
        const size_t avail = m_data.size() - m_cmapSubOff;
        if (m_cmapFmt == 12 && avail >= 16) {
            const uint32_t nGroups = RdBE32(sub + 12);
            size_t lo = 0, hi = std::min<size_t>(nGroups, (avail - 16) / 12);
            while (lo < hi) {
                const size_t mid = (lo + hi) / 2;
                const uint8_t* g = sub + 16 + mid * 12;
                const uint32_t startCp = RdBE32(g);
                const uint32_t endCp   = RdBE32(g + 4);
                if (cp < startCp) hi = mid;
                else if (cp > endCp) lo = mid + 1;
                else return static_cast<uint16_t>(RdBE32(g + 8) + (cp - startCp));
            }
        } else if (m_cmapFmt == 4 && cp <= 0xFFFFu && avail >= 14) {
            const uint16_t segCountX2 = RdBE16(sub + 6);
            const uint16_t segCount = segCountX2 / 2;
            const size_t endCodeOff = 14;
            const size_t startCodeOff = endCodeOff + segCountX2 + 2;
            const size_t idDeltaOff = startCodeOff + segCountX2;
            const size_t idRangeOff = idDeltaOff + segCountX2;
            if (idRangeOff + segCountX2 > avail) return 0;
            for (uint16_t s = 0; s < segCount; ++s) {
                const uint16_t endC = RdBE16(sub + endCodeOff + s * 2);
                if (cp > endC) continue;
                const uint16_t startC = RdBE16(sub + startCodeOff + s * 2);
                if (cp < startC) return 0;
                const int16_t idDelta = RdBES16(sub + idDeltaOff + s * 2);
                const uint16_t idRange = RdBE16(sub + idRangeOff + s * 2);
                if (idRange == 0) {
                    return static_cast<uint16_t>((static_cast<int32_t>(cp) + idDelta) & 0xFFFF);
                }
                const size_t glyphIdxOff = idRangeOff + s * 2 + idRange + (cp - startC) * 2;
                if (glyphIdxOff + 2 > avail) return 0;
                const uint16_t g = RdBE16(sub + glyphIdxOff);
                return g ? static_cast<uint16_t>((static_cast<int32_t>(g) + idDelta) & 0xFFFF) : 0;
            }
        }
        return 0;
    }

    bool GetGlyphOffsetAndLength(uint16_t gid, uint32_t& outOff, uint32_t& outLen) const {
        if (gid >= m_numGlyphs) return false;
        uint32_t g0 = 0, g1 = 0;
        if (m_locaFormat == 0) {
            if (static_cast<size_t>(gid + 1) * 2u + 2u > m_locaLen) return false;
            g0 = static_cast<uint32_t>(RdBE16(m_data.data() + m_locaOff + gid * 2)) * 2u;
            g1 = static_cast<uint32_t>(RdBE16(m_data.data() + m_locaOff + (gid + 1) * 2)) * 2u;
        } else {
            if (static_cast<size_t>(gid + 1) * 4u + 4u > m_locaLen) return false;
            g0 = RdBE32(m_data.data() + m_locaOff + gid * 4);
            g1 = RdBE32(m_data.data() + m_locaOff + (gid + 1) * 4);
        }
        if (g1 <= g0 || g1 > m_glyfLen) return false;
        outOff = m_glyfOff + g0;
        outLen = g1 - g0;
        return true;
    }

    void AppendGlyphSegments(uint16_t gid, float m00, float m01, float m10, float m11, float dx, float dy,
                             std::vector<Seg2D>& outSegs, int depth = 0) const
    {
        if (depth > 6) return;
        uint32_t goff = 0, glen = 0;
        if (!GetGlyphOffsetAndLength(gid, goff, glen) || glen < 10) return;
        const uint8_t* gp = m_data.data() + goff;
        const uint8_t* gend = gp + glen;
        const int16_t nContours = RdBES16(gp);

        if (nContours > 0) {
            if (10 + static_cast<size_t>(nContours) * 2 + 2 > glen) return;
            const uint16_t lastPt = RdBE16(gp + 10 + (nContours - 1) * 2);
            const uint32_t nPts = static_cast<uint32_t>(lastPt) + 1u;
            if (nPts > 4096) return;
            const uint8_t* p = gp + 10 + nContours * 2;
            const uint16_t insLen = RdBE16(p);
            p += 2 + insLen;
            if (p >= gend) return;

            std::vector<uint8_t> flags(nPts);
            for (uint32_t i = 0; i < nPts; ) {
                if (p >= gend) return;
                const uint8_t fl = *p++;
                flags[i++] = fl;
                if (fl & 0x08u) {
                    if (p >= gend) return;
                    const uint8_t rep = *p++;
                    for (uint8_t r = 0; r < rep && i < nPts; ++r) flags[i++] = fl;
                }
            }
            std::vector<float> xs(nPts), ys(nPts);
            int32_t curX = 0;
            for (uint32_t i = 0; i < nPts; ++i) {
                const uint8_t fl = flags[i];
                if (fl & 0x02u) {
                    if (p >= gend) return;
                    const int d = *p++;
                    curX += (fl & 0x10u) ? d : -d;
                } else if (!(fl & 0x10u)) {
                    if (p + 2 > gend) return;
                    curX += RdBES16(p);
                    p += 2;
                }
                xs[i] = static_cast<float>(curX);
            }
            int32_t curY = 0;
            for (uint32_t i = 0; i < nPts; ++i) {
                const uint8_t fl = flags[i];
                if (fl & 0x04u) {
                    if (p >= gend) return;
                    const int d = *p++;
                    curY += (fl & 0x20u) ? d : -d;
                } else if (!(fl & 0x20u)) {
                    if (p + 2 > gend) return;
                    curY += RdBES16(p);
                    p += 2;
                }
                ys[i] = static_cast<float>(curY);
            }

            auto xform = [&](float x, float y) -> std::pair<float, float> {
                // Flip font Y upwards -> screen Y downwards
                const float tx = m00 * x + m01 * y + dx;
                const float ty = -(m10 * x + m11 * y + dy);
                return { tx, ty };
            };
            auto addQuad = [&](std::pair<float,float> a, std::pair<float,float> ctrl, std::pair<float,float> b) {
                constexpr int kSteps = 6;
                std::pair<float,float> prev = a;
                for (int s = 1; s <= kSteps; ++s) {
                    const float t = static_cast<float>(s) / kSteps;
                    const float u = 1.0f - t;
                    const float nx = u * u * a.first + 2.0f * u * t * ctrl.first + t * t * b.first;
                    const float ny = u * u * a.second + 2.0f * u * t * ctrl.second + t * t * b.second;
                    outSegs.push_back({ prev.first, prev.second, nx, ny });
                    prev = { nx, ny };
                }
            };

            uint32_t startIdx = 0;
            for (int c = 0; c < nContours; ++c) {
                const uint32_t endIdx = RdBE16(gp + 10 + c * 2);
                if (endIdx < startIdx || endIdx >= nPts) break;
                const uint32_t cnt = endIdx - startIdx + 1u;

                struct CtlPt { std::pair<float,float> pt; bool on; };
                std::vector<CtlPt> ring;
                ring.reserve(cnt * 2);
                for (uint32_t k = 0; k < cnt; ++k) {
                    const uint32_t idx = startIdx + k;
                    const uint32_t nextIdx = startIdx + ((k + 1u) % cnt);
                    const bool on = (flags[idx] & 0x01u) != 0;
                    const bool nextOn = (flags[nextIdx] & 0x01u) != 0;
                    const auto pCur = xform(xs[idx], ys[idx]);
                    ring.push_back({ pCur, on });
                    if (!on && !nextOn) {
                        const auto pNext = xform(xs[nextIdx], ys[nextIdx]);
                        ring.push_back({ { 0.5f * (pCur.first + pNext.first), 0.5f * (pCur.second + pNext.second) }, true });
                    }
                }
                if (!ring.empty()) {
                    size_t firstOn = 0;
                    while (firstOn < ring.size() && !ring[firstOn].on) ++firstOn;
                    if (firstOn < ring.size()) {
                        const size_t m = ring.size();
                        for (size_t k = 0; k < m; ) {
                            const auto& cur = ring[(firstOn + k) % m];
                            const auto& nxt = ring[(firstOn + k + 1) % m];
                            if (nxt.on) {
                                outSegs.push_back({ cur.pt.first, cur.pt.second, nxt.pt.first, nxt.pt.second });
                                ++k;
                            } else {
                                const auto& nn = ring[(firstOn + k + 2) % m];
                                addQuad(cur.pt, nxt.pt, nn.pt);
                                k += 2;
                            }
                        }
                    }
                }
                startIdx = endIdx + 1u;
            }
        } else if (nContours < 0) {
            // Compound glyph (accented characters, symbols)
            const uint8_t* p = gp + 10;
            while (p + 4 <= gend) {
                const uint16_t compFlags = RdBE16(p);
                const uint16_t subGid    = RdBE16(p + 2);
                p += 4;
                float cdx = 0.0f, cdy = 0.0f;
                if (compFlags & 0x0001u) {
                    if (p + 4 > gend) break;
                    if (compFlags & 0x0002u) { cdx = RdBES16(p); cdy = RdBES16(p + 2); }
                    p += 4;
                } else {
                    if (p + 2 > gend) break;
                    if (compFlags & 0x0002u) { cdx = static_cast<int8_t>(p[0]); cdy = static_cast<int8_t>(p[1]); }
                    p += 2;
                }
                float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f;
                if (compFlags & 0x0008u) {
                    if (p + 2 > gend) break;
                    a = d = RdBES16(p) / 16384.0f; p += 2;
                } else if (compFlags & 0x0040u) {
                    if (p + 4 > gend) break;
                    a = RdBES16(p) / 16384.0f; d = RdBES16(p + 2) / 16384.0f; p += 4;
                } else if (compFlags & 0x0080u) {
                    if (p + 8 > gend) break;
                    a = RdBES16(p) / 16384.0f;     b = RdBES16(p + 2) / 16384.0f;
                    c = RdBES16(p + 4) / 16384.0f; d = RdBES16(p + 6) / 16384.0f;
                    p += 8;
                }
                AppendGlyphSegments(
                    subGid,
                    m00 * a + m01 * c, m00 * b + m01 * d,
                    m10 * a + m11 * c, m10 * b + m11 * d,
                    m00 * cdx + m01 * cdy + dx,
                    m10 * cdx + m11 * cdy + dy,
                    outSegs, depth + 1);
                if (!(compFlags & 0x0020u)) break; // MORE_COMPONENTS
            }
        }
    }

    RasterGlyph RasterizeCodepoint(uint32_t cp, int pxHeight) const {
        RasterGlyph out;
        if (cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n') {
            const float scale = m_valid ? (static_cast<float>(pxHeight) / static_cast<float>(m_unitsPerEm)) : 0.5f;
            uint16_t spGid = MapCodepointToGlyph(' ');
            if (m_valid && spGid > 0 && m_numHMetrics > 0) {
                const uint16_t mIdx = std::min<uint16_t>(spGid, m_numHMetrics - 1);
                if (static_cast<size_t>(mIdx) * 4u + 2u <= m_hmtxLen) {
                    out.advancePx = RdBE16(m_data.data() + m_hmtxOff + mIdx * 4) * scale;
                }
            }
            if (out.advancePx <= 0.5f) out.advancePx = pxHeight * 0.32f;
            if (cp == '\t') out.advancePx *= 4.0f;
            return out;
        }

        const uint16_t gid = MapCodepointToGlyph(cp);
        if (m_valid && gid != 0) {
            const float scale = static_cast<float>(pxHeight) / static_cast<float>(m_unitsPerEm);
            const uint16_t mIdx = std::min<uint16_t>(gid, static_cast<uint16_t>(std::max<int>(0, m_numHMetrics - 1)));
            if (static_cast<size_t>(mIdx) * 4u + 2u <= m_hmtxLen) {
                out.advancePx = RdBE16(m_data.data() + m_hmtxOff + mIdx * 4) * scale;
            }
            std::vector<Seg2D> segs;
            AppendGlyphSegments(gid, scale, 0.0f, 0.0f, scale, 0.0f, 0.0f, segs);
            if (!segs.empty()) {
                float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
                for (const auto& s : segs) {
                    minX = std::min({ minX, s.x0, s.x1 });
                    maxX = std::max({ maxX, s.x0, s.x1 });
                    minY = std::min({ minY, s.y0, s.y1 });
                    maxY = std::max({ maxY, s.y0, s.y1 });
                }
                const int ix0 = static_cast<int>(std::floor(minX));
                const int iy0 = static_cast<int>(std::floor(minY));
                const int ix1 = static_cast<int>(std::ceil(maxX));
                const int iy1 = static_cast<int>(std::ceil(maxY));
                const int gw = std::clamp(ix1 - ix0, 1, 256);
                const int gh = std::clamp(iy1 - iy0, 1, 256);
                out.width = gw;
                out.height = gh;
                out.bearingX = ix0;
                out.bearingY = -iy0;
                out.alpha.assign(static_cast<size_t>(gw) * gh, 0);

                // 4x4 supersampled non-zero winding scanline rasterizer
                std::vector<float> xHits;
                xHits.reserve(32);
                for (int py = 0; py < gh; ++py) {
                    std::vector<uint16_t> accum(gw, 0);
                    for (int sy = 0; sy < 4; ++sy) {
                        const float y = static_cast<float>(iy0 + py) + (sy + 0.5f) * 0.25f;
                        xHits.clear();
                        for (const auto& s : segs) {
                            if ((s.y0 <= y && s.y1 > y) || (s.y1 <= y && s.y0 > y)) {
                                const float t = (y - s.y0) / (s.y1 - s.y0);
                                xHits.push_back(s.x0 + t * (s.x1 - s.x0));
                            }
                        }
                        if (xHits.size() < 2) continue;
                        std::sort(xHits.begin(), xHits.end());
                        for (size_t k = 0; k + 1 < xHits.size(); k += 2) {
                            const float xa = xHits[k] - ix0;
                            const float xb = xHits[k + 1] - ix0;
                            const int pxStart = std::max(0, static_cast<int>(std::floor(xa)));
                            const int pxEnd   = std::min(gw - 1, static_cast<int>(std::ceil(xb)));
                            for (int px = pxStart; px <= pxEnd; ++px) {
                                for (int sx = 0; sx < 4; ++sx) {
                                    const float sampleX = px + (sx + 0.5f) * 0.25f;
                                    if (sampleX >= xa && sampleX < xb) ++accum[px];
                                }
                            }
                        }
                    }
                    for (int px = 0; px < gw; ++px) {
                        out.alpha[static_cast<size_t>(py) * gw + px] =
                            static_cast<uint8_t>(std::min<uint32_t>(255u, (static_cast<uint32_t>(accum[px]) * 255u + 8u) / 16u));
                    }
                }
                if (out.advancePx < 1.0f) out.advancePx = static_cast<float>(gw + 1);
                return out;
            }
        }

        // Crisp geometric fallback glyph for missing Unicode symbols (e.g. U+263E moon, arrows, emojis)
        const int gw = std::max(4, (pxHeight * 3) / 5);
        const int gh = std::max(6, (pxHeight * 3) / 4);
        out.width = gw;
        out.height = gh;
        out.bearingX = 1;
        out.bearingY = gh - 1;
        out.advancePx = static_cast<float>(gw + 2);
        out.alpha.assign(static_cast<size_t>(gw) * gh, 0);
        auto setA = [&](int x, int y, uint8_t a) {
            if (x >= 0 && x < gw && y >= 0 && y < gh) out.alpha[static_cast<size_t>(y) * gw + x] = a;
        };
        if (cp == 0x25BEu || cp == 0x25BCu) { // Down triangle ▾
            for (int y = gh / 3; y < (gh * 3) / 4; ++y) {
                int half = std::max(0, ((gh * 3) / 4 - 1 - y));
                for (int x = gw / 2 - half; x <= gw / 2 + half; ++x) setA(x, y, 240);
            }
        } else if (cp == 0x203Au || cp == 0x25B6u || cp == '>') { // Right chevron › / ▶
            for (int y = 1; y < gh - 1; ++y) {
                int dx = (gh / 2) - std::abs(y - gh / 2);
                setA(1 + dx / 2, y, 240); setA(2 + dx / 2, y, 220);
            }
        } else {
            for (int x = 0; x < gw; ++x) { setA(x, 0, 210); setA(x, gh - 1, 210); }
            for (int y = 0; y < gh; ++y) { setA(0, y, 210); setA(gw - 1, y, 210); }
            for (int y = 2; y < gh - 2; ++y)
                for (int x = 2; x < gw - 2; ++x)
                    if (((cp >> ((y * 3 + x) & 7)) & 1u) != 0) setA(x, y, 220);
        }
        return out;
    }

    std::vector<uint8_t> m_data;
    bool m_valid = false;
    uint16_t m_unitsPerEm = 2048;
    int16_t  m_locaFormat = 0;
    int16_t  m_ascent = 1638;
    int16_t  m_descent = -410;
    int16_t  m_lineGap = 0;
    uint16_t m_numHMetrics = 0;
    uint16_t m_numGlyphs = 0;
    uint32_t m_hmtxOff = 0, m_hmtxLen = 0;
    uint32_t m_locaOff = 0, m_locaLen = 0;
    uint32_t m_glyfOff = 0, m_glyfLen = 0;
    uint32_t m_cmapOff = 0, m_cmapLen = 0;
    uint32_t m_cmapSubOff = 0;
    int      m_cmapFmt = 0;

    mutable std::mutex m_cacheMutex;
    mutable std::unordered_map<uint64_t, RasterGlyph> m_cache;
};

struct SystemFontBank {
    TrueTypeFace sansReg;
    TrueTypeFace sansBold;
    TrueTypeFace monoReg;

    SystemFontBank() {
        static const char* kSansReg[] = {
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/ttf-bitstream-vera/Vera.ttf"
        };
        static const char* kSansBold[] = {
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
            "/usr/share/fonts/truetype/ttf-bitstream-vera/VeraBd.ttf"
        };
        static const char* kMonoReg[] = {
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
            "/usr/share/fonts/truetype/ttf-bitstream-vera/VeraMono.ttf"
        };
        for (const char* p : kSansReg)  if (sansReg.LoadFromFile(p)) break;
        for (const char* p : kSansBold) if (sansBold.LoadFromFile(p)) break;
        for (const char* p : kMonoReg)  if (monoReg.LoadFromFile(p)) break;
    }

    TrueTypeFace& Select(const std::wstring& family, DWRITE_FONT_WEIGHT weight) {
        std::wstring low = Explorer2::PathUtils::ToLower(family);
        if (low.find(L"consol") != std::wstring::npos || low.find(L"mono") != std::wstring::npos ||
            low.find(L"courier") != std::wstring::npos || low.find(L"cascadia") != std::wstring::npos) {
            if (monoReg.IsLoaded()) return monoReg;
        }
        if (weight >= DWRITE_FONT_WEIGHT_SEMI_BOLD && sansBold.IsLoaded()) return sansBold;
        return sansReg;
    }
};

SystemFontBank& Fonts() {
    static SystemFontBank s_bank;
    return s_bank;
}

// Decode UTF-32 (or UTF-16 surrogate pairs) from WCHAR buffer
std::vector<uint32_t> DecodeCodepoints(const WCHAR* str, UINT32 len) {
    std::vector<uint32_t> out;
    if (!str || len == 0) return out;
    out.reserve(len);
    for (UINT32 i = 0; i < len; ++i) {
        const uint32_t c = static_cast<uint32_t>(str[i]);
        if (c >= 0xD800u && c <= 0xDBFFu && i + 1 < len) {
            const uint32_t lo = static_cast<uint32_t>(str[i + 1]);
            if (lo >= 0xDC00u && lo <= 0xDFFFu) {
                out.push_back(0x10000u + (((c - 0xD800u) << 10) | (lo - 0xDC00u)));
                ++i;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

// ==============================================================================================================
// 2. DirectWrite Implementation (SwTextFormat, SwTextLayout, SwDWriteFactory)
// ==============================================================================================================
class SwInlineObject final : public IDWriteInlineObject {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteInlineObject)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }
private:
    std::atomic<ULONG> m_ref{ 1 };
};

class SwTextFormat : public IDWriteTextFormat {
public:
    SwTextFormat(const WCHAR* family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style, FLOAT size)
        : m_family(family ? family : L"Segoe UI")
        , m_weight(weight)
        , m_style(style)
        , m_size(size > 1.0f ? size : 12.0f) {}
    virtual ~SwTextFormat() = default;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteTextFormat)) {
            *ppv = static_cast<IDWriteTextFormat*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE SetTextAlignment(DWRITE_TEXT_ALIGNMENT a) override { m_textAlign = a; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT a) override { m_paraAlign = a; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetWordWrapping(DWRITE_WORD_WRAPPING w) override { m_wrap = w; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetTrimming(const DWRITE_TRIMMING* t, IDWriteInlineObject* sign) override {
        if (t) m_trim = *t;
        m_hasEllipsis = (sign != nullptr) || (m_trim.granularity != DWRITE_TRIMMING_GRANULARITY_NONE);
        return S_OK;
    }
    DWRITE_TEXT_ALIGNMENT STDMETHODCALLTYPE GetTextAlignment() override { return m_textAlign; }
    DWRITE_PARAGRAPH_ALIGNMENT STDMETHODCALLTYPE GetParagraphAlignment() override { return m_paraAlign; }
    DWRITE_WORD_WRAPPING STDMETHODCALLTYPE GetWordWrapping() override { return m_wrap; }
    FLOAT STDMETHODCALLTYPE GetFontSize() override { return m_size; }
    const WCHAR* STDMETHODCALLTYPE GetFontFamilyNamePtr() override { return m_family.c_str(); }
    DWRITE_FONT_WEIGHT STDMETHODCALLTYPE GetFontWeight() override { return m_weight; }
    DWRITE_FONT_STYLE STDMETHODCALLTYPE GetFontStyle() override { return m_style; }

    const DWRITE_TRIMMING& GetTrimmingStruct() const { return m_trim; }
    bool HasEllipsis() const { return m_hasEllipsis; }

protected:
    std::atomic<ULONG> m_ref{ 1 };
    std::wstring m_family;
    DWRITE_FONT_WEIGHT m_weight = DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STYLE m_style = DWRITE_FONT_STYLE_NORMAL;
    FLOAT m_size = 12.0f;
    DWRITE_TEXT_ALIGNMENT m_textAlign = DWRITE_TEXT_ALIGNMENT_LEADING;
    DWRITE_PARAGRAPH_ALIGNMENT m_paraAlign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    DWRITE_WORD_WRAPPING m_wrap = DWRITE_WORD_WRAPPING_WRAP;
    DWRITE_TRIMMING m_trim = { DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0 };
    bool m_hasEllipsis = false;
};

struct LaidOutLine {
    std::vector<uint32_t> cps;
    float widthPx = 0.0f;
};

class SwTextLayout final : public IDWriteTextLayout {
public:
    SwTextLayout(const WCHAR* str, UINT32 len, IDWriteTextFormat* fmt, FLOAT maxW, FLOAT maxH)
        : m_text(str ? std::wstring(str, len) : std::wstring())
        , m_maxW(std::max(0.0f, maxW))
        , m_maxH(std::max(0.0f, maxH))
    {
        if (fmt) {
            m_family    = fmt->GetFontFamilyNamePtr() ? fmt->GetFontFamilyNamePtr() : L"Segoe UI";
            m_weight    = fmt->GetFontWeight();
            m_style     = fmt->GetFontStyle();
            m_size      = fmt->GetFontSize();
            m_textAlign = fmt->GetTextAlignment();
            m_paraAlign = fmt->GetParagraphAlignment();
            m_wrap      = fmt->GetWordWrapping();
            auto* swFmt = dynamic_cast<SwTextFormat*>(fmt);
            if (swFmt) {
                m_trim = swFmt->GetTrimmingStruct();
                m_hasEllipsis = swFmt->HasEllipsis();
            }
        }
        BuildLines();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteTextFormat) || riid == __uuidof(IDWriteTextLayout)) {
            *ppv = static_cast<IDWriteTextLayout*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE SetTextAlignment(DWRITE_TEXT_ALIGNMENT a) override { m_textAlign = a; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT a) override { m_paraAlign = a; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetWordWrapping(DWRITE_WORD_WRAPPING w) override { m_wrap = w; BuildLines(); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetTrimming(const DWRITE_TRIMMING* t, IDWriteInlineObject* sign) override {
        if (t) m_trim = *t;
        m_hasEllipsis = (sign != nullptr) || (m_trim.granularity != DWRITE_TRIMMING_GRANULARITY_NONE);
        BuildLines();
        return S_OK;
    }
    DWRITE_TEXT_ALIGNMENT STDMETHODCALLTYPE GetTextAlignment() override { return m_textAlign; }
    DWRITE_PARAGRAPH_ALIGNMENT STDMETHODCALLTYPE GetParagraphAlignment() override { return m_paraAlign; }
    DWRITE_WORD_WRAPPING STDMETHODCALLTYPE GetWordWrapping() override { return m_wrap; }
    FLOAT STDMETHODCALLTYPE GetFontSize() override { return m_size; }
    const WCHAR* STDMETHODCALLTYPE GetFontFamilyNamePtr() override { return m_family.c_str(); }
    DWRITE_FONT_WEIGHT STDMETHODCALLTYPE GetFontWeight() override { return m_weight; }
    DWRITE_FONT_STYLE STDMETHODCALLTYPE GetFontStyle() override { return m_style; }

    HRESULT STDMETHODCALLTYPE GetMetrics(DWRITE_TEXT_METRICS* tm) override {
        if (!tm) return E_POINTER;
        std::memset(tm, 0, sizeof(*tm));
        float maxLineW = 0.0f;
        for (const auto& l : m_lines) maxLineW = std::max(maxLineW, l.widthPx);
        const float totalH = static_cast<float>(std::max<size_t>(1, m_lines.size())) * m_lineHeightPx;
        tm->width = maxLineW;
        tm->widthIncludingTrailingWhitespace = maxLineW;
        tm->height = totalH;
        tm->layoutWidth = m_maxW;
        tm->layoutHeight = m_maxH;
        tm->lineCount = static_cast<UINT32>(std::max<size_t>(1, m_lines.size()));
        if (m_textAlign == DWRITE_TEXT_ALIGNMENT_CENTER) tm->left = 0.5f * (m_maxW - maxLineW);
        else if (m_textAlign == DWRITE_TEXT_ALIGNMENT_TRAILING) tm->left = m_maxW - maxLineW;
        if (m_paraAlign == DWRITE_PARAGRAPH_ALIGNMENT_CENTER) tm->top = 0.5f * (m_maxH - totalH);
        else if (m_paraAlign == DWRITE_PARAGRAPH_ALIGNMENT_FAR) tm->top = m_maxH - totalH;
        return S_OK;
    }

    const WCHAR* STDMETHODCALLTYPE GetTextPtr() override { return m_text.c_str(); }
    UINT32 STDMETHODCALLTYPE GetTextLength() override { return static_cast<UINT32>(m_text.size()); }
    FLOAT STDMETHODCALLTYPE GetMaxWidth() override { return m_maxW; }
    FLOAT STDMETHODCALLTYPE GetMaxHeight() override { return m_maxH; }

    const std::vector<LaidOutLine>& Lines() const { return m_lines; }
    float LineHeightPx() const { return m_lineHeightPx; }
    float AscentPx() const { return m_ascentPx; }
    int PixelFontHeight() const { return m_pxHeight; }

private:
    void BuildLines() {
        m_lines.clear();
        m_pxHeight = std::clamp(static_cast<int>(std::lround(m_size)), 6, 128);
        TrueTypeFace& face = Fonts().Select(m_family, m_weight);
        m_ascentPx     = face.GetAscentPx(m_size);
        m_lineHeightPx = face.GetLineHeightPx(m_size);

        const std::vector<uint32_t> cps = DecodeCodepoints(m_text.c_str(), static_cast<UINT32>(m_text.size()));
        if (cps.empty()) return;

        const bool doWrap = (m_wrap != DWRITE_WORD_WRAPPING_NO_WRAP) && (m_maxW > 4.0f);
        LaidOutLine cur;
        size_t lastSpacePos = std::string::npos;
        float widthAtLastSpace = 0.0f;

        for (size_t i = 0; i < cps.size(); ++i) {
            const uint32_t cp = cps[i];
            if (cp == '\r') continue;
            if (cp == '\n') {
                m_lines.push_back(std::move(cur));
                cur = LaidOutLine{};
                lastSpacePos = std::string::npos;
                continue;
            }
            const float adv = face.GetGlyph(cp, m_pxHeight).advancePx;
            if (doWrap && !cur.cps.empty() && cur.widthPx + adv > m_maxW) {
                if (lastSpacePos != std::string::npos && lastSpacePos + 1 < cur.cps.size()) {
                    std::vector<uint32_t> rem(cur.cps.begin() + static_cast<ptrdiff_t>(lastSpacePos + 1), cur.cps.end());
                    cur.cps.resize(lastSpacePos);
                    cur.widthPx = widthAtLastSpace;
                    m_lines.push_back(std::move(cur));
                    cur = LaidOutLine{};
                    for (uint32_t rcp : rem) {
                        cur.widthPx += face.GetGlyph(rcp, m_pxHeight).advancePx;
                        cur.cps.push_back(rcp);
                    }
                } else {
                    m_lines.push_back(std::move(cur));
                    cur = LaidOutLine{};
                }
                lastSpacePos = std::string::npos;
            }
            if (cp == ' ') {
                lastSpacePos = cur.cps.size();
                widthAtLastSpace = cur.widthPx;
            }
            cur.cps.push_back(cp);
            cur.widthPx += adv;
        }
        if (!cur.cps.empty()) m_lines.push_back(std::move(cur));

        // Apply character/ellipsis trimming on single-line or overflowing lines when requested
        if (m_trim.granularity != DWRITE_TRIMMING_GRANULARITY_NONE && m_maxW > 2.0f) {
            const float dotAdv = face.GetGlyph('.', m_pxHeight).advancePx;
            const float ellAdv = m_hasEllipsis ? (3.0f * dotAdv) : 0.0f;
            for (auto& line : m_lines) {
                if (line.widthPx <= m_maxW) continue;
                const float budget = std::max(0.0f, m_maxW - ellAdv);
                float w = 0.0f;
                size_t keep = 0;
                while (keep < line.cps.size()) {
                    const float gAdv = face.GetGlyph(line.cps[keep], m_pxHeight).advancePx;
                    if (w + gAdv > budget) break;
                    w += gAdv;
                    ++keep;
                }
                line.cps.resize(keep);
                if (m_hasEllipsis && m_maxW >= ellAdv) {
                    line.cps.push_back('.'); line.cps.push_back('.'); line.cps.push_back('.');
                    w += ellAdv;
                }
                line.widthPx = w;
            }
        }
    }

    std::atomic<ULONG> m_ref{ 1 };
    std::wstring m_text;
    std::wstring m_family = L"Segoe UI";
    DWRITE_FONT_WEIGHT m_weight = DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STYLE m_style = DWRITE_FONT_STYLE_NORMAL;
    FLOAT m_size = 12.0f;
    int m_pxHeight = 12;
    FLOAT m_maxW = 0.0f;
    FLOAT m_maxH = 0.0f;
    float m_ascentPx = 10.0f;
    float m_lineHeightPx = 15.0f;
    DWRITE_TEXT_ALIGNMENT m_textAlign = DWRITE_TEXT_ALIGNMENT_LEADING;
    DWRITE_PARAGRAPH_ALIGNMENT m_paraAlign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    DWRITE_WORD_WRAPPING m_wrap = DWRITE_WORD_WRAPPING_WRAP;
    DWRITE_TRIMMING m_trim = { DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0 };
    bool m_hasEllipsis = false;
    std::vector<LaidOutLine> m_lines;
};

class SwDWriteFactory final : public IDWriteFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFactory)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE CreateTextFormat(
        const WCHAR* family, IDWriteFontCollection*, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style,
        DWRITE_FONT_STRETCH, FLOAT size, const WCHAR*, IDWriteTextFormat** out) override
    {
        if (!out) return E_POINTER;
        *out = new SwTextFormat(family, weight, style, size);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateEllipsisTrimmingSign(IDWriteTextFormat*, IDWriteInlineObject** out) override {
        if (!out) return E_POINTER;
        *out = new SwInlineObject();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateTextLayout(
        const WCHAR* str, UINT32 len, IDWriteTextFormat* fmt, FLOAT maxW, FLOAT maxH, IDWriteTextLayout** out) override
    {
        if (!out || !fmt) return E_POINTER;
        *out = new SwTextLayout(str, len, fmt, maxW, maxH);
        return S_OK;
    }
private:
    std::atomic<ULONG> m_ref{ 1 };
};

// ==============================================================================================================
// 3. Direct2D Resources (SwD2DBitmap, SwSolidColorBrush, SwPathGeometry)
// ==============================================================================================================
class SwD2DBitmap final : public ID2D1Bitmap {
public:
    SwD2DBitmap(D2D1_SIZE_U sz, const void* srcData, UINT32 pitch, const D2D1_BITMAP_PROPERTIES* props)
        : m_size(sz)
    {
        if (props) {
            if (props->dpiX > 0.0f) m_dpiX = props->dpiX;
            if (props->dpiY > 0.0f) m_dpiY = props->dpiY;
            m_alphaMode = props->pixelFormat.alphaMode;
        }
        const size_t total = static_cast<size_t>(sz.width) * sz.height * 4u;
        m_pixels.assign(total, 0);
        if (srcData && sz.width > 0 && sz.height > 0) {
            CopyFromMemory(nullptr, srcData, pitch ? pitch : (sz.width * 4u));
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1Bitmap)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    D2D1_SIZE_F STDMETHODCALLTYPE GetSize() const override {
        return D2D1::SizeF(static_cast<float>(m_size.width) * 96.0f / m_dpiX,
                           static_cast<float>(m_size.height) * 96.0f / m_dpiY);
    }
    D2D1_SIZE_U STDMETHODCALLTYPE GetPixelSize() const override { return m_size; }

    HRESULT STDMETHODCALLTYPE CopyFromMemory(const D2D1_RECT_U* dstRect, const void* srcData, UINT32 pitch) override {
        if (!srcData) return E_INVALIDARG;
        const UINT32 x0 = dstRect ? dstRect->left   : 0u;
        const UINT32 y0 = dstRect ? dstRect->top    : 0u;
        const UINT32 x1 = dstRect ? std::min(dstRect->right, m_size.width)   : m_size.width;
        const UINT32 y1 = dstRect ? std::min(dstRect->bottom, m_size.height) : m_size.height;
        if (x1 <= x0 || y1 <= y0) return S_OK;
        const UINT32 rowW = x1 - x0;
        const UINT32 srcStride = pitch ? pitch : (rowW * 4u);
        const auto* srcBytes = static_cast<const uint8_t*>(srcData);
        for (UINT32 y = 0; y < (y1 - y0); ++y) {
            uint8_t* dstRow = m_pixels.data() + (static_cast<size_t>(y0 + y) * m_size.width + x0) * 4u;
            const uint8_t* srcRow = srcBytes + static_cast<size_t>(y) * srcStride;
            std::memcpy(dstRow, srcRow, static_cast<size_t>(rowW) * 4u);
            if (m_alphaMode == D2D1_ALPHA_MODE_IGNORE) {
                for (UINT32 x = 0; x < rowW; ++x) dstRow[x * 4u + 3u] = 255u;
            }
        }
        return S_OK;
    }

    const std::vector<uint8_t>& Pixels() const { return m_pixels; }
    D2D1_ALPHA_MODE AlphaMode() const { return m_alphaMode; }

private:
    std::atomic<ULONG> m_ref{ 1 };
    D2D1_SIZE_U m_size;
    float m_dpiX = 96.0f;
    float m_dpiY = 96.0f;
    D2D1_ALPHA_MODE m_alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    std::vector<uint8_t> m_pixels;
};

class SwSolidColorBrush final : public ID2D1SolidColorBrush {
public:
    SwSolidColorBrush(const D2D1_COLOR_F& c, float opacity) : m_color(c), m_opacity(opacity) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1SolidColorBrush)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    void STDMETHODCALLTYPE SetColor(const D2D1_COLOR_F& c) override { m_color = c; }
    D2D1_COLOR_F STDMETHODCALLTYPE GetColor() const override { return m_color; }
    float Opacity() const { return m_opacity; }

private:
    std::atomic<ULONG> m_ref{ 1 };
    D2D1_COLOR_F m_color;
    float m_opacity = 1.0f;
};

struct PathFigure {
    std::vector<D2D1_POINT_2F> pts;
    bool filled = true;
    bool closed = false;
};

class SwPathGeometry;

class SwGeometrySink final : public ID2D1GeometrySink {
public:
    explicit SwGeometrySink(std::vector<PathFigure>* dst) : m_dst(dst) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1GeometrySink)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F startPoint, D2D1_FIGURE_BEGIN figureBegin) override {
        m_cur = PathFigure{};
        m_cur.filled = (figureBegin == D2D1_FIGURE_BEGIN_FILLED);
        m_cur.pts.push_back(startPoint);
        m_inFig = true;
    }

    void STDMETHODCALLTYPE AddLine(D2D1_POINT_2F point) override {
        if (m_inFig) m_cur.pts.push_back(point);
    }

    void STDMETHODCALLTYPE AddArc(const D2D1_ARC_SEGMENT& arc) override {
        if (!m_inFig || m_cur.pts.empty()) return;
        const D2D1_POINT_2F p1 = m_cur.pts.back();
        const D2D1_POINT_2F p2 = arc.point;
        double rx = std::abs(arc.size.width);
        double ry = std::abs(arc.size.height);
        if (rx < 1e-4 || ry < 1e-4 || (std::hypot(p2.x - p1.x, p2.y - p1.y) < 1e-4)) {
            m_cur.pts.push_back(p2);
            return;
        }
        // W3C SVG F.6.5 Endpoint-to-Center Arc Parameterization
        constexpr double kPi = 3.14159265358979323846;
        const double phi = arc.rotationAngle * (kPi / 180.0);
        const double cosPhi = std::cos(phi), sinPhi = std::sin(phi);
        const double dx2 = 0.5 * (p1.x - p2.x), dy2 = 0.5 * (p1.y - p2.y);
        const double x1p =  cosPhi * dx2 + sinPhi * dy2;
        const double y1p = -sinPhi * dx2 + cosPhi * dy2;

        const double lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
        if (lam > 1.0) {
            const double s = std::sqrt(lam);
            rx *= s; ry *= s;
        }
        const double rx2 = rx * rx, ry2 = ry * ry, x1p2 = x1p * x1p, y1p2 = y1p * y1p;
        const bool fA = (arc.arcSize == D2D1_ARC_SIZE_LARGE);
        const bool fS = (arc.sweepDirection == D2D1_SWEEP_DIRECTION_CLOCKWISE);
        double num = std::max(0.0, rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2);
        double den = std::max(1e-12, rx2 * y1p2 + ry2 * x1p2);
        double coef = std::sqrt(num / den) * ((fA == fS) ? -1.0 : 1.0);
        const double cxp =  coef * (rx * y1p / ry);
        const double cyp = -coef * (ry * x1p / rx);
        const double cx = cosPhi * cxp - sinPhi * cyp + 0.5 * (p1.x + p2.x);
        const double cy = sinPhi * cxp + cosPhi * cyp + 0.5 * (p1.y + p2.y);

        auto vecAngle = [](double ux, double uy, double vx, double vy) -> double {
            const double dot = ux * vx + uy * vy;
            const double len = std::max(1e-12, std::hypot(ux, uy) * std::hypot(vx, vy));
            const double ang = std::acos(std::clamp(dot / len, -1.0, 1.0));
            return (ux * vy - uy * vx < 0.0) ? -ang : ang;
        };
        const double theta1 = vecAngle(1.0, 0.0, (x1p - cxp) / rx, (y1p - cyp) / ry);
        double dTheta = vecAngle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
        if (!fS && dTheta > 0.0) dTheta -= 2.0 * kPi;
        else if (fS && dTheta < 0.0) dTheta += 2.0 * kPi;

        const int steps = std::clamp(static_cast<int>(std::ceil(std::abs(dTheta) / (kPi / 18.0))), 4, 72);
        for (int i = 1; i <= steps; ++i) {
            const double th = theta1 + dTheta * (static_cast<double>(i) / steps);
            const double ex = rx * std::cos(th), ey = ry * std::sin(th);
            m_cur.pts.push_back(D2D1::Point2F(
                static_cast<float>(cosPhi * ex - sinPhi * ey + cx),
                static_cast<float>(sinPhi * ex + cosPhi * ey + cy)));
        }
    }

    void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END figureEnd) override {
        if (!m_inFig) return;
        m_cur.closed = (figureEnd == D2D1_FIGURE_END_CLOSED);
        if (m_dst && m_cur.pts.size() >= 2) m_dst->push_back(std::move(m_cur));
        m_inFig = false;
    }

    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }

private:
    std::atomic<ULONG> m_ref{ 1 };
    std::vector<PathFigure>* m_dst = nullptr;
    PathFigure m_cur;
    bool m_inFig = false;
};

class SwPathGeometry final : public ID2D1PathGeometry {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1PathGeometry)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE Open(ID2D1GeometrySink** sink) override {
        if (!sink) return E_POINTER;
        m_figures.clear();
        *sink = new SwGeometrySink(&m_figures);
        return S_OK;
    }
    const std::vector<PathFigure>& Figures() const { return m_figures; }

private:
    std::atomic<ULONG> m_ref{ 1 };
    std::vector<PathFigure> m_figures;
};

// ==============================================================================================================
// 4. Software BGRA32 Render Target (HWND, DC, and WIC Bitmap Render Targets)
// ==============================================================================================================
struct PremulBgra {
    uint8_t b = 0, g = 0, r = 0, a = 0;
};

inline PremulBgra BrushToColor(ID2D1Brush* brush, float extraOpacity = 1.0f) {
    auto* sb = static_cast<SwSolidColorBrush*>(brush);
    if (!sb) return {};
    const D2D1_COLOR_F c = sb->GetColor();
    const float a = std::clamp(c.a * sb->Opacity() * extraOpacity, 0.0f, 1.0f);
    const float r = std::clamp(c.r, 0.0f, 1.0f);
    const float g = std::clamp(c.g, 0.0f, 1.0f);
    const float b = std::clamp(c.b, 0.0f, 1.0f);
    return {
        static_cast<uint8_t>(std::lround(b * 255.0f)),
        static_cast<uint8_t>(std::lround(g * 255.0f)),
        static_cast<uint8_t>(std::lround(r * 255.0f)),
        static_cast<uint8_t>(std::lround(a * 255.0f))
    };
}

inline void BlendStraightOverPremul(uint8_t* dst, uint8_t sb, uint8_t sg, uint8_t sr, uint32_t sa) {
    if (sa == 0) return;
    if (sa >= 255u) {
        dst[0] = sb; dst[1] = sg; dst[2] = sr; dst[3] = 255u;
        return;
    }
    const uint32_t invA = 255u - sa;
    dst[0] = static_cast<uint8_t>((sb * sa + dst[0] * invA + 127u) / 255u);
    dst[1] = static_cast<uint8_t>((sg * sg * 0u + sb * 0u + sg * sa + dst[1] * invA + 127u) / 255u);
    dst[2] = static_cast<uint8_t>((sr * sa + dst[2] * invA + 127u) / 255u);
    dst[3] = static_cast<uint8_t>(sa + (dst[3] * invA + 127u) / 255u);
}

class SwRenderTarget final : public ID2D1HwndRenderTarget, public ID2D1DCRenderTarget {
public:
    SwRenderTarget(HWND hwnd, D2D1_SIZE_U sz, IWICBitmap* wicTarget = nullptr)
        : m_hwnd(hwnd), m_wicTarget(wicTarget)
    {
        if (m_wicTarget) m_wicTarget->AddRef();
        m_transform = D2D1::Matrix3x2F::Identity();
        ResizeInternal(sz.width ? sz.width : 1u, sz.height ? sz.height : 1u);
    }

    ~SwRenderTarget() {
        if (m_wicTarget) m_wicTarget->Release();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1RenderTarget) || riid == __uuidof(ID2D1HwndRenderTarget)) {
            *ppv = static_cast<ID2D1HwndRenderTarget*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == __uuidof(ID2D1DCRenderTarget)) {
            *ppv = static_cast<ID2D1DCRenderTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE CreateBitmap(
        D2D1_SIZE_U size, const void* srcData, UINT32 pitch,
        const D2D1_BITMAP_PROPERTIES* props, ID2D1Bitmap** out) override
    {
        if (!out || size.width == 0 || size.height == 0) return E_INVALIDARG;
        *out = new SwD2DBitmap(size, srcData, pitch, props);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateSolidColorBrush(
        const D2D1_COLOR_F* color, const D2D1_BRUSH_PROPERTIES* props, ID2D1SolidColorBrush** out) override
    {
        if (!out) return E_POINTER;
        *out = new SwSolidColorBrush(color ? *color : D2D1::ColorF(0, 1.0f), props ? props->opacity : 1.0f);
        return S_OK;
    }

    void STDMETHODCALLTYPE BeginDraw() override {
        m_transform = D2D1::Matrix3x2F::Identity();
        m_clipStack.clear();
        UpdateEffectiveClip();
        if (m_wicTarget && m_width > 0 && m_height > 0) {
            m_wicTarget->CopyPixels(nullptr, m_width * 4u, static_cast<UINT>(m_fb.size()), m_fb.data());
        }
    }

    HRESULT STDMETHODCALLTYPE EndDraw(D2D1_TAG* t1, D2D1_TAG* t2) override {
        if (t1) *t1 = 0;
        if (t2) *t2 = 0;
        if (m_wicTarget && m_width > 0 && m_height > 0) {
            IWICBitmapLock* lock = nullptr;
            if (SUCCEEDED(m_wicTarget->Lock(nullptr, WICBitmapLockWrite, &lock)) && lock) {
                UINT stride = 0, cb = 0;
                BYTE* dst = nullptr;
                if (SUCCEEDED(lock->GetStride(&stride)) && SUCCEEDED(lock->GetDataPointer(&cb, &dst)) && dst) {
                    for (UINT32 y = 0; y < m_height; ++y) {
                        std::memcpy(dst + static_cast<size_t>(y) * stride,
                                    m_fb.data() + static_cast<size_t>(y) * m_width * 4u,
                                    static_cast<size_t>(m_width) * 4u);
                    }
                }
                lock->Release();
            }
        }
        if (m_boundDc && m_width > 0 && m_height > 0) {
            Explorer2::Hal::BlitBgraToDcDib(m_boundDc, m_boundRect, m_fb.data(), static_cast<int>(m_width), static_cast<int>(m_height));
        }
        if (m_hwnd && m_width > 0 && m_height > 0) {
            Explorer2::Hal::PresentHwndFramebuffer(m_hwnd, m_fb.data(), m_width, m_height);
        }
        return S_OK;
    }

    void STDMETHODCALLTYPE Clear(const D2D1_COLOR_F* cc) override {
        const D2D1_COLOR_F c = cc ? *cc : D2D1::ColorF(0, 0.0f);
        const float a = std::clamp(c.a, 0.0f, 1.0f);
        // Premultiplied BGRA clear value
        const uint8_t pb = static_cast<uint8_t>(std::lround(std::clamp(c.b, 0.0f, 1.0f) * a * 255.0f));
        const uint8_t pg = static_cast<uint8_t>(std::lround(std::clamp(c.g, 0.0f, 1.0f) * a * 255.0f));
        const uint8_t pr = static_cast<uint8_t>(std::lround(std::clamp(c.r, 0.0f, 1.0f) * a * 255.0f));
        const uint8_t pa = static_cast<uint8_t>(std::lround(a * 255.0f));
        const uint32_t px32 = static_cast<uint32_t>(pb) | (static_cast<uint32_t>(pg) << 8) |
                              (static_cast<uint32_t>(pr) << 16) | (static_cast<uint32_t>(pa) << 24);

        const int x0 = m_clipStack.empty() ? 0 : m_clipX0;
        const int y0 = m_clipStack.empty() ? 0 : m_clipY0;
        const int x1 = m_clipStack.empty() ? static_cast<int>(m_width)  : m_clipX1;
        const int y1 = m_clipStack.empty() ? static_cast<int>(m_height) : m_clipY1;
        for (int y = y0; y < y1; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(m_fb.data() + static_cast<size_t>(y) * m_width * 4u);
            std::fill(row + x0, row + x1, px32);
        }
    }

    void STDMETHODCALLTYPE SetTransform(const D2D1_MATRIX_3X2_F* t) override {
        m_transform = t ? *t : D2D1::Matrix3x2F::Identity();
    }

    void STDMETHODCALLTYPE PushAxisAlignedClip(const D2D1_RECT_F* clipRect, D2D1_ANTIALIAS_MODE) override {
        if (!clipRect) return;
        const D2D1_POINT_2F p0 = XformPt(D2D1::Point2F(clipRect->left, clipRect->top));
        const D2D1_POINT_2F p1 = XformPt(D2D1::Point2F(clipRect->right, clipRect->bottom));
        m_clipStack.push_back(D2D1::RectF(std::min(p0.x, p1.x), std::min(p0.y, p1.y),
                                          std::max(p0.x, p1.x), std::max(p0.y, p1.y)));
        UpdateEffectiveClip();
    }

    void STDMETHODCALLTYPE PopAxisAlignedClip() override {
        if (!m_clipStack.empty()) {
            m_clipStack.pop_back();
            UpdateEffectiveClip();
        }
    }

    void STDMETHODCALLTYPE FillRectangle(const D2D1_RECT_F* rect, ID2D1Brush* brush) override {
        if (!rect || !brush) return;
        const PremulBgra col = BrushToColor(brush);
        if (col.a == 0) return;
        const D2D1_RECT_F tr = XformRect(*rect);
        if (tr.right <= tr.left || tr.bottom <= tr.top) return;

        const int ix0 = std::max(m_clipX0, static_cast<int>(std::floor(tr.left)));
        const int iy0 = std::max(m_clipY0, static_cast<int>(std::floor(tr.top)));
        const int ix1 = std::min(m_clipX1, static_cast<int>(std::ceil(tr.right)));
        const int iy1 = std::min(m_clipY1, static_cast<int>(std::ceil(tr.bottom)));

        for (int y = iy0; y < iy1; ++y) {
            const float covY = std::clamp(std::min(static_cast<float>(y + 1), tr.bottom) - std::max(static_cast<float>(y), tr.top), 0.0f, 1.0f);
            if (covY <= 0.0f) continue;
            uint8_t* row = m_fb.data() + static_cast<size_t>(y) * m_width * 4u;
            for (int x = ix0; x < ix1; ++x) {
                const float covX = std::clamp(std::min(static_cast<float>(x + 1), tr.right) - std::max(static_cast<float>(x), tr.left), 0.0f, 1.0f);
                const uint32_t sa = static_cast<uint32_t>(std::lround(col.a * covX * covY));
                BlendStraightOverPremul(row + x * 4, col.b, col.g, col.r, sa);
            }
        }
    }

    void STDMETHODCALLTYPE DrawRectangle(const D2D1_RECT_F* rect, ID2D1Brush* brush, FLOAT strokeWidth, ID2D1StrokeStyle*) override {
        if (!rect || !brush || strokeWidth <= 0.0f) return;
        const float hw = 0.5f * strokeWidth;
        const float l = std::min(rect->left, rect->right), r = std::max(rect->left, rect->right);
        const float t = std::min(rect->top, rect->bottom), b = std::max(rect->top, rect->bottom);
        const D2D1_RECT_F topR    = D2D1::RectF(l - hw, t - hw, r + hw, t + hw);
        const D2D1_RECT_F botR    = D2D1::RectF(l - hw, b - hw, r + hw, b + hw);
        const D2D1_RECT_F leftR   = D2D1::RectF(l - hw, t + hw, l + hw, b - hw);
        const D2D1_RECT_F rightR  = D2D1::RectF(r - hw, t + hw, r + hw, b - hw);
        FillRectangle(&topR, brush);
        FillRectangle(&botR, brush);
        if (b - t > strokeWidth) {
            FillRectangle(&leftR, brush);
            FillRectangle(&rightR, brush);
        }
    }

    void STDMETHODCALLTYPE FillRoundedRectangle(const D2D1_ROUNDED_RECT* rr, ID2D1Brush* brush) override {
        if (!rr || !brush) return;
        if (rr->radiusX <= 0.5f && rr->radiusY <= 0.5f) {
            FillRectangle(&rr->rect, brush);
            return;
        }
        RasterizeRoundedRect(*rr, brush, 0.0f, true);
    }

    void STDMETHODCALLTYPE DrawRoundedRectangle(const D2D1_ROUNDED_RECT* rr, ID2D1Brush* brush, FLOAT strokeWidth, ID2D1StrokeStyle*) override {
        if (!rr || !brush || strokeWidth <= 0.0f) return;
        if (rr->radiusX <= 0.5f && rr->radiusY <= 0.5f) {
            DrawRectangle(&rr->rect, brush, strokeWidth, nullptr);
            return;
        }
        RasterizeRoundedRect(*rr, brush, strokeWidth, false);
    }

    void STDMETHODCALLTYPE FillEllipse(const D2D1_ELLIPSE* el, ID2D1Brush* brush) override {
        if (el && brush) RasterizeEllipse(*el, brush, 0.0f, true);
    }

    void STDMETHODCALLTYPE DrawEllipse(const D2D1_ELLIPSE* el, ID2D1Brush* brush, FLOAT strokeWidth, ID2D1StrokeStyle*) override {
        if (el && brush && strokeWidth > 0.0f) RasterizeEllipse(*el, brush, strokeWidth, false);
    }

    void STDMETHODCALLTYPE DrawLine(D2D1_POINT_2F p0, D2D1_POINT_2F p1, ID2D1Brush* brush, FLOAT strokeWidth, ID2D1StrokeStyle*) override {
        if (!brush || strokeWidth <= 0.0f) return;
        const PremulBgra col = BrushToColor(brush);
        if (col.a == 0) return;
        const D2D1_POINT_2F a = XformPt(p0);
        const D2D1_POINT_2F b = XformPt(p1);
        const float rad = std::max(0.5f, 0.5f * strokeWidth);
        const int ix0 = std::max(m_clipX0, static_cast<int>(std::floor(std::min(a.x, b.x) - rad - 1.0f)));
        const int iy0 = std::max(m_clipY0, static_cast<int>(std::floor(std::min(a.y, b.y) - rad - 1.0f)));
        const int ix1 = std::min(m_clipX1, static_cast<int>(std::ceil(std::max(a.x, b.x) + rad + 1.0f)));
        const int iy1 = std::min(m_clipY1, static_cast<int>(std::ceil(std::max(a.y, b.y) + rad + 1.0f)));

        const float vx = b.x - a.x, vy = b.y - a.y;
        const float len2 = vx * vx + vy * vy;
        for (int y = iy0; y < iy1; ++y) {
            const float py = y + 0.5f;
            uint8_t* row = m_fb.data() + static_cast<size_t>(y) * m_width * 4u;
            for (int x = ix0; x < ix1; ++x) {
                const float px = x + 0.5f;
                float t = (len2 > 1e-6f) ? std::clamp(((px - a.x) * vx + (py - a.y) * vy) / len2, 0.0f, 1.0f) : 0.0f;
                const float dist = std::hypot(px - (a.x + t * vx), py - (a.y + t * vy));
                const float cov = std::clamp(rad + 0.5f - dist, 0.0f, 1.0f);
                if (cov > 0.0f) {
                    BlendStraightOverPremul(row + x * 4, col.b, col.g, col.r, static_cast<uint32_t>(std::lround(col.a * cov)));
                }
            }
        }
    }

    void STDMETHODCALLTYPE DrawGeometry(ID2D1Geometry* geom, ID2D1Brush* brush, FLOAT strokeWidth, ID2D1StrokeStyle*) override {
        auto* pg = static_cast<SwPathGeometry*>(geom);
        if (!pg || !brush || strokeWidth <= 0.0f) return;
        for (const auto& fig : pg->Figures()) {
            for (size_t i = 0; i + 1 < fig.pts.size(); ++i) {
                DrawLine(fig.pts[i], fig.pts[i + 1], brush, strokeWidth, nullptr);
            }
            if (fig.closed && fig.pts.size() >= 3) {
                DrawLine(fig.pts.back(), fig.pts.front(), brush, strokeWidth, nullptr);
            }
        }
    }

    void STDMETHODCALLTYPE FillGeometry(ID2D1Geometry* geom, ID2D1Brush* brush, ID2D1Brush*) override {
        auto* pg = static_cast<SwPathGeometry*>(geom);
        if (!pg || !brush) return;
        const PremulBgra col = BrushToColor(brush);
        if (col.a == 0) return;

        struct Edge { float x0, y0, x1, y1; };
        std::vector<Edge> edges;
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        for (const auto& fig : pg->Figures()) {
            if (!fig.filled || fig.pts.size() < 3) continue;
            const size_t n = fig.pts.size();
            for (size_t i = 0; i < n; ++i) {
                const D2D1_POINT_2F a = XformPt(fig.pts[i]);
                const D2D1_POINT_2F b = XformPt(fig.pts[(i + 1) % n]);
                edges.push_back({ a.x, a.y, b.x, b.y });
                minX = std::min({ minX, a.x, b.x }); maxX = std::max({ maxX, a.x, b.x });
                minY = std::min({ minY, a.y, b.y }); maxY = std::max({ maxY, a.y, b.y });
            }
        }
        if (edges.empty()) return;

        const int ix0 = std::max(m_clipX0, static_cast<int>(std::floor(minX)));
        const int iy0 = std::max(m_clipY0, static_cast<int>(std::floor(minY)));
        const int ix1 = std::min(m_clipX1, static_cast<int>(std::ceil(maxX)));
        const int iy1 = std::min(m_clipY1, static_cast<int>(std::ceil(maxY)));

        std::vector<float> xHits;
        for (int y = iy0; y < iy1; ++y) {
            std::vector<uint8_t> covRow(ix1 - ix0, 0);
            for (int sy = 0; sy < 4; ++sy) {
                const float py = y + (sy + 0.5f) * 0.25f;
                xHits.clear();
                for (const auto& e : edges) {
                    if ((e.y0 <= py && e.y1 > py) || (e.y1 <= py && e.y0 > py)) {
                        const float t = (py - e.y0) / (e.y1 - e.y0);
                        xHits.push_back(e.x0 + t * (e.x1 - e.x0));
                    }
                }
                if (xHits.size() < 2) continue;
                std::sort(xHits.begin(), xHits.end());
                for (size_t k = 0; k + 1 < xHits.size(); k += 2) {
                    const float xa = xHits[k], xb = xHits[k + 1];
                    const int s0 = std::max(ix0, static_cast<int>(std::floor(xa)));
                    const int s1 = std::min(ix1 - 1, static_cast<int>(std::ceil(xb)));
                    for (int x = s0; x <= s1; ++x) {
                        const float overlap = std::clamp(std::min(static_cast<float>(x + 1), xb) - std::max(static_cast<float>(x), xa), 0.0f, 1.0f);
                        covRow[x - ix0] = static_cast<uint8_t>(std::min<int>(255, covRow[x - ix0] + static_cast<int>(std::lround(overlap * 63.75f))));
                    }
                }
            }
            uint8_t* row = m_fb.data() + static_cast<size_t>(y) * m_width * 4u;
            for (int x = ix0; x < ix1; ++x) {
                const uint8_t c = covRow[x - ix0];
                if (c > 0) {
                    BlendStraightOverPremul(row + x * 4, col.b, col.g, col.r, (static_cast<uint32_t>(col.a) * c + 127u) / 255u);
                }
            }
        }
    }

    void STDMETHODCALLTYPE DrawBitmap(
        ID2D1Bitmap* bitmap, const D2D1_RECT_F* dstRectIn, FLOAT opacity,
        D2D1_BITMAP_INTERPOLATION_MODE interp, const D2D1_RECT_F* srcRectIn) override
    {
        auto* bmp = static_cast<SwD2DBitmap*>(bitmap);
        if (!bmp || opacity <= 0.0f) return;
        const D2D1_SIZE_U bsz = bmp->GetPixelSize();
        if (bsz.width == 0 || bsz.height == 0) return;

        const D2D1_SIZE_F dipSz = bmp->GetSize();
        const D2D1_RECT_F rawDst = dstRectIn ? *dstRectIn : D2D1::RectF(0.0f, 0.0f, dipSz.width, dipSz.height);
        const D2D1_RECT_F dst = XformRect(rawDst);
        const float dstW = dst.right - dst.left;
        const float dstH = dst.bottom - dst.top;
        if (dstW <= 0.0f || dstH <= 0.0f) return;

        const float dpiScaleX = static_cast<float>(bsz.width) / std::max(1.0f, dipSz.width);
        const float dpiScaleY = static_cast<float>(bsz.height) / std::max(1.0f, dipSz.height);
        const D2D1_RECT_F src = srcRectIn
            ? D2D1::RectF(srcRectIn->left * dpiScaleX, srcRectIn->top * dpiScaleY,
                          srcRectIn->right * dpiScaleX, srcRectIn->bottom * dpiScaleY)
            : D2D1::RectF(0.0f, 0.0f, static_cast<float>(bsz.width), static_cast<float>(bsz.height));
        const float srcW = src.right - src.left;
        const float srcH = src.bottom - src.top;
        if (srcW <= 0.0f || srcH <= 0.0f) return;

        // Clamp strictly within source sub-rectangle so shared icon texture atlases never bleed across cells
        const float minSx = std::clamp(src.left, 0.0f, static_cast<float>(bsz.width) - 0.001f);
        const float maxSx = std::clamp(src.right - 0.001f, minSx, static_cast<float>(bsz.width) - 0.001f);
        const float minSy = std::clamp(src.top, 0.0f, static_cast<float>(bsz.height) - 0.001f);
        const float maxSy = std::clamp(src.bottom - 0.001f, minSy, static_cast<float>(bsz.height) - 0.001f);

        // Pixel-center inclusion: pixel (x, y) is drawn iff (x + 0.5, y + 0.5) lies inside [dst.left, dst.right) x [dst.top, dst.bottom)
        const int ix0 = std::max(m_clipX0, static_cast<int>(std::ceil(dst.left - 0.5f)));
        const int iy0 = std::max(m_clipY0, static_cast<int>(std::ceil(dst.top - 0.5f)));
        const int ix1 = std::min(m_clipX1, static_cast<int>(std::ceil(dst.right - 0.5f)));
        const int iy1 = std::min(m_clipY1, static_cast<int>(std::ceil(dst.bottom - 0.5f)));
        if (ix1 <= ix0 || iy1 <= iy0) return;

        const auto& spx = bmp->Pixels();
        const uint32_t op255 = static_cast<uint32_t>(std::lround(std::clamp(opacity, 0.0f, 1.0f) * 255.0f));
        const bool premulSrc = (bmp->AlphaMode() != D2D1_ALPHA_MODE_STRAIGHT);

        for (int y = iy0; y < iy1; ++y) {
            const float v = ((y + 0.5f) - dst.top) / dstH;
            const float sy = std::clamp(src.top + v * srcH - 0.5f, minSy, maxSy);
            uint8_t* dstRow = m_fb.data() + static_cast<size_t>(y) * m_width * 4u;

            for (int x = ix0; x < ix1; ++x) {
                const float u = ((x + 0.5f) - dst.left) / dstW;
                const float sx = std::clamp(src.left + u * srcW - 0.5f, minSx, maxSx);

                uint32_t b = 0, g = 0, r = 0, a = 0;
                if (interp == D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR) {
                    const int nx = std::clamp(static_cast<int>(std::floor(sx + 0.5f)), static_cast<int>(minSx), static_cast<int>(maxSx));
                    const int ny = std::clamp(static_cast<int>(std::floor(sy + 0.5f)), static_cast<int>(minSy), static_cast<int>(maxSy));
                    const uint8_t* p = spx.data() + (static_cast<size_t>(ny) * bsz.width + nx) * 4u;
                    b = p[0]; g = p[1]; r = p[2]; a = p[3];
                } else {
                    const int xA = static_cast<int>(std::floor(sx));
                    const int yA = static_cast<int>(std::floor(sy));
                    const int xB = std::min(xA + 1, static_cast<int>(maxSx));
                    const int yB = std::min(yA + 1, static_cast<int>(maxSy));
                    const int xA0 = std::max(xA, static_cast<int>(minSx));
                    const int yA0 = std::max(yA, static_cast<int>(minSy));
                    const uint32_t fx = static_cast<uint32_t>(std::lround(std::clamp(sx - xA, 0.0f, 1.0f) * 256.0f));
                    const uint32_t fy = static_cast<uint32_t>(std::lround(std::clamp(sy - yA, 0.0f, 1.0f) * 256.0f));
                    const uint32_t w00 = (256u - fx) * (256u - fy);
                    const uint32_t w10 = fx * (256u - fy);
                    const uint32_t w01 = (256u - fx) * fy;
                    const uint32_t w11 = fx * fy;
                    const uint8_t* p00 = spx.data() + (static_cast<size_t>(yA0) * bsz.width + xA0) * 4u;
                    const uint8_t* p10 = spx.data() + (static_cast<size_t>(yA0) * bsz.width + xB)  * 4u;
                    const uint8_t* p01 = spx.data() + (static_cast<size_t>(yB)  * bsz.width + xA0) * 4u;
                    const uint8_t* p11 = spx.data() + (static_cast<size_t>(yB)  * bsz.width + xB)  * 4u;
                    b = (p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11 + 32768u) >> 16;
                    g = (p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11 + 32768u) >> 16;
                    r = (p00[2] * w00 + p10[2] * w10 + p01[2] * w01 + p11[2] * w11 + 32768u) >> 16;
                    a = (p00[3] * w00 + p10[3] * w10 + p01[3] * w01 + p11[3] * w11 + 32768u) >> 16;
                }

                if (a == 0) continue;
                uint8_t* d = dstRow + x * 4;
                if (premulSrc) {
                    const uint32_t pb = (b * op255 + 127u) / 255u;
                    const uint32_t pg = (g * op255 + 127u) / 255u;
                    const uint32_t pr = (r * op255 + 127u) / 255u;
                    const uint32_t pa = (a * op255 + 127u) / 255u;
                    const uint32_t invA = 255u - pa;
                    d[0] = static_cast<uint8_t>(std::min<uint32_t>(255u, pb + (d[0] * invA + 127u) / 255u));
                    d[1] = static_cast<uint8_t>(std::min<uint32_t>(255u, pg + (d[1] * invA + 127u) / 255u));
                    d[2] = static_cast<uint8_t>(std::min<uint32_t>(255u, pr + (d[2] * invA + 127u) / 255u));
                    d[3] = static_cast<uint8_t>(std::min<uint32_t>(255u, pa + (d[3] * invA + 127u) / 255u));
                } else {
                    BlendStraightOverPremul(d, static_cast<uint8_t>(b), static_cast<uint8_t>(g),
                                            static_cast<uint8_t>(r), (a * op255 + 127u) / 255u);
                }
            }
        }
    }

    void STDMETHODCALLTYPE DrawText(
        const WCHAR* string, UINT32 stringLength, IDWriteTextFormat* textFormat,
        const D2D1_RECT_F* layoutRect, ID2D1Brush* defaultFillBrush,
        D2D1_DRAW_TEXT_OPTIONS options, DWRITE_MEASURING_MODE) override
    {
        if (!string || stringLength == 0 || !textFormat || !layoutRect || !defaultFillBrush) return;
        if (layoutRect->right < layoutRect->left || layoutRect->bottom < layoutRect->top) return;
        SwTextLayout tempLayout(
            string, stringLength, textFormat,
            std::max(0.0f, layoutRect->right - layoutRect->left),
            std::max(0.0f, layoutRect->bottom - layoutRect->top));
        DrawTextLayout(D2D1::Point2F(layoutRect->left, layoutRect->top), &tempLayout, defaultFillBrush, options);
    }

    void STDMETHODCALLTYPE DrawTextLayout(
        D2D1_POINT_2F origin, IDWriteTextLayout* textLayout,
        ID2D1Brush* defaultFillBrush, D2D1_DRAW_TEXT_OPTIONS options) override
    {
        if (!textLayout || !defaultFillBrush) return;
        auto* sw = dynamic_cast<SwTextLayout*>(textLayout);
        if (!sw) return;
        const auto& lines = sw->Lines();
        if (lines.empty()) return;

        const PremulBgra col = BrushToColor(defaultFillBrush);
        if (col.a == 0) return;

        const D2D1_POINT_2F baseOrig = XformPt(origin);
        const float boxW = sw->GetMaxWidth();
        const float boxH = sw->GetMaxHeight();
        const float lineH = sw->LineHeightPx();
        const float ascent = sw->AscentPx();
        const int pxH = sw->PixelFontHeight();
        const float totalH = static_cast<float>(lines.size()) * lineH;

        float startY = baseOrig.y;
        const auto pAlign = sw->GetParagraphAlignment();
        if (pAlign == DWRITE_PARAGRAPH_ALIGNMENT_CENTER && boxH > 0.0f) startY += 0.5f * (boxH - totalH);
        else if (pAlign == DWRITE_PARAGRAPH_ALIGNMENT_FAR && boxH > 0.0f) startY += (boxH - totalH);

        int clipL = m_clipX0, clipT = m_clipY0, clipR = m_clipX1, clipB = m_clipY1;
        if (options & D2D1_DRAW_TEXT_OPTIONS_CLIP) {
            clipL = std::max(clipL, static_cast<int>(std::floor(baseOrig.x)));
            clipT = std::max(clipT, static_cast<int>(std::floor(baseOrig.y)));
            clipR = std::min(clipR, static_cast<int>(std::ceil(baseOrig.x + boxW)));
            clipB = std::min(clipB, static_cast<int>(std::ceil(baseOrig.y + boxH)));
        }

        TrueTypeFace& face = Fonts().Select(sw->GetFontFamilyNamePtr(), sw->GetFontWeight());
        const auto tAlign = sw->GetTextAlignment();

        for (size_t li = 0; li < lines.size(); ++li) {
            const auto& line = lines[li];
            float penX = baseOrig.x;
            if (tAlign == DWRITE_TEXT_ALIGNMENT_CENTER && boxW > 0.0f) penX += 0.5f * (boxW - line.widthPx);
            else if (tAlign == DWRITE_TEXT_ALIGNMENT_TRAILING && boxW > 0.0f) penX += (boxW - line.widthPx);
            const int baselineY = static_cast<int>(std::lround(startY + li * lineH + ascent));

            for (uint32_t cp : line.cps) {
                const RasterGlyph& rg = face.GetGlyph(cp, pxH);
                if (rg.width > 0 && rg.height > 0 && !rg.alpha.empty()) {
                    const int gx0 = static_cast<int>(std::lround(penX)) + rg.bearingX;
                    const int gy0 = baselineY - rg.bearingY;
                    const int xStart = std::max(clipL, gx0);
                    const int yStart = std::max(clipT, gy0);
                    const int xEnd   = std::min(clipR, gx0 + rg.width);
                    const int yEnd   = std::min(clipB, gy0 + rg.height);
                    for (int py = yStart; py < yEnd; ++py) {
                        uint8_t* dstRow = m_fb.data() + static_cast<size_t>(py) * m_width * 4u;
                        const uint8_t* aRow = rg.alpha.data() + static_cast<size_t>(py - gy0) * rg.width;
                        for (int px = xStart; px < xEnd; ++px) {
                            const uint8_t ga = aRow[px - gx0];
                            if (ga > 0) {
                                BlendStraightOverPremul(dstRow + px * 4, col.b, col.g, col.r,
                                                        (static_cast<uint32_t>(col.a) * ga + 127u) / 255u);
                            }
                        }
                    }
                }
                penX += rg.advancePx;
            }
        }
    }

    void STDMETHODCALLTYPE SetDpi(FLOAT dx, FLOAT dy) override {
        if (dx > 0.0f) m_dpiX = dx;
        if (dy > 0.0f) m_dpiY = dy;
    }
    void STDMETHODCALLTYPE GetDpi(FLOAT* dx, FLOAT* dy) const override {
        if (dx) *dx = m_dpiX;
        if (dy) *dy = m_dpiY;
    }
    D2D1_SIZE_F STDMETHODCALLTYPE GetSize() const override {
        return D2D1::SizeF(static_cast<float>(m_width) * 96.0f / m_dpiX,
                           static_cast<float>(m_height) * 96.0f / m_dpiY);
    }
    D2D1_SIZE_U STDMETHODCALLTYPE GetPixelSize() const override {
        return D2D1::SizeU(m_width, m_height);
    }

    HRESULT STDMETHODCALLTYPE Resize(const D2D1_SIZE_U* pixelSize) override {
        if (!pixelSize) return E_INVALIDARG;
        ResizeInternal(std::max(1u, pixelSize->width), std::max(1u, pixelSize->height));
        return S_OK;
    }
    HWND STDMETHODCALLTYPE GetHwnd() const override { return m_hwnd; }

    HRESULT STDMETHODCALLTYPE BindDC(const HDC hDC, const RECT* pSubRect) override {
        if (!hDC || !pSubRect) return E_INVALIDARG;
        m_boundDc = hDC;
        m_boundRect = *pSubRect;
        const UINT32 w = static_cast<UINT32>(std::max<LONG>(1, pSubRect->right - pSubRect->left));
        const UINT32 h = static_cast<UINT32>(std::max<LONG>(1, pSubRect->bottom - pSubRect->top));
        ResizeInternal(w, h);
        return S_OK;
    }

private:
    void ResizeInternal(UINT32 w, UINT32 h) {
        m_width  = std::clamp(w, 1u, 8192u);
        m_height = std::clamp(h, 1u, 8192u);
        m_fb.assign(static_cast<size_t>(m_width) * m_height * 4u, 0);
        UpdateEffectiveClip();
    }

    D2D1_POINT_2F XformPt(D2D1_POINT_2F p) const {
        return D2D1::Point2F(
            p.x * m_transform._11 + p.y * m_transform._21 + m_transform._31,
            p.x * m_transform._12 + p.y * m_transform._22 + m_transform._32);
    }

    D2D1_RECT_F XformRect(const D2D1_RECT_F& r) const {
        const D2D1_POINT_2F a = XformPt(D2D1::Point2F(r.left, r.top));
        const D2D1_POINT_2F b = XformPt(D2D1::Point2F(r.right, r.bottom));
        return D2D1::RectF(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y));
    }

    void UpdateEffectiveClip() {
        float x0 = 0.0f, y0 = 0.0f, x1 = static_cast<float>(m_width), y1 = static_cast<float>(m_height);
        for (const auto& c : m_clipStack) {
            x0 = std::max(x0, c.left);
            y0 = std::max(y0, c.top);
            x1 = std::min(x1, c.right);
            y1 = std::min(y1, c.bottom);
        }
        m_clipX0 = std::clamp(static_cast<int>(std::floor(x0)), 0, static_cast<int>(m_width));
        m_clipY0 = std::clamp(static_cast<int>(std::floor(y0)), 0, static_cast<int>(m_height));
        m_clipX1 = std::clamp(static_cast<int>(std::ceil(x1)), m_clipX0, static_cast<int>(m_width));
        m_clipY1 = std::clamp(static_cast<int>(std::ceil(y1)), m_clipY0, static_cast<int>(m_height));
    }

    void RasterizeRoundedRect(const D2D1_ROUNDED_RECT& rr, ID2D1Brush* brush, float strokeW, bool fill) {
        const PremulBgra col = BrushToColor(brush);
        if (col.a == 0) return;
        const D2D1_RECT_F tr = XformRect(rr.rect);
        const float w = tr.right - tr.left, h = tr.bottom - tr.top;
        if (w <= 0.0f || h <= 0.0f) return;
        const float rx = std::clamp(std::abs(rr.radiusX), 0.0f, 0.5f * w);
        const float ry = std::clamp(std::abs(rr.radiusY), 0.0f, 0.5f * h);
        const float cx = 0.5f * (tr.left + tr.right), cy = 0.5f * (tr.top + tr.bottom);
        const float bx = 0.5f * w - rx, by = 0.5f * h - ry;
        const float rAvg = std::max(0.0f, 0.5f * (rx + ry));
        const float pad = fill ? 1.0f : (0.5f * strokeW + 1.0f);

        const int ix0 = std::max(m_clipX0, static_cast<int>(std::floor(tr.left - pad)));
        const int iy0 = std::max(m_clipY0, static_cast<int>(std::floor(tr.top - pad)));
        const int ix1 = std::min(m_clipX1, static_cast<int>(std::ceil(tr.right + pad)));
        const int iy1 = std::min(m_clipY1, static_cast<int>(std::ceil(tr.bottom + pad)));

        for (int y = iy0; y < iy1; ++y) {
            const float qy = std::abs((y + 0.5f) - cy) - by;
            uint8_t* row = m_fb.data() + static_cast<size_t>(y) * m_width * 4u;
            for (int x = ix0; x < ix1; ++x) {
                const float qx = std::abs((x + 0.5f) - cx) - bx;
                const float outD = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f));
                const float inD  = std::min(std::max(qx, qy), 0.0f);
                const float sdf  = outD + inD - rAvg;
                const float cov  = fill
                    ? std::clamp(0.5f - sdf, 0.0f, 1.0f)
                    : std::clamp(0.5f * strokeW + 0.5f - std::abs(sdf), 0.0f, 1.0f);
                if (cov > 0.0f) {
                    BlendStraightOverPremul(row + x * 4, col.b, col.g, col.r, static_cast<uint32_t>(std::lround(col.a * cov)));
                }
            }
        }
    }

    void RasterizeEllipse(const D2D1_ELLIPSE& el, ID2D1Brush* brush, float strokeW, bool fill) {
        const PremulBgra col = BrushToColor(brush);
        if (col.a == 0) return;
        const D2D1_POINT_2F c = XformPt(el.point);
        const float rx = std::max(0.5f, std::abs(el.radiusX));
        const float ry = std::max(0.5f, std::abs(el.radiusY));
        const float pad = fill ? 1.0f : (0.5f * strokeW + 1.0f);
        const int ix0 = std::max(m_clipX0, static_cast<int>(std::floor(c.x - rx - pad)));
        const int iy0 = std::max(m_clipY0, static_cast<int>(std::floor(c.y - ry - pad)));
        const int ix1 = std::min(m_clipX1, static_cast<int>(std::ceil(c.x + rx + pad)));
        const int iy1 = std::min(m_clipY1, static_cast<int>(std::ceil(c.y + ry + pad)));

        const float rAvg = 0.5f * (rx + ry);
        for (int y = iy0; y < iy1; ++y) {
            const float ny = ((y + 0.5f) - c.y) / ry;
            uint8_t* row = m_fb.data() + static_cast<size_t>(y) * m_width * 4u;
            for (int x = ix0; x < ix1; ++x) {
                const float nx = ((x + 0.5f) - c.x) / rx;
                const float sdf = (std::hypot(nx, ny) - 1.0f) * rAvg;
                const float cov = fill
                    ? std::clamp(0.5f - sdf, 0.0f, 1.0f)
                    : std::clamp(0.5f * strokeW + 0.5f - std::abs(sdf), 0.0f, 1.0f);
                if (cov > 0.0f) {
                    BlendStraightOverPremul(row + x * 4, col.b, col.g, col.r, static_cast<uint32_t>(std::lround(col.a * cov)));
                }
            }
        }
    }

    std::atomic<ULONG> m_ref{ 1 };
    HWND m_hwnd = nullptr;
    IWICBitmap* m_wicTarget = nullptr;
    HDC m_boundDc = nullptr;
    RECT m_boundRect = {};
    UINT32 m_width = 1;
    UINT32 m_height = 1;
    float m_dpiX = 96.0f;
    float m_dpiY = 96.0f;
    D2D1_MATRIX_3X2_F m_transform;
    std::vector<D2D1_RECT_F> m_clipStack;
    int m_clipX0 = 0, m_clipY0 = 0, m_clipX1 = 1, m_clipY1 = 1;
    std::vector<uint8_t> m_fb;
};

class SwD2DFactory final : public ID2D1Factory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1Factory)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE CreatePathGeometry(ID2D1PathGeometry** out) override {
        if (!out) return E_POINTER;
        *out = new SwPathGeometry();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateHwndRenderTarget(
        const D2D1_RENDER_TARGET_PROPERTIES*,
        const D2D1_HWND_RENDER_TARGET_PROPERTIES* hp,
        ID2D1HwndRenderTarget** out) override
    {
        if (!out || !hp) return E_POINTER;
        const D2D1_SIZE_U sz = (hp->pixelSize.width > 0 && hp->pixelSize.height > 0)
            ? hp->pixelSize : D2D1::SizeU(1280, 800);
        *out = new SwRenderTarget(hp->hwnd, sz, nullptr);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateDCRenderTarget(
        const D2D1_RENDER_TARGET_PROPERTIES*, ID2D1DCRenderTarget** out) override
    {
        if (!out) return E_POINTER;
        *out = new SwRenderTarget(nullptr, D2D1::SizeU(256, 256), nullptr);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateWicBitmapRenderTarget(
        IWICBitmap* target, const D2D1_RENDER_TARGET_PROPERTIES*, ID2D1RenderTarget** out) override
    {
        if (!out || !target) return E_POINTER;
        UINT w = 0, h = 0;
        target->GetSize(&w, &h);
        *out = static_cast<ID2D1HwndRenderTarget*>(new SwRenderTarget(nullptr, D2D1::SizeU(w ? w : 64u, h ? h : 64u), target));
        return S_OK;
    }

private:
    std::atomic<ULONG> m_ref{ 1 };
};

} // namespace

// Strong definitions overriding weak stubs in Win32HalCore.cpp
HRESULT DWriteCreateFactory(DWRITE_FACTORY_TYPE, REFIID, IUnknown** factory) {
    if (!factory) return E_POINTER;
    *factory = new SwDWriteFactory();
    return S_OK;
}

HRESULT D2D1CreateFactory(D2D1_FACTORY_TYPE, REFIID, const void*, void** ppIFactory) {
    if (!ppIFactory) return E_POINTER;
    *ppIFactory = new SwD2DFactory();
    return S_OK;
}
