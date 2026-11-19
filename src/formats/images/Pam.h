#pragma once
// Pam.h — Netpbm Portable Arbitrary Map (.pam, P7) Decoder

namespace Formats {

inline bool DecodePAM(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 16 || data[0] != 'P' || data[1] != '7') return false;
    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 4096));
    size_t endHdr = sv.find("ENDHDR");
    if (endHdr == std::string_view::npos) return false;
    size_t pixelStart = endHdr + 6;
    if (pixelStart < size && data[pixelStart] == '\r') ++pixelStart;
    if (pixelStart < size && data[pixelStart] == '\n') ++pixelStart;

    uint32_t w = 0, h = 0, depth = 0, maxval = 255;
    size_t pos = 2;
    while (pos < endHdr) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos || eol > endHdr) eol = endHdr;
        std::string_view ln = sv.substr(pos, eol - pos);
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t' || ln.front() == '\r')) ln.remove_prefix(1);
        if (ln.rfind("WIDTH ", 0) == 0) w = static_cast<uint32_t>(std::strtoul(std::string(ln.substr(6)).c_str(), nullptr, 10));
        else if (ln.rfind("HEIGHT ", 0) == 0) h = static_cast<uint32_t>(std::strtoul(std::string(ln.substr(7)).c_str(), nullptr, 10));
        else if (ln.rfind("DEPTH ", 0) == 0) depth = static_cast<uint32_t>(std::strtoul(std::string(ln.substr(6)).c_str(), nullptr, 10));
        else if (ln.rfind("MAXVAL ", 0) == 0) maxval = static_cast<uint32_t>(std::strtoul(std::string(ln.substr(7)).c_str(), nullptr, 10));
        pos = eol + 1;
    }
    if (w == 0 || h == 0 || w > 8192 || h > 8192 || depth == 0 || depth > 4 || maxval == 0) return false;
    uint32_t bps = (maxval > 255) ? 2 : 1;
    size_t needed = static_cast<size_t>(w) * h * depth * bps;
    if (pixelStart + needed > size) return false;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = depth;
    outInfo.bitsPerChannel = bps * 8;
    outInfo.formatName = L"Netpbm PAM (P7)";
    outInfo.bgraPixels.resize(static_cast<size_t>(w) * h * 4);

    const uint8_t* p = data + pixelStart;
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        uint8_t c[4] = { 0, 0, 0, 255 };
        for (uint32_t d = 0; d < depth; ++d) {
            uint32_t v = (bps == 2) ? ((static_cast<uint32_t>(p[0]) << 8) | p[1]) : p[0];
            p += bps;
            c[d] = static_cast<uint8_t>(std::min<uint32_t>(255, (v * 255) / maxval));
        }
        uint8_t r = c[0], g = (depth >= 3) ? c[1] : c[0], b = (depth >= 3) ? c[2] : c[0];
        uint8_t a = (depth == 2) ? c[1] : ((depth == 4) ? c[3] : 255);
        uint8_t* dst = &outInfo.bgraPixels[i * 4];
        dst[0] = static_cast<uint8_t>((b * a) / 255);
        dst[1] = static_cast<uint8_t>((g * a) / 255);
        dst[2] = static_cast<uint8_t>((r * a) / 255);
        dst[3] = a;
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
