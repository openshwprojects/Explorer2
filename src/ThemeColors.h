#pragma once
// The dark theme of the main window and of the preview pane's viewers.
//
// The drawing code names the colours of the light design (0xFFFFFF for a panel, 0x1E1E1E for text, ...). In the dark
// theme each of them is replaced by its dark counterpart from the tables here, so one entry serves every place that
// uses the colour. A colour that is not in a table is drawn as it is: the accents (0x0078D7), the status colours,
// and everything that is dark by design in both themes (the bars of the image and video viewer). A light mark on
// such a bar must therefore not be one of the listed colours: 0xFEFEFE is the white that stays white.
#include <cstdint>

namespace Explorer2 {

inline bool g_darkTheme = false;        // set by the main window when it starts to paint

// The main window
inline uint32_t DarkThemeRgb(uint32_t rgb) {
    switch (rgb) {
    case 0xFFFFFF: return 0x181B20;
    case 0xF8FAFC:
    case 0xF5F6F7:
    case 0xF3F3F3:
    case 0xF1F5F9:
    case 0xF0F3F6:
    case 0xEEF2F6: return 0x1E222A;
    case 0xF3F5F8: return 0x101216;       // the page behind the cards of Programs, Monitoring, LAN, Log...
    case 0xFAFBFC: return 0x1C2027;       // every second row of their lists
    case 0xF8FBFF: return 0x18263A;       // address bar of the active pane in split view
    case 0xE8ECF0: return 0x2A313C;       // scroll arrows at the ends of a ribbon wider than the window
    case 0xC7CFD8: return 0x3A4352;       // ... and their edge
    case 0xD8E0EA: return 0x2B333F;       // outline of the cards
    case 0xB8C4D0: return 0x4A5464;       // outline of the small ribbon buttons
    case 0x444444: return 0xA3ADBB;       // arrow of an open branch in the tree
    case 0x004578: return 0x9CCBFF;       // text of the search results bar
    case 0x8D3B00: return 0xFDBA74;       // ... while the search is running
    case 0xEFF6FF:
    case 0xEBF5FF:
    case 0xE8EEF4:
    case 0xE8EEF5:
    case 0xE2E8F0: return 0x262C36;
    case 0xE5F3FB:
    case 0xE5F3FF:
    case 0xE8F1FB:
    case 0xE1ECF4:
    case 0xDBEAFE:
    case 0xDCEBFC:
    case 0xE0F2FE: return 0x243B55;
    case 0xCBE8F6:
    case 0xCCE8FF: return 0x1E4976;
    case 0xDADADA:
    case 0xDFDFDF:
    case 0xD9D9D9:
    case 0xE5E5E5:
    case 0xCBD5E1:
    case 0xD1D5DB:
    case 0xCCCCCC: return 0x333B48;
    case 0x0F172A:
    case 0x111111:
    case 0x1E1E1E:
    case 0x1E293B:
    case 0x2B2B2B:
    case 0x333333: return 0xE2E8F0;
    case 0x334155:
    case 0x454545:
    case 0x475569:
    case 0x4C566A: return 0xCBD5E1;
    case 0x555555:      // date, type and size of the details view
    case 0x64748B:
    case 0x666666:
    case 0x6E6E6E:
    case 0x767676: return 0x94A3B8;
    }
    return rgb;
}

// The preview pane and its viewers (text, hex, archive, spreadsheet, PDF frame): the same table, and before it the
// colours that only they use on surfaces which turn dark - blue and green text on tinted boxes, the syntax colours
// of the text viewer, the mark of a search hit
inline uint32_t DarkThemePaneRgb(uint32_t rgb) {
    switch (rgb) {
    case 0x1E3A8A: return 0xBFDBFE;     // text of the blue buttons and notes
    case 0x1E40AF:
    case 0x1D4ED8: return 0x93C5FD;
    case 0x0369A1: return 0x7DD3FC;     // also: type names in code
    case 0x005A9E: return 0x9CCBFF;
    case 0x0033B3: return 0x7AA2F7;     // keywords
    case 0x7C3AED: return 0xB794F6;     // preprocessor lines
    case 0x15803D: return 0x4ADE80;     // strings
    case 0x166534: return 0x86EFAC;     // text of the green buttons ...
    case 0xF0FDF4: return 0x12261B;     // ... and their fill
    case 0xDCFCE7: return 0x163322;
    case 0xFEE2E2: return 0x3A1D20;     // hover of the close button
    case 0xFFF1F2: return 0x2E1A1D;     // a button that deletes
    case 0x2563EB: return 0x60A5FA;     // every second group of bytes in the hex view
    case 0xFEF08A: return 0x5B4708;     // a search hit in the text
    case 0xFCFDFF: return 0x1A1D23;     // every second row of a table
    }
    return DarkThemeRgb(rgb);
}

inline uint32_t ThemedPaneRgb(uint32_t rgb) { return g_darkTheme ? DarkThemePaneRgb(rgb) : rgb; }

} // namespace Explorer2
