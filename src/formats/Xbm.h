#pragma once
// ============================================================================
// 8. X11 Bitmap (.xbm) & X11 Pixmap (.xpm)
// ============================================================================
bool DecodeXBM(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 8 * 1024 * 1024));
    uint32_t w = 0, h = 0;
    size_t pos = 0;
    while (pos < sv.size()) {
        size_t defPos = sv.find("#define", pos);
        if (defPos == std::string_view::npos) break;
        size_t eol = sv.find('\n', defPos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view line = sv.substr(defPos + 7, eol - (defPos + 7));
        if (line.find("_width") != std::string_view::npos) {
            size_t p = line.find("_width") + 6;
            w = static_cast<uint32_t>(std::strtoul(std::string(line.substr(p)).c_str(), nullptr, 10));
        } else if (line.find("_height") != std::string_view::npos) {
            size_t p = line.find("_height") + 7;
            h = static_cast<uint32_t>(std::strtoul(std::string(line.substr(p)).c_str(), nullptr, 10));
        }
        pos = eol + 1;
        if (w > 0 && h > 0) break;
    }
    if (w == 0 || h == 0 || w > 4096 || h > 4096) return false;
    size_t brace = sv.find('{', pos);
    if (brace == std::string_view::npos) return false;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 1;
    outInfo.bitsPerChannel = 1;
    outInfo.formatName = L"X11 Bitmap (.xbm)";
    outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4, 255);

    uint32_t rowBytes = (w + 7) / 8;
    std::vector<uint8_t> bitBytes;
    bitBytes.reserve(static_cast<size_t>(rowBytes) * h);

    const char* cur = sv.data() + brace + 1;
    const char* end = sv.data() + sv.size();
    while (cur < end && bitBytes.size() < static_cast<size_t>(rowBytes) * h) {
        while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n' || *cur == ',')) ++cur;
        if (cur >= end || *cur == '}') break;
        char* next = nullptr;
        unsigned long val = std::strtoul(cur, &next, 0);
        if (next == cur) { ++cur; continue; }
        bitBytes.push_back(static_cast<uint8_t>(val & 0xFF));
        cur = next;
    }
    if (bitBytes.empty()) return false;

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t byteIdx = static_cast<size_t>(y) * rowBytes + (x / 8);
            bool bitOn = (byteIdx < bitBytes.size()) && ((bitBytes[byteIdx] >> (x & 7)) & 1);
            uint8_t c = bitOn ? 20 : 248;
            WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(y) * w + x) * 4], c, c, c, 255);
        }
    }
    outInfo.valid = true;
    return true;
}
