#pragma once
// Xpm.h — X11 Pixmap (.xpm, XPM2 & XPM3) Decoder with #RGB/#RRGGBB/#RRRRGGGGBBBB, None & Full X11 rgb.txt

namespace XpmDetail {

inline bool ParseX11ColorSpec(std::string_view spec, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    while (!spec.empty() && static_cast<unsigned char>(spec.front()) <= 0x20u) spec.remove_prefix(1);
    while (!spec.empty() && static_cast<unsigned char>(spec.back())  <= 0x20u) spec.remove_suffix(1);
    if (spec.empty()) return false;

    std::string norm;
    norm.reserve(spec.size());
    for (char c : spec) {
        if (c == ' ' || c == '\t' || c == '_') continue;
        if (c >= 'A' && c <= 'Z') norm.push_back(static_cast<char>(c - 'A' + 'a'));
        else norm.push_back(c);
    }
    if (norm == "none" || norm == "transparent" || norm == "mask") {
        r = g = b = a = 0u;
        return true;
    }

    a = 255u;
    auto hexDigit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    if (spec[0] == '#') {
        std::string_view hex = spec.substr(1);
        size_t n = 0;
        while (n < hex.size() && hexDigit(hex[n]) >= 0) ++n;
        auto rdHex = [&](size_t off, size_t digits) -> uint32_t {
            uint32_t v = 0;
            for (size_t i = 0; i < digits; ++i) v = (v << 4) | static_cast<uint32_t>(hexDigit(hex[off + i]));
            return v;
        };
        if (n == 3u) { // #RGB (4-bit per channel)
            r = static_cast<uint8_t>(rdHex(0, 1) * 17u);
            g = static_cast<uint8_t>(rdHex(1, 1) * 17u);
            b = static_cast<uint8_t>(rdHex(2, 1) * 17u);
            return true;
        }
        if (n == 6u) { // #RRGGBB (8-bit per channel)
            r = static_cast<uint8_t>(rdHex(0, 2));
            g = static_cast<uint8_t>(rdHex(2, 2));
            b = static_cast<uint8_t>(rdHex(4, 2));
            return true;
        }
        if (n == 9u) { // #RRRGGGBBB (12-bit per channel)
            r = static_cast<uint8_t>((rdHex(0, 3) * 255u + 2047u) / 4095u);
            g = static_cast<uint8_t>((rdHex(3, 3) * 255u + 2047u) / 4095u);
            b = static_cast<uint8_t>((rdHex(6, 3) * 255u + 2047u) / 4095u);
            return true;
        }
        if (n == 12u) { // #RRRRGGGGBBBB (16-bit X11 XParseColor)
            r = static_cast<uint8_t>((rdHex(0, 4) * 255u + 32767u) / 65535u);
            g = static_cast<uint8_t>((rdHex(4, 4) * 255u + 32767u) / 65535u);
            b = static_cast<uint8_t>((rdHex(8, 4) * 255u + 32767u) / 65535u);
            return true;
        }
        if (n == 8u) { // #AARRGGBB / #RRGGBBAA fallback -> high RRGGBB
            r = static_cast<uint8_t>(rdHex(0, 2));
            g = static_cast<uint8_t>(rdHex(2, 2));
            b = static_cast<uint8_t>(rdHex(4, 2));
            a = static_cast<uint8_t>(rdHex(6, 2));
            return true;
        }
        return false;
    }

    // X11 rgb:RRRR/GGGG/BBBB syntax
    if (norm.size() > 4u && norm.substr(0, 4) == "rgb:") {
        std::string_view rest = norm.substr(4);
        size_t s1 = rest.find('/'), s2 = (s1 != std::string_view::npos) ? rest.find('/', s1 + 1) : std::string_view::npos;
        if (s1 != std::string_view::npos && s2 != std::string_view::npos) {
            auto parseComp = [&](std::string_view cs) -> uint8_t {
                if (cs.empty() || cs.size() > 4u) return 0u;
                uint32_t v = 0, maxV = (1u << (cs.size() * 4u)) - 1u;
                for (char ch : cs) {
                    int d = hexDigit(ch);
                    if (d < 0) return 0u;
                    v = (v << 4) | static_cast<uint32_t>(d);
                }
                return static_cast<uint8_t>((v * 255u + maxV / 2u) / maxV);
            };
            r = parseComp(rest.substr(0, s1));
            g = parseComp(rest.substr(s1 + 1, s2 - s1 - 1));
            b = parseComp(rest.substr(s2 + 1));
            return true;
        }
    }

    // X11 gray0..gray100 / grey0..grey100
    if (norm.size() >= 5u && (norm.substr(0, 4) == "gray" || norm.substr(0, 4) == "grey") &&
        norm[4] >= '0' && norm[4] <= '9')
    {
        int pct = std::atoi(norm.c_str() + 4);
        if (pct >= 0 && pct <= 100) {
            r = g = b = static_cast<uint8_t>((pct * 255 + 50) / 100);
            return true;
        }
    }

    struct X11Named { const char* name; uint8_t r, g, b; };
    static constexpr X11Named kX11Colors[] = {
        { "aliceblue", 240, 248, 255 },        { "antiquewhite", 250, 235, 215 },
        { "aqua", 0, 255, 255 },               { "aquamarine", 127, 255, 212 },
        { "azure", 240, 255, 255 },            { "beige", 245, 245, 220 },
        { "bisque", 255, 228, 196 },           { "black", 0, 0, 0 },
        { "blanchedalmond", 255, 235, 205 },   { "blue", 0, 0, 255 },
        { "blue1", 0, 0, 255 },                { "blue2", 0, 0, 238 },
        { "blue3", 0, 0, 205 },                { "blue4", 0, 0, 139 },
        { "blueviolet", 138, 43, 226 },        { "brown", 165, 42, 42 },
        { "burlywood", 222, 184, 135 },        { "cadetblue", 95, 158, 160 },
        { "chartreuse", 127, 255, 0 },         { "chocolate", 210, 105, 30 },
        { "coral", 255, 127, 80 },             { "cornflowerblue", 100, 149, 237 },
        { "cornsilk", 255, 248, 220 },         { "crimson", 220, 20, 60 },
        { "cyan", 0, 255, 255 },               { "cyan1", 0, 255, 255 },
        { "cyan2", 0, 238, 238 },              { "cyan3", 0, 205, 205 },
        { "cyan4", 0, 139, 139 },              { "darkblue", 0, 0, 139 },
        { "darkcyan", 0, 139, 139 },           { "darkgoldenrod", 184, 134, 11 },
        { "darkgray", 169, 169, 169 },         { "darkgrey", 169, 169, 169 },
        { "darkgreen", 0, 100, 0 },            { "darkkhaki", 189, 183, 107 },
        { "darkmagenta", 139, 0, 139 },        { "darkolivegreen", 85, 107, 47 },
        { "darkorange", 255, 140, 0 },         { "darkorchid", 153, 50, 204 },
        { "darkred", 139, 0, 0 },              { "darksalmon", 233, 150, 122 },
        { "darkseagreen", 143, 188, 143 },     { "darkslateblue", 72, 61, 139 },
        { "darkslategray", 47, 79, 79 },       { "darkslategrey", 47, 79, 79 },
        { "darkturquoise", 0, 206, 209 },      { "darkviolet", 148, 0, 211 },
        { "deeppink", 255, 20, 147 },          { "deepskyblue", 0, 191, 255 },
        { "dimgray", 105, 105, 105 },          { "dimgrey", 105, 105, 105 },
        { "dodgerblue", 30, 144, 255 },        { "firebrick", 178, 34, 34 },
        { "floralwhite", 255, 250, 240 },      { "forestgreen", 34, 139, 34 },
        { "fuchsia", 255, 0, 255 },            { "gainsboro", 220, 220, 220 },
        { "ghostwhite", 248, 248, 255 },       { "gold", 255, 215, 0 },
        { "goldenrod", 218, 165, 32 },         { "gray", 190, 190, 190 },
        { "grey", 190, 190, 190 },             { "green", 0, 255, 0 },
        { "green1", 0, 255, 0 },               { "green2", 0, 238, 0 },
        { "green3", 0, 205, 0 },               { "green4", 0, 139, 0 },
        { "greenyellow", 173, 255, 47 },       { "honeydew", 240, 255, 240 },
        { "hotpink", 255, 105, 180 },          { "indianred", 205, 92, 92 },
        { "indigo", 75, 0, 130 },              { "ivory", 255, 255, 240 },
        { "khaki", 240, 230, 140 },            { "lavender", 230, 230, 250 },
        { "lavenderblush", 255, 240, 245 },    { "lawngreen", 124, 252, 0 },
        { "lemonchiffon", 255, 250, 205 },     { "lightblue", 173, 216, 230 },
        { "lightcoral", 240, 128, 128 },       { "lightcyan", 224, 255, 255 },
        { "lightgoldenrodyellow", 250, 250, 210 }, { "lightgray", 211, 211, 211 },
        { "lightgrey", 211, 211, 211 },        { "lightgreen", 144, 238, 144 },
        { "lightpink", 255, 182, 193 },        { "lightsalmon", 255, 160, 122 },
        { "lightseagreen", 32, 178, 170 },     { "lightskyblue", 135, 206, 250 },
        { "lightslategray", 119, 136, 153 },   { "lightslategrey", 119, 136, 153 },
        { "lightsteelblue", 176, 196, 222 },   { "lightyellow", 255, 255, 224 },
        { "lime", 0, 255, 0 },                 { "limegreen", 50, 205, 50 },
        { "linen", 250, 240, 230 },            { "magenta", 255, 0, 255 },
        { "magenta1", 255, 0, 255 },           { "magenta2", 238, 0, 238 },
        { "magenta3", 205, 0, 205 },           { "magenta4", 139, 0, 139 },
        { "maroon", 176, 48, 96 },             { "mediumaquamarine", 102, 205, 170 },
        { "mediumblue", 0, 0, 205 },           { "mediumorchid", 186, 85, 211 },
        { "mediumpurple", 147, 112, 219 },     { "mediumseagreen", 60, 179, 113 },
        { "mediumslateblue", 123, 104, 238 },  { "mediumspringgreen", 0, 250, 154 },
        { "mediumturquoise", 72, 209, 204 },   { "mediumvioletred", 199, 21, 133 },
        { "midnightblue", 25, 25, 112 },       { "mintcream", 245, 255, 250 },
        { "mistyrose", 255, 228, 225 },        { "moccasin", 255, 228, 181 },
        { "navajowhite", 255, 222, 173 },      { "navy", 0, 0, 128 },
        { "navyblue", 0, 0, 128 },             { "oldlace", 253, 245, 230 },
        { "olive", 128, 128, 0 },              { "olivedrab", 107, 142, 35 },
        { "orange", 255, 165, 0 },             { "orange1", 255, 165, 0 },
        { "orange2", 238, 154, 0 },            { "orange3", 205, 133, 0 },
        { "orange4", 139, 90, 0 },             { "orangered", 255, 69, 0 },
        { "orchid", 218, 112, 214 },           { "palegoldenrod", 238, 232, 170 },
        { "palegreen", 152, 251, 152 },        { "paleturquoise", 175, 238, 238 },
        { "palevioletred", 219, 112, 147 },    { "papayawhip", 255, 239, 213 },
        { "peachpuff", 255, 218, 185 },        { "peru", 205, 133, 63 },
        { "pink", 255, 192, 203 },             { "plum", 221, 160, 221 },
        { "powderblue", 176, 224, 230 },       { "purple", 160, 32, 240 },
        { "rebeccapurple", 102, 51, 153 },     { "red", 255, 0, 0 },
        { "red1", 255, 0, 0 },                 { "red2", 238, 0, 0 },
        { "red3", 205, 0, 0 },                 { "red4", 139, 0, 0 },
        { "rosybrown", 188, 143, 143 },        { "royalblue", 65, 105, 225 },
        { "saddlebrown", 139, 69, 19 },        { "salmon", 250, 128, 114 },
        { "sandybrown", 244, 164, 96 },        { "seagreen", 46, 139, 87 },
        { "seashell", 255, 245, 238 },         { "sienna", 160, 82, 45 },
        { "silver", 192, 192, 192 },           { "skyblue", 135, 206, 235 },
        { "slateblue", 106, 90, 205 },         { "slategray", 112, 128, 144 },
        { "slategrey", 112, 128, 144 },        { "snow", 255, 250, 250 },
        { "springgreen", 0, 255, 127 },        { "steelblue", 70, 130, 180 },
        { "tan", 210, 180, 140 },              { "teal", 0, 128, 128 },
        { "thistle", 216, 191, 216 },          { "tomato", 255, 99, 71 },
        { "turquoise", 64, 224, 208 },         { "violet", 238, 130, 238 },
        { "wheat", 245, 222, 179 },            { "white", 255, 255, 255 },
        { "whitesmoke", 245, 245, 245 },       { "yellow", 255, 255, 0 },
        { "yellow1", 255, 255, 0 },            { "yellow2", 238, 238, 0 },
        { "yellow3", 205, 205, 0 },            { "yellow4", 139, 139, 0 },
        { "yellowgreen", 154, 205, 50 }
    };
    for (const auto& entry : kX11Colors) {
        if (norm == entry.name) {
            r = entry.r; g = entry.g; b = entry.b;
            return true;
        }
    }
    return false;
}

inline std::string_view ExtractXpmModeColor(std::string_view rest, std::string_view modeKey) {
    auto isWs = [](char c) { return c == ' ' || c == '\t'; };
    auto isKeyToken = [](std::string_view tok) {
        return tok == "c" || tok == "s" || tok == "g" || tok == "g4" || tok == "m";
    };

    size_t p = 0;
    while (p < rest.size()) {
        while (p < rest.size() && isWs(rest[p])) ++p;
        if (p >= rest.size()) break;
        size_t te = p;
        while (te < rest.size() && !isWs(rest[te])) ++te;
        std::string_view tok = rest.substr(p, te - p);
        p = te;
        if (tok == modeKey) {
            while (p < rest.size() && isWs(rest[p])) ++p;
            size_t valStart = p;
            size_t valEnd = valStart;
            while (p < rest.size()) {
                while (p < rest.size() && isWs(rest[p])) ++p;
                if (p >= rest.size()) break;
                size_t we = p;
                while (we < rest.size() && !isWs(rest[we])) ++we;
                std::string_view word = rest.substr(p, we - p);
                if (valEnd > valStart && isKeyToken(word) && we < rest.size()) {
                    break;
                }
                valEnd = we;
                p = we;
            }
            return rest.substr(valStart, valEnd - valStart);
        }
    }
    return {};
}

} // namespace XpmDetail

