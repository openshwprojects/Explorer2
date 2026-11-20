#pragma once
// Widths of the tabs in the ribbon's tab strip.
//
// Each tab has a designed width with comfortable padding around its caption. A window too narrow for all of them (a
// small window, a large UI scale) used to squeeze every tab by the same factor, and then the long captions ran
// into their neighbours. Here the padding goes first, and when even that is not enough the longest captions are
// cut to one common width (they are drawn with an ellipsis), so short ones such as "LAN" stay whole.
#include <algorithm>
#include <cstddef>
#include <vector>

namespace Explorer2 {

constexpr float kRibbonTabPadRoomy = 14.0f;     // least padding (both sides together) while the captions have room
constexpr float kRibbonTabPadTight = 8.0f;      // the same once the strip is squeezed
constexpr float kRibbonTabMinWidth = 24.0f;     // a cut caption never gets less than this

// natural[i]: the designed width of tab i; textW[i]: the width of its caption; avail: the room for all tabs.
// Returns the width of every tab. A tab narrower than textW[i] + kRibbonTabPadTight has to cut its caption.
inline std::vector<float> FitRibbonTabWidths(const std::vector<float>& natural, const std::vector<float>& textW, float avail) {
    const size_t n = std::min(natural.size(), textW.size());
    std::vector<float> w(n, 0.0f);
    if (n == 0) return w;

    // 1. Room for everything: the designed widths (a caption longer than its design gets what it needs)
    float sum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        w[i] = std::max(natural[i], textW[i] + kRibbonTabPadRoomy);
        sum += w[i];
    }
    if (sum <= avail) return w;

    // 2. Less padding, every caption still whole: what is left over is shared out in proportion to the padding
    //    each tab had
    std::vector<float> need(n);
    float needSum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        need[i] = textW[i] + kRibbonTabPadTight;
        needSum += need[i];
    }
    if (needSum <= avail) {
        float flex = 0.0f;
        for (size_t i = 0; i < n; ++i) flex += w[i] - need[i];
        const float spare = avail - needSum;
        for (size_t i = 0; i < n; ++i) {
            w[i] = need[i] + ((flex > 0.0f) ? spare * (w[i] - need[i]) / flex : 0.0f);
        }
        return w;
    }

    // 3. Not even that fits: the widest tabs are cut to one common width, the others keep theirs
    std::vector<float> sorted(need);
    std::sort(sorted.begin(), sorted.end());
    float cap = sorted.back();
    float remaining = std::max(0.0f, avail);
    for (size_t k = 0; k < n; ++k) {
        const float share = remaining / static_cast<float>(n - k);
        if (sorted[k] > share) {
            cap = share;
            break;
        }
        remaining -= sorted[k];
    }
    cap = std::max(cap, kRibbonTabMinWidth);
    for (size_t i = 0; i < n; ++i) w[i] = std::min(need[i], cap);
    return w;
}

// ---- Which tabs the strip shows --------------------------------------------------------------------------------
// The user can switch tabs off ("Ustawienia" tab, "Karty wstążki"): hiddenMask has bit t set for the tab whose
// RibbonTab value is t, alwaysShown names the ones that cannot be switched off. A hidden tab is still drawn while
// it is the one in use - a shortcut or a command can open it, and the ribbon body under the strip then needs its
// caption - and while it is busy (the transfers tab during a copy shows the progress).
inline bool RibbonTabShown(unsigned hiddenMask, unsigned alwaysShown, int tab, int activeTab, bool busy = false) {
    if (tab < 0 || tab >= 32) return true;
    const unsigned bit = 1u << tab;
    if (alwaysShown & bit) return true;
    if (tab == activeTab || busy) return true;
    return (hiddenMask & bit) == 0;
}

} // namespace Explorer2
