#pragma once
#include <algorithm>

namespace Explorer2 {

// The widths the five details columns (name, date, type, size, row buttons) are drawn and hit-tested with.
// They are the stored widths while those fit the list. In a narrower list (a 1024 px screen, a split pane) the
// columns give up width, the name first, down to a minimum each; what still does not fit is left out (width 0:
// type, then date, then the row buttons, then size), so that no column ends up beyond the right edge, where it
// could not be reached. rowButtons = false (Ten komputer, a read-only repository) leaves the last column out.
inline void FitDetailsColumns(const float stored[5], bool rowButtons, float availW, float out[5]) {
    for (int c = 0; c < 5; ++c) out[c] = std::max(0.0f, stored[c]);
    if (!rowButtons) out[4] = 0.0f;
    auto total = [&]() { return out[0] + out[1] + out[2] + out[3] + out[4]; };
    if (total() <= availW) return;

    // 1. Narrower columns: the name first (it is the widest and its text can be cut), then type, date, size, buttons
    static const float kMinW[5] = { 150.0f, 96.0f, 70.0f, 76.0f, 128.0f };
    static const int kShrinkOrder[5] = { 0, 2, 1, 3, 4 };
    for (int c : kShrinkOrder) {
        const float excess = total() - availW;
        if (excess <= 0.0f) return;
        const float floorW = std::min(out[c], kMinW[c]);
        out[c] = std::max(floorW, out[c] - excess);
    }

    // 2. Still too wide (a split pane on a small screen): whole columns go, the least needed first. The name stays.
    static const int kHideOrder[4] = { 2, 1, 4, 3 };
    for (int c : kHideOrder) {
        if (total() <= availW) break;
        out[c] = 0.0f;
    }
    const float spare = availW - total();
    if (spare > 0.0f) {
        // Room left over by a hidden column goes back to the name, up to its stored width
        out[0] = std::min(std::max(out[0], stored[0]), out[0] + spare);
    } else if (spare < 0.0f) {
        out[0] = std::max(60.0f, out[0] + spare);
    }
}

} // namespace Explorer2