bool DecodeXPM(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 16) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 8 * 1024 * 1024));
    if (sv.find("XPM") == std::string_view::npos) return false;

    std::vector<std::string_view> strings;
    const bool isXpm2 = (sv.substr(0, 6) == "! XPM2" || sv.find("\n! XPM2") != std::string_view::npos);

    if (isXpm2) {
        size_t p = sv.find("! XPM2");
        p = sv.find('\n', p);
        while (p != std::string_view::npos && p + 1u < sv.size()) {
            ++p;
            size_t eol = sv.find('\n', p);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view line = sv.substr(p, eol - p);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (!line.empty() && line.front() != '!') strings.push_back(line);
            p = eol;
        }
    } else {
        // Strip /* ... */ C comments before scanning quoted strings so comment quotes never corrupt indices
        size_t pos = 0;
        while (pos < sv.size()) {
            if (pos + 1u < sv.size() && sv[pos] == '/' && sv[pos + 1] == '*') {
                size_t endC = sv.find("*/", pos + 2u);
                if (endC == std::string_view::npos) break;
                pos = endC + 2u;
                continue;
            }
            if (pos + 1u < sv.size() && sv[pos] == '/' && sv[pos + 1] == '/') {
                size_t eol = sv.find('\n', pos + 2u);
                if (eol == std::string_view::npos) break;
                pos = eol + 1u;
                continue;
            }
            if (sv[pos] == '"') {
                size_t q2 = pos + 1u;
                while (q2 < sv.size()) {
                    if (sv[q2] == '\\' && q2 + 1u < sv.size()) { q2 += 2u; continue; }
                    if (sv[q2] == '"') break;
                    ++q2;
                }
                if (q2 >= sv.size()) break;
                strings.push_back(sv.substr(pos + 1u, q2 - pos - 1u));
                pos = q2 + 1u;
            } else {
                ++pos;
            }
        }
    }
    if (strings.size() < 3u) return false;

    unsigned int w = 0, h = 0, ncolors = 0, cpp = 0;
    if (sscanf_s(std::string(strings[0]).c_str(), "%u %u %u %u", &w, &h, &ncolors, &cpp) != 4) return false;
    if (w == 0 || h == 0 || w > 4096 || h > 4096 || ncolors == 0 || ncolors > 65536 || cpp == 0 || cpp > 8 ||
        static_cast<uint64_t>(ncolors) + h + 1ULL > strings.size())
    {
        return false;
    }

    std::unordered_map<std::string, uint32_t> palette;
    palette.reserve(ncolors);

    for (unsigned int i = 0; i < ncolors; ++i) {
        std::string_view line = strings[1u + i];
        if (line.size() < cpp) continue;
        std::string key(line.substr(0, cpp));
        std::string_view rest = line.substr(cpp);

        uint8_t r = 0, g = 0, b = 0, a = 255u;
        bool parsed = false;
        static constexpr std::string_view kModes[] = { "c", "g", "g4", "m" };
        for (std::string_view m : kModes) {
            std::string_view spec = XpmDetail::ExtractXpmModeColor(rest, m);
            if (!spec.empty() && XpmDetail::ParseX11ColorSpec(spec, r, g, b, a)) {
                parsed = true;
                break;
            }
        }
        (void)parsed;
        const uint32_t packed = (static_cast<uint32_t>(a) << 24) |
                                (static_cast<uint32_t>(r) << 16) |
                                (static_cast<uint32_t>(g) << 8)  |
                                static_cast<uint32_t>(b);
        palette[std::move(key)] = packed;
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"X11 Pixmap (.xpm)";
    outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4u, 0u);

    std::string keyBuf(cpp, '\0');
    for (unsigned int y = 0; y < h; ++y) {
        std::string_view row = strings[1u + ncolors + y];
        for (unsigned int x = 0; x < w; ++x) {
            if ((x + 1u) * cpp > row.size()) break;
            std::memcpy(keyBuf.data(), row.data() + static_cast<size_t>(x) * cpp, cpp);
            uint8_t r = 0, g = 0, b = 0, a = 255u;
            if (auto it = palette.find(keyBuf); it != palette.end()) {
                const uint32_t c32 = it->second;
                a = static_cast<uint8_t>((c32 >> 24) & 0xFFu);
                r = static_cast<uint8_t>((c32 >> 16) & 0xFFu);
                g = static_cast<uint8_t>((c32 >> 8)  & 0xFFu);
                b = static_cast<uint8_t>( c32        & 0xFFu);
            }
            WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(y) * w + x) * 4u], r, g, b, a);
        }
    }
    outInfo.valid = true;
    return true;
}
