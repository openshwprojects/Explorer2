#pragma once

bool DecodeXPM(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 8 * 1024 * 1024));
    if (sv.find("XPM") == std::string_view::npos) return false;

    std::vector<std::string_view> strings;
    size_t pos = 0;
    while (pos < sv.size()) {
        size_t q1 = sv.find('"', pos);
        if (q1 == std::string_view::npos) break;
        size_t q2 = sv.find('"', q1 + 1);
        if (q2 == std::string_view::npos) break;
        strings.push_back(sv.substr(q1 + 1, q2 - q1 - 1));
        pos = q2 + 1;
    }
    if (strings.size() < 3) return false;

    unsigned int w = 0, h = 0, ncolors = 0, cpp = 0;
    if (sscanf_s(std::string(strings[0]).c_str(), "%u %u %u %u", &w, &h, &ncolors, &cpp) != 4) return false;
    // The colour count is checked in 64 bits: a value near 4 billion would wrap the sum, pass, and then
    // ask for a palette of that many entries.
    if (w == 0 || h == 0 || w > 4096 || h > 4096 || cpp == 0 || cpp > 4 ||
        static_cast<uint64_t>(ncolors) + h + 1ULL > strings.size()) return false;

    struct XpmColor { std::string key; uint8_t r = 0, g = 0, b = 0, a = 255; };
    std::vector<XpmColor> palette;
    palette.reserve(ncolors);

    for (unsigned int i = 0; i < ncolors; ++i) {
        std::string_view line = strings[1 + i];
        if (line.size() < cpp) continue;
        XpmColor xc;
        xc.key = std::string(line.substr(0, cpp));
        size_t cPos = line.find("c ", cpp);
        if (cPos == std::string_view::npos) cPos = line.find("c\t", cpp);
        if (cPos != std::string_view::npos) {
            std::string_view colSpec = line.substr(cPos + 2);
            while (!colSpec.empty() && (colSpec.front() == ' ' || colSpec.front() == '\t')) colSpec.remove_prefix(1);
            if (colSpec.substr(0, 4) == "None" || colSpec.substr(0, 4) == "none") {
                xc.a = 0;
            } else if (!colSpec.empty() && colSpec[0] == '#' && colSpec.size() >= 7) {
                unsigned int hex = static_cast<unsigned int>(std::strtoul(std::string(colSpec.substr(1, 6)).c_str(), nullptr, 16));
                xc.r = static_cast<uint8_t>((hex >> 16) & 0xFF);
                xc.g = static_cast<uint8_t>((hex >> 8) & 0xFF);
                xc.b = static_cast<uint8_t>(hex & 0xFF);
            } else if (colSpec.substr(0, 5) == "white") {
                xc.r = xc.g = xc.b = 255;
            } else if (colSpec.substr(0, 3) == "red") {
                xc.r = 220; xc.g = 38; xc.b = 38;
            } else if (colSpec.substr(0, 4) == "blue") {
                xc.r = 37; xc.g = 99; xc.b = 235;
            } else if (colSpec.substr(0, 5) == "green") {
                xc.r = 22; xc.g = 163; xc.b = 74;
            }
        }
        palette.push_back(std::move(xc));
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"X11 Pixmap (.xpm)";
    outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4, 0);

    for (unsigned int y = 0; y < h; ++y) {
        std::string_view row = strings[1 + ncolors + y];
        for (unsigned int x = 0; x < w; ++x) {
            if ((x + 1) * cpp > row.size()) break;
            std::string_view k = row.substr(x * cpp, cpp);
            uint8_t r = 0, g = 0, b = 0, a = 255;
            for (const auto& pc : palette) {
                if (pc.key == k) { r = pc.r; g = pc.g; b = pc.b; a = pc.a; break; }
            }
            WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(y) * w + x) * 4], r, g, b, a);
        }
    }
    outInfo.valid = true;
    return true;
}

